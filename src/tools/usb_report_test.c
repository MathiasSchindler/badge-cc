#include "display.h"
#include "graphics.h"
#include "diagnostics_logic.h"
#include "../shared/usb_report.h"
#include "../arch/rp2350/bootrom_reboot.h"
#include "usb_report_build.h"

extern volatile uint8_t __report_request[];
extern volatile uint8_t __report_response[];
static badger_canvas_t canvas;
static uint8_t request[USB_REPORT_WIRE_SIZE];
static uint8_t response[USB_REPORT_WIRE_SIZE];
static uint8_t nonce[USB_REPORT_NONCE_SIZE];

static void show(const char *state, uint16_t color, int32_t detail) {
    char number[12];
    badger_gfx_clear(&canvas, 0x0863u);
    badger_gfx_text(&canvas, 8, 14, "USB REPORT TEST", 2u, 0xffffu);
    badger_gfx_text(&canvas, 8, 49, state, 1u, color);
    diag_decimal(number, detail);
    badger_gfx_text(&canvas, 8, 69, number, 1u, 0xffffu);
    badger_gfx_text(&canvas, 8, 108, "RAM MAILBOX 2007F100", 1u, 0x9cf3u);
    badger_gfx_text(&canvas, 8, 128, "NO RADIO / NO FLASH LOG WRITES", 1u, 0x9cf3u);
    badger_gfx_text(&canvas, 8, 168, "HOST WILL VERIFY NONCE AND CRC", 1u, 0xfda8u);
    badger_gfx_text(&canvas, 8, 210, "BOOTSEL USB SHOULD APPEAR AUTOMATICALLY", 1u, 0xffffu);
    badger_display_begin();
    for (uint32_t i = 0u; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i) {
        badger_display_write(canvas.pixels[i]);
    }
    badger_display_end();
}

static void publish(void) {
    for (uint32_t i = 0u; i < 4u; ++i) __report_response[i] = 0u;
    __asm__ volatile ("dsb" ::: "memory");
    for (uint32_t i = 4u; i < USB_REPORT_WIRE_SIZE; ++i) __report_response[i] = response[i];
    __asm__ volatile ("dsb" ::: "memory");
    for (uint32_t i = 0u; i < 4u; ++i) __report_response[i] = response[i];
    __asm__ volatile ("dsb" ::: "memory");
}

void bare_main(void) {
    for (uint32_t i = 0u; i < USB_REPORT_WIRE_SIZE; ++i) request[i] = __report_request[i];
    for (uint32_t i = 0u; i < 4u; ++i) {
        __report_response[i] = 0u;
        __report_request[i] = 0u;
    }
    usb_record_result_t result = usb_report_check_request(request, sizeof(request), USB_REPORT_BUILD_ID);
    for (uint32_t i = 0u; i < USB_REPORT_NONCE_SIZE; ++i) nonce[i] = request[16u + i];
    usb_report_response(response, USB_REPORT_BUILD_ID, nonce,
                        result == USB_RECORD_OK ? USB_REPORT_OK : USB_REPORT_REQUEST_INVALID,
                        result == USB_RECORD_OK ? 0xfeedc0deu : (uint32_t)result);
    publish();
    badger_display_init();
    show(result == USB_RECORD_OK ? "KNOWN REPORT WRITTEN / REBOOTING" :
         "INVALID REQUEST / RETURNING TO BOOTSEL",
         result == USB_RECORD_OK ? 0x5ecbu : 0xfda8u, (int32_t)result);
    int32_t error = badger_reboot_to_bootsel();
    usb_report_response(response, USB_REPORT_BUILD_ID, nonce, USB_REPORT_ROM_ERROR, (uint32_t)error);
    publish();
    show("BOOTSEL REBOOT FAILED / USE BOOT BUTTON", 0xf9c7u, error);
    while (1) __asm__ volatile ("wfi");
}
