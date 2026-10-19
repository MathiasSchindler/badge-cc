#include "app.h"
#include "../../src/radio/wifi_firmware.h"
#include "../../src/radio/wifi_assets.h"

const char ref_role[] = "VENDOR STATION";
static wifi_fw_report_t firmware;
static uint8_t request[REF_COUNTER_BYTES], reply[REF_COUNTER_BYTES];
static uint8_t saved_nonce[16], tx[REF_FRAME_MAX], rx[1514];
static uint8_t prepared[REF_EARLY_PREPARED][REF_FRAME_MAX];
static uint32_t prepared_digest[REF_EARLY_PREPARED];
static uint8_t ready;
static uint32_t received;

static int service(void *context, const wifi_fw_report_t *report) {
    (void)report;
    return ref_service(context);
}

static int command(uint32_t cmd, uint8_t set, uint32_t size, uint32_t capacity) {
    badger_bytes_store_u32(ref_detail + 16u, cmd);
    badger_bytes_zero(reply, sizeof(reply));
    int ok = wifi_firmware_control(cmd, set, 0u, request, size, reply, capacity, &received);
    badger_bytes_store_u32(ref_detail + 20u, firmware.controller_status);
    return ok;
}

static uint32_t variable(const char *name) {
    badger_bytes_zero(request, sizeof(request));
    badger_bytes_zero(ref_detail + 96u, 32u);
    uint32_t i = 0u;
    do {
        request[i] = (uint8_t)name[i];
        if (i < 31u) ref_detail[96u+i] = (uint8_t)name[i];
    } while (name[i++]);
    return i;
}

static int scalar(uint32_t cmd, uint32_t value) {
    badger_bytes_zero(request, sizeof(request));
    badger_bytes_store_u32(request, value);
    return command(cmd, 1u, 4u, sizeof(reply));
}

static int var_set(const char *name, uint32_t value, uint8_t bss) {
    uint32_t size = variable(name);
    if (bss) { badger_bytes_store_u32(request + size, 0u); size += 4u; }
    badger_bytes_store_u32(request + size, value);
    return command(263u, 1u, size + 4u, sizeof(reply));
}

static int query(uint32_t id, uint32_t offset, uint32_t capacity) {
    uint32_t size = variable(ref_queries[id]);
    if (id == 12u) badger_bytes_copy(request + size, ref_detail + 38u, 6u);
    int ok = command(262u, 0u, capacity, capacity);
    if (ok && received > capacity) { firmware.error = WIFI_FW_PROTOCOL; return 0; }
    uint8_t *q = ref_detail + REF_QUERY_BASE + REF_QUERY_BYTES * id;
    badger_bytes_store_u32(q, 1u);
    badger_bytes_store_u32(q + 4u, (uint32_t)ok);
    badger_bytes_store_u32(q + 8u, firmware.error);
    badger_bytes_store_u32(q + 12u, firmware.controller_status);
    badger_bytes_store_u32(q + 16u, received);
    badger_bytes_store_u32(q + 20u, ok && received >= 4u ? badger_bytes_load_u32(reply) : 0u);
    badger_bytes_store_u32(q + 24u, offset);
    if (ok && offset) badger_bytes_copy(ref_detail + offset, reply, received);
    return ok || firmware.error == WIFI_FW_CONTROLLER_ERROR;
}

static int snapshot(uint8_t after) {
    return query(after ? 14u : 13u, after ? REF_COUNTER_AFTER : REF_COUNTER_BEFORE, REF_COUNTER_BYTES) &&
        query(after ? 16u : 15u, after ? REF_AMPDU_AFTER : REF_AMPDU_BEFORE, 512u) &&
        query(after ? 18u : 17u, after ? REF_DUMP_AFTER : REF_DUMP_BEFORE, REF_DUMP_BYTES);
}

static int configure(const uint8_t nonce[16]) {
    uint32_t ssid_size = 0u, password_size = 0u;
    while (ssid_size < 33u && ref_ssid[ssid_size]) ++ssid_size;
    while (password_size < 64u && ref_password[password_size]) ++password_size;
    if (!ssid_size || ssid_size > 32u || password_size < 8u || password_size > 63u ||
        nonce[11] > 3u || (CLEAN_RX_HT_ADVANCE && nonce[11]) ||
        nonce[12] > 7u) { firmware.error = WIFI_FW_PROTOCOL; return 0; }
    ref_mac(ref_detail + 32u, nonce);
    if (!scalar(3u, 1u) || !var_set("apsta", 0u, 0u)) return 0;
    uint32_t size = variable("cur_etheraddr");
    badger_bytes_copy(request + size, ref_detail + 32u, 6u);
    if (!command(263u, 1u, size + 6u, sizeof(reply)) ||
        !var_set("nmode", 1u, 0u) ||
        !var_set("ampdu", 1u, 0u) ||
        !var_set("ampdu_mpdu", REF_AMPDU_LIMIT, 0u) ||
        !scalar(2u, 0u) || !scalar(86u, 0u) ||
        !scalar(134u, 4u) ||
        !var_set("bsscfg:sup_wpa", 1u, 1u) ||
        !var_set("bsscfg:sup_wpa2_eapver", 0xffffffffu, 1u) ||
        !var_set("bsscfg:sup_wpa_tmo", 5000u, 1u)) return 0;
    badger_bytes_zero(request, sizeof(request));
    request[0] = (uint8_t)password_size; request[2] = 1u;
    badger_bytes_copy(request + 4u, ref_password, password_size);
    int ok = command(268u, 1u, 68u, sizeof(reply));
    ref_wipe(request, sizeof(request)); ref_wipe(reply, sizeof(reply));
    ref_wipe(ref_password, sizeof(ref_password));
    if (!ok || !scalar(20u, 1u) || !scalar(22u, 0u) ||
        !var_set("mfp", 1u, 0u) || !scalar(165u, 0x80u)) return 0;
    badger_bytes_zero(request, sizeof(request));
    badger_bytes_store_u32(request, ssid_size);
    badger_bytes_copy(request + 4u, ref_ssid, ssid_size);
    ok = command(26u, 1u, 36u, sizeof(reply));
    ref_wipe(request, sizeof(request)); ref_wipe(ref_ssid, sizeof(ref_ssid));
    return ok;
}

static int associate(void) {
    uint32_t start = ref_now();
    while (ref_now() - start < 10000000u && ref_service(0)) {
        badger_bytes_zero(request, sizeof(request));
        if (!command(23u, 0u, 6u, 6u)) {
            if (firmware.error != WIFI_FW_CONTROLLER_ERROR ||
                firmware.controller_status != 0xffffffefu) return 0;
            continue;
        }
        if (received != 6u) { firmware.error = WIFI_FW_PROTOCOL; return 0; }
        uint8_t bits = 0u;
        for (uint32_t i = 0u; i < 6u; ++i) bits |= reply[i];
        if (!bits) continue;
        badger_bytes_copy(ref_detail + 38u, reply, 6u);
        if (!query(12u, 0u, 256u)) return 0;
        if (!badger_bytes_load_u32(ref_detail + REF_QUERY_BASE + 12u*REF_QUERY_BYTES + 4u))
            continue;
        if (received < 84u || reply[0] < 3u || reply[0] > 7u ||
            !badger_bytes_equal(reply + 16u, ref_detail + 38u, 6u)) {
            firmware.error = WIFI_FW_PROTOCOL; return 0;
        }
        uint32_t flags = badger_bytes_load_u32(reply + 8u);
        badger_bytes_store_u32(ref_detail + 44u, flags);
        if ((flags & 0x30u) == 0x30u) return 1;
    }
    firmware.error = WIFI_FW_TIMEOUT;
    return 0;
}

void ref_run(const uint8_t nonce[16]) {
    int ok = 0;
    if (nonce[14] == 1u && !ready) {
        badger_bytes_zero(ref_detail, REF_TRACE_BYTES);
        badger_bytes_store_u32(ref_detail, REF_MAGIC);
        badger_bytes_store_u32(ref_detail + 12u, 1u);
        badger_bytes_copy(saved_nonce, nonce, 16u);
        const wifi_fw_assets_t *assets = wifi_firmware_assets();
        ok = wifi_firmware_begin(wifi_board_wire(), assets, &firmware, service, 0) &&
            configure(nonce) && associate();
        if (ok) {
            badger_bytes_store_u32(ref_detail + 48u, assets->firmware_crc);
            badger_bytes_copy(ref_detail + 64u, (const uint8_t *)firmware.version, 64u);
            ok = var_set("2g_rate", 0x01010000u | nonce[12], 0u);
        }
        for (uint32_t i = 0u; ok && i < 12u; ++i) ok = query(i, 0u, 256u);
        if (ok) {
            const uint8_t *q = ref_detail + REF_QUERY_BASE + 11u*REF_QUERY_BYTES;
            uint32_t channel = badger_bytes_load_u32(q + 20u) & 255u;
            if (!badger_bytes_load_u32(q + 4u) || !channel || channel > 14u ||
                !(badger_bytes_load_u32(ref_detail + 44u) & 0x2000u)) {
                firmware.error = WIFI_FW_PROTOCOL; ok = 0;
            } else badger_bytes_store_u32(ref_detail + 24u, channel);
        }
        if (ok) ok = wifi_firmware_data_enable();
        if (ok) { ready = 1u; badger_bytes_store_u32(ref_detail + 4u, 1u); }
    } else if (nonce[14] == 2u && ready &&
               badger_bytes_equal(nonce, saved_nonce, 13u)) {
        ok = snapshot(0u);
        uint32_t prepared_count = !CLEAN_RX_HT_ADVANCE && nonce[11] ? REF_EARLY_PREPARED : 0u;
        uint32_t prepare_start = ref_now();
        if (ok && prepared_count && !ref_prepare_early(prepared,prepared_digest,nonce,ref_detail+38u)) {
            firmware.error = WIFI_FW_PROTOCOL; ok = 0;
        }
        uint32_t prepare_us = prepared_count ? ref_now()-prepare_start : 0u;
        uint32_t sent = 0u, start = ref_now(), digest = REF_DIGEST_INITIAL;
        uint32_t first_eight = 0u, next_eight = 0u;
        uint32_t minimum = 0xffffffffu, maximum = 0u, buckets[4] = {0u};
        while (ok && sent < REF_FRAME_COUNT && ref_now() - start < 6000000u) {
            int cached = sent < prepared_count;
            uint32_t size = cached ? ref_fixture_size(nonce[11]) :
                ref_frame(tx, nonce, ref_detail + 38u, sent);
            const uint8_t *frame = cached ? prepared[sent] : tx;
            uint32_t before = ref_now();
            ok = wifi_firmware_send_frame(0u, frame, size);
            uint32_t elapsed = ref_now()-before;
            if (ok) {
                if (elapsed < minimum) minimum = elapsed;
                if (elapsed > maximum) maximum = elapsed;
                ++buckets[elapsed <= 200u ? 0u : elapsed <= 400u ? 1u : elapsed <= 800u ? 2u : 3u];
                digest = cached ? prepared_digest[sent] : ref_digest(digest,frame,size); ++sent;
                if (sent == 8u) first_eight = ref_now()-start;
                if (sent == 16u) next_eight = ref_now()-start-first_eight;
            }
        }
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS,sent);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+4u,ref_now()-start);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+8u,sent ? minimum : 0u);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+12u,maximum);
        for (uint32_t i = 0u; i < 4u; ++i)
            badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+16u+4u*i,buckets[i]);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+32u,first_eight);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+36u,next_eight);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+40u,prepared_count);
        badger_bytes_store_u32(ref_detail+REF_SUBMIT_STATS+44u,prepare_us);
        badger_bytes_store_u32(ref_detail + 28u, sent);
        badger_bytes_store_u32(ref_detail + 52u, digest);
        if (ok && sent != REF_FRAME_COUNT) { firmware.error = WIFI_FW_TIMEOUT; ok = 0; }
        start = ref_now();
        while (ok && ref_now() - start < 1200000u) {
            uint32_t length; uint8_t iface;
            ok = wifi_firmware_read_frame(rx, &length, &iface) >= 0;
        }
        if (ok) ok = snapshot(1u);
        if (ok) badger_bytes_store_u32(ref_detail + 4u, 2u);
        ready = 0u;
        wifi_firmware_stop();
    } else firmware.error = WIFI_FW_PROTOCOL;
    if (!ok) {
        badger_bytes_store_u32(ref_detail + 8u, firmware.error ? firmware.error : WIFI_FW_PROTOCOL);
        badger_bytes_store_u32(ref_detail + 20u, firmware.controller_status);
        ref_stop();
    }
}

void ref_idle(void) {
    if (!ready) return;
    uint32_t size; uint8_t iface;
    if (wifi_firmware_read_frame(rx, &size, &iface) < 0) {
        badger_bytes_store_u32(ref_detail + 8u, firmware.error);
        ref_stop();
    }
}

void ref_stop(void) { ready = 0u; wifi_firmware_stop(); }
