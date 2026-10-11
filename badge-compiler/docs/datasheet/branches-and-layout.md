# Branches, folding and literal layout

Evidence: [fact ledger](fact-ledger.json), `M33-BRANCH-001`,
`M33-FOLD-001`, `THUMB-LITERAL-001`, `M33-FETCH-001`.
RP2350 sections 3.7.4.9.7-.9 and .12, PDF 130-133 / printed 129-132;
Arm ARM C2.4.79, PDF/printed 671-672.
Core timing inherits `M33-TIMING-BASE-001`: high-confidence documentary
conditions, not an exact whole-program predictor.

## Preserve the predicates

Taken cost: `basic + 2 + L + U - (K & F)`.
Not-taken cost: `basic + 1 - F`.

- `basic`: load cost for PC-loading branches; zero for the documented ordinary
  MOV-to-PC, B/conditional-B, BL and BX branch forms.
- `L`: branch arises from a load.
- `U`: target is not word-aligned, its first instruction is 32-bit, and the
  immediately preceding executed instruction is neither PUSH nor POP.
- `F`: branch is B, conditional B or BX LR; preceding instruction is 16-bit,
  was sequentially executed or word-aligned, and was not itself folded.
- `K`: direction was known before the preceding instruction; false for a
  conditional branch whose predecessor sets flags.

These Boolean conditions are not uniform costs by mnemonic. Folding eligibility
depends on actual dynamic entry/history as well as final byte layout.

## Avoid attractive but unsupported heuristics

- Aligning every block spends padding and fetch/cache capacity even where `U`
  cannot occur. Compare the relevant target's width and actual incoming paths.
- Return continuations after BL are also branch targets; not only basic-block
  labels matter.
- A flag-setting narrow predecessor can change `K`. Code size and dependency/
  branch costs need separate evaluation.
- Opcode 0xBF00 is the documented NOP folding case, not legacy 0x46C0.
  Folding prerequisites still apply; inserted NOPs are not inherently free.
- IT overhead may fold with a following 16-bit instruction, but a condition-
  failed body instruction still consumes a cycle. This is not arbitrary dual
  issue; overlapping multi-cycle operations save at most one cycle.

## Literal pools are part of the layout solution

For ordinary LDR literal:

- T1: 16-bit, low destination, unsigned imm8 scaled by four; offsets 0..1020.
- T2: 32-bit, positive/negative offset in -4095..4095.
- Architectural base: `Align(PC,4)`, not simply the byte address of the opcode.

Check final instruction/pool addresses, alignment and reach together with
relocations, branch ranges, padding and fetch effects. A positive T1 range is
not permission to place a pool anywhere within a symmetric 1 KiB window.
PC destinations/branches need separate rules.

The documented SRAM prefetch/literal-bank example is possible contention,
not a generic extra cycle for every literal. D02 should sweep exact layouts
before adopting an alignment or shared-pool placement policy.

## D06 implementation outcome

[Final-width layout](final-layout.md) implements opt-in `thumb-relax`:
structured branch relaxation and relocatable MOVW/MOVT address pairs,
with guarded literals retained for out-of-range symbolic addends.
Final linked bytes, padding, reach and instruction boundaries are qualified.
Shared-pool placement and blanket target alignment are not enabled;
static branch geometry does not establish dynamic folding or hardware speed.
