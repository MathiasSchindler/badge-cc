#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keypad_host.h"
#include "../firmware/trace.h"

static const uint8_t hid[] = {
    0x06,0x43,0xff,0x0a,0x02,0x1a,0xa1,0x01,0x85,0x11,0x95,0x13,0x75,0x08,
    0x15,0x00,0x26,0xff,0x00,0x09,0x02,0x81,0x00,0x09,0x02,0x91,0x00,0xc0,
    0x06,0x43,0xff,0x0a,0x08,0x1a,0xa1,0x01,0x85,0x13,0x96,0x1f,0x00,0x75,
    0x08,0x15,0x00,0x26,0xff,0x00,0x09,0x08,0x82,0x02,0x01,0x09,0x08,0x92,
    0x02,0x01,0xc0,0x06,0x43,0xff,0x0a,0x10,0x1a,0xa1,0x01,0x85,0x14,0x96,
    0xfe,0x0f,0x75,0x08,0x15,0x00,0x26,0xff,0x00,0x09,0x10,0x92,0x02,0x01,
    0xc0,0x05,0x01,0x09,0x80,0xa1,0x01,0x85,0x04,0x95,0x01,0x75,0x02,0x15,
    0x01,0x25,0x03,0x09,0x82,0x09,0x81,0x09,0x83,0x81,0x00,0x75,0x06,0x81,
    0x03,0xc0,0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x05,0x95,0x01,0x75,0x01,
    0x15,0x00,0x25,0x01,0x0a,0x24,0x02,0x81,0x02,0x95,0x07,0x81,0x03,0xc0
};
static const uint8_t configuration[] = {
    9,2,41,0,1,1,4,0x80,180, 9,4,0,0,2,3,0,0,0,
    9,0x21,0x11,1,0,1,0x22,140,0, 7,5,0x81,3,32,0,5, 7,5,1,3,64,0,5
};
typedef struct {
    uint32_t registers[0x120u / 4u], dpram[4096u / 4u], time, service_calls, cancel_at;
    uint32_t sie_written_at, buffer_written_at, fault, setups, outputs;
    uint8_t device[18], config[41], setup[8], phase, data_pid, address, configured;
    uint8_t input_pid, output_pid, diversion[2], drop_set_reply, lie_readback, stop_stuck;
    uint8_t queue[128][32], lengths[128], head, count;
    uint16_t data_at, data_size, force_short;
    const uint8_t *data;
    keypad_trace_t trace;
} mock_t;
static mock_t mock;
static usb_host_t usb;
static keypad_host_t keypad;

static void enqueue(const uint8_t *raw, uint8_t length) {
    assert(mock.count < 128u && length <= 32u);
    uint8_t slot = (uint8_t)((mock.head + mock.count) % 128u);
    memcpy(mock.queue[slot], raw, length);
    mock.lengths[slot] = length;
    ++mock.count;
}

static void fill_input(const uint8_t *raw, uint16_t length, uint8_t pid) {
    memcpy((uint8_t *)mock.dpram + HOST_EPX_DATA, raw, length);
    mock.dpram[HOST_EPX_BUFFER / 4u] = HOST_FULL | HOST_LAST | (pid ? HOST_PID : 0u) | length;
    mock.registers[HOST_SIE_STATUS / 4u] |= HOST_COMPLETE;
}

static void transaction(uint32_t command) {
    uint32_t address_endpoint = mock.registers[0];
    assert((address_endpoint & 127u) == mock.address);
    uint8_t endpoint = (uint8_t)(address_endpoint >> 16u);
    uint32_t buffer = mock.dpram[HOST_EPX_BUFFER / 4u];
    uint16_t length = (uint16_t)(buffer & 0x3ffu);
    uint8_t pid = (buffer & HOST_PID) != 0u;
    assert(length <= 64u);
    mock.registers[HOST_SIE_STATUS / 4u] = 2u << 8u;
    if (mock.fault) {
        uint32_t fault = mock.fault;
        mock.fault = 0u;
        if (fault == 1u) mock.registers[HOST_SIE_STATUS / 4u] |= 1u << 24u;
        if (fault == 2u) mock.registers[HOST_SIE_STATUS / 4u] |= 1u << 29u;
        if (fault == 3u) return;
        if (fault == 4u) {
            mock.registers[HOST_SIE_STATUS / 4u] |= (1u << 28u) | HOST_COMPLETE;
            mock.registers[HOST_NAK_POLL / 4u] |= HOST_STOPPED_NAK;
            mock.dpram[HOST_EPX_BUFFER / 4u] = HOST_FULL | HOST_LAST | length;
        }
        if (fault == 5u) mock.registers[HOST_SIE_STATUS / 4u] = 0u;
        if (fault == 6u) {
            mock.registers[HOST_SIE_STATUS / 4u] |= HOST_COMPLETE;
            mock.dpram[HOST_EPX_BUFFER / 4u] = HOST_FULL | HOST_LAST | 65u;
        }
        return;
    }
    if (command & 2u) {
        assert(endpoint == 0u);
        memcpy(mock.setup, mock.dpram, 8u);
        ++mock.setups;
        mock.data_at = 0u;
        mock.data_pid = 1u;
        mock.data_size = (uint16_t)((uint16_t)mock.setup[6] | (uint16_t)mock.setup[7] << 8u);
        mock.phase = mock.data_size ? 1u : 2u;
        if (mock.setup[1] == 6u) {
            uint16_t available;
            switch (mock.setup[3]) {
            case 1u: mock.data = mock.device; available = sizeof(mock.device); break;
            case 2u: mock.data = mock.config; available = sizeof(mock.config); break;
            case 0x22u: mock.data = hid; available = sizeof(hid); break;
            default: assert(0);
            }
            if (mock.data_size > available) mock.data_size = available;
        } else assert(mock.setup[1] == 5u || mock.setup[1] == 9u);
        mock.registers[HOST_SIE_STATUS / 4u] |= HOST_COMPLETE | (1u << 30u);
        return;
    }
    assert(buffer & HOST_AVAILABLE);
    if (endpoint == 0u) {
        if (mock.phase == 1u) {
            assert((command & 8u) && pid == mock.data_pid);
            uint16_t size = (uint16_t)(mock.data_size - mock.data_at);
            if (size > length) size = length;
            if (mock.force_short && size > mock.force_short) size = mock.force_short;
            fill_input(mock.data + mock.data_at, size, pid);
            mock.data_at = (uint16_t)(mock.data_at + size);
            mock.data_pid ^= 1u;
            if (mock.data_at == mock.data_size || size < length) mock.phase = 2u;
            mock.force_short = 0u;
            return;
        }
        assert(mock.phase == 2u && pid == 1u && length == 0u);
        if (mock.setup[0] & 0x80u) {
            assert(command & 4u);
            mock.dpram[HOST_EPX_BUFFER / 4u] = HOST_LAST | HOST_PID;
            mock.registers[HOST_SIE_STATUS / 4u] |= HOST_COMPLETE | (1u << 30u);
        } else {
            assert(command & 8u);
            fill_input(mock.setup, 0u, pid);
            if (mock.setup[1] == 5u) mock.address = mock.setup[2];
            if (mock.setup[1] == 9u) mock.configured = mock.setup[2];
        }
        mock.phase = 0u;
        return;
    }
    assert(endpoint == 1u && mock.configured);
    if (command & 8u) {
        assert(pid == mock.input_pid);
        if (!mock.count) {
            mock.registers[HOST_SIE_STATUS / 4u] |= 1u << 28u;
            mock.registers[HOST_NAK_POLL / 4u] |= HOST_STOPPED_NAK;
            return;
        }
        assert(length >= mock.lengths[mock.head]);
        fill_input(mock.queue[mock.head], mock.lengths[mock.head], pid);
        mock.head = (uint8_t)((mock.head + 1u) % 128u);
        --mock.count;
        mock.input_pid ^= 1u;
    } else {
        assert((command & 4u) && pid == mock.output_pid && length == 20u && (buffer & HOST_FULL));
        uint8_t reply[20];
        memcpy(reply, (uint8_t *)mock.dpram + HOST_EPX_DATA, 20u);
        assert(reply[0] == 0x11u && reply[1] == 0xffu);
        ++mock.outputs;
        mock.output_pid ^= 1u;
        if (reply[2] == 0u) {
            assert(reply[4] == 0x1bu && reply[5] == 4u);
            reply[4] = 0x0bu;
        } else {
            assert(reply[2] == 0x0bu && reply[4] == 1u && (reply[5] == 0xa1u || reply[5] == 0xa2u));
            uint8_t control_id = (uint8_t)(reply[5] - 0xa1u);
            if ((reply[3] >> 4u) == 2u) {
                reply[6] = mock.diversion[control_id];
                if (mock.lie_readback && mock.outputs >= 4u) reply[6] ^= 1u;
            } else {
                assert((reply[3] >> 4u) == 3u && (reply[6] == 2u || reply[6] == 3u));
                mock.diversion[control_id] = reply[6] & 1u;
            }
        }
        if (mock.drop_set_reply && (reply[3] >> 4u) == 3u) mock.drop_set_reply = 0u;
        else enqueue(reply, sizeof(reply));
        mock.dpram[HOST_EPX_BUFFER / 4u] = HOST_LAST | (pid ? HOST_PID : 0u) | length;
        mock.registers[HOST_SIE_STATUS / 4u] |= HOST_COMPLETE | (1u << 30u);
    }
}

static uint32_t mock_read(void *context, uint32_t address) {
    (void)context;
    if (address >= HOST_USB_BASE && address < HOST_USB_BASE + sizeof(mock.registers))
        return mock.registers[(address - HOST_USB_BASE) / 4u];
    assert(address >= HOST_DPRAM_BASE && address < HOST_DPRAM_BASE + sizeof(mock.dpram));
    return mock.dpram[(address - HOST_DPRAM_BASE) / 4u];
}

static void mock_write(void *context, uint32_t address, uint32_t value) {
    (void)context;
    if (address >= HOST_DPRAM_BASE && address < HOST_DPRAM_BASE + sizeof(mock.dpram)) {
        uint32_t index = (address - HOST_DPRAM_BASE) / 4u;
        if (index == HOST_EPX_BUFFER / 4u) {
            if (value & HOST_AVAILABLE) assert(mock.time - mock.buffer_written_at >= 1u);
            else mock.buffer_written_at = mock.time;
        }
        mock.dpram[index] = value;
        return;
    }
    assert(address >= HOST_USB_BASE && address < HOST_USB_BASE + sizeof(mock.registers));
    uint32_t offset = address - HOST_USB_BASE;
    if (offset == HOST_SIE_STATUS) { mock.registers[offset / 4u] &= ~value; return; }
    if (offset == HOST_NAK_POLL) {
        uint32_t stopped = mock.registers[offset / 4u] & HOST_STOPPED_NAK;
        mock.registers[offset / 4u] = (value & ~HOST_STOPPED_NAK) |
            ((value & HOST_STOPPED_NAK) ? 0u : stopped);
        return;
    }
    if (offset == HOST_SIE_CTRL) {
        assert(!(value & ((1u << 11u) | (1u << 16u))));
        if (value & HOST_START) {
            assert(mock.time - mock.sie_written_at >= 1u);
            mock.registers[offset / 4u] = value & ~HOST_START;
            transaction(value);
            return;
        }
        if (value & HOST_RESET) {
            mock.address = mock.configured = mock.input_pid = mock.output_pid = 0u;
            mock.time += 12000u;
        }
        mock.sie_written_at = mock.time;
        if (!(value & HOST_STOP) || !mock.stop_stuck) value &= ~(HOST_RESET | HOST_STOP);
    }
    if (offset == 0x78u) assert(value == 2u);
    mock.registers[offset / 4u] = value;
}

static uint32_t mock_now(void *context) { (void)context; mock.time += 20u; return mock.time; }
static int mock_service(void *context) {
    (void)context;
    ++mock.service_calls;
    return !mock.cancel_at || mock.service_calls < mock.cancel_at;
}

static int mock_report(void *context, const uint8_t *raw, uint16_t length, uint8_t feature) {
    (void)context;
    uint8_t wire[64];
    if (!keypad_trace_record(wire, raw, length, feature, mock.time / 1000u, mock.trace.total + 1u)) return 0;
    wire[7] = 2u;
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
    return keypad_trace_accept(&mock.trace, wire);
}

static void init(uint8_t ep0_size) {
    memset(&mock, 0, sizeof(mock));
    const uint8_t descriptor[] = {18,1,0,2,0,0,0,ep0_size,0x6d,4,0x54,0xc3,0,2,1,2,3,1};
    memcpy(mock.device, descriptor, sizeof(descriptor));
    memcpy(mock.config, configuration, sizeof(configuration));
    mock.registers[HOST_SIE_STATUS / 4u] = 2u << 8u;
    host_io_t io = {.read = mock_read, .write = mock_write, .now = mock_now, .service = mock_service};
    usb_host_init(&usb, &io);
    usb_host_start(&usb);
    keypad_host_t fresh = {.usb = &usb, .report = mock_report};
    keypad = fresh;
    assert(mock.registers[0x40u / 4u] == 3u && usb.sie == HOST_PULLDOWN);
}

static void test_configuration(void) {
    keypad_endpoints_t endpoints;
    assert(sizeof(hid) == 140u && keypad_host_known_hid(hid, sizeof(hid)));
    uint8_t changed_hid[140]; memcpy(changed_hid, hid, sizeof(hid)); changed_hid[0] ^= 1u;
    assert(!keypad_host_known_hid(changed_hid, sizeof(changed_hid)));
    assert(!keypad_host_known_hid(hid, 139u));
    assert(keypad_host_configuration(configuration, sizeof(configuration), &endpoints) == HOST_OK);
    assert(endpoints.input == 0x81u && endpoints.output == 1u &&
           endpoints.input_size == 32u && endpoints.output_size == 64u &&
           endpoints.input_interval == 5u && endpoints.report_size == 140u);
    for (unsigned length = 0; length < sizeof(configuration); ++length)
        assert(keypad_host_configuration(configuration, (uint16_t)length, &endpoints) == HOST_DESCRIPTOR);
    const unsigned offsets[] = {0,1,2,4,5,9,12,13,14,15,16,18,23,24,25,27,29,30,31,33,34,36,37,38,40};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
        uint8_t invalid[41]; memcpy(invalid, configuration, sizeof(invalid));
        invalid[offsets[i]] = 0u;
        if (offsets[i] == 12u || offsets[i] == 15u || offsets[i] == 16u) invalid[offsets[i]] = 1u;
        if (offsets[i] == 29u) invalid[29] = 0x80u;
        if (offsets[i] == 36u) invalid[36] = 0x81u;
        assert(keypad_host_configuration(invalid, sizeof(invalid), &endpoints) == HOST_DESCRIPTOR);
    }
    uint8_t high_speed[41]; memcpy(high_speed, configuration, sizeof(high_speed));
    high_speed[38] = 0u; high_speed[39] = 4u;
    assert(keypad_host_configuration(high_speed, sizeof(high_speed), &endpoints) == HOST_DESCRIPTOR);
}

static void test_packets(void) {
    init(64u);
    assert(usb_host_reset(&usb) == HOST_OK);
    uint8_t data[64] = {0};
    uint16_t got;
    assert(usb_host_packet(&usb, HOST_IN, 16u, 1u, 0u, data, 32u, &got) == HOST_ARGUMENT);
    assert(usb_host_packet(&usb, HOST_IN, 1u, 1u, 0u, data, 65u, &got) == HOST_ARGUMENT);
    const host_result_t results[] = {HOST_BUS_ERROR, HOST_STALL, HOST_TIMEOUT, HOST_NAK,
                                     HOST_DETACHED, HOST_BUFFER_ERROR};
    for (uint32_t fault = 1u; fault <= 6u; ++fault) {
        mock.registers[HOST_SIE_STATUS / 4u] = 2u << 8u;
        mock.fault = fault;
        assert(usb_host_packet(&usb, HOST_IN, 1u, 1u, 0u, data, 32u, &got) == results[fault - 1u]);
        assert(got == 0u);
    }
    mock.registers[HOST_SIE_STATUS / 4u] = 2u << 8u;
    mock.cancel_at = mock.service_calls + 3u;
    mock.fault = 3u;
    assert(usb_host_packet(&usb, HOST_IN, 1u, 1u, 0u, data, 32u, &got) == HOST_CANCELLED);
    mock.cancel_at = 0u;
    mock.stop_stuck = 1u;
    mock.fault = 3u;
    assert(usb_host_packet(&usb, HOST_IN, 1u, 1u, 0u, data, 32u, &got) == HOST_STOP_TIMEOUT);
    assert(usb.stop_failed);
    mock.stop_stuck = 0u;
    assert(usb_host_packet(&usb, HOST_IN, 1u, 1u, 0u, data, 32u, &got) == HOST_STOP_TIMEOUT);
    init(64u);
    assert(usb_host_reset(&usb) == HOST_OK);
    const uint8_t setup[] = {0x80,6,0,1,0,0,8,0};
    mock.force_short = 4u;
    assert(usb_host_control(&usb, setup, data, sizeof(data), &got) == HOST_OK && got == 4u);
}

static void test_keypad(void) {
    for (uint8_t size = 8u; size <= 64u; size = (uint8_t)(size * 2u)) {
        init(size);
        assert(keypad_host_enumerate(&keypad) == HOST_OK);
        assert(keypad.ready && usb.address == 1u && mock.address == 1u && mock.configured == 1u);
        assert(usb.ep0_size == size && keypad.report_crc == diag_crc32(hid, sizeof(hid)));
    }
    for (uint8_t key = 1u; key <= 9u; ++key) {
        uint8_t raw[32] = {0x13,0xff,2,0,0,1,key};
        enqueue(raw, sizeof(raw)); raw[6] = 0u; enqueue(raw, sizeof(raw));
    }
    for (unsigned i = 0; i < 18u; ++i) assert(keypad_host_poll(&keypad) == HOST_OK);
    assert(mock.trace.total == 18u && !mock.trace.unknown && !keypad.input_pid);
    uint8_t pid = keypad.input_pid;
    assert(keypad_host_poll(&keypad) == HOST_NAK && keypad.input_pid == pid);
    assert(keypad_host_navigation(&keypad) == HOST_OK);
    assert(keypad.feature == 0x0bu && keypad.changed == 3u && mock.diversion[0] && mock.diversion[1]);
    for (unsigned i = 0; i < 4u; ++i) {
        uint8_t nav[20] = {0x11,0xff,0x0b,0,1,(i & 1u) ? 0xa2u : 0xa1u};
        enqueue(nav, sizeof(nav)); nav[4] = nav[5] = 0u; enqueue(nav, sizeof(nav));
    }
    for (unsigned i = 0; i < 8u; ++i) assert(keypad_host_poll(&keypad) == HOST_OK);
    assert(keypad_host_restore(&keypad) == HOST_OK);
    assert(!keypad.changed && !mock.diversion[0] && !mock.diversion[1]);
    assert(mock.trace.total == 37u && mock.trace.unknown == 11u);
    assert(keypad_trace_selected(&mock.trace)->wire[7] == 2u);
    init(64u); mock.device[8] ^= 1u;
    assert(keypad_host_enumerate(&keypad) == HOST_WRONG_DEVICE && !mock.address && !mock.configured);
    init(64u); mock.device[4] = 9u;
    assert(keypad_host_enumerate(&keypad) == HOST_HUB && !mock.address && !mock.configured);
    init(64u); mock.config[38] = 0u; mock.config[39] = 4u;
    assert(keypad_host_enumerate(&keypad) == HOST_DESCRIPTOR && !mock.configured);
    init(64u); mock.registers[HOST_SIE_STATUS / 4u] = 1u << 8u;
    assert(keypad_host_enumerate(&keypad) == HOST_SPEED_ERROR);
}

static void test_navigation_failure(void) {
    init(64u);
    assert(keypad_host_enumerate(&keypad) == HOST_OK);
    mock.diversion[0] = 1u;
    assert(keypad_host_navigation(&keypad) == HOST_OK && keypad.changed == 2u);
    assert(keypad_host_restore(&keypad) == HOST_OK && mock.diversion[0] && !mock.diversion[1]);
    init(64u);
    assert(keypad_host_enumerate(&keypad) == HOST_OK);
    mock.drop_set_reply = 1u;
    assert(keypad_host_navigation(&keypad) == HOST_REPLY_TIMEOUT);
    assert(keypad.changed == 1u && mock.diversion[0]);
    assert(keypad_host_restore(&keypad) == HOST_OK && !mock.diversion[0] && !keypad.changed);
    init(64u);
    assert(keypad_host_enumerate(&keypad) == HOST_OK);
    mock.lie_readback = 1u;
    assert(keypad_host_navigation(&keypad) == HOST_HID_REPLY && keypad.changed == 1u);
    assert(keypad_host_restore(&keypad) == HOST_HID_REPLY && keypad.changed == 1u);
    mock.lie_readback = 0u;
    assert(keypad_host_restore(&keypad) == HOST_OK && !keypad.changed);
}

int main(void) {
    test_configuration();
    test_packets();
    test_keypad();
    test_navigation_failure();
    puts("Native host: register ordering/no VBUS drive, EP0 8/16/32/64, descriptors/address/config,");
    puts("NAK/stall/CRC/timeout/cancel/detach, raw keys, 37-report trace and navigation restore PASS");
    return 0;
}
