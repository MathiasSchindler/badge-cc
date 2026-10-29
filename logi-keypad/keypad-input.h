#ifndef U26_KEYPAD_INPUT_H
#define U26_KEYPAD_INPUT_H

#include <stdint.h>

static inline int match_hidpp_reply(const uint8_t request[20], uint32_t report_id,
                                    const uint8_t *report, uint32_t length) {
    if (!report || (report_id != 0x11 && report_id != 0x13) ||
        length != (report_id == 0x11 ? 20u : 32u) ||
        report[0] != report_id || report[1] != request[1]) return 0;
    if (report[2] == request[2] && report[3] == request[3]) return 1;
    if (report[2] == 0xff && report[3] == request[2] &&
        report[4] == request[3]) return -1;
    return 0;
}

static inline int decode_button(uint32_t report_id, const uint8_t *report, uint32_t length) {
    if (!report || report_id != 0x13 || length != 32 ||
        report[0] != 0x13 || report[1] != 0xff || report[2] != 0x02 ||
        report[3] != 0x00 || report[5] != 0x01 || report[6] > 9) return -1;
    for (uint32_t i = 7; i < length; ++i) {
        if (report[i] != 0) return -1;
    }
    return report[6];
}

static inline int decode_navigation(uint8_t feature, uint32_t report_id,
                                    const uint8_t *report, uint32_t length) {
    if (!feature || !report || report_id != 0x11 || length != 20 ||
        report[0] != 0x11 || report[1] != 0xff ||
        report[2] != feature || report[3] != 0) return -1;
    int active = 0;
    int ended = 0;
    for (uint32_t i = 4; i < length; i += 2) {
        unsigned cid = ((unsigned)report[i] << 8) | report[i + 1];
        if (cid == 0) {
            ended = 1;
            continue;
        }
        if (ended || (cid != 0x01a1 && cid != 0x01a2)) return -1;
        int bit = cid == 0x01a1 ? 1 : 2;
        if (active & bit) return -1;
        active |= bit;
    }
    return active;
}

#endif
