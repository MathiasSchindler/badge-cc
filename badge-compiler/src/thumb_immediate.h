#ifndef BADGE_COMPILER_THUMB_IMMEDIATE_H
#define BADGE_COMPILER_THUMB_IMMEDIATE_H

#include <stdint.h>

static inline int thumb_modified(uint32_t value) {
  if (value <= 255)
    return (int)value;
  uint32_t low = value & 255u, high = (value >> 8) & 255u;
  if (value == low * UINT32_C(0x00010001))
    return (int)(256 + low);
  if (value == high * UINT32_C(0x01000100))
    return (int)(512 + high);
  if (value == low * UINT32_C(0x01010101))
    return (int)(768 + low);

  /* Rotations 8..31 shift the byte left without wrapping its bits. */
  unsigned shift = 24u - (unsigned)__builtin_clz(value);
  if (value & ((UINT32_C(1) << shift) - 1))
    return -1;
  return (int)(((32 - shift) << 7) | ((value >> shift) & 127u));
}
#endif
