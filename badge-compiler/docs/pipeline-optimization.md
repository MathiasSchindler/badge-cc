# Pipeline optimization for the RP2350 badge

Proposed on 2026-10-10, following the completed D00-D09 work and fresh
maze pipeline comparisons. PO00-PO09 are implemented and qualified; PO09 promotes the independently
qualified PO06+PO07 owned profile. The PO10-PO18 follow-up below was added on
2026-10-11 from the cold-booted red-square experiment and emitted-code inspection.
PO10-PO17 are now implemented and host-qualified. PO18 independently qualifies
the combined chain, mitigates initialization timing and promotes
SSA/eight-pass/direct/global-compact/thumb-widths/word owned defaults.
See [current promotion and cached physical evidence](current-promotion.md);
the retained milestone measurements below describe their original controls.

## Objective and scope

Improve the generated code for freestanding C on the GitHub Universe badge's
RP2350 Arm Cortex-M33. Prioritize measured application runtime, while tracking
linked bytes, RAM, stack demand and host compilation cost separately.
There is no requirement to support another language, RISC-V or a generic
multi-target architecture, nor a promise of globally optimal binaries.

The goal is a fully optimized compiler for this target, **not a 1:1 clone of
LLVM or a stopping point at LLVM parity**. Aim to surpass LLVM in linked size
and/or measured execution performance wherever general, correct,
RP2350-specific optimization permits. LLVM's representations, pass order,
instruction choices and allocation strategies are references to investigate,
not requirements to reproduce or ceilings on improvement.

Exploit the narrow C/ABI/platform scope and verified RP2350 properties when
they enable better results. Alternative IR, target-specific selection,
allocation and cross-stage decisions are welcome if their assumptions are
explicit and they benefit ordinary programs rather than recognized benchmarks.
Judge them by preserved C semantics, real hardware results and resource
budgets, not similarity to LLVM output.

Size and speed can conflict: report both and retain explicit policy tradeoffs
where neither candidate dominates. Beating LLVM on one reached workload does
not establish universal superiority, and matching it does not end the search.

Firmware remains dependency-free and requires no standard C library.
LLVM is a development oracle and comparison tool, not a new dependency of
the owned production pipeline.

The current owned baseline, following PO09 promotion, is:

- Frontend: `ssa`.
- Backend: `direct/global-compact/thumb-calls/word`.
- C06: `mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory`.
- Separate opt-in D04-D07 candidates remain available.

Earlier tables and individual PO00-PO09 campaign statements describe their
historical controls. Preserve those explicit controls and immutable reports;
do not reinterpret them as measurements of the new baseline.

Do not combine candidates to hide regressions or change the maze's algorithms,
fixed-point arithmetic, rendering quality or expected results to manufacture
a compiler win. Do not emit special code for benchmark names, source paths,
seeds or known answers. Small adversarial tests remain correctness guards;
they are not the principal performance acceptance workloads.

This plan complements, rather than reopens, the
[datasheet plan](datasheet-plan.md) and
[D09 qualification contract](datasheet/qualification.md).
D08's SRAM, scalar SMULL and GPIO-helper follow-ups remain separately scoped;
FPU/DCP features and RCP hardening are not prerequisites.

## Starting evidence

See [current hardware results](datasheet/qualification.md#current-default-versus-llvm-2026-10-10)
and [actual maze stage isolation](datasheet/qualification.md#actual-maze-stage-isolation-2026-10-10).
The generated evidence is local build data, not committed source:

- [Fresh two-badge default/LLVM benchmark](../../build/current-badge-benchmark-I6HhpA/results.json).
- [Verified maze stage analysis](../../build/maze-stage-comparison-sWm4hl/analysis.json).
- [Complete stage capture](../../build/maze-stage-comparison-sWm4hl/results.json).

| Observation | Implication and limit |
| --- | --- |
| Six module workloads take 3.65-6.26x as long with the current owned compiler as with Clang 22.1.8 `-Oz` | A substantial hardware gap remains; these are suite workloads, not whole applications |
| All 5,170 maze preprocessing tokens match | No demonstrated preprocessing cause for this maze; not proof for every C source |
| Both ASTs contain the same 29 function definitions | Reassuring inventory; different schemas and no complete cross-frontend semantic proof |
| Initial IR has 212/149 allocas and 0/27 PHIs for owned/Clang | Lowering already differs before optimization; instruction counts are not timing estimates |
| Identical owned optimized IR yields 24,960/5,040 object `.text` bytes with owned/LLVM backends | Major static backend disparity independent of frontend and assembler; not a 4.95x measured runtime result |
| Owned initial IR through LLVM optimization/backend yields 4,708 bytes; Clang initial IR yields 4,588 | LLVM recovers much of the representation difference here; includes differing frontend metadata/attributes |
| Same actual owned assembly gives identical linked flash through both assemblers | Assembler is a control boundary, not the demonstrated cause |
| Same owned objects give identical flash through both linkers | No demonstrated owned-object linker gap |
| Same LLVM objects differ only in mergeable string order and corresponding pointers | Explained placement difference, not different instructions or an established runtime regression |

Object `.text` excludes readonly data and can contain functions discarded
at link time. Linked text includes readonly data and flash-end sections.
Neither measure alone assigns responsibility for application runtime.

## Milestones and dependencies

Dependencies below are actual prerequisites, not an instruction to implement
every speculative optimization. PO02 chooses the bounded backend opportunity;
PO03/PO04 may be deferred if their proposed mechanism is not supported by data.

| ID | Milestone | Prerequisites | Status |
| --- | --- | --- | --- |
| PO00 | Reusable actual-source stage comparison | Existing stage evidence | Done; fresh 8-module maze and 37-module/12-image corpus captures, explicit divergences retained |
| PO01 | Reached application timing on both badges | PO00 capture/provenance contract | Done; six actual-source workloads, two seeds, four role-swapped rounds, matched raw reports and verified full-flash restoration |
| PO02 | Attribute backend expansion on fixed IR | PO00, PO01 | Done; 11-module exact-IR crossover, byte-preserving emission traces, semantic checks and four paired hardware rounds; bounded compare/branch opportunity selected |
| PO03 | General target lowering and selection improvements | PO02 identifies selection/expansion costs | Done; opt-in compare/branch fusion, matched corpus and repeated physical timing; defaults unchanged |
| PO04 | Register, spill and frame traffic improvements | PO02 identifies allocation/home costs | Done; opt-in home/PHI transport improvements, exact-IR replay, corpus and repeated physical timing; defaults unchanged |
| PO05 | Cleaner initial IR and semantic frontend checks | PO00 | Done; opt-in SSA expressions, cross-frontend semantic checks, 37-module corpus and 1,344 matched physical samples; frame tradeoff explicit |
| PO06 | General C06 profitability improvements | PO00, PO01; PO05 where relying on new IR forms | Done; bounded local load reuse, identical-input optimizer/emission evidence and 1,344 qualified physical samples; broader regressions retained, defaults unchanged |
| PO07 | ABI and measured cross-function economics | PO02, PO01; relevant PO03/PO04 results | Done; opt-in call transport, bidirectional ABI and 1,344 physical samples; five qualified workload wins, no resolved regressions, defaults unchanged |
| PO08 | Assembler/linker control-boundary audits | PO00 | Done |
| PO09 | Independent corpus/hardware qualification and promotion | Each candidate's completed milestone; PO01 and relevant PO08 checks | Done; qualified PO06+PO07 promoted to owned defaults |
| PO10 | Attribute small-function costs and establish delta-focused gates | Completed PO09 and red-square evidence | Done; fixed-IR 872-byte backend gap, six helper ledgers and 37-module/554-function production inventory |
| PO11 | Instruction-scoped assembly/effect facts | PO10 | Done; host-qualified `thumb-effects` candidate, hardware-timing gate retained |
| PO12 | Lightweight ABI frames and caller-saved residency | PO10; PO11 for assembly-sensitive cases | Done; host-qualified `thumb-frames`, physical-timing promotion gate retained |
| PO13 | Effect-safe CFG, branch predicates and counted-loop selection | PO10; PO11 for assembly-sensitive cases | Done; host-qualified `thumb-control`, physical timing still gated |
| PO14 | Address/constant reuse and direct operand transport | PO10; PO11 for assembly-sensitive cases | Done; host-qualified `thumb-operands`, physical timing still gated |
| PO15 | Pressure-aware pure loop motion and induction lowering | PO10, PO12, PO14 | Planned |
| PO16 | Costed sharing, specialization and call economics | PO10, PO12; PO11 where assembly/effects are involved | Planned |
| PO17 | Final-width size/speed selection on the simplified backend | PO12, PO13, PO14 | Planned |
| PO18 | Independent general-purpose qualification and promotion | Each candidate's completed milestone; relevant interaction checks | Done; cached combined-chain qualification, cold-boot timing mitigation and consistent owned-default promotion |

PO09 is repeated per candidate. It need not wait for all research milestones.
An explicitly documented no-go/defer is an acceptable outcome for a
speculative opportunity; it is not permission to skip correctness gates.

### PO00 - Make stage isolation reproducible

Turn the session's actual-maze experiment into maintained host tooling using
the existing compiler drivers and inspectors, without a parallel build system.

ToDos:

- [x] Capture preprocessing, normalized tokens, AST/typed-AST inspection,
  initial IR, optimized IR, assembly, objects and final image for actual
  application sources.
- [x] Include all maze dependencies and the currently enumerated full corpus;
  record the actual module/image set instead of hard-coding historical counts.
- [x] Preserve actual build flags, preprocessing definitions, target/ABI,
  linker script, source/tool hashes and candidate policy selections.
- [x] Capture Clang initial IR with its optimizer disabled; distinguish C
  frontend folding from later LLVM optimization.
- [x] Normalize only documented nonsemantic differences: filenames, locations
  and tool bookkeeping. Never erase volatile access, types, alignment,
  overflow flags, attributes or instructions to force equality.
- [x] Automate same-preprocessed-input frontend, same-IR backend,
  same-assembly assembler and same-object linker crossovers.
- [x] Retain explicit unsupported-input diagnostics. Do not silently fall back
  to LLVM or imply the owned backend accepts arbitrary LLVM IR.
- [x] Write machine-readable summaries with hashes and links to raw artifacts;
  failed attempts must remain failed and inspectable.

Acceptance: fresh actual-maze capture reproduces the known token and
same-assembly equivalence; every divergent boundary has an explicit
explanation or open issue. The tool does not change ordinary production
output, add firmware dependencies or require new fixtures.

#### PO00 implementation and reproduction

[pipeline-stages.mjs](../tools/pipeline-stages.mjs) delegates actual compilation
and linking to the existing Makefile and `owned-cc.sh`. Its host-only compiler
wrapper records the exact successful/failed invocations; it does not introduce
an alternative production compiler or build graph. Corpus scope builds the
normal `all` target plus USB Wi-Fi UDP, then enumerates their actual C/assembly
modules and linked images. It does not hard-code the module/image count.

From the workspace root, with the existing LLVM host tools installed:

```sh
export LLVM_BIN=/opt/homebrew/opt/llvm/bin
export LLVM_LLD=/opt/homebrew/bin/ld.lld
make -C badge-compiler test-pipeline-stages
node badge-compiler/tools/pipeline-stages.mjs --capture build/po00-new-maze maze
node badge-compiler/tools/pipeline-stages.mjs --capture build/po00-new-corpus corpus
node badge-compiler/tools/pipeline-stages.mjs --verify build/po00-new-corpus/results.json
```

Output directories must be new; existing directories/files/symlinks are
refused without overwriting evidence. `--capture` defaults to `corpus`.
Use `LLVM_CLANG`, `LLVM_OPT`, `LLVM_LLD`, `LLVM_OBJCOPY`, `LLVM_NM` and
`LLVM_SIZE` to override individual tool paths. Unset `BADGE_OPT_PASSES` and
`BADGE_CODEGEN_*` to capture defaults; explicit selections use the established
driver environment and are recorded. Ordinary firmware builds do not acquire
a Node or LLVM dependency from this development tool.

The capture retains:

- Exact flags/defines, sources, tool executables, selected policies, generated
  headers, linker scripts, logs and SHA-256 manifests.
- Both preprocessors' text/tokens, owned AST/typed-AST views, Clang AST,
  verified initial IR, actual optimized IR, assembly, objects and final images.
- Same-input frontend attempts, fixed-owned-IR backend metrics, reassembly
  of actual owned assembly, and both linkers on each identical object pool.
- Explicit rejected crossovers and unresolved comparisons. A missing output
  is represented by a rejected attempt and a `null` comparison, never equality.

Already applied `-I`/`-D`/`-U` options are omitted only when Clang reads
preprocessed input; target, semantic and warning settings remain intact.
IR comparison normalization removes only ModuleID/source filename and
integer `!srcloc` coordinates; annotation types/arity remain intact.
AST location omission and token normalization use the existing owned tools.
Inline keywords, build IDs, attributes, arithmetic flags, types, alignment,
volatile effects and instructions are never removed to make captures agree.

`qualified-stage-capture` means the capture completed, not that all
crossovers agree or that a candidate is speed/semantics qualified.
`--verify` checks the current source/executable hashes and all retained
artifact hashes without compiling or accessing badges. Source/tool drift
is an explicit verification failure requiring a fresh capture.

Qualification evidence:

- [PO00 result](../build/pipeline/po00-results.json).
- [Fresh maze capture](../../build/po00-verified-maze/results.json):
  8 C modules, one assembly input, one image and 470 hashed artifacts.
- [Fresh full-corpus capture](../../build/po00-verified-corpus/results.json):
  37 C modules, two assembly inputs, 12 images and 2,333 hashed artifacts.
- Nine [host regression tests](../tests/pipeline-stages.test.mjs) pass,
  including normalization guards, corrupted/missing evidence, failed captures,
  existing-directory refusal and multicall-tool symlink handling.

The fresh maze reproduces all 5,170 equal tokens, identical same-frontend
crossovers and the 24,960/5,040-byte fixed-IR backend comparison.
All 12 corpus images give identical complete flash for both same-assembly
and same-owned-object linker comparisons; all original LLVM links replay.
119 production/core source hashes remain unchanged. Eleven ordinary image
binaries match the D09 baseline exactly; USB Wi-Fi UDP differs in generated
build identity, with only its three identity arguments and source filename
changed in optimized IR. The raw difference remains recorded.

The full corpus also retains **61 explicit issue records**, not 61 newly
diagnosed compiler bugs:

- Ten preprocessing and ten parser-tree differences: the byte-helper header
  selects six `inline` keywords for Clang but static functions for owned code.
  USB Wi-Fi UDP additionally has different generated build IDs.
- Thirty optional frontend rejections: owned semantic/IR handling rejects
  Clang's inline declarations; Clang's unchanged `-Werror` policy rejects
  unused static helpers in owned-preprocessed input. Neither warning policy
  nor C tokens are weakened to obtain a crossover.
- Eleven LLVM-object linker byte differences require PO08 placement/relocation
  review, including two unequal-size outputs. Named function layouts match,
  but that alone is not a proof about every changed byte. The maze's exact
  binary hashes match the prior fully verified string/relocation audit.
  PO08 now provides a maintained full-corpus linked-value/layout audit that
  explains these boundary differences; earlier frontend/IR issues remain open.

All 37 reverse Clang-optimized-IR backend attempts are explicitly rejected
with retained diagnostics; arbitrary LLVM IR support is not claimed.
The earlier failed capture attempts remain preserved as failures.
No badges were programmed, compiler defaults changed or speed claim added.

### PO01 - Measure reached real application work

Extend measurement coverage beyond the existing compiler suite. Instrument
actual application functions; do not replace them with simplified copies.

ToDos:

- [x] Measure maze generation, physics/collision/hazards, RAM rendering and
  display transfer separately, plus the reached end-to-end frame path.
- [x] Distinguish direct function timing from driver/checksum/instrumentation
  overhead. Keep validation outside timed intervals where possible and
  report unavoidable included work.
- [x] Vary runtime inputs: maze seeds and supported levels, tilt trajectories,
  wall/hazard encounters, clipping, dirty regions and redraw sizes.
- [x] Make scripted inputs reproducible while preserving normal application
  semantics; include physical IMU/display behavior as a separate coverage gate.
- [x] Select unrelated reached production workloads from the actual corpus,
  such as diagnostics, graphics and IMU processing, with documented coverage.
- [x] Build controlled LLVM and owned variants with equivalent configuration.
  Keep `-Oz` as the historical control; label any additional `-O2`/`-O3`
  comparison separately rather than changing the reference silently.
- [x] Use both selected badges, repeated role swaps, fixed input sequences,
  fresh nonces, retained raw timings and captured clock settings.
- [x] Distinguish warm-XIP, cold/startup and display/bus-bound behavior where
  measured; do not infer cold performance from warm batches.
- [x] Snapshot full original flash before writes; restore and independently
  reread/hash-verify it even after a failed collection.

Acceptance: matched outputs and guarded memory, repeatable paired timings
for reached application work, explicit coverage gaps and verified restoration.
Report raw ticks unless independently calibrated; no invented absolute clock
rate or synthetic aggregate "badge score."

#### PO01 implementation and reproduction

The maintained [application benchmark](../tools/application-benchmark.mjs)
wraps the existing [paired runner](../tools/compiler-compare.mjs) with fresh
full-flash snapshots and independently verified restoration, including failure
paths. Its isolated application protocol uses suite `0x41505030`, workload
version 4 and 16 CRC-protected records; legacy suite/version behavior remains.
The [actual-source harness](../../src/tools/compiler_test/application.c)
includes the real maze implementation, not a simplified algorithm. Production
render decision logic is extracted into one shared `render_frame` function,
called by both the normal game and the benchmark; timing is opt-in only.

```sh
make -C badge-compiler test-application \
  LLVM_CLANG=/opt/homebrew/opt/llvm/bin/clang \
  LLVM_LLD=/opt/homebrew/bin/ld.lld \
  QEMU_ARM=/opt/homebrew/bin/qemu-system-arm

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
node badge-compiler/tools/application-benchmark.mjs --measure \
  9BBF88DD17D66E7D 08DA26F86E57F5EA

node badge-compiler/tools/application-benchmark.mjs --verify \
  build/application-benchmark-runs/run.ymQNpm/results.json
```

`--measure` replaces applications temporarily: both selected badges must start
in BOOTSEL. It snapshots **both** complete 16 MiB images before any campaign,
attempts restoration for each backed-up device even if another operation fails,
and leaves both in BOOTSEL. Do not remove retained backups. `--verify` recomputes
raw-record validation, requests, clocks, paired ratios, image hashes and full
readback hashes. `--verify-current` additionally rejects current-source drift.
The lower-level `compiler-compare.mjs --application` does **not** provide the
snapshot wrapper; prefer the maintained preservation command.

[PO01 measurements and coverage](application-measurements.md) report six
workloads, seeds `3276377847`/`123456789`, four role-swapped rounds, eight raw
reports and 336 timing samples. All results match, clock settings match and
both full originals are restored. Owned/LLVM ratios range from 4.252x RAM
rendering to 10.599x input/physics. There is no compiler promotion.

Physical IMU acquisition/motion, visually certified panel output and
cold/startup timing are explicitly **unqualified** gates, not checked-off
hardware certifications. The frame measurement reaches actual rendering and
panel writes but excludes live I2C input and main-loop pacing. The failed
initial overlong physics batch is retained separately with verified restoration;
it contributes no accepted timing. PO00 captures remain historical capture-time
evidence: PO01 source/Makefile edits intentionally cause their current-source
verifier to report drift, without rewriting old provenance.

### PO02 - Explain expansion after fixed optimized IR

Investigate the largest observed static disparity before choosing a fix.
Use identical IR and real application profiles to avoid blaming the frontend
for backend behavior.

ToDos:

- [x] Compare owned/LLVM output per reached function using the same optimized
  IR and equivalent ABI, with the LLVM IR optimizer disabled for this crossover.
- [x] Measure executable bytes, emitted instruction counts, dynamic execution
  where observable, stack loads/stores, copies, calls, branches and prologues.
- [x] Separate actual spills/reloads from parameter homes, private-frame
  objects, PHI transfers, scratch preservation and required volatile accesses.
- [x] Trace hot code to the responsible IR operation and structured emission
  decision; retain examples from real source rather than a recognizer rule.
- [x] Determine whether widening/narrowing, address generation, scalar homes,
  PHI transport, aggregate operations or call preservation explain the cost.
- [x] Execute relevant hybrid images with semantic checks before treating
  their hardware timing as valid; previously linked hybrids are not certified.
- [x] Rank opportunities by reached runtime cost and explainable mechanism,
  not the largest static object or an average ratio alone.
- [x] Select a bounded first change and record its legality, expected mechanism,
  negative cases, work bounds and rejected alternatives.

Acceptance: a hash-bound attribution report identifies a concrete general
mechanism on reached real workloads, with a measurable experiment and
without claiming code-size ratios are runtime ratios.

#### PO02 implementation and reproduction

The [fixed-IR backend tool](../tools/backend-attribution.mjs) reuses the normal
owned driver/build graph, canonicalizing only `source_filename`. Both backends
consume byte-identical owned optimized IR per C module; LLVM's normal IR
optimizer is disabled, but target legalization/machine passes remain enabled.
Both images use the owned linker and original owned assembly inputs, isolating
the C backend rather than assembler/linker differences.

The isolated `badge-codegen-attribution` build annotates IR line boundaries,
slot/object/parameter reservations and allocator decisions. Every annotated
object is **byte-identical** to its normal owned counterpart. Normal compiler
builds gain no trace output or optimization-policy change.

```sh
LLVM_BIN=/opt/homebrew/opt/llvm/bin \
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
make -C badge-compiler test-backend-attribution

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
node badge-compiler/tools/application-benchmark.mjs --measure-fixed-ir \
  9BBF88DD17D66E7D 08DA26F86E57F5EA

LLVM_BIN=/opt/homebrew/opt/llvm/bin \
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
node badge-compiler/tools/backend-attribution.mjs --capture NEW_DIRECTORY \
  build/application-benchmark-runs/run.PnNksy/results.json

node badge-compiler/tools/backend-attribution.mjs --verify \
  badge-compiler/build/po02-release-attribution/results.json
```

[PO02 attribution and first-change contract](backend-attribution.md) publishes
the hardware crossover, per-function traffic, IR/emission examples, category
limits and ranked opportunities. The first selected PO03 experiment is
**single-use adjacent i32 compare/conditional-branch fusion**, not a benchmark
recognizer, new default or claim that all stack accesses are spills.

Four role-swapped rounds, eight raw reports and 336 timing samples pass:
owned/fixed-IR-LLVM ratios remain 3.503-5.730x across these reached workloads.
Both fresh full originals are independently restored/hash-verified and left
in BOOTSEL. Dynamic retired-instruction sampling is unavailable here; static
machine counts and analytically known loop visits are labeled separately from
measured workload ticks. Physical IMU, visual/electrical panel and cold-start
qualification remain separate gates. Parser-correction captures are retained
as superseded analysis, not accepted evidence; the final report recomputes its
metrics from hash-bound traces/disassemblies.

### PO03 - Improve general target lowering and selection

Implement only opportunities supported by PO02.

ToDos:

- [x] Reduce redundant instruction sequences for integer/boolean operations,
  extensions, comparisons and address calculations where semantics permit.
- [x] Reuse structured target effects and the shared RP2350 cost model;
  account for APSR, scratch registers, dependencies and immediate legality.
- [x] Check whether repeated constant/address materialization can safely
  reuse an available value without increasing spills or crossing barriers.
- [x] Check compare/branch and PHI-transfer opportunities across actual
  control flow; preserve edge semantics and parallel-copy correctness.
- [x] Audit aggregate copy/zero expansion and general memory lowering;
  preserve alignment, overlap, bounds and freestanding behavior.
- [x] Keep volatile/MMIO accesses and inline assembly conservative.
- [x] Preserve explicit baseline selection, callable API/CLI/driver consistency
  and bounded compiler work.

Acceptance: semantic and ABI checks pass, linked budgets fit, the intended
instruction traffic actually disappears, and paired application timing
confirms the benefit. Narrower encodings alone do not earn speed promotion.

### PO03 implementation and qualification

[Contract, evidence and reproduction](compare-branch.md) document the opt-in
`thumb-branch` candidate. It fuses adjacent single-use i32 comparison/branch
pairs under `global-compact`, preserving local APSR consumption and exact
post-conditional PHI transport. Other residency policies, assembly,
nonadjacent/multiple-use conditions and narrow/wide comparisons retain the
old path. Allocation is unchanged; constant/address reuse and aggregate-memory
rewrites were audited and deferred rather than added without legality and
spill evidence.

All 11 historical baseline objects remain byte-identical. Exact IR replay
removes 3,206 emitted instructions across 458 comparisons. Fresh corpus
qualification covers 37 C modules, 12 images, 554 frame/call checks per
profile and mixed LLVM/owned ABI execution, without frame, spill or image
growth. Ordinary maze linked text falls from 41,284 to 37,068 bytes (10.21%).

Eight preservation-wrapped campaigns produce 1,344 actual-badge timing samples.
All six workloads improve outside the exact per-board/per-repeat noise
envelope for both seeds, with no regressions: elapsed time falls
5.11-29.84%, including 19.06% generation, 25.86% RAM rendering and 8.83%
reached frames. The ordinary Clang comparison still shows a 3.152-8.855x
gap; LLVM is not a stopping point. Both fresh full originals are restored,
independently hash-verified and left in BOOTSEL. Default promotion is not part
of PO03 and remains reserved for PO09.

### PO04 - Reduce register, spill and frame traffic

Do not presume every stack access is a spill or repeat previously rejected
allocation heuristics without new evidence.

ToDos:

- [x] Inspect actual liveness, interference, call clobbers and home assignment
  on the hot functions identified by PO02.
- [x] Remove unnecessary private scalar homes and reloads when value ownership,
  address escape and control-flow semantics make this safe.
- [x] Improve copy/coalescing and PHI transport where compatible with existing
  scratch, edge-transfer and frame reservations.
- [x] Evaluate rematerialization versus keeping a constant/address live using
  reached costs, extra live ranges and induced spills.
- [x] Account for call-crossing values, callee-saved registers and prologue/
  epilogue overhead; do not optimize one instruction in isolation.
- [x] Preserve AAPCS stack alignment, outgoing arguments, callbacks and
  bidirectional LLVM/owned calls.
- [x] Recompute frame/call metadata and conservative stack uncertainties.
  Main-RAM headroom is not scratch-stack capacity.

Acceptance: intended dynamic/frame traffic decreases without hidden scratch
or call-preservation bugs. No new frame growth or uncertainty may be hidden;
any tradeoff needs explicit budgets and promotion review.

### PO04 implementation and qualification

[Scalar-home/PHI contract, evidence and reproduction](scalar-homes.md) document
the opt-in `thumb-homes` selector, based explicitly on PO03 `thumb-branch`.
It omits unused/register-owned non-addressable homes while retaining
inline-assembly spill homes and real objects. Simple PHI literals materialize
directly after old-register consumers are resolved; cycles retain r6 and
unsupported/wide/stack sources retain buffering. Allocation/coalescing
priorities and callee-save sets are unchanged. General address/live-constant
rematerialization was evaluated and deferred rather than adding new live
ranges or a speculative spill heuristic.

All 11 exact-IR PO03 baseline objects remain byte-identical. Across 140 traced
functions, 3,798 homes, 324 emitted loads and 324 stores disappear, and summed
frame reservations fall 2,384 bytes. Generation's PHI-buffer traffic falls
from 45+45 loads/stores to 1+1. Fresh qualification covers 37 C modules,
12 images, 554 frame/call checks per profile and bidirectional LLVM/owned ABI
execution: 542 frames shrink, none grows and uncertainties are unchanged.
Ordinary maze text falls from 37,068 to 35,868 bytes, another 3.24% against PO03.

The fresh 1,344-sample hardware experiment isolates PO04 against PO03.
Generation improves 2.00%, physics/input 1.67% and diagnostics/graphics 0.59%
outside exact per-board/per-repeat envelopes for both seeds. Frames improve
only for the dense seed; RAM rendering and panel transfer remain within
noise. No regressions are detected. An earlier USB transport failure is
retained as rejected evidence, with verified restoration; only the fresh
complete experiment qualifies. Both full originals are restored,
independently hash-verified and left in BOOTSEL. Defaults remain unchanged;
PO09 retains independent promotion responsibility.

### PO05 - Produce cleaner initial IR

Improve C lowering generally, not by copying Clang's AST schema.

ToDos:

- [x] Extend cross-frontend checks of function signatures, target type/record
  layouts, integer promotions and representative typed expressions.
- [x] Lower condition values directly as booleans where valid, avoiding
  unnecessary `i1 -> i32 -> i1` round trips.
- [x] Evaluate direct PHIs for conditional/logical expressions instead of
  temporary result slots, preserving evaluation order and short-circuiting.
- [x] Avoid redundant loads/stores and temporary objects where C semantics,
  address escape and lifetime requirements permit.
- [x] Preserve signed/unsigned arithmetic, casts, aggregate layout, volatile
  effects, pointer operations and the supported C subset.
- [x] Add only justified attributes/metadata. Never copy Clang's `nsw`,
  `noundef`, alias or alignment assumptions without semantic proof.
- [x] Compare resulting initial and optimized IR through both execution routes
  and both accepted backend routes; check negative diagnostics.

Acceptance: LLVM-valid IR, preserved supported C behavior and demonstrably
cleaner lowered constructs across actual sources. Count reductions alone do
not establish hardware benefit; affected passes/backend contracts remain valid.

### PO05 implementation and qualification

[SSA expression lowering, semantic contracts and evidence](ssa-expressions.md)
document opt-in `--expression-lowering=ssa` /
`BADGE_IR_EXPRESSION_LOWERING=ssa`. Conditions retain i1 where semantically
valid; scalar conditional/logical joins use PHIs, preserving final semantic
types, actual nested predecessors and short-circuit evaluation. C boolean
values still become C int where required. Aggregate copies, void effects,
casts, volatile operations and ABI remain intact; no speculative attributes
are added. Legacy frontend and optimizer/backend defaults are unchanged.

Across the same 37 actual preprocessed modules, owned signatures are unchanged
and 36 modules have cleaner initial IR. All 37 legacy modules match archived
PO04 frontend output after only documented source/build-ID normalization.
The candidate removes 1,097 allocas/loads, 2,194 stores, 3,101 comparisons
and 3,075 boolean extensions; it adds 1,097 scalar PHIs. The independent
target oracle covers promotions, scalar signatures, record/union layout,
pointer width, nested effects, casts, void arms and aggregate snapshots.
Both frontend policies pass native sanitizer/mutation/negative tests, and
both LLVM/owned optimizer routes and accepted backends execute correctly.

The 37-module/12-image corpus passes linked RAM/flash budgets and 554
frame/call checks per policy. All images shrink; ordinary maze text is
35,868 to 35,460 bytes, another 1.14% against PO04. One Wi-Fi function's
frame grows 8 bytes; two frames shrink and summed reservations fall 8 bytes.
This tradeoff is explicitly captured, not a no-growth or promotion result.
Uncertainties/calls/outgoing areas and data/BSS remain unchanged.
PO09 retains promotion review; PO05 does not require that initial IR counts
translate into speed improvements.

The fresh frontend-only experiment measures 1,344 samples on both badges,
with two seeds, two repeats and role swaps. Against legacy lowering with the
same PO04 backend, generation improves 6.83%, RAM rendering 0.32% and
diagnostics/graphics 1.33% outside exact uncertainty envelopes for both seeds.
Physics improves only on the dense seed; full frames only on the second
seed; transfer remains within noise. No regressions are resolved. Both
complete original flash images are restored, independently hash-verified
and left in BOOTSEL. The candidate still trails ordinary LLVM `-Oz` by
3.141-8.687x on these measured batches.

### PO06 - Improve general IR optimization profitability

Use cleaner IR when available, but do not wait for a frontend rewrite to
investigate existing optimized IR.

ToDos:

- [x] Identify surviving redundant operations, CFG structure and memory traffic
  in measured application hot paths.
- [x] Select independently scoped improvements such as boolean/cast cleanup,
  safe expression reuse, dead-path cleanup or aggregate-memory lowering.
- [x] Measure optimization recovery using the same frontend input with owned
  and LLVM optimizers; include emitted code and hardware, not IR counts alone.
- [x] Include actual materialization, live-range, call and code-size costs in
  profitability decisions; preserve D07's quotient-sharing constraints.
- [x] Do not revive rejected loop motion, signed div/rem expansion or broad
  algebraic rewrites without their specific legality and hardware gates.
- [x] Verify IR after each pass; retain diagnostics, budget-exhaustion and
  output-preservation behavior.
- [x] Keep pass order explicit and each candidate independently selectable.

Acceptance: correctness on the full supported corpus and a resolved benefit
on reached workloads without unexplained regressions. The default pass list
changes only through PO09.

### PO06 implementation and qualification

[Bounded local load reuse, optimizer recovery and physical evidence](local-memory.md)
document opt-in `local-memory`, appended independently to the unchanged C06
defaults. It reuses exact-pointer/type ordinary scalar loads only within
two-instruction producer/consumer windows. Every store, call, volatile or
opaque access and block boundary invalidates the observation. No new SSA
definitions, stored-value captures, alias promises or division/algebraic
expansion are introduced. D07 quotient-sharing constraints remain unchanged.

The same 37 actual PO05 SSA inputs are optimized by owned control, candidate
and LLVM; 222 candidate pipeline-prefix outputs are independently verified,
and both owned/LLVM objects are emitted. The final candidate removes 10
optimized IR loads, 45 emitted instructions, 25 emitted loads and five
private transport stores. Source IR stores/calls/arithmetic are unchanged.
All 554 frame/call checks are unchanged, no module allocation count grows,
three of twelve images shrink and none grows. Ordinary maze text is
35,460 to 35,380 bytes, another 0.23% against PO05; RAM/flash budgets pass.

The complete final two-badge experiment captures 1,344 matched warm samples.
Maze generation improves 0.84% outside exact per-board/repeat envelopes for
both seeds; other workloads do not qualify as uniform wins. There are no
resolved regressions. LLVM's physical reference optimizes and emits the
byte-identical initial owned SSA IR, rather than ordinary Clang C or PO02's
optimized-IR/backend-only crossover. Initial modules and hashes are archived
and verified across every campaign. Both originals are restored,
independently full-flash hash-verified and left in BOOTSEL.

Two broader candidates were rejected after small second-seed frame
regressions despite larger static/generation wins. Their code, objects and
complete raw measurements are retained; uncertainty gates were not relaxed.
The accepted proximity-limited candidate is independently qualified, not a
claim of a proven cause for those regressions. Defaults remain unchanged;
PO09 retains promotion and physical/scratch-stack coverage review.

### PO07 - Improve ABI and cross-function economics

The large cross-file suite gap is a lead, not proof that calls alone are
responsible. Follow actual application profiles.

ToDos:

- [x] Separate call transport, save/restore, parameter/result handling and
  callee computation in measured cross-function paths.
- [x] Remove redundant argument/result copies and conservative saves only
  when the AAPCS and clobber model prove them unnecessary.
- [x] Audit small integer and aggregate arguments/returns in both directions
  between LLVM and owned code.
- [x] Investigate bounded inlining only for measured suitable callees; account
  for code growth, register pressure, stack demand and repeated call sites.
- [x] Preserve indirect calls, recursion, callbacks, volatile effects and
  timing-sensitive helper contracts; do not assume a whole-program call graph.

Acceptance: general call-path improvements with bidirectional ABI execution,
actual linked sizes and application timings. No whole-program stack or
callback safety claim follows from local-frame checks alone.

### PO07 implementation and qualification

[ABI-safe call transport and physical evidence](call-transport.md) document
opt-in `thumb-calls`, based on PO04 `thumb-homes`. Incoming non-byval register
words are captured directly from their ABI register instead of through r5.
Outgoing word stores use the SP-relative immediate form through byte offset
4092; 4096 and above retain address materialization. AAPCS alignment,
split arguments, narrow extension, hidden results and byval snapshots retain
their existing contracts. Saves, allocation, result transport, call targets,
intrinsics, inline assembly and noncall computation are unchanged.

The same 37 optimized modules produce verified trace/normal object equality.
All 554 function records separate entry, argument transport, result/stack
restoration and noncall lowering; no function instructions grow. The candidate
removes 989 entry copies and 1,246 outgoing address instructions, with unchanged
load/store counts, every frame/save reservation and all call uncertainties.
Eleven of twelve actual images shrink; none grows. Ordinary maze text is
35,460 to 34,884 bytes, another 1.62% against the independent PO05 control.
Instrumented text is 37,472 to 36,800 bytes; linked RAM/flash budgets pass.
PO06 is not enabled: this is not inferred combination qualification.

Both optimizer routes pass LLVM/owned mixed-ABI execution in both directions,
small/wide/composite/hidden-return boundaries, large frames and callback-live
values. Additional runtime-sampled tests must retain actual indirect and
recursive calls. Native sanitizer/mutation, resource/error/output-preservation
and driver/protocol tests pass. Oversized-byval source-load and arbitrary
LLVM integer-width limits found during probing remain explicitly documented,
not new supported interfaces. Bounded inlining and save/result elision were
investigated but not selected: repeated callees, hidden scratch use and
code-growth/live-range uncertainty do not justify speculative expansion.

Eight complete two-badge campaigns provide 1,344 matched raw samples. Outside
the unchanged exact per-board/repeat noise envelopes on both seeds, elapsed
time improves 1.61% for physics/input, 0.45% for RAM render, 2.78% for display
transfer, 2.14% for reached frames and 2.88% for diagnostics/graphics.
Generation remains within noise. No resolved regressions occur. LLVM is the
ordinary whole-source Clang C reference here, not an optimizer/backend
crossover. Both originals are restored, full-flash hash-verified and left
in BOOTSEL. PO07 is retained opt-in; defaults, physical/interrupt and
production-stack qualification remain PO09 responsibilities.

### PO08 - Preserve assembler/linker control boundaries

Maintain these as equivalence/regression checks; prioritize performance work
elsewhere unless fresh evidence identifies a reached problem.

ToDos:

- [x] Repeat identical-assembly crossovers on accepted actual corpus assembly,
  checking linked instructions, data and relocation results.
- [x] Repeat identical-object crossovers using the same script, object order,
  entry point and section-GC settings.
- [x] Distinguish object-file bookkeeping from loaded flash/RAM differences.
- [x] Verify mergeable strings/constants by values and resolved references,
  not only binary equality or total size.
- [x] Check branch reach, fixups, section alignment, GC, weak/common symbols
  and physical linker-region budgets on affected applications.
- [x] Consider pooling or linker relaxation only with measured relevance;
  keep D06's alignment/shared-pool deferrals explicit.

Acceptance: equality where required, explained placement differences where
allowed, and no unresolved relocation/layout defects. Merely matching LLD's
string ordering is not an optimization milestone.

#### PO08 implementation and qualification (complete)

The maintained [control-boundary report](control-boundaries.md) and
[`boundary-evidence.mjs`](../tools/boundary-evidence.mjs) qualify 39 exact
actual-source assembly inputs, twelve images and 72 fresh links under
accepted PO07 SSA/call-transport settings, without combining PO06 or changing
defaults. All original/reassembled owned loaded images are identical across
both assemblers and linkers. Allocated sections, relocation semantics,
nonrelocation bytes, function layouts, GC/NOLOAD and physical script budgets
are independently checked and recomputed by persistent verification.

The eleven ordinary LLVM-object byte differences are explained by mergeable
string/constant placement and resolved-reference values. LLD shortens only
diagnostics (20 bytes) and Wi-Fi (180 bytes); other load lengths and every
function layout match. There are 392 audited merged references per linker.
Owned objects request no mergeable allocated sections and receive no loaded
size benefit from LLD. Object-file metadata size differences are not firmware
improvements. D06 shared-pool/alignment/relaxation deferrals remain explicit.

Five focused tests plus nine reused pipeline-stage tests pass, along with
native sanitizer/mutation and LLVM assembler/object/linker oracles. Final
full-corpus capture and persistent verification pass after corrected auditor
probes; failed evidence is retained. Both badges are untouched, original
firmware remains restored/hash-verified in BOOTSEL, and no new timing win is
claimed. Earlier frontend/IR rejections and production/interrupt/stack gates
are not closed by this boundary audit.

### PO09 - Qualify and decide each promotion

Reuse the [D09 gate](datasheet/qualification.md), extended with PO01's
application coverage.

ToDos:

- [x] Archive a fresh baseline and an independent candidate with exact
  source/tool/firmware hashes; never overwrite failed evidence.
- [x] Run targeted correctness/negative tests, relevant sanitizers, LLVM IR
  verification, owned/LLVM execution, mixed ABI and raw-PHI checks.
- [x] Build the actual full corpus and ordinary/instrumented application
  variants; enumerate current inputs and validate linked budgets.
- [x] Compare code/data/BSS, frame/call metadata and conservative stack
  uncertainties; preserve unresolved production/interrupt limits.
- [x] Repeat reached application and unrelated workload measurements on both
  badges, with swaps, raw samples, control drift and verified restoration.
- [x] Use predeclared exact tick-based noise comparisons; strict equality
  remains unresolved. A favorable mean cannot hide a resolved bad role.
- [x] Reject speed-default promotion with unexplained resolved regressions;
  label acceptable size/runtime tradeoffs as separately selected policies.
- [x] Qualify combinations separately after their components; do not infer
  safety or benefit by adding independent results.
- [x] Persist the decision, remaining limits and directly related documentation.
  Wire any approved defaults consistently through API, CLI and build drivers.

Acceptance: a specific promote/retain/reject decision supported by verified
correctness, budgets, real hardware evidence and application coverage.
Physical panel timing, network behavior and whole-program stack safety remain
explicit gates where affected.

#### PO09 implementation and qualification (complete)

The maintained [promotion gate](pipeline-promotion.md) qualifies fresh current
default, PO03, PO04, PO05, PO06, PO07 and independently combined PO06+PO07
profiles. Seven full-corpus and instrumented-maze builds, 37 modules and
554 frame/call records per profile, both optimizer execution routes, mixed
ABI/raw-PHI/actual-maze checks and targeted sanitizer/protocol tests pass.
No candidate grows a frame, spill count or ordinary image against the fresh
default. Combined maze text is 34,804 versus 41,284 bytes (15.70% smaller);
fresh combined-policy PO08 controls also pass. The subsequent authorized
promotion selects this combination as the owned compiler default.

Full hardware qualification now passes **56 campaigns, 112 swapped rounds,
224 raw reports and 14,112 timing samples**. Every profile has repeatable wins
on all six application and twelve unrelated workloads on both seeds, with
no resolved role/repeat regressions. Exact decisions and full restorations
are independently recomputed by persistent current-source verification.
The archived gate initially retained all six profiles. Following explicit user
authorization, the independently qualified PO06+PO07 profile is now default:
`ssa`, the previous pass list plus `local-memory`, and
`direct/global-compact/thumb-calls/word`. A single 37-module replay proves
default frontend/optimized IR, assembly and objects identical to the qualified
explicit profile; no new hardware campaign was needed. Older alternatives
remain explicit; physical/live peripherals, cold-XIP, interrupt and whole-program
stack certification remain limitations. See the
[promotion decision](pipeline-promotion.md#authorized-default-promotion).

The combined profile qualifies 26.81% generation, 18.68% input/physics,
26.47% RAM-render, 7.76% transfer, 10.95% reached-frame and 33.17% diagnostics
elapsed-time reductions against the current default. These descriptive paired
medians do not establish candidate-versus-candidate superiority or LLVM parity.
Combination qualification is measured separately, not added from components.

Earlier USB read/flash failures and mock deadlines remain archived. Exact
archived-firmware isolation passed 26 runs on both boards; no deterministic
compiler bug was established. A fresh capture stopped before execution on
a flash timeout; five complete same-provenance campaigns are verified and
continued with 51 new campaigns, all without retries. Tested bounded transport
guards preserve failures and require recovered BOOTSEL plus complete reflash/
recollection; no deadline or raw-result gate is weakened. Final 24-case
promotion/protocol/runner tests pass. Both full originals are restored,
hash-verified and left BOOTSEL. The report documents historical failures
without claiming an unproven I/O root cause was repaired.

## General follow-up from the red-square experiment

### Evidence and what it does not establish

The [red-square experiment](../../docs/red-square.md) produced identical-looking,
owner-confirmed cold-bootable images with ordinary LLVM `-Oz` and the promoted
owned pipeline. The [byte ledger](../../build/red-square-results.json) separates
loaded bytes from ELF bookkeeping and UF2 packaging.

| Function/cost | LLVM bytes | Owned bytes | General opportunity |
| --- | ---: | ---: | --- |
| `delay_cycles` | 10 | 100 | Leaf frames, assembly effects, PHI transport, direct predicates and countdown loops |
| `write_byte` | 32 | 134 | Frameless leaf code, common address bases and direct volatile-store operands |
| `command` | 24 | 92 | Minimal call frames, scratch-register ownership and address reuse across calls |
| `commands` | 40 | 214 | Pointer predicates, byte-load normalization, call arguments, nested loops and spill pressure |
| `fill` | 36 | 104 | Pure invariant computation, counted loops and call-live allocation |
| `bare_main` | 184 | 554 | GPIO address induction, CFG cleanup and register/constant reuse |
| Shared startup, tables and alignment | 234 | 234 | Not the demonstrated compiler gap |
| **Loaded image** | **560** | **1,432** | **872 bytes of generated C/literal-pool difference** |

The C function totals are 326 versus 1,198 bytes. These are generated-code
sizes, **not measured execution times**, and their sum does not independently
attribute frontend, optimizer and backend contributions. PO10 must perform
the fixed-IR crossover before assigning all 872 bytes to the backend.
The experiment did not measure startup duration, bus waveform or per-loop cycles.

Direct inspection of the
[owned optimized IR](../../build/red-square-owned/universe26/firmware/red_square.o.owned/optimized.ll)
and [emitted assembly](../../build/red-square-owned/universe26/firmware/red_square.o.owned/assembly.s)
does establish these concrete mechanisms:

- A zero-local-frame helper still pushes six or eight registers, materializes
  zero in `r12`, adjusts SP by zero, establishes `r7` and reverses that work.
  [Frame emission](../src/codegen_frame.c) currently uses fixed save groups.
- The delay loop spills/reloads its live counter around `nop` assembly.
  [Assembly liveness](../src/codegen_global.c) assigns homes to allocated
  registers live across assembly without distinguishing a no-clobber `nop`.
- [Compare/branch selection](../src/codegen_select.c) rejects fusion when
  assembly occurs anywhere in the function and only handles `i32` comparisons.
  The outer pointer comparison in `commands` materializes a boolean and
  compares it again.
- [Branch layout](../src/codegen_frame.c) is disabled function-wide by any
  `volatile`, `asm`, `call` or `inttoptr` token. All six helpers report
  `branch-layout: active 0`, although many control edges themselves have no
  externally observable effect.
- MMIO stores repeatedly construct related addresses with `movw`/`movt`
  and copy addresses through multiple registers. Byte loads are followed by
  redundant-looking normalization/copies, and one command argument takes an
  adjacent private-stack store/reload path.
- `fill` computes invariant high/low pixel bytes inside the loop containing
  calls. GPIO setup recomputes affine addresses from the pin induction value.
  The profitability of hoisting them depends on their additional live ranges.

These are opportunities in drivers, parsers, protocol encoders, graphics,
memory operations and ordinary loops. No optimization may recognize a
demo name, a particular framebuffer size, an LCD command table or a known
MMIO address as permission to change C-visible effects.

### PO10 - Attribute the gap and establish reusable gates

- [x] Reuse the archived experiment as starting evidence; freeze referenced
  inputs/tools before rebuilding paths that older reports hash.
- [x] Extend PO00/PO02 attribution rather than create a parallel pipeline:
  compile identical owned initial/optimized IR through owned and LLVM
  backends, distinguishing optimizer recovery from backend expansion.
- [x] Enumerate actual production examples of leaf functions, assembly
  barriers, volatile stores, pointer/counted loops and call-live values.
  Keep red-square helpers as reproductions, not the acceptance corpus.
- [x] Classify loaded instructions, literals, padding, saves, frame bytes,
  per-iteration stack traffic, boolean materialization and branch chains.
  Every gap should have a demonstrated cause or an explicit unknown label.
- [x] Record baseline compiler/source/policy identities and define the small
  targeted tests, full-corpus gate and affected hardware checks in advance.

Acceptance: reproducible attribution of all six helper gaps and a production
inventory for each proposed mechanism. Counts and attribution must reconcile
with ELF bytes. No speed claim from instruction counts alone.

#### PO10 implementation and qualification

The maintained [small-function attribution tool](../tools/small-function-attribution.mjs)
reuses PO00/PO02/C06 helpers and archives capture-local tools/inputs.
[The qualified result](small-function-attribution.md) reproduces the entire
872-byte gap on identical optimized IR: 1,432 owned versus 560 LLVM loaded bytes.
All six ELF-symbol ledgers reconcile, with no undecoded function bytes.
The 37-module/554-function inventory includes 294 zero-local-frame functions
with saves, 222 leaf candidates, 89 volatile-bearing functions, 44 adjacent
pointer-predicate functions and three NOP-text candidates in ordinary display/USB.
These are candidates, not legality proofs.

LLVM optimization of owned initial IR yields a larger 612-byte image with
`write_byte` eliminated; the frontend/attribute route yields 560. This
reinforces costed sharing research, not mandatory LLVM imitation.
Seven PO10 tests, five focused PO02 helper tests and persistent
`--verify`/`--verify-current` pass. No badges are touched and no full corpus
is rebuilt. PO18's hardware/cache contract is recorded, not implemented here.

### PO11 - Replace function-wide caution with instruction-scoped effects

- [x] Represent register reads/writes, flags, memory effects and control
  effects for supported inline assembly at its actual instruction location.
  Reuse shared analysis/effect helpers where their contracts apply.
- [x] Initially classify only provable instruction forms, such as operand-free
  `nop` sequences; do not trust an empty constraint string to prove arbitrary
  assembly preserves registers. Unknown assembly keeps conservative handling.
- [x] Separate register preservation from a `"memory"` clobber. Preserve
  memory barriers even when no register spill is needed; preserve `wfi`,
  architectural barriers and unknown effects rather than treating them as nops.
- [x] Remove live-value spills only for registers proved preserved. Allow
  unrelated compare/branch selection where no flag hazard intervenes.
- [x] Test known and unknown assembly, multiline sequences, explicit memory
  clobbers, flags, loop-carried values and combinations with calls/volatile I/O.

Acceptance: a proved no-clobber `nop` needs no counter spill/reload, while
unknown assembly and memory barriers retain their required protection.
There is no global weakening of the inline-assembly contract.

#### PO11 implementation and qualification

[Instruction-scoped assembly effects](assembly-effects.md) are implemented
in the explicit `thumb-effects` selector, inheriting `thumb-calls`.
Operand-free NOP sequences preserve registers; memory/flag clobbers remain
independent, and unknown assembly stays conservative. Adjacent compare/branch
selection no longer excludes an entire assembly-bearing function; intervening
assembly still prevents fusion. Older selectors and the default are unchanged.

The fixed-IR 37-module/554-function production pass removes three static
assembly spill/reload pairs and adds 41 fused comparisons. Eleven of twelve
ordinary images shrink by 24-280 bytes, with no frame/save/data/BSS growth.
Control objects and loaded images exactly replay their archived identities.
Sanitizer mechanism tests and Cortex-M33 execution cover all residency modes,
calls, volatile I/O, actual unknown register clobbers and flag hazards.
Memory-barrier and persisted-evidence tamper tests pass.

The affected display busy waits and USB ownership-settling wait are explicitly
timing-audited, but their changed physical timing is not qualified. The candidate
gate mitigates that risk: no default promotion or badge campaign is claimed.
PO12 is next; a timing-safe default promotion requires the separate affected-path
hardware gate, not another seven-profile campaign.

### PO12 - Lightweight frames and ABI-aware register allocation

- [x] Derive actual callee-save needs from finalized emitted register use,
  including helper scratch registers, PHI cycles, byval copies and assembly.
  An allocated-register mask alone is insufficient.
- [x] Remove zero-byte SP adjustments; omit `r7` and a frame pointer where
  every stack access has a correct SP-relative form. Recompute incoming stack
  argument offsets from actual saves and alignment.
- [x] Use caller-saved registers for eligible leaf values and intervals that
  do not cross calls. Split or rematerialize short lifetimes rather than
  forcing all values into persistent callee-saved homes.
- [x] Save LR only where needed, and save the smallest legal register set.
  Respect AAPCS call-site alignment, recursive/indirect calls, hidden results,
  outgoing stack arguments and existing public ABI behavior.
- [x] Validate mixed LLVM/owned callers/callees, high-register scratch use,
  PHI cycles, narrow/wide/composite returns and negative resource limits.

Acceptance: eligible stack-free leaf helpers have no push/pop or frame setup;
zero-sized local frames never cause zero SP arithmetic. Actual frame/call
metadata matches emission and no frame/spill growth is hidden. Do not limit
this to the delay function.

#### PO12 implementation and qualification

[Lightweight frames](lightweight-frames.md) are implemented in `thumb-frames`,
inheriting PO11 without changing the default. Finalized emitted register
operands determine saves, incoming offsets and LR need; stable-SP leaves omit
`r7`, and zero local bytes never generate SP arithmetic. A bounded `r3`
extension handles eligible short scalar intervals; call-crossing values remain
protected. General interval splitting/rematerialization is deferred, not claimed.

The 37-module/554-function same-IR comparison against PO11 has nine stack-free
functions, 32 smaller local frames, 244 fewer unallocated values and no frame/save/
per-module spill growth. Twelve images shrink by 32-3,148 bytes. Mixed-ABI
Cortex-M33 execution covers existing large-offset/high-scratch/composite/recursive/
indirect fixtures in both directions, plus cyclic PHIs and opaque register writes.
Static evidence and negative resource gates pass; physical timing remains gated.

### PO13 - Direct predicates, local CFG cleanup and counted loops

- [x] Permit branch-to-next removal, branch threading and condition inversion
  around calls/volatile operations when execution paths and effect order are
  unchanged. Do not move an effect merely to make a nicer layout.
- [x] Extend single-use compare/branch fusion to supported pointer and narrow
  predicates with correct normalization and signed/unsigned interpretation.
  Track flag definitions/uses instead of excluding whole functions.
- [x] Select a fused decrement/flag-tested backedge for eligible counted loops,
  using the original zero-trip condition and proved trip-count semantics.
  Handle post-decrement's observable exit value; test 0, 1, boundary and
  wrapping counts, early exits, nesting and counters used after the loop.
- [x] Coalesce induction/PHI transport and remove empty block chains without
  introducing extra iteration, bypassing calls or losing side effects.

Acceptance: eligible loop/control paths need no materialized boolean,
redundant second compare, unnecessary PHI copy or branch-only trampoline.
Compare final encodings and reached runtime, not just prettier IR.
Countdown fusion is a general selection rule, not a delay recognizer.

#### PO13 implementation and qualification

[Direct control selection](control-selection.md) is implemented in explicit
`thumb-control`, inheriting PO12. Adjacent narrow/pointer predicates are selected
with correct normalization and signedness; plain decrement/zero predicates use
the decrement flags while preserving original zero-trip guards and wrapping
exit values. Effectful functions permit safe edge-only layout changes.
Bounded threading bypasses branch-only chains, stopping before PHI targets;
unreachable intermediate labels may remain, not a general dead-block pass.

Same-IR production qualification adds 152 fused comparisons and one decrement
predicate across 37 modules/554 functions. All twelve images shrink by
48-10,576 bytes versus PO12, with no frame/save/spill growth. Cortex-M33 boundary,
postdecrement/nested/early-exit/volatile and mixed-ABI tests pass. Final ELF
encoding reduction is tested, not inferred from prettier assembly.
The default and physical-timing gate remain unchanged; PO14 is next.

### PO14 - Address reuse, constants and direct operand transport

- [x] Reuse related constant-address bases within proved availability/live
  ranges; choose base-plus-offset versus literal versus `movw`/`movt` using
  final bytes, pressure and target cost. Do not invent loads from MMIO.
- [x] Feed stores, byte loads, arithmetic and call arguments from their
  allocated operands where legal; eliminate redundant copies and private
  spill/reload round trips. Avoid re-zero-extending an already normalized byte.
- [x] Reuse constants across safe regions, preserving or rematerializing them
  at calls according to cost. Track flags and high-register encoding penalties.
- [x] Consider address induction for regular arrays/register banks, preserving
  skipped elements, access width, alignment, pointer rules and exact order.
- [x] Validate arbitrary constant-address regions, RAM/flash arrays, signed
  indices, crossing offset limits, calls, barriers and narrow values.

Acceptance: related address formation and operand transport shrink on
production drivers/parsers as well as the reproductions. Every original
volatile access remains present, with identical address, width, value and
order. A compiler may reuse the address calculation; it may not combine
or replace volatile GPIO writes with a different transaction sequence.

#### PO14 implementation and qualification

[Bounded operand/address selection](operand-selection.md) is implemented as
`thumb-operands`, inheriting PO13. Aligned scalar accesses and eligible outgoing
arguments use allocated operands directly. Canonical byte/halfword loads and
extensions avoid duplicate masks; selected narrow arithmetic retains modular
normalization. Existing scratch exclusions and live ranges are unchanged.

Related numeric/symbol constants reuse proved physical-register availability
with strictly smaller flag-neutral copies/offset formation. Calls, CFG joins,
IT and unknown instructions end availability; opaque-assembly functions skip
constant reuse. No memory values, MMIO transactions or effectful instructions
are combined or moved. Address induction was considered and deferred to PO15's
separate pressure/zero-trip qualification.

Same-IR production evidence records 3,942 direct memory selections and 483
reused materializations (2,118 bytes saved in those sequences). All 37 modules
and twelve images shrink; image savings are 52-11,144 bytes versus PO13, without
frame/save/spill growth. Ten targeted PO14 tests, retained ABI/control/driver
gates and historical frame-inspector verification pass.

The final capture reuses verified PO13 controls and compiles only the changed
candidate backend, with no frontend or hardware campaign. Defaults and physical
timing remain gated.

### PO15 - Pressure-aware pure loop motion and induction lowering

- [x] Reuse [existing loop analysis](../src/ir_loop_invariant.c), but isolate
  a bounded pure-scalar candidate rather than globally enabling the old
  mixed-result `loop-invariant` or `loop-cost` profiles.
- [x] Hoist invariant shifts/masks, safe affine address computations and
  other nontrapping scalar work when dominance and zero-trip semantics allow.
  Do not speculate loads, division, flagged/poison-producing operations,
  volatile accesses or unknown effects.
- [x] Compare hoisting with cheap rematerialization at use sites. Account for
  live ranges across calls, extra callee saves, spills and small trip counts.
- [x] Strength-reduce proved induction expressions while preserving C/IR
  integer overflow, pointer bounds, exit values and skipped iterations.
- [x] Test general encoders, constant fills, graphics and strided data loops;
  require an actual-production win rather than a contrived invariant fixture.

Acceptance: invariant pixel-byte calculations can leave eligible loops
without introducing a larger frame or a resolved runtime regression.
Record rejected hoists; a size gain cannot conceal spill-driven slowdowns.

Implemented as explicit [loop-scalar](loop-scalar.md), with conservative
pressure gates and per-function admission against actual PO14 assembled bytes
and finalized frame/save/spill facts. The initial pressure-only regressions
are retained as failed evidence; the final 37-module/554-function/twelve-image
capture admits seven changed modules, saving eight executable bytes without
allocation growth. Three images are relinked; alignment absorbs two image
changes, while `usb_wifi_udp` saves eight loaded bytes.

The real two-color, full-pixel graphics verification batch reaches 5,376,064
versus 5,068,868 QEMU instructions. Eight focused tests, 141 retained legacy
loop fixtures and static report-tamper checks pass. This is not badge-cycle
qualification; zero/small trips and other backend policies have no general
speed guarantee. Defaults stay unchanged and no badges are touched.

### PO16 - Costed sharing, specialization and cross-function economics

- [x] Compare keeping a helper, inlining it, specializing constant arguments,
  tail calls and sharing repeated instruction sequences using total linked
  cost, stack pressure, effect order and measured hot-path behavior.
- [x] Start with bounded module-local, non-address-taken functions and known
  call edges. Apply strict clone/code-growth/work budgets; require an explicit
  closed-world contract before extending across independently compiled files.
- [x] Model duplicated MMIO/constant-setup sequences. The experiment's LLVM
  image shrank 612 to 560 bytes when repeated byte-write code was kept shared:
  blind inlining is not the goal. Outlining can beat LLVM's chosen policy.
- [x] Permit constant specialization only where the full-program cost is
  beneficial, including any retained generic helper and all cloned bodies.
  Preserve recursion, indirect/public calls, volatile effects and barriers.
- [x] Honor supported explicit inline/noinline contracts, diagnosing unsupported
  requests rather than accepting and ignoring them.

Acceptance: improve total ordinary image size and/or reached runtime under
a declared policy on multiple production call graphs. Shared helpers must
not be penalized merely because LLVM prefers inlining, and a specialized
constant-fill loop must not become demo-specific answer substitution.

Implemented as explicit [cross-function](cross-function.md). Its first bounded
policy compares constant specialization (including reduced caller/helper
argument lists), straight-line inlining, tail-forwarder elimination,
identical whole-helper sharing and retention against complete-module owned
encodings and finalized allocation facts. Native tail jumps and arbitrary
partial-sequence outlining are not assumed. Public/address-taken/recursive
functions remain unchanged; noinline is honored and forced unsupported
contracts diagnose.

The qualified PO15-input corpus saves 406 executable bytes in sixteen changed
modules, and eleven of twelve ordinary images save 4-200 loaded bytes without
frame/save/spill growth. Actual USB/CRC and Wi-Fi helper batches reach fewer
verified QEMU instructions. Fourteen targeted tests and persistent tamper
verification pass. Hash-checked final reuse avoids recompiling/relinking
identical production outputs. Red-square is unchanged; defaults and affected
hardware timing remain gated, and no badges were programmed.

### PO17 - Final-width selection after the structural waste is removed

- [x] Reuse D04/D06 selection and final-layout groundwork, but do not enable
  their previously regressing profiles wholesale.
- [x] Choose low-register placement, Thumb-16 operations, branch widths,
  literals, tail sharing and local layout using actual final distances and
  flag liveness. Charge any added moves, spills or padding.
- [x] Evaluate size and speed independently, including XIP/flash behavior
  where affected; a shorter encoding need not execute faster.
- [x] Keep assembly, barriers and volatile ordering intact. Maintain owned
  assembler range checks and loaded-image equality at changed boundaries.

Acceptance: final loaded-byte savings with no hidden ABI/resource changes;
no guessed short-branch ranges or unqualified scheduling changes.
Smaller-than-LLVM code and faster-than-LLVM code are valid goals separately.

Implemented as explicit [thumb-widths](final-widths.md), inheriting PO14
operands and using verified PO16 IR. The first selective policy keeps the
existing register placement, local layout and shared epilogues rather than
paying for new copies/outlining; low-register memory/normalization and
destructive ADD substitutions preserve flags. Owned assembly resolves
structured branches and guarded literal-load/branch widths from final
distances and alignment. No historical compact arithmetic or scheduling
profile is enabled wholesale.

The 37-module/560-final-function corpus saves 35,070 executable bytes; all
twelve images save 48-16,676 loaded bytes with identical allocation,
frame/save/outgoing/call facts and unchanged data/BSS. Red-square reaches
984 bytes versus 1,064 for PO14-PO16 and 560 for LLVM. Actual USB and Wi-Fi
batches shrink with identical reached instruction counts. Exact final-width
boundaries, mixed ABI, effects and persistent tamper checks pass. Final
hash-checked reuse avoids repeated production assembly/relinks.

This completes the host size-selection implementation, **not** physical
flash/XIP-speed qualification. Changed fetch placement can still affect
busy-wait/strobe margins; PO18 physical timing/default promotion remains
required. No badges were programmed.

### PO18 - Qualify and promote each general candidate efficiently

Use PO09's exact correctness/noise principles, **not its historical seven-profile
control inventory or automatic retain decision**. The new baseline is the
promoted SSA/local-memory/thumb-calls profile.

- [x] Declare each baseline/candidate policy, scope, measurements and promotion
  decision before capture. Qualify interactions separately when their combined
  emission changes; do not add independent percentages.
- [x] Run targeted tests first, then one necessary actual-corpus/ABI/budget
  gate for a compiler semantic/emission change. Keep red-square as an extra
  oracle; ordinary driver, parser, graphics, maze and unrelated workloads
  establish generality.
- [x] Reuse immutable source/tool/header/flag/default-policy/link-script keyed
  artifacts. Build once per real image identity, not once per seed, repeat
  or board role. A seed is reusable only if it is runtime input rather than
  a compile-time definition.
- [x] Hash-verify reused objects/images and their provenance. A cache mismatch
  requires an explicit rebuild or error, not a silent stale-image fallback.
  Deduplicate overlapping host selectors in a single runner.
- [x] Measure only baseline and the qualified candidate/required combination,
  retaining the declared repeats, seeds, role swaps, exact noise comparisons
  and current control-drift observations. Do not repeat all historical profiles
  or an already passing offline matrix after unrelated host/documentation edits.
- [x] If required images are byte-identical to already qualified outputs,
  record equivalence and reuse that coverage instead of reflashing solely
  to reconfirm the same image. Newly affected timing paths still need evidence.
- [x] Preserve both badges' full original flash and verify restoration for
  each hardware session. Keep failures; do not weaken timeouts/raw-result gates.
- [x] Publish size, measured time, frame/call/stack, compile cost and tradeoffs.
  Promote consistently through API, CLI and build drivers when general
  correctness and no major downside are demonstrated. Do not leave a qualified
  candidate permanently opt-in merely because its assembly differs from LLVM.

Important hardware mitigation: faster code can shorten source busy waits and
GPIO setup/pulse intervals even when C-visible effects are unchanged.
Before promoting an affected candidate, audit reached production delay and
strobe contracts. Use a validated hardware timer or a documented minimum-cycle
delay primitive where needed, keeping such firmware changes separate from the
compiler speed comparison. Preserve explicit nops/barriers, but do not assume
their unchanged presence proves all timing margins unchanged. Qualify cold
boot and affected live I/O timing without using deliberate slowdowns to hide
a compiler regression. This is particularly relevant to PO11-PO15.

### PO18 implementation and qualification (complete)

[The promotion report](current-promotion.md) records six immutable images,
26 cache reuses, sixteen baseline/candidate campaigns with both seeds,
repetitions and physical roles, and no resolved regressions. All six reached
application workloads and twelve general timing workloads improve.
Both serviced and unserviced LCD initialization now use the inherited timer;
the owner confirms optical correctness after complete power removal with
both profiles on both badges. All originals are restored and full-flash
hash-verified after both sessions.

The combined PO11-PO17 chain is now the owned default, including `loop-scalar`
and `cross-function`, without changing root LLVM selection. Default backend
emission matches all 37 qualified module assemblies; only the changed display
object is substituted, and eleven affected production images are relinked.
No production object is reassembled during finalization. Explicit prior
policies and unsupported-IR diagnostics remain available; scheduling and
rejected flag-setting compact profiles are not promoted.

### Success targets and exploration beyond LLVM

The first structural target is **zero unnecessary frame/SP work in eligible
leaf functions**, **zero no-clobber-nop spills**, and **zero redundant boolean
round trips for supported branch-only predicates**. These are testable code
properties, not guessed percentage promises. Each milestone must also earn
ordinary-production linked-size or measured-runtime benefit.

The red-square LLVM sizes (10/32/24/40/36/184 bytes) are useful comparators,
not mandatory per-function encodings or a required IR architecture. A stronger
owned solution may allocate different registers, use different CFG/loop forms,
keep a shared helper LLVM duplicated, or specialize a hot call LLVM kept generic.
Report an actual win over LLVM on each qualifying workload; do not stop at parity
or claim universal superiority from this one image.

For GPIO/protocol optimizations beyond ordinary C semantics, an explicitly
defined platform operation/intrinsic could expose a mask transaction or bulk
write contract with proved RP2350 effects and timing. That is a separate
opt-in language/platform contract, not permission to collapse arbitrary
volatile stores or silently rewrite the application into a different algorithm.

## Recommended next execution slice

1. **PO10 is complete:** reuse its fixed-IR attribution, frozen inputs and
   production inventory rather than repeating captures solely for a baseline table.
2. **PO11 is host-qualified:** reuse its explicit candidate and production
   deltas; keep unknown forms conservative and hardware-timing promotion gated.
3. **PO12 is host-qualified:** reuse finalized-frame and bounded caller-saved
   evidence rather than repeating the unchanged baseline.
4. **PO13 is host-qualified:** reuse direct-predicate/countdown and effect-safe
   CFG evidence with the same frozen production inputs.
5. **PO14 is host-qualified:** reuse bounded address/constant and direct-operand
   evidence; proceed to pressure-aware PO15 rather than rebuilding controls.
6. **PO18 is complete:** reuse the cached combined-chain timing, cold-boot
   observations and default-equivalence evidence; do not repeat the historical
   profile inventory or reflash byte-identical images.
7. **PO15-PO17 are promoted:** retain their persistent loop/helper/width
   contracts and explicit controls. New emission or timing-sensitive firmware
   changes need their own bounded qualification, not a legacy full matrix.

Keep startup-size tricks and compiler-independent demo rewrites out of compiler
acceptance. The objective is a generally better C compiler for this badge,
demonstrated on real applications, not a compiler that wins its own fixtures.
