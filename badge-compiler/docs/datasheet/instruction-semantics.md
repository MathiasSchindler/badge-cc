# Instruction semantics and actual target features

Evidence: [fact ledger](fact-ledger.json), IDs `RP2350-CONFIG-001`,
`RP2350-CONFIG-002`, `THUMB-ADD-001` through `THUMB-ADD-003`,
`THUMB-MOV-001`, `THUMB-MOV-002`, `M33-ALIGN-001`.
Exact editions and applicability are in the [source catalog](reference-sources.json).
Confidence is high for these bounded architectural/configuration claims;
unaligned-access permission is conditional on memory type and control state.

## Device, not a generic architecture superset

RP2350 uses Thumb execution on its Cortex-M33 with Armv8-M Main, Security and
DSP support. DSP is **integer instruction support**, not a separate accelerator
with independent registers. The enabled single-precision FPU is distinct from
the DCP. Double-precision loads/stores do not establish double arithmetic.
The chip explicitly has no MTB and does not use CDE modules. The newer
architecture manual also covers features such as MVE/PACBTI: their presence
in that manual does not make them legal on this chip.

Separate three questions: does the architecture define a form, does RP2350
implement it, and is it permitted in the application's actual execution state?
See [coprocessors](coprocessors.md) for the third question.

## Bounded encoding starter set

These are **ordinary-data** rules, not a complete assembler specification.
SP/PC aliases, restrictions and control-flow uses need their own references.

| Form | Bytes | Data operands/immediate | Flag property | Fact |
| --- | --- | --- | --- | --- |
| ADD immediate T1 | 2 | Low Rd/Rn; 0..7 | Sets flags outside IT | `THUMB-ADD-001` |
| ADD immediate T2 | 2 | Low destructive Rdn; 0..255 | Sets flags outside IT | `THUMB-ADD-001` |
| ADD immediate T3 | 4 | Modified immediate; register restrictions apply | Explicit S | `THUMB-ADD-002` |
| ADDW/T4 | 4 | Plain immediate 0..4095 | Does not set flags | `THUMB-ADD-002` |
| ADD register T1 | 2 | Three low registers | Sets flags outside IT | `THUMB-ADD-003` |
| ADD register T2 | 2 | Destructive destination; extended fields | Does not set flags | `THUMB-ADD-003` |
| MOV immediate T1 | 2 | Low Rd; 0..255 | Sets flags outside IT | `THUMB-MOV-001` |
| MOV immediate T2 | 4 | Modified immediate | Explicit S; expansion affects carry | `THUMB-MOV-001` |
| MOV register T1 | 2 | Extended register fields | Does not set flags | `THUMB-MOV-002` |

Consequences for D04:

- Width is not flag semantics: a `.w` suffix alone is insufficient.
- A compact two-address form needs operand ties satisfied, possibly with a
  copy whose size, dependencies and register pressure must be counted.
- Low registers create opportunities, but high registers are not uniformly
  wide. Do not sacrifice residency merely to satisfy a simplistic width rule.
- Existing [modified-immediate enumeration](../../src/thumb_immediate.h)
  should be reused rather than replaced by guessed numeric ranges.
- A rewrite must preserve APSR state that a later instruction consumes. See
  [registers and ABI](registers-and-abi.md) for implicit effects and call boundaries.

No form is selected differently in D01. New instructions/aliases require
additional exact encoding references before implementation.

## Memory legality precedes access profitability

`M33-ALIGN-001`: supported unaligned single accesses to Normal memory are split
into aligned bus transactions. Device memory and double/multiple transfers
have stricter alignment requirements; `CCR.UNALIGN_TRP` can reject otherwise
supported accesses. This is not permission to widen volatile/MMIO operations,
cross object or memory-region boundaries, or weaken proven alignment.

Treat architecture word alignment and AAPCS public-interface 8-byte alignment
as different requirements. Neither can be inferred from average access cost.
