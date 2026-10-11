# PO13: direct predicates and effect-preserving control selection

**Implemented and host-qualified as the explicit `thumb-control` candidate.**
It inherits PO12's `thumb-frames`. The qualified default remains `thumb-calls`;
display/LCD/USB physical timing must be qualified before promotion.

## Rules and safety

### Adjacent predicates

Single-use, adjacent compare/branch pairs now include `i8`, `i16` and pointer
predicates in global-compact residency. Narrow operands are normalized to
their declared widths; signed comparisons sign-extend both operands.
Pointer comparisons use unsigned address interpretation, including null.
Wide, extra-use and nonadjacent comparisons retain boolean materialization.
Flag-changing instructions between a comparison and branch prevent fusion.

There is no inter-instruction flag reuse: the selected branch emits its
comparison immediately before the conditional edge. Existing PHI edge-copy
handling remains intact.

### Decrement and flags

A plain unflagged `sub i32 %x, 1`, immediately followed by a single-use
`icmp eq/ne i32 %next, 0` and its branch, can emit `subs.w` directly into
the allocated result home and branch on its flags. It requires a register
home for `%next`, so no result spill/normalization can intervene.

The rule is arithmetic-equivalent for **every** 32-bit value, including
wrapping subtraction. It preserves the exact original control-flow graph;
it does not guess a trip count, convert pretests to posttests, or invent
an iteration. Original zero-trip guards and early exits remain.
It also applies to straight-line decrement predicates: it is not a delay
recognizer or a loop-name heuristic.

For `while (counter-- != 0)`, the predicate observes the old counter,
not `%next`. That shape is deliberately not fused by the decrement rule.
The wrapping observable exit value remains intact. Narrow counters, flagged
subtraction and any intervening instruction are excluded.

The existing allocator's PHI affinity/parallel-copy logic coalesces eligible
counter transport. The qualified countdown test has no PHI-cycle scratch
copy; genuine cycles retain their transfers.

### CFG edges around effects

Branch-to-next removal, condition inversion and final-block return fallthrough
are allowed in functions containing calls, volatile accesses and assembly.
These transformations change only control transport, never move or eliminate
an effectful instruction.

Bounded branch threading bypasses blocks containing **only** an unconditional
branch. It stops before a target containing PHIs, preserving predecessor labels
and incoming values. Self/cyclic chains remain bounded during analysis.
Intermediate blocks and labels may remain in emitted code, but eligible reached
edges bypass their trampolines; this is not a general unreachable-block deletion
pass. The shared CFG and allocator still analyze the original IR.

Old selectors retain their prior eligibility and emitted behavior.

## Qualification

[The persistent report](../build/po13-control-selection/results.json) compares
identical archived optimized IR across **37 modules, 554 functions and twelve
ordinary images**, using PO12 as control. The same maintained evidence engine
reuses archived source, objects and link identities; no frontend/optimizer
corpus or hardware matrix is repeated.

- All control objects and loaded binaries exactly replay PO12.
- **152 additional compare/branch pairs fuse.**
- **One production decrement/flag predicate fuses.**
- No per-module allocation spill, assembly spill, local frame or saved-register
  growth. Data/BSS and physical-region budgets are unchanged.
- All twelve images shrink, by **48-10,576 bytes**.

| Image | PO12 bytes | PO13 bytes | Saved |
| --- | ---: | ---: | ---: |
| button_mapper | 12,388 | 11,416 | 972 |
| diagnostics | 56,036 | 52,380 | 3,656 |
| gpio_events | 10,272 | 9,516 | 756 |
| gpio_monitor | 8,128 | 7,440 | 688 |
| hello_idle | 536 | 488 | 48 |
| imu_demo | 28,276 | 26,880 | 1,396 |
| lower_mapper | 12,624 | 11,652 | 972 |
| peripheral_probe | 15,496 | 14,348 | 1,148 |
| screen_test | 5,492 | 5,040 | 452 |
| tilt_breakout | 21,832 | 20,292 | 1,540 |
| tilt_maze | 33,660 | 31,604 | 2,056 |
| usb_wifi_udp | 363,436 | 352,860 | 10,576 |

Sanitizer-backed Cortex-M33 tests exercise all ten narrow predicates at
signed/unsigned boundaries and six pointer predicates at null/high-bit
addresses. Countdown tests cover zero, one, 31, 65,536, high-bit and wrapping
arithmetic, old-counter postdecrement exit values, nested loops, early exits,
volatile work, extra boolean uses and intervening effects. Both control and
candidate execute the countdown/source-loop oracle; their final ELF executable
section byte counts confirm a real encoding reduction.

Three LLVM/owned mixed/all-owned AAPCS executions cover indirect/recursive
calls and composite transport under the new layout. Retained PO12 mixed-ABI
and old branch tests pass. Resource-limit and persisted-report tamper tests
reject invalid results explicitly.

These are reached correctness and static size results, **not measured speed
or physical timing**. Faster busy waits and peripheral paths remain explicitly
gated; no badge is flashed or default promoted.

## Use and reproduction

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-control \
  bash badge-compiler/scripts/build-owned.sh
make -C badge-compiler test-control-selection
node badge-compiler/tools/control-selection-evidence.mjs \
  --capture badge-compiler/build/po13-new-capture \
  badge-compiler/build/po10-qualified-small-functions-v2/results.json \
  badge-compiler/build/po12-lightweight-frames/results.json
node badge-compiler/tools/control-selection-evidence.mjs \
  --verify-current badge-compiler/build/po13-new-capture/results.json
PO13_REPORT=badge-compiler/build/po13-new-capture/results.json \
  node --test --test-name-pattern='persistent PO13' \
  badge-compiler/tests/control-selection.test.mjs
```

Run from the project root with existing LLVM and QEMU tools on `PATH`.
Captures refuse existing directories, preserve failures and archive inputs/tools.
Historical verification rehashes artifacts and recomputes accounting; current
verification also checks current source/tool identities.
