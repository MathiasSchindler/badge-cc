#include "wifi_link.h"
#include "../shared/bytes.h"

enum {
    CMD_UP = 2, CMD_DOWN = 3, CMD_INFRA = 20, CMD_AUTH = 22, CMD_BSSID = 23,
    CMD_CHANNEL = 30, CMD_ANTENNA = 64, CMD_DTIM = 78, CMD_PM = 86,
    CMD_GMODE = 110, CMD_WSEC = 134, CMD_ASSOCLIST = 159,
    CMD_WPA_AUTH = 165, CMD_GET_VAR = 262, CMD_SET_VAR = 263, CMD_PMK = 268,
    MAX_PEERS = 16, ASSOCLIST_SIZE = 4 + 6 * MAX_PEERS,
    STA_INFO_MIN_SIZE = 84, STA_ASSOC = 0x10, STA_AUTHORIZED = 0x20
};

static void clear(uint8_t *data, uint32_t size) {
    badger_bytes_zero(data, size);
}

static void copy(uint8_t *to, const uint8_t *from, uint32_t size) {
    badger_bytes_copy(to, from, size);
}

static uint32_t read32(const uint8_t *data) {
    return badger_bytes_load_u32(data);
}

static uint32_t read16(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8u;
}

static void store(uint8_t *data, uint32_t value) {
    badger_bytes_store_u32(data, value);
}

static int equal_mac(const uint8_t *a, const uint8_t *b) {
    return badger_bytes_equal(a, b, 6u);
}

static int zero_mac(const uint8_t *mac) {
    uint8_t bits = 0u;
    for (uint32_t i = 0u; i < 6u; ++i) bits |= mac[i];
    return bits == 0u;
}

static int peer_mac(const wifi_link_t *link, const uint8_t *mac) {
    return !zero_mac(mac) && !(mac[0] & 1u) && !equal_mac(mac, link->report.mac);
}

static int fail(wifi_link_t *link, wifi_link_error_t error) {
    link->report.authorized = 0u;
    link->report.associated = 0u;
    link->report.error = error;
    link->report.stage = WIFI_LINK_FAILED;
    return 0;
}

static void waiting(wifi_link_t *link) {
    link->report.stage = WIFI_LINK_WAIT_PEER;
    link->report.error = WIFI_LINK_OK;
    link->report.authorized = 0u;
    link->report.associated = 0u;
    link->report.peer_count = 0u;
    clear(link->report.peer, 6u);
}

static int command(wifi_link_t *link, uint32_t cmd, uint8_t set, uint8_t iface,
                    uint32_t size, uint32_t capacity, uint32_t *received) {
    uint32_t length = 0u;
    link->report.last_command = cmd;
    link->report.controller_status = 0u;
    clear(link->reply, sizeof(link->reply));
    if (!link->ops.control(link->ops.context, cmd, set, iface, link->buffer, size,
                           link->reply, capacity, &length))
        return fail(link, WIFI_LINK_RADIO_ERROR);
    if (length > capacity) return fail(link, WIFI_LINK_BAD_REPLY);
    if (received != 0) *received = length;
    return 1;
}

static uint32_t variable(wifi_link_t *link, const char *name) {
    clear(link->buffer, sizeof(link->buffer));
    uint32_t size = 0u;
    do {
        link->buffer[size] = (uint8_t)name[size];
    } while (name[size++] != '\0');
    return size;
}

static int scalar(wifi_link_t *link, uint32_t cmd, uint32_t value, uint8_t iface) {
    clear(link->buffer, sizeof(link->buffer));
    store(link->buffer, value);
    return command(link, cmd, 1u, iface, 4u, sizeof(link->reply), 0);
}

static int var_scalar(wifi_link_t *link, const char *name, uint32_t value, uint8_t iface) {
    uint32_t size = variable(link, name);
    store(link->buffer + size, value);
    return command(link, CMD_SET_VAR, 1u, iface, size + 4u, sizeof(link->reply), 0);
}

static int var_pair(wifi_link_t *link, const char *name, uint32_t index, uint32_t value) {
    uint32_t size = variable(link, name);
    store(link->buffer + size, index);
    store(link->buffer + size + 4u, value);
    return command(link, CMD_SET_VAR, 1u, 0u, size + 8u, sizeof(link->reply), 0);
}

static int set_mac(wifi_link_t *link, uint8_t iface) {
    uint32_t size = variable(link, "cur_etheraddr");
    copy(link->buffer + size, link->report.mac, 6u);
    return command(link, CMD_SET_VAR, 1u, iface, size + 6u, sizeof(link->reply), 0);
}

static int passphrase(wifi_link_t *link, uint8_t iface) {
    clear(link->buffer, sizeof(link->buffer));
    link->buffer[0] = 32u;
    link->buffer[2] = 1u; /* wsec_pmk: ASCII passphrase, not a raw 32-byte PMK. */
    copy(link->buffer + 4u, (const uint8_t *)link->password, 32u);
    int ok = command(link, CMD_PMK, 1u, iface, 68u, sizeof(link->reply), 0);
    clear(link->buffer, sizeof(link->buffer));
    clear(link->reply, sizeof(link->reply));
    return ok;
}

static int configure_ap(wifi_link_t *link) {
    if (!var_scalar(link, "ampdu_ba_wsize", 2u, 0u)) return 0;
    uint32_t size = variable(link, "bsscfg:ssid");
    store(link->buffer + size, 1u);
    store(link->buffer + size + 4u, 15u);
    copy(link->buffer + size + 8u, (const uint8_t *)link->ssid, 15u);
    if (!command(link, CMD_SET_VAR, 1u, 0u, size + 40u, sizeof(link->reply), 0) ||
        !set_mac(link, 1u) ||
        !scalar(link, CMD_CHANNEL, 1u, 0u) ||
        !var_pair(link, "bsscfg:wsec", 1u, link->open_network ? 0u : 4u) ||
        !var_scalar(link, "mfp", 0u, 1u) ||
        !var_pair(link, "bsscfg:wpa_auth", 1u, link->open_network ? 0u : 0x80u) ||
        (!link->open_network && !passphrase(link, 1u)) ||
        !scalar(link, CMD_GMODE, 1u, 1u) ||
        !var_scalar(link, "2g_mrate", 22u, 1u) ||
        !scalar(link, CMD_DTIM, 1u, 1u)) return 0;
    return var_pair(link, "bss", 1u, 1u);
}

static int configure_station(wifi_link_t *link) {
    if (!scalar(link, CMD_WSEC, link->open_network ? 0u : 4u, 0u) ||
        !var_pair(link, "bsscfg:sup_wpa", 0u, link->open_network ? 0u : 1u) ||
        (!link->open_network &&
         (!var_pair(link, "bsscfg:sup_wpa2_eapver", 0u, 0xffffffffu) ||
          !var_pair(link, "bsscfg:sup_wpa_tmo", 0u, 5000u) ||
          !passphrase(link, 0u))) ||
        !scalar(link, CMD_INFRA, 1u, 0u) ||
        !scalar(link, CMD_AUTH, 0u, 0u) ||
        !var_scalar(link, "mfp", link->open_network ? 0u : 1u, 0u) ||
        !scalar(link, CMD_WPA_AUTH, link->open_network ? 0u : 0x80u, 0u)) return 0;

    /* Extended join scans channel 1; WPA2 completes asynchronously when enabled. */
    uint32_t size = variable(link, "join");
    uint8_t *join = link->buffer + size;
    store(join, 15u);
    copy(join + 4u, (const uint8_t *)link->ssid, 15u);
    for (uint32_t i = 40u; i < 56u; i += 4u) store(join + i, 0xffffffffu);
    for (uint32_t i = 56u; i < 62u; ++i) join[i] = 0xffu;
    store(join + 64u, 1u);
    join[68] = 1u;
    join[69] = 0x10u; /* 2.4 GHz, 20 MHz, channel 1 chanspec. */
    return command(link, CMD_SET_VAR, 1u, 0u, size + 70u, sizeof(link->reply), 0);
}

void wifi_link_init(wifi_link_t *link, const wifi_link_ops_t *ops, uint8_t role,
                    const uint8_t nonce[16], const uint8_t mac[6]) {
    if (link == 0) return;
    clear((uint8_t *)link, sizeof(*link));
    link->report.role = role;
    if (ops == 0 || ops->control == 0 || nonce == 0 || mac == 0 || role > WIFI_LINK_OPEN_STATION) {
        fail(link, WIFI_LINK_BAD_CONFIG);
        return;
    }
    link->open_network = role >= WIFI_LINK_OPEN_AP;
    if (link->open_network) link->report.role = role == WIFI_LINK_OPEN_AP ? WIFI_LINK_AP : WIFI_LINK_STATION;
    link->ops = *ops;
    copy(link->report.mac, mac, 6u);
    if ((mac[0] & 3u) != 2u) {
        fail(link, WIFI_LINK_BAD_CONFIG);
        return;
    }
    const char hex[] = "0123456789abcdef";
    const char prefix[] = "U26LAB-";
    for (uint32_t i = 0u; i < 7u; ++i) link->ssid[i] = prefix[i];
    for (uint32_t i = 0u; i < 16u; ++i) {
        if (!link->open_network) {
            link->password[2u * i] = hex[nonce[i] >> 4u];
            link->password[2u * i + 1u] = hex[nonce[i] & 15u];
        }
        if (i < 4u) {
            link->ssid[7u + 2u * i] = hex[nonce[i] >> 4u];
            link->ssid[8u + 2u * i] = hex[nonce[i] & 15u];
        }
    }
}

int wifi_link_configure(wifi_link_t *link) {
    if (link == 0) return 0;
    if (link->report.stage == WIFI_LINK_FAILED) return 0;
    if (link->ops.control == 0 || link->report.role > WIFI_LINK_STATION ||
        (link->report.stage != WIFI_LINK_IDLE && link->report.stage != WIFI_LINK_FIRMWARE))
        return fail(link, WIFI_LINK_BAD_CONFIG);
    link->report.stage = WIFI_LINK_CONFIGURE;
    /* APSTA and the primary MAC require DOWN even if firmware was just loaded. */
    if (!scalar(link, CMD_DOWN, 1u, 0u) ||
        !var_scalar(link, "bus:txglom", 0u, 0u) ||
        !var_scalar(link, "apsta", 1u, 0u) || !set_mac(link, 0u)) return 0;
    uint32_t size = variable(link, "country");
    store(link->buffer + size, 0x5858u);
    store(link->buffer + size + 4u, 0xffffffffu);
    store(link->buffer + size + 8u, 0x5858u);
    if (!command(link, CMD_SET_VAR, 1u, 0u, size + 12u, sizeof(link->reply), 0) ||
        !scalar(link, CMD_ANTENNA, 0u, 0u) ||
        !var_scalar(link, "ampdu_ba_wsize", 8u, 0u) ||
        !var_scalar(link, "ampdu_mpdu", 4u, 0u) ||
        !var_scalar(link, "ampdu_rx_factor", 0u, 0u)) return 0;

    size = variable(link, "bsscfg:event_msgs");
    /* Subscribe only to association/authentication/link events; poll uses queries. */
    store(link->buffer + size, 0u);
    link->buffer[size + 4u] = 0xebu;
    link->buffer[size + 5u] = 0x18u;
    link->buffer[size + 6u] = 1u;
    link->buffer[size + 9u] = 0x40u;
    if (!command(link, CMD_SET_VAR, 1u, 0u, size + 4u + 19u, sizeof(link->reply), 0))
        return 0;
    link->report.stage = WIFI_LINK_START;
    clear(link->buffer, sizeof(link->buffer));
    if (!command(link, CMD_UP, 1u, 0u, 0u, sizeof(link->reply), 0) ||
        !scalar(link, CMD_PM, 0u, 0u)) return 0;
    if (!(link->report.role == WIFI_LINK_AP ? configure_ap(link) : configure_station(link)))
        return 0;

    variable(link, "cur_etheraddr");
    uint32_t received = 0u;
    uint8_t iface = link->report.role == WIFI_LINK_AP ? 1u : 0u;
    if (!command(link, CMD_GET_VAR, 0u, iface, 20u, 20u, &received)) return 0;
    if (received < 6u || !equal_mac(link->reply, link->report.mac))
        return fail(link, WIFI_LINK_BAD_REPLY);
    waiting(link);
    return 1;
}

static int authorization(wifi_link_t *link, uint8_t iface) {
    uint32_t size = variable(link, "sta_info"), received = 0u;
    copy(link->buffer + size, link->report.peer, 6u);
    /* GET_VAR's wire buffer length is also its firmware reply budget. */
    if (!command(link, CMD_GET_VAR, 0u, iface, sizeof(link->buffer),
                 sizeof(link->reply), &received)) {
        if (link->report.error == WIFI_LINK_RADIO_ERROR &&
            (link->report.controller_status == 0xffffffefu ||
             link->report.controller_status == 0xffffffe2u)) {
            waiting(link); /* Peer disappeared between association and sta_info. */
            return 1;
        }
        return 0;
    }
    if (received < STA_INFO_MIN_SIZE) return fail(link, WIFI_LINK_BAD_REPLY);
    uint32_t version = read16(link->reply), length = read16(link->reply + 2u);
    /* sta_info versions 3..7 retain this aligned common prefix. */
    if (version < 3u || version > 7u || length < STA_INFO_MIN_SIZE || length > received ||
        !equal_mac(link->reply + 16u, link->report.peer))
        return fail(link, WIFI_LINK_BAD_REPLY);
    uint32_t flags = read32(link->reply + 8u);
    link->report.associated = (flags & STA_ASSOC) != 0u;
    if (link->report.associated && (link->open_network || (flags & STA_AUTHORIZED))) {
        link->report.authorized = (flags & STA_AUTHORIZED) != 0u;
        link->report.stage = WIFI_LINK_CONNECTED;
    }
    return 1;
}

int wifi_link_poll(wifi_link_t *link) {
    if (link == 0) return 0;
    if (link->report.stage == WIFI_LINK_FAILED) return 0;
    if (link->ops.control == 0 || link->report.role > WIFI_LINK_STATION ||
        (link->report.stage != WIFI_LINK_WAIT_PEER && link->report.stage != WIFI_LINK_CONNECTED))
        return fail(link, WIFI_LINK_BAD_CONFIG);
    waiting(link);
    clear(link->buffer, sizeof(link->buffer));
    uint32_t received = 0u;
    if (link->report.role == WIFI_LINK_AP) {
        store(link->buffer, MAX_PEERS);
        if (!command(link, CMD_ASSOCLIST, 0u, 1u, ASSOCLIST_SIZE, ASSOCLIST_SIZE, &received))
            return 0;
        if (received < 4u) return fail(link, WIFI_LINK_BAD_REPLY);
        uint32_t count = read32(link->reply);
        if (count > MAX_PEERS || received < 4u + 6u * count)
            return fail(link, WIFI_LINK_BAD_REPLY);
        for (uint32_t i = 0u; i < count; ++i) {
            const uint8_t *mac = link->reply + 4u + 6u * i;
            if (!peer_mac(link, mac)) return fail(link, WIFI_LINK_BAD_REPLY);
            for (uint32_t j = 0u; j < i; ++j)
                if (equal_mac(mac, link->reply + 4u + 6u * j))
                    return fail(link, WIFI_LINK_BAD_REPLY);
        }
        if (count == 0u) return 1;
        link->report.peer_count = count;
        copy(link->report.peer, link->reply + 4u, 6u);
        return authorization(link, 1u);
    }
    if (!command(link, CMD_BSSID, 0u, 0u, 6u, 6u, &received)) {
        if (link->report.error == WIFI_LINK_RADIO_ERROR &&
            link->report.controller_status == 0xffffffefu) {
            waiting(link); /* BCME_NOTASSOCIATED during scan/join is not failure. */
            return 1;
        }
        return 0;
    }
    if (received != 6u) return fail(link, WIFI_LINK_BAD_REPLY);
    if (zero_mac(link->reply)) return 1;
    if (!peer_mac(link, link->reply)) return fail(link, WIFI_LINK_BAD_REPLY);
    copy(link->report.peer, link->reply, 6u);
    link->report.peer_count = 1u;
    return authorization(link, 0u);
}

const char *wifi_link_stage_name(wifi_link_stage_t stage) {
    switch (stage) {
    case WIFI_LINK_IDLE: return "IDLE";
    case WIFI_LINK_FIRMWARE: return "FIRMWARE";
    case WIFI_LINK_CONFIGURE: return "CONFIGURE";
    case WIFI_LINK_START: return "START";
    case WIFI_LINK_WAIT_PEER: return "WAIT PEER";
    case WIFI_LINK_CONNECTED: return "CONNECTED";
    case WIFI_LINK_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

const char *wifi_link_error_name(wifi_link_error_t error) {
    switch (error) {
    case WIFI_LINK_OK: return "OK";
    case WIFI_LINK_RADIO_ERROR: return "RADIO ERROR";
    case WIFI_LINK_BAD_REPLY: return "BAD REPLY";
    case WIFI_LINK_JOIN_TIMEOUT: return "JOIN TIMEOUT";
    case WIFI_LINK_BAD_CONFIG: return "BAD CONFIG";
    default: return "UNKNOWN";
    }
}
