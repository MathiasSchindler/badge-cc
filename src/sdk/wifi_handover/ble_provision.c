#include "ble_provision.h"
#include "provision_protocol.h"

#include <stdio.h>
#include <string.h>
#include "btstack.h"
#include "wifi_handover.h"

#define HOST_STALE_MS 8000u
#define STARTUP_TIMEOUT_MS 15000u
#define OPERATION_TIMEOUT_MS 15000u
#define ERROR_RESPONSE_GRACE_MS 100u
#define NO_SELECTION UINT8_MAX
#define VALUE_HANDLE \
    ATT_CHARACTERISTIC_0000FF2B_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE
#define ATT_ERROR_ADMISSION_CLOSED 0x80u

typedef enum { MODE_NONE, MODE_HOST, MODE_SCAN, MODE_CLIENT } ble_mode_t;
typedef enum { QUERY_NONE, QUERY_SERVICE, QUERY_CHARACTERISTIC, QUERY_VALUE } query_t;
typedef struct {
    bd_addr_t address;
    bd_addr_type_t address_type;
    uint32_t session_id;
    uint32_t seen_ms;
    int8_t rssi;
} host_t;

static const uuid128_t service_uuid = {
    0x00,0x00,0xff,0x2a,0x00,0x00,0x10,0x00,
    0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb
};
static const uuid128_t characteristic_uuid = {
    0x00,0x00,0xff,0x2b,0x00,0x00,0x10,0x00,
    0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb
};
static uint8_t advertising_data[] = {
    2, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    3, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x2a, 0xff,
    8, BLUETOOTH_DATA_TYPE_SERVICE_DATA_16_BIT_UUID, 0x2a, 0xff,
    PROVISION_VERSION, 0, 0, 0, 0,
    9, BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME, 'U','2','6',' ','W','I','F','I'
};

static ble_provision_view_t view;
static ble_offer_fn offer_callback;
static ble_received_fn received_callback;
static btstack_packet_callback_registration_t hci_registration;
static host_t hosts[EXP_MAX_HOSTS];
static host_t target;
static ble_mode_t mode;
static query_t query;
static bool initialized;
static bool startup_pending;
static bool admission_open;
static bool selection_lost;
static bool initiating;
static bool central;
static bool disconnect_pending;
static bool disconnect_issued;
static bool snapshot_attempted;
static bool snapshot_served;
static bool received_delivered;
static uint8_t snapshot_error;
static uint8_t snapshot[PROVISION_WIRE_SIZE];
static uint32_t now_ms;
static uint32_t startup_deadline_ms;
static uint32_t deadline_ms;
static uint32_t disconnect_at_ms;
static uint32_t generation;
static uint32_t connection_generation;
static uint32_t query_cookie;
static uint32_t host_session;
static hci_con_handle_t connection = HCI_CON_HANDLE_INVALID;
static bd_addr_t remote_address;
static gatt_client_service_t peer_service;
static gatt_client_characteristic_t peer_characteristic;
static provision_rx_t receive_buffer;

static void client_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size);

static void status(ble_provision_phase_t phase, int error, const char *text) {
    view.phase = phase;
    view.error_code = error;
    snprintf(view.status, sizeof(view.status), "%s", text);
}

static bool elapsed(uint32_t deadline) {
    return (int32_t)(now_ms - deadline) >= 0;
}

static bool valid_address(const bd_addr_t address, bd_addr_type_t type) {
    if (type != BD_ADDR_TYPE_LE_PUBLIC && type != BD_ADDR_TYPE_LE_RANDOM)
        return false;
    uint8_t any = 0u, all = 0xffu;
    for (unsigned i = 0; i < 6u; ++i) {
        any |= address[i];
        all &= address[i];
    }
    return any != 0u && all != 0xffu &&
           (type != BD_ADDR_TYPE_LE_RANDOM || (address[0] & 0xc0u) != 0x80u);
}

static bool operation_allowed(void) {
    if (!view.stack_ready) {
        view.error_code = BLE_PROVISION_ERR_NOT_READY;
        snprintf(view.status, sizeof(view.status), "BLE stack not ready");
        return false;
    }
    if (initiating || connection != HCI_CON_HANDLE_INVALID) {
        view.error_code = BLE_PROVISION_ERR_BUSY;
        snprintf(view.status, sizeof(view.status), "BLE busy; wait or stop");
        return false;
    }
    return true;
}

static void disable_air(void) {
    gap_stop_scan();
    gap_advertisements_enable(0);
}

static void invalidate_query(void) {
    query = QUERY_NONE;
    ++query_cookie;
    provision_rx_init(&receive_buffer);
}

static void request_disconnect(uint32_t delay_ms) {
    if (connection == HCI_CON_HANDLE_INVALID || disconnect_issued) return;
    if (!disconnect_pending || delay_ms == 0u)
        disconnect_at_ms = now_ms + delay_ms;
    disconnect_pending = true;
    if (delay_ms == 0u) {
        uint8_t result = gap_disconnect(connection);
        disconnect_issued = result == ERROR_CODE_SUCCESS;
        if (!disconnect_issued) {
            view.error_code = result;
            snprintf(view.status, sizeof(view.status), "BLE disconnect failed");
        }
    }
}

static void fail(int error, const char *text, uint32_t disconnect_delay) {
    admission_open = false;
    disable_air();
    mode = MODE_NONE;
    invalidate_query();
    status(BLE_ERROR, error, text);
    if (initiating) gap_connect_cancel();
    request_disconnect(disconnect_delay);
}

static void refresh_selection(void) {
    if (view.selected < view.host_count) {
        view.session_id = hosts[view.selected].session_id;
        view.rssi = hosts[view.selected].rssi;
    } else {
        view.selected = NO_SELECTION;
        view.session_id = 0u;
        view.rssi = -127;
    }
}

static void expire_hosts(void) {
    for (uint8_t i = 0u; i < view.host_count;) {
        if ((uint32_t)(now_ms - hosts[i].seen_ms) < HOST_STALE_MS) {
            ++i;
            continue;
        }
        if (view.selected == i) {
            view.selected = NO_SELECTION;
            selection_lost = true;
            status(BLE_SCANNING, BLE_PROVISION_ERR_NO_HOST,
                   "Selected host expired; choose next");
        } else if (view.selected != NO_SELECTION && view.selected > i) {
            --view.selected;
        }
        --view.host_count;
        memmove(hosts + i, hosts + i + 1u,
                (view.host_count - i) * sizeof(hosts[0]));
        memset(hosts + view.host_count, 0, sizeof(hosts[0]));
    }
    refresh_selection();
}

static void advertise(void) {
    bd_addr_t unused = {0};
    little_endian_store_32(advertising_data, 12, host_session);
    gap_advertisements_set_params(0x00a0u, 0x00f0u, 0u, 0u, unused, 0x07u, 0u);
    gap_advertisements_set_data(sizeof(advertising_data), advertising_data);
    gap_advertisements_enable(1);
    status(BLE_ADVERTISING, 0, "Lab BLE: admission open");
}

/* Parse all AD fields, rejecting malformed/ambiguous session data. A scan
 * response alone is never a connectable host announcement. */
static void advertisement(uint8_t *packet, uint16_t size) {
    if (mode != MODE_SCAN || size < 12u ||
        gap_event_advertising_report_get_advertising_event_type(packet) != 0u)
        return;
    uint8_t length = gap_event_advertising_report_get_data_length(packet);
    if ((size_t)length + 12u != size) return;
    const uint8_t *data = gap_event_advertising_report_get_data(packet);
    bool uuid_found = false, session_found = false;
    uint32_t session = 0u;
    for (size_t offset = 0u; offset < length;) {
        size_t field_length = data[offset++];
        if (field_length == 0u || field_length > length - offset) return;
        uint8_t type = data[offset];
        const uint8_t *value = data + offset + 1u;
        size_t value_length = field_length - 1u;
        if (type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS ||
            type == BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS) {
            if ((value_length & 1u) != 0u) return;
            for (size_t i = 0u; i < value_length; i += 2u)
                if (value[i] == 0x2au && value[i + 1u] == 0xffu)
                    uuid_found = true;
        }
        if (type == BLUETOOTH_DATA_TYPE_SERVICE_DATA_16_BIT_UUID &&
            value_length >= 2u && value[0] == 0x2au && value[1] == 0xffu) {
            if (session_found || value_length != 7u ||
                value[2] != PROVISION_VERSION) return;
            session = little_endian_read_32(value, 3);
            session_found = true;
        }
        offset += field_length;
    }
    if (!uuid_found || !session_found || session == 0u) return;
    host_t candidate = {0};
    gap_event_advertising_report_get_address(packet, candidate.address);
    candidate.address_type = gap_event_advertising_report_get_address_type(packet);
    if (!valid_address(candidate.address, candidate.address_type) ||
        memcmp(candidate.address, view.local_address, 6u) == 0) return;
    candidate.session_id = session;
    candidate.rssi = gap_event_advertising_report_get_rssi(packet);
    candidate.seen_ms = now_ms;
    expire_hosts();
    for (uint8_t i = 0u; i < view.host_count; ++i) {
        if (hosts[i].address_type != candidate.address_type ||
            memcmp(hosts[i].address, candidate.address, 6u) != 0) continue;
        if (hosts[i].session_id != session) {
            if (view.selected == i) {
                view.selected = NO_SELECTION;
                selection_lost = true;
                status(BLE_SCANNING, BLE_PROVISION_ERR_SESSION,
                       "Host session changed; choose next");
            }
        }
        hosts[i] = candidate;
        refresh_selection();
        return;
    }
    if (view.host_count == EXP_MAX_HOSTS) return;
    hosts[view.host_count++] = candidate;
    if (view.selected == NO_SELECTION && !selection_lost) {
        view.selected = 0u;
        status(BLE_SCANNING, 0, "Host found; select then connect");
    }
    refresh_selection();
}

static uint16_t read_callback(hci_con_handle_t handle, uint16_t attribute,
                              uint16_t offset, uint8_t *buffer,
                              uint16_t buffer_size) {
    if (attribute != VALUE_HANDLE)
        return ATT_READ_ERROR_CODE_OFFSET + ATT_ERROR_INVALID_HANDLE;
    if (handle != connection || central ||
        connection_generation != generation || mode != MODE_HOST ||
        !admission_open || disconnect_pending)
        return ATT_READ_ERROR_CODE_OFFSET +
               (snapshot_error != 0u ? snapshot_error : ATT_ERROR_ADMISSION_CLOSED);
    if (offset > PROVISION_WIRE_SIZE)
        return ATT_READ_ERROR_CODE_OFFSET + ATT_ERROR_INVALID_OFFSET;
    if (!snapshot_attempted) {
        if (offset != 0u)
            return ATT_READ_ERROR_CODE_OFFSET + ATT_ERROR_INVALID_OFFSET;
        snapshot_attempted = true;
        handover_credentials_t credentials = {0};
        uint32_t saved_generation = generation;
        bool offered = offer_callback != NULL &&
                       offer_callback(remote_address, &credentials);
        if (generation != saved_generation || handle != connection ||
            !admission_open || mode != MODE_HOST)
            return ATT_READ_ERROR_CODE_OFFSET + ATT_ERROR_ADMISSION_CLOSED;
        if (!offered) {
            snapshot_error = ATT_ERROR_ADMISSION_CLOSED;
            fail(snapshot_error, "Host full; admission closed", ERROR_RESPONSE_GRACE_MS);
        } else if (credentials.session_id != host_session ||
                   !provision_encode(&credentials, snapshot, sizeof(snapshot))) {
            snapshot_error = ATT_ERROR_UNLIKELY_ERROR;
            fail(BLE_PROVISION_ERR_PROTOCOL, "Invalid host credentials",
                 ERROR_RESPONSE_GRACE_MS);
        }
        memset(&credentials, 0, sizeof(credentials));
    }
    if (snapshot_error != 0u)
        return ATT_READ_ERROR_CODE_OFFSET + snapshot_error;
    uint16_t copied = att_read_callback_handle_blob(
        snapshot, sizeof(snapshot), offset, buffer, buffer_size);
    if (buffer != NULL && offset + copied == sizeof(snapshot))
        snapshot_served = true;
    return copied;
}

static void start_query(query_t next) {
    query = next;
    ++query_cookie;
    uint16_t high = (uint16_t)(query_cookie >> 16);
    uint16_t low = (uint16_t)query_cookie;
    uint8_t result;
    switch (next) {
    case QUERY_SERVICE:
        result = gatt_client_discover_primary_services_by_uuid128_with_context(
            client_handler, connection, service_uuid, high, low);
        break;
    case QUERY_CHARACTERISTIC:
        result = gatt_client_discover_characteristics_for_service_with_context(
            client_handler, connection, &peer_service, high, low);
        break;
    case QUERY_VALUE:
        result = gatt_client_read_long_value_of_characteristic_using_value_handle_with_context(
            client_handler, connection, peer_characteristic.value_handle, high, low);
        break;
    default:
        return;
    }
    if (result != ERROR_CODE_SUCCESS)
        fail(result, "Cannot start GATT query", 0u);
}

static void client_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET || size < 8u || query == QUERY_NONE ||
        !central || disconnect_pending || mode != MODE_CLIENT ||
        connection_generation != generation ||
        little_endian_read_16(packet, 2) != connection ||
        little_endian_read_16(packet, 4) != (uint16_t)(query_cookie >> 16) ||
        little_endian_read_16(packet, 6) != (uint16_t)query_cookie) return;
    switch (hci_event_packet_get_type(packet)) {
    case GATT_EVENT_SERVICE_QUERY_RESULT: {
        if (query != QUERY_SERVICE || size < 28u) break;
        gatt_client_service_t found;
        gatt_event_service_query_result_get_service(packet, &found);
        if (peer_service.start_group_handle != 0u ||
            found.start_group_handle == 0u ||
            found.end_group_handle <= found.start_group_handle ||
            memcmp(found.uuid128, service_uuid, sizeof(service_uuid)) != 0) {
            fail(BLE_PROVISION_ERR_PROTOCOL, "Invalid provisioning service", 0u);
            break;
        }
        peer_service = found;
        break;
    }
    case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
        if (query != QUERY_CHARACTERISTIC || size < 32u) break;
        gatt_client_characteristic_t found;
        gatt_event_characteristic_query_result_get_characteristic(packet, &found);
        if (memcmp(found.uuid128, characteristic_uuid, sizeof(characteristic_uuid)) != 0)
            break;
        if (peer_characteristic.value_handle != 0u ||
            found.start_handle <= peer_service.start_group_handle ||
            found.value_handle <= found.start_handle ||
            found.value_handle > found.end_handle ||
            found.end_handle > peer_service.end_group_handle ||
            (found.properties & ATT_PROPERTY_READ) == 0u) {
            fail(BLE_PROVISION_ERR_PROTOCOL, "Invalid provisioning value", 0u);
            break;
        }
        peer_characteristic = found;
        break;
    }
    case GATT_EVENT_LONG_CHARACTERISTIC_VALUE_QUERY_RESULT: {
        if (query != QUERY_VALUE || size < 14u) break;
        uint16_t length = gatt_event_long_characteristic_value_query_result_get_value_length(packet);
        if (gatt_event_long_characteristic_value_query_result_get_value_handle(packet) !=
                peer_characteristic.value_handle ||
            (size_t)length + 14u != size ||
            !provision_rx_append(&receive_buffer,
                gatt_event_long_characteristic_value_query_result_get_value_offset(packet),
                gatt_event_long_characteristic_value_query_result_get_value(packet),
                length))
            fail(BLE_PROVISION_ERR_PROTOCOL, "Invalid GATT fragment", 0u);
        break;
    }
    case GATT_EVENT_QUERY_COMPLETE: {
        if (size != 9u) {
            fail(BLE_PROVISION_ERR_PROTOCOL, "Invalid GATT completion", 0u);
            break;
        }
        uint8_t result = gatt_event_query_complete_get_att_status(packet);
        if (result != ATT_ERROR_SUCCESS) {
            fail(result, result == ATT_ERROR_ADMISSION_CLOSED ?
                 "Host full or admission closed" : "GATT query failed", 0u);
        } else if (query == QUERY_SERVICE) {
            if (peer_service.start_group_handle == 0u)
                fail(BLE_PROVISION_ERR_PROTOCOL, "Provisioning service missing", 0u);
            else start_query(QUERY_CHARACTERISTIC);
        } else if (query == QUERY_CHARACTERISTIC) {
            if (peer_characteristic.value_handle == 0u)
                fail(BLE_PROVISION_ERR_PROTOCOL, "Provisioning value missing", 0u);
            else start_query(QUERY_VALUE);
        } else if (query == QUERY_VALUE) {
            handover_credentials_t credentials;
            if (!provision_rx_finish(&receive_buffer, true, target.session_id,
                                     &credentials)) {
                fail(BLE_PROVISION_ERR_PROTOCOL, "Invalid config or session mismatch", 0u);
                break;
            }
            invalidate_query();
            mode = MODE_NONE;
            status(BLE_COMPLETE, 0, "Config received; leaving BLE");
            if (!received_delivered) {
                received_delivered = true;
                if (received_callback != NULL) received_callback(&credentials);
            }
            memset(&credentials, 0, sizeof(credentials));
            request_disconnect(0u);
        }
        break;
    }
    default:
        break;
    }
}

static void connection_complete(uint8_t *packet) {
    uint8_t result = gap_subevent_le_connection_complete_get_status(packet);
    if (result != ERROR_CODE_SUCCESS) {
        bool was_initiating = initiating;
        initiating = false;
        if (was_initiating && mode == MODE_CLIENT)
            fail(result, "BLE connection failed", 0u);
        else if (was_initiating && view.phase == BLE_IDLE)
            status(BLE_IDLE, 0, "BLE stopped; Wi-Fi unchanged");
        return;
    }
    hci_con_handle_t handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
    bool is_central = gap_subevent_le_connection_complete_get_role(packet) == HCI_ROLE_MASTER;
    bd_addr_t address;
    gap_subevent_le_connection_complete_get_peer_address(packet, address);
    bd_addr_type_t type = gap_subevent_le_connection_complete_get_peer_address_type(packet);
    bool expected = valid_address(address, type) &&
        connection == HCI_CON_HANDLE_INVALID &&
        ((is_central && initiating && mode == MODE_CLIENT &&
          type == target.address_type && memcmp(address, target.address, 6u) == 0) ||
         (!is_central && !initiating && mode == MODE_HOST && admission_open));
    if (is_central) initiating = false;
    if (connection != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(handle);
        return;
    }
    connection = handle;
    central = is_central;
    connection_generation = generation;
    disconnect_pending = false;
    disconnect_issued = false;
    disable_air();
    if (!expected) {
        bool stopping = mode == MODE_NONE;
        if (stopping) request_disconnect(0u);
        else fail(BLE_PROVISION_ERR_PEER, "Unexpected BLE peer or address", 0u);
        return;
    }
    memcpy(remote_address, address, sizeof(remote_address));
    snapshot_attempted = false;
    snapshot_served = false;
    snapshot_error = 0u;
    received_delivered = false;
    memset(snapshot, 0, sizeof(snapshot));
    memset(&peer_service, 0, sizeof(peer_service));
    memset(&peer_characteristic, 0, sizeof(peer_characteristic));
    provision_rx_init(&receive_buffer);
    status(BLE_READING, 0, central ? "Reading selected host config" : "Serving lab BLE credentials");
    if (central) start_query(QUERY_SERVICE);
    else deadline_ms = now_ms + OPERATION_TIMEOUT_MS;
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET || size < 2u) return;
    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (size < 3u) break;
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING &&
            startup_pending) {
            startup_pending = false;
            if ((int32_t)(btstack_run_loop_get_time_ms() - startup_deadline_ms) >= 0) {
                status(BLE_ERROR, BLE_PROVISION_ERR_TIMEOUT,
                       "BLE startup timed out; reset badge");
                break;
            }
            view.stack_ready = true;
            gap_local_bd_addr(view.local_address);
            status(BLE_IDLE, 0, "Lab BLE ready; unencrypted");
        } else if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING &&
                   view.stack_ready) {
            view.stack_ready = false;
            fail(BLE_PROVISION_ERR_NOT_READY, "BLE stack stopped unexpectedly", 0u);
        }
        break;
    case GAP_EVENT_ADVERTISING_REPORT:
        advertisement(packet, size);
        break;
    case HCI_EVENT_META_GAP:
        if (size >= 36u && hci_event_gap_meta_get_subevent_code(packet) ==
                           GAP_SUBEVENT_LE_CONNECTION_COMPLETE)
            connection_complete(packet);
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (size < 6u || hci_event_disconnection_complete_get_connection_handle(packet) != connection)
            break;
        connection = HCI_CON_HANDLE_INVALID;
        disconnect_pending = false;
        disconnect_issued = false;
        invalidate_query();
        memset(snapshot, 0, sizeof(snapshot));
        memset(remote_address, 0, sizeof(remote_address));
        if (mode == MODE_HOST && admission_open) {
            advertise();
            if (!snapshot_served)
                snprintf(view.status, sizeof(view.status), "Client left; admission open");
        } else if (mode == MODE_CLIENT) {
            fail(BLE_PROVISION_ERR_DISCONNECTED, "BLE disconnected before config", 0u);
        } else if (view.phase == BLE_IDLE) {
            status(BLE_IDLE, 0, "BLE stopped; Wi-Fi unchanged");
        }
        break;
    case HCI_EVENT_HARDWARE_ERROR:
        startup_pending = false;
        view.stack_ready = false;
        fail(size >= 3u ? packet[2] : BLE_PROVISION_ERR_NOT_READY,
             "BLE controller error", 0u);
        break;
    case BTSTACK_EVENT_POWERON_FAILED:
        startup_pending = false;
        view.stack_ready = false;
        status(BLE_ERROR, BLE_PROVISION_ERR_NOT_READY, "BLE power-on failed");
        break;
    default:
        break;
    }
}

void ble_provision_init(ble_offer_fn offer, ble_received_fn received) {
    if (initialized) return;
    initialized = true;
    startup_pending = true;
    now_ms = btstack_run_loop_get_time_ms();
    startup_deadline_ms = now_ms + STARTUP_TIMEOUT_MS;
    offer_callback = offer;
    received_callback = received;
    view.selected = NO_SELECTION;
    view.rssi = -127;
    status(BLE_STARTING, 0, "Starting lab BLE");
    l2cap_init();
    gatt_client_init();
    gatt_client_mtu_enable_auto_negotiation(0u);
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(0u);
    att_server_init(profile_data, read_callback, NULL);
    hci_registration.callback = packet_handler;
    hci_add_event_handler(&hci_registration);
    int result = hci_power_control(HCI_POWER_ON);
    if (result != 0) {
        startup_pending = false;
        status(BLE_ERROR, result, "Cannot power on BLE");
    }
}

void ble_provision_host(uint32_t session_id) {
    if (!operation_allowed()) return;
    if (session_id == 0u) {
        view.error_code = BLE_PROVISION_ERR_SESSION;
        snprintf(view.status, sizeof(view.status), "Host session must be nonzero");
        return;
    }
    ++generation;
    disable_air();
    invalidate_query();
    mode = MODE_HOST;
    admission_open = true;
    host_session = session_id;
    view.host_count = 0u;
    view.selected = NO_SELECTION;
    view.session_id = session_id;
    view.rssi = -127;
    advertise();
}

void ble_provision_scan(void) {
    if (!operation_allowed()) return;
    ++generation;
    admission_open = false;
    disable_air();
    invalidate_query();
    mode = MODE_SCAN;
    selection_lost = false;
    memset(hosts, 0, sizeof(hosts));
    view.host_count = 0u;
    view.selected = NO_SELECTION;
    refresh_selection();
    gap_set_scan_parameters(0u, 0x0060u, 0x0030u);
    gap_start_scan();
    status(BLE_SCANNING, 0, "Scanning for selected hosts");
}

void ble_provision_next(void) {
    if (!operation_allowed()) return;
    if (mode != MODE_SCAN) {
        view.error_code = BLE_PROVISION_ERR_NO_HOST;
        snprintf(view.status, sizeof(view.status), "Scan before selecting a host");
        return;
    }
    expire_hosts();
    if (view.host_count == 0u) {
        status(BLE_SCANNING, BLE_PROVISION_ERR_NO_HOST, "No live BLE hosts");
        return;
    }
    view.selected = view.selected == NO_SELECTION ? 0u :
                    (uint8_t)((view.selected + 1u) % view.host_count);
    selection_lost = false;
    refresh_selection();
    status(BLE_SCANNING, 0, "Host selected; connect to join");
}

void ble_provision_connect(void) {
    if (!operation_allowed()) return;
    if (mode == MODE_SCAN) expire_hosts();
    if (mode != MODE_SCAN || view.selected >= view.host_count) {
        view.error_code = BLE_PROVISION_ERR_NO_HOST;
        snprintf(view.status, sizeof(view.status), "Select a live host first");
        return;
    }
    target = hosts[view.selected];
    ++generation;
    disable_air();
    mode = MODE_CLIENT;
    initiating = true;
    deadline_ms = now_ms + OPERATION_TIMEOUT_MS;
    status(BLE_CONNECTING, 0, "Connecting to selected host");
    uint8_t result = gap_connect(target.address, target.address_type);
    if (result != ERROR_CODE_SUCCESS) {
        initiating = false;
        fail(result, "Cannot initiate BLE connection", 0u);
    }
}

void ble_provision_stop(void) {
    ++generation;
    admission_open = false;
    mode = MODE_NONE;
    invalidate_query();
    memset(snapshot, 0, sizeof(snapshot));
    if (initialized) {
        disable_air();
        if (initiating) gap_connect_cancel();
        request_disconnect(0u);
    }
    if (!view.stack_ready && view.phase == BLE_ERROR) return;
    status(view.stack_ready ? BLE_IDLE : BLE_STARTING, 0,
           initiating || connection != HCI_CON_HANDLE_INVALID ?
           "Stopping BLE; wait for disconnect" : "BLE stopped; Wi-Fi unchanged");
}

void ble_provision_poll(uint32_t time_ms) {
    now_ms = time_ms;
    if (!initialized) return;
    if (startup_pending && elapsed(startup_deadline_ms)) {
        startup_pending = false;
        status(BLE_ERROR, BLE_PROVISION_ERR_TIMEOUT, "BLE startup timed out; reset badge");
    }
    if (!view.stack_ready) return;
    if (mode == MODE_SCAN) expire_hosts();
    if ((mode == MODE_CLIENT || (mode == MODE_HOST &&
         connection != HCI_CON_HANDLE_INVALID)) && elapsed(deadline_ms))
        fail(BLE_PROVISION_ERR_TIMEOUT, "BLE provisioning timed out", 0u);
    if (disconnect_pending && !disconnect_issued && elapsed(disconnect_at_ms))
        request_disconnect(0u);
}

const ble_provision_view_t *ble_provision_view(void) {
    return &view;
}
