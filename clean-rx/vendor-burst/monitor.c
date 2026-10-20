#include "app.h"
#include "monitor.h"
#include "../../src/radio/wifi_firmware.h"
#include "../../src/radio/wifi_assets.h"

const char ref_role[] = "CYW VENDOR MONITOR";
static wifi_fw_report_t firmware;
static uint8_t session[14], active;
static uint32_t armed_at;

static int service(void *context, const wifi_fw_report_t *report) {
    (void)context;
    (void)report;
    return ref_service(0);
}

static void receive(void *context, const uint8_t *packet, uint32_t length) {
    (void)context;
    ref_monitor_packet(ref_detail, session, packet, length, ref_now());
}

static int command(uint32_t cmd, uint8_t set, const uint8_t *request, uint32_t size,
                    uint32_t *value, uint32_t *second) {
    uint32_t index = badger_bytes_load_u32(ref_detail + 96u);
    if (index >= REF_MONITOR_CONTROL_COUNT) {
        firmware.error = WIFI_FW_PROTOCOL;
        return 0;
    }
    uint8_t reply[16] = {0};
    uint32_t received = 0u;
    int ok = wifi_firmware_control(cmd, set, 0u, request, size, reply, sizeof(reply), &received);
    uint8_t *r = ref_detail + REF_MONITOR_CONTROLS + index * REF_MONITOR_CONTROL_BYTES;
    badger_bytes_store_u32(r, cmd);
    badger_bytes_store_u32(r + 4u, set);
    badger_bytes_store_u32(r + 8u, (uint32_t)ok);
    badger_bytes_store_u32(r + 12u, firmware.error);
    badger_bytes_store_u32(r + 16u, firmware.controller_status);
    badger_bytes_store_u32(r + 20u, received);
    badger_bytes_store_u32(r + 24u, received >= 4u ? usb_report_u32(reply) : 0u);
    badger_bytes_store_u32(r + 28u, received >= 8u ? usb_report_u32(reply + 4u) : 0u);
    badger_bytes_store_u32(ref_detail + 96u, index + 1u);
    badger_bytes_store_u32(ref_detail + 16u, cmd);
    badger_bytes_store_u32(ref_detail + 20u, firmware.controller_status);
    if (ok && !set && received != (cmd == 29u ? 12u : 4u)) {
        firmware.error = WIFI_FW_PROTOCOL;
        return 0;
    }
    *value = usb_report_u32(reply);
    *second = usb_report_u32(reply + 4u);
    return ok;
}

static int scalar(uint32_t cmd, uint8_t set, uint32_t input, uint32_t *value,
                   uint32_t *second) {
    uint8_t request[12] = {0};
    badger_bytes_store_u32(request, input);
    return command(cmd, set, request, cmd == 29u ? 12u : 4u, value, second);
}

void ref_run(const uint8_t nonce[16]) {
    uint32_t value = 0u, second = 0u;
    int ok = 0;
    if (nonce[14] == 1u && !active && nonce[10] == 0u && nonce[12] == 0u &&
        nonce[13] == 6u && (nonce[11] == 1u || nonce[11] == 2u)) {
        badger_bytes_zero(ref_detail, REF_TRACE_BYTES);
        badger_bytes_copy(session, nonce, sizeof(session));
        badger_bytes_store_u32(ref_detail, REF_MAGIC);
        badger_bytes_store_u32(ref_detail + 24u, 6u);
        ref_mac(ref_detail + 32u, nonce);
        badger_bytes_store_u32(ref_detail + 64u, REF_MONITOR_MAGIC);
        badger_bytes_store_u32(ref_detail + 68u, 1u);
        const uint8_t mpc[8] = {'m','p','c',0u,0u,0u,0u,0u};
        ok = wifi_firmware_begin(wifi_board_wire(), wifi_firmware_assets(), &firmware,
                                 service, 0) &&
            wifi_firmware_raw_enable(receive, 0) &&
            command(263u, 1u, mpc, sizeof(mpc), &value, &second) &&
            scalar(86u, 1u, 0u, &value, &second) &&
            scalar(2u, 1u, 0u, &value, &second) &&
            scalar(30u, 1u, 6u, &value, &second) &&
            scalar(108u, 1u, 1u, &value, &second) &&
            scalar(107u, 0u, 0u, &value, &second);
        if (ok) {
            badger_bytes_store_u32(ref_detail + 108u, value);
            ok = value == 1u && scalar(29u, 0u, 0u, &value, &second);
        }
        if (ok) {
            badger_bytes_store_u32(ref_detail + 112u, value);
            badger_bytes_store_u32(ref_detail + 116u, second);
            ok = value == 6u && second == 6u;
        }
        if (ok) {
            active = 1u;
            armed_at = ref_now();
            badger_bytes_store_u32(ref_detail + 4u, 1u);
        }
    } else if (nonce[14] == 2u && active && badger_bytes_equal(session, nonce, 14u)) {
        uint32_t started = ref_now(), drain = ref_clock_ticks * 300u / ref_clock_frames;
        ok = 1;
        while (ok && ref_now() - started < drain)
            ok = wifi_firmware_raw_poll() >= 0;
        if (ok) ok = scalar(108u, 1u, 0u, &value, &second) &&
                     scalar(107u, 0u, 0u, &value, &second) && value == 0u;
        badger_bytes_store_u32(ref_detail + 120u, firmware.tx_packets);
        badger_bytes_store_u32(ref_detail + 124u, firmware.rx_packets);
        if (ok) badger_bytes_store_u32(ref_detail + 4u, 2u);
        ref_stop();
    }
    if (!ok) {
        if (!badger_bytes_load_u32(ref_detail + 8u))
            badger_bytes_store_u32(ref_detail + 8u,
                firmware.error ? firmware.error : WIFI_FW_PROTOCOL);
        ref_stop();
    }
}

void ref_idle(void) {
    if (!active) return;
    uint32_t limit = ref_clock_ticks * 12000u / ref_clock_frames;
    if (ref_now() - armed_at >= limit || wifi_firmware_raw_poll() < 0) {
        badger_bytes_store_u32(ref_detail + 8u,
            ref_now() - armed_at >= limit ? WIFI_FW_TIMEOUT : firmware.error);
        ref_stop();
    }
}

void ref_stop(void) {
    active = 0u;
    wifi_firmware_stop();
}
