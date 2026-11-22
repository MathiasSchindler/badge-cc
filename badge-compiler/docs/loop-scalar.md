# PO15: pressure-aware pure loop motion and induction

PO15 originally added the explicit `loop-scalar` optimizer pass and qualified
it on the host/QEMU without a default change or badge-cycle claim.
[PO18](current-promotion.md) subsequently qualifies the combined chain on
both badges and promotes it. The legacy `loop-invariant` and `loop-cost`
experiments remain unchanged and explicit.

## Selection and safety

[The existing loop analyzer](../src/ir_loop_invariant.c) finds natural loops,
dominance and existing unique unconditional preheaders. The new policy proposes
at most one expression per loop and counts loop PHIs and externally defined
values used inside the loop. Incoming PHI seeds used only on outside edges do
not consume this budget. Calls lower the pressure limit from three to two;
opaque effects, including inline assembly, reject the loop.

Eligible motion covers unflagged integer arithmetic up to 32 bits, literal
in-range shifts, integer/pointer casts and non-`inbounds` scalar addresses.
Operands must already be available in the preheader. Loads, stores, division,
remainder, comparisons, flagged operations, poison/undef literals and unknown
effects never move. Volatile memory order, access width and calls are unchanged.
An existing eligible preheader expression used inside the loop prevents adding
another lifetime on repeated passes.

Without calls, a two-edge i32 induction PHI, constant add/sub latch update and
constant multiply may become a scaled PHI plus modular latch addition. The
multiply's original block must dominate the latch. Literal seeds are folded;
the original induction is removed only when its sole uses are that multiply
and its now-unused update. Otherwise counters and exit values remain intact.
The identity is modulo 2^32, not signed-overflow reasoning. The original
addresses, bounds and memory operations remain; this is **not** pointer-PHI
induction, loop unrolling or speculation of a memory access.

## Final-backend admission, not a guessed profitability score

The initial pressure-only experiment grew many production functions. Its
[failed economics probe](../build/po15-loop-probe/results.json) is retained,
not relabeled as qualification.

[Final admission](../src/ir_loop_admission.c) compiles the original and proposed
function under **direct/global-compact/thumb-operands/word**. It retains module
types, globals and declarations for correct address materialization, while
omitting other function bodies. [The shared owned assembler](../src/assembly_cost.c)
measures actual executable bytes after encoding/relaxation. Final frame bytes,
callee-save bytes and allocation spills must not grow, independently of code
size. Equal-size proposals may remain because repeated work can decrease.
Failure rejects every proposal in that function; it does not repeatedly search
or recompile alternative candidates. Backend/assembler failures diagnose and
fail the optimizer rather than silently masquerading as a rejected proposal.

This adds host compilation cost only to the explicit pass. Loop analysis keeps
its 50-million work budget; final admission additionally caps cumulative
original/proposed input at 64 MiB and uses the backend/assembler's existing
limits. Exhaustion fails explicitly and preserves an existing optimizer output.

Admission is specific to this backend policy. Selecting the pass with another
backend remains semantically valid, but **does not establish that backend's
profitability**. Zero-trip speculation can execute a little extra pure setup;
unknown small trip counts are not a promise of faster execution. General
hardware promotion still requires affected-workload timing qualification.

## Qualified results

[The persistent report](../build/po15-loop-scalar/results.json) uses the verified
PO14 archived production corpus:

- 37 modules / 554 functions / twelve images checked.
- Seven modules have admitted IR changes; only those seven are recompiled.
  Unchanged module objects and every PO14 control are reused.
- Graphics executable code: **1,716 -> 1,714 bytes**.
- Wi-Fi board executable code: **3,464 -> 3,458 bytes**.
- Other module executable sizes do not grow. No frame, save or allocation-spill
  growth is admitted.
- Three affected images are relinked. Alignment absorbs the graphics savings
  in two ordinary images; `usb_wifi_udp` saves **8 loaded bytes**. The other
  eleven image sizes are unchanged. Data/BSS, physical budgets and ELF-to-raw
  loaded-byte equality are checked.
- Of 29 proposed functions, seven are admitted; ten reject spill growth,
  seven frame growth, two save growth and three final code growth.

Eight sanitizer/LLVM-verifier/QEMU tests cover zero and one trips, 65,536
requested iterations with early exit, wrapping/strided induction, skipped
stores, nested loops, calls, address motion, flags/traps/poison, pressure,
idempotence, bounded failures and measured-IR output aliases. The real
`badger_gfx_clear` source fills all 76,800 pixels twice, in two colors; every
pixel is verified:

| Reached instruction oracle | Control | PO15 |
| --- | ---: | ---: |
| Shift/byte-extraction batch | 10,318 | 9,296 |
| Production fill plus full verification batch | 5,376,064 | 5,068,868 |

These count single-instruction QEMU trace entries, **not cycles or elapsed
badge time**. The fill batch includes verification loops and startup, not just
the clear function. Semihosting termination can vary a trace count by one.
The earlier 141-fixture loop-invariant/loop-cost suite also passes. Two static
evidence tests reject fabricated policy, savings, allocation and artifact facts.
Retained optimizer API/mutation tests and three existing metrics/default
contract tests pass.

The [red-square extra oracle](../../build/red-square-po15-candidate/events.json)
admits no IR changes: its proposed motion would grow a frame, and other loops
hit the pressure/opaque-effect gates. Byte-identical optimized IR lets us reuse
the previous image rather than compile it again. The PO14 chain therefore
remains **1,064 loaded bytes**, versus 560 for LLVM and 1,432 for the owned
default; PO15 does not shrink that particular demo.

The capture's first final verification only rejected Node's successful `spec`
reporter format. The report retains that failed report and a hash-checked
verifier-only finalization record; no compilation or runtime campaign was
repeated to correct accounting.

## Usage and reproduction

### Private global loop bounds

`loop-bounds` is a separate opt-in pass for a header that tests an induction
PHI against one private i32 global load, or the unflagged product of two such
loads. It moves that exact sequence into an existing unique preheader. The
header is always reached from the preheader, including for a zero-trip loop.

The pass checks the loop and its bounded nonrecursive chain of direct internal
calls for writes to either global. It follows actual pointer arguments and
simple byte addresses back to distinct private objects or fixed stack
objects. An unknown pointer, an indirect or external call, a recursive call,
volatile/atomic memory, or an opaque effect blocks hoisting. Inline constant
memory intrinsics are considered only when nonvolatile and their destination
is proven disjoint.

Admission compares the isolated function under `thumb-constants` and rejects
growth in encoded bytes, frame, saves, or spills. This pass is not part of the
default pipeline. To test it **after** a full sixteen-pass profile, apply it as
a standalone pass to the saved optimized IR:

```sh
badge-compiler/build/badge-opt --passes=loop-bounds input.ll -o bound-candidate.ll
badge-compiler/build/badge-codegen --scalar-selection=thumb-constants \
  bound-candidate.ll -o bound-candidate.s
make -C badge-compiler test-loop-scalar
```

The focused product fixture falls from 76 to 46 reached QEMU instructions.
In Maze, the hazard bound is legal but the current allocator grows its
spill count from 5 to 13, so it is rejected. The wall loop cannot prove the
effects of its external square-root call. An explicit stack-cache probe
was slower than unrestricted register hoisting and did not reduce the
hazard spill count. The admitted pass left the final Maze IR and all 123
replayed firmware modules unchanged; no hardware timing is implied.

### Pure scalar loop motion

For the historical pure scalar loop experiment, select `loop-scalar` with
the qualified backend:

```sh
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar \
BADGE_CODEGEN_SCALAR_SELECTION=thumb-operands \
  bash badge-compiler/scripts/build-owned.sh all
```

Focused validation and fresh capture:

```sh
make -C badge-compiler test-loop-scalar
node badge-compiler/tools/loop-scalar-evidence.mjs --capture \
  badge-compiler/build/po15-new-capture \
  badge-compiler/build/po14-qualified-operands/results.json
```

Static verification does **not** rerun compilation, QEMU or hardware:

```sh
node badge-compiler/tools/loop-scalar-evidence.mjs --verify \
  badge-compiler/build/po15-loop-scalar/results.json
```

`--verify-current` additionally checks source identities. Capture freezes host
tools, hashes inputs/artifacts, records commands and runtime results, and uses
`badge-ir-metrics --emit-ir=ready.ll` to emit measured IR in the same optimizer
run, avoiding a duplicate optimization for event collection.

No badge was flashed, no hardware campaign ran, and neither the owned defaults
nor the root LLVM firmware default changed.
