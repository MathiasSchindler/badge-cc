#include "../tools/picoctl_protocol.h"
#include "../tools/meshtastic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void checksum(uint8_t *record) {
    usb_store_u32(record + 60, usb_crc32(record, 60));
}

static void from_hex(const char *text, uint8_t *out) {
    assert(picoctl_hex(text, out, (uint32_t)(strlen(text) / 2)));
}

static void crypto_tests(void) {
    uint8_t key[32], block[32], expected[32];
    mesh_aes_t aes;
    /* FIPS-197 appendix C.1 and C.3. */
    from_hex("000102030405060708090a0b0c0d0e0f", key);
    from_hex("00112233445566778899aabbccddeeff", block);
    assert(mesh_aes_init(&aes, key, 16));
    mesh_aes_encrypt(&aes, block, block);
    from_hex("69c4e0d86a7b0430d8cdb78070b4c55a", expected);
    assert(!memcmp(block, expected, 16));
    from_hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
    from_hex("00112233445566778899aabbccddeeff", block);
    assert(mesh_aes_init(&aes, key, 32));
    mesh_aes_encrypt(&aes, block, block);
    from_hex("8ea2b7ca516745bfeafc49904b496089", expected);
    assert(!memcmp(block, expected, 16));
    assert(!mesh_aes_init(&aes, key, 24));
    /* NIST SP 800-38A F.5.1 / F.5.5, two blocks each. */
    uint8_t iv[16];
    from_hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv);
    from_hex("2b7e151628aed2a6abf7158809cf4f3c", key);
    from_hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", block);
    mesh_ctr_iv(key, 16, iv, block, 32);
    from_hex("874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff", expected);
    assert(!memcmp(block, expected, 32));
    from_hex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key);
    from_hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51", block);
    mesh_ctr_iv(key, 32, iv, block, 32);
    from_hex("601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5", expected);
    assert(!memcmp(block, expected, 32));
}

static uint32_t text_frame(uint8_t *frame, const mesh_channel_t *channel, uint32_t to,
                           const char *message) {
    uint32_t size = (uint32_t)strlen(message);
    const uint8_t header[16] = {(uint8_t)to, (uint8_t)(to >> 8), (uint8_t)(to >> 16),
                                (uint8_t)(to >> 24), 0x44, 0x33, 0x22, 0x11,
                                0x88, 0x77, 0x66, 0x55, 0x63, channel->hash, 0, 0x44};
    memcpy(frame, header, 16);
    frame[16] = 0x08; frame[17] = 0x01; frame[18] = 0x12; frame[19] = (uint8_t)size;
    memcpy(frame + 20, message, size);
    mesh_ctr(channel->key, channel->key_length, 0x11223344u, 0x55667788u, frame + 16, 4 + size);
    return 20 + size;
}

static void meshtastic_tests(void) {
    mesh_channel_t channels[3];
    uint8_t bytes[48], frame[256];
    uint32_t size;
    char line[512];
    assert(mesh_base64_decode("AQ==", bytes, sizeof(bytes), &size) && size == 1 && bytes[0] == 1);
    assert(mesh_base64_decode("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", bytes,
                              sizeof(bytes), &size) && size == 32 && bytes[31] == 31);
    assert(!mesh_base64_decode("AQ=", bytes, sizeof(bytes), &size));
    assert(!mesh_base64_decode("A", bytes, sizeof(bytes), &size));
    assert(!mesh_base64_decode("AQ*=", bytes, sizeof(bytes), &size));
    assert(!mesh_base64_decode("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", bytes, 8, &size));
    /* The public default channel (LongFast, PSK AQ==) is channel hash 8 on air. */
    assert(mesh_channel_parse(channels, "  LongFast AQ==\r\n"));
    assert(channels[0].key_length == 16 && channels[0].key[0] == 0xd4 && channels[0].hash == 0x08);
    assert(mesh_channel_parse(channels + 1, "Test AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="));
    assert(channels[1].key_length == 32);
    assert(channels[1].hash == mesh_channel_hash("Test", channels[1].key, 32));
    assert(mesh_channel_parse(channels + 2, "Plain AA==") && channels[2].key_length == 0);
    assert(!mesh_channel_parse(channels + 2, "Missing"));
    assert(!mesh_channel_parse(channels + 2, "Two AQ== extra"));
    assert(!mesh_channel_parse(channels + 2, "Bad A*=="));

    size = text_frame(frame, channels + 1, MESH_BROADCAST, "Hallo Potsdam \xc3\xa4");
    assert(mesh_describe(frame, size, channels, 2, line, sizeof(line)));
    assert(strstr(line, "!11223344 -> ^all id=0x55667788 hop=3/3"));
    assert(strstr(line, "[Test] TEXT_MESSAGE_APP \"Hallo Potsdam \xc3\xa4\""));
    size = text_frame(frame, channels, 0xdeadbeefu, "x\n\"");
    assert(mesh_describe(frame, size, channels, 2, line, sizeof(line)));
    assert(strstr(line, "!11223344 -> !deadbeef") && strstr(line, "[LongFast] TEXT_MESSAGE_APP \"x?\\\"\""));
    /* Wrong key with the right hash must not decode as a valid Data message. */
    mesh_channel_t wrong = channels[1];
    wrong.key[0] ^= 1;
    size = text_frame(frame, channels + 1, MESH_BROADCAST, "secret");
    assert(!mesh_describe(frame, size, &wrong, 1, line, sizeof(line)));
    assert(strstr(line, "no matching channel key"));
    frame[13] = 0;
    assert(!mesh_describe(frame, size, channels, 2, line, sizeof(line)));
    assert(strstr(line, "^all") && strstr(line, "no matching channel key"));
    frame[0] = 1;
    assert(!mesh_describe(frame, size, channels, 2, line, sizeof(line)));
    assert(strstr(line, "PKI direct message"));
    assert(!mesh_describe(frame, 15, channels, 2, line, sizeof(line)) && strstr(line, "short frame"));

    mesh_data_t data;
    const uint8_t position[] = {0x08, 0x03, 0x12, 0x0e, 0x0d, 0x80, 0x04, 0xa3, 0x1e,
                                0x15, 0x00, 0xe1, 0xf5, 0x05, 0x18, 0x22, 0x70, 0x07,
                                0x48, 0x01};
    assert(mesh_data_decode(position, sizeof(position), &data) && data.portnum == 3);
    const uint8_t no_port[] = {0x12, 0x00};
    assert(!mesh_data_decode(no_port, sizeof(no_port), &data));
    const uint8_t wrong_wire[] = {0x0a, 0x00};
    assert(!mesh_data_decode(wrong_wire, sizeof(wrong_wire), &data));
    const uint8_t truncated[] = {0x08, 0x01, 0x12, 0x05, 0x41};
    assert(!mesh_data_decode(truncated, sizeof(truncated), &data));
    memcpy(frame + 16, position, sizeof(position));
    memcpy(frame, "\xff\xff\xff\xff\x01\x02\x03\x04\x05\x06\x07\x08\x03", 13);
    frame[13] = channels[2].hash;
    assert(mesh_describe(frame, 16 + sizeof(position), channels + 2, 1, line, sizeof(line)));
    assert(strstr(line, "POSITION_APP alt=34m sats=7 lat=51.40000 lon=10.00000"));
    assert(!strcmp(mesh_port_name(67), "TELEMETRY_APP") && !strcmp(mesh_port_name(500), "PORT"));
}

static void packet_record_tests(uint32_t build) {
    uint8_t record[PICOCTL_PACKET_SIZE] = {0};
    picoctl_packet_t packet;
    usb_store_u32(record, 0x50573250u);
    record[4] = 1;
    record[6] = PICOCTL_PACKET_SIZE & 255u;
    record[7] = PICOCTL_PACKET_SIZE >> 8;
    usb_store_u32(record + 8, build);
    usb_store_u32(record + 12, 1);
    usb_store_u32(record + 16, 9);
    usb_store_u32(record + 20, 2);
    usb_store_u32(record + 24, 5);
    usb_store_u32(record + 32, 869525000u);
    record[36] = 250;
    record[38] = 11;
    record[39] = 1;
    usb_store_u32(record + 40, (uint32_t)-400);
    usb_store_u32(record + 44, (uint32_t)-7);
    record[64] = 3;
    record[68] = 0xab;
    usb_store_u32(record + 324, usb_crc32(record, 324));
    assert(picoctl_packet(record, build, &packet));
    assert(packet.present && packet.newest == 9 && packet.oldest == 2 && packet.sequence == 5);
    assert(packet.frequency_hz == 869525000u && packet.bandwidth_khz == 250 && packet.sf == 11);
    assert(packet.profile == 1 && packet.rssi_qdb == -400 && packet.snr_qdb == -7);
    assert(packet.length == 3 && packet.payload[0] == 0xab);
    assert(!picoctl_packet(record, build + 1, &packet));
    for (unsigned i = 0; i < PICOCTL_PACKET_SIZE; i += 7) {
        record[i] ^= 0x10;
        assert(!picoctl_packet(record, build, &packet));
        record[i] ^= 0x10;
    }
    usb_store_u32(record + 24, 0);
    usb_store_u32(record + 324, usb_crc32(record, 324));
    assert(!picoctl_packet(record, build, &packet));
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
    crypto_tests();
    meshtastic_tests();
    packet_record_tests(build);
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
