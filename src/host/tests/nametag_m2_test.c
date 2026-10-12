#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../usb/live_usb.h"
#include "../../shared/nametag_measure.h"
#include "../../tools/diagnostics_logic.h"

static void crc(uint8_t packet[64]) {
    uint32_t value = diag_crc32(packet, 60u);
    for (uint32_t i = 0u; i < 4u; ++i) packet[60u + i] = (uint8_t)(value >> (i * 8u));
}

static void setup(live_usb_control_t *usb, uint8_t type, uint8_t request, uint16_t value, uint16_t length) {
    uint8_t wire[8] = {type, request, (uint8_t)value, (uint8_t)(value >> 8u), 0, 0,
                      (uint8_t)length, (uint8_t)(length >> 8u)};
    live_usb_control_setup(usb, wire);
}

int main(void) {
    uint32_t words[NAMETAG_M2_WORDS], decoded[NAMETAG_M2_WORDS], stats[4];
    uint8_t nonce[16], wire[64];
    for (uint32_t i = 0u; i < 16u; ++i) nonce[i] = (uint8_t)(i + 1u);
    for (uint32_t i = 0u; i < NAMETAG_M2_WORDS; ++i) words[i] = 0xffffffffu - i * 131071u;
    for (uint32_t page = 0u; page < NAMETAG_M2_PAGES; ++page) {
        assert(nametag_m2_page(wire, words, 123u, nonce, page));
        assert(nametag_m2_page_decode(decoded, wire, 64u, 123u, nonce, page) == USB_RECORD_OK);
    }
    assert(memcmp(words, decoded, sizeof(words)) == 0);
    assert(!nametag_m2_page(wire, words, 123u, nonce, NAMETAG_M2_PAGES));
    assert(nametag_m2_page(wire, words, 123u, nonce, 0u));
    assert(nametag_m2_page_decode(decoded, wire, 63u, 123u, nonce, 0u) == USB_RECORD_LENGTH);
    assert(nametag_m2_page_decode(decoded, wire, 64u, 124u, nonce, 0u) == USB_RECORD_BUILD);
    wire[16] ^= 1u; crc(wire);
    assert(nametag_m2_page_decode(decoded, wire, 64u, 123u, nonce, 0u) == USB_RECORD_NONCE);
    assert(nametag_m2_page(wire, words, 123u, nonce, 0u));
    wire[32] = 1u; crc(wire);
    assert(nametag_m2_page_decode(decoded, wire, 64u, 123u, nonce, 0u) == USB_RECORD_PAYLOAD);
    assert(nametag_m2_page(wire, words, 123u, nonce, NAMETAG_M2_PAGES - 1u));
    wire[59] = 1u; crc(wire);
    assert(nametag_m2_page_decode(decoded, wire, 64u, 123u, nonce, NAMETAG_M2_PAGES - 1u) == USB_RECORD_RESERVED);
    wire[60] ^= 1u;
    assert(nametag_m2_page_decode(decoded, wire, 64u, 123u, nonce, NAMETAG_M2_PAGES - 1u) == USB_RECORD_CRC);
    uint32_t samples[32];
    for (uint32_t i = 0u; i < 32u; ++i) samples[i] = 32u - i;
    assert(nametag_m2_statistics(samples, 32u, stats));
    assert(stats[0] == 1u && stats[1] == 16u && stats[2] == 32u && stats[3] == 32u && samples[0] == 32u);
    assert(!nametag_m2_statistics(samples, 0u, stats));
    assert(!nametag_m2_statistics(samples, 33u, stats));
    live_usb_control_t usb;
    memset(&usb, 0, sizeof(usb));
    live_usb_control_init(&usb, 123u, "9BBF88DD17D66E7D");
    usb.address = 1u; usb.configured = 1u;
    setup(&usb, 0x40u, LIVE_USB_M2_SUBMIT, 0u, 64u);
    usb_report_request_for_test(wire, 123u, nonce, NAMETAG_M2_TEST);
    live_usb_control_complete(&usb, 0u, wire, 64u);
    assert(usb.m2_pending && !usb.m2_ready && !usb.m2_valid);
    live_usb_control_complete(&usb, 1u, 0, 0u);
    assert(usb.m2_ready && !usb.m2_pending);
    uint32_t generation = usb.m2_generation;
    usb.m2_ready = 0u;
    setup(&usb, 0xc0u, LIVE_USB_M2_PAGE, 0u, 64u);
    assert(usb.action == USB_ACTION_STALL && usb.m2_generation == generation);
    memcpy(usb.m2_words, words, sizeof(words));
    usb.m2_valid = 1u;
    setup(&usb, 0xc0u, LIVE_USB_M2_PAGE, 0u, 64u);
    assert(usb.action == USB_ACTION_IN && usb.length == 64u);
    assert(nametag_m2_page_decode(decoded, usb.data, 64u, 123u, nonce, 0u) == USB_RECORD_OK);
    setup(&usb, 0x40u, LIVE_USB_M2_SUBMIT, 0u, 64u);
    assert(usb.m2_generation != generation && !usb.m2_valid);
    wire[60] ^= 1u;
    live_usb_control_complete(&usb, 0u, wire, 64u);
    assert(usb.action == USB_ACTION_STALL && !usb.m2_pending && !usb.m2_ready);
    live_usb_control_reset(&usb);
    assert(!usb.m2_valid && !usb.m2_ready && !usb.configured);
    puts("M2: exact paged shape/CRC/build/nonce/padding, statistics, status-ACK gating, cancellation and reset PASS");
    return 0;
}
