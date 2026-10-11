# PO04: scalar homes and PHI transport

PO04 follows [PO02's attribution](backend-attribution.md) and
[PO03's compare/branch candidate](compare-branch.md). It removes unnecessary
private scalar reservations and PHI-buffer memory traffic without changing
register allocation or recognizing particular applications.

Select the candidate explicitly:

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-homes \
  bash badge-compiler/scripts/build-owned.sh
```

`thumb-homes` inherits PO03's `thumb-branch` lowering. Home/PHI changes apply
only to `global-compact` residency. Other residency policies retain their
ordinary lowering. The compiler default remains `thumb-layout`; neither PO03
nor PO04 is promoted by this milestone.

## Investigation and implementation

The existing allocator already builds per-block liveness, edge PHI uses,
interference and scratch-register constraints. It allocates values to the
same callee-saved r4/r5/r8/r10/r11 set, with existing affinity/coalescing.
However, compact frame coloring still assigned a scalar home even when all
reads/writes used an allocated register, or a value had no uses. PHI constants
also went through a store/load buffer despite having no dependency to capture.
These are separate from real objects and register-pressure evictions.

[Compact frame assignment](../src/codegen_global.c) now:

- Omits non-addressable scalar homes when an allocated register owns the
  value for its lifetime, or the existing analysis finds no uses.
- Retains homes for values live across inline assembly, using the same
  existing spill/restore liveness information. Arbitrary assembly remains
  conservatively opaque.
- Retains and interference-colors unallocated scalar homes.
- Reserves actual `alloca` objects, aggregates, wide values and by-value
  argument objects exactly as before, with their original sizes/alignment.

An omitted home's slot is explicitly marked, not silently aliased to a real
slot. Unexpected reads/writes/callee loads that require it diagnose an error.
No C address can point to a backend SSA home; an escaping object's storage is
distinct from the scalar pointer value used to address it.

[Parallel PHI transport](../src/codegen.c) now defers only simple one-token
integer/boolean/null constants for allocated scalar destinations. It first
captures every source that needs buffering, then uses the existing dependency-
ordered copy solver. A constant writes its destination only after any pending
copy has consumed that destination's old register contents. Register cycles
still use reserved r6. Direct constants have no source dependencies and do not
clobber r6. Narrow values retain normalization.

PHI buffer reservation is omitted only when **every** incoming edge of that
allocated scalar PHI uses an allocated register or a supported simple
constant. Otherwise the original buffer reservation and captured-source
path remain. Unallocated/wide/aggregate PHIs, stack sources and complex
expressions remain buffered. A constant edge can still avoid its buffer
accesses when another edge requires that PHI's reservation.

Complex pointer/address expressions are not deferred: evaluating one after
parallel copies could read overwritten operands or clobber cycle scratch.
General constant/address rematerialization versus a longer live range remains
deferred. Pure PHI literals introduce **no new live range or allocation**;
their already-required immediate materialization replaces two memory
operations. The shared target cost model distinguishes immediate encoding
costs from unknown memory waits; no constant load or symbol reference is
assumed free, and no frequency/spill heuristic is added without measured
evidence. Changing coalescing priorities or register counts would confound
this experiment and is not included.

Call-crossing values remain in the existing callee-saved registers or homes.
Saved-register sets, outgoing argument areas, callback calling convention
and AAPCS eight-byte stack alignment do not change. Incoming argument offsets
are recomputed from the reduced frame and unchanged save area. Prologue/
epilogue frame adjustments use the new size; zero-size frames still use the
existing conservative prologue, rather than silently adding a leaf-function
optimization.

Work is bounded by the existing charged selection/register budget. Home
coloring does not gain another fixed-point pass; it skips unnecessary homes
in the existing traversal. PHI source scans/hash probes are charged, and the
copy solver remains bounded by the five-register set. Low-budget sanitizer
tests verify explicit failure and preservation of pre-existing output.

## Semantic and static qualification

- [Final corpus, linked budgets and frame/call metadata](../build/po04-release-corpus/offline.json).
- [Exact historical-IR replay and scalar/PHI/frame metrics](../build/po04-traffic/results.json).
- [Sanitizer, all-predicate, volatile, cyclic-PHI and work-budget tests](../build/po04-tests.log).
- [Callable API checks](../build/po04-api-tests.log).
- [Driver, protocol, restoration and exact-noise tests](../build/po04-protocol-tests.log).
- [Actual production-source application semantic execution](../build/po04-application-oracle.log).
- [Full native codegen sanitizer regression suite](../build/po04-codegen-regressions.log):
  33 positive, 27 negative and 113 mutation fixtures, API/CLI equivalence,
  repeated calls, input bounds and output preservation/alias checks.
- [Narrow/null and conservative PHI negative cases](../build/po04-negative-tests.log).

The replay feeds the same historical owned optimized IR to PO03 and PO04.
All **11 PO03 baseline objects remain byte-identical**. Traced and normal
objects are identical for both profiles. Across **140 functions**:

- **3,798** scalar home reservations are omitted.
- **324** emitted loads and **324** stores disappear.
- Summed frame reservations fall **2,384 bytes**.
- Register assignments, callee-save bytes, calls and real object sizes remain
  unchanged. No function's load/store count, frame or PHI reservation grows.

These are static emitted counts and reservations, not retired instructions,
dynamic spill events or measured stack high-water marks.

For example, production clear-loop entry changes from

```asm
movw r0, #0
str.w r0, [r7, #16]
ldr.w r5, [r7, #16]
```

to `movw r5, #0`. The destination write is still correctly ordered against
other parallel copies; both memory operations disappear. Clear's sole pixel
store and all loop iterations remain unchanged. This entry PHI executes once
per call, not once per pixel: a static saving is not automatically a large
runtime saving.

| Actual function | Frame before/after | PHI-buffer loads + stores before/after |
| --- | ---: | ---: |
| `generate_maze` | 1,088 / 1,072 bytes | 45 + 45 / 1 + 1 |
| `collide_wall` | 48 / 16 bytes | 11 + 11 / 0 + 0 |
| `physics_step` | 32 / 16 bytes | 2 + 2 / 0 + 0 |
| `screen_pixel` | 32 / 8 bytes | 3 + 3 / 0 + 0 |
| `badger_gfx_clear` | 24 / 0 bytes | 1 + 1 / 0 + 0 |
| `badger_gfx_text` | 56 / 48 bytes | 8 + 8 / 4 + 4 |
| `lcd_write_byte` | 8 / 0 bytes | 0 + 0 / 0 + 0 |

Generation's real DFS/private object storage remains; its frame does not
magically become a tiny leaf frame. Clear's old PHI-buffer traffic was not
spilling. LCD byte writes retain their actual volatile stores even though a
private scalar reservation disappears.

Fresh corpus qualification covers **37 C modules**, **12 images** and
**554 frame/call checks per profile**. **542** function frames shrink; none
grows. Their summed local/save frame reduction is **9,640 bytes**, not a
single execution chain's stack saving. Calls, outgoing areas and conservative
uncertainties remain unchanged. Main-RAM headroom is not scratch-stack capacity.

Both LLVM-optimized and owned-optimized semantic oracle routes pass,
including bidirectional LLVM/owned AAPCS calls, large incoming offsets,
callbacks, register rotations, narrow cycles and wide PHIs. The actual maze
and production-source application oracles pass under LLVM and owned execution.
Focused tests preserve inline-assembly live-value homes and real private
objects, reject unexpected home accesses, and retain baseline behavior for
other residency policies.

No linked image grows; `hello_idle` remains unchanged. Data/BSS and flash/RAM
budgets are preserved.

| Ordinary linked image | PO03 text bytes | PO04 text bytes |
| --- | ---: | ---: |
| `tilt_maze` | 37,068 | 35,868 |
| `diagnostics` | 60,920 | 59,152 |
| `usb_wifi_udp` | 378,092 | 372,292 |

Ordinary maze text falls another **3.24%** against PO03, or **13.12%**
against the 41,284-byte `thumb-layout` baseline. The separately instrumented
maze falls from 39,136 to 37,880 bytes, with unchanged 16-byte data and
160,448-byte BSS. These are not the application harness image sizes.

The earlier `po04-corpus` capture is superseded by the final direct-destination
constant materialization above. Its evidence is retained, not overwritten.

## Real-badge experiment

The maintained preservation wrapper compares `thumb-branch` against
`thumb-homes`, with ordinary whole-source Clang 22.1.8 `-Oz` as an unchanged
reference. It does not attribute PO03's gains to PO04 or use the fixed-IR
LLVM backend denominator.

Two seeds, two repeats per seed and two profiles each have two swapped
physical rounds, seven warm samples per workload/badge/round. Raw records bind
source identity, fingerprints, guards, encounter counts, clocks, fresh
nonces and actually executed images. The existing exact integer-rational
per-board/per-repeat envelope includes sample spreads, repeated same-board
control drift and paired LLVM drift. Means cannot hide regressions.

The [initial incomplete transport campaign](../../build/application-benchmark-runs/run.p1MO0t/results.json)
is retained as rejected evidence. A USB summary-read error stopped the second
role of the fourth campaign; it is not a valid timing qualification. Both
full originals were independently restored/hash-verified on that failure.
No collector timeout was relaxed, error swallowed or partial campaign
substituted for the required fresh repeated experiment.

### Completed fresh qualification

[Final hardware capture and full-flash restoration](../../build/application-benchmark-runs/run.iVRAbJ/results.json)
contains **eight campaigns, 16 rounds, 32 raw reports and 1,344 workload
timing samples**. All raw outputs, guards, counts, encounter results, clocks,
nonces and executed-image identities match. Capture-time source verification
passes. No observation is classified as a regression.

Incremental reductions below are against a freshly measured PO03
`thumb-branch` control, not the original `thumb-layout` baseline. Percentages
summarize eight paired medians; qualification still uses each exact per-board/
per-repeat gate, not the geometric mean.

| Workload | Elapsed reduction vs PO03 | Exact repeatability classification | PO04 / ordinary LLVM |
| --- | ---: | --- | ---: |
| Maze generation | **2.00%** | Improvement on both seeds, both badges/repeats | 7.650x |
| Physics/input | **1.67%** | Improvement on both seeds, both badges/repeats | 8.707x |
| RAM rendering | 0.047% | All observations within noise | 3.151x |
| Panel transfer | 0.010% | All observations within noise | 5.434x |
| Reached frames | 0.108% | Dense seed improves; intermediate seed within noise | 5.284x |
| Diagnostics/graphics | **0.59%** | Improvement on both seeds, both badges/repeats | 3.229x |

Only the bold reductions qualify across both seeded workloads. The frame
result is deliberately not claimed as a universal gain. Large clear-loop
iteration counts do not magnify its entry-only PHI saving; panel transfer is
dominated by unchanged GPIO writes. Summed frame reductions are a static
storage benefit, not proof of an equivalent dynamic speedup.

Both fresh full 16 MiB originals were restored and independently reread:

| Badge | Original/restored SHA-256 |
| --- | --- |
| `9BBF88DD17D66E7D` | `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff` |
| `08DA26F86E57F5EA` | `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d` |

Both remain in BOOTSEL. PO04's semantic/ABI, frame/traffic and reached-benefit
gates are complete. `thumb-homes` remains opt-in pending independent PO09
promotion and broader interaction review. LLVM still leads by 3.151-8.707x
on these reached workloads; later milestones remain necessary.

## Reproduction

Run from the repository root with the existing LLVM/LLD/QEMU tools available:

```sh
make -C badge-compiler test-scalar-homes
node --test badge-compiler/tests/application-protocol.test.mjs \
  badge-compiler/tests/datasheet_qualification_test.mjs \
  badge-compiler/tests/build_owned_test.mjs
node badge-compiler/tools/qualify-datasheet.mjs --offline-homes NEW_CORPUS_DIR

node badge-compiler/tools/scalar-home-evidence.mjs --capture NEW_TRACE_DIR \
  badge-compiler/build/po03-release-traffic/results.json
node badge-compiler/tools/scalar-home-evidence.mjs --verify NEW_TRACE_DIR/results.json

BADGE_CODEGEN_SCALAR_SELECTION=thumb-homes \
  bash badge-compiler/tests/application-oracle.sh
node badge-compiler/tools/application-benchmark.mjs --measure-homes SERIAL SERIAL
node badge-compiler/tools/application-benchmark.mjs --verify REPORT.json
```

Use fresh output directories. Hardware measurement takes fresh full 16 MiB
snapshots of both badges before programming, restores originals in `finally`
even on failure, then rereads and hashes every byte. Each profile is selected
explicitly, ignoring accidental policy environment overrides. Historical
PO01, fixed-IR and PO03 report verification remains supported.
`--verify-current` additionally checks capture-time source hashes; later
source changes must remain visible as drift.

Physical motion/IMU acquisition, electrical/visual panel certification,
controlled cold-XIP/startup, arbitrary-program performance and interrupt-
inclusive production stack safety remain unqualified. The application's
64 KiB stack linker is isolated benchmark support, not proof of a safe
production scratch stack. LLVM remains a performance reference, not a
specification or stopping point.
