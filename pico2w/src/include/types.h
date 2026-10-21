#ifndef PICO2W_TYPES_H
#define PICO2W_TYPES_H

typedef __UINT8_TYPE__ uint8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINTPTR_TYPE__ uintptr_t;

_Static_assert(sizeof(uint32_t) == 4, "32-bit integers required");

#endif
