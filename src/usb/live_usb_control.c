#include "live_usb.h"
#include "../shared/bytes.h"
#include "../radio/wifi_usb_report.h"
#ifdef LIVE_USB_FIRMWARE
#include "../radio/wifi_firmware.h"
#endif
#ifdef LIVE_USB_LINK
#include "../radio/wifi_link.h"
#endif
#ifdef LIVE_USB_UDP
#include "../radio/wifi_udp_report.h"
#endif

static void cancel_udp(live_usb_control_t *control) {
    control->udp_out=control->udp_pending=control->udp_ready=control->udp_running=0u;
    control->udp_valid=control->udp_complete=control->udp_frozen=0u;
    if (++control->udp_generation==0u) ++control->udp_generation;
}

#ifdef LIVE_USB_NAMETAG_M2
static void cancel_m2(live_usb_control_t *control) {
    control->m2_out = control->m2_pending = control->m2_ready = control->m2_valid = 0u;
    if (++control->m2_generation == 0u) ++control->m2_generation;
}
#endif
#ifdef LIVE_USB_COMPILER
static void cancel_compiler(live_usb_control_t *control) {
    control->compiler_out = control->compiler_pending = control->compiler_ready = control->compiler_valid = 0u;
    if (++control->compiler_generation == 0u) ++control->compiler_generation;
}
#endif

static const uint8_t device_descriptor[] = {
    18u, 1u, 0u, 2u, 0u, 0u, 0u, 64u,
    LIVE_USB_VID & 255u, LIVE_USB_VID >> 8u,
    LIVE_USB_PID & 255u, LIVE_USB_PID >> 8u,
    0u, 1u, 1u, 2u, 3u, 1u
};
static const uint8_t configuration_descriptor[] = {
    9u, 2u, 18u, 0u, 1u, 1u, 0u, 0x80u, 125u,
    9u, 4u, 0u, 0u, 0u, 0xffu, 0u, 0u, 0u
};

static uint16_t read16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8u);
}

static void stall(live_usb_control_t *control) {
#ifdef LIVE_USB_LOGI_HOST
    control->logi_fs_pending = 0u;
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
    control->keypad_out = control->keypad_pending = 0u;
#endif
#ifdef LIVE_USB_CLEAN
    for (uint32_t i=0u;i<64u;++i) ((volatile uint8_t *)control->clean_secret)[i]=0u;
    control->clean_secret_ready=control->clean_secret_out=0u;
#endif
    control->phase = USB_PHASE_STALL;
    control->action = USB_ACTION_STALL;
    control->address_pending = 0u;
    control->configuration_pending = 0u;
    control->reboot_pending = 0u;
    control->probe_pending = 0u;
#ifdef LIVE_USB_NAMETAG_M2
    control->m2_out = control->m2_pending = 0u;
#endif
#ifdef LIVE_USB_COMPILER
    control->compiler_out = control->compiler_pending = 0u;
#endif
    ++control->rejected;
}

static void send(live_usb_control_t *control, const uint8_t *data,
                  uint16_t size, uint16_t requested) {
    if (size > requested) size = requested;
    if (size > LIVE_USB_PACKET) {
        stall(control);
        return;
    }
    for (uint16_t i = 0u; i < size; ++i) control->data[i] = data[i];
    control->length = size;
    control->action = USB_ACTION_IN;
    control->phase = USB_PHASE_DATA_IN;
}

static void status_in(live_usb_control_t *control) {
    control->length = 0u;
    control->action = USB_ACTION_IN;
    control->phase = USB_PHASE_STATUS_IN;
}

static void string_descriptor(live_usb_control_t *control, const char *text,
                               uint16_t requested) {
    uint8_t bytes[64];
    uint16_t length = 2u;
    for (uint16_t i = 0u; text[i] != '\0' && length + 2u <= sizeof(bytes); ++i) {
        bytes[length++] = (uint8_t)text[i];
        bytes[length++] = 0u;
    }
    bytes[0] = (uint8_t)length;
    bytes[1] = 3u;
    send(control, bytes, length, requested);
}

void live_usb_control_reset(live_usb_control_t *control) {
#ifdef LIVE_USB_LOGI_HOST
    control->logi_status_valid = 0u;
    control->logi_fs_valid = control->logi_fs_pending = control->logi_fs_ready = 0u;
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
    control->keypad_out = control->keypad_pending = control->keypad_ready = 0u;
    control->keypad_status_valid = 0u;
#endif
#ifdef LIVE_USB_CLEAN
    for (uint32_t i=0u;i<64u;++i) ((volatile uint8_t *)control->clean_secret)[i]=0u;
    control->clean_secret_ready=control->clean_secret_out=0u;
#endif
    cancel_udp(control);
#ifdef LIVE_USB_NAMETAG_M2
    cancel_m2(control);
#endif
#ifdef LIVE_USB_COMPILER
    cancel_compiler(control);
#endif
    control->phase = USB_PHASE_IDLE;
    control->action = USB_ACTION_NONE;
    control->configured = 0u;
    control->address = 0u;
    control->address_pending = 0u;
    control->configuration_pending = 0u;
    control->reboot_pending = 0u;
    control->reboot_requested = 0u;
    control->report_valid = 0u;
    control->trace_valid = 0u;
    control->probe_pending = 0u;
    control->probe_ready = 0u;
    control->probe_inflight = 0u;
    ++control->probe_generation;
    control->length = 0u;
}

int live_usb_control_poll_immediate(const live_usb_control_t *control) {
    return !control->configured || control->phase != USB_PHASE_IDLE;
}

void live_usb_control_init(live_usb_control_t *control, uint32_t build_id,
                            const char serial[17]) {
    control->build_id = build_id;
    control->requests = 0u;
    control->rejected = 0u;
    control->probe_generation = 0u;
    control->udp_generation = 0u;
#ifdef LIVE_USB_NAMETAG_M2
    control->m2_generation = 0u;
#endif
#ifdef LIVE_USB_COMPILER
    control->compiler_generation = 0u;
#endif
    for (uint32_t i = 0u; i < 16u; ++i) control->serial[i] = serial[i];
    control->serial[16] = '\0';
    live_usb_control_reset(control);
}

void live_usb_control_setup(live_usb_control_t *control, const uint8_t setup[8]) {
#ifdef LIVE_USB_LOGI_HOST
    control->logi_fs_pending = 0u;
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
    control->keypad_out = control->keypad_pending = 0u;
#endif
#ifdef LIVE_USB_CLEAN
    if (control->clean_secret_out) {
        for (uint32_t i=0u;i<64u;++i) ((volatile uint8_t *)control->clean_secret)[i]=0u;
        control->clean_secret_ready=0u;
    }
    control->clean_secret_out=0u;
#endif
    uint8_t type = setup[0], request = setup[1];
    uint16_t value = read16(setup + 2u);
    uint16_t index = read16(setup + 4u);
    uint16_t length = read16(setup + 6u);
    control->action = USB_ACTION_NONE;
    control->address_pending = 0u;
    control->configuration_pending = 0u;
    control->reboot_pending = 0u;
    control->probe_pending = 0u;
    control->udp_out = control->udp_pending = 0u;
#ifdef LIVE_USB_COMPILER
    control->compiler_out = control->compiler_pending = 0u;
#endif
    control->phase = USB_PHASE_IDLE;

#ifdef LIVE_USB_LOGI_HOST
    if ((type & 0x60u) == 0x40u && request != LIVE_USB_LOGI_STATUS &&
        request != LIVE_USB_LOGI_PROBE && request != LIVE_USB_LOGI_FS_STATUS &&
        request != LIVE_USB_INFO && request != LIVE_USB_BOOTSEL) {
        stall(control);
        return;
    }
#endif
    if (type == 0x80u && request == 6u && length != 0u) {
        uint8_t descriptor = (uint8_t)(value >> 8u);
        uint8_t number = (uint8_t)value;
        if (descriptor == 1u && number == 0u && index == 0u) {
            send(control, device_descriptor, sizeof(device_descriptor), length);
        } else if (descriptor == 2u && number == 0u && index == 0u) {
            send(control, configuration_descriptor, sizeof(configuration_descriptor), length);
        } else if (descriptor == 3u && number == 0u && index == 0u) {
            const uint8_t languages[] = {4u, 3u, 9u, 4u};
            send(control, languages, sizeof(languages), length);
        } else if (descriptor == 3u && (index == 0x0409u || index == 0u) && number >= 1u && number <= 3u) {
            string_descriptor(control, number == 1u ? "Universe badge lab" :
#ifdef LIVE_USB_LOGI_HOST
                               number == 2u ? "U26 Logi Host Diagnostics" : control->serial, length);
#else
#ifdef LIVE_USB_KEYPAD_TRACE
                               number == 2u ? "U26 Keypad Trace" : control->serial, length);
#else
                               number == 2u ? "U26 Live Reports" : control->serial, length);
#endif
#endif
        } else stall(control);
    } else if (type == 0u && request == 5u && value < 128u &&
               index == 0u && length == 0u && control->configured == 0u) {
        control->pending_address = (uint8_t)value;
        control->address_pending = 1u;
        status_in(control);
    } else if (type == 0u && request == 9u && value <= 1u &&
               index == 0u && length == 0u && control->address != 0u) {
        control->pending_configuration = (uint8_t)value;
        control->configuration_pending = 1u;
        status_in(control);
    } else if (type == 0x80u && request == 8u && value == 0u && index == 0u && length == 1u) {
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
#ifdef LIVE_USB_NAMETAG_M2
    } else if (type == 0xc0u && request == LIVE_USB_M2_PAGE && control->configured &&
               value < NAMETAG_M2_PAGES && index == 0u && length == 64u && control->m2_valid) {
        uint8_t page[64];
        if (nametag_m2_page(page, control->m2_words, control->build_id, control->m2_nonce, value))
            send(control, page, 64u, length);
        else stall(control);
#endif
#ifdef LIVE_USB_UDP
    } else if (type==0xc0u && request==LIVE_USB_UDP_PAGE && control->configured &&
               value<WIFI_UDP_DETAIL_PAGES && index==0u && length==64u &&
               control->udp_valid && control->udp_frozen) {
        uint8_t page[64];
        wifi_udp_page(page,control->udp_detail,control->udp_report,(uint8_t)value);
        send(control,page,64u,length);
#endif
#ifndef LIVE_USB_COMPILER
    } else if (type == 0xc0u && request == LIVE_USB_WIFI_TRACE && control->configured &&
               value < WIFI_TRACE_PAGES && index == 0u && length == 64u &&
               control->report_valid && control->trace_valid &&
               control->report_test != USB_REPORT_TEST_ID) {
        uint8_t page[64];
        wifi_trace_page(page, control->trace, control->report, (uint8_t)value);
        send(control, page, sizeof(page), length);
#else
    } else if (type == 0xc0u && request == LIVE_USB_COMPILER_PAGE && control->configured &&
               index == 0u && value < COMPILER_SUITE_RECORDS && length == 64u &&
               control->compiler_valid) {
        send(control, control->compiler_records + value * 64u, 64u, length);
#endif
    } else if (control->configured && value == 0u && index == 0u) {
#ifdef LIVE_USB_LOGI_HOST
        if (type == 0x40u && request == LIVE_USB_LOGI_PROBE && length == 0u &&
            !control->logi_fs_ready) {
            control->logi_fs_pending = 1u;
            status_in(control);
            return;
        }
        if (type == 0xc0u && request == LIVE_USB_LOGI_FS_STATUS && length == 64u &&
            control->logi_fs_valid) {
            send(control, control->logi_fs_status, 64u, length);
            return;
        }
        if (type == 0xc0u && request == LIVE_USB_LOGI_STATUS && length == 64u &&
            control->logi_status_valid) {
            send(control, control->logi_status, 64u, length);
            return;
        }
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
        if (type == 0x40u && request == KEYPAD_TRACE_SUBMIT && length == 64u &&
            !control->keypad_ready) {
            control->keypad_out = 1u;
            control->length = 64u;
            control->action = USB_ACTION_OUT;
            control->phase = USB_PHASE_DATA_OUT;
            return;
        }
        if (type == 0xc0u && request == KEYPAD_TRACE_STATUS && length == 64u &&
            control->keypad_status_valid) {
            send(control, control->keypad_status, 64u, length);
            return;
        }
#endif
#ifdef LIVE_USB_CLEAN
        if (type==0x40u && request==LIVE_USB_CLEAN_SECRET && length==64u && !control->probe_inflight) {
            for (uint32_t i=0u;i<64u;++i) ((volatile uint8_t *)control->clean_secret)[i]=0u;
            control->clean_secret_ready=0u; control->clean_secret_out=1u;
            control->length=64u; control->action=USB_ACTION_OUT; control->phase=USB_PHASE_DATA_OUT;
        } else
#endif
#ifdef LIVE_USB_NAMETAG_M2
        if (type == 0x40u && request == LIVE_USB_M2_SUBMIT && length == 64u) {
            cancel_m2(control);
            control->m2_out = 1u;
            control->length = 64u;
            control->action = USB_ACTION_OUT;
            control->phase = USB_PHASE_DATA_OUT;
        } else
#endif
#ifdef LIVE_USB_COMPILER
        if (type == 0x40u && request == LIVE_USB_COMPILER_SUBMIT && length == 64u) {
            cancel_compiler(control);
            control->compiler_out = 1u;
            control->length = 64u;
            control->action = USB_ACTION_OUT;
            control->phase = USB_PHASE_DATA_OUT;
        } else
#endif
#ifdef LIVE_USB_UDP
        if (type==0x40u && request==LIVE_USB_UDP_START && length==64u &&
            control->probe_inflight && control->incoming_test==WIFI_LINK_TEST_ID) {
            cancel_udp(control);
            control->udp_out=1u;
            control->length=64u;
            control->action=USB_ACTION_OUT;
            control->phase=USB_PHASE_DATA_OUT;
        } else if (type==0xc0u && length==64u && control->udp_valid &&
                   (request==LIVE_USB_UDP_REPORT ||
                    (request==LIVE_USB_UDP_FREEZE && control->udp_complete))) {
            if (request==LIVE_USB_UDP_FREEZE) control->udp_frozen=1u;
            send(control,control->udp_report,64u,length);
        } else
#endif
        if (type == 0x40u && (request == LIVE_USB_SUBMIT
#ifndef LIVE_USB_COMPILER
#ifndef LIVE_USB_LINK
                             || request == LIVE_USB_WIFI_SUBMIT || request == LIVE_USB_WIFI_PIO_SUBMIT
#else
                             || request == LIVE_USB_LINK_AP || request == LIVE_USB_LINK_STATION
#ifndef LIVE_USB_UDP
                             || request == LIVE_USB_LINK_OPEN_AP || request == LIVE_USB_LINK_OPEN_STATION ||
                                request == LIVE_USB_LINK_WPA2_FIXTURE
#endif
#endif
#endif
#ifdef LIVE_USB_FIRMWARE
                             || request == LIVE_USB_FW_SUBMIT
#endif
                             ) &&
            length == USB_REPORT_WIRE_SIZE) {
            control->probe_pio = request == LIVE_USB_WIFI_PIO_SUBMIT;
            control->incoming_test = request == LIVE_USB_SUBMIT ? USB_REPORT_TEST_ID : WIFI_USB_TEST_ID;
#ifdef LIVE_USB_FIRMWARE
            if (request == LIVE_USB_FW_SUBMIT) control->incoming_test = WIFI_FW_TEST_ID;
#endif
#ifdef LIVE_USB_LINK
            if (request == LIVE_USB_LINK_AP || request == LIVE_USB_LINK_STATION ||
                request == LIVE_USB_LINK_OPEN_AP || request == LIVE_USB_LINK_OPEN_STATION ||
                request == LIVE_USB_LINK_WPA2_FIXTURE) {
                control->incoming_test = WIFI_LINK_TEST_ID;
                control->probe_role = request == LIVE_USB_LINK_WPA2_FIXTURE ? WIFI_LINK_FIXTURE_AP :
                                      request == LIVE_USB_LINK_OPEN_AP ? WIFI_LINK_OPEN_AP :
                                      request == LIVE_USB_LINK_OPEN_STATION ? WIFI_LINK_OPEN_STATION :
                                      request == LIVE_USB_LINK_AP ? WIFI_LINK_AP : WIFI_LINK_STATION;
            }
#endif
            control->report_valid = 0u;
            cancel_udp(control);
            control->trace_valid = 0u;
            control->probe_ready = 0u;
            control->probe_inflight = 0u;
            ++control->probe_generation;
            if (control->probe_generation == 0u) ++control->probe_generation;
            control->length = USB_REPORT_WIRE_SIZE;
            control->action = USB_ACTION_OUT;
            control->phase = USB_PHASE_DATA_OUT;
        } else if (type == 0xc0u && length == USB_REPORT_WIRE_SIZE && control->report_valid &&
                   ((request == LIVE_USB_REPORT && control->report_test == USB_REPORT_TEST_ID) ||
                    (request == LIVE_USB_WIFI_REPORT && control->report_test == WIFI_USB_TEST_ID)
#ifdef LIVE_USB_FIRMWARE
                    || (request == LIVE_USB_FW_REPORT && control->report_test == WIFI_FW_TEST_ID)
#endif
#ifdef LIVE_USB_LINK
                    || (request == LIVE_USB_LINK_REPORT && control->report_test == WIFI_LINK_TEST_ID)
#endif
                    )) {
            send(control, control->report, sizeof(control->report), length);
        } else if (type == 0x40u && request == LIVE_USB_BOOTSEL && length == 0u) {
            control->reboot_pending = 1u;
            status_in(control);
        } else if (type == 0xc0u && request == LIVE_USB_INFO && length == 12u) {
            uint8_t info[12] = {'U','2','6','L', 1u, 0u, 64u, 0u};
            for (uint32_t i = 0u; i < 4u; ++i) info[8u + i] = (uint8_t)(control->build_id >> (8u * i));
            send(control, info, sizeof(info), length);
        } else stall(control);
    } else stall(control);
}

void live_usb_control_complete(live_usb_control_t *control, uint8_t in,
                                const uint8_t *data, uint16_t length) {
    control->action = USB_ACTION_NONE;
    if (control->phase == USB_PHASE_DATA_IN && in && length == control->length) {
        control->phase = USB_PHASE_STATUS_OUT;
        control->action = USB_ACTION_OUT;
        control->length = 0u;
    } else if (control->phase == USB_PHASE_STATUS_OUT && !in && length == 0u) {
        control->phase = USB_PHASE_IDLE;
    } else if (control->phase == USB_PHASE_STATUS_IN && in && length == 0u) {
        control->phase = USB_PHASE_IDLE;
#ifdef LIVE_USB_LOGI_HOST
        if (control->logi_fs_pending) {
            control->logi_fs_pending = 0u;
            control->logi_fs_valid = 0u;
            control->logi_fs_ready = 1u;
        }
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
        if (control->keypad_pending) {
            control->keypad_pending = 0u;
            control->keypad_ready = 1u;
        }
#endif
#ifdef LIVE_USB_CLEAN
        if (control->clean_secret_out==2u) {
            control->clean_secret_out=0u;
            control->clean_secret_ready=1u;
        }
#endif
        if (control->address_pending) {
            control->address = control->pending_address;
            control->address_pending = 0u;
            control->action = USB_ACTION_ADDRESS;
        }
        if (control->configuration_pending) {
#ifdef LIVE_USB_LOGI_HOST
            control->logi_fs_pending = control->logi_fs_ready = control->logi_fs_valid = 0u;
#endif
#ifdef LIVE_USB_KEYPAD_TRACE
            control->keypad_out = control->keypad_pending = control->keypad_ready = 0u;
            control->keypad_status_valid = 0u;
#endif
#ifdef LIVE_USB_CLEAN
            for (uint32_t i=0u;i<64u;++i) ((volatile uint8_t *)control->clean_secret)[i]=0u;
            control->clean_secret_ready=control->clean_secret_out=0u;
#endif
            cancel_udp(control);
#ifdef LIVE_USB_NAMETAG_M2
            cancel_m2(control);
#endif
#ifdef LIVE_USB_COMPILER
            cancel_compiler(control);
#endif
            control->configured = control->pending_configuration;
            control->configuration_pending = 0u;
            control->report_valid = 0u;
            control->trace_valid = 0u;
            control->probe_ready = 0u;
            control->probe_inflight = 0u;
            ++control->probe_generation;
        }
        if (control->probe_pending) {
            control->probe_pending = 0u;
            control->probe_ready = 1u;
        }
#ifdef LIVE_USB_NAMETAG_M2
        if (control->m2_pending) {
            control->m2_pending = 0u;
            control->m2_ready = 1u;
        }
#endif
        if (control->udp_pending) {
            control->udp_pending=0u;
            control->udp_ready=1u;
        }
#ifdef LIVE_USB_COMPILER
        if (control->compiler_pending) {
            control->compiler_pending = 0u;
            control->compiler_ready = 1u;
        }
#endif
        if (control->reboot_pending) {
            control->reboot_requested = 1u;
            control->reboot_pending = 0u;
        }
    } else if (control->phase == USB_PHASE_DATA_OUT && !in &&
               length == USB_REPORT_WIRE_SIZE && data != 0) {
#ifdef LIVE_USB_KEYPAD_TRACE
        if (control->keypad_out) {
            control->keypad_out = 0u;
            if (!keypad_trace_wire_valid(data) ||
                (data[6] == KEYPAD_TRACE_RECORD && data[7] != 1u)) {
                stall(control);
                return;
            }
            badger_bytes_copy(control->keypad_record, data, 64u);
            control->keypad_pending = 1u;
            status_in(control);
            return;
        }
#endif
#ifdef LIVE_USB_CLEAN
        if (control->clean_secret_out==1u) {
            control->clean_secret_out=2u;
            badger_bytes_copy(control->clean_secret,data,64u);
            status_in(control);
            return;
        }
#endif
#ifdef LIVE_USB_NAMETAG_M2
        if (control->m2_out) {
            control->m2_out = 0u;
            if (usb_report_check_request_for_test(data, length, control->build_id,
                                                  NAMETAG_M2_TEST) != USB_RECORD_OK) {
                stall(control);
                return;
            }
            badger_bytes_copy(control->m2_nonce, data + 16u, 16u);
            control->m2_pending = 1u;
            status_in(control);
            return;
        }
#endif
#ifdef LIVE_USB_COMPILER
        if (control->compiler_out) {
            control->compiler_out = 0u;
            if (usb_report_check_request_for_test(data, length, control->build_id,
                                                  COMPILER_SUITE_ID) != USB_RECORD_OK) {
                stall(control);
                return;
            }
            badger_bytes_copy(control->compiler_nonce, data + 16u, 16u);
            control->compiler_pending = 1u;
            status_in(control);
            return;
        }
#endif
#ifdef LIVE_USB_UDP
        if (control->udp_out) {
            usb_record_result_t result=usb_report_check_request_for_test(data,length,control->build_id,WIFI_LINK_TEST_ID);
            for (uint32_t i=0u;result==USB_RECORD_OK && i<16u;++i) {
                if (data[16u+i]!=control->probe_nonce[i]) result=USB_RECORD_NONCE;
            }
            control->udp_out=0u;
            if (result!=USB_RECORD_OK) { stall(control); return; }
            control->udp_pending=1u;
            status_in(control);
            return;
        }
#endif
        usb_record_result_t result = usb_report_check_request_for_test(data, length, control->build_id,
                                                                       control->incoming_test);
        if (result != USB_RECORD_OK) {
            stall(control);
            return;
        }
        if (control->incoming_test != USB_REPORT_TEST_ID) {
            for (uint32_t i = 0u; i < 16u; ++i) control->probe_nonce[i] = data[16u + i];
            control->probe_pending = 1u;
        } else {
            usb_report_response(control->report, control->build_id, data + 16u,
                                USB_REPORT_OK, 0xfeedc0deu);
            control->report_test = USB_REPORT_TEST_ID;
            control->report_valid = 1u;
            ++control->requests;
        }
        status_in(control);
    } else stall(control);
}

uint32_t live_usb_probe_take(live_usb_control_t *control, uint8_t nonce[16]) {
    if (!control->probe_ready || !control->configured || control->reboot_requested) return 0u;
    control->probe_ready = 0u;
    control->probe_inflight = 1u;
    for (uint32_t i = 0u; i < 16u; ++i) nonce[i] = control->probe_nonce[i];
    return control->probe_generation;
}

void live_usb_probe_finish(live_usb_control_t *control, uint32_t generation,
                           const uint8_t report[64]) {
    if (!control->probe_inflight || generation == 0u || generation != control->probe_generation ||
        !control->configured || control->reboot_requested) return;
    control->probe_inflight = 0u;
    for (uint32_t i = 0u; i < 64u; ++i) control->report[i] = report[i];
    control->report_test = control->incoming_test;
    control->report_valid = 1u;
    ++control->requests;
}

void live_usb_trace_finish(live_usb_control_t *control, uint32_t generation,
                           const uint8_t trace[WIFI_TRACE_SIZE]) {
    if (control->trace_valid || !control->report_valid || control->report_test == USB_REPORT_TEST_ID ||
        generation == 0u || generation != control->probe_generation ||
        !control->configured || control->reboot_requested) return;
    for (uint32_t i = 0u; i < WIFI_TRACE_SIZE; ++i) control->trace[i] = trace[i];
    control->trace_valid = 1u;
}

#ifdef LIVE_USB_LINK
void live_usb_link_details(live_usb_control_t *control, uint32_t generation,
                            const uint8_t detail[WIFI_TRACE_SIZE]) {
    if (!control->probe_inflight || generation != control->probe_generation || !control->report_valid ||
        !control->configured || control->reboot_requested || control->incoming_test != WIFI_LINK_TEST_ID) return;
    badger_bytes_copy(control->trace, detail, WIFI_TRACE_SIZE);
    control->trace_valid = 1u;
}

void live_usb_link_publish(live_usb_control_t *control, uint32_t generation,
                            const uint8_t report[64]) {
    if (!control->probe_inflight || generation == 0u || generation != control->probe_generation ||
        !control->configured || control->reboot_requested || control->incoming_test != WIFI_LINK_TEST_ID) return;
    badger_bytes_copy(control->report, report, 64u);
    control->report_test = WIFI_LINK_TEST_ID;
    control->report_valid = 1u;
    ++control->requests;
}
#endif

#ifdef LIVE_USB_UDP
uint32_t live_usb_udp_take(live_usb_control_t *control) {
    if (!control->udp_ready || !control->configured || control->reboot_requested) return 0u;
    control->udp_ready=0u;
    control->udp_running=1u;
    return control->udp_generation;
}

void live_usb_udp_publish(live_usb_control_t *control, uint32_t link_generation,
                          uint32_t udp_generation, const uint8_t report[64],
                          const uint8_t detail[512], uint8_t complete) {
    if (!control->udp_running || control->udp_frozen || udp_generation==0u ||
        udp_generation!=control->udp_generation || link_generation!=control->probe_generation ||
        !control->configured || control->reboot_requested) return;
    badger_bytes_copy(control->udp_report, report, 64u);
    badger_bytes_copy(control->udp_detail, detail, 512u);
    control->udp_complete=complete;
    control->udp_valid=1u;
}
#endif
