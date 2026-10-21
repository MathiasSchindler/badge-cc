#include "control.h"

static const uint8_t device_descriptor[] = {
    18u, 1u, 0u, 2u, 0u, 0u, 0u, 64u,
    USB_VID & 255u, USB_VID >> 8u, USB_PID & 255u, USB_PID >> 8u,
    1u, 1u, 1u, 2u, 3u, 1u
};
static const uint8_t configuration_descriptor[] = {
    9u, 2u, 18u, 0u, 1u, 1u, 0u, 0x80u, 50u,
    9u, 4u, 0u, 0u, 0u, 0xffu, 0u, 0u, 0u
};
static const uint8_t ms_string[] = {
    18, 3, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, 0x57, 0
};
static const uint8_t ms_compatible[] = {
    40, 0, 0, 0, 0, 1, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
/* Microsoft OS 1.0 extended property: REG_MULTI_SZ DeviceInterfaceGUIDs. */
static const uint16_t ms_properties[] = {
    146, 0, 0x0100, 5, 1,
    136, 0, 7, 0, 42,
    'D', 'e', 'v', 'i', 'c', 'e', 'I', 'n', 't', 'e', 'r', 'f', 'a', 'c', 'e',
    'G', 'U', 'I', 'D', 's', 0,
    80, 0,
    '{', 'D', '1', '7', '5', '1', '8', 'E', '7', '-', 'B', 'D', '1', 'D', '-',
    '4', 'C', '3', 'D', '-', '8', '1', 'A', '0', '-', '5', '6', '0', '5', 'C',
    'A', 'F', 'E', '4', '0', '2', '7', '}', 0, 0
};

static uint16_t read16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8u);
}

static void stall(usb_control_t *control) {
    control->phase = USB_PHASE_STALL;
    control->action = USB_ACTION_STALL;
    control->address_pending = 0u;
    control->configuration_pending = 0u;
    control->reboot_pending = 0u;
    control->report_pending = 0u;
    control->lora_pending = 0u;
    control->in_remaining = 0u;
    control->in_zlp = 0u;
    ++control->rejected;
}

static void send(usb_control_t *control, const uint8_t *data,
                  uint16_t size, uint16_t requested) {
    if (size > requested) size = requested;
    uint16_t first = size > USB_PACKET_SIZE ? USB_PACKET_SIZE : size;
    for (uint16_t i = 0u; i < first; ++i) control->data[i] = data[i];
    control->in_source = size > first ? data + first : 0;
    control->in_remaining = size - first;
    control->in_zlp = size < requested && size != 0u && (size % USB_PACKET_SIZE) == 0u;
    control->in_pid = 1u;
    control->length = first;
    control->action = USB_ACTION_IN;
    control->phase = USB_PHASE_DATA_IN;
}

static void status_in(usb_control_t *control) {
    control->in_pid = 1u;
    control->length = 0u;
    control->in_remaining = 0u;
    control->in_zlp = 0u;
    control->action = USB_ACTION_IN;
    control->phase = USB_PHASE_STATUS_IN;
}

static void string_descriptor(usb_control_t *control, const char *text,
                               uint16_t requested) {
    uint8_t data[64];
    uint16_t length = 2u;
    for (uint16_t i = 0u; text[i] != '\0' && length + 2u <= sizeof(data); ++i) {
        data[length++] = (uint8_t)text[i];
        data[length++] = 0u;
    }
    data[0] = (uint8_t)length;
    data[1] = 3u;
    send(control, data, length, requested);
}

void usb_control_reset(usb_control_t *control) {
    control->phase = USB_PHASE_IDLE;
    control->in_remaining = 0u;
    control->in_zlp = 0u;
    control->action = USB_ACTION_NONE;
    control->configured = 0u;
    control->address = 0u;
    control->address_pending = 0u;
    control->configuration_pending = 0u;
    control->reboot_pending = 0u;
    control->reboot_requested = 0u;
    control->report_pending = 0u;
    control->report_valid = 0u;
    control->lora_pending = 0u;
    control->lora_requested = 0u;
    control->lora_running = 0u;
    control->lora_valid = 0u;
    control->length = 0u;
}

void usb_control_init(usb_control_t *control, uint32_t build_id,
                       const uint32_t chip_id[2]) {
    static const char hex[] = "0123456789ABCDEF";
    control->build_id = build_id;
    control->requests = 0u;
    control->rejected = 0u;
    control->lora_sequence = 0u;
    control->lora_disabled = 0u;
    control->app_status_valid = 0u;
    control->wifi_status_valid = 0u;
    for (uint32_t word = 0u; word < 2u; ++word) {
        control->chip_id[word] = chip_id[word];
        for (uint32_t digit = 0u; digit < 8u; ++digit)
            control->serial[(1u - word) * 8u + digit] =
                hex[(chip_id[word] >> ((7u - digit) * 4u)) & 15u];
    }
    control->serial[16] = '\0';
    usb_control_reset(control);
}

void usb_control_setup(usb_control_t *control, const uint8_t setup[8]) {
    uint8_t type = setup[0], request = setup[1];
    uint16_t value = read16(setup + 2u);
    uint16_t index = read16(setup + 4u);
    uint16_t length = read16(setup + 6u);
    if (control->report_pending) control->report_valid = 0u;
    if (control->lora_pending) control->lora_valid = 0u;
    control->action = USB_ACTION_NONE;
    control->address_pending = 0u;
    control->configuration_pending = 0u;
    control->reboot_pending = 0u;
    control->report_pending = 0u;
    control->lora_pending = 0u;
    control->phase = USB_PHASE_IDLE;
    control->in_remaining = 0u;
    control->in_zlp = 0u;

    control->out_request = request;
    if (type == 0x80u && request == 6u && length != 0u) {
        uint8_t descriptor = (uint8_t)(value >> 8u);
        uint8_t number = (uint8_t)value;
        if (descriptor == 1u && number == 0u && index == 0u) {
            send(control, device_descriptor, sizeof(device_descriptor), length);
        } else if (descriptor == 2u && number == 0u && index == 0u) {
            send(control, configuration_descriptor, sizeof(configuration_descriptor), length);
        } else if (descriptor == 3u && number == 0xeeu && index == 0u) {
            send(control, ms_string, sizeof(ms_string), length);
        } else if (descriptor == 3u && number == 0u && index == 0u) {
            const uint8_t languages[] = {4u, 3u, 9u, 4u};
            send(control, languages, sizeof(languages), length);
        } else if (descriptor == 3u && (index == 0x0409u || index == 0u) &&
                   number >= 1u && number <= 3u) {
            string_descriptor(control, number == 1u ? "Pico 2 W lab" :
                               number == 2u ? "Pico 2 W Reports" : control->serial, length);
        } else stall(control);
    } else if (type == 0xc0u && request == 0x57u && value == 0u && index == 4u && length != 0u) {
        send(control, ms_compatible, sizeof(ms_compatible), length);
    } else if (type == 0xc1u && request == 0x57u && value == 0u && index == 5u && length != 0u) {
        send(control, (const uint8_t *)ms_properties, sizeof(ms_properties), length);
    } else if (type == 0u && request == 5u && value < 128u &&
               index == 0u && length == 0u && !control->configured) {
        control->pending_address = (uint8_t)value;
        control->address_pending = 1u;
        status_in(control);
    } else if (type == 0u && request == 9u && value <= 1u &&
               index == 0u && length == 0u && control->address != 0u) {
        control->pending_configuration = (uint8_t)value;
        control->configuration_pending = 1u;
        status_in(control);
    } else if (type == 0x80u && request == 8u && value == 0u && index == 0u &&
               length == 1u && control->address != 0u) {
        send(control, &control->configured, 1u, length);
    } else if (request == 0u && value == 0u && length == 2u &&
               ((type == 0x80u && index == 0u) ||
                (type == 0x81u && index == 0u && control->configured) ||
                (type == 0x82u && (index == 0u || index == 0x80u)))) {
        const uint8_t status[] = {0u, 0u};
        send(control, status, sizeof(status), length);
    } else if (type == 0x81u && request == 10u && value == 0u && index == 0u &&
               length == 1u && control->configured) {
        const uint8_t alternate = 0u;
        send(control, &alternate, 1u, length);
    } else if (type == 1u && request == 11u && value == 0u && index == 0u &&
               length == 0u && control->configured) {
        status_in(control);
    } else if (control->configured && value == 0u && index == 0u) {
        if (type == 0x40u && request == USB_SUBMIT && length == USB_REPORT_SIZE) {
            control->report_valid = 0u;
            control->length = USB_REPORT_SIZE;
            control->action = USB_ACTION_OUT;
            control->phase = USB_PHASE_DATA_OUT;
        } else if (type == 0xc0u && request == USB_REPORT &&
                   length == USB_REPORT_SIZE && control->report_valid) {
            send(control, control->report, sizeof(control->report), length);
        } else if (type == 0x40u && request == USB_BOOTSEL && length == 0u) {
            control->reboot_pending = 1u;
            status_in(control);
        } else if (type == 0xc0u && request == USB_INFO && length == 12u) {
            uint8_t info[12] = {'P', '2', 'W', 'L', 1u, 0u, 64u, 0u};
            usb_store_u32(info + 8u, control->build_id);
            send(control, info, sizeof(info), length);
        } else if (type == 0x40u && request == USB_LORA_SUBMIT &&
                   length == USB_REPORT_SIZE && !control->lora_requested &&
                   !control->lora_running && !control->lora_disabled) {
            control->lora_valid = 0u;
            control->length = USB_REPORT_SIZE;
            control->action = USB_ACTION_OUT;
            control->phase = USB_PHASE_DATA_OUT;
        } else if (type == 0xc0u && request == USB_LORA_REPORT &&
                   length == USB_REPORT_SIZE && control->lora_valid) {
            send(control, control->lora_report, sizeof(control->lora_report), length);
        } else if (type == 0xc0u && request == USB_APP_STATUS &&
                   length == USB_REPORT_SIZE && control->app_status_valid) {
            send(control, control->app_status, sizeof(control->app_status), length);
        } else if (type == 0xc0u && request == USB_WIFI_STATUS &&
                   length == USB_REPORT_SIZE && control->wifi_status_valid) {
            send(control, control->wifi_status, sizeof(control->wifi_status), length);
        } else stall(control);
    } else stall(control);
}

void usb_control_complete(usb_control_t *control, uint8_t in,
                           const uint8_t *data, uint16_t length) {
    control->action = USB_ACTION_NONE;
    if (control->phase == USB_PHASE_DATA_IN && in && length == control->length) {
        if (control->in_remaining != 0u || control->in_zlp) {
            uint16_t next = control->in_remaining > USB_PACKET_SIZE ?
                            USB_PACKET_SIZE : control->in_remaining;
            for (uint16_t i = 0u; i < next; ++i) control->data[i] = control->in_source[i];
            if (next != 0u) control->in_source += next;
            else control->in_zlp = 0u;
            control->in_remaining -= next;
            control->in_pid ^= 1u;
            control->length = next;
            control->action = USB_ACTION_IN;
            return;
        }
        control->phase = USB_PHASE_STATUS_OUT;
        control->action = USB_ACTION_OUT;
        control->length = 0u;
    } else if (control->phase == USB_PHASE_STATUS_OUT && !in && length == 0u) {
        control->phase = USB_PHASE_IDLE;
    } else if (control->phase == USB_PHASE_STATUS_IN && in && length == 0u) {
        control->phase = USB_PHASE_IDLE;
        if (control->address_pending) {
            control->address = control->pending_address;
            control->address_pending = 0u;
            control->action = USB_ACTION_ADDRESS;
        }
        if (control->configuration_pending) {
            control->configured = control->pending_configuration;
            control->configuration_pending = 0u;
            control->report_valid = 0u;
            control->lora_valid = 0u;
            control->lora_requested = 0u;
            control->lora_running = 0u;
        }
        if (control->report_pending) {
            control->report_pending = 0u;
            control->report_valid = 1u;
            ++control->requests;
        }
        if (control->reboot_pending) {
            control->reboot_requested = 1u;
            control->reboot_pending = 0u;
        }
        if (control->lora_pending) {
            control->lora_pending = 0u;
            control->lora_requested = 1u;
            control->lora_valid = 1u;
            ++control->lora_sequence;
            usb_store_u32(control->lora_report + 40u, control->lora_sequence);
            usb_store_u32(control->lora_report + 60u, usb_crc32(control->lora_report, 60u));
        }
    } else if (control->phase == USB_PHASE_DATA_OUT && !in && data != 0 &&
               length == USB_REPORT_SIZE) {
        uint8_t lora = control->out_request == USB_LORA_SUBMIT;
        if (usb_report_check_test(data, length, control->build_id,
                                  lora ? 2u : USB_REPORT_TEST_ID) != USB_RECORD_OK) {
            stall(control);
            return;
        }
        if (lora) {
            for (uint32_t i = 0u; i < USB_REPORT_SIZE; ++i)
                control->lora_report[i] = i < 32u ? data[i] : 0u;
            usb_store_u32(control->lora_report, USB_RESPONSE_MAGIC);
            control->lora_pending = 1u;
        } else {
            usb_report_response(control->report, control->build_id, data + 16u,
                                 control->chip_id, control->requests + 1u);
            control->report_pending = 1u;
        }
        status_in(control);
    } else stall(control);
}

void usb_control_lora_result(usb_control_t *control, const sx1262_result_t *result) {
    if (!control->configured || !control->lora_running) return;
    usb_store_u32(control->lora_report + 32u, result->error == 0 ? 1u : 2u);
    usb_store_u32(control->lora_report + 36u, (uint32_t)result->error);
    for (uint32_t i = 0u; i < sizeof(result->samples); ++i)
        control->lora_report[44u + i] = result->samples[i];
    usb_store_u32(control->lora_report + 60u, usb_crc32(control->lora_report, 60u));
    control->lora_running = 0u;
}
