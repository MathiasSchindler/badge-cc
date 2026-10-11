#include "suite.h"
#include "../diagnostics_logic.h"
#include "../../shared/bytes.h"

#ifndef COMPILER_TEST_APPLICATION
const char *compiler_module_name(uint32_t module) {
    switch (module) {
        case 0u: return "ARITHMETIC";
        case 1u: return "STRINGS";
        case 2u: return "MEMORY";
        case 3u: return "CONTROL FLOW";
        case 4u: return "CROSS FILE ABI";
        case 5u: return "GRAPHICS";
        default: return "INVALID MODULE";
    }
}

void compiler_module_run(uint32_t module, uint32_t seed, compiler_result_t *out,
                          badger_canvas_t *canvas, void (*service)(void *), void *context) {
    out->cases = 0u;
    for (uint32_t i = 0u; i < 6u; ++i) out->values[i] = 0u;
    switch (module) {
        case 0u: compiler_arithmetic(seed, out); break;
        case 1u: compiler_strings(seed, out); break;
        case 2u: compiler_memory(seed, out); break;
        case 3u: compiler_control(seed, out); break;
        case 4u: compiler_abi(seed, out); break;
        case 5u: compiler_graphics(seed, out, canvas, service, context); break;
    }
}

#endif

void compiler_record(uint8_t wire[64], uint32_t build, const uint8_t nonce[16],
                      uint32_t module, const compiler_result_t *result) {
    usb_report_header(wire, COMPILER_SUITE_MAGIC, build, nonce, COMPILER_SUITE_ID + 1u + module);
    badger_bytes_store_u32(wire + 32u, result->cases);
    for (uint32_t i = 0u; i < 6u; ++i) badger_bytes_store_u32(wire + 36u + i * 4u, result->values[i]);
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
}

void compiler_summary(uint8_t wire[64], uint32_t build, const uint8_t nonce[16],
                       uint32_t status, uint32_t detail) {
    usb_report_header(wire, COMPILER_SUITE_MAGIC, build, nonce, COMPILER_SUITE_ID);
    badger_bytes_store_u32(wire + 32u, status);
    badger_bytes_store_u32(wire + 36u, COMPILER_SUITE_MODULES);
    badger_bytes_store_u32(wire + 40u, detail);
    badger_bytes_store_u32(wire + 44u, usb_report_u32(nonce));
#ifdef COMPILER_TEST_TIMING
    badger_bytes_store_u32(wire + 48u, COMPILER_TIMING_SAMPLES);
    badger_bytes_store_u32(wire + 52u, COMPILER_TIMING_REPEATS);
    badger_bytes_store_u32(wire + 56u, COMPILER_TIMING_VERSION);
#endif
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
}

void compiler_data_record(uint8_t wire[64], uint32_t build, const uint8_t nonce[16],
                          uint32_t page, const uint32_t values[7]) {
    usb_report_header(wire, COMPILER_SUITE_MAGIC, build, nonce, COMPILER_SUITE_ID + page);
    for (uint32_t i = 0u; i < 7u; ++i)
        badger_bytes_store_u32(wire + 32u + i * 4u, values[i]);
    badger_bytes_store_u32(wire + 60u, diag_crc32(wire, 60u));
}

#ifndef COMPILER_TEST_APPLICATION
static int result_equal(const compiler_result_t *a, const compiler_result_t *b) {
    if (a->cases != b->cases) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (a->values[i] != b->values[i]) return 0;
    return 1;
}
int compiler_measure(uint32_t module, uint32_t seed, const compiler_result_t *expected,
                     badger_canvas_t *canvas, uint32_t (*clock)(void *),
                     void (*service)(void *), void *context,
                     uint32_t samples[COMPILER_TIMING_SAMPLES]) {
    compiler_result_t measured;
    uint32_t repeats = module == 5u ? 1u : COMPILER_TIMING_REPEATS;
    compiler_module_run(module, seed, &measured, canvas, 0, 0);
    if (!result_equal(expected, &measured)) return 0;
    service(context);
    for (uint32_t sample = 0u; sample < COMPILER_TIMING_SAMPLES; ++sample) {
        uint32_t before = clock(context);
        for (uint32_t repeat = 0u; repeat < repeats; ++repeat)
            compiler_module_run(module, seed, &measured, canvas, 0, 0);
        samples[sample] = clock(context) - before;
        if (!result_equal(expected, &measured) ||
            samples[sample] < 100u || samples[sample] > 1000000u) return 0;
        service(context);
    }
    return 1;
}
#endif
