#include "dashboard.h"
#include "dashboard_page.h"

typedef struct {
    uint8_t *data;
    uint32_t capacity;
    uint32_t used;
    uint8_t failed;
} writer_t;

static int match(const char *a, const char *b) {
    for (uint32_t i = 0u;; ++i) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
    }
}

static void character(writer_t *writer, uint8_t ch) {
    if (writer->used == writer->capacity) writer->failed = 1u;
    else writer->data[writer->used++] = ch;
}

static void text(writer_t *writer, const char *value) {
    for (uint32_t i = 0u; value[i]; ++i) character(writer, (uint8_t)value[i]);
}

static void number(writer_t *writer, uint32_t n) {
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + n % 10u);
        n /= 10u;
    } while (n);
    while (count) character(writer, (uint8_t)digits[--count]);
}

static void signed_number(writer_t *writer, int32_t n) {
    if (n < 0) character(writer, '-');
    number(writer, n < 0 ? 0u - (uint32_t)n : (uint32_t)n);
}

static void hex(writer_t *writer, uint32_t value, uint32_t digits) {
    static const char alphabet[] = "0123456789abcdef";
    for (uint32_t i = digits; i > 0u; --i)
        character(writer, (uint8_t)alphabet[(value >> ((i - 1u) * 4u)) & 15u]);
}

static void field(writer_t *writer, const char *key, uint32_t n) {
    text(writer, key);
    number(writer, n);
}

static void config_json(writer_t *writer, const dashboard_config_t *config) {
    field(writer, "{\"frequency_hz\":", config->frequency_hz);
    field(writer, ",\"sf\":", config->sf);
    field(writer, ",\"bandwidth_khz\":", config->bandwidth_khz);
    character(writer, '}');
}

static uint32_t status_json(dashboard_t *app) {
    writer_t writer = {app->json, sizeof(app->json), 0u, 0u};
    text(&writer, "{\"build_id\":\"");
    hex(&writer, app->build_id, 8u);
    field(&writer, "\",\"uptime_ms\":", app->uptime_ms);
    text(&writer, ",\"ip\":\"");
    for (uint32_t i = 0u; i < sizeof(app->ip) && app->ip[i]; ++i) {
        uint8_t ch = (uint8_t)app->ip[i];
        character(&writer, (ch >= '0' && ch <= '9') || ch == '.' ? ch : '?');
    }
    text(&writer, "\",\"wifi_error\":");
    signed_number(&writer, app->wifi_error);
    text(&writer, ",\"lora_error\":");
    signed_number(&writer, app->lora_error);
    field(&writer, ",\"lora_state\":", app->lora_state);
    field(&writer, ",\"lora_status\":", app->lora_status);
    field(&writer, ",\"lora_command\":", app->lora_command);
    field(&writer, ",\"lora_device_errors\":", app->lora_device_errors);
    field(&writer, ",\"lora_polls\":", app->lora_polls);
    field(&writer, ",\"lora_response\":", app->lora_response);
    field(&writer, ",\"received\":", app->received);
    field(&writer, ",\"crc_errors\":", app->crc_errors);
    field(&writer, ",\"header_errors\":", app->header_errors);
    field(&writer, ",\"overwritten\":", app->overwritten);
    field(&writer, ",\"network_drops\":", app->network_drops);
    field(&writer, ",\"retransmits\":", app->retransmits);
    text(&writer, ",\"config\":");
    config_json(&writer, &app->config);
    text(&writer, ",\"packets\":[");
    if (app->packet_count > DASHBOARD_PACKETS) return 0u;
    for (uint32_t i = 0u; i < app->packet_count; ++i) {
        const dashboard_packet_t *packet = app->packets + i;
        if (packet->length > sizeof(packet->data)) return 0u;
        if (i) character(&writer, ',');
        field(&writer, "{\"sequence\":", packet->sequence);
        field(&writer, ",\"uptime_ms\":", packet->uptime_ms);
        field(&writer, ",\"frequency_hz\":", packet->frequency_hz);
        field(&writer, ",\"sf\":", packet->sf);
        field(&writer, ",\"bandwidth_khz\":", packet->bandwidth_khz);
        text(&writer, ",\"rssi_qdb\":");
        signed_number(&writer, packet->rssi_qdb);
        text(&writer, ",\"snr_qdb\":");
        signed_number(&writer, packet->snr_qdb);
        field(&writer, ",\"length\":", packet->length);
        uint32_t mtype = packet->length ? packet->data[0] >> 5u : 255u;
        uint8_t data_frame = packet->length >= 12u && mtype >= 2u && mtype <= 5u &&
                             (packet->data[0] & 31u) == 0u &&
                             (packet->data[5] & 15u) <= packet->length - 12u;
        text(&writer, ",\"lorawan_candidate\":");
        text(&writer, data_frame ? "true" : "false");
        field(&writer, ",\"mtype\":", mtype);
        text(&writer, ",\"devaddr\":");
        if (data_frame) {
            character(&writer, '"');
            for (uint32_t j = 4u; j > 0u; --j) hex(&writer, packet->data[j], 2u);
            character(&writer, '"');
            field(&writer, ",\"fcnt16\":", (uint32_t)packet->data[6] |
                                            (uint32_t)packet->data[7] << 8u);
        } else text(&writer, "null,\"fcnt16\":null");
        text(&writer, ",\"hex\":\"");
        for (uint32_t j = 0u; j < packet->length; ++j) hex(&writer, packet->data[j], 2u);
        text(&writer, "\"}");
    }
    text(&writer, "]}");
    return writer.failed ? 0u : writer.used;
}

int dashboard_parse_config(const uint8_t *data, uint32_t length,
                             dashboard_config_t *config) {
    static const char *keys[] = {"frequency_hz=", "sf=", "bandwidth_khz="};
    uint32_t values[3] = {0u, 0u, 0u}, position = 0u;
    for (uint32_t field_index = 0u; field_index < 3u; ++field_index) {
        const char *key = keys[field_index];
        for (uint32_t i = 0u; key[i]; ++i)
            if (position == length || data[position++] != (uint8_t)key[i]) return 0;
        uint32_t digits = 0u;
        while (position < length && data[position] != '&') {
            uint8_t ch = data[position++];
            if (ch < '0' || ch > '9' || ++digits > 9u) return 0;
            values[field_index] = values[field_index] * 10u + ch - '0';
        }
        if (!digits) return 0;
        if (field_index != 2u && (position == length || data[position++] != '&')) return 0;
    }
    if (position != length || values[0] < 863000000u || values[0] > 870000000u ||
        values[1] < 7u || values[1] > 12u ||
        (values[2] != 125u && values[2] != 250u && values[2] != 500u)) return 0;
    config->frequency_hz = values[0];
    config->sf = values[1];
    config->bandwidth_khz = values[2];
    return 1;
}

uint32_t dashboard_http(void *context, const http_request_t *request,
                         uint8_t *response, uint32_t capacity) {
    dashboard_t *app = (dashboard_t *)context;
    static const uint8_t bad[] = "{\"error\":\"Invalid EU868 receiver profile\"}";
    static const uint8_t denied[] = "{\"error\":\"Missing X-Pico-Request: 1\"}";
    static const uint8_t unavailable[] = "{\"error\":\"Radio configuration failed; check lora_error\"}";
    static const uint8_t missing[] = "{\"error\":\"Not found\"}";
    static const uint8_t overflow[] = "{\"error\":\"Status capacity exceeded\"}";
    uint32_t status = 404u, length = sizeof(missing) - 1u;
    const uint8_t *body = missing;
    if (!request->post && match(request->path, "/"))
        return http_response(response, capacity, 200u, "text/html; charset=utf-8",
                               dashboard_page, sizeof(dashboard_page) - 1u);
    if (request->post && match(request->path, "/api/config")) {
        dashboard_config_t config;
        if (!request->write_allowed) {
            status = 403u;
            body = denied;
            length = sizeof(denied) - 1u;
        } else if (!dashboard_parse_config(request->body, request->body_size, &config)) {
            status = 400u;
            body = bad;
            length = sizeof(bad) - 1u;
        } else {
            int32_t error = app->configure(app->context, &config);
            app->lora_error = error;
            if (error != 0) {
                status = 503u;
                body = unavailable;
                length = sizeof(unavailable) - 1u;
            } else {
                app->config = config;
                status = 200u;
            }
        }
    } else if (!request->post && match(request->path, "/api/status")) status = 200u;
    if (status == 200u) {
        length = status_json(app);
        body = app->json;
        if (!length) {
            status = 500u;
            body = overflow;
            length = sizeof(overflow) - 1u;
        }
    }
    return http_response(response, capacity, status, "application/json", body, length);
}
