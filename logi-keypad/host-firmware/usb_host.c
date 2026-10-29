#include "usb_host.h"
#include "../../src/shared/bytes.h"

static uint32_t read_usb(usb_host_t *h, uint32_t offset) {
    return h->io.read(h->io.context, HOST_USB_BASE + offset);
}

static void write_usb(usb_host_t *h, uint32_t offset, uint32_t value) {
    h->io.write(h->io.context, HOST_USB_BASE + offset, value);
}

static uint32_t now(usb_host_t *h) { return h->io.now(h->io.context); }

host_result_t usb_host_wait(usb_host_t *h, uint32_t us) {
    uint32_t start = now(h);
    for (uint32_t loops = 0u; loops < 2000000u; ++loops) {
        if (!h->io.service(h->io.context)) return HOST_CANCELLED;
        if (now(h) - start >= us) return HOST_OK;
    }
    return HOST_TIMEOUT;
}

void usb_host_off(usb_host_t *h) {
    write_usb(h, 0x40u, 0u);
    write_usb(h, HOST_SIE_CTRL, 0u);
    write_usb(h, 0x78u, 2u);
    h->sie = 0u;
}

void usb_host_init(usb_host_t *h, const host_io_t *io) {
    badger_bytes_zero((uint8_t *)h, sizeof(*h));
    badger_bytes_copy((uint8_t *)&h->io, (const uint8_t *)io, sizeof(h->io));
    h->ep0_size = 8u;
    usb_host_off(h);
}

void usb_host_start(usb_host_t *h) {
    usb_host_off(h);
    for (uint32_t offset = 0u; offset < 4096u; offset += 4u)
        h->io.write(h->io.context, HOST_DPRAM_BASE + offset, 0u);
    write_usb(h, 0x74u, 9u);
    write_usb(h, 0x90u, 0u);
    write_usb(h, 0x54u, 0u);
    write_usb(h, HOST_NAK_POLL, HOST_STOP_ON_NAK | (16u << 16u) | 16u | HOST_STOPPED_NAK);
    h->sie = HOST_PULLDOWN;
    write_usb(h, HOST_SIE_CTRL, h->sie);
    write_usb(h, 0x40u, 3u);
    h->address = 0u;
    h->ep0_size = 8u;
    h->stop_failed = 0u;
}

uint32_t usb_host_speed(usb_host_t *h) {
    return (read_usb(h, HOST_SIE_STATUS) >> 8u) & 3u;
}

host_result_t usb_host_reset(usb_host_t *h) {
    if (usb_host_speed(h) != 2u) return HOST_SPEED_ERROR;
    h->sie = HOST_PULLDOWN;
    write_usb(h, HOST_SIE_CTRL, h->sie | HOST_RESET);
    host_result_t result = usb_host_wait(h, 50000u);
    if (result != HOST_OK) return result;
    uint32_t start = now(h);
    for (uint32_t loops = 0u;; ++loops) {
        if (!h->io.service(h->io.context)) return HOST_CANCELLED;
        if (!(read_usb(h, HOST_SIE_CTRL) & HOST_RESET)) break;
        if (now(h) - start >= 100000u || loops >= 2000000u) return HOST_TIMEOUT;
    }
    h->address = 0u;
    h->ep0_size = 8u;
    write_usb(h, 0x44u, 0u);
    h->sie |= HOST_SOF;
    write_usb(h, HOST_SIE_CTRL, h->sie);
    return usb_host_wait(h, 10000u);
}

static void put_data(usb_host_t *h, uint32_t offset, const uint8_t *data, uint16_t length) {
    for (uint32_t i = 0u; i < length; i += 4u) {
        uint32_t word = 0u;
        for (uint32_t j = 0u; j < 4u && i + j < length; ++j)
            word |= (uint32_t)data[i + j] << (8u * j);
        h->io.write(h->io.context, HOST_DPRAM_BASE + offset + i, word);
    }
}

static void get_data(usb_host_t *h, uint8_t *data, uint16_t length) {
    for (uint32_t i = 0u; i < length; i += 4u) {
        uint32_t word = h->io.read(h->io.context, HOST_DPRAM_BASE + HOST_EPX_DATA + i);
        for (uint32_t j = 0u; j < 4u && i + j < length; ++j)
            data[i + j] = (uint8_t)(word >> (8u * j));
    }
}

static host_result_t stop_packet(usb_host_t *h, host_result_t result) {
    write_usb(h, HOST_SIE_CTRL, h->sie | HOST_STOP);
    uint32_t start = now(h);
    for (uint32_t loops = 0u;; ++loops) {
        if (!(read_usb(h, HOST_SIE_CTRL) & HOST_STOP)) break;
        if (now(h) - start >= 1000u || loops >= 100000u) {
            h->stop_failed = 1u;
            return HOST_STOP_TIMEOUT;
        }
    }
    /* STOP must reach the USB clock domain before buffer ownership is revoked. */
    start = now(h);
    for (uint32_t loops = 0u; now(h) - start < 1u; ++loops)
        if (loops >= 100000u) { h->stop_failed = 1u; return HOST_STOP_TIMEOUT; }
    h->io.write(h->io.context, HOST_DPRAM_BASE + HOST_EPX_BUFFER, 0u);
    write_usb(h, HOST_SIE_CTRL, h->sie);
    return result;
}

host_result_t usb_host_packet(usb_host_t *h, host_packet_kind_t kind,
                              uint8_t endpoint, uint8_t interrupt, uint8_t pid,
                              uint8_t *data, uint16_t length, uint16_t *received) {
    if (kind > HOST_OUT || endpoint > 15u || pid > 1u || interrupt > 1u ||
        length > 64u || (length && !data) || !received ||
        (kind == HOST_SETUP && (endpoint || length != 8u || interrupt || pid))) return HOST_ARGUMENT;
    *received = 0u;
    if (h->stop_failed) return HOST_STOP_TIMEOUT;
    if (usb_host_speed(h) != 2u) return HOST_DETACHED;
    if (!(h->sie & HOST_SOF)) return HOST_ARGUMENT;
    write_usb(h, HOST_SIE_STATUS, 0xff841000u);
    write_usb(h, 0x58u, 1u);
    write_usb(h, HOST_NAK_POLL, HOST_STOP_ON_NAK | (16u << 16u) | 16u | HOST_STOPPED_NAK);
    write_usb(h, 0u, h->address | (uint32_t)endpoint << 16u);
    h->io.write(h->io.context, HOST_DPRAM_BASE + HOST_EPX_CONTROL,
                (1u << 31u) | (1u << 29u) | (interrupt ? 3u << 26u : 0u) | HOST_EPX_DATA);
    uint32_t buffer = HOST_LAST | (pid ? HOST_PID : 0u) | length;
    if (kind == HOST_SETUP) put_data(h, 0u, data, length);
    else if (kind == HOST_OUT) { put_data(h, HOST_EPX_DATA, data, length); buffer |= HOST_FULL; }
    h->io.write(h->io.context, HOST_DPRAM_BASE + HOST_EPX_BUFFER, buffer);
    host_result_t result = usb_host_wait(h, 1u);
    if (result != HOST_OK) return result;
    if (kind != HOST_SETUP)
        h->io.write(h->io.context, HOST_DPRAM_BASE + HOST_EPX_BUFFER, buffer | HOST_AVAILABLE);
    uint32_t command = kind == HOST_SETUP ? 2u : kind == HOST_IN ? 8u : 4u;
    write_usb(h, HOST_SIE_CTRL, h->sie | command);
    result = usb_host_wait(h, 1u);
    if (result != HOST_OK) return result;
    write_usb(h, HOST_SIE_CTRL, h->sie | command | HOST_START);
    uint32_t started = now(h);
    for (uint32_t loops = 0u;; ++loops) {
        if (!h->io.service(h->io.context)) return stop_packet(h, HOST_CANCELLED);
        uint32_t status = read_usb(h, HOST_SIE_STATUS);
        h->last_status = status;
        h->last_buffer = h->io.read(h->io.context, HOST_DPRAM_BASE + HOST_EPX_BUFFER);
        if (((status >> 8u) & 3u) != 2u) return stop_packet(h, HOST_DETACHED);
        if (status & (1u << 29u)) return stop_packet(h, HOST_STALL);
        if (status & HOST_ERRORS) return stop_packet(h, HOST_BUS_ERROR);
        if ((status & (1u << 28u)) || (read_usb(h, HOST_NAK_POLL) & HOST_STOPPED_NAK)) {
            ++h->naks;
            return stop_packet(h, HOST_NAK);
        }
        if (status & HOST_COMPLETE) {
            uint16_t got = (uint16_t)(h->last_buffer & 0x3ffu);
            if (kind != HOST_SETUP && ((h->last_buffer & HOST_AVAILABLE) ||
                got > length || (kind == HOST_IN && !(h->last_buffer & HOST_FULL)) ||
                (kind == HOST_OUT && ((h->last_buffer & HOST_FULL) || got != length))))
                return stop_packet(h, HOST_BUFFER_ERROR);
            if (kind == HOST_IN && got) get_data(h, data, got);
            *received = kind == HOST_SETUP ? 8u : got;
            ++h->packets;
            write_usb(h, HOST_SIE_CTRL, h->sie);
            return HOST_OK;
        }
        if (now(h) - started >= 10000u || loops >= 2000000u)
            return stop_packet(h, HOST_TIMEOUT);
    }
}

static host_result_t control_packet(usb_host_t *h, host_packet_kind_t kind,
                                     uint8_t pid, uint8_t *data, uint16_t length,
                                     uint16_t *received, uint32_t started) {
    for (;;) {
        if (now(h) - started >= 1000000u) return HOST_TIMEOUT;
        host_result_t result = usb_host_packet(h, kind, 0u, 0u, pid, data, length, received);
        if (result != HOST_NAK) return result;
        result = usb_host_wait(h, 1000u);
        if (result != HOST_OK) return result;
    }
}

host_result_t usb_host_control(usb_host_t *h, const uint8_t setup[8],
                               uint8_t *data, uint16_t capacity, uint16_t *received) {
    if (!setup) return HOST_ARGUMENT;
    uint16_t length = (uint16_t)((uint16_t)setup[6] | (uint16_t)setup[7] << 8u);
    if (!received || length > capacity || (length && !data) ||
        (h->ep0_size != 8u && h->ep0_size != 16u && h->ep0_size != 32u && h->ep0_size != 64u))
        return HOST_ARGUMENT;
    *received = 0u;
    uint8_t packet[8];
    badger_bytes_copy(packet, setup, 8u);
    uint16_t got;
    uint32_t started = now(h);
    host_result_t result = control_packet(h, HOST_SETUP, 0u, packet, 8u, &got, started);
    if (result != HOST_OK) return result;
    uint8_t in = setup[0] & 0x80u, pid = 1u;
    while (*received < length) {
        uint16_t size = (uint16_t)(length - *received);
        if (size > h->ep0_size) size = h->ep0_size;
        result = control_packet(h, in ? HOST_IN : HOST_OUT, pid,
                                  data + *received, size, &got, started);
        if (result != HOST_OK) return result;
        *received = (uint16_t)(*received + got);
        pid ^= 1u;
        if (got < size) break;
    }
    result = control_packet(h, in ? HOST_OUT : HOST_IN, 1u, 0, 0u, &got, started);
    return result;
}

const char *usb_host_error(host_result_t result) {
    switch (result) {
    case HOST_OK: return "OK";
    case HOST_NAK: return "NAK / NO NEW DATA";
    case HOST_ARGUMENT: return "INVALID TRANSFER";
    case HOST_TIMEOUT: return "USB TIMEOUT";
    case HOST_DETACHED: return "DEVICE DISCONNECTED";
    case HOST_STALL: return "ENDPOINT STALL";
    case HOST_BUS_ERROR: return "USB BUS/PID/CRC ERROR";
    case HOST_CANCELLED: return "HOME OR EXTERNAL VBUS LOST";
    case HOST_BUFFER_ERROR: return "INVALID BUFFER RESULT";
    case HOST_STOP_TIMEOUT: return "CONTROLLER STOP TIMEOUT";
    case HOST_SPEED_ERROR: return "FULL SPEED REQUIRED";
    case HOST_DESCRIPTOR: return "UNSUPPORTED DESCRIPTORS";
    case HOST_WRONG_DEVICE: return "NOT LOGITECH 046D:C354";
    case HOST_HID_ERROR: return "HID++ ERROR REPLY";
    case HOST_REPLY_TIMEOUT: return "HID++ REPLY TIMEOUT";
    case HOST_HID_REPLY: return "HID++ READBACK MISMATCH";
    case HOST_HUB: return "USB HUBS ARE NOT SUPPORTED";
    case HOST_RECOVERY: return "BOOTSEL WATCHDOG RENEW FAILED";
    }
    return "UNKNOWN HOST ERROR";
}
