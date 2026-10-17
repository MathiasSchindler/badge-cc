#ifndef CLEAN_RX_MAC_H
#define CLEAN_RX_MAC_H

#include "../../src/shared/stdint.h"
#include "frame_limits.h"

#define CLEAN_MAC_CW_MAX 1023u
#define CLEAN_MAC_RX_LIFETIME_US 512000u
#define CLEAN_MAC_RX_HISTORY 64u

typedef struct {
    uint32_t sampled_us, idle_us, nav_until, random;
    uint16_t cw, cw_min, slots, ifs_us;
    uint8_t slot_us, idle, nav_valid;
} clean_dcf_t;

void clean_dcf_init(clean_dcf_t *s, uint32_t now, uint32_t seed, uint8_t short_slot);
int clean_dcf_poll(clean_dcf_t *s, uint32_t now, int busy);
void clean_dcf_nav(clean_dcf_t *s, uint32_t now, uint16_t duration);
void clean_dcf_bad_rx(clean_dcf_t *s, uint32_t now);
void clean_dcf_good_rx(clean_dcf_t *s);
void clean_dcf_complete(clean_dcf_t *s, uint32_t now, int acked);
void clean_dcf_new_frame(clean_dcf_t *s, uint32_t now);
void clean_dcf_slot(clean_dcf_t *s, uint32_t now, uint8_t short_slot);

typedef struct {
    uint16_t sequence, fragments;
    uint8_t valid, subtype, protected_frame, delivered;
} clean_mac_history_t;

enum {
    CLEAN_MAC_INVALID = -1, CLEAN_MAC_UNSUPPORTED = -2,
    CLEAN_MAC_FRAGMENT_ORDER = -3, CLEAN_MAC_FRAGMENT_BUSY = -4,
    CLEAN_MAC_IGNORED = 0, CLEAN_MAC_DELIVER = 1,
    CLEAN_MAC_FRAGMENT = 2, CLEAN_MAC_DUPLICATE = 3, CLEAN_MAC_PROTECTED = 4
};

typedef struct {
    clean_mac_history_t history[CLEAN_MAC_RX_HISTORY];
    uint8_t frame[24u + CLEAN_MSDU_BYTES + 4u];
    uint32_t started_us, length;
    uint16_t sequence;
    uint8_t active, next_fragment;
    uint32_t delivered, duplicates, fragments, malformed, unsupported;
    uint32_t expired, fragment_errors;
} clean_mac_rx_t;

void clean_mac_rx_init(clean_mac_rx_t *s);
/* FCS-bearing, pinned-peer, non-QoS MPDUs. DELIVER uses the original input for
 * fragment zero, or s->frame/s->length for a completed fragmented MSDU.
 * Protected frames bypass this cache: authenticate/decrypt before delivery. */
int clean_mac_rx(clean_mac_rx_t *s, const uint8_t *mpdu, uint32_t length,
                 uint32_t now);
/* Only call after CCMP MIC/PN validation. Keep Protected set in the header,
 * replace the CCMP body with plaintext and append a reconstructed FCS. */
int clean_mac_rx_authenticated(clean_mac_rx_t *s, const uint8_t *mpdu,
                               uint32_t length, uint32_t now);
uint32_t clean_mac_fcs(const volatile uint8_t *bytes, uint32_t length);
int clean_mac_valid(const volatile uint8_t *mpdu, uint32_t length);
int clean_mac_retry(volatile uint8_t *mpdu, uint32_t length);
int clean_mac_nav_duration(const volatile uint8_t *mpdu, uint32_t length,
                           const uint8_t own[6]);

#endif
