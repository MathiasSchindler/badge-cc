# D09: independent candidate qualification and promotion

D09 provides a repeatable gate for the implemented D04-D07 candidates, not
a combined optimization profile or implementation of the D08 follow-ups.
An accepted experiment is not automatically a speed default. Existing
`direct/global-compact/thumb-layout/word` and
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`
controls remain explicit and unchanged.

## Current default versus LLVM: 2026-10-10

Subsequent [PO01 actual application measurements](../application-measurements.md)
extend the baseline below with real maze generation, input/physics, RAM drawing,
panel transfer and reached frame paths. Four role-swapped rounds and 336 raw
samples match, with independently verified full-flash restoration. These
separate measurements do not alter D09 candidate decisions or certify live
IMU acquisition, cold-XIP or visual/electrical panel behavior.

A subsequent fresh hardware baseline compares the current default owned
compiler with **LLVM/Clang 22.1.8**, using the repository's freestanding
`-Oz`, Cortex-M33 Thumb build settings. This is not a comparison against
LLVM `-O3` or a combined D04-D07 profile.

The [current baseline evidence](../../../build/current-badge-benchmark-I6HhpA/results.json)
retains two seeds (`3276377845`, `123456789`), both swapped physical roles per
seed, four rounds, eight raw reports and **672 workload timing samples**.
Every checked result matches. Current source and compiler executable hashes,
image hashes, raw samples and clock settings are retained.

| Workload | Owned/LLVM elapsed-time ratio | Range across four paired median ratios |
| --- | ---: | ---: |
| Arithmetic module | 5.078x | 5.077-5.078x |
| Strings module | 4.382x | 4.382-4.382x |
| Memory module | 5.547x | 5.546-5.549x |
| Control-flow module | 3.652x | 3.644-3.661x |
| Cross-file ABI module | 6.259x | 6.258-6.260x |
| Graphics module | 5.076x | 5.075-5.076x |
| Arithmetic kernel | 2.817x | 2.817-2.818x |
| Branch/PHI kernel | 2.948x | 2.948-2.949x |
| Cross-file call kernel | 4.593x | 4.592-4.594x |
| Unsigned div/rem kernel | 3.565x | 3.557-3.573x |
| Signed div/rem kernel | 2.304x | 2.282-2.326x |
| Ordinary memory kernel | 4.849x | 4.846-4.854x |

Ratios are geometric means of four paired medians; **5x means the owned
code takes five times as long**, not five percent longer. Ranges are
descriptive, not statistical confidence intervals. There is no averaged
"whole badge" score. Absolute intervals are timer0 raw ticks, not
independently calibrated microseconds.

These are measurements on real badges, but the module drivers and kernels
are tests, not complete production applications. Strings and memory reach
existing diagnostic/byte helpers; graphics reaches the actual canvas
clear/rectangle/text helpers and includes pixel fingerprint accumulation
inside its timed workload. Its ratio is therefore not an isolated drawing
or panel-transfer measurement. Timings are warm-working-state batches.
No compiler rule recognizes these workloads, seeds or expected answers.

Both complete original 16 MiB flash images were restored and independently
reread/hash-verified; both badges remain in BOOTSEL. No production source,
compiler pass or default changed during this baseline.

The gap remains substantial. The next performance investigation should
measure reached real application work (maze generation, physics/collision,
RAM drawing and separately display transfer), with varied runtime inputs.
Use those profiles to identify general IR/allocation/lowering deficiencies
and then rerun unrelated applications and this baseline. Small adversarial
tests remain useful correctness guards, not proof of an application speedup
or justification for fixture-specific lowering.

## Actual maze stage isolation: 2026-10-10

The [verified stage analysis](../../../build/maze-stage-comparison-sWm4hl/analysis.json)
and [complete capture](../../../build/maze-stage-comparison-sWm4hl/results.json)
use the ordinary production maze source and actual startup/display/IMU
dependencies, not a new fixture. No compiler behavior or badge firmware was
changed. These are static comparisons, not new hardware speed measurements.

Both toolchains implement the conceptual sequence of preprocessing,
lexing/parsing, semantic analysis, IR, optimization, target code generation,
assembly and linking. That does not require identical internal
representations. In the owned driver, `badge-ir` invokes the shared parser
and semantic analysis internally; it does not consume the JSON outputs of
separate `badge-parse`/`badge-sema` processes. Clang can likewise use its
integrated assembler without persisting textual assembly.

### Frontend

The two preprocessed text files differ in formatting/line markers, but all
**5,170 normalized tokens match exactly**. Feeding either file to the owned
parser produces identical location-free AST and typed-AST JSON, and identical
owned initial IR after source-filename normalization. Clang also emits
identical initial IR from either preprocessing output after excluding the
source filename and inline-assembly source-location annotation.

Clang and owned AST formats are different; they cannot be meaningfully
compared byte-for-byte. Both identify the same 29 function definitions.
That inventory and the preprocessing crossovers do not constitute a complete
cross-frontend semantic AST-equivalence proof.

Initial IR was captured before LLVM optimization using
`-Oz -Xclang -disable-llvm-passes`; LLVM verification accepts both modules.
The two frontends produce different lowering even at this point:

| Maze translation unit, before optimization | Owned frontend | Clang frontend |
| --- | ---: | ---: |
| Function definitions | 29 | 29 |
| `alloca` instructions | 212 | 149 |
| `load` instructions | 697 | 638 |
| `store` instructions | 440 | 339 |
| PHIs | 0 | 27 |

For example, `clamp`'s nested conditional expression uses two temporary
stack slots in owned initial IR and PHIs in Clang initial IR. The owned
comparison also passes through `zext i1` and a comparison against zero
before branching, whereas Clang branches on the original `i1`.
These are different representations, not evidence of a C correctness bug.
Clang also supplies optimization-relevant attributes and metadata.
Raw instruction counts alone are not runtime estimates.

### Identical IR, different backend

Hold the owned optimized IR file fixed. Send it directly to either
`badge-codegen` or LLVM target code generation, with LLVM's IR pass pipeline
disabled. The maze object's executable `.text` bytes are:

| Frontend and IR optimizer | Target backend | Object `.text` bytes |
| --- | --- | ---: |
| Owned / owned, identical optimized IR | Owned | 24,960 |
| Owned / owned, identical optimized IR | LLVM | 5,040 |
| Owned / LLVM `default<Oz>` | LLVM | 4,708 |
| Clang / LLVM `default<Oz>` | LLVM | 4,588 |

Object `.text` excludes readonly data and may include subsequently discarded
functions. This approximately 4.95x same-IR code-size difference isolates
target lowering/instruction selection/register allocation/machine
optimization as a major static disparity, not the assembler. It does not
assign a fraction of the measured runtime gap to those stages.
The latter two rows hold LLVM optimization/backend settings fixed, but the
different frontend IR includes different attributes/metadata as well as
instructions.

The full independently built production images have linked text of
41,508 bytes owned versus 8,684 bytes LLVM, with data/BSS 16/160,440 bytes
in both. Replacing only the maze translation unit's backend with LLVM
reduces the owned-dependency image's linked text to 21,564 bytes.
Linked text here includes readonly data and flash-end sections.
Hybrid images were linked and statically inspected, not newly executed.

The reverse arbitrary-LLVM-IR crossover is not yet available: feeding
Clang's optimized maze IR to `badge-codegen` explicitly fails on the named
structure declaration at line 6. The owned pipeline emits valid LLVM IR,
but its backend accepts a bounded subset, not all LLVM IR.

### Identical assembly and identical objects

The actual owned maze assembly was assembled independently by `badge-asm`
and LLVM's integrated assembler. With identical real dependencies and LLD,
the resulting **complete flash binaries match byte-for-byte**.
Linking the same owned object set with `badge-link` and LLD also produces
identical complete flash bytes.

With the same LLVM-generated object set, the linkers produce equal-size
images but not identical bytes: 362 of 8,700 flash bytes differ. All 53
named function addresses/sizes match. Every differing byte is verified to
belong to the reordered 354-byte mergeable string pool or its associated
`R_ARM_ABS32` literal-pool addresses. All 20 string relocations resolve to
the same original strings, the string inventory matches, and all remaining
flash bytes match. This is constant-placement/relocation behavior, not
different instructions or an established runtime regression.

PO00 now provides the maintained
[actual-source stage capture tool](../../tools/pipeline-stages.mjs).
Its [reproduction and qualification](../pipeline-optimization.md#po00-implementation-and-reproduction)
cover the maze dependencies and current full corpus, including explicit
compiler-conditional preprocessing differences and rejected crossovers
outside the maze. The historical comparisons above retain their original
evidence; PO00 makes no additional hardware speed or execution claim.

## Reproduce

Use the installed host C compiler, LLVM, LLD and QEMU; no package installation
or new firmware dependency is introduced. From the workspace root:

```sh
export PATH="/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH"
export LLVM_BIN=/opt/homebrew/opt/llvm/bin
export LLVM_CLANG="$LLVM_BIN/clang"
export LLVM_OPT="$LLVM_BIN/opt"
export LLVM_LLD=/opt/homebrew/bin/ld.lld
export QEMU_ARM=/opt/homebrew/bin/qemu-system-arm
make -C badge-compiler all test-target-cost test-compact test-scheduling \
  test-final-layout test-target-strength test-datasheet-qualification
make COMPILER_TEST_TIMING=1 test-compiler-test test-tilt-maze

node badge-compiler/tools/qualify-datasheet.mjs --offline build/d09-new-offline
node badge-compiler/tools/qualify-datasheet.mjs --hardware \
  build/d09-new-hardware build/d09-new-offline/offline.json \
  9BBF88DD17D66E7D 08DA26F86E57F5EA
node badge-compiler/tools/qualify-datasheet.mjs --analyze \
  build/d09-new-hardware/reviewed.json build/d09-new-hardware/hardware.json
```

The hardware command **replaces both selected applications temporarily**.
Use only explicitly selected, unsecured 16 MiB RP2350 Arm badges initially
in BOOTSEL. Output directories must be new. Failed runs retain explicit
failed status and logs; do not overwrite a failed experiment to hide it.
Flash snapshots are local recovery data, not publication artifacts.

The [qualifier](../../tools/qualify-datasheet.mjs) builds a fresh default
control and four independent profiles:

| Profile | C07 scalar selection | C06 difference |
| --- | --- | --- |
| control | `thumb-layout` | None |
| D04 | `thumb-compact` | None |
| D05 | `thumb-schedule` | None |
| D06 | `thumb-relax` | None |
| D07 | `thumb-layout` | `target-strength-reduce` after `constant-fold` |

No compact/scheduling/relaxation composition is assumed. D08's scalar SMULL,
SRAM placement, GPIOC, floating and RCP decisions remain separate projects.

## Offline gates

The final fresh campaign enumerates **37 C modules and 12 linked images
per profile**, plus a separate `TILT_MAZE_COMPARE` image. It deliberately
rejects an unexpected corpus count rather than recycling historical
36-module/11-image collector assumptions.

For every profile, LLVM verifies optimized IR, both existing LLVM-IR and
owned-IR execution routes pass, and the actual maze differential oracle
passes. These routes include raw PHIs, guarded memory, wide arithmetic,
register pressure, scratch/flags and bidirectional mixed AAPCS calls.
The native maze fingerprint is `7789aa8c`.

The targeted D04-D07 sanitizer/boundary suites retain negative diagnostics,
budget exhaustion and output-preservation coverage. D09 additionally tests
collector lifecycle/error paths and strict noise/promotion rules, including
one bad physical role hidden by a favorable geometric mean and corrupt raw
reports. Existing stage/policy/provenance driver tests remain intact.

Each profile checks **552 function frame/call records**: calls, outgoing
arguments and uncertainty metadata are preserved, with no frame growth.
Across five profiles that is **2,760 checks**, not 2,760 distinct functions.
Local frames plus saves peak at 1,568 bytes. This is not a bound for nested
calls, recursion, callbacks or interrupts, nor a certification of the
production 4 KiB scratch stack.

All images preserve data/BSS and fit the actual linker flash/RAM regions;
Wi-Fi's reserved mailbox is excluded from main-RAM headroom. Fresh maze text:

| Profile | Ordinary maze text | Instrumented maze text |
| --- | ---: | ---: |
| control | 41,508 | 43,720 |
| D04 | 38,300 | 40,364 |
| D05 | 41,508 | 43,720 |
| D06 | 36,756 | 38,712 |
| D07 | 41,496 | 43,708 |

Ordinary maze data/BSS remains 16/160,440 bytes; instrumented maze remains
16/160,448 bytes. Smaller text is not a hardware timing result.

## Hardware method and decision gate

The completed paired sequence is control, D04, D05, D06, D07, followed by its
repeat. Every campaign swaps LLVM/owned physical roles, uses the fixed seed
`3276377845` with fresh nonces, validates clock settings and all fingerprints,
kernel guards and work counts, and retains seven raw timings per workload
and role. LLVM is an independently retained drift control.

Ten complete campaigns give **20 role-swapped rounds, 40 raw reports and
3,360 workload timing samples** for the existing twelve-workload suite.
This suite exercises reached shared graphics code, but is not a substitute
for new physical maze physics/full RAM-drawing or panel-transfer timing.
The separately built instrumented maze and actual maze execution oracle are
not presented as those unmeasured hardware timings.

For each workload, role and repeat, the predeclared relative uncertainty is
the sum of:

1. Control raw range divided by its median.
2. Candidate raw range divided by its median.
3. Same-board repeated-control median drift.
4. Paired LLVM control median drift.

This uses D02's conservative spread/control-drift approach, not a numeric
noise constant borrowed from a different clock/memory experiment. A candidate
must improve beyond the envelope on **all four role/repeat observations**
for a workload to count as a repeatable improvement. Equality is unresolved.
Any resolved role/repeat regression blocks speed promotion; geometric means
remain descriptive and cannot hide one bad role.

Comparisons use exact integer-rational tick arithmetic; floating percentages
are display only. Synthetic one-tick ties (999/1001 against a 1,000-tick
control with a one-tick range) exposed floating cancellation in the original
classifier. Both directions now remain within noise, as strict equality
requires. The ten protocol/qualification tests and six sanitizer-backed
native collector scenarios pass with this correction.

Offline correctness/budget checks and two verified restorations are mandatory
before a decision. A measured shared-code win without unexplained regression
is only eligible for speed review: production interrupt/peripheral/stack
coverage remains a distinct gate. A size tradeoff remains explicitly
selected, never silently substituted for the speed default.

## Completed outcomes

All five independent profiles pass the fresh offline gates. The exact
analysis of all ten completed campaigns gives:

| Candidate | Repeatable improved workloads | Workloads with resolved regression | Decision |
| --- | --- | --- | --- |
| D04 | strings, memory, control-flow, graphics, kernel-arithmetic, kernel-cross-file-call, kernel-unsigned-divrem | kernel-signed-divrem | Reject speed promotion; retain explicit size tradeoff |
| D05 | strings, memory, control-flow, graphics | None | Retain opt-in pending production-runtime coverage |
| D06 | strings, memory, control-flow, graphics, kernel-cross-file-call | cross-file-abi, kernel-branch-phi, kernel-signed-divrem | Reject speed promotion; retain explicit layout candidate |
| D07 | None | None | Retain opt-in; no resolved speed benefit in this suite |

An improvement requires all four observations; a regression in any
role/repeat is sufficient to block promotion. Unlisted workloads do not
establish repeatable improvement; the machine-readable result retains every
observation and exact boundary. In particular, D07's lowered instruction
reduction and D06's smaller text do not establish a speed-default win.
No compiler policy or pass default changes.

The final collector completed the role-swapped pilot and ten qualification
campaigns. Both original **16 MiB** flash images were restored, reread in full
and independently SHA-256 verified. Both badges are left in BOOTSEL.

## Collector lifecycle

The retained diagnostic localized the original D05 failure precisely:
both 1,792-byte reports were saved, both BOOTSEL requests and interface/
handle closes completed, then **`libusb_exit` stalled** until the unchanged
60-second process deadline. That is a teardown failure, not bad candidate
results or permission to accept a timed-out campaign.

The [collector](../../../src/host/tools/compiler_test_host.c) now retains
references to the selected live device objects, explicitly requests BOOTSEL,
closes handles, and processes/observes their hotplug removal **before**
destroying the context. Only selected device identities are observed; other
USB devices are not reset. Event/enumeration errors and a bounded detach
observation failure are explicit, and the overall deadline is retained.

An intermediate live-teardown/watchdog-only experiment avoided the context
stall but exceeded the original 30-second host recovery deadline. Both
original flash images were restored and independently verified after that
failed attempt. It is not the final approach. The runner retains a bounded
120-second emergency recovery wait; inherited reference-clock watchdog
counts are not a calibrated host wall clock.

This changes host collection/lifecycle behavior, not badge firmware or
compiler lowering. There is no skipped destructor, forced-success timeout
fallback, disabled alarm or acceptance of a partial campaign.

## Evidence and scope

The [D09 result](../../build/datasheet/d09-results.json) binds source/tools,
validation logs, fresh IR/assembly/images, campaign summaries and raw reports
to the final decisions. Raw bytes are independently re-decoded for CRC,
nonce, source identity, clocks, fingerprints and samples; reported statistics
are checked rather than trusted. Full original snapshots and independent
full-flash rereads must match before final publication.

The original capture-time hardware report is retained, not overwritten.
Read-only `--analyze` verifies its offline artifacts, campaigns, raw reports
and restorations before recomputing exact decisions. The final result
separately records capture-time source hashes, archived capture-time
qualifier/Makefile, and the post-capture host analysis/test revision.
All other captured compiler/firmware/tool hashes still match. This is not
a claim of recompiling the measured images with the revised analyzer.

Results do not certify arbitrary production paths, external oscillator
frequency, electrical panel timing or security protection. D09 remains a
reusable per-candidate gate: a future change, combination or D08 follow-up
requires a fresh control and its own relevant qualification.
