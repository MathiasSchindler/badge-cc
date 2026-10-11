#ifndef BADGE_COMPILER_TARGET_H
#define BADGE_COMPILER_TARGET_H

#include <stdint.h>

#define BADGE_IR_TRIPLE "\"thumbv8m.main-unknown-none-eabi\""
#define BADGE_IR_LAYOUT                                                        \
  "\"e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64\""

static inline int badge_direct_i64_sdiv(uint64_t divisor) {
  return divisor && !(divisor & (divisor - 1)) && divisor <= UINT32_MAX;
}

#endif
