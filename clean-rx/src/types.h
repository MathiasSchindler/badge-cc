#ifndef CYW43_TYPES_H
#define CYW43_TYPES_H

/* Freestanding fixed-width integer types for ARMv7-M (32-bit Cortex-M3) */
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;
typedef unsigned int       uintptr_t;
typedef unsigned int       size_t;

_Static_assert(sizeof(uint8_t)  == 1u, "uint8_t is 1 byte");
_Static_assert(sizeof(uint16_t) == 2u, "uint16_t is 2 bytes");
_Static_assert(sizeof(uint32_t) == 4u, "uint32_t is 4 bytes");
_Static_assert(sizeof(uint64_t) == 8u, "uint64_t is 8 bytes");

#endif
