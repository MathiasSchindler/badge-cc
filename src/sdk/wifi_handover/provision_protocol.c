#include "provision_protocol.h"

#include <string.h>

_Static_assert(EXP_SSID_SIZE == 24u, "wire SSID layout");
_Static_assert(EXP_PASSWORD_SIZE == 33u, "wire PSK layout");
_Static_assert(PROVISION_WIRE_SIZE <= 100u, "bounded provisioning");

static bool valid_strings(const handover_credentials_t *c, size_t *ssid_length) {
    size_t n = 0;
    while (n < sizeof(c->ssid) && c->ssid[n] != '\0') {
        uint8_t ch = (uint8_t)c->ssid[n];
        if (ch < 0x20u || ch > 0x7eu) return false;
        ++n;
    }
    if (n == 0u || n == sizeof(c->ssid)) return false;
    for (size_t i = 0; i < 32u; ++i) {
        char ch = c->password[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return false;
    }
    if (c->password[32] != '\0') return false;
    *ssid_length = n;
    return true;
}

static bool valid(const handover_credentials_t *c, size_t *ssid_length) {
    return c != NULL && c->session_id != 0u && c->token != 0u &&
           c->slot >= 1u && c->slot < EXP_MAX_PLAYERS &&
           c->channel >= 1u && c->channel <= 11u &&
           valid_strings(c, ssid_length);
}

bool provision_encode(const handover_credentials_t *c,
                      uint8_t *wire, size_t capacity) {
    size_t ssid_length;
    if (wire == NULL || capacity < PROVISION_WIRE_SIZE ||
        !valid(c, &ssid_length)) return false;
    uint8_t encoded[PROVISION_WIRE_SIZE] = {PROVISION_VERSION,
                                          PROVISION_WIRE_SIZE};
    encoded[2] = c->slot;
    encoded[3] = c->channel;
    for (unsigned i = 0; i < 4u; ++i)
        encoded[4u + i] = (uint8_t)(c->session_id >> (8u * i));
    for (unsigned i = 0; i < 8u; ++i)
        encoded[8u + i] = (uint8_t)(c->token >> (8u * i));
    memcpy(encoded + 16u, c->ssid, ssid_length);
    memcpy(encoded + 40u, c->password, sizeof(c->password));
    memcpy(wire, encoded, sizeof(encoded));
    return true;
}

bool provision_decode(const uint8_t *wire, size_t length,
                      handover_credentials_t *credentials) {
    if (wire == NULL || credentials == NULL || length != PROVISION_WIRE_SIZE ||
        wire[0] != PROVISION_VERSION || wire[1] != PROVISION_WIRE_SIZE)
        return false;
    handover_credentials_t decoded = {0};
    decoded.slot = wire[2];
    decoded.channel = wire[3];
    for (unsigned i = 0; i < 4u; ++i)
        decoded.session_id |= (uint32_t)wire[4u + i] << (8u * i);
    for (unsigned i = 0; i < 8u; ++i)
        decoded.token |= (uint64_t)wire[8u + i] << (8u * i);
    memcpy(decoded.ssid, wire + 16u, sizeof(decoded.ssid));
    memcpy(decoded.password, wire + 40u, sizeof(decoded.password));
    size_t ssid_length;
    if (!valid(&decoded, &ssid_length)) return false;
    for (size_t i = ssid_length; i < sizeof(decoded.ssid); ++i)
        if (decoded.ssid[i] != '\0') return false;
    *credentials = decoded;
    return true;
}

void provision_rx_init(provision_rx_t *rx) {
    if (rx != NULL) memset(rx, 0, sizeof(*rx));
}

bool provision_rx_append(provision_rx_t *rx, size_t offset,
                         const uint8_t *bytes, size_t length) {
    if (rx == NULL) return false;
    if (rx->failed || offset != rx->length || offset > PROVISION_WIRE_SIZE ||
        length > PROVISION_WIRE_SIZE - offset ||
        (length != 0u && bytes == NULL) ||
        (length == 0u && offset != PROVISION_WIRE_SIZE)) {
        rx->failed = true;
        return false;
    }
    if (length != 0u) memcpy(rx->wire + offset, bytes, length);
    rx->length += length;
    return true;
}

bool provision_rx_finish(const provision_rx_t *rx, bool att_success,
                         uint32_t selected_session,
                         handover_credentials_t *credentials) {
    handover_credentials_t decoded;
    if (rx == NULL || credentials == NULL || rx->failed || !att_success ||
        !provision_decode(rx->wire, rx->length, &decoded) ||
        decoded.session_id != selected_session) return false;
    *credentials = decoded;
    return true;
}
