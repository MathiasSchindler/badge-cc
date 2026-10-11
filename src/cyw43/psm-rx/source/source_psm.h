#ifndef SOURCE_PSM_H
#define SOURCE_PSM_H

#include <stdint.h>

#define SOURCE_PSM_ABI 2u
#define SOURCE_PSM_ARCH 15u
#define SOURCE_PSM_CHIP_REV 5u
#define SOURCE_PSM_D11_REV 39u
#define SOURCE_PSM_HARDWARE_QUALIFIED 0u
#define SOURCE_PSM_HAS_COMPLETE_RECEIVER 0u
#define SOURCE_PSM_POLL_BUDGET 64u
#define SOURCE_PSM_PRECOPY_POLL_BUDGET 1023u
#define SOURCE_PSM_PRECOPY_EVENT_WINDOWS 16u
#define SOURCE_PSM_PRECOPY_EVENT_POLLS_PER_WINDOW 65535u
#define SOURCE_PSM_PRECOPY_EVENT_POLLS 1048560u
#define SOURCE_PSM_MAC_WAIT_WINDOWS 16u
#define SOURCE_PSM_MAC_WAIT_POLLS_PER_WINDOW 65535u
#define SOURCE_PSM_MAC_WAIT_POLLS 1048560u

/* All addresses here are 16-bit SHM WORD indices, not ARM or MMIO addresses. */
#define SOURCE_PSM_MAILBOX_WORD 0x0600u
#define SOURCE_PSM_MAILBOX_WORDS 16u
#define SOURCE_PSM_GENERATION_WORD 0x0610u
#define SOURCE_PSM_GENERATION_WORDS 2u
#define SOURCE_PSM_COPY_WORD 0x0620u
#define SOURCE_PSM_COPY_BYTES 180u
#define SOURCE_PSM_HEADER_WORD 0x07f4u
#define SOURCE_PSM_HEADER_WORDS 12u
#define SOURCE_PSM_MAX_DIAGNOSTIC_FRAME_BYTES 1023u
#define SOURCE_PSM_HEADER_TAG 0xda15u
#define SOURCE_PSM_PREFIX_VERSION 4u
#define SOURCE_PSM_PRECOPY_RXCTL_READBACK 0x0800u

enum source_psm_mailbox_offset {
    SOURCE_PSM_MB_ABI = 0,
    SOURCE_PSM_MB_STATE = 1,
    SOURCE_PSM_MB_FAULT = 2,
    SOURCE_PSM_MB_RECOVERY_FAULT = 3,
    SOURCE_PSM_MB_RXCTL = 4,
    SOURCE_PSM_MB_CORECTL = 5,
    SOURCE_PSM_MB_RX_LENGTH = 6,
    SOURCE_PSM_MB_PUBLISH_MAY_HAVE_ISSUED = 7,
    SOURCE_PSM_MB_CONTROL_CYCLE_OBSERVED = 8,
    SOURCE_PSM_MB_BLOCKERS = 9,
    SOURCE_PSM_MB_BUILD_LO = 10,
    SOURCE_PSM_MB_BUILD_HI = 11,
    SOURCE_PSM_MB_GENERATION_LO = 12,
    SOURCE_PSM_MB_GENERATION_HI = 13
};

enum source_psm_state {
    SOURCE_PSM_STATE_CLEARED = 0,
    SOURCE_PSM_STATE_CORE_WAIT = 1,
    SOURCE_PSM_STATE_FLUSH_WAIT = 2,
    SOURCE_PSM_STATE_EVENT_WAIT = 3,
    SOURCE_PSM_STATE_TAIL_ENTRY = 4,
    SOURCE_PSM_STATE_COMPLETE_WAIT = 5,
    SOURCE_PSM_STATE_BUSY_ASSERT_WAIT = 6,
    SOURCE_PSM_STATE_BUSY_CLEAR_WAIT = 7,
    SOURCE_PSM_STATE_RECOVERING_AFTER_CYCLE = 8,
    SOURCE_PSM_STATE_STOPPED_AFTER_CYCLE = 9,
    SOURCE_PSM_STATE_PRECOPY_WAIT = 10,
    SOURCE_PSM_STATE_PREFIX_SNAPSHOT = 11,
    SOURCE_PSM_STATE_STOPPED_AFTER_PREFIX = 12,
    SOURCE_PSM_STATE_MAC_WAIT = 13,
    SOURCE_PSM_STATE_FAULT = 15
};

enum source_psm_fault {
    SOURCE_PSM_FAULT_NONE = 0,
    SOURCE_PSM_FAULT_CORE_TIMEOUT = 1,
    SOURCE_PSM_FAULT_FLUSH_TIMEOUT = 2,
    SOURCE_PSM_FAULT_STALE_EVENTS = 3,
    SOURCE_PSM_FAULT_EVENT_TIMEOUT = 4,
    SOURCE_PSM_FAULT_FULL_OR_BAD_EVENT = 5,
    SOURCE_PSM_FAULT_PRECOPY_UNQUALIFIED = 6,
    SOURCE_PSM_FAULT_ENTRY_PRECONDITION = 7,
    SOURCE_PSM_FAULT_COMPLETE_TIMEOUT = 8,
    SOURCE_PSM_FAULT_BAD_PLCP = 9,
    SOURCE_PSM_FAULT_FCS = 10,
    SOURCE_PSM_FAULT_BUSY_ASSERT_TIMEOUT = 11,
    SOURCE_PSM_FAULT_BUSY_CLEAR_TIMEOUT = 12,
    SOURCE_PSM_FAULT_LENGTH = 13,
    SOURCE_PSM_FAULT_GENERATION = 14,
    SOURCE_PSM_FAULT_PRECOPY_TIMEOUT = 15,
    SOURCE_PSM_FAULT_RX_STATUS = 16,
    SOURCE_PSM_FAULT_PREFIX_CHANGED = 17,
    SOURCE_PSM_FAULT_MAC_TIMEOUT = 18,
    SOURCE_PSM_FAULT_RXCTL_READBACK = 19
};

enum source_psm_recovery_fault {
    SOURCE_PSM_RECOVERY_NONE = 0,
    SOURCE_PSM_RECOVERY_FLUSH_TIMEOUT = 1,
    SOURCE_PSM_RECOVERY_EVENTS_NOT_CLEAR = 2
};

enum source_psm_blocker {
    SOURCE_PSM_BLOCK_COLD_PHY_POWER_RESET_TIMING = 1u << 0,
    SOURCE_PSM_BLOCK_PHYSICAL_TX_INHIBITION = 1u << 1,
    SOURCE_PSM_BLOCK_CORE_ACK_AND_FLUSH_TIMING = 1u << 2,
    SOURCE_PSM_BLOCK_EVENT_EOI_SEMANTICS = 1u << 3,
    SOURCE_PSM_BLOCK_FRESH_PRECOPY = 1u << 4,
    SOURCE_PSM_BLOCK_HEADER_DMA_BINDING = 1u << 5,
    SOURCE_PSM_BLOCK_SOURCE_EXECUTION = 1u << 6
};
#define SOURCE_PSM_REQUIRED_BLOCKERS 127u

struct source_psm_image {
    const uint8_t *bytes;
    uint32_t byte_count;
    uint32_t instruction_words;
    uint32_t max_instructions_to_terminal;
    uint32_t unresolved_blockers;
    uint32_t build_id;
    uint16_t entry_word_pc;
    uint16_t hardware_qualified;
};

/* No loader, activation callback or qualification override is supplied. */
extern const struct source_psm_image source_psm_cold_probe;
extern const struct source_psm_image source_psm_rxe_transfer;
extern const struct source_psm_image source_psm_precopy_probe;

enum source_psm_prefix_offset {
    /* Format4 checks observed post-request readback; format2 was historical.
     * Format3 is fault-only sampling with COMMITTED zero, never acceptance. */
    SOURCE_PSM_PREFIX_FORMAT = 0,
    SOURCE_PSM_PREFIX_LENGTH_BEFORE = 1,
    SOURCE_PSM_PREFIX_STATUS_BEFORE = 2,
    SOURCE_PSM_PREFIX_CORECTL = 3,
    SOURCE_PSM_PREFIX_RXCTL = 4,
    SOURCE_PSM_PREFIX_PLCP0 = 5,
    SOURCE_PSM_PREFIX_PLCP1 = 6,
    SOURCE_PSM_PREFIX_PLCP2 = 7,
    SOURCE_PSM_PREFIX_LENGTH_AFTER = 8,
    SOURCE_PSM_PREFIX_STATUS_AFTER = 9,
    SOURCE_PSM_PREFIX_TAG = 10,
    SOURCE_PSM_PREFIX_COMMITTED = 11
};

enum source_psm_evidence_error {
    SOURCE_PSM_EVIDENCE_ARGUMENT = 1u << 0,
    SOURCE_PSM_EVIDENCE_IMAGE_BYTES = 1u << 1,
    SOURCE_PSM_EVIDENCE_ABI = 1u << 2,
    SOURCE_PSM_EVIDENCE_BUILD = 1u << 3,
    SOURCE_PSM_EVIDENCE_GENERATION = 1u << 4,
    SOURCE_PSM_EVIDENCE_PRIMARY_FAULT = 1u << 5,
    SOURCE_PSM_EVIDENCE_RECOVERY_FAULT = 1u << 6,
    SOURCE_PSM_EVIDENCE_NONTERMINAL = 1u << 7,
    SOURCE_PSM_EVIDENCE_BLOCKER_WORD = 1u << 8,
    SOURCE_PSM_EVIDENCE_INCONSISTENT_STATE = 1u << 9,
    SOURCE_PSM_EVIDENCE_PREFIX_SNAPSHOT = 1u << 10,
    SOURCE_PSM_EVIDENCE_CCK_PLCP = 1u << 11
};

struct source_psm_preflight_result {
    uint32_t evidence_errors;
    uint32_t unresolved_blockers;
    uint16_t dma_start_permitted;
};

/*
 * Read-only evidence inspection. Even evidence_errors==0 never authorizes DMA.
 * Caller supplies serialized, authoritative code readback and terminal mailbox
 * snapshot; this function performs no MMIO and manufactures no quiescence.
 */
struct source_psm_preflight_result source_psm_preflight(
    const struct source_psm_image *image,
    uint32_t expected_generation,
    const uint16_t mailbox[SOURCE_PSM_MAILBOX_WORDS],
    const uint8_t *code_readback,
    uint32_t code_readback_bytes);

/* DSSS/CCK rates 1/2/5.5/11 Mbps. Service permits locked-clock and, at 11 Mbps,
 * length-extension bits only. A matching CRC is not proof of fresh reception. */
struct source_psm_preflight_result source_psm_precopy_evidence(
    uint32_t expected_generation,
    const uint16_t mailbox[SOURCE_PSM_MAILBOX_WORDS],
    const uint8_t *code_readback,
    uint32_t code_readback_bytes,
    const uint16_t prefix[SOURCE_PSM_HEADER_WORDS]);

/* Returns evidence-error bits for a stable complete MAC_WAIT mailbox.
 * Zero means bound READY only, never prefix/RX qualification or DMA permission. */
uint32_t source_psm_precopy_ready(
    uint32_t expected_generation,
    const uint16_t mailbox[SOURCE_PSM_MAILBOX_WORDS],
    const uint8_t *code_readback,
    uint32_t code_readback_bytes);

#endif
