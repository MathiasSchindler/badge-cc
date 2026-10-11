# DSP, FPU and RP2350 coprocessors

Evidence: [fact ledger](fact-ledger.json), `RP2350-COPROC-001`,
`RP2350-DCP-001`, `RP2350-DCP-002`, `RP2350-DCP-003`, `M33-FPU-001`,
`M33-FPU-002`, `M33-FPU-TIMING-001`, `RP2350-COPROC-TIMING-001`,
`RP2350-GPIOC-001`, `RP2350-RCP-001`.
RP2350 sections 3.6, 3.7.3.5 and 3.7.4.9.10-.11.
Semantics are documentary, high confidence; timings inherit the restricted
core baseline. Actual application-entry state is unresolved.

| Facility | What it supplies | What it does not imply |
| --- | --- | --- |
| DSP | Integer instruction extension on the core | An independent arithmetic engine |
| FPU | FPv5 single-precision arithmetic, FP register views | Double-precision arithmetic or hard-float ABI |
| DCP | Double-precision assistance via canned CPU/coprocessor sequences | Conventional full double FPU or strict automatic C substitution |
| GPIOC | Specific SIO GPIO operations through port 0 | Generic MMIO acceleration |
| RCP | Fault-detection/validation/canary mechanisms | General computation speedup or automatic security proof |

## State is part of legality

Secure coprocessor instructions need the corresponding CPACR permissions;
Non-secure use needs NSACR and CPACR_NS permissions. GPIO access masks are
additional constraints. Chip support alone does not establish usable runtime
state.

The inspected [startup](../../../src/arch/rp2350/start.c) sets VTOR, copies data,
zeros BSS and calls main; it does not explicitly initialize CPACR there.
That observation does **not** prove actual FPU/coprocessor access is disabled.
Bootrom handoff, image security and live control registers need observation.
D02's 12 historical reports all capture CONTROL=0, CPACR=`0x0000c000` and
NSACR=`0x80`, indicating port 7 permissions rather than CP0/CP4/CP10/CP11
enablement. CPACR_NS, FPSCR and GPIO_NSMASK were not captured. These records
do not establish current playable firmware's application-entry state.

## FPU costs and semantics

FP issue occupies the integer core for one cycle. FPU occupancy and result
availability are separate: scalar VADD/VSUB/VMUL execute in one cycle with
result delay; VMLA/VFMA occupy three cycles; VDIV/VSQRT fourteen, also with
result-delay qualifications. Following integer work need not wait for all FP
execution, while a following FP operation may wait for readiness.

An accumulator chain and an independent stream are different experiments.
Fused operations cannot simply be split or reassociated if required rounding
would change. FPSCR FZ/DN state, NaN/subnormal behavior, public calling
convention and lazy exception stacking must be qualified together.

## DCP is a sequence, not a C operator

Port 4 selects the current security state's DCP; port 5 selects the Non-secure
instance but is usable only from Secure code. Canned sequences include register
transfers and CPU mantissa multiplication. Engaged state needs the documented
save/restore protocol if an interrupt uses the same instance.

Standard sequences use nearest-even rounding but flush subnormal input/output
to zero. Faster divide/sqrt variants are not always correctly rounded. Those
are material semantic constraints: never silently map strict generic double
operations to them. Benchmark figures involving SDK helpers are not a ready
dependency-free compiler implementation.

Ideal CDP/MCR/MCRR interface transfers cost one cycle; MRC/MRRC add result
delay, and readiness can stall. This is not the latency of an entire DCP
operation. RCP delay variants additionally need the E26 caveat.

## D08 independent decisions

- [DSP/scalar long multiply](dsp-decision.md): GO for bounded signed widening
  multiplication using existing scalar SMULL; packed DSP remains deferred.
- [FPU](fpu-decision.md): defer end-to-end support pending a real floating
  workload, semantics, base-PCS ABI and initialization/context coverage.
- [DCP](dcp-decision.md): no strict automatic substitution; an explicit
  relaxed/FTZ API would require numerical and ownership qualification.
- [GPIOC](gpioc-decision.md): GO only for a bounded explicit-helper
  state/waveform pilot; no automatic volatile/MMIO rewrite.
- [RCP](rcp-decision.md): no speed feature; separate hardening requirements.

GO authorizes separately bounded work, not feature enablement. No coprocessor
instruction, default change or runtime dependency is introduced by D08.
