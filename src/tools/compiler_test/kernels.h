#ifndef UNIVERSE26_COMPILER_KERNELS_H
#define UNIVERSE26_COMPILER_KERNELS_H

#include "suite.h"

#define COMPILER_KERNELS COMPILER_TIMING_KERNELS
#define COMPILER_KERNEL_WORK 128u
#define COMPILER_KERNEL_FIRST_PAGE (COMPILER_TIMING_FIRST_PAGE + COMPILER_SUITE_MODULES)

uint32_t compiler_kernel_arithmetic(uint32_t seed, const uint32_t *source, uint32_t *dest);
uint32_t compiler_kernel_branch(uint32_t seed, const uint32_t *source, uint32_t *dest);
uint32_t compiler_kernel_call(uint32_t seed, const uint32_t *source, uint32_t *dest);
uint32_t compiler_kernel_unsigned_divrem(uint32_t seed, const uint32_t *source, uint32_t *dest);
uint32_t compiler_kernel_signed_divrem(uint32_t seed, const uint32_t *source, uint32_t *dest);
uint32_t compiler_kernel_memory(uint32_t seed, const uint32_t *source, uint32_t *dest);
int compiler_kernel_run(uint32_t kernel, uint32_t seed, compiler_result_t *out);
int compiler_kernel_measure(uint32_t kernel, uint32_t seed, const compiler_result_t *expected,
                            uint32_t (*clock)(void *), void (*service)(void *), void *context,
                            uint32_t samples[COMPILER_TIMING_SAMPLES]);

#endif
