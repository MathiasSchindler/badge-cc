#include "ir_pass.h"

void ir_constant_fold(ir_context *, ir_module *);
void ir_aggregate_memory(ir_context *, ir_module *);
void ir_wide_division(ir_context *, ir_module *);
void ir_dead_instructions(ir_context *, ir_module *);
void ir_scalar_promotion(ir_context *, ir_module *);
void ir_mem2reg(ir_context *, ir_module *);
void ir_sccp(ir_context *, ir_module *);
void ir_instruction_combine(ir_context *, ir_module *);
void ir_scalar_cleanup(ir_context *, ir_module *);
void ir_local_cse(ir_context *, ir_module *);
void ir_loop_invariant(ir_context *, ir_module *);
void ir_loop_cost(ir_context *, ir_module *);
void ir_loop_scalar(ir_context *, ir_module *);
void ir_cross_function(ir_context *, ir_module *);
void ir_target_strength_reduce(ir_context *, ir_module *);
void ir_local_memory(ir_context *, ir_module *);

const ir_pass badge_ir_passes[] = {
    {"cross-function",
     "Bounded module-local helper specialization and straight-line inlining with whole-module final-byte admission",
     ir_cross_function, IR_PASS_OPTIMIZATION},
    {"loop-scalar",
     "Bounded unflagged scalar motion and modular induction with conservative loop-pressure budgets",
     ir_loop_scalar, IR_PASS_OPTIMIZATION},
    {"local-memory",
     "Reuse exact-pointer scalar loads within bounded same-block windows",
     ir_local_memory, IR_PASS_OPTIMIZATION},
    {"constant-fold",
     "Fold unflagged scalar integer constants and propagate SSA literals",
     ir_constant_fold, IR_PASS_OPTIMIZATION},
    {"aggregate-memory",
     "Legalize wide array-carrier loads/stores into scalar memory operations",
     ir_aggregate_memory, IR_PASS_LEGALIZATION},
    {"wide-division",
     "Lower general i64 division/remainder to owned restoring-division IR",
     ir_wide_division, IR_PASS_LEGALIZATION},
    {"dead-instructions",
     "Remove unused pure nontrapping SSA definitions with an indexed worklist",
     ir_dead_instructions, IR_PASS_OPTIMIZATION},
    {"scalar-promotion",
     "Promote direct scalar slots confined to one block or initialized once in "
     "entry",
     ir_scalar_promotion, IR_PASS_OPTIMIZATION},
    {"mem2reg",
     "Promote nonescaping scalar slots with pruned SSA and trivial PHI cleanup",
     ir_mem2reg, IR_PASS_OPTIMIZATION},
    {"sccp",
     "Propagate scalar integer constants on executable edges, repair CFG/PHIs "
     "and clean up",
     ir_sccp, IR_PASS_OPTIMIZATION},
    {"instruction-combine",
     "Eliminate typed neutral arithmetic and simplify proven integer cast "
     "chains",
     ir_instruction_combine, IR_PASS_OPTIMIZATION},
    {"scalar-cleanup",
     "Combine scalar expressions and forward equality/nonzero tests of "
     "extended i1 values",
     ir_scalar_cleanup, IR_PASS_OPTIMIZATION},
    {"local-cse",
     "Reuse matching pure arithmetic, comparisons, casts and addresses within "
     "a block",
     ir_local_cse, IR_PASS_OPTIMIZATION},
    {"loop-invariant",
     "Hoist proven nontrapping pure expressions from natural loops into "
     "existing preheaders",
     ir_loop_invariant, IR_PASS_OPTIMIZATION},
    {"loop-cost",
     "Hoist one costly i32 invariant per natural loop with a bounded live-range "
     "budget and existing speculation checks",
     ir_loop_cost, IR_PASS_OPTIMIZATION},
    {"target-strength-reduce",
     "Reduce unflagged i32 unsigned power-of-two div/rem with bounded "
     "target costs and quotient-sharing accounting",
     ir_target_strength_reduce, IR_PASS_OPTIMIZATION},
};
const unsigned badge_ir_pass_count =
    sizeof(badge_ir_passes) / sizeof(*badge_ir_passes);

const char *const badge_ir_default_pipeline[] = {
    "mem2reg", "scalar-cleanup", "constant-fold", "aggregate-memory",
    "wide-division", "local-memory", "loop-scalar", "cross-function"};
const unsigned badge_ir_default_pass_count =
    sizeof(badge_ir_default_pipeline) / sizeof(*badge_ir_default_pipeline);
