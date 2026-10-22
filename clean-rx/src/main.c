#include "cyw43_hw.h"
#include "sdio_ring.h"
#include "tx_diag.h"
#include "phy_rates.h"
#include "unicast.h"
#include "mac.h"
#include "ht20.h"
#include "ht_observer.h"
#include "observer_dma.h"
#include "early_rx.h"
#include "../../src/shared/bytes.h"
#ifdef CLEAN_SDIO_SELF_TEST
#include "sdio_test.h"
#endif

#define MBOX  ((volatile fw_mbox_t *)FW_MBOX_ADDR)
#define MULTI ((volatile fw_multi_t *)FW_MULTI_ADDR)
#define UNICAST ((volatile clean_unicast_status_t *)CLEAN_UNICAST_ADDR)
#define MAC_RADIO ((volatile clean_mac_radio_status_t *)CLEAN_MAC_RADIO_ADDR)
#define MAC_TRACE ((volatile clean_mac_trace_status_t *)CLEAN_MAC_TRACE_ADDR)
#define DCF_TRACE ((volatile clean_dcf_trace_t *)CLEAN_DCF_TRACE_ADDR)
#define TX_COMPLETION ((volatile clean_tx_completion_status_t *)CLEAN_TX_COMPLETION_ADDR)
#define HT_RX ((volatile clean_ht_rx_t *)CLEAN_HT_RX_ADDR)
#define HT_DIAG ((volatile clean_ht_diag_t *)CLEAN_HT_DIAG_ADDR)
#if CLEAN_RX_HT_WINDOW
static uint16_t observer_restart_attempts;
#endif
#if CLEAN_RX_HT_REARM_WAIT
_Static_assert(CLEAN_AUTH_FAILURE_ADDR+sizeof(clean_auth_failure_t) <= CLEAN_OBSERVER_REARM_ADDR &&
               CLEAN_OBSERVER_REARM_ADDR+2u*CLEAN_OBSERVER_REARM_WORDS <= 0x20d80u &&
               CLEAN_OBSERVER_REARM_SHM >= 0x7b0u &&
               CLEAN_OBSERVER_REARM_SHM+CLEAN_OBSERVER_REARM_WORDS <= 0x7f4u,
               "Rearm decision trace must not overlap diagnostics or capture storage");
#endif

_Static_assert(CLEAN_RX_LAYOUT_ADDR+4u*CLEAN_RX_LAYOUT_WORDS <= CLEAN_OBSERVER_PHY_ADDR &&
               CLEAN_OBSERVER_PHY_ADDR+CLEAN_OBSERVER_PHY_BYTES <= CLEAN_HT_FAILURE_ADDR,
               "Stopped PHY snapshot must fit the unused layout/failure gap");
_Static_assert(CLEAN_HT_EARLY_WORDS == 10u &&
               4u+2u*(CLEAN_HT_EARLY_WORDS+4u) == CLEAN_OBSERVER_PHY_BYTES,
               "Receive-window publication must fit the shared diagnostic gap");
#if CLEAN_RX_HT_PHY_SNAPSHOT
static void snapshot_observer_phy(uint32_t phase) {
    static const uint16_t indices[CLEAN_OBSERVER_PHY_REGS] = CLEAN_OBSERVER_PHY_INDICES;
    volatile uint32_t *header = (volatile uint32_t *)CLEAN_OBSERVER_PHY_ADDR;
    volatile uint16_t *values = (volatile uint16_t *)(CLEAN_OBSERVER_PHY_ADDR+8u);
    if (!phase) header[0] = header[1] = 0u;
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_PHY_REGS; ++i)
        values[phase*CLEAN_OBSERVER_PHY_REGS+i] = phy_reg_read(indices[i]);
    header[1] |= 1u<<phase;
    if (phase) header[0] = CLEAN_OBSERVER_PHY_MAGIC;
}
#endif

_Static_assert(CLEAN_MAC_RADIO_ADDR >= CLEAN_UNICAST_ADDR + sizeof(clean_unicast_status_t) &&
               CLEAN_MAC_RADIO_ADDR + sizeof(clean_mac_radio_status_t) <= FW_MULTI_ADDR,
               "MAC radio status does not overlap transport or captures");
_Static_assert(CLEAN_DCF_MAX_SLOTS == CLEAN_MAC_CW_MAX, "PSM/software contention bound");
_Static_assert(FW_MAX_FRAMES > 0u && FW_MAX_FRAMES <= 16u &&
               (FW_MAX_FRAMES & (FW_MAX_FRAMES - 1u)) == 0u,
               "Capture FIFO must fit credit validation and power-of-two indexing");
_Static_assert(CLEAN_MAC_TRACE_ADDR >= CLEAN_MAC_RADIO_ADDR + sizeof(clean_mac_radio_status_t) &&
               CLEAN_MAC_TRACE_ADDR + sizeof(clean_mac_trace_status_t) <= FW_MULTI_ADDR,
               "MAC trace does not overlap radio status or captures");

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
static uint8_t station_mac, station_hw, nav_sampled;
static clean_dcf_t station_legacy_dcf;
static clean_edca_t station_edca;
static clean_edca_ac_t policy_edca[4];
static uint8_t policy_edca_pending;
static clean_dcf_t *station_contention(void) {
    return station_edca.enabled ? &station_edca.ac[station_edca.selected] : &station_legacy_dcf;
}
#define station_dcf (*station_contention())
static uint16_t rx_discard_fcs, rx_discard_length;
static uint8_t nav_frame[CLEAN_MPDU_BYTES], station_address[6], station_peer[6];
static uint32_t access_started;
static uint32_t ack_started, ack_window;
static uint32_t cck_ack_window;
static clean_tx_tail_t cck_tx_tail;
static uint32_t last_tx_identity;
static uint8_t last_tx_valid;
static uint8_t capture_prepared;
static uint16_t ack_test_configured, ack_test_epoch;
static uint32_t ack_expiry_at, ack_expiry_started, ack_expiry_window;
static uint32_t rx_published_frames;
#if CLEAN_RX_HT_OBSERVER_FIFO
static uint16_t observer_capture_ids[FW_MAX_FRAMES];
#endif
static uint8_t policy_pending, policy_short_slot, policy_response;
static uint16_t policy_basic;
static uint32_t policy_command;
static uint16_t cts_sampled, short_sampled, ack_sampled;
static uint16_t kinds_sampled[4];
static clean_tx_diag_t tx_diag;
static void station_publish_mac(void);

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

#if CLEAN_RX_HT_COPY_DMA_PROBE
static void observer_dma_sample(const volatile uint8_t *buf, uint32_t index,
    uint32_t status0, uint32_t status1, uint32_t frozen) {
    uint16_t owner = shm_read16(CLEAN_OBSERVER_COPY_SHM);
    uint16_t epoch = shm_read16(CLEAN_OBSERVER_COPY_SHM+1u);
    if (!owner || owner != shm_read16(CLEAN_OBSERVER_EARLY_SHM) ||
        epoch != shm_read16(CLEAN_OBSERVER_EARLY_SHM+1u) ||
        shm_read16(CLEAN_OBSERVER_COPY_SHM+2u) != 3u) return;
    uint8_t nonce[14];
    for (uint32_t i = 0u; i < 14u; i += 2u) {
        uint16_t word = shm_read16(CLEAN_OBSERVER_EARLY_SHM+
            CLEAN_OBSERVER_EARLY_HEADER+13u+i/2u);
        nonce[i] = (uint8_t)word;
        nonce[i+1u] = (uint8_t)(word>>8u);
    }
    const uint32_t context[7] = {MULTI->dma_frames,index,dma_producer,status0,status1,
        (uint32_t)owner | ((uint32_t)epoch<<16u),shm_read16(0x601u) | frozen};
    clean_observer_dma_record((volatile uint32_t *)CLEAN_OBSERVER_DMA_ADDR,
        buf,nonce,context);
}
#endif

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
#if CLEAN_RX_HT_COPY_DMA_PROBE
    clean_observer_dma_init((volatile uint32_t *)CLEAN_OBSERVER_DMA_ADDR);
#endif
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
#if CLEAN_RX_HT_COPY_DMA_PROBE
    _Static_assert(CLEAN_OBSERVER_DMA_PREFIX <= FW_D11_DMA_BUF_BYTES,
        "Completed DMA prefix must fit a descriptor buffer");
    observer_dma_sample(buf,idx,st0,st1,0u);
#endif
    if (slot) {
        slot->dma_rx_len = dma_len;
        slot->dma_completed = 1u;
        for (uint32_t i = 0u; i < 38u; ++i)
            slot->dma_hdr[i] = buf[i];
    }

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

static int sdio_f2_send_frame(const volatile fw_frame_slot_t *slot, uint32_t channel) {
    uint32_t rxe_len = slot->rxe_len;
    if (rxe_len < 14u || rxe_len > CLEAN_RXE_BYTES)
        return fail(0x85u, FW_MULTI_ADDR, rxe_len);
    uint32_t mpdu_len = rxe_len - 6u;
    uint32_t sdpcm_len = 18u + mpdu_len;
    uint32_t fragments = sdpcm_len > CLEAN_F2_PACKET_BYTES ?
        (mpdu_len + CLEAN_F2_FRAGMENT_BYTES - 1u) / CLEAN_F2_FRAGMENT_BYTES : 1u;

    int credit = clean_sdio_credit_available(sdio_tx_posted, MBOX->f2_consumed,
                                              SDIO_TX_WINDOW);
    if (credit < 0) {
        (void)fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
        return 0;
    }
    if (!credit) {
        if (!station_enabled && !CLEAN_RX_HT_DATA && !CLEAN_RX_HT_OBSERVER_FIFO)
            MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 1u);
        return 0;
    }
    uint32_t status = mmio_read32(SDIOD_TX_DMA_STATUS0);
    for (uint32_t part = 0u; part < fragments; ++part) {
        int available = clean_sdio_available((sdio_tx_idx + part) & 31u, status);
        if (available < 0) {
            (void)fail(0x86u, SDIOD_TX_DMA_STATUS0, status);
            return 0;
        }
        if (!available) {
            if (!station_enabled && !CLEAN_RX_HT_DATA && !CLEAN_RX_HT_OBSERVER_FIFO)
                MBOX->f2_counts = clean_sdio_count(MBOX->f2_counts, 1u);
            return 0;
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
#if CLEAN_RX_HT_OBSERVER
#if CLEAN_RX_HT_OBSERVER_FIFO
        uint16_t correlation = observer_capture_ids[
            (slot->frame_index-1u)&(FW_MAX_FRAMES-1u)];
#else
        /* Passive diagnostic ID; ordinary profiles keep reserved bytes zero. */
        uint16_t correlation = shm_read16(CLEAN_OBSERVER_HELD);
#if CLEAN_RX_HT_ADVANCE
        if (correlation && correlation == shm_read16(CLEAN_OBSERVER_EARLY_SHM) &&
            shm_read16(CLEAN_OBSERVER_ADVANCE) == 1u)
            correlation |= CLEAN_OBSERVER_ADVANCE_TAG;
#endif
#endif
        pkt[10] = (uint8_t)correlation;
        pkt[11] = (uint8_t)(correlation >> 8u);
#endif
        pkt[12] = (uint8_t)CLEAN_RX_PHASE_MAGIC | (uint8_t)(rx_after_explicit_tx << 7u);
        pkt[13] = (uint8_t)(CLEAN_RX_PHASE_MAGIC >> 8u);

        /* 4-byte BDC header at offset 14 */
        pkt[14] = fragments > 1u ? 0x21u : 0x20u;
        pkt[15] = slot->raw[2]; /* PLCP[0] rate/SIGNAL byte */
        pkt[16] = clean_rx_phy_metadata(slot->phy_status0, slot->rxe_status2, channel);
        pkt[17] = clean_ht_rx_version(CLEAN_RX_HT_DATA, slot->phy_status0,
                                      slot->raw + 2u, mpdu_len);
        if (pkt[17] == CLEAN_HT_AMPDU_PROBE_VERSION)
            pkt[16] = CLEAN_RX_HT20 | CLEAN_RX_UNSUPPORTED_PHY | (uint8_t)channel;
        else if (pkt[17]) pkt[16] |= CLEAN_RX_HT20;
#if CLEAN_RX_HT_EARLY_FIFO
        /* Early HT-SIG admission precedes the terminal PHY-status header. */
        if (correlation&CLEAN_OBSERVER_EARLY_TAG)
            pkt[16] = CLEAN_RX_UNSUPPORTED_PHY|(uint8_t)channel;
#endif

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
    return 1;
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

static uint32_t mac_now(void) {
    return mmio_read32(D11_BASE + (station_hw ? 0x180u : 0x1cu));
}

static void station_tx_phase(uint32_t index) {
    volatile clean_tx_error_status_t *s =
        (volatile clean_tx_error_status_t *)CLEAN_TX_ERROR_ADDR;
    volatile clean_tx_phase_t *p = &s->phase[index];
    p->at = mac_now();
    p->error = mmio_read16(D11_BASE + 0x3eeu);
    p->txe = mmio_read16(D11_BASE + 0x50eu);
    p->ifs = mmio_read16(D11_BASE + 0x690u);
    p->phyctl = mmio_read16(D11_BASE + 0x50cu);
    p->phyctl1 = mmio_read16(D11_BASE + 0x514u);
    p->bytes = mmio_read16(D11_BASE + 0x56au);
    p->select = mmio_read16(D11_BASE + 0x568u);
    p->owners = shm_read16(CLEAN_DCF_REQUEST) |
        (shm_read16(CLEAN_DCF_TX_PENDING) << 4u) |
        (shm_read16(CLEAN_RESPONSE_PENDING) << 8u) |
        (shm_read16(CLEAN_TX_CLEANUP_REQUEST) << 12u);
    s->valid |= 1u << index;
}

static void station_bad_rx(uint32_t now) {
    if (!station_edca.enabled) clean_dcf_bad_rx(&station_legacy_dcf, now);
    else for (uint32_t i = 0u; i < 4u; ++i) clean_dcf_bad_rx(&station_edca.ac[i], now);
}

static void station_good_rx(void) {
    if (!station_edca.enabled) clean_dcf_good_rx(&station_legacy_dcf);
    else for (uint32_t i = 0u; i < 4u; ++i) clean_dcf_good_rx(&station_edca.ac[i]);
}

static void station_observe_errors(uint32_t now) {
    uint16_t fcs = shm_read16(0x7a8u), length = shm_read16(0x7a9u);
    if (fcs != rx_discard_fcs || length != rx_discard_length)
        station_bad_rx(now);
    rx_discard_fcs = fcs;
    rx_discard_length = length;
}

static int station_rx_idle(uint16_t state) {
    return state == PSM_STATE_READY || state == PSM_STATE_WAIT_RX;
}

static int station_phy_busy(void) {
    return shm_read16(CLEAN_RESPONSE_PENDING) ||
           shm_read16(CLEAN_TX_CLEANUP_REQUEST) ||
           (mmio_read32(D11_BASE + 0x158u) & 7u) ||
           (mmio_read16(D11_BASE + 0x690u) & 0xf00u);
}

static void station_hw_backoff(void) {
    if (!station_hw) return;
    uint32_t control = mmio_read32(D11_MACCONTROL);
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    shm_write16(CLEAN_DCF_TRACE_SHM, station_dcf.slots);
    shm_write16(CLEAN_DCF_SLOTS, station_dcf.slots);
    shm_write16(CLEAN_DCF_IDLE_VALID, 0u);
    mmio_write32(D11_MACCONTROL, control);
    (void)mmio_read32(D11_MACCONTROL);
}

static void station_hw_slot(void) {
    if (!station_hw) return;
    mmio_write16(D11_BASE + 0x684u, (uint16_t)(0x1feu + station_dcf.slot_us));
    shm_write16(CLEAN_DCF_SLOT_US, station_dcf.slot_us);
    shm_write16(CLEAN_DCF_DIFS_US, (uint16_t)(10u + station_dcf.aifsn * station_dcf.slot_us));
    shm_write16(CLEAN_DCF_EIFS_US, 314u);
}

static uint16_t station_response_read(void *context, uint32_t word) {
    (void)context;
    return shm_read16(word);
}

static void station_response_write(void *context, uint32_t word, uint16_t value) {
    (void)context;
    shm_write16(word, value);
}

static int station_response_rates(uint16_t basic) {
    clean_response_io_t io = {.read = station_response_read, .write = station_response_write,
                              .context = 0};
    if (clean_response_rates_install(&io, basic, CLEAN_RX_HT_RESPONSE)) return 1;
    return fail(0x85u, io.failed_word ? io.failed_word : FW_TX_BUF_ADDR, io.actual);
}

static int station_policy_boundary(uint16_t state) {
    return station_rx_idle(state) || state == PSM_STATE_CAPTURED;
}

static int station_policy_word(uint32_t word, uint16_t expected) {
    uint16_t actual = shm_read16(word);
    return actual == expected ? 1 : fail(0x85u, word, actual);
}

static int station_ack_test_configure(uint16_t count) {
    ack_test_configured = count;
    ++ack_test_epoch;
    ack_expiry_at = ack_expiry_started = ack_expiry_window = 0u;
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        shm_write16(CLEAN_ACK_OBSERVE_SHM + i, 0u);
    for (uint32_t i = 0u; i < 3u; ++i)
        shm_write16(CLEAN_ACK_ARITH_SHM + i, 0u);
    for (uint32_t i = 0u; i < CLEAN_ACK_TEST_WORDS; ++i)
        shm_write16(CLEAN_ACK_TEST_SHM + i, 0u);
    shm_write16(CLEAN_TEST_ACK_REMAINING, count);
    shm_write16(CLEAN_TEST_ACK_DROPPED, 0u);
    shm_write16(CLEAN_ACK_TEST_LIMIT, count);
    shm_write16(CLEAN_ACK_TEST_EPOCH, ack_test_epoch);
    for (uint32_t i = 0u; i < CLEAN_ACK_TEST_WORDS; ++i)
        if (!station_policy_word(CLEAN_ACK_TEST_SHM + i,
                i == 0u ? count : i == 9u ? ack_test_epoch : 0u)) return 0;
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        if (!station_policy_word(CLEAN_ACK_OBSERVE_SHM + i, 0u)) return 0;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!station_policy_word(CLEAN_ACK_ARITH_SHM + i, 0u)) return 0;
    return station_policy_word(CLEAN_TEST_ACK_REMAINING, count) &&
        station_policy_word(CLEAN_TEST_ACK_DROPPED, 0u);
}

static void snapshot_ack_test(void) {
    volatile clean_ack_test_status_t *s =
        (volatile clean_ack_test_status_t *)CLEAN_ACK_TEST_ADDR;
    s->magic = 0u;
    s->configured = ack_test_configured;
    s->limit = shm_read16(CLEAN_ACK_TEST_LIMIT);
    s->remaining = shm_read16(CLEAN_TEST_ACK_REMAINING);
    s->dropped = shm_read16(CLEAN_TEST_ACK_DROPPED);
    s->fault = shm_read16(CLEAN_ACK_TEST_FAULT);
    s->phase = shm_read16(CLEAN_ACK_TEST_PHASE);
    s->before = shm_read16(CLEAN_ACK_TEST_BEFORE);
    s->after = shm_read16(CLEAN_ACK_TEST_AFTER);
    s->dropped_before = shm_read16(CLEAN_ACK_TEST_DROPPED_BEFORE);
    s->clock = shm_read16(CLEAN_ACK_TEST_CLOCK);
    s->frame_control = shm_read16(CLEAN_ACK_TEST_FC);
    s->sequence = shm_read16(CLEAN_ACK_TEST_SEQUENCE);
    s->epoch = shm_read16(CLEAN_ACK_TEST_EPOCH);
    s->reserved = 0u;
    __asm__ volatile("dsb" ::: "memory");
    s->magic = CLEAN_ACK_TEST_MAGIC;
    volatile clean_ack_observe_t *o =
        (volatile clean_ack_observe_t *)CLEAN_ACK_OBSERVE_ADDR;
    o->magic = 0u;
    volatile uint16_t *words = (volatile uint16_t *)(CLEAN_ACK_OBSERVE_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_ACK_OBSERVE_WORDS; ++i)
        words[i] = shm_read16(CLEAN_ACK_OBSERVE_SHM + i);
    o->arm_at = ack_expiry_at;
    o->arm_started = ack_expiry_started;
    o->arm_window = ack_expiry_window;
    __asm__ volatile("dsb" ::: "memory");
    o->magic = CLEAN_ACK_OBSERVE_MAGIC;
    volatile clean_ack_arith_t *a = (volatile clean_ack_arith_t *)CLEAN_ACK_ARITH_ADDR;
    a->magic = 0u;
    a->airtime = shm_read16(CLEAN_ACK_TEST_AIRTIME);
    a->sum = shm_read16(CLEAN_ACK_TEST_SUM);
    a->limit = shm_read16(CLEAN_ACK_TEST_LIMIT_SEEN);
    a->reserved = 0u;
    __asm__ volatile("dsb" ::: "memory");
    a->magic = CLEAN_ACK_ARITH_MAGIC;
}

static void station_apply_policy(uint32_t mac_en) {
    if (!policy_pending || !station_policy_boundary(shm_read16(0x601u))) return;
    uint32_t now = mac_now();
    clean_edca_t next_edca;
    badger_bytes_copy((uint8_t *)&next_edca, (const uint8_t *)&station_edca, sizeof(next_edca));
    clean_dcf_t next_legacy, previous;
    badger_bytes_copy((uint8_t *)&next_legacy, (const uint8_t *)&station_legacy_dcf,
                      sizeof(next_legacy));
    badger_bytes_copy((uint8_t *)&previous, (const uint8_t *)&station_dcf, sizeof(previous));
    if (policy_edca_pending) {
        if (!clean_edca_configure(&next_edca, policy_edca, now,
                                  previous.random, policy_short_slot)) {
            fail(0x85u, FW_TX_BUF_ADDR, policy_command);
            return;
        }
        for (uint32_t i = 0u; i < 4u; ++i) {
            clean_dcf_t *d = &next_edca.ac[i];
            uint32_t remaining = previous.nav_until - now;
            if (previous.nav_valid && remaining && remaining <= 32767u)
                clean_dcf_nav(d, now, (uint16_t)remaining);
            if (previous.ifs_us > 10u + previous.aifsn * previous.slot_us)
                clean_dcf_bad_rx(d, now);
        }
    } else if (next_edca.enabled) {
        for (uint32_t i = 0u; i < 4u; ++i)
            clean_dcf_slot(&next_edca.ac[i], now, policy_short_slot);
    } else clean_dcf_slot(&next_legacy, now, policy_short_slot);
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
    uint32_t control = mmio_read32(D11_MACCONTROL);
    if ((control & 3u) != (clean_mac_pause_psm(mac_en) & 3u)) {
        fail(0x85u, D11_MACCONTROL, control);
        return;
    }
    if (!station_policy_boundary(shm_read16(0x601u)) || station_phy_busy()) {
        mmio_write32(D11_MACCONTROL, mac_en);
        return;
    }
    if (station_hw && !policy_edca_pending) {
        uint16_t slots = shm_read16(CLEAN_DCF_SLOTS);
        if (slots > CLEAN_DCF_MAX_SLOTS) {
            fail(0x85u, CLEAN_DCF_SLOTS, slots);
            return;
        }
        if (next_edca.enabled) next_edca.ac[next_edca.selected].slots = slots;
        else next_legacy.slots = slots;
    }
    if (!station_response_rates(policy_basic)) return;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (!station_policy_word(0x612u + i, (uint16_t)(station_address[2u*i] |
                ((uint16_t)station_address[2u*i + 1u] << 8u))) ||
            !station_policy_word(CLEAN_ACK_PEER + i, (uint16_t)(station_peer[2u*i] |
                ((uint16_t)station_peer[2u*i + 1u] << 8u)))) return;
    badger_bytes_copy((uint8_t *)&station_edca, (const uint8_t *)&next_edca, sizeof(station_edca));
    badger_bytes_copy((uint8_t *)&station_legacy_dcf, (const uint8_t *)&next_legacy,
                      sizeof(station_legacy_dcf));
    station_hw_slot();
    station_hw_backoff();
    shm_write16(CLEAN_QOS_RESPONSE_POLICY, policy_response);
    if (!station_policy_word(CLEAN_QOS_RESPONSE_POLICY, policy_response)) return;
    if (station_hw &&
        (!station_policy_word(CLEAN_DCF_SLOT_US, station_dcf.slot_us) ||
         !station_policy_word(CLEAN_DCF_DIFS_US,
             (uint16_t)(10u + station_dcf.aifsn * station_dcf.slot_us)) ||
         !station_policy_word(CLEAN_DCF_EIFS_US, 314u) ||
         !station_policy_word(CLEAN_DCF_SLOTS, station_dcf.slots) ||
         !station_policy_word(CLEAN_DCF_IDLE_VALID, 0u))) return;
    if (station_hw) {
        uint16_t slot = mmio_read16(D11_BASE + 0x684u);
        if (slot != 0x1feu + station_dcf.slot_us) {
            fail(0x85u, D11_BASE + 0x684u, slot);
            return;
        }
    }
    policy_edca_pending = policy_pending = 0u;
    station_publish_mac();
    __asm__ volatile("dsb" ::: "memory");
    mmio_write32(D11_MACCONTROL, mac_en);
    control = mmio_read32(D11_MACCONTROL);
    if ((control & 3u) != (mac_en & 3u)) {
        fail(0x85u, D11_MACCONTROL, control);
        return;
    }
    MBOX->host_cmd = 2u;
    __asm__ volatile("dsb" ::: "memory");
}

static void snapshot_dcf_trace(void) {
    DCF_TRACE->draw = shm_read16(CLEAN_DCF_TRACE_SHM);
    DCF_TRACE->before = shm_read16(CLEAN_DCF_TRACE_SHM + 1u);
    DCF_TRACE->after = shm_read16(CLEAN_DCF_TRACE_SHM + 2u);
    DCF_TRACE->clock = shm_read16(CLEAN_DCF_TRACE_SHM + 3u);
    DCF_TRACE->fault = shm_read16(CLEAN_DCF_TRACE_SHM + 4u);
    DCF_TRACE->slots = shm_read16(CLEAN_DCF_SLOTS);
    DCF_TRACE->magic = CLEAN_DCF_TRACE_MAGIC;
}

static void station_publish_mac(void) {
    if (!station_mac) return;
    snapshot_ack_test();
    volatile clean_edca_status_t *edca = (volatile clean_edca_status_t *)CLEAN_EDCA_STATUS_ADDR;
    edca->magic = CLEAN_EDCA_STATUS_MAGIC;
    edca->cw_min = station_dcf.cw_min;
    edca->cw_max = station_dcf.cw_max;
    edca->aifs_us = station_hw ? shm_read16(CLEAN_DCF_DIFS_US) :
        (uint16_t)(10u + station_dcf.aifsn * station_dcf.slot_us);
    edca->txop_32us = station_edca.enabled ?
        station_edca.parameters[station_edca.selected].txop_32us : 0u;
    edca->ac = station_edca.selected;
    edca->tid = station_edca.tid;
    edca->enabled = station_edca.enabled;
    edca->reserved = 0u;
    MAC_RADIO->cw = station_dcf.cw;
    MAC_RADIO->slots = station_hw ? shm_read16(CLEAN_DCF_SLOTS) : station_dcf.slots;
    MAC_RADIO->nav_until = station_dcf.nav_valid ? station_dcf.nav_until : 0u;
    MAC_RADIO->pending_rx = shm_read16(0x601u) == PSM_STATE_CAPTURED ||
        MULTI->captured_frames != rx_published_frames;
    uint16_t cts = shm_read16(CLEAN_CTS_SENT), short_count = shm_read16(CLEAN_SHORT_SENT);
    uint16_t ack = shm_read16(CLEAN_ACK_CONSUMED);
    MAC_RADIO->control_acks += (uint16_t)(ack - ack_sampled);
    MAC_RADIO->cts_responses += (uint16_t)(cts - cts_sampled);
    MAC_RADIO->short_responses += (uint16_t)(short_count - short_sampled);
    cts_sampled = cts;
    short_sampled = short_count;
    ack_sampled = ack;
    for (uint32_t i = 0u; i < 4u; ++i) {
        uint16_t count = shm_read16(CLEAN_ACK_KINDS + i);
        MAC_TRACE->responses[i] += (uint16_t)(count - kinds_sampled[i]);
        kinds_sampled[i] = count;
    }
    MAC_RADIO->policy_pending = policy_pending;
    MAC_TRACE->ack_stage = shm_read16(CLEAN_ACK_STAGE);
    MAC_TRACE->ack_frame_control = shm_read16(CLEAN_ACK_FRAME_CONTROL);
    MAC_TRACE->ack_rx_status = shm_read16(CLEAN_ACK_RX_STATUS);
    MAC_TRACE->bad_fcs = shm_read16(0x7a8u);
    MAC_TRACE->bad_frame_control = shm_read16(CLEAN_BAD_FRAME_CONTROL);
    MAC_TRACE->bad_frame_length = shm_read16(CLEAN_BAD_FRAME_LENGTH);
    MAC_TRACE->ack_elapsed = shm_read16(CLEAN_ACK_ELAPSED);
    MAC_TRACE->ack_window = shm_read16(CLEAN_ACK_WINDOW);
    MAC_TRACE->response_delay = shm_read16(CLEAN_RESPONSE_DELAY);
    MAC_TRACE->ack_dropped = shm_read16(CLEAN_TEST_ACK_DROPPED);
    if (station_hw) {
        MAC_TRACE->ifs_status = mmio_read16(D11_BASE + 0x690u);
        MAC_TRACE->ifs_backoff = mmio_read16(D11_BASE + 0x68au);
        MAC_TRACE->ifs_idle = mmio_read16(D11_BASE + 0x68eu);
        MAC_TRACE->psm_ifs = shm_read16(CLEAN_DCF_IFS_SAMPLE);
        MAC_TRACE->psm_tsf = shm_read16(CLEAN_DCF_CLOCK_SAMPLE);
        MAC_TRACE->psm_polls = shm_read16(CLEAN_DCF_POLLS);
        snapshot_dcf_trace();
    }
    MAC_RADIO->address_lo = (uint32_t)station_address[0] |
        ((uint32_t)station_address[1] << 8u) |
        ((uint32_t)station_address[2] << 16u) |
        ((uint32_t)station_address[3] << 24u);
    MAC_RADIO->address_hi = (uint32_t)station_address[4] |
        ((uint32_t)station_address[5] << 8u);
}

static int station_counter_ok(void) {
    if (!station_hw || !shm_read16(CLEAN_DCF_TRACE_SHM + 4u)) return 1;
    station_publish_mac();
    return fail(0x8eu, CLEAN_DCF_SLOTS, shm_read16(CLEAN_DCF_TRACE_SHM + 1u));
}

static void station_consume_ack(void) {
    if (shm_read16(0x601u) != PSM_STATE_CAPTURED || shm_read16(0x606u) != 20u ||
        shm_read16(CLEAN_SHM_FRAME + 4u) != 0xd4u) return;
    for (uint32_t i = 0u; i < 3u; ++i)
        if (shm_read16(CLEAN_SHM_FRAME + 6u + i) !=
            ((uint16_t)station_address[2u*i] |
             ((uint16_t)station_address[2u*i + 1u] << 8u))) return;
    if (!capture_prepared) (void)rx_dma_poll_and_reclaim(0);
    else ++MAC_RADIO->prepared_acks;
    /* An ACK can complete between the first state sample and transport
     * preparation. Its staged bytes must not survive into the next capture. */
    capture_prepared = 0u;
    station_good_rx();
    ++MAC_RADIO->control_acks;
    shm_write16(0x601u, 0u);
    nav_sampled = 0u;
}

static void station_service_tx(void) {
    if (station_hw && shm_read16(CLEAN_DCF_REQUEST) == CLEAN_DCF_FIRED &&
        !cck_tx_tail.ended && !shm_read16(CLEAN_DCF_TX_PENDING))
        ack_started = (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
            ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u);
    uint32_t now = mac_now();
    if (cck_ack_window &&
        clean_tx_tail_poll(&cck_tx_tail, (mmio_read16(D11_BASE + 0x690u) & 0x100u) != 0u)) {
        volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
        tail[0] = now - ack_started;
        tail[1] = mmio_read16(D11_BASE + 0x506u);
        tail[2] = mmio_read16(D11_BASE + 0x54cu);
        tail[3] = mmio_read16(D11_BASE + 0x56au);
        /* The CCK TX-request edge precedes the physical tail by more than
         * PLCP airtime alone. Start its bounded ACK wait at observed TX end. */
        shm_write16(CLEAN_DCF_ACK_UNTIL, (uint16_t)(now + cck_ack_window));
        shm_write16(CLEAN_DCF_ACK_US, (uint16_t)(now - ack_started + cck_ack_window));
        ack_started = now;
        ack_window = cck_ack_window;
    }
    if (now - ack_started >= ack_window && shm_read16(CLEAN_ACK_WAITING)) {
        ack_expiry_at = now;
        ack_expiry_started = ack_started;
        ack_expiry_window = ack_window;
        shm_write16(CLEAN_ACK_CLEAR_NOW, (uint16_t)now);
        shm_write16(CLEAN_ACK_CLEAR_DUE, shm_read16(CLEAN_DCF_ACK_UNTIL));
        shm_write16(CLEAN_ACK_CLEAR_REASON, 1u);
        shm_write16(CLEAN_ACK_WAITING, 0u);
    }
    station_consume_ack();
}

static void station_observe_capture(void) {
    station_observe_errors(mac_now());
    uint16_t state = shm_read16(0x601u);
    if (state != PSM_STATE_CAPTURED) { nav_sampled = 0u; return; }
    if (nav_sampled) return;
    nav_sampled = 1u;
    uint32_t n = shm_read16(0x606u);
    if (n < 20u || n > CLEAN_RXE_BYTES) {
        station_bad_rx(mac_now());
        return;
    }
    n -= CLEAN_PLCP_BYTES;
    for (uint32_t i = 0u; i < n; i += 2u) {
        uint16_t w = shm_read16(CLEAN_SHM_FRAME + 4u + i / 2u);
        nav_frame[i] = (uint8_t)w;
        if (i + 1u < n) nav_frame[i + 1u] = (uint8_t)(w >> 8u);
    }
    int duration = clean_mac_nav_duration(nav_frame, n, station_address);
    uint32_t now = station_hw ?
        (uint32_t)shm_read16(CLEAN_DCF_RX_LO) |
        ((uint32_t)shm_read16(CLEAN_DCF_RX_HI) << 16u) : mac_now();
    station_observe_errors(now);
    if (duration < 0) station_bad_rx(now);
    else {
        station_good_rx();
        if (station_edca.enabled) {
            for (uint32_t i = 0u; i < 4u; ++i)
                clean_dcf_nav(&station_edca.ac[i], now, (uint16_t)duration);
        } else clean_dcf_nav(&station_dcf, now, (uint16_t)duration);
        if (station_dcf.nav_valid) shm_write16(CLEAN_CTS_ALLOWED, 0u);
        if (station_hw && station_dcf.nav_valid) {
            uint32_t remaining = station_dcf.nav_until - mac_now();
            if (remaining && remaining <= 32767u) {
                mmio_write16(D11_BASE + 0x70cu, (uint16_t)remaining);
                mmio_write16(D11_BASE + 0x700u, 0x103cu);
            }
        }
    }
}

#if CLEAN_RX_HT_EARLY_FIFO
static int observer_receive_early(uint32_t rx_start) {
    uint16_t owner = shm_read16(CLEAN_OBSERVER_EARLY_SHM);
    if (!owner) return 1;
    uint16_t header[CLEAN_OBSERVER_EARLY_HEADER];
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_EARLY_HEADER; ++i)
        header[i] = shm_read16(CLEAN_OBSERVER_EARLY_SHM+i);
    if (owner != header[0] || owner != shm_read16(CLEAN_OBSERVER_EARLY_SHM))
        return 1;
    if (header[13] == owner) return 1;
    if (header[13]) return fail(0x8fu,CLEAN_OBSERVER_EARLY_SHM+13u,header[13]);
    int length = clean_observer_early_length(header);
    if (!length) return 1;
    if (length < 0 || shm_read16(CLEAN_OBSERVER_SHM) != 1u ||
        owner > shm_read16(CLEAN_OBSERVER_SHM+1u))
        return fail(0x8fu,CLEAN_OBSERVER_EARLY_SHM,header[2]);
    uint32_t total = MULTI->captured_frames;
    int credit = clean_sdio_credit_available(total,rx_published_frames,FW_MAX_FRAMES);
    if (credit < 0) return fail(0x8cu,FW_MULTI_ADDR,total-rx_published_frames);
    if (!credit) return 1;
    uint16_t prefix[CLEAN_OBSERVER_RECORD_WORDS];
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_RECORD_WORDS; ++i)
        prefix[i] = shm_read16(CLEAN_OBSERVER_SHM+CLEAN_OBSERVER_HEADER_WORDS+
            (owner-1u)*CLEAN_OBSERVER_RECORD_WORDS+i);
    uint32_t index = total&(FW_MAX_FRAMES-1u);
    volatile fw_frame_slot_t *slot = &MULTI->frames[index];
    for (uint32_t i = 0u; i < (uint32_t)length; i += 2u) {
        uint16_t word = shm_read16(CLEAN_OBSERVER_EARLY_SHM+
            CLEAN_OBSERVER_EARLY_HEADER+i/2u);
        slot->raw[8u+i] = (uint8_t)word;
        if (i+1u < (uint32_t)length) slot->raw[9u+i] = (uint8_t)(word>>8u);
    }
    if (owner != shm_read16(CLEAN_OBSERVER_EARLY_SHM) ||
        header[1] != shm_read16(CLEAN_OBSERVER_EARLY_SHM+1u)) return 1;
    int decoded = clean_observer_early_decode(header,prefix,slot->raw);
    if (decoded != length)
        return fail(decoded == -2 ? 0x8eu : 0x8fu,CLEAN_OBSERVER_EARLY_SHM,header[2]);
    for (uint32_t i = 0u; i < 3u; ++i) {
        uint16_t ta = shm_read16(CLEAN_OBSERVER_SHM+3u+i);
        if (slot->raw[18u+2u*i] != (uint8_t)ta ||
            slot->raw[19u+2u*i] != (uint8_t)(ta>>8u))
            return fail(0x8fu,CLEAN_OBSERVER_EARLY_SHM,owner);
    }
    for (uint32_t i = 8u+(uint32_t)length; i < CLEAN_RAW_BYTES; ++i) slot->raw[i] = 0u;
    slot->frame_index = (uint16_t)(total+1u);
    slot->rxe_len = header[2];
    slot->rxe_status1 = header[3];
    slot->rxe_status2 = header[4];
    slot->phy_status0 = header[12];
    slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER-rx_start)>>16u);
    slot->dma_rx_len = slot->dma_completed = slot->f2_received = slot->f2_sdpcm_len = 0u;
    observer_capture_ids[index] = owner|CLEAN_OBSERVER_EARLY_TAG;
    __asm__ volatile("dsb" ::: "memory");
    MULTI->captured_frames = total+1u;
    __asm__ volatile("dsb" ::: "memory");
    shm_write16(CLEAN_OBSERVER_EARLY_SHM+13u,owner);
    return 1;
}
#endif

static int station_publish_capture(uint32_t channel) {
    uint32_t total = MULTI->captured_frames;
    if (clean_sdio_credit_available(total, rx_published_frames, FW_MAX_FRAMES) < 0)
        return fail(0x8cu, FW_MULTI_ADDR, total - rx_published_frames);
    if (total == rx_published_frames) return 1;
    volatile fw_frame_slot_t *slot = &MULTI->frames[rx_published_frames & (FW_MAX_FRAMES - 1u)];
    int posted = sdio_f2_send_frame(slot, channel);
    if (MBOX->fw_state == FW_STATE_ERROR) return 0;
    if (!posted) return 1;
    if (!CLEAN_RX_HT_OBSERVER_FIFO && slot->rxe_len >= 34u) {
        const volatile uint8_t *m = slot->raw + 8u;
        uint8_t bound = 1u;
        for (uint32_t i = 0u; i < 6u; ++i)
            if (m[4u + i] != station_address[i] || m[10u + i] != station_peer[i])
                bound = 0u;
        if (bound) {
            ++MAC_TRACE->published[clean_mac_frame_kind(m[0])];
            if (!clean_mac_valid(m, slot->rxe_len - CLEAN_PLCP_BYTES))
                ++MAC_TRACE->invalid;
        }
    }
    ++rx_published_frames;
    return 1;
}

#if CLEAN_RX_AMPDU_DMA
static int ampdu_dma_receive(uint32_t rx_start) {
    uint32_t status = mmio_read32(D11_RX_DMA_STATUS0);
    uint32_t current = ((status & 0x1fffu) -
        (FW_D11_DMA_RING_ADDR & 0x1fffu)) >> 4u;
    if (current >= 32u) return fail(0x8du, D11_RX_DMA_STATUS0, status);
    if (current == dma_consumer) return 1;
    const volatile uint8_t *buf = (const volatile uint8_t *)(uintptr_t)
        (FW_D11_DMA_BUF_ADDR + dma_consumer * FW_D11_DMA_BUF_BYTES);
    __asm__ volatile("dsb" ::: "memory");
    uint16_t outcome = (uint16_t)(buf[2] | ((uint16_t)buf[3] << 8u));
    if (!outcome) return 1;
    uint32_t total = MULTI->captured_frames;
    int credit = clean_sdio_credit_available(total, rx_published_frames, FW_MAX_FRAMES);
    if (credit < 0) return fail(0x8cu, FW_MULTI_ADDR, total - rx_published_frames);
    if (!credit) return 1;
    volatile fw_frame_slot_t *slot = &MULTI->frames[total & (FW_MAX_FRAMES - 1u)];
    int decoded = clean_ht_ampdu_dma_decode(buf, FW_D11_DMA_BUF_BYTES, slot->raw);
    if (decoded == CLEAN_AMPDU_DMA_BAD) {
        (void)rx_dma_poll_and_reclaim(0);
        return 1;
    }
    if (decoded != CLEAN_AMPDU_DMA_GOOD) {
        uint32_t error = decoded == CLEAN_AMPDU_DMA_FCS_ERROR ? 0x8eu :
            decoded == CLEAN_AMPDU_DMA_PREFIX_ERROR ? 0x8fu : 0x8du;
        return fail(error, FW_D11_DMA_BUF_ADDR +
                    dma_consumer * FW_D11_DMA_BUF_BYTES + 38u,
                    (uint32_t)buf[0] | ((uint32_t)buf[1] << 8u));
    }
    slot->frame_index = (uint16_t)(total + 1u);
    slot->rxe_len = CLEAN_HT_AMPDU_PROBE_MPDU + CLEAN_PLCP_BYTES;
    slot->rxe_status1 = 0u;
    slot->rxe_status2 = 0u;
    slot->phy_status0 = (uint16_t)(buf[4] | ((uint16_t)buf[5] << 8u));
    slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER - rx_start) >> 16u);
    slot->f2_received = 0u;
    slot->f2_sdpcm_len = 0u;
    if (!rx_dma_poll_and_reclaim(slot))
        return fail(0x8du, D11_RX_DMA_STATUS0, status);
    __asm__ volatile("dsb" ::: "memory");
    MULTI->captured_frames = total + 1u;
    return 1;
}
#endif

#if CLEAN_RX_AMPDU_SHM
static clean_ampdu_probe_trace_t ampdu_shm_snapshot;
static uint16_t ampdu_shm_outcomes[3];
static uint8_t ampdu_shm_copied;
static uint8_t ampdu_shm_next;

static int ampdu_shm_receive(uint32_t mac_en, uint32_t rx_start) {
    if (!ampdu_shm_copied) {
        if (shm_read16(0x601u) != CLEAN_AMPDU_SHM_HELD_STATE) return 1;
        mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
        uint32_t control = mmio_read32(D11_MACCONTROL);
        if ((control & 3u) != (clean_mac_pause_psm(mac_en) & 3u))
            return fail(0x8du, D11_MACCONTROL, control);
        uint16_t state = shm_read16(0x601u);
        uint16_t count = shm_read16(CLEAN_AMPDU_PROBE_SHM);
        if (state != CLEAN_AMPDU_SHM_HELD_STATE || !count || count > 3u ||
            shm_read16(CLEAN_AMPDU_IF_SHM) != 3u ||
            shm_read16(CLEAN_AMPDU_IF_SHM + 1u) != count)
            return fail(0x8du, CLEAN_AMPDU_PROBE_SHM, count);
        uint8_t *bytes = (uint8_t *)&ampdu_shm_snapshot + 4u;
        for (uint32_t i = 0u; i < CLEAN_AMPDU_PROBE_WORDS; ++i) {
            uint16_t word = shm_read16(CLEAN_AMPDU_PROBE_SHM + i);
            bytes[2u * i] = (uint8_t)word;
            bytes[2u * i + 1u] = (uint8_t)(word >> 8u);
        }
        ampdu_shm_snapshot.magic = CLEAN_AMPDU_PROBE_MAGIC;
        for (uint32_t i = 0u; i < count; ++i)
            ampdu_shm_outcomes[i] = shm_read16(CLEAN_AMPDU_IF_SHM + 11u + 8u * i);
        uint32_t status = CLEAN_AMPDU_IF_SHM + CLEAN_AMPDU_SHM_STATUS_WORD;
        uint32_t published = (uint32_t)shm_read16(status) |
            ((uint32_t)shm_read16(status + 1u) << 16u);
        shm_write16(status + 4u, state);
        shm_write16(status + 5u, (uint16_t)(shm_read16(status + 5u) + 1u));
        uint32_t held = mmio_read32(D11_BASE + 0x180u) - published;
        shm_write16(status + 6u, (uint16_t)held);
        shm_write16(status + 7u, (uint16_t)(held >> 16u));
        ampdu_shm_copied = 1u;
        shm_write16(0x601u, 0u);
        mmio_write32(D11_MACCONTROL, mac_en);
    }
    if (ampdu_shm_next == ampdu_shm_snapshot.count) return 1;
    uint32_t total = MULTI->captured_frames;
    int credit = clean_sdio_credit_available(total, rx_published_frames, FW_MAX_FRAMES);
    if (credit < 0) return fail(0x8cu, FW_MULTI_ADDR, total - rx_published_frames);
    if (!credit) return 1;
    volatile fw_frame_slot_t *slot = &MULTI->frames[total & (FW_MAX_FRAMES - 1u)];
    int decoded = clean_ht_ampdu_shm_decode(&ampdu_shm_snapshot, ampdu_shm_next,
                                           ampdu_shm_outcomes[ampdu_shm_next], slot->raw);
    if (decoded < 0)
        return fail(decoded == CLEAN_AMPDU_DMA_FCS_ERROR ? 0x8eu : 0x8du,
                    CLEAN_AMPDU_PROBE_SHM, (uint32_t)ampdu_shm_next);
    ++ampdu_shm_next;
    if (decoded == CLEAN_AMPDU_DMA_BAD) return 1;
    slot->frame_index = (uint16_t)(total + 1u);
    slot->rxe_len = 86u;
    slot->rxe_status1 = 0u;
    uint32_t status = 264u + 2u * (ampdu_shm_next - 1u);
    slot->rxe_status2 = (uint16_t)(ampdu_shm_snapshot.raw[status] |
        ((uint16_t)ampdu_shm_snapshot.raw[status + 1u] << 8u));
    slot->phy_status0 = ampdu_shm_snapshot.phy;
    slot->rx_tick_hi = (uint16_t)((ARM_CYCLE_COUNTER - rx_start) >> 16u);
    slot->dma_rx_len = slot->dma_completed = 0u;
    slot->f2_received = slot->f2_sdpcm_len = 0u;
    __asm__ volatile("dsb" ::: "memory");
    MULTI->captured_frames = total + 1u;
    return 1;
}
#endif

/* Template preparation is deliberately outside the contention interval. The
 * PSM may answer RX while waiting; in that case its TXE setup takes precedence. */
static int station_edca_select(uint32_t mac_en, uint32_t tid, uint32_t exchange_us) {
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
    (void)mmio_read32(D11_MACCONTROL);
    if (!station_rx_idle(shm_read16(0x601u)) || station_phy_busy()) {
        mmio_write32(D11_MACCONTROL, mac_en);
        return 2;
    }
    uint16_t slots = shm_read16(CLEAN_DCF_SLOTS);
    if (slots > CLEAN_DCF_MAX_SLOTS)
        return fail(0x8eu, CLEAN_DCF_SLOTS, slots);
    station_dcf.slots = slots;
    if (clean_edca_select(&station_edca, tid, mac_now()) < 0)
        return fail(0x85u, FW_TX_BUF_ADDR, tid);
    if (!exchange_us || !clean_edca_txop_valid(&station_edca, exchange_us))
        return fail(0x85u, FW_TX_BUF_ADDR, tid);
    station_hw_slot();
    station_hw_backoff();
    mmio_write32(D11_MACCONTROL, mac_en);
    return 1;
}

static int station_wait_access(uint32_t mac_en, uint16_t ack_sent, uint16_t cts_sent) {
    if (station_hw) shm_write16(CLEAN_DCF_REQUEST, 1u);
    mmio_write32(D11_MACCONTROL, mac_en);
    (void)mmio_read32(D11_MACCONTROL);
    for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
        uint32_t now = mac_now();
        uint16_t state = shm_read16(0x601u);
        if (station_hw && shm_read16(CLEAN_DCF_REQUEST) == CLEAN_DCF_FIRED)
            return 1;
        int credit = clean_sdio_credit_available(sdio_tx_posted, MBOX->f2_consumed,
                                                  SDIO_TX_WINDOW);
        if (credit < 0) return fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
        if (state == PSM_STATE_FAULT) return fail(0x88u, 0x601u, state);
        if (!station_counter_ok()) return 0;
        if (shm_read16(CLEAN_ACK_ERROR)) return fail(0x87u, CLEAN_ACK_ERROR, shm_read16(CLEAN_ACK_ERROR));
        if (state == PSM_STATE_CAPTURED || !credit ||
            (station_hw && shm_read16(CLEAN_DCF_REQUEST) == CLEAN_DCF_DEFERRED) ||
            shm_read16(CLEAN_ACK_SENT) != ack_sent ||
            shm_read16(CLEAN_CTS_SENT) != cts_sent ||
            now - access_started >= 50000u) {
            if (station_hw) {
                mmio_write32(D11_MACCONTROL, mac_en & ~2u);
                (void)mmio_read32(D11_MACCONTROL);
                uint16_t request = shm_read16(CLEAN_DCF_REQUEST);
                shm_write16(CLEAN_DCF_REQUEST, clean_dcf_cancel_request(request));
                mmio_write32(D11_MACCONTROL, mac_en);
                if (request == CLEAN_DCF_FIRED) return 1;
            } else (void)clean_dcf_poll(&station_dcf, now, 1);
            return 2;
        }
        if (station_hw) continue;
        /* PHYDEBUG: CRS, TX byte strobe and MAC TX-frame request. TXA alone
         * is not a whole-PPDU-active indication (brcmsmac d11.h). */
        uint32_t busy = station_phy_busy() || !station_rx_idle(state);
        if (!clean_dcf_poll(&station_dcf, now, busy != 0u)) continue;
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        (void)mmio_read32(D11_MACCONTROL);
        if (station_phy_busy() ||
            !station_rx_idle(shm_read16(0x601u)) || shm_read16(CLEAN_ACK_SENT) != ack_sent ||
            shm_read16(CLEAN_CTS_SENT) != cts_sent) {
            (void)clean_dcf_poll(&station_dcf, mac_now(), 1);
            mmio_write32(D11_MACCONTROL, mac_en);
            return 2;
        }
        return 1;
    }
    return fail(0x82u, D11_BASE + (station_hw ? 0x180u : 0x1cu), mac_now());
}

/* RX can hold the PSM across the PHY-start pulse. Hand over ARM's owned
 * observation while the PSM is paused, without manufacturing completion. */
static uint32_t station_observe_tx_start(uint16_t ifs, uint32_t observed_at) {
    if (!(ifs & 0x100u) || shm_read16(CLEAN_DCF_TX_PENDING) != 1u) return 0u;
    uint32_t control = mmio_read32(D11_MACCONTROL), handed = 0u;
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    if (clean_tx_phy_start_owned(shm_read16(CLEAN_DCF_REQUEST),
            shm_read16(CLEAN_DCF_TX_PENDING), shm_read16(CLEAN_RESPONSE_PENDING),
            shm_read16(CLEAN_TX_COMPLETION), ifs)) {
        shm_write16(CLEAN_DCF_TX_LO, (uint16_t)observed_at);
        shm_write16(CLEAN_DCF_TX_HI, (uint16_t)(observed_at >> 16u));
        shm_write16(CLEAN_DCF_ACK_UNTIL,
            (uint16_t)(observed_at + shm_read16(CLEAN_DCF_ACK_US)));
        shm_write16(CLEAN_DCF_TX_PENDING, 0u);
        ++*(volatile uint32_t *)CLEAN_TX_START_HANDOFFS_ADDR;
        handed = 1u;
    }
    mmio_write32(D11_MACCONTROL, control);
    (void)mmio_read32(D11_MACCONTROL);
    return handed;
}

/* A held RX capture prevents PSM's idle sampler from seeing late Data done.
 * Pause it to prove TXE has not been reused before handing over real status. */
static uint32_t station_observe_tx_completion(void) {
    if (shm_read16(0x601u) != PSM_STATE_CAPTURED ||
        (shm_read16(CLEAN_TX_COMPLETION) & 7u) == 7u) return 0u;
    uint32_t control = mmio_read32(D11_MACCONTROL), handed = 0u;
    mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    uint16_t completion = shm_read16(CLEAN_TX_COMPLETION);
    if (shm_read16(0x601u) == PSM_STATE_CAPTURED &&
        (completion & 7u) != 7u &&
        clean_tx_completion_owned(shm_read16(CLEAN_DCF_REQUEST),
            shm_read16(CLEAN_DCF_TX_PENDING), shm_read16(CLEAN_RESPONSE_PENDING),
            completion, mmio_read16(D11_BASE + 0x50eu),
            mmio_read16(D11_BASE + 0x690u))) {
        shm_write16(CLEAN_TX_COMPLETION, completion | 7u);
        ++*(volatile uint32_t *)CLEAN_TX_COMPLETION_HANDOFFS_ADDR;
        handed = 1u;
    }
    mmio_write32(D11_MACCONTROL, control);
    (void)mmio_read32(D11_MACCONTROL);
    return handed;
}

/* 0: fatal, 1: complete, 2: deferred, 3: attempted but completion timed out. */
static int d11_transmit_template_frame(uint32_t total_len, uint32_t is_ofdm, uint32_t channel,
                                       uint32_t mac_tx_ctl, uint32_t reliable,
                                       uint32_t short_preamble, uint32_t ht_probe) {
    if (total_len < 16u || total_len > CLEAN_TX_BYTES)
        return fail(0x83u, FW_TX_BUF_ADDR, total_len);
    const volatile uint8_t *tx_buf = (const volatile uint8_t *)FW_TX_BUF_ADDR +
        (ht_probe == 2u ? CLEAN_HT_DATA_HEADER : 0u);
    uint16_t phyctl1 = 0x1a02u;
    uint32_t ofdm_airtime = 0u;
    clean_ht_tx_t ht_tx;
    uint32_t station = shm_read16(CLEAN_ACK_ENABLE);
    uint32_t ba_listen = CLEAN_RX_AMPDU_BA && ht_probe == 2u &&
        ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_AMPDU_PROBE_VERSION;
    if (ba_listen) station = 1u;
    uint16_t ack_sent = shm_read16(CLEAN_ACK_SENT);
    uint16_t cts_sent = shm_read16(CLEAN_CTS_SENT);
    if (ht_probe) {
        uint32_t aggregate_probe = ht_probe == 2u && CLEAN_RX_AMPDU_PROBE &&
            ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_AMPDU_PROBE_VERSION;
        uint32_t ht_length = total_len - CLEAN_PLCP_BYTES;
        clean_ht20_config_t config;
        if (!(ht_probe == 2u ? CLEAN_RX_HT_DATA : CLEAN_RX_HT_PROBE) ||
            !(aggregate_probe ? clean_ht_ampdu_probe_sig_valid(tx_buf, ht_length) :
              CLEAN_RX_HT_STIMULUS &&
              ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_STIMULUS_VERSION ?
                  clean_ht_stimulus_sig_valid(tx_buf, ht_length) :
              CLEAN_RX_HT_HANDOFF_STIMULUS &&
              ((const volatile uint8_t *)FW_TX_BUF_ADDR)[0] == CLEAN_HT_HANDOFF_VERSION ?
                  clean_ht_handoff_sig_valid(tx_buf, ht_length) :
                                clean_ht20_sig_valid(tx_buf, ht_length)) ||
            !clean_ht20_configure(&config, ht_length, tx_buf[0], CLEAN_HT20_MCS_MASK))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        ht_tx = config.controls;
        phyctl1 = config.controls.phyctl1;
        ofdm_airtime = config.airtime_us;
    } else if (is_ofdm) {
        int rate = clean_ofdm_rate_index(tx_buf[0]);
        if (rate < 0 || !clean_ofdm_plcp_valid(tx_buf, total_len - CLEAN_PLCP_BYTES))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        phyctl1 = clean_ofdm_rates[(uint32_t)rate].phyctl1;
        ofdm_airtime = clean_ofdm_airtime_us(total_len - CLEAN_PLCP_BYTES, (uint32_t)rate);
    } else {
        int rate = clean_cck_rate_index(tx_buf[0]);
        if (rate < 0 || !clean_cck_plcp_valid(tx_buf, total_len - CLEAN_PLCP_BYTES) ||
            !clean_cck_preamble_valid((uint32_t)rate, short_preamble))
            return fail(0x83u, FW_TX_BUF_ADDR, total_len);
        phyctl1 = clean_cck_rates[rate].phyctl1;
    }
    if (is_ofdm && short_preamble) return fail(0x83u, FW_TX_BUF_ADDR, total_len);
    uint32_t airtime = is_ofdm || ht_probe ? ofdm_airtime :
        clean_cck_airtime_us(total_len - CLEAN_PLCP_BYTES,
                             (uint32_t)clean_cck_rate_index(tx_buf[0]), short_preamble);
    uint32_t ack_timeout = station_mac ? (ht_probe ?
        clean_ht_ack_timeout_us(tx_buf[0], station_dcf.slot_us) :
        clean_ack_timeout_us(is_ofdm ? (uint32_t)clean_ofdm_rate_index(tx_buf[0]) + 4u :
            (uint32_t)clean_cck_rate_index(tx_buf[0]), station_dcf.slot_us)) : 0u;
    tx_diag.magic = CLEAN_TX_DIAG_MAGIC;
    tx_diag.resume_ticks = 0u;
    tx_diag.mac_resume_us = 0u;
    tx_diag.psm_after = 0u;
    TX_COMPLETION->latched = 0u;
    uint32_t phy_started_by_arm = 0u;
    uint32_t completion_by_arm = 0u;
    tx_diag.start_ticks = ARM_CYCLE_COUNTER;
    tx_diag.length = total_len;
    tx_diag.channel = (uint16_t)channel;
    tx_diag.psm_before = shm_read16(0x601u);
    tx_diag.status_before = tx_status();

    mmio_write32(D11_MACCONTROL, station_mac ?
                 clean_mac_pause_psm(mac_tx_ctl) : 0x84020400u);
    (void)mmio_read32(D11_MACCONTROL);
    if (station_mac && (station_phy_busy() || !station_rx_idle(shm_read16(0x601u)))) {
        mmio_write32(D11_MACCONTROL, mac_tx_ctl | 2u);
        (void)mmio_read32(D11_MACCONTROL);
        return 2;
    }
    volatile clean_tx_error_status_t *tx_error =
        (volatile clean_tx_error_status_t *)CLEAN_TX_ERROR_ADDR;
    tx_error->magic = CLEAN_TX_ERROR_MAGIC;
    tx_error->valid = 0u;
    tx_error->select_operand = tx_error->select_readback = 0u;
    station_tx_phase(CLEAN_TX_PREPARE);
    if (station_hw) shm_write16(CLEAN_DCF_IDLE_VALID, 0u);
    MBOX->rc_dacbuf = 0x55550001u;
    phy_tx_hw_configure(channel);
    if (station_mac || ba_listen) phy_tx_set_automatic();
    else phy_tx_set_inhibit(0);

    MBOX->rc_dacbuf = 0x55550002u;
    uint16_t fc_tx = (uint16_t)tx_buf[6] | ((uint16_t)tx_buf[7] << 8u);
    uint32_t use_hw_ccmp = (shm_read16(0x615u) != 0u) && ((fc_tx & 0x4000u) != 0u);
    uint32_t words = (total_len + 3u) >> 2u;
    if (ht_probe) HT_DIAG->template_crc_zeroed = ht_probe == 1u && CLEAN_RX_HT_CLEAR_SIG_CRC;
    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < words; ++i) {
        const volatile uint8_t *b = tx_buf + 4u * i;
        uint32_t w = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                     ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        w = clean_ht_probe_template_word(w, i, ht_probe == 1u, CLEAN_RX_HT_CLEAR_SIG_CRC);
        mmio_write32(D11_TPL_WR_DATA, w);
    }
    mmio_write32(D11_TPL_WR_PTR, 0u);
    for (uint32_t i = 0u; i < words; ++i) {
        const volatile uint8_t *b = tx_buf + 4u * i;
        uint32_t expected = (uint32_t)b[0] | ((uint32_t)b[1] << 8u) |
                            ((uint32_t)b[2] << 16u) | ((uint32_t)b[3] << 24u);
        expected = clean_ht_probe_template_word(expected, i, ht_probe == 1u, CLEAN_RX_HT_CLEAR_SIG_CRC);
        uint32_t actual = mmio_read32(D11_TPL_WR_DATA);
        if (actual != expected) return fail(0x8du, i * 4u, actual);
    }

    mmio_write32(D11_MACCONTROL, mac_tx_ctl);
    (void)mmio_read32(D11_MACCONTROL);
    MBOX->rc_dacbuf = 0x55550003u;
    if (station_mac) mmio_write16(0x18001502u, 0u);
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
    if (ht_probe) {
        mmio_write16(0x18001510u, ht_tx.mmplcp[0]);
        mmio_write16(0x18001512u, ht_tx.mmplcp[1]);
    }
    mmio_write16(0x1800150cu, ht_probe ? ht_tx.phyctl :
                 is_ofdm ? 0x0041u : short_preamble ? 0x0050u : 0x0040u);
    mmio_write16(0x18001514u, phyctl1);
    if (ht_probe) {
        const uint16_t offsets[4] = {0x50cu,0x514u,0x510u,0x512u};
        const uint16_t expected[4] = {ht_tx.phyctl,ht_tx.phyctl1,ht_tx.mmplcp[0],ht_tx.mmplcp[1]};
        volatile uint16_t *actual = (volatile uint16_t *)&HT_DIAG->tx;
        for (uint32_t i = 0u; i < 4u; ++i) {
            actual[i] = mmio_read16(D11_BASE + offsets[i]);
            if (actual[i] != expected[i])
                return fail(0x8du, D11_BASE + offsets[i], actual[i]);
        }
    }
    mmio_write16(0x18001508u, 0x0000u);
    mmio_write16(0x1800150au, 0x0000u);
    mmio_write16(0x1800154cu, 0x0000u);
    mmio_write16(0x1800156au, (uint16_t)total_len);
    if (!station_mac) mmio_write16(0x18001568u, 0x0826u);
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
    if (station_mac) {
        cck_tx_tail.active = cck_tx_tail.ended = 0u;
        volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
        tail[0] = tail[1] = tail[2] = tail[3] = 0u;
        cck_ack_window = station_hw && !is_ofdm && !ht_probe ?
            clean_ack_timeout_us((uint32_t)clean_cck_rate_index(tx_buf[0]), station_dcf.slot_us) : 0u;
        if (station_hw) {
            shm_write16(CLEAN_DCF_ACK_US,
                (uint16_t)(airtime + ack_timeout));
        }
        station_tx_phase(CLEAN_TX_ARMED);
        int access = station_wait_access(mac_tx_ctl | 2u, ack_sent, cts_sent);
        if (access != 1) return access;
        station_tx_phase(CLEAN_TX_FIRED);
        if (!station_hw) {
            mmio_write32(D11_MACCONTROL, mac_tx_ctl);
            (void)mmio_read32(D11_MACCONTROL);
            shm_write16(CLEAN_ACK_SEEN, 0u);
            shm_write16(CLEAN_ACK_WAITING, 1u);
        }
    }
    tx_diag.trigger_ticks = ARM_CYCLE_COUNTER;
    tx_diag.mac_trigger_us = station_hw ?
        (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
        ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u) : mac_now();
    if (station_mac) {
        ack_started = tx_diag.mac_trigger_us;
        ack_window = airtime + ack_timeout;
    }
    if (!station_hw) {
        if (station_mac) mmio_write16(0x18001568u, 0x0826u);
        mmio_write16(0x18001502u, 0x00d0u);
    }
    if (reliable) ++UNICAST->attempts;
    if (station_mac) {
        last_tx_identity = (uint32_t)tx_buf[28] | ((uint32_t)tx_buf[29] << 8u) |
            ((uint32_t)tx_buf[6] << 16u) |
            (ht_probe ? (uint32_t)(tx_buf[30] & 15u) << 24u : 0u);
        last_tx_valid = 1u;
    }
    MBOX->rc_dacbuf = 0x55550005u;
    (void)mmio_read16(0x18001502u);
    tx_diag.phy_debug_first = mmio_read32(D11_BASE + 0x158u);
    uint32_t start_observed_at = mac_now();
    tx_diag.status_first = tx_status();
    if (station_hw)
        phy_started_by_arm = station_observe_tx_start((uint16_t)tx_diag.status_first,
                                                      start_observed_at);
    if (station_mac || ba_listen) {
        mmio_write32(D11_MACCONTROL, mac_tx_ctl | 2u);
        (void)mmio_read32(D11_MACCONTROL);
    }

    uint16_t st_50e_seen = 0u, st_690_seen = 0u;
    uint32_t tx_conditions = 0u;
    uint32_t raw_conditions = 0u;
    uint32_t completion_polls = 2000u + airtime;
    if (station_hw) {
        uint32_t completion_started = mac_now();
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            station_service_tx();
            uint16_t txe = mmio_read16(D11_BASE + 0x50eu);
            uint16_t ifs = mmio_read16(D11_BASE + 0x690u);
            if (!(tx_error->valid & (1u << CLEAN_TX_FIRST_ERROR)) &&
                !(txe & 0x480u) && !(ifs & 0xf00u) &&
                mmio_read16(D11_BASE + 0x3eeu))
                station_tx_phase(CLEAN_TX_FIRST_ERROR);
            st_50e_seen |= txe;
            st_690_seen |= ifs;
            if (!(txe & 0x80u)) raw_conditions |= 1u;
            if (txe & 0x400u) raw_conditions |= 2u;
            if (!(ifs & 0x100u)) raw_conditions |= 4u;
            completion_by_arm |= station_observe_tx_completion();
            if ((shm_read16(CLEAN_TX_COMPLETION) & 7u) == 7u) {
                tx_conditions = 7u;
                break;
            }
            if (!clean_tx_completion_waiting(completion_started, mac_now(), airtime)) break;
            (void)delay_ticks(180u);
        }
    } else {
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            if (station_mac) station_service_tx();
            uint16_t v = mmio_read16(0x1800150eu);
            st_50e_seen |= v;
            if (!(v & 0x80u)) raw_conditions |= 1u;
            if (!(v & 0x80u)) {
                tx_conditions |= 1u;
                break;
            }
            (void)delay_ticks(180u);
        }
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            if (station_mac) station_service_tx();
            uint16_t v = mmio_read16(0x1800150eu);
            st_50e_seen |= v;
            if (v & 0x400u) raw_conditions |= 2u;
            if (v & 0x400u) {
                tx_conditions |= 2u;
                break;
            }
            (void)delay_ticks(180u);
        }
        for (uint32_t i = 0u; i < completion_polls; ++i) {
            if (station_mac) station_service_tx();
            uint16_t v = mmio_read16(0x18001690u);
            st_690_seen |= v;
            if (!(v & 0x100u)) raw_conditions |= 4u;
            if (!(v & 0x100u)) {
                tx_conditions |= 4u;
                break;
            }
            (void)delay_ticks(180u);
        }
    }
    MBOX->vco_status = ((uint32_t)st_50e_seen << 16u) | st_690_seen;
    uint32_t phy_drained = 1u;
    if (station_mac) {
        /* CCK drain overlaps the PPDU; do not hold its reply for a second
         * airtime. Other PHYs retain their conservative post-drain guard. */
        uint32_t wait = mac_now();
        if (cck_ack_window && !shm_read16(CLEAN_DCF_TX_PENDING))
            wait = (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
                ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u);
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
            station_service_tx();
            if (mac_now() - wait >= airtime) {
                phy_drained = 1u;
                break;
            }
        }
    } else if (station) {
        uint32_t active_seen = tx_diag.phy_debug_first & 2u;
        uint32_t wait = mac_now();
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 200000u; ++poll) {
            uint32_t active = mmio_read32(D11_BASE + 0x158u) & 2u;
            active_seen |= active;
            if (active_seen && !active) { phy_drained = 1u; break; }
            if (mac_now() - wait >= airtime + 2000u) break;
        }
        mmio_write16(0x18001502u, 0u);
        mmio_write16(0x18001568u, 0u);
        phy_tx_set_automatic();
    }
    if ((is_ofdm || ht_probe) && !station) {
        /* MAC completion precedes the OFDM PHY tail. Retain TX enable for a
         * complete PPDU after MAC drain, including the ERP signal extension. */
        uint32_t drain_start = mac_now();
        phy_drained = 0u;
        for (uint32_t poll = 0u; poll < 2000000u; ++poll) {
            if (mac_now() - drain_start >= ofdm_airtime) {
                phy_drained = 1u;
                break;
            }
        }
    }
    tx_diag.complete_ticks = ARM_CYCLE_COUNTER;
    tx_diag.mac_complete_us = mac_now();
    tx_diag.status_after = tx_status();
    tx_diag.phy_error = mmio_read16(D11_BASE + 0x3eeu);
    tx_error->select_operand = shm_read16(CLEAN_TX_SELECT_OPERAND);
    tx_error->select_readback = shm_read16(CLEAN_TX_SELECT_READBACK);
    for (uint32_t i = 0u; i < 3u; ++i)
        tx_error->plcp[i] = mmio_read16(D11_BASE + 0x5c0u + 2u * i);
    tx_error->reserved = 0u;
    tx_diag.psm_debug = mmio_read32(D11_BASE + 0x154u);
    if (station_hw) {
        completion_by_arm |= station_observe_tx_completion();
        TX_COMPLETION->latched = shm_read16(CLEAN_TX_COMPLETION);
        TX_COMPLETION->raw_conditions = raw_conditions;
        TX_COMPLETION->request = shm_read16(CLEAN_DCF_REQUEST);
        TX_COMPLETION->pipeline = shm_read16(CLEAN_TX_PRE_RESPONSE) |
            ((uint32_t)shm_read16(CLEAN_DCF_TX_PENDING) << 16u) |
            (phy_started_by_arm << 17u) |
            ((uint32_t)shm_read16(CLEAN_RESPONSE_PENDING) << 18u) |
            ((uint32_t)shm_read16(CLEAN_TX_CLEANUP_REQUEST) << 20u) |
            (completion_by_arm << 22u) |
            ((uint32_t)shm_read16(0x601u) << 24u);
        if ((TX_COMPLETION->latched & 7u) == 7u) tx_conditions = 7u;
        if (!shm_read16(CLEAN_DCF_TX_PENDING)) {
            tx_diag.mac_trigger_us = (uint32_t)shm_read16(CLEAN_DCF_TX_LO) |
                ((uint32_t)shm_read16(CLEAN_DCF_TX_HI) << 16u);
            ack_started = clean_tx_tail_ack_start(&cck_tx_tail, ack_started,
                                                  tx_diag.mac_trigger_us);
        }
        if (tx_conditions == 7u && !(st_50e_seen & 0x400u))
            ++TX_COMPLETION->preserved;
    }
    MBOX->fw_substage = tx_conditions;

    if (!station) {
        mmio_write16(0x18001502u, 0x0000u);
        mmio_write16(0x18001568u, 0x0000u);
        mmio_write16(0x180017c0u, 0x8300u);
        mmio_write16(0x180017d0u, 0x0300u);
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        (void)mmio_read32(D11_MACCONTROL);
        phy_tx_set_inhibit(1);
    }
    uint32_t quiescent = station_hw && phy_drained &&
        clean_tx_incomplete_retryable(TX_COMPLETION->latched, TX_COMPLETION->request,
            shm_read16(CLEAN_DCF_TX_PENDING), shm_read16(CLEAN_RESPONSE_PENDING),
            mmio_read16(D11_BASE + 0x50eu), mmio_read16(D11_BASE + 0x690u));
    uint32_t phy_aborted = quiescent && TX_COMPLETION->latched == 5u &&
        !shm_read16(CLEAN_RESPONSE_PENDING) && cck_ack_window && cck_tx_tail.ended &&
        clean_tx_cck_abort_retryable(tx_error, tx_diag.phy_error, airtime);
    uint32_t incomplete = quiescent && (!tx_diag.phy_error || phy_aborted);
    if (phy_aborted) {
        volatile clean_tx_abort_status_t *s =
            (volatile clean_tx_abort_status_t *)CLEAN_TX_ABORT_ADDR;
        if (!s->count) {
            s->fired_at = tx_error->phase[CLEAN_TX_FIRED].at;
            s->before_error = tx_error->phase[CLEAN_TX_PREPARE].error;
            s->armed_error = tx_error->phase[CLEAN_TX_ARMED].error;
            s->fired_error = tx_error->phase[CLEAN_TX_FIRED].error;
            s->completion = (uint16_t)TX_COMPLETION->latched;
            s->first = tx_error->phase[CLEAN_TX_FIRST_ERROR];
        }
        ++s->count;
    }
    if (station_mac && phy_drained && (tx_conditions == 7u || incomplete)) {
        uint32_t control = mmio_read32(D11_MACCONTROL);
        mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(control));
        (void)mmio_read32(D11_MACCONTROL);
        shm_write16(CLEAN_TX_CLEANUP_REQUEST, incomplete ? CLEAN_TX_CLEANUP_ABORT : 1u);
        mmio_write32(D11_MACCONTROL, control);
        (void)mmio_read32(D11_MACCONTROL);
    }
    MBOX->rc_dacbuf = 0x55550000u | total_len;
    if (!phy_drained) return fail(0x82u, D11_BASE + 0x1cu, ofdm_airtime);
    if ((ht_probe || (is_ofdm && !station)) && tx_diag.phy_error != 0u)
        return fail(0x82u, D11_BASE + 0x3eeu, tx_diag.phy_error);
    if (incomplete) {
        ++TX_COMPLETION->timeouts;
        return 3;
    }
    return tx_conditions == 7u || fail(0x82u, 0x1800150eu, MBOX->vco_status);
}

static void snapshot_rx_trace(void) {
    volatile uint16_t *trace = (volatile uint16_t *)(uintptr_t)CLEAN_RX_TRACE_ADDR;
    for (uint32_t i = 0u; i < CLEAN_RX_TRACE_WORDS; ++i)
        trace[i] = shm_read16(CLEAN_RX_TRACE_SHM + i);
    volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
    tail[4] = phy_reg_read(0x465u);
    tail[5] = phy_reg_read(0x42cu);
}

static void snapshot_ack_rx(void) {
    volatile clean_ack_rx_t *rx = (volatile clean_ack_rx_t *)CLEAN_ACK_RX_ADDR;
    volatile uint16_t *words = (volatile uint16_t *)(CLEAN_ACK_RX_ADDR + 4u);
    rx->magic = CLEAN_ACK_RX_MAGIC;
    for (uint32_t i = 0u; i < CLEAN_ACK_RX_WORDS; ++i)
        words[i] = shm_read16(CLEAN_ACK_RX_SHM + i);
}

#if CLEAN_RX_HT_REARM_RESET
static uint16_t observer_tx_inhibited(void) {
    return (uint16_t)(
        (!(phy_reg_read(0x44du)&0x4000u) ? 1u : 0u) |
        ((phy_reg_read(0x44cu)&0x4000u) ? 2u : 0u) |
        (!(phy_reg_read(0x939u)&0x800u) ? 4u : 0u) |
        ((phy_reg_read(0x938u)&0x800u) ? 8u : 0u));
}

static void observer_reset_service(void) {
    if (shm_read16(CLEAN_OBSERVER_RESET_SHM) != 1u) return;
    uint32_t control = mmio_read32(D11_MACCONTROL);
    mmio_write32(D11_MACCONTROL,clean_mac_pause_psm(control));
    (void)mmio_read32(D11_MACCONTROL);
    if (shm_read16(CLEAN_OBSERVER_RESET_SHM) == 1u) {
        uint32_t start = mac_now();
        uint16_t count = shm_read16(CLEAN_OBSERVER_RESET_SHM+1u);
        uint16_t guard = observer_tx_inhibited();
        shm_write16(CLEAN_OBSERVER_RESET_SHM+2u,(uint16_t)control);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+3u,(uint16_t)(control>>16u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+4u,phy_reg_read(0x440u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+5u,phy_reg_read(0x44au));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+6u,phy_reg_read(0xa00u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+10u,guard);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+14u,mmio_read16(D11_BASE+0x408u));
        int allowed = !count && guard == 15u &&
            !(MBOX->config_flags&CFG_TX_ENABLE) &&
            shm_read16(CLEAN_OBSERVER_REARM_SHM+16u) == 2u;
        if (allowed) {
#if CLEAN_RX_HT_REARM_PARK
            uint32_t parked = mac_now();
            uint32_t polls = 0u;
            while (mac_now()-parked < 16u && polls < 65535u) ++polls;
            if (mac_now()-parked < 16u) allowed = 0;
#else
            shm_write16(CLEAN_OBSERVER_RESET_SHM+1u,1u);
#define OBSERVER_RESET_WRITE(index,mask,value) \
            if ((mask) == 0xffffu) phy_reg_write(index,value); \
            else phy_reg_mod(index,mask,value);
            CLEAN_OBSERVER_RX_RESET_OPS(OBSERVER_RESET_WRITE)
#undef OBSERVER_RESET_WRITE
#endif
        }
        uint16_t phy440 = phy_reg_read(0x440u), phy44a = phy_reg_read(0x44au);
        uint16_t phya00 = phy_reg_read(0xa00u);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+7u,phy440);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+8u,phy44a);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+9u,phya00);
        uint16_t after = observer_tx_inhibited();
        shm_write16(CLEAN_OBSERVER_RESET_SHM+11u,after);
        uint32_t elapsed = mac_now()-start;
        shm_write16(CLEAN_OBSERVER_RESET_SHM+12u,(uint16_t)elapsed);
        shm_write16(CLEAN_OBSERVER_RESET_SHM+13u,(uint16_t)(elapsed>>16u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM+15u,mmio_read16(D11_BASE+0x408u));
        shm_write16(CLEAN_OBSERVER_RESET_SHM,(uint16_t)(
            allowed && after == 15u &&
#if CLEAN_RX_HT_REARM_PARK
            phy440 == shm_read16(CLEAN_OBSERVER_RESET_SHM+4u) &&
            phy44a == shm_read16(CLEAN_OBSERVER_RESET_SHM+5u) &&
            phya00 == shm_read16(CLEAN_OBSERVER_RESET_SHM+6u)
#else
            (phy440&1u) && phy44a == 0x80u && phya00 == 0xffu
#endif
            ? 2u : 3u));
    }
    mmio_write32(D11_MACCONTROL,control);
    (void)mmio_read32(D11_MACCONTROL);
}
#endif

static void snapshot_psm(uint32_t started, uint32_t polls) {
#if CLEAN_RX_HT_WINDOW
    volatile uint32_t *commit = (volatile uint32_t *)CLEAN_OBSERVER_PHY_ADDR;
    volatile uint16_t *live = (volatile uint16_t *)(CLEAN_OBSERVER_PHY_ADDR+4u);
    ++*commit;
    __asm__ volatile("dsb" ::: "memory");
#endif
    snapshot_rx_trace();
    for (uint32_t i = 0u; i < 16u; ++i) {
        MBOX->psm_mailbox[i] = shm_read16(0x600u + i);
        MBOX->psm_diag[i] = shm_read16(0x7a0u + i);
        if (i < CLEAN_RX_FAULT_WORDS) MBOX->rx_fifo_fault[i] = shm_read16(0x620u + i);
    }
    sample_rxe8(MBOX->rxe_pre);
    MBOX->rx_polls = polls;
    MBOX->rx_elapsed_ticks = ARM_CYCLE_COUNTER - started;
    MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
#if CLEAN_RX_HT_WINDOW
    for (uint32_t i = 0u; i < CLEAN_HT_EARLY_WORDS; ++i)
        live[i] = shm_read16(CLEAN_HT_EARLY_SHM+i);
    live[10] = shm_read16(CLEAN_OBSERVER_SHM+1u);
    live[11] = shm_read16(CLEAN_OBSERVER_SHM+2u);
    live[12] = shm_read16(CLEAN_OBSERVER_EARLY_SHM);
    live[13] = observer_restart_attempts;
#if CLEAN_RX_HT_REARM_WAIT
    volatile uint16_t *rearm = (volatile uint16_t *)CLEAN_OBSERVER_REARM_ADDR;
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_REARM_WORDS; ++i)
        rearm[i] = shm_read16(CLEAN_OBSERVER_REARM_SHM+i);
    if (rearm[0] != shm_read16(CLEAN_OBSERVER_REARM_SHM)) rearm[0] |= 1u;
#if CLEAN_RX_HT_REARM_RECOVER
    if (rearm[16] != shm_read16(CLEAN_OBSERVER_REARM_SHM+16u)) rearm[16] |= 1u;
#endif
#if CLEAN_RX_HT_REARM_RESET
    if (rearm[32] != shm_read16(CLEAN_OBSERVER_RESET_SHM)) rearm[32] = 0xffffu;
#endif
#endif
    __asm__ volatile("dsb" ::: "memory");
    ++*commit;
#endif
}

static int load_and_start_psm(uint32_t generation, uint32_t channel) {
    if (CLEAN_RX_HT_RX) {
        mmio_write32(D11_MACCONTROL, 0x84020400u);
        uint32_t maccontrol = mmio_read32(D11_MACCONTROL);
        if (maccontrol & 3u) return fail(0x8eu, D11_MACCONTROL, maccontrol);
        const uint16_t locations[] = {0u,0x400u,0x800u,0xc00u,0xfffu};
        for (uint32_t i = 0u; i < 5u; ++i)
            shm_write16(locations[i], (uint16_t)(0xa10fu + 0x111u*i));
        for (uint32_t i = 0u; i < 5u; ++i) {
            uint16_t value = shm_read16(locations[i]);
            if (value != (uint16_t)(0xa10fu + 0x111u*i))
                return fail(0x8eu, locations[i], value);
        }
        for (uint32_t i = 0u; i < 5u; ++i) shm_write16(locations[i], 0u);
    }
#if CLEAN_RX_HT_REARM_WAIT
    for (uint32_t i = 0u; i < CLEAN_OBSERVER_REARM_WORDS; ++i) {
        shm_write16(CLEAN_OBSERVER_REARM_SHM+i,0u);
        ((volatile uint16_t *)CLEAN_OBSERVER_REARM_ADDR)[i] = 0u;
    }
#endif
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
    ack_test_epoch = 0u;
    if (!station_ack_test_configure(0u)) return 0;
#if CLEAN_RX_AMPDU_PROBE
    for (uint32_t i = 0u; i < CLEAN_AMPDU_PROBE_WORDS; ++i)
        shm_write16(CLEAN_AMPDU_PROBE_SHM + i, 0u);
    ((volatile clean_ampdu_probe_trace_t *)CLEAN_AMPDU_PROBE_ADDR)->magic = 0u;
#endif
#if CLEAN_RX_AMPDU_INTERFACE
    for (uint32_t i = 0u; i < CLEAN_AMPDU_IF_WORDS; ++i)
        shm_write16(CLEAN_AMPDU_IF_SHM + i, 0u);
    ((volatile clean_ampdu_interface_t *)CLEAN_AMPDU_IF_ADDR)->magic = 0u;
#endif
#if CLEAN_RX_AMPDU_DMA
    shm_write16(CLEAN_AMPDU_DMA_PENDING, 0u);
    shm_write16(CLEAN_AMPDU_DMA_SUBMITTED, 0u);
#endif
    volatile uint32_t *tail = (volatile uint32_t *)(uintptr_t)CLEAN_TX_TAIL_ADDR;
    for (uint32_t i = 0u; i < 4u; ++i) tail[i] = 0u;
    volatile clean_tx_abort_status_t *abort_status =
        (volatile clean_tx_abort_status_t *)CLEAN_TX_ABORT_ADDR;
    volatile uint8_t *abort_bytes = (volatile uint8_t *)abort_status;
    for (uint32_t i = 0u; i < sizeof(*abort_status); ++i) abort_bytes[i] = 0u;
    abort_status->magic = CLEAN_TX_ABORT_MAGIC;
    volatile uint8_t *ht_bytes = (volatile uint8_t *)HT_RX;
    for (uint32_t i = 0u; i < sizeof(*HT_RX); ++i) ht_bytes[i] = 0u;
    HT_RX->magic = CLEAN_HT_RX_MAGIC;
    volatile uint8_t *ht_diag_bytes = (volatile uint8_t *)HT_DIAG;
    for (uint32_t i = 0u; i < sizeof(*HT_DIAG); ++i) ht_diag_bytes[i] = 0u;
    ((volatile clean_ht_failure_t *)CLEAN_HT_FAILURE_ADDR)->magic = 0u;
    ((volatile clean_auth_failure_t *)CLEAN_AUTH_FAILURE_ADDR)->magic = 0u;
    HT_DIAG->magic = CLEAN_HT_DIAG_MAGIC;
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
            snapshot_rx_trace();
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
#if CLEAN_RX_HT_WINDOW
    ++observer_restart_attempts;
#endif
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
    ((volatile clean_edca_status_t *)CLEAN_EDCA_STATUS_ADDR)->magic = 0u;
    volatile uint32_t *mac_words = (volatile uint32_t *)CLEAN_MAC_RADIO_ADDR;
    for (uint32_t i = 0u; i < sizeof(clean_mac_radio_status_t) / 4u; ++i) mac_words[i] = 0u;
    volatile uint32_t *trace_words = (volatile uint32_t *)CLEAN_MAC_TRACE_ADDR;
    for (uint32_t i = 0u; i < sizeof(clean_mac_trace_status_t) / 4u; ++i) trace_words[i] = 0u;
    DCF_TRACE->magic = 0u;
    if (MBOX->fw_substage != 0u) {
        MBOX->fw_error = 0x99u;
        MBOX->fw_state = FW_STATE_ERROR;
        __asm__ volatile("dsb" ::: "memory");
        return;
    }
    MBOX->magic = FW_MAGIC;
    MBOX->fw_state = FW_STATE_PMU_PLL;
    static const uint32_t layout[CLEAN_RX_LAYOUT_WORDS] = CLEAN_RX_LAYOUT_VALUES;
    volatile uint32_t *published_layout = (volatile uint32_t *)CLEAN_RX_LAYOUT_ADDR;
    for (uint32_t i = 0u; i < CLEAN_RX_LAYOUT_WORDS; ++i) published_layout[i] = layout[i];
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
    uint8_t capture_target_reached = 0u;
    MULTI->magic = FW_MULTI_MAGIC;
    MULTI->target_frames = target_frames;
    MULTI->captured_frames = 0u;

    uint32_t mac_en = 0xc0020403u;
    if (flags & CFG_PROMISCUOUS)
        mac_en |= 0x01d00000u;
#if CLEAN_RX_HT_PHY_SNAPSHOT
    snapshot_observer_phy(0u);
#endif
    mmio_write32(D11_MACCONTROL, mac_en);
    MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
    MBOX->fw_state = FW_STATE_RX_ACTIVE;

    uint32_t rx_start = ARM_CYCLE_COUNTER;
    uint32_t polls = 0u;
#if CLEAN_RX_HT_OBSERVER_FIFO
    uint32_t observer_stop_started = 0u, observer_stop_polls = 0u;
#endif
    for (; scanner_mode || polls < 1800000u; ++polls) {
#if CLEAN_RX_HT_REARM_RESET
        observer_reset_service();
#endif
        if ((polls & 4095u) == 0u) {
            snapshot_psm(rx_start, polls);
            station_publish_mac();
        }
        uint32_t cmd = MBOX->host_cmd;
#if CLEAN_RX_HT_OBSERVER_FIFO
        if (!station_publish_capture(channel)) break;
#endif
#if CLEAN_RX_HT_EARLY_FIFO
        if (!observer_receive_early(rx_start)) break;
#endif
#if CLEAN_RX_AMPDU_DMA
        if (!station_mac && !ampdu_dma_receive(rx_start)) break;
#endif
#if CLEAN_RX_AMPDU_SHM
        if (!ampdu_shm_receive(mac_en, rx_start)) break;
#endif
#if CLEAN_RX_AMPDU_DELIVERY
        if (!station_mac && !station_publish_capture(channel)) break;
#endif
#if CLEAN_RX_AMPDU_SHM
        if (!station_mac && capture_target_reached &&
            MULTI->captured_frames == rx_published_frames) break;
#endif
        if (station_mac) {
            if (policy_pending && cmd != policy_command) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if (!station_publish_capture(channel)) break;
            if (capture_target_reached &&
                MULTI->captured_frames == rx_published_frames) break;
            if (!station_counter_ok()) break;
            station_consume_ack();
            station_observe_capture();
            uint32_t now = mac_now();
            if (!station_hw) (void)clean_dcf_poll(&station_dcf, now,
                station_phy_busy() ||
                !station_rx_idle(shm_read16(0x601u)));
            if (station_hw && station_dcf.nav_valid &&
                now - station_dcf.nav_until < 0x80000000u) station_dcf.nav_valid = 0u;
            if (!station_dcf.nav_valid && !shm_read16(CLEAN_CTS_ALLOWED))
                shm_write16(CLEAN_CTS_ALLOWED, 1u);
            station_apply_policy(mac_en);
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            cmd = policy_pending ? 2u : MBOX->host_cmd;
        }
        if (station_enabled && shm_read16(CLEAN_ACK_ERROR)) {
            fail(0x87u, CLEAN_ACK_ERROR, shm_read16(CLEAN_ACK_ERROR));
            break;
        }
#if CLEAN_RX_HT_OBSERVER_FIFO
        if (cmd == 3u) {
            if (!observer_stop_polls) {
                observer_stop_started = mac_now();
                mmio_write32(D11_MACCONTROL,0x04020400u);
                (void)mmio_read32(D11_MACCONTROL);
            }
            int drain = clean_sdio_drain_state(MULTI->captured_frames,rx_published_frames,
                FW_MAX_FRAMES,observer_stop_started,mac_now(),observer_stop_polls++);
            if (drain < 0) {
                fail(drain == -1 ? 0x8cu : 0x89u,FW_MULTI_ADDR,
                     MULTI->captured_frames-rx_published_frames);
                break;
            }
            uint32_t pending = MULTI->captured_frames-rx_published_frames;
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED && pending < FW_MAX_FRAMES)
                cmd = 2u;
            else if (drain > 0) continue;
        }
#endif
        if (cmd == 3u) {
            station_publish_mac();
            snapshot_ack_rx();
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
                if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT) {
                    shm_write16(0x601u, 0u);
                    capture_prepared = 0u;
                }
                mmio_write32(D11_MACCONTROL, mac_en);
                MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            }
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 5u || (cmd & 0xffu) == CLEAN_TX_CCK_SHORT ||
            (cmd & 0xffu) == CLEAN_HT_PROBE_CMD || (cmd & 0xffu) == CLEAN_HT_DATA_CMD) {
            if ((flags & CFG_TX_ENABLE) == 0u) {
                fail(0x81u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint32_t tx_len  = clean_tx_length(cmd);
            uint32_t is_ofdm = (cmd >> 18u) & 1u;
            uint32_t tx_cnt  = (cmd >> 19u) & 0xffu;
            if (tx_cnt == 0u) tx_cnt = 1u;
            uint32_t reliable = (cmd & CLEAN_TX_ACK_REQUIRED) != 0u;
            uint32_t short_preamble = (cmd & 255u) == CLEAN_TX_CCK_SHORT;
            const volatile uint8_t *packet = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            uint32_t ht_probe = (cmd & 255u) == CLEAN_HT_DATA_CMD ? 2u :
                (cmd & 255u) == CLEAN_HT_PROBE_CMD ? 1u : 0u;
            uint32_t ht_clocked = ht_probe == 2u && packet[0] == CLEAN_HT_CLOCKED_VERSION;
            const volatile uint8_t *frame = packet + (ht_probe == 2u ? CLEAN_HT_DATA_HEADER : 0u);
            if ((ht_probe == 1u && !clean_ht_probe_command_valid(cmd, CLEAN_RX_HT_PROBE,
                                                               station_enabled, packet)) ||
                (ht_probe == 2u && !(ht_clocked ?
                    clean_ht_clocked_command_valid(cmd, CLEAN_RX_HT_DATA,
                        station_hw ? 3u : 0u, packet) :
                    clean_ht_data_command_valid(cmd, CLEAN_RX_HT_DATA, station_enabled, packet)))) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if (ht_clocked) reliable = 1u;
            if (ht_clocked && !clean_ht_clocked_policy_valid(MAC_RADIO->profile,
                    policy_basic, policy_short_slot, policy_response)) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            int tx_tid = ht_clocked ? (int)(frame[30] & 15u) :
                clean_station_tx_tid(frame + 6u, tx_len >= 6u ? tx_len - 6u : 0u,
                                     station_edca.enabled);
            if (station_edca.enabled && (tx_tid < 0 || tx_tid >= 8 ||
                                         (ht_probe && !ht_clocked))) {
                fail(0x85u, FW_TX_BUF_ADDR, cmd);
                break;
            }
            if (station_enabled && (cmd & (CLEAN_TX_REARM_RX | CLEAN_TX_MATCH_SOURCE))) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint8_t addresses_match = 1u;
            if (reliable && tx_len >= 22u)
                for (uint32_t i = 0u; i < 6u; ++i)
                    if (frame[10u + i] != station_peer[i] ||
                        frame[16u + i] != station_address[i]) addresses_match = 0u;
            if (reliable && (!station_enabled || tx_cnt > 8u ||
                tx_len < 34u || tx_len > CLEAN_TX_BYTES ||
                (!ht_clocked && !is_ofdm &&
                    (!station_mac || !clean_cck_plcp_valid(frame, tx_len - 6u))) ||
                !clean_mac_valid(frame + 6u, tx_len - 6u) ||
                (frame[10] & 1u) ||
                (!ht_clocked && tx_tid < 0) ||
                !addresses_match)) {
                fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                break;
            }
            if (station_mac && !reliable) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            if ((cmd & CLEAN_TX_MATCH_SOURCE) &&
                (tx_len < 34u || tx_len > CLEAN_TX_BYTES || (frame[6] & 0x0fu) != 0u ||
                 (frame[7] & 3u) != 0u || (frame[16] & 1u) != 0u)) {
                fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                break;
            }
            if (reliable) {
                UNICAST->magic = CLEAN_UNICAST_MAGIC;
                UNICAST->result = CLEAN_UNICAST_IDLE;
                UNICAST->attempts = 0u;
                UNICAST->ack_received = 0u;
                UNICAST->started_us = mac_now();
                access_started = UNICAST->started_us;
            }
            if (station_mac) {
                int selected = 1;
                if (station_edca.enabled && !policy_pending) {
                    uint32_t exchange;
                    if (ht_clocked) {
                        int rate = clean_ht_ack_rate(policy_basic, frame[0]);
                        uint32_t ack = rate < 0 ? 0u : rate < 4 ?
                            clean_cck_airtime_us(14u, (uint32_t)rate, 0u) :
                            clean_ofdm_airtime_us(14u, (uint32_t)rate - 4u);
                        exchange = ack ? clean_ht20_airtime_us(CLEAN_HT_ACK_MPDU,
                                                               frame[0]) + 10u + ack : 0u;
                    } else exchange = clean_legacy_exchange_us(frame, tx_len, is_ofdm,
                                                               short_preamble, policy_basic);
                    selected = station_edca_select(mac_en, (uint32_t)tx_tid, exchange);
                    if (!selected) break;
                }
                uint32_t identity = (uint32_t)frame[28] | ((uint32_t)frame[29] << 8u) |
                    ((uint32_t)frame[6] << 16u) |
                    (frame[6] == 0x88u || frame[6] == 0xc8u ?
                        (uint32_t)(frame[30] & 15u) << 24u : 0u);
                if (last_tx_valid && identity != last_tx_identity) {
                    uint8_t redraw = station_dcf.cw != station_dcf.cw_min;
                    clean_dcf_new_frame(&station_dcf, mac_now());
                    if (redraw) station_hw_backoff();
                }
                int credit = clean_sdio_credit_available(sdio_tx_posted, MBOX->f2_consumed,
                                                          SDIO_TX_WINDOW);
                if (credit < 0) {
                    fail(0x8cu, FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET, MBOX->f2_consumed);
                    break;
                }
                if (selected == 2 || policy_pending || !credit || !station_rx_idle(shm_read16(0x601u)) ||
                    station_phy_busy()) {
                    UNICAST->result = CLEAN_UNICAST_BUSY;
                    ++MAC_RADIO->deferrals;
                    UNICAST->finished_us = mac_now();
                    (void)clean_dcf_poll(&station_dcf, UNICAST->finished_us, 1);
                    station_publish_mac();
                    MBOX->host_cmd = 2u;
                    __asm__ volatile("dsb" ::: "memory");
                    continue;
                }
            }
            if (!station_mac) {
                mmio_write32(D11_MACCONTROL, 0x84020400u);
                (void)mmio_read32(D11_MACCONTROL);
                (void)delay_ticks(2000u);
            }
            if (cmd & CLEAN_TX_MATCH_SOURCE) {
                d11_set_addrmatch(0u, frame + 16u);
                d11_set_addrmatch(3u, frame + 22u);
            }
            for (uint32_t t = 0u; t < tx_cnt; ++t) {
                if (reliable) {
                    /* Freeze the sequence number, change only Retry and its FCS. */
                    if (t) {
                        volatile uint8_t *retry = (volatile uint8_t *)FW_TX_BUF_ADDR;
                        if (!clean_mac_retry(retry + 6u, tx_len - 6u)) {
                            fail(0x85u, FW_TX_BUF_ADDR, tx_len);
                            break;
                        }
                    }
                    shm_write16(CLEAN_ACK_SEEN, 0u);
                    shm_write16(CLEAN_ACK_WAITING, station_mac ? 0u : 1u);
                }
                int submitted = d11_transmit_template_frame(tx_len, is_ofdm, channel,
                    mac_en & ~2u, reliable, short_preamble, ht_probe);
                if (!submitted) break;
                if (submitted == 2) {
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    UNICAST->result = CLEAN_UNICAST_BUSY;
                    ++MAC_RADIO->deferrals;
                    break;
                }
                if (submitted == 3) {
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    UNICAST->result = CLEAN_UNICAST_TIMEOUT;
                    ++MAC_RADIO->timeouts;
                    clean_dcf_complete(&station_dcf, mac_now(), 0);
                    station_hw_backoff();
                    break;
                }
                if (reliable) {
                    mmio_write32(D11_MACCONTROL, mac_en);
                    uint32_t wait = mac_now();
                    uint32_t ack = 0u;
                    uint32_t timeout = 1500u;
                    if (station_mac) {
                        uint32_t rate = is_ofdm ?
                            (uint32_t)clean_ofdm_rate_index(frame[0]) + 4u :
                            (uint32_t)clean_cck_rate_index(frame[0]);
                        timeout = ht_clocked ?
                            clean_ht_ack_timeout_us(frame[0], station_dcf.slot_us) :
                            clean_ack_timeout_us(rate, station_dcf.slot_us);
                    }
                    for (uint32_t poll = 0u; poll < 200000u; ++poll) {
                        if (station_mac) station_service_tx();
                        ack = shm_read16(CLEAN_ACK_SEEN);
                        if (ack || (station_mac && !shm_read16(CLEAN_ACK_WAITING)) ||
                            mac_now() - wait >= timeout) break;
                    }
                    shm_write16(CLEAN_ACK_WAITING, 0u);
                    if (ack) {
                        UNICAST->ack_received = 1u;
                        UNICAST->result = CLEAN_UNICAST_ACKED;
                        if (station_mac) {
                            clean_dcf_complete(&station_dcf, mac_now(), 1);
                            station_hw_backoff();
                            /* ACK control frames are consumed by the MAC, not
                             * application traffic occupying the only F2 credit. */
                            station_consume_ack();
                        }
                        break;
                    }
                    UNICAST->result = CLEAN_UNICAST_TIMEOUT;
                    if (station_mac) {
                        ++MAC_RADIO->timeouts;
                        clean_dcf_complete(&station_dcf, mac_now(), 0);
                        station_hw_backoff();
                    }
                    /* A reply can still be arriving after an ACK timeout.
                     * Keep station RX live, not just completed captures. */
                    if (shm_read16(0x601u) == PSM_STATE_CAPTURED) break;
                    if (!station_mac)
                        mmio_write32(D11_MACCONTROL, 0x84020400u);
                }
                if (!station_mac && t + 1u < tx_cnt)
                    (void)delay_ticks(180u * 1500u);
            }
            if (reliable) {
                UNICAST->finished_us = mac_now();
                UNICAST->ack_sent = shm_read16(CLEAN_ACK_SENT);
                UNICAST->ack_error = shm_read16(CLEAN_ACK_ERROR);
                station_publish_mac();
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if (cmd & CLEAN_TX_REARM_RX) {
                if (!d11_restart_rx_psm(channel)) break;
            } else if (!reliable &&
                       !(CLEAN_RX_AMPDU_BA && ht_probe == 2u &&
                         packet[0] == CLEAN_HT_AMPDU_PROBE_VERSION) &&
                       (shm_read16(0x601u) == PSM_STATE_CAPTURED ||
                       shm_read16(0x601u) == PSM_STATE_FAULT))
                shm_write16(0x601u, 0u);
            if (!reliable) capture_prepared = 0u;
            rx_after_explicit_tx = 1u;
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            tx_diag.resume_ticks = ARM_CYCLE_COUNTER;
            tx_diag.mac_resume_us = mac_now();
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
                (p[6] & 1u) || clean_ack_rate(basic, 0u) < 0 || p[14] > 1u ||
                (p[15] < 1u || p[15] > 3u) || station_enabled) {
                fail(0x85u, FW_TX_BUF_ADDR, basic);
                break;
            }
            mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
            (void)mmio_read32(D11_MACCONTROL);
            for (uint32_t i = 0u; i < 3u; ++i)
                shm_write16(0x612u + i, (uint16_t)((uint16_t)p[2u*i] |
                            ((uint16_t)p[2u*i + 1u] << 8u)));
            d11_set_addrmatch(0u, p);
            d11_set_addrmatch(3u, p + 6u);
            for (uint32_t i = 0u; i < 3u; ++i)
                shm_write16(CLEAN_ACK_PEER + i, (uint16_t)((uint16_t)p[6u + 2u*i] |
                            ((uint16_t)p[7u + 2u*i] << 8u)));
            for (uint32_t i = 0u; i < 3u; ++i) {
                if (shm_read16(0x612u + i) !=
                    ((uint16_t)p[2u*i] | ((uint16_t)p[2u*i+1u] << 8u)) ||
                    shm_read16(CLEAN_ACK_PEER + i) !=
                    ((uint16_t)p[6u+2u*i] | ((uint16_t)p[7u+2u*i] << 8u))) {
                    fail(0x85u, 0x612u + i, shm_read16(0x612u + i));
                    break;
                }
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if (!station_response_rates(basic)) break;
            policy_basic = basic;
            policy_short_slot = p[14];
            policy_response = policy_pending = 0u;
            policy_command = 0u;
            station_edca.enabled = policy_edca_pending = 0u;
            shm_write16(CLEAN_ACK_WAITING, 0u);
            shm_write16(CLEAN_ACK_SEEN, 0u);
            shm_write16(CLEAN_ACK_SENT, 0u);
            shm_write16(CLEAN_ACK_ERROR, 0u);
            shm_write16(CLEAN_QOS_RESPONSE_POLICY, 0u);
            shm_write16(CLEAN_RESPONSE_POLICY, p[15] >= 2u ?
                        3u | CLEAN_RESPONSE_INTERNAL_ACK : 0u);
            shm_write16(CLEAN_ACK_CONSUMED, 0u);
            MAC_TRACE->magic = CLEAN_MAC_TRACE_MAGIC;
            for (uint32_t i = 0u; i < 4u; ++i) shm_write16(CLEAN_ACK_KINDS + i, 0u);
            shm_write16(CLEAN_CTS_ALLOWED, 1u);
            shm_write16(CLEAN_CTS_SENT, 0u);
            shm_write16(CLEAN_SHORT_SENT, 0u);
            shm_write16(CLEAN_TX_CLEANUP_REQUEST, 0u);
            shm_write16(CLEAN_TX_CLEANUP_COUNT, 0u);
            shm_write16(CLEAN_TX_COMPLETION, 0u);
            shm_write16(CLEAN_TX_PRE_RESPONSE, 0u);
            TX_COMPLETION->magic = CLEAN_TX_COMPLETION_MAGIC;
            TX_COMPLETION->latched = TX_COMPLETION->preserved = TX_COMPLETION->raw_conditions = 0u;
            TX_COMPLETION->request = TX_COMPLETION->pipeline = 0u;
            TX_COMPLETION->timeouts = 0u;
            *(volatile uint32_t *)CLEAN_TX_START_HANDOFFS_ADDR = 0u;
            *(volatile uint32_t *)CLEAN_TX_COMPLETION_HANDOFFS_ADDR = 0u;
            if (!station_ack_test_configure(0u)) break;
            shm_write16(CLEAN_ACK_ENABLE, 1u);
            station_enabled = 1u;
            station_mac = (uint8_t)(p[15] >= 2u);
            station_hw = (uint8_t)(p[15] == 3u);
            rx_published_frames = MULTI->captured_frames;
            rx_discard_fcs = shm_read16(0x7a8u);
            rx_discard_length = shm_read16(0x7a9u);
            for (uint32_t i = 0u; i < 6u; ++i) {
                station_address[i] = p[i];
                station_peer[i] = p[6u + i];
            }
            clean_dcf_init(&station_dcf, mac_now(),
                ARM_CYCLE_COUNTER ^ ((uint32_t)p[2] << 24u) ^ ((uint32_t)p[3] << 16u) ^
                ((uint32_t)p[4] << 8u) ^ p[5], p[14]);
            station_hw_slot();
            station_hw_backoff();
            shm_write16(CLEAN_DCF_ENABLE, station_hw);
            MAC_RADIO->magic = CLEAN_MAC_RADIO_MAGIC;
            MAC_RADIO->profile = p[15];
            station_publish_mac();
            nav_sampled = 0u;
            UNICAST->magic = CLEAN_UNICAST_MAGIC;
            UNICAST->result = CLEAN_UNICAST_IDLE;
            phy_tx_hw_configure(channel);
            phy_tx_set_inhibit(0);
            phy_tx_set_automatic();
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        if ((cmd & 0xffu) == 8u || (cmd & 0xffu) == CLEAN_WMM_POLICY_CMD) {
            const volatile uint8_t *p = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            uint16_t basic = (uint16_t)((uint16_t)p[12] | ((uint16_t)p[13] << 8u));
            clean_edca_ac_t next_edca[4];
            uint32_t wmm = (cmd & 0xffu) == CLEAN_WMM_POLICY_CMD;
            uint8_t matches = 1u;
            for (uint32_t i = 0u; i < 6u; ++i)
                if (p[i] != station_address[i] || p[6u + i] != station_peer[i])
                    matches = 0u;
            if (!station_mac || !matches || clean_ack_rate(basic, 0u) < 0 ||
                p[14] > 1u || p[15] != MAC_RADIO->profile ||
                (wmm && (!station_hw || !clean_wmm_policy_decode(next_edca, cmd,
                    CLEAN_RX_WMM, MAC_RADIO->profile, p)))) {
                fail(0x85u, FW_TX_BUF_ADDR, basic);
                break;
            }
            policy_basic = basic;
            policy_short_slot = p[14];
            if (wmm) {
                for (uint32_t i = 0u; i < 4u; ++i) policy_edca[i] = next_edca[i];
                policy_response = p[16];
                policy_edca_pending = 1u;
            }
            policy_pending = 1u;
            policy_command = cmd;
            continue;
        }
        if ((cmd & 0xffu) == CLEAN_AMPDU_BA_CMD) {
#if CLEAN_RX_AMPDU_BA
            uint8_t plcp[6];
            uint32_t mcs = cmd >> 8u;
            int rate = clean_ht_ack_rate(policy_basic, mcs);
            if (!station_hw || mcs >= 8u || policy_basic != 0x150u ||
                rate < 4 || shm_read16(CLEAN_AMPDU_BA_WORD) ||
                ampdu_shm_copied || shm_read16(CLEAN_AMPDU_PROBE_SHM) ||
                !clean_ofdm_plcp_rate(plcp, 32u, (uint32_t)rate - 4u)) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
            uint32_t control = mmio_read32(D11_MACCONTROL);
            if ((control & 3u) != (clean_mac_pause_psm(mac_en) & 3u)) {
                fail(0x8du, D11_MACCONTROL, control);
                break;
            }
            for (uint32_t i = 1u; i < CLEAN_AMPDU_BA_WORDS; ++i) {
                uint16_t value = i >= 14u && i <= 16u ?
                    (uint16_t)(plcp[2u*(i-14u)] |
                        ((uint16_t)plcp[2u*(i-14u)+1u] << 8u)) :
                    i == 17u ? clean_ofdm_rates[rate-4].phyctl1 : 0u;
                shm_write16(CLEAN_AMPDU_BA_WORD + i, value);
                if (shm_read16(CLEAN_AMPDU_BA_WORD + i) != value) {
                    fail(0x8du, CLEAN_AMPDU_BA_WORD + i, value);
                    break;
                }
            }
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            shm_write16(CLEAN_AMPDU_BA_WORD, 1u);
            if (shm_read16(CLEAN_AMPDU_BA_WORD) != 1u) {
                fail(0x8du, CLEAN_AMPDU_BA_WORD, shm_read16(CLEAN_AMPDU_BA_WORD));
                break;
            }
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
#else
            fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
            break;
#endif
        }
        if ((cmd & 0xffu) == CLEAN_HT_RESPONSE_CMD) {
            int policy = clean_ht_response_policy(cmd, CLEAN_RX_HT_RESPONSE,
                                                   MAC_RADIO->profile);
            if (!station_hw || policy < 0) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            policy_response = (uint8_t)policy;
            policy_pending = 1u;
            policy_command = cmd;
            continue;
        }
        if ((cmd & 0xffu) == CLEAN_EDCA_CMD) {
            const volatile uint8_t *p = (const volatile uint8_t *)FW_TX_BUF_ADDR;
            if (!clean_edca_command_valid(cmd, CLEAN_RX_EDCA, MAC_RADIO->profile) ||
                !station_hw || !clean_edca_decode(policy_edca, p) ||
                !clean_edca_valid(policy_edca)) {
                fail(0x85u, FW_TX_BUF_ADDR, cmd);
                break;
            }
            policy_edca_pending = policy_pending = 1u;
            policy_command = cmd;
            continue;
        }
        if ((cmd & 0xffu) == 9u) {
            int count = clean_test_ack_count(cmd, MAC_RADIO->profile);
            if (!station_hw || count < 0) {
                fail(0x85u, FW_MBOX_ADDR + 24u, cmd);
                break;
            }
            uint16_t ack_fault = shm_read16(CLEAN_ACK_TEST_FAULT);
            uint16_t ack_phase = shm_read16(CLEAN_ACK_TEST_PHASE);
            if (ack_fault || ack_phase > 2u) {
                fail(0x87u, ack_fault ? CLEAN_ACK_TEST_FAULT : CLEAN_ACK_TEST_PHASE,
                     ack_fault ? ack_fault : ack_phase);
                break;
            }
            if (!station_policy_boundary(shm_read16(0x601u))) continue;
            mmio_write32(D11_MACCONTROL, clean_mac_pause_psm(mac_en));
            (void)mmio_read32(D11_MACCONTROL);
            if (!station_policy_boundary(shm_read16(0x601u)) || station_phy_busy() ||
                shm_read16(CLEAN_ACK_TEST_PHASE)) {
                mmio_write32(D11_MACCONTROL, mac_en);
                (void)mmio_read32(D11_MACCONTROL);
                continue;
            }
            shm_write16(CLEAN_DCF_IDLE_VALID, 0u);
            if (!station_ack_test_configure((uint16_t)count)) break;
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
            if (shm_read16(0x601u) == PSM_STATE_CAPTURED || shm_read16(0x601u) == PSM_STATE_FAULT) {
                shm_write16(0x601u, 0u);
                capture_prepared = 0u;
            }
            mmio_write32(D11_MACCONTROL, mac_en);
            MBOX->maccontrol_active = mmio_read32(D11_MACCONTROL);
            MBOX->host_cmd = 2u;
            __asm__ volatile("dsb" ::: "memory");
            continue;
        }
        uint16_t st = shm_read16(0x601u);
        if (st == PSM_STATE_FAULT) {
            if (station_enabled || CLEAN_RX_HT_REARM_WAIT || CLEAN_RX_HT_COPY_WAIT) {
#if CLEAN_RX_HT_REARM_WAIT || CLEAN_RX_HT_COPY_WAIT
                snapshot_psm(rx_start,polls);
                fail(0x88u,0x602u,shm_read16(0x602u));
#else
                fail(0x88u,0x601u,st);
#endif
                break;
            }
            if (!d11_restart_rx_psm(channel)) break;
            capture_prepared = 0u;
            mmio_write32(D11_MACCONTROL, mac_en);
            continue;
        }
        if (st == PSM_STATE_CAPTURED) {
            if (capture_target_reached) continue;
            if (station_mac) {
                station_consume_ack();
                if (shm_read16(0x601u) != PSM_STATE_CAPTURED) continue;
                station_observe_capture();
                if (station_hw) {
                    uint32_t captured = (uint32_t)shm_read16(CLEAN_DCF_RX_LO) |
                        ((uint32_t)shm_read16(CLEAN_DCF_RX_HI) << 16u);
                    uint32_t held = mac_now() - captured;
                    if (held > MAC_TRACE->rx_capture_max_us)
                        MAC_TRACE->rx_capture_max_us = held;
                }
                uint32_t length = shm_read16(0x606u);
                if (length < CLEAN_PLCP_BYTES ||
                    !clean_mac_rx_relevant(nav_frame, length - CLEAN_PLCP_BYTES,
                                           station_address, station_peer)) {
                    if (!capture_prepared) (void)rx_dma_poll_and_reclaim(0);
                    capture_prepared = 0u;
                    ++MAC_TRACE->rx_filtered;
                    shm_write16(0x601u, 0u);
                    nav_sampled = 0u;
                    continue;
                }
            }
            if (station_enabled || CLEAN_RX_HT_DATA || CLEAN_RX_HT_OBSERVER_FIFO) {
                int credit = (station_mac || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) ?
                    clean_sdio_credit_available(MULTI->captured_frames, rx_published_frames,
                                                FW_MAX_FRAMES) :
                    clean_sdio_credit_available(sdio_tx_posted, MBOX->f2_consumed,
                                                SDIO_TX_WINDOW);
                if (credit < 0) {
                    fail(0x8cu, (station_mac || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) ? FW_MULTI_ADDR :
                        FW_MBOX_ADDR + CLEAN_SDIO_ACK_OFFSET,
                        (station_mac || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) ?
                            MULTI->captured_frames - rx_published_frames :
                        MBOX->f2_consumed);
                    break;
                }
                /* An ACKed frame is retained in SHM when all SRAM slots are
                 * occupied, or in its SRAM slot until F2 accepts it. */
                if (!credit) {
                    if (station_enabled) ++MAC_RADIO->rx_waits;
                    continue;
                }
            }
            uint32_t total = MULTI->captured_frames;
            uint32_t idx = total & (FW_MAX_FRAMES - 1u);
            volatile fw_frame_slot_t *slot = &MULTI->frames[idx];
            if (!capture_prepared) {
                uint16_t rxe_len = shm_read16(0x606u);
                uint16_t fc_word = shm_read16(CLEAN_SHM_FRAME + 4u);
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
                        (uint8_t)ta_w2, (uint8_t)(ta_w2 >> 8u)};
                    d11_transmit_ack_frame(ta, channel, mac_en & ~2u);
                    mmio_write32(D11_MACCONTROL, mac_en);
                }
                slot->frame_index = (uint16_t)(total + 1u);
#if CLEAN_RX_HT_OBSERVER_FIFO
                uint16_t correlation = shm_read16(CLEAN_OBSERVER_HELD);
#if CLEAN_RX_HT_ADVANCE
                if (correlation && correlation == shm_read16(CLEAN_OBSERVER_EARLY_SHM) &&
                    shm_read16(CLEAN_OBSERVER_ADVANCE) == 1u)
                    correlation |= CLEAN_OBSERVER_ADVANCE_TAG;
#endif
                observer_capture_ids[idx] = correlation;
#endif
                slot->rxe_len = rxe_len;
                slot->rxe_status1 = shm_read16(0x7a7u);
                slot->rxe_status2 = shm_read16(0x603u);
                /* The held PSM header belongs to this capture; DMA can lag it. */
                slot->phy_status0 = shm_read16(CLEAN_SHM_PHY_STATUS0);
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
                if ((slot->phy_status0 & 3u) >= 2u && rxe_len >= CLEAN_PLCP_BYTES + 14u) {
                    HT_RX->rxe_length = rxe_len;
                    for (uint32_t i = 0u; i < 4u; ++i)
                        HT_RX->phy[i] = shm_read16(CLEAN_SHM_PHY_STATUS0 + i);
                    for (uint32_t i = 0u; i < 6u; ++i) HT_RX->sig[i] = slot->raw[2u+i];
                    uint32_t length = rxe_len - CLEAN_PLCP_BYTES;
                    for (uint32_t i = 0u; i < sizeof(HT_RX->mpdu); ++i)
                        HT_RX->mpdu[i] = i < length ? slot->raw[8u+i] : 0u;
                    ++HT_RX->captures;
                }
                slot->dma_rx_len = 0u;
                slot->dma_completed = 0u;
                slot->f2_received = 0u;
                slot->f2_sdpcm_len = 0u;
                (void)rx_dma_poll_and_reclaim(slot);
                capture_prepared = 1u;
            }
            if (station_mac || CLEAN_RX_AMPDU_SHM || CLEAN_RX_HT_OBSERVER_FIFO) {
                capture_prepared = 0u;
                MULTI->captured_frames = total + 1u;
                if (!scanner_mode && total + 1u >= target_frames)
                    capture_target_reached = 1u;
                shm_write16(0x601u, 0u);
                nav_sampled = 0u;
                continue;
            }
            int posted = sdio_f2_send_frame(slot, channel);
            if (MBOX->fw_state == FW_STATE_ERROR) break;
            if ((station_enabled || CLEAN_RX_HT_DATA) && !posted) {
                if (station_enabled) ++MAC_RADIO->rx_waits;
                continue;
            }
            capture_prepared = 0u;
            MULTI->captured_frames = total + 1u;
#if CLEAN_RX_AMPDU_DMA
            rx_published_frames = total + 1u;
#endif
            if (!scanner_mode && total + 1u >= target_frames)
                break;
            shm_write16(0x601u, 0u);
            nav_sampled = 0u;
        }
    }

#if CLEAN_RX_HT_WINDOW
    ++*(volatile uint32_t *)CLEAN_OBSERVER_PHY_ADDR;
    __asm__ volatile("dsb" ::: "memory");
#endif
    MBOX->rx_elapsed_ticks = ARM_CYCLE_COUNTER - rx_start;
    MBOX->rx_polls = polls;

    mmio_write32(D11_MACCONTROL, 0x04020400u);
    (void)mmio_read32(D11_MACCONTROL);
#if CLEAN_RX_HT_PHY_SNAPSHOT
    snapshot_observer_phy(1u);
#endif
    phy_tx_set_inhibit(1);
    MULTI->dma_status0 = mmio_read32(D11_RX_DMA_STATUS0);
    MULTI->dma_status1 = mmio_read32(D11_RX_DMA_STATUS1);
    (void)delay_ticks(20000u);
    rx_dma_stop();
#if CLEAN_RX_HT_COPY_DMA_PROBE
    volatile uint32_t *dma_trace = (volatile uint32_t *)CLEAN_OBSERVER_DMA_ADDR;
    uint32_t stopped_status = mmio_read32(D11_RX_DMA_STATUS0);
    uint32_t stopped_control = mmio_read32(D11_RX_DMA_CTL);
    dma_trace[30] = stopped_status;
    dma_trace[31] = stopped_control;
    if ((stopped_status>>28u) || stopped_control) {
        fail(0x8du,D11_RX_DMA_STATUS0,stopped_status);
    } else {
        __asm__ volatile("dsb" ::: "memory");
        uint32_t stopped_status1 = mmio_read32(D11_RX_DMA_STATUS1);
        for (uint32_t i = 0u; i < 32u; ++i)
            observer_dma_sample((const volatile uint8_t *)(FW_D11_DMA_BUF_ADDR+
                i*FW_D11_DMA_BUF_BYTES),i,stopped_status,stopped_status1,0x80000000u);
    }
#endif

    if (station_hw) snapshot_dcf_trace();
    if (station_mac) {
        snapshot_ack_test();
        snapshot_ack_rx();
    }
#if CLEAN_RX_AMPDU_PROBE
    volatile clean_ampdu_probe_trace_t *aggregate =
        (volatile clean_ampdu_probe_trace_t *)CLEAN_AMPDU_PROBE_ADDR;
    volatile uint16_t *aggregate_words = (volatile uint16_t *)(CLEAN_AMPDU_PROBE_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_AMPDU_PROBE_WORDS; ++i)
        aggregate_words[i] = shm_read16(CLEAN_AMPDU_PROBE_SHM + i);
    aggregate->magic = CLEAN_AMPDU_PROBE_MAGIC;
#endif
#if CLEAN_RX_AMPDU_INTERFACE
    volatile uint16_t *interface_words = (volatile uint16_t *)(CLEAN_AMPDU_IF_ADDR + 4u);
    for (uint32_t i = 0u; i < CLEAN_AMPDU_IF_WORDS; ++i)
        interface_words[i] = shm_read16(CLEAN_AMPDU_IF_SHM + i);
    ((volatile clean_ampdu_interface_t *)CLEAN_AMPDU_IF_ADDR)->magic = CLEAN_AMPDU_IF_MAGIC;
#endif
    if (station_mac) {
        volatile clean_auth_failure_t *failure =
            (volatile clean_auth_failure_t *)CLEAN_AUTH_FAILURE_ADDR;
        volatile uint16_t *words = (volatile uint16_t *)(CLEAN_AUTH_FAILURE_ADDR + 4u);
        for (uint32_t i = 0u; i < CLEAN_AUTH_FAILURE_WORDS; ++i)
            words[i] = shm_read16(CLEAN_AUTH_FAILURE_SHM + i);
        failure->magic = CLEAN_AUTH_FAILURE_MAGIC;
    }
    volatile uint16_t *early = (volatile uint16_t *)&HT_DIAG->early;
    for (uint32_t i = 0u; i < CLEAN_HT_EARLY_WORDS; ++i)
        early[i] = shm_read16(CLEAN_HT_EARLY_SHM + i);
    if (CLEAN_RX_HT_RESPONSE) {
        volatile clean_ht_failure_t *failure = (volatile clean_ht_failure_t *)CLEAN_HT_FAILURE_ADDR;
        volatile uint16_t *words = (volatile uint16_t *)(CLEAN_HT_FAILURE_ADDR + 4u);
        for (uint32_t i = 0u; i < CLEAN_HT_FAILURE_WORDS; ++i)
            words[i] = shm_read16(CLEAN_HT_FAILURE_SHM + i);
        failure->magic = CLEAN_HT_FAILURE_MAGIC;
    }
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
