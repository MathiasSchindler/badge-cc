# C06 optimization plan

Proposed on 2026-10-07; runtime priorities updated on 2026-10-08. This is a
bounded first C06 release plus a separately tracked RP2350 speed follow-up,
not a commitment to reproduce LLVM's optimizer. M00-M09 are complete under
the approved measured-size re-scope. RP2350 follow-ups remain proposals.

For the subsequent current-default promotion and next implementation stages,
see [the maze-driven general optimization plan](maze-optimization.md).
This document retains the earlier milestone qualification history.

## Goal and boundaries

Reduce RP2350 execution time, generated badge code and stack demand while
preserving defined program behavior, the LLVM-free build, and the existing
independently testable stages:

```text
C05 frontend IR -> C06 optimized/legalized IR -> C07 assembly
  -> C08/C09 objects -> C10 ELF -> C11 binary -> C12 UF2
```

Keep canonical textual IR at both C06 boundaries. Reuse the
[pass API](../src/ir_pass.h), [core](../src/ir_core.c), and separate
[registry/default pipeline](../src/ir_passes.c). New passes are opt-in until their
acceptance gates pass. The existing `aggregate-memory` and `wide-division`
legalizations remain available and required wherever C07 needs them.

The only target is the current RP2350 Arm Secure/Cortex-M33 configuration:
Thumb-2, 32-bit pointers, little-endian data and the existing AAPCS interface.
No generic multi-target cost framework, another architecture, FPU support or
SIMD/vectorization project is required. SCCP and CSE are still worthwhile:
their purpose here is to remove work in the badge corpus, not to support
arbitrary C programs or reproduce general LLVM optimization coverage.

The initial C06 release ends at M09. Register allocation and machine
instruction selection belong to C07, and their proposals are explicitly
separate below. Linker pooling, floating point, arbitrary LLVM IR, general
alias analysis, unrestricted loop transformations and whole-program
optimization remain excluded. The narrowly bounded loop follow-up below is
not a prerequisite or an implicit extension of M09.

## Why these steps

Current C05 output stores parameters and local scalar variables in allocas,
then reloads them repeatedly. For example, the screen demo's `rgb565`
function stores all three parameters and immediately reloads them; its
`glyph_row` function also uses stack slots to communicate between branches.
The current constant-folding pass retains folded definitions as `freeze`
instructions. These are concrete opportunities for memory promotion and
dead-instruction removal, not hypothetical advanced optimizations.

There are two independent costs:

- **C06:** redundant memory operations, dead instructions, redundant
  expressions and control flow.
- **C07:** every surviving SSA value is currently spilled to a stack slot.
  Cleaner SSA does not mean register allocation has happened. PHIs can also
  increase backend storage, so each pass must be measured through C07.

M06 can remove branch paths exposed by promotion; M07 can remove repeated
normalization/identity work; M08 can reuse repeated calculations. All can
improve speed as well as size. None guarantees a speedup merely because the
IR is shorter: C07 may still reload the surviving values or increase their
live ranges. Measure the emitted program, not an estimated IR speed score.

The [current backend](../src/codegen.c) gives a concrete RP2350 opportunity:
SSA reads/writes use stack homes, and `cg_stack` materializes an offset into
`r12`, adds frame base `r7`, then loads/stores through `r12`. A legal small
offset can instead use a single Thumb-2 frame-relative load/store. Constants
are materialized with `MOVW`/`MOVT`, and symbol addresses currently include
inline literals with a branch around each literal. These are bounded target
opportunities, not proof that they account for the entire measured slowdown.
The backend already emits hardware `UDIV`/`SDIV`, `MLS` for remainder and
`UMULL`/`MLA` in wide multiplication; adding those existing capabilities
again is not a milestone.

### Recorded starting point

Figures below are flash payload bytes, not UF2 container sizes.

| Metric | Owned C06 + owned C07 baseline | Comparison / qualification |
| --- | ---: | --- |
| All 11 images | 2,158,480 | Ordinary LLVM: 293,520 |
| All 11 images with LLVM C06 and owned C07 | 842,684 | Existing comparison route, not a promised result or bound |
| Unchanged vendor payloads | 225,920 | Present in the Wi-Fi image; exclude from optimization gains |
| Aggregate excluding vendor payloads | 1,932,560 | Primary release size metric; repeated shared code is counted per image |
| Screen test | 20,668 | Fully owned image observed working on the badge |
| Diagnostics | 506,316 | High-value size benchmark |
| IMU demo | 163,676 | Carrier-memory benchmark |
| Wi-Fi host, including payloads | 1,141,616 | High-value size/stack benchmark |
| Largest local frame plus register saves | 5,312 | `live_usb_control_reset`; not total call-stack usage |
| Screen-test conservative direct-call stack bound | 1,512 | Below the initial 4,096-byte region; not a measured high-water mark |

The existing 5,312-byte frame exceeds the initial 4 KiB SCRATCH_Y stack
region. Reducing it is a priority, but even a frame below 4 KiB does not
establish safety for nested calls, outgoing arguments, interrupts or
indirect calls.

## Supplementary hardware execution baseline

On 2026-10-08 the [two-badge timing mode](implementation.md#on-badge-execution-timing)
was added independently of the remaining optimization milestones. It measures
seven samples for each of six deterministic workloads, checks result equality
and clock configuration, and swaps physical compiler assignments before
combining median ratios. Compiler defaults remain unchanged; this baseline
explicitly selects the opt-in M05 pipeline.

Two independent-seed runs measured owned/LLVM ratios of approximately
12.6-17.8x, with all results matching and both badges recovered. Raw samples,
clock snapshots and provenance are retained in the
[first report](../../build/compiler-compare-runs/run.55B5xe/results.json) and
[repeat report](../../build/compiler-compare-runs/run.3H7x0J/results.json).
These are full C05-C07 workload comparisons, not C06-only ablations, absolute
microsecond measurements, or new release thresholds. C07 register allocation
remains outside M00-M09 and is now a separate proposed follow-up below.
Future pass ablations can use the same runner
with explicit `BADGE_OPT_PASSES` selection, subject to its timing safety bounds.

## Milestones and implementation tracker

Status values: **Done**, **Pending**, **In progress**, **Blocked**, or
**Deferred**. Record results against each milestone, not just that a pass
exists. Dependencies are explicit; table order alone is not a dependency.

| ID | Milestone / bounded scope | Depends on | Expected benefit | Acceptance gate | Status | Measured result / implementation |
| --- | --- | --- | --- | --- | --- | --- |
| M00 | Behavior-preserving plumbing: linked core, separate pass files, explicit default policy/roles, scratch reclamation, indexed names, shared narrow contracts and owned-only tests | None | Makes incremental work isolated and testable; no code-quality gain claimed | Existing outputs unchanged; owned/oracle and sanitizer tests pass | Done | All 182 saved artifacts byte-identical; see [refactor record](implementation.md#run-the-boundary-tests) |
| M01 | Reproducible benchmark/reporting: per-function opcode/alloca counts, linked image sizes, frame sizes, resolved direct-call stack bounds and build cost | M00 | Identifies actual reached-code opportunities and establishes comparable measurements | Machine-readable before/after report for 36 modules and 11 images; fixed inputs/configuration; unresolved stack paths explicitly reported | Done | [Report/methods](implementation.md#c06-m01-reproducible-measurements), [baseline](../build/c06-metrics/m01-baseline/report.json), [equivalent comparison](../build/c06-metrics/m01-equivalent/report.json): 36 modules / 11 images; six deterministic builds; 182 original artifacts identical; 2,850 residual allocas; 1,932,560 nonvendor bytes; 5,312-byte largest frame; screen bound 1,488, diagnostics 4,280, IMU 4,144; unknown Wi-Fi paths explicit; owned/oracle/sanitizer tests pass |
| M02 | Minimal structured function/block/instruction views, typed operands, def-use and CFG indexes, conservative effects, and an owned supported-subset verifier | M01 | Safety foundation for deleting/rewriting IR; no direct size benefit promised | Round-trip preserves untouched IR; malformed references/types/terminators/PHIs diagnosed; all existing corpus accepted and unchanged | Done | [API/implementation](../src/ir_analysis.h), [contract and limits](implementation.md#c06-m02-structured-views-and-owned-verification): typed views, indexed names/uses/CFG, immediate dominators and edge-correct PHIs; mandatory pre/post-pass verification; 72 sanitized byte-identical C05/C06 round trips; full 36-module/11-image build and all 179 immediate pre-M02 artifacts unchanged; owned/oracle/metrics gates and six-module hardware differential run pass |
| M03 | Worklist dead-instruction elimination for an explicit whitelist of pure, nontrapping instructions; remove unused folded `freeze` definitions and dead expression trees | M02 | Fewer emitted instructions and C07 SSA slots, without alias analysis | All eligible unused trees removed in fixtures; calls, memory accesses, inline assembly and terminators preserved initially; measurable reached-code or frame reduction on corpus | Done (opt-in) | [Pass](../src/ir_dead_instructions.c), [qualification](implementation.md#c06-m03-worklist-dead-instruction-elimination), [baseline](../build/c06-metrics/m03-baseline-qualified/report.json), [candidate](../build/c06-metrics/m03-dce-qualified/report.json): 1,007 instructions removed; nonvendor bytes 1,995,340 -> 1,970,092 (-25,248; 1.265%); all 11 images smaller; 10 maximum frames smaller, one unchanged; largest 5,312 -> 5,136 bytes (still above 4 KiB); sanitized corpus/LLVM/target and six-module hardware differential gates pass; all 179 default artifacts unchanged |
| M04 | Trivial scalar promotion: entry-block fixed-size allocas with direct, same-type accesses confined to one block, or a single entry-block store proven to precede every load | M02, M03 | Removes parameter spill/reload and simple local-variable traffic; exposes constants | Every eligible fixture slot promoted; escaping, volatile, mismatched and unsupported accesses untouched; source-level differential tests pass | Done (opt-in) | [Pass](../src/ir_scalar_promotion.c), [qualification/limits](implementation.md#c06-m04-trivial-scalar-promotion), [M03-only baseline](../build/c06-metrics/m04-m03-baseline/report.json), [candidate](../build/c06-metrics/m04-scalar-promotion/report.json): 459/3,096 slots promoted, 846 loads and 463 stores removed; 52,232 fewer nonvendor bytes (1,970,092 -> 1,917,860; 2.651% incremental); all 11 images smaller, nine maximum frames smaller/two unchanged; largest 5,136 -> 5,112 bytes still above 4 KiB; sanitizer/LLVM/source/IR/ABI and six-module hardware gates pass; default unchanged |
| M05 | Full scalar mem2reg for eligible nonescaping allocas: dominance/frontiers, PHI insertion, SSA renaming and trivial PHI cleanup; only types/forms C07 already supports | M04 | Removes stack-based communication across branches and loops; unlocks propagation | Branch/loop/backedge, pointer and i64 fixtures pass; uninitialized values follow IR semantics, never invented zero defaults; measure PHI/frame costs through C07 | Done (default in M09) | [Pass](../src/ir_mem2reg.c), [original qualification](implementation.md#c06-m05-pruned-scalar-mem2reg), [M09 default/ablation](implementation.md#c06-m09-qualified-default-release): historical M04 -> M05 saved 193,400 bytes (10.084%) and reduced largest frame 5,112 -> 3,504. M09 enables full mem2reg directly, without redundant scalar promotion, after final-context correctness/size/frame/runtime ablations |
| M06 | Sparse conditional constant propagation plus CFG cleanup: proven constant branches/switches, unreachable blocks, PHI repair, and a cleanup run of existing folding/DCE | M05 | Avoids executing eliminated paths and emitting their operations/calls | Finite worklist convergence; PHI incoming edges exactly match CFG; unknown/undef/poison conditions are not guessed; reached code demonstrably shrinks; before/after hardware timing recorded | Done (opt-in) | [Pass](../src/ir_sccp.c), [contract/qualification](implementation.md#c06-m06-sparse-conditional-constant-propagation), [M05 baseline](../build/c06-metrics/m06-m05-qualified/report.json), [candidate](../build/c06-metrics/m06-sccp-qualified/report.json): 41 blocks/47 edges removed; 24 terminators simplified; instructions 30,410 -> 30,369; nonvendor bytes 1,724,460 -> 1,723,800 (-660; 0.0383%); all images shrink, frames unchanged; sanitizer/LLVM/corpus/target/ABI gates pass. Fixed-seed role-swapped [M05](../../build/compiler-compare-runs/run.41DIv8/results.json)/[M06](../../build/compiler-compare-runs/run.wC1prb/results.json) timing matches all six modules, changes -0.0101% to +0.3845%, no meaningful runtime benefit; defaults unchanged |
| M07 | Small instruction-combining whitelist: type-correct neutral arithmetic identities and proven redundant cast chains; no reassociation, division replacement or unsafe flag/poison simplification | M02, M03 | Removes operations, narrow-value normalization and SSA spill traffic on Cortex-M33 | Each rule has positive/negative boundary tests and a documented semantic precondition; preserve narrow signed/unsigned and overflow behavior; inspect C07 output and measure reached rules | Done (default in M09) | [Pass](../src/ir_instruction_combine.c), [original rules/qualification](implementation.md#c06-m07-bounded-instruction-combining), [M09 ablation](../build/c06-metrics/m09-conservative-no-instruction-combine/report.json): historical M07 removed 31 instructions/996 linked bytes after SCCP; final M09-context combining saves 1,716 bytes, reduces largest frame 3,680 -> 3,672 and shows no >5% hardware runtime regression |
| M08 | Basic-block common-subexpression elimination for pure arithmetic/comparisons/casts and eligible address calculations; no load/call elimination or cross-block value numbering | M02, M03, M07 | Reuses repeated calculations and pointer arithmetic without general alias analysis | Keys include types, operands and semantic flags; no operand reordering without proof; reused definitions dominate replacements; linked/frame benefit demonstrated; check longer-live values for spill/runtime regressions | Done (opt-in; runtime caveat) | [Pass](../src/ir_local_cse.c), [contract/qualification](implementation.md#c06-m08-basic-block-common-subexpression-elimination), [baseline](../build/c06-metrics/m08-m07-qualified/report.json), [candidate](../build/c06-metrics/m08-cse-qualified/report.json): 40 arithmetic/1 comparison/38 cast/127 address reuses; 206 instructions and 6,520 bytes removed (0.3785%); ten images shrink; 61 local frames shrink, none grows; largest frame 3,504 -> 3,496. Normal/forced-collision sanitizer/LLVM fixtures, corpus/target/ABI and repeated physical correctness gates pass; all 228 default artifact hashes unchanged. Arithmetic/strings improve about 7%/6%, but memory regresses 7.64-7.65% in both fixed-seed role-swapped repeats; static work/spills decrease and hot helper assembly is unchanged, while linked placement changes. Root cause not proven: explicit M09 default-qualification blocker |
| M09 | Qualify and enable a bounded default optimization pipeline; publish per-pass size, frame and hardware-runtime ablations and remaining bottlenecks | M01/M02 complete; M03-M08 complete or explicitly deferred with evidence | Turns individual passes into a measured, reproducible improvement on RP2350 | Supported-owned correctness/independence gates pass; default gains satisfy explicitly revised size scope; no hidden runtime regressions; unresolved stack paths remain documented production-readiness blockers | Done (revised scope) | [Release decision/ablations](implementation.md#c06-m09-qualified-default-release), [release index](../build/c06-metrics/m09-release.json), [default snapshot](../build/c06-metrics/m09-default-qualified/report.json): default mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division. Saves 246,896 bytes (12.3736%); all 11 images/max frames shrink, largest 5,312 -> 3,672; ten entry-call bounds unknown. Controlled actual-default hardware is 20.51-36.17% faster than legacy across six workloads. Redundant scalar promotion and late DCE/SCCP/CSE runtime trade-offs excluded; no backend changes. Full unit/oracle/owned-only/metrics, sanitized corpus, independent 38 objects/11 executable/UF2 and hardware gates pass. Broader LLVM-optimized-IR-to-C07 oracle route retains explicitly reported pre-existing byte-string restriction |

M07 can proceed independently of full mem2reg once its stated prerequisites
exist. Do not split M02 into an unrestricted LLVM parser project: build only
the analyses and verification needed for the supported corpus and the next
passes. Preserve valid opaque attributes/metadata; transformations must treat
unknown effects conservatively. Invalid supported IR must fail explicitly.

## RP2350 speed follow-up tracker

These follow-ups have their own acceptance gates, not a requirement to finish
every item before M09. P00's measurement campaign and P01's opt-in frame
addressing are complete. P02 is implemented opt-in with mixed runtime results;
P03-P06 are implemented and hardware-qualified opt-in; P06 has mixed runtime
results and remains explicit-only.
Preserve C05/C06 IR and C07 assembly boundaries. C07 selections need separate
provenance; `BADGE_OPT_PASSES` continues to mean C06 passes only. Reuse the
existing native lowerer, M02 views where appropriate, assembler/oracle tests
and hardware runner rather than building a general machine-IR framework.

**C07 preparation (2026-10-08):** a behavior-preserving refactor exposes the
[callable backend API](../src/codegen.h), separates the [CLI](../src/badge_codegen.c), and
extracts [frame/incoming-ABI policy](../src/codegen_frame.c) and [Thumb helpers](../src/codegen_thumb.c).
`cg_stack` remains the P01 seam; stack homes, scratch registers, saved registers,
PHI copies and outgoing AAPCS calls are unchanged. This preparation does not
implement P00-P06 or alter default selections. See the [module/API contract](implementation.md#c07-backend-seams-for-the-p-series).

| ID | Stage and bounded scope | Depends on | RP2350 benefit to test | Acceptance gate | Status |
| --- | --- | --- | --- | --- | --- |
| P00 | Measurement: fixed-seed before/after comparisons with fresh nonces; add small isolated arithmetic, branch/PHI, call, div/rem and ordinary-memory kernels | Existing timing runner and M01/M02 | Separates kernel work from fingerprints and identifies real Cortex-M33 costs | Identical sources, seed and work count for baseline/candidate; role swaps and at least two complete repeats; checksums/guards outside new kernel intervals; retain whole-module suite and raw samples/provenance | Done |
| P01 | C07 frame addressing: fold eligible stack homes into direct Thumb-2 base-plus-immediate loads/stores; retain existing address materialization for other offsets | Existing C07/C08 tests and timing runner | Removes offset materialization and address addition from repeated SSA spill accesses | Test zero, encoding boundaries, alignment, large-offset fallback, i64 halves, byval/outgoing arguments and reserved `r7`/`r12`; oracle encodings and ABI pass; fewer emitted address instructions and reproducible hardware benefit | Done (opt-in) |
| P02 | C07 block-local register residency for 32-bit integers/pointers; explicit scratch reservations, conservative boundary/call/asm flush and existing stack homes as fallback | M02, P00 | Reduces repeated SRAM loads/stores within hot blocks | Respect current `r0-r6` temporaries, frame-base `r7`, caller/callee saves and stack alignment; wide/aggregate values retain existing lowering; count eliminated reloads/stores; corpus, mixed ABI and hardware gates pass | Done (opt-in; mixed runtime) |
| P03 | C07 cross-block 32-bit register allocation, including loop-carried values and PHI edge parallel copies; no allocator expansion to aggregates or i64 pairs initially | P02 | Keeps loop state in registers across iterations rather than round-tripping through SRAM | Finite liveness/allocation work; forced-spill and register-pressure cases; cycles/critical edges, calls/asm, narrow normalization and irreducible CFG fixtures; frame/call-stack and hardware measurements required | Done (opt-in) |
| P04 | C07 native scalar selection: legal immediates/shifted address operands, single-use multiply-add patterns and adjacent matching quotient/remainder reuse | P00 | Uses Cortex-M33 forms to avoid materialization, temporary spills and duplicate hardware division | Per-pattern semantic/encoding tests and measured C07/hardware benefit; retain hardware variable `UDIV`/`SDIV`; no blanket multiply-to-shifts or magic-division replacement; overflow/poison flags and operand dependencies preserved | Done (opt-in) |
| P05 | C07 constant-size ordinary-memory copy/fill paths: proved alignment/provenance, word chunks and exact byte tails; only intrinsic paths that actually reach C07 | P00 | Uses aligned 32-bit SRAM transfers instead of byte-at-a-time loops where justified | Guard bytes, zero/tiny/odd lengths and alignment fixtures; no overread/overwrite, volatile/MMIO widening or changed access order for device memory; unchanged byte path when ineligible; defer if no reached corpus candidate | Done (opt-in) |
| P06 | Later C06 loop-invariant work removal: natural loops with an existing unique preheader; outside-loop operands and a small nontrapping pure whitelist | M06, M08, P00 | Avoids repeated constant/address calculations; may include integer division only with an explicit nonzero/overflow/flag proof | No loads, calls, volatile/MMIO, asm or general alias analysis; zero/one/many-iteration, nested-loop and ineligible irreducible cases; mandatory verifier and target execution; account for extra live ranges and zero-trip costs | Done (opt-in; mixed runtime) |

P03 qualification (2026-10-08): [cross-block allocation](implementation.md#p03-cross-block-register-allocation)
uses bounded CFG liveness/interference and three callee-saved registers
(r8/r10/r11; r9 reserved), conservative pressure spills and parallel PHI
edge moves with cycle breaking. Six-mode sanitizers, fully allocated and
spilled PHIs, zero/one/multiple rotation/critical-edge execution, indirect/
mixed/large-frame AAPCS, assembly and irreducible-loop gates pass, as do full
oracle and owned-only suites. All 36 C06 inputs/local frames/outgoing calls
are unchanged; all 38 objects match independent LLVM MC. Additional save
area is zero or eight bytes, preserving alignment; maximum frame including
saves is 3,680 bytes and unresolved stack bounds remain unresolved.
[Final corpus](../build/c06-metrics/p03-global-final/report.json) saves 42,332
nonvendor bytes (3.37%) versus the byte-identical P02 control.
Two role-swapped campaigns match all twelve LLVM results:
[first](../../build/compiler-compare-runs/run.BSiasa/results.json) and
[repeat](../../build/compiler-compare-runs/run.Gwdevd/results.json).
Controlled P02 comparisons improve all measured whole modules by 1.82-13.40%
and kernels by 6.74-21.44%, with repeat variation below 0.027%. Final counter
comments do not change any qualified machine output. Ordinary diagnostics
pass and both badges are restored to BOOTSEL. Defaults remain unchanged;
P06's separate qualification is recorded below.

P04 qualification (2026-10-08): [native scalar selection](implementation.md#p04-native-scalar-selection)
adds independent `legacy|thumb` policy with legacy unchanged by default.
Canonical unflagged i32 immediate/GEP patterns, adjacent single-use MLA and
matching div/rem reuse preserve flags/dependencies, original allocation and
hardware SDIV/UDIV. Twelve-mode sanitizer fixtures, independent LLVM/raw-IR
and mixed-ABI execution, full oracle, owned-only and driver gates pass.
A fresh legacy control reproduces P03 bytes; all 36 release/sanitizer modules
and 38 independent LLVM MC objects agree. IR, frames, saved registers,
outgoing arguments and calls are unchanged; max frame remains 3,680 bytes
and unresolved stack bounds remain unresolved.
[Corpus](../build/c06-metrics/p04-thumb/report.json) saves 13,468 nonvendor bytes
(1.11%). [First](../../build/compiler-compare-runs/run.BNic1c/p03-comparison.json)
and [repeat](../../build/compiler-compare-runs/run.CePptc/p03-comparison.json)
role-swapped campaigns match all twelve LLVM results and improve whole
modules 1.39-6.25% and kernels 1.11-12.65% versus P03; repeat variation is
below 0.036%. One collector-deadline campaign was rejected as incomplete;
a fresh complete retry passed without changing the deadline or policy.
Ordinary diagnostics pass and both badges are restored to BOOTSEL.
P04 remains opt-in; P05 qualification follows.

P05 qualification (2026-10-08): [word memory selection](implementation.md#p05-word-memory-selection)
adds independent `byte|word` policy, with byte unchanged by default.
Only nonvolatile constant-size copy/fill intrinsic paths with locally proved
stack-object extent and four-byte alignment use words plus exact byte tails.
Unknown/aligned parameters, MMIO, volatile, variable/negative offsets and
lost proofs keep byte lowering. All 24 policy combinations pass retained
sanitizer fixtures; length/tail/alignment/guard/nonzero-fill raw-IR execution,
mixed ABI, full oracle, owned-only and 25 driver/report tests pass.
Fresh byte output matches P04 exactly; 36 release/sanitizer modules and
38 independent LLVM MC objects agree. IR, frames, save masks and call models
are unchanged; max frame stays 3,680 bytes and unresolved bounds remain
unresolved. [Corpus](../build/c06-metrics/p05-word/report.json) reaches 73 copies
and 52 fills in seven modules and saves 336 nonvendor bytes (0.028%).
[First](../../build/compiler-compare-runs/run.vgmiQ7/p04-comparison.json) and
[repeat](../../build/compiler-compare-runs/run.9dWqFc/p04-comparison.json)
role-swapped campaigns match all twelve LLVM results. Cross-file ABI
elapsed time falls 24.82-24.84%; most other intervals are unchanged or
within small measured variation, not evidence of a universal memory speedup.
Repeat variation stays below 0.036%. Ordinary diagnostics pass and both
badges are restored to BOOTSEL. P05 remains opt-in; P06 qualification follows.

P06 qualification (2026-10-08): [loop-invariant motion](implementation.md#p06-loop-invariant-motion)
registers explicit-only `loop-invariant`, leaving the five-pass default
unchanged. Natural loops use existing unique preheaders and dominating
outside operands; topological inner/outer motion preserves CFG/PHIs/names.
A small unflagged nontrapping arithmetic/cast/address whitelist excludes
memory/calls/assembly and unproved shifts/division/overflow/poison flags.
Work is capped at 50M units and tracked storage at the existing 128 MiB.
38 sanitizer/LLVM/idempotence/scaling/resource fixtures, C/raw-IR zero-trip/
nested/irreducible/volatile/trap differential execution, mixed ABI, full
oracle, final owned-only and 25 driver/report gates pass.
[Corpus](../build/c06-metrics/p06-loop-final/report.json) preserves input/C05,
instruction/opcode/volatile counts, frames, save masks and call models;
all 36 optimizer/backend sanitizer outputs and 38 independent MC objects
agree. Fresh P05 output is byte-identical. P06 hoists 281 instructions
in 23 modules, with 13 extra nested moves; bytes grow 148 (0.0123%) and
68 more used values remain stack-backed under global allocation. Max frame
stays 3,680 bytes and unresolved bounds remain unresolved.
[First](../../build/compiler-compare-runs/run.4Lq5L2/p05-comparison.json) and
[repeat](../../build/compiler-compare-runs/run.p8jFi1/p05-comparison.json)
campaigns match all twelve LLVM results. Whole arithmetic improves 12.595%
and graphics 2.283-2.284%, but whole memory regresses 0.771-0.774%.
Repeat variation reaches 0.063%; pressure is measured without assigning
isolated causality. Ordinary diagnostics pass and both badges are restored
to BOOTSEL. P00-P06 are complete within scope; defaults remain unchanged.

P02 qualification (2026-10-08): [bounded local residency](implementation.md#p02-block-local-register-residency)
uses `r0` carry-through and already-saved `r8`, with unchanged homes, frames,
scratch reservations and wide/aggregate lowering. Cross-block/PHI homes are
write-through; calls/asm/control boundaries invalidate residency. Four-mode
sanitizers, work-limit failures, pressure/call/asm/large-frame/mixed/all-owned
ABI and optimizer execution pass. All 36 C06 inputs/frame models remain
identical; all 38 objects match independent LLVM MC. It removes 7,212 emitted
reloads and 6,963 stores, saving 27,716 nonvendor bytes (2.16%) versus P01.
Two final role-swapped campaigns match all twelve LLVM results:
[first](../../build/compiler-compare-runs/run.U9yRM5/results.json) and
[repeat](../../build/compiler-compare-runs/run.fXoiNK/results.json).
Controlled P01 comparisons show strings 35.97-36.07% faster and several
kernels 1.03-4.58% faster, but whole arithmetic 6.98% slower and other
small regressions. Initial r8-only move overhead was reduced with r0
carry-through; residual causes are not separately attributed. P02 remains
explicit-only, not an enabled default or a universal runtime win. Normal
diagnostics pass and both badges are restored to BOOTSEL.

P01 qualification (2026-10-08): [direct Thumb-2 frame addressing](implementation.md#p01-direct-thumb-2-frame-addressing)
is selected independently of C06 through `BADGE_CODEGEN_FRAME_ADDRESSING=direct`;
the default remains materialized. Both modes pass sanitizer, LLVM encoding and
mixed/all-owned Cortex-M33 ABI execution, including large frames, i64 halves,
byval/outgoing arguments and incoming offsets above 64 KiB. All 36 modules
retain identical C06 IR and frame/call-stack models. Eligible accesses remove
113,016 instructions (44.36% of emitted corpus instructions); aggregate
nonvendor bytes fall 1,748,444 -> 1,285,180 (26.50%). Two fixed-seed role-swapped
campaigns match all twelve LLVM results:
[first](../../build/compiler-compare-runs/run.91a8cd/results.json) and
[repeat](../../build/compiler-compare-runs/run.xPAgAz/results.json).
Controlled comparisons against the corresponding frozen P00 campaigns show
38.92-49.36% lower kernel times and 35.57-48.93% lower whole-module times,
with unchanged control images, sources, clocks and work. Ordinary full-frame
correctness passes; both badges are restored to ordinary diagnostics and
BOOTSEL. Existing unresolved entry-call stack bounds remain unresolved.

P00 qualification (2026-10-08): [six isolated kernels and timing boundaries](implementation.md#p00-isolated-kernel-baseline),
seven samples of 64 calls x 128 iterations, checksums/guards and every returned
value checked outside intervals, unchanged whole-module suite and timing safety
bounds. Normal/timing sanitizers, protocol/USB/runner/comparison guards and both
firmware builds pass. Two fixed-seed campaigns each complete role swaps with
all twelve results matching: [baseline](../../build/compiler-compare-runs/run.hxehji/results.json)
and [repeat](../../build/compiler-compare-runs/run.drXj3t/results.json).
Observed owned/LLVM isolated ratios span 8.142-17.307x; owned kernel medians
vary less than 0.02% across repeats. [Controlled comparison utility](../tools/compiler-timing-compare.mjs)
validates provenance/work/clocks/fingerprints and reports paired candidate/owned-baseline
change with LLVM drift separate. No optimization was enabled by P00 itself;
subsequent P01-P06 qualifications are recorded above.

M09 enables only the qualified conservative selection; SCCP, CSE and
standalone late DCE stay explicit-only with their recorded runtime caveats.
P00 enables controlled ablation; P01-P05's target-specific selections and
P06's bounded C06 motion are now separately qualified. They remain explicit
choices with measured tradeoffs, not automatic additions to M09's default.
Table order does not add dependencies.

RP2350 policy for all of these:

- Prefer native 32-bit work where source/IR semantics permit it. Never
  silently truncate i64 or change signed division rounding.
- Hardware multiply/divide already exist. A shift/add expansion is useful
  only if the resulting Cortex-M33 sequence, including materialization and
  spills, is cheaper; unsigned power-of-two division/remainder are candidate
  special cases, not permission to rewrite general signed division.
- Prefer legal immediate/address forms before introducing more temporaries.
  Thumb-16 width reductions may also help flash/XIP fetch footprint, but
  instruction reach, register limits and live condition flags must be proven.
  No flash/cache speedup is inferred from smaller bytes alone.
- Ordinary SRAM/flash buffers and volatile device registers are different.
  Do not widen, combine, duplicate, reorder or speculate MMIO/volatile
  accesses. Framebuffer RAM speed is not LCD/SPI speed.
- Keep the existing little-endian layout, ABI, reserved registers, secure
  mode, stack alignment and hardware clock configuration. No multicore,
  clock boosting or source-level native-helper substitution is counted as
  a compiler optimization.

## Release targets, not unmeasured promises

**M09 decision (2026-10-08):** the user approved replacing the proposed 20%
size target with measured C06 gains, leaving risky passes explicit-only and
retaining stack-readiness limitations. The current-source release measures
**1,995,340 -> 1,748,444 nonvendor bytes (12.3736%)** and maximum frame plus
saves **5,312 -> 3,672 bytes**. Every image/frame improves over the legacy
default and all six controlled version-2 runtimes improve. Ten unresolved
entry-call bounds still block a general production-stack safety claim.
The earlier targets below are historical hypotheses, not retroactively met
or a reason to extend M09 into C07/P-series work.

Proposed initial C06/M09 targets, to confirm or revise using M01 and
controlled hardware measurements:

- At least **20% reduction in aggregate non-vendor flash payload**:
  1,932,560 -> at most **1,546,048 bytes**. With unchanged payloads, that is
  at most **1,771,968 aggregate flash bytes**.
- Reduce the largest 5,312-byte local-frame-plus-saves allocation below
  **4,096 bytes**, while also reporting call-chain bounds. This target is not
  a stack-safety certificate; unresolved or oversized call chains remain
  explicit blockers for hardware readiness.
- No linked-image size or maximum local-frame regression in the default
  pipeline without an explicit, documented trade-off decision.
- Track cold build time and peak compiler memory. Investigate a repeatable
  build-time regression above 25% or a newly exceeded resource limit before
  enabling the responsible pass. Timing methodology is established in M01;
  a single noisy run is not a regression.
- Publish before/after hardware runtime for the affected workload and all
  six existing modules. Flag a repeatable slowdown above **5%** for explicit
  investigation/trade-off review, rather than default-enabling it because
  flash shrank. This is a proposed review threshold, not a statistical
  confidence claim or a promised speedup. Smaller stable gains still count.

These targets are hypotheses, not predictions. If C07 spill/frame behavior
prevents the frame goal, document the evidence and move that goal into a
separate C07 milestone; do not silently expand this C06 release.

For an optimization milestone, synthetic fixtures prove capability, but
**at least one real corpus image must show a reached-code size or frame
benefit** before default enablement in M09. A pass with no measured production
benefit remains opt-in/deferred even if its unit tests pass. A separately
proposed speed-follow-up item may justify a runtime benefit instead, with
size/frame trade-offs documented; it does not silently waive M09's gates.

## Measurement and correctness gates

### M01 report contract

Produce one report row per module/function or image, as appropriate, with:

- Source/tool/input hashes, target flags, pass list and generated build ID.
- Before/after alloca, load/store, PHI, branch and instruction counts.
- Candidate, transformed and ineligible counts with reasons for skipped cases.
- Linked binary bytes, vendor bytes and their separated difference.
- Local frame plus saves, outgoing-call allocations, and direct-call bounds;
  recursion and unknown/indirect edges must not appear as zero-cost edges.
- Build-time/memory measurements and the exact measurement method.
- For runtime work: fixed workload/seed and repetitions, baseline/candidate
  C06 and C07 selections, raw hardware samples, clock snapshots, minimum/
  median/maximum and role-swapped ratios. Separate LLVM-relative ratios from
  candidate-versus-owned-baseline improvement.

Use a stable build directory and identical packaged assets/generated headers.
Wi-Fi build IDs currently depend on build-directory filenames; control that
input rather than attributing a build-ID-only difference to optimization.
Only linked/reached code counts toward flash benefit: C10 already garbage
collects unused sections. Removing an already discarded function is not a
smaller badge image.

IR counts and emitted instruction counts are useful diagnostics, not proof
of runtime speed. Measure hardware execution only with explicit authorization,
fixed clock configuration/input and a documented method. The current runner
reports inherited timer ticks, not assumed microseconds or CPU cycles.
QEMU CPU fixtures are not a peripheral timing model; host QEMU wall time is
not badge runtime.

For a runtime ablation, keep sources, data volume, seed, warm-up and batch
counts identical across baseline and candidate. LLVM is an oracle/control,
not the baseline for attributing a particular owned pass's gain. Record both
comparisons. Use fresh nonces even with a fixed seed. M06 added the runner's
explicit `--seed=UINT32` control; P00 still owns the isolated-kernel campaign.
Independent random-seed runs alone are not a controlled
before/after pass measurement.

Keep new kernel checksums and USB/display servicing outside timed intervals,
consume their outputs after execution, and retain the no-LTO/separate-unit
boundary so discarded work cannot masquerade as speed. The existing whole
module timings include fingerprints and remain a separate end-to-end
workload check. Keep repetition counts equal for the two compilers; if a
test exceeds timer/watchdog bounds, reject it or version a smaller shared
workload, not independently shrink the slower compiler's work.

Warm-working-state results do not certify cold flash/XIP behavior, interrupt
latency, physical display rate, USB servicing or Wi-Fi/Bluetooth readiness.
Expand those measurements explicitly if a proposed layout/loop change needs
them; do not infer them from the current six timings.

M01 is implemented by [native IR measurements](../tools/badge_ir_metrics.c),
[ELF metadata](../src/badge_elf_info.c) and the host-only
[benchmark driver](../tools/benchmark-c06.mjs). See the
[qualified results, schema and limitations](implementation.md#c06-m01-reproducible-measurements).
Its per-call screen bound refines the original 1,512-byte summed estimate to
1,488 without changing firmware. Diagnostics and IMU direct-call estimates
also exceed 4 KiB; Wi-Fi remains unknown and already has an oversized local
frame. These findings do not certify hardware stack safety.

A subsequent [physical Wi-Fi smoke test](implementation.md#physical-wifiudp-smoke-test)
with an isolated 64 KiB stack initially timed out during AP startup.
Cooperative application repairs, isolated owned assembly primitives and
authorized 20M startup/40M smoke lifetime budgets now yield 200/200 owned
echoes in two final runs, without losses/drops. USB enumeration pacing and
ARP refresh have dedicated regression tests. A normal LLVM control passed
on repeat, but an earlier counter-inconsistent dataset was rejected;
reversed physical roles still fail under both compiler paths. These are
application/runtime repairs, not a C06 optimization milestone. M02+ were
pending at that Wi-Fi checkpoint; M00-M09 are now complete. Production stack
safety remains unqualified, and changed application
inputs invalidate comparison with historical M01 snapshots. Do not equate
smaller code/frames or a successful build with Wi-Fi readiness; hardware
timing/servicing and semantic qualification remain separate gates.

### Every transformation

The optional [automatic two-badge differential suite](implementation.md#automatic-two-badge-compiler-comparison)
now supplements these gates: LLVM on `9BBF88DD17D66E7D`, owned on
`08DA26F86E57F5EA`, six non-Wi-Fi arithmetic/string/memory/control/ABI/graphics
modules, fresh shared input seeds, strict live-USB records and automatic
BOOTSEL recovery. Three physical runs matched all module results. Use it
before/after future passes when the badges are attached and hardware testing
is authorized; its isolated 64 KiB stack and aggregate fingerprints do not
replace verifier, sanitizer, ABI/QEMU or stack-safety gates. This addition
is a regression gate, not an optimization milestone; M02's separate owned
analysis/verifier acceptance gates are now complete.

1. Retain canonical C05/C06 artifacts and isolate the named pass with
   `--passes=...`. Preserve mandatory legalization for executable tests.
2. Verify supported IR before/after changes with the mandatory M02 owned
   verifier; use LLVM verification independently throughout development.
3. Test positive and ineligible cases, integer width/sign boundaries, CFG/PHI
   corners, volatile/side-effect preservation and malformed input under
   ASan/UBSan. Do not conflate valid-but-ineligible cases with malformed IR.
4. Differentially execute defined-input fixtures before/after the pass on
   Cortex-M33. Use the existing legal baseline where raw C05 IR is not
   backend-ready, plus LLVM/native source-level references to reduce shared
   frontend/backend blind spots. Retain mixed and all-owned ABI tests.
5. Build all 36 modules and 11 images without LLVM target tools. Continue
   independent assembly/object/linker and UF2 checks. Optimized images need
   not match pre-optimization bytes: semantic equivalence and correct
   downstream encoding are the requirements.
6. Preserve atomic output/error handling and bounded allocation/worklists.
   Limit exhaustion is an explicit failure, not partial "successful" output.
7. Measure the pass individually and in its intended pipeline position.
   Rebuild analyses after mutations initially; caching/invalidation frameworks
   are not required for this release.
8. For a runtime-oriented change, perform controlled owned-before/after
   hardware measurements as above, with LLVM correctness/reference retained.
   Name the affected emitted instructions/traffic, but do not claim the
   entire LLVM gap is explained by that observation.

### Pipeline policy

M09 freezes the measured default order:
`mem2reg,instruction-combine,constant-fold,aggregate-memory,wide-division`.
The later [Z04 qualification](maze-optimization.md#z04-candidate-contract)
replaces that combiner slot with `scalar-cleanup` after repeated hardware
and playable-maze approval. The other slots/order are unchanged, and the
old profile remains an explicit control.
Standalone late DCE and SCCP cleanup expose repeatable string timing penalties;
CSE has its independent memory penalty. They remain explicit-only. No
post-legalization cleanup is implicitly added; internal M05/M07 cleanup
remains part of those verified passes. Root LLVM compiler selection is unchanged.

Use finite convergence/worklists inside passes. Any outer repeat-to-fixpoint
schedule needs an explicit bound and a reported convergence result; avoid
indefinitely rerunning the entire pipeline. M09 publishes the exact default order, every initial optional-entry ablation,
and final-context corpus/hardware ablations for both selected optimizations.

Existing commands remain:

```sh
make -C badge-compiler test-owned
make -C badge-compiler test-oracle
bash badge-compiler/tools/oracle.sh
bash badge-compiler/scripts/build-owned.sh
```

Tool paths may be supplied as documented in the [README](implementation.md).
Host tests and `benchmark-c06.mjs` never flash devices. The explicitly
selected `compiler-compare.mjs [--timing]` hardware command does replace both
named applications and requires authorization and BOOTSEL preflight. The
existing screen smoke test covers the old image only; optimized images
require a separately authorized hardware test, including timing-sensitive
initialization.

## Stop conditions and later work

Stop the initial C06 release at M09. The proposed P00-P06 follow-ups are
separately authorized implementation steps, not a new completion requirement.
If a milestone's measured gain is absent or its
supported scope would require broad alias/loop/interprocedural analysis,
defer it with evidence rather than extending it indefinitely. Do not add
more infrastructure without a concrete next pass that needs it.

After publishing the results, select one follow-up at a time from measured
bottlenecks, using the RP2350 tracker above rather than opening all items at
once. Other candidates remain:

- Small-aggregate scalar replacement or narrowly scoped stack-object memory
  forwarding if residual aggregate traffic dominates.
- Bounded small-leaf inlining if call overhead/constant arguments dominate,
  with a strict code-growth budget.
- Further C07 register-allocation/frame-quality work beyond P02/P03's
  initial 32-bit scalar scope, only if measured bottlenecks justify it.

No proposed follow-up is implicitly implemented or enabled by this document.
General loop optimization, vectorization, global alias analysis and pursuit
of LLVM parity remain out of scope.
