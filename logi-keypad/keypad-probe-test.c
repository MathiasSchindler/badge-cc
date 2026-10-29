#define main keypad_probe_main
#include "keypad-probe.c"
#undef main
#include <assert.h>

static void assert_event_text(int key, const char *expected) {
    FILE *out = tmpfile();
    assert(out);
    print_button_event(out, key);
    assert(fflush(out) == 0);
    rewind(out);
    char text[128];
    assert(fgets(text, sizeof(text), out));
    assert(strcmp(text, expected) == 0);
    assert(fgetc(out) == EOF && !ferror(out));
    assert(fclose(out) == 0);
}

static void test_button_capture(void) {
    /* All 18 reports from the user's 2026-10-29 row-major capture. */
    const uint8_t capture[][32] = {
        { 0x13, 0xff, 2, 0, 0xc7, 1, 1 },
        { 0x13, 0xff, 2, 0, 0xc8, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xc9, 1, 2 },
        { 0x13, 0xff, 2, 0, 0xca, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xcb, 1, 3 },
        { 0x13, 0xff, 2, 0, 0xcc, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xcd, 1, 4 },
        { 0x13, 0xff, 2, 0, 0xce, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xcf, 1, 5 },
        { 0x13, 0xff, 2, 0, 0xc0, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xc1, 1, 6 },
        { 0x13, 0xff, 2, 0, 0xc2, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xc3, 1, 7 },
        { 0x13, 0xff, 2, 0, 0xc4, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xc5, 1, 8 },
        { 0x13, 0xff, 2, 0, 0xc6, 1, 0 },
        { 0x13, 0xff, 2, 0, 0xc7, 1, 9 },
        { 0x13, 0xff, 2, 0, 0xc8, 1, 0 }
    };
    for (size_t i = 0; i < sizeof(capture) / sizeof(capture[0]); ++i) {
        int expected = i % 2 ? 0 : (int)(i / 2 + 1);
        assert(decode_button(0x13, capture[i], sizeof(capture[i])) == expected);
    }
    uint8_t report[33] = { 0x13, 0xff, 2, 0, 0xc7, 1, 1 };
    for (size_t size = 0; size < 32; ++size) {
        assert(decode_button(0x13, report, size) == -1);
    }
    assert(decode_button(0x13, report, 33) == -1);
    assert(decode_button(0x11, report, 32) == -1);
    assert(decode_button(0x13, NULL, 32) == -1);
    const size_t fixed_fields[] = { 0, 1, 2, 3, 5 };
    for (size_t i = 0; i < sizeof(fixed_fields) / sizeof(fixed_fields[0]); ++i) {
        report[fixed_fields[i]] ^= 1;
        assert(decode_button(0x13, report, 32) == -1);
        report[fixed_fields[i]] ^= 1;
    }
    for (size_t i = 7; i < 32; ++i) {
        report[i] = 1;
        assert(decode_button(0x13, report, 32) == -1);
        report[i] = 0;
    }
    for (unsigned key = 10; key <= 255; ++key) {
        report[6] = (uint8_t)key;
        assert(decode_button(0x13, report, 32) == -1);
    }
    report[6] = 1;
    for (unsigned opaque = 0; opaque <= 255; ++opaque) {
        report[4] = (uint8_t)opaque;
        assert(decode_button(0x13, report, 32) == 1);
    }
    assert_event_text(1, "  decoded: press key=1 row=1 column=1\n");
    assert_event_text(5, "  decoded: press key=5 row=2 column=2\n");
    assert_event_text(9, "  decoded: press key=9 row=3 column=3\n");
    assert_event_text(0, "  decoded: release (no key; released key not encoded)\n");
    assert_event_text(-1, "  decoded: unknown (raw report preserved)\n");
}

static void test_saved_capture(const char *path) {
    FILE *capture = fopen(path, "r");
    assert(capture);
    char line[1024];
    unsigned events = 0;
    while (fgets(line, sizeof(line), capture)) {
        const char *input = strstr(line, "input id=");
        if (!input) continue;
        unsigned report_id;
        size_t length;
        int offset = 0;
        assert(sscanf(input, "input id=0x%x length=%zu raw=%n",
                      &report_id, &length, &offset) == 2);
        assert(offset > 0 && length == 32);
        const char *hex = input + offset;
        uint8_t report[32];
        for (size_t i = 0; i < sizeof(report); ++i) {
            unsigned byte;
            int used = 0;
            assert(sscanf(hex, "%2x%n", &byte, &used) == 1 && used > 0);
            assert(byte <= 255);
            report[i] = (uint8_t)byte;
            hex += used;
        }
        while (*hex == ' ' || *hex == '\n' || *hex == '\r') ++hex;
        assert(*hex == '\0');
        int expected = events % 2 ? 0 : (int)(events / 2 + 1);
        assert(events < 18);
        assert(decode_button(report_id, report, length) == expected);
        ++events;
    }
    assert(!ferror(capture) && events == 18);
    assert(fclose(capture) == 0);
    puts("Saved capture replay passed: 9 presses and 9 releases, keys 1..9.");
}

static void test_navigation_protocol(void) {
    uint8_t report[32] = { 0x11, 0xff, 0x0b, 0, 0x01, 0xa1 };
    assert(decode_navigation(0x0b, 0x11, report, 20) == 1);
    report[5] = 0xa2;
    assert(decode_navigation(0x0b, 0x11, report, 20) == 2);
    report[6] = 1;
    report[7] = 0xa1;
    assert(decode_navigation(0x0b, 0x11, report, 20) == 3);
    report[7] = 0xa2;
    assert(decode_navigation(0x0b, 0x11, report, 20) == -1);
    memset(report + 4, 0, sizeof(report) - 4);
    assert(decode_navigation(0x0b, 0x11, report, 20) == 0);
    report[6] = 1;
    report[7] = 0xa1;
    assert(decode_navigation(0x0b, 0x11, report, 20) == -1);
    report[4] = 1;
    report[5] = 0xa3;
    assert(decode_navigation(0x0b, 0x11, report, 20) == -1);
    memset(report + 4, 0, sizeof(report) - 4);
    report[3] = 0x31;
    assert(decode_navigation(0x0b, 0x11, report, 20) == -1);
    report[3] = 0;
    report[2] = 0x23;
    assert(decode_navigation(0x23, 0x11, report, 20) == 0);
    assert(decode_navigation(0x0b, 0x11, report, 20) == -1);
    assert(decode_navigation(0, 0x11, report, 20) == -1);
    assert(decode_navigation(0x23, 0x13, report, 20) == -1);
    assert(decode_navigation(0x23, 0x11, NULL, 20) == -1);
    for (size_t size = 0; size < 20; ++size) {
        assert(decode_navigation(0x23, 0x11, report, size) == -1);
    }
    assert(decode_navigation(0x23, 0x11, report, 21) == -1);

    uint8_t request[20] = { 0x11, 0xff, 0x0b, 0x31, 1, 0xa1, 3 };
    memcpy(report, request, sizeof(request));
    assert(match_hidpp_reply(request, 0x11, report, 20) == 1);
    report[3] = 0;
    assert(match_hidpp_reply(request, 0x11, report, 20) == 0);
    report[3] = 0x32;
    assert(match_hidpp_reply(request, 0x11, report, 20) == 0);
    report[3] = 0x31;
    report[2] = 0x0c;
    assert(match_hidpp_reply(request, 0x11, report, 20) == 0);
    report[2] = 0xff;
    report[3] = 0x0b;
    report[4] = 0x31;
    report[5] = 9;
    assert(match_hidpp_reply(request, 0x11, report, 20) == -1);
    report[1] = 0xfe;
    assert(match_hidpp_reply(request, 0x11, report, 20) == 0);
    memcpy(report, request, sizeof(request));
    report[0] = 0x13;
    assert(match_hidpp_reply(request, 0x13, report, 32) == 1);
    assert(match_hidpp_reply(request, 0x11, report, 20) == 0);
    assert(match_hidpp_reply(request, 0x13, report, 31) == 0);
    assert(match_hidpp_reply(request, 0x11, NULL, 20) == 0);

    FILE *out = tmpfile();
    assert(out);
    for (int active = 0; active < 4; ++active) print_navigation_event(out, active);
    assert(fflush(out) == 0);
    rewind(out);
    const char *expected[] = {
        "  decoded: navigation active=none (release)\n",
        "  decoded: navigation active=left\n",
        "  decoded: navigation active=right\n",
        "  decoded: navigation active=left+right\n"
    };
    char text[128];
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        assert(fgets(text, sizeof(text), out) && strcmp(text, expected[i]) == 0);
    }
    assert(fgetc(out) == EOF && !ferror(out));
    assert(fclose(out) == 0);
}

static void test_lcd_images(void) {
    uint8_t pixels[LCD_PIXELS_SIZE];
    const unsigned samples[7][2] = {
        { 50, 24 }, { 82, 40 }, { 82, 70 }, { 50, 94 },
        { 34, 70 }, { 34, 40 }, { 50, 58 }
    };
    const uint8_t masks[] = { 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
    for (unsigned key = 1; key <= 9; ++key) {
        assert(lcd_pixels(key, pixels));
        for (size_t i = 3; i < sizeof(pixels); i += 4) assert(pixels[i] == 255);
        for (unsigned bit = 0; bit < 7; ++bit) {
            size_t offset = (samples[bit][1] * LCD_KEY_SIZE + samples[bit][0]) * 4;
            int white = pixels[offset] == 255 && pixels[offset + 1] == 255 &&
                        pixels[offset + 2] == 255;
            assert(white == !!(masks[key - 1] & (1u << bit)));
        }
        CFDataRef jpeg = lcd_jpeg(key);
        assert(jpeg);
        CFIndex size = CFDataGetLength(jpeg);
        const uint8_t *bytes = CFDataGetBytePtr(jpeg);
        assert(size > 4 && size <= 65535 && bytes[0] == 0xff && bytes[1] == 0xd8);
        assert(bytes[size - 2] == 0xff && bytes[size - 1] == 0xd9);
        CGImageSourceRef source = CGImageSourceCreateWithData(jpeg, NULL);
        assert(source && CFEqual(CGImageSourceGetType(source), CFSTR("public.jpeg")));
        CGImageRef decoded = CGImageSourceCreateImageAtIndex(source, 0, NULL);
        assert(decoded && CGImageGetWidth(decoded) == 118 && CGImageGetHeight(decoded) == 118);
        printf("LCD key %u offline JPEG: %ld bytes, decoded 118x118.\n", key, (long)size);
        CGImageRelease(decoded);
        CFRelease(source);
        CFRelease(jpeg);
    }

    uint8_t image[65535], packet[LCD_PACKET_SIZE];
    for (size_t i = 0; i < sizeof(image); ++i) image[i] = (uint8_t)i;
    const size_t sizes[] = { 1, 4075, 4076, 8165, 8166, 65535 };
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        size_t offset = 0;
        unsigned part = 1;
        while (offset < sizes[s]) {
            size_t count = lcd_packet(packet, 9, image, sizes[s], offset, part);
            assert(count > 0);
            assert(packet[0] == 0x14 && packet[1] == 0xff &&
                   packet[2] == 2 && packet[3] == 0x2b);
            assert((packet[4] & 0x1f) == part);
            assert(!!(packet[4] & 0x80) == (part == 1));
            assert(!!(packet[4] & 0x40) == (offset + count == sizes[s]));
            size_t header = part == 1 ? 20 : 5;
            assert(memcmp(packet + header, image + offset, count) == 0);
            for (size_t i = header + count; i < sizeof(packet); ++i) assert(packet[i] == 0);
            if (part == 1) {
                assert(packet[9] == 1 && packet[10] == 0x53);
                assert(packet[11] == 1 && packet[12] == 0x42);
                assert(packet[13] == 0 && packet[14] == 118);
                assert(packet[15] == 0 && packet[16] == 118);
                assert(packet[18] == sizes[s] >> 8 && packet[19] == (sizes[s] & 255));
            }
            offset += count;
            ++part;
        }
        assert(offset == sizes[s] && part <= 18);
    }
    assert(lcd_packet(packet, 1, image, 1, 0, 1) == 1);
    assert(packet[4] == 0xe1 && packet[9] == 0 && packet[10] == 23 &&
           packet[11] == 0 && packet[12] == 6);
    uint8_t request[20];
    memcpy(request, packet, sizeof(request));
    /* The device's reply sequence is not an echo of the output fragment flags. */
    uint8_t ack[32] = { 0x13, 0xff, 2, 0x2b, 0xc1, 0 };
    assert(match_hidpp_reply(request, 0x13, ack, sizeof(ack)) == 1);
    ack[4] = 0xc2;
    assert(match_hidpp_reply(request, 0x13, ack, sizeof(ack)) == 1);
    ack[3] = 0;
    assert(match_hidpp_reply(request, 0x13, ack, sizeof(ack)) == 0);
    ack[3] = 0x2b;
    ack[5] = 0xfb;
    assert(match_hidpp_reply(request, 0x13, ack, sizeof(ack)) == 1);
    fputs("The next LCD validation errors are expected.\n", stderr);
    assert(!lcd_pixels(0, pixels));
    assert(!lcd_pixels(10, pixels));
    assert(!lcd_pixels(1, NULL));
    assert(!lcd_packet(packet, 0, image, 1, 0, 1));
    assert(!lcd_packet(packet, 1, image, 0, 0, 1));
    assert(!lcd_packet(packet, 1, image, 65536, 0, 1));
    assert(!lcd_packet(packet, 1, image, 1, 0, 0));
    assert(!lcd_packet(packet, 1, image, 65535, 4076, 2));
}

int main(int argc, char **argv) {
    assert(argc == 1 || argc == 2);
    test_button_capture();
    test_navigation_protocol();
    test_lcd_images();
    const char *invalid[] = {
        "", "0", "-1", "+1", " 1", "1 ", "1x", "1.5", "3601",
        "99999999999999999999999999999999999999"
    };
    unsigned seconds = 123;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!parse_seconds(invalid[i], &seconds));
        assert(seconds == 123);
    }
    assert(parse_seconds("1", &seconds) && seconds == 1);
    assert(parse_seconds("3600", &seconds) && seconds == 3600);
    assert(parse_seconds("0003", &seconds) && seconds == 3);

    uint8_t word[] = { 0x34, 0x12 };
    assert(little16(word) == 0x1234);
    int value = 32;
    int64_t actual = 0;
    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &value);
    assert(n && number(n, &actual) && actual == 32);
    CFRelease(n);
    assert(!number(NULL, &actual));
    assert(!number(CFSTR("32"), &actual));

    const uint8_t config[] = {
        9, 2, 41, 0, 1, 1, 4, 0x80, 180,
        9, 4, 0, 0, 2, 3, 0, 0, 0,
        9, 0x21, 0x11, 1, 0, 1, 0x22, 140, 0,
        7, 5, 0x81, 3, 32, 0, 1,
        7, 5, 1, 3, 0, 4, 1
    };
    assert(print_configuration(config, sizeof(config)));
    uint8_t other_speed[sizeof(config)];
    memcpy(other_speed, config, sizeof(config));
    other_speed[1] = 7;
    other_speed[33] = 5;
    other_speed[38] = 64;
    other_speed[39] = 0;
    other_speed[40] = 5;
    assert(print_configuration(other_speed, sizeof(other_speed)));
    const uint8_t truncated[] = { 7, 5, 0x81 };
    const uint8_t zero_length[] = { 0, 5 };
    const uint8_t short_length[] = { 1, 5 };
    const uint8_t missing_type[] = { 7 };
    fputs("The next four malformed-descriptor errors are expected.\n", stderr);
    assert(!print_configuration(truncated, sizeof(truncated)));
    assert(!print_configuration(zero_length, sizeof(zero_length)));
    assert(!print_configuration(short_length, sizeof(short_length)));
    assert(!print_configuration(missing_type, sizeof(missing_type)));
    if (argc == 2) test_saved_capture(argv[1]);
    puts("Probe unit tests passed.");
    return 0;
}
