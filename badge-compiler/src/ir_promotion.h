#ifndef BADGE_IR_PROMOTION_H
#define BADGE_IR_PROMOTION_H

#include "ir_analysis.h"

const char *ir_scalar_slot_reason(const ir_analysis *, unsigned);

static inline void ir_promotion_erase(ir_module *m, const ir_instruction *in) {
  for (unsigned l = in->line; l <= in->end_line; l++)
    m->lines[l - 1].replacement = "";
}

#endif
