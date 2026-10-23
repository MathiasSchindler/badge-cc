#include "health.h"
#include "usb/report.h"

void health_init(health_t *h, const uint32_t id[2], uint32_t now) {
    h->id[0] = id[0]; h->id[1] = id[1];
    h->boot_ms = now;
    h->pending = 1u;
}

void health_update(health_t *h, const health_snapshot_t *s) {
    if (h->current.wifi_stage != s->wifi_stage || h->current.wifi_error != s->wifi_error ||
        h->current.dhcp_state != s->dhcp_state || h->current.network_error != s->network_error ||
        h->current.ip != s->ip || h->current.connected != s->connected ||
        h->current.radio_state != s->radio_state || h->current.radio_error != s->radio_error)
        h->pending = 1u;
    uint8_t *to = (uint8_t *)&h->current;
    const uint8_t *from = (const uint8_t *)s;
    for (uint32_t i = 0u; i < sizeof(*s); ++i) to[i] = from[i];
}

int health_due(const health_t *h, uint32_t now) {
    uint32_t elapsed = now - (h->attempted ? h->last_attempt_ms : h->boot_ms);
    return h->current.radio_state == SX1262_RX_RUNNING && elapsed >= HEALTH_MIN_MS &&
           (h->pending || !h->attempted || elapsed >= HEALTH_HEARTBEAT_MS);
}

void health_payload(health_t *h, uint32_t now, uint8_t payload[LINK_PACKET_SIZE]) {
    ++h->sequence;
    h->last_attempt_ms = now;
    h->attempted = 1u;
    h->pending = 0u;
    usb_store_u32(payload, HEALTH_MAGIC);
    usb_store_u32(payload + 4u, h->id[0]);
    usb_store_u32(payload + 8u, h->id[1]);
    usb_store_u32(payload + 12u, h->sequence);
    usb_store_u32(payload + 16u, now);
    usb_store_u32(payload + 20u, (h->current.wifi_stage & 15u) |
        ((h->current.wifi_error & 255u) << 4u) | ((h->current.dhcp_state & 15u) << 12u) |
        ((h->current.network_error & 255u) << 16u) | ((h->current.connected & 1u) << 24u) |
        ((h->current.radio_state & 3u) << 25u));
    usb_store_u32(payload + 24u, h->current.ip);
    usb_store_u32(payload + 28u, (uint32_t)h->current.radio_error);
    usb_store_u32(payload + 32u, usb_crc32(payload, 32u));
}

static uint32_t text(uint8_t *out, uint32_t n, const char *s) {
    while (*s) out[n++] = (uint8_t)*s++;
    return n;
}

static uint32_t number(uint8_t *out, uint32_t n, uint32_t value) {
    uint8_t digits[10]; uint32_t count = 0u;
    do { digits[count++] = (uint8_t)('0' + value % 10u); value /= 10u; } while (value);
    while (count) out[n++] = digits[--count];
    return n;
}

static uint32_t field(uint8_t *out, uint32_t n, const char *key, uint32_t value) {
    n = text(out, n, key);
    return number(out, n, value);
}

static uint32_t signed_field(uint8_t *out, uint32_t n, const char *key, int32_t value) {
    n = text(out, n, key);
    if (value < 0) { out[n++] = '-'; return number(out, n, 0u - (uint32_t)value); }
    return number(out, n, (uint32_t)value);
}

uint32_t health_http(health_t *h, const http_request_t *request, uint8_t *out, uint32_t capacity) {
    static const uint8_t page[] =
        "<!doctype html><meta name=\"viewport\" content=\"width=device-width\"><title>Pico remote health</title>"
        "<h1>Pico Wi-Fi / LoRa health</h1><p>Private LoRa: 869.850 MHz, SF7/BW125, 0 dBm. "
        "Startup/change reports are coalesced to at most one/minute; heartbeat every five minutes. "
        "Radio errors are shown below. TxDone is not proof of delivery. No acknowledgement protocol. "
        "If both paths fail, remote reporting is unavailable.</p>"
        "<p>Wi-Fi stages: 6 joining, 7 connected, 8 failed. DHCP: 3 bound, 4 renewing, 5 failed. "
        "Radio: 1 receiving, 2 fault. Times are device uptime milliseconds. Error 0 means none.</p>"
        "<p id=\"error\" role=\"alert\"></p><pre id=\"status\">Connecting...</pre>"
        "<p><a href=\"/api/status\">Full radio diagnostics and raw packets</a></p>"
        "<script>async function poll(){try{const r=await fetch('/api/health',"
        "{cache:'no-store',signal:AbortSignal.timeout(10000)});if(!r.ok)throw Error('HTTP '+r.status);"
        "document.getElementById('status').textContent=JSON.stringify(await r.json(),null,2);"
        "document.getElementById('error').textContent='';}catch(e){document.getElementById('error').textContent="
        "'Connection failed; last snapshot may be stale: '+e.message;}setTimeout(poll,3000);}poll();</script>";
    if (request->post) {
        static const uint8_t denied[] = "{\"error\":\"Remote health mode is read-only; radio profile locked\"}";
        return http_response(out, capacity, 403u, "application/json", denied, sizeof(denied)-1u);
    }
    if (request->path[1] == '\0')
        return http_response(out, capacity, 200u, "text/html; charset=utf-8", page, sizeof(page)-1u);
    uint8_t *r = h->json;
    uint32_t n = text(r, 0u, "{\"serial\":\"");
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t w = 2u; w > 0u; --w)
        for (uint32_t i = 8u; i > 0u; --i)
            r[n++] = (uint8_t)hex[(h->id[w-1u] >> ((i-1u)*4u)) & 15u];
    n = field(r,n,"\",\"wifi_stage\":",h->current.wifi_stage);
    n = field(r,n,",\"uptime_ms\":",h->uptime_ms);
    n = field(r,n,",\"wifi_error\":",h->current.wifi_error);
    n = field(r,n,",\"wifi_connected\":",h->current.connected);
    n = field(r,n,",\"dhcp_state\":",h->current.dhcp_state);
    n = field(r,n,",\"network_error\":",h->current.network_error);
    n = text(r,n,",\"ip\":\"");
    for (uint32_t i = 4u; i > 0u; --i) {
        n = number(r,n,(h->current.ip >> ((i-1u)*8u)) & 255u);
        if (i > 1u) r[n++] = '.';
    }
    n = field(r,n,"\",\"radio_state\":",h->current.radio_state);
    n = signed_field(r,n,",\"radio_error\":",h->current.radio_error);
    n = field(r,n,",\"health_attempts\":",h->sequence);
    n = field(r,n,",\"health_tx_done\":",h->sent);
    n = signed_field(r,n,",\"tx_error\":",h->tx_error);
    n = signed_field(r,n,",\"restore_error\":",h->restore_error);
    n = field(r,n,",\"last_attempt_uptime_ms\":",h->last_attempt_ms);
    n = field(r,n,",\"last_tx_done_uptime_ms\":",h->last_success_ms);
    n = field(r,n,",\"peer_seen\":",h->peer_seen);
    n = field(r,n,",\"last_peer_rx_uptime_ms\":",h->last_rx_ms);
    n = text(r,n,",\"delivery_confirmed\":false,\"heartbeat_ms\":300000,\"min_report_ms\":60000}");
    return http_response(out, capacity, 200u, "application/json", r, n);
}
