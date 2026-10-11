# D08: FPU decision

**DEFER end-to-end floating-point support. NO-GO for automatic FPU lowering
in the current integer optimization campaign or converting maze fixed-point
arithmetic to manufacture a beneficiary.** A future feature needs its own
requirements and qualification, not an implied D08 implementation milestone.

## Applicability and reached-code evidence

RP2350 supplies the FPv5 **single-precision** FPU, not general hardware double
arithmetic. [FPU facts](coprocessors.md) and [the ledger](fact-ledger.json)
(`M33-FPU-001`, `M33-FPU-002`, `M33-FPU-TIMING-001`) distinguish issue,
execution occupancy and result availability. One-cycle integer-core issue
is not one-cycle latency for every floating operator.

[D08 evidence](../../build/datasheet/d08-results.json) finds zero float/double
type tokens across all 37 optimized control modules after removing quoted
strings. This is scoped to that corpus, not a claim that no floating source
exists anywhere in the repository.

The current frontend parses float/double types and C05 emits some floating
arithmetic/comparisons/casts, but support is not end-to-end:

- `float f(float x) { return x+x; }` produces `fadd float`; LLVM's verifier
  accepts the emitted IR.
- C06 rejects that IR with `IR type nesting/length limit exceeded`.
  This is the current diagnostic boundary, not a claim of implemented scalar
  floating analysis or a newly fixed diagnostic.
- C04 rejects `1.0f` with `floating constants are not supported by this C04 subset`.
- C07 has no floating-register lowering; the owned assembler rejects
  `vadd.f32`. Negative output-preservation probes pass.
- C05 currently marks `use-soft-float=true`; the owned linker rejects
  nonzero VFP argument-ABI tags. Hard-float interoperability is not supported.

## ABI, initialization and semantics prerequisites

Retain **base PCS** as the first proposed interface: hardware arithmetic in
internal FP registers with integer-register/stack arguments (softfp-style).
This avoids silently changing existing public ABI, but still requires
deliberate compiler attributes/object tags and mixed-ABI tests. Hard-float
would be a separate ABI project. Existing integer i64/double representation
is not permission to reinterpret it as single precision.

[Startup](../../../src/arch/rp2350/start.c) does not explicitly initialize
CPACR. D02's 12 historical environment reports agree on CONTROL=0,
CPACR=`0x0000c000`, NSACR=`0x80`: port 7 permissions, not CP10/CP11 enablement.
CPACR_NS and FPSCR were not captured. Neither startup silence nor these
historical records determine the current playable firmware's FPU state.

Before lowering, specify coprocessor access/security setup, barriers,
FP register aliasing/allocation and ABI preservation; FPSCR rounding, FZ/DN
and exception status; interrupt use and lazy/eager FP stacking and stack
budgets. Define literal parsing, exact constant conversion, FP IR analysis,
NaN/infinity/subnormal/signed-zero behavior, comparisons and conversions.
Do not contract multiply+add, reassociate or change precision without the
required rounding/semantics contract.

## Dependency-free option, cost and future gate

A separately scoped single-precision feature could implement literals,
typed IR, encoding and base-PCS hardware lowering in the owned compiler.
Unsupported operations/double arithmetic need explicit diagnostics or
original freestanding helpers with fully specified semantics, not libc,
libm, SDK or opaque fallback symbols. A complete software floating library
is not a prerequisite for optimizing this integer corpus.

Cost includes transfers between integer/FP registers, saves, conversions,
code/stack growth and initialization/exception overhead. Independent FP work
may overlap differently from dependent chains; scalar VADD/VSUB/VMUL,
multiply-accumulate and divide/sqrt have different timing. No whole-program
speed or size gain is established.

Reopen only with a real reached floating workload and approved semantics.
Require boundary/oracle tests for rounding modes and special values,
mixed base-PCS calls, FP register live ranges, interrupt/security state and
stack limits, negative unsupported diagnostics and complete owned builds.
Hardware timing and recovery remain D09 gates. Until then preserve the
integer default, fixed-point games and explicit unsupported-feature behavior.
