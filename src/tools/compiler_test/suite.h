#ifndef UNIVERSE26_COMPILER_SUITE_H
#define UNIVERSE26_COMPILER_SUITE_H

#include <stdint.h>
#include "../../shared/usb_report.h"
#include "../../platform/badger/graphics.h"

#ifdef COMPILER_TEST_APPLICATION
#define COMPILER_SUITE_ID 0x41505030u
#define COMPILER_TIMING_VERSION 4u
#define COMPILER_TIMING_KERNELS 0u
#else
#define COMPILER_SUITE_ID 0x434f4d50u
#define COMPILER_TIMING_VERSION 3u
#define COMPILER_TIMING_KERNELS 6u
#endif
#define COMPILER_SUITE_MAGIC 0x31544342u
#define COMPILER_SUITE_MODULES 6u
#define COMPILER_TIMING_SAMPLES 7u
#define COMPILER_TIMING_REPEATS 64u
#define COMPILER_TIMING_CLOCK_PAGE (COMPILER_SUITE_MODULES + 1u)
#define COMPILER_TIMING_OVERHEAD_PAGE (COMPILER_TIMING_CLOCK_PAGE + 2u)
#define COMPILER_TIMING_FIRST_PAGE (COMPILER_TIMING_OVERHEAD_PAGE + 1u)
#ifdef COMPILER_TEST_TIMING
#define COMPILER_GRAPHICS_STRIDE 4u
#define COMPILER_SUITE_RECORDS (COMPILER_TIMING_FIRST_PAGE + COMPILER_SUITE_MODULES + 2u * COMPILER_TIMING_KERNELS)
#else
#define COMPILER_GRAPHICS_STRIDE 1u
#define COMPILER_SUITE_RECORDS (COMPILER_SUITE_MODULES + 1u)
#endif
#define COMPILER_SUITE_BYTES (COMPILER_SUITE_RECORDS * USB_REPORT_WIRE_SIZE)

typedef struct {
    uint32_t cases;
    uint32_t values[6];
} compiler_result_t;

typedef struct {
    uint32_t first;
    uint16_t second;
    uint8_t third;
} compiler_tuple_t;

void compiler_arithmetic(uint32_t seed, compiler_result_t *out);
void compiler_strings(uint32_t seed, compiler_result_t *out);
void compiler_memory(uint32_t seed, compiler_result_t *out);
void compiler_control(uint32_t seed, compiler_result_t *out);
void compiler_abi(uint32_t seed, compiler_result_t *out);
void compiler_graphics(uint32_t seed, compiler_result_t *out, badger_canvas_t *canvas,
                        void (*service)(void *), void *context);
compiler_tuple_t compiler_tuple(compiler_tuple_t value, uint32_t delta);
uint64_t compiler_wide(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                       uint32_t e, uint32_t f, uint64_t wide);
uint32_t compiler_mix(uint32_t value, uint32_t salt);
const char *compiler_module_name(uint32_t module);
void compiler_module_run(uint32_t module, uint32_t seed, compiler_result_t *out,
                          badger_canvas_t *canvas, void (*service)(void *), void *context);
void compiler_record(uint8_t wire[64], uint32_t build, const uint8_t nonce[16],
                      uint32_t module, const compiler_result_t *result);
void compiler_summary(uint8_t wire[64], uint32_t build, const uint8_t nonce[16],
                       uint32_t status, uint32_t detail);
void compiler_data_record(uint8_t wire[64], uint32_t build, const uint8_t nonce[16],
                          uint32_t page, const uint32_t values[7]);
int compiler_measure(uint32_t module, uint32_t seed, const compiler_result_t *expected,
                     badger_canvas_t *canvas, uint32_t (*clock)(void *),
                     void (*service)(void *), void *context,
                     uint32_t samples[COMPILER_TIMING_SAMPLES]);

#endif
