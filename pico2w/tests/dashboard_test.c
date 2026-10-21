#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "app/dashboard.h"

static dashboard_t app;
static uint8_t response[HTTP_RESPONSE_MAX];
static uint32_t configured;
static int32_t config_error;

static int32_t configure(void *context, const dashboard_config_t *config) {
    (void)context;
    assert(config->frequency_hz == 868100000u && config->sf == 7u && config->bandwidth_khz == 125u);
    ++configured;
    return config_error;
}

static void fixture(void) {
    memset(&app, 0, sizeof(app));
    app.noise_qdb = -2147483647 - 1; /* widest signed values keep the JSON bound honest */
    app.noise_min_qdb = -2147483647 - 1;
    app.noise_max_qdb = -2147483647 - 1;
    app.noise_samples = 4294967295u;
    app.build_id = 0x12345678;
    app.uptime_ms = 123456;
    app.received = 8;
    app.overwritten = 3;
    app.lora_state = 1;
    app.lora_status = 0x52;
    app.lora_command = 0xc0;
    app.lora_polls = 123;
    app.config = (dashboard_config_t){868100000u, 7u, 125u, DASHBOARD_PROFILE_LORAWAN};
    app.configure = configure;
    strcpy(app.ip, "192.168.1.80");
    app.packet_count = DASHBOARD_PACKETS;
    for (uint32_t i = 0; i < DASHBOARD_PACKETS; ++i) {
        dashboard_packet_t *packet = app.packets + i;
        packet->sequence = 100 - i;
        packet->uptime_ms = 120000 - i * 1000;
        packet->frequency_hz = 868100000;
        packet->sf = 7;
        packet->bandwidth_khz = 125;
        packet->rssi_qdb = -401;
        packet->snr_qdb = -9;
        packet->length = 255;
        memset(packet->data, 0xab, sizeof(packet->data));
        packet->data[0] = 0x40;
        packet->data[1] = 0x78; packet->data[2] = 0x56;
        packet->data[3] = 0x34; packet->data[4] = 0x12;
        packet->data[5] = 0;
        packet->data[6] = 0x34; packet->data[7] = 0x12;
    }
}

static uint32_t get(const char *path) {
    http_request_t request = {0};
    strcpy(request.path, path);
    return dashboard_http(&app, &request, response, sizeof(response));
}

static uint32_t post(uint8_t allowed, const char *body) {
    http_request_t request = {0};
    strcpy(request.path, "/api/config");
    request.post = 1;
    request.write_allowed = allowed;
    request.body = (const uint8_t *)body;
    request.body_size = (uint32_t)strlen(body);
    return dashboard_http(&app, &request, response, sizeof(response));
}

static void page_noise_check(void) {
    uint32_t size = get("/");
    response[size] = 0;
    assert(strstr((char *)response, "id=\"noise\"") != NULL);
    assert(strstr((char *)response, "d.noise_samples?") != NULL);
}

int main(int argc, char **argv) {
    fixture();
    if (argc == 2) {
#ifdef _WIN32
        assert(_setmode(_fileno(stdout), _O_BINARY) != -1);
#endif
        uint32_t size = get(strcmp(argv[1], "--page") == 0 ? "/" : "/api/status");
        assert(fwrite(response, 1, size, stdout) == size);
        return 0;
    }
    uint32_t size = get("/api/status");
    assert(size > 6000 && size < sizeof(response));
    assert(memcmp(response, "HTTP/1.1 200 OK", 15) == 0);
    response[size] = 0;
    assert(strstr((char *)response, "\"devaddr\":\"12345678\",\"fcnt16\":4660") != NULL);
    assert(strstr((char *)response, "\"rssi_qdb\":-401,\"snr_qdb\":-9") != NULL);
    assert(strstr((char *)response, "\"lora_state\":1,\"lora_status\":82,\"lora_command\":192,"
                  "\"lora_device_errors\":0,\"lora_polls\":123") != NULL);
    assert(strstr((char *)response, "\"noise_qdb\":-2147483648,\"noise_min_qdb\":-2147483648,"
                  "\"noise_max_qdb\":-2147483648,\"noise_samples\":4294967295,") != NULL);
    page_noise_check();
    app.packets[0].crc_error = 1;
    size = get("/api/status"); response[size] = 0;
    assert(strstr((char *)response, "\"crc_error\":true,\"lorawan_candidate\":false,\"mtype\":255,"
                  "\"devaddr\":null,\"fcnt16\":null,\"mesh\":null") != NULL);
    assert(strstr((char *)response, "\"crc_error\":false,\"lorawan_candidate\":true") != NULL);
    app.packets[0].crc_error = 0;
    app.packets[0].length = 1;
    size = get("/api/status"); response[size] = 0;
    assert(strstr((char *)response, "\"devaddr\":null,\"fcnt16\":null,\"mesh\":null") != NULL);
    assert(strstr((char *)response, "\"config\":{\"frequency_hz\":868100000,\"sf\":7,"
                  "\"bandwidth_khz\":125,\"profile\":\"lorawan\"}") != NULL);
    {
        dashboard_packet_t *packet = app.packets + 1;
        static const uint8_t header[16] = {0xff, 0xff, 0xff, 0xff, 0x44, 0x33, 0x22, 0x11,
                                           0x88, 0x77, 0x66, 0x55, 0x7b, 0x08, 0x00, 0x44};
        memcpy(packet->data, header, sizeof(header));
        packet->length = 20;
        packet->profile = DASHBOARD_PROFILE_MESHTASTIC;
        size = get("/api/status"); response[size] = 0;
        assert(strstr((char *)response, "\"profile\":\"meshtastic\",\"rssi_qdb\"") != NULL);
        assert(strstr((char *)response, "\"lorawan_candidate\":false,\"mtype\":255,"
                      "\"devaddr\":null,\"fcnt16\":null,\"mesh\":{\"to\":\"ffffffff\","
                      "\"from\":\"11223344\",\"id\":\"55667788\",\"hop_limit\":3,"
                      "\"hop_start\":3,\"want_ack\":true,\"via_mqtt\":true,\"channel\":8,"
                      "\"next_hop\":0,\"relay_node\":68}") != NULL);
        fixture();
    }
    {
        static const char mesh[] = "frequency_hz=869525000&sf=11&bandwidth_khz=250&profile=meshtastic";
        static const char lorawan[] = "frequency_hz=868100000&sf=7&bandwidth_khz=125&profile=lorawan";
        static const char *invalid[] = {
            "frequency_hz=869525000&sf=11&bandwidth_khz=250&profile=",
            "frequency_hz=869525000&sf=11&bandwidth_khz=250&profile=mesh",
            "frequency_hz=869525000&sf=11&bandwidth_khz=250&profile=meshtasticx",
            "frequency_hz=869525000&sf=11&bandwidth_khz=250&profile=meshtastic&",
            "frequency_hz=869525000&sf=11&bandwidth_khz=250&mode=meshtastic",
        };
        dashboard_config_t parsed;
        assert(dashboard_parse_config((const uint8_t *)mesh, sizeof(mesh) - 1u, &parsed));
        assert(parsed.profile == DASHBOARD_PROFILE_MESHTASTIC && parsed.frequency_hz == 869525000u &&
               parsed.sf == 11u && parsed.bandwidth_khz == 250u);
        assert(dashboard_parse_config((const uint8_t *)lorawan, sizeof(lorawan) - 1u, &parsed));
        assert(parsed.profile == DASHBOARD_PROFILE_LORAWAN);
        for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
            assert(!dashboard_parse_config((const uint8_t *)invalid[i], (uint32_t)strlen(invalid[i]),
                                           &parsed));
    }
    size = get("/");
    assert(size > 4000 && memcmp(response, "HTTP/1.1 200 OK", 15) == 0);
    assert(get("/favicon.ico") > 0 && memcmp(response, "HTTP/1.1 404", 12) == 0);
    static const char body[] = "frequency_hz=868100000&sf=7&bandwidth_khz=125";
    static const uint32_t frequencies[] = {863000000u, 868100000u, 870000000u};
    static const uint32_t bandwidths[] = {125u, 250u, 500u};
    for (uint32_t f = 0u; f < 3u; ++f)
        for (uint32_t sf = 7u; sf <= 12u; ++sf)
            for (uint32_t bw = 0u; bw < 3u; ++bw) {
                char input[100];
                int length = snprintf(input, sizeof(input),
                    "frequency_hz=%u&sf=%u&bandwidth_khz=%u",
                    (unsigned)frequencies[f], (unsigned)sf, (unsigned)bandwidths[bw]);
                dashboard_config_t parsed;
                assert(length > 0 && dashboard_parse_config((const uint8_t *)input,
                                                              (uint32_t)length, &parsed));
                assert(parsed.frequency_hz == frequencies[f] && parsed.sf == sf &&
                       parsed.bandwidth_khz == bandwidths[bw]);
            }
    assert(post(0, body) > 0 && memcmp(response, "HTTP/1.1 403", 12) == 0 && !configured);
    static const char *bad[] = {
        "", "frequency_hz=862999999&sf=7&bandwidth_khz=125",
        "frequency_hz=870000001&sf=7&bandwidth_khz=125",
        "frequency_hz=868100000&sf=6&bandwidth_khz=125",
        "frequency_hz=868100000&sf=13&bandwidth_khz=125",
        "frequency_hz=868100000&sf=7&bandwidth_khz=126",
        "frequency_hz=8681000000&sf=7&bandwidth_khz=125",
        "frequency_hz=-868100000&sf=7&bandwidth_khz=125",
        "frequency_hz=868100000&sf=7&bandwidth_khz=125&",
        "frequency_hz=868100000&sf=7&bandwidth_khz=125&sf=9",
        "frequency_hz=868100000&sf=7&bandwidth_khz=",
        "frequency_hz=868100000&sf=&bandwidth_khz=125"
    };
    for (uint32_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(post(1, bad[i]) > 0);
        assert(memcmp(response, "HTTP/1.1 400", 12) == 0 && !configured);
    }
    assert(post(1, body) > 0 && configured == 1);
    config_error = -3006;
    assert(post(1, body) > 0 && configured == 2);
    assert(memcmp(response, "HTTP/1.1 503", 12) == 0 && app.lora_error == -3006);
    assert(app.config.frequency_hz == 868100000);
    app.packet_count = DASHBOARD_PACKETS + 1;
    assert(get("/api/status") > 0 && memcmp(response, "HTTP/1.1 500", 12) == 0);
    puts("PASS: bounded JSON, metadata, UI, strict EU868 settings, CSRF header and error reporting");
    return 0;
}
