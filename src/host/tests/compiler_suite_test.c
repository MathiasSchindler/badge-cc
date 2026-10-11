#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../tools/compiler_test/suite.h"
#include "../../tools/compiler_test/kernels.h"
#include "../../tools/diagnostics_logic.h"

static badger_canvas_t canvas;

typedef struct {
    uint32_t ticks, step, services, reads;
} test_clock_t;

static uint32_t test_clock(void *context) {
    test_clock_t *clock = context;
    ++clock->reads;
    clock->ticks += clock->step;
    return clock->ticks;
}

static void test_service(void *context) {
    test_clock_t *clock = context;
    assert((clock->reads & 1u) == 0u);
    ++clock->services;
}

static void test_kernels(uint32_t seed) {
    uint32_t source[COMPILER_KERNEL_WORK], dest[COMPILER_KERNEL_WORK];
    uint32_t a = seed, b = seed ^ 0x9e3779b9u, arithmetic = seed, call = seed;
    uint32_t unsigned_divrem = 0u, signed_divrem = 0u;
    uint32_t divisor = (seed & 127u) + 3u;
    int32_t signed_divisor = (int32_t)divisor;
    if ((seed & 0x80000000u) != 0u) signed_divisor = -signed_divisor;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) {
        source[i] = seed ^ (i * 0x9e3779b9u);
        dest[i] = 0x5a5a5a5au;
        arithmetic = (arithmetic + i) * 1664525u + 1013904223u;
        arithmetic ^= arithmetic >> 13u;
        if (((a ^ b) & 1u) != 0u) {
            a += b ^ i;
            b -= a;
        } else {
            a ^= b + i;
            b += a >> 3u;
        }
        call = (call ^ i) * 16777619u;
        unsigned_divrem += (source[i] / divisor) ^ (source[i] % divisor);
        int32_t dividend = (int32_t)(source[i] & 0x7fffffffu) - 1073741824;
        signed_divrem += (uint32_t)(dividend / signed_divisor) ^ (uint32_t)(dividend % signed_divisor);
    }
    uint32_t expected[COMPILER_KERNELS] = {arithmetic, a ^ b, call, unsigned_divrem,
                                          signed_divrem, source[COMPILER_KERNEL_WORK - 1u] +
                                          seed + COMPILER_KERNEL_WORK - 1u};
    for (uint32_t kernel = 0u; kernel < COMPILER_KERNELS; ++kernel) {
        compiler_result_t result;
        assert(compiler_kernel_run(kernel, seed, &result));
        assert(result.cases == COMPILER_KERNEL_WORK && result.values[0] == expected[kernel]);
        assert(result.values[1] == diag_crc32((const uint8_t *)source, sizeof(source)));
        if (kernel == 5u)
            for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) dest[i] = source[i] + seed + i;
        assert(result.values[2] == diag_crc32((const uint8_t *)dest, sizeof(dest)));
        assert(result.values[3] == 1u && result.values[4] == 1u && result.values[5] == COMPILER_KERNEL_WORK);
        test_clock_t clock = {0xfffffb00u, 1000u, 0u, 0u};
        uint32_t samples[COMPILER_TIMING_SAMPLES];
        assert(compiler_kernel_measure(kernel, seed, &result, test_clock, test_service, &clock, samples));
        assert(clock.services == COMPILER_TIMING_SAMPLES + 1u && clock.reads == COMPILER_TIMING_SAMPLES * 2u);
        for (uint32_t i = 0u; i < COMPILER_TIMING_SAMPLES; ++i) assert(samples[i] == 1000u);
        for (uint32_t lane = 0u; lane < 6u; ++lane) {
            result.values[lane] ^= 1u;
            assert(!compiler_kernel_measure(kernel, seed, &result, test_clock, test_service, &clock, samples));
            result.values[lane] ^= 1u;
        }
        for (uint32_t bound = 0u; bound < 4u; ++bound) {
            clock.step = bound == 0u ? 99u : bound == 1u ? 1000001u : bound == 2u ? 100u : 1000000u;
            assert(compiler_kernel_measure(kernel, seed, &result, test_clock, test_service, &clock, samples) ==
                   (bound >= 2u));
        }
    }
    compiler_result_t result;
    test_clock_t clock = {0u, 1000u, 0u, 0u};
    uint32_t samples[COMPILER_TIMING_SAMPLES];
    assert(!compiler_kernel_run(COMPILER_KERNELS, seed, &result));
    assert(!compiler_kernel_measure(COMPILER_KERNELS, seed, &result, test_clock, test_service, &clock, samples));
}

int main(void) {
    const uint32_t cases[6] = {256u, 8u, 64u, 128u, 64u, 76800u};
    const uint32_t seeds[4] = {0u, 1u, 0x80000000u, 0xffffffffu};
    for (uint32_t s = 0u; s < 4u; ++s) {
        test_kernels(seeds[s]);
        for (uint32_t module = 0u; module < COMPILER_SUITE_MODULES; ++module) {
            compiler_result_t first, second;
            compiler_module_run(module, seeds[s], &first, &canvas, NULL, NULL);
            compiler_module_run(module, seeds[s], &second, &canvas, NULL, NULL);
            assert(first.cases == cases[module]);
            assert(memcmp(&first, &second, sizeof(first)) == 0);
            if (module == 2u) {
                assert(first.values[0] == 64u);
                assert(first.values[2] == 0xa5a5a5u);
                assert(first.values[3] == (seeds[s] ^ 0x12345678u));
                assert(first.values[5] == 1u);
            }
            if (module == 3u) {
                assert(first.values[0] == 31u);
                assert(first.values[1] == 1720u);
            }
            if (module == 4u) {
                assert(first.values[1] == seeds[s] + 2080u);
                assert(first.values[3] == 186u);
            }
            if (module == 5u) {
                assert(first.values[4] == 0xffffu);
                assert(first.values[5] == 0xf800u);
                uint32_t crc = 0xffffffffu, sum = 0u, white = 0u, green = 0u;
                for (uint32_t i = 0u; i < cases[module]; i += COMPILER_GRAPHICS_STRIDE) {
                    uint16_t pixel = canvas.pixels[i];
                    uint8_t bytes[2] = {(uint8_t)pixel, (uint8_t)(pixel >> 8u)};
                    crc = diag_crc32_update(crc, bytes, 2u);
                    sum += pixel;
                    if (pixel == 0xffffu) ++white;
                    if (pixel == 0x07e0u) ++green;
                }
                assert(first.values[0] == ~crc && first.values[1] == sum);
                assert(first.values[2] == white && first.values[3] == green);
            }
            uint8_t nonce[16] = {1u}, wire[64];
            compiler_summary(wire, 0x12345678u, nonce, 1u, 0u);
#ifdef COMPILER_TEST_TIMING
            assert(usb_report_u32(wire + 56u) == COMPILER_TIMING_VERSION);
            assert(COMPILER_SUITE_RECORDS == 28u);
            assert(COMPILER_GRAPHICS_STRIDE == 4u);
#else
            assert(usb_report_u32(wire + 56u) == 0u);
            assert(COMPILER_GRAPHICS_STRIDE == 1u);
#endif
            compiler_record(wire, 0x12345678u, nonce, module, &first);
            assert(usb_report_u32(wire) == COMPILER_SUITE_MAGIC);
            assert(usb_report_u32(wire + 12u) == COMPILER_SUITE_ID + module + 1u);
            assert(usb_report_u32(wire + 32u) == cases[module]);
            assert(usb_report_u32(wire + 60u) == diag_crc32(wire, 60u));
        }
    }
    compiler_result_t expected;
    compiler_module_run(0u, 1u, &expected, &canvas, NULL, NULL);
    test_clock_t clock = {0xfffffb00u, 1000u, 0u, 0u};
    uint32_t samples[COMPILER_TIMING_SAMPLES];
    assert(compiler_measure(0u, 1u, &expected, &canvas, test_clock, test_service, &clock, samples));
    assert(clock.services == COMPILER_TIMING_SAMPLES + 1u);
    assert(clock.reads == COMPILER_TIMING_SAMPLES * 2u);
    for (uint32_t i = 0u; i < COMPILER_TIMING_SAMPLES; ++i) assert(samples[i] == 1000u);
    expected.values[0] ^= 1u;
    assert(!compiler_measure(0u, 1u, &expected, &canvas, test_clock, test_service, &clock, samples));
    expected.values[0] ^= 1u;
    clock.step = 99u;
    assert(!compiler_measure(0u, 1u, &expected, &canvas, test_clock, test_service, &clock, samples));
    clock.step = 1000001u;
    assert(!compiler_measure(0u, 1u, &expected, &canvas, test_clock, test_service, &clock, samples));
    compiler_module_run(5u, 1u, &expected, &canvas, NULL, NULL);
    clock.step = 1000u; clock.services = 0u;
    assert(compiler_measure(5u, 1u, &expected, &canvas, test_clock, test_service, &clock, samples));
    assert(clock.services == COMPILER_TIMING_SAMPLES + 1u);
    uint8_t nonce[16] = {1u}, wire[64];
    compiler_data_record(wire, 0x12345678u, nonce, COMPILER_TIMING_FIRST_PAGE, samples);
    assert(usb_report_u32(wire + 12u) == COMPILER_SUITE_ID + COMPILER_TIMING_FIRST_PAGE);
    assert(usb_report_u32(wire + 32u) == samples[0]);
    assert(usb_report_u32(wire + 60u) == diag_crc32(wire, 60u));
    puts("compiler suite: deterministic modules/kernels, independent arithmetic/divrem/memory references, records, timing samples/wrap/bounds and correctness gating PASS");
    return 0;
}
