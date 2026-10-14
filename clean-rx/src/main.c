#include "cyw43_hw.h"

#define MBOX  ((volatile fw_mbox_t *)FW_MBOX_ADDR)
#define MULTI ((volatile fw_multi_t *)FW_MULTI_ADDR)

static uint32_t dma_consumer;
static uint32_t dma_producer;
static uint32_t sdio_tx_idx;
static uint8_t  sdpcm_seq;

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
        mmio_write16(0x18001402u, 0x06a0u);
        mmio_write16(0x18001450u, 0x4e01u);
        mmio_write16(0x18001452u, 0x015bu);
        mmio_write16(0x180014e4u, 0x0090u);
        mmio_write16(0x18001404u, 0x0200u);
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
        mmio_write16(D11_MACHWCAP, 0x01ddu);
        mmio_write16(D11_MACHWCAP + 2u, 0x01ddu);
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
static void sdio_f2_init(void) {
    mmio_write32(SDIOD_TX_DMA_CTL, 0u);
    for (uint32_t i = 0u; i < 200u; ++i) {
        if ((mmio_read32(SDIOD_TX_DMA_STATUS0) & 0xf0000000u) == 0u) break;
    }
    volatile uint32_t *ring = (volatile uint32_t *)FW_SDIO_TX_RING_ADDR;
    for (uint32_t i = 0u; i < 32u * 4u; ++i)
        ring[i] = 0u;
    __asm__ volatile("dsb" ::: "memory");

    mmio_write32(SDIOD_TX_DMA_ADDRLOW, FW_SDIO_TX_RING_ADDR);
    mmio_write32(SDIOD_TX_DMA_ADDRHIGH, 0u);
    mmio_write32(SDIOD_TX_DMA_CTL, 0x000a0801u);
    mmio_write32(SDIOD_TX_DMA_PTR, FW_SDIO_TX_RING_ADDR);
    sdio_tx_idx = 0u;
}

static void sdio_f2_send_frame(const volatile fw_frame_slot_t *slot) {
    uint32_t rxe_len = slot->rxe_len;
    if (rxe_len < 14u || rxe_len > 510u) return;
    uint32_t mpdu_len = rxe_len - 6u;
    uint32_t sdpcm_len = 18u + mpdu_len;
    uint32_t dma_len = (sdpcm_len + 3u) & ~3u;

    if (sdio_tx_idx >= 31u) {
        for (uint32_t wait = 0u; wait < 2000u; ++wait) {
            uint32_t cur = (mmio_read32(SDIOD_TX_DMA_STATUS0) & 0x1fffu) >> 4u;
            if (cur == sdio_tx_idx) break;
        }
        sdio_f2_init();
    }

    uint32_t idx = sdio_tx_idx;
    uint32_t buf_addr = FW_SDIO_TX_BUF_ADDR + idx * FW_SDIO_TX_BUF_BYTES;
    volatile uint8_t *pkt = (volatile uint8_t *)(uintptr_t)buf_addr;
    for (uint32_t i = 0u; i < dma_len; ++i) pkt[i] = 0u;

    /* 12-byte SDPCM header + 2-byte pad */
    pkt[0] = (uint8_t)sdpcm_len;
    pkt[1] = (uint8_t)(sdpcm_len >> 8u);
    pkt[2] = (uint8_t)~sdpcm_len;
    pkt[3] = (uint8_t)(~sdpcm_len >> 8u);
    pkt[4] = sdpcm_seq++;
    pkt[5] = 2u;  /* Channel 2 = Data */
    pkt[6] = 0u;
    pkt[7] = 14u; /* Data offset = 14 */
    pkt[8] = 0u;
    pkt[9] = 8u;  /* Credit */
    pkt[10] = 0u;
    pkt[11] = 0u;

    /* 4-byte BDC header at offset 14 */
    pkt[14] = 0x20u;
    pkt[15] = slot->raw[2]; /* PLCP[0] rate/SIGNAL byte */
    uint8_t is_ofdm = (uint8_t)((slot->dma_hdr[4] & 1u) || ((slot->rxe_status2 >> 9u) & 3u));
    pkt[16] = (uint8_t)((shm_read16(0x609u) & 0xfu) | (is_ofdm ? 0x10u : 0u));
    pkt[17] = 0u;

    for (uint32_t i = 0u; i < mpdu_len; ++i)
        pkt[18u + i] = slot->raw[8u + i];
    __asm__ volatile("dsb" ::: "memory");

    volatile uint32_t *ring = (volatile uint32_t *)FW_SDIO_TX_RING_ADDR;
    ring[4u * idx + 0u] = 0xc0000000u;
    ring[4u * idx + 1u] = dma_len & 0x7fffu;
    ring[4u * idx + 2u] = buf_addr;
    ring[4u * idx + 3u] = 0u;
    __asm__ volatile("dsb" ::: "memory");

    sdio_tx_idx = idx + 1u;
    mmio_write32(SDIOD_TX_DMA_PTR, FW_SDIO_TX_RING_ADDR + sdio_tx_idx * 16u);
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
    if (total_len < 16u || total_len > 512u) return 0;
    const volatile uint8_t *tx_buf = (const volatile uint8_t *)FW_TX_BUF_ADDR;

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
    mmio_write16(0x18001514u, 0x1a02u);
    mmio_write16(0x18001508u, 0x0000u);
    mmio_write16(0x1800150au, 0x0000u);
    mmio_write16(0x1800154cu, 0x0000u);
    mmio_write16(0x1800156au, (uint16_t)total_len);
    mmio_write16(0x18001568u, 0x0826u);
    mmio_write16(0x18001500u, 0x0000u);
    MBOX->rc_dacbuf = 0x55550004u;
    mmio_write16(0x18001502u, 0x00d0u);
    MBOX->rc_dacbuf = 0x55550005u;
    (void)mmio_read16(0x18001502u);

    uint16_t st_50e_seen = 0u, st_690_seen = 0u;
    for (uint32_t i = 0u; i < 2000u; ++i) {
        uint16_t v = mmio_read16(0x1800150eu);
        st_50e_seen |= v;
        if ((v & 0x0080u) == 0u) break;
        (void)delay_ticks(180u);
    }
    for (uint32_t i = 0u; i < 2000u; ++i) {
        uint16_t v = mmio_read16(0x1800150eu);
        st_50e_seen |= v;
        if ((v & 0x0400u) != 0u) break;
        (void)delay_ticks(180u);
    }
    for (uint32_t i = 0u; i < 2000u; ++i) {
        uint16_t v = mmio_read16(0x18001690u);
        st_690_seen |= v;
        if ((v & 0x0100u) == 0u) break;
        (void)delay_ticks(180u);
    }
    MBOX->vco_status = ((uint32_t)st_50e_seen << 16u) | st_690_seen;

    mmio_write16(0x18001502u, 0x0000u);
    mmio_write16(0x18001568u, 0x0000u);
    mmio_write16(0x180017c0u, 0x8300u);
    mmio_write16(0x180017d0u, 0x0300u);

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    phy_tx_set_inhibit(1);
    MBOX->rc_dacbuf = 0x55550000u | total_len;
    return 1;
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

    shm_write16(0x60cu, (uint16_t)generation);
    shm_write16(0x60du, (uint16_t)(generation >> 16u));
    shm_write16(0x610u, (uint16_t)generation);
    shm_write16(0x611u, (uint16_t)(generation >> 16u));

    static const uint8_t start_bits[4] = {0u, 4u, 6u, 2u};
    for (uint32_t i = 0u; i < 4u; ++i) {
        mmio_write32(D11_MACCONTROL, 0x84020400u | start_bits[i]);
        (void)mmio_read32(D11_MACCONTROL);
    }
    uint32_t ready = 0u;
    for (uint32_t poll = 0u; poll < 2000u; ++poll) {
        uint16_t st = shm_read16(0x601u);
        if (st == PSM_STATE_READY) {
            ready = 1u;
            break;
        }
        if (st == PSM_STATE_FAULT) {
            return fail(0x71u, shm_read16(0x602u), st);
        }
        (void)delay_ticks(500u);
    }
    if (!ready)
        return fail(0x72u, 0x601u, shm_read16(0x601u));

    mmio_write32(D11_MACCONTROL, 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    shm_write16(0x609u, (uint16_t)channel);
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
    MBOX->fw_state = FW_STATE_RX_ACTIVE;
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
    sdio_f2_init();

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

    uint32_t rx_start = ARM_CYCLE_COUNTER;
    uint32_t polls = 0u;
    for (; scanner_mode || polls < 1800000u; ++polls) {
        uint32_t cmd = MBOX->host_cmd;
        if (cmd == 3u)
            break;
        if ((cmd & 0xffu) == 4u) {
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
            uint32_t tx_len  = (cmd >> 8u) & 0x03ffu;
            uint32_t is_ofdm = (cmd >> 18u) & 1u;
            uint32_t tx_cnt  = (cmd >> 19u) & 0xffu;
            if (tx_cnt == 0u) tx_cnt = 1u;
            mmio_write32(D11_MACCONTROL, 0x84020400u);
            (void)mmio_read32(D11_MACCONTROL);
            (void)delay_ticks(2000u);
            for (uint32_t t = 0u; t < tx_cnt; ++t) {
                (void)d11_transmit_template_frame(tx_len, is_ofdm, channel, mac_en & ~2u);
                if (t + 1u < tx_cnt)
                    (void)delay_ticks(180u * 1500u);
            }
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT)
                shm_write16(0x601u, 0u);
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 6u) {
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
            for (uint32_t poll = 0u; poll < 2000u; ++poll) {
                if (shm_read16(0x601u) == PSM_STATE_READY) break;
                (void)delay_ticks(200u);
            }
            shm_write16(0x609u, (uint16_t)channel);
            shm_write16(0x615u, saved_ccmp_en);
            for (uint32_t k = 0u; k < 8u; ++k) shm_write16(0x618u + k, saved_tk[k]);
            mmio_write32(D11_MACCONTROL, mac_en);
            continue;
        }
        if (st == PSM_STATE_CAPTURED) {
            uint32_t total = MULTI->captured_frames;
            uint32_t idx = total & (FW_MAX_FRAMES - 1u);
            volatile fw_frame_slot_t *slot = &MULTI->frames[idx];
            uint16_t rxe_len = shm_read16(0x606u);
            uint16_t fc_word  = shm_read16(0x6a4u);
            uint16_t ra_word0 = shm_read16(0x6a6u);
            uint16_t ra_word1 = shm_read16(0x6a7u);
            uint16_t ra_word2 = shm_read16(0x6a8u);
            uint32_t frame_type = (fc_word >> 2u) & 3u;
            if ((flags & CFG_TX_ENABLE) && rxe_len >= 30u &&
                (frame_type == 0u || frame_type == 2u) &&
                ra_word0 == shm_read16(0x612u) &&
                ra_word1 == shm_read16(0x613u) &&
                ra_word2 == shm_read16(0x614u)) {
                uint16_t ta_w0 = shm_read16(0x6a9u);
                uint16_t ta_w1 = shm_read16(0x6aau);
                uint16_t ta_w2 = shm_read16(0x6abu);
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
            if (copy_words > 256u) copy_words = 256u;
            for (uint32_t w_idx = 0u; w_idx < copy_words; ++w_idx) {
                uint16_t w = shm_read16(0x6a0u + w_idx);
                slot->raw[2u * w_idx] = (uint8_t)w;
                slot->raw[2u * w_idx + 1u] = (uint8_t)(w >> 8u);
            }
            for (uint32_t b_idx = 2u * copy_words; b_idx < 512u; ++b_idx)
                slot->raw[b_idx] = 0u;
            slot->dma_rx_len = 0u;
            slot->dma_completed = 0u;
            slot->f2_received = 0u;
            slot->f2_sdpcm_len = 0u;
            (void)rx_dma_poll_and_reclaim(slot);
            sdio_f2_send_frame(slot);
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
    MULTI->dma_status0 = mmio_read32(D11_RX_DMA_STATUS0);
    MULTI->dma_status1 = mmio_read32(D11_RX_DMA_STATUS1);
    (void)delay_ticks(20000u);
    rx_dma_stop();

    sample_rxe8(MBOX->rxe_post);
    for (uint32_t i = 0u; i < 16u; ++i)
        MBOX->psm_mailbox[i] = shm_read16(0x600u + i);
    for (uint32_t i = 0u; i < 16u; ++i)
        MBOX->psm_diag[i] = shm_read16(0x7a0u + i);
    if (MULTI->captured_frames > 0u) {
        for (uint32_t i = 0u; i < 96u; ++i)
            MBOX->rx_snapshot[i] = MULTI->frames[0].raw[i];
        MBOX->psm_mailbox[1] = PSM_STATE_CAPTURED;
        MBOX->psm_mailbox[2] = 0u;
    }
    for (uint32_t i = 0u; i < 90u; ++i) {
        uint16_t w = shm_read16(0x6a0u + i);
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
