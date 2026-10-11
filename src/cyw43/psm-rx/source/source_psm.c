#include "source_psm.h"

struct source_psm_preflight_result source_psm_preflight(
    const struct source_psm_image *image,
    uint32_t expected_generation,
    const uint16_t mailbox[SOURCE_PSM_MAILBOX_WORDS],
    const uint8_t *code_readback,
    uint32_t code_readback_bytes)
{
    struct source_psm_preflight_result result = {
        0u, SOURCE_PSM_REQUIRED_BLOCKERS, 0u
    };
    uint32_t actual_build;
    uint32_t actual_generation;
    uint32_t i;

    if ((image != &source_psm_cold_probe && image != &source_psm_rxe_transfer &&
         image != &source_psm_precopy_probe) ||
        mailbox == 0 || expected_generation == 0u) {
        result.evidence_errors = SOURCE_PSM_EVIDENCE_ARGUMENT;
        return result;
    }
    if (code_readback == 0 || code_readback_bytes != image->byte_count) {
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_IMAGE_BYTES;
    } else {
        for (i = 0; i < image->byte_count; ++i) {
            if (code_readback[i] != image->bytes[i]) {
                result.evidence_errors |= SOURCE_PSM_EVIDENCE_IMAGE_BYTES;
                break;
            }
        }
    }
    actual_build = (uint32_t)mailbox[SOURCE_PSM_MB_BUILD_LO] |
                   ((uint32_t)mailbox[SOURCE_PSM_MB_BUILD_HI] << 16);
    actual_generation = (uint32_t)mailbox[SOURCE_PSM_MB_GENERATION_LO] |
                        ((uint32_t)mailbox[SOURCE_PSM_MB_GENERATION_HI] << 16);
    if (mailbox[SOURCE_PSM_MB_ABI] != SOURCE_PSM_ABI)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_ABI;
    if (actual_build != image->build_id)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_BUILD;
    if (actual_generation != expected_generation)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_GENERATION;
    if (mailbox[SOURCE_PSM_MB_FAULT] != SOURCE_PSM_FAULT_NONE)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_PRIMARY_FAULT;
    if (mailbox[SOURCE_PSM_MB_RECOVERY_FAULT] != SOURCE_PSM_RECOVERY_NONE)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_RECOVERY_FAULT;
    if (mailbox[SOURCE_PSM_MB_STATE] != SOURCE_PSM_STATE_FAULT &&
        mailbox[SOURCE_PSM_MB_STATE] != SOURCE_PSM_STATE_STOPPED_AFTER_CYCLE &&
        (image != &source_psm_precopy_probe ||
         mailbox[SOURCE_PSM_MB_STATE] != SOURCE_PSM_STATE_STOPPED_AFTER_PREFIX))
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_NONTERMINAL;
    if (mailbox[SOURCE_PSM_MB_BLOCKERS] != SOURCE_PSM_REQUIRED_BLOCKERS)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_BLOCKER_WORD;
    if ((mailbox[SOURCE_PSM_MB_STATE] == SOURCE_PSM_STATE_STOPPED_AFTER_CYCLE &&
         (image != &source_psm_rxe_transfer ||
          mailbox[SOURCE_PSM_MB_PUBLISH_MAY_HAVE_ISSUED] != 1u ||
          mailbox[SOURCE_PSM_MB_CONTROL_CYCLE_OBSERVED] != 1u ||
          mailbox[SOURCE_PSM_MB_FAULT] != SOURCE_PSM_FAULT_NONE ||
          mailbox[SOURCE_PSM_MB_RECOVERY_FAULT] != SOURCE_PSM_RECOVERY_NONE)) ||
        (mailbox[SOURCE_PSM_MB_STATE] == SOURCE_PSM_STATE_STOPPED_AFTER_PREFIX &&
         (image != &source_psm_precopy_probe ||
          mailbox[SOURCE_PSM_MB_FAULT] != SOURCE_PSM_FAULT_NONE ||
          mailbox[SOURCE_PSM_MB_RECOVERY_FAULT] != SOURCE_PSM_RECOVERY_NONE ||
          mailbox[SOURCE_PSM_MB_RXCTL] != SOURCE_PSM_PRECOPY_RXCTL_READBACK ||
          (mailbox[SOURCE_PSM_MB_CORECTL] & 0x20a0u) != 0x2020u ||
          mailbox[SOURCE_PSM_MB_RX_LENGTH] < 6u ||
          mailbox[SOURCE_PSM_MB_RX_LENGTH] > SOURCE_PSM_MAX_DIAGNOSTIC_FRAME_BYTES)) ||
        (image == &source_psm_precopy_probe &&
         (mailbox[SOURCE_PSM_MB_PUBLISH_MAY_HAVE_ISSUED] != 0u ||
          mailbox[SOURCE_PSM_MB_CONTROL_CYCLE_OBSERVED] != 0u ||
          mailbox[SOURCE_PSM_MB_RECOVERY_FAULT] != 0u)) ||
        (mailbox[SOURCE_PSM_MB_STATE] == SOURCE_PSM_STATE_FAULT &&
         ((mailbox[SOURCE_PSM_MB_FAULT] == SOURCE_PSM_FAULT_NONE &&
           mailbox[SOURCE_PSM_MB_RECOVERY_FAULT] == SOURCE_PSM_RECOVERY_NONE) ||
          mailbox[SOURCE_PSM_MB_CONTROL_CYCLE_OBSERVED] != 0u)) ||
        mailbox[SOURCE_PSM_MB_PUBLISH_MAY_HAVE_ISSUED] > 1u ||
        mailbox[SOURCE_PSM_MB_CONTROL_CYCLE_OBSERVED] > 1u ||
        mailbox[14] != 0u || mailbox[15] != 0u)
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_INCONSISTENT_STATE;
    return result;
}

uint32_t source_psm_precopy_ready(
    uint32_t expected_generation,
    const uint16_t mailbox[SOURCE_PSM_MAILBOX_WORDS],
    const uint8_t *code_readback,
    uint32_t code_readback_bytes)
{
    struct source_psm_preflight_result result = source_psm_preflight(
        &source_psm_precopy_probe, expected_generation, mailbox,
        code_readback, code_readback_bytes);
    if (mailbox != 0 && mailbox[SOURCE_PSM_MB_STATE] == SOURCE_PSM_STATE_MAC_WAIT)
        result.evidence_errors &= ~SOURCE_PSM_EVIDENCE_NONTERMINAL;
    else
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_NONTERMINAL;
    return result.evidence_errors;
}

struct source_psm_preflight_result source_psm_precopy_evidence(
    uint32_t expected_generation,
    const uint16_t mailbox[SOURCE_PSM_MAILBOX_WORDS],
    const uint8_t *code_readback,
    uint32_t code_readback_bytes,
    const uint16_t prefix[SOURCE_PSM_HEADER_WORDS])
{
    struct source_psm_preflight_result result = source_psm_preflight(
        &source_psm_precopy_probe, expected_generation, mailbox,
        code_readback, code_readback_bytes);
    uint32_t crc = 0xffffu;
    uint32_t i, bit;
    uint16_t signal_service;

    if (mailbox == 0 || prefix == 0) {
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_ARGUMENT;
        return result;
    }
    if (mailbox[SOURCE_PSM_MB_STATE] != SOURCE_PSM_STATE_STOPPED_AFTER_PREFIX ||
        prefix[SOURCE_PSM_PREFIX_FORMAT] != SOURCE_PSM_PREFIX_VERSION ||
        prefix[SOURCE_PSM_PREFIX_COMMITTED] != 1u ||
        prefix[SOURCE_PSM_PREFIX_TAG] != SOURCE_PSM_HEADER_TAG ||
        prefix[SOURCE_PSM_PREFIX_LENGTH_BEFORE] != mailbox[SOURCE_PSM_MB_RX_LENGTH] ||
        prefix[SOURCE_PSM_PREFIX_LENGTH_AFTER] != prefix[SOURCE_PSM_PREFIX_LENGTH_BEFORE] ||
        prefix[SOURCE_PSM_PREFIX_STATUS_BEFORE] != prefix[SOURCE_PSM_PREFIX_STATUS_AFTER] ||
        (prefix[SOURCE_PSM_PREFIX_STATUS_BEFORE] & 0x9000u) != 0x1000u ||
        prefix[SOURCE_PSM_PREFIX_CORECTL] != mailbox[SOURCE_PSM_MB_CORECTL] ||
        prefix[SOURCE_PSM_PREFIX_RXCTL] != mailbox[SOURCE_PSM_MB_RXCTL])
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_PREFIX_SNAPSHOT;

    for (i = 0u; i < 4u; ++i) {
        crc ^= (prefix[SOURCE_PSM_PREFIX_PLCP0 + i / 2u] >> ((i & 1u) * 8u)) & 0xffu;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0x8408u & (0u - (crc & 1u)));
    }
    signal_service = prefix[SOURCE_PSM_PREFIX_PLCP0];
    uint16_t signal = signal_service & 0xffu;
    if ((signal != 0x0au && signal != 0x14u && signal != 0x37u && signal != 0x6eu) ||
        (signal_service & 0x7b00u) != 0u ||
        (signal != 0x6eu && (signal_service & 0x8000u) != 0u) ||
        prefix[SOURCE_PSM_PREFIX_PLCP1] == 0u ||
        (crc ^ 0xffffu) != prefix[SOURCE_PSM_PREFIX_PLCP2])
        result.evidence_errors |= SOURCE_PSM_EVIDENCE_CCK_PLCP;
    return result;
}
