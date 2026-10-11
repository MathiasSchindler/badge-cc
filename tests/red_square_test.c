#include <stdint.h>
#ifdef RED_SQUARE_TARGET_TEST
static int failed;
#define assert(condition) do { if (!(condition)) failed = 1; } while (0)
#else
#include <assert.h>
#include <stdio.h>
#endif

static uint32_t low, high, low_outputs, high_outputs;
static uint32_t pixels, red, black, commands_seen, delay_count;
static uint32_t x0, x1, y0, y1, x, y, current, payload, index, first;
static uint32_t pin_count;
static uint32_t control_index, control_remaining;
static int display_on;
static uint8_t frame[320][240];
static const uint8_t expected_control[] = {
    0x01, 0,
    0x3a, 1, 0x05,
    0xb2, 5, 0x0c, 0x0c, 0x00, 0x33, 0x33,
    0xc0, 1, 0x2c, 0xc2, 1, 0x01, 0xc3, 1, 0x0f, 0xc4, 1, 0x20,
    0xd0, 2, 0xa4, 0xa1, 0xc6, 1, 0x0f, 0xb0, 2, 0x00, 0xc0,
    0xb7, 1, 0x35, 0xbb, 1, 0x1b,
    0xe0, 14, 0xf0, 0x00, 0x06, 0x04, 0x05, 0x05, 0x31,
              0x44, 0x48, 0x36, 0x12, 0x12, 0x2b, 0x34,
    0xe1, 14, 0xf0, 0x0b, 0x0f, 0x0f, 0x0d, 0x26, 0x31,
              0x43, 0x47, 0x38, 0x14, 0x14, 0x2c, 0x32,
    0x21, 0, 0x11, 0,
    0x2a, 4, 0x00, 0x00, 0x00, 0xef,
    0x2b, 4, 0x00, 0x00, 0x01, 0x3f,
    0x36, 1, 0x90, 0x2c, 0,
    0x2a, 4, 0x00, 0x58, 0x00, 0x97,
    0x2b, 4, 0x00, 0x80, 0x00, 0xbf,
    0x2c, 0, 0x29, 0,
};

void red_square_delay(uint32_t cycles) {
    assert(cycles == (delay_count == 0u ? 6000000u : 4000000u));
    ++delay_count;
}

static void byte(uint32_t value) {
    assert((low & (1u << 27)) == 0u);
    assert((low & (1u << 26)) == 0u);
    assert((high & (1u << 9)) != 0u);
    if ((low & (1u << 28)) == 0u) {
        assert(control_remaining == 0u && control_index + 2u <= sizeof(expected_control));
        assert(expected_control[control_index++] == value);
        control_remaining = expected_control[control_index++];
        current = value;
        payload = index = first = 0;
        ++commands_seen;
        if (value == 0x2cu) { x = x0; y = y0; }
        if (value == 0x29u) display_on = 1;
    } else if (current == 0x2cu) {
        if (index++ % 2u == 0u) first = value;
        else {
            uint32_t pixel = (first << 8) | value;
            assert(pixel == 0u || pixel == 0xf800u);
            assert(x <= x1 && y <= y1 && x < 320u && y < 240u);
            frame[x][y] = pixel != 0u;
            ++pixels;
            if (pixel) ++red; else ++black;
            if (++y > y1) { y = y0; ++x; }
        }
    } else {
        assert(control_remaining != 0u && control_index < sizeof(expected_control));
        assert(expected_control[control_index++] == value);
        --control_remaining;
        if (current == 0x2au || current == 0x2bu) {
            payload = (payload << 8) | value;
            if (++index == 4u) {
                if (current == 0x2au) { y0 = payload >> 16; y1 = payload & 65535u; }
                else { x0 = payload >> 16; x1 = payload & 65535u; }
            }
        }
    }
}

void red_square_register(uint32_t address, uint32_t value) {
    if (address >= 0x40028000u && address < 0x40029000u) {
        assert(value == 5u);
        uint32_t pin = (address - 0x40028004u) / 8u;
        assert(pin >= 26u && pin <= 41u && pin != 29u && pin != 40u);
        ++pin_count;
        return;
    }
    if (address >= 0x40038000u && address < 0x40039000u) {
        assert(value == 0x21u);
        return;
    }
    switch (address - 0xd0000000u) {
    case 0x18:
        if ((value & (1u << 30)) != 0u && (low & (1u << 30)) == 0u &&
            (low_outputs & (1u << 30)) != 0u) {
            assert((high_outputs & 255u) == 255u);
            byte(high & 255u);
        }
        low |= value;
        break;
    case 0x1c: high |= value; break;
    case 0x20: low &= ~value; break;
    case 0x24: high &= ~value; break;
    case 0x38: low_outputs |= value; break;
    case 0x3c: high_outputs |= value; break;
    default: assert(0);
    }
}

void bare_main(void);
int main(void) {
    bare_main();
    assert(pin_count == 14u && delay_count == 2u && display_on);
    assert(commands_seen == 24u);
    assert(control_remaining == 0u && control_index == sizeof(expected_control));
    assert(pixels == 80896u && black == 76800u && red == 4096u);
    assert((low & ((1u << 26) | (1u << 27))) == ((1u << 26) | (1u << 27)));
    for (uint32_t x = 0; x < 320u; ++x)
        for (uint32_t y = 0; y < 240u; ++y)
            assert(frame[x][y] == (x >= 128u && x < 192u && y >= 88u && y < 152u));
#ifdef RED_SQUARE_TARGET_TEST
    return failed;
#else
    puts("red-square: exact 320x240 black frame, centered 64x64 RGB565 red square, GPIO and delays PASS");
#endif
}
