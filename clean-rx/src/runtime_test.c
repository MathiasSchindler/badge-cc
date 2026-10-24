#ifdef CLEAN_TYPES_FIRST
#include "bytes.h"
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bytes.h"

_Static_assert(sizeof(uintptr_t) == sizeof(void *), "native pointer width");
_Static_assert(sizeof(intptr_t) == sizeof(void *), "native signed pointer width");
_Static_assert(sizeof(size_t) == sizeof(sizeof(0)), "native size width");

int main(void) {
    uint8_t source[264], destination[264], expected[264];
    for (uint32_t i = 0u; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 37u);
    assert((uint8_t *)(uintptr_t)source == source);
    clean_bytes_copy(0, 0, 0u);
    clean_bytes_zero(0, 0u);
    assert(clean_bytes_equal(0, 0, 0u));
    for (uint32_t offset = 0u; offset < 8u; ++offset) {
        for (uint32_t count = 0u; count <= 256u; ++count) {
            memset(destination, 0xa5, sizeof(destination));
            memset(expected, 0xa5, sizeof(expected));
            memcpy(expected + offset, source + offset, count);
            clean_bytes_copy(destination + offset, source + offset, count);
            assert(!memcmp(destination, expected, sizeof(destination)));
            assert(clean_bytes_equal(destination + offset, source + offset, count));
            if (count) {
                destination[offset + count - 1u] ^= 1u;
                assert(!clean_bytes_equal(destination + offset, source + offset, count));
            }
            memset(expected + offset, 0, count);
            clean_bytes_zero(destination + offset, count);
            assert(!memcmp(destination, expected, sizeof(destination)));
        }
        static const uint32_t values[] = {0u, 1u, 0x12345678u, 0x80000000u, 0xffffffffu};
        for (uint32_t i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
            memset(destination, 0xa5, sizeof(destination));
            clean_bytes_store_u32(destination + offset, values[i]);
            for (uint32_t byte = 0u; byte < sizeof(destination); ++byte)
                assert(destination[byte] == (byte >= offset && byte < offset + 4u ?
                    (uint8_t)(values[i] >> (8u * (byte - offset))) : 0xa5u));
            assert(clean_bytes_load_u32(destination + offset) == values[i]);
        }
    }
    puts("clean runtime: native types, empty ranges, byte bounds and unaligned little-endian words PASS");
    return 0;
}
