#ifndef BADGE_IR_LOOP_ADMISSION_H
#define BADGE_IR_LOOP_ADMISSION_H
#include "codegen.h"
#include "ir_analysis.h"
void ir_loop_admission(ir_context *, ir_module *, const ir_analysis *);
void ir_function_admission(ir_context *, ir_module *, const ir_analysis *,
                           badge_scalar_selection);
#endif
