#ifndef BADGE_COMPILER_THUMB_IMMEDIATE_H
#define BADGE_COMPILER_THUMB_IMMEDIATE_H

#include <stdint.h>

static inline int thumb_modified(uint32_t value) {
  for (unsigned code = 0; code < 4096; ++code) {
    uint32_t v;
    if (code < 1024) {
      uint32_t byte = code & 255;
      switch (code >> 8) {
      case 0:
        v = byte;
        break;
      case 1:
        if (!byte)
          continue;
        v = byte | (byte << 16);
        break;
      case 2:
        if (!byte)
          continue;
        v = (byte << 8) | (byte << 24);
        break;
      default:
        if (!byte)
          continue;
        v = byte * 0x01010101u;
        break;
      }
    } else {
      uint32_t byte = 128 | (code & 127);
      unsigned rotate = code >> 7;
      v = (byte >> rotate) | (byte << (32 - rotate));
    }
    if (v == value)
      return (int)code;
  }
  return -1;
}
#endif
