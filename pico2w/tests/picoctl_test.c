#include "../tools/picoctl_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void checksum(uint8_t *record) {
    usb_store_u32(record + 60, usb_crc32(record, 60));
}

int main(void) {
    char serial[17];
    uint8_t bytes[32], nonce[16] = {1}, record[64];
    const uint32_t build = 0xd1828acdu, chip[2] = {0xb9492c77u, 0x7c4918bau};
    assert(picoctl_serial("7c4918bab9492c77", serial));
    assert(strcmp(serial, "7C4918BAB9492C77") == 0);
    assert(!picoctl_serial("7C4918BAB9492C7Z", serial));
    assert(!picoctl_serial("7C4918BAB9492C77X", serial));
    assert(!picoctl_hex("0x12", bytes, 1));
    assert(picoctl_hex("d1828acd", bytes, 4));
    usb_report_response(record, build, nonce, chip, 10);
    assert(picoctl_identity(record, build, nonce, serial));
    for (unsigned i = 0; i < 64; ++i) {
        record[i] ^= 1;
        assert(!picoctl_identity(record, build, nonce, serial));
        record[i] ^= 1;
    }
    nonce[0] = 2;
    assert(!picoctl_identity(record, build, nonce, serial));
    nonce[0] = 1;
    assert(!picoctl_identity(record, build + 1, nonce, serial));
    assert(!picoctl_identity(record, build, nonce, "7C4918BAB9492C78"));
    memset(record, 0, sizeof(record));
    usb_store_u32(record, 0x53573250u);
    record[4] = 1;
    record[6] = 64;
    usb_store_u32(record + 8, build);
    usb_store_u32(record + 12, 7);
    usb_store_u32(record + 20, 3);
    usb_store_u32(record + 28, 0xc0a8b232u);
    usb_store_u32(record + 32, (uint32_t)-3003);
    usb_store_u32(record + 52, 1);
    checksum(record);
    picoctl_status_t status;
    assert(picoctl_status(record, build, &status));
    assert(status.lora_error == -3003);
    assert(picoctl_network_ready(&status));
    for (unsigned i = 0; i < 64; ++i) {
        record[i] ^= 1;
        assert(!picoctl_status(record, build, &status));
        record[i] ^= 1;
    }
    for (unsigned offset = 16; offset <= 24; offset += 8) {
        usb_store_u32(record + offset, 1);
        checksum(record);
        assert(picoctl_status(record, build, &status));
        assert(!picoctl_network_ready(&status));
        usb_store_u32(record + offset, 0);
    }
    const uint32_t bad_ips[] = {0, 0x7f000001u, 0xe0000001u, 0xffffffffu};
    for (unsigned i = 0; i < sizeof(bad_ips) / sizeof(bad_ips[0]); ++i) {
        usb_store_u32(record + 28, bad_ips[i]);
        checksum(record);
        assert(picoctl_status(record, build, &status));
        assert(!picoctl_network_ready(&status));
    }
    memset(record, 0, sizeof(record));
    usb_store_u32(record, 0x44573250u);
    record[4] = 1;
    record[6] = 64;
    usb_store_u32(record + 8, build);
    usb_store_u32(record + 12, 14);
    usb_store_u32(record + 36, 7);
    checksum(record);
    assert(picoctl_wifi_diagnostic(record, build));
    assert(!picoctl_wifi_diagnostic(record, build + 1));
    for (unsigned i = 0; i < 64; ++i) {
        record[i] ^= 1;
        assert(!picoctl_wifi_diagnostic(record, build));
        record[i] ^= 1;
    }
    const unsigned invalid_offsets[] = {12, 36, 40};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t saved = usb_load_u32(record + invalid_offsets[i]);
        usb_store_u32(record + invalid_offsets[i], 99);
        checksum(record);
        assert(!picoctl_wifi_diagnostic(record, build));
        usb_store_u32(record + invalid_offsets[i], saved);
    }
    uint8_t block[512] = {0};
    const uint32_t header[] = {0x0a324655, 0x9e5d5157, 0x2000, 0x10000000,
                              256, 0, 1, 0xe48bff59};
    for (unsigned i = 0; i < 8; ++i) usb_store_u32(block + i * 4, header[i]);
    usb_store_u32(block + 508, 0x0ab16f30);
    assert(picoctl_uf2_block(block, 0, 1));
    for (unsigned offset = 0; offset < 32; ++offset) {
        block[offset] ^= 1;
        assert(!picoctl_uf2_block(block, 0, 1));
        block[offset] ^= 1;
    }
    assert(!picoctl_uf2_block(block, 1, 1));
    assert(!picoctl_uf2_block(block, 0, 0));
    assert(!picoctl_uf2_block(block, 0, 16385));
    puts("picoctl protocol tests passed");
    return 0;
}
