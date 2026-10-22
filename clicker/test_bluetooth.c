#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bluetooth.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"

static uint8_t ram[0x80000], patch_ram[0x300000];
static uint32_t ticks, writes, base, control, host, fail_address;
static uint8_t automatic, silence, wrong_opcode, command_status, cancelled;
static clicker_bt_t bt;
static const uint8_t patch[] = {
    2u, 'v', 0u, 3u, 2u, 0u, 0u, 4u, 0u, 0x0du,
    5u, 0u, 1u, 0u, 1u, 2u, 3u, 4u, 5u, 0u, 0u, 0u, 1u
};

static void ring_store(uint32_t address, uint32_t index, const uint8_t *data, uint32_t length) {
    for (uint32_t i = 0u; i < length; ++i) ram[address + ((index + i) & 4095u)] = data[i];
}

static void event(uint8_t type, const uint8_t *packet, uint32_t length) {
    uint8_t frame[520] = {0};
    badger_bytes_store_u32(frame, length);
    frame[3] = type;
    memcpy(frame + 4u, packet, length);
    uint32_t total = (length + 7u) & ~3u;
    uint32_t index = badger_bytes_load_u32(ram + base + 0x2008u);
    ring_store(base + 0x1000u, index, frame, total);
    badger_bytes_store_u32(ram + base + 0x2008u, (index + total) & 4095u);
}

static void process(void) {
    uint32_t in = badger_bytes_load_u32(ram + base + 0x2000u);
    uint32_t out = badger_bytes_load_u32(ram + base + 0x2004u);
    if (in == out) return;
    uint8_t frame[520];
    uint32_t length = (in - out) & 4095u;
    assert(length <= sizeof(frame));
    for (uint32_t i = 0u; i < length; ++i) frame[i] = ram[base + ((out + i) & 4095u)];
    assert(frame[3] == 1u);
    uint8_t reply[] = {0x0eu, 4u, 1u, frame[4], frame[5], command_status};
    if (wrong_opcode) reply[3] ^= 1u;
    badger_bytes_store_u32(ram + base + 0x2004u, in);
    if (!silence) event(4u, reply, sizeof(reply));
}

static int read32(void *context, uint32_t address, uint32_t *value) {
    (void)context;
    if (address == fail_address) return 0;
    if (address == 0x18000c7cu) *value = control;
    else if (address == 0x18000d68u) *value = base;
    else {
        assert(address + 4u <= sizeof(ram));
        *value = badger_bytes_load_u32(ram + address);
    }
    return 1;
}

static int write32(void *context, uint32_t address, uint32_t value) {
    (void)context;
    if (address == fail_address) return 0;
    ++writes;
    if (address == 0x19640894u) assert(value == 3u);
    else if (address == 0x18000d6cu) {
        uint32_t previous = host;
        host = value;
        if (automatic && ((value ^ previous) & 2u) != 0u && (value & (1u << 24u))) process();
    } else {
        assert(address + 4u <= sizeof(ram));
        badger_bytes_store_u32(ram + address, value);
    }
    return 1;
}

static uint8_t *memory_at(uint32_t address, uint32_t length) {
    assert((address & 3u) == 0u && (length & 3u) == 0u && length != 0u);
    if (address >= 0x19000000u && address - 0x19000000u + length <= sizeof(patch_ram)) {
        return patch_ram + address - 0x19000000u;
    }
    assert(address + length <= sizeof(ram));
    return ram + address;
}

static int read_memory(void *context, uint32_t address, uint8_t *data, uint32_t length) {
    (void)context;
    if (address == fail_address) return 0;
    memcpy(data, memory_at(address, length), length);
    return 1;
}

static int write_memory(void *context, uint32_t address, const uint8_t *data, uint32_t length) {
    (void)context;
    if (address == fail_address) return 0;
    ++writes;
    memcpy(memory_at(address, length), data, length);
    return 1;
}

static uint32_t clock_tick(void *context) { (void)context; ticks += 1000u; return ticks; }
static int service(void *context) { (void)context; return !cancelled; }
static const clicker_bt_io_t io = {0, read32, write32, read_memory, write_memory, clock_tick, service};

static void initialize(void) {
    memset(ram, 0, sizeof(ram));
    memset(patch_ram, 0xa5, sizeof(patch_ram));
    ticks = writes = host = 0u;
    base = 0x60000u;
    control = (1u << 24u) | (1u << 8u);
    fail_address = 0xffffffffu;
    automatic = silence = wrong_opcode = command_status = cancelled = 0u;
}

static void start(void) {
    initialize();
    assert(clicker_bt_start(&bt, &io, patch, sizeof(patch), diag_crc32(patch, sizeof(patch))));
    assert(bt.stage == CLICKER_BT_HCI && bt.credits == 1u && bt.patch_bytes == 5u);
    assert(memcmp(patch_ram + 0xd0001u, patch + 14u, 5u) == 0);
    assert(patch_ram[0xd0000u] == 0xa5u && patch_ram[0xd0006u] == 0xa5u);
}

int main(void) {
    start();
    uint8_t output[512], type;
    uint32_t length;
    assert(clicker_bt_receive(&bt, &type, output, sizeof(output), &length) == 0 && length == 0u);
    const uint8_t reset[] = {3u, 0x0cu, 0u};
    assert(clicker_bt_send(&bt, 1u, reset, sizeof(reset)) == 1);
    assert(bt.tx_packets == 1u && ram[base] == 3u && ram[base + 3u] == 1u);
    assert(memcmp(ram + base + 4u, reset, sizeof(reset)) == 0 && ram[base + 7u] == 0u);
    badger_bytes_store_u32(ram + base + 0x2004u, 12u);
    assert(clicker_bt_send(&bt, 1u, reset, sizeof(reset)) == 0 && bt.error == CLICKER_BT_OK);

    start();
    badger_bytes_store_u32(ram + base + 0x2000u, 4092u);
    badger_bytes_store_u32(ram + base + 0x2004u, 4092u);
    assert(clicker_bt_send(&bt, 1u, reset, sizeof(reset)) == 1);
    assert(ram[base + 4092u] == 3u && ram[base + 4095u] == 1u);
    assert(memcmp(ram + base, reset, sizeof(reset)) == 0);
    assert(badger_bytes_load_u32(ram + base + 0x2000u) == 4u);
    const uint8_t completed[] = {0x0eu, 4u, 1u, 3u, 0x0cu, 0u};
    badger_bytes_store_u32(ram + base + 0x2008u, 4092u);
    badger_bytes_store_u32(ram + base + 0x200cu, 4092u);
    event(4u, completed, sizeof(completed));
    assert(clicker_bt_receive(&bt, &type, output, sizeof(output), &length) == 1);
    assert(type == 4u && length == sizeof(completed) && memcmp(output, completed, length) == 0);
    assert(bt.rx_packets == 1u && badger_bytes_load_u32(ram + base + 0x200cu) == 8u);

    start();
    uint8_t acl[CLICKER_BT_PACKET_MAX];
    for (uint32_t i = 0u; i < sizeof(acl); ++i) acl[i] = (uint8_t)i;
    acl[0] = 1u; acl[1] = 0x20u;
    acl[2] = 0xfcu; acl[3] = 1u;
    assert(clicker_bt_send(&bt, 2u, acl, sizeof(acl)) == 1);
    assert(badger_bytes_load_u32(ram + base + 0x2000u) == 516u);
    event(2u, acl, sizeof(acl));
    assert(clicker_bt_receive(&bt, &type, output, sizeof(output), &length) == 1);
    assert(type == 2u && length == sizeof(acl) && memcmp(output, acl, length) == 0);

    start();
    event(4u, completed, sizeof(completed));
    assert(clicker_bt_receive(&bt, &type, output, sizeof(completed) - 1u, &length) == -1);
    assert(bt.error == CLICKER_BT_PACKET && badger_bytes_load_u32(ram + base + 0x200cu) == 0u);

    start();
    event(4u, completed, sizeof(completed));
    badger_bytes_store_u32(ram + base + 0x2008u, 4u);
    assert(clicker_bt_receive(&bt, &type, output, sizeof(output), &length) == 0);
    assert(badger_bytes_load_u32(ram + base + 0x200cu) == 0u);
    badger_bytes_store_u32(ram + base + 0x2008u, 12u);
    assert(clicker_bt_receive(&bt, &type, output, sizeof(output), &length) == 1);

    start();
    automatic = 1u;
    assert(clicker_bt_command(&bt, 0x0c03u, 0, 0u, output, sizeof(output), &length));
    assert(length == 1u && output[0] == 0u && bt.credits == 1u);
    ticks = 0xfffff000u;
    assert(clicker_bt_command(&bt, 0x0c03u, 0, 0u, output, sizeof(output), &length));
    assert(bt.tx_packets == 2u && bt.rx_packets == 2u);

    for (uint32_t fault = 0u; fault < 4u; ++fault) {
        start();
        automatic = 1u;
        if (fault == 0u) command_status = 0x0cu;
        if (fault == 1u) wrong_opcode = 1u;
        if (fault == 2u) silence = 1u;
        if (fault == 3u) fail_address = base;
        assert(!clicker_bt_command(&bt, 0x0c03u, 0, 0u, output, sizeof(output), &length));
        const clicker_bt_error_t expected[] = {
            CLICKER_BT_COMMAND, CLICKER_BT_COMMAND, CLICKER_BT_TIMEOUT, CLICKER_BT_IO
        };
        assert(bt.error == expected[fault] && length == 0u);
    }

    for (uint32_t fault = 0u; fault < 5u; ++fault) {
        start();
        if (fault == 0u) badger_bytes_store_u32(ram + base + 0x2008u, 3u);
        if (fault == 1u) badger_bytes_store_u32(ram + base + 0x2004u, 4096u);
        if (fault >= 2u) {
            event(4u, completed, sizeof(completed));
            if (fault == 2u) ram[base + 0x1002u] = 1u;
            if (fault == 3u) ram[base + 0x1005u] = 0xffu;
            if (fault == 4u) ram[base + 0x1003u] = 7u;
        }
        assert(clicker_bt_receive(&bt, &type, output, sizeof(output), &length) == -1);
        assert(bt.error == (fault < 2u ? CLICKER_BT_POINTER : CLICKER_BT_PACKET));
        assert(badger_bytes_load_u32(ram + base + 0x200cu) == 0u);
    }

    initialize();
    assert(!clicker_bt_start(&bt, &io, patch, sizeof(patch), diag_crc32(patch, sizeof(patch)) ^ 1u));
    assert(bt.error == CLICKER_BT_PATCH && writes == 0u);
    for (uint32_t size = 0u; size < sizeof(patch); ++size) {
        initialize();
        assert(!clicker_bt_start(&bt, &io, patch, size, diag_crc32(patch, size)));
        assert(bt.error == CLICKER_BT_PATCH && writes == 0u);
    }
    initialize();
    base = 0xfffffffcu;
    assert(!clicker_bt_start(&bt, &io, patch, sizeof(patch), diag_crc32(patch, sizeof(patch))));
    assert(bt.error == CLICKER_BT_POINTER);
    initialize();
    control = 0u;
    assert(!clicker_bt_start(&bt, &io, patch, sizeof(patch), diag_crc32(patch, sizeof(patch))));
    assert(bt.error == CLICKER_BT_TIMEOUT && bt.stage == CLICKER_BT_READY);
    initialize();
    control = 1u << 24u;
    assert(!clicker_bt_start(&bt, &io, patch, sizeof(patch), diag_crc32(patch, sizeof(patch))));
    assert(bt.error == CLICKER_BT_TIMEOUT && bt.stage == CLICKER_BT_AWAKE);
    initialize();
    cancelled = 1u;
    assert(!clicker_bt_start(&bt, &io, patch, sizeof(patch), diag_crc32(patch, sizeof(patch))));
    assert(bt.error == CLICKER_BT_CANCELLED && writes == 0u);
    puts("PASS: BT patch bounds/RMW/CRC, startup, aligned rings/wrap/backpressure,");
    puts("      partial packets, command completions/status, timeouts/wrap and I/O failures");
    return 0;
}
