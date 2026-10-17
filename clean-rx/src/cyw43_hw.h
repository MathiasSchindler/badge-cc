#ifndef CYW43_HW_H
#define CYW43_HW_H

#include "types.h"
#include "frame_limits.h"

/* Core Backplane Base Addresses */
#define CC_BASE              0x18000000u /* ChipCommon + PMU + GCI */
#define D11_BASE             0x18001000u /* 802.11 MAC + RXE + IHR + SHM + PHY/Radio Bridge */
#define SDIOD_BASE           0x18002000u /* SDIO / gSPI Device Core */
#define ARMCM3_BASE          0x18003000u /* ARM Cortex-M3 Core */
#define SOCRAM_BASE          0x18004000u /* SoC SRAM Controller */

/* AI Wrapper Base Addresses */
#define D11_WRAP_BASE        0x18101000u /* D11 AI Wrapper (IOCTRL +0x408, RESETCTRL +0x800) */
#define ARM_WRAP_BASE        0x18103000u /* ARM AI Wrapper */
#define RAM_WRAP_BASE        0x18104000u /* SOCRAM AI Wrapper */

/* ChipCommon & PMU Registers (offsets from 0x18000000) */
#define CC_CAPABILITIES      0x18000004u
#define CC_CAPABILITIES_EXT  0x180000acu
#define CC_CLK_CTL_ST        0x180001e0u
#define CC_SR_CONTROL0       0x18000504u
#define CC_SR_CONTROL1       0x18000508u
#define PMU_CONTROL          0x18000600u
#define PMU_CAPABILITIES     0x18000604u
#define PMU_STATUS           0x18000608u
#define PMU_RES_STATE        0x1800060cu
#define PMU_RES_PENDING      0x18000610u
#define PMU_TIMER            0x18000614u
#define PMU_MIN_RES_MASK     0x18000618u
#define PMU_MAX_RES_MASK     0x1800061cu
#define PMU_RES_TABLE_SEL    0x18000620u
#define PMU_RES_DEP_MASK     0x18000624u
#define PMU_RES_UPDN_TIMER   0x18000628u
#define PMU_REG_CONTROL_ADDR 0x18000658u
#define PMU_REG_CONTROL_DATA 0x1800065cu
#define PMU_PLL_CONTROL_ADDR 0x18000660u
#define PMU_PLL_CONTROL_DATA 0x18000664u
#define GCI_OUTPUT_REG1      0x18000d64u
#define GCI_CONTROL_REG1     0x18000d74u

/* D11 Core & DMA Registers (offsets from 0x18001000) */
#define D11_BIST_STATUS      0x1800100cu
#define D11_INT_CONTROL      0x18001100u
#define D11_MACCONTROL       0x18001120u
#define D11_MACCOMMAND       0x18001124u
#define D11_MACINTSTATUS     0x18001128u
#define D11_MACINTMASK       0x1800112cu
#define D11_TPL_WR_PTR       0x18001130u
#define D11_TPL_WR_DATA      0x18001134u
#define D11_OBJ_ADDR         0x18001160u
#define D11_OBJ_DATA         0x18001164u
#define D11_MACHWCAP         0x1800115cu
#define D11_TSF_CFP_MAXDUR32  0x18001190u
#define D11_CLK_CTL_ST       0x180011e0u
#define D11_RX_DMA_CTL       0x18001220u
#define D11_RX_DMA_PTR       0x18001224u
#define D11_RX_DMA_ADDRLOW   0x18001228u
#define D11_RX_DMA_ADDRHIGH  0x1800122cu
#define D11_RX_DMA_STATUS0   0x18001230u
#define D11_RX_DMA_STATUS1   0x18001234u
#define D11_RADIO_REG_ADDR   0x180013d8u
#define D11_RADIO_REG_DATA   0x180013dau
#define D11_PHY_REG_ADDR     0x180013fcu
#define D11_PHY_REG_DATA     0x180013feu

/* SDIOD Function 2 TX DMA Registers */
#define SDIOD_TX_DMA_CTL      0x18002200u
#define SDIOD_TX_DMA_PTR      0x18002204u
#define SDIOD_TX_DMA_ADDRLOW  0x18002208u
#define SDIOD_TX_DMA_ADDRHIGH 0x1800220cu
#define SDIOD_TX_DMA_STATUS0  0x18002210u
#define SDIOD_TX_DMA_STATUS1  0x18002214u

/* ARM Cortex-M3 Core Cycle Counter */
#define ARM_CYCLE_COUNTER    (*(volatile uint32_t *)0x18003090u)

/* ROM Board Calibration Data Span */
#define ROM_BOARD_DATA_BASE  0x1e09020cu
#define ROM_BOARD_DATA_BYTES 2044u

/* Shared SRAM Mailbox, Frame Capture, and DMA Ring Layout */
#define FW_MAGIC             0x4f505258u /* "OPRX" */
#define FW_MULTI_MAGIC       0x4d554c54u /* "MULT" */

#define FW_MBOX_ADDR         0x00020000u
#define FW_MULTI_ADDR        0x00020d80u
#define FW_D11_DMA_RING_ADDR CLEAN_D11_RING_ADDR
#define FW_D11_DMA_BUF_ADDR  CLEAN_D11_BUFFER_ADDR
#define FW_D11_DMA_BUF_BYTES CLEAN_D11_BUFFER_BYTES
#define FW_D11_DMA_CONTROL   0x0004084du /* parity-disable 0x800 | mode 0x40000 | offset (38<<1) | enable 1 */

#define FW_SDIO_TX_RING_ADDR CLEAN_SDIO_RING_ADDR
#define FW_SDIO_TX_BUF_ADDR  CLEAN_SDIO_BUFFER_ADDR
#define FW_SDIO_TX_BUF_BYTES CLEAN_SDIO_BUFFER_BYTES
#define FW_TX_BUF_ADDR       CLEAN_TX_BUFFER_ADDR

#define FW_MAX_FRAMES        8u

/* Firmware Execution States */
enum {
    FW_STATE_BOOT       = 0u,
    FW_STATE_PMU_PLL    = 1u,
    FW_STATE_PMU_RES    = 2u,
    FW_STATE_WAIT_HT    = 3u,
    FW_STATE_D11_INIT   = 4u,
    FW_STATE_COLD_PHY   = 5u,
    FW_STATE_EXTRA_INIT = 6u,
    FW_STATE_PSM_LOAD   = 7u,
    FW_STATE_PSM_READY  = 8u,
    FW_STATE_RX_ACTIVE  = 9u,
    FW_STATE_RX_DONE    = 10u,
    FW_STATE_ERROR      = 0xffu
};

/* Configuration Flags */
#define CFG_D11_RXE          (1u << 0)
#define CFG_D11_BRANCH_MASKS (1u << 1)
#define CFG_D11_BTCX_RFSW    (1u << 2)
#define CFG_D11_IFS_TSF_CLK  (1u << 3)
#define CFG_RADIO_LPF        (1u << 4)
#define CFG_PROMISCUOUS      (1u << 5)
#define CFG_CONTINUOUS_RX    (1u << 7)
#define CFG_TX_ENABLE        (1u << 8)
#define CFG_STA_ROLE         (1u << 9)
#define CFG_WPA2_CCMP        (1u << 10) /* Enable D11 Hardware AES-128-CCMP Encryption & Decryption */

#define CFG_CHANNEL_SHIFT    16u
#define CFG_CHANNEL_MASK     0x00ff0000u
#define CFG_CHANNEL_GET(f)   ((((f) & CFG_CHANNEL_MASK) >> CFG_CHANNEL_SHIFT) ? (((f) & CFG_CHANNEL_MASK) >> CFG_CHANNEL_SHIFT) : 1u)

/* PSM SHM State Values (SHM word 0x601) */
#define PSM_STATE_WAIT_RX    3u
#define PSM_STATE_READY      13u
#define PSM_STATE_CAPTURED   14u
#define PSM_STATE_FAULT      15u

typedef struct {
    uint16_t frame_index;
    uint16_t rxe_len;
    uint16_t rxe_status1;
    uint16_t rxe_status2;
    uint16_t dma_rx_len;
    uint8_t  dma_completed;
    uint8_t  f2_received;
    uint16_t f2_sdpcm_len;
    uint16_t rx_tick_hi;
    uint8_t  dma_hdr[38];
    uint16_t phy_status0;
    uint8_t  raw[CLEAN_RAW_BYTES];
} fw_frame_slot_t;

_Static_assert(sizeof(fw_frame_slot_t) == 56u + CLEAN_RAW_BYTES, "Frame slot layout");

typedef struct {
    uint32_t magic;
    uint32_t target_frames;
    uint32_t captured_frames;
    uint32_t discard_bad_fcs;
    uint32_t discard_oversize;
    uint32_t discard_rx_event;
    uint32_t discard_plcp;
    uint32_t dma_frames;
    uint32_t dma_status0;
    uint32_t dma_status1;
    fw_frame_slot_t frames[FW_MAX_FRAMES];
} fw_multi_t;

_Static_assert(sizeof(fw_multi_t) == 40u + FW_MAX_FRAMES * sizeof(fw_frame_slot_t),
               "Multi buffer layout");
_Static_assert(FW_MULTI_ADDR + sizeof(fw_multi_t) <= FW_D11_DMA_RING_ADDR,
               "Capture slots must not overlap D11 ring");
_Static_assert(FW_D11_DMA_BUF_ADDR + 32u * FW_D11_DMA_BUF_BYTES <= FW_SDIO_TX_RING_ADDR,
               "D11 slots must not overlap SDIO ring");
_Static_assert(38u + CLEAN_RXE_BYTES <= FW_D11_DMA_BUF_BYTES, "D11 receive slot size");

typedef struct {
    uint32_t magic;
    uint32_t fw_state;
    uint32_t fw_substage;
    uint32_t fw_error;
    uint32_t fw_error_addr;
    uint32_t fw_error_val;
    uint32_t host_cmd;
    uint32_t config_flags;
    uint32_t generation;
    uint32_t rc_lpf;
    uint32_t rc_adc;
    uint32_t rc_dacbuf;
    uint32_t vco_status;
    uint32_t dc_idac;
    uint32_t dc_dcoe;
    uint32_t rx_polls;
    uint32_t rx_elapsed_ticks;
    uint32_t maccontrol_active;
    uint16_t psm_mailbox[16];
    uint16_t psm_diag[16];
    uint16_t rxe_pre[8];
    uint16_t rxe_post[8];
    uint8_t  rx_snapshot[96];
    uint8_t  rx_input[180];
    uint32_t f2_counts;
    uint32_t f2_consumed;
    uint32_t f2_posted;
    uint16_t rx_fifo_fault[CLEAN_RX_FAULT_WORDS];
} fw_mbox_t;

_Static_assert(sizeof(fw_mbox_t) == 480u, "Mailbox size");
_Static_assert(__builtin_offsetof(fw_mbox_t, rx_fifo_fault) == 456u, "RX fault snapshot offset");

static inline uint32_t mmio_read32(uint32_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}
static inline void mmio_write32(uint32_t addr, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)addr = val;
    __asm__ volatile("dsb" ::: "memory");
}
static inline uint16_t mmio_read16(uint32_t addr) {
    return *(volatile uint16_t *)(uintptr_t)addr;
}
static inline void mmio_write16(uint32_t addr, uint16_t val) {
    *(volatile uint16_t *)(uintptr_t)addr = val;
    __asm__ volatile("dsb" ::: "memory");
}
static inline uint8_t mmio_read8(uint32_t addr) {
    return *(volatile uint8_t *)(uintptr_t)addr;
}
static inline void mmio_write8(uint32_t addr, uint8_t val) {
    *(volatile uint8_t *)(uintptr_t)addr = val;
    __asm__ volatile("dsb" ::: "memory");
}

static inline int delay_ticks(uint32_t ticks) {
    uint32_t start = ARM_CYCLE_COUNTER;
    for (uint32_t i = 0u; i < 20000000u; ++i) {
        if (ARM_CYCLE_COUNTER - start >= ticks) return 1;
    }
    return 0;
}

static inline uint16_t phy_reg_read(uint16_t reg) {
    mmio_write16(D11_PHY_REG_ADDR, reg);
    return mmio_read16(D11_PHY_REG_DATA);
}
static inline void phy_reg_write(uint16_t reg, uint16_t val) {
    mmio_write32(D11_PHY_REG_ADDR, (uint32_t)reg | ((uint32_t)val << 16u));
}
static inline void phy_reg_mod(uint16_t reg, uint16_t mask, uint16_t val) {
    uint16_t cur = phy_reg_read(reg);
    mmio_write16(D11_PHY_REG_DATA, (uint16_t)((cur & ~mask) | (val & mask)));
}
static inline uint16_t radio_reg_read(uint16_t reg) {
    mmio_write16(D11_RADIO_REG_ADDR, reg);
    return mmio_read16(D11_RADIO_REG_DATA);
}
static inline void radio_reg_write(uint16_t reg, uint16_t val) {
    mmio_write16(D11_RADIO_REG_ADDR, reg);
    mmio_write16(D11_RADIO_REG_DATA, val);
}
static inline void radio_reg_mod(uint16_t reg, uint16_t mask, uint16_t val) {
    uint16_t cur = radio_reg_read(reg);
    radio_reg_write(reg, (uint16_t)((cur & ~mask) | (val & mask)));
}

int pmu_init_pll_and_resources(void);
int d11_and_phy_cold_init(uint32_t *rc_lpf_out, uint32_t *rc_adc_out, uint32_t *rc_dacbuf_out,
                          uint32_t *vco_status_out, uint32_t *dc_idac_out, uint32_t *dc_dcoe_out);
int phy_tune_channel_2g(uint32_t channel, uint32_t *vco_status_out, uint32_t *dc_idac_out, uint32_t *dc_dcoe_out);
int phy_fast_switch_channel_2g(uint32_t channel, uint32_t *vco_status_out);
void phy_tx_hw_configure(uint32_t channel);
void phy_tx_set_inhibit(int inhibit);
void phy_tx_set_automatic(void);

extern const uint8_t psm_ucode_bytes[];
extern const uint32_t psm_ucode_byte_count;

#endif
