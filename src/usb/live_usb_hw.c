#include "live_usb_hw.h"

#define REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define USB(offset) REG(0x50110000u + (offset))
#define DPRAM(offset) REG(0x50100000u + (offset))
#define EP0_IN DPRAM(0x80u)
#define EP0_OUT DPRAM(0x84u)
#define DATA1 0x2000u
#define FULL 0x8000u
#define AVAILABLE 0x400u
#define STALL 0x800u

static live_usb_stats_t stats;

static uint32_t now_us(void) {
    return REG(0x400b000cu);
}

static int wait_mask(volatile uint32_t *reg, uint32_t mask, uint32_t wanted) {
    uint32_t start = now_us();
    for (uint32_t loops = 0u; loops < 1000000u; ++loops) {
        if ((*reg & mask) == wanted) return 1;
        if (now_us() - start >= 100000u) return 0;
    }
    return 0;
}

static int pause_us(uint32_t us) {
    uint32_t start = now_us();
    for (uint32_t loops = 0u; loops < 1000000u; ++loops) {
        if (now_us() - start >= us) return 1;
    }
    return 0;
}

int32_t live_usb_hw_init(void) {
    stats.resets = stats.setups = stats.completions = stats.hardware_errors = 0u;
    /* Do not reset a PLL currently clocking the executing core. */
    uint32_t sys = REG(0x4001003cu);
    if ((sys & 1u) && ((sys >> 5u) & 7u) == 1u) return -2001;
    if ((REG(0x40048004u) & (1u << 12u)) == 0u) {
        REG(0x4004800cu) = 196u;
        REG(0x40048000u) = 0x00fabaa0u;
    }
    if (!wait_mask(&REG(0x40048004u), 1u << 31u, 1u << 31u)) return -2002;
    REG(0x40022000u) = (1u << 28u) | (1u << 15u);
    REG(0x40010060u) = 0u;
    REG(0x40023000u) = 1u << 15u;
    if (!wait_mask(&REG(0x40020008u), 1u << 15u, 1u << 15u)) return -2003;
    REG(0x40058000u) = 1u;
    REG(0x40058008u) = 100u;
    REG(0x40058004u) = 0x0cu;
    if (!wait_mask(&REG(0x40058000u), 1u << 31u, 1u << 31u)) return -2004;
    REG(0x4005800cu) = (5u << 16u) | (5u << 12u);
    REG(0x40058004u) = 4u;
    REG(0x40010064u) = 1u << 16u;
    REG(0x40010060u) = 1u << 11u;
    REG(0x40023000u) = 1u << 28u;
    if (!wait_mask(&REG(0x40020008u), 1u << 28u, 1u << 28u)) return -2005;
    for (uint32_t offset = 0u; offset < 4096u; offset += 4u) DPRAM(offset) = 0u;
    USB(0x74u) = 0x09u;
    USB(0x78u) = 0x0cu;
    USB(0x00u) = 0u;
    USB(0x40u) = 1u;
    USB(0x4cu) = 1u << 29u;
    USB(0x90u) = 0u;
    if (!pause_us(10000u)) return -2006;
    USB(0x4cu) |= 1u << 16u;
    return 0;
}

void live_usb_hw_disconnect(void) {
    USB(0x4cu) &= ~(1u << 16u);
}

static void arm_buffer(volatile uint32_t *buffer, uint32_t value) {
    *buffer = value;
    /* The controller must observe buffer fields before ownership changes. */
    __asm__ volatile ("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop" ::: "memory");
    *buffer = value | AVAILABLE;
}

static void apply_action(live_usb_control_t *control) {
    if (control->action == USB_ACTION_IN) {
        volatile uint8_t *data = (volatile uint8_t *)0x50100100u;
        for (uint16_t i = 0u; i < control->length; ++i) data[i] = control->data[i];
        arm_buffer(&EP0_IN, FULL | DATA1 | control->length);
    } else if (control->action == USB_ACTION_OUT) {
        arm_buffer(&EP0_OUT, DATA1 | control->length);
    } else if (control->action == USB_ACTION_ADDRESS) {
        USB(0x00u) = control->address;
    } else if (control->action == USB_ACTION_STALL) {
        USB(0x68u) = 3u;
        EP0_IN = STALL;
        EP0_OUT = STALL;
    }
    control->action = USB_ACTION_NONE;
}

static int abort_ep0(void) {
    USB(0x60u) = 3u;
    if (!wait_mask(&USB(0x64u), 3u, 3u)) return 0;
    EP0_IN = 0u;
    EP0_OUT = 0u;
    USB(0x58u) = 3u;
    USB(0x68u) = 0u;
    USB(0x60u) = 0u;
    USB(0x64u) = 3u;
    return 1;
}

static int32_t poll_active(live_usb_control_t *control, uint32_t status) {
    if (status & (1u << 19u)) {
        USB(0x50u) = 1u << 19u;
        USB(0x00u) = 0u;
        EP0_IN = EP0_OUT = 0u;
        USB(0x58u) = 3u;
        USB(0x68u) = 0u;
        live_usb_control_reset(control);
        ++stats.resets;
    }
    if (status & (1u << 17u)) {
        /* A fast host can deliver its next SETUP before we service the
           previous status ACK. Commit that ACK before aborting old buffers. */
        if (control->phase == USB_PHASE_STATUS_IN && (USB(0x58u) & 1u) != 0u) {
            USB(0x58u) = 1u;
            ++stats.completions;
            live_usb_control_complete(control, 1u, 0, (uint16_t)(EP0_IN & 0x3ffu));
            apply_action(control);
        }
        uint8_t setup[8];
        volatile uint8_t *source = (volatile uint8_t *)0x50100000u;
        for (uint32_t i = 0u; i < sizeof(setup); ++i) setup[i] = source[i];
        if (!abort_ep0()) return -2010;
        USB(0x50u) = 1u << 17u;
        ++stats.setups;
        live_usb_control_setup(control, setup);
        apply_action(control);
    }
    uint32_t done = USB(0x58u) & 3u;
    if (done & 1u) {
        USB(0x58u) = 1u;
        uint16_t length = (uint16_t)(EP0_IN & 0x3ffu);
        ++stats.completions;
        live_usb_control_complete(control, 1u, 0, length);
        apply_action(control);
    }
    if (done & 2u) {
        USB(0x58u) = 2u;
        uint16_t length = (uint16_t)(EP0_OUT & 0x3ffu);
        uint8_t data[LIVE_USB_PACKET];
        if (length > sizeof(data)) return -2011;
        volatile uint8_t *source = (volatile uint8_t *)0x50100100u;
        for (uint32_t i = 0u; i < length; ++i) data[i] = source[i];
#ifdef LIVE_USB_CLEAN
        uint8_t secret=control->clean_secret_out;
#endif
        ++stats.completions;
        live_usb_control_complete(control, 0u, data, length);
#ifdef LIVE_USB_CLEAN
        if (secret) for (uint32_t i=0u;i<sizeof(data);++i) {
            ((volatile uint8_t *)data)[i]=0u;
            source[i]=0u;
        }
#endif
        apply_action(control);
    }
    uint32_t errors = status & 0x8f800000u;
    if (errors != 0u) {
        ++stats.hardware_errors;
        USB(0x50u) = errors;
    }
    return 0;
}

int32_t live_usb_hw_poll(live_usb_control_t *control) {
    uint32_t status = USB(0x50u);
    if ((status & ((1u << 19u) | (1u << 17u) | 0x8f800000u)) == 0u &&
        (USB(0x58u) & 3u) == 0u) return 0;
    return poll_active(control, status);
}

const live_usb_stats_t *live_usb_hw_stats(void) {
    return &stats;
}
