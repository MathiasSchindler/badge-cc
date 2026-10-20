#include "rp2350/bootrom.h"
#include "usb/hardware.h"
#include "build_id.h"

static usb_control_t control;
volatile int32_t pico2w_error;
volatile uint32_t pico2w_stage;

static void halt(int32_t error) __attribute__((noreturn));
static void halt(int32_t error) {
    pico2w_error = error;
    while (1) __asm__ volatile ("wfi");
}

void bare_main(void) {
    pico2w_stage = 1u;
    /* Arm bounded-lifetime recovery before any USB clock changes. */
    int32_t error = rp2350_schedule_bootsel(8000u);
    if (error != 0) halt(error);

    uint32_t id[2];
    error = rp2350_chip_id(id);
    if (error != 0) halt(error);
    usb_control_init(&control, PICO2W_BUILD_ID, id);
    pico2w_stage = 2u;
    error = usb_hw_init();
    if (error != 0) halt(error);

    pico2w_stage = 3u;
    while (1) {
        error = usb_hw_poll(&control);
        if (error != 0 || control.reboot_requested) {
            pico2w_error = error;
            pico2w_stage = 4u;
            usb_hw_disconnect();
            int32_t reboot_error = rp2350_schedule_bootsel(10u);
            halt(reboot_error != 0 ? reboot_error : error);
        }
    }
}
