#include "dashboard.h"
#include "dashboard_page.h"
#include "dashboard_script_generated.h"

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

static const char *profile_name(uint32_t profile) {
    return profile == DASHBOARD_PROFILE_MESHTASTIC ? "meshtastic" : "lorawan";
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u |
           (uint32_t)p[3] << 24u;
}

static void config_json(writer_t *writer, const dashboard_config_t *config) {
    field(writer, "{\"frequency_hz\":", config->frequency_hz);
    field(writer, ",\"sf\":", config->sf);
    field(writer, ",\"bandwidth_khz\":", config->bandwidth_khz);
    text(writer, ",\"profile\":\"");
    text(writer, profile_name(config->profile));
    text(writer, "\"}");
}

/* Plain Meshtastic header; the payload stays encrypted on the device. */
static void meshtastic_json(writer_t *writer, const dashboard_packet_t *packet) {
    const uint8_t *p = packet->data;
    text(writer, ",\"mesh\":");
    if (packet->profile != DASHBOARD_PROFILE_MESHTASTIC || packet->length < 16u ||
        packet->crc_error) {
        text(writer, "null");
        return;
    }
    text(writer, "{\"to\":\"");
    hex(writer, le32(p), 8u);
    text(writer, "\",\"from\":\"");
    hex(writer, le32(p + 4u), 8u);
    text(writer, "\",\"id\":\"");
    hex(writer, le32(p + 8u), 8u);
    field(writer, "\",\"hop_limit\":", p[12] & 7u);
    field(writer, ",\"hop_start\":", (uint32_t)p[12] >> 5u);
    text(writer, ",\"want_ack\":");
    text(writer, (p[12] & 8u) ? "true" : "false");
    text(writer, ",\"via_mqtt\":");
    text(writer, (p[12] & 16u) ? "true" : "false");
    field(writer, ",\"channel\":", p[13]);
    field(writer, ",\"next_hop\":", p[14]);
    field(writer, ",\"relay_node\":", p[15]);
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
    text(&writer, ",\"noise_qdb\":");
    signed_number(&writer, app->noise_qdb);
    text(&writer, ",\"noise_min_qdb\":");
    signed_number(&writer, app->noise_min_qdb);
    text(&writer, ",\"noise_max_qdb\":");
    signed_number(&writer, app->noise_max_qdb);
    field(&writer, ",\"noise_samples\":", app->noise_samples);
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
        text(&writer, ",\"profile\":\"");
        text(&writer, profile_name(packet->profile));
        character(&writer, '"');
        text(&writer, ",\"rssi_qdb\":");
        signed_number(&writer, packet->rssi_qdb);
        text(&writer, ",\"snr_qdb\":");
        signed_number(&writer, packet->snr_qdb);
        text(&writer, ",\"signal_rssi_qdb\":");
        signed_number(&writer, packet->signal_rssi_qdb);
        field(&writer, ",\"flags\":", packet->flags);
        field(&writer, ",\"length\":", packet->length);
        text(&writer, ",\"crc_error\":");
        text(&writer, packet->crc_error ? "true" : "false");
        uint8_t lorawan = packet->profile == DASHBOARD_PROFILE_LORAWAN && !packet->crc_error;
        uint32_t mtype = packet->length && lorawan ? packet->data[0] >> 5u : 255u;
        uint8_t data_frame = lorawan && packet->length >= 12u && mtype >= 2u && mtype <= 5u &&
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
        meshtastic_json(&writer, packet);
        text(&writer, ",\"hex\":\"");
        for (uint32_t j = 0u; j < packet->length; ++j) hex(&writer, packet->data[j], 2u);
        text(&writer, "\"}");
    }
    text(&writer, "]}");
    return writer.failed ? 0u : writer.used;
}

static void activity_event_json(writer_t *w, const activity_event_t *e, uint32_t duration_ms) {
    field(w, "{\"sequence\":", e->sequence);
    field(w, ",\"kind\":", e->kind);
    field(w, ",\"frequency_hz\":", e->frequency_hz);
    field(w, ",\"sf\":", e->sf);
    field(w, ",\"bandwidth_khz\":", e->bandwidth_khz);
    field(w, ",\"profile\":", e->profile);
    field(w, ",\"start_ms\":", e->start_ms);
    field(w, ",\"duration_ms\":", duration_ms);
    field(w, ",\"truncated\":", e->truncated);
    text(w, ",\"baseline_qdb\":");
    if (e->baseline_valid) signed_number(w, e->baseline_qdb);
    else text(w, "null");
    text(w, ",\"peak_qdb\":");
    signed_number(w, e->peak_qdb);
    text(w, ",\"above_floor_qdb\":");
    if (e->baseline_valid) signed_number(w, e->peak_qdb - e->baseline_qdb);
    else text(w, "null");
    text(w, ",\"interval_ms\":");
    if (e->has_interval) number(w, e->interval_ms);
    else text(w, "null");
    field(w, ",\"blind_ms\":", e->blind_ms);
    field(w, ",\"samples\":", e->samples);
    field(w, ",\"max_gap_ms\":", e->max_gap_ms);
    field(w, ",\"packets\":", e->packets);
    field(w, ",\"crc_errors\":", e->crc_errors);
    field(w, ",\"first_packet\":", e->first_packet);
    field(w, ",\"last_packet\":", e->last_packet);
    character(w, '}');
}

static uint32_t activity_json(dashboard_t *app) {
    const activity_t *s = app->activity;
    writer_t w = {app->json, sizeof(app->json), 0u, 0u};
    text(&w, "{\"build_id\":\"");
    hex(&w, app->build_id, 8u);
    field(&w, "\",\"uptime_ms\":", app->uptime_ms);
    field(&w, ",\"session\":", s->session);
    field(&w, ",\"running\":", s->running);
    text(&w, ",\"error\":");
    signed_number(&w, s->error);
    text(&w, ",\"restore_error\":");
    signed_number(&w, s->restore_error);
    field(&w, ",\"sweep\":", s->sweep);
    field(&w, ",\"index\":", s->index);
    field(&w, ",\"dwell_ms\":", ACTIVITY_DWELL_MS);
    field(&w, ",\"sample_target_ms\":", ACTIVITY_SAMPLE_MS);
    field(&w, ",\"warmup_ms\":", ACTIVITY_WARMUP_MS);
    field(&w, ",\"threshold_qdb\":", ACTIVITY_THRESHOLD_QDB);
    field(&w, ",\"sample_count\":", s->sample_count);
    field(&w, ",\"max_gap_ms\":", s->max_gap_ms);
    field(&w, ",\"overwritten\":", s->overwritten);
    text(&w, ",\"config\":");
    config_json(&w, &app->config);
    text(&w, ",\"channel_columns\":[\"frequency_hz\",\"visits\",\"observed_ms\",\"samples\","
             "\"max_gap_ms\",\"bursts\",\"packets\",\"crc_errors\",\"baseline_qdb\",\"peak_qdb\"],\"channels\":[");
    for (uint32_t i = 0u; i < ACTIVITY_CHANNELS; ++i) {
        const activity_channel_t *c = s->channels + i;
        if (i) character(&w, ',');
        character(&w, '[');
        number(&w, c->frequency_hz);
        field(&w, ",", c->visits);
        field(&w, ",", c->observed_ms);
        field(&w, ",", c->samples);
        field(&w, ",", c->max_gap_ms);
        field(&w, ",", c->bursts);
        field(&w, ",", c->packets);
        field(&w, ",", c->crc_errors);
        character(&w, ',');
        if (c->baseline_valid) signed_number(&w, c->baseline_qdb);
        else text(&w, "null");
        character(&w, ',');
        if (c->samples) signed_number(&w, c->peak_qdb);
        else text(&w, "null");
        character(&w, ']');
    }
    text(&w, "],\"active_burst\":");
    if (s->burst_active) {
        activity_event_json(&w, &s->burst, app->uptime_ms - s->burst.start_ms);
    } else text(&w, "null");
    text(&w, ",\"events\":[");
    uint32_t count = s->count < 8u ? s->count : 8u;
    for (uint32_t i = 0u; i < count; ++i) {
        if (i) character(&w, ',');
        const activity_event_t *e = &s->events[(s->head + ACTIVITY_EVENTS - 1u - i) % ACTIVITY_EVENTS];
        activity_event_json(&w, e, e->duration_ms);
    }
    text(&w, "]}");
    return w.failed ? 0u : w.used;
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
    uint32_t profile = DASHBOARD_PROFILE_LORAWAN;
    if (position != length) {
        static const char prefix[] = "&profile=";
        static const char *names[] = {"lorawan", "meshtastic"};
        for (uint32_t i = 0u; prefix[i]; ++i)
            if (position == length || data[position++] != (uint8_t)prefix[i]) return 0;
        uint32_t rest = length - position, found = 0u;
        for (uint32_t n = 0u; n < 2u && !found; ++n) {
            uint32_t size = 0u;
            while (names[n][size]) ++size;
            if (size != rest) continue;
            found = 1u;
            for (uint32_t i = 0u; i < size; ++i)
                if (data[position + i] != (uint8_t)names[n][i]) found = 0u;
            if (found) profile = n;
        }
        if (!found) return 0;
        position = length;
    }
    if (values[0] < 863000000u || values[0] > 870000000u ||
        values[1] < 7u || values[1] > 12u ||
        (values[2] != 125u && values[2] != 250u && values[2] != 500u)) return 0;
    config->frequency_hz = values[0];
    config->sf = values[1];
    config->bandwidth_khz = values[2];
    config->profile = profile;
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
    static const uint8_t busy[] = "{\"error\":\"Stop the activity survey before changing profiles\"}";
    static const uint8_t survey_bad[] = "{\"error\":\"Use action=start or action=stop\"}";
    static const uint8_t survey_failed[] = "{\"error\":\"Activity command failed; inspect /api/activity error and restore_error\"}";
    uint32_t status = 404u, length = sizeof(missing) - 1u;
    const uint8_t *body = missing;
    if (!request->post && match(request->path, "/"))
        return http_response(response, capacity, 200u, "text/html; charset=utf-8",
                               dashboard_page, sizeof(dashboard_page) - 1u);
    if (!request->post && match(request->path, "/dashboard.js"))
        return http_response(response, capacity, 200u, "text/javascript; charset=utf-8",
                               dashboard_script, sizeof(dashboard_script) - 1u);
    if (!request->post && match(request->path, "/dashboard-core.js"))
        return http_response(response, capacity, 200u, "text/javascript; charset=utf-8",
                               dashboard_core, sizeof(dashboard_core) - 1u);
    if (match(request->path, "/api/activity") && app->activity && app->activity_control) {
        if (request->post) {
            if (!request->write_allowed)
                return http_response(response, capacity, 403u, "application/json", denied, sizeof(denied) - 1u);
            uint8_t start = request->body_size == 12u;
            const char *expected = start ? "action=start" : "action=stop";
            uint8_t valid = request->body_size == (start ? 12u : 11u);
            for (uint32_t i = 0u; valid && i < request->body_size; ++i)
                if (request->body[i] != (uint8_t)expected[i]) valid = 0u;
            if (!valid)
                return http_response(response, capacity, 400u, "application/json", survey_bad, sizeof(survey_bad) - 1u);
            int32_t error = app->activity_control(app->context, start);
            if (error != 0)
                return http_response(response, capacity, error == SX1262_RX_BUSY_ERROR ? 409u : 503u,
                                     "application/json", survey_failed, sizeof(survey_failed) - 1u);
        }
        uint32_t size = activity_json(app);
        return http_response(response, capacity, size ? 200u : 500u, "application/json",
                             size ? app->json : overflow, size ? size : sizeof(overflow) - 1u);
    }
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
        } else if (app->activity && app->activity->running) {
            status = 409u;
            body = busy;
            length = sizeof(busy) - 1u;
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
