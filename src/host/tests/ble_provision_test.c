#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "btstack.h"
#include "../../sdk/wifi_handover/ble_provision.h"
#include "../../sdk/wifi_handover/provision_protocol.h"
#include "wifi_handover.h"

#define VALUE_HANDLE \
    ATT_CHARACTERISTIC_0000FF2B_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE

static btstack_packet_handler_t hci_handler, gatt_handler;
static att_read_callback_t server_read;
static uint8_t ads[31], ads_length;
static bool advertising, scanning, reject_offer, reentrant_stop;
static unsigned power_ons, disconnects, cancellations, offers, received;
static uint16_t context_high, context_low, client_handle;
static unsigned query_kind;
static uint32_t time_ms;
static bd_addr_t connect_address;
static bd_addr_type_t connect_type;
static handover_credentials_t offered, delivered;
static const bd_addr_t address_a = {0xc0, 1, 2, 3, 4, 5};
static const bd_addr_t address_b = {0xc0, 1, 2, 3, 4, 6};
static const uint8_t service_uuid[16] = {
    0,0,0xff,0x2a,0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb
};

/* These stubs model the asynchronous BTstack boundary, not BLE radio timing.
 * Production sources are linked separately; no implementation C is included. */
void l2cap_init(void) {}
uint32_t btstack_run_loop_get_time_ms(void) { return time_ms; }
void gatt_client_init(void) {}
void gatt_client_mtu_enable_auto_negotiation(uint8_t enabled) { assert(!enabled); }
void sm_init(void) {}
void sm_set_io_capabilities(io_capability_t capability) {
    assert(capability == IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
}
void sm_set_authentication_requirements(uint8_t requirements) { assert(!requirements); }
void att_server_init(const uint8_t *db, att_read_callback_t read,
                     att_write_callback_t write) {
    assert(db != NULL && write == NULL);
    server_read = read;
}
void hci_add_event_handler(btstack_packet_callback_registration_t *registration) {
    hci_handler = registration->callback;
}
int hci_power_control(HCI_POWER_MODE power_mode) {
    assert(power_mode == HCI_POWER_ON);
    ++power_ons;
    return 0;
}
void gap_local_bd_addr(bd_addr_t address) {
    const bd_addr_t local = {0xc0, 1, 2, 3, 4, 7};
    memcpy(address, local, 6);
}
void gap_start_scan(void) { scanning = true; }
void gap_stop_scan(void) { scanning = false; }
void gap_set_scan_parameters(uint8_t type, uint16_t interval, uint16_t window) {
    assert(type == 0u && window <= interval);
}
void gap_advertisements_enable(int enabled) { advertising = enabled != 0; }
void gap_advertisements_set_params(uint16_t min, uint16_t max, uint8_t type,
                                  uint8_t direct_type, bd_addr_t direct,
                                  uint8_t channels, uint8_t filter) {
    (void)direct_type;
    (void)direct;
    assert(min <= max && type == 0u && channels == 7u && filter == 0u);
}
void gap_advertisements_set_data(uint8_t length, uint8_t *data) {
    assert(length <= sizeof(ads));
    memcpy(ads, data, length);
    ads_length = length;
}
uint8_t gap_connect(const bd_addr_t address, bd_addr_type_t type) {
    memcpy(connect_address, address, 6u);
    connect_type = type;
    return ERROR_CODE_SUCCESS;
}
uint8_t gap_connect_cancel(void) { ++cancellations; return ERROR_CODE_SUCCESS; }
uint8_t gap_disconnect(hci_con_handle_t handle) {
    assert(handle != HCI_CON_HANDLE_INVALID);
    ++disconnects;
    return ERROR_CODE_SUCCESS;
}
uint16_t att_read_callback_handle_blob(const uint8_t *blob, uint16_t size,
                                       uint16_t offset, uint8_t *buffer,
                                       uint16_t capacity) {
    if (buffer == NULL) return size;
    assert(offset <= size);
    uint16_t length = size - offset;
    if (length > capacity) length = capacity;
    memcpy(buffer, blob + offset, length);
    return length;
}
static uint8_t start_query(btstack_packet_handler_t handler,
                           hci_con_handle_t handle, uint16_t high,
                           uint16_t low, unsigned kind) {
    gatt_handler = handler;
    client_handle = handle;
    context_high = high;
    context_low = low;
    query_kind = kind;
    return ERROR_CODE_SUCCESS;
}
uint8_t gatt_client_discover_primary_services_by_uuid128_with_context(
    btstack_packet_handler_t handler, hci_con_handle_t handle,
    const uint8_t *uuid, uint16_t high, uint16_t low) {
    assert(memcmp(uuid, service_uuid, 16) == 0);
    return start_query(handler, handle, high, low, 1u);
}
uint8_t gatt_client_discover_characteristics_for_service_with_context(
    btstack_packet_handler_t handler, hci_con_handle_t handle,
    gatt_client_service_t *service, uint16_t high, uint16_t low) {
    assert(service->start_group_handle == 10u && service->end_group_handle == 15u);
    return start_query(handler, handle, high, low, 2u);
}
uint8_t gatt_client_read_long_value_of_characteristic_using_value_handle_with_context(
    btstack_packet_handler_t handler, hci_con_handle_t handle,
    uint16_t value_handle, uint16_t high, uint16_t low) {
    assert(value_handle == 12u);
    return start_query(handler, handle, high, low, 3u);
}
void gatt_client_deserialize_service(const uint8_t *packet, int offset,
                                     gatt_client_service_t *service) {
    memset(service, 0, sizeof(*service));
    service->start_group_handle = little_endian_read_16(packet, offset);
    service->end_group_handle = little_endian_read_16(packet, offset + 2);
    reverse_128(packet + offset + 4, service->uuid128);
}
void gatt_client_deserialize_characteristic(const uint8_t *packet, int offset,
                                            gatt_client_characteristic_t *value) {
    memset(value, 0, sizeof(*value));
    value->start_handle = little_endian_read_16(packet, offset);
    value->value_handle = little_endian_read_16(packet, offset + 2);
    value->end_handle = little_endian_read_16(packet, offset + 4);
    value->properties = little_endian_read_16(packet, offset + 6);
    reverse_128(packet + offset + 8, value->uuid128);
}

static bool offer(const uint8_t address[6], handover_credentials_t *out) {
    assert(memcmp(address, address_a, 6u) == 0);
    ++offers;
    *out = offered;
    if (reentrant_stop) ble_provision_stop();
    return !reject_offer;
}
static void receive(const handover_credentials_t *credentials) {
    delivered = *credentials;
    ++received;
    if (reentrant_stop) ble_provision_stop();
}
static void tick(uint32_t delta) {
    time_ms += delta;
    ble_provision_poll(time_ms);
}
static void connect_event(bool central, const bd_addr_t address,
                          uint8_t type, uint8_t result) {
    uint8_t packet[36] = {HCI_EVENT_META_GAP, 34,
                         GAP_SUBEVENT_LE_CONNECTION_COMPLETE};
    packet[3] = result;
    little_endian_store_16(packet, 4, 1u);
    packet[6] = central ? HCI_ROLE_MASTER : HCI_ROLE_SLAVE;
    packet[7] = type;
    reverse_bytes(address, packet + 8, 6);
    hci_handler(HCI_EVENT_PACKET, 0, packet, sizeof(packet));
}
static void disconnected(void) {
    uint8_t packet[6] = {HCI_EVENT_DISCONNECTION_COMPLETE, 4, 0, 1, 0, 0x13};
    hci_handler(HCI_EVENT_PACKET, 0, packet, sizeof(packet));
}
static void report(const bd_addr_t address, uint8_t type, uint32_t session) {
    uint8_t packet[43] = {GAP_EVENT_ADVERTISING_REPORT};
    packet[1] = ads_length + 10u;
    packet[2] = 0u;
    packet[3] = type;
    reverse_bytes(address, packet + 4, 6);
    packet[10] = (uint8_t)-42;
    packet[11] = ads_length;
    memcpy(packet + 12, ads, ads_length);
    little_endian_store_32(packet, 24, session);
    hci_handler(HCI_EVENT_PACKET, 0, packet, ads_length + 12u);
}
static void query_header(uint8_t *packet, uint8_t event, size_t length) {
    memset(packet, 0, length);
    packet[0] = event;
    packet[1] = (uint8_t)(length - 2u);
    little_endian_store_16(packet, 2, client_handle);
    little_endian_store_16(packet, 4, context_high);
    little_endian_store_16(packet, 6, context_low);
}
static void complete(uint8_t result) {
    uint8_t packet[9];
    query_header(packet, GATT_EVENT_QUERY_COMPLETE, sizeof(packet));
    packet[8] = result;
    gatt_handler(HCI_EVENT_PACKET, 0, packet, sizeof(packet));
}
static void discover(void) {
    uint8_t packet[32];
    assert(query_kind == 1u);
    query_header(packet, GATT_EVENT_SERVICE_QUERY_RESULT, 28u);
    little_endian_store_16(packet, 8, 10u);
    little_endian_store_16(packet, 10, 15u);
    reverse_128(service_uuid, packet + 12);
    gatt_handler(HCI_EVENT_PACKET, 0, packet, 28u);
    complete(ATT_ERROR_SUCCESS);
    assert(query_kind == 2u);
    query_header(packet, GATT_EVENT_CHARACTERISTIC_QUERY_RESULT, sizeof(packet));
    little_endian_store_16(packet, 8, 11u);
    little_endian_store_16(packet, 10, 12u);
    little_endian_store_16(packet, 12, 15u);
    little_endian_store_16(packet, 14, ATT_PROPERTY_READ);
    uint8_t uuid[16];
    memcpy(uuid, service_uuid, sizeof(uuid));
    uuid[3] = 0x2bu;
    reverse_128(uuid, packet + 16);
    gatt_handler(HCI_EVENT_PACKET, 0, packet, sizeof(packet));
    complete(ATT_ERROR_SUCCESS);
    assert(query_kind == 3u);
}
static void fragment(uint16_t offset, const uint8_t *data, uint16_t length) {
    uint8_t packet[14u + PROVISION_WIRE_SIZE];
    assert(length <= PROVISION_WIRE_SIZE);
    query_header(packet, GATT_EVENT_LONG_CHARACTERISTIC_VALUE_QUERY_RESULT,
                 length + 14u);
    little_endian_store_16(packet, 8, 12u);
    little_endian_store_16(packet, 10, offset);
    little_endian_store_16(packet, 12, length);
    memcpy(packet + 14, data, length);
    gatt_handler(HCI_EVENT_PACKET, 0, packet, length + 14u);
}
static void begin_client(void) {
    ble_provision_scan();
    report(address_a, BD_ADDR_TYPE_LE_RANDOM, offered.session_id);
    ble_provision_connect();
    assert(ble_provision_view()->phase == BLE_CONNECTING);
    assert(memcmp(connect_address, address_a, 6u) == 0);
    assert(connect_type == BD_ADDR_TYPE_LE_RANDOM);
    connect_event(true, address_a, BD_ADDR_TYPE_LE_RANDOM, 0u);
    assert(ble_provision_view()->phase == BLE_READING);
}

static void host_tests(void) {
    uint8_t first[PROVISION_WIRE_SIZE], assembled[PROVISION_WIRE_SIZE];
    ble_provision_host(offered.session_id);
    assert(advertising && !scanning && ads_length == 26u);
    assert(ads[11] == PROVISION_VERSION);
    assert(little_endian_read_32(ads, 12) == offered.session_id);
    assert(memcmp(ads + 18, "U26 WIFI", 8u) == 0);
    connect_event(false, address_a, BD_ADDR_TYPE_LE_RANDOM, 0u);
    assert(!advertising && ble_provision_view()->phase == BLE_READING);
    ble_provision_host(offered.session_id + 1u);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_BUSY);
    assert(ble_provision_view()->session_id == offered.session_id);
    assert(server_read(2u, VALUE_HANDLE, 0, NULL, 0) >= ATT_READ_ERROR_CODE_OFFSET);
    assert(server_read(1u, VALUE_HANDLE + 1u, 0, NULL, 0) >= ATT_READ_ERROR_CODE_OFFSET);
    assert(server_read(1u, VALUE_HANDLE, 1u, NULL, 0) ==
           ATT_READ_ERROR_CODE_OFFSET + ATT_ERROR_INVALID_OFFSET);
    assert(offers == 0u);
    assert(provision_encode(&offered, first, sizeof(first)));
    assert(server_read(1u, VALUE_HANDLE, 0, NULL, 0) == PROVISION_WIRE_SIZE);
    assert(offers == 1u);
    ++offered.token;
    for (uint16_t offset = 0; offset < PROVISION_WIRE_SIZE;) {
        assert(server_read(1u, VALUE_HANDLE, offset, NULL, 0) == PROVISION_WIRE_SIZE);
        uint16_t length = server_read(1u, VALUE_HANDLE, offset, assembled + offset, 22u);
        assert(length != 0u);
        offset += length;
    }
    assert(memcmp(first, assembled, sizeof(first)) == 0 && offers == 1u);
    disconnected();
    assert(advertising);
    reject_offer = true;
    connect_event(false, address_a, BD_ADDR_TYPE_LE_RANDOM, 0u);
    unsigned previous_disconnects = disconnects;
    assert(server_read(1u, VALUE_HANDLE, 0, NULL, 0) ==
           ATT_READ_ERROR_CODE_OFFSET + 0x80u);
    assert(offers == 2u && !advertising);
    assert(ble_provision_view()->phase == BLE_ERROR);
    assert(server_read(1u, VALUE_HANDLE, 0, assembled, 22u) ==
           ATT_READ_ERROR_CODE_OFFSET + 0x80u);
    tick(99u);
    assert(disconnects == previous_disconnects);
    tick(1u);
    assert(disconnects == previous_disconnects + 1u);
    disconnected();
    assert(!advertising);
    reject_offer = false;
    ble_provision_host(offered.session_id);
    assert(advertising);
    connect_event(false, address_a, BD_ADDR_TYPE_LE_RANDOM, 0u);
    reentrant_stop = true;
    assert(server_read(1u, VALUE_HANDLE, 0, NULL, 0) >= ATT_READ_ERROR_CODE_OFFSET);
    reentrant_stop = false;
    assert(!advertising && ble_provision_view()->phase == BLE_IDLE);
    disconnected();
}

static void browse_tests(void) {
    ble_provision_scan();
    report(address_a, BD_ADDR_TYPE_LE_RANDOM, offered.session_id);
    report(address_a, BD_ADDR_TYPE_LE_RANDOM, offered.session_id);
    report(address_b, BD_ADDR_TYPE_LE_RANDOM, offered.session_id + 1u);
    assert(ble_provision_view()->host_count == 2u && ble_provision_view()->selected == 0u);
    ble_provision_next();
    assert(ble_provision_view()->session_id == offered.session_id + 1u);
    report(address_b, BD_ADDR_TYPE_LE_RANDOM, offered.session_id + 2u);
    assert(ble_provision_view()->selected == UINT8_MAX);
    ble_provision_connect();
    assert(ble_provision_view()->phase == BLE_SCANNING && scanning);
    ble_provision_next();
    assert(ble_provision_view()->session_id == offered.session_id);
    tick(7999u);
    report(address_b, BD_ADDR_TYPE_LE_RANDOM, offered.session_id + 2u);
    tick(1u);
    assert(ble_provision_view()->host_count == 1u);
    assert(ble_provision_view()->selected == UINT8_MAX);
    ble_provision_next();
    assert(ble_provision_view()->session_id == offered.session_id + 2u);
    tick(8000u);
    assert(ble_provision_view()->host_count == 0u);
    report(address_a, (uint8_t)2u, offered.session_id);
    assert(ble_provision_view()->host_count == 0u);
    ble_provision_scan();
    for (uint8_t i = 0u; i < 6u; ++i) {
        bd_addr_t address = {0xc0, 2, 3, 4, 5, i};
        report(address, BD_ADDR_TYPE_LE_RANDOM, offered.session_id + i);
    }
    assert(ble_provision_view()->host_count == EXP_MAX_HOSTS);
    assert(ble_provision_view()->selected == 0u);
    assert(ble_provision_view()->session_id == offered.session_id);
    for (uint8_t i = 0u; i < EXP_MAX_HOSTS; ++i) ble_provision_next();
    assert(ble_provision_view()->selected == 0u);
    ble_provision_stop();
    assert(!scanning && !advertising);
}

static void client_tests(void) {
    uint8_t wire[PROVISION_WIRE_SIZE];
    assert(provision_encode(&offered, wire, sizeof(wire)));
    begin_client();
    discover();
    uint8_t stale[9];
    query_header(stale, GATT_EVENT_QUERY_COMPLETE, sizeof(stale));
    for (uint16_t offset = 0; offset < sizeof(wire); offset += 22u) {
        uint16_t length = sizeof(wire) - offset;
        if (length > 22u) length = 22u;
        fragment(offset, wire + offset, length);
        assert(received == 0u);
    }
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u && delivered.token == offered.token);
    assert(ble_provision_view()->phase == BLE_COMPLETE);
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u);
    disconnected();

    begin_client();
    unsigned before = query_kind;
    gatt_handler(HCI_EVENT_PACKET, 0, stale, sizeof(stale));
    assert(query_kind == before);
    discover();
    fragment(0, wire, sizeof(wire));
    complete(ATT_ERROR_UNLIKELY_ERROR);
    assert(received == 1u && ble_provision_view()->phase == BLE_ERROR);
    disconnected();

    begin_client();
    discover();
    fragment(0u, wire, 22u);
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u && ble_provision_view()->phase == BLE_ERROR);
    disconnected();

    begin_client();
    discover();
    fragment(22u, wire + 22u, 22u);
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u && ble_provision_view()->phase == BLE_ERROR);
    disconnected();

    begin_client();
    discover();
    wire[4] ^= 1u;
    fragment(0u, wire, sizeof(wire));
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u && ble_provision_view()->phase == BLE_ERROR);
    disconnected();
    wire[4] ^= 1u;

    begin_client();
    tick(15000u);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_TIMEOUT);
    disconnected();

    ble_provision_scan();
    report(address_a, BD_ADDR_TYPE_LE_RANDOM, offered.session_id);
    ble_provision_connect();
    ble_provision_stop();
    assert(cancellations != 0u);
    ble_provision_host(offered.session_id);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_BUSY);
    connect_event(true, address_a, BD_ADDR_TYPE_LE_RANDOM, 0u);
    assert(!advertising && !scanning);
    disconnected();

    ble_provision_scan();
    report(address_a, BD_ADDR_TYPE_LE_RANDOM, offered.session_id);
    ble_provision_connect();
    connect_event(true, address_a, BD_ADDR_TYPE_LE_PUBLIC, 0u);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_PEER);
    disconnected();

    begin_client();
    discover();
    fragment(0u, wire, sizeof(wire));
    ble_provision_stop();
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u && ble_provision_view()->phase == BLE_IDLE);
    disconnected();
    ble_provision_scan();
    fragment(0u, wire, sizeof(wire));
    complete(ATT_ERROR_SUCCESS);
    assert(received == 1u && ble_provision_view()->phase == BLE_SCANNING);

    /* A cancellation's terminal failure releases the busy guard, including
     * when the 15-second operation deadline wraps the 32-bit clock. */
    time_ms = UINT32_MAX - 10000u;
    ble_provision_poll(time_ms);
    ble_provision_scan();
    report(address_a, BD_ADDR_TYPE_LE_RANDOM, offered.session_id);
    ble_provision_connect();
    tick(14999u);
    assert(ble_provision_view()->phase == BLE_CONNECTING);
    tick(1u);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_TIMEOUT);
    connect_event(true, address_a, BD_ADDR_TYPE_LE_RANDOM,
                  ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER);

    begin_client();
    discover();
    fragment(0u, wire, sizeof(wire));
    reentrant_stop = true;
    complete(ATT_ERROR_SUCCESS);
    reentrant_stop = false;
    assert(received == 2u && ble_provision_view()->phase == BLE_IDLE);
    disconnected();
    assert(power_ons == 1u);
}

static void startup_timeout_test(void) {
    tick(14999u);
    assert(ble_provision_view()->phase == BLE_STARTING);
    assert(!ble_provision_view()->stack_ready);
    tick(1u);
    assert(ble_provision_view()->phase == BLE_ERROR);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_TIMEOUT);
    assert(strstr(ble_provision_view()->status, "startup timed out") != NULL);
    uint8_t late_working[] = {BTSTACK_EVENT_STATE, 1, HCI_STATE_WORKING};
    hci_handler(HCI_EVENT_PACKET, 0, late_working, sizeof(late_working));
    assert(!ble_provision_view()->stack_ready);
    assert(ble_provision_view()->phase == BLE_ERROR);
    ble_provision_stop();
    assert(ble_provision_view()->phase == BLE_ERROR);
    assert(ble_provision_view()->error_code == BLE_PROVISION_ERR_TIMEOUT);
    assert(power_ons == 1u && !advertising && !scanning && received == 0u);
    puts("ble_provision_test (startup timeout): PASS");
}

int main(int argc, char **argv) {
    bool startup_timeout = argc > 1 && strcmp(argv[1], "startup-timeout") == 0;
    if (startup_timeout) time_ms = UINT32_MAX - 10000u;
    offered = (handover_credentials_t){
        .session_id = 0x12345678u, .token = 123u, .slot = 1u, .channel = 6u,
        .ssid = "U26W-TEST", .password = "0123456789abcdef0123456789ABCDEF"
    };
    ble_provision_init(offer, receive);
    if (startup_timeout) {
        startup_timeout_test();
        return 0;
    }
    ble_provision_host(offered.session_id);
    assert(!advertising && ble_provision_view()->error_code == BLE_PROVISION_ERR_NOT_READY);
    uint8_t working[] = {BTSTACK_EVENT_STATE, 1, HCI_STATE_WORKING};
    hci_handler(HCI_EVENT_PACKET, 0, working, sizeof(working));
    assert(ble_provision_view()->stack_ready);
    tick(1u);
    host_tests();
    browse_tests();
    client_tests();
    uint8_t stopped[] = {BTSTACK_EVENT_STATE, 1, HCI_STATE_OFF};
    hci_handler(HCI_EVENT_PACKET, 0, stopped, sizeof(stopped));
    assert(!ble_provision_view()->stack_ready);
    assert(ble_provision_view()->phase == BLE_ERROR);
    uint8_t failed[] = {BTSTACK_EVENT_POWERON_FAILED, 0};
    hci_handler(HCI_EVENT_PACKET, 0, failed, sizeof(failed));
    assert(!ble_provision_view()->stack_ready);
    assert(ble_provision_view()->phase == BLE_ERROR);
    puts("ble_provision_test: PASS");
    return 0;
}
