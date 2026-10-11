# Maze-driven general compiler optimization

## Objective

Make the actual tilt maze faster with the owned compiler while improving
generated code across the freestanding corpus. Do not recognize maze function
names, rewrite the game to accommodate the compiler, add dependencies to
firmware, or optimize only a synthetic substitute for the application.

LLVM remains an independent correctness and performance reference, not a
promise of identical instructions or immediate performance parity.

## Starting point

The preceding [general compiler improvements](implementation.md#current-default-promotion-and-zero-storage)
fixed zero-storage placement and promoted qualified direct/global/thumb/word
policies. Preserve the archived control images and explicit policies. Z01's
subsequent scalar-default promotion is recorded below.

| Instrumented maze | LLVM | Owned control |
| --- | ---: | ---: |
| Flash payload bytes | 9,272 | 58,472 |
| User-reported RAM draw ticks | 7,719 | 56,202 |
| User-reported physics ticks/step | 377 | 5,403 |

These are one fixed badge pairing, not role-swapped maze measurements.
Raw ticks exclude display transfers and IMU access. The original owned
drawing result was 118,417 ticks; the first improvements made it 2.11 times
faster. See [saved results](../../build/maze-compiler-improvements.json).

## Evidence before implementation

Inspection of the current owned instrumented maze's saved assembly and linked
ELF gives:

| Function | Assembly instructions | Register moves | Private frame accesses |
| --- | ---: | ---: | ---: |
| `collide_wall` | 705 | 145 | 172 |
| `physics_step` | 392 | 95 | 52 |
| `draw_board` | 856 | 120 | 157 |
| `generate_maze` | 2,569 | 417 | 638 |

These are static counts, not dynamic profiles or executed-instruction totals.
Instructions count mnemonic lines; moves count register-to-register `mov`;
frame accesses count operands based on `r7`. Inline literals and branches
also contribute overhead. LLVM inlines some helpers, so individual function
sizes cannot always be compared directly.

For example, owned physics emits `mov r0, r8; mov r2, r10;
add.w r0, r0, r2; mov r8, r0`. The allocated inputs and destination could
instead use `add.w r8, r8, r10`. This is a general backend issue, independent
of application names or constants.

## Staged implementation

| Stage | Scope | Dependency | Status |
| --- | --- | --- | --- |
| Z00 | Archive current evidence; distinguish static counts from runtime; compare hot application assembly | None | Initial static analysis complete; finer dynamic attribution pending |
| Z01 | Qualified allocated-register operand/result selection for unflagged i32 arithmetic, legal immediates and existing multiply-add/div-rem patterns | Z00 | Done; promoted to default |
| Z02 | Improve allocation/scratch-register integration, available register use, copy/PHI coalescing and pressure-aware spills | Z01 evidence | Done; qualified `global-flex` promoted |
| Z03 | Investigate narrow scalar/boolean residency and lifetime-based private stack-slot reuse | Z00; allocation safety | Done; qualified `global-compact` promoted |
| Z04 | Revisit loop-invariant work, induction/address strength reduction, expression reuse and bounded inlining | Z02 pressure measurements | Done; scalar cleanup qualified and promoted; loop-cost remains opt-in |
| Z05 | Compact legal Thumb forms, redundant branches/moves and literal-pool/layout improvements | Measured reached-code candidates | Done; qualified branch layout promoted |
| Z06 | Profile host compiler time/memory; improve measured lookup/allocation bottlenecks; close stack-bound and feature gaps | Independent of runtime stages | Done; host optimizations and conservative stack reporting qualified |

Each stage is a bounded change with its own control and validation. Pending
stages are research directions, not commitments to enable every technique.
Existing mixed-result CSE/SCCP/loop passes remain explicit-only.

The [datasheet-driven follow-up plan](datasheet-plan.md) separately proposes
source extraction, verified target knowledge, hardware calibration and
cost-informed compiler candidates. It does not change the qualified Z defaults.

### Z06 host and stack-analysis contract

Z06 is a host-side improvement, not another target selector or C06 pass.
The qualified `direct/global-compact/thumb-layout/word` tuple and optimizer
pipeline remain unchanged. Firmware has no new dependency or source change.

An in-process backend harness measures compiler API intervals using monotonic
time and `getrusage`. Input reads, output comparisons/hashing and output writes
are outside those intervals. Each module warms once; five batches alternate
control/candidate process order, compiling each of the 37 qualified inputs
50 times. The OS RSS field is a **cumulative process peak**, not a
per-function allocation count or isolated per-module peak. This measures C07,
not whole source-to-UF2 build speed or badge execution speed.

Sampling repeated maze compilation reached string comparisons, assembly
formatting and allocator/free paths. The selected broad changes are:

- Per-function, open-addressed SSA-name indexes for definition/lookup, retaining
  the original linked-list order used by allocation and emission. Growth
  keeps the table at most half full, uses the existing allocation budget,
  and rejects duplicate definitions before allocating.
- Size the lexer token array by `min(input_bytes, 1,000,000)`. Each token
  consumes at least one input byte, so this preserves the original million-
  token ceiling without reserving that array for every small input.
- Format assembly directly into available output capacity; only retry when
  growth is necessary. The 64 MiB output ceiling, diagnostics, failure cleanup
  and public output ownership remain intact.

There is no arena, target-specific fixture recognition, allocation-priority
change, instruction change or new C language feature in this bounded stage.
The profiling tools use the existing host C/POSIX/Node environment; they are
not linked into firmware.

```sh
make -C badge-compiler build/profile-codegen
# Archive a control executable before changing compiler sources.
node badge-compiler/tools/profile-host.mjs \
  badge-compiler/build/z06-control/profile-codegen \
  badge-compiler/build/profile-codegen \
  build/maze-opt-z05-corpus/universe26/firmware \
  build/maze-opt-z06-host-profile.json
```

Five complete batches reduce aggregate C07 CPU from **2,526,434 to 1,756,783
microseconds (-30.46%)** and process peak RSS from **23,691,264 to 11,075,584
bytes (-53.25%)**. These CPU totals cover 50 compilations per module, not a
single compilation. All 37 module CPU medians improve; generated output
lengths/fingerprints agree in every batch. Exact release/sanitizer assembly
checks, not those fingerprints alone, gate target equivalence.

Stack analysis now computes strongly connected components iteratively rather
than memoizing a recursive DFS's partially unrolled cycles. Recursion reasons
and known-prefix results are independent of function order. Cyclic prefixes
stop before an intra-component call; recursive bounds remain `null`. A
3,000-function acyclic chain is analyzed without the previous 1,024-level
recursion limit. Duplicate qualified identities fail explicitly.

Reports additionally distinguish:

- `max_known_direct_chain_bytes`: the largest *complete* bound among known
  functions, even if another linked function remains unknown.
- `entry_reachable_functions` and `entry_reachable_unknown_functions`: modeled
  direct-call reachability, with `entry_reachability_complete` false when
  unknown instructions or opaque calls prevent complete reachability.
- `recursive_components`: canonical cyclic function identities.

All twelve linked corpus graphs agree after reversing function order.
The maze has a **1,320-byte largest completely known direct chain**, but its
entry bound remains **unknown**, propagated from the indirect callback in
`display.o:init_delay`. Five maze functions inherit that uncertainty.
The hello image has an 88-byte direct entry bound. These are software direct-
call results, not interrupt-inclusive stack bounds; callbacks, arbitrary
assembly and interrupt nesting are not guessed away. General interprocedural
callback-target analysis and additional C-language features remain future work.

#### Z06 final qualification

Two further five-batch campaigns reproduced the host improvement:

| Campaign | Control/candidate CPU (microseconds) | CPU reduction | Process peak RSS reduction |
| --- | --- | --- | --- |
| Initial | 2,526,434 / 1,756,783 | 30.46% | 53.25% |
| Hardened wrapper repeat | 2,737,081 / 1,903,808 | 30.44% | 53.31% |
| Final wrapper | 2,711,234 / 1,880,213 | 30.65% | 52.39% |

All 37 module CPU medians improved in each campaign. The reusable wrapper
checks input/executable SHA-256 provenance after each capture, rejects report
aliases and symlinks, and publishes complete reports through atomic replacement.
Malformed, failed or drifting workers preserve the previous report.

Qualification passed the 80-policy native sanitizer matrix (33 positive,
27 negative and 113 mutation cases), both optimized execution routes, raw
PHIs, mixed ABI, real maze and fast-I/O oracles. All 45 corpus/instrumented
modules retain exact qualified IR and assembly under implicit release and
sanitizer defaults; their metrics reports agree exactly. Explicit Z04 controls
remain unchanged. The 27 driver/report tests and four profiling tests pass,
including native profiler argument/error/row checks and output alias protection.
The profiling tests are included in the normal compiler test target.

A fresh source-to-UF2 maze build is byte-identical to the flashed Z05 image:
SHA-256 `3c5ab4acb65ac119b556bf108b75943583223dbb175d883d8f17fec07bf48da9`.
There is no target speedup claim, default promotion or hardware reflash in Z06.

Evidence: [final qualification](../../build/maze-opt-z06-results.json),
[initial host measurements](../../build/maze-opt-z06-host-profile.json),
[repeat measurements](../../build/maze-opt-z06-host-profile-repeat.json),
[final measurements](../../build/maze-opt-z06-host-profile-final.json)
and [linked stack graphs](../../build/maze-opt-z06-stacks.json).

### Z05 candidate contract

The qualified and promoted `thumb-layout` policy inherits Z01's allocated-register scalar selection and removes
redundant wide branches at the structured CFG-emission boundary. The current
default is now `thumb-layout`; use `thumb-registers` explicitly for a Z04
control. No instruction-width or literal-pool changes are included.

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-layout \
  bash badge-compiler/scripts/build-owned.sh
```

Layout is enabled only with global register residency, and disabled for an
entire function containing a `call`, `volatile`, inline `asm`, or `inttoptr`
token. This conservatively retains existing lowering for opaque operations,
timing-sensitive hardware code and local-register flushes. There are no
application names, workload constants or assembly-text peepholes.

- An unconditional edge may fall through to the physical next block after
  completing all PHI captures, normalization and parallel transfers.
- Only the last emitted conditional arm can fall through. If the true target
  is physically next and the false target has no PHIs, invert the local
  condition and emit the false edge first. Otherwise retain the original arm
  order, allowing only the final false edge to fall through.
- Switch case edges retain their jumps; only the final default edge can
  fall through.
- The final block's final return can fall through to the common epilogue.
  Earlier returns and non-next targets retain their jumps.

Conditional branches still target local veneers, not distant CFG labels.
The no-false-PHI inversion guard avoids increasing the distance to that
veneer. Branches around inline literal data remain untouched. Fallthrough
is restricted to final instructions, not merely blocks with matching names.
Global finish hooks emit only bookkeeping comments in these eligible
functions; inline-assembly restore instructions are excluded by the guard.

Each function reports `@ branch-layout: active N fallthroughs N inversions N`.
Tests compare actual emitted `b.w` counts with those counters across frame,
residency and memory policies, and require guarded functions to match the
old assembly exactly after removing the new comment. Qualification compares
identical optimized IR, unchanged explicit/default controls, sanitizer output,
stack metadata and both independent execution routes. Smaller binaries are
not accepted as proof of faster execution; hardware qualification and
playable-maze confirmation precede any default promotion.

### Z05 qualification results

The native backend suite now covers 80 frame/residency/scalar/memory policy
combinations, 33 positive, 27 negative and 113 mutation cases. The focused
`build/badge_codegen_test --layout` suite additionally checks eight CFG
shapes, actual removed branch counts, same-target and wide PHIs, and exact
unchanged assembly for four guarded operation classes. Both optimizer/backend
execution routes, raw narrow/wide/parallel PHIs, mixed ABI, actual maze and
fast-I/O differential execution pass. Pipeline/build/metrics/comparison-driver
tests pass, including explicit layout-policy provenance.

The fresh twelve-image corpus has 37 modules and 552 functions; 159 functions
enable layout, removing **802** actual `b.w` instructions and inverting **196**
conditional layouts. Eight instrumented modules also pass release/sanitizer
agreement, LLVM IR verification, identical-input controls and exact archived
Z04 assembly preservation. IR comparisons normalize only `source_filename`
and the three established USB build-ID call operands. Per-function frames,
saves, calls, outgoing arguments and unknown-path metadata are unchanged:
local-frame sum **18,536 bytes**, largest individual frame including saves
**1,568 bytes**. These are not call-chain stack bounds.

| Maze payload | Z04 control | Z05 candidate | Change |
| --- | ---: | ---: | ---: |
| Uninstrumented binary | 42,024 | 41,480 | -544 bytes (-1.29%) |
| Instrumented binary | 44,280 | 43,736 | -544 bytes (-1.23%) |
| Instrumented UF2 | 88,576 | 87,552 | -1,024 bytes |

Instrumented data/BSS remain 16 / 160,448 bytes. All twelve corpus images
are no larger than their controls.

A fresh Z04 control and two complete Z05 campaigns use seed **3276377845**
and both physical roles. All results and fingerprints match; no failed or
excluded campaign exists. Changes below are candidate/control owned raw-tick
median ratios paired by physical role, not percentage speedups.

| Workload | First change | Repeated change |
| --- | ---: | ---: |
| Arithmetic | -1.445% | -1.445% |
| Strings | -4.776% | -4.770% |
| Memory | -8.785% | -8.809% |
| Control flow | -5.788% | -5.786% |
| Cross-file ABI | -1.988% | -1.990% |
| Graphics | -6.228% | -6.229% |
| Arithmetic kernel | -13.522% | -13.581% |
| Branch/PHI kernel | -12.016% | -12.031% |
| Cross-file call kernel | -8.187% | -8.207% |
| Unsigned div/rem kernel | -4.210% | -4.202% |
| Signed div/rem kernel | -7.007% | -6.977% |
| Ordinary memory kernel | -7.086% | -7.086% |

Maximum absolute LLVM-control drift is **0.0714%**. Both games were restored
sequentially with flash verification: LLVM on `9BBF88DD17D66E7D`, Z05 on
`08DA26F86E57F5EA`. The user confirmed both games work normally and explicitly
approved promotion to the shared CLI/API scalar default.

| User-confirmed playable maze | Z04 | Z05 | Change |
| --- | ---: | ---: | ---: |
| Owned RAM drawing ticks | 35,198 | 31,342 | -10.95% |
| Owned physics ticks/step | 4,268 | 4,240 | -0.66% |
| LLVM RAM drawing ticks | 7,718 | 7,718 | Unchanged |
| LLVM physics ticks/step | 377 | 377 | Unchanged |

The promoted policy tuple is `direct/global-compact/thumb-layout/word`.
C06 remains `mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`.
Post-promotion checks compare implicit release/sanitizer output with all 45
qualified modules, preserve explicit Z04 controls, exercise CLI/API agreement
and checked metrics, and require a fresh implicit-default maze UF2 to equal
the flashed candidate byte for byte. No game-source or firmware dependency
change was required.

Evidence: [Z05 results](../../build/maze-opt-z05-results.json),
[fresh control](../../build/compiler-compare-runs/run.8iHerx/results.json),
[first paired comparison](../../build/compiler-compare-runs/run.ByTn36/z05-comparison.json),
and [repeated paired comparison](../../build/compiler-compare-runs/run.gWxb0o/z05-comparison.json).

### Z04 candidate contract

The qualified and promoted Z04 pass is `scalar-cleanup`, which shares the existing
typed, worklist-based instruction combiner and adds boolean expression
forwarding. The previous `instruction-combine` pass stays unchanged as an
explicit control. No application-name, maze-constant or helper-name rules
are introduced.

The shared CLI/API default is now
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`.
Use the old `instruction-combine` profile explicitly for a Z03 control.

```sh
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division \
  bash badge-compiler/scripts/build-owned.sh
```

An equality/nonzero test of an extended i1 value can reuse that original
boolean: `icmp ne (zext/sext i1 %p), 0`, `icmp eq (zext i1 %p), 1`, and
`icmp eq (sext i1 %p), -1` all forward `%p`. Either comparison operand order
is supported, with literals interpreted at the actual destination width
(i8/i16/i32/i64). Non-boolean sources, nonidentity tests, metadata and direct
undef/poison literals retain existing lowering. The shared alias worklist
repairs every typed use; existing DCE removes an extension only when its
other users no longer need it. Volatile reads/writes, calls, CFG, PHIs, ABI
and trap/poison semantics are not reordered or newly speculated.

The 50M-unit work and 128 MiB allocation bounds remain explicit failures.
The existing combiner suite now verifies 308 positive/negative, LLVM-valid,
idempotent cases across both passes, including signed/unsigned extensions,
both operand orders, all supported destination widths, shared wide users,
volatile input, chains and resource/invalid-input failures. Existing target
IR fixtures also execute narrow-to-wide boolean chains and a retained
volatile i64 user independently against LLVM. Both backend routes, mixed
ABI, actual maze behavior and fast-I/O boundaries pass.

Across 37 fresh C modules and 552 functions, the candidate forwards 2,202
boolean tests in 35 modules. All twelve images build, with unchanged data
and BSS. Release/sanitizer optimizer and backend output agree for the corpus
and all eight instrumented maze modules. The old implicit default matches
the archived Z03 control on identical input, apart from the three recorded
USB provenance constants. Standalone cleanup is idempotent on all corpus
inputs; the complete multi-pass pipeline is not claimed to be a fixed point.
Calls, outgoing argument sizes and unknown stack paths remain unchanged.
The static local-frame sum stays 18,536 bytes; the largest individual frame
including saves stays 1,568 bytes. Neither figure bounds dynamic stack use.

The uninstrumented payload shrinks from 48,672 to 42,024 bytes (13.66%).
The instrumented payload shrinks from 51,124 to 44,280 bytes. These are size
results, not runtime proxies.

Two complete fixed-seed (`3276377845`), role-swapped hardware campaigns match
every LLVM fingerprint and improve all twelve workloads:

| Workload | First owned-tick change | Repeat |
| --- | ---: | ---: |
| Arithmetic module | -2.908% | -2.908% |
| Strings | -18.466% | -18.471% |
| Memory module | -20.696% | -20.699% |
| Control flow | -24.126% | -24.130% |
| Cross-file ABI | -3.016% | -3.006% |
| Graphics | -21.196% | -21.194% |
| Arithmetic kernel | -21.602% | -21.602% |
| Branch/PHI | -32.096% | -32.096% |
| Cross-file call kernel | -13.245% | -13.253% |
| Unsigned div/rem | -12.030% | -12.051% |
| Signed div/rem | -15.164% | -15.164% |
| Ordinary memory | -22.316% | -22.316% |

Control: `../../build/compiler-compare-runs/run.j5FDDk/results.json`.
Qualified comparisons:
`../../build/compiler-compare-runs/run.R0lcFJ/z04-comparison.json` and
`../../build/compiler-compare-runs/run.TZFkoi/z04-comparison.json`.
Maximum absolute LLVM drift is 0.142%; most workloads show none.
The initial `run.UxrPCu` collector exceeded the unchanged 60-second deadline
after complete matching payloads; preserve it but exclude it from qualification.

LLVM and candidate maze images have been sequentially flashed and verified.
The user confirmed both games work normally and approved promotion.
Owned RAM drawing falls from 49,159 to 35,198 ticks; physics falls from
4,930 to 4,268 ticks per step. LLVM remains 7,718 / 377. These are raw
startup benchmark ticks, not FPS, panel-transfer or IMU timings. Current evidence
is in [the Z04 report](../../build/maze-opt-z04-results.json).

Post-promotion CLI/API sanitizer checks, both standalone pass suites and
18 metrics/driver/report tests pass. Implicit release/sanitizer optimizer,
backend and metrics output reproduce all 37 corpus plus eight instrumented
maze modules; explicit Z03 controls remain exact. A fresh source-to-UF2
build with all optimization/codegen override variables unset produces the
exact flashed candidate image.

### Z04 loop-motion candidate (not promoted)

The first Z04 candidate was the opt-in `loop-cost` pass, not wholesale
promotion of the old loop/CSE/SCCP pipelines. The actual graphics IR
recomputes an outer-column multiplication for each inner-loop pixel.
The existing natural-loop, dominator, preheader, speculation and resource
checks are shared with `loop-invariant`; that older pass retains its behavior.

```sh
BADGE_OPT_PASSES=mem2reg,instruction-combine,constant-fold,loop-cost,aggregate-memory,wide-division \
  bash badge-compiler/scripts/build-owned.sh
```

Only unflagged i32 multiplication by a nontrivial constant and proved-safe
constant division/remainder are cost candidates. Zero, unit and positive/
negative power-of-two multipliers, and power-of-two divisor magnitudes, are
excluded. Variable division, signed overflow cases, poison/undef literals,
loads, stores, calls, assembly and metadata remain ineligible.
Operands must already dominate an existing unique preheader; the result
must have a use in the loop. No CFG, PHI, memory-order or ABI changes occur.

To limit extra cross-loop live ranges, a loop is transformed only when it
has exactly one available costly candidate and no existing costly result
live from a block dominating its preheader. Existing outside-loop work,
including motion to an outer preheader, counts toward the budget,
so repeating the pass does not progressively hoist dependency chains.
Nested loops may move the same result farther outward after the inner move.
This conservative bound is a profitability heuristic, not a speed proof
or a complete register-pressure model. New scans charge the existing
50-million-unit work limit; allocation remains bounded to 128 MiB.

Its preserved offline/hardware evidence is in
[the loop-cost report](../../build/maze-opt-z04-loop-cost-results.json).
The expanded existing native loop suite verifies 141 idempotent LLVM IR
fixtures, including systematic odd constants, dependency chains, live-range
budget rejection, narrow fallback, volatile inputs and atomic resource/
invalid-input failures. Both backend optimizer routes, mixed ABI and the
actual maze's fingerprint/rendering/hazards/progression execute successfully.

Fresh control and candidate builds cover 37 C modules, 552 functions and
all twelve corpus images. Exactly three original expressions move, in
graphics and diagnostics. Release/sanitizer IR and backend output agree;
the implicit default reproduces the unchanged Z03 control. The three exact
USB protocol build-ID operands are recorded separately, not broadly masked.
Identical-input frame/call comparisons preserve calls, outgoing argument
sizes and unknown stack paths.

The static local-frame sum grows from 18,536 to 18,552 bytes; the rectangle
and text functions each gain eight bytes. The largest individual frame
including saves remains 1,568 bytes. Neither figure bounds dynamic
call-chain stack use. The uninstrumented maze payload grows from 48,672
to 48,696 bytes; data and BSS are unchanged. Two complete campaigns match all results but give no repeatable general
speedup; graphics regresses 0.143/0.144%. Actual rectangle assembly spills
the hoisted result and loses multiply-add fusion. Do not promote this
pipeline. Keep `loop-cost` explicit-only for further pressure/profitability
research; the final candidate above does not include it.

### Z01 contract

Use `--scalar-selection=thumb-registers` or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-registers`, now the shared CLI/API default.
Use `thumb` explicitly as the previous control. The new mode inherits Thumb
selection and uses allocated registers only with global residency.

Keep the allocator's r8/r10/r11 set, r9 reservation, scratch registers,
parallel PHIs, call preservation and inline-assembly spills unchanged.
Only supported unflagged i32 operations are selected directly. Other widths,
flagged operations and non-global policies retain existing lowering.
Maintain the r4 quotient convention for adjacent div/rem reuse. Memory,
volatile/MMIO access order and firmware timing loops are outside this change.

## Acceptance and measurement

1. Native sanitizer and assembler tests across every policy combination;
   systematic operations, constants, repeated operands, aliased destinations,
   register pressure, PHIs, calls and fallback cases.
2. Independent LLVM/QEMU execution, including existing scalar/allocation/
   mixed-ABI cases and the actual maze's deterministic fingerprint.
3. Fresh owned builds of all default applications and the Wi-Fi host image;
   no radio experiment. Compare text, data, BSS and frame demand separately.
4. Same-source control versus candidate with identical C06 passes: `thumb`
   versus `thumb-registers` for Z01; `global` versus `global-flex` for Z02;
   `global-flex` versus `global-compact` for Z03.
   Z04 replaces only the combiner slot with qualified `scalar-cleanup`;
   compare explicit old/new five-pass profiles on identical source.
   Prove ordinary control output stays unchanged where the new policy is off.
5. Fixed-seed six-module/six-kernel hardware campaign, physical role swaps
   and two complete repeats; preserve hashes, clocks, nonces and raw samples.
   Failed/incomplete campaigns do not count. Do not relax safety deadlines.
6. Restore LLVM and candidate maze images, confirm gameplay, and record
   actual startup drawing and physics ticks. Keep scope distinct from FPS.

Promote only after correctness gates and repeatable hardware improvement.
Investigate repeatable workload regressions above 5%; no silent global
trade-off. Do not accept a smaller binary as a proxy for faster execution.
Unknown stack paths remain unknown until independently bounded or measured.

Generated results belong in fresh build directories. Keep earlier measurements
and images unchanged. Record completed-stage results here and in machine-
readable comparison reports rather than creating a maze-specific compiler rule.

## Z01 results

The selector removes operand/result shuttling without changing allocation,
frame policy, memory operations, calls or the game source.

| Function | Control instructions / moves | Candidate instructions / moves |
| --- | ---: | ---: |
| `collide_wall` | 705 / 145 | 636 / 76 |
| `physics_step` | 392 / 95 | 358 / 61 |
| `draw_board` | 856 / 120 | 824 / 88 |
| `generate_maze` | 2,569 / 417 | 2,448 / 296 |

Private frame access counts are unchanged; Z01 does not solve spilling.
All eight saved control C modules emit byte-identical assembly when the
previous `thumb` policy is selected. The native suite covers 36 complete
policy combinations and 624 additional parameterized register-operation
cases. Expanded existing scalar execution inputs cover wrapping arithmetic,
every defined variable shift count, signed boundaries, repeated operands and
zero immediates. Full `test-oracle`, `test-fast-io` and `test-metrics` pass
with the candidate enabled, as do both LLVM/owned optimization routes,
mixed ABI checks and the actual maze QEMU oracle.
All eleven default applications plus the Wi-Fi host image build.

Instrumented owned maze flash payload falls from 58,472 to 56,852 bytes
(-2.77%); uninstrumented payload falls from 55,728 to 54,240 bytes (-2.67%).
Data and BSS remain unchanged.

Fresh fixed-seed `thumb` hardware control:
[`run.r6PSlq`](../../build/compiler-compare-runs/run.r6PSlq/results.json).
Both candidate campaigns,
[`run.HSfg7j`](../../build/compiler-compare-runs/run.HSfg7j/results.json) and
[`run.UmT0jc`](../../build/compiler-compare-runs/run.UmT0jc/results.json),
match all six modules and six kernels in both badge roles and recover to
BOOTSEL. Paired comparisons
[one](../../build/compiler-compare-runs/run.HSfg7j/z01-comparison.json) and
[two](../../build/compiler-compare-runs/run.UmT0jc/z01-comparison.json)
validate source identity, LLVM image, clocks, fresh nonces and raw samples.

| Workload | Reduction in owned ticks across the two repeats |
| --- | ---: |
| Arithmetic | 5.59% |
| Strings | 7.36% |
| Memory | 6.71% |
| Control flow | 6.73-6.74% |
| Cross-file ABI | Essentially unchanged (-0.011% to +0.009% reduction) |
| Graphics | 6.46% |
| Arithmetic kernel | 22.98-23.03% |
| Branch/PHI kernel | 10.76-10.79% |
| Cross-file call kernel | 6.03-6.06% |
| Unsigned div/rem kernel | 5.08-5.09% |
| Signed div/rem kernel | 6.22-6.26% |
| Ordinary-memory kernel | 4.90-4.91% |

LLVM-control drift is at most 0.071%; no material workload regression was
observed. These reductions concern median raw ticks, not percentage speedup.

Both maze images were flash-verified and rebooted. The user confirms both
games work and reports:

| Actual maze startup benchmark | Previous owned | Z01 owned | LLVM control |
| --- | ---: | ---: | ---: |
| RAM draw ticks | 56,202 | 51,824 | 7,718 |
| Physics ticks per step | 5,403 | 5,176 | 377 |

Drawing uses 7.79% fewer ticks and physics uses 4.20% fewer ticks. The
remaining LLVM gaps are 6.71 times for drawing and 13.73 times for physics.
These are user-read measurements from one fixed pairing, not the broad
role-swapped suite or overall FPS.

Z01 is promoted only after these correctness and physical gates. Final
default/API checks must confirm that unspecified selection produces the same
assembly as the explicitly qualified candidate. These checks pass for all
37 corpus C modules and the complete sanitizer/API matrix. Frame/call metadata
is unchanged throughout the corpus. C06 inputs are unchanged apart from
source-filename paths and three explicitly checked generated USB Wi-Fi
protocol build-ID constants; no size/timing gain is attributed to those IDs.
Earlier control policies and
images remain available. Machine-readable evidence is in
[the Z01 report](../../build/maze-opt-z01-results.json).

### Next bounded step

Investigate Z02/Z03 pressure and boolean/narrow-value spill traffic before
expanding the register set. Z01 leaves 52 private frame accesses in
`physics_step` and 172 in `collide_wall`; these static counts motivate
measurement, not a claim that every access executes or can be removed.
Retain r9's reservation and explicit scratch/clobber ownership. Do not expand
allocation by changing a register-count constant alone.

## Z02 candidate contract

Select `--register-residency=global-flex` or
`BADGE_CODEGEN_REGISTER_RESIDENCY=global-flex`, now the shared CLI/API
default at Z02 completion. Z03 below records the subsequent default.
Select `global` explicitly for the qualified Z01 control.

The candidate permits persistent r4/r5 values only when instruction-level
scratch masks and CFG liveness prove them safe. Masks constrain live-through
values, operands and results: this also protects the hidden r4 quotient used
by adjacent div/rem selection. Alignment-sensitive memory masks share the
backend's memory-lowering rules; unknown operations conservatively reserve
both registers. Calls retain conservative setup constraints, parameters
cannot occupy prologue scratch r5, and inline-assembly live values use the
existing save/restore path extended to five registers.

The allocator keeps r0-r3/r6/r7/r9/r12 outside the persistent pool. r6 remains
the PHI-cycle scratch register. Parallel copies and assembly preservation
handle all five pool registers; AAPCS saves and eight-byte stack alignment
are preserved. Existing private homes remain available as conservative spills.

Copy/PHI affinities choose compatible colors without merging interfering
live ranges. Spill priority preserves existing use costs first; equal-cost
values prefer fewer eligible colors, then lower interference degree.
Low scratch-compatible registers are preferred to retain unrestricted
registers for constrained values. Eligible identity copies operate directly
on their allocated source. Allocation and new analysis retain bounded work;
no maze identifiers enter these rules.

### Rejected initial priority

The initial `score * eligible_colors / (degree + 1)` ranking passed correctness
checks but regressed the arithmetic kernel by **9.684%** in both complete
role-swapped physical campaigns. Neither control nor candidate spilled an
eligible value in that function. The candidate instead emitted two extra
register copies on every loop backedge; dividing use cost by interference
degree deprioritized long-lived loop/PHI values.

Changing only the priority comparator to use cost first removes those two
backedge copies on the identical optimized IR. This is a general allocator
rule, not a kernel-name special case. The original results remain archived in
`../../build/compiler-compare-runs/run.ZfPMgi/z02-comparison.json` and
`../../build/compiler-compare-runs/run.Iyd6XU/z02-comparison.json`; they must not
be presented as qualification of the revised allocator.

### Revised Z02 hardware and corpus evidence

The use-cost-first revision passes two complete six-module/six-kernel
campaigns, each swapping the physical LLVM/owned roles. Both use the archived
Z01 control's exact seed `3276377845`; every result matches LLVM, and both
devices recover to BOOTSEL. Positive changes below mean **slower**:

| Workload | First owned-tick change | Repeat |
| --- | ---: | ---: |
| Arithmetic module | -9.952% | -9.952% |
| Strings | -3.772% | -3.774% |
| Memory module | +0.971% | +0.973% |
| Control flow | -4.467% | -4.462% |
| Cross-file ABI | -2.571% | -2.555% |
| Graphics | -0.344% | -0.341% |
| Arithmetic kernel | -0.146% | -0.146% |
| Branch/PHI kernel | -13.299% | -13.291% |
| Cross-file call kernel | -6.588% | -6.588% |
| Unsigned div/rem kernel | -6.515% | -6.464% |
| Signed div/rem kernel | -9.238% | -9.238% |
| Ordinary-memory kernel | -8.019% | -8.019% |

The initial 9.684% arithmetic-kernel regression is removed. That kernel is
now essentially unchanged, not a substantial speedup. The small whole-memory
regression is retained explicitly; no repeatable regression exceeds the 5%
gate. Two earlier revised campaigns used random seeds, so the pairing tool
correctly rejected comparison with the fixed-seed control. They supply
correctness evidence only and are excluded from performance qualification.

Valid paired reports:

- `../../build/compiler-compare-runs/run.xn6jBj/z02-comparison.json`
- `../../build/compiler-compare-runs/run.X7cgSA/z02-comparison.json`
- Control: `../../build/compiler-compare-runs/run.VKBnSn/results.json`

Fresh builds produce all twelve corpus images, including the offline USB
Wi-Fi image; no radio experiment is performed. All 37 C modules retain the
same optimized IR after source-filename normalization, except exactly three
known generated USB protocol build-ID constants (`2092338486` to
`4132272326`). Explicit `global` assembly is unchanged under the same narrow
build-ID normalization. Across 552 functions, private frames, outgoing stack
arguments, call models and unresolved stack information are unchanged;
70 functions need eight fewer callee-save bytes. The maximum frame including
saves remains 3,680 bytes. This is not a proof of a complete stack bound.

| Actual maze | Z01 flash payload | Revised Z02 flash payload |
| --- | ---: | ---: |
| Instrumented | 56,852 bytes | 54,892 bytes |
| Uninstrumented | 54,240 bytes | 52,336 bytes |

Instrumented BSS remains 160,448 bytes and uninstrumented BSS 160,440 bytes.
The allocator does not implement stack-slot reuse.

| Function | Z01 instructions / moves / frame accesses | Revised Z02 |
| --- | ---: | ---: |
| `collide_wall` | 636 / 76 / 172 | 602 / 99 / 115 |
| `physics_step` | 358 / 61 / 52 | 354 / 67 / 42 |
| `draw_board` | 824 / 88 / 157 | 820 / 112 / 129 |
| `generate_maze` | 2,448 / 296 / 638 | 2,374 / 387 / 473 |

These are static counts, not dynamic accesses, drawing ticks or FPS.
More moves and fewer SRAM accesses do not automatically imply faster code.

### Z02 offline validation and playable maze

The final revision passes the native sanitizer/assembler matrix across all
48 complete policy combinations, including 832 systematic register-operation
cases, 32 memory-scratch cases and six identity-copy cases. The existing
global-residency oracle also exercises five-value cyclic PHIs with zero,
one and multiple loop iterations. Both LLVM-optimized and owned-optimized
IR execute through the owned backend; mixed LLVM/owned ABI, the full oracle,
fast-I/O helpers, reporting tests and the actual deterministic maze pass.
The maze retains fingerprint `0x7789aa8c`, calibration, drawing, hazards
and progression. No maze-specific selection or allocation rules are added.

Both exact badges were flash-verified and rebooted with the restored games.
The user confirmed both games display and play correctly:

| User-read startup benchmark | Z01 owned | Z02 owned | LLVM |
| --- | ---: | ---: | ---: |
| RAM board drawing ticks | 51,824 | 50,951 | 7,718 |
| Physics ticks/step (`P`) | 5,176 | 5,099 | 377 |

Drawing uses **1.685% fewer ticks** and physics **1.488% fewer ticks** than
Z01 on the same fixed badge pairing. These are raw timer ticks, exclude panel
transfers and IMU access, and are not role-swapped maze measurements or FPS.
The user approved promoting the qualified revised allocator; `global-flex`
was promoted to the shared CLI/API default, with `global` retained as the explicit
control. C06 passes, frame/scalar/memory policies and clock settings are
unchanged. Machine-readable evidence is in
[Z02 results](../../build/maze-opt-z02-results.json).

After promotion, the CLI/API sanitizer matrix and driver/report tests pass.
All 37 corpus C modules and all eight instrumented-maze C modules produce
byte-identical assembly under the promoted release and sanitizer defaults
versus the qualified explicit policy. A fresh actual-maze build without a
residency override also produces a byte-identical UF2 to the flash-verified
qualified image. Historical images and reports remain unchanged.

## Z03 candidate contract

Select `--register-residency=global-compact` or
`BADGE_CODEGEN_REGISTER_RESIDENCY=global-compact`, now the qualified shared
CLI/API default. Use `global-flex` explicitly for the preceding Z02 control.
`BADGE_RESIDENCY_GLOBAL_COMPACT` is the corresponding public options-API enum.

The candidate extends the same scratch-aware five-register allocator to
supported one-word integer values, including i1/i8/i16. Every definition,
incoming argument and constant PHI input remains normalized to its declared
width before a value becomes resident. Signed interpretation is still
performed at signed operations/ABI boundaries. Narrow identity copies cannot
truncate a live source register; copy lowering requires matching widths.
r9 remains reserved and r6 remains PHI-cycle scratch.

The existing CFG liveness/interference graph also colors four-byte private
scalar SSA homes. Noninterfering values can share a home, including across
blocks and loop edges. Register-resident values retain valid homes for
inline-assembly preservation; live-across values cannot share those homes.
PHI inputs are captured before destinations change, preserving parallel
copies when a dying input and its successor PHI share storage.

Addressable alloca buffers remain distinct, pinned for the entire function,
with their original size and alignment. Wide/aggregate SSA homes remain
separate, as do PHI capture storage and by-value argument objects. This is
not addressable-object lifetime analysis or variable-sized spill packing.
The existing 64 KiB pre-layout frame ceiling and bounded analysis/allocation
budgets remain in force. Only private layout changes; volatile/MMIO ordering,
AAPCS stack alignment, incoming offsets and outgoing argument sizing do not.

All 37 archived Z02 corpus controls remain byte-identical on identical IR.
Both optimizer/backend execution routes, mixed ABI and actual maze
fingerprint/calibration/rendering/hazards/progression pass. All twelve corpus
images build; the candidate instrumented maze payload is 51,124 bytes and
uninstrumented payload 48,672 bytes. BSS is unchanged. Across 552 functions,
482 private frames shrink; the largest individual frame including saves
drops from 3,680 to 1,568 bytes. Calls, outgoing argument sizes and unresolved
stack information are unchanged. This is not a complete dynamic stack bound.

The existing native suite now covers 60 complete policy combinations,
including systematic narrow arithmetic and a measurable 80-definition
reuse chain. Existing execution fixtures add signed/unsigned narrow
mixed-ABI calls with five arguments, loop-carried narrowing and callback
preservation, plus i1/i8/i16 PHIs with negative constants and zero/one/many
iterations. The full native matrix, pipeline/driver/report tests and fast-I/O
helper execution pass. Smaller images and frames are not speed measurements.

### Z03 hardware qualification and playable maze

Two complete fixed-seed (`3276377845`) campaigns swap both physical roles
and match all LLVM results. They compare the candidate with a fresh Z02
control. Every measured workload improves in both repeats:

| Workload | First owned-tick change | Repeat |
| --- | ---: | ---: |
| Arithmetic module | -0.712% | -0.714% |
| Strings | -3.890% | -3.897% |
| Memory module | -3.963% | -3.965% |
| Control flow | -5.288% | -5.288% |
| Cross-file ABI | -1.394% | -1.413% |
| Graphics | -3.519% | -3.521% |
| Arithmetic kernel | -4.908% | -4.952% |
| Branch/PHI kernel | -6.198% | -6.143% |
| Cross-file call kernel | -2.644% | -2.619% |
| Unsigned div/rem kernel | -2.306% | -2.313% |
| Signed div/rem kernel | -2.737% | -2.745% |
| Ordinary-memory kernel | -3.292% | -3.333% |

An initial control campaign (`run.Y5A0iU`) matched all results but its
second collector exceeded the unchanged 60-second deadline. It is excluded
from timing qualification. A fresh control after native testing completed
passed; no deadlines, clock settings or firmware work counts were relaxed.

Valid reports:

- Control: `../../build/compiler-compare-runs/run.vL0y1c/results.json`
- `../../build/compiler-compare-runs/run.a1JKfB/z03-comparison.json`
- `../../build/compiler-compare-runs/run.OhQbow/z03-comparison.json`

Both games were restored, flash-verified and rebooted. The user confirmed
correct display/gameplay and approved default promotion:

| User-read startup benchmark | Z02 owned | Z03 owned | LLVM |
| --- | ---: | ---: | ---: |
| RAM board drawing ticks | 50,951 | 49,159 | 7,718 |
| Physics ticks/step (`P`) | 5,099 | 4,930 | 377 |

Drawing uses **3.517% fewer ticks** and physics **3.314% fewer ticks** than
Z02. The uninstrumented payload shrinks **7.001%**, from 52,336 to 48,672
bytes. Startup values are one fixed badge pairing, not role-swapped maze
timing or FPS, and exclude panel transfers and IMU access. The compiler's
other policy defaults and C06 passes remain unchanged. See the
[machine-readable Z03 results](../../build/maze-opt-z03-results.json).

After promotion, the implicit CLI/API sanitizer matrix and driver/report
tests pass. Promoted release and sanitizer defaults emit byte-identical
assembly for all 37 corpus and eight instrumented-maze C modules versus the
qualified explicit candidate. A fresh source-to-UF2 build with no residency
override also produces the exact flash-verified candidate game image.
