# Badge compiler: plan and LLVM oracle

For the stable stage-by-stage inventory of LLVM tools and project-owned
alternatives, see [pipeline status](status.md). This page contains the plan,
implementation contracts and validation details.

## Backend phase ownership after RF01

RF01 is a behavior-preserving extraction, not a new optimization or default
promotion. [codegen.c](../src/codegen.c) retains parsing, instruction lowering
and the public compilation API. [codegen_cfg.c](../src/codegen_cfg.c) owns the
existing preselection CFG transformations and the shared PHI-target query.
Its `cg_prepare_cfg` entry point runs, in the unchanged order:
boolean-edge forwarding, jump-block removal, guarded-decrement sinking and
byte-pointer advance scheduling. Existing admission checks, work/allocation
budgets, mutations and diagnostics are unchanged.

[codegen_frame.c](../src/codegen_frame.c) owns function generation and a common
`finish_function` path for ordinary lowering and counted-leaf emission.
That path finalizes the frame/return, emits finalized register summaries
where selected, closes the function symbol and clears the current function.
Counted leaves still intentionally bypass ordinary argument/body lowering
and its peepholes/statistics; the refactor does not broaden their proof.

All compiler, optimizer cost-admission and attribution builds include the
new module. Owned/oracle provenance and future constant-selection captures
also include its source.

[Exact-equivalence tests](../tests/backend-refactor.test.mjs) compare against
the immutable pre-refactor RS06 backend. The local RF01 campaign checks
46 IR inputs and 942 exact outcomes across all 16 scalar policies, including
46 attribution comparisons, six explicit negative cases, alternative
register/frame/memory settings and 13 complete identical images.
Fresh maze and red-square builds are also byte-identical, at 19,600 and 624
loaded bytes respectively. No badge is reflashed.

Twenty RS01-RS06/attribution tests, fifteen driver/default tests, seven
historical evidence/tamper tests, the pipeline checks and native optimizer
tests pass. The native codegen suite has an **unchanged pre-existing failure**:
`residency_tests` expects the historical fixed-frame prologue at line 386.
The frozen RS06 and refactored backends both abort there with byte-identical
diagnostics; that unrelated assertion is not modified or reported as passing.
See [RF01 validation and baseline-failure evidence](../build/rf01-backend/validation.json)
and the [exact comparison report](../build/rf01-backend/results.json).

Typed helper-selection plans, shared generated-assembly interpretation,
policy metadata consolidation and cost-interface changes remain separate
future rounds. They are not mixed into this mechanical extraction.

## Goal and scope

Incrementally replace the compiler toolchain for the project's freestanding
RP2350 Cortex-M33 C applications. Use LLVM as an oracle for investigation,
diagnostics and differential tests, and as the remaining downstream toolchain
while each replacement is developed. Our compiler need not reproduce LLVM's
IR, instructions, binary layout or optimization decisions.

There are two application scenarios:

| Scenario | Initial corpus | Compiler responsibility |
| --- | --- | --- |
| No Wi-Fi | The ten default applications (`make all`): startup, display, buttons, IMU, touch, diagnostics and graphics | Compile our freestanding RP2350 code; no radio firmware blob required. |
| Wi-Fi with vendor firmware | `usb-wifi-udp`: our freestanding host transport, firmware loader, link management, ARP/IPv4/UDP and USB reporting | Compile RP2350 host code and embed the unchanged vendor firmware/NVRAM/CLM data. The firmware inside the radio remains a supplied binary, not a compiler input. |

The existing USB/UDP lab image is a representative compilation corpus for
the second scenario, not a requirement that future applications use USB or
this network protocol. Compiling it offline does not run a radio experiment.
Generated build-ID headers and binary assets follow the existing Makefile's
recipes. See [vendor asset provenance and distribution limits](../../docs/wifi-assets.md);
generated blobs stay ignored and are not copied into compiler source.

Source-owned Wi-Fi-firmware experiments, CYW43439 CPU/PSM toolchains, and the
standalone register-probe `wifi-bringup` target are outside this compiler
project's scope. SDK apps are also outside the initial freestanding corpus.
No network credentials are captured. The offline pipeline/oracle scripts do
not flash badges or access USB. The separately invoked
[physical differential runner](#automatic-two-badge-compiler-comparison)
does replace both explicitly selected applications.

This contains the oracle/capture harness, our `elf2bin` image extractor,
`badge-cpp` C preprocessor, `badge-parse` syntax parser, `badge-sema`
semantic checker, `badge-ir` LLVM IR generator, `badge-opt` IR passes,
`badge-codegen` Arm backend, `badge-object` ELF object
writer, `badge-asm` Thumb assembler and `badge-link` linker.
The owned frontend now emits LLVM IR without invoking Clang. The oracle's
`owned` route now uses our C01-C12 tools and owned assembly preprocessing.
The `backend` and `owned-ir` routes retain LLVM optimization/backend
respectively for comparison. A separate `build-owned.sh` builds without
LLVM target tools; the LLVM oracle remains optional. The owned C06
supported-subset verifier now runs at every IR pass boundary.

## Correctness first, quality over time

**Current policy:** C05 defaults to `ssa`, C06 includes bounded `local-memory`,
`loop-scalar` and `cross-function`, and C07 defaults to
direct/global-compact/thumb-widths/word; see
[PO18's cached promotion and timing qualification](current-promotion.md).
The subsequent [maze-driven Z01 qualification](maze-optimization.md#z01-results)
promotes allocated-register operand selection, and
[Z02](maze-optimization.md#z02-candidate-contract) promotes scratch-aware
allocation. [Z03](maze-optimization.md#z03-candidate-contract) adds narrow
residency and private scalar home reuse. The earlier measurements below
retain their original policies.
Z05 adds qualified default `thumb-layout` scalar selection, preserving PHI transfers
while removing redundant CFG/epilogue branches in conservatively eligible
global-residency functions. Explicit `thumb-registers` preserves Z04 lowering;
instruction widths and literal pools are unchanged. See the
[contract and qualification](maze-optimization.md#z05-candidate-contract).
Z06 adds measured host-side SSA indexing, smaller token storage and buffered
formatting while requiring exact target-output preservation. Stack reports
use iterative component analysis with order-independent recursion results
and explicit known-chain/reachability information. See the
[host and stack-analysis contract](maze-optimization.md#z06-host-and-stack-analysis-contract).
D05 adds opt-in `thumb-schedule`, a fixed-window typed local scheduler with
physical register dependencies and conservative side-effect/flag barriers.
Allocation, widths and defaults remain unchanged. Correctness/corpus gates
pass; D09 completes repeated suite qualification after fixing collector teardown.
See [the scheduling contract and evidence](datasheet/local-scheduling.md).
D06 adds opt-in `thumb-relax`, baseline Z05 layout with relocatable
MOVW/MOVT symbol addresses and bounded structured branch relaxation.
Final instruction widths, directive padding and section positions converge
together. Linked-byte and execution qualification passes; defaults remain
unchanged; D09 rejects speed promotion after resolved suite regressions. See
[the final-layout contract and evidence](datasheet/final-layout.md).
D07 adds opt-in C06 `target-strength-reduce`, replacing eligible unsigned
i32 power-of-two div/rem using shared target costs, constant materialization
and lost quotient-sharing accounting. It adds no SSA values or motion.
Actual lowered-byte, pressure, ABI and corpus gates pass; the pipeline
default remains unchanged and D09 finds no resolved suite speed benefit. See
[the IR profitability contract and evidence](datasheet/ir-profitability.md).
D08 adds [independent memory and coprocessor decisions](datasheet/README.md#d08-independent-decisions),
not lowering or runtime enablement. Bounded scalar signed-long-multiply,
selected-function placement and explicit GPIO-helper follow-ups require
their own correctness/state/hardware gates; floating support and RCP
hardening remain separate. Existing defaults and production bytes are unchanged.
D09 adds [independent qualification and promotion](datasheet/qualification.md):
five fresh offline profiles, ten repeated hardware campaigns, exact rational
noise boundaries, observed-detach collector cleanup and two verified
full-flash restorations. No defaults change. D04/D06 have resolved regressions;
D05 requires production-runtime coverage; D07 has no resolved suite benefit.
Whole-program stack, physical maze and peripheral timing remain uncertified.
The P00-P06 sections retain their historical qualification settings and
results. Statements there about opt-in/default status describe those campaigns,
not the current CLI or API contract.

The first implementation may generate larger, slower code than LLVM and may
compile more slowly. That is acceptable if it implements the supported C
semantics and ABI, fits the badge's resources, and preserves application
behavior, including required timing and volatile hardware access.

The long-term objective is a compiler specialized for this codebase and one
platform. It may eventually beat LLVM on selected metrics; that is a measured
goal, not a guaranteed outcome. We do not need other architectures or unused
C features, but must diagnose unsupported constructs rather than miscompile
them.

Separate two types of tests:

- **Oracle plumbing tests:** the scripts below re-enter LLVM at different
  boundaries. Byte identity is a useful regression check for this unchanged
  LLVM pipeline and for embedding opaque vendor assets.
- **Our compiler's acceptance tests:** defined program behavior, target ABI,
  correct boot/layout/relocations, hardware interactions and bounded resource
  use. Different valid ASTs, IR, instruction sequences and firmware bytes are
  allowed. LLVM agreement is evidence, not the definition of correctness.

Measure flash code/constants separately from the fixed vendor-blob size,
static RAM, stack high-water mark, runtime cycles/latency, and compile time.
Record trade-offs instead of requiring the new compiler to improve every
metric or match LLVM from its first version. Performance budgets are soft
until explicitly set; physical memory limits and application timing contracts
remain correctness constraints.

## Current default promotion and zero storage

The first promotion selected shared [backend defaults](../src/codegen.h) of `direct` frame
addressing, `global` register residency, `thumb` scalar selection and `word`
memory selection. Z01 subsequently qualified `thumb-registers`; Z05 inherits
that selection and promoted `thumb-layout` as its scalar default.
PO09 promoted the independently qualified PO06+PO07 profile, using `ssa`
expression lowering, bounded `local-memory` reuse and `thumb-calls` selection,
including its PO03/PO04 predecessors. See the
[authorized promotion and byte-identical replay](pipeline-promotion.md#authorized-default-promotion).
PO18 subsequently promotes the combined PO11-PO17 chain to `thumb-widths`
with `loop-scalar,cross-function`; see [current defaults and timing contracts](current-promotion.md).
Z02/Z03 promote `global-compact` residency through scratch-aware allocation
and private home reuse; see the [follow-up plan and results](maze-optimization.md).
CLI default queries, help and convenience API entry points
use the same definitions. Partially explicit APIs inherit these defaults only
for unspecified policies; the fully explicit selections API and driver
environment variables still support materialized/stack/legacy/byte controls.
C06 uses `mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function`.
P02 local residency and P06 loop-invariant
motion remain explicit-only because their earlier results were mixed.

This promotes existing general implementations, not application-specific
patterns. Frame offsets fall back when the direct encoding is unavailable;
the global allocator retains conservative spills and ABI preservation.
Word copy/fill selection still requires proved alignment, bounded length and
nonvolatile ordinary memory, including exact tails. Arbitrary loads/stores,
volatile accesses and MMIO are not widened by this selection.

The C05 [static initializer lowering](../src/badge_ir.c) now emits
`zeroinitializer` for relocation-free objects whose complete representation
is zero. Literal null pointers need no relocation. Real symbol and nonzero
pointer initializers preserve their existing lowering. Writable default-section
globals therefore use the backend's existing `.bss` path; constants retain
readonly placement, and explicit sections retain their placement semantics.
This fixes wasted flash across the corpus rather than recognizing a framebuffer
or maze variable by name.

Validation includes 264 parameterized scalar/array/record/union/pointer storage
combinations across initialization, qualification and explicit-section choices,
plus pointer relocation preservation/override checks. Frontend IR verification,
the backend sanitizer policy matrix and CLI/API consistency checks pass.
`test-owned`, `test-oracle`, `test-fast-io`, `test-tilt-maze` and `test-metrics` pass, as do
owned builds of all eleven default applications plus the Wi-Fi UDP host image.
No radio experiment was performed.

Physical timing uses the unchanged version-3 six-module/six-kernel suite,
seed `3276377845`, the same LLVM control image and both badges with swapped
roles. Complete campaigns
[`run.p0w9PQ`](../../build/compiler-compare-runs/run.p0w9PQ/results.json) and
[`run.tJfux2`](../../build/compiler-compare-runs/run.tJfux2/results.json) match
every LLVM fingerprint and recover both devices to BOOTSEL. Compared with
the same-source conservative control
[`run.ll0v7g`](../../build/compiler-compare-runs/run.ll0v7g/results.json),
owned median tick reductions are:

| Workload | Reduction in each repeated candidate campaign |
| --- | ---: |
| Arithmetic | 8.79-8.80% |
| Strings | 45.83% |
| Memory | 12.04-12.05% |
| Control flow | 14.19% |
| Cross-file ABI | 26.22-26.24% |
| Graphics | 9.57-9.58% |
| Arithmetic kernel | 22.43% |
| Branch/PHI kernel | 22.46% |
| Cross-file call kernel | 9.16% |
| Unsigned div/rem kernel | 11.04-11.05% |
| Signed div/rem kernel | 22.41-22.42% |
| Ordinary-memory kernel | 11.90% |

The [first](../../build/compiler-compare-runs/improved-defaults-timing-1.json)
and [second](../../build/compiler-compare-runs/improved-defaults-timing-2.json)
comparisons validate clocks, source identity, image hashes, fresh nonces,
fingerprints and timing bounds. Ratios use the geometric mean of paired
owned median tick ratios across device roles. LLVM-control drift is at most
0.07%. The owned compiler still takes about 3.56-8.48 times LLVM's ticks in
the successful improved campaigns; these are improvements, not LLVM parity.
Both control and candidate use the new C05 zero-storage lowering, so this
comparison isolates backend policies, not the storage change.

Two other attempts,
[`run.jhDaN3`](../../build/compiler-compare-runs/run.jhDaN3/) and
[`run.aZIucK`](../../build/compiler-compare-runs/run.aZIucK/), hit the
unchanged 60-second host collector deadline. Both had written the complete
1792-byte result buffers for the failing round; the collector saves these
before USB cleanup. The precise timeout cause was not established. They
remain failed campaigns, excluded from timing comparisons; both devices
recovered and unchanged retries passed. No deadline was relaxed.

The actual maze's uninstrumented owned flash payload falls from 280,032 to
55,728 bytes (-80.10%); text falls from 119,576 to 55,712 bytes (-53.41%),
and 160,440 bytes move from initialized data to `.bss`. Static application
RAM is unchanged. See the [maze follow-up](../../docs/tilt-maze.md#compiler-improvement-follow-up)
for images and the separate playable benchmark: the user confirms both
restored games work and reports owned drawing 118,417 -> 56,202 ticks
(52.54% fewer; 2.11 times faster), versus LLVM's 7,719 ticks. The remaining
physics gap is 5,403 owned versus 377 LLVM ticks per step; no original owned
physics reading is available. These maze readings are one fixed badge
pairing, not the role-swapped broad-suite measurements. Production stack/interrupt/
Wi-Fi readiness limitations from the earlier qualification remain unchanged.

## Automatic two-badge compiler comparison

[compiler-compare.mjs](../tools/compiler-compare.mjs) builds the same freestanding,
non-Wi-Fi test application with LLVM and our complete owned C01-C12 pipeline.
It flash-verifies the two explicitly selected badges, collects deterministic
per-module results through live USB, validates them independently in Node,
compares all six result lanes and confirms both badges return to BOOTSEL.
No buttons, visual judgment or Wi-Fi connection are needed for a successful
cycle. The graphics module also draws its framebuffer on both displays.

**This replaces both applications.** Start with both badges in BOOTSEL:

```sh
LLVM_BIN=/opt/homebrew/opt/llvm/bin HOST_CC=/usr/bin/cc \
  node badge-compiler/tools/compiler-compare.mjs \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

The first serial receives LLVM, the second owned code. `LLVM_BIN` is optional
when `clang`, `llvm-objcopy`, `llvm-nm` and `llvm-size` are on `PATH`; LLVM's
LLD must also be discoverable. Host requirements are Node, make, a C compiler,
`pkg-config`/libusb and USB-enabled picotool at `build/picotool-usb/picotool`
(or `PICOTOOL`). These are host tools, not target runtime libraries.
The owned build uses no LLVM target tool and embeds no vendor radio payload.
The two builds use separate object directories and the same isolated
[64 KiB stack layout](../scripts/memmap_compiler_test.ld), with ordinary application
RAM confined below `0x2006f000`. Production stack settings do not change.

### On-badge execution timing

Add `--timing` to measure the same six workloads plus six P00 isolated kernels, then automatically repeat
with the LLVM/owned images swapped between the two physical badges:

```sh
LLVM_BIN=/opt/homebrew/opt/llvm/bin HOST_CC=/usr/bin/cc \
  node badge-compiler/tools/compiler-compare.mjs --timing \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

Both timing and ordinary comparisons now use the qualified PO09 compiler
default unless `BADGE_OPT_PASSES` is supplied:
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory`.
The runner queries `badge-opt --default-passes` for provenance rather than
maintaining a separate timing default. LLVM retains the normal `-Oz`
freestanding flags, with no LTO. Timing builds use isolated
`compiler-llvm-timing`/`compiler-owned-timing` directories.

For controlled pass ablations, put `--seed=3276377845` after `--timing`
(or before the serials in ordinary mode). The optional decimal seed accepts
exactly 0..4294967295; omitting it retains random workload seeds. Every run
and role-swapped round still gets fresh random nonce bytes beyond the seed.
Use identical seed, suite source, repetitions and clock snapshots for
before/after comparisons; a fixed seed is not a replayed request nonce.

For each module the firmware first computes an untimed reference result,
performs one additional warm-up, then collects seven samples. Each sample
executes 64 complete module calls, except graphics, which executes one
framebuffer workload. Setup, correctness comparisons, USB polling, watchdog
renewal and physical display transfers are outside the timed interval.
Graphics timing versions **2 and 3** include full-frame drawing into RAM and a
CRC/fingerprint of every fourth pixel (19,200 sampled pixels), not SPI
transfer or panel refresh. The ordinary correctness mode still fingerprints
all 76,800 pixels. Version 1 used the full-frame CRC inside timing; its legacy
compiler exceeded the unchanged one-million-tick bound. The decoder retains
version-1/2 reads but rejects mixed/unknown timing versions. Version 3 adds
isolated kernels without changing the version-2 graphics workload. Do not compare
graphics timings across versions. Dispatch, result initialization and batching-loop
cost are included. Separate translation units and no LTO keep the repeated
external workload calls observable.

### P00 isolated kernel baseline

[kernels.c](../../src/tools/compiler_test/kernels.c) adds arithmetic recurrence,
branch/loop-carried PHIs, cross-file calls to the existing `compiler_mix`,
unsigned and signed variable division/remainder, and aligned ordinary-RAM
word transformation kernels. All integer arithmetic is defined: unsigned
wrap is intentional, shifts are in range, divisors have magnitude 3-130,
and signed dividends exclude division-overflow inputs.

[kernel_timing.c](../../src/tools/compiler_test/kernel_timing.c) selects the kernel,
prepares inputs and computes a reference outside timing. After a warm-up,
each of seven samples runs **64 calls of 128 iterations**. Buffer setup,
source/destination CRCs, source-immutability and boundary guards, result
comparisons, USB/display servicing and watchdog renewal are outside both
clock reads. Every returned value is captured and checked after the interval;
the batch loop, indirect-call/prologue/epilogue and result-capture store costs
remain included. Separate translation units and no LTO prevent repeated calls
from being discarded. The existing whole-module timings still include their
fingerprints and are reported separately, not replaced by these kernels.

With explicit authorization, two fixed-seed (`3276377845`) campaigns each
completed both physical compiler assignments, with fresh request nonces and
identical source-suite identity, images, work and clocks. All six modules
and all six kernel fingerprints/guards matched in every round:

| Isolated kernel | First owned/LLVM ratio | Repeat owned/LLVM ratio |
| --- | ---: | ---: |
| Arithmetic | 13.837x | 13.836x |
| Branch/PHI | 17.307x | 17.305x |
| Cross-file call | 12.158x | 12.160x |
| Unsigned div/rem | 9.901x | 9.901x |
| Signed div/rem | 8.142x | 8.143x |
| Ordinary memory | 17.255x | 17.246x |

Raw kernel samples span 701-17,951 inherited TIMER0 ticks, within the unchanged
100-1,000,000 bounds and 1% empty-read-overhead gate. Paired owned kernel
medians vary by less than 0.02% between these two campaigns; this is observed
repeatability, not a statistical confidence claim. Reports retain all samples,
minimum/median/maximum, physical roles, nonces/seed, clocks, overhead, source
identity, UF2 hashes, LLVM version, individual owned-tool hashes (including C07)
and selected C06 passes:
[baseline](../../build/compiler-compare-runs/run.hxehji/results.json),
[repeat](../../build/compiler-compare-runs/run.drXj3t/results.json).
Verified LLVM/owned UF2 copies are also retained in those run directories;
the [baseline archive manifest](../../build/compiler-compare-runs/run.hxehji/image-archives.json)
and [repeat archive manifest](../../build/compiler-compare-runs/run.drXj3t/image-archives.json)
match their recorded image hashes. The runner now archives and flashes
per-run verified UF2 files rather than relying on mutable shared build paths.

For a subsequent optimization, run the same command with the same seed twice
before and twice after the change. Compare complete baseline/candidate
campaigns with the host-only utility:

```sh
node badge-compiler/tools/compiler-timing-compare.mjs \
  build/compiler-compare-runs/run.hxehji/results.json \
  build/compiler-compare-runs/run.drXj3t/results.json
```

[compiler-timing-compare.mjs](../tools/compiler-timing-compare.mjs) validates schema/version,
fresh nonces, source identity, seed/work, swapped physical roles, clocks,
raw-sample statistics, bounds, fingerprints/guards and an unchanged LLVM
toolchain/control image. It computes candidate-versus-owned-baseline median
ratios paired by physical role, rather than mistaking LLVM-relative ratios for
an optimization gain; LLVM timing drift is reported separately. Its JSON output
also records the images, toolchains, clocks and per-role nonces/medians.
The [repeat comparison](../../build/compiler-compare-runs/run.drXj3t/repeat-comparison.json)
shows near-zero change because these two campaigns use identical compiler images.

Both normal/timing sanitizer profiles, expanded USB page-boundary tests,
protocol/runner failure tests and offline-comparison guards pass. LLVM and
owned timing firmware builds pass, and the physical comparisons validate
execution of both. Emitted owned code retains cross-file `BL`, hardware
`UDIV`/`SDIV` and quotient/remainder paths; there is no source-level native
helper substitution. The default compiler policy, clocks, watchdog, collector
deadline and timing safety bounds are unchanged.

Final-source ordinary [full-frame correctness](../../build/compiler-compare-runs/run.BIKFMy/results.json)
also matches all six modules. Both badges were recovered to BOOTSEL after
each campaign; the last flashed images restore ordinary diagnostic roles
(LLVM on `9BBF88DD17D66E7D`, owned on `08DA26F86E57F5EA`).

P00 measures a warm-working-state baseline; it makes no compiler optimization,
cold-XIP, total-stack-safety, interrupt-latency, physical-display-rate or
radio/production-readiness claim. Added code can change flash placement, so
these version-3 images form the new controlled baseline, not a before/after
attribution against older version-2 binaries.

Measurements use unsigned differences of RP2350 TIMER0's raw low-word
counter, including wraparound. Units are **inherited timer ticks**, not
assumed microseconds or hardware CPU-cycle counts. Seven empty clock-read
samples are reported without subtracting them. Every workload sample must
be 100-1,000,000 ticks and at least 100 times the maximum empty-read cost.
Timing-mode summary reads allow five seconds while the target deliberately
does not poll USB inside a measured block; the 60-second collector deadline,
progress watchdog and target lifetime remain bounded.

The device records system/reference clock dividers and selection, TIMER0
tick/source settings, and both PLL configurations/status. Timing mode clears
inherited sticky PLL `LOCK_N` history using write-one-to-clear while preserving
divider/bypass settings; new lock-loss events or clock changes fail the run.
Clock configurations must agree between badges. No clock rates are changed.
The role swap further reduces fixed device-specific bias.

All sampled batches' final results must match their untimed reference, and
both compiler paths must agree on all six correctness lanes before a final
timing comparison is published. A fresh nonce is used per round, with the
same seed in both rounds. The report keeps raw samples, minimum/median/maximum
batch times, repetitions, clock snapshots, empty-read cost, build/tool/pass
provenance and assignments. The final owned/LLVM ratio is the geometric mean
of the two role-swapped median ratios; `(ratio - 1) * 100` is percent slower.
This is a repeated, warm-working-state workload comparison, not a cold-start,
interrupt, radio, display-latency or statistical confidence-interval claim.

On 2026-10-08, two complete role-swapped runs with independent seeds passed
all module results and recovered both badges:
[first timing report](../../build/compiler-compare-runs/run.55B5xe/results.json)
and [repeat timing report](../../build/compiler-compare-runs/run.3H7x0J/results.json).

| Workload | First owned/LLVM median ratio | Independent-seed repeat |
| --- | ---: | ---: |
| Arithmetic | 12.840x | 12.835x |
| Strings | 16.924x | 16.991x |
| Memory | 17.834x | 17.844x |
| Control flow | 12.624x | 12.599x |
| Cross-file ABI | 14.591x | 14.583x |
| Graphics and framebuffer CRC | 15.819x | 15.819x |

These are whole-module M05-pipeline measurements, not a universal slowdown
factor or an attribution to one C06 pass. In particular, the existing C07
spill-heavy backend remains part of the comparison.

The [ordinary default-pipeline hardware regression](../../build/compiler-compare-runs/run.wNXSHz/results.json)
also passed all six modules and recovered both badges, without timing mode.

### Correctness modules and transport

| Module | Work per seed | Coverage |
| --- | ---: | --- |
| [Arithmetic](../../src/tools/compiler_test/arithmetic.c) | 256 iterations | Unsigned wrap/multiplication, boundary shift counts, signed/unsigned division and remainder, 64-bit carry and constant shifts |
| [Strings](../../src/tools/compiler_test/strings.c) | 8 cases | Decimal/hex formatting including signed extrema, reversal, lengths and byte fingerprints |
| [Memory](../../src/tools/compiler_test/memory.c) | 64 sizes | Zero-length and unaligned copy/equality, guard bytes, byte-width word loads/stores and zeroing |
| [Control flow](../../src/tools/compiler_test/control.c) | 128 iterations | Nested loops, break/continue, switch, primes, Euclidean GCD and integer square root |
| [Cross-file ABI](../../src/tools/compiler_test/abi.c) | 64 calls | Indirect function calls, struct arguments/returns, narrow fields, arguments beyond register slots and 64-bit arguments/returns |
| [Graphics](../../src/tools/compiler_test/graphics.c) | 76,800 pixels | Clipped rectangles/text, color regions, canonical little-endian framebuffer CRC, sums/counts and corner pixels |

Each run generates one fresh 16-byte nonce; its first word supplies the same
runtime input seed to both badges. The source-suite build ID is identical
across compiler paths, while UF2 SHA-256, compiler commands, LLVM version,
owned-tool hashes and selected C06 passes distinguish their provenance.
Results are seven 64-byte records: summary plus six modules. Each record
has a magic/version/length, source-build ID, ordered module ID, nonce,
case count/six values and CRC. Missing/partial/stale/wrong-build records,
device errors, wrong case counts and mismatches fail the run, not just the
module printout. JSON retains both values and exact differing lane indexes.

With `--timing`, version 3 expands to 28 records: the original seven,
two clock-register pages, one empty-read sample page, six workload sample pages,
and six pairs of kernel result/sample pages. Versions 1/2 retain their historical
sixteen-record layout.
The clock pages hold the initial snapshot, checked against a final snapshot
before publishing the report.
The source-suite ID incorporates timing mode, and the independent decoder
checks all pages/CRCs, ordering, timing parameters and bounds. The final
`results.json` uses schema 3, separate `modules`/`kernels` arrays and both complete rounds; ordinary mode
retains schema 1 and the seven-record shape. A failed device summary is
decoded before incomplete later pages so its failure detail is not hidden.

USB command `0x61` submits the checked 64-byte request; `0x62` reads a
64-byte page selected by `wValue` 0-6 (0-27 in current timing mode). These commands
exist only in the
`LIVE_USB_COMPILER` build. Work starts after the request's status handshake;
new requests, reset or configuration changes cancel the prior generation.
Reports become readable only after the entire suite and display transfer
finish, and remain immutable for that request. USB is serviced during
framebuffer checks, display initialization and bounded pixel chunks.
Enumeration/EP0 polling is never throttled. The collector checks both live
identities, tests corrupt-request/unavailable-page rejection and submits
the same request to both before waiting for results.

An initial retained-SRAM implementation failed on both badges:
BOOTSEL clears SRAM, as already recorded in [usb-report.md](../../docs/usb-report.md).
The final tool reads results **before** reboot and does not rely on RAM
retention, bypass clearing or write persistent test logs to flash.
Successful or failed collection requests BOOTSEL where possible; the runner
checks recovery for both even on collection/comparison failure. The target
has an eight-second progress watchdog and a fixed 30M inherited-tick
application lifetime; the collector has a 60-second process deadline and
bounded USB/observation calls. A pre-main fault may still require physical
recovery; absence of a fresh report is always a failure.

Independent sanitizer fixtures cover zero/extreme seeds, guard/known values,
records, deferred submission, malformed requests/pages and reset/generation
cancellation. Node tests include an independent bitwise CRC oracle, stale/
corrupt/incomplete records, lane mismatches and a mocked complete runner
checking flash/start ordering, failure propagation and both recoveries:

```sh
make -j1 HOST_CC=/usr/bin/cc test-compiler-test
make -j1 BUILD_DIR=badge-compiler/build/timing-host-tests \
  COMPILER_TEST_TIMING=1 HOST_CC=/usr/bin/cc test-compiler-test
```

Timing fixtures also cover exact counter wrap between reads, service calls
outside measured intervals, deterministic graphics without USB callbacks,
incorrect results, under-resolved/overlong samples, clock differences,
PLL lock-loss flags, overhead bounds, role swapping, nonce freshness,
sample statistics and rejection of performance results for mismatching code.

On 2026-10-07 three live-USB hardware comparisons passed all six modules,
with fresh seeds and automatic recovery:
[first](../../build/compiler-compare-runs/run.HnGYI2/results.json),
[repeat](../../build/compiler-compare-runs/run.9RJH8J/results.json),
[final provenance-enabled run](../../build/compiler-compare-runs/run.vNQyc7/results.json).
Final source build ID: `f1eaabef`. LLVM/owned flash payloads were
8,788/331,800 bytes, respectively; this is not an optimization benchmark.
Final ELF checks confirmed vectors, resolved symbols, RAM/stack separation
and no linked radio code. Every UF2 block/payload/padding is checked by the
runner before upload. Existing live/UDP tests, collectors and the complete
36-module/11-image owned corpus also passed.

**Limits:** this is a bounded differential regression suite, not proof of
all C semantics. Some lanes are aggregates/CRC fingerprints and can collide.
Framebuffer agreement does not certify physical display signals or pixels.
No floating point, atomics, variadic calls or comprehensive alias/UB testing
is claimed. Initial experiments with variable 64-bit shifts failed the
current owned backend, and LLVM emitted unavailable freestanding runtime
helpers for variable 64-bit shifts/division. Those operations are explicitly
outside this first suite; they were not silently counted as passing.
The fixed larger stack is not a measured high-water certification.

To add a module, add a pure deterministic source routine and declaration in
[suite.h](../../src/tools/compiler_test/suite.h), register its dispatch/name in
[suite.c](../../src/tools/compiler_test/suite.c), update the Makefile module
source list and the count/expected case count in
[compiler-test-protocol.mjs](../tools/compiler-test-protocol.mjs), and add host
positive/boundary tests. Increase the C module count as needed; keep the
record layout compatible or version it explicitly. Capture canonical bytes,
not struct padding or timing/peripheral-dependent values. Rerun both the
host gates and the physical comparison. This is an additional optional
hardware gate before/after C06 changes, not completion of M02.

## First replacement: ELF-to-binary extraction

[elf2bin.c](../src/elf2bin.c) is a small host-side C replacement for the one
`llvm-objcopy` operation used by our freestanding build:

```sh
make -C badge-compiler
badge-compiler/build/elf2bin -O binary \
  build/universe26/firmware/screen_test.elf \
  badge-compiler/build/screen_test.bin
```

The command-line shape is compatible with `llvm-objcopy -O binary`. To use it
in the existing production Makefile without changing that Makefile's default:

```sh
make -C badge-compiler
make -B RP2350_OBJCOPY="$PWD/badge-compiler/build/elf2bin" \
  build/universe26/uf2/screen_test.uf2
```

The tool runs on the development host and uses its C library/POSIX file APIs;
it does not add libc or dependencies to badge firmware.

Supported input is a little-endian ELF32 Arm executable with ordinary program
and section tables and file-backed allocated `SHT_PROGBITS` sections mapped
unambiguously to `PT_LOAD` segments. Section load addresses are derived from
segment physical addresses, not just section virtual addresses. Thus `.data`
initializers belong in flash even though the variables run in RAM.

Only allocated file-backed section contents are copied. Segment padding,
symbol tables, debug information, `.bss`, `.noinit` and the NOLOAD USB mailbox
are not copied. Output spans the first through last copied flash byte,
zero-filling gaps like LLVM's default binary output. The image must start at
`0x10000000` and fit the board's 16 MiB flash window; the existing linker
scripts retain their stricter build limits. ELF input is capped at 64 MiB.
Vendor firmware payloads are copied unchanged as section data.

This is intentionally not general objcopy: ELF64, other machines/endianness,
relocatable objects, dynamic loading, extended header numbering, sectionless
ELFs, allocated TLS/compressed sections, other allocated section types and
other command-line operations are rejected. It does not validate C semantics,
instruction safety or Picobin correctness; those belong to other stages.

Malformed input fails explicitly before writing output. Successful extraction
writes a temporary file beside the destination and renames it into place;
write/rename failures preserve existing output. Input/output aliases and
existing nonregular outputs, including symlinks/devices, are rejected.
This is atomic replacement, not a guarantee of durability across power loss.

Tests:

```sh
# Independent fixtures, malformed inputs and CLI checks, under ASan/UBSan:
make -C badge-compiler test
# Compare the synthetic RAM-initializer/gap fixture with LLVM too:
make -C badge-compiler test-oracle
# Full application corpus and all LLVM re-entry routes:
bash badge-compiler/tools/oracle.sh
```

The focused tests cover RAM VMA versus flash LMA, zero-filled gaps, ignored
segment padding, section order, NOBITS/metadata exclusion, the exact 16 MiB
output boundary, overlapping/ambiguous/out-of-flash mappings, truncated tables,
unsupported formats, 512 deterministic byte mutations, oversized input,
output preservation and file alias rejection.

The oracle builds this tool and uses it for Makefile ELF-to-binary extraction
and hence the final UF2 images. LLVM remains the reference for extraction and
for vendor object-section inspection (`--dump-section`, not implemented here).
Each baseline and each re-entry executable is extracted by both tools and
must compare byte-for-byte. `objcopy-comparisons.tsv` records those results.
Unlike code generation, extraction has no optimization freedom: the same
supported ELF must yield the same flash bytes.

## Second replacement: C preprocessing

[badge_cpp.c](../src/badge_cpp.c) is a standalone, host-side C preprocessor. It does
not call Clang, import Clang's macro dump or fall back to another preprocessor.
The current target environment is an explicit small set of Cortex-M33
freestanding C11 predefined macros, including the integer type macros needed
by our project-owned `stdint.h`. It is not Clang's complete predefined-macro
environment and does not pretend to be Clang.

```sh
make -C badge-compiler
badge-compiler/build/badge-cpp \
  -Isrc/shared -Isrc/platform/badger \
  src/tools/screen_test.c -o badge-compiler/build/screen_test.i

# Consume the output without invoking another C preprocessor:
clang --target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
  -std=c11 -Oz -ffreestanding -fno-builtin \
  -x cpp-output -c badge-compiler/build/screen_test.i \
  -o badge-compiler/build/screen_test.o
```

That last command is a minimal usage example, not a replacement for the full
production flags. The oracle retains the exact flags from the root Makefile.
The root Makefile's default C compiler is unchanged.

Implemented:

- ASCII identifiers, preprocessing numbers, string/character literals and
  maximal-munch punctuation; comment removal, CRLF normalization and
  backslash-newline splicing.
- Object-like and fixed-arity function-like macros, empty arguments,
  argument prescan, rescanning across replacement boundaries, and per-token
  hide sets for recursive expansion suppression.
- Quoted/angle includes and macro-expanded include names, local-first quoted
  lookup, ordered `-I` search, include guards and `#pragma once`.
- `#define`, `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`,
  `#include`, `#error`, empty directives, and `#pragma once`.
- Integer conditional expressions with 64-bit signed/unsigned preprocessing
  arithmetic, operator precedence, `defined`, short-circuit logic, and `?:`.
  Evaluated division by zero, invalid shifts and signed overflow are errors;
  inactive branches do not perform arithmetic. Unknown identifiers become
  zero, as required in `#if`.
- Source-file/physical-line tracking, generated line markers, `__FILE__` and
  `__LINE__`, plus joined/separate `-I`, `-D`, `-U` and `-o` options.
- `--tokens` for token-stream comparisons that ignore formatting, comments
  and line-marker metadata.

This is intentionally a **tested corpus subset**, not a claim of all C11
preprocessing. Stringification (`#`), token pasting (`##`), variadic macros,
trigraphs, digraphs, universal-character/extended identifiers, source `#line`,
other pragmas, character constants in `#if`, `_Pragma`, `__COUNTER__`,
`__DATE__` and `__TIME__` are not implemented. Unsupported directives/macros
fail explicitly in active processing; unsupported lexical forms fail before
branch selection. There is no implicit system include path,
`include_next`, dependency-file generation or macro-dump mode. Add features
and their tests when the input contract actually needs them.

Limits are explicit: 512 distinct macro names, 32 macro parameters,
64 include/conditional/argument-expansion nesting levels, 64 include paths,
512 distinct input files, 250,000 tokens per vector, one million macro
expansions, 8 MiB per source file, 16 MiB per text buffer and 128 MiB of
tracked allocation. These are initial implementation bounds, not hardware
limits or an optimization target.

Diagnostics include file and line. Output is buffered until preprocessing
succeeds, then uses the same atomic file writer as `elf2bin`. Failed processing
does not overwrite an existing output, and outputs cannot alias any source
or included header. The tool has no USB or radio access.

The oracle now compares our and Clang's normalized tokens for **all 36 C
translation units**, compiles `ours.i` with `-x cpp-output`, and relinks all
11 applications through the `ours-cpp` route. The two `.S` sources still use
Clang preprocessing: our C output formatting is not an assembly interface.
The vendor blob remains unchanged opaque data.

Per-object artifacts include `ours.i`, `oracle.tokens`, `ours.tokens` and
`ours-cpp.o`. Per-image `ours-cpp.uf2` combines **our C preprocessing and
our ELF extraction**, with Clang code generation/assembly, LLD and the
existing UF2 encoder:

```text
our badge-cpp -> Clang (-x cpp-output) -> LLD -> our elf2bin -> our bin2uf2
```

`make -C badge-compiler test` runs sanitizer-backed positive, negative,
mutation and file-preservation tests for all five owned tools.
`make -C badge-compiler test-oracle` additionally checks a preprocessor-only
fixture against Clang, including nested/recursive macros, aliases spanning
replacement boundaries, and rescanning examples. The complete application
oracle adds actual target compilation and linking, rather than trusting our
token normalizer alone. Byte-identical firmware is the measured result,
not a universal requirement for a future different code generator.

## Third replacement: C03 syntax parsing

[badge-parse](../src/badge_parse.c) reads preprocessed C directly; it neither calls
Clang nor consumes Clang AST JSON. It reuses [c_lexer.h](../src/c_lexer.h), extracted
from our preprocessor without changing its tokenization contract. This wires
an owned C02 subset into C03: keyword recognition, numeric-token spelling
validation and preprocessing line-marker interpretation.

```sh
make -C badge-compiler all
badge-compiler/build/badge-parse \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o.oracle/ours.i \
  -o badge-compiler/build/screen-test.ast.json
```

The parser supports the corpus's declarations, nested pointer/array/function
declarators, abstract declarators, prototypes and function definitions; structs,
unions, enums and bit-fields; casts, `sizeof`, `_Alignof`, `_Generic`, unary,
binary, assignment and conditional expressions; calls/subscripts/member access;
brace/designated initializers and compound literals; blocks, all C control-flow
statements and labels; `_Atomic`, `_Alignas` and `_Static_assert`; GNU
`__attribute__`, assembly labels and basic/extended `__asm__`, including operand
groups and `asm goto`.

Scoped typedef/ordinary-name classification disambiguates declarations, casts
and `sizeof`. Parameters, blocks, `for` declarations and enum constants are
tracked; record member names do not hide ordinary typedefs. Prototype names
do not leak, and definition parameters are visible in the function body.
This is **syntactic name classification, not C04 semantic analysis**: the
parser does not resolve expression identifiers, validate types/redeclarations,
evaluate constants/static assertions, lay out records, check lvalues/control-flow
constraints, interpret attributes/assembly constraints or generate IR.
Literal spellings are retained for subsequent decoding and typing.

### AST interchange contract

The UTF-8 JSON envelope is
`{"schema":"badge-c-ast-v1","locations":true,"ast":...}`.
Every node has:

- `kind`: grammatical category; `text` when a name, operator, modifier or
  literal spelling belongs to the node.
- `location`: original `file` and 1-based `line` from preprocessing markers
  (line zero is retained for compiler pseudo-files when supplied).
- `tokens`: `[begin,end]`, a zero-based, end-exclusive range in the normalized
  C-token stream, excluding newlines/markers. Empty syntactic components can
  have zero-length ranges.
- `children`: ordered syntax nodes; no inferred types or implicit conversions.

The root is `translation_unit`. Declarations contain `specifiers` followed by
`init_declarator` children; a `function_definition` additionally ends with its
body `block`. Declarator operator nodes put the underlying declarator first,
then qualifiers/dimensions/parameters. Parenthesized declarators are retained:
`int *a[3]` is `pointer_declarator(array_declarator(name))`, whereas
`int (*b)[3]` is `array_declarator(parenthesized_declarator(pointer_declarator(name)))`.
These are **syntax constructors**, not already-resolved pointer/array types.
Record/enum tags have explicit `record_definition`/`enum_definition` children
when defined. Binary/assignment operands are left then right; conditionals
are condition, true expression, false expression. A `for` has initializer,
condition, increment and body children, with `empty` placeholders as needed.
String literals retain individually quoted `string_segment` children.

`--no-locations` sets the envelope's `locations` to false and omits node
locations for deterministic comparisons; token ranges and syntax remain.
It does not remove any semantic information from this untyped tree.
This versioned schema is ours, not Clang's typed AST format.

### Input limits, diagnostics and validation

Inputs must be regular preprocessed files, with ASCII identifiers and UTF-8
source/filenames. GNU `# number "file" flags` markers (flags 1-4) and
`#line number ["file"]` are understood; omitted filenames retain the current
file. Marker filenames support escaped quotes/backslashes and non-NUL octal
bytes forming valid UTF-8. Unprocessed directives, K&R/implicit-int forms,
GNU statement expressions/nested functions/`typeof`, digraphs, trigraphs and
extended identifiers are outside the contract and diagnosed rather than
passed to another frontend.

Bounds are 16 MiB input, 250,000 lexed tokens (including marker/newline tokens),
256 recursive parsing/binding-import steps and AST levels, 1,000,000 AST nodes,
128 MiB tracked allocation and 64 MiB JSON output; overflowing source-line
numbers are rejected. Output is buffered and atomically replaced only after
success; input aliases and nonregular outputs are rejected. Parser diagnostics
use mapped source locations; failures during initial lexing/marker decoding
refer to the input `.i` file's physical lines.

The shared C token limit was raised from 100,000 to 250,000 for larger
translation units, including SDK-header probes. The AST-node ceiling follows
the token limit; memory, input, nesting and macro-expansion budgets are
unchanged. This is a host-side resource allowance, not Pico SDK compatibility
or a change to badge memory requirements. Preprocessor, token-normalization
and parser tests accept exactly 250,000 tokens and reject 250,001 while
preserving existing output files.

The Pico SDK BLE tilt Pong probe now clears initial lexing of the real
SDK/GCC-preprocessed application. C02/C03/C05 still reject its first retained
`#pragma GCC diagnostic push` with `invalid line marker`; this directive is
outside the preprocessed-input contract. These failing frontend probes used
about 32 MiB peak host RSS, not a full-compilation memory measurement. SDK
driver flags remain unsupported, and no owned Pong UF2 or hardware run is
claimed. The existing default corpus still builds with all 179 baseline
artifact hashes unchanged after the token-limit increase.

Sanitizer-backed tests cover valid/invalid/explicitly unsupported inputs,
operator precedence/associativity, declarator binding, typedef shadowing,
locations, UTF-8/JSON spelling, mutations, nesting and file preservation.
The current run passed 49 positive, 52 negative, 4 intentionally unsupported
fixtures and 114 mutations under ASan/UBSan.
`make test` also checks fixtures with `LLVM_CLANG` (default `clang`); grammar
fixtures deliberately suppress shift-parenthesis, unused-value and dangling-else
warnings, not syntax errors. Unsupported fixtures separately verify that
Clang accepts constructs our contract intentionally rejects.

The full oracle parses both preprocessors' outputs for **36 configured C
translation units** and checks **36 identical structural AST pairs**.
All 144 full/structural JSON documents were independently decoded and checked
for schema, location fields and nested token-range validity. Existing firmware
the original eight re-entry routes remain 88/88 identical complete images;
all 187 ELF extractions match LLVM. C10/C05/C07/C06 comparisons are described separately below,
including the C09 route below.
Each object gains `ours.ast.json`, `preprocessed.ast.json` and their
`*.structure.ast.json` variants; `parser-comparisons.tsv` records success.
These syntax-only captures remain an inspection/interchange boundary.
The C04 checker and C05 emitter below share the same in-memory parser tree;
the `owned-ir` route now compiles firmware through our frontend.

## Fourth replacement: C04 semantic analysis

[badge-sema](../src/badge_sema.c) reads preprocessed C, uses our parser through
[c_frontend.h](../src/c_frontend.h), and annotates the resulting in-memory syntax
tree. It does not invoke Clang, import Clang's AST or fall back to LLVM.
The CLI reads `.i`, not serialized C03 JSON:

```sh
make -C badge-compiler all
badge-compiler/build/badge-sema \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o.oracle/ours.i \
  -o badge-compiler/build/screen-test.typed.ast.json
```

### Target semantics and supported subset

The type system models little-endian Cortex-M33 Arm EABI, AAPCS soft float:
pointers, `int` and `long` are 4 bytes; `short` is 2; `long long` is 8;
`float` is 4 and `double`/`long double` are 8. Plain `char` is unsigned.
Scalar natural alignment equals size, up to 8 bytes. Enums use unsigned
`int` when all enumerators are nonnegative, otherwise signed `int`;
enumerator constants themselves have type `int`.

The checker implements:

- Separate ordinary/tag namespaces, scoped declarations, typedefs,
  prototype/parameter scopes, linkage and compatible redeclarations.
- Qualified scalar, pointer, array, function, record and enum types;
  parameter adjustment, integer promotions and usual arithmetic conversions.
- Natural struct/union layout, padding, member offsets, flexible array tails,
  inferred array bounds and explicit `_Alignas`.
- Identifier resolution, modifiable lvalues, pointer compatibility/arithmetic,
  calls/arguments, casts, member access, `sizeof`, `_Alignof` and `_Generic`.
- Target-width integer/character/string decoding and integer constant
  evaluation, including signed conversions, unsigned wrap and short-circuit
  expressions. Invalid constant arithmetic is diagnosed where a constant
  value is required; this is not a general undefined-behavior detector.
- Scalar/aggregate/designated initialization, compound literals, static
  address constants, enum values and `_Static_assert`.
- Return/condition checks, loop/switch constraints, case-value uniqueness
  and label/goto resolution.
- `section`, `used` and `noreturn` attributes; assembly operand types/lvalues
  and `asm goto` labels. Instruction/register constraints remain a backend
  responsibility.

Value-conversion annotations preserve the source lvalue's qualified type,
including discarded volatile reads. A constant lvalue address is distinguished
from a constant pointer value: an MMIO address is not the value loaded from
that address.

The explicit exclusions are floating literals/constant folding, bit-fields,
atomics, TLS, complex/imaginary types, VLAs, wide literals and
multibyte/multicharacter character constants, anonymous record members,
empty records, general initializer
brace elision, inline linkage semantics, non-prototype function definitions,
declaration assembly labels, GNU void/function-pointer arithmetic and
unrecognized semantic attributes such as `packed`/`aligned`. Floating types
and variable arithmetic are supported without floating constant evaluation.
Unsupported constructs fail explicitly; syntactic C03 support does not imply
C04 support. Warning parity, uninitialized-value analysis and proof of all
return paths are not part of this checker.

### Typed-AST contract

The UTF-8 `badge-c-typed-ast-v1` envelope contains `target`
(`thumbv8m.main-unknown-none-eabi`), `abi` (`aapcs-soft-float`), `byte_order`
(`little`), `locations`, `types`, `symbols` and the original `ast`.

- The type table has numeric IDs, kind, size/alignment, qualifier bits
  (`const=1`, `volatile=2`, `restrict=4`), completeness and unsignedness.
  Derived types reference their base; arrays include lengths; functions
  include prototype/variadic flags and adjusted parameter types; records
  include named fields with type IDs and byte offsets. Function types have
  no object size/alignment (both zero); incomplete types do not have an
  established object layout.
- Symbols have IDs, names, types, kinds, linkage (`none=0`, `internal=1`,
  `external=2`), alignment, storage/definition flags and optional attribute
  metadata. String bytes are encoded as `bytes_hex`; section strings retain
  their source spelling as `section_spelling`.
- Syntax nodes retain the C03 location/token/children contract and gain
  optional type, conversion, operation-type and symbol references,
  lvalue/rvalue/function categories, integer-constant/ICE information,
  address information and member/initializer byte offsets.
- Integer constants and address bit patterns are decimal **strings**, not
  JSON numbers, to preserve all 64 bits. Address addends are signed decimal
  strings. Address metadata can describe a constant **lvalue location**,
  not just a constant loaded pointer value.

`--no-locations` omits source locations for deterministic comparison.
Shared frontend resource limits and atomic output/file-alias protections
remain in force; semantic errors preserve existing output.

### Validation and remaining boundary

The sanitizer-backed [semantic tests](../tests/badge_sema_test.c) passed **54 positive,
84 negative, 11 intentionally unsupported and 25 mutation cases**, with
Clang acceptance/rejection differentials. All **17 LLVM target-layout/constant
probes** in [sema_oracle_fixture.c](../tests/fixtures/sema_oracle_fixture.c) match, covering
scalar widths/alignments, struct/union layout, offsets, enum/char signedness,
integer ranks, sign extension, wrap, shifts and short-circuit evaluation.

The full oracle checks **72 preprocessed inputs** and **36 matching typed-AST
pairs**. All **144 typed JSON documents** were independently decoded and
validated for references, ABI sizes/layouts, constants, locations and nested
token spans. Each C object gains `ours.typed.ast.json`,
`preprocessed.typed.ast.json`, their `*.typed.structure.ast.json` variants
and Clang's `record-layouts.txt`; `sema-comparisons.tsv` records results.

Existing checks still pass: **88/88 original-route firmware comparisons** and **187/187
extractions**. These establish unchanged downstream plumbing, not execution
of our typed AST. C05 below additionally lowers this tree into verified LLVM
IR and connects it to firmware generation through our semantic frontend.

## Fifth replacement: C09 ELF object writing

[badge-object](../src/badge_object.c) is a native host-side writer for little-endian
ELF32 Arm EABI5 relocatable objects. It accepts already-encoded section bytes,
symbols and relocation descriptors through a [C API](../src/badge_object.h) or a
versioned text manifest. It does not invoke LLVM, assemble instructions,
choose registers, apply relocations or link an executable.

```sh
make -C badge-compiler all
# Standalone object construction:
badge-compiler/build/badge-object input.bobj -o output.o

# Oracle adapters: decode an existing supported object into the same model:
badge-compiler/build/badge-object --emit-manifest \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o \
  -o badge-compiler/build/screen-test.bobj
badge-compiler/build/badge-object badge-compiler/build/screen-test.bobj \
  -o badge-compiler/build/screen-test-owned.o
# Direct decode + rebuild, without the text intermediary:
badge-compiler/build/badge-object --from-elf \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o \
  -o badge-compiler/build/screen-test-direct.o
```

The ELF adapter is an oracle/re-entry convenience, not the required input
format for future C08/C07. A fresh object can be built without any reference
ELF; the unit tests do this with handwritten Thumb bytes and fixups.

### C API and manifest contract

`badge_object_write(const bo_object_t *, unsigned char **, size_t *)`
serializes caller-owned input arrays and returns a `malloc`-owned buffer.
Failure emits a diagnostic and returns NULL/zero outputs. Compile
[badge_object.c](../src/badge_object.c) with `-DBADGE_OBJECT_LIBRARY` to omit its CLI
and adapters when linking it into another host tool. No target libc is added.

The input consists of sections, symbols and relocations. Section/symbol
references are **one-based input indices**, not final ELF indices. Zero
denotes an undefined symbol section or absent section link. A symbol section
can also be `BO_ABS=65521` or `BO_COMMON=65522`. An address-significance
section uses `BO_LINK_SYMTAB=4294967295`.

The writer generates the ELF header, null section/symbol, section-name and
symbol-name tables, symbol table and per-target REL tables. It groups local
symbols before global/weak symbols, sets `sh_info`/`sh_link`, and remaps both
relocation and address-significance references to the final symbol indices.
It preserves input section order and relocation order within each section.
Given the same model, output is deterministic; reference ELF byte identity is
not required because table layout and string pooling can differ.

The text contract is `badge-object-v1`, followed by these line records:

```text
section ID NAME TYPE FLAGS ALIGNMENT LINK ENTRY_SIZE SIZE HEX_BYTES
symbol  ID NAME BINDING TYPE VISIBILITY SECTION VALUE SIZE
reloc   SECTION OFFSET TYPE SYMBOL
```

IDs must be sequential from one within sections/symbols. References may point
forward; validation occurs after parsing. Numbers are unsigned decimal or
`0x` hexadecimal. Names can be bare tokens or quoted strings with
`\"`, `\\`, `\n`, `\r`, `\t` escapes. Empty names use `""`. Blank lines and
`#` comments at field boundaries are allowed. Byte payloads are contiguous
hexadecimal pairs; `-` represents empty data or NOBITS storage.

For example, a complete object defining a two-byte Thumb function:

```text
badge-object-v1
section 1 ".text.answer" 1 6 2 0 0 2 7047
symbol 1 "answer" 1 2 0 1 1 2
```

`7047` encodes `bx lr`; the function's value is **1**, not 0, because the
Thumb-state bit belongs in its ELF symbol value. Sizes/ranges use the
underlying byte offset with that bit removed.

### Supported target subset

- `SHT_PROGBITS`, `SHT_NOBITS`, Arm exception-index/attribute sections and
  LLVM address-significance sections.
- Allocated/writable/executable sections, merge/string metadata, link-order
  relationships and excluded metadata.
- Local/global/weak symbols; NOTYPE, OBJECT, FUNC, SECTION and FILE types;
  standard visibility, undefined/absolute/common symbols and Thumb functions.
- ELF `SHT_REL`, with implicit addends already present in payload bytes:
  `R_ARM_ABS32` (2), `R_ARM_REL32` (3), `R_ARM_THM_CALL` (10),
  `R_ARM_THM_JUMP24` (30), `R_ARM_PREL31` (42), and D06's
  `R_ARM_THM_MOVW_ABS_NC` (47) / `R_ARM_THM_MOVT_ABS` (48).
  The latter occupy four-byte Thumb instructions at halfword-aligned sites
  in executable sections.
- Natural object alignment, zero-filled file padding, and NOBITS sizes
  without allocating/writing their virtual contents.

Arm attributes, machine instructions and unwind records are caller-supplied
payloads. C09 preserves their bytes and validates container metadata; it does
not infer CPU features, generate attribute tags, validate instructions or
interpret unwind programs. These responsibilities remain with the assembler/
backend. LLVM address-significance ULEB128 data is interpreted because its
symbol indices must be remapped.

The header declares `EM_ARM`, `ET_REL` and EABI5 (`e_flags=0x05000000`), like
the corpus objects. This is not support for ELF64, big endian, executables,
dynamic objects, RELA, TLS, COMDAT/groups, debug-specific section types,
extended numbering or every Arm relocation. Unknown section types/flags,
symbol kinds and relocations fail explicitly; the adapter does not silently
discard unsupported sections.

Bounds are 4,096 model sections, 65,536 symbols, 262,144 relocations,
4,096 bytes per input name, power-of-two alignments up to 65,536, and 64 MiB
input/output files. NOBITS sizes can span the uint32 range without growing
the file. References, symbol extents, relocation places/alignment, link-order
metadata, merge entry sizes and ULEB128 references are checked.
CLI output uses the existing atomic writer; malformed input, file aliases,
nonregular destinations and write failures cannot report success or replace
an existing output with a partial result.

### Validation and integration

[badge_object_test.c](../tests/badge_object_test.c) passes under ASan/UBSan:
**48 rejected API cases, 18 rejected manifests and 98 manifest mutations**,
plus every truncated prefix and deterministic byte mutations of the ELF
fixture. Positive assertions cover local ordering, visibility, Thumb marking,
relocation remapping, section links, widening ULEB128 references, large NOBITS,
canonical rebuilds and file preservation.

[object_oracle.sh](../tests/object_oracle.sh) assembles an independent
[LLVM fixture](../tests/fixtures/object_oracle_fixture.S), builds its objects through both CLI
routes, checks deterministic reconstruction, and compares linked bytes after
LLD resolves calls, branches, data references, weak/common symbols and unwind
references. LLD also accepts the handwritten C API fixture.

The application oracle writes `object.bobj` and a fresh `object.o` for
**38 translation units: 36 C and two assembly**. LLVM independently decoded
matching payloads, section metadata/links, symbols, relocation targets and
address-significance references for all 38 objects and the assembly fixture.
The new `object` route relinks all **11 firmware images**, with identical
flash bytes and unchanged vendor assets. It produces `object.uf2`; the
screen-test result matches the image flashed and observed working.
No C09 image was flashed automatically.

This replay route tests C09 separately from assembly. LLVM generates its
initial reference objects; the C08 route below instead supplies fresh encoded
sections/fixups directly to the same writer. Root production defaults are
unchanged.

## Sixth replacement: C08 Thumb assembly and instruction encoding

[badge_asm.c](../src/badge_asm.c) reads saved Cortex-M33 assembly and uses the private
[Thumb encoder](../src/badge_thumb.h) to generate bytes and fixups. It calls C09's
C API directly: it does not invoke LLVM, import an existing object, or fall
back to another assembler.

```sh
make -C badge-compiler
badge-compiler/build/badge-asm \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o.oracle/native.s \
  -o badge-compiler/build/screen-ours.o
```

### Supported assembly contract

- Cortex-M33, little-endian Thumb and soft-float EABI, with the integer/DSP
  instruction forms needed by the complete corpus: arithmetic, shifts,
  modified immediates, MOVW/MOVT, loads/stores, register lists, multiply/divide,
  bit-fields, extensions, table branches, hints and SY barriers.
- Local conditional/unconditional branches, calls, CBZ/CBNZ, literal loads
  ADR address generation and IT blocks. Layout relaxation selects narrow/wide encodings and checks
  ranges/alignment; explicit `.n`/`.w` forms are checked. Explicit flag-setting
  instructions in IT use encodings that actually set flags.
- Named labels, affine symbol expressions, same-section differences,
  `.equ`/`.set` and `name = expression` without reassignment, common/weak/global/hidden symbols,
  function types/sizes and Thumb marking. Layout-affecting constants must be
  available when used; this is not general forward-expression assembly.
- Sections, alignment, fixed-width data, strings, zero/space and `.incbin`
  with optional offset/length. NOBITS accepts only zero initialization.
- CPU/FPU declarations, target ABI attributes, file/ident/addrsig metadata,
  executable-section mapping symbols and `.cantunwind` exception-index
  records. Floating-point instructions and general unwind programs are not
  implemented.
- The original five C09 relocations: ABS32, REL32, THM_CALL, THM_JUMP24
  and PREL31, plus D06's THM_MOVW_ABS_NC and THM_MOVT_ABS through
  `#:lower16:` / `#:upper16:` modifiers.
  Local fixups are resolved; external calls/branches and data references
  retain the required implicit addends and symbolic relocations.

`.if`/`.error`/`.endif` support **assertion-only** regions, as used by Picobin;
instruction/data emission or `.else` inside a conditional is rejected rather
than silently skipped. Numeric local labels, macros/repetition, general
conditional assembly, debug directives, ARM mode, other targets, arbitrary
ISA forms, additional relocations and hexadecimal string escapes are outside
the supported contract and fail explicitly.

Input is capped at 16 MiB and 500,000 parsed records; tracked allocations at
192 MiB; individual sections and included files at 64 MiB. Expressions and
alias chains have a nesting limit of 64; relaxation is bounded to 128 passes.
Widths widen monotonically; directive/section spans also participate in
convergence. Final encoding must remain stable and explicit `.n` requests
must fit two bytes. Symbolic MOVW/MOVT REL addends are signed 16-bit;
absolute modifier expressions may be signed/unsigned 32-bit.
C09's section/symbol/relocation limits also apply. Errors include source line
locations. Output is written atomically only after successful serialization;
source/incbin aliases and nonregular destinations are rejected.

### Validation and integration

[badge_asm_test.c](../tests/badge_asm_test.c) passes under ASan/UBSan with **30 positive,
62 negative, 12 unsupported and 24 mutation cases**, plus exact forward/
backward branch thresholds, explicit IT flags, location-dependent aliases,
incbin ranges and output/file-safety assertions.
[asm_oracle.sh](../tests/asm_oracle.sh) compares the
[instruction fixture](../tests/fixtures/asm_oracle_fixture.S) and C09 fixture against LLVM
after LLD linking, without section garbage collection.

The full oracle assembles **all 38 translation units** from their saved
assembly. Independent LLVM decoding also confirmed every allocated section's
bytes, canonical relocation targets/addends and public symbols, including
code discarded by firmware linking. The `asm` route relinks all **11 firmware
images** byte-for-byte identically, and checks embedded vendor firmware,
NVRAM and CLM bytes directly. It produces `asm.uf2`; the screen-test UF2
matches the baseline previously flashed and observed working. No C08 image
was flashed.

C08/C09 now operate without LLVM's assembler/object writer on this route.
LLVM still generates assembly from C, preprocesses handwritten `.S`, and
links with LLD on the `asm` route. C10 is implemented by the separate route
below; C06/C07 are now implemented below. Root build defaults remain unchanged.

## Seventh replacement: C10 linking

[badge_link.c](../src/badge_link.c) is a native, standalone RP2350 linker. It
reuses C09's public `badge_object_read` / `badge_object_free` API for scoped
ELF decoding rather than adding a second object reader. It resolves and
relocates objects itself, writes an ELF32 Arm EABI5 executable and optional
map, and does not invoke LLD or another linker.

```sh
make -C badge-compiler
badge-compiler/build/badge-link --help
# From the root after an oracle capture:
badge-compiler/build/badge-link \
  -T src/arch/rp2350/memmap_flash.ld \
  -Map badge-compiler/build/link-screen.map \
  -o badge-compiler/build/link-screen.elf \
  badge-compiler/build/oracle/universe26/firmware/start.o.oracle/asm.o \
  badge-compiler/build/oracle/universe26/firmware/picobin.o.oracle/asm.o \
  badge-compiler/build/oracle/universe26/firmware/display.o.oracle/asm.o \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o.oracle/asm.o
```

### Linker/script contract

- The existing [flash](../../src/arch/rp2350/memmap_flash.ld) and
  [report/mailbox](../../src/arch/rp2350/memmap_report.ld) scripts remain the
  source of truth. The linker parses their syntax and evaluates their
  expressions; it does not identify filenames or hard-code their layouts.
- Supported script syntax: `MEMORY`, `ENTRY`, `SECTIONS`, output sections,
  `NOLOAD`, `> REGION`, `AT > REGION`, one-byte fills, input-section selectors
  with exact names/trailing `*`, `KEEP`, `COMMON`, `/DISCARD/`, assignments,
  location-counter advance/alignment, `PROVIDE` and `ASSERT`.
  Expressions support uint32 literals/`k` suffixes, names, `.`, parentheses,
  addition/subtraction/comparisons and `ALIGN`, `ORIGIN`, `LENGTH`, `LOADADDR`.
  Assignments are evaluated in script order; unsupported forward value
  dependencies fail rather than defaulting to zero.
- Local/global/weak/absolute/common symbol resolution; duplicate strong
  definitions and object/script collisions fail. Strong definitions override
  weak/common ones; common alignment/size coalesces. Hidden definitions are
  localized in executable symbol tables. Referenced undefined weak symbols
  use Arm's absolute/PC-relative behavior, including nonzero addends.
- Section GC defaults on, with `--no-gc-sections` available. Entry and `KEEP`
  sections are roots; relocation references define reachability. Live
  discarded/orphan sections or unresolved strong references fail.
- Input-order layout respects section alignment, flash code/Picobin order,
  RAM data initialization load addresses, BSS/noinit/common allocation,
  mailbox reservation, stack symbols, region bounds and script assertions.
  Executable gaps use the target trap fill; explicit fills are honored.
- The original five C09 REL relocations are applied with Thumb function marking,
  implicit addends, branch-opcode/alignment/range checks and PREL31 limits.
  Branches target file-backed Thumb code; no ARM interworking or veneers.
  NOBITS/common data require NOLOAD output.
- D06's THM_MOVW_ABS_NC/THM_MOVT_ABS fixups validate opcode, reserved bits
  and destination, decode the same signed 16-bit REL addend from either
  instruction, and insert the low/high half of the complete ELF symbol
  value plus addend. Odd data/function-address offsets are preserved;
  no blanket Thumb-bit stripping is applied.
- ET_EXEC includes load segments, allocated section tables, Thumb entry,
  localized/global symbols and validated soft-float Cortex-M33 ABI metadata.
  Input `.comment`, empty nonexecuting `.note.GNU-stack` and addrsig metadata
  are deliberately discarded; our scripts discard exception tables.
  Compatible input attributes are checked; the first attribute payload is
  retained, not a general attribute-merging implementation.

This is not general GNU ld/LLD: archives/libraries, dynamic linking, TLS,
COMDAT, arbitrary orphan placement, linker relaxation/veneers, general
unwind linking, additional relocations, script includes/macros, sorting,
arbitrary expressions and reassignment are outside the contract.
Unknown options, syntax, section kinds and incompatible ABI inputs diagnose
explicitly.

Mergeable strings/constants are **preserved without pooling or reordering**.
This is an intentional initial quality trade-off, not a failed round trip.
[link_oracle.c](../tools/link_oracle.c) creates reference objects with only optional
merge/string flags cleared, preserving bytes, symbols and fixups; LLD then
independently links the same unoptimized data policy. This helper is used
only by the oracle, never as a linker fallback.

Limits are 128 objects, 16,384 aggregate input sections, 262,144 input symbols,
65,536 global names, 64 output sections, 32 memory regions, 8,192 script
commands, 64 KiB script input, expression depth 64, 192 MiB tracked/model
storage, and 64 MiB executables/maps. C09 file/relocation limits also apply.
Malformed objects, overlapping fixups, backward location moves, bad entry
points and region/range overflow fail before output replacement.
Source/script aliases, ELF/map aliases (including equivalent paths), symlinks
and nonregular output files are rejected. Each output uses atomic replacement;
ELF/map publication is not a two-file transaction or power-loss durability
guarantee.

### Linker validation and downstream ownership

[badge_link_test.c](../tests/badge_link_test.c) passes under ASan/UBSan: GC/KEEP,
weak/strong/common and hidden symbols, all five fixups, exact forward/backward
Thumb range thresholds, RAM LMA/BSS layout, 17 malformed/unsupported scripts,
truncated/mutated ELF input, common coalescing and atomic/alias file safety.
Runtime script tests explicitly exercise alignment, backward moves, undefined
values and failed assertions rather than failing at an earlier boundary.
[link_oracle.sh](../tests/link_oracle.sh) checks five independent C-API/LLVM fixtures
against LLD for image bytes and public symbols, including signed PREL31 and
unresolved weak references with nonzero addends.

The full `linker` route uses our **C08-C12** tools for all **11 applications**.
All 11 flash images and public symbols match the unmerged LLD reference.
Independent LLVM decoding also matches every nonempty allocated section's
bytes, flags, alignment, VMA/LMA and load-segment mapping, plus executable
entry/ABI values. Both flash/report scripts, including mailbox and stack
addresses, are exercised. Vendor payloads remain unchanged.

Compared with the ordinary pooled LLVM baseline, one image is identical and
ten differ. The Wi-Fi image grows from **250,216 to 250,396 bytes**, still within
the script's flash limit. These differences are recorded, not labeled
byte-identical; the independent unmerged reference is the required exact
comparison. `linker.uf2` is produced for every application. No C10 firmware
was flashed or executed on hardware.

C05-C07 are implemented below. The complete documented main C pipeline now
has owned alternatives. Production build defaults remain unchanged.

## Eighth replacement: C05 LLVM IR generation

[badge_ir.c](../src/badge_ir.c) emits textual LLVM IR from the owned typed AST.
It uses the shared [semantic interface](../src/c_semantic.h), parser arena and
frontend file handling; it does not invoke LLVM, import JSON, or replay
Clang's IR. The standalone CLI consumes preprocessed C:

```sh
make -C badge-compiler
badge-compiler/build/badge-ir \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o.oracle/ours.i \
  -o badge-compiler/build/screen-owned.ll
opt -passes=verify -disable-output badge-compiler/build/screen-owned.ll
```

### Lowering and ABI contract

- Fixed little-endian Cortex-M33/Thumb data layout, 32-bit pointers and
  AAPCS soft float, with the C04 target types/conversions.
- Integer, pointer and floating-type arithmetic/conversions, scalar memory
  accesses at their C widths, array decay/indexing and record member offsets.
  C04 still excludes floating literals; backend-generated runtime helpers
  must be supplied by the program or fail explicitly at linking.
- Automatic/static storage, strings, zero initialization, positional/nested
  initializers and field/index designators. Reinitializing an aggregate
  clears its previous bytes/relocations. Pointer constants retain symbolic
  references and byte addends. Const arrays are emitted read-only.
- Record rvalues are snapshots; copies/zeros use constant-size inline LLVM
  memory intrinsics rather than adding libc calls. Volatile scalar accesses
  and aggregate copies/initialization retain volatile semantics.
- Narrow scalar `signext`/`zeroext`; records up to 64 bytes use word carriers
  (`i32`, or `i64` for alignment >=8), larger arguments use aligned `byval`.
  Returns up to four bytes use integer carriers; larger records use `sret`.
  Direct/indirect calls, prototypes, varargs and parameter adjustment share
  the same lowering.
- If/loops, break/continue, switch/fallthrough, labels/goto, short-circuit,
  comma and conditional expressions, compound literals, and the semantic
  checker's selected `_Generic` association, including lvalue selections.
- Section/alignment/used/noreturn annotations; decoded section names and
  `llvm.compiler.used` optimization retention. Operand-free inline/module assembly;
  inline clobbers are limited to `memory` and `cc`.

This is the documented C04 subset, not an unrestricted C compiler.
VLA/bit-field/atomic/complex types, floating literals, unsupported attributes,
incomplete record ABI types, asm operands/register clobbers/asm goto, multiple
union initializer items and designators traversing a nested union member
are explicitly diagnosed. Initialize a nested union as its own braced
subobject instead. Bitcode serialization remains external (`llvm-as`).

The shared frontend bounds apply: 16 MiB input/global-object payload,
128 MiB tracked arena, 64 MiB output and nesting depth 256. The emitter
uses atomic output replacement and preserves existing output on errors;
input/output aliases are rejected. Output is frontend IR with minsize
attributes, not an owned optimization pass.

### C05 validation and firmware integration

[badge_ir_test.c](../tests/badge_ir_test.c) runs **53 LLVM-verified positive,
16 negative and 29 mutation fixtures** under ASan/UBSan, with CLI
preservation/hard-link/symlink alias checks. Fixtures include volatile
increments/records, generic lvalues, aggregate initializer overrides,
section decoding, signed/narrow conversions and the exact 64/65-byte
direct-versus-byval ABI threshold.

[ir_oracle.sh](../tests/ir_oracle.sh) verifies and lowers independent C fixtures with
both frontends, then executes their Arm-layout IR retargeted for the host
at O0/Oz. Bidirectional mixed-frontend calls cover one/two/three-byte record
returns, sret, 8-byte alignment, large byval arguments and narrow/bool values.
Host fixtures exclude pointer-containing records and pointer-width-sensitive
layout assumptions. This tests semantics and ABI IR interoperation, not
physical Arm execution or timing.

The full oracle generates/verifies **72 modules from 36 configured C units**
and checks matching owned IR from both preprocessors, ignoring only
`source_filename`. It then optimizes with LLVM, saves target assembly,
assembles through C08/C09 and links/extracts/encodes through C10-C12.
All **11 applications** produce `owned-ir.uf2` without unresolved symbols.
Independent LLVM decoding of all **38 owned-route objects**, including
discarded code, matches allocated bytes, canonical fixups and public symbols.
The harness additionally requires all 11 images/public-symbol sets to match
independent LLVM assembly plus unmerged LLD on that same assembly input;
`ir-linker-comparisons.tsv` records this downstream check.
Vendor firmware/NVRAM/CLM payloads remain unchanged.

One image matches the ordinary Clang baseline and ten differ; code identity
is not required for the owned frontend. Total flash payload is **294,528
versus 293,520 bytes (+1,008 / +0.34%)**, including unchanged vendor blobs.
Screen test is **1,680 versus 1,676**, diagnostics **16,204 versus 15,772**,
and Wi-Fi **250,644 versus 250,216** bytes. This new frontend trade-off is
separate from C10's accepted approximately 0.1% pooling difference.
Stack high-water marks, timing and physical badge behavior remain unmeasured.
Nothing was flashed; production build defaults are unchanged.

## Ninth replacement: C07 Cortex-M33 code generation

[badge_codegen.c](../src/badge_codegen.c) is the standalone native C backend's
command-line driver:

```sh
make -C badge-compiler
badge-compiler/build/badge-codegen \
  badge-compiler/build/oracle/universe26/firmware/screen_test.o.oracle/owned-ir.optimized.ll \
  -o badge-compiler/build/screen-backend.s
badge-compiler/build/badge-asm badge-compiler/build/screen-backend.s \
  -o badge-compiler/build/screen-backend.o
```

It parses textual IR itself and emits Cortex-M33 Thumb assembly, without
invoking Clang/llc, using LLVM libraries, importing machine-code captures or
falling back to another backend. C08/C09 encode its instructions and objects.
The input contract is **backend-ready C05 integer/pointer IR**, legalized by
our C06 or optimized with LLVM 22.1.8 `opt -passes='default<Oz>'`, the fixed
32-bit little-endian target data layout and
`thumbv8m.main-unknown-none-eabi` triple. This is not a general LLVM IR parser
or an IR verifier.

### C07 backend seams for the P-series

The behavior-preserving preparation keeps the textual IR and assembly
boundaries and separates:

- [codegen.h](../src/codegen.h): the callable `badge_codegen_compile` API. It borrows
  the source for the call, returns 1 on success and transfers a NUL-terminated
  assembly buffer to the caller; the reported size excludes the terminator.
  The caller frees the buffer. On failure it returns 0, reports diagnostics
  on stderr and leaves supplied outputs NULL/zero. Calls have independent
  contexts, including after failed compilations.
- [codegen.c](../src/codegen.c): context/resource lifetime, IR parsing, globals,
  instruction selection, PHI edges and call lowering. The diagnostic filename
  does not cause file access. The borrowed input retains its original charge
  against the 128 MiB allocation budget.
- [codegen_internal.h](../src/codegen_internal.h): private model and helper declarations,
  not a public machine-IR or multi-target abstraction.
- [codegen_frame.c](../src/codegen_frame.c): unchanged SSA lookup/stack-home allocation,
  PHI scratch and byval-object frame planning, incoming AAPCS arguments and
  prologue/epilogue emission. Outgoing calls remain with instruction selection.
- [codegen_thumb.c](../src/codegen_thumb.c): immediate, frame-address/stack-access,
  symbol-literal, normalization, save and scalar-memory emission helpers.
  `cg_stack` is the localized P01 direct-frame-access seam.
- [codegen_register.c](../src/codegen_register.c): subsequent opt-in P02 local residency,
  bounded use tracking, scratch reservations and private-home write-back policy.
- [codegen_global.c](../src/codegen_global.c): subsequent opt-in P03 CFG liveness,
  interference coloring, pressure spills and live-across-assembly preservation.
- [codegen_select.c](../src/codegen_select.c): subsequent opt-in P04 bounded native
  immediate/address selection, multiply-add fusion and quotient/remainder reuse.
- [badge_codegen.c](../src/badge_codegen.c): only CLI parsing, bounded file reads,
  alias checks and atomic output replacement.

Link `codegen.c codegen_frame.c codegen_thumb.c codegen_register.c codegen_global.c codegen_select.c` to call the backend without
the CLI or filesystem I/O. The release, sanitizer and unit-test Make targets
share these source/header lists; owned and oracle provenance includes them.
The public API requires non-NULL source, filename and output pointers; outputs
must not own an earlier result. The source need not be NUL-terminated, but
embedded NUL bytes within its supplied length are rejected.

No instruction-selection, lookup algorithm, register, frame, ABI, optimization
default or driver-process policy changes are part of this preparation.
This preparation implemented no P-series optimization; P00's separate
[measurement campaign](#p00-isolated-kernel-baseline) is now complete and
P01 is subsequently implemented and qualified [below](#p01-direct-thumb-2-frame-addressing);
P02 is subsequently implemented as [opt-in local residency](#p02-block-local-register-residency);
P03 is subsequently qualified as [opt-in cross-block allocation](#p03-cross-block-register-allocation);
P04 is subsequently qualified as [opt-in native scalar selection](#p04-native-scalar-selection);
P05 is subsequently qualified as [opt-in word memory selection](#p05-word-memory-selection);
P06 is subsequently qualified as [opt-in loop-invariant motion](#p06-loop-invariant-motion).
In particular, this preparation is not register
allocation or an in-process replacement for the complete shell pipeline.

Qualification (2026-10-08): the full owned rebuild preserves all **110**
captured artifacts byte-for-byte: 36 assembly files, 38 objects, 11 ELF
images, 14 binary files (including three radio assets) and 11 UF2 images.
All 36 production modules also emit identical assembly with the release,
ASan/UBSan and Apple-host-compiler builds. `test-oracle` and system-only-PATH
`test-owned` pass, including Cortex-M33 differential execution, bidirectional
mixed LLVM/owned AAPCS calls and all-owned ABI execution. The C07 sanitizer
test retains 33 positive, 27 negative and 113 mutation fixtures and adds
API/CLI exact-output equality, unchanged borrowed input, repeated success
after failure, explicit failure outputs, exact 16 MiB input acceptance and
oversize/NUL/invalid-argument rejection. CLI output preservation and
same-path/hard-link/symlink alias rejection remain covered. No hardware was
flashed; firmware bytes, optimization defaults and existing stack-readiness
limitations are unchanged.

### P01 direct Thumb-2 frame addressing

P01 is **opt-in**. The legacy API and CLI default remain materialized; query
the actual CLI default with `badge-codegen --default-frame-addressing`.
The explicit [API](../src/codegen.h) accepts `BADGE_FRAME_MATERIALIZED` or
`BADGE_FRAME_DIRECT`; invalid selections fail with diagnostics and cleared
outputs.

```sh
badge-compiler/build/badge-codegen --frame-addressing=direct input.ll -o output.s
BADGE_CODEGEN_FRAME_ADDRESSING=direct bash badge-compiler/scripts/build-owned.sh
LLVM_BIN=/opt/homebrew/opt/llvm/bin BADGE_CODEGEN_FRAME_ADDRESSING=direct \
  node badge-compiler/tools/compiler-compare.mjs --timing --seed=3276377845 \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

In [cg_stack](../src/codegen_thumb.c), word-aligned offsets within the Thumb-2
unsigned imm12 range emit a single `ldr.w`/`str.w rN, [r7, #offset]`.
The largest eligible offset is 4092. Zero is eligible; unaligned offsets,
4096 and larger retain the original MOVW/MOVT plus ADD and load/store path.
Each eligible access removes two instructions without changing APSR flags.
This is not Thumb-16 width selection or register allocation. Materialized
alloca/byval pointers, ordinary/MMIO accesses, outgoing SP-relative stores,
frame allocation/alignment, PHI staging and saved registers are unchanged.
Loads into indirect-call scratch `r12` remain valid; `r7` remains reserved
as the frame base. Each i64 half is selected independently.

`BADGE_OPT_PASSES` still selects C06 only. Owned/oracle builds record
`codegen-frame-addressing.txt`; corpus reports record
`configuration.codegen_frame_addressing`, and hardware reports record
`toolchain.ownedFrameAddressing` alongside the native tool hashes.
Historical P00 reports predate that field and retain their original provenance.
Changing an environment selection requires a forced target rebuild, as in
the build/benchmark/comparison drivers above.

Both modes pass all retained C07 fixtures under ASan/UBSan, exact helper tests
at zero, encoding/alignment boundaries, 4092/4095/4096, 65532/65536 and 131116,
and release/sanitizer assembly agreement for all 36 production modules.
[LLVM encoding fixtures](../tests/fixtures/frame_addressing_oracle_fixture.S) independently
check forced-wide low/high-register accesses and fallback sequences.
Independent LLVM MC decoding of the direct production corpus also matches
all 38 objects' allocated bytes, canonical fixups and public symbols.
[Large-frame ABI execution](../tests/fixtures/frame_addressing_abi_fixture.c) combines a
61,000-byte local object, a 4,000-byte byval argument, split register/stack
arguments, indirect calls, wide carry and incoming offsets above 64 KiB.
Both mixed LLVM/owned directions and all-owned execution pass; the original
narrow/record/sret ABI and optimizer differential fixtures still pass.
The QEMU-only startup uses mapped MPS2 RAM for these large stacks; badge
startup and memory maps are unchanged. Byval source loads beyond the existing
4095-byte displacement limit remain a pre-existing unsupported case.

The [fresh materialized corpus](../build/c06-metrics/p01-materialized/report.json)
and [direct corpus](../build/c06-metrics/p01-direct/report.json) have identical
preprocessed/frontend/optimized inputs, vendor assets and frame/call models.
Across 36 modules, 56,508 eligible accesses remove exactly 113,016
instructions: 254,744 -> 141,728 (44.36%). Summed across all eleven images,
nonvendor bytes fall 1,748,444 -> 1,285,180, saving 463,264 bytes (26.50%).
The largest frame including saves remains 3,672 bytes; all existing incomplete
entry-call bounds remain incomplete.

The pre-edit artifact hashes match 105/110 fresh default outputs. The five
Wi-Fi artifacts carry an older generated build ID, before P00's root Makefile
changes; restoring just the three historical ID constants reproduces the
original assembly SHA-256 exactly. No default C07 instruction change is hidden
by that build-identity difference.
The complete `test-oracle` and `test-owned` suites pass; the latter runs with
system utilities and an explicit Node shim, without Homebrew LLVM tools.

Two fixed-seed campaigns each swap both physical roles and match all six
whole modules and six kernels, with unchanged source identity, LLVM control
UF2, clocks and work. The [first controlled P00 comparison](../../build/compiler-compare-runs/run.91a8cd/p00-comparison.json)
and [independent repeat](../../build/compiler-compare-runs/run.xPAgAz/p00-comparison.json)
show:

| Kernel | Reduction in owned elapsed time, first / repeat |
| --- | ---: |
| Arithmetic | 46.96% / 46.96% |
| Branch/PHI | 49.33% / 49.36% |
| Cross-file calls | 38.92% / 38.94% |
| Unsigned div/rem | 44.43% / 44.43% |
| Signed div/rem | 43.58% / 43.56% |
| Ordinary memory | 47.71% / 47.69% |

Whole-module reductions range from 35.57% to 48.93%, with no slowdown.
[Candidate repeat variation](../../build/compiler-compare-runs/run.xPAgAz/repeat-comparison.json)
is below 0.061% for kernels and 0.118% for whole modules. The owned kernels
still take 4.59-9.02 times LLVM's time; these are measured workload results,
not a claim of LLVM parity or instruction-count-to-cycle equivalence.
[Ordinary full-frame correctness](../../build/compiler-compare-runs/run.YQmLMC/results.json)
also passes, using archived UF2 files for real flashing. Both badges retain
ordinary diagnostics and are recovered to BOOTSEL. Defaults remain unchanged.

### P02 block-local register residency

P02 is implemented as a bounded **one-entry, opt-in cache**, not a general
register allocator. Its [policy module](../src/codegen_register.c) handles only
four-byte SSA integers/pointers with 32-bit representation. It keeps a new
result in `r0`; the existing, already-saved `r8` preserves it when scratch
use would overwrite it. Simple scalar binary consumers may reuse `r0` in
place when the dirty value dies at that instruction, or its stack home is
already current. `r1-r6` remain scratch, `r7` remains the frame base and
`r12` remains address/call scratch. No saved-register mask, frame allocation,
alignment or outgoing-argument layout changes.

All SSA stack homes remain allocated. Block-local dirty values are written
back on eviction only if still needed; dead private-home stores are omitted.
Values referenced by another block or a PHI use write-through homes.
Calls (including intrinsics/assembly), branches, switches, PHIs, returns
and unreachable instructions flush and invalidate residency before lowering.
No cache state propagates across blocks. Wide, aggregate, boolean and narrow
values keep their existing lowering. Actual ordinary/volatile/MMIO loads
and stores are not cached or reordered: only private SSA homes are involved.
Named-type/metadata references are conservative use overestimates. Analysis
uses a tracked-allocation hash table and a 33,554,432-unit work budget;
exhaustion fails explicitly rather than silently disabling the feature.

```sh
BADGE_CODEGEN_FRAME_ADDRESSING=direct BADGE_CODEGEN_REGISTER_RESIDENCY=local \
  bash badge-compiler/scripts/build-owned.sh
badge-compiler/build/badge-codegen --frame-addressing=direct \
  --register-residency=local input.ll -o output.s
```

The two C07 selections are independent. The default remains
`--register-residency=stack`, discoverable with `--default-register-residency`.
The old APIs preserve that default; `badge_codegen_compile_with_options`
accepts both enums. Invalid/duplicate CLI selections and invalid API enums
fail explicitly. Owned/oracle builds record `codegen-register-residency.txt`;
corpus reports record `configuration.codegen_register_residency`, and hardware
reports record `toolchain.ownedRegisterResidency`. C06 selections and build
parallelism remain unchanged. Force target rebuilds when switching modes.

All retained C07 fixtures run in all four frame/residency combinations.
Exact chain, repeated-operand and live-out/write-through checks cover the
`r0` path; resource-limit and invalid-option checks retain failure semantics.
The [new mixed-ABI fixture](../tests/fixtures/register_residency_abi_fixture.c) adds register
pressure, eight arguments, indirect/nested calls, assembly barriers, wide
carry and record carriers. Existing self-loop/parallel/irreducible PHIs,
wide/narrow/pointer/volatile and large-frame oracle fixtures still execute
correctly. Full `test-oracle`, owned-only tests, driver/report tests,
release/ASan/UBSan agreement for all 36 modules and independent LLVM MC
equality for all 38 objects pass.

The [P01 control snapshot](../build/c06-metrics/p02-stack/report.json) has
byte-identical output hashes to the original P01 corpus. The
[final P02 corpus](../build/c06-metrics/p02-local-final/report.json) retains exact
preprocessed/frontend/optimized hashes and frame/call-stack models.
Across emitted functions, it avoids **7,212 frame reloads and 6,963 frame
stores**. Aggregate nonvendor bytes fall 1,285,180 -> 1,257,464:
**27,716 bytes (2.16%) smaller**. The largest frame including saves remains
3,672 bytes; unresolved entry-call bounds remain unresolved.

**Runtime is mixed, not a blanket speedup.** Two complete fixed-seed,
role-swapped campaigns match all twelve LLVM results and compare against
the corresponding P01 campaigns with identical source/work/clocks and LLVM
control images:
[first comparison](../../build/compiler-compare-runs/run.U9yRM5/p01-comparison.json)
and [repeat comparison](../../build/compiler-compare-runs/run.fXoiNK/p01-comparison.json).

| Workload | Owned elapsed-time change versus P01, first / repeat |
| --- | ---: |
| Whole arithmetic | +6.98% / +6.98% |
| Strings | -35.97% / -36.07% |
| Whole memory | +3.16% / +3.16% |
| Control flow | +1.49% / +1.49% |
| Cross-file ABI | +1.42% / +1.38% |
| Graphics | +0.84% / +0.83% |
| Arithmetic kernel | -4.28% / -4.30% |
| Branch/PHI kernel | +0.64% / +0.70% |
| Call kernel | -1.08% / -1.03% |
| Unsigned div/rem kernel | +0.91% / +0.90% |
| Signed div/rem kernel | -4.53% / -4.58% |
| Ordinary-memory kernel | -1.24% / -1.26% |

Negative means faster. [Final repeat variation](../../build/compiler-compare-runs/run.fXoiNK/repeat-comparison.json)
is below 0.04% for every workload. The initial r8-only experiment was slower
on most workloads; retaining last-use results in r0 substantially reduced
move overhead. The remaining whole-arithmetic regression is reproducible,
not measurement noise; moves, unchanged wide/PHI lowering and changed code
layout have not been separately attributed. This is why P02 remains opt-in,
not part of a newly enabled default profile. It does not establish that
register residency, smaller code or lower SRAM traffic always means faster
RP2350 execution.

An incomplete USB-deadline run is retained but excluded from timing evidence.
[Ordinary full-frame diagnostics](../../build/compiler-compare-runs/run.6Fq5G4/results.json)
also pass. Both badges retain ordinary diagnostics and are recovered to
BOOTSEL. This local mode does not implement cross-block allocation; the
separate P03 mode below does.

### P03 cross-block register allocation

P03 is implemented and hardware-qualified **opt-in**:

```sh
BADGE_CODEGEN_FRAME_ADDRESSING=direct BADGE_CODEGEN_REGISTER_RESIDENCY=global \
  bash badge-compiler/scripts/build-owned.sh
badge-compiler/build/badge-codegen --frame-addressing=direct \
  --register-residency=global input.ll -o output.s
```

`BADGE_RESIDENCY_GLOBAL` is the corresponding public options-API enum.
`stack`, `local` and `global` are alternative residency policies, not C06
passes. Both old APIs, the CLI/environment defaults and the root LLVM
compiler default are unchanged. Both frame-addressing modes work with global
allocation; direct addressing is the measured pairing. Force target rebuilds
when changing selections. The existing register-selection provenance files
and JSON fields record `global` independently of C06 passes.

The later [Z02 extension](maze-optimization.md#z02-candidate-contract) adds
`global-flex` / `BADGE_RESIDENCY_GLOBAL_FLEX` as a separate policy, now the
qualified CLI/API default at Z02 completion; Z03's `global-compact` is now
the default, and `global-flex` remains the explicit Z02 control.
It shares this CFG allocator but admits r4/r5 only where scratch masks prove
them safe, adds soft copy/PHI affinities and uses pressure tie-breaks after
use cost. Original `global` retains its three-register pool and ranking.

The [allocator](../src/codegen_global.c) uses the existing C07 model, not a new
machine IR. Bitset CFG liveness reaches a fixed point; PHI operand uses belong
to predecessor edges rather than the successor body. A symmetric interference
graph and deterministic, weighted greedy coloring assign only four-byte
32-bit integer/pointer values to **r8, r10 and r11**. PHIs and cross-block uses
receive priority. Excess pressure keeps the existing stack-backed lowering;
unused private-home stores can be omitted. All stack homes remain allocated.
Narrow integers/booleans, i64 pairs and aggregate carriers retain their
original normalization, memory accesses and lowering.

`r0-r6` remain scratch, `r7` the frame base, `r9` reserved and `r12` the
address/indirect-call scratch. Functions using r10/r11 save/restore
`{r4-r8,r10,r11,lr/pc}`: **32 saved bytes rather than 24**, still eight-byte
aligned. Incoming stack arguments use that actual save size; outgoing
arguments and local frame allocation do not change. Ordinary calls preserve
allocated values under AAPCS, including indirect and mixed LLVM/owned calls.
Inline assembly spills allocated live-across values to their homes and
reloads them afterward. Unsupported clobbers still fail explicitly.

The [edge lowerer](../src/codegen.c) captures stack/constant/aggregate PHI sources
before any destination changes. Allocated register-to-register PHIs use
parallel moves; r6 breaks cycles. Stack destinations retain staged copies,
so cycles mixing allocated and spilled values remain safe. Copies occur
inside the selected branch/switch arm: critical edges, self-loops and
irreducible control flow need no speculative copies or CFG rewriting.
No ordinary, volatile or MMIO access is widened, cached or reordered.

Analysis and lookup share a **33,554,432 work-unit limit per compilation**,
with explicit failure rather than a silent fallback. Graph/bitset storage
uses the existing tracked 128 MiB allocation budget; input/token/output
limits remain in force. Named-type/label references may conservatively
overestimate uses and interference, never authorize a missing home.

Per-function assembly records allocated/spilled values and avoided SSA-home
loads/stores, register PHI copies and assembly spill/reload pairs. These are
**static emission counts**, not dynamically executed traffic. Direct PHI
forwarding is included in avoided home-load counts; its scratch store/load
pair is counted separately. Assembly preservation adds a store and reload
per reported pair.

#### P03 qualification and measurements (2026-10-08)

All retained positive/negative/mutation fixtures run under all **six**
frame/residency combinations. New exact checks cover fully allocated PHI
edges, three-register cycles, forced spills, live-across assembly, indirect
callee reads and API/CLI output preservation. The
[raw-IR execution fixture](../tests/fixtures/global_residency_oracle_fixture.ll) tests zero/one/
multiple rotations, a three-way PHI cycle and critical edges against LLVM.
Existing mixed/all-owned eight-argument/byval/callback/large-frame fixtures,
narrow/wide/volatile cases and parallel/irreducible loop suites pass QEMU
with owned and LLVM optimizers. Full `test-oracle`, owned-only and
driver/report tests pass. All 36 modules have identical release/ASan/UBSan
assembly, and all 38 objects match independent LLVM MC allocated bytes,
canonical fixups and public symbols, including discarded sections.

The [fresh P02 control](../build/c06-metrics/p03-local-control/report.json)
reproduces the original P02 output hashes exactly. The
[final P03 corpus](../build/c06-metrics/p03-global-final/report.json) preserves
all preprocessed/frontend/optimized inputs, local frame sizes, outgoing
allocations and call models. Aggregate nonvendor bytes fall
**1,257,464 -> 1,215,132**, saving **42,332 bytes (3.37%)**. Across emitted
functions: 9,810 values are allocated, 3,976 used values remain stack-backed,
13,839 home loads and 11,183 home stores are avoided, 1,331 PHI scratch pairs
are replaced with register copies and 13 assembly spill/reload pairs remain.
These avoidance totals are relative to stack lowering, not additional
reductions on top of P02's counters.

The single forced-serial build samples take 6.30 seconds for the P02 control
and 6.61 seconds for P03 (about 5% higher). Host tools are bootstrapped outside
that interval; one sample per variant is not a repeat-qualified wall-time
claim. The normal bounded `-j4` policy is unchanged.

447 emitted functions use the larger save mask. The largest frame including
saves is **3,680 bytes**, versus P02's 3,672; the complete `hello_idle` entry
bound rises 128 -> 136 bytes. Previously unresolved call/interrupt/recursion
bounds remain unresolved; this does not establish complete stack readiness
for every firmware image.

Two complete fixed-seed, role-swapped campaigns match all twelve LLVM results.
Controlled comparisons require the same sources/work/clocks and LLVM control
UF2: [first P02 comparison](../../build/compiler-compare-runs/run.BSiasa/p02-comparison.json)
and [independent repeat](../../build/compiler-compare-runs/run.Gwdevd/p02-comparison.json).

| Workload | Owned elapsed-time change versus P02, first / repeat |
| --- | ---: |
| Whole arithmetic | -9.06% / -9.06% |
| Strings | -12.53% / -12.49% |
| Whole memory | -13.15% / -13.15% |
| Control flow | -13.40% / -13.39% |
| Cross-file ABI | -1.83% / -1.82% |
| Graphics | -8.33% / -8.33% |
| Arithmetic kernel | -10.98% / -10.95% |
| Branch/PHI kernel | -21.44% / -21.44% |
| Call kernel | -6.74% / -6.78% |
| Unsigned div/rem kernel | -10.84% / -10.82% |
| Signed div/rem kernel | -7.00% / -6.98% |
| Ordinary-memory kernel | -9.38% / -9.42% |

Negative means faster. [Repeat variation](../../build/compiler-compare-runs/run.Gwdevd/repeat-comparison.json)
is below 0.027% for every workload. All measured workloads improve over P02;
this is not a promise about unmeasured programs or parity with LLVM.
The final PHI-counter correction changes assembly comments only: all
production objects/images remain byte-identical to the qualified candidate.
[Ordinary full-frame diagnostics](../../build/compiler-compare-runs/run.iiVF8g/results.json)
pass, ordinary firmware is restored and both badges are recovered to BOOTSEL.
P03 remains explicit-only; P04's separate qualification follows below.

### P04 native scalar selection

P04 is implemented and hardware-qualified **opt-in**, independently of frame
addressing, register residency and C06 passes:

```sh
BADGE_CODEGEN_FRAME_ADDRESSING=direct \
BADGE_CODEGEN_REGISTER_RESIDENCY=global \
BADGE_CODEGEN_SCALAR_SELECTION=thumb \
bash badge-compiler/scripts/build-owned.sh
```

The CLI accepts `--scalar-selection=legacy|thumb` among its leading policy
flags, in any order. Duplicate/invalid selections fail before compilation or
hardware access. `--default-scalar-selection` reports `legacy`. The new
`badge_codegen_compile_with_scalar_selection` API takes an explicit selection;
the existing API entry points retain legacy selection and the existing
two-field options structure is unchanged. C07 defaults remain **materialized
frames, stack residency, legacy scalar selection and byte memory**; the root firmware default
remains LLVM and bounded `-j4` builds are unchanged.

Owned/oracle builds record `codegen-scalar-selection.txt`; corpus reports
record `configuration.codegen_scalar_selection` and hardware reports record
`toolchain.ownedScalarSelection`. Source provenance includes the selector and
[shared Thumb immediate predicate](../src/thumb_immediate.h), extracted unchanged
from the assembler rather than maintaining a second encoding definition.

[codegen_select.c](../src/codegen_select.c) admits canonical, unflagged i32 scalar
patterns and otherwise retains existing lowering:

- Legal Thumb modified immediates, ADDW/SUBW imm12 forms, modular negative
  add/sub constants and immediate shifts 0-31 avoid materialization. Zero
  arithmetic/shift identities omit the arithmetic instruction.
- Constant i32 GEP indices and record offsets use target low-word wrapping
  arithmetic. Variable i32 power-of-two strides greater than one use a
  shifted ADD operand; other widths/strides retain existing lowering.
- Adjacent same-block MUL/ADD pairs fuse to MLA only when the product has
  one conservative function-wide use. Local residency flushes on both
  boundaries so deferred multiplicands still have valid homes.
- Adjacent matching signed or unsigned quotient/remainder pairs, in either
  order, reuse the first hardware quotient in r4. The second result uses
  MOV or MLS. Exact/flagged operations, different operands and intervening
  instructions/calls/assembly/blocks reject reuse.

The selector keeps C06 IR, original liveness/allocation, stack homes, save
masks and ABI. It does not widen/reorder/cache ordinary, volatile or MMIO
memory accesses, replace variable SDIV/UDIV, perform magic division or
blanket multiply-to-shifts, or infer overflow/poison proofs. Conservative
use counting may reject opportunities, not omit a needed product.
Selection uses the existing tracked 128 MiB allocation budget and shared
33,554,432-unit bounded analysis/lookup budget, with explicit errors.

Per-function `@ scalar-selection:` comments count emitted immediate,
shifted-address, constant-address, multiply-add and shared-division patterns.
These are **static pattern counts**, not dynamic executions or a universal
instruction-saving count.

#### P04 qualification and measurements (2026-10-08)

All retained positive/negative/mutation sanitizer fixtures run under all
**twelve** frame/residency/selection combinations. Exact pattern/rejection
tests cover encoding boundaries, unencodable fallbacks, flags, single-use
dependencies and division barriers. CLI/API exact output, duplicate/invalid
flags and output preservation pass. The independent LLVM-compiled
[raw-IR caller](../tests/fixtures/scalar_selection_oracle_caller.c) checks wrapping boundaries,
signed division (excluding undefined INT_MIN/-1), both quotient/remainder
orders, deferred local operands, negative addresses, stride fallbacks and
memory guards against [owned and LLVM callees](../tests/fixtures/scalar_selection_oracle_fixture.ll).
Explicit stack/materialized, local/direct and global/direct Thumb execution,
mixed/all-owned ABI, wide/narrow/volatile/PHI/irreducible suites, full
`test-oracle`, system-only-PATH owned tests and all 24 driver/report tests pass.

The [fresh legacy control](../build/c06-metrics/p04-legacy-control/report.json)
reproduces P03 output hashes exactly, including the shared encoder extraction.
The [P04 corpus](../build/c06-metrics/p04-thumb/report.json) and
[invariant summary](../build/p04/corpus-summary.json) cover all 36 C modules,
38 objects and eleven images. Release and ASan/UBSan assembly agree for every
C module; all objects match independent LLVM MC allocated bytes, canonical
fixups and public symbols, including discarded sections. Inputs and C06 IR,
local/saved frames, outgoing arguments and call models remain identical.
Aggregate nonvendor bytes fall **1,215,132 -> 1,201,664**, saving
**13,468 bytes (1.11%)**. Static selections total 985 immediates, 2,057 constant
addresses, 115 multiply-adds and three shared divisions. Shifted variable
addresses are exercised by raw fixtures but have no reached candidates in
this corpus. The largest frame including saves remains **3,680 bytes**;
existing unresolved stack bounds remain unresolved. Single build samples
are recorded, not a repeat-qualified compilation-walltime claim.

Two complete fixed-seed, role-swapped campaigns match all twelve LLVM
results, with seven samples per interval and fresh nonces. Controlled P03
comparisons validate identical sources/work/clocks/devices and LLVM control
images: [first](../../build/compiler-compare-runs/run.BNic1c/p03-comparison.json)
and [independent repeat](../../build/compiler-compare-runs/run.CePptc/p03-comparison.json).

| Workload | Owned elapsed-time change versus P03, first / repeat |
| --- | ---: |
| Whole arithmetic | -6.25% / -6.25% |
| Strings | -2.94% / -2.95% |
| Whole memory | -1.77% / -1.76% |
| Control flow | -2.37% / -2.38% |
| Cross-file ABI | -1.39% / -1.40% |
| Graphics | -2.17% / -2.17% |
| Arithmetic kernel | -8.97% / -8.98% |
| Branch/PHI kernel | -1.98% / -1.96% |
| Call kernel | -1.54% / -1.51% |
| Unsigned div/rem kernel | -1.11% / -1.16% |
| Signed div/rem kernel | -12.65% / -12.64% |
| Ordinary-memory kernel | -1.59% / -1.53% |

Negative means faster. [Repeat variation](../../build/compiler-compare-runs/run.CePptc/repeat-comparison.json)
is below 0.036% for every workload; candidate images are identical across
accepted campaigns. An intervening campaign
[run.bWoiJA](../../build/compiler-compare-runs/run.bWoiJA/round2-collector.log)
hit the unchanged 60-second collector deadline in its second round; it was
retained as incomplete evidence and excluded, not treated as a pass. Both
devices recovered and a fresh complete retry passed; the timeout's cause is
not attributed. Improvements are not a promise about unmeasured programs
or LLVM parity. [Ordinary full-frame diagnostics](../../build/compiler-compare-runs/run.ekw970/results.json)
pass; ordinary images are restored and both badges are in BOOTSEL.
P04 remains opt-in; P05's separate qualification follows below.

### P05 word memory selection

P05 is implemented and hardware-qualified **opt-in**, independently of the
other C07 policies:

```sh
BADGE_CODEGEN_FRAME_ADDRESSING=direct \
BADGE_CODEGEN_REGISTER_RESIDENCY=global \
BADGE_CODEGEN_SCALAR_SELECTION=thumb \
BADGE_CODEGEN_MEMORY_SELECTION=word \
bash badge-compiler/scripts/build-owned.sh
```

`--memory-selection=byte|word` is a leading CLI policy flag, in any order
with the other three. Duplicate/invalid policies fail explicitly; the
hardware runner rejects invalid environment values before touching devices.
`--default-memory-selection` reports **byte**. The new
`badge_codegen_compile_with_selections` API takes both scalar and memory
selections; all previous APIs retain byte memory selection and the public
two-field options structure is unchanged. C06 passes, root LLVM, existing
C07 defaults and bounded `-j4` builds remain unchanged.

Owned/oracle builds record `codegen-memory-selection.txt`; corpus reports
record `configuration.codegen_memory_selection` and hardware reports record
`toolchain.ownedMemorySelection`. The implementation remains in the existing
[lowerer](../src/codegen.c) and [private value model](../src/codegen_internal.h), without
a general alias/provenance framework or a second memory lowering pipeline.

Eligibility is deliberately narrower than a nonvolatile `align 4` assertion:

- Only existing constant-size memcpy/memset intrinsic paths reaching C07 are
  considered. Memmove, C library calls, variable lengths and scalar accesses
  are not rewritten.
- The destination, and the source for copies, must derive from a local
  fixed-size alloca. Constant nonnegative GEP offsets retain a remaining
  object extent and reduce alignment as needed. Every transferred byte must
  fit that extent, and both actual pointer alignments must be at least four.
- Unknown pointers, even aligned parameters, globals, integer/device
  addresses, loaded pointers, PHIs/selects, variable/negative offsets and
  lost proofs retain the byte path. No alignment-only assumption authorizes
  a wider access to MMIO.
- The volatile operand must be the constant false/zero. Volatile operations
  retain the original byte accesses and ordering.

Eligible lengths of at least four use aligned 32-bit LDR/STR chunks and
exactly zero to three byte transfers for the tail. A single word is emitted
straight-line; multiple words use a bounded counted loop, not size-dependent
unrolling. Fills replicate the low byte into all four lanes, including
variable nonzero bytes. Zero/tiny lengths and every ineligible case preserve
the original byte lowering. There is no read/write beyond the intrinsic's
length, no speculative alignment probe and no memory-access reordering across
intrinsics. Original SSA homes, allocation, call barriers, save masks and
frame/ABI planning remain unchanged.

Per-function `@ memory-selection:` comments report word-copy/fill sites and
selected word/tail byte totals. These count **static intrinsic lengths**,
not dynamic traffic or the number of emitted loop instructions. Copy totals
count payload bytes once, not load bytes plus store bytes. Proof propagation
is conservative in emission order; a missed proof rejects an optimization,
never substitutes an assumed safe pointer.

#### P05 qualification and measurements (2026-10-08)

All retained positive/negative/mutation sanitizer fixtures now run under
**24** frame/residency/scalar/memory combinations. Additional exact checks
cover lengths 0/1/2/3/4/5/7/8/9/15/16/17/63/64/65, single-word and counted
paths, tail counts, align-1 and misaligned objects, unknown aligned
parameters, integer MMIO sources, volatile calls, out-of-extent rejection,
variable/negative GEPs, invalid API enums and four-policy CLI/API output
equality/preservation. Independent MC checks include post-increment word
and byte transfers and both fill-replication shifts.

The [raw LLVM IR fixture](../tests/fixtures/memory_selection_oracle_fixture.ll) and
[independent LLVM-compiled caller](../tests/fixtures/memory_selection_oracle_caller.c) compare
entire guarded buffers, patterned source bytes, all those lengths, nonzero
fills 0/1/127/128/255, volatile and unaligned fallbacks. Owned and LLVM
execution passes with global/direct, local/direct and stack/materialized
policies, alongside retained mixed/all-owned ABI, wide/narrow/volatile/PHI/
irreducible suites. The test-only byte helpers also expose the generic
memcpy/memset4 AAPCS names needed by LLVM's optimized oracle. Full
`test-oracle`, system-only-PATH owned tests and all 25 driver/report tests pass.

The [fresh byte control](../build/c06-metrics/p05-byte-control/report.json)
reproduces all P04 output hashes exactly. The
[word corpus](../build/c06-metrics/p05-word/report.json) and
[invariant summary](../build/p05/corpus-summary.json) preserve every input/C06
IR hash, local/saved frame, outgoing argument allocation and call model.
All 36 release/sanitizer modules agree, and all 38 objects match independent
LLVM MC bytes/fixups/public symbols, including discarded sections.
There are **73 word-copy and 52 word-fill sites across seven production
modules**, covering 1,180 selected payload bytes. This corpus has no selected
byte tails; the raw fixtures exercise them. Aggregate nonvendor bytes fall
**1,201,664 -> 1,201,328**, saving **336 bytes (0.028%)**. The maximum frame
including saves stays **3,680 bytes** and existing unresolved stack bounds
remain unresolved.

Two complete fixed-seed role-swapped campaigns match all twelve LLVM
results, with seven samples per interval and fresh nonces. Controlled P04
comparisons validate identical sources/work/clocks/devices and LLVM images:
[first](../../build/compiler-compare-runs/run.vgmiQ7/p04-comparison.json) and
[independent repeat](../../build/compiler-compare-runs/run.9dWqFc/p04-comparison.json).

| Workload | Owned elapsed-time change versus P04, first / repeat |
| --- | ---: |
| Whole arithmetic | -0.001% / -0.001% |
| Strings | -0.270% / -0.270% |
| Whole memory | -0.047% / -0.062% |
| Control flow | -0.003% / -0.002% |
| Cross-file ABI | -24.82% / -24.84% |
| Graphics | +0.006% / +0.003% |
| Arithmetic kernel | 0.000% / 0.000% |
| Branch/PHI kernel | +0.007% / 0.000% |
| Call kernel | -0.007% / -0.007% |
| Unsigned div/rem kernel | -0.030% / +0.024% |
| Signed div/rem kernel | +0.007% / -0.043% |
| Ordinary-memory kernel | -0.027% / -0.045% |

Negative means faster. The clear runtime benefit is the **cross-file ABI
workload**, which includes aggregate copying; this is not a 25% generic
memory speedup. Most other intervals are unchanged or within small measured
variation, and no isolated cause is assigned to the small string change.
[Repeat variation](../../build/compiler-compare-runs/run.9dWqFc/repeat-comparison.json)
is below 0.036% throughout; both accepted campaigns use identical candidate
UF2s. [Ordinary full-frame diagnostics](../../build/compiler-compare-runs/run.Shd6xP/results.json)
pass, ordinary images are restored and both badges are in BOOTSEL.
P05 remains opt-in; P06's separate qualification follows below.

### P06 loop-invariant motion

Z04 also exposes `loop-cost`, using this same motion and safety machinery
with a conservative costly-operation/live-range gate. It does not change
`loop-invariant`; it is not part of the default pipeline. Its
[candidate contract](maze-optimization.md#z04-loop-motion-candidate-not-promoted) documents
the operation filter, repeated-pass budget and qualification status.

The selected Z04 candidate is instead
[scalar expression cleanup](maze-optimization.md#z04-candidate-contract):
the existing typed combiner plus identity tests of extended i1 values.
It introduces no loop motion or memory reordering and adds no SSA definitions;
allocation follows the rewritten uses.
All twelve physical workloads improve in both qualification repeats;
the user confirmed gameplay and approved the new shared CLI/API default.

P06 registers the **opt-in C06 `loop-invariant` pass**. It does not change
the five-pass default or enable the opt-in C07 policies:

```sh
BADGE_OPT_PASSES=mem2reg,instruction-combine,constant-fold,loop-invariant,aggregate-memory,wide-division \
BADGE_CODEGEN_FRAME_ADDRESSING=direct \
BADGE_CODEGEN_REGISTER_RESIDENCY=global \
BADGE_CODEGEN_SCALAR_SELECTION=thumb \
BADGE_CODEGEN_MEMORY_SELECTION=word \
bash badge-compiler/scripts/build-owned.sh
```

The ordering first exposes SSA/constant operands, then performs motion
before aggregate and wide-division legalization. `BADGE_OPT_PASSES` still
selects C06 only; the four C07 policies remain separately recorded.
At the original P06 qualification, `badge-opt --list-passes` discovered the
new pass and `--default-passes` still
reports `mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division`.
The root firmware compiler remains LLVM and bounded `-j4` builds are unchanged.

[ir_loop_invariant.c](../src/ir_loop_invariant.c) reuses the M02 typed operand,
CFG, RPO and dominator views. A dominator backedge identifies a natural loop;
reverse predecessor closure unions all its latches. Every member must be
reachable and dominated by the header. The only destination is an
**already-existing unique reachable preheader** with one successor, the
header. Multiple entries, missing/conditional preheaders and ineligible
irreducible/unreachable regions are not rewritten. No blocks, edges, PHIs
or SSA names are created or changed.

The whitelist is unflagged integer add/sub/mul/bitwise arithmetic,
comparisons, casts and non-inbounds GEPs. Shifts need a literal amount below
the integer width. Literal undef/poison, metadata-bearing instructions,
overflow/exact/disjoint/inbounds flags and all nonwhitelisted operations
are rejected. Loads, stores, volatile/MMIO, calls, assembly, allocations,
freeze and general memory/alias reasoning remain outside this pass.
Address computation may move; the memory access using it does not.

An integer division/remainder can move only when it is unflagged **i32**,
the literal divisor is nonzero after target-width normalization, and
signed overflow is excluded. For divisor -1, the numerator must also be
literal and not INT_MIN. Variable divisors and unproved overflow/exactness
retain their original placement; general i64 division stays with the
existing legalizer. This does not replace hardware SDIV/UDIV or introduce
magic division.

Each local operand must either be a parameter or have a definition outside
the loop that dominates the preheader. Already-hoisted definitions satisfy
the same test, allowing dependency chains. Inner loops are processed first;
their pending motion is visited in its actual destination order so outer
motion remains topological and idempotent. Original instruction text is
inserted before the preheader terminator and removed from its old position.
The owned verifier runs on the rewritten module. Zero-trip loops may perform
additional **nontrapping pure computation**, never newly execute an
unproved division or memory access.

Work is capped at **50,000,000 units per pass**, including conservative
worst-case charges for the shared dominator query, predecessor/member walks,
motion records and copied text. Exhaustion fails explicitly; there is no
silent fallback. Analysis and records share the existing tracked 128 MiB
arena, with input/token/output and pipeline limits preserved. Observations
record natural loops with preheaders, rejected entries/operands, unique
`invariant_hoisted` transformations and additional `nested_motion` events.
These are static counts, not dynamically eliminated executions.

#### P06 qualification and measurements (2026-10-08)

[38 ASan/UBSan, LLVM-verified and idempotent fixtures](../tests/ir_loop_invariant_test.c)
cover arithmetic/address/cast dependencies, safe and rejected division,
flags, poison/undef, variable and excessive shifts, volatile/assembly,
multiple latches/entries, missing preheaders, irreducible/unreachable loops,
nested motion, 2,000 independent expressions, and atomic malformed-input/
work-limit/allocation failures. The
[raw IR](../tests/fixtures/loop_invariant_ir_fixture.ll) and
[independent LLVM caller](../tests/fixtures/loop_invariant_oracle_caller.c) execute zero/one/
many iterations, nested loops with outer-dependent values, irreducible
entries, volatile guards, and zero-trip zero-divisor/INT_MIN overflow cases.
The [C source fixture](../tests/fixtures/loop_invariant_oracle_fixture.c) also agrees with
LLVM source/IR and host execution. Explicit P06 execution passes with
global/direct, local/direct and stack/materialized C07 policies, alongside
retained mixed/all-owned ABI, PHI/pressure/wide/narrow/volatile suites.
Full `test-oracle`, final owned-only tests and all 25 driver/report tests pass.

The [fresh P05 control](../build/c06-metrics/p06-p05-control-final/report.json)
reproduces P05 output hashes exactly. The
[final P06 corpus](../build/c06-metrics/p06-loop-final/report.json) and
[invariant/pressure summary](../build/p06/corpus-summary.json) cover all 36 C
modules, 38 objects and eleven images. Optimizer and backend release/
sanitizer output agree for all 36 modules; all objects match independent
LLVM MC bytes, fixups and public symbols, including discarded sections.
Input/C05 hashes, total/per-function instruction and opcode counts,
volatile access counts, local/saved frames, outgoing arguments and call
models remain unchanged.

Across **347 natural loops with existing preheaders**, P06 hoists **281
unique instructions**, with 13 additional nested moves, changing 23 optimized
IR modules. Aggregate nonvendor bytes grow **1,201,328 -> 1,201,476**:
**148 extra bytes (0.0123%)**. The largest frame including saves remains
**3,680 bytes**; unresolved stack bounds remain unresolved.

Longer live ranges are a real tradeoff: global allocated/stack-backed used
values change **9,810/3,976 -> 9,742/4,044**; static avoided home loads/stores
change **13,765/11,112 -> 13,705/11,048**. These counters describe emitted
lowering, not dynamic loop traffic. Regenerated control assembly matches its
archived hashes exactly. Zero-trip setup cost and register pressure prevent
a claim that every program improves.

Two complete fixed-seed, role-swapped campaigns match all twelve LLVM
results, with seven samples per interval and fresh nonces. Controlled P05
comparisons validate identical sources/work/clocks/devices and LLVM images:
[first](../../build/compiler-compare-runs/run.4Lq5L2/p05-comparison.json) and
[independent repeat](../../build/compiler-compare-runs/run.p8jFi1/p05-comparison.json).

| Workload | Owned elapsed-time change versus P05, first / repeat |
| --- | ---: |
| Whole arithmetic | -12.595% / -12.595% |
| Strings | -1.064% / -1.068% |
| Whole memory | +0.774% / +0.771% |
| Control flow | -0.011% / -0.011% |
| Cross-file ABI | -0.650% / -0.639% |
| Graphics | -2.283% / -2.284% |
| Arithmetic kernel | -0.090% / -0.090% |
| Branch/PHI kernel | -0.057% / -0.064% |
| Call kernel | -0.071% / -0.086% |
| Unsigned div/rem kernel | -0.090% / -0.072% |
| Signed div/rem kernel | -0.035% / -0.035% |
| Ordinary-memory kernel | -0.099% / -0.054% |

Negative means faster. Arithmetic and graphics improve, but **whole memory
regresses reproducibly by about 0.77%**. Increased register pressure is
measured, not established as the isolated cause of that regression. Small
kernel differences should not be overinterpreted:
[repeat variation](../../build/compiler-compare-runs/run.p8jFi1/repeat-comparison.json)
reaches 0.063%. Both campaigns use identical candidate UF2s.
[Ordinary full-frame diagnostics](../../build/compiler-compare-runs/run.TTu7bE/results.json)
pass, ordinary images are restored and both badges are in BOOTSEL.
**P06 stays explicit-only with mixed runtime results.** P00-P06 are now
implemented and qualified within their documented scopes, not enabled as
one new default pipeline.

### Selection, allocation and ABI

- Integer widths 1/8/16/32/64, opaque pointers, fixed arrays and literal
  records; sectioned globals, byte strings, symbol addresses and relocations.
  Read-only zero constants remain in read-only file-backed sections.
- Scalar integer/pointer arithmetic, comparisons, casts, memory accesses,
  GEP, select, freeze, aggregate carriers and insert/extractvalue.
  Wide add/subtract, multiply, bitwise operations and comparisons are owned;
  wide division directly supports signed positive power-of-two constants,
  and C06 lowers general division/remainder to owned helper IR.
- Blocks, conditional branches, switches and parallel PHI copies. Edge
  copies stage every incoming word before writing destinations, including
  cyclic PHIs. Baseline local literal pools avoid long literal reach.
  D06's opt-in `thumb-relax` uses supported MOVW/MOVT address relocations
  for signed-16-bit addends, retaining guarded pools for larger addends
  and preserving those PHI transfers.
- AAPCS soft-float register/stack argument placement, 8-byte alignment,
  narrow argument normalization and signed/unsigned return extension,
  record carriers, `sret`, aligned `byval` and indirect calls. Internal
  `fastcc` functions/calls are consistently lowered to our AAPCS convention;
  external/indirect `fastcc` and unsupported ABI attributes are diagnosed.
- Fixed-size inline copy/clear intrinsics, lifetime hints, scalar min/max/abs,
  and operand-free inline assembly with no clobbers, memory/flag clobbers,
  or their combination.
  Memory access ordering is preserved. Aligned volatile scalars keep their
  access width; unaligned nonvolatile scalars use bytewise expansion.
  [PO11](assembly-effects.md) adds explicit `thumb-effects` instruction-local
  register preservation for proven NOP sequences; memory barriers and unknown
  assembly stay conservative. [PO18](current-promotion.md) subsequently
  qualifies affected display/USB timing and promotes the combined chain.
  [PO12](lightweight-frames.md) adds explicit `thumb-frames`, finalized save
  masks, stable-SP leaf frames, resolved incoming offsets and bounded `r3`
  residency. It inherits PO11 and is included in the PO18-qualified default.
  [PO13](control-selection.md) adds explicit `thumb-control`, narrow/pointer
  adjacent predicates, decrement/flag selection and effect-preserving CFG edges.
  [PO14](operand-selection.md) adds explicit `thumb-operands`, bounded
  flag-neutral numeric/symbol reuse, direct scalar memory/stack arguments
  and canonical narrow-value transport. It preserves existing scratch masks,
  effect order and the physical-timing promotion gate.
  [PO15](loop-scalar.md) adds explicit `loop-scalar`, bounded pure scalar
  motion and modular i32 induction. Final PO14 code generation and owned
  assembly reject code/frame/save/spill growth per proposed function; other
  backend policies have no profitability qualification. Defaults stay unchanged.
  [PO16](cross-function.md) adds explicit `cross-function`: module-local,
  non-address-taken helper specialization with reduced argument signatures,
  bounded straight-line/tail-forwarder inlining and identical-helper sharing.
  Whole-module owned encodings include retained generic bodies and clones;
  caller/clone frame, save and spill growth is rejected independently of size.
  Public/indirect/recursive ABIs remain unchanged; unknown assembly references
  retain the module, `noinline` is honored and unsupported forced contracts
  diagnose. No cross-translation-unit or native tail-frame assumptions are made.
  [PO17](final-widths.md) adds `thumb-widths`, inheriting PO14 with
  flag-preserving destructive ADD, low-register memory/normalization forms
  and owned-assembler final branch/literal-width selection. It preserves
  allocation and instruction order, without D04 flag-setting arithmetic or
  D05 scheduling. The PO16-input corpus saves 35,070 executable bytes and all
  twelve images shrink; frames/saves/spills/calls are unchanged. It is a
  host-qualified size candidate, not a physical XIP-speed/default promotion.
- Conservative default **stack-backed SSA allocation**: each SSA value has a slot,
  r0-r6/r12 are scratch registers and r7 is the frame pointer. r4-r8/lr are
  saved, frame sizes are multiples of eight and large offsets use computed
  addresses. Opt-in [P02](#p02-block-local-register-residency) and
  [P03](#p03-cross-block-register-allocation) avoid private-home traffic while
  retaining those slots; instruction scheduling is not implemented.
  Assembly comments report the local frame plus 24 saved-register bytes,
  or 32 when P03 uses r10/r11;
  outgoing arguments, nested calls and interrupts add further stack usage.

Unsupported input fails explicitly before output replacement: floating
types/operations, vector/named IR types, atomic accesses, unaligned volatile
scalars, module assembly, variadic definitions, dynamic/count allocas,
dynamic-length memory intrinsics, unlegalized general wide division/remainder
and variable wide shifts,
wide switches/min/max/abs, nested/non-word aggregate SSA components and aggregate
loads/stores wider than eight bytes. Our optimized C05 record accesses use
scalar operations/carriers instead. Special LLVM runtime globals such as
constructors/`llvm.used` are rejected; C05's `llvm.compiler.used` is consumed
as an optimization-retention hint, not a linker-GC root. Section/symbol
names are restricted to simple assembler-safe spellings.

Bounds: 16 MiB input/object types, 128 MiB tracked allocations, 64 MiB
assembly output, one million tokens, type depth 64, 256 arguments/fields,
8-byte stack alignment and 64 KiB per local frame/outgoing argument area.
Atomic host output replacement preserves existing output on errors and
rejects aliases.

### C07 validation

[badge_codegen_test.c](../tests/badge_codegen_test.c) exercises **33 positive,
27 negative and 113 mutation fixtures** under ASan/UBSan, plus CLI
preservation/hard-link/symlink checks. Positive outputs are assembled through
C08/C09, including large offsets, PHIs, unaligned nonvolatile accesses,
wide arithmetic/comparisons, function sections and read-only zero constants.
The optional `build/badge-codegen-sanitize` target validates production IR
through the same CLI with sanitizers.

[codegen_oracle.sh](../tests/codegen_oracle.sh) executes Thumb firmware on QEMU's
**MPS2 AN505 Cortex-M33**, not host-retargeted IR. The integer/control-flow
fixture runs through our backend, LLVM's backend on the same IR, and
ordinary Clang compilation. It exercises volatile accesses, cyclic PHIs,
indirect/six-argument calls, an 8 KiB local array, 64-bit multiplication,
carry/borrow, signed division rounding and comparisons.
Bidirectional mixed Clang-source/owned calls and an all-owned ABI image
cover narrow/bool values, record sizes 1/2/3, sret, alignment eight and
80-byte byval arguments, including signed-to-unsigned narrow conversions.
Test-only startup provides semihosting exit and the LLVM oracle's AEABI
memory helpers. Execution has a timeout and nonzero results fail.
QEMU models the CPU, not RP2350 peripherals or badge timing.

The full `backend` oracle route is:

```text
C -> badge-cpp -> badge-ir -> LLVM opt -> badge-codegen
  -> badge-asm / badge-object -> badge-link -> elf2bin -> bin2uf2 -> UF2
```

All 36 C units lower through our backend; the two preprocessed assembly units
join through C08/C09. All 11 applications link without unresolved symbols.
`codegen-comparisons.tsv` records the C units; independent LLVM assembly
and unmerged LLD must reproduce every backend image/public-symbol set in
`codegen-linker-comparisons.tsv`. `backend.uf2` is generated alongside the
earlier routes. Default production builds and hardware are not changed.

### Initial backend quality

This is a correctness-first **experimental spill-only backend**, not yet a
size/performance replacement for LLVM. The current full-corpus flash payloads
are:

| Image / aggregate | Ordinary Clang | Owned IR + LLVM backend | Owned backend |
| --- | ---: | ---: | ---: |
| Screen test | 1,676 | 1,680 | 10,044 |
| Diagnostics | 15,772 | 16,204 | 108,404 |
| IMU demo | 5,720 | 5,808 | 54,536 |
| Wi-Fi host, including unchanged vendor payload | 250,216 | 250,644 | 506,204 |
| All 11 images | 293,520 | 294,528 | 842,684 |

The owned backend aggregate is **+549,164 bytes / +187.10%** against the
ordinary baseline. Every image still fits its existing linker regions.
The fixed firmware/NVRAM/CLM blobs total 225,920 bytes. Subtracting those
unchanged bytes, the remaining aggregate flash payload grows from 67,600
to 616,764 bytes (**+812.37%**); that remainder includes boot metadata,
constants and layout gaps as well as code.
The largest generated local-frame-plus-register-save allocation is
**1,752 bytes** in diagnostics `sample_sensors`; outgoing arguments,
callee chains and interrupts are additional. These are static sizes, not
stack high-water measurements. This cost is separate from C10's accepted
approximately 0.1% string-pooling difference. Improving register allocation,
small-offset accesses and instruction selection is a future C07 quality
milestone; C06 optimization alone will not eliminate spill traffic.
No runtime speed, timing-sensitive badge behavior or physical stack bounds
have been certified. Do not infer hardware readiness from image/link tests.

## Tenth replacement: C06 owned IR passes

[badge_opt.c](../src/badge_opt.c) is the thin CLI for the native C, standalone
textual IR pass driver in [ir_core.c](../src/ir_core.c).
It invokes no LLVM tools/libraries and has no LLVM fallback. Its job at this
milestone is **backend legalization plus modest constant simplification**,
not reproducing LLVM's optimizer:

```sh
make -C badge-compiler
badge-compiler/build/badge-opt input.frontend.ll -o output.ready.ll
badge-compiler/build/badge-opt --list-passes
# Debug a pass independently (not necessarily backend-ready afterward):
badge-compiler/build/badge-opt --passes=constant-fold input.frontend.ll -o folded.ll
```

### Modular pass architecture

[ir_pass.h](../src/ir_pass.h) defines the bounded module/token/line model, source
spans, output/replacement helpers, indexed names, diagnostics and the
`ir_pass` interface. [ir_core.c](../src/ir_core.c) implements parsing, serialization,
allocation and scheduling; its `ir_pipeline` API is linked directly by tests
without including its implementation or invoking the CLI.
[ir_passes.c](../src/ir_passes.c) contains the pass registry and a **separate,
explicit default pipeline**. Registering an experimental pass does not
enable it by default. Each pass declares an optimization or legalization
role; role metadata does not implicitly reorder or enable passes.
Implementations live in [ir_constant_fold.c](../src/ir_constant_fold.c),
[ir_aggregate_memory.c](../src/ir_aggregate_memory.c) and
[ir_wide_division.c](../src/ir_wide_division.c). M09's qualified default order is:

| Pass | Responsibility |
| --- | --- |
| `mem2reg` | Promote eligible scalar slots across CFG/PHIs, with verified internal DCE cleanup |
| `instruction-combine` | Apply the bounded neutral arithmetic/cast whitelist, with verified internal DCE cleanup |
| `constant-fold` | Fold unflagged fixed-width integer arithmetic, bitwise/shift operations, comparisons and casts; propagate known SSA literals within their function |
| `aggregate-memory` | Split wide `[N x i32/i64]` carrier loads/stores into scalar accesses, GEPs and extract/insertvalue; preserve byte offsets and valid alignment |
| `wide-division` | Lower general `i64` signed/unsigned division/remainder to module-private owned restoring-division IR; retain directly supported signed power-of-two division |

The driver reparses between passes, so later passes see earlier rewrites.
Each successful pass releases its parsing/rewrite scratch allocations after
serializing into a separate retained source buffer. Allocations made by the
caller before the pipeline are preserved; failure cleanup remains owned by
the context's `ir_destroy`. Pass/module pointers must not escape a pass.
The source and output buffers are separately bounded; the 128 MiB tracked
allocation limit is not multiplied by the number of passes.
Constant lookup uses a per-function hash index. Fresh-name collision checks
use a lazily built module index that normalizes quoted/unquoted and
hex-escaped names without changing generated name order.
Untouched source spans, globals, attributes and metadata are retained;
generated definitions use collision-checked names. Optional `--passes=...`
selects/order/repeats up to 16 passes for isolation. Unknown/empty selections
diagnose; no switch silently invokes LLVM.
`--default-passes` prints the exact active comma-separated default order.
The legacy route remains explicit:
`--passes=constant-fold,aggregate-memory,wide-division`. See
[M09's release decision and ablations](#c06-m09-qualified-default-release).
Individual M00-M08 measurements below are historical opt-in qualifications;
M09 supersedes their old default-policy statements, not their measurements.

To add an optimization, implement a new pass against the shared API, add
its source to `OPT_SOURCES`, and register it in `badge_ir_passes`. Test it
with `--passes=...` before deliberately adding its name at the required
place in `badge_ir_default_pipeline`; no driver/backend rewrite is required.
M02 adds reusable typed views and verification in
[ir_analysis.h](../src/ir_analysis.h)/[ir_analysis.c](../src/ir_analysis.c), described
[below](#c06-m02-structured-views-and-owned-verification). Later passes can
build these indexes without replacing the canonical token/line serializer.
Retain negative/side-effect tests and target differential execution for each
new pass. This is a supported-subset foundation, not an unrestricted LLVM
parser or whole-program-analysis framework.

The proposed next optimization release is tracked in
[plan-c06.md](plan-c06.md), with bounded milestones, dependencies, measured
benefit gates and release targets. Its pending milestones are not implemented
features.

The plan now makes RP2350 runtime speed an explicit goal and includes a
[separate target-specific follow-up tracker](plan-c06.md#rp2350-speed-follow-up-tracker):
frame-relative accesses, bounded register residency/allocation, native scalar
selection, safe ordinary-memory transfers and limited loop-invariant work.
These follow-ups are now implemented and qualified within their documented
scopes, with explicit-only compiler selections rather than generic
multi-target support or automatic default enablement.
Runtime gains need fixed-input owned-before/after hardware measurements;
the current LLVM-relative timings alone do not attribute gains to a C06 pass.

### C06 M02 structured views and owned verification

Implemented on 2026-10-08. This is an analysis/validation milestone:
**no new transformation or default pass is enabled**, and no code-size or
stack improvement is claimed.

[ir_analysis.h](../src/ir_analysis.h) exposes immutable, indexed views over the
existing source spans:

- Interned `void`, i1/i8/i16/i32/i64, opaque pointer, array and literal
  ordinary/packed struct types; return/parameter and instruction-result types.
- Functions, declarations, named blocks and logical instructions, including
  multiline switches with original start/end line numbers.
- Typed SSA/global/literal/block operands, parameter/instruction definitions,
  canonical quoted/escaped-name indexes and linked definition-to-use chains.
- CFG successor/predecessor slices retaining parallel-edge multiplicity,
  reachability/reverse-postorder and immediate dominators.
- Conservative effects for allocation, memory reads/writes/traps, volatile
  accesses, calls, control flow and opaque annotations. Calls/inline assembly
  remain effectful even when opaque attributes claim purity; metadata
  attachments never permit effect removal.

`ir_analyze(context, module, &view)` builds and validates a reusable view.
Its arrays and indexes belong to the context's scratch arena, refer to the
current text/tokens and must not outlive that module or survive a rewrite.
Rebuild them after changing IR. `ir_verify(context, module)` performs the
same checks, then releases its analysis scratch while retaining the caller's
module/allocations. Failure cleanup remains `ir_destroy`; no partial
"successful" analysis is returned.

The owned verifier checks supported instruction grammar and types, duplicate/
unresolved SSA/global/block names, global storage types and initializer symbol
references, integer widths/flags/casts, memory alignment, GEP/aggregate indices,
direct call signatures/argument counts and explicit indirect-call signatures.
Every block has exactly one final terminator; PHIs precede non-PHIs and match
the CFG's incoming **edges**, including equivalent values on parallel edges.
Ordinary SSA uses must follow their definition and be dominated by it; PHI
values are checked on their predecessor edges, including loop backedges.
Unreachable edges follow LLVM's dominance convention rather than inventing
reachability. Analysis users must treat `IR_NONE` reachability/idom entries
conservatively; `ir_dominates` does not prove cross-block dominance there.

The driver verifies before each selected/default pass and after the final
pass, so invalid input or a bad rewrite fails before output publication.
Verification is independent of pass registration/selection and invokes
neither LLVM tools nor libraries. To verify without transforming, preserving
every original byte:

```sh
badge-compiler/build/badge-opt --verify-only input.ll -o checked.ll
cmp input.ll checked.ll
make -C badge-compiler CC=/usr/bin/cc build/ir_analysis_test
cd badge-compiler
BADGE_TEST_MODE=owned ./build/ir_analysis_test
```

Atomic replacement and input/output alias checks also apply to `--verify-only`.
Globals, attributes, comments, whitespace and metadata are serialized from
their original spans, not regenerated from the views.

This intentionally verifies the canonical C05/C06 subset, **not arbitrary
LLVM IR or all LLVM attribute/metadata semantics**. Global initializer
expressions and top-level annotation bodies remain preserved opaque text,
apart from storage-type/name-reference checks; they are not a new constant
expression evaluator. ABI attributes are retained, not a proof of arbitrary
ABI contracts. Unsupported instructions/types/operand forms diagnose rather
than being analyzed as pure. Opaque pointers do not prove pointee types or
alias relationships; indirect target identity, memory safety, poison/undef
refinements and actual runtime behavior still require the other gates.
Valid potential-UB instructions (e.g. division by zero or out-of-range
shifts) are not rejected merely for being potentially poisonous/trapping.
Named/vector/floating types, exception/atomic instructions and general LLVM
constant expressions are outside this subset.

Resource bounds retain 16 MiB input/output, one million tokens and 128 MiB
tracked arena storage. Type nesting is bounded to 32 and literal structs to
256 fields. Switch comparisons, dominator convergence and SSA/PHI checks
have explicit 20M-work budgets; limit exhaustion is a diagnostic, never a
partial result. Graph traversal uses explicit stacks, not recursive CFG
walking. Analysis scratch is reclaimed per verification/pass. This adds
host verification work, not target instructions; it is not a performance
optimization or an LLVM-equivalent verifier.

[ir_analysis_test.c](../tests/ir_analysis_test.c) covers typed/indexed relationships,
effects, diamonds/loops/backedges, parallel/unreachable edges, normalized
names, arrays/structs/packed globals, indirect/variadic calls, inline assembly,
opaque annotations, malformed types/references/terminators/PHIs, byte
mutations, resource failures and atomic verify-only output preservation.
The independent pipeline registry test also injects a bad rewrite and
requires the final verifier to reject it. Positive analysis fixtures are
checked by LLVM in oracle mode; sanitizer/owned-only tests need no LLVM.

Final qualification:

- All **72 C05/C06 corpus boundary files** verified and round-tripped
  byte-identically under ASan/UBSan.
- Complete **36-C-module/11-image LLVM-free build** passed; all **179**
  captured frontend/C06 IR, assembly, object, ELF/bin/UF2 artifacts matched
  their immediate pre-M02 SHA-256 baseline. This is a current unchanged-input
  comparison, not reuse of the historical M01 application baseline.
- Full owned-only suite passed with LLVM absent from `PATH`; full oracle
  suite passed LLVM IR checks, Cortex-M33 execution/mixed ABI and independent
  assembly/object/link comparisons. All 11 metrics tests passed.
- The [final two-badge differential run](../../build/compiler-compare-runs/run.8n2ZjJ/results.json)
  matched all six arithmetic/string/memory/control/ABI/graphics modules with
  a fresh seed; both badges automatically recovered to BOOTSEL.

M02 is complete and enables no transformations by itself. The subsequently
completed [M03 DCE](#c06-m03-worklist-dead-instruction-elimination),
[M04 scalar-promotion](#c06-m04-trivial-scalar-promotion),
[M05 mem2reg](#c06-m05-pruned-scalar-mem2reg),
[M06 SCCP](#c06-m06-sparse-conditional-constant-propagation),
[M07 instruction combining](#c06-m07-bounded-instruction-combining) and
[M08 local CSE](#c06-m08-basic-block-common-subexpression-elimination) passes consume these
indexes. M09 enables mem2reg and instruction combining; standalone DCE,
scalar promotion, SCCP and CSE remain explicit-only.

Constant folding uses unsigned fixed-width arithmetic and explicit signed
magnitudes/sign extension; it avoids host signed overflow. Overflow/exact
flags are not discarded by the folding pass, and undefined division/shift
cases are not folded. Constant definitions remain typed `freeze` instructions
so forward/backedge references stay valid. Volatile accesses and calls are
not eliminated or reordered. The default now includes mem2reg and bounded
instruction combining with their internal DCE cleanup. It does not enable
SCCP, CSE, global elimination, inlining, loop optimization or register
allocation. Standalone post-folding DCE remains explicit-only.

The division pass emits a fixed 64-iteration unsigned algorithm with carry
handling, plus signed magnitude/sign restoration. Remainder uses
`numerator - quotient * denominator`. No libc/AEABI runtime is added to
owned firmware. Defined negative rounding, unsigned denominators above
2^63 and signed minimum values are covered; zero divisors trap, and signed
minimum divided by -1 remains outside defined LLVM/C behavior.
Exact-division annotations may be dropped during legalization, preserving
all defined results. C07 now supports constant 64-bit shifts needed by the
helper and reading constant/zero carrier components.

### Contract and validation

Input is **canonical C05 textual integer/pointer IR**, with the documented
target/data layout and one-line instructions/function headers. This tool
is not an independent full LLVM IR verifier; arbitrary named/vector/floating
IR is not a supported complete backend path. Transformed aggregate memory
requires 1..256 i32/i64 carriers, simple SSA/global/null addresses and
ordinary alignment suffixes. Wide volatile aggregate SSA accesses and
unsupported aggregate shapes/metadata are diagnosed instead of silently
changing access semantics. General division accepts scalar SSA/integer
operands. C07's other explicit subset limits remain.

Bounds are 16 MiB input/output, one million tokens, 128 MiB tracked arena
allocations and 16 explicitly selected passes. Atomic output replacement
and input/hard-link/symlink alias rejection follow the other owned tools.
CLI tests cover oversized/NUL input, invalid pass selection and output
preservation.

[badge_opt_test.c](../tests/badge_opt_test.c) runs **27 LLVM-verified/backend-assembled
positive, 12 negative and 186 mutation fixtures** under ASan/UBSan.
[ir_pipeline_test.c](../tests/ir_pipeline_test.c) links the core against an independent
test registry: an extra registered pass stays disabled by default, explicit
selection enables it, roles/order are checked, 16 repeated 16 MiB scratch
allocations are reclaimed, caller allocations survive, and hash collisions,
escaped names and shared target-legality boundaries are exercised.
All 36 production modules additionally reproduce release IR with
`build/badge-opt-sanitize`. The C06 execution mode of
[codegen_oracle.sh](../tests/codegen_oracle.sh) uses our passes, not LLVM optimization,
then executes semantic fixtures and bidirectional mixed Clang-source/owned
ABI calls on Cortex-M33. [opt_oracle_fixture.c](../tests/fixtures/opt_oracle_fixture.c) checks
19 signed/unsigned division/remainder boundary vectors and record carriers
on our backend and LLVM's backend of the same legalized IR; native Clang
execution independently validates the expected arithmetic vectors.
Fixed-width shift boundary regressions remain in the C07 runtime fixture.

### LLVM-free source-to-UF2 build

```sh
# All 10 ordinary images plus vendor-blob Wi-Fi firmware:
bash badge-compiler/scripts/build-owned.sh
# Only diagnostics:
bash badge-compiler/scripts/build-owned.sh diagnostics
# Serial compilation for comparisons:
BADGE_BUILD_JOBS=1 bash badge-compiler/scripts/build-owned.sh
```

[build-owned.sh](../scripts/build-owned.sh) reuses the root Makefile without changing its
defaults, builds into ignored `build/owned/` and records targets, logs and
source/tool hashes. [owned-cc.sh](../scripts/owned-cc.sh) accepts only the root recipes'
known target flags, dispatches C through C01-C12, and uses
`badge-cpp --assembly` for `.S`. That mode defines `__ASSEMBLER__` and
preserves token adjacency so `.section`, `.text.name`, `cortex-m33` and
expanded asset paths retain assembly spelling. It uses the existing
macro/include/conditional engine, not Clang.

Owned builds default to four parallel jobs. `BADGE_BUILD_JOBS` accepts an
integer from 1 to 16 and controls both host-tool and firmware Make invocations;
`build-jobs.txt` records the selection. Parallelism is only across independent
build jobs: all compilation stages and saved intermediates remain unchanged.
The [C06 benchmark](../tools/benchmark-c06.mjs) explicitly uses one job regardless of
the environment so its serial walltime comparisons remain comparable.

[badge-sha256](../src/badge_sha256.c) writes the existing SHA-256 manifest format
without starting a scripting runtime for each source file. Owned builds and
oracle captures share it; file/read/write failures are fatal. Root Makefile
build-ID and vendor-input verification recipes continue to use `shasum`.

C06 scratch allocations use zero-initialized, `max_align_t`-aligned chunks
with the existing nested allocation marks. The 128 MiB limit still counts
live requested payload bytes, not chunk slack or allocator headers; releasing
a mark preserves older allocations and reclaims newer storage. Analysis
indexes store integer IDs directly instead of formatting/parsing decimal
strings. Short formatted strings also avoid a second formatting pass. These
are host-side implementation changes: all canonical textual IR boundaries,
default passes and mandatory verification calls remain unchanged.

C08 caches mnemonic/directive decoding and parsed expression syntax across
relaxation passes. It does not cache layout-dependent values: forward labels,
aliases, the current location (`.`), `.equ` definition locations and fixups
are reevaluated as before. Symbol creation retains first-use ordering; IT
handling, branch widening and the assembly/object boundary are unchanged.
Cache storage is charged to the existing 192 MiB assembler memory limit.

On 2026-10-08, the 36-C-unit/two-assembly-input/11-image corpus was measured
on an Apple M4 Max with warm filesystem caches and normal background load.
All 228 saved intermediates and ELF/map/bin/UF2 artifacts stayed byte-identical
to the pre-speedup snapshot. The complete sanitizer suite, LLVM differential
fixtures and Cortex-M33/QEMU semantic and mixed-ABI execution passed.

| Walltime measurement | Before | After |
| --- | ---: | ---: |
| Serial C-to-UF2 Make build, host tools prebuilt | 3.716 s | 3.413 s |
| Serial forced Make build, packaging helpers rebuilt | 4.141 s | 3.828 s |
| Four-job forced Make build, packaging helpers rebuilt | 1.418 s | 1.126 s |
| C08/C09 stage replay, whole corpus | 0.598 s | 0.353 s |
| C06 stage replay, whole corpus | 0.468 s | 0.402 s |

Build figures are medians of three measured runs after an excluded warm-up;
the serial/four-job experiment interleaves job counts. Stage figures are
medians of five aggregate warm replays, include process startup and file I/O,
and are not an additive decomposition of Make walltime. The new default
`build-owned.sh` invocation measured 1.099 s median. These are host-specific
observations, not fixed performance guarantees. The serial
[C06 before/after snapshot](../build/c06-metrics/walltime-speedups-20261008/report.json)
retains configuration, output hashes and resource measurements.

[owned-pipeline.sh](../scripts/owned-pipeline.sh) supplies the owned stage execution
helpers shared by this adapter and [capture-cc.sh](../scripts/capture-cc.sh). The oracle
still generates its LLVM reference artifacts independently; only the owned
side reuses these helpers. [owned_pipeline_test.sh](../tests/owned_pipeline_test.sh)
checks stage order, quoted paths/flags and immediate error propagation.
[target.h](../src/target.h) shares the exact IR target/layout declarations between
C05/C06/C07 and the direct i64 signed-division legality predicate between
C06 and C07. It is a narrow fixed-target contract, not a new target framework.

[badge-elf-info](../src/badge_elf_info.c) supplies the root recipes' executable
undefined-symbol/allocated-size checks without llvm-nm/size; malformed ELF
is an explicit failure. Its sanitizer suite covers 253 truncations and
invalid symbol/string tables. Link failures/unresolved symbols fail the
wrapper rather than succeeding with an empty inspection result.
The Wi-Fi option selects the UF2 recipe, not the unrelated libusb host
collector target.

A complete rebuild passes with `PATH=/usr/bin:/bin:/usr/sbin:/sbin` and
system `cc` as the **host** compiler. No LLVM target/oracle executable is
needed to turn sources into these images. Bootstrap still needs a native
C compiler, host C/POSIX libraries, Bash, Make and standard utilities;
vendor radio firmware remains external data, not source-owned software.
This is not a self-hosting compiler or a dependency-free operating environment.

The oracle separately saves `owned.optimized.ll`, `owned.s`, `owned.o` and
`owned.uf2`. All **36 IR modules verify**, all **38 objects** match independent
LLVM assembly for allocated bytes/fixups/public symbols, and all **11 images
and public-symbol sets** match independent MC/unmerged LLD.
`opt-comparisons.tsv` and `opt-linker-comparisons.tsv` record those checks;
all **187 ELF extractions** match LLVM. Owned assembly preprocessing preserves
the unchanged vendor blobs and Picobin metadata.

### Fully owned quality and hardware boundary

| Image / aggregate | Ordinary LLVM | Owned C06-C07 |
| --- | ---: | ---: |
| Screen test | 1,676 | 20,668 |
| Diagnostics | 15,772 | 506,316 |
| IMU demo | 5,720 | 163,676 |
| Wi-Fi host, including vendor payload | 250,216 | 1,141,616 |
| All 11 images | 293,520 | 2,158,480 |

The aggregate increase is **+1,864,960 bytes / +635.38%**. All images fit
the existing linker flash/static-RAM regions, but that is not stack or
timing certification. LLVM C06 formerly removed substantial dead code/data
and promoted memory; this minimal C06 does not.

The largest local frame plus saves is **5,312 bytes** in
`live_usb_control_reset`. The existing stack starts at the top of the
**4 KiB SCRATCH_Y** region; this single frame already crosses that region
into adjacent SCRATCH_X, before nested calls/interrupts. Stack high-water
marks, interaction with adjacent scratch/mailbox memory and timing-sensitive
peripherals are unverified. Do not flash these experimental images as a
hardware-ready replacement. Improving memory promotion/dead-code passes and
C07 register allocation/frame quality, then explicitly authorized hardware
tests, are the next milestones. No hardware was flashed or production default
changed by this implementation.

### Physical screen-test smoke test

On 2026-10-07, with explicit user authorization, `screen_test.uf2` was
freshly rebuilt through the fully owned toolchain with LLVM target tools
absent from `PATH` and copied to the attached RP2350 BOOTSEL volume. The
20,668-byte flash image matched the oracle-verified owned binary; its
vectors and all 81 UF2 blocks were validated before upload. A conservative
direct-call stack bound, including outgoing call allocations, was 1,512 bytes
within the initial 4 KiB stack region; this is not a measured high-water mark.

The BOOTSEL volume disconnected after upload, and the user confirmed the
complete expected display: the white border, "GITHUB UNIVERSE", "2026",
"DISPLAY OK", "320 X 240", and green/blue/purple/orange bars. This verifies
boot/startup and this display demo on the physical badge, not other images,
Wi-Fi, worst-case stack usage or peripheral timing margins. Production
toolchain defaults remain unchanged.

### Physical Wi-Fi/UDP smoke test

The user authorized paired trials, cooperative application repairs and
isolated owned assembly primitives. Both compiler paths retain the unchanged
vendor CYW43439 firmware/NVRAM/CLM. The qualified physical roles are AP
`9BBF88DD17D66E7D` and station `08DA26F86E57F5EA`.

The optional [test build overlay](../scripts/wifi-smoke.mk) and
[isolated linker script](../scripts/memmap_wifi_smoke.ld) reserve 64 KiB of main SRAM
at `0x2006f000..0x2007f000`, below the unchanged report mailbox. Ordinary
data/BSS/noinit are confined to the preceding 444 KiB. Production linker
scripts, stack policy and compiler defaults are unchanged. This test does
not prove a complete stack bound for indirect calls.

```sh
make -C badge-compiler all CC=/usr/bin/cc
make -B -j1 -f Makefile -f badge-compiler/scripts/wifi-smoke.mk \
  BUILD_DIR=badge-compiler/build/owned HOST_CC=/usr/bin/cc owned-wifi-smoke
```

The overlay explicitly selects owned C01-C12 tools, reuses the root
Makefile's exact source/asset dependency set, and creates
`badge-compiler/build/wifi-smoke/usb_wifi_udp.uf2` plus a matching collector
at `badge-compiler/build/owned/host-tools/usb_wifi_udp_host`.
Host `pkg-config`/libusb are required for collection. This command builds
only; it does not upload anything. Do not run it concurrently with the
default owned build: they reuse object paths with different flags.
The overlay enables project-owned byte/FIFO/display primitives and an
explicitly authorized 40M-tick application lifetime. Ordinary builds keep
portable C primitives, their 4 KiB stack and 30M-tick UDP lifetime.

For an explicitly authorized upload of an already-built image:

```sh
bash src/host/tools/usb_wifi_pair_run.sh --prebuilt \
  9BBF88DD17D66E7D 08DA26F86E57F5EA \
  badge-compiler/build/wifi-smoke/usb_wifi_udp.uf2 \
  badge-compiler/build/owned/host-tools/usb_wifi_udp_host
```

The runner verifies both RP2350 identities, records image/collector SHA-256
and roles, flash-verifies both before rebooting either, and checks bounded
BOOTSEL recovery. The collector verifies both live identities before
submitting both roles without waiting for AP initialization.

**Localized repairs:** long unserviced CRC/display work and owned-code
overhead prevented timely startup/progress. Asset validation now services
USB/watchdog between 256-byte CRC chunks; initialization/rendering is
cooperative and bounded, with changed-tile drawing and paced publication.
Native primitives reduce isolated FIFO/setup/display and byte-work overhead.
Attempts at 512-/2,040-byte backplane bursts failed physical readback and
were reverted: transfers remain 64 bytes, and all 224,936 firmware/NVRAM
bytes must read back correctly before releasing the radio CPU.

A flat USB polling throttle introduced during pacing caused repeated LLVM
enumeration resets. Polling is now immediate while unconfigured or any EP0
transfer is active; only configured, idle polling is paced. Host assertions
cover enumeration and every EP0 phase. Speculative reset/debounce changes
were reverted.

The final pre-refresh owned image `fa951e3f` completed 100 attempts per
direction twice, but lost one station request during AP ARP-cache expiry
([first](../../build/usb-live-runs/pair.FvCG8MBK),
[repeat](../../build/usb-live-runs/pair.JwXaIzfs)). Proactive refresh now queries
the validated neighbor during the final 200K ticks of its existing 2M-tick
TTL, retaining the still-valid cache and respecting pending UDP responses.
Sending a query does **not** renew the TTL: only validated ARP does.
Ignored refreshes still invalidate the cache at exact expiry and retain
the original bounded discovery behavior. Host tests exercise these rules,
invalid-source replies and timer wrap.

| Final build / evidence | Result |
| --- | --- |
| Owned `475fbeeb`, [first](../../build/usb-live-runs/pair.Vsc1vSJM) and [repeat](../../build/usb-live-runs/pair.p1fOQEl0) | Both WPA2 authorized; each sent/received/echoed 100. **200/200 echoes in each run**, zero losses, duplicates, rejected frames or radio drops; complete frozen sample pages and BOOTSEL recovery verified. |
| Normal LLVM `b53793f3`, [repeat control](../../build/usb-live-runs/pair.5I1ZlWtU) | 200/200 echoes, all echo counters 100, zero losses/duplicates/rejections/radio drops; recovery verified. |
| Same LLVM image, [first control](../../build/usb-live-runs/pair.CBK9RG4A) | Both received 100 but frozen AP echo count was 99. Strict cross-peer validation rejected the dataset; the cause of this intermittent counter inconsistency remains unlocalized. |
| Reversed physical roles, owned and [LLVM control](../../build/usb-live-runs/pair.r4NAeH2a) | Firmware validated, but station `9BBF88DD17D66E7D` timed out joining. Not qualified; this is not an owned-compiler-specific failure. |

Owned first-run mean/p95/max RTTs were **87.907/159.500/169.058 ms** (AP)
and **86.768/166.325/192.227 ms** (station); repeat means were 86.250/84.686 ms.
The successful LLVM control means were 3.947/3.705 ms. These are end-to-end
short-run measurements, not an isolated compiler speed comparison.
The final owned smoke payload is 1,154,328 bytes / 4,510 UF2 blocks,
SHA-256 `f676af2cdbcd356216ccd3a7d3138744517ed07991d0a202557e4aaeadf5508b`.
Independent final checks validated every UF2 block, family/order/padding,
vectors, RAM/stack/mailbox separation, resolved symbols and exact vendor
bytes for both smoke and default owned UDP images.

All limits use inherited timer ticks (measured approximately 0.477 ticks/µs,
not assumed 1 MHz). Asset/startup/control budgets are 6M/20M/3M; startup
20M and smoke lifetime 40M were explicitly authorized. Join/run/response
deadlines remain 8M/15M/100K, ARP TTL remains 2M, and the recovery watchdog
remains eight seconds. CRCs, wire validation and 100 samples are unchanged.
Successful collection alone is not proof of zero loss: inspect the counts.

Coupled sanitizer tests, owned/oracle compiler suites, native encoding and
Cortex-M33 mixed-caller fixtures passed; the final full 36-module/11-image
LLVM-free default build also passed. Reproduce application tests with:

```sh
make -j1 HOST_CC=/usr/bin/cc test-wifi-udp test-wifi-firmware \
  test-live-usb test-wifi-link test-wifi-pair-run test-diagnostics test-usb-report
```

This qualifies the isolated demo in the stated roles, not the production
4 KiB stack, indirect-call/interrupt stack bounds or all radio conditions.
Physical final-error display and pre-main/stalled-clock recovery are not
generally certified; serviced LCD waits rely on cancellation/watchdog and
lack a dedicated stalled-clock fixture. Setup MMIO fixtures check final
registers/guards, not the complete temporal bus trace. Both badges were
last recovered to BOOTSEL after the LLVM control. No new C06 optimization
was enabled: M01 stays complete and M02+ pending. Application changes mean
historical 182-artifact/M01 snapshots are retained evidence, not comparable
unchanged-input optimization baselines.

## C06 M01 reproducible measurements

[benchmark-c06.mjs](../tools/benchmark-c06.mjs) produces a schema-1 JSON snapshot and
TSV tables for all **36 C modules and 11 linked images**. It uses only Node's
standard library and our native tools, not LLVM target tools. Node is a
host-side reporting dependency; ordinary owned firmware builds and
`test-owned` do not require it. No benchmark accesses USB or flashes firmware.

```sh
CC=/usr/bin/cc HOST_CC=/usr/bin/cc \
  node badge-compiler/tools/benchmark-c06.mjs --label=baseline --runs=3
CC=/usr/bin/cc HOST_CC=/usr/bin/cc \
  node badge-compiler/tools/benchmark-c06.mjs --label=candidate --runs=3 \
    --passes=constant-fold,aggregate-memory,wide-division \
    --compare=badge-compiler/build/c06-metrics/baseline
make -C badge-compiler test-metrics CC=/usr/bin/cc
```

Labels are exclusive: a completed snapshot is never overwritten. Use a new
label for each candidate. `--runs` accepts 1-9; the default is three. Passes
are optional and preserve the existing default when omitted. Explicit
selection must include any legalizations required by the corpus. Direct
owned builds also accept `BADGE_OPT_PASSES=name,...`; an explicitly empty or
invalid selection fails rather than silently selecting defaults.

Each run forcibly rebuilds the host compiler tools **before** timing, then
performs a forced serial source-to-UF2 rebuild in the fixed `build/owned`
directory. `CC` and `HOST_CC` must name executable host compilers. Stable paths
matter because the Wi-Fi build ID includes generated asset filenames.
Artifact hashes must agree across repetitions and remain unchanged during
collection. Comparison rejects changed sources, preprocessed/C05 inputs,
target flags, root Makefile, build ID, assets, image sets or linker scripts.
Optimizer/tool hashes are recorded, but may change between candidates.
Host timing deltas are omitted when host/compiler/Node/Make metadata differs.

The snapshot contains:

- `report.json`: provenance, actual ordered passes, per-function IR opcode
  histograms, before/after module totals, pass candidate/outcome/reason counts,
  assembly frame/call models, linked ELF sizes/stack paths, build resources,
  and optional cross-snapshot deltas.
- `functions.tsv`: per-function before/after instruction, alloca, load/store,
  PHI, branch and call counts. These include unlinked functions; they are
  opportunity diagnostics, not reached-code savings.
- `images.tsv`: linked flash/vendor/nonvendor bytes, largest local frame plus
  saves, entry direct-call bound and unknown-function count.
- `modules/` and `images/`: native IR and ELF metadata reports; build/resource
  logs and `SUCCESS` mark a complete snapshot. Failed staging directories
  retain `FAILED` and diagnostic logs and are not valid baselines.

[badge-ir-metrics](../tools/badge_ir_metrics.c) measures actual C06 passes using an
optional [observation API](../src/ir_pass.h), disabled in ordinary compilation:

```sh
badge-compiler/build/badge-ir-metrics \
  --check=output.ready.ll input.frontend.ll -o metrics.json
badge-compiler/build/badge-elf-info --metrics image.elf
```

`--check` requires the measured pipeline to reproduce the compiled IR
byte-for-byte. Observation counters distinguish candidates, transformed and
skipped cases, propagated operands, generated accesses/helpers, and pass
start/completion. A normalized definition is not necessarily a removed
instruction or a flash saving. Report failures preserve existing output.
Metrics still count the canonical token/line IR model, not M02's analysis
indexes. Their linked pipeline now benefits from the same mandatory M02
boundary verification; no metric schema or optimization policy changed.

### Methods and limits

Wall time uses a monotonic clock around the build command. `/usr/bin/time`
provides user/system CPU and OS maximum RSS (`-l` on macOS, `-v` on Linux;
reported units are converted to bytes). Report collection and host compiler
bootstrap are excluded; normal target-build packaging/Make overhead is
included. Builds are forced, **not cold-cache**: shared OS caches are never
purged. Load on this non-isolated host affects timings. RSS is the OS's
reported maximum, not simultaneous process-tree memory or C06 arena usage.
These are host build costs, not badge cycles, latency or runtime speed.

Stack models use verified C07 prologue/epilogue annotations and active outgoing
allocations **at each call**, not their sum over a function. Only linked ELF
functions are included; map address-range containment and local/global symbol
bindings qualify same-named static functions by object. Exact C07
branch-around literal pools and unreachable traps are recognized. Indirect,
unresolved, recursive, missing-model and unsupported stack/control paths yield
`null` complete bounds and explicit reasons, never zero-cost edges.
`known_prefix_bytes` and recognized outgoing allocations on unknown paths are
diagnostics, not upper bounds. Bounds exclude interrupts/fault handlers and
are conservative direct-call estimates, not observed stack high-water marks.
The initial stack region is read from each actual linker script and checked
against ELF `__StackTop`. Embedded vendor bytes and every UF2 block/padding
are independently checked against the generated binaries.

### Qualified M01 baseline

The saved [baseline](../build/c06-metrics/m01-baseline/report.json) and
[equivalent explicit-pipeline comparison](../build/c06-metrics/m01-equivalent/report.json)
each passed three deterministic full builds with system host compilers and
LLVM target tools absent from `PATH`.

| Metric | Result |
| --- | ---: |
| Aggregate flash / unchanged vendor / nonvendor bytes | 2,158,480 / 225,920 / 1,932,560 |
| Before / after IR instructions, all modules | 42,122 / 42,198 |
| Residual allocas / loads / stores after C06 | 2,850 / 8,891 / 5,499 |
| Largest linked local frame plus saves | 5,312 (`live_usb_control_reset`) |
| Screen / diagnostics / IMU entry direct-call bounds | 1,488 / 4,280 / 4,144 bytes |
| Wi-Fi complete entry bound | Unknown; indirect paths remain |
| Baseline median target build, three runs | 4.664 seconds |
| Baseline maximum OS-reported RSS | 69,976,064 bytes |

The 1,488-byte screen bound refines the earlier conservative 1,512-byte sum;
the firmware did not change. Diagnostics and IMU estimates exceed the initial
4,096-byte region: stack safety cannot be established from these bounds.
Wi-Fi already exceeds it in a single frame. Branch-insensitive estimates
above a region are warnings, not proof of an observed overflow.

All **182 pre-M01 artifacts** remained byte-identical. Equivalent default and
explicit pipelines have zero IR, flash and known stack/frame deltas; unknown
stack deltas remain `null`. The 11 focused metrics fixtures, complete owned
and LLVM/QEMU oracle suites, and sanitized reporting of all 36 IR modules and
11 ELF images passed. No new optimization was enabled and no badge was
flashed. At M01 completion, M02 and later milestones remained pending;
subsequent M02-M05 qualification is recorded separately.

## C06 M03 worklist dead-instruction elimination

[ir_dead_instructions.c](../src/ir_dead_instructions.c) implements the registered
optimization pass `dead-instructions`. **It is not enabled by default.**
The existing three-pass default and its generated artifacts are unchanged;
M09 remains the separate default-pipeline qualification milestone.

The pass analyzes the current module with M02, copies definition use counts,
and seeds a worklist with zero-use eligible instruction definitions.
Deleting an instruction decrements each local operand use independently,
including repeated operands and PHI incoming edges. A producer is queued
only when its count transitions to zero, so each instruction is processed
at most once. Original instruction line spans are deleted without renaming
SSA values or rewriting CFG edges; untouched bytes retain their original
formatting. Work/storage are linear in indexed instructions, definitions
and operands, within the existing input/arena limits. M02 verification still
runs before each pass and after the final rewrite.

Eligibility requires both an explicit opcode whitelist and **zero effects**:
nontrapping integer arithmetic/bitwise/shifts, integer comparisons, casts,
`freeze`, `select`, GEP, aggregate extract/insert and PHI. Unused poison/undef
values and poison-producing `nsw`/`nuw`/`exact`/`inbounds` operations need not
be evaluated; this does not authorize replacement of live values or unsafe
algebraic simplification. Division/remainder stay conservatively trapping.
All allocations, loads/stores (including ordinary nonvolatile accesses),
calls (even attributed pure calls), inline assembly, terminators and opaque
annotations/metadata remain. Live operands of these roots retain their
producer trees. Self-referential and mutually dependent dead SSA cycles
remain: zero-use worklist DCE is not SCC deletion, CFG cleanup or mem2reg.

Metrics report `candidate/pure_nontrapping_definition`,
`transformed/unused_definition_removed`,
`skipped/effectful_or_not_whitelisted` and
`skipped/still_referenced_or_cyclic`. The last category does not claim to
distinguish externally live definitions from cycles.

### Selecting and reproducing M03

Placing cleanup last also removes dead values introduced by mandatory
legalization. Earlier/repeated selections remain available for experiments.
Keep both legalizations for executable corpus tests:

```sh
badge-compiler/build/badge-opt \
  --passes=constant-fold,aggregate-memory,wide-division,dead-instructions \
  frontend.ll -o optimized.ll

BADGE_OPT_PASSES=constant-fold,aggregate-memory,wide-division,dead-instructions \
  bash badge-compiler/scripts/build-owned.sh

# Use fresh labels; snapshots are exclusive and existing reports are not overwritten.
node badge-compiler/tools/benchmark-c06.mjs --label=my-m03-baseline --runs=3
node badge-compiler/tools/benchmark-c06.mjs --label=my-m03-dce --runs=3 \
  --passes=constant-fold,aggregate-memory,wide-division,dead-instructions \
  --compare=badge-compiler/build/c06-metrics/my-m03-baseline

# Same selected optimizer in existing independent LLVM/Cortex-M33 ABI fixtures.
cd badge-compiler
BADGE_OPT_PASSES=constant-fold,aggregate-memory,wide-division,dead-instructions \
  IR_OPTIMIZER=owned bash tests/codegen_oracle.sh
```

The oracle command requires Clang, opt, LLD and QEMU in `PATH` or configured
through `LLVM_CLANG`, `LLVM_OPT`, `LLVM_LLD` and `QEMU_ARM`. The normal owned
pipeline requires none of them.

### Qualified measurements and remaining limits

The [current-input baseline](../build/c06-metrics/m03-baseline-qualified/report.json)
and [DCE candidate/comparison](../build/c06-metrics/m03-dce-qualified/report.json)
each contain three deterministic builds of all 36 modules and 11 images.
Their C05 inputs, application/build ID, target flags, linker scripts and
vendor assets match; provenance includes the analysis and DCE sources.
These are not comparisons to the older, incompatible M01 application.

- C06 instructions: **44,538 -> 43,531**, removing **1,007** definitions.
- Aggregate nonvendor linked payload: **1,995,340 -> 1,970,092 bytes**,
  saving **25,248 bytes (1.265%)**. Total payload including unchanged
  225,920 vendor bytes: **2,221,260 -> 2,196,012 bytes**.
- All 11 images are smaller. Alloca/load/store/call/branch/PHI and volatile
  access totals are unchanged; this milestone does not promote memory.
- Maximum local-frame-plus-saves decreases in ten images and is unchanged
  for `tilt_breakout`; none regress.

| Image | Linked payload before -> after | Maximum local frame before -> after |
| --- | --- | --- |
| `hello_idle` | 1,364 -> 1,276 | 136 -> 120 |
| `screen_test` | 22,628 -> 21,700 | 536 -> 464 |
| `imu_demo` | 165,636 -> 163,400 | 2,248 -> 2,136 |
| `diagnostics` | 509,168 -> 503,484 | 1,360 -> 1,264 |
| `tilt_breakout` | 88,880 -> 87,180 | 1,400 -> 1,400 |
| `usb_wifi_udp` | 1,185,864 -> 1,176,668 | 5,312 -> 5,136 |

Values are bytes. Full per-image/function data are in the snapshots.
The known `hello_idle` direct entry-call bound decreases **168 -> 144**;
the other ten entry bounds remain `null` with unresolved-path reasons.
The largest frame **still exceeds 4,096 bytes**. Smaller static frames are
not production stack-safety, interrupt/high-water or Wi-Fi certification.
M09's larger aggregate-size/frame release targets have not been achieved.

Measured forced-build median wall time is **4.679 -> 5.213 seconds**
(about +11.4%); maximum OS-reported RSS is
**72,450,048 -> 70,270,976 bytes**. These are host samples using M01's
methodology, not runtime speed measurements or guaranteed cost improvements.

Qualification includes:

- [ASan/UBSan fixtures](../tests/ir_dead_instructions_test.c): folded freezes,
  shared trees/repeated uses, integer widths, poison/flags, pointer/aggregate
  values, PHI edges/live uses/cycles, opaque/effectful roots, unreachable
  instructions, cross-function/quoted names, empty modules, invalid-IR
  rejection, repeated-pass idempotence and a **10,000-definition cascade**.
  Positive fixtures independently pass LLVM verification.
- All 36 candidate modules reproduce release C06 output under sanitizers,
  independently pass LLVM verification and are byte-identical on a second
  DCE run. All 72 frontend/optimized boundaries verify and round-trip.
- Complete owned-only, independent LLVM/oracle and 11 metrics tests pass.
  DCE-selected Cortex-M33 fixtures pass arithmetic, wide division and
  bidirectional mixed/all-owned ABI execution.
- The [physical LLVM-versus-DCE run](../../build/compiler-compare-runs/run.FbFeOd/results.json)
  matches all six arithmetic/string/memory/control/ABI/graphics modules
  (77,320 work items), with both selected badges automatically recovered to
  BOOTSEL. Existing fingerprint, operation-coverage and isolated 64 KiB-stack
  limitations still apply; this is not a repeated Wi-Fi RF qualification.
- Rebuilding the unchanged default pipeline preserves **all 179**
  immediate pre-M02/default artifact SHA-256 hashes.

M03 is complete **as an opt-in pass**; subsequent M04 qualification follows.

## C06 M04 trivial scalar promotion

[ir_scalar_promotion.c](../src/ir_scalar_promotion.c) implements the registered
optimization pass `scalar-promotion`. It remains **opt-in**, alongside M03;
the existing three-pass default is unchanged. This is bounded promotion,
not the general mem2reg/PHI construction subsequently implemented in M05.

M02 now records `ir_instruction.allocated_type` (`IR_NONE` for non-allocas).
The pass analyzes once and walks each slot's indexed uses. Eligibility
requires:

- A once-executed entry-block alloca with fixed, positive storage count
  (omitted count means one), of type `i1/i8/i16/i32/i64` or `ptr`.
- Every pointer use is the direct address operand of an ordinary load or
  store, with exactly the allocated type. No pointer escape or alias proof
  is assumed, even for a zero-offset GEP.
- No volatile or opaque/metadata-bearing allocation/access.
- Either all accesses are in one reachable block, or there is exactly one
  store in entry that precedes every load. Entry dominates all reachable
  blocks; instruction order is checked for loads within entry.

For the single-block case, stores update a local current-value reference and
loads forward that value. A read before any store becomes `undef` only in
entry, or when the slot is never stored at all. If a nonentry block reads
before its first store, the slot remains unchanged: a loop revisit might
observe the preceding iteration's value. The pass does not invent zero,
insert a freeze, or speculate about initialization. Unreachable accesses
are conservatively left for later CFG work.

Eligible allocations and their stores/loads are removed. Load result uses
are replaced through typed operand indexes, including PHI edge values and
call operands. Iterative path-compressed substitution resolves chains of
promoted loads across slots without recursive traversal, textual name
matching or quadratic rescans. Quoted names and cross-function SSA names
use M02's existing resolution. SSA/control-flow structure is otherwise
unchanged; mandatory before/after verification still applies.

This is one eligibility sweep. Promoting a pointer-valued slot can expose
direct accesses to a slot initially classified as escaping; a subsequent
explicit promotion pass may then qualify it. That is not implicit alias
analysis or an unbounded fixed-point loop. No additional pass is silently
selected. Storage/traversal are bounded by the indexed definitions/uses and
the existing arena/input limits, in addition to M02's analysis budgets.

Observer metrics record candidate slots, eligibility (`single_block` or
`single_entry_store`), promoted slots, removed loads/stores, substitutions
and residual reasons. Skips distinguish unsupported storage/counts, nonentry
allocations, escapes, volatile/opaque accesses, type mismatches, unreachable
accesses, ambiguous initialization and required CFG promotion.

### Selecting and reproducing M04

Put promotion before folding to expose scalar constants; preserve both
legalizations and finish with M03 cleanup:

```sh
BADGE_OPT_PASSES=scalar-promotion,constant-fold,aggregate-memory,wide-division,dead-instructions \
  bash badge-compiler/scripts/build-owned.sh

# Fresh, exclusive labels. The baseline already includes M03, isolating M04.
node badge-compiler/tools/benchmark-c06.mjs --label=my-m04-baseline --runs=3 \
  --passes=constant-fold,aggregate-memory,wide-division,dead-instructions
node badge-compiler/tools/benchmark-c06.mjs --label=my-m04-candidate --runs=3 \
  --passes=scalar-promotion,constant-fold,aggregate-memory,wide-division,dead-instructions \
  --compare=badge-compiler/build/c06-metrics/my-m04-baseline

cd badge-compiler
BADGE_OPT_PASSES=scalar-promotion,constant-fold,aggregate-memory,wide-division,dead-instructions \
  IR_OPTIMIZER=owned bash tests/codegen_oracle.sh
```

The oracle uses the LLVM/QEMU environment variables described for M03.
Standalone `badge-opt --passes=scalar-promotion` isolates just this pass;
it does not supply the legalizations required by arbitrary executable inputs.

### Qualified M04 ablation and limits

The [M03-only baseline](../build/c06-metrics/m04-m03-baseline/report.json) and
[M04 candidate/comparison](../build/c06-metrics/m04-scalar-promotion/report.json)
each capture three deterministic 36-module/11-image builds, with identical
application/C05 inputs, build ID, target flags, linker scripts and vendor
assets. Provenance includes the promotion source and allocated-type analysis.

| Corpus metric | M03 only | M04 + M03 |
| --- | --- | --- |
| C06 instructions | 43,531 | 41,763 |
| Allocas | 3,096 | 2,637 |
| Loads | 9,505 | 8,659 |
| Stores | 5,874 | 5,411 |
| Nonvendor linked bytes | 1,970,092 | 1,917,860 |
| Total linked bytes | 2,196,012 | 2,143,780 |

All **459 eligible slots** are promoted (14.826% of candidates), removing
**846 loads / 463 stores**. This saves **52,232 linked bytes (2.651%
incremental)**. Vendor payloads (225,920 bytes), calls, branches, PHIs and
volatile-access totals remain unchanged. All production promotions use the
single-block mode. The single-entry-store mode is independently tested but
does not contribute to this corpus's savings: C05 places most multiblock
parameter stores in `b0`, after its allocation-only `entry` prelude. M04
does not relabel that block or silently expand to arbitrary dominating stores.

Residual slots: **2,417 require CFG promotion**, **196 have unsupported
aggregate storage**, and **24 have escaping/indirect uses**. These counts
are pass-time classifications, not a promise that all residual slots will
be promotable by M05.

| Image | Linked bytes before -> after | Maximum frame plus saves before -> after |
| --- | --- | --- |
| `hello_idle` | 1,276 -> 1,200 | 120 -> 112 |
| `screen_test` | 21,700 -> 20,364 | 464 -> 464 |
| `imu_demo` | 163,400 -> 160,248 | 2,136 -> 2,056 |
| `diagnostics` | 503,484 -> 497,044 | 1,264 -> 1,248 |
| `tilt_breakout` | 87,180 -> 82,708 | 1,400 -> 1,376 |
| `usb_wifi_udp` | 1,176,668 -> 1,151,488 | 5,136 -> 5,112 |

All 11 images shrink. Nine maximum frames shrink; `screen_test` and
`peripheral_probe` maxima stay unchanged. None regress. The known
`hello_idle` entry-call bound is **144 -> 136 bytes**; the other ten remain
`null` with unresolved-path reasons. The largest frame still exceeds the
production **4,096-byte** stack. M04 is not a stack-safety, interrupt,
timing or Wi-Fi RF qualification.

Measured forced-build median wall time is **4.933 -> 4.892 seconds**;
maximum OS-reported RSS is **70,221,824 -> 70,320,128 bytes**. These host
samples show no measured cost regression of concern, not guaranteed speed
or memory improvements.

Qualification:

- [ASan/UBSan fixtures](../tests/ir_scalar_promotion_test.c) check promotion and
  byte-identical preservation, overwritten/shared values, narrow/i64/pointer
  types, undef/poison, dominance/order, branch/loop/live and parallel PHI
  edges, escaping/volatile/opaque/mismatched/dynamic/aggregate/nonentry
  cases, alias exposure on repetition, cross-function/quoted names,
  a **5,000-slot chain** and reversed textual CFG order. Positive outputs
  independently pass LLVM verification; invalid IR preserves the CLI's
  existing output file rather than disappearing through promotion.
- All 36 corpus transformations reproduce release output under sanitizers,
  independently pass LLVM verification, and all 72 frontend/C06 boundaries
  round-trip byte-identically. Complete owned-only/oracle/11 metrics gates
  pass; the system-only-PATH default build preserves all **179** prior
  artifact hashes.
- The [source differential fixture](../tests/fixtures/scalar_promotion_oracle_fixture.c)
  covers narrow/wide arithmetic, pointers, address escape, volatile values
  and loops. It executes through owned code generation, LLVM generation
  from owned optimized IR, direct LLVM C compilation and native host C.
  The [IR execution fixture](../tests/fixtures/scalar_promotion_ir_fixture.ll) independently
  covers entry-store branches, i64/PHI joins and per-iteration single-block
  initialization. Mixed/all-owned ABI and wide-division gates also pass.
- The [paired-badge run](../../build/compiler-compare-runs/run.b98j5p/results.json)
  uses the recorded M04/M03 pipeline, matches all six modules/77,320 work
  items against LLVM, and recovers both badges to BOOTSEL. The established
  fingerprint, operation-coverage and isolated 64 KiB-stack limits remain.

M04 is complete **as an opt-in pass**; subsequent M05 qualification follows.

## C06 M05 pruned scalar mem2reg

[ir_mem2reg.c](../src/ir_mem2reg.c) adds the registered optimization pass `mem2reg`.
**M03-M05 remain opt-in; the three-pass default is unchanged.** M05 removes
private scalar stack communication across general branches and loops,
without alias analysis, CFG rewriting or register allocation.

### Eligibility and SSA construction

M04 and M05 share [ir_promotion.h](../src/ir_promotion.h) and the common verified-slot
eligibility implementation in [ir_scalar_promotion.c](../src/ir_scalar_promotion.c):
positive fixed-count, entry-block `i1/i8/i16/i32/i64` or `ptr` allocas;
only direct, ordinary, same-type loads/stores; no address escape, GEP/alias,
volatile/opaque access, dynamic count or unsupported aggregate storage.
M05 removes M04's single-block/single-entry-store restriction. Memory uses
in unreachable blocks remain conservatively ineligible.

The pass:

1. Reuses M02's verified CFG and immediate dominators to construct a
   dominator tree and dominance frontiers. Parallel edges are not collapsed
   in the CFG; frontier entries are deduplicated independently.
2. Finds each slot's upward-exposed loads and propagates live-in backwards,
   stopping at stores. Pruned iterated frontiers place PHIs only where an
   incoming value is needed; store-only slots do not acquire dead PHIs.
3. Walks the dominator tree with explicit frames, initializing each slot to
   **`undef`** and forwarding stores/loads in instruction order. Per-slot
   memory-access indexes avoid rescanning unrelated instructions. Renaming
   supports textual block order differing from dominance order.
4. Fills PHI operands from predecessor end values, once for every CFG edge,
   including repeated branch/switch predecessors. An unreachable predecessor
   contributing to a reachable PHI receives `undef`; no value is fabricated
   as zero and no reachable initialization is guessed.
5. Resolves promoted load aliases and removes trivial generated/existing
   pure scalar PHIs through a dependency worklist. Self references are
   ignored when identifying one common nonself input; an all-self cycle can
   become `undef`. Integer constants compare modulo their width, and global
   names use canonical quoted-name equivalence. Undef and poison are not
   treated as equal to arbitrary defined values. Opaque PHIs are retained.

PHIs use collision-checked fresh SSA names and remain at block starts.
Typed operand substitutions and original instruction erasure preserve
unrelated bytes and attributes/metadata. M02 verifies before every selected
pass and after the final rewrite. General nontrivial PHI cycles, escaping
storage and aggregate promotion are outside this milestone; no SCC
simplification or speculative algebraic transformation is implied.

### Bounds and pass selection

Storage uses indexed arrays, bounded context allocations and explicit
worklists, not recursive CFG/SSA traversal or a blocks-by-slots value matrix.
The additional mem2reg work budget is **50 million steps**, with at most
**1 million PHI records**, within the existing **128 MiB tracked arena** and
**16 MiB input/output** limits. Exceeding a limit diagnoses and prevents
publishing output; it does not silently retain partially optimized IR.
Per-slot block/liveness/frontier work remains bounded and may reject
pathologically large supported inputs.

Metrics record candidate/promoted slots, removed loads/stores, inserted
PHIs, generated/existing trivial PHIs removed, retained generated PHIs,
SSA substitutions, undef forwarding and ineligible reasons. Promotion uses
one eligibility sweep: eliminating pointer-valued storage may expose another
slot on a subsequent explicit pass, rather than implying alias analysis.

The qualified pipeline keeps M04's cheap promotion first, then general
promotion, folding, required legalizations and M03 cleanup:

```sh
BADGE_OPT_PASSES=scalar-promotion,mem2reg,constant-fold,aggregate-memory,wide-division,dead-instructions \
  bash badge-compiler/scripts/build-owned.sh

# Use fresh, exclusive snapshot labels. Baseline contains M04/M03 but not M05.
node badge-compiler/tools/benchmark-c06.mjs --label=my-m05-baseline --runs=3 \
  --passes=scalar-promotion,constant-fold,aggregate-memory,wide-division,dead-instructions
node badge-compiler/tools/benchmark-c06.mjs --label=my-m05-candidate --runs=3 \
  --passes=scalar-promotion,mem2reg,constant-fold,aggregate-memory,wide-division,dead-instructions \
  --compare=badge-compiler/build/c06-metrics/my-m05-baseline

cd badge-compiler
BADGE_OPT_PASSES=scalar-promotion,mem2reg,constant-fold,aggregate-memory,wide-division,dead-instructions \
  IR_OPTIMIZER=owned bash tests/codegen_oracle.sh
```

The oracle uses the LLVM/QEMU variables described for M03; ordinary owned
builds and sanitizer/unit tests in owned mode require none of those tools.
`badge-opt --passes=mem2reg` independently selects this pass; it neither
implicitly selects M04 nor provides the mandatory legalizations.

### Qualified M05 ablation

The [M04 baseline](../build/c06-metrics/m05-m04-baseline/report.json) and
[M05 candidate/comparison](../build/c06-metrics/m05-mem2reg/report.json) each
contain three deterministic builds of all 36 modules and 11 images.
Application/C05 inputs, target flags, build ID, linker scripts and vendor
assets match; provenance includes mem2reg and the shared safety header.

| Corpus metric | M04/M03 | M05/M04/M03 |
| --- | --- | --- |
| Instructions | 41,763 | 30,410 |
| Allocas | 2,637 | 220 |
| Loads | 8,659 | 2,124 |
| Stores | 5,411 | 1,474 |
| PHIs | 4 | 1,548 |
| Nonvendor linked bytes | 1,917,860 | 1,724,460 |
| Total linked bytes | 2,143,780 | 1,950,380 |

All **2,417 eligible remaining slots** are promoted, eliminating
**6,535 loads / 3,937 stores**. **1,544 generated PHIs** are inserted and
retained in this corpus; trivial-PHI cleanup is exercised in separate
fixtures rather than claimed as a production benefit. The four other
PHIs come from mandatory wide-division legalization.

The incremental linked saving is **193,400 bytes (10.084%)**. Vendor assets
(225,920 bytes), calls, branches and volatile-access totals remain unchanged.
Residual storage comprises **196 aggregate slots / 24 escaping or indirect
slots**; these remain real memory rather than being silently modeled as SSA.

| Image | Linked bytes before -> after | Maximum frame plus saves before -> after |
| --- | --- | --- |
| `hello_idle` | 1,200 -> 1,140 | 112 -> 96 |
| `screen_test` | 20,364 -> 16,040 | 464 -> 264 |
| `imu_demo` | 160,248 -> 146,808 | 2,056 -> 1,896 |
| `diagnostics` | 497,044 -> 475,560 | 1,248 -> 1,032 |
| `peripheral_probe` | 55,868 -> 46,640 | 1,144 -> 664 |
| `usb_wifi_udp` | 1,151,488 -> 1,050,544 | 5,112 -> 3,504 |

All 11 images and maximum frames shrink, including C07's PHI value slots and
parallel-copy scratch costs. The largest local frame plus saves is now
**3,504 bytes**, below the 4,096-byte local-frame goal **with M05 selected**.
This is not a production stack certificate: the known `hello_idle` entry
bound is **136 -> 120 bytes**, while the other ten remain `null`; nested
calls, outgoing arguments, indirect calls and interrupts require separate
qualification. Wi-Fi RF/timing has not been requalified for this pipeline.

An independently configuration-checked comparison to the compatible
current-input default snapshot saves **270,880 nonvendor bytes (13.576%)**
across M03-M05. Historical M01 application inputs remain incompatible;
the proposed 20% release improvement is not yet achieved.

Measured forced-build median wall time is **4.943 -> 4.836 seconds**;
maximum OS-reported RSS is **69,910,528 -> 72,040,448 bytes**. These are M01
methodology host samples, not guaranteed compilation/runtime speedups or
simultaneous process-tree memory measurements.

### Qualification and default preservation

- [ASan/UBSan fixtures](../tests/ir_mem2reg_test.c) cover diamonds, nested merges,
  loop/backedge swaps, irreducible CFGs, narrow/bool/pointer/i64 PHIs,
  repeated and unreachable predecessor edges, undef/poison, quoted/global
  equivalence, fresh-name collisions, liveness pruning, cascading original
  and generated PHI cleanup, exclusions, output idempotence, reversed
  1,000-block CFGs and 5,000-slot chains. Positive outputs independently pass
  LLVM verification. Malformed IR preserves an existing CLI output;
  allocation exhaustion diagnoses explicitly.
- All 36 candidate transformations reproduce release output under
  sanitizers and independently pass LLVM verification; all 72 frontend/C06
  boundaries verify and round-trip byte-identically. Full owned-only,
  independent LLVM/oracle and all 11 metrics tests pass.
- The extended [source fixture](../tests/fixtures/scalar_promotion_oracle_fixture.c) executes
  loop-carried narrow/bool/pointer/i64 swaps through owned code generation,
  LLVM code generation from owned IR, direct LLVM C compilation and host C.
  The [IR fixture](../tests/fixtures/mem2reg_ir_fixture.ll) independently executes irreducible
  loops and repeated switch/branch edges. Existing scalar/entry-store,
  wide-division and bidirectional mixed/all-owned ABI gates also pass.
- The [physical run](../../build/compiler-compare-runs/run.j3I633/results.json)
  records the selected six-pass pipeline, matches all six modules/77,320
  work items against LLVM, and automatically recovers both badges to
  BOOTSEL. Existing fingerprint and operation-coverage limits and the
  isolated 64 KiB demo stack still apply.
- Final system-only-PATH owned build/tests preserve **all 179**
  prior default artifact hashes. No new default pass is enabled.

M05 is complete **as an opt-in pass**. M06 qualification follows; M09 still
owns combined default qualification and release decisions.

## C06 M06 sparse conditional constant propagation

[ir_sccp.c](../src/ir_sccp.c) adds the opt-in `sccp` pass. **The three-pass compiler
default and timing mode's M05 default selection remain unchanged.**
The qualified M06 pipeline is:

```sh
BADGE_OPT_PASSES=scalar-promotion,mem2reg,sccp,constant-fold,aggregate-memory,wide-division,dead-instructions \
  bash badge-compiler/scripts/build-owned.sh

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
BADGE_OPT_PASSES=scalar-promotion,mem2reg,sccp,constant-fold,aggregate-memory,wide-division,dead-instructions \
  node badge-compiler/tools/compiler-compare.mjs --timing --seed=3276377845 \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

`badge-opt --passes=sccp` independently selects SCCP, including its folding
and DCE cleanup, but does not implicitly promote storage or perform required
backend legalizations.

### Contract and bounds

SCCP uses an unknown/constant/overdefined integer lattice, indexed def-use
worklists, executable blocks and individual executable CFG edges. Function
arguments are overdefined; PHIs meet only executable incoming edges.
Conditional branches and switches activate one edge only with a proven
integer condition; otherwise all possible edges remain. Lattice changes
reschedule uses, including backedges. Remaining reached bottom cycles are
resolved conservatively rather than guessed.

Integer arithmetic, comparisons and casts reuse the same unsigned,
fixed-width evaluator as ordinary folding through [ir_integer.h](../src/ir_integer.h).
Overflow/exact/disjoint flags and opaque instruction results are not folded.
Overshifts, division by zero and signed minimum/-1 division or remainder
are not evaluated. Undef/poison and their freezes are not invented constants;
a proven select condition can select its defined arm without evaluating the
unselected poison arm. Pointer arithmetic/comparisons, loads, aggregates and
interprocedural constant propagation are outside the lattice.

The rewrite substitutes proven values, replaces proven terminators, removes
unreachable labels/instruction spans (including multiline switches), and
filters PHIs against the **retained CFG edges**, not merely live predecessor
blocks. Parallel edges retain their required multiplicity. A dependency
worklist also removes trivial scalar/pointer PHIs using existing values,
without inserting a freeze of an unknown value or collapsing unanchored
cycles. Opaque PHIs retain their suffix metadata while their incoming edges
are repaired; opaque terminators are not simplified. Modules containing
`blockaddress` conservatively skip SCCP entirely because deleting an
address-taken block is not justified by ordinary executable-edge analysis.

Reachable calls and volatile/MMIO operations are neither folded nor
reordered. Effectful operations can disappear only inside a block proved
unreachable. The original module must verify before any deletion, including
its dead code. The pass serializes, releases scratch, reparses and verifies
between CFG rewriting, existing constant folding and existing DCE; final
pipeline verification remains mandatory.

The additional SCCP budget is **50 million work steps**, within the existing
128 MiB tracked arena, 16 MiB text and one-million-token limits. Work and
allocation exhaustion diagnose and preserve existing CLI output; there is
no partial-success fallback. Observations distinguish removed blocks/edges,
repaired incoming PHI edges, constants, trivial PHIs and operand propagation.
Cleanup observations are attributed to the encompassing `sccp` pass.

### Corpus size and host cost

The [M05 baseline](../build/c06-metrics/m06-m05-qualified/report.json) and
[M06 candidate/comparison](../build/c06-metrics/m06-sccp-qualified/report.json) each
contain three deterministic forced builds of the same 36 modules and
11 images. Source IR, target flags, build IDs, linker scripts and vendor
payloads match.

| Corpus metric | M05 | M06 |
| --- | ---: | ---: |
| Instructions | 30,410 | 30,369 |
| Branches | 6,615 | 6,592 |
| PHIs | 1,548 | 1,548 |
| Allocas / loads / stores | 220 / 2,124 / 1,474 | unchanged |
| Calls / volatile loads / volatile stores | 2,142 / 117 / 214 | unchanged |
| Nonvendor linked bytes | 1,724,460 | 1,723,800 |
| Total linked bytes | 1,950,380 | 1,949,720 |

SCCP removes **41 unreachable blocks / 47 CFG edges**, simplifying 24
terminators. The incremental linked saving is **660 bytes (0.0383%)**;
all 11 images shrink, by 32-152 bytes each. Maximum frames do not change;
the largest remains **3,504 bytes**, not a production stack certificate.
The 1,015 constants deleted by SCCP are pass observations, **not** 1,015
additional savings versus M05's existing folding/DCE. No incremental
production PHI or memory-access reduction is claimed.

A [current-source default snapshot](../build/c06-metrics/m06-default-qualified/report.json)
records the unchanged three-pass pipeline. Combined M03-M06 saving is
**271,540 bytes (13.609%)**, still below the proposed 20% size release goal.
Historical token-limit/M05 artifact-hash checks remain historical evidence,
not a claim that changed tool provenance has the same hashes today.

Host median user CPU time grows **1.85 -> 1.93 seconds**; maximum
OS-reported RSS grows **70,500,352 -> 98,402,304 bytes**. Median wall
times are **4.118 -> 5.064 seconds**, with individual samples ranging from
3.984 to 6.003 seconds. These three-run host samples show additional
compilation cost, not a runtime improvement or simultaneous process-tree
memory measurement.

### Controlled RP2350 runtime ablation

The completed [M05 hardware baseline](../../build/compiler-compare-runs/run.41DIv8/results.json)
and [M06 hardware candidate](../../build/compiler-compare-runs/run.wC1prb/results.json)
use seed **3276377845**, identical suite/build identity and LLVM image,
matching clock snapshots and workload counts, and fresh nonces in all four
rounds. Each pipeline swaps compiler images between both badges. All six
correctness modules match in every round; both devices recover to BOOTSEL.

| Workload | M05 owned/LLVM | M06 owned/LLVM | Relative runtime change |
| --- | ---: | ---: | ---: |
| Arithmetic | 12.8367x | 12.8354x | -0.0101% |
| Strings | 17.0056x | 17.0710x | +0.3845% |
| Memory | 17.8435x | 17.8486x | +0.0285% |
| Control flow | 12.6000x | 12.5993x | -0.0052% |
| Cross-file ABI | 14.5856x | 14.5853x | -0.0017% |
| Graphics + framebuffer CRC | 15.8193x | 15.8193x | -0.0003% |

These raw-tick, role-swapped median ratios show **no meaningful runtime
improvement in the existing six workloads**, and no observed regression
near the 5% investigation threshold. They are not a statistical confidence
interval or a universal slowdown estimate. SCCP's small production CFG
cleanup does not address C07's pervasive spill traffic.

Interrupted attempts are retained but excluded from this comparison.
One flash timeout left a partial image, which was explicitly restored and
verified. Subsequent USB collection failures also occurred with the unchanged
M05 control; after physically reconnecting both badges, both full comparisons
passed. No transport failure is reported as a compiler speed result.

### Validation

- [ASan/UBSan fixtures](../tests/ir_sccp_test.c): 31 independently LLVM-verified,
  idempotent cases covering diamonds, constant/unknown/default/multiline
  switches, loop lattice changes, irreducible edges, quoted labels,
  parallel edges, cascading scalar/pointer PHIs, metadata, block addresses,
  poison/undef/select/freeze and narrow/i64 arithmetic boundaries.
  An 8,000-definition chain exercises sparse scheduling; malformed dead code
  and deliberately exhausted work budget preserve existing CLI output.
- All 36 corpus transformations reproduce release output under sanitizers;
  all 72 frontend/output boundaries independently pass LLVM verification.
  Existing optimizer, M02-M05, metrics and default-policy tests pass.
- [Source](../tests/fixtures/sccp_oracle_fixture.c) and [raw IR](../tests/fixtures/sccp_ir_fixture.ll) execute
  successfully through owned Thumb code generation and independent LLVM
  compilation on Cortex-M33 QEMU; direct LLVM C and host C also agree.
  Existing wide-division, loop/irreducible/parallel-edge and bidirectional
  mixed/all-owned AAPCS execution gates pass with the M06 pipeline.
- Runner tests validate exact uint32 seed bounds, fresh nonces across
  invocations/role swaps, unchanged random mode and failure rejection.

M06 is complete **as an opt-in pass**. M07/M08 qualification follows; M09 and RP2350 follow-ups remain
pending; the fixed-seed option implements only part of proposed P00, not
its isolated-kernel campaign or the backend speedups.

## C06 M07 bounded instruction combining

[ir_instruction_combine.c](../src/ir_instruction_combine.c) registers the opt-in
`instruction-combine` pass. **Compiler defaults and the timing runner's
M05 default selection remain unchanged.** The qualified combined pipeline is:

```sh
BADGE_OPT_PASSES=scalar-promotion,mem2reg,sccp,instruction-combine,constant-fold,aggregate-memory,wide-division,dead-instructions \
  bash badge-compiler/scripts/build-owned.sh

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
BADGE_OPT_PASSES=scalar-promotion,mem2reg,sccp,instruction-combine,constant-fold,aggregate-memory,wide-division,dead-instructions \
  node badge-compiler/tools/compiler-compare.mjs --timing --seed=3276377845 \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

`badge-opt --passes=instruction-combine` independently selects combining
and its DCE cleanup. It does not implicitly select promotion/SCCP or the
mandatory backend legalizations.

### Enabled rules and semantic preconditions

Only verified, nonopaque scalar arithmetic/casts enter the whitelist.
The ordinary operands remain in their original order; there is no general
reassociation, constant folding, division replacement or range analysis.

| Rule | Preconditions / replacement |
| --- | --- |
| `add_zero`, `or_zero`, `xor_zero` | One operand is integer zero; forward the other operand, with the same type |
| `sub_zero` | Right operand is zero; forward the left (not `0-x`) |
| `mul_one` | One operand has bit-pattern 1; forward the other. **Retain `nsw i1`**, where 1 denotes signed -1 and can overflow |
| `and_all_ones` | One operand is all ones modulo its integer width; forward the other |
| `shift_zero` | Shift count is exactly zero for `shl/lshr/ashr`; forward the left operand |
| `identity_bitcast` | Same integer type, or same pointer type with a local SSA source |
| `trunc_extension_roundtrip` | `trunc(zext/sext x)` returns to x's original integer width; forward x |
| `trunc_extension_chain` | Truncating an extension to a width above x's width becomes the original extension; below x's width becomes a truncation of x |
| `trunc_chain` | Two strictly narrowing truncations become one |
| `zext_chain`, `sext_chain` | Two strictly widening extensions of the same kind become one |
| `sext_after_zext` | The inner zero extension strictly widened x, so its sign bit is zero; extend x directly with `zext` |

Constants are compared modulo the verified width using the shared integer
helpers. Compatible `nsw/nuw/exact/disjoint` flags can disappear **only**
where the neutral identity proves their additional poison condition cannot
occur. The `nsw i1` multiplication exception is explicitly tested.
Direct undef/poison literals are not forwarded or turned into invented
constants. Local SSA values, including poison, retain the identity's existing
propagation semantics. Freeze, select, comparisons and PHIs are not algebraic
combining candidates.

`zext(trunc x)`, `sext(trunc x)` and `zext(sext x)` are retained unless an
intervening rule independently establishes a listed precondition.
There is no pointer/int roundtrip cancellation. Global/literal pointer
bitcasts are retained: exposing a direct callee from an indirect call can
change the supported verifier's declaration/ABI checks. Casts carrying
metadata, and chains through opaque producers, are not combined.

Indexed SSA aliases and a dependency worklist handle cascades, PHI/backedge
uses and reversed block order without recursive substitution. Removed
definitions forward existing typed operands; no new freeze or speculative
zero is inserted. Original use-chain links are retained for scheduling even
when a cast's effective source changes. Unanchored cyclic aliases are not
deleted. Effectful calls, memory, volatile/MMIO and trapping operations
remain; CFG edges and memory-access ordering are unchanged.

The additional combining budget is **50 million work steps**, within the
existing 128 MiB arena, 16 MiB text and one-million-token limits. Exhaustion
diagnoses and preserves existing CLI output. The pass uses the shared
`ir_reparse` core helper to serialize, release scratch and verify before
existing DCE, then reparse/verify its cleanup result. M06 uses the same helper;
its transformation behavior is unchanged.

### Reached rules, linked size and C07 effects

The [M06 baseline](../build/c06-metrics/m07-m06-qualified/report.json) and
[M07 candidate/comparison](../build/c06-metrics/m07-combine-qualified/report.json)
each contain three deterministic forced builds of identical inputs across
36 modules and 11 images.

| Reached rule | Corpus transformations |
| --- | ---: |
| `zext_chain` | 20 |
| `sext_after_zext` | 1 |
| `trunc_extension_chain` | 4 |
| `sub_zero` | 6 |
| `add_zero` | 1 |
| Other enabled rules | 0 (covered by fixtures) |

These are rule events, not independent byte savings. Seven neutral
definitions disappear directly; DCE removes 24 now-unused producers.
Instructions fall **30,369 -> 30,338**. Alloca/load/store/PHI/branch/call and
volatile-access totals remain unchanged. Nonvendor linked bytes fall
**1,723,800 -> 1,722,804**, saving **996 bytes (0.0578%)** incrementally.
Ten images shrink; `hello_idle` remains unchanged. The largest frame plus
saves stays **3,504 bytes**.

Representative production C07 inspection in `gpio_monitor.o` confirms
less actual machine work, not just shorter IR:

| Function | Static Thumb instructions | Static loads / stores | Local frame bytes |
| --- | --- | --- | --- |
| `rgb565` | 146 -> 139 | 19/19 -> 18/18 | 80 -> 72 |
| `screen_pixel` | 793 -> 769 | 92/84 -> 89/81 | 296 -> 288 |
| `bare_main` | 632 -> 616 | 91/72 -> 89/70 | 248 -> 240 |

Instruction counts exclude directives and labels. The load/store counts
are static `ldr*`/`str*` counts in these emitted functions, not dynamic
access counts or a general memory analysis. `gpio_monitor`'s image maximum
frame plus saves drops **320 -> 312**; other image maxima do not change.
The private `update_devices` local frame in `peripheral_probe.o` also drops
320 -> 288, without changing that image's maximum.

The [current-source default snapshot](../build/c06-metrics/m07-default-qualified/report.json)
preserves **all 228** captured default artifact hashes from M06.
Cumulative M03-M07 saving is **272,536 bytes (13.659%)**, still below the
proposed 20% release goal. Stack/interrupt/RF safety is not certified.

Median host user CPU time is **1.94 -> 2.04 seconds**; median wall time is
5.192 -> 4.944 seconds, with individual samples from 4.807 to 7.091 seconds.
Maximum OS-reported RSS is 97,255,424 -> 95,944,704 bytes. These three-run
samples show extra CPU cost, not a guaranteed compilation speed or memory
improvement, nor simultaneous process-tree memory measurements.

### Controlled hardware qualification

Seed **3276377845**, suite source/build identity, repetitions, LLVM image
and clock snapshots match between the final
[M06 baseline](../../build/compiler-compare-runs/run.aYMypX/results.json) and
[M07 candidate](../../build/compiler-compare-runs/run.GpGXth/results.json).
Both use fresh nonces and swap images between the physical badges.

| Workload | M06 owned/LLVM | M07 owned/LLVM | Relative runtime change |
| --- | ---: | ---: | ---: |
| Arithmetic | 12.8363x | 12.8362x | -0.0013% |
| Strings | 17.0646x | 17.0689x | +0.0250% |
| Memory | 17.8496x | 17.8441x | -0.0306% |
| Control flow | 12.5998x | 12.5976x | -0.0172% |
| Cross-file ABI | 14.5888x | 14.5950x | +0.0424% |
| Graphics + framebuffer CRC | 15.8193x | 15.8193x | -0.0000% |

An independent [baseline](../../build/compiler-compare-runs/run.QocvQf/results.json) /
[candidate](../../build/compiler-compare-runs/run.d5ryJa/results.json) repeat
uses exactly the same emitted firmware hashes and records changes from
-0.0261% to +0.0668%. All six modules match LLVM in all eight comparison
rounds; both badges recover to BOOTSEL after every completed invocation.

There is **no meaningful runtime improvement in these workloads**, and no
observed regression near the 5% investigation threshold. Tiny differences
are not a statistical confidence claim. Units remain inherited TIMER0
ticks, not assumed microseconds or CPU cycles. C07 spill traffic remains
the major outstanding runtime limitation.

### Validation

- [132 ASan/UBSan and LLVM-verified/idempotent fixtures](../tests/ir_instruction_combine_test.c)
  exercise each enabled rule and rejected boundary, `i1/i8/i16/i32/i64`,
  flags, signed/unsigned casts, metadata, poison/undef/freeze, pointer-call
  conservatism, alias cascades, quoted names, PHIs/backedges and reversed
  blocks. An 8,000-definition chain checks scheduling/substitution; invalid
  input, work-budget and allocation exhaustion diagnose atomically.
- All 36 corpus outputs reproduce release results under sanitizers and
  pass LLVM verification at all 72 frontend/output boundaries. M07 also
  remains idempotent on the legalized corpus. Existing M02-M06 and default
  policy/metrics tests remain passing.
- [C](../tests/fixtures/instruction_combine_oracle_fixture.c) and
  [raw IR](../tests/fixtures/instruction_combine_ir_fixture.ll) execute through owned and
  independent LLVM Cortex-M33 code generation. All 256 byte values,
  signed/unsigned extension distinctions and i64 minimum/all-ones flagged
  identities are checked; direct LLVM C and host C agree. Existing
  bidirectional mixed/all-owned AAPCS, division, loop and CFG fixtures pass.

M07 is complete **as an opt-in pass**. M08 qualification follows; M09 and the RP2350 backend
follow-ups remain pending.

## C06 M08 basic-block common-subexpression elimination

[ir_local_cse.c](../src/ir_local_cse.c) registers opt-in `local-cse`. Compiler
defaults and the timing runner's M05 default selection remain unchanged:

```sh
BADGE_OPT_PASSES=scalar-promotion,mem2reg,sccp,instruction-combine,local-cse,constant-fold,aggregate-memory,wide-division,dead-instructions \
  bash badge-compiler/scripts/build-owned.sh

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
BADGE_OPT_PASSES=scalar-promotion,mem2reg,sccp,instruction-combine,local-cse,constant-fold,aggregate-memory,wide-division,dead-instructions \
  node badge-compiler/tools/compiler-compare.mjs --timing --seed=3276377845 \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

`badge-opt --passes=local-cse` selects CSE and verified DCE cleanup, not
promotion or the mandatory backend legalizations.

### Rules, exclusions and bounds

Only effect-free integer arithmetic, comparisons, casts and GEP address
calculations with a result definition are eligible. An expression can reuse
only an earlier, same-type definition in the **same reachable basic block**.
There is no cross-block expression table, operand reordering, reassociation,
load/call elimination, or alias analysis. Unreachable blocks are skipped.

Keys include the result type and the complete opcode/flag/type/punctuation
token skeleton, with typed operand identities. Thus comparison predicates,
cast signedness, GEP element/index types and `inbounds`, and
`nsw/nuw/exact/disjoint` remain distinct. Integer literals normalize modulo
their width; quoted local/global spellings resolve to their indexed identities.
No hash alone establishes equality: lengths and complete key bytes must match.
The per-block open-addressed table stays at most half occupied and uses the
existing core hash, with generation-based resets.

Calls, loads/stores, volatile/MMIO, metadata/opaque instructions, trapping
division/remainder, direct undef/poison literals, independent freezes,
selects, PHIs and aggregate operations are not CSE candidates. Pure SSA
calculations may match across a call/store inside their block; this neither
reuses memory results nor changes memory/call ordering. Expressions using
different load definitions do not match.

Reverse-postorder traversal establishes dominating aliases before downstream
keys, without moving emitted instructions or sharing expression tables between
blocks. Typed substitutions repair all uses, including PHIs/backedges.
Shared reparse/verification runs before and after existing DCE cleanup.

The additional budget is **50 million work steps** for keys, probes,
equality comparisons, aliases and substitutions, within the existing
128 MiB arena, 16 MiB text and one-million-token limits. Exhaustion diagnoses
and preserves an existing CLI output file atomically.

### Corpus, linked size and C07 effects

The [M07 baseline](../build/c06-metrics/m08-m07-qualified/report.json) and
[M08 comparison](../build/c06-metrics/m08-cse-qualified/report.json) each contain
three deterministic forced builds across 36 C modules and 11 images:

| Expression class | Reused definitions |
| --- | ---: |
| Arithmetic | 40 |
| Comparisons | 1 |
| Casts | 38 |
| Address calculations | 127 |
| Total | 206 |

Instructions fall **30,338 -> 30,132**. Nonvendor linked bytes fall
**1,722,804 -> 1,716,284**, saving **6,520 bytes (0.3785%)** incrementally.
Ten images shrink; `hello_idle` is unchanged. Alloca/load/store/PHI/branch/
call/volatile totals remain unchanged. Sixty-one function-local frames shrink;
none grows. Image maxima shrink for `imu_demo` (**1,896 -> 1,864**) and
`usb_wifi_udp` (**3,504 -> 3,496** bytes including saves); other maxima
are unchanged. Ten entry-call stack bounds remain unknown, so this is not
stack or interrupt certification.

Representative real C07 output:

| Function | Static Thumb instructions | Static loads / stores | Local frame bytes |
| --- | --- | --- | --- |
| `wifi_firmware_read_frame` | 1,076 -> 1,047 | 153/126 -> 149/122 | 464 -> 448 |
| `poll_packet` | 1,786 -> 1,715 | 259/204 -> 248/195 | 704 -> 672 |
| `live_usb_control_setup` | 10,096 -> 10,082 | 1,266/1,161 -> 1,264/1,159 | 3,480 -> 3,472 |
| Diagnostic `compiler_memory` workload | 672 -> 584 | 105/80 -> 94/69 | 408 -> 360 |

Counts exclude directives/labels; loads/stores are static `ldr*`/`str*`
instructions, not dynamic accesses or an assertion that every access is a
spill. C07 still uses SSA stack homes; longer-lived representatives do not
cause a new register-allocation policy.

Median forced-build wall time is **5.028 -> 5.055 seconds** (+0.52%);
maximum OS-reported RSS is **90,701,824 -> 92,323,840 bytes**. These small
three-run differences are measurements, not a compilation-speed guarantee.
The [default snapshot](../build/c06-metrics/m08-default-qualified/report.json)
preserves all **228** M07 default artifact hashes. Combined M03-M08 savings
are **279,056 bytes (13.9854%)**, still below the proposed 20% release goal.

### Repeated controlled hardware timing

The [M07 baseline](../../build/compiler-compare-runs/run.pfyvin/results.json) /
[M08 candidate](../../build/compiler-compare-runs/run.kZFgc1/results.json) and
independent [baseline repeat](../../build/compiler-compare-runs/run.YJX7bY/results.json) /
[candidate repeat](../../build/compiler-compare-runs/run.m1L2c5/results.json)
share seed **3276377845**, source/build identity, repetitions, LLVM image
and exact clock snapshots. Each invocation swaps compiler roles between
the two badges; all eight nonces are fresh.

| Workload | M08 runtime change vs M07 | Independent repeat |
| --- | ---: | ---: |
| Arithmetic | -7.1716% | -7.1792% |
| Strings | -6.0410% | -5.9394% |
| Memory | **+7.6372%** | **+7.6477%** |
| Control flow | -0.6166% | -0.6196% |
| Cross-file ABI | -1.5661% | -1.5685% |
| Graphics + framebuffer CRC | -1.2147% | -1.2151% |

Negative means faster. All six modules match LLVM in all eight rounds;
both badges recover to BOOTSEL. An initial baseline invocation timed out
during USB collection; it recovered safely and is excluded from timing.
The unchanged baseline passed on retry.

**The memory regression exceeds 5% and is not hidden or waived.** Repetition
rules out a one-off sample. Inspection finds fewer instructions/loads/stores
and a smaller `compiler_memory` frame, not additional spill traffic.
The five byte-operation helpers and `diag_crc32`/`diag_crc32_update` emit
byte-identical assembly before/after, but their linked addresses change.
An isolated baseline rebuild reproduces the exact hardware firmware hash:
`diag_crc32_update` moves from **0x1001ea00 -> 0x1001e35c** with unchanged
0x31e-byte size. Code/data layout and XIP/cache behavior are plausible
contributors, **not an experimentally established root cause**.
No alignment hack, source workaround or backend change is included.

The pass therefore remains opt-in; M09 must resolve or explicitly re-scope
this repeatable regression before enabling a default pipeline. TIMER0 units
remain raw ticks, not assumed microseconds or CPU cycles. Even the faster
workloads remain substantially slower than LLVM.

### Validation

- [92 ASan/UBSan and LLVM-verified/idempotent fixtures](../tests/ir_local_cse_test.c),
  repeated with every key forced to hash-collide, cover widths, flags,
  operand order, signed casts, GEP types, quoted identities, undef/poison/
  freeze, metadata, divisions, calls/memory/volatile, alias cascades,
  PHI/backedge/reversed-block uses, unreachable/empty modules and exact keys.
  An 8,000-repetition fixture checks scaling; invalid input, work and arena
  exhaustion diagnose atomically. Both variants also pass in owned-only mode.
- All 36 full candidate transformations match release under sanitizers;
  all 72 frontend/output LLVM verification boundaries pass. CSE's own
  outputs are idempotent on all 36 legalized inputs. Later legalization
  creates fresh CSE opportunities in `imu_demo`; this is not a whole-pipeline
  fixed-point guarantee, and no hidden second CSE pass is added.
- [C](../tests/fixtures/local_cse_oracle_fixture.c) and [raw IR](../tests/fixtures/local_cse_ir_fixture.ll)
  execute through owned and independent LLVM Cortex-M33 code generation,
  with direct LLVM/host C checks where applicable. All 256 byte values,
  signed i64 casts, flagged arithmetic, repeated addresses, call counts,
  volatile loads and retained division are exercised.
- Existing default-policy/core and M02-M07 regressions, source/raw-IR
  loop/CFG/division and bidirectional/all-owned AAPCS execution pass;
  all 16 reporting/protocol/runner tests pass.

M08 is complete **as an opt-in implementation with a documented runtime
regression**. M09 excludes it from defaults; RP2350 backend follow-ups remain pending.

## C06 M09 qualified default release

The approved first-release scope is the **measured C06 improvement**, not
the original proposed 20% size target. M09 is complete with the following
default, in this exact order:

```text
mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division
```

This changes the **owned C06 compiler** default; the root Makefile's LLVM
compiler selection, clocks, stack regions and production application code
are unchanged. No repeat-to-fixpoint schedule or backend/P-series work is
included. Both hardware comparison modes inherit the same compiler default
and retain explicit `BADGE_OPT_PASSES` overrides.

```sh
badge-compiler/build/badge-opt --default-passes
bash badge-compiler/scripts/build-owned.sh

# Explicit pre-M09 route, retained for controlled comparisons:
BADGE_OPT_PASSES=constant-fold,aggregate-memory,wide-division \
  bash badge-compiler/scripts/build-owned.sh

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
  node badge-compiler/tools/compiler-compare.mjs --timing --seed=3276377845 \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
```

### Size, frames and costs

The [legacy baseline](../build/c06-metrics/m09-legacy/report.json) and
[activated-default snapshot](../build/c06-metrics/m09-default-qualified/report.json)
each contain three deterministic forced builds of the same 36 modules,
11 images, assets, flags, build directory and generated application ID.

| Metric | Legacy default | M09 default |
| --- | ---: | ---: |
| Aggregate nonvendor bytes | 1,995,340 | **1,748,444** |
| Vendor bytes | 225,920 | 225,920 |
| IR instructions | 44,538 | 31,380 |
| Scalar allocas | 3,096 | 220 |
| Loads / stores | 9,505 / 5,874 | 2,124 / 1,474 |
| PHIs | 4 | 1,548 |
| Largest local frame including saves | 5,312 | **3,672** |
| Median forced-build wall seconds | 4.785 | 4.830 |
| Maximum OS-reported RSS bytes | 72,597,504 | 92,897,280 |

The release saves **246,896 nonvendor bytes (12.3736%)**. Every image and
every image's maximum local frame shrinks versus the legacy default.
Calls, branch and volatile-access totals remain unchanged. All **228**
activated-default artifact hashes equal the previously qualified explicit
conservative selection; they are intentionally not the legacy hashes.

Build wall time changes +0.94%, below the 25% investigation threshold;
OS peak RSS increases 20,299,776 bytes. RSS is not simultaneous process-tree
memory or tracked arena usage. No new limit is exceeded.

**Stack readiness is not certified.** The maximum frame meets the <4 KiB
local-frame goal, but ten image entry-call bounds remain unresolved;
`hello_idle`'s resolved bound is 128 bytes. Interrupt/indirect-call and
production Wi-Fi/display/USB servicing safety remain separate qualifications.
The diagnostic images use their existing isolated 64 KiB stack.

### Per-pass ablations and release choices

All size rows are three-run fixed-corpus snapshots. Runtime uses version 2,
the same seed, workload counts, clock snapshots and LLVM control image.
Negative runtime changes mean faster. Internal cleanup is part of the named
pass; omitting an explicit entry does not disable cleanup inside other passes.

| Default optimization omitted | Nonvendor bytes | Largest frame + saves | M09 runtime vs omitted version, six-workload range |
| --- | ---: | ---: | ---: |
| None | 1,748,444 | 3,672 | Baseline |
| [mem2reg](../build/c06-metrics/m09-conservative-no-mem2reg/report.json) | 1,993,660 | 5,304 | **-32.13% to -20.54%** |
| [instruction-combine](../build/c06-metrics/m09-conservative-no-instruction-combine/report.json) | 1,750,160 | 3,680 | -0.55% to -0.02% |

Both added default optimizations have reached production size/frame benefits
and no observed >5% runtime regression in their final pipeline position.
Small combining timing differences are not a statistical confidence claim.
Constant folding and both legalizations retain the legacy baseline contract:
folding is needed to normalize constant wide shift counts when SCCP is absent.
An exploratory no-SCCP/no-folding hardware build correctly failed C07's
supported-input check rather than producing firmware.

The broader [M07 candidate](../build/c06-metrics/m09-candidate/report.json) and
its [no scalar promotion](../build/c06-metrics/m09-no-scalar-promotion/report.json),
[no mem2reg](../build/c06-metrics/m09-no-mem2reg/report.json),
[no SCCP](../build/c06-metrics/m09-no-sccp/report.json),
[no combining](../build/c06-metrics/m09-no-instruction-combine/report.json) and
[no standalone DCE](../build/c06-metrics/m09-no-dead-instructions/report.json)
snapshots publish every initial optional-entry ablation.
Removing scalar promotion or standalone DCE from that candidate preserves
all 228 artifact hashes: mem2reg and existing cleanup already cover the work.
They are not enabled solely because they helped an earlier standalone stage.

Removing SCCP changes the cleanup context, so M09 separately measures
[the final conservative selection](../build/c06-metrics/m09-conservative/report.json)
and [standalone late DCE added back](../build/c06-metrics/m09-cleanup-candidate/report.json).
Late DCE saves a further 24,980 bytes and reduces the maximum frame to 3,504,
but causes a **22.37-22.43% string-workload slowdown** in two independent,
role-swapped [first](../../build/compiler-compare-runs/run.BbQK5V/results.json) /
[repeat](../../build/compiler-compare-runs/run.QRikvB/results.json) runs.
The [SCCP candidate](../../build/compiler-compare-runs/run.Zg5RzQ/results.json)
also has the slower string timing. These observations implicate the
post-folding cleanup/layout context, **not a proven SCCP-specific bug**.
No cache/alignment workaround or source substitution is used.

Therefore:

- `scalar-promotion` stays explicit-only; it is redundant in the measured
  full-mem2reg pipeline.
- Standalone `dead-instructions` stays explicit-only. Internal M05/M07
  cleanup remains enabled, but the late cleanup's runtime/size trade-off is
  not accepted for this default.
- `sccp` stays explicit-only with its cleanup-context runtime caveat.
- `local-cse` stays explicit-only with its independently repeated M08
  memory slowdown. M08's version-1 comparisons remain valid internally;
  their graphics timings are not compared with version 2.

These are measured release choices, not deleted capabilities. The historical
maximum-size-saving pipeline remains available by explicit pass selection.

### Controlled hardware release comparison

The version-2 [legacy baseline](../../build/compiler-compare-runs/run.AbSGtI/results.json)
and [actual-default run](../../build/compiler-compare-runs/run.ev5z7p/results.json)
use seed **3276377845**, matching clocks and LLVM image, and fresh nonces.
Each swaps compiler roles across the badges.

| Workload | M09 runtime change vs legacy default |
| --- | ---: |
| Arithmetic | -30.5164% |
| Strings | -36.1654% |
| Memory | -29.7956% |
| Control flow | -22.2358% |
| Cross-file ABI | -20.5126% |
| Full drawing + sampled framebuffer fingerprint | -25.3161% |

Two earlier independent explicit-selection runs
([first](../../build/compiler-compare-runs/run.ZYhUAp/results.json),
[repeat](../../build/compiler-compare-runs/run.wlxREc/results.json))
and the actual-default run produce the same owned firmware hash and stable
timings. Both selected-pass hardware ablations also pass:
[without mem2reg](../../build/compiler-compare-runs/run.t6KepY/results.json) and
[without combining](../../build/compiler-compare-runs/run.tVUxre/results.json).
The ordinary [full-frame correctness run](../../build/compiler-compare-runs/run.5d94yd/results.json)
matches LLVM for all six modules, with full-frame fingerprints over all
76,800 graphics pixels.
All completed runs recover both badges to BOOTSEL.

The old legacy/full-CRC timing attempt exceeded the safety bound and is
excluded. The user approved timing version 2's smaller shared fingerprint;
no clock, repetitions, watchdog, tick bounds or collector deadline was relaxed.
A separate incomplete candidate collection timed out over USB and is excluded;
its recovered retry passed. This is not a claim of universal C correctness,
cold-XIP speed, physical display throughput or LLVM parity: owned/LLVM
ratios remain roughly **13.5-17.7x**.

### Final validation and remaining limits

- Full `test-oracle` and `test-metrics` pass: sanitizer/unit/default-policy,
  existing M02-M08 positive/negative/resource cases, LLVM verification,
  Cortex-M33 source/raw-IR and bidirectional/all-owned AAPCS execution,
  assembler/object/linker fixtures and 12 metrics tests.
- System-only-PATH `test-owned` passes. All 36 actual-default corpus
  transformations equal release under sanitizers and pass all 72 LLVM
  frontend/output boundaries.
- Every one of the 38 actual-default objects matches LLVM assembly/decoding
  in allocated bytes, flags/alignment, normalized relocations and public symbols.
  All 11 executable allocated bytes/flags/alignment/VMA/LMA/segment mappings
  and entry/ABI values match independent LLD. Metrics validates all UF2
  payloads, family IDs, block numbering and padding.
- Normal and version-2 host diagnostic sanitizer tests and all five protocol/
  mock-runner tests pass, including legacy decoding, mixed-version rejection,
  bounds, nonce freshness and compiler-default discovery.
- The broader `oracle.sh` stopped in its **LLVM-optimized-IR-to-C07** route:
  LLVM renders a CRC-table backslash as `\\`, which C07's existing byte-string
  reader rejects. This occurs before the owned C06 route, is unrelated to
  the default change, and is not silently skipped or fixed here. The actual
  supported owned outputs receive the independent 38-object/11-image checks
  above; unrestricted LLVM-optimized IR remains outside this release.

The [machine-readable release index](../build/c06-metrics/m09-release.json)
links the policy, measurements and physical evidence. M00-M09's bounded C06
release is complete under the explicitly revised size goal. P00's separately
authorized [measurement campaign](#p00-isolated-kernel-baseline) is complete;
P01's separately qualified [opt-in frame addressing](#p01-direct-thumb-2-frame-addressing)
is complete; [P02 local residency](#p02-block-local-register-residency) is also
implemented opt-in with documented runtime tradeoffs; [P03 cross-block
allocation](#p03-cross-block-register-allocation) is also implemented and
hardware-qualified opt-in. [P04 native scalar selection](#p04-native-scalar-selection)
is also hardware-qualified opt-in; [P05 word memory selection](#p05-word-memory-selection)
is also hardware-qualified opt-in; [P06 loop-invariant motion](#p06-loop-invariant-motion)
is hardware-qualified explicit-only with mixed runtime results.
P00-P06 are complete within their documented scopes, not implicitly enabled.

## Run the boundary tests

From the project root:

```sh
# Owned assertions, CLI/resource tests and sanitizer fixtures, no LLVM target tools:
PATH=/usr/bin:/bin:/usr/sbin:/sbin make -C badge-compiler test-owned CC=/usr/bin/cc
# Existing LLVM comparisons and Cortex-M33 differential execution:
make -C badge-compiler test-oracle LLVM_CLANG=clang LLVM_OPT=opt LLVM_LLD=ld.lld
```

`test-owned` explicitly selects `BADGE_TEST_MODE=owned`; it runs the same
owned fixtures without launching Clang or LLVM verification. `test` retains
oracle checks by default, and `test-oracle` enforces oracle mode. Required
oracle tools missing in that mode are errors, never a reason to silently
skip comparisons. Owned-only tests do not replace independent LLVM
verification or target execution evidence. The subsequent
[M02 implementation](#c06-m02-structured-views-and-owned-verification) adds
structured CFG/def-use/dominance analyses and owned subset verification.
The opt-in [M03 implementation](#c06-m03-worklist-dead-instruction-elimination),
[M04 implementation](#c06-m04-trivial-scalar-promotion) and
[M05 implementation](#c06-m05-pruned-scalar-mem2reg) add measured instruction
cleanup/scalar promotion; later optimizations remain pending.
The original plumbing refactor and M02 change none of the generated corpus
code, and M03-M05 leave the default pipeline unchanged.

Refactor validation on 2026-10-07 passed both test modes, the complete
LLVM oracle corpus and the system-only-PATH source-to-UF2 build. **All 182
saved pre-refactor artifacts** matched SHA-256 checks: frontend/C06 IR,
assembly, objects, all 11 ELF/bin/UF2 images and the three packaged vendor
payloads. Independent LLVM decoding matched all 38 objects; all 36 production
C06/C07 modules reproduced release outputs under ASan/UBSan. Cortex-M33
semantic/division and mixed/all-owned ABI execution passed, and all 11 UF2
payloads, block fields and padding were decoded and checked. Aggregate flash
remains 2,158,480 bytes and stack-producing assembly is unchanged. The
screen-test UF2 is identical to the physically tested image; no new flash
or production-default change was performed during the refactor.

```sh
bash badge-compiler/tools/oracle.sh
# No-Wi-Fi corpus only:
bash badge-compiler/tools/oracle.sh all
# Vendor-blob Wi-Fi host corpus only:
bash badge-compiler/tools/oracle.sh usb-wifi-udp
# Smaller initial corpus:
bash badge-compiler/tools/oracle.sh diagnostics
# Select another complete LLVM installation:
LLVM_BIN=/opt/homebrew/opt/llvm/bin bash badge-compiler/tools/oracle.sh
```

Requires Bash, Make, a native C compiler (`cc`), `shasum`, and a complete LLVM
installation containing Clang, LLD, opt, llc, llvm-as/dis, llvm-objcopy,
llvm-readobj, llvm-objdump, llvm-nm, and llvm-size. By default the installation
is the directory containing `clang` on PATH. LLD may be packaged separately
(as on Homebrew): it is selected from PATH, or with `LLD=/path/to/ld.lld`,
and its release must match Clang's. Use tools from one LLVM release;
IR syntax, intrinsics, attributes, and bitcode are version-dependent.
The focused `make -C badge-compiler test-oracle` suite additionally requires
Python 3 and `qemu-system-arm` with the `mps2-an505` machine; `QEMU_ARM` can
select its executable. The complete firmware capture does not run QEMU.

The existing [Makefile](../../Makefile) remains the source of truth for source
lists, defines, include paths, object order, and link flags. The harness forces
a serial rebuild into ignored `badge-compiler/build/oracle/`, substituting a
compiler wrapper but not editing production sources or recipes. Allowed
targets are deliberately limited to `all`, `diagnostics`, and `usb-wifi-udp`.
It never calls `make clean`.

Outputs include:

- `toolchain.txt`: exact tool paths and version output.
- `harness.sha256`, `targets.txt`, `build.log`: capture provenance.
- `vendor-assets.sha256`: vendor input, packaged asset and generated build-ID
  hashes when the vendor-blob Wi-Fi target is selected.
- `universe26/firmware/<object-name>.o.oracle/`: per-translation-unit artifacts.
- `universe26/firmware/<image-name>.elf.oracle/`: relinked ELF/map/binary artifacts.
- `universe26/firmware/<image-name>.elf.oracle/ours-cpp.uf2`: firmware produced
  through both owned stages; never automatically flashed.
- `universe26/firmware/<image-name>.elf.oracle/object.uf2`: firmware relinked
  from our C09-written objects; never automatically flashed.
- `universe26/firmware/<image-name>.elf.oracle/asm.uf2`: firmware assembled
  and object-written by C08/C09; never automatically flashed.
- `universe26/firmware/<image-name>.elf.oracle/linker.uf2`: firmware assembled,
  object-written, linked and image-encoded by C08-C12.
- `universe26/firmware/<image-name>.elf.oracle/owned-ir.uf2`: C01-C05 and
  C08-C12 owned; LLVM optimization/backend and `.S` preprocessing remain.
- `universe26/firmware/<image-name>.elf.oracle/backend.uf2`: C01-C05 and
  C07-C12 owned; only LLVM optimization and `.S` preprocessing remain.
- `universe26/firmware/<image-name>.elf.oracle/owned.uf2`: fully owned C01-C12
  and assembly preprocessing; LLVM is used only for independent validation.
- `universe26/uf2/`: baseline UF2 images made with our existing converter.
- `comparisons.tsv`: complete firmware binary comparison for each route.
- `objcopy-comparisons.tsv`: owned extraction versus LLVM for each baseline and
  re-entry executable; any mismatch fails the run.
- `cpp-comparisons.tsv`: normalized token comparison for each C translation
  unit, with its actual source and compile-time configuration.
- `parser-comparisons.tsv`: owned parser acceptance of Clang/owned `.i` files
  and structural AST comparison, excluding source-location differences.
- `sema-comparisons.tsv`: owned semantic acceptance of both `.i` files and
  typed-AST structural comparison, excluding source-location differences.
- `ir-comparisons.tsv`: LLVM verification of both owned frontend modules
  and matching owned IR after omitting only the input filename.
- `ir-linker-comparisons.tsv`: required image/public-symbol matches against
  LLVM assembly and unmerged LLD linking of the owned frontend's assembly.
- `codegen-comparisons.tsv`: owned target lowering and assembly of all C units.
- `codegen-linker-comparisons.tsv`: required image/public-symbol matches against
  independent LLVM assembly and unmerged LLD of our backend's assembly.
- `opt-comparisons.tsv`: owned C06 output verification and C07/C08 lowering.
- `opt-linker-comparisons.tsv`: required fully owned image/public-symbol matches
  against independent LLVM assembly and unmerged LLD.
- `object-comparisons.tsv`: C09 manifest capture and object writing for each
  C/assembly translation unit; `owned-object.txt` is LLVM's inspection.
- `asm-comparisons.tsv`: C08 assembly and C09 serialization for every C/assembly
  unit, producing `asm.o` directly from `native.s` or `preprocessed.s`.
- `linker-comparisons.tsv`: required linked-byte and public-symbol comparisons
  against the unmerged LLD reference; ordinary baseline differences remain
  visible in `comparisons.tsv`.
- `SUCCESS`: written only after the entire selected run passes. Previous
  artifacts may remain when targets change; only this run's log, targets,
  comparison table, and success marker describe the current run.

Each artifact directory has `commands.sh` recording shell-quoted invocations.
Output redirections are represented by the artifact names documented below,
not in that command log. Preprocessed sources retain line markers.

## Tested baseline: 2026-10-07

On the development Mac, Clang/LLVM and separately packaged Homebrew LLD
**22.1.8** completed a forced `all usb-wifi-udp` rebuild covering both scenarios:

| Check | Measured result |
| --- | --- |
| C translation units | 36, with preprocessing, AST, frontend/optimized IR, bitcode, assembly and object captures |
| Assembly translation units | 2, Picobin metadata and vendor-blob packaging, preprocessed and reassembled |
| Complete baseline firmware images | 10 no-Wi-Fi images + 1 vendor-blob Wi-Fi image, each ELF/bin/UF2 |
| Preprocessed C -> objects -> linked flash bytes | 11/11 identical |
| Optimized textual IR -> Clang -> linked flash bytes | 11/11 identical |
| Assembly -> objects -> linked flash bytes | 11/11 identical |
| Optimized textual IR -> llc -> linked flash bytes | 11/11 identical |
| Frontend textual IR -> standalone opt -> llc -> linked flash bytes | 11/11 identical |
| Owned C preprocessing | 36/36 normalized token streams identical to Clang; 11/11 linked flash binaries identical |
| Owned C parsing | 72/72 preprocessed inputs parsed; 36/36 structural AST pairs identical; syntax ASTs only, not code generation |
| Owned C semantic analysis | 72/72 inputs checked; 36/36 typed-AST pairs identical; 144 typed JSON documents independently validated; shared model feeds C05 |
| Owned C IR generation | 72/72 modules verified; 36/36 owned-IR pairs match; 11/11 owned frontend-to-UF2 builds; 38/38 new assembly objects independently match LLVM |
| IR fixtures | 53 positive, 16 negative, 29 mutations and CLI safety checks pass under sanitizers; reference/owned and bidirectional mixed-frontend ABI execution pass at O0/Oz |
| Owned target code generation | 36/36 optimized owned-IR inputs lower and assemble; all 38 objects, including discarded code, independently match LLVM bytes/fixups/public symbols; all 11 ELF/bin/UF2 images match independent LLVM assembly plus unmerged LLD |
| Backend fixtures | 33 positive, 27 negative, 113 mutations and CLI safety checks under sanitizers; all 36 corpus modules reproduce release assembly under sanitizers; Cortex-M33 differential execution, bidirectional mixed source-level ABI and all-owned ABI execution |
| Owned IR passes | 26 verified/assembled positive, 12 negative, 186 mutation fixtures under sanitizers; 36 corpus modules verify/reproduce release IR; full Cortex-M33 semantic/ABI and 19 wide division/remainder vectors |
| Fully owned build | 36 C + 2 assembly units, 11 ELF/bin/UF2 images without LLVM target tools; all objects and linked images independently match MC/unmerged LLD |
| Semantic fixtures | 54 positive, 84 negative, 11 unsupported and 25 mutation cases pass under ASan/UBSan; Clang differentials and 17/17 target-layout/constant probes match |
| Owned ELF object writing | 38/38 object models captured and rewritten; 11/11 relinked firmware images identical; vendor payloads unchanged |
| Object fixtures | ASan/UBSan API/manifest/ELF safety tests pass; LLVM fixture matches after LLD linking; handwritten C API object links; library-mode strict-warning build passes |
| Owned assembly/encoding | 38/38 units assembled through C08/C09; every allocated section/fixup/public symbol independently decoded by LLVM; 11/11 linked images identical |
| Assembler fixtures | 30 positive, 62 negative, 12 unsupported and 24 mutations pass under ASan/UBSan; branch-width thresholds, IT flags, aliases, incbin and atomic file safety checked; LLVM fixture matches |
| Owned linking | 11/11 images/public-symbol sets match unmerged LLD; independent LLVM decoding confirms allocated section/segment layouts and entry/ABI; original pooled baseline has 1 identical and 10 different images |
| Linker fixtures | Sanitizer safety/layout/resolution/range tests pass; 17 script rejections and ELF truncations/mutations; five C API/LLVM fixtures match LLD |
| Preprocessor fixtures | 41 positive, 42 negative and 37 mutation cases passed under ASan/UBSan; complex rescanning fixture matched Clang |
| IR verification / normalized bitcode round trips | Passed for all 36 C translation units |
| Vendor assets | Firmware/NVRAM/CLM object sections match packaged input bytes; 7 input/asset/generated-header hashes recorded |
| Owned ELF extraction | 187/187 outputs identical to LLVM: 11 baseline + 132 re-entry + 44 unmerged-reference executables; UF2 inputs produced by our tool |
| Extractor fixtures | ASan/UBSan tests passed, including 512 byte mutations; synthetic RAM-initializer/gap image also matched LLVM |
| Production override | Screen-test build with `RP2350_OBJCOPY=.../elf2bin` produced exactly the UF2 previously flashed and observed working; no additional flash |
| Combined owned stages | Screen-test UF2 from `badge-cpp` and `elf2bin` is identical to the image previously flashed and observed working; no additional flash |
| Unresolved symbols | None in any baseline or relinked image |
| Build warnings/errors | None in the successful run |
| Shell syntax, help, unsupported target and missing toolchain | Tested; `clean`, excluded `wifi-bringup` and absent LLVM installation rejected |
| Hardware execution | Not tested; no flash, reboot or USB access |

That is **132 complete-image comparisons against the ordinary baseline**:
the original eight routes remain 88/88 identical, while C10 adds one identical
and ten explicitly different images. C10 additionally passes **11/11 required
unmerged-LLD comparisons**, not just individual source-file compilation.
C05 adds one identical and ten different images, with independently tested
frontend semantics rather than an exact optimized-code requirement.
C07 adds eleven different images and eleven exact independent MC/unmerged-LLD
comparisons of its own assembly, plus target execution/ABI evidence.
C06 adds eleven fully owned images and eleven exact independent MC/unmerged-LLD
comparisons, with owned assembly preprocessing and a separate LLVM-free build.
These characterize the tested policies, not a requirement that our compiler
produce the same optimized bytes or code quality.

The original plumbing run (`all wifi-bringup`) covered 24 C units, 1 assembly
unit and 11 images; it has been superseded by the two-scenario corpus.
Before preserving textual IR use-list order, the IR routes
differed for `imu_demo` and `diagnostics` (register choices/instruction ordering
were among the observed differences). Enabling explicit preservation made
all 55 comparisons identical. This is why the capture configuration matters.

The bitcode experiment also showed that byte-identical serialization is too
strict: reassembly changed constant use-list ordering while canonical
disassembly remained identical. The harness tests that actual supported
contract instead of hiding a byte mismatch.

## Boundary inventory

| Boundary | Capture/tool | Can feed next stage? | Contract and replacement opportunity |
| --- | --- | --- | --- |
| Source + headers + definitions -> preprocessed C | Our `badge-cpp`, `ours.i`, compared with `clang -E`; Clang retains `-dM`/`-M` inspection | Yes: `clang -x cpp-output -c` | Implemented for the current C corpus; tokens, conditional inclusion, macros, target environment and locations are tested. Assembly preprocessing remains external. |
| C -> Clang AST | `-Xclang -ast-dump=json`, `ast.json` | Not through a supported JSON importer | Diagnostic reference for declarations/types/expressions, not our persistent interchange format. Clang AST/PCH serialization is implementation-specific. |
| Preprocessed C -> owned syntax AST | `badge-parse`, `ours.ast.json` / `preprocessed.ast.json` | Yes in memory: `badge-sema` shares the parser; no JSON importer | Implemented C03 subset with project-owned `badge-c-ast-v1` JSON; no type checking or LLVM AST import/export. |
| Owned syntax AST -> typed AST | `badge-sema`, `ours.typed.ast.json` / `preprocessed.typed.ast.json`; shared `c_semantic.h` | Yes: badge-ir shares the in-memory model | Implemented C04 subset: target types/layouts, name resolution, conversions and semantic constraints; `badge-c-typed-ast-v1`. |
| Owned typed AST -> frontend LLVM IR | `badge-ir`, `ours.frontend.ll` / `preprocessed.frontend.ll` | Yes: opt, llc, Clang, llvm-as | Implemented C05 subset; scalar/aggregate/control-flow/volatile/ABI lowering without invoking LLVM. |
| C -> frontend LLVM IR | `-S -emit-llvm -Xclang -disable-llvm-passes`, `frontend.ll` | Yes: opt, llvm-as, llc, Clang | Recommended initial output of our own C frontend. Generated with production `-Oz` attributes but LLVM passes disabled, rather than `-O0`/`optnone`. Not a literal unmodified parse tree. |
| LLVM text IR <-> bitcode | `llvm-as`, `llvm-dis`, `opt -S`, `frontend.bc`, `roundtrip.ll/.bc` | Yes | Tests an actual bitcode round trip using canonical disassembly equality, not serialized byte equality. Prefer textual IR for our first emitter; delegate bitcode serialization initially. |
| Frontend IR -> optimized IR | Clang's `optimized.ll`; separately `opt -passes='default<Oz>'`, `opt.ll` | Yes: llc or Clang | Optimization replacement boundary. Standalone opt is not assumed to reproduce Clang's full target-aware pass pipeline. |
| Optimized IR -> Arm assembly | `clang -S`, `native.s` | Yes: `badge-asm` or `clang -x assembler -c` | Backend replacement boundary: instruction selection, calling convention, register allocation, stack frames, instruction encoding preparation. |
| Optimized IR -> object directly | `llc -filetype=obj` and Clang consuming `-x ir` | Yes: LLD | Alternative backend/object-writer boundary. llc is tested independently, not assumed to share every Clang driver setting. |
| Assembly source -> preprocessed assembly | `clang -E` on `.S`, `preprocessed.s` | Yes: `badge-asm` or integrated assembler | Picobin metadata and vendor-blob `.incbin` packaging follow this route; they do not pass through C or LLVM IR. |
| Arm assembly -> relocatable ELF | Our `badge-asm` -> `asm.o`; Clang integrated assembler as reference | Yes: LLD | Implemented C08/C09 subset, preserving encoded bytes, sections, symbols, attributes, Thumb marking and fixups. The `asm` route tests complete firmware relinks. |
| Encoded sections + symbols + fixups -> relocatable ELF | Our `badge-object`, C API or `object.bobj` -> `object.o`; `owned-object.txt` inspected by LLVM | Yes: LLD | Implemented C09 subset; called directly by C08. The independent `object` route tests reference-object replay/relinks. |
| Objects + linker script -> executable ELF | Our `badge-link`; Clang/LLD reference; `.elf`, `.map`, `linker-executable.txt` | Yes: our elf2bin or llvm-objcopy | Implemented C10 subset: script parsing, symbol resolution, GC, VMA/LMA, fixups, entry, linker-defined symbols and assertions; `linker` route uses C08-C12. |
| Executable ELF -> flash bytes | Our `elf2bin -O binary`, checked against `llvm-objcopy`; `.bin` | Yes: our bin2uf2 | Already replaced for our supported ELF subset, including load addresses, gaps, and initialized RAM data stored in flash. |
| Flash bytes -> UF2 | Existing project-owned `bin2uf2` | Yes: ROM BOOTSEL loader | Already ours. 512-byte blocks, 256-byte payloads, flash base `0x10000000`, family ID `0xe48bff59`. |
| UF2 -> running application | Existing ROM bootloader and startup | Hardware only | Deliberately not exercised by these offline tests. Physical validation is a separate authorized step. |

The harness verifies both IR files and standalone opt output, checks normalized
IR after bitcode reassembly, assembles extracted assembly, and relinks **all objects**
through ten routes: oracle-preprocessed C, optimized IR consumed by Clang,
assembly, optimized IR consumed by llc, standalone opt followed by llc, and
our preprocessed C, owned object writing, owned assembly/encoding, and owned
linking, and owned IR generation followed by LLVM optimization/backend and
owned downstream tools. For `.S`
inputs the LLVM re-entry routes reuse the preprocessed assembly object;
the object and asm routes rewrite or assemble it independently. These are not
ten independent C/IR tests of that assembly source. The vendor firmware is opaque
data and must remain unchanged through all routes.

Preprocessed-C, assembly, object and asm routes must reproduce the baseline flash binary
byte for byte. Other routes must link with no unresolved symbols and record
`identical` or `different`; a difference is not silently treated as equality
or evidence of behavioral correctness. ELF bytes alone are not compared
because source-file symbols and non-loadable metadata can differ. The flash
image comparison includes code, constants, Picobin metadata, and `.data`
initializers.

These byte-comparison checks validate the LLVM capture harness; they are not
acceptance gates for a future compiler producing different valid code.

Clang IR capture explicitly uses `-Xclang -emit-llvm-uselists`. LLVM use-list
order is not a C semantic requirement, but losing it can change instruction
selection/scheduling and register allocation on reimport. LLVM 22's ordinary
`llvm-dis` output is captured as `disassembled.ll`; the round-trip test uses
`opt -S -preserve-ll-uselistorder` to produce `roundtrip.ll`. Standalone opt
output also requests preservation. Bitcode reassembly can still reorder
constant use-lists, so the test compares ordinary disassembly after removing
only the filename-dependent ModuleID comment, not bitcode bytes or order.
These details matter for byte-level oracle comparisons, not for imposing
LLVM's internal ordering on our compiler.

## What we already own

- C applications and hardware drivers.
- [Startup/vector table](../../src/arch/rp2350/start.c), including data copying,
  BSS clearing and the call to `bare_main`.
- [Picobin assembly metadata](../../src/arch/rp2350/picobin.S).
- [Flash linker script](../../src/arch/rp2350/memmap_flash.ld) and the separate
  report-image layout.
- [UF2 encoder](../../src/host/tools/bin2uf2.c).
- [ELF-to-binary extractor](../src/elf2bin.c), now used by the oracle build.
- [C preprocessor](../src/badge_cpp.c), exercised across the full C corpus.
- [Shared lexer](../src/c_lexer.h) and [syntax parser](../src/badge_parse.c), exercised on
  both preprocessors' outputs.
- [Semantic checker](../src/badge_sema.c), producing target-aware typed ASTs.
- [IR emitter](../src/badge_ir.c), connecting those trees to the owned firmware route.
- [Thumb assembler/encoder](../src/badge_asm.c) and [ELF object writer](../src/badge_object.c),
  exercised across every captured assembly input.
- [Linker](../src/badge_link.c), parsing both production layouts and validated against
  LLD for the initial unmerged data policy.
- Host-side tests for application logic.

We own the metadata source, an assembler, and a linker for their supported
subsets, as well as the linker-script rules. The boot ROM and remaining LLVM
optimization/backend stages are external. The project's
[stdint header](../../src/shared/stdint.h) depends on compiler predefined type
macros, so even a no-libc frontend needs the right target environment.

## Implementation plan and acceptance gates

### 0. Establish reproducible reference artifacts (this change)

Capture the boundaries above using unchanged production flags. Record actual
round-trip results, versions, commands, and failures. Keep generated artifacts
ignored; retain scripts and the test record. Capture both scenarios with
explicit generated-header and vendor-data provenance. Check the embedded
firmware/NVRAM/CLM object sections byte-for-byte against the packaged inputs.
Do not add source-radio experiments to this corpus.

### 1. Define the supported C/target contract

Inventory features from real translation units and add small focused fixtures.
Use C11 plus the extensions actually needed by this code, not a claim of all
C11 support. Fix Arm EABI/AAPCS, little endian, type sizes/alignment, unsigned
plain char as in the current driver, pointer width, integer promotions,
struct/union layout, bit-fields, enum behavior, and floating-point ABI.
Record CPU features and LLVM data layout from reference artifacts.

Pay particular attention to volatile MMIO, integer/pointer conversions,
64-bit operations, static initializers, arrays, function pointers,
`__attribute__` sections/alignment/used/noreturn, and inline assembly
(`wfi`, `nop`, barriers, and operand constraints). No libc does not imply no
compiler builtins or helper calls: track every generated external reference.

Gate: each required feature has a positive fixture and invalid-input
diagnostics where applicable. Identify unsupported features explicitly.

### 2. Own lexing and preprocessing

Implemented for the current corpus by `badge-cpp`. Extend its explicit input
contract as needed, keeping targeted negative and differential tests.
Compare normalized token streams, not raw
whitespace; keep separate tests for spelling/locations and macro corner cases.
Feed our `.i` into Clang to keep the rest of the pipeline unchanged.

Gate: fixtures match oracle preprocessing semantics; all corpus `.i` files
compile and relink, and preserve required behavior. Binary comparison is a
diagnostic rather than a general acceptance criterion. Invalid input fails
explicitly.

### 3. Own parsing, semantic analysis and LLVM IR generation

Parsing, semantic analysis and IR generation are implemented as C03-C05 by
`badge-parse`, `badge-sema` and `badge-ir`, accepting `.i` from either
preprocessor. LLVM still optimizes and generates Arm assembly; our tools
then encode, link and package the firmware.
Use Clang AST dumps as an investigation aid, not as a format to reproduce.

Gate: LLVM verifier passes; ABI/layout fixtures pass; application logic tests
and differential execution agree on defined behavior. Compare normalized IR
and object structure as diagnostics, not an exact-text correctness criterion.
Then combine our existing preprocessor and the new frontend.

### 4. Own Arm assembly and ELF object writing

C09 object writing and C08 assembly/encoding are implemented by `badge-object`
and `badge-asm`, with standalone API/instruction fixtures and complete corpus
relink comparisons. C08 feeds encoded sections/fixups directly into C09 before
we own the backend. Extend their explicit contracts when backend needs expand,
with malformed and boundary-value fixtures.

Gate: instruction encodings satisfy the Arm ISA; section/symbol/relocation
contracts agree; LLD accepts mixed LLVM/our objects; linked programs preserve
behavior. Use LLVM encoding comparisons to investigate differences, without
requiring identical section ordering or equivalent instruction encodings.
Preserve vendor blob bytes exactly, including `.incbin` alignment and size.

### 5. Own Cortex-M33 code generation

Implemented by `badge-codegen` for the contract above, with our C08/C09
assembler/object writer. Continue improving register allocation, instruction
selection and frame quality while retaining the target execution/ABI tests.

Gate: differential execution and ABI interoperability tests pass, including
volatile access width/order, stack alignment and callee-saved registers.
Exact instruction sequences are not required for a different valid backend.
Use a simulator/emulator only after verifying its RP2350/Arm coverage; MMIO
requires a model or later hardware tests.

### 6. Own linking and image extraction

Image extraction is now implemented by `elf2bin` for our supported executable
ELF subset. Retain its focused and LLVM differential tests as the compiler and
linker evolve; expand its contract explicitly when new required ELF features
appear. C10 linking is implemented by `badge-link` for the documented subset.

Known-good LLVM and owned ELF objects now feed our linker. It parses the actual
scripts and implements relocations, Thumb symbol semantics, KEEP/GC behavior,
load/run addresses, stack/vector placement and overflow checks. Extend the
explicit contract when needed; do not quietly ignore new script features.

Gate: boot metadata, memory layout, load/run addresses and relocation effects
satisfy the target contract, with structural ELF checks, behavioral tests and
negative tests for undefined symbols, unsupported relocations and region
overflow. LLD/objcopy comparisons are diagnostics; valid alternative layouts
are allowed within the required memory/boot constraints. Preserve opaque
vendor assets exactly. Reuse our existing UF2 converter.

### 7. Optimize and validate end to end

Keep LLVM optimization initially if useful; our first code generator need not
optimize at all. Add semantic differential tests and the quality metrics above,
then implement optimizations aimed at this codebase/platform. Compare against
fixed LLVM configurations (including size- and speed-oriented builds), with
the same inputs and measurement conditions. Investigate opportunities to
outperform them on selected workloads without sacrificing correctness.
Separate host logic tests, simulated target execution and physical badge
tests. No undefined C behavior should be treated as a required LLVM result.

Gate: a fully owned selected pipeline passes the corpus and focused fixtures,
then an explicitly authorized screen/diagnostic hardware smoke test with
BOOTSEL recovery. Never make hardware flashing part of the default oracle run.

Recommended next compiler milestone: **reduce stack and code costs** with
C06 memory promotion/dead-code passes and C07 register allocation. The
documented pipeline now has owned C01-C12/A01-A02 replacements; LLVM remains
an oracle. Hardware smoke tests require explicit authorization and stack/
timing assessment first.

## Oracle limitations

LLVM is a useful reference, not a proof of correctness. Byte identity proves
that a particular round trip preserved a particular image, not that the C
program or compiler is universally correct. A different valid compiler can
produce different bytes. IR/AST/MIR diagnostics are version-sensitive; LLVM
MIR and pass-internal dumps are deferred rather than promised as stable APIs.
Focused frontend suites include negative compiler fixtures. The application
harness does not execute complete badge applications, decode UF2 or perform
physical testing. The separate C07 fixtures now perform Cortex-M33 differential
execution, without an RP2350 peripheral model.
