#include <stdint.h>
#include "../shared/usb_report.h"
#include "diagnostics_logic.h"
#include "../arch/rp2350/bootrom_reboot.h"

static uint32_t d02_affine(uint32_t value, uint32_t multiplier, uint32_t addend, uint32_t count) {
    while (count != 0u) {
        if ((count & 1u) != 0u) value = value * multiplier + addend;
        addend = addend * (multiplier + 1u);
        multiplier = multiplier * multiplier;
        count >>= 1u;
    }
    return value;
}

#include "d02_generated.h"

#ifdef D02_ORACLE
int main(void) {
    for (uint32_t placement = 0u; placement < 2u; ++placement) {
        for (uint32_t index = 0u; index < D02_CASES; ++index) {
            uint32_t data[10];
            for (uint32_t i = 0u; i < 7u; ++i) data[i] = 3u;
            if (index == D02_STORE_ADDRESS) data[0] = (uint32_t)(uintptr_t)&data[8];
            data[7] = 0xa5a5a5a5u;
            data[8] = 0u;
            data[9] = 0x5a5a5a5au;
            d02_function function = placement == 0u ? flash_functions[index] : ram_functions[index];
            uint32_t value = function(D02_ITERATIONS, data, dividends[index]);
            uint32_t expected = index == D02_STORE_ADDRESS ? (uint32_t)(uintptr_t)&data[8] ^ 1u : d02_expected(index);
            if (value != expected) return (int)(index + 1u);
            if (data[7] != 0xa5a5a5a5u || data[9] != 0x5a5a5a5au) return 100;
            if (index == D02_STORE_DATA && data[8] != 3u) return 101;
            if (index == D02_STORE_ADDRESS && data[8] != 3u) return 102;
        }
    }
    return 0;
}
#else
extern volatile uint8_t __d02_request[], __d02_response[];
extern uint32_t d02_mask(void);
extern void d02_unmask(uint32_t value);
extern uint32_t d02_control(void);
extern void d02_barrier(void);

static uint8_t record[64], nonce[16];
static uint32_t original_source, original_reset, original_mask;
static uint32_t original_clocks[10];

/* BOOTSEL erases all SRAM. Persist only after every timed interval has ended. */
static int persist(void) {
    typedef uintptr_t (*lookup_fn)(uint32_t, uint32_t);
    typedef void (*simple_fn)(void);
    typedef void (*erase_fn)(uint32_t, uint32_t, uint32_t, uint8_t);
    typedef void (*program_fn)(uint32_t, const uint8_t *, uint32_t);
    uintptr_t entry = *(const volatile uint16_t *)0x16u;
    if ((entry & 1u) == 0u || entry < 0x20u || entry >= 0x10000u) return 0;
    lookup_fn lookup = (lookup_fn)entry;
    const uint32_t codes[6] = {0x4649u, 0x5845u, 0x4552u, 0x5052u, 0x4346u, 0x5843u};
    uintptr_t functions[6];
    for (uint32_t i = 0u; i < 6u; ++i) {
        functions[i] = lookup(codes[i], 4u);
        if ((functions[i] & 1u) == 0u || functions[i] < 0x20u || functions[i] >= 0x10000u) return 0;
    }
    uint32_t bytes = ((D02_METADATA + D02_CASES * 5u) * 64u + 255u) & ~255u;
    ((simple_fn)functions[0])();
    ((simple_fn)functions[1])();
    ((erase_fn)functions[2])(0x00ff0000u, 16384u, 4096u, 0x20u);
    ((program_fn)functions[3])(0x00ff0000u, (const uint8_t *)__d02_response, bytes);
    ((simple_fn)functions[4])();
    ((simple_fn)functions[5])();
    d02_barrier();
    for (uint32_t i = 0u; i < bytes; ++i)
        if (*(const volatile uint8_t *)(uintptr_t)(0x10ff0000u + i) != __d02_response[i]) return 0;
    return 1;
}

static uint32_t read32(uint32_t address) {
    return *(volatile uint32_t *)(uintptr_t)address;
}

static void write32(uint32_t address, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)address = value;
}

static uint32_t clock_register(uint32_t index) {
    static const uint32_t addresses[] = {
        0x4001003cu, 0x40010040u, 0x40010030u, 0x40010034u,
        0x40050000u, 0x40050008u, 0x4005000cu,
        0x40058000u, 0x40058008u, 0x4005800cu
    };
    return read32(addresses[index]);
}

static void publish(uint32_t index, const uint32_t values[7]) {
    usb_report_header(record, 0x32423044u, D02_BUILD, nonce, D02_SUITE + index);
    for (uint32_t i = 0u; i < 7u; ++i) {
        uint32_t value = values[i];
        for (uint32_t byte = 0u; byte < 4u; ++byte)
            record[32u + 4u * i + byte] = (uint8_t)(value >> (8u * byte));
    }
    uint32_t crc = diag_crc32(record, 60u);
    for (uint32_t i = 0u; i < 4u; ++i) record[60u + i] = (uint8_t)(crc >> (8u * i));
    volatile uint8_t *out = __d02_response + index * 64u;
    for (uint32_t i = 0u; i < 4u; ++i) out[i] = 0u;
    d02_barrier();
    for (uint32_t i = 4u; i < 64u; ++i) out[i] = record[i];
    d02_barrier();
    for (uint32_t i = 0u; i < 4u; ++i) out[i] = record[i];
    d02_barrier();
}

static uint32_t clock_read(void) {
    d02_barrier();
    return read32(0x400b0028u);
}

static int dma_stop(void) {
    write32(0x50000464u, 1u);
    uint32_t limit = 1000000u;
    while ((read32(0x50000464u) & 1u) != 0u && limit != 0u) --limit;
    write32(0x5000000cu, 0u);
    return limit != 0u && (read32(0x5000000cu) & 0x04000000u) == 0u;
}

static int dma_start(uint32_t mode) {
    write32(0x50000000u, mode == 3u ? 0x2003e000u : 0x20040000u);
    write32(0x50000004u, 0x20040004u);
    /* ENDLESS disables decrement, but a zero count still halts immediately. */
    write32(0x50000008u, 0xf0000001u);
    write32(0x5000000cu, (0x3fu << 17u) | (1u << 23u) | (2u << 2u) | 1u);
    d02_barrier();
    return (read32(0x5000000cu) & 0x04000000u) != 0u;
}

static void prepare(uint32_t index) {
    volatile uint32_t *data = (volatile uint32_t *)0x2003e000u;
    for (uint32_t i = 0u; i < 7u; ++i) data[i] = 3u;
    if (index == D02_STORE_ADDRESS) data[0] = 0x2003e020u;
    data[7] = 0xa5a5a5a5u;
    data[8] = 0u;
    data[9] = 0x5a5a5a5au;
    write32(0x20040000u, 3u);
    write32(0x20040004u, 0u);
    d02_barrier();
}

static int guards(uint32_t index) {
    if (read32(0x2003e01cu) != 0xa5a5a5a5u || read32(0x2003e024u) != 0x5a5a5a5au) return 0;
    if (read32(0x2003e000u) != (index == D02_STORE_ADDRESS ? 0x2003e020u : 3u)) return 0;
    if (index == D02_STORE_DATA && read32(0x2003e020u) != 3u) return 0;
    if (index == D02_STORE_ADDRESS && read32(0x2003e020u) != 3u) return 0;
    return 1;
}

static void invalidate(d02_function function, const uint8_t *end) {
    uint32_t start = (uint32_t)(uintptr_t)function & ~7u;
    uint32_t finish = (uint32_t)(uintptr_t)end;
    for (uint32_t address = start; address < finish; address += 8u)
        *(volatile uint8_t *)(uintptr_t)(0x18000002u | (address & 0x00fffff8u)) = 0u;
    d02_barrier();
}

static int measure(uint32_t index, uint32_t mode, uint32_t values[7]) {
    d02_function function = mode == 1u || mode == 2u ? flash_functions[index] : ram_functions[index];
    uint32_t expected = d02_expected(index);
    for (uint32_t sample = 0u; sample < D02_SAMPLES; ++sample) {
        prepare(index);
        if (function(D02_ITERATIONS, (const uint32_t *)0x2003e000u, dividends[index]) != expected || !guards(index)) return 1;
        prepare(index);
        if (mode == 2u) invalidate(function, flash_ends[index]);
        if (mode >= 3u && !dma_start(mode)) return 2;
        uint32_t before = clock_read();
        uint32_t result = function(D02_ITERATIONS, (const uint32_t *)0x2003e000u, dividends[index]);
        uint32_t after = clock_read();
        uint32_t busy = mode < 3u ? 0u : read32(0x5000000cu);
        if (mode >= 3u && !dma_stop()) return 7;
        values[sample] = after - before;
        if (mode >= 3u && (busy & 0x04000000u) == 0u) return 3;
        if (mode >= 3u && (read32(0x5000000cu) & 0x64000000u) != 0u) return 4;
        if (result != expected || !guards(index)) return 5;
        if (values[sample] == 0u || values[sample] >= 0x40000000u) return 6;
    }
    return 0;
}

static void finish(uint32_t status, uint32_t detail) {
    if (!dma_stop()) {
        status = 2u;
        detail = 5u;
    }
    uint32_t summary[7] = {status, D02_CASES, 5u, D02_SAMPLES, D02_ITERATIONS, 1u, detail};
    publish(0u, summary);
    if (!persist()) {
        summary[0] = 2u;
        summary[6] = 7u;
        publish(0u, summary);
        while (1) __asm__ volatile ("wfi");
    }
    if ((original_reset & 4u) != 0u) write32(0x40022000u, 4u);
    write32(0x400b0038u, original_source);
    d02_barrier();
    d02_unmask(original_mask);
    int32_t error = badger_reboot_to_bootsel();
    summary[0] = 3u;
    summary[6] = (uint32_t)error;
    publish(0u, summary);
    while (1) __asm__ volatile ("wfi");
}

void bare_main(void) {
    uint8_t request[64];
    uint32_t values[7];
    for (uint32_t i = 0u; i < 64u; ++i) request[i] = __d02_request[i];
    for (uint32_t i = 0u; i < 16u; ++i) nonce[i] = request[16u + i];
    for (uint32_t i = 0u; i < 4u; ++i) __d02_response[i] = 0u;
    original_mask = d02_mask();
    original_source = read32(0x400b0038u);
    original_reset = read32(0x40020000u);
    for (uint32_t i = 0u; i < 10u; ++i) original_clocks[i] = clock_register(i);
    write32(0x40023000u, 4u);
    uint32_t limit = 1000000u;
    while ((read32(0x40020008u) & 4u) == 0u && limit != 0u) --limit;
    if (limit == 0u) finish(2u, 1u);
    for (uint32_t channel = 0u; channel < 16u; ++channel)
        if ((read32(0x5000000cu + 64u * channel) & 0x04000000u) != 0u) finish(2u, 2u);
    if (usb_report_check_request_for_test(request, 64u, D02_BUILD, D02_SUITE) != USB_RECORD_OK) finish(2u, 3u);
    values[0] = read32(0x40000000u);
    values[1] = read32(0xe000ed00u);
    values[2] = d02_control();
    values[3] = read32(0xe000ed88u);
    values[4] = read32(0xe000ed8cu);
    values[5] = original_mask;
    values[6] = original_source;
    publish(1u, values);
    values[0] = read32(0x4001003cu);
    values[1] = read32(0x40010040u);
    values[2] = read32(0x40010030u);
    values[3] = read32(0x40010034u);
    values[4] = read32(0x40050000u);
    values[5] = read32(0x40050008u);
    values[6] = read32(0x4005000cu);
    publish(2u, values);
    write32(0x400b0038u, 1u);
    d02_barrier();
    values[0] = read32(0x400b0038u);
    values[1] = read32(0x400b002cu);
    values[2] = read32(0x400b0024u);
    values[3] = read32(0x400b0030u);
    values[4] = read32(0x400c8000u);
    values[5] = read32(0x400d0000u);
    values[6] = read32(0x400d0004u);
    publish(3u, values);
    values[0] = 0x2003e000u;
    values[1] = 0x2003e020u;
    values[2] = 0x20040000u;
    values[3] = 0x20040004u;
    values[4] = original_reset;
    values[5] = read32(0x5000000cu);
    values[6] = read32(0xd0000000u);
    publish(4u, values);
    values[0] = read32(0xe0001000u);
    values[1] = read32(0x40010044u);
    values[2] = read32(0x40058000u);
    values[3] = read32(0x40058008u);
    values[4] = read32(0x4005800cu);
    values[5] = (uint32_t)(uintptr_t)ram_functions[0];
    values[6] = (uint32_t)(uintptr_t)flash_functions[0];
    publish(5u, values);
    for (uint32_t i = 0u; i < 7u; ++i) {
        uint32_t before = clock_read();
        values[i] = clock_read() - before;
    }
    publish(6u, values);
    for (uint32_t mode = 0u; mode < 5u; ++mode) {
        for (uint32_t index = 0u; index < D02_CASES; ++index) {
            int error = measure(index, mode, values);
            if (error != 0) finish(2u, 0x10000u + (mode * D02_CASES + index) * 16u + (uint32_t)error);
            publish(D02_METADATA + mode * D02_CASES + index, values);
        }
    }
    if (read32(0x400b0038u) != 1u) finish(2u, 4u);
    for (uint32_t i = 0u; i < 10u; ++i)
        if (clock_register(i) != original_clocks[i]) finish(2u, 6u);
    values[0] = 1u;
    values[1] = read32(0x400b0038u);
    values[2] = read32(0x5000000cu);
    values[3] = read32(0xe000ed88u);
    values[4] = read32(0xe000ed8cu);
    values[5] = clock_register(4u);
    values[6] = clock_register(7u);
    publish(7u, values);
    finish(1u, 0u);
}
#endif
