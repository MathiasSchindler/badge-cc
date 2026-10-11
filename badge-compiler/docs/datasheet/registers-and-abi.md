# Registers, APSR and ABI boundaries

Evidence: [fact ledger](fact-ledger.json), `M33-APSR-001`,
`AAPCS-CORE-001`, `AAPCS-SAVED-001`, `AAPCS-FLAGS-001`, `AAPCS-STACK-001`,
`AAPCS-ARGS-001`, `AAPCS-VFP-001`, `M33-FPU-002`.
Architecture reference: DDI0553B.y D1.2.4, PDF/printed 1463-1464.
ABI reference: AAPCS32 2025Q4, sections 6.1, 6.2.1, 6.5 and 7.1.
Confidence is high within the selected PCS and implemented register subset.

## Architectural effects

| State | Position | Meaning/use |
| --- | --- | --- |
| N | 31 | Negative signed result |
| Z | 30 | Zero result |
| C | 29 | Carry/last shifted bit, according to instruction |
| V | 28 | Signed overflow |
| Q | 27 | Sticky saturation/overflow; software clears it |
| GE | 19..16 | Parallel arithmetic lane state consumed by SEL |

These are not all interchangeable predicates. Each chosen encoding needs
explicit and implicit register/flag definitions and uses: condition evaluation,
carry operations, shifts, saturating/DSP operations and IT context must not be
lost in a future scheduler. Sticky Q is not a simple newly-computed result flag.
GE needs DSP support. APSR is not banked between security states; do not infer
transition preservation from banked general control registers.

The compact ADD/MOV cases in [instruction-semantics.md](instruction-semantics.md)
illustrate why flag behavior must be encoded independently of width. This
starter set is not a complete implicit-effects catalog for every opcode.

## Base public interface

- r0-r3: initial argument/result and scratch roles.
- r12: scratch, including linker/veneer use; do not hold a value across calls
  just because the source callee happens not to mention it.
- r4-r8, r10, r11, SP: preserved.
- r9: platform-specific, not universally spare or universally preserved.
- N/Z/C/V/Q/GE: not guaranteed across public interfaces.

AAPCS uses cross-architecture CPSR terminology. Apply its condition/status
contract to M-profile state without inventing A32 control/mode registers.
ABI preservation does not override compiler frame, PHI, scratch or platform
reservations. This milestone changes none of those policies.

Arguments follow staged marshaling rules, not one register per source
parameter. Double-word-aligned arguments round NCRN to even, a hidden result
address occupies r0 when required, and composite/stack allocation has additional
rules. Use the complete AAPCS procedure before expanding supported interfaces.

## Stack and floating-point interfaces

The stack is full-descending, bounded, word-aligned at all times and 8-byte
aligned at public interfaces. No assumed below-SP red zone is available.
Frame size, caller's outgoing space and exception/interrupt usage must be
accounted separately.

FPU presence does not select VFP argument passing. The non-variadic VFP PCS is
a separate interoperability choice; variadic interfaces use base marshaling.
s16-s31/d8-d15 are preserved; s0-s15/d0-d7 are scratch/argument/result registers.
These aliased views do not establish extra independent registers or NEON/MVE.

Lazy FP exception stacking reserves space even if stores are deferred.
Before emitting FP instructions, qualify runtime access, FPSCR modes, interrupt
context and stack demand as well as instruction encoding and ABI.
