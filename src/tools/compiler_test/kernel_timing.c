#include "../diagnostics_logic.h"
#include "kernels.h"

typedef uint32_t (*kernel_fn)(uint32_t, const uint32_t *, uint32_t *);
typedef struct {
    uint32_t before, source[COMPILER_KERNEL_WORK], between;
    uint32_t dest[COMPILER_KERNEL_WORK], after;
} kernel_state_t;

static kernel_fn kernel_function(uint32_t kernel) {
    switch (kernel) {
        case 0u: return compiler_kernel_arithmetic;
        case 1u: return compiler_kernel_branch;
        case 2u: return compiler_kernel_call;
        case 3u: return compiler_kernel_unsigned_divrem;
        case 4u: return compiler_kernel_signed_divrem;
        case 5u: return compiler_kernel_memory;
        default: return 0;
    }
}

static void kernel_prepare(kernel_state_t *state, uint32_t seed) {
    state->before = state->between = state->after = 0xa5a5a5a5u;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i) {
        state->source[i] = seed ^ (i * 0x9e3779b9u);
        state->dest[i] = 0x5a5a5a5au;
    }
}

static int kernel_result(const kernel_state_t *state, uint32_t seed, uint32_t value,
                         compiler_result_t *out) {
    uint32_t unchanged = 1u;
    for (uint32_t i = 0u; i < COMPILER_KERNEL_WORK; ++i)
        if (state->source[i] != (seed ^ (i * 0x9e3779b9u))) unchanged = 0u;
    out->cases = COMPILER_KERNEL_WORK;
    out->values[0] = value;
    out->values[1] = diag_crc32((const uint8_t *)state->source, sizeof(state->source));
    out->values[2] = diag_crc32((const uint8_t *)state->dest, sizeof(state->dest));
    out->values[3] = (uint32_t)(state->before == 0xa5a5a5a5u && state->between == 0xa5a5a5a5u &&
                                state->after == 0xa5a5a5a5u);
    out->values[4] = unchanged;
    out->values[5] = COMPILER_KERNEL_WORK;
    return out->values[3] != 0u && unchanged != 0u;
}

static int kernel_equal(const compiler_result_t *a, const compiler_result_t *b) {
    if (a->cases != b->cases) return 0;
    for (uint32_t i = 0u; i < 6u; ++i)
        if (a->values[i] != b->values[i]) return 0;
    return 1;
}

int compiler_kernel_run(uint32_t kernel, uint32_t seed, compiler_result_t *out) {
    kernel_fn function = kernel_function(kernel);
    if (!function) return 0;
    kernel_state_t state;
    kernel_prepare(&state, seed);
    uint32_t value = function(seed, state.source, state.dest);
    return kernel_result(&state, seed, value, out);
}

int compiler_kernel_measure(uint32_t kernel, uint32_t seed, const compiler_result_t *expected,
                            uint32_t (*clock)(void *), void (*service)(void *), void *context,
                            uint32_t samples[COMPILER_TIMING_SAMPLES]) {
    kernel_fn function = kernel_function(kernel);
    if (!function) return 0;
    kernel_state_t state;
    compiler_result_t measured;
    uint32_t values[COMPILER_TIMING_REPEATS];
    kernel_prepare(&state, seed);
    uint32_t value = function(seed, state.source, state.dest);
    if (!kernel_result(&state, seed, value, &measured) || !kernel_equal(expected, &measured))
        return 0;
    service(context);
    for (uint32_t sample = 0u; sample < COMPILER_TIMING_SAMPLES; ++sample) {
        kernel_prepare(&state, seed);
        uint32_t before = clock(context);
        for (uint32_t repeat = 0u; repeat < COMPILER_TIMING_REPEATS; ++repeat)
            values[repeat] = function(seed, state.source, state.dest);
        samples[sample] = clock(context) - before;
        if (samples[sample] < 100u || samples[sample] > 1000000u) return 0;
        for (uint32_t repeat = 0u; repeat < COMPILER_TIMING_REPEATS; ++repeat)
            if (values[repeat] != expected->values[0]) return 0;
        if (!kernel_result(&state, seed, values[COMPILER_TIMING_REPEATS - 1u], &measured) ||
            !kernel_equal(expected, &measured))
            return 0;
        service(context);
    }
    return 1;
}
