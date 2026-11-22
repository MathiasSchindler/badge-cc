#include "ir_pass.h"

void ir_constant_fold(ir_context *, ir_module *);
void ir_aggregate_memory(ir_context *, ir_module *);
void ir_copy_chains(ir_context *, ir_module *);
void ir_small_aggregate(ir_context *, ir_module *);
void ir_sret_forward(ir_context *, ir_module *);
void ir_wide_division(ir_context *, ir_module *);
void ir_dead_instructions(ir_context *, ir_module *);
void ir_scalar_promotion(ir_context *, ir_module *);
void ir_mem2reg(ir_context *, ir_module *);
void ir_sccp(ir_context *, ir_module *);
void ir_instruction_combine(ir_context *, ir_module *);
void ir_low_bits(ir_context *, ir_module *);
void ir_linear_bias(ir_context *, ir_module *);
void ir_scalar_cleanup(ir_context *, ir_module *);
void ir_local_cse(ir_context *, ir_module *);
void ir_loop_invariant(ir_context *, ir_module *);
void ir_loop_cost(ir_context *, ir_module *);
void ir_loop_scalar(ir_context *, ir_module *);
void ir_loop_bounds(ir_context *, ir_module *);
void ir_cross_function(ir_context *, ir_module *);
void ir_hot_inline(ir_context *, ir_module *);
void ir_compact_inline(ir_context *, ir_module *);
void ir_target_strength_reduce(ir_context *, ir_module *);
void ir_exact_pow2(ir_context *, ir_module *);
void ir_divrem_pairs(ir_context *, ir_module *);
void ir_local_memory(ir_context *, ir_module *);
void ir_global_memory(ir_context *, ir_module *);

const ir_pass badge_ir_passes[] = {
    {"cross-function",
     "Bounded module-local helper specialization and straight-line inlining with whole-module final-byte admission",
     ir_cross_function, IR_PASS_OPTIMIZATION},
    {"hot-inline",
     "Prioritize measured internal helpers for final-byte and stack-gated inlining",
     ir_hot_inline, IR_PASS_OPTIMIZATION},
    {"compact-inline",
     "Inline bounded internal helpers using thumb-constants final bytes and stack admission",
     ir_compact_inline, IR_PASS_OPTIMIZATION},
    {"loop-scalar",
     "Bounded unflagged scalar motion and modular induction with conservative loop-pressure budgets",
     ir_loop_scalar, IR_PASS_OPTIMIZATION},
    {"loop-bounds",
     "Hoist proven private global bounds out of simple loop headers",
     ir_loop_bounds, IR_PASS_OPTIMIZATION},
    {"local-memory",
     "Reuse exact-pointer scalar loads within bounded same-block windows",
     ir_local_memory, IR_PASS_OPTIMIZATION},
    {"global-memory",
     "Reuse exact global-address loads in a bounded same-block window without crossing writes or calls",
     ir_global_memory, IR_PASS_OPTIMIZATION},
    {"constant-fold",
     "Fold unflagged scalar integer constants and propagate SSA literals",
     ir_constant_fold, IR_PASS_OPTIMIZATION},
    {"aggregate-memory",
     "Legalize wide array-carrier loads/stores into scalar memory operations",
     ir_aggregate_memory, IR_PASS_LEGALIZATION},
    {"sret-forward",
     "Forward private aggregate results and remove adjacent private copy chains",
     ir_sret_forward, IR_PASS_OPTIMIZATION},
    {"copy-chains",
     "Remove adjacent exact-size copies through single-use private aggregate buffers",
     ir_copy_chains, IR_PASS_OPTIMIZATION},
    {"small-aggregate",
     "Promote private two-to-four-word records using existing SSA construction",
     ir_small_aggregate, IR_PASS_OPTIMIZATION},
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
    {"low-bits",
     "Remove masks whose changed bits cannot reach observable single-use consumers",
     ir_low_bits, IR_PASS_OPTIMIZATION},
    {"linear-bias",
     "Combine constant offsets in unflagged two-term modular sums",
     ir_linear_bias, IR_PASS_OPTIMIZATION},
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
    {"exact-pow2",
     "Rewrite signed divisions of proven exact power-of-two multiples",
     ir_exact_pow2, IR_PASS_OPTIMIZATION},
    {"divrem-pairs",
     "Share bounded unsigned quotient/remainder pairs and lower proven exact signed divisions",
     ir_divrem_pairs, IR_PASS_OPTIMIZATION},
};
const unsigned badge_ir_pass_count =
    sizeof(badge_ir_passes) / sizeof(*badge_ir_passes);

const char *const badge_ir_default_pipeline[] = {
    "mem2reg", "scalar-cleanup", "constant-fold", "aggregate-memory",
    "wide-division", "local-memory", "loop-scalar", "cross-function",
    "global-memory", "sret-forward", "compact-inline", "low-bits",
    "linear-bias", "small-aggregate", "dead-instructions", "divrem-pairs"};
const unsigned badge_ir_default_pass_count =
    sizeof(badge_ir_default_pipeline) / sizeof(*badge_ir_default_pipeline);
