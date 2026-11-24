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
void ir_libc_strcmp(ir_context *, ir_module *);
void ir_instruction_combine(ir_context *, ir_module *);
void ir_low_bits(ir_context *, ir_module *);
void ir_linear_bias(ir_context *, ir_module *);
void ir_scalar_cleanup(ir_context *, ir_module *);
void ir_local_cse(ir_context *, ir_module *);
void ir_loop_invariant(ir_context *, ir_module *);
void ir_loop_cost(ir_context *, ir_module *);
void ir_loop_scalar(ir_context *, ir_module *);
void ir_loop_bounds(ir_context *, ir_module *);
void ir_loop_bit_seed(ir_context *, ir_module *);
void ir_pointer_induction(ir_context *, ir_module *);
void ir_cross_function(ir_context *, ir_module *);
void ir_hot_inline(ir_context *, ir_module *);
void ir_compact_inline(ir_context *, ir_module *);
void ir_guard_split(ir_context *, ir_module *);
void ir_target_strength_reduce(ir_context *, ir_module *);
void ir_exact_pow2(ir_context *, ir_module *);
void ir_divrem_pairs(ir_context *, ir_module *);
void ir_bounded_decimal(ir_context *, ir_module *);
void ir_bounded_signed_reciprocal(ir_context *, ir_module *);
void ir_local_memory(ir_context *, ir_module *);
void ir_global_memory(ir_context *, ir_module *);

#define OPT IR_PASS_OPTIMIZATION, 0
#define TYPED_OPT IR_PASS_OPTIMIZATION, 1
#define LEGALIZE IR_PASS_LEGALIZATION, 0
const ir_pass badge_ir_passes[] = {
    {"cross-function",
     "Bounded module-local helper specialization and straight-line inlining with whole-module final-byte admission",
     ir_cross_function, OPT},
    {"hot-inline",
     "Prioritize measured internal helpers for final-byte and stack-gated inlining",
     ir_hot_inline, OPT},
    {"compact-inline",
     "Inline bounded internal helpers using thumb-constants final bytes and stack admission",
     ir_compact_inline, OPT},
    {"guard-split",
     "Move bounded private helper rejection guards into a loop under final byte and stack gates",
     ir_guard_split, TYPED_OPT},
    {"loop-scalar",
     "Bounded unflagged scalar motion and modular induction with conservative loop-pressure budgets",
     ir_loop_scalar, OPT},
    {"loop-bounds",
     "Hoist proven private global bounds out of simple loop headers",
     ir_loop_bounds, OPT},
    {"loop-bit-seed",
     "Replace even-bit seed loops with a count-leading-zeros calculation",
     ir_loop_bit_seed, OPT},
    {"pointer-induction",
     "Try bounded byte pointer recurrences under final code and spill gates",
     ir_pointer_induction, OPT},
    {"local-memory",
     "Reuse exact-pointer scalar loads within bounded same-block windows",
     ir_local_memory, TYPED_OPT},
    {"global-memory",
     "Reuse exact global-address loads and forward nearby private stores without crossing barriers",
     ir_global_memory, TYPED_OPT},
    {"constant-fold",
     "Fold unflagged scalar integer constants and propagate SSA literals",
     ir_constant_fold, OPT},
    {"aggregate-memory",
     "Legalize wide array-carrier loads/stores into scalar memory operations",
     ir_aggregate_memory, LEGALIZE},
    {"sret-forward",
     "Forward private aggregate results and remove adjacent private copy chains",
     ir_sret_forward, TYPED_OPT},
    {"copy-chains",
     "Remove adjacent exact-size copies through single-use private aggregate buffers",
     ir_copy_chains, TYPED_OPT},
    {"small-aggregate",
     "Remove overwritten private fills, pack byte pairs, and promote small records to SSA",
     ir_small_aggregate, TYPED_OPT},
    {"wide-division",
     "Lower general i64 division/remainder to owned restoring-division IR",
     ir_wide_division, LEGALIZE},
    {"dead-instructions",
     "Remove unused pure nontrapping SSA definitions with an indexed worklist",
     ir_dead_instructions, TYPED_OPT},
    {"scalar-promotion",
     "Promote direct scalar slots confined to one block or initialized once in "
     "entry",
     ir_scalar_promotion, TYPED_OPT},
    {"mem2reg",
     "Promote nonescaping scalar slots with pruned SSA and trivial PHI cleanup",
     ir_mem2reg, TYPED_OPT},
    {"sccp",
     "Propagate scalar integer constants on executable edges, repair CFG/PHIs "
     "and clean up",
     ir_sccp, OPT},
    {"libc-strcmp",
     "Opt-in SCCP of literal equality calls to the standard libc strcmp",
     ir_libc_strcmp, OPT},
    {"instruction-combine",
     "Eliminate typed neutral arithmetic and simplify proven integer cast "
     "chains",
     ir_instruction_combine, TYPED_OPT},
    {"low-bits",
     "Remove masks whose changed bits cannot reach observable single-use consumers",
     ir_low_bits, TYPED_OPT},
    {"linear-bias",
     "Combine constant offsets in unflagged two-term modular sums",
     ir_linear_bias, TYPED_OPT},
    {"scalar-cleanup",
     "Combine scalar expressions and forward equality/nonzero tests of "
     "extended i1 values",
     ir_scalar_cleanup, TYPED_OPT},
    {"local-cse",
     "Reuse matching pure arithmetic, comparisons, casts and addresses within "
     "a block",
     ir_local_cse, TYPED_OPT},
    {"loop-invariant",
     "Hoist proven nontrapping pure expressions from natural loops into "
     "existing preheaders",
     ir_loop_invariant, OPT},
    {"loop-cost",
     "Hoist one costly i32 invariant per natural loop with a bounded live-range "
     "budget and existing speculation checks",
     ir_loop_cost, OPT},
    {"target-strength-reduce",
     "Reduce unflagged i32 unsigned power-of-two div/rem with bounded "
     "target costs and quotient-sharing accounting",
     ir_target_strength_reduce, OPT},
    {"exact-pow2",
     "Rewrite signed divisions of proven exact power-of-two multiples",
     ir_exact_pow2, TYPED_OPT},
    {"divrem-pairs",
     "Share bounded signed and unsigned quotient/remainder pairs, lower proven exact signed divisions, and strength-reduce profitable unsigned powers of two",
     ir_divrem_pairs, TYPED_OPT},
    {"bounded-decimal",
     "Use an exact reciprocal for proven bounded unsigned decimal quotient/remainder pairs",
     ir_bounded_decimal, OPT},
    {"bounded-signed-reciprocal",
     "Try exact multiply/shift for a signed constant division of an extended byte or halfword",
     ir_bounded_signed_reciprocal, TYPED_OPT},
};
#undef LEGALIZE
#undef TYPED_OPT
#undef OPT
const unsigned badge_ir_pass_count =
    sizeof(badge_ir_passes) / sizeof(*badge_ir_passes);

const char *const badge_ir_default_pipeline[] = {
    "mem2reg", "scalar-cleanup", "constant-fold", "aggregate-memory",
    "wide-division", "local-memory", "loop-scalar", "cross-function",
    "global-memory", "sret-forward", "compact-inline", "low-bits",
    "linear-bias", "small-aggregate", "dead-instructions", "divrem-pairs",
    "guard-split"};
_Static_assert(sizeof(badge_ir_default_pipeline) / sizeof(*badge_ir_default_pipeline) <=
                   IR_PIPELINE_LIMIT,
               "the default IR pipeline must also be explicitly selectable");
const unsigned badge_ir_default_pass_count =
    sizeof(badge_ir_default_pipeline) / sizeof(*badge_ir_default_pipeline);
