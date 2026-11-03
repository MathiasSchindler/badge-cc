#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "../src/shared/bytes.h"
#include "../src/tools/diagnostics_logic.h"
#include "../src/usb/live_usb.h"

typedef struct {
    uint32_t registers[0x100u / 4u], program[4], timer, fifo, fifo_error, line;
    uint32_t captures, cancel_reset, stall_pio, available;
    uint32_t configured_pads, configured_mux, execctrl, shiftctrl, pinctrl, clkdiv;
    uint32_t x, isr;
    uint32_t pattern, patterned, sample_index;
    uint8_t pc;
} mock_t;
static mock_t mock;
static logi_board_t board;
static live_usb_control_t usb;

static void execute(void) {
    assert(mock.pinctrl == LOGI_DP << 15u);
    assert(mock.shiftctrl == 1u << 18u);
    assert(mock.clkdiv == 1u << 16u);
    assert(mock.configured_pads == 3u && mock.configured_mux == 3u);
    if (mock.stall_pio) return;
    unsigned instructions = 0u;
    for (;;) {
        assert(mock.pc < 4u && ++instructions < 100u);
        uint32_t instruction = mock.program[mock.pc];
        if (instruction == 0x4002u) {
            uint32_t line = mock.line;
            if (mock.patterned) {
                assert(mock.sample_index < LOGI_SAMPLE_COUNT);
                line = mock.pattern >> (2u * mock.sample_index++) & 3u;
            }
            mock.isr = mock.isr >> 2u | line << 30u;
            ++mock.pc;
        }
        else if (instruction == 0x0040u) {
            uint32_t value = mock.x--;
            mock.pc = value ? 0u : (uint8_t)(mock.pc + 1u);
        } else if (instruction == 0x8020u) {
            assert(!mock.available);
            mock.available = 1u;
            mock.fifo = mock.isr;
            ++mock.captures;
            ++mock.pc;
        } else if (instruction == 0x0003u) return;
        else assert(0);
    }
}

static uint32_t read_mock(void *context, uint32_t address) {
    (void)context;
    if (address == 0x40020008u) return mock.cancel_reset ? 0u : 1u << 11u;
    if (address == LOGI_PIO + 4u) return mock.available ? 0u : 1u << 8u;
    if (address == LOGI_PIO + 8u) return mock.fifo_error;
    if (address == LOGI_PIO + 0x20u) {
        assert(mock.available);
        mock.available = 0u;
        return mock.fifo;
    }
    if (address == 0x40028000u + LOGI_DP * 8u) return (mock.line & 1u) << 17u;
    if (address == 0x40028000u + LOGI_DM * 8u) return (mock.line >> 1u) << 17u;
    assert(0);
    return 0u;
}

static void write_mock(void *context, uint32_t address, uint32_t value) {
    (void)context;
    if (address == 0x40022000u || address == 0x40023000u) { assert(value == 1u << 11u); return; }
    for (uint32_t pin = LOGI_DP; pin <= LOGI_DM; ++pin) {
        if (address == 0x40038004u + pin * 4u) {
            assert(value == 0xc6u);
            mock.configured_pads |= 1u << (pin - LOGI_DP);
            return;
        }
        if (address == 0x40028004u + pin * 8u) {
            assert(value == 6u);
            mock.configured_mux |= 1u << (pin - LOGI_DP);
            return;
        }
    }
    if (address >= LOGI_PIO + 0x48u && address < LOGI_PIO + 0x58u) {
        mock.program[(address - LOGI_PIO - 0x48u) / 4u] = value;
        return;
    }
    if (address == LOGI_PIO + 0xc8u) { mock.clkdiv = value; return; }
    if (address == LOGI_PIO + 0xccu) { mock.execctrl = value; return; }
    if (address == LOGI_PIO + 0xd0u) { mock.shiftctrl = value; return; }
    if (address == LOGI_PIO + 0xdcu) { mock.pinctrl = value; return; }
    if (address == LOGI_PIO + 0xd8u) {
        if (value == 0xe080u) return;
        if (value == 0xe02fu) { mock.x = 15u; return; }
        assert(value == 0u);
        mock.pc = 0u;
        return;
    }
    assert(address == LOGI_PIO);
    assert(value == 0u || value == 1u || value == 0x110u);
    if (value == 0x110u) mock.isr = 0u;
    if (value == 1u) execute();
}

static uint32_t now_mock(void *context) { (void)context; mock.timer += 100u; return mock.timer; }

static void init(void) {
    memset(&mock, 0, sizeof(mock));
    logi_io_t io = {.read = read_mock, .write = write_mock, .now = now_mock};
    assert(logi_board_init(&board, &io) == 0);
}

static void capture(uint8_t state) {
    mock.line = state;
    logi_board_poll(&board);
    assert(board.pending);
    logi_board_poll(&board);
    assert(!board.pending && !board.error);
}

static void setup(uint8_t type, uint8_t request, uint16_t value, uint16_t size) {
    const uint8_t bytes[] = {type,request,(uint8_t)value,(uint8_t)(value >> 8u),0,0,
                             (uint8_t)size,(uint8_t)(size >> 8u)};
    live_usb_control_setup(&usb, bytes);
}

static void test_usb(void) {
    live_usb_control_init(&usb, 123u, "B72817DA2FE6A757");
    setup(0xc0u, LIVE_USB_LOGI_STATUS, 0u, 64u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_PROBE, 0u, 0u);
    assert(usb.action == USB_ACTION_STALL && !usb.logi_fs_ready);
    setup(0u, 5u, 1u, 0u); live_usb_control_complete(&usb, 1u, NULL, 0u);
    setup(0u, 9u, 1u, 0u); live_usb_control_complete(&usb, 1u, NULL, 0u);
    setup(0xc0u, LIVE_USB_LOGI_STATUS, 0u, 64u); assert(usb.action == USB_ACTION_STALL);
    logi_board_status(&board, usb.logi_status, 123u, 100u, 0u);
    usb.logi_status_valid = 1u;
    setup(0xc0u, LIVE_USB_LOGI_STATUS, 0u, 64u);
    assert(usb.action == USB_ACTION_IN && usb.length == 64u && !memcmp(usb.data, usb.logi_status, 64u));
    live_usb_control_complete(&usb, 1u, NULL, 64u);
    live_usb_control_complete(&usb, 0u, NULL, 0u);
    assert(usb.phase == USB_PHASE_IDLE);
    setup(0xc0u, LIVE_USB_LOGI_STATUS, 1u, 64u); assert(usb.action == USB_ACTION_STALL);
    setup(0xc0u, LIVE_USB_LOGI_STATUS, 0u, 63u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_STATUS, 0u, 64u); assert(usb.action == USB_ACTION_STALL);
    const uint8_t bad_index[] = {0xc0u, LIVE_USB_LOGI_STATUS, 0u, 0u, 1u, 0u, 64u, 0u};
    live_usb_control_setup(&usb, bad_index); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_WIFI_SUBMIT, 0u, 64u); assert(usb.action == USB_ACTION_STALL);
    setup(0x80u, 6u, 0x0100u, 18u);
    assert(usb.action == USB_ACTION_IN && usb.data[10] == 0x27u && usb.data[11] == 0x40u);
    setup(0x40u, LIVE_USB_LOGI_PROBE, 0u, 1u); assert(usb.action == USB_ACTION_STALL);
    setup(0xc0u, LIVE_USB_LOGI_PROBE, 0u, 0u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_PROBE, 0u, 0u);
    assert(usb.logi_fs_pending && !usb.logi_fs_ready);
    setup(0xc0u, LIVE_USB_INFO, 0u, 12u);
    assert(!usb.logi_fs_pending && !usb.logi_fs_ready);
    setup(0x40u, LIVE_USB_LOGI_PROBE, 0u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(usb.logi_fs_ready && !usb.logi_fs_pending && !usb.logi_fs_valid);
    setup(0x40u, LIVE_USB_LOGI_PROBE, 0u, 0u); assert(usb.action == USB_ACTION_STALL);
    usb.logi_fs_ready = 0u;
    usb.logi_fs_valid = 1u;
    setup(0xc0u, LIVE_USB_LOGI_FS_STATUS, 0u, 64u);
    assert(usb.action == USB_ACTION_IN && usb.length == 64u);
    setup(0xc0u, LIVE_USB_LOGI_FS_STATUS, 0u, 63u); assert(usb.action == USB_ACTION_STALL);
    usb.logi_fs_trace[9] = 0x4bu;
    setup(0xc0u, LIVE_USB_LOGI_FS_TRACE, 0u, 64u);
    assert(usb.action == USB_ACTION_IN && usb.length == 64u && usb.data[9] == 0x4bu);
    setup(0xc0u, LIVE_USB_LOGI_FS_TRACE, 0u, 63u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_KEYPAD, 0u, 0u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_KEYPAD, 31u, 0u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_KEYPAD, 2u, 0u);
    assert(usb.logi_fs_pending && usb.logi_keypad_pending == 2u && !usb.logi_fs_ready);
    setup(0xc0u, LIVE_USB_INFO, 0u, 12u);
    assert(!usb.logi_fs_pending && !usb.logi_keypad_pending);
    setup(0x40u, LIVE_USB_LOGI_KEYPAD, 30u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(usb.logi_fs_ready && usb.logi_keypad_seconds == 30u && !usb.logi_keypad_pending);
    usb.logi_fs_ready = 0u; usb.logi_keypad_active = 1u;
    setup(0x40u, LIVE_USB_LOGI_KEYPAD, 2u, 0u); assert(usb.action == USB_ACTION_STALL);
    setup(0x40u, LIVE_USB_LOGI_PROBE, 0u, 0u); assert(usb.action == USB_ACTION_STALL);
    usb.logi_keypad_valid = 1u;
    usb.logi_keypad_events[31][14] = 32u;
    setup(0xc0u, LIVE_USB_LOGI_KEYPAD_STATUS, 0u, 64u); assert(usb.action == USB_ACTION_IN);
    setup(0xc0u, LIVE_USB_LOGI_KEYPAD_EVENT, 31u, 64u);
    assert(usb.action == USB_ACTION_IN && usb.data[14] == 32u);
    setup(0xc0u, LIVE_USB_LOGI_KEYPAD_EVENT, 32u, 64u); assert(usb.action == USB_ACTION_STALL);
    setup(0xc0u, LIVE_USB_LOGI_KEYPAD_STATUS, 0u, 63u); assert(usb.action == USB_ACTION_STALL);
    usb.logi_keypad_active = 0u;
    setup(0x40u, LIVE_USB_BOOTSEL, 0u, 0u);
    assert(!usb.reboot_requested);
    setup(0xc0u, LIVE_USB_INFO, 0u, 12u); assert(!usb.reboot_requested);
    setup(0x40u, LIVE_USB_BOOTSEL, 0u, 0u);
    live_usb_control_complete(&usb, 1u, NULL, 0u);
    assert(usb.reboot_requested);
    live_usb_control_reset(&usb);
    assert(!usb.logi_status_valid && !usb.reboot_requested && !usb.configured);
    assert(!usb.logi_fs_pending && !usb.logi_fs_ready && !usb.logi_fs_valid);
    assert(!usb.logi_keypad_pending && !usb.logi_keypad_seconds && !usb.logi_keypad_valid);
}

int main(void) {
    init(); mock.patterned = 1u; mock.pattern = 0xe4e4e4e4u;
    capture(0u);
    assert(board.last_word == mock.pattern && board.changes == 15u);
    for (unsigned i = 0u; i < 4u; ++i) assert(board.histogram[i] == 4u);
    mock.patterned = 0u;
    capture(0u);
    assert(board.changes == 16u && board.last_word == 0u);
    init();
    for (uint8_t state = 0u; state < 4u; ++state) capture(state);
    assert(board.captures == 4u && board.samples == 64u && board.changes == 3u);
    for (unsigned i = 0; i < 4u; ++i) assert(board.histogram[i] == 16u);
    assert(board.last_word == 0xffffffffu);
    capture(1u); assert(board.last_word == 0x55555555u);
    capture(2u); assert(board.last_word == 0xaaaaaaaau);
    uint8_t status[64];
    logi_board_status(&board, status, 123u, 999u, 0u);
    assert(badger_bytes_load_u32(status) == LOGI_STATUS_MAGIC);
    assert(badger_bytes_load_u32(status + 60u) == diag_crc32(status, 60u));
    assert(status[52] == 2u && status[53] == LOGI_MODE_PASSIVE);
    test_usb();
    init(); mock.stall_pio = 1u;
    logi_board_poll(&board);
    mock.timer += 10000u;
    logi_board_poll(&board);
    assert(board.error == -4402 && board.errors == 1u && !board.pending);
    logi_board_status(&board, status, 123u, 999u, 7u);
    assert((int32_t)badger_bytes_load_u32(status + 44u) == -4402);
    assert(badger_bytes_load_u32(status + 48u) == 7u);
    assert(badger_bytes_load_u32(status + 56u) == 1u);
    init(); mock.fifo_error = 1u;
    logi_board_poll(&board); logi_board_poll(&board);
    assert(board.error == -4403 && board.errors == 1u);
    init(); board.samples = 0xfffffff0u;
    logi_board_poll(&board); assert(board.error == -4404);
    mock.cancel_reset = 1u;
    logi_io_t io = {.read = read_mock, .write = write_mock, .now = now_mock};
    assert(logi_board_init(&board, &io) == -4401);
    puts("Logi Host: input-only pads, PIO instruction model, all line states, counters/CRC,");
    puts("bounded errors, isolated diagnostic USB requests and deferred BOOTSEL PASS");
    return 0;
}
