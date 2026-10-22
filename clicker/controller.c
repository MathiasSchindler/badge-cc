#include "controller.h"
#include "board.h"
#include "bluetooth.h"
#include "vendor_generated.h"
#include "display.h"
#include "wifi_board.h"
#include "../src/radio/wifi_firmware.h"
#include "../src/tools/diagnostics_logic.h"
#include "../src/arch/rp2350/bootrom_reboot.h"
#ifdef CLICKER_HID
#include "hid.h"
static uint8_t hid_running;
#endif

static badger_canvas_t *screen;
static clicker_input_t *buttons;
static uint8_t (*read_buttons)(void);
static wifi_fw_report_t firmware;
static clicker_bt_t bluetooth;
static const char *phase;
static uint32_t started_at, drawn_at;
static uint8_t complete, succeeded, hci_version;
static uint16_t manufacturer, acl_size;
static uint8_t acl_count, address[6];
#ifdef CLICKER_ADVERTISE
static uint8_t advertising, cleaning_up;
#endif

static uint32_t now(void *context) {
    (void)context;
    const wifi_wire_t *wire = wifi_board_wire();
    return wire->now_us(wire->context);
}

static void text(uint32_t y, const char *label, uint32_t value, uint16_t color) {
    char hex[9];
    diag_hex(hex, value, 8u);
    badger_gfx_text(screen, 4, (int32_t)y, label, 1u, color);
    badger_gfx_text(screen, 4 + 6 * 9, (int32_t)y, hex, 1u, color);
}

static void draw(void) {
    badger_gfx_rect(screen, 0, 124, 320, 116, 0x0843u);
    badger_gfx_text(screen, 4, 128, CLICKER_BOARD_NAME " CLICKER", 1u, 0xffffu);
    badger_gfx_text(screen, 4, 140, phase, 1u,
                    complete ? (succeeded ? 0x07e0u : 0xf800u) : 0xffe0u);
    if (complete && succeeded) {
        text(152u, "HCI VER", hci_version, 0xffffu);
        text(164u, "BT MANU", manufacturer, 0xffffu);
        text(176u, "BLE ACL", ((uint32_t)acl_size << 16u) | acl_count, 0xffffu);
    } else {
        text(152u, "FW ST/ERR", ((uint32_t)firmware.stage << 16u) | firmware.error, 0xffffu);
        text(164u, "BT ST/ERR", ((uint32_t)bluetooth.stage << 16u) | bluetooth.error, 0xffffu);
        text(176u, "BP ADDR", bluetooth.address ? bluetooth.address : firmware.address, 0xffffu);
    }
    text(188u, "TX/RX", (bluetooth.tx_packets << 16u) | bluetooth.rx_packets, 0x07ffu);
    text(200u, "CMD/STAT", ((uint32_t)bluetooth.opcode << 16u) | bluetooth.status, 0x07ffu);
    if (complete && succeeded) {
        char hex[9];
        badger_gfx_text(screen, 4, 212, "BT ADDR", 1u, 0xffffu);
        for (uint32_t i = 0u; i < 6u; ++i) {
            diag_hex(hex, address[5u - i], 2u);
            badger_gfx_text(screen, 58 + (int32_t)(i * 18u), 212, hex, 1u, 0xffffu);
        }
    } else {
        text(212u, "ACTUAL", bluetooth.error ? bluetooth.actual : firmware.actual, 0xffffu);
    }
    badger_gfx_text(screen, 4, 228,
#ifdef CLICKER_ADVERTISE
                    advertising ? "B STOPS ADVERTISING - NO HID" :
#endif
                    complete ? "B TO BOOTSEL - NO HID" :
                    "B CANCELS - NO ADVERTISING", 1u, 0xffffu);
    badger_display_begin_region(0u, 124u, 320u, 116u);
    for (uint32_t x = 0u; x < 320u; ++x) {
        badger_display_write_pixels(&screen->pixels[x * 240u + 124u], 116u);
    }
    badger_display_end_update();
}

static int service(void *context) {
    (void)context;
#ifdef CLICKER_HID
    if (hid_running) return 1;
#endif
    uint32_t tick = now(0);
    clicker_input_sample(buttons, read_buttons(), tick);
    if (tick - drawn_at >= 250000u) {
        drawn_at = tick;
        draw();
    }
    return
#ifdef CLICKER_HID
        tick - started_at < 30000000u;
#else
#ifdef CLICKER_ADVERTISE
        (cleaning_up || (buttons->stable & 2u) == 0u) && tick - started_at < 60000000u;
#else
        (buttons->stable & 2u) == 0u && tick - started_at < 30000000u;
#endif
#endif
}

static int firmware_service(void *context, const wifi_fw_report_t *report) {
    (void)report;
    return service(context);
}

static int read32(void *context, uint32_t location, uint32_t *value) {
    (void)context;
    return wifi_firmware_backplane_read32(location, value);
}

static int write32(void *context, uint32_t location, uint32_t value) {
    (void)context;
    return wifi_firmware_backplane_write32(location, value);
}

static int read_memory(void *context, uint32_t location, uint8_t *data, uint32_t length) {
    (void)context;
    return wifi_firmware_backplane_read(location, data, length);
}

static int write_memory(void *context, uint32_t location, const uint8_t *data, uint32_t length) {
    (void)context;
    return wifi_firmware_backplane_write(location, data, length);
}

static int command(uint16_t opcode, uint8_t reply[16], uint32_t expected_length) {
    uint32_t length;
    if (!clicker_bt_command(&bluetooth, opcode, 0, 0u, reply, 16u, &length)) return 0;
    if (length != expected_length) {
        bluetooth.error = CLICKER_BT_PACKET;
        bluetooth.expected = expected_length;
        bluetooth.actual = length;
        return 0;
    }
    return 1;
}

static int run_checks(void) {
    static const wifi_fw_assets_t assets = {
        wifi_bt, WIFI_BT_SIZE, WIFI_BT_CRC, nvram, NVRAM_SIZE, NVRAM_CRC,
        clm, CLM_SIZE, CLM_CRC
    };
    phase = "LOADING WIFI-BT CONTROLLER FIRMWARE";
    draw();
    if (!wifi_firmware_begin(wifi_board_wire(), &assets, &firmware, firmware_service, 0)) return 0;
    phase = "BLUETOOTH PATCH AND SHARED BUS";
    static const clicker_bt_io_t io = {0, read32, write32, read_memory, write_memory, now, service};
    if (!clicker_bt_start(&bluetooth, &io, bt_patch, BT_PATCH_SIZE, BT_PATCH_CRC)) return 0;
    phase = "QUERYING HCI - NO ADVERTISING";
    uint8_t reply[16];
    if (!command(0x0c03u, reply, 1u) || !command(0x1001u, reply, 9u)) return 0;
    hci_version = reply[1];
    manufacturer = (uint16_t)((uint32_t)reply[5] | (uint32_t)reply[6] << 8u);
    if (!command(0x1009u, reply, 7u)) return 0;
    for (uint32_t i = 0u; i < 6u; ++i) address[i] = reply[i + 1u];
    if (!command(0x2002u, reply, 4u)) return 0;
    acl_size = (uint16_t)((uint32_t)reply[1] | (uint32_t)reply[2] << 8u);
    acl_count = reply[3];
    return command(0x2003u, reply, 9u);
}

#ifdef CLICKER_ADVERTISE
static int advertise_command(uint16_t opcode, const uint8_t *parameters, uint8_t size) {
    uint8_t reply[1];
    uint32_t length;
    if (!clicker_bt_command(&bluetooth, opcode, parameters, size, reply, sizeof(reply), &length)) return 0;
    if (length != 1u) {
        bluetooth.error = CLICKER_BT_PACKET;
        return 0;
    }
    return 1;
}

static int advertise(void) {
    static const uint8_t fallback_address[] = {0xacu, 0x1fu, 0x12u, 0xa2u, 0x43u, 0x43u};
    uint8_t all_zero = 1u, all_ff = 1u, fallback = 1u;
    for (uint32_t i = 0u; i < 6u; ++i) {
        if (address[i] != 0u) all_zero = 0u;
        if (address[i] != 255u) all_ff = 0u;
        if (address[i] != fallback_address[i]) fallback = 0u;
    }
    if (all_zero || all_ff || fallback || (address[5] & 1u) != 0u) {
        bluetooth.error = CLICKER_BT_ARGUMENT;
        return 0;
    }
    /* ADV_NONCONN_IND: no connections, scan responses, pairing or HID claims. */
    static const uint8_t parameters[15] = {
        0xa0u, 0u, 0xa0u, 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 7u, 0u
    };
    static const uint8_t data[32] = {
        21u, 2u, 1u, 6u, 17u, 9u,
        'U', 'n', 'i', 'v', 'e', 'r', 's', 'e', ' ',
        'C', 'l', 'i', 'c', 'k', 'e', 'r'
    };
    uint8_t enabled = 1u;
    if (!advertise_command(0x2006u, parameters, sizeof(parameters)) ||
        !advertise_command(0x2008u, data, sizeof(data)) ||
        !advertise_command(0x200au, &enabled, 1u)) return 0;
    advertising = complete = 1u;
    phase = "BOUNDED ADVERTISING - NO HID/PAIRING";
    draw();
    uint32_t start = now(0);
    while (now(0) - start < 25000000u) {
        if (!service(0)) break;
    }
    advertising = 0u;
    cleaning_up = 1u;
    enabled = 0u;
    return advertise_command(0x200au, &enabled, 1u);
}
#endif

void clicker_controller_run(badger_canvas_t *canvas, clicker_input_t *input,
                             uint8_t (*buttons_read)(void)) {
    screen = canvas;
    buttons = input;
    read_buttons = buttons_read;
    wifi_board_init();
    wifi_board_select_pio(1u);
    started_at = drawn_at = now(0);
    succeeded = (uint8_t)run_checks();
#ifdef CLICKER_HID
    if (succeeded) {
        hid_running=1u;
        clicker_hid_run(canvas,input,buttons_read,&bluetooth,address,acl_size,acl_count);
    }
#endif
#ifdef CLICKER_ADVERTISE
    if (succeeded) succeeded = (uint8_t)advertise();
#endif
    wifi_firmware_stop();
    /* A failed begin already powers down, including failures before retention. */
    wifi_wire_power_off(wifi_board_wire());
    complete = 1u;
    phase = succeeded ?
#ifdef CLICKER_ADVERTISE
        "ADVERTISING DONE - RADIO NOW OFF" :
#else
        "HCI VERIFIED - RADIO NOW OFF" :
#endif
        "CONTROLLER CHECK FAILED - RADIO OFF";
    draw();
    clicker_input_init(buttons);
    clicker_input_set_ready(buttons, 1u);
    for (;;) {
        uint8_t report[CLICKER_REPORT_SIZE];
        clicker_input_sample(buttons, read_buttons(), now(0));
        if (clicker_input_peek(buttons, report)) {
            if (report[2] == CLICKER_KEY_BLANK) {
                int32_t result = badger_reboot_to_bootsel();
                phase = "BOOTSEL REQUEST FAILED";
                bluetooth.actual = (uint32_t)result;
                succeeded = 0u;
                draw();
            }
            clicker_input_accept(buttons);
        }
    }
}
