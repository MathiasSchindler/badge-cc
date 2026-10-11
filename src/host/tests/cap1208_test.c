#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../platform/badger/cap1208.h"
#include "../../platform/badger/i2c_bitbang.h"

static uint8_t registers[2][256];
static uint8_t present[2];
static uint8_t identified[2];
static uint32_t calls;
static uint32_t writes;
static uint32_t fail_call;
static int corrupt_reg;
static uint8_t corrupt_mask;
static int changed_reg;
static uint32_t changes_left;
static uint32_t status_reads;
static uint32_t delta_reads;
static uint32_t interrupt_clears;
static uint8_t main_before_clear;
static uint8_t main_written;
static uint8_t reassert_interrupt;
static uint8_t last_address;

static uint32_t device_index(uint8_t address) {
    assert(address == 0x28u || address == 0x29u);
    last_address = address;
    return address == 0x28u ? 0u : 1u;
}

int badger_i2c_read_registers(uint8_t address, uint8_t reg,
                             uint8_t *data, uint32_t length) {
    uint32_t device = device_index(address);
    ++calls;
    assert(length != 0u);
    assert((uint32_t)reg + length <= 256u);
    if (!present[device]) return 0;
    if (calls == fail_call) {
        /* The real bit-bang API may modify its destination before failing. */
        for (uint32_t i = 0u; i < length; ++i) data[i] = 0xeeu;
        return 0;
    }
    if (reg == 0x02u) {
        assert(length == 2u);
        ++status_reads;
    }
    if (reg == 0x10u) {
        assert(length == 8u);
        ++delta_reads;
        if (changes_left != 0u) {
            registers[device][changed_reg] ^= 0x01u;
            --changes_left;
        }
    }
    for (uint32_t i = 0u; i < length; ++i) {
        data[i] = registers[device][reg + i];
    }
    if (reg == 0xfdu) {
        assert(length == 3u);
        identified[device] = data[0] == 0x6bu && data[1] == 0x5du;
    }
    return 1;
}

int badger_i2c_write_register(uint8_t address, uint8_t reg, uint8_t value) {
    uint32_t device = device_index(address);
    ++calls;
    ++writes;
    assert(present[device]);
    assert(identified[device]);
    assert(reg == 0x00u || reg == 0x20u || reg == 0x21u ||
           reg == 0x27u || reg == 0x28u || reg == 0x2au ||
           reg == 0x2bu || reg == 0x44u || reg == 0x61u);
    if (calls == fail_call) return 0;
    if (reg == 0x00u && (value & 1u) == 0u) {
        assert(status_reads != 0u);
        ++interrupt_clears;
        main_before_clear = registers[device][reg];
        main_written = value;
        registers[device][0x02u] &= (uint8_t)~0x08u;
    }
    registers[device][reg] = value;
    if (reg == corrupt_reg) registers[device][reg] ^= corrupt_mask;
    if (reg == 0x00u && reassert_interrupt) registers[device][reg] |= 1u;
    return 1;
}

static void reset_mock(void) {
    memset(registers, 0, sizeof(registers));
    memset(present, 0, sizeof(present));
    memset(identified, 0, sizeof(identified));
    present[0] = 1u;
    for (uint32_t i = 0u; i < 2u; ++i) {
        registers[i][0xfdu] = 0x6bu;
        registers[i][0xfeu] = 0x5du;
        registers[i][0xffu] = 0xa7u;
        registers[i][0x00u] = 0x31u;
        registers[i][0x02u] = 0x08u;
        registers[i][0x1fu] = 0x2fu;
        registers[i][0x20u] = 0xb8u;
        registers[i][0x28u] = 0xffu;
        registers[i][0x2au] = 0x8cu;
        registers[i][0x2bu] = 0x8fu;
        registers[i][0x44u] = 0x6du;
        registers[i][0x61u] = 0x77u;
        for (uint32_t j = 0x30u; j <= 0x37u; ++j) {
            registers[i][j] = (uint8_t)(0x40u + j - 0x30u);
        }
    }
    calls = 0u;
    writes = 0u;
    fail_call = 0u;
    corrupt_reg = -1;
    corrupt_mask = 0u;
    changed_reg = -1;
    changes_left = 0u;
    status_reads = 0u;
    delta_reads = 0u;
    interrupt_clears = 0u;
    main_before_clear = 0u;
    main_written = 0u;
    reassert_interrupt = 0u;
    last_address = 0u;
}

static void assert_zero_sample(const cap1208_sample_t *sample) {
    assert(sample->touched == 0u);
    assert(sample->general_status == 0u);
    assert(sample->noise == 0u);
    assert(sample->calibrating == 0u);
    assert(sample->base_count_out_of_limit == 0u);
    assert(sample->main_control == 0u);
    for (uint32_t i = 0u; i < 8u; ++i) assert(sample->delta[i] == 0);
}

static void initialize(void) {
    uint8_t product = 0xffu;
    uint8_t revision = 0xffu;
    assert(cap1208_init(&product, &revision) == CAP1208_OK);
    assert(product == 0x6bu);
    assert(revision == 0xa7u);
}

static void test_identity(void) {
    uint8_t product;
    uint8_t revision;
    cap1208_sample_t sample;
    for (uint32_t device = 0u; device < 2u; ++device) {
        reset_mock();
        present[0] = device == 0u;
        present[1] = device == 1u;
        initialize();
        assert(cap1208_get_address() == 0x28u + device);
        assert(last_address == 0x28u + device);
        assert(writes == 10u);
        assert(cap1208_read(&sample) == CAP1208_OK);
        assert(last_address == 0x28u + device);
    }
    for (uint32_t reg = 0xfdu; reg <= 0xfeu; ++reg) {
        reset_mock();
        present[1] = 1u;
        registers[0][reg] ^= 0xffu;
        registers[1][reg] ^= 0xffu;
        product = revision = 0xffu;
        assert(cap1208_init(&product, &revision) == CAP1208_ID_MISMATCH);
        assert(cap1208_get_address() == 0u);
        assert(product == registers[1][0xfdu]);
        assert(revision == 0xa7u);
        assert(writes == 0u);
        assert(cap1208_read(&sample) == CAP1208_NOT_INITIALIZED);
        assert_zero_sample(&sample);
    }
    reset_mock();
    present[1] = 1u;
    registers[0][0xfdu] = 0x50u;
    initialize();
    assert(last_address == 0x29u);
    assert(!identified[0]);
    reset_mock();
    present[0] = 0u;
    product = revision = 0xffu;
    assert(cap1208_init(&product, &revision) == CAP1208_I2C_ERROR);
    assert(cap1208_get_address() == 0u);
    assert(product == 0u && revision == 0u && writes == 0u);
}

static void test_configuration(void) {
    static const uint8_t regs[] = {
        0x00u, 0x21u, 0x27u, 0x28u, 0x2au, 0x2bu, 0x20u, 0x44u, 0x61u
    };
    static const uint8_t corrupt_bits[] = {
        0x20u, 0x01u, 0x80u, 0x40u, 0x80u, 0x80u, 0x08u, 0x01u, 0x04u
    };
    uint8_t product;
    uint8_t revision;
    reset_mock();
    initialize();
    assert(registers[0][0x00u] == 0x00u);
    assert(registers[0][0x21u] == 0xffu);
    assert(registers[0][0x27u] == 0xffu);
    assert(registers[0][0x28u] == 0x00u);
    assert(registers[0][0x2au] == 0x0cu);
    assert(registers[0][0x2bu] == 0x0fu);
    assert(registers[0][0x20u] == 0xa0u);
    assert(registers[0][0x44u] == 0x72u);
    assert(registers[0][0x61u] == 0x33u);
    assert(registers[0][0x1fu] == 0x2fu);
    for (uint32_t j = 0x30u; j <= 0x37u; ++j) {
        assert(registers[0][j] == 0x40u + j - 0x30u);
    }
    assert(interrupt_clears == 1u);
    assert(main_before_clear == 1u && main_written == 0u);
    for (uint32_t i = 0u; i < sizeof(regs); ++i) {
        cap1208_sample_t sample;
        reset_mock();
        corrupt_reg = regs[i];
        corrupt_mask = corrupt_bits[i];
        assert(cap1208_init(&product, &revision) == CAP1208_CONFIG_ERROR);
        assert(cap1208_read(&sample) == CAP1208_NOT_INITIALIZED);
        assert_zero_sample(&sample);
    }
    reset_mock();
    reassert_interrupt = 1u;
    initialize();
    assert(registers[0][0x00u] == 1u);
}

static void test_init_failures(void) {
    uint8_t product;
    uint8_t revision;
    reset_mock();
    initialize();
    uint32_t initialization_calls = calls;
    for (uint32_t i = 1u; i <= initialization_calls; ++i) {
        cap1208_sample_t sample;
        reset_mock();
        fail_call = i;
        product = revision = 0xffu;
        assert(cap1208_init(&product, &revision) == CAP1208_I2C_ERROR);
        assert(product == (i == 1u ? 0u : 0x6bu));
        assert(revision == (i == 1u ? 0u : 0xa7u));
        if (i == 1u) assert(writes == 0u);
        assert(cap1208_read(&sample) == CAP1208_NOT_INITIALIZED);
        assert_zero_sample(&sample);
    }
}

static void test_samples(void) {
    static const uint8_t raw[] = {0x80u, 0x81u, 0xffu, 0u, 1u, 0x3fu, 0x7eu, 0x7fu};
    static const int8_t expected[] = {-128, -127, -1, 0, 1, 63, 126, 127};
    cap1208_sample_t sample;
    reset_mock();
    initialize();
    registers[0][0x02u] = 1u;
    registers[0][0x03u] = 0xa5u;
    registers[0][0x0au] = 0x18u;
    registers[0][0x00u] = 0xc1u;
    memcpy(&registers[0][0x10u], raw, sizeof(raw));
    uint32_t clears = interrupt_clears;
    assert(cap1208_read(&sample) == CAP1208_OK);
    assert(sample.touched == 0xa5u && sample.general_status == 1u);
    assert(sample.noise == 0x18u);
    assert(sample.main_control == 0xc1u);
    assert(main_before_clear == 0xc1u && main_written == 0xc0u);
    assert(interrupt_clears == clears + 1u);
    for (uint32_t i = 0u; i < 8u; ++i) assert(sample.delta[i] == expected[i]);
    for (uint32_t mask = 0u; mask <= 0xffu; ++mask) {
        registers[0][0x03u] = (uint8_t)mask;
        registers[0][0x02u] = mask != 0u;
        assert(cap1208_read(&sample) == CAP1208_OK);
        assert(sample.touched == mask);
    }
}

static void test_read_failures(void) {
    cap1208_sample_t sample;
    reset_mock();
    initialize();
    uint32_t start = calls;
    assert(cap1208_read(&sample) == CAP1208_OK);
    uint32_t reading_calls = calls - start;
    for (uint32_t i = 1u; i <= reading_calls; ++i) {
        reset_mock();
        initialize();
        uint32_t clears = interrupt_clears;
        fail_call = calls + i;
        memset(&sample, 0xa5, sizeof(sample));
        assert(cap1208_read(&sample) == CAP1208_I2C_ERROR);
        assert_zero_sample(&sample);
        assert(interrupt_clears == clears);
        fail_call = 0u;
        assert(cap1208_read(&sample) == CAP1208_OK);
    }
}

static void test_changing_status(void) {
    static const int changing[] = {0x02, 0x03, 0x0a, 0x26, 0x2e};
    cap1208_sample_t sample;
    for (uint32_t i = 0u; i < sizeof(changing) / sizeof(changing[0]); ++i) {
        reset_mock();
        initialize();
        changed_reg = changing[i];
        changes_left = 3u;
        uint32_t clears = interrupt_clears;
        memset(&sample, 0xa5, sizeof(sample));
        assert(cap1208_read(&sample) == CAP1208_NOT_READY);
        assert_zero_sample(&sample);
        assert(delta_reads == 3u);
        assert(interrupt_clears == clears);
    }
    reset_mock();
    initialize();
    changed_reg = 0x03;
    changes_left = 1u;
    assert(cap1208_read(&sample) == CAP1208_OK);
    assert(sample.touched == 1u);
    assert(delta_reads == 2u);
}

static void test_faults_and_calibration(void) {
    cap1208_sample_t sample;
    uint8_t product;
    uint8_t revision;
    for (uint32_t flag = 0x20u; flag <= 0x40u; flag <<= 1u) {
        reset_mock();
        registers[0][0x02u] |= (uint8_t)flag;
        assert(cap1208_init(&product, &revision) == CAP1208_SENSOR_FAULT);
        assert(cap1208_read(&sample) == CAP1208_SENSOR_FAULT);
        assert(sample.general_status == flag);
        assert(interrupt_clears == 2u);
    }
    reset_mock();
    initialize();
    registers[0][0x2eu] = 0x81u;
    assert(cap1208_read(&sample) == CAP1208_SENSOR_FAULT);
    assert(sample.base_count_out_of_limit == 0x81u);
    registers[0][0x2eu] = 0u;
    assert(cap1208_read(&sample) == CAP1208_OK);

    reset_mock();
    registers[0][0x26u] = 0xa5u;
    initialize();
    assert(cap1208_read(&sample) == CAP1208_NOT_READY);
    assert(sample.calibrating == 0xa5u);
    registers[0][0x26u] = 0u;
    assert(cap1208_read(&sample) == CAP1208_OK);

    reset_mock();
    initialize();
    registers[0][0x02u] = 0x08u;
    assert(cap1208_read(&sample) == CAP1208_CONFIG_ERROR);
    assert(sample.general_status == 0x08u);
    assert(cap1208_read(&sample) == CAP1208_NOT_INITIALIZED);
    assert_zero_sample(&sample);
    initialize();
    registers[0][0x00u] = 0x31u;
    assert(cap1208_read(&sample) == CAP1208_CONFIG_ERROR);
    assert(sample.main_control == 0x31u);
    assert(main_written == 0x30u);
    assert(cap1208_read(&sample) == CAP1208_NOT_INITIALIZED);
}

static void test_arguments_and_names(void) {
    uint8_t output = 0xffu;
    reset_mock();
    assert(cap1208_read(0) == CAP1208_INVALID_ARGUMENT);
    assert(cap1208_init(0, &output) == CAP1208_INVALID_ARGUMENT);
    assert(output == 0u);
    output = 0xffu;
    assert(cap1208_init(&output, 0) == CAP1208_INVALID_ARGUMENT);
    assert(output == 0u);
    assert(cap1208_init(0, 0) == CAP1208_INVALID_ARGUMENT);
    assert(calls == 0u);
    static const char *const names[] = {
        "OK", "I2C ERROR", "ID MISMATCH", "CONFIG ERROR", "SENSOR FAULT",
        "NOT READY", "NOT INITIALIZED", "INVALID ARGUMENT"
    };
    for (uint32_t i = 0u; i < sizeof(names) / sizeof(names[0]); ++i) {
        assert(strcmp(cap1208_result_name((cap1208_result_t)i), names[i]) == 0);
    }
    assert(strcmp(cap1208_result_name((cap1208_result_t)99), "UNKNOWN") == 0);
}

int main(void) {
    test_identity();
    test_configuration();
    test_init_failures();
    test_samples();
    test_read_failures();
    test_changing_status();
    test_faults_and_calibration();
    test_arguments_and_names();
    puts("cap1208 tests passed");
    return 0;
}
