#include "cyw43_hw.h"
#include "sdio_ring.h"
#include "../tx_diag.h"
#include "../phy_rates.h"
#include "../unicast.h"
#ifdef CLEAN_SDIO_SELF_TEST
#include "../sdio_test.h"
#endif

#define MBOX  ((volatile fw_mbox_t *)FW_MBOX_ADDR)
#define MULTI ((volatile fw_multi_t *)FW_MULTI_ADDR)
#define UNICAST ((volatile clean_unicast_status_t *)CLEAN_UNICAST_ADDR)

_Static_assert(__builtin_offsetof(fw_mbox_t, rx_input) + 32u == CLEAN_TX_DIAG_OFFSET,
               "TX diagnostic trace offset");
_Static_assert(sizeof(clean_tx_diag_t) <= sizeof(((fw_mbox_t *)0)->rx_input),
               "TX diagnostic fits mailbox");
_Static_assert(__builtin_offsetof(fw_mbox_t, f2_counts) == 444u, "F2 counter offset");
_Static_assert(__builtin_offsetof(fw_mbox_t, f2_consumed) == CLEAN_SDIO_ACK_OFFSET,
               "F2 host-consumption offset");
_Static_assert(__builtin_offsetof(fw_mbox_t, f2_posted) == CLEAN_SDIO_POSTED_OFFSET,
               "F2 publication offset");

static uint32_t dma_consumer;
static uint32_t dma_producer;
static uint32_t sdio_tx_idx;
static uint32_t sdio_tx_posted;

#ifdef CLEAN_SDIO_SELF_TEST
#define SDIO_TX_WINDOW CLEAN_SDIO_SELF_TEST_WINDOW
#else
#define SDIO_TX_WINDOW CLEAN_SDIO_WINDOW
#endif
static uint8_t  sdpcm_seq;
static uint8_t rx_after_explicit_tx;
static uint8_t station_enabled;
static clean_tx_diag_t tx_diag;

static void publish_tx_diag(void) {
    const uint8_t *bytes = (const uint8_t *)&tx_diag;
    for (uint32_t i = 0u; i < sizeof(tx_diag); ++i) MBOX->rx_input[i] = bytes[i];
}

static uint32_t tx_status(void) {
    return ((uint32_t)mmio_read16(0x1800150eu) << 16u) | mmio_read16(0x18001690u);
}

static int fail(uint32_t err, uint32_t addr, uint32_t val) {
    MBOX->fw_error = err;
    MBOX->fw_error_addr = addr;
    MBOX->fw_error_val = val;
    MBOX->fw_state = FW_STATE_ERROR;
    __asm__ volatile("dsb" ::: "memory");
    return 0;
}

/* SHM 16-bit word access via D11 object selector (0x18001160) and data port (0x18001164/0x18001166) */
static uint16_t shm_read16(uint32_t word_index) {
    mmio_write32(D11_OBJ_ADDR, 0x00010000u | (word_index >> 1u));
    (void)mmio_read32(D11_OBJ_ADDR);
    return mmio_read16(D11_OBJ_DATA + 2u * (word_index & 1u));
}

static void shm_write16(uint32_t word_index, uint16_t val) {
    mmio_write32(D11_OBJ_ADDR, 0x00010000u | (word_index >> 1u));
    (void)mmio_read32(D11_OBJ_ADDR);
    mmio_write16(D11_OBJ_DATA + 2u * (word_index & 1u), val);
}

static void sample_rxe8(volatile uint16_t out[8]) {
    static const uint32_t addrs[8] = {
        0x18001408u, 0x180014f0u, 0x18001412u, 0x18001418u,
        0x180014a0u, 0x180014b0u, 0x18001450u, 0x18001452u
    };
    for (uint32_t i = 0u; i < 8u; ++i)
        out[i] = mmio_read16(addrs[i]);
}

/* Configure D11 RXE, PSM external condition branch masks, RF switch / BTCX grant, IFS/TSF/NAV, and Radio LPF */
static void d11_configure_mac_and_rxe(uint32_t flags, uint16_t rc_lpf) {
    if (flags & CFG_RADIO_LPF) {
        radio_reg_mod(0xa3u, 0x0010u, 0x0010u);
        radio_reg_mod(0xa3u, 0x0008u, 0x0008u);
        radio_reg_mod(0xa3u, 0x0004u, 0x0004u);
        radio_reg_mod(0xa3u, 0x0001u, 0x0001u);
        radio_reg_mod(0xa3u, 0x0002u, 0x0002u);
        radio_reg_mod(0xa3u, 0x2000u, 0x2000u);
        uint16_t gmult = rc_lpf ? rc_lpf : 4880u;
        radio_reg_mod(0x72u, 0x1fffu, (uint16_t)(((uint32_t)2396u * gmult) >> 15u));
        radio_reg_mod(0x76u, 0x1fffu, (uint16_t)(((uint32_t)3058u * gmult) >> 15u));
        radio_reg_mod(0x73u, 0x1fffu, (uint16_t)(((uint32_t)3594u * gmult) >> 15u));
        radio_reg_mod(0x74u, 0x1fffu, (uint16_t)(((uint32_t)1797u * gmult) >> 15u));
        radio_reg_mod(0x75u, 0x1fffu, (uint16_t)(((uint32_t)2909u * gmult) >> 15u));
        radio_reg_mod(0x77u, 0x00ffu, 0x00aau);
        radio_reg_mod(0x71u, 0x0008u, 0x0000u);
        radio_reg_mod(0x72u, 0x2000u, 0x0000u);
    }
    if (flags & CFG_D11_RXE) {
        mmio_write16(0x18001406u, 0x0001u);
        mmio_write16(0x18001406u, 0x0000u);
        mmio_write16(0x1800140cu, 0x0018u);
        mmio_write16(0x18001406u, 0x0000u);
        mmio_write16(0x18001448u, 0x0c02u);
        mmio_write16(0x18001402u, CLEAN_SHM_FRAME);
        mmio_write16(0x18001450u, 0x4e01u);
        mmio_write16(0x18001452u, 0x015bu);
        mmio_write16(0x180014e4u, 0x0090u);
        mmio_write16(0x18001404u, CLEAN_RAW_BYTES);
        mmio_write16(0x180014feu, 0x0000u);
    }
    if (flags & CFG_D11_BRANCH_MASKS) {
        mmio_write16(0x18001490u, 0x0000u);
        mmio_write16(0x180014a0u, 0xf3f1u);
        mmio_write16(0x180014b0u, 0xfdefu);
        mmio_write16(0x180014a8u, 0xffffu);
        mmio_write16(0x180014a8u, 0x0000u);
        mmio_write16(0x180014a2u, 0x0001u);
        mmio_write16(0x180014b2u, 0xfffbu);
        mmio_write16(0x180014aau, 0xffffu);
        mmio_write16(0x180014aau, 0x0000u);
        mmio_write16(0x180014a4u, 0x1acfu);
        mmio_write16(0x180014acu, 0x0000u);
        mmio_write16(0x180014bcu, 0x0000u);
        mmio_write16(0x180014a6u, 0x02d7u);
        mmio_write16(0x180014b6u, 0xfdffu);
        mmio_write16(0x180014aeu, 0xffffu);
    }
    if (flags & CFG_D11_BTCX_RFSW) {
        mmio_write16(0x1800149eu, (uint16_t)(mmio_read16(0x1800149eu) | 0x00ffu));
        mmio_write16(0x1800149cu, (uint16_t)((mmio_read16(0x1800149cu) & ~0x00ffu) | 0x0004u));
        mmio_write16(0x180016b4u, (uint16_t)(mmio_read16(0x180016b4u) | 0x0003u));
        mmio_write16(0x180016b8u, (uint16_t)(mmio_read16(0x180016b8u) | 0x00c0u));
        mmio_write16(0x180016bau, 0x0010u);
        mmio_write16(0x180016bcu, 0x0064u);
        mmio_write16(0x180016f0u, 0x00c2u);
        mmio_write16(0x180016f2u, (uint16_t)(mmio_read16(0x180016f2u) | 0x00c0u));
    }
    if (flags & CFG_D11_IFS_TSF_CLK) {
        mmio_write32(D11_INT_CONTROL, 0x01000000u);
        mmio_write32(D11_MACCOMMAND, 0x00000004u);
        mmio_write32(D11_MACINTSTATUS, 0x00000000u);
        mmio_write32(D11_MACINTMASK, 0x00000000u);
        mmio_write16(D11_TSF_CFP_MAXDUR32, 0x01ddu);
        mmio_write16(D11_TSF_CFP_MAXDUR32 + 2u, 0x01ddu);
        mmio_write32(D11_CLK_CTL_ST, mmio_read32(D11_CLK_CTL_ST) | 0x00000002u);
        mmio_write16(0x180014d8u, (uint16_t)(mmio_read16(0x180014d8u) & ~0x0019u));
        mmio_write16(0x18001600u, 0x8004u);
        mmio_write16(0x18001610u, 0x010au);
        mmio_write16(0x18001612u, 0x0002u);
        mmio_write16(0x1800162eu, 0xcccdu);
        mmio_write16(0x18001630u, 0x000cu);
        mmio_write16(0x18001646u, 0xc000u);
        mmio_write16(0x1800164au, 0x0060u);
        mmio_write16(0x1800164eu, 0x0005u);
        mmio_write16(0x18001652u, 0x0060u);
        mmio_write16(0x18001656u, 0x0005u);
        mmio_write16(0x18001686u, 0x09d0u);
        mmio_write16(0x18001680u, 0x3e3eu);
        mmio_write16(0x18001682u, 0x023eu);
        mmio_write16(0x18001684u, 0x0212u);
        mmio_write16(0x18001696u, 0x0008u);
        mmio_write16(0x1800169au, 0x00e4u);
        for (uint32_t k = 0u; k < 4u; ++k) {
            mmio_write16(0x18001688u, (uint16_t)(k << 12u));
            mmio_write16(0x1800169cu, 0x0002u);
        }
        mmio_write16(0x18001688u, 0x0052u);
        mmio_write16(0x18001696u, 0x0206u);
        mmio_write16(0x1800169au, 0x0207u);
        mmio_write16(0x1800169cu, 0x0317u);
        mmio_write16(0x1800169eu, 0x0000u);
        mmio_write16(0x180016a8u, 0x0c3cu);
        mmio_write16(0x18001700u, 0x003cu);
        mmio_write16(0x180017c0u, 0x0083u);
        mmio_write16(0x180017d0u, 0x0003u);
    }
}

/* D11 RX DMA ring management (0x18001220..0x18001234) */
static void rx_dma_init(void) {
    mmio_write32(D11_RX_DMA_CTL, 0u);
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read32(D11_RX_DMA_STATUS0) >> 28u) == 0u) break;
    }
    volatile uint32_t *ring = (volatile uint32_t *)FW_D11_DMA_RING_ADDR;
    volatile uint32_t *bufs = (volatile uint32_t *)FW_D11_DMA_BUF_ADDR;
    for (uint32_t i = 0u; i < (32u * FW_D11_DMA_BUF_BYTES) / 4u; ++i)
        bufs[i] = 0u;

    for (uint32_t slot = 0u; slot < 32u; ++slot) {
        uint32_t buf_addr = FW_D11_DMA_BUF_ADDR + slot * FW_D11_DMA_BUF_BYTES;
        uint32_t ctrl1 = (slot == 31u) ? (1u << 28u) : 0u;
        uint32_t ctrl2 = FW_D11_DMA_BUF_BYTES & 0x7fffu;
        ring[4u * slot + 0u] = ctrl1;
        ring[4u * slot + 1u] = ctrl2;
        ring[4u * slot + 2u] = buf_addr;
        ring[4u * slot + 3u] = 0u;
    }
    __asm__ volatile("dsb" ::: "memory");

    dma_consumer = 0u;
    dma_producer = 31u;
    mmio_write32(D11_RX_DMA_ADDRLOW, FW_D11_DMA_RING_ADDR);
    mmio_write32(D11_RX_DMA_ADDRHIGH, 0u);
    mmio_write32(D11_RX_DMA_PTR, FW_D11_DMA_RING_ADDR);
    mmio_write32(D11_RX_DMA_CTL, FW_D11_DMA_CONTROL);
    mmio_write32(D11_RX_DMA_PTR, FW_D11_DMA_RING_ADDR + dma_producer * 16u);
}

static void rx_dma_stop(void) {
    mmio_write32(D11_RX_DMA_CTL, 0u);
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read32(D11_RX_DMA_STATUS0) >> 28u) == 0u) break;
    }
}

static int rx_dma_poll_and_reclaim(volatile fw_frame_slot_t *slot) {
    uint32_t st0 = mmio_read32(D11_RX_DMA_STATUS0);
    uint32_t st1 = mmio_read32(D11_RX_DMA_STATUS1);
    MULTI->dma_status0 = st0;
    MULTI->dma_status1 = st1;
    uint32_t curr = ((st0 & 0x1fffu) - (FW_D11_DMA_RING_ADDR & 0x1fffu)) >> 4u;
    if (curr >= 32u || curr == dma_consumer)
        return 0;

    uint32_t idx = dma_consumer;
    const volatile uint8_t *buf = (const volatile uint8_t *)(uintptr_t)(FW_D11_DMA_BUF_ADDR + idx * FW_D11_DMA_BUF_BYTES);
    __asm__ volatile("dsb" ::: "memory");

    uint16_t dma_len = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8u));
    slot->dma_rx_len = dma_len;
    slot->dma_completed = 1u;
    for (uint32_t i = 0u; i < 38u; ++i)
        slot->dma_hdr[i] = buf[i];

    volatile uint32_t *buf_w = (volatile uint32_t *)(uintptr_t)(FW_D11_DMA_BUF_ADDR + idx * FW_D11_DMA_BUF_BYTES);
    for (uint32_t i = 0u; i < FW_D11_DMA_BUF_BYTES / 4u; ++i)
        buf_w[i] = 0u;

    uint32_t post_slot = dma_producer;
    uint32_t post_buf = FW_D11_DMA_BUF_ADDR + post_slot * FW_D11_DMA_BUF_BYTES;
    volatile uint32_t *ring = (volatile uint32_t *)FW_D11_DMA_RING_ADDR;
    ring[4u * post_slot + 0u] = (post_slot == 31u) ? (1u << 28u) : 0u;
    ring[4u * post_slot + 1u] = FW_D11_DMA_BUF_BYTES & 0x7fffu;
    ring[4u * post_slot + 2u] = post_buf;
    ring[4u * post_slot + 3u] = 0u;
    __asm__ volatile("dsb" ::: "memory");

    dma_consumer = (idx + 1u) & 31u;
    dma_producer = (post_slot + 1u) & 31u;
    mmio_write32(D11_RX_DMA_PTR, FW_D11_DMA_RING_ADDR + dma_producer * 16u);
    MULTI->dma_frames++;
    return 1;
}

/* SDIOD Function 2 TX DMA ring management (0x18002200..0x18002214) */
static int sdio_f2_init(void) {
    mmio_write32(SDIOD_TX_DMA_CTL, 0u);
    uint32_t status = 0u;
    for (uint32_t i = 0u; i < 2000u; ++i) {
        status = mmio_read32(SDIOD_TX_DMA_STATUS0);
        if ((status & 0xf0000000u) == 0u) break;
    }
    if (status & 0xf0000000u) return fail(0x87u, SDIOD_TX_DMA_STATUS0, status);
    volatile uint32_t *ring = (volatile uint32_t *)FW_SDIO_TX_RING_ADDR;
    for (uint32_t i = 0u; i < 32u * 4u; ++i)
        ring[i] = 0u;
    __asm__ volatile("dsb" ::: "memory");

    mmio_write32(SDIOD_TX_DMA_ADDRLOW, FW_SDIO_TX_RING_ADDR);
    mmio_write32(SDIOD_TX_DMA_ADDRHIGH, 0u);
    mmio_write32(SDIOD_TX_DMA_PTR, FW_SDIO_TX_RING_ADDR);
    sdio_tx_idx = 0u;
    sdio_tx_posted = 0u;
    MBOX->f2_counts = 0u;
    MBOX->f2_consumed = 0u;
    MBOX->f2_posted = 0u;
#if defined(CLEAN_SDIO_SELF_TEST) && !defined(CLEAN_SDIO_SELF_TEST_DMA32)
    mmio_write32(SDIOD_TX_DMA_CTL, 0x000a0801u);
#else
    mmio_write32(SDIOD_TX_DMA_CTL, CLEAN_SDIO_DMA_CONTROL);
#endif
    uint32_t control = mmio_read32(SDIOD_TX_DMA_CTL);
#if defined(CLEAN_SDIO_SELF_TEST) && !defined(CLEAN_SDIO_SELF_TEST_DMA32)
    if (control != 0x000a0801u)
#else
    if (control != CLEAN_SDIO_DMA_CONTROL)
#endif
        return fail(0x8bu, SDIOD_TX_DMA_CTL, control);
    return 1;
}

static void sdio_f2_send_frame(const volatile fw_frame_slot_t *slot, uint32_t channel) {
    uint32_t rxe_len = slot->rxe_len;
    if (rxe_len < 14u || rxe_len > CLEAN_RXE_BYTES) return;
    uint32_t mpdu_len = rxe_len - 6u;
    uint32_t sdpcm_len = 18u + mpdu_len;
    uint32_t fragments = sdpcm_len > CLEAN_F2_PACKET_BYTES ? 2u : 1u;

    int credit = clean_sdio_credit_available(sdio_tx_posted, MBOX->f2_consumed,
                                              SDIO_TX_WINDOW);
    if (credit < 0) {
        (void)fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
        return;
    }
    if (!credit) {
        MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 1u);
        return;
    }
    uint32_t status = mmio_read32(SDIOD_TX_DMA_STATUS0);
    for (uint32_t part = 0u; part < fragments; ++part) {
        int available = clean_sdio_available((sdio_tx_idx + part) & 31u, status);
        if (available < 0) {
            (void)fail(0x86u, SDIOD_TX_DMA_STATUS0, status);
            return;
        }
        if (!available) {
            MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 1u);
            return;
        }
    }

    uint8_t sequence = sdpcm_seq++;
    for (uint32_t part = 0u; part < fragments; ++part) {
        uint32_t offset = part * CLEAN_F2_FRAGMENT_BYTES;
        uint32_t payload = mpdu_len - offset;
        if (fragments > 1u && payload > CLEAN_F2_FRAGMENT_BYTES)
            payload = CLEAN_F2_FRAGMENT_BYTES;
        uint32_t header = fragments > 1u ? CLEAN_F2_FRAGMENT_HEADER : 18u;
        uint32_t physical_len = header + payload;
        uint32_t dma_len = (physical_len + 3u) & ~3u;
        uint32_t idx = sdio_tx_idx;
        uint32_t buf_addr = FW_SDIO_TX_BUF_ADDR + idx * FW_SDIO_TX_BUF_BYTES;
        volatile uint8_t *pkt = (volatile uint8_t *)(uintptr_t)buf_addr;
        for (uint32_t i = 0u; i < dma_len; ++i)
            pkt[i] = 0u;

        /* 12-byte SDPCM header + 2-byte pad */
        pkt[0] = (uint8_t)physical_len;
        pkt[1] = (uint8_t)(physical_len >> 8u);
        pkt[2] = (uint8_t)~physical_len;
        pkt[3] = (uint8_t)(~physical_len >> 8u);
        pkt[4] = sequence;
        pkt[5] = 2u; /* Channel 2 = Data */
        pkt[6] = 0u;
        pkt[7] = 14u; /* Data offset = 14 */
        pkt[8] = 0u;
        pkt[9] = 8u; /* Credit */
        pkt[10] = 0u;
        pkt[11] = 0u;
        pkt[12] = (uint8_t)CLEAN_RX_PHASE_MAGIC | (uint8_t)(rx_after_explicit_tx << 7u);
        pkt[13] = (uint8_t)(CLEAN_RX_PHASE_MAGIC >> 8u);

        /* 4-byte BDC header at offset 14 */
        pkt[14] = fragments > 1u ? 0x21u : 0x20u;
        pkt[15] = slot->raw[2]; /* PLCP[0] rate/SIGNAL byte */
        uint8_t is_ofdm = (uint8_t)((slot->dma_hdr[4] & 1u) || ((slot->rxe_status2 >> 13u) & 1u));
        pkt[16] = (uint8_t)((channel & 0xfu) | (is_ofdm ? 0x10u : 0u));
        pkt[17] = 0u;

        if (fragments > 1u) {
            pkt[18] = (uint8_t)mpdu_len;
            pkt[19] = (uint8_t)(mpdu_len >> 8u);
            pkt[20] = (uint8_t)offset;
            pkt[21] = (uint8_t)(offset >> 8u);
        }
        for (uint32_t i = 0u; i < payload; ++i)
            pkt[header + i] = slot->raw[8u + offset + i];
        __asm__ volatile("dsb" ::: "memory");

        volatile uint32_t *ring = (volatile uint32_t *)FW_SDIO_TX_RING_ADDR;
        ring[4u * idx + 0u] = clean_sdio_flags(idx);
        ring[4u * idx + 1u] = dma_len & 0x7fffu;
        ring[4u * idx + 2u] = buf_addr;
        ring[4u * idx + 3u] = 0u;
        __asm__ volatile("dsb" ::: "memory");

        sdio_tx_idx = (idx + 1u) & 31u;
    }
    /* Both physical packets are ready before publishing one logical-frame credit. */
    ++sdio_tx_posted;
    MBOX->f2_posted = sdio_tx_posted;
    mmio_write32(SDIOD_TX_DMA_PTR, FW_SDIO_TX_RING_ADDR + sdio_tx_idx * 16u);
    MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 0u);
}

/* Transmit an 11 Mbps CCK IEEE 802.11 ACK frame (FC=0x00d4, RA=ta[0..5]) immediately upon unicast reception */
static void d11_transmit_ack_frame(const uint8_t ta[6], uint32_t channel, uint32_t mac_tx_ctl) {
    uint8_t ack[20];
    ack[0] = 0x6eu; ack[1] = 0x84u; ack[2] = 0x0bu; ack[3] = 0x00u; ack[4] = 0xd0u; ack[5] = 0x9bu;
    ack[6] = 0xd4u; ack[7] = 0x00u; ack[8] = 0x00u; ack[9] = 0x00u;
    for (uint32_t i = 0u; i < 6u; ++i) ack[10u + i] = ta[i];
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 6u; i < 16u; ++i) {
        crc ^= ack[i];
        for (uint32_t b = 0u; b < 8u; ++b)
            crc = (crc & 1u) ? ((crc >> 1u) ^ 0xedb88320u) : (crc >> 1u);
    }
    crc = ~crc;
    ack[16] = (uint8_t)crc;
    ack[17] = (uint8_t)(crc >> 8u);
    ack[18] = (uint8_t)(crc >> 16u);
    ack[19] = (uint8_t)(crc >> 24u);

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    phy_tx_hw_configure(channel);
    phy_tx_set_inhibit(0);

    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < 5u; ++i) {
        const uint8_t *b = ack + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_TPL_WR_DATA, w);
    }

    mmio_write32(D11_MACCONTROL, mac_tx_ctl);
    (void)mmio_read32(D11_MACCONTROL);
    mmio_write16(0x18001570u, 0x0000u);
    mmio_write16(0x18001568u, 0x0000u);
    mmio_write16(0x180017c0u, 0x0100u);
    mmio_write16(0x1800150cu, 0x0040u);
    mmio_write16(0x18001514u, 0x1a02u);
    mmio_write16(0x18001508u, 0x0000u);
    mmio_write16(0x1800150au, 0x0000u);
    mmio_write16(0x1800154cu, 0x0000u);
    mmio_write16(0x1800156au, 20u);
    mmio_write16(0x18001568u, 0x0826u);
    mmio_write16(0x18001500u, 0x0000u);
    mmio_write16(0x18001502u, 0x00d0u);
    (void)mmio_read16(0x18001502u);

    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read16(0x1800150eu) & 0x0080u) == 0u) break;
        (void)delay_ticks(90u);
    }
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read16(0x1800150eu) & 0x0400u) != 0u) break;
        (void)delay_ticks(90u);
    }
    for (uint32_t i = 0u; i < 1000u; ++i) {
        if ((mmio_read16(0x18001690u) & 0x0100u) == 0u) break;
        (void)delay_ticks(90u);
    }

    mmio_write16(0x18001502u, 0x0000u);
    mmio_write16(0x18001568u, 0x0000u);
    mmio_write16(0x180017c0u, 0x0083u);
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    phy_tx_set_inhibit(1);
    shm_write16(0x7afu, (uint16_t)(shm_read16(0x7afu) + 1u));
}

/* Transmit a raw 802.11 frame (6-byte PLCP + MPDU) stored at FW_TX_BUF_ADDR via D11 Template RAM + TXE */
static int d11_transmit_template_frame(uint32_t total_len, uint32_t is_ofdm, uint32_t channel, uint32_t mac_tx_ctl) {
    if (total_len < 16u || total_len > CLEAN_TX_BYTES)
        return fail(0x83u, FW_TX_BUF_ADDR, total_len);
    const volatile uint8_t *tx_buf = (const volatile uint8_t *)FW_TX_BUF_ADDR;
    uint16_t phyctl1 = 0x1a02u;
    uint32_t ofdm_airtime = 0u;
    uint32_t station = shm_read16(CLEAN_ACK_ENABLE);
    if (is_ofdm) {
        int rate = clean_ofdm_rate_index(tx_buf[0]);
        if (rate < 0 || !clean_ofdm_plcp_valid(tx_buf, total_len - CLEAN_PLCP_BYTES))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        phyctl1 = clean_ofdm_rates[(uint32_t)rate].phyctl1;
        ofdm_airtime = clean_ofdm_airtime_us(total_len - CLEAN_PLCP_BYTES, (uint32_t)rate);
    }
    tx_diag.magic = CLEAN_TX_DIAG_MAGIC;
    tx_diag.resume_ticks = 0u;
    tx_diag.mac_resume_us = 0u;
    tx_diag.psm_after = 0u;
    tx_diag.start_ticks = ARM_CYCLE_COUNTER;
    tx_diag.length = total_len;
    tx_diag.channel = (uint16_t)channel;
    tx_diag.psm_before = shm_read16(0x601u);
    tx_diag.status_before = tx_status();

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    MBOX->rc_dacbuf = 0x55550001u;
    phy_tx_hw_configure(channel);
    phy_tx_set_inhibit(0);

    MBOX->rc_dacbuf = 0x55550002u;
    uint16_t fc_tx = (uint16_t)tx_buf[6] | ((uint16_t)tx_buf[7] << 8u);
    uint32_t use_hw_ccmp = (shm_read16(0x615u) != 0u) && ((fc_tx & 0x4000u) != 0u);
    uint32_t words = (total_len + 3u) >> 2u;
    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < words; ++i) {
        const volatile uint8_t *b = tx_buf + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_TPL_WR_DATA, w);
    }
    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < words; ++i) {
        const volatile uint8_t *b = tx_buf + 4u * i;
        uint32_t expected = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                            ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        uint32_t actual = mmio_read32(D11_TPL_WR_DATA);
        if (actual != expected) return fail(0x8du, i * 4u, actual);
    }

    mmio_write32(D11_MACCONTROL, mac_tx_ctl);
    (void)mmio_read32(D11_MACCONTROL);
    MBOX->rc_dacbuf = 0x55550003u;
    mmio_write16(0x18001570u, 0x0000u);
    mmio_write16(0x18001572u, (uint16_t)(total_len << 4u));
    mmio_write16(0x18001568u, 0x0000u);
    if (use_hw_ccmp) {
        uint16_t iv_off = ((fc_tx & 0x0080u) != 0u) ? 32u : 30u;
        mmio_write16(0x180017c8u, 0x0000u);
        mmio_write16(0x180017c2u, iv_off);
        mmio_write16(0x180017c6u, 0x0110u);
        for (uint32_t k = 0u; k < 8u; ++k) {
            mmio_write16(0x180017c4u, shm_read16(0x618u + k));
        }
        mmio_write16(0x180017c0u, 0x1170u);
        mmio_write16(0x180017d4u, 0x0000u);
        mmio_write16(0x180017d6u, (uint16_t)total_len);
        mmio_write16(0x180017d0u, 0x0305u);
    } else {
        mmio_write16(0x180017c0u, 0x0100u);
        mmio_write16(0x180017d0u, 0x0300u);
    }
    mmio_write16(0x1800150cu, is_ofdm ? 0x0041u : 0x0040u);
    mmio_write16(0x18001514u, phyctl1);
    mmio_write16(0x18001508u, 0x0000u);
    mmio_write16(0x1800150au, 0x0000u);
    mmio_write16(0x1800154cu, 0x0000u);
    mmio_write16(0x1800156au, (uint16_t)total_len);
    mmio_write16(0x18001568u, 0x0826u);
    mmio_write16(0x18001500u, 0x0000u);
    static const uint16_t phy_regs[CLEAN_TX_PHY_COUNT] = CLEAN_TX_PHY_REGS;
    static const uint16_t radio_regs[8] = CLEAN_TX_RADIO_REGS;
    static const uint16_t ihr_regs[8] = CLEAN_TX_IHR_REGS;
    for (uint32_t i = 0u; i < CLEAN_TX_PHY_COUNT; ++i) tx_diag.phy[i] = phy_reg_read(phy_regs[i]);
    for (uint32_t i = 0u; i < 8u; ++i) {
        tx_diag.radio[i] = radio_reg_read(radio_regs[i]);
        tx_diag.ihr[i] = mmio_read16(D11_BASE + ihr_regs[i]);
    }
    uint16_t saved_9a5 = phy_reg_read(0x9a5u);
    phy_reg_mod(0x9a5u, 2u, 2u);
    for (uint32_t group = 0u; group < 2u; ++group) {
        phy_reg_write(0x00du, 0u);
        phy_reg_write(0x00eu, group ? 112u : 96u);
        for (uint32_t i = 0u; i < 4u; ++i)
            tx_diag.iq[group * 4u + i] = phy_reg_read(0x456u);
    }
    phy_reg_write(0x9a5u, saved_9a5);
    tx_diag.maccontrol = mmio_read32(D11_MACCONTROL);
    tx_diag.gci_output = mmio_read32(GCI_OUTPUT_REG1);
    tx_diag.gci_control = mmio_read32(GCI_CONTROL_REG1);
    MBOX->rc_dacbuf = 0x55550004u;
    tx_diag.trigger_ticks = ARM_CYCLE_COUNTER;
    tx_diag.mac_trigger_us = mmio_read32(D11_BASE + 0x1cu);
    mmio_write16(0x18001502u, 0x00d0u);
    MBOX->rc_dacbuf = 0x55550005u;
    (void)mmio_read16(0x18001502u);
    tx_diag.phy_debug_first = mmio_read32(D11_BASE + 0x158u);
    tx_diag.status_first = tx_status();

    uint16_t st_50e_seen = 0u, st_690_seen = 0u;
    uint32_t tx_conditions = 0u;
    for (uint32_t i = 0u; i < 2000u; ++i) {
        uint16_t v = mmio_read16(0x1800150eu);
        st_50e_seen |= v;
        if ((v & 0x0080u) == 0u) {
            tx_conditions |= 1u;
            break;
        }
        (void)delay_ticks(180u);
    }
    for (uint32_t i = 0u; i < 2000u; ++i) {
        uint16_t v = mmio_read16(0x1800150eu);
        st_50e_seen |= v;
        if ((v & 0x0400u) != 0u) {
            tx_conditions |= 2u;
            break;
        }
        (void)delay_ticks(180u);
    }
    for (uint32_t i = 0u; i < 2000u; ++i) {
        uint16_t v = mmio_read16(0x18001690u);
        st_690_seen |= v;
        if ((v & 0x0100u) == 0u) {
            tx_conditions |= 4u;
            break;
        }
        (void)delay_ticks(180u);
    }
    MBOX->vco_status = ((uint32_t)st_50e_seen << 16u) | st_690_seen;
    MBOX->fw_substage = tx_conditions;
    uint32_t phy_drained = 1u;
    if (station) {
        uint32_t active_seen = tx_diag.phy_debug_first & 2u;
        uint32_t wait = mmio_read32(D11_BASE + 0x1cu);
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 200000u; ++poll) {
            uint32_t active = mmio_read32(D11_BASE + 0x158u) & 2u;
            active_seen |= active;
            if (active_seen && !active) { phy_drained = 1u; break; }
            if (mmio_read32(D11_BASE + 0x1cu) - wait >= 5000u) break;
        }
        mmio_write16(0x18001502u, 0u);
        mmio_write16(0x18001568u, 0u);
        phy_tx_set_automatic();
    }
    if (is_ofdm && !station) {
        /* MAC completion precedes the OFDM PHY tail. Retain TX enable for a
         * complete PPDU after MAC drain, including the ERP signal extension. */
        uint32_t drain_start = mmio_read32(D11_BASE + 0x1cu);
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
            if (mmio_read32(D11_BASE + 0x1cu) - drain_start >= ofdm_airtime) {
                phy_drained = 1u;
                break;
            }
        }
    }
    tx_diag.complete_ticks = ARM_CYCLE_COUNTER;
    tx_diag.mac_complete_us = mmio_read32(D11_BASE + 0x1cu);
    tx_diag.status_after = tx_status();
    tx_diag.phy_error = mmio_read16(D11_BASE + 0x3eeu);
    tx_diag.psm_debug = mmio_read32(D11_BASE + 0x154u);

    if (!station) {
        mmio_write16(0x18001502u, 0x0000u);
        mmio_write16(0x18001568u, 0x0000u);
        mmio_write16(0x180017c0u, 0x8300u);
        mmio_write16(0x180017d0u, 0x0300u);
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        (void)mmio_read32(D11_MACCONTROL);
        phy_tx_set_inhibit(1);
    }
    MBOX->rc_dacbuf = 0x55550000u | total_len;
    if (!phy_drained) return fail(0x82u, D11_BASE + 0x1cu, ofdm_airtime);
    if (is_ofdm && !station && tx_diag.phy_error != 0u)
        return fail(0x82u, D11_BASE + 0x3eeu, tx_diag.phy_error);
    return tx_conditions == 7u || fail(0x82u, 0x1800150eu, MBOX->vco_status);
}

static void snapshot_psm(uint32_t started, uint32_t polls) {
    for (uint32_t i = 0u; i < 16u; ++i) {
        MBOX->psm_mailbox[i] = shm_read16(0x600u + i);
        MBOX->psm_diag[i] = shm_read16(0x7a0u + i);
        if (i < CLEAN_RX_FAULT_WORDS) MBOX->rx_fifo_fault[i] = shm_read16(0x620u + i);
    }
    sample_rxe8(MBOX->rxe_pre);
    MBOX->rx_polls = polls;
    MBOX->rx_elapsed_ticks = ARM_CYCLE_COUNTER - started;
    MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
}

static int load_and_start_psm(uint32_t generation, uint32_t channel) {
    uint32_t words = psm_ucode_byte_count / 4u;
    for (uint32_t i = 0u; i < words; ++i) {
        const uint8_t *b = psm_ucode_bytes + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_OBJ_ADDR, i);
        (void)mmio_read32(D11_OBJ_ADDR);
        mmio_write32(D11_OBJ_DATA, w);
    }
    for (uint32_t i = 0u; i < words; ++i) {
        const uint8_t *b = psm_ucode_bytes + 4u * i;
        uint32_t expected = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                            ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        mmio_write32(D11_OBJ_ADDR, i);
        (void)mmio_read32(D11_OBJ_ADDR);
        uint32_t actual = mmio_read32(D11_OBJ_DATA);
        if (actual != expected)
            return fail(0x70u, i, actual);
    }

    for (uint32_t i = 0u; i < 32u; ++i)  shm_write16(0x600u + i, 0u);
    for (uint32_t i = 0u; i < 480u; ++i) shm_write16(0x620u + i, 0u);
    shm_write16(CLEAN_SHM_CAPTURE_LIMIT, CLEAN_RAW_BYTES);
    shm_write16(CLEAN_SHM_RXE_LIMIT, CLEAN_RXE_BYTES);

    shm_write16(0x60cu, (uint16_t)generation);
    shm_write16(0x60du, (uint16_t)(generation >> 16u));
    shm_write16(0x610u, (uint16_t)generation);
    shm_write16(0x611u, (uint16_t)(generation >> 16u));

    static const uint8_t start_bits[4] = {0u, 4u, 6u, 2u};
    uint32_t started = ARM_CYCLE_COUNTER;
    for (uint32_t i = 0u; i < 4u; ++i) {
        mmio_write32(D11_MACCONTROL, 0x84020400u | start_bits[i]);
        (void)mmio_read32(D11_MACCONTROL);
    }
    uint32_t ready = 0u;
    for (uint32_t poll = 0u; poll < 2000u; ++poll) {
        uint16_t st = shm_read16(0x601u);
        if (st == PSM_STATE_READY) {
            snapshot_psm(started, poll);
            ready = 1u;
            break;
        }
        if (st == PSM_STATE_FAULT) {
            snapshot_psm(started, poll);
            /* Observe late completion without restarting or accepting the failed PSM. */
            (void)delay_ticks(2000000u);
            sample_rxe8(MBOX->rxe_post);
            return fail(0x71u, shm_read16(0x602u), st);
        }
        (void)delay_ticks(500u);
    }
    if (!ready) {
        snapshot_psm(started, 2000u);
        return fail(0x72u, 0x601u, shm_read16(0x601u));
    }

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    shm_write16(0x609u, (uint16_t)channel);
    return 1;
}

static void d11_set_addrmatch(uint16_t offset, const volatile uint8_t *address) {
    mmio_write16(D11_BASE + 0x420u, 0x0020u | offset);
    for (uint32_t i = 0u; i < 3u; ++i)
        mmio_write16(D11_BASE + 0x422u,
                     (uint16_t)address[2u * i] | ((uint16_t)address[2u * i + 1u] << 8u));
}

static int d11_restart_rx_psm(uint32_t channel) {
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    uint16_t saved_tk[8];
    uint16_t saved_ccmp_en = shm_read16(0x615u);
    for (uint32_t k = 0u; k < 8u; ++k) saved_tk[k] = shm_read16(0x618u + k);
    shm_write16(0x601u, 0u);
    shm_write16(0x602u, 0u);
    shm_write16(0x609u, (uint16_t)channel);
    static const uint8_t restart_bits[4] = {0u, 4u, 6u, 2u};
    for (uint32_t i = 0u; i < 4u; ++i) {
        mmio_write32(D11_MACCONTROL, 0x84020400u | restart_bits[i]);
        (void)mmio_read32(D11_MACCONTROL);
    }
    uint16_t state = 0u;
    for (uint32_t poll = 0u; poll < 2000u; ++poll) {
        state = shm_read16(0x601u);
        if (state == PSM_STATE_READY || state == PSM_STATE_FAULT) break;
        (void)delay_ticks(200u);
    }
    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    if (state != PSM_STATE_READY) return fail(0x84u, 0x601u, state);
    shm_write16(0x609u, (uint16_t)channel);
    shm_write16(0x615u, saved_ccmp_en);
    for (uint32_t k = 0u; k < 8u; ++k) shm_write16(0x618u + k, saved_tk[k]);
    return 1;
}

void cyw43_main(void) {
    if (MBOX->fw_substage != 0u) {
        MBOX->fw_error = 0x99u;
        MBOX->fw_state = FW_STATE_ERROR;
        __asm__ volatile("dsb" ::: "memory");
        return;
    }
    MBOX->magic = FW_MAGIC;
    MBOX->fw_state = FW_STATE_PMU_PLL;
    uint32_t flags = MBOX->config_flags;
    uint32_t generation = MBOX->generation ? MBOX->generation : 0x13572468u;

    /* 1. Initialize PMU PLL and PMU Resources */
    int pmu_rc = pmu_init_pll_and_resources();
    if (pmu_rc != 1) {
        fail(0x11u, PMU_RES_PENDING, (uint32_t)(-pmu_rc));
        return;
    }

    /* 2. Wait for host to request HT clock on Function 1 (0x1000e = 0x10) */
    MBOX->fw_state = FW_STATE_WAIT_HT;
    __asm__ volatile("dsb" ::: "memory");
    uint32_t ht_ok = 0u;
    for (uint32_t i = 0u; i < 2000000u; ++i) {
        if (MBOX->host_cmd >= 1u && (mmio_read32(CC_CLK_CTL_ST) & 0x30000u) == 0x30000u) {
            ht_ok = 1u;
            break;
        }
    }
    if (!ht_ok) {
        fail(0x30u, CC_CLK_CTL_ST, mmio_read32(CC_CLK_CTL_ST));
        return;
    }

#ifdef CLEAN_SDIO_SELF_TEST
    _Static_assert(__builtin_offsetof(fw_mbox_t, f2_consumed) == CLEAN_SDIO_TEST_ACK_OFFSET,
                   "Transport acknowledgement offset");
    _Static_assert(__builtin_offsetof(fw_mbox_t, rc_adc) == CLEAN_SDIO_TEST_WINDOW_OFFSET,
                   "Transport window offset");
    _Static_assert(__builtin_offsetof(fw_mbox_t, rc_dacbuf) == CLEAN_SDIO_TEST_PATTERN_OFFSET,
                   "Transport pattern offset");
#ifdef CLEAN_SDIO_SELF_TEST_LARGE
    const uint32_t test_pattern = 2u;
#elif defined(CLEAN_SDIO_SELF_TEST_TRANSITION)
    const uint32_t test_pattern = 1u;
#else
    const uint32_t test_pattern = 0u;
#endif
    if (!sdio_f2_init()) return;
    MBOX->rc_lpf = 0u;
    MBOX->rc_adc = CLEAN_SDIO_SELF_TEST_WINDOW;
    MBOX->rc_dacbuf = test_pattern;
#ifdef CLEAN_SDIO_SELF_TEST_PACED
    MBOX->fw_substage = CLEAN_SDIO_TEST_PACING_TICKS;
#else
    MBOX->fw_substage = 0u;
#endif
    MBOX->fw_state = FW_STATE_PSM_READY;
    while (MBOX->host_cmd < 2u) __asm__ volatile("nop");
    MBOX->fw_state = FW_STATE_RX_ACTIVE;
    uint32_t sequence = 0u;
    while (1) {
        uint32_t command = MBOX->host_cmd;
        if ((command & 255u) != 7u) continue;
        uint32_t count = command >> 8u;
        if (count < 1u || count > 16u) {
            (void)fail(0x88u, FW_MBOX_ADDR + 24u, command);
            return;
        }
        for (uint32_t i = 0u; i < count; ++i) {
#if CLEAN_SDIO_SELF_TEST_WINDOW > 0u
            uint32_t waits = 0u;
            while (!clean_sdio_test_window_open(sequence, MBOX->f2_consumed,
                                                CLEAN_SDIO_SELF_TEST_WINDOW) &&
                   waits < 20000000u) ++waits;
            if (waits == 20000000u) {
                (void)fail(0x8au, FW_MBOX_ADDR + CLEAN_SDIO_TEST_ACK_OFFSET, MBOX->f2_consumed);
                return;
            }
#endif
            volatile fw_frame_slot_t *slot = &MULTI->frames[0];
            uint32_t length = clean_sdio_test_pattern_length(++sequence, test_pattern);
            slot->rxe_len = (uint16_t)(length + 6u);
            for (uint32_t byte = 0u; byte < length; ++byte)
                slot->raw[8u + byte] = clean_sdio_test_byte(sequence, byte);
            sdio_f2_send_frame(slot, 6u);
            if (MBOX->fw_state == FW_STATE_ERROR) return;
#ifdef CLEAN_SDIO_SELF_TEST_PACED
            if (!delay_ticks(CLEAN_SDIO_TEST_PACING_TICKS)) {
                (void)fail(0x89u, 0x18003090u, CLEAN_SDIO_TEST_PACING_TICKS);
                return;
            }
#endif
        }
        MBOX->host_cmd = 2u;
        __asm__ volatile("dsb" ::: "memory");
    }
#endif

    /* 3. Release D11 & PHY from reset, load ROM board tables, and run Radio 20691 + LCN20 PHY calibration */
    MBOX->fw_state = FW_STATE_COLD_PHY;
    uint32_t rc_lpf = 0u, rc_adc = 0u, rc_dacbuf = 0u, vco_st = 0u, dc_idac = 0u, dc_dcoe = 0u;
    if (!d11_and_phy_cold_init(&rc_lpf, &rc_adc, &rc_dacbuf, &vco_st, &dc_idac, &dc_dcoe)) {
        fail(0x51u, D11_PHY_REG_ADDR, 0u);
        return;
    }
    MBOX->rc_lpf = rc_lpf;
    MBOX->rc_adc = rc_adc;
    MBOX->rc_dacbuf = rc_dacbuf;
    MBOX->vco_status = vco_st;
    MBOX->dc_idac = dc_idac;
    MBOX->dc_dcoe = dc_dcoe;

    /* 4. Tune to requested initial 2.4 GHz channel (if != 1) */
    uint32_t channel = CFG_CHANNEL_GET(flags);
    if (channel != 1u) {
        if (!phy_tune_channel_2g(channel, &vco_st, &dc_idac, &dc_dcoe)) {
            fail(0x61u, channel, 0u);
            return;
        }
        MBOX->vco_status = vco_st;
        MBOX->dc_idac = dc_idac;
        MBOX->dc_dcoe = dc_dcoe;
    }

    /* 5. Configure D11 RXE, PSM Branch Masks, BTCX / RF Switch, IFS/TSF/NAV, and Radio LPF */
    MBOX->fw_state = FW_STATE_EXTRA_INIT;
    d11_configure_mac_and_rxe(flags, (uint16_t)rc_lpf);

    /* 6. Load and start D11 PSM microcode to PSM_STATE_READY */
    MBOX->fw_state = FW_STATE_PSM_LOAD;
    if (!load_and_start_psm(generation, channel))
        return;

    sample_rxe8(MBOX->rxe_pre);

    /* 7. Announce PSM_READY and wait for host_cmd >= 2 */
    MBOX->fw_state = FW_STATE_PSM_READY;
    __asm__ volatile("dsb" ::: "memory");
    while (MBOX->host_cmd < 2u) {
        __asm__ volatile("nop");
    }

    /* 8. Pre-configure TX chain, set station MAC for Auto-ACK, initialize RX/SDIO DMA, and enable MAC + PSM */
    phy_tx_hw_configure(channel);
    phy_tx_set_inhibit(1);
    if (flags & CFG_STA_ROLE) {
        shm_write16(0x612u, 0x0802u);
        shm_write16(0x613u, 0x57dau);
        shm_write16(0x614u, 0xeaf5u);
    } else {
        shm_write16(0x612u, 0x9b02u);
        shm_write16(0x613u, 0x88bfu);
        shm_write16(0x614u, 0x7dddu);
    }
    rx_dma_init();
    if (!sdio_f2_init()) return;

    uint32_t raw_target = MULTI->target_frames;
    uint32_t scanner_mode = (raw_target == 0xffffffffu);
    uint32_t target_frames = raw_target ? raw_target : 1u;
    if (!scanner_mode && target_frames > FW_MAX_FRAMES) target_frames = FW_MAX_FRAMES;
    MULTI->magic = FW_MULTI_MAGIC;
    MULTI->target_frames = target_frames;
    MULTI->captured_frames = 0u;

    uint32_t mac_en = 0xc0020403u;
    if (flags & CFG_PROMISCUOUS)
        mac_en |= 0x01d00000u;
    mmio_write32(D11_MACCONTROL, mac_en);
    MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
    MBOX->fw_state = FW_STATE_RX_ACTIVE;

    uint32_t rx_start = ARM_CYCLE_COUNTER;
    uint32_t polls = 0u;
    for (; scanner_mode || polls < 1800000u; ++polls) {
        if ((polls & 4095u) == 0u) snapshot_psm(rx_start, polls);
        uint32_t cmd = MBOX->host_cmd;
        if (station_enabled && shm_read16(CLEAN_ACK_ERROR)) {
            fail(0x87u, CLEAN_ACK_ERROR, shm_read16(CLEAN_ACK_ERROR));
            break;
        }
        if (cmd == 3u) {
            if (station_enabled) {
                UNICAST->ack_sent = shm_read16(CLEAN_ACK_SENT);
                UNICAST->ack_error = shm_read16(CLEAN_ACK_ERROR);
                UNICAST->ack_txe_status = shm_read16(0x635u);
                UNICAST->ack_ifs_status = shm_read16(0x636u);
            }
            break;
        }
        if ((cmd & 0xffu) == 4u) {
            if (station_enabled) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint32_t new_ch = (cmd >> 8u) & 0xffu;
            if (new_ch >= 1u && new_ch <= 14u) {
                mmio_write32(D11_MACCONTROL, 0x84020400u);
                (void)mmio_read32(D11_MACCONTROL);
                (void)delay_ticks(2000u);
                if (phy_fast_switch_channel_2g(new_ch, &vco_st)) {
                    MBOX->vco_status = vco_st;
                    channel = new_ch;
                }
                shm_write16(0x609u, (uint16_t)channel);
                if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT)
                    shm_write16(0x601u, 0u);
                mmio_write32(D11_MACCONTROL, mac_en);
                MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            }
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 5u) {
            if ((flags & CFG_TX_ENABLE) == 0u) {
                fail(0x81u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint32_t tx_len  = clean_tx_length(cmd);
            uint32_t is_ofdm = (cmd >> 18u) & 1u;
            uint32_t tx_cnt  = (cmd >> 19u) & 0xffu;
            if (tx_cnt == 0u) tx_cnt = 1u;
            uint32_t reliable = (cmd & CLEAN_TX_ACK_REQUIRED) != 0u;
            const volatile uint8_t *frame = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            if (station_enabled && (cmd & (CLEAN_TX_REARM_RX | CLEAN_TX_MATCH_SOURCE))) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if (reliable && (!station_enabled || tx_cnt > 8u ||
                !is_ofdm || tx_len < 34u || tx_len > CLEAN_TX_BYTES ||
                (frame[10] & 1u) ||
                !(((frame[6] & 0x0fu)==0u && (frame[7]&7u)==0u) ||
                  (frame[6]==8u && (frame[7]&7u)==1u)) ||
                (frame[28] & 15u) ||
                ((uint16_t)frame[10] | ((uint16_t)frame[11] << 8u)) != shm_read16(CLEAN_ACK_PEER) ||
                ((uint16_t)frame[12] | ((uint16_t)frame[13] << 8u)) != shm_read16(CLEAN_ACK_PEER+1u) ||
                ((uint16_t)frame[14] | ((uint16_t)frame[15] << 8u)) != shm_read16(CLEAN_ACK_PEER+2u) ||
                ((uint16_t)frame[16] | ((uint16_t)frame[17] << 8u)) != shm_read16(0x612u) ||
                ((uint16_t)frame[18] | ((uint16_t)frame[19] << 8u)) != shm_read16(0x613u) ||
                ((uint16_t)frame[20] | ((uint16_t)frame[21] << 8u)) != shm_read16(0x614u))) {
                fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                break;
            }
            if ((cmd & CLEAN_TX_MATCH_SOURCE) &&
                (tx_len < 34u || tx_len > CLEAN_TX_BYTES || (frame[6] & 0x0fu) != 0u ||
                 (frame[7] & 3u) != 0u || (frame[16] & 1u) != 0u)) {
                fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                break;
            }
            mmio_write32(D11_MACCONTROL, 0x84020400u);
            (void)mmio_read32(D11_MACCONTROL);
            (void)delay_ticks(2000u);
            if (cmd & CLEAN_TX_MATCH_SOURCE) {
                d11_set_addrmatch(0u, frame + 16u);
                d11_set_addrmatch(3u, frame + 22u);
            }
            if (reliable) {
                UNICAST->magic = CLEAN_UNICAST_MAGIC;
                UNICAST->result = CLEAN_UNICAST_IDLE;
                UNICAST->attempts = 0u;
                UNICAST->ack_received = 0u;
                UNICAST->started_us = mmio_read32(D11_BASE + 0x1cu);
            }
            for (uint32_t t = 0u; t < tx_cnt; ++t) {
                if (reliable) {
                    /* Freeze the sequence number, change only Retry and its FCS. */
                    if (t) {
                        volatile uint8_t *retry = (volatile uint8_t *)FW_TX_BUF_ADDR;
                        retry[7] |= 8u;
                        uint32_t crc = 0xffffffffu;
                        for (uint32_t i = 6u; i < tx_len - 4u; ++i) {
                            crc ^= retry[i];
                            for (uint32_t bit = 0u; bit < 8u; ++bit)
                                crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
                        }
                        crc = ~crc;
                        for (uint32_t i = 0u; i < 4u; ++i)
                            retry[tx_len - 4u + i] = (uint8_t)(crc >> (8u * i));
                    }
                    shm_write16(CLEAN_ACK_SEEN, 0u);
                    shm_write16(CLEAN_ACK_WAITING, 1u);
                    UNICAST->attempts = t + 1u;
                }
                if (!d11_transmit_template_frame(tx_len, is_ofdm, channel, mac_en & ~2u))
                    break;
                if (reliable) {
                    mmio_write32(D11_MACCONTROL, mac_en);
                    uint32_t wait = mmio_read32(D11_BASE + 0x1cu);
                    uint32_t ack = 0u;
                    for (uint32_t poll = 0u; poll < 200000u; ++poll) {
                        ack = shm_read16(CLEAN_ACK_SEEN);
                        if (ack || mmio_read32(D11_BASE + 0x1cu) - wait >= 1500u) break;
                    }
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    if (ack) {
                        UNICAST->ack_received = 1u;
                        UNICAST->result = CLEAN_UNICAST_ACKED;
                        break;
                    }
                    UNICAST->result = CLEAN_UNICAST_TIMEOUT;
                    /* Preserve a management reply even when its preceding ACK
                     * was missed. It must be validated by the station machine. */
                    if (shm_read16(0x601u) == PSM_STATE_CAPTURED) break;
                    mmio_write32(D11_MACCONTROL, 0x84020400u);
                }
                if (t + 1u < tx_cnt)
                    (void)delay_ticks(180u * 1500u);
            }
            if (reliable) {
                UNICAST->finished_us = mmio_read32(D11_BASE + 0x1cu);
                UNICAST->ack_sent = shm_read16(CLEAN_ACK_SENT);
                UNICAST->ack_error = shm_read16(CLEAN_ACK_ERROR);
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if (cmd & CLEAN_TX_REARM_RX) {
                if (!d11_restart_rx_psm(channel)) break;
            } else if (!reliable && (shm_read16(0x601u) == PSM_STATE_CAPTURED ||
                       shm_read16(0x601u) == PSM_STATE_FAULT))
                shm_write16(0x601u, 0u);
            rx_after_explicit_tx = 1u;
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            tx_diag.resume_ticks = ARM_CYCLE_COUNTER;
            tx_diag.mac_resume_us = mmio_read32(D11_BASE + 0x1cu);
            tx_diag.psm_after = shm_read16(0x601u);
            publish_tx_diag();
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 7u) {
            const volatile uint8_t *p = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            uint16_t basic = (uint16_t)((uint16_t)p[12] | ((uint16_t)p[13] << 8u));
            uint8_t nonzero = 0u, peer = 0u;
            for (uint32_t i = 0u; i < 6u; ++i) { nonzero |= p[i]; peer |= p[6u + i]; }
            if (!(flags & CFG_TX_ENABLE) || !nonzero || !peer || (p[0] & 1u) ||
                (p[6] & 1u) || clean_ack_rate(basic, 0u) < 0 || p[14] > 1u || p[15] != 1u) {
                fail(0x85u, FW_TX_BUF_ADDR, basic);
                break;
            }
            mmio_write32(D11_MACCONTROL, 0x84020400u);
            (void)mmio_read32(D11_MACCONTROL);
            for (uint32_t i = 0u; i < 3u; ++i)
                shm_write16(0x612u + i, (uint16_t)((uint16_t)p[2u*i] |
                            ((uint16_t)p[2u*i + 1u] << 8u)));
            d11_set_addrmatch(0u, p);
            d11_set_addrmatch(3u, p + 6u);
            for (uint32_t i = 0u; i < 3u; ++i)
                shm_write16(CLEAN_ACK_PEER + i, (uint16_t)((uint16_t)p[6u + 2u*i] |
                            ((uint16_t)p[7u + 2u*i] << 8u)));
            for (uint32_t i = 0u; i < 12u; ++i) {
                uint8_t plcp[6];
                uint16_t phyctl1;
                if (!clean_ack_template(plcp, (uint32_t)clean_ack_rate(basic, i), &phyctl1)) {
                    fail(0x85u, FW_TX_BUF_ADDR, i);
                    break;
                }
                for (uint32_t w = 0u; w < 3u; ++w)
                    shm_write16(CLEAN_ACK_TABLE + 4u*i + w,
                        (uint16_t)((uint16_t)plcp[2u*w] | ((uint16_t)plcp[2u*w + 1u] << 8u)));
                shm_write16(CLEAN_ACK_TABLE + 4u*i + 3u, phyctl1);
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            shm_write16(CLEAN_ACK_WAITING, 0u);
            shm_write16(CLEAN_ACK_SEEN, 0u);
            shm_write16(CLEAN_ACK_SENT, 0u);
            shm_write16(CLEAN_ACK_ERROR, 0u);
            shm_write16(CLEAN_ACK_ENABLE, 1u);
            station_enabled = 1u;
            UNICAST->magic = CLEAN_UNICAST_MAGIC;
            UNICAST->result = CLEAN_UNICAST_IDLE;
            phy_tx_hw_configure(channel);
            phy_tx_set_inhibit(0);
            phy_tx_set_automatic();
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED) shm_write16(0x601u, 0u);
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 6u) {
            if (station_enabled) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            /* Install 128-bit AES-CCMP Temporal Key (TK) from FW_TX_BUF_ADDR into SHM 0x618..0x61f and enable HW CCMP */
            const volatile uint8_t *key_buf = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            mmio_write32(D11_MACCONTROL, 0x84020400u);
            (void)mmio_read32(D11_MACCONTROL);
            for (uint32_t k = 0u; k < 8u; ++k) {
                uint16_t kw = (uint16_t)key_buf[2u * k] | ((uint16_t)key_buf[2u * k + 1u] << 8u);
                shm_write16(0x618u + k, kw);
            }
            shm_write16(0x615u, (uint16_t)((cmd >> 8u) & 1u));
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT)
                shm_write16(0x601u, 0u);
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        uint16_t st = shm_read16(0x601u);
        if (st == PSM_STATE_FAULT) {
            if (station_enabled) {
                fail(0x88u, 0x601u, st);
                break;
            }
            if (!d11_restart_rx_psm(channel)) break;
            mmio_write32(D11_MACCONTROL, mac_en);
            continue;
        }
        if (st == PSM_STATE_CAPTURED) {
            if (station_enabled) {
                int credit = clean_sdio_credit_available(sdio_tx_posted, MBOX->f2_consumed,
                                                          SDIO_TX_WINDOW);
                if (credit < 0) {
                    fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
                    break;
                }
                /* The PSM may already have ACKed this frame. Keep its capture
                 * reserved instead of discarding it behind an occupied F2 slot. */
                if (!credit) continue;
            }
            uint32_t total = MULTI->captured_frames;
            uint32_t idx = total & (FW_MAX_FRAMES - 1u);
            volatile fw_frame_slot_t *slot = &MULTI->frames[idx];
            uint16_t rxe_len = shm_read16(0x606u);
            uint16_t fc_word  = shm_read16(CLEAN_SHM_FRAME + 4u);
            uint16_t ra_word0 = shm_read16(CLEAN_SHM_FRAME + 6u);
            uint16_t ra_word1 = shm_read16(CLEAN_SHM_FRAME + 7u);
            uint16_t ra_word2 = shm_read16(CLEAN_SHM_FRAME + 8u);
            uint32_t frame_type = (fc_word >> 2u) & 3u;
            if ((flags & CFG_TX_ENABLE) && !station_enabled && rxe_len >= 30u &&
                (frame_type == 0u || frame_type == 2u) &&
                ra_word0 == shm_read16(0x612u) &&
                ra_word1 == shm_read16(0x613u) &&
                ra_word2 == shm_read16(0x614u)) {
                uint16_t ta_w0 = shm_read16(CLEAN_SHM_FRAME + 9u);
                uint16_t ta_w1 = shm_read16(CLEAN_SHM_FRAME + 10u);
                uint16_t ta_w2 = shm_read16(CLEAN_SHM_FRAME + 11u);
                uint8_t ta[6] = {
                    (uint8_t)ta_w0, (uint8_t)(ta_w0 >> 8u),
                    (uint8_t)ta_w1, (uint8_t)(ta_w1 >> 8u),
                    (uint8_t)ta_w2, (uint8_t)(ta_w2 >> 8u)
                };
                d11_transmit_ack_frame(ta, channel, mac_en & ~2u);
                mmio_write32(D11_MACCONTROL, mac_en);
            }
            slot->frame_index = (uint16_t)(total + 1u);
            slot->rxe_len = rxe_len;
            slot->rxe_status1 = shm_read16(0x7a7u);
            slot->rxe_status2 = shm_read16(0x603u);
            slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER - rx_start) >> 16u);
            uint32_t copy_words = ((uint32_t)rxe_len + 3u) >> 1u;
            if (copy_words > CLEAN_RAW_BYTES / 2u) copy_words = CLEAN_RAW_BYTES / 2u;
            for (uint32_t w_idx = 0u; w_idx < copy_words; ++w_idx) {
                uint16_t w = shm_read16(CLEAN_SHM_FRAME + w_idx);
                slot->raw[2u * w_idx] = (uint8_t)w;
                slot->raw[2u * w_idx + 1u] = (uint8_t)(w >> 8u);
            }
            for (uint32_t b_idx = 2u * copy_words; b_idx < CLEAN_RAW_BYTES; ++b_idx)
                slot->raw[b_idx] = 0u;
            slot->dma_rx_len = 0u;
            slot->dma_completed = 0u;
            slot->f2_received = 0u;
            slot->f2_sdpcm_len = 0u;
            (void)rx_dma_poll_and_reclaim(slot);
            sdio_f2_send_frame(slot, channel);
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            MULTI->captured_frames = total + 1u;
            if (!scanner_mode && total + 1u >= target_frames)
                break;
            shm_write16(0x601u, 0u);
        }
    }

    MBOX->rx_elapsed_ticks = ARM_CYCLE_COUNTER - rx_start;
    MBOX->rx_polls = polls;

    mmio_write32(D11_MACCONTROL, 0x04020400u);
    (void)mmio_read32(D11_MACCONTROL);
    phy_tx_set_inhibit(1);
    MULTI->dma_status0 = mmio_read32(D11_RX_DMA_STATUS0);
    MULTI->dma_status1 = mmio_read32(D11_RX_DMA_STATUS1);
    (void)delay_ticks(20000u);
    rx_dma_stop();

    sample_rxe8(MBOX->rxe_post);
    for (uint32_t i = 0u; i < 16u; ++i)
        MBOX->psm_mailbox[i] = shm_read16(0x600u + i);
    for (uint32_t i = 0u; i < 16u; ++i) {
        MBOX->psm_diag[i] = shm_read16(0x7a0u + i);
        if (i < CLEAN_RX_FAULT_WORDS) MBOX->rx_fifo_fault[i] = shm_read16(0x620u + i);
    }
    if (MULTI->captured_frames > 0u) {
        for (uint32_t i = 0u; i < 96u; ++i)
            MBOX->rx_snapshot[i] = MULTI->frames[0].raw[i];
        MBOX->psm_mailbox[1] = PSM_STATE_CAPTURED;
        MBOX->psm_mailbox[2] = 0u;
    }
    if (tx_diag.magic == CLEAN_TX_DIAG_MAGIC) {
        publish_tx_diag();
    } else
        for (uint32_t i = 0u; i < 90u; ++i) {
            uint16_t w = shm_read16(CLEAN_SHM_FRAME + i);
            MBOX->rx_input[2u * i] = (uint8_t)w;
            MBOX->rx_input[2u * i + 1u] = (uint8_t)(w >> 8u);
        }
    MULTI->discard_bad_fcs  = MBOX->psm_diag[8];
    MULTI->discard_oversize = MBOX->psm_diag[9];
    MULTI->discard_rx_event = MBOX->psm_diag[10];
    MULTI->discard_plcp     = MBOX->psm_diag[11];

    MBOX->fw_state = MBOX->fw_error ? FW_STATE_ERROR : FW_STATE_RX_DONE;
    __asm__ volatile("dsb" ::: "memory");
}
