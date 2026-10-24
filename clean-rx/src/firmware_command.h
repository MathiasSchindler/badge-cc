#ifndef CLEAN_RX_FIRMWARE_COMMAND_H
#define CLEAN_RX_FIRMWARE_COMMAND_H

#include "firmware_profile.h"
#include "ht_contract.h"

static inline int clean_runtime_command_allowed(uint32_t command, uint8_t version) {
    uint32_t kind = command & 255u;
    return kind != 9u && kind != CLEAN_HT_PROBE_CMD && kind != CLEAN_AMPDU_BA_CMD &&
        (kind != CLEAN_HT_DATA_CMD || version == CLEAN_HT_DATA_VERSION);
}

#endif
