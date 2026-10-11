# D08: DSP and scalar long-multiply decision

**GO for one bounded scalar widening-multiply selector (S08-LMUL).
DEFER packed DSP and automatic vectorization.** The approved opportunity is
ordinary signed long multiplication, not a requirement for the packed DSP
extension. No selector is implemented or enabled by D08.

## Applicability and actual beneficiaries

RP2350 implements the Cortex-M33 integer DSP extension; its lane, saturation,
accumulation and APSR effects require instruction-specific semantics.
See [instruction semantics](instruction-semantics.md) and
[coprocessor boundaries](coprocessors.md). DSP instructions execute on the
integer core, not on an independent coprocessor.

The specific candidate is **SMULL**, an Armv8-M Main scalar instruction:
signed 32-bit operands produce a full signed 64-bit result without updating
flags. Source: the pinned Armv8-M ARM **DDI0553B.y, C2.4.200, PDF/printed
page 877**, cataloged in [reference sources](reference-sources.json), SHA-256
`9a810da15f30d9e120b4e3238616d5d3a547cc23ba6f2c02c716c31f9851f3cd`.
The two destinations must differ; SP/PC operands are excluded. Do not confuse
this candidate with halfword multiply/accumulate or saturation.

[D08 evidence](../../build/datasheet/d08-results.json) finds five syntactic
`sext i32 -> i64` operand pairs feeding unflagged `mul i64`: four in IMU
`render_visual`, one in breakout `update_paddle`. Same-block casts occur in
all five. These are candidate sites, not five proved legal/profitable rewrites
or measured execution-frequency counts. No maze beneficiary is invented.

The current [C07 lowering](../../src/codegen.c) computes generic 64-bit
multiplication with two cross-product multiply operations, UMULL, an add and
a result move, plus loads/saves. The owned assembler already supports SMULL.
A two-instruction SMULL/return fixture assembles and links with text bytes
identical to LLVM's object under the owned linker. This checks one encoding,
not the proposed selector's semantics or register allocation.

## Correctness and freestanding implementation

Restrict the first selector to full signed i32 operands explicitly extended
to i64 and an unflagged i64 multiply. A signed 32-by-32 product fits signed
64 bits. Do not truncate arbitrary 64-bit inputs, substitute unsigned
extensions or infer signedness from source names. Preserve other uses of
the extensions and the exact result pair, ABI and live values.

Reuse typed IR/backend pattern analysis, existing pair-residency and scratch
rules, and the owned SMULL encoder. No helper library, SDK or runtime state
initialization is required. Do not add a floating/coprocessor dependency.
Unsupported forms continue through the generic lowering. Allocation effects
must be priced: fewer operations do not automatically mean fewer spills,
smaller frames or faster code.

Packed operations remain deferred until a reached pattern proves lane width,
signedness, overflow/saturation and packing semantics. Sticky Q and GE state,
flags and aliasing prevent arbitrary packed substitutions. Existing
assembler support for some DSP opcodes is not blanket semantic coverage.

## S08-LMUL boundary and acceptance

Add an explicitly selected widening pattern only; keep the generic control.
Use shared target costs for instruction bytes, conditional issue costs,
materialization and pair pressure. Verify actual allocated assembly, not
only shorter IR. Require no unexplained frame growth or lost fusion.

Test zero, +/-1, signed extrema and mixed signs, overlapping/live operands,
results across calls and PHIs, all supported residency/scalar policy pairs,
and negative unsigned/full-width/flagged forms. Compare exact 64-bit results
with LLVM/native sanitizer references; differential encoding tests need
more than the single D08 fixture. Run fresh 37-module/12-image and ABI/stack
checks, then D09 timing with reached IMU/breakout work. The opportunity is
small and bounded; its bytes, spill reduction and hardware speed remain
unmeasured until that implementation exists.
