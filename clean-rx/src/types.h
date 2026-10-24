#ifndef CYW43_TYPES_H
#define CYW43_TYPES_H

typedef __UINT8_TYPE__ uint8_t;
typedef __INT8_TYPE__ int8_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __SIZE_TYPE__ size_t;

_Static_assert(sizeof(uint8_t)  == 1u, "uint8_t is 1 byte");
_Static_assert(sizeof(uint16_t) == 2u, "uint16_t is 2 bytes");
_Static_assert(sizeof(uint32_t) == 4u, "uint32_t is 4 bytes");
_Static_assert(sizeof(uint64_t) == 8u, "uint64_t is 8 bytes");
_Static_assert(sizeof(uintptr_t) == sizeof(void *), "uintptr_t matches pointers");
_Static_assert(sizeof(intptr_t) == sizeof(void *), "intptr_t matches pointers");
_Static_assert(sizeof(size_t) == sizeof(sizeof(0)), "size_t matches sizeof");

#endif
