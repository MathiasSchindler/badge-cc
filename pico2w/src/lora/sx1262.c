#include "sx1262.h"
#include "hardware.h"

int32_t sx1262_wait(int32_t (*service)(void), uint32_t delay_us) {
    uint32_t start = sx1262_hw_time_us();
    for (uint32_t loops = 0u; loops < 1000000u; ++loops) {
        int32_t error = service();
        if (error != 0) return error;
        uint32_t elapsed = sx1262_hw_time_us() - start;
        if (delay_us != 0u) {
            if (elapsed >= delay_us) return 0;
        } else {
            if (!sx1262_hw_busy()) return 0;
            if (elapsed >= 100000u) return SX1262_BUSY_ERROR;
        }
    }
    return SX1262_TIMER_ERROR;
}

int32_t sx1262_transfer(const uint8_t *out, uint8_t *in, uint32_t length,
                        uint8_t *tail, uint32_t tail_length,
                        int32_t (*service)(void)) {
    int32_t error = sx1262_wait(service, 0u);
    if (error != 0) return error;
    sx1262_hw_select(1u);
    for (uint32_t i = 0u; i < length && error == 0; ++i)
        error = sx1262_hw_byte(out[i], in + i);
    for (uint32_t i = 0u; i < tail_length && error == 0; ++i)
        error = sx1262_hw_byte(0u, tail + i);
    sx1262_hw_select(0u);
    if (error != 0) return error;
    return sx1262_wait(service, 0u);
}

static int32_t exchange(const uint8_t *out, uint8_t *in, uint32_t length,
                         int32_t (*service)(void)) {
    return sx1262_transfer(out, in, length, 0, 0u, service);
}

static int valid_status(uint8_t status) {
    uint8_t command = (uint8_t)((status >> 1u) & 7u);
    return (status & 0x70u) == 0x20u &&
           command != 3u && command != 4u && command != 5u && command != 7u;
}

static int32_t sample(uint8_t *data, uint8_t packet_type,
                       int32_t (*service)(void)) {
    const uint8_t set_type[] = {0x8au, packet_type};
    const uint8_t get_status[] = {0xc0u, 0u};
    const uint8_t get_type[] = {0x11u, 0u, 0u};
    const uint8_t get_errors[] = {0x17u, 0u, 0u, 0u};
    const uint8_t get_irq[] = {0x12u, 0u, 0u, 0u};
    const uint8_t get_sync[] = {0x1du, 0x07u, 0x40u, 0u, 0u, 0u};
    uint8_t in[6];
    int32_t error = exchange(set_type, in, sizeof(set_type), service);
    if (error != 0) return error;
    error = exchange(get_status, in, sizeof(get_status), service);
    if (error != 0) return error;
    data[0] = in[1];
    error = exchange(get_type, in, sizeof(get_type), service);
    if (error != 0) return error;
    data[1] = in[2];
    error = exchange(get_errors, in, sizeof(get_errors), service);
    if (error != 0) return error;
    data[2] = in[2];
    data[3] = in[3];
    error = exchange(get_irq, in, sizeof(get_irq), service);
    if (error != 0) return error;
    data[4] = in[2];
    data[5] = in[3];
    error = exchange(get_sync, in, sizeof(get_sync), service);
    if (error != 0) return error;
    data[6] = in[4];
    data[7] = in[5];
    if (!valid_status(data[0]) || data[1] != packet_type)
        return SX1262_RESPONSE_ERROR;
    if (data[2] != 0u || data[3] != 0u) return SX1262_DEVICE_ERROR;
    return 0;
}

void sx1262_probe(sx1262_result_t *result, int32_t (*service)(void)) {
    for (uint32_t i = 0u; i < sizeof(result->samples); ++i) result->samples[i] = 0u;
    int32_t error = sx1262_hw_init();
    if (error == 0) {
        sx1262_hw_reset(1u);
        error = sx1262_wait(service, 20000u);
        sx1262_hw_reset(0u);
        if (error == 0) error = sx1262_wait(service, 10000u);
        if (error == 0) error = sample(result->samples, 1u, service);
        if (error == 0) error = sample(result->samples + 8u, 0u, service);
        if (error == 0 &&
            (result->samples[6] != result->samples[14] ||
             result->samples[7] != result->samples[15]))
            error = SX1262_RESPONSE_ERROR;
    }
    sx1262_hw_release();
    result->error = error;
}
