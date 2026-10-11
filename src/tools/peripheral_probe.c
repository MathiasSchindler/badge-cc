#include "display.h"
#include "i2c_bitbang.h"

#define TIMER0_TIMELR (*(volatile uint32_t *)(0x400b0000u + 0x0cu))
#define MAX_DEVICES 8u
#define MSA301_ADDRESS 0x26u
#define MSA301_PART_ID_REGISTER 0x01u
#define MSA301_EXPECTED_PART_ID 0x13u

static const uint8_t font_letters[26][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
    {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12},
    {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
    {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
    {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
};

static const uint8_t font_digits[10][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
    {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
    {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
};

static badger_i2c_result_t bus_result;
static uint8_t devices[MAX_DEVICES];
static uint32_t device_count;
static int device_overflow;
static int msa_found;
static uint8_t msa_part_id;
static uint16_t msa_x;
static uint16_t msa_y;
static uint16_t msa_z;
static int pad_found;
static int pad_initialized;
static uint8_t pad_address;
static uint16_t pad_raw;
static uint16_t pad_changed;

static uint32_t time_us(void) {
    return TIMER0_TIMELR;
}

static void wait_us(uint32_t duration) {
    uint32_t start = time_us();
    while (time_us() - start < duration) {}
}

static char hex_digit(uint32_t value) {
    value &= 0x0fu;
    return value < 10u ? (char)('0' + value) :
                         (char)('A' + value - 10u);
}

static void hex2(char output[3], uint32_t value) {
    output[0] = hex_digit(value >> 4u);
    output[1] = hex_digit(value);
    output[2] = '\0';
}

static void hex4(char output[5], uint32_t value) {
    output[0] = hex_digit(value >> 12u);
    output[1] = hex_digit(value >> 8u);
    output[2] = hex_digit(value >> 4u);
    output[3] = hex_digit(value);
    output[4] = '\0';
}

static uint8_t glyph_row(char character, uint32_t row) {
    if (character >= 'A' && character <= 'Z') {
        return font_letters[(uint32_t)(character - 'A')][row];
    }
    if (character >= '0' && character <= '9') {
        return font_digits[(uint32_t)(character - '0')][row];
    }
    return 0u;
}

static int text_pixel(const char *text, uint32_t x, uint32_t y,
                      uint32_t origin_x, uint32_t origin_y,
                      uint32_t scale) {
    if (x < origin_x || y < origin_y) return 0;
    uint32_t local_x = x - origin_x;
    uint32_t local_y = y - origin_y;
    if (local_y >= 7u * scale) return 0;
    uint32_t index = local_x / (6u * scale);
    uint32_t cell_x = (local_x / scale) % 6u;
    for (uint32_t position = 0; position < index; ++position) {
        if (text[position] == '\0') return 0;
    }
    char character = text[index];
    if (character == '\0' || cell_x >= 5u) return 0;
    return (glyph_row(character, local_y / scale) &
            (1u << (4u - cell_x))) != 0u;
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    return (uint16_t)(((uint16_t)(red & 0xf8u) << 8) |
                      ((uint16_t)(green & 0xfcu) << 3) |
                      ((uint16_t)blue >> 3));
}

static int address_list_pixel(uint32_t x, uint32_t y) {
    for (uint32_t index = 0; index < device_count; ++index) {
        char address[3];
        hex2(address, devices[index]);
        if (text_pixel(address, x, y, 72u + index * 28u, 52u, 1u)) {
            return 1;
        }
    }
    return 0;
}

static uint16_t page_pixel(uint32_t x, uint32_t y) {
    uint16_t background = rgb565(9u, 22u, 42u);
    uint16_t white = rgb565(248u, 250u, 252u);
    uint16_t green = rgb565(46u, 160u, 67u);
    uint16_t yellow = rgb565(210u, 153u, 34u);
    uint16_t red = rgb565(207u, 34u, 46u);
    char value[5];

    if (x < 6u || x >= BADGER_DISPLAY_WIDTH - 6u ||
        y < 6u || y >= BADGER_DISPLAY_HEIGHT - 6u) return green;
    if (text_pixel("I2C PERIPHERAL PROBE", x, y, 28u, 15u, 2u)) return white;
    if (text_pixel("BUS GPIO 04 05", x, y, 82u, 37u, 1u)) return white;

    if (bus_result != BADGER_I2C_OK) {
        if (text_pixel("BUS NOT IDLE HIGH", x, y, 42u, 95u, 2u)) return red;
        if (text_pixel("SCAN ABORTED", x, y, 78u, 135u, 2u)) return yellow;
        return background;
    }

    if (text_pixel("DEV", x, y, 25u, 52u, 1u) ||
        address_list_pixel(x, y)) return white;
    if (device_overflow &&
        text_pixel("MORE", x, y, 265u, 52u, 1u)) return yellow;
    if (device_count == 0u &&
        text_pixel("NO I2C DEVICES", x, y, 82u, 80u, 2u)) return red;
    if (device_count == 0u) {
        if (text_pixel("EXTERNAL BUS EMPTY", x, y, 52u, 125u, 2u)) {
            return yellow;
        }
        return background;
    }

    if (msa_found) {
        char id[3];
        hex2(id, msa_part_id);
        if (text_pixel("MSA301 ID", x, y, 25u, 80u, 2u) ||
            text_pixel(id, x, y, 210u, 80u, 2u)) {
            return msa_part_id == MSA301_EXPECTED_PART_ID ? green : yellow;
        }
        hex4(value, msa_x);
        if (text_pixel("X", x, y, 25u, 112u, 2u) ||
            text_pixel(value, x, y, 48u, 112u, 2u)) return white;
        hex4(value, msa_y);
        if (text_pixel("Y", x, y, 125u, 112u, 2u) ||
            text_pixel(value, x, y, 148u, 112u, 2u)) return white;
        hex4(value, msa_z);
        if (text_pixel("Z", x, y, 225u, 112u, 2u) ||
            text_pixel(value, x, y, 248u, 112u, 2u)) return white;
    } else if (text_pixel("MSA301 NOT FOUND", x, y, 55u, 80u, 2u)) {
        return yellow;
    }

    if (pad_found) {
        char address[3];
        hex2(address, pad_address);
        if (text_pixel("PAD CAND", x, y, 25u, 148u, 2u) ||
            text_pixel(address, x, y, 190u, 148u, 2u)) return green;
        hex4(value, pad_raw);
        if (text_pixel("RAW", x, y, 25u, 178u, 2u) ||
            text_pixel(value, x, y, 95u, 178u, 2u)) return white;
        hex4(value, pad_changed);
        if (text_pixel("CHG", x, y, 180u, 178u, 2u) ||
            text_pixel(value, x, y, 250u, 178u, 2u)) return yellow;
        if (text_pixel("PRESS LOWER CONTROLS", x, y, 42u, 211u, 2u)) {
            return white;
        }
    } else if (text_pixel("PAD CAND NOT FOUND", x, y, 42u, 165u, 2u)) {
        return yellow;
    }
    return background;
}

static void show_page(void) {
    badger_display_begin();
    for (uint32_t x = 0; x < BADGER_DISPLAY_WIDTH; ++x) {
        for (uint32_t y = 0; y < BADGER_DISPLAY_HEIGHT; ++y) {
            badger_display_write(page_pixel(x, y));
        }
    }
    badger_display_end();
}

static int is_pad_candidate(uint8_t address) {
    return address == 0x21u || address == 0x23u ||
           address == 0x25u || address == 0x27u;
}

static void scan_bus(void) {
    for (uint32_t address = 0x08u; address <= 0x77u; ++address) {
        uint8_t value;
        if (!badger_i2c_probe_read((uint8_t)address, &value)) continue;
        if (device_count < MAX_DEVICES) {
            devices[device_count++] = (uint8_t)address;
        } else {
            device_overflow = 1;
        }
        if (address == MSA301_ADDRESS) msa_found = 1;
        if (!pad_found && is_pad_candidate((uint8_t)address)) {
            pad_found = 1;
            pad_address = (uint8_t)address;
        }
    }
}

static void update_devices(void) {
    uint8_t data[6];
    if (msa_found &&
        badger_i2c_read_registers(MSA301_ADDRESS,
                                  MSA301_PART_ID_REGISTER,
                                  data, 1u)) {
        msa_part_id = data[0];
    }
    if (msa_found &&
        badger_i2c_read_registers(MSA301_ADDRESS, 0x02u, data, 6u)) {
        msa_x = (uint16_t)data[0] | (uint16_t)data[1] << 8u;
        msa_y = (uint16_t)data[2] | (uint16_t)data[3] << 8u;
        msa_z = (uint16_t)data[4] | (uint16_t)data[5] << 8u;
    }
    if (pad_found &&
        badger_i2c_read_registers(pad_address, 0x00u, data, 2u)) {
        uint16_t current = (uint16_t)data[0] | (uint16_t)data[1] << 8u;
        if (pad_initialized) {
            pad_changed |= current ^ pad_raw;
        } else {
            pad_initialized = 1;
        }
        pad_raw = current;
    }
}

void bare_main(void) {
    badger_display_init();
    bus_result = badger_i2c_init();
    if (bus_result == BADGER_I2C_OK) {
        scan_bus();
        update_devices();
    }
    show_page();

    while (bus_result == BADGER_I2C_OK) {
        wait_us(100000u);
        uint16_t old_pad = pad_raw;
        uint16_t old_x = msa_x;
        uint16_t old_y = msa_y;
        uint16_t old_z = msa_z;
        update_devices();
        if (pad_raw != old_pad || msa_x != old_x ||
            msa_y != old_y || msa_z != old_z) {
            show_page();
        }
    }

    while (1) {
        __asm__ volatile ("wfi");
    }
}
