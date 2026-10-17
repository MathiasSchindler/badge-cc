#ifndef CLEAN_RX_MAC_H
#define CLEAN_RX_MAC_H

#include "../../src/shared/stdint.h"
#include "frame_limits.h"

#define CLEAN_MAC_CW_MAX 1023u
#define CLEAN_MAC_RX_LIFETIME_US 512000u
#define CLEAN_MAC_RX_HISTORY 64u

typedef struct {
    uint32_t sampled_us, idle_us, nav_until, random;
    uint16_t cw, cw_min, cw_max, slots, ifs_us;
    uint8_t slot_us, idle, nav_valid, aifsn;
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
    uint8_t aifsn, acm, ecw_min, ecw_max;
    uint16_t txop_32us;
} clean_edca_ac_t;

typedef struct {
    clean_dcf_t ac[4];
    clean_edca_ac_t parameters[4];
    uint8_t selected, tid, enabled;
} clean_edca_t;

/* WMM ACI order: BE, BK, VI, VO. One outstanding controller TX at a time. */
int clean_edca_decode(clean_edca_ac_t ac[4], const volatile uint8_t bytes[16]);
int clean_edca_valid(const clean_edca_ac_t ac[4]);
int clean_edca_configure(clean_edca_t *s, const clean_edca_ac_t ac[4],
                         uint32_t now, uint32_t seed, uint8_t short_slot);
int clean_edca_select(clean_edca_t *s, uint32_t tid, uint32_t now);
int clean_edca_txop_valid(const clean_edca_t *s, uint32_t exchange_us);

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
    uint8_t frame[26u + CLEAN_MSDU_BYTES + 4u];
    uint32_t started_us, length;
    uint16_t sequence;
    uint8_t active, next_fragment;
    uint32_t delivered, duplicates, fragments, malformed, unsupported;
    uint32_t expired, fragment_errors;
} clean_mac_rx_t;

void clean_mac_rx_init(clean_mac_rx_t *s);
void clean_mac_rx_expire(clean_mac_rx_t *s, uint32_t now);
/* FCS-bearing, pinned-peer, non-QoS MPDUs. DELIVER uses the original input for
 * fragment zero, or s->frame/s->length for a completed fragmented MSDU.
 * Protected frames bypass this cache: authenticate/decrypt before delivery. */
int clean_mac_rx(clean_mac_rx_t *s, const uint8_t *mpdu, uint32_t length,
                 uint32_t now);
/* Only call after CCMP MIC/PN validation. Keep Protected set in the header,
 * replace the CCMP body with plaintext and append a reconstructed FCS. */
int clean_mac_rx_authenticated(clean_mac_rx_t *s, const uint8_t *mpdu,
                               uint32_t length, uint32_t now);

typedef struct {
    clean_mac_rx_t tid[16];
} clean_qos_rx_t;

void clean_qos_rx_init(clean_qos_rx_t *s);
void clean_qos_rx_expire(clean_qos_rx_t *s, uint32_t now);
/* Pinned-peer, three-address QoS Data/Null with normal/No Ack policy.
 * Fragment output resides in tid[MPDU TID].frame; unfragmented delivery
 * uses the original input. Block ACK/HT Control require separate support. */
int clean_qos_rx(clean_qos_rx_t *s, const uint8_t *mpdu, uint32_t length, uint32_t now);
/* Authenticated plaintext with Protected retained and reconstructed FCS.
 * Protected A-MSDUs remain disabled until negotiated crypto handling exists. */
int clean_qos_rx_authenticated(clean_qos_rx_t *s, const uint8_t *mpdu,
                               uint32_t length, uint32_t now);
uint32_t clean_mac_fcs(const volatile uint8_t *bytes, uint32_t length);
int clean_mac_valid(const volatile uint8_t *mpdu, uint32_t length);
int clean_mac_retry(volatile uint8_t *mpdu, uint32_t length);
int clean_mac_nav_duration(const volatile uint8_t *mpdu, uint32_t length,
                           const uint8_t own[6]);

typedef struct {
    const volatile uint8_t *body;
    uint32_t length, offset;
} clean_amsdu_t;

typedef struct {
    const volatile uint8_t *destination, *source, *payload;
    uint16_t length;
} clean_amsdu_subframe_t;

/* Validate the whole plaintext aggregate before exposing any subframe.
 * The caller retains immutable storage and performs peer/MIC/replay checks. */
int clean_amsdu_open(clean_amsdu_t *s, const volatile uint8_t *body, uint32_t length);
int clean_amsdu_next(clean_amsdu_t *s, clean_amsdu_subframe_t *subframe);

#endif
