# Badge compiler

An owned freestanding C toolchain for the GitHub Universe 2026 badge's
RP2350 Cortex-M33, using LLVM as an independent correctness and performance
oracle. It includes preprocessing, parsing, semantic analysis, IR generation
and optimization, Thumb code generation, assembly, ELF object writing, linking
and binary extraction. The root firmware build still defaults to LLVM.

## Directory layout

See the [RP2350 assembler roadmap](docs/badge-assembler.md) for planned
instruction coverage, source-address evidence and two-core synchronization
support.

| Directory | Contents |
| --- | --- |
| [src/](src/) | Compiler implementation, target support, object/linker code and production host utilities. |
| [tests/](tests/) | Native unit tests, driver mocks and executable LLVM/QEMU regression checks. |
| [tests/fixtures/](tests/fixtures/) | Hand-written C, assembly, LLVM IR and linker-script test inputs. |
| [scripts/](scripts/) | Build drivers, shared pipeline, capture wrapper and firmware integration configuration. |
| [tools/](tools/) | LLVM corpus oracle, static metrics, offline comparisons and physical badge comparison utilities. |
| [docs/](docs/) | Pipeline status, optimization plan, detailed contracts and qualification history. |
| `build/` | Ignored generated tools, intermediates, measurements and qualification evidence. |

The nine `.ll` files in [tests/fixtures/](tests/fixtures/) are required source
fixtures, not leftover compilation output. They exercise precise SSA/PHI,
optimization and backend edge cases that C alone cannot reliably produce.
Generated `.ll` intermediates belong under `build/`. Historical measurement
reports, archived captures and qualification firmware have been retained unchanged.

## Build and test

Run these commands from the project root:

```sh
# Build the native tools.
make -C badge-compiler all

# Build the default freestanding corpus, including the Wi-Fi host image.
# This does not flash badges or run a radio experiment.
bash badge-compiler/scripts/build-owned.sh

# Native and driver checks without LLVM target tools.
make -C badge-compiler test-owned

# Independent LLVM/QEMU differential checks.
make -C badge-compiler test-oracle

# Metrics/reporting checks and shared assembly-helper execution checks.
make -C badge-compiler test-metrics test-fast-io

# Execute the real maze source with owned and LLVM ARM code under QEMU.
make -C badge-compiler test-tilt-maze

# System-register/interrupt encodings and restoring Cortex-M33 runtime probe.
# Host-only; does not flash a badge.
make -C badge-compiler test-assembler-system

# Numeric labels, explicit literal pools and RP2350 assembly compatibility.
# Host-only, including LLVM and Cortex-M33 emulator checks.
make -C badge-compiler test-assembler-ergonomics

# Exclusives/acquire-release encoding, bounded lock and restoration tests.
# Host-only; physical two-core qualification is separately permission-gated.
make -C badge-compiler test-assembler-synchronization
```

BA05's bounded two-core diagnostic is now physically qualified on the
selected A4 badge, including contention, payload integrity, progress,
deliberate timeouts and verified original-flash restoration. See the
[qualification record and limits](docs/badge-assembler.md#ba05--multicore-synchronization-instruction-coverage);
this is not production C atomic support or a fairness/performance claim.

The oracle targets require LLVM and, for target execution, QEMU. See
[tool selection and boundary tests](docs/implementation.md#run-the-boundary-tests)
for supported tool paths and prerequisites. The standalone
[corpus oracle](tools/oracle.sh) captures production compilation boundaries;
its limitations are documented in the manual.

Scripts and tools now use their subdirectory paths; old root-level script
paths are not compatibility aliases. The root firmware Makefile and test
drivers use the new locations. Generated build output remains in the same
directories.

Firmware provenance IDs may change because their existing recipes hash
Makefile contents and input paths. Start fresh control/candidate campaigns
after a layout change; do not rewrite historical manifests to make old and
new provenance appear identical.

## Documentation

- [Pipeline status](docs/status.md): current owned stages and their supported scope.
- [Implementation manual](docs/implementation.md): contracts, acceptance gates,
  oracle usage, optimization qualification and historical evidence.
- [C06 optimization plan](docs/plan-c06.md): M-series and RP2350-focused P-series.
- [Datasheet-driven optimization plan](docs/datasheet-plan.md): traceable source
  extraction, target facts, hardware timing experiments and compiler milestones.
- [Datasheet reference corpus](docs/datasheet/README.md): qualified local RP2350
  extraction, page/section/table lookup and source-format limitations.
- [Physical badge comparison](docs/implementation.md#automatic-two-badge-compiler-comparison):
  explicitly selected badges, role-swapped correctness and runtime measurements.

Physical comparison tools replace both selected badge applications; they are
separate from the offline build and test commands above.

### Native Windows host build

The owned tools can also be built with Python 3.9+ (standard library only)
and a native Clang/GCC-compatible C compiler, without Make, Bash, WSL,
the Pico SDK, or LLVM target utilities:

```powershell
python badge-compiler\scripts\build.py
python -m unittest discover -s badge-compiler\tests -p test_native_tools.py -v
python pico2w\tools\build.py all --toolchain owned --owned-tools badge-compiler\build
```

Run these from the repository root. `--cc` selects the native host compiler
for the first command; the test runner uses `CC` or `cc`.
The Python builder produces all 13 normal tools and a `native-tools.json`
manifest under `badge-compiler/build/`. On Windows, the tools have `.exe`
suffixes. The existing POSIX Make/shell workflows remain available; this
entry point does not port the full shell/oracle test suite to Windows.

The host portability layer uses Win32 file identities instead of the CRT's
zero inode values, handles both path separators, retains output-alias
protection, and replaces completed temporary outputs without deleting the
old destination first. Binary I/O preserves CR/LF and Ctrl-Z bytes.
Object manifest serialization uses a checked temporary stream on Windows
where `open_memstream` is unavailable. The POSIX implementations are retained.
Windows paths currently use the ANSI Win32 APIs and the existing `PATH_MAX`
bound; arbitrary Unicode/long-path support is not qualified.

Portable regressions cover nested/include-once headers and hard links,
repeated output replacement, binary SHA-256, object manifest round trips,
assembler/linker/extractor alias rejection, the C-to-object pipeline, and
existing IR-pipeline, cost-model and ELF-reader C tests. A symlink test reports
an explicit skip if Windows does not grant symlink creation.

The complete headless Pico 2 W target builds through all owned target stages
and produces a structurally checked Arm Secure UF2. The target has no libc,
runtime libraries, SDK or external firmware dependencies; only the native
compiler tools use the host C library. LLVM is used independently to inspect
the owned ELF and compare extracted binary bytes, not as a hidden pipeline
stage. Physical Pico boot/USB behavior and generated-code execution still
require separate qualification. See the
[standalone Pico guide](../pico2w/README.md#owned-compiler).
Independent LLVM linking of the owned Pico objects produces a byte-identical
firmware binary, in addition to the ELF-extraction comparison.

The [maze compiler comparison](../docs/tilt-maze.md#compiler-comparison)
records the separate playable two-badge experiment, output-size findings,
opt-in deterministic benchmark and physical drawing timings.

## Current code-generation defaults

The owned CLI/API defaults are `ssa` and
`direct/global-compact/thumb-constants/word`. The sixteen-pass optimizer
profile appends `global-memory`, `sret-forward`, `compact-inline`, `low-bits`,
`linear-bias`, `small-aggregate`, `dead-instructions`, and `divrem-pairs`
to the preceding eight passes. Query the exact list with
`badge-opt --default-passes`. `divrem-pairs` also replaces profitable
unsigned power-of-two divisions and remainders with shifts and masks.
See the [qualification results](docs/current-promotion.md#november-default-update).
The [short Thumb and tiny-leaf follow-up](docs/current-promotion.md#short-thumb-instruction-and-tiny-leaf-follow-up)
adds five instruction selections and a bounded exception for inlining
tiny integer helpers.
The [constant arithmetic follow-up](docs/current-promotion.md#constant-arithmetic-and-bit-masks)
adds six more selections for masks, scaled integer operations and comparisons.
The [integer cast and wide comparison follow-up](docs/current-promotion.md#direct-integer-casts-and-wide-comparisons)
removes register moves and uses a shorter unsigned 64-bit comparison sequence.
The [wide arithmetic follow-up](docs/current-promotion.md#wide-integer-arithmetic)
shortens 64-bit shifts, signed power-of-two division, negation, bitwise
operations and multiplication.
The [constant-divisor specialization](docs/current-promotion.md#constant-divisor-specialization)
also uses native word division and a shorter restoring loop when the
wide divisor is a known positive constant below `2^31`.
The [runtime-divisor follow-up](docs/current-promotion.md#runtime-divisor-specialization)
also dispatches variable divisors to the word path when possible and
bounds the wide loop by the number of possible quotient bits.
The [address and register-copy follow-up](docs/current-promotion.md#address-and-register-copy-folding)
folds temporary addresses and register moves into their memory transfers.
The [stack instruction follow-up](docs/current-promotion.md#immediate-stack-adjustments-and-paired-stack-transfers)
uses immediate stack adjustments and combines eligible stack transfers.
The [call and return follow-up](docs/current-promotion.md#call-and-return-frame-simplification)
keeps returned results in ABI registers and simplifies safe call frames.
The [comparison and branch follow-up](docs/current-promotion.md#comparison-and-branch-lowering)
uses shorter boolean results and feeds 64-bit comparisons directly into branches.
The [ten follow-up experiments](docs/current-promotion.md#ten-follow-up-experiments-text-loops-and-control-flow)
qualify repeated-argument specialization and shared return tails, and record
the remaining runtime, size, and stack trade-offs.
The [IMU and Maze follow-up](docs/current-promotion.md#imu-labels-and-automatic-maze-guard-splitting)
caches labels in the IMU firmware and qualifies the explicit `guard-split`
pass on Maze.
The earlier `thumb-widths`/eight-pass profile remains explicitly selectable.
Target-costed optimizer defaults require the
badge backend's supported IR contract; the earlier explicit six-pass
pipeline remains available for broader standalone optimizer use.
The previous `instruction-combine` profile remains an explicit Z03 control.
The separate `loop-cost` experiment stays explicit-only
after a small repeatable graphics regression.
See the [contracts and qualification evidence](docs/maze-optimization.md#z04-candidate-contract).

The backend still incorporates the qualified P01/P03/P04/P05 and maze-driven
Z01/Z02/Z03/Z05 implementations; local residency and costed loop-invariant
motion remain explicit-only. Word memory selection requires proved alignment
and bounded nonvolatile ordinary memory; it does not widen arbitrary hardware
accesses.

The [Z02 allocator](docs/maze-optimization.md#z02-candidate-contract) adds
scratch-safe r4/r5 residency and copy/PHI affinities. Select
`--register-residency=global` or `BADGE_CODEGEN_REGISTER_RESIDENCY=global`
for the preceding qualified three-register control. Changing policies
requires fresh or forced target builds.

The [Z03 allocator](docs/maze-optimization.md#z03-candidate-contract) adds
normalized narrow-value residency and CFG-lifetime reuse of private scalar
homes, without sharing addressable buffers. Select `global-flex` explicitly
for the preceding Z02 control.

Z05's preceding qualified default `--scalar-selection=thumb-layout` (or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-layout`) inherits allocated-register
selection and removes redundant CFG/epilogue branches in eligible global-
residency functions. Calls, volatile accesses, inline assembly and integer-
to-pointer conversions retain old lowering. Select `thumb-registers` explicitly
for the preceding Z04 control; see the
[layout contract and results](docs/maze-optimization.md#z05-candidate-contract).

D04's opt-in `--scalar-selection=thumb-compact` (or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-compact`) inherits that layout and
chooses legal compact forms using already allocated registers. It changes no
allocation policy or defaults. All twelve corpus images have smaller linked
text, but repeated hardware timing includes a signed-div/rem regression;
this is a size-oriented candidate, not a promoted speed policy. See
[compact selection and qualification](docs/datasheet/compact-selection.md).

D05's opt-in `--scalar-selection=thumb-schedule` (or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-schedule`) instead inherits baseline
layout and applies bounded dependency-aware scheduling to typed, allocated
machine instructions. It preserves flags, side-effect barriers, allocation
and instruction widths, and introduces no moves/spills. Correctness and
corpus gates pass; D09 completes repeated suite timing with shared-code wins,
but production-runtime coverage remains open. Defaults are unchanged. See
[local scheduling and qualification](docs/datasheet/local-scheduling.md).

D06's opt-in `--scalar-selection=thumb-relax` (or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-relax`) inherits baseline layout,
materializes eligible symbol addresses with relocatable MOVW/MOVT pairs
and relaxes structured branches using final instruction widths. All twelve
fresh corpus images have smaller text; maze text decreases by 11.45%.
Data/BSS and allocation are unchanged. This does not compose D04/D05,
enable shared pools or blanket alignment. D09 rejects speed promotion after
resolved suite regressions.
See [final-width layout and qualification](docs/datasheet/final-layout.md).

D07 introduced standalone C06 `target-strength-reduce`, using the shared model to
replace unflagged unsigned i32 power-of-two div/rem with shifts/masks while
accounting for materialization, pressure and lost quotient sharing.
The standalone pass remains selectable for isolation. The original
qualification removed 35 UDIV and 17 MLS instructions, with no image or
frame growth. D09 found no resolved suite speed benefit. The default now
also checks finalized Thumb code and allocation before accepting a rewrite. See
[IR profitability, usage and qualification](docs/datasheet/ir-profitability.md).

D08 records [six independent memory/DSP/FPU/DCP/GPIOC/RCP decisions](docs/datasheet/README.md#d08-independent-decisions).
Only bounded scalar long-multiply, placement and explicit GPIO-helper
follow-ups are opened; floating support and RCP hardening remain separate.
No compiler/runtime features, defaults or badge images change.

D09 adds [independent qualification and promotion gates](docs/datasheet/qualification.md):
five fresh offline profiles and ten completed role-swapped hardware campaigns.
The collector observes selected-device detach before USB context teardown;
both original full flash images are restored and independently verified.
D04/D06 regress, D05 lacks production-runtime coverage, and D07 has no resolved
suite speed benefit. All candidates remain opt-in; defaults are unchanged.

The next evidence-driven milestones are in the
[pipeline optimization plan](docs/pipeline-optimization.md): actual application
timing, fixed-IR backend attribution, general lowering/allocation/IR
improvements and independent promotion gates.
PO00 is implemented as the host-only
[stage capture and crossover tool](tools/pipeline-stages.mjs);
see [usage, qualification and retained divergences](docs/pipeline-optimization.md#po00-implementation-and-reproduction).
PO01 adds the preservation-wrapped [actual application benchmark](tools/application-benchmark.mjs):
six production-source workloads, two seeds and four role-swapped rounds.
All 336 raw timing samples qualify, outputs/clocks match and both full flash
images are independently restored and verified. Current owned output remains
4.252-10.599x slower than LLVM `-Oz` on these batches. See
[results, reproduction and explicit physical/cold coverage gaps](docs/application-measurements.md).
PO02 adds [fixed-IR backend attribution](docs/backend-attribution.md):
11 identical-IR module crossovers, byte-preserving emission traces and four
semantic-checked paired hardware rounds. The backend-only gap remains
3.503-5.730x on measured batches. Adjacent single-use compare/branch fusion is
selected as the bounded first PO03 experiment; defaults remain unchanged.
PO03 implements that [general compare/branch candidate](docs/compare-branch.md)
originally as opt-in `--scalar-selection=thumb-branch` /
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-branch` under `global-compact`.
The 37-module/12-image corpus and mixed ABI checks pass; ordinary maze linked
text is 10.21% smaller. Repeated, role-swapped measurements on both badges
show 5.11-29.84% less elapsed time across all six reached workloads, outside
the exact noise envelopes, with no regressions. Both original full flash
images are restored/hash-verified. Defaults remain unchanged pending PO09;
the candidate still trails ordinary LLVM `-Oz` by 3.152-8.855x.
PO04 adds [scalar-home elision and direct PHI constants](docs/scalar-homes.md)
originally as opt-in `--scalar-selection=thumb-homes` /
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-homes`, based on PO03.
It preserves allocation/callee-save sets, removes unnecessary homes and PHI
buffer accesses, and shrinks 542 of 554 corpus frames without growth.
Ordinary maze text is another 3.24% smaller. Fresh paired badge measurements
qualify 2.00% generation, 1.67% physics/input and 0.59% diagnostics/graphics
improvements against PO03; rendering/transfer remain within noise, and frames
improve only for the dense seed. No regressions are detected. Both full
original images are restored/hash-verified; defaults remain unchanged.

PO05 adds [SSA expression/condition lowering](docs/ssa-expressions.md), selected
with `--expression-lowering=ssa` or `BADGE_IR_EXPRESSION_LOWERING=ssa`.
It preserves C value types, nested evaluation and short circuiting while
removing private scalar result slots and boolean round trips. Initial IR is
cleaner in 36 of 37 actual modules; both optimizer/backend execution routes
and cross-frontend type/layout checks pass. All 12 images shrink; ordinary
maze text is another 1.14% smaller than PO04. One Wi-Fi frame grows 8 bytes,
explicitly recorded for promotion review. Legacy frontend/default backend
policies remain unchanged.

Fresh frontend-only measurements on both badges qualify 6.83% faster
generation, 0.32% faster RAM rendering and 1.33% faster diagnostics/graphics
on both seeds; physics/frame improvements qualify on only one seed each,
and transfer stays within noise. All 1,344 samples match with no resolved
regressions. Both full originals are restored/hash-verified and left in
BOOTSEL. The ordinary LLVM `-Oz` gap remains 3.141-8.687x.

The frontend canonicalizes relocation-free, all-zero static storage to
`zeroinitializer`, allowing writable globals to occupy `.bss` rather than
flash-backed data. Constants, explicit sections and real pointer relocations
retain their existing placement rules.

PO06 originally adds opt-in [bounded local scalar load reuse](docs/local-memory.md).
The current default appends `local-memory`; the original qualified experiment holds
PO05 SSA frontend and PO04 backend identical. It reuses nearby exact-pointer
loads, treats every write/call/volatile access as a barrier, and adds no SSA
definitions or alias assumptions. Three of twelve images shrink, no frame
grows, and maze text is another 0.23% smaller. All 1,344 final physical samples
match; generation improves 0.84% on both seeds/boards/repeats, with no resolved
regressions. LLVM optimizes the same initial owned IR in this experiment.
Broader candidates with frame timing regressions are retained as rejected
evidence. Both originals are restored/hash-verified; defaults remain unchanged.

PO07 originally adds opt-in [ABI-safe call transport](docs/call-transport.md):
`--scalar-selection=thumb-calls` /
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-calls`, based on PO04 `thumb-homes`.
It captures incoming ABI register words directly and shortens outgoing stack
addresses, without changing allocation, saves, frames, call targets or result
transport. Bidirectional LLVM/owned ABI and genuine recursive/indirect
execution pass. Eleven of twelve images shrink; maze text is 1.62% smaller
against the independent PO05 SSA control. All 1,344 physical samples match;
physics, RAM rendering, transfer, reached frames and diagnostics qualify
0.45-2.88% elapsed-time improvements on both seeds/boards/repeats, with no
resolved regressions. Generation is within noise. PO06 is not combined
in this experiment, bounded inlining is not selected, both originals are
restored/hash-verified, and defaults remain unchanged.

PO08 maintains [assembler/linker control boundaries](docs/control-boundaries.md).
Thirty-nine exact assembly inputs and 72 linker runs across twelve actual
images qualify: owned loaded bytes are identical under both assemblers and
linkers. Independent relocation/value, function-layout, GC/NOLOAD, merged-data
and physical-budget checks explain eleven ordinary LLVM-object placement
differences, including two smaller LLD outputs. Object bookkeeping is not
counted as a firmware win. No pooling, relaxation, defaults or badge firmware
change is justified by this audit; earlier frontend/IR differences remain
separate work.

PO09 adds a maintained [pipeline promotion gate](docs/pipeline-promotion.md)
with seven fresh explicit profiles, actual-corpus/ABI qualification and
two-suite, two-seed, repeated role-swapped hardware checks. Offline and
combined-policy assembler/linker checks pass; combined maze text is 15.70%
smaller than the pre-promotion control with no frame/spill/image growth.
**Hardware qualification is complete:** 56 campaigns and 14,112 raw timing
samples match, with repeatable wins in all six reached-application and twelve
unrelated workloads for each candidate against the pre-promotion control, and no
resolved regressions. The independently qualified PO06+PO07 profile is now
the owned compiler default. Its inherited PO03/PO04/PO05 improvements are included.
Earlier alternative profiles remain explicitly selectable; D04-D07 and other
experiments with mixed results are not enabled. Historical milestone statements
above describe their original campaigns, not the current default.
Physical/live-peripheral, cold-XIP and whole-program interrupt/stack coverage
remain explicit limits, not newly certified by this promotion.
Historical USB/read/flash failures remain archived rather than accepted.
Both originals are restored/full-flash hash-verified in BOOTSEL.

The cold-booted [red-square size experiment](../docs/red-square.md) exposes
general tiny-function/backend gaps. The
[PO10-PO18 follow-up plan](docs/pipeline-optimization.md#general-follow-up-from-the-red-square-experiment)
prioritizes scoped assembly effects, lightweight frames, loop/control/address
lowering and costed sharing, with general production gates rather than
demo-specific optimizations or LLVM imitation.
PO10 is [implemented and qualified](docs/small-function-attribution.md):
identical optimized IR reproduces the full 872-byte backend size gap, with
six reconciled helper ledgers and a 37-module/554-function production
inventory. This is host-only attribution, not a new optimization or timing
claim; the existing corpus is reused and no badges are programmed.

PO11 adds [instruction-scoped inline-assembly effects](docs/assembly-effects.md)
as the explicit `thumb-effects` selector. Proven operand-free NOPs avoid
live-register spills, including when they carry a memory clobber; unknown
assembly remains conservative. The candidate inherits `thumb-calls` and
enables otherwise eligible adjacent compare/branch fusion in assembly-bearing
functions. Its 37-module host qualification removes three spill/reload pairs
and adds 41 fused comparisons. PO18 now qualifies the affected display/USB
timing and includes these transformations in the `thumb-widths` default.

PO12 adds [lightweight frames](docs/lightweight-frames.md) as `thumb-frames`,
inheriting PO11. Finalized register use determines saves and incoming argument
offsets; stack-free leaves omit frame setup, and bounded caller-saved residency
keeps short scalar values out of callee-saved homes. The twelve-image host
comparison saves 32-3,148 bytes versus PO11, without frame/save/spill growth.
The physical-timing promotion gate and existing default remain unchanged.

PO13 adds [direct control selection](docs/control-selection.md) as `thumb-control`:
normalized narrow/pointer branch predicates, adjacent decrement/flag selection
and effect-preserving layout/threading. Twelve images shrink by 48-10,576 bytes
versus PO12; correctness is host/QEMU qualified, not a hardware-speed claim.

PO14 adds [bounded address reuse and direct operand transport](docs/operand-selection.md)
as `thumb-operands`. It reuses available numeric/symbol constants, feeds aligned
scalar memory and eligible outgoing arguments from allocated registers, and
avoids duplicate narrow normalization. Twelve images shrink by 52-11,144 bytes
versus PO13 without frame/save/spill growth; physical timing remains gated.

PO15 adds [pressure-aware pure loop motion and modular induction](docs/loop-scalar.md)
as the explicit `loop-scalar` optimizer pass. Actual PO14 backend encodings and
final allocation facts reject growth rather than relying on an IR-only estimate.
The production corpus saves eight executable bytes, and the real graphics-fill
verification batch reaches 5.71% fewer QEMU instructions. Only seven changed
modules and three affected images are rebuilt; defaults and physical timing
remain gated.

PO16 adds [costed helper sharing and specialization](docs/cross-function.md)
as the explicit `cross-function` pass. Whole-module final-byte/allocation gates
include every retained generic helper and clone. Constant argument removal,
bounded inlining, tail-forwarding elimination and identical-helper sharing
remain alternatives to keeping a shared body. The production corpus saves
406 executable bytes; eleven images save 4-200 loaded bytes without frame,
save or spill growth. Actual USB/CRC and Wi-Fi helper batches reach fewer
QEMU instructions. Defaults and physical timing remain gated.

PO17 adds [selective final-width selection](docs/final-widths.md) as
`thumb-widths`, inheriting PO14 operands without historical compact
flag-setting arithmetic or scheduling. Flag-preserving narrow operations
and assembler-resolved branch/literal widths save 35,070 executable bytes
across the PO16-input corpus. All twelve ordinary images shrink, with
allocation, frames, saves and calls unchanged. Red-square reaches 984 loaded
bytes (LLVM: 560). Reached production instruction counts are unchanged;
physical flash/XIP speed and default promotion were subsequently qualified
by [PO18](docs/current-promotion.md).

RS01 introduced `--scalar-selection=thumb-constants` /
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants` as a candidate. It inherits
`thumb-widths` and adds flag-safe numeric synthesis, local address/constant
reuse around fenced opaque assembly, proven dead-temporary copy folding,
shifted power-of-two multiply/adds, direct i32 compare operands, branch-only
boolean PHI forwarding, trampoline removal and AAPCS-safe small arguments.
Bounded NZCV liveness through unconditional edges, equivalent narrow
arithmetic and range-proved conditional-branch reduction extend the candidate.
Shifted address temporaries fold into indexed word memory accesses only when
their overwrite-before-use and unchanged-base/index proofs succeed.
The [RS01 progress and evidence](docs/red-square.md#rs01-implementation-and-qualification-progress)
cover 37 fixed-IR modules and twelve smaller images without frame/save growth.
The compact demo drops from 992 to 772 loaded bytes, but sequencing remains
244 bytes versus LLVM's 184-byte target. That measurement did not qualify
RS01 for a default change; the later combined profile is documented above.

RS02 extends that explicit candidate with bounded shared-base lowering for
linear absolute-store leaves. It removes dead address temporaries and their
register saves without changing volatile-store order or strobe NOPs.
The [byte-transport qualification](docs/red-square.md#rs02-implementation-and-host-qualification)
reaches the **32-byte LLVM helper target**, down from 56 after RS01 and 74
under promoted defaults; the full demo is **748 loaded bytes**.
Direct RAM/ABI and encoding tests pass. RS02 is complete under the user's
approved [functional-hardware scope](docs/red-square.md#rs02-physical-acceptance---2026-10-11):
both badges passed role-swapped display tests and twelve cold boots total.
Electrical pulse widths remain unmeasured, and defaults are unchanged.
The original full flashes are preserved; the comparison deployment has since
been superseded by the RS03 campaign below.

RS03 adds bounded ordered-call-wrapper selection to the same opt-in candidate.
It forwards the incoming argument unchanged, preserves one shared address
base across the call and rematerializes store values without increasing
frame/save/outgoing demand. The
[command-wrapper qualification](docs/red-square.md#rs03-implementation-and-host-qualification)
reaches **24 bytes**, matching LLVM (44 after RS02, 56 under promoted defaults);
the complete demo is **728 loaded bytes**. Public/address-taken calls, both
compiler ABI directions, clobbering callees and ordered pre/post effects pass
host tests. That initial 728-byte combination was not flashed.

The subsequent
[internal-call preservation](docs/red-square.md#rs03-internal-call-preservation-and-physical-acceptance)
beats LLVM with a **20-byte wrapper / 724-byte image**, still using only
8 saved-register bytes. Conservative finalized register summaries include
hidden scratch and available internal callees; unknown, public, recursive
and forward-defined callees retain the rematerialization path.
Both badges passed the exact 724-byte candidate and LLVM control in swapped
roles, including **twelve cold boots total**. The user approved functional
acceptance with electrical timing explicitly unmeasured. That campaign left
owned 724 bytes on `9BBF88DD17D66E7D`, LLVM 568 bytes on
`08DA26F86E57F5EA`. Pre-campaign full flashes are preserved; defaults are unchanged.

The [RS04 table-interpreter qualification](docs/red-square.md#rs04-implementation-and-qualification)
now matches LLVM at **40 bytes** (112 after RS03), with a **644-byte loaded
image** and interpreter stack demand reduced from 40 to 16 bytes. General
scalar callback constraints, byte-pointer/PHI coalescing, bounded spill-free
low-register allocation, direct byte arguments and safe zero branches are
opt-in; pressured or unsupported functions retain conservative allocation.
The pixel-fill helper also improves from 52 to 42 bytes. Eleven targeted
tests, fifteen driver/default tests, pipeline checks, independent encodings
and frozen corpus/resource gates pass. Both badges passed **one cold boot
each** and the user accepted functional qualification with electrical timing
unmeasured. That campaign left owned **644 bytes** on
`9BBF88DD17D66E7D`, LLVM **568 bytes** on `08DA26F86E57F5EA`.
Full pre-campaign flashes and earlier qualification records remain preserved;
neither compiler defaults nor the root firmware Makefile are promoted.

The [RS05 pixel-fill qualification](docs/red-square.md#rs05-implementation-and-qualification)
beats LLVM's 36-byte helper at **34 bytes**, linking a **636-byte image**.
General pure callback-argument forwarding and flag-safe narrow shift/extract
selection preserve the ordered byte transfers and fill's 16 saved-register
bytes. Twelve targeted tests, fifteen driver/default tests, pipeline and six
evidence/tamper tests pass. The frozen 37-module/559-function/12-image corpus
has no resource or byte growth versus RS04 and saves 102 additional executable
bytes. Both badges passed correct-display testing and **one cold boot each**;
the user accepted functional-only qualification with electrical timing
unmeasured. That campaign left owned **636 bytes** on `9BBF88DD17D66E7D`
and LLVM **568 bytes** on `08DA26F86E57F5EA`. Full pre-campaign flashes are
preserved and the physical provenance/tamper tests pass. Defaults are unchanged.

The [RS06 counted-delay qualification](docs/red-square.md#rs06-implementation-and-qualification)
matches LLVM at **20 bytes**, removes the helper's four saved-register bytes
and links a **624-byte image**. General closed scalar counted leaves retain
every explicit NOP and use a stack-free incoming-register countdown. Unsupported
effects, CFGs, live exits and flagged arithmetic retain normal lowering.
Thirteen targeted tests, fifteen driver/default tests, pipeline and seven
evidence/tamper tests pass; actual reached NOP counts and bounded unsigned
boundary prefixes are checked. The shorter wait retains **24 million explicit
NOP cycles / 160 ms at at most 150 MHz**. Frozen corpus resources and bytes
are non-growing versus RS05. Both badges passed **one cold boot each** with
user-approved functional-only acceptance; electrical timing is unmeasured.
That campaign left owned **624 bytes** on `9BBF88DD17D66E7D`,
LLVM **568 bytes** on `08DA26F86E57F5EA`. Full pre-campaign flashes remain
preserved. Defaults and the root firmware Makefile are unchanged; RS01's
244-versus-184-byte sequencing target remains open.

The subsequent [maze deployment](../build/maze-rs06-hardware/results.json)
replaces that red-square pair with freshly built maze firmware:
owned **19,600 bytes** on `9BBF88DD17D66E7D`, LLVM **8,704 bytes** on
`08DA26F86E57F5EA`. The owned build explicitly uses `thumb-constants` with
RS01-RS06 improvements; broad defaults remain unchanged. Both full prior
flashes are preserved, both maze images are flash-verified and started,
the sanitized host maze tests pass, and the user confirmed both displays
and tilt response.

The opt-in `thumb-constants` profile now selects direct Thumb address forms
for single-index pointer arithmetic and static frame addresses. It uses
allocated base/index/result registers where available, folds power-of-two
scaling into the add, and uses nonflagging stack-relative forms when the
offset fits. It does not change allocation, memory access width or volatile
ordering. Other pointer shapes retain the general lowering.

Against the previous `thumb-constants` profile, a fixed-IR check across
37 modules saved 3,504 executable bytes with no module-size or measured
stack-resource growth. Fresh tilt-maze and diagnostics images saved 340 and
352 loaded bytes respectively; red square was unchanged. The Cortex-M33
maze oracle reached 4.05% fewer QEMU instructions, which is not an on-badge
speed measurement. Default output remained identical across the fixed corpus.
Run `make -C badge-compiler test-constant-selection` for the focused address
and encoding checks, and set `BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants`
when running `make -C badge-compiler test-tilt-maze` to exercise the candidate.

The same opt-in profile also adds a bounded loop-hotness bonus to global
compact allocation. It weights reachable natural loops (including PHI uses
on their incoming edges), caps the nesting bonus at two levels, and leaves
interference and ABI constraints unchanged. It does not split live ranges.
The conservative bonus saved 10,479 of 51,463,554 reached maze-oracle
instructions; direct frame traffic in the three profiled maze routines did
not change. Across 37 fixed-IR modules, executable size fell by 40 bytes net
with four small module increases and no measured stack-resource growth.
A stronger weighting was rejected after a 1.51% instruction-count
regression. Neither result is a physical speed qualification.

Subsequent opt-in backend changes, measured one at a time from that control:

| Change | Maze-oracle instructions saved | Fixed-corpus executable bytes saved |
| --- | ---: | ---: |
| Forward adjacent, same-width ordinary loads from the value just stored | 4,773 | 54 |
| Fold byte/halfword transfers and eligible immediate offsets into memory operands | 765,999 | 2,880 |
| Recompute eligible spilled static frame addresses instead of keeping pointer homes | 5,037 | 984 |

Forwarding retains every store and does not apply to volatile accesses,
different pointers or widths. Address folding keeps the original access and
requires the temporary address to be dead or overwritten. Frame
rematerialization retains the real objects; it removes only pointer homes
for addresses that fit a single instruction. It cut reached direct frame
loads/stores in `generate_maze` from 182,142 to 146,018. These are QEMU and
fixed-IR results, not badge timer measurements; the production default has
not changed.

Further opt-in changes are independently measured:

| Experiment | Host qualification |
| --- | --- |
| Reuse materialized addresses across safe single-entry labels | 16 fewer executable bytes across 37 modules; unchanged maze instruction count |
| `hot-inline` using LLVM IR function entry counts | Hot-call oracle: 110 to 90 executable bytes and roughly 2,193 to 1,169 reached instructions |
| Share repeated symbol addresses in a function-tail literal pool | Nine modules shrink by 710 executable bytes in total; maze oracle reaches 50,677,266 to 49,700,688 instructions |
| Group selected hot flash sections after boot metadata | Same 18,644-byte maze image size and QEMU instruction count; owned and LLD produce identical bytes for the new order |
| Select signed halfword and widening products | Five widening sites in production IMU/breakout IR save 184 executable bytes; reached breakout oracle: 1,754,927 to 1,753,007 instructions |

Address reuse is bounded to unreferenced generated labels or an immediate
sole jump to the label, with no inline assembly in the function. It does not
reuse memory values or merge arbitrary CFG facts. Literal pools are size
candidates for repeated symbol addresses, bounded by wide literal reach and
placed after the return; QEMU does not price XIP memory stalls.

Run `badge-opt --passes=hot-inline` to try the hottest internal straight-line
helpers first. Attach `!prof !0` to a function and define
`!0 = !{!"function_entry_count", i64 128}` for a measured count of 128.
The pass uses supplied counts; it does not collect a profile. Unprofiled and
zero-count helpers are skipped, and the existing whole-module code-size,
frame, save and spill gates still apply.

To relink existing function-section objects with a flash-only order, use
the [hot layout tool](tools/hot-layout.mjs):

```sh
node badge-compiler/tools/hot-layout.mjs src/arch/rp2350/memmap_flash.ld \
  build/maze-hot.ld generate_maze random_next draw_board collide_wall
make -C badge-compiler test-hot-layout
```

Pass the generated script to the same linker in place of the original.
Section garbage collection, memory regions and startup data placement remain
unchanged. SRAM execution and on-badge XIP timing were not qualified.
`SMULBB` uses the low signed halfword of each operand; no matching halfword
pair was found in the production corpus. Its encoding and behavior are
checked against LLVM at signed boundaries. Production gains above are from
`SMULL`, not multi-lane packed DSP.

[Compile Graph](../compile-graph/README.md) adds LLVM remarks and an identical-IR
backend view. Its maze comparison highlighted excess register transport.
`thumb-constants` now compares normalized narrow values directly for equality
and unsigned predicates, uses narrow register CMP encodings even with high
registers, and can materialize the boolean in its allocated destination.
Signed narrow predicates keep explicit sign extension. On fixed inputs this
saves 5,298 executable bytes across 37 modules; no module or measured frame
resources grew. The linked maze shrank from 18,644 to 18,184 bytes. The maze
oracle reached 32,335 fewer instructions in QEMU (0.065%); no physical timing
or default promotion is claimed.

The historical profiled Maze follow-up used `thumb-constants` and added
`global-memory` and `sret-forward` to the then-default eight-pass profile.
To reproduce that exact experiment rather than the current default:

```sh
passes=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function
BADGE_CODEGEN_SCALAR_SELECTION=thumb-constants \
  BADGE_OPT_PASSES="$passes,global-memory,sret-forward" \
  make -C badge-compiler test-tilt-maze
```

| Change, measured in order | Maze QEMU instructions saved |
| --- | ---: |
| Preserve callee-saved residency across simple direct calls | 915,967 |
| Fuse sole-use signed loads and emit extensions directly | 2,190,325 |
| Retain address facts across ABI calls and reuse nearby global loads | 1,004,258 |
| Forward private aggregate return buffers | 623,154 |
| Write standalone division results directly | 429,023 |
| Forward adjacent spilled addresses and use exact shift scaling | 1,697,385 |
| Fold in-range wide conditional branch pairs | 243,808 |

The `hot-inline` pass also accepts profiled straight-line helpers behind an
unconditional entry jump, with the same size and stack gates. Its focused
oracle measures 110 to 90 executable bytes and about 2,192 to 1,169 reached
instructions. This is separate from the maze count; the maze was not built
with a profile.

The combined maze reaches 42,564,433 versus 49,668,353 instructions and
2,431,002 versus 4,521,858 direct frame accesses. In a fixed 37-module
corpus, executable bytes fall from 145,152 to 141,176 with no module growing.
Some save areas and frames grow; the maze generation frame increases from
1,040 to 1,056 bytes, and its reached frame traffic increases. The opt-in
maze image shrinks from 18,184 to 17,420 flash bytes. These are QEMU and
offline measurements, not physical clock or XIP timing.

The original backend policies remain available for controlled comparisons:

```sh
BADGE_CODEGEN_FRAME_ADDRESSING=materialized \
BADGE_CODEGEN_REGISTER_RESIDENCY=stack \
BADGE_CODEGEN_SCALAR_SELECTION=legacy \
BADGE_CODEGEN_MEMORY_SELECTION=byte \
  bash badge-compiler/scripts/build-owned.sh
```

Reproduce the immediate pre-promotion PO09 control with:

```sh
BADGE_IR_EXPRESSION_LOWERING=legacy \
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division \
BADGE_CODEGEN_FRAME_ADDRESSING=direct \
BADGE_CODEGEN_REGISTER_RESIDENCY=global-compact \
BADGE_CODEGEN_SCALAR_SELECTION=thumb-layout \
BADGE_CODEGEN_MEMORY_SELECTION=word \
  bash badge-compiler/scripts/build-owned.sh
```

Query each default with `badge-codegen --default-frame-addressing`,
`--default-register-residency`, `--default-scalar-selection` or
`--default-memory-selection`. Force rebuilding or use a fresh build directory
when changing policies. See [default promotion and validation](docs/implementation.md#current-default-promotion-and-zero-storage)
for the broad test coverage, hardware measurements and retained limitations.
Also query `badge-ir --default-expression-lowering` and `badge-opt --default-passes`.

## Maze-driven optimization follow-up

[The staged plan](docs/maze-optimization.md) uses the actual maze and the broad
corpus to guide general compiler improvements. Qualified allocated-register
operand selection is inherited by `thumb-constants`. The preceding
Z04 selector without branch layout is explicitly selectable with
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-registers` or
`badge-codegen --scalar-selection=thumb-registers`.
It retains existing Thumb selection and, with global residency, operates
directly on allocated registers for eligible i32 operations. Other residency
policies retain their existing lowering. Use `BADGE_CODEGEN_SCALAR_SELECTION=thumb`
to reproduce the pre-Z01 scalar selector. Z01 passed broad tests, repeated
role-swapped hardware checks and the actual playable maze benchmark.

Z06 improves host compilation without changing generated firmware: indexed
SSA lookups, input-bounded token storage and buffered assembly formatting.
The reproducible [host profiler](tools/profile-host.mjs) uses
`make build/profile-codegen` from this directory and compares two archived
profiler executables on identical optimized inputs. Qualified corpus batches
use 30.46% less C07 CPU and 53.25% less process peak RSS; these are not
whole-build or badge-runtime speedups.

Stack reports now use iterative component analysis, support deep direct
chains, and expose known chains separately from unresolved entry/callback
bounds. Interrupt-inclusive bounds remain unproved. See the
[Z06 contract and measurements](docs/maze-optimization.md#z06-host-and-stack-analysis-contract).
