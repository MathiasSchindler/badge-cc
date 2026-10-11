# D05 - Bounded dependency-aware local scheduling

`thumb-schedule` is an opt-in C07 policy. It inherits the qualified
`thumb-layout` selection and branch layout, then reorders selected allocated
machine instructions within small basic-block windows. It changes neither
allocation nor the default profile. It does **not** compose D04's compact
encodings, recognize workloads, insert NOPs or introduce moves/spills.

D09's [fresh independent qualification](qualification.md#completed-outcomes)
fixes the collector teardown blocker and completes repeated role-swapped
suite timing. Strings, memory, control-flow and graphics improve beyond
the exact envelope, without resolved suite regressions. The candidate remains
opt-in pending production maze/peripheral/stack coverage; partial D05
campaigns below remain historical failed evidence, not accepted timings.

```sh
badge-compiler/build/badge-codegen --scalar-selection=thumb-schedule \
  input.ll -o output.s
BADGE_CODEGEN_SCALAR_SELECTION=thumb-schedule \
  bash badge-compiler/scripts/build-owned.sh
make -C badge-compiler test-scheduling
```

The callable backend exposes `BADGE_SELECTION_THUMB_SCHEDULE`. CLI validation,
pipeline/build drivers, benchmark provenance and the hardware collector accept
the same policy. Ordinary builds still use
`direct/global-compact/thumb-layout/word`.

## Structured boundary and effects

[The scheduler](../../src/codegen_schedule.c) operates on a fixed typed array
at the actual selection/emission boundary, not parsed assembly strings. Each
record has an opcode, allocated physical operands, selected immediate and
explicit GPR read/write masks. Supported records cover MOVW/MOVT/MOV,
flag-preserving register/immediate integer ALU and standalone shifts, MUL,
MLA, and direct aligned private-frame word loads.

MOVT reads its previous destination. Destructive operand overlap is retained
in both masks. MLA reads both multipliers and its accumulator; an accumulator
that also occurs as a multiplier is an early input. Frame loads read r7, so
base-register dependencies are not lost. Scratch reuse and allocated-value
overlap are physical dependencies, not an assumption that distinct IR values
are independent.

The representation admits only instructions that preserve APSR. All other
output flushes the queue before emission:

- Stores, including spills, and all ordinary/general, volatile or MMIO memory
  operations are barriers.
- Calls, inline assembly, carry chains, compare/IT sequences, divisions and
  unsupported operations are barriers.
- Labels, branches, PHI-transfer emissions, prologues/epilogues and
  basic-block boundaries are barriers.

There is no store/load alias guessing or memory speculation. A frame load
cannot be chosen as the instruction moved earlier. Independent ALU work may
cross a private-frame load, but frame-load order is retained and no store or
general-memory access can be crossed. This limited permission does not extend
to addressable application buffers. Existing frame allocation, scratch/PHI
reservations and call preservation remain unchanged.

## Bounded scheduling and readiness protection

The maximum window is **eight records**. On a known adjacent
producer/consumer delay, a greedy bounded search considers later independent
one-issue work. RAW, WAR and WAW checks cover every crossed instruction,
including the delayed consumer. A candidate that reads the delayed producer
is not useful filler.

Timing comes from the shared [D03 target model](target-cost-model.md), backed
by [the instruction notes](instruction-timing.md) and
[D02 calibration](hardware-calibration.md). Baseline issue and result
readiness remain distinct. Expanded-immediate complex ALU forms are
classified conservatively; plain imm12 and standalone shifts are exempt.
Multiply-family late forwarding applies only to an exclusively late MLA
accumulator input. An unknown dependency is not permission to reorder.

Independence alone is insufficient: filling one gap can remove another
producer's useful gap or make a value less ready at the window exit. Each
trial therefore runs a small local core-readiness scoreboard. A move is
accepted only when:

1. Total modeled local stalls strictly decrease.
2. No GPR has a larger pending readiness delay at the window exit.

This is a conditional local check, not a full runtime/WCET estimator.
Private-frame load readiness describes the core baseline, not arbitrary SRAM
waits. XIP fetch, bus contention, caches, interrupts and DMA are not converted
into fixed per-opcode corrections. External entry readiness is not inferred
across opaque barriers. Hardware measurement remains necessary.

There are no per-record heap allocations or new liveness/allocation analyses.
The compile-wide **16,000,000 charged-check budget** covers queue admission,
search, dependency comparisons, score construction and exit checks. Exceeding
it reports `local scheduling analysis work limit exceeded`; it does not
silently disable the policy. An unsupported typed timing query reports an
explicit error. Unsupported ordinary emissions instead retain their existing
lowering as visible opaque barriers.

The output annotation reports windows, encountered modeled hazards,
reordering moves, unfilled hazards, opaque barriers and cumulative work.
Work is module-wide: take its maximum/final value, **not the sum of
per-function comments**. Hazard/move counts are static scheduling observations,
not measurements of actual pipeline-stall cycles.

## Correctness and preservation qualification

The sanitizer-backed native suite includes targeted RAW/WAR/WAW conflicts,
scratch/operand overlap, MOVT's implicit use, MLA early/late roles, private
frame loads, side-effect/flag/control barriers, relocated-stall regressions
and window-exit readiness regressions.

It also executes **100,000 deterministic randomized windows**, comparing all
13 GPRs before/after scheduling. A separate reference readiness oracle checks
strict modeled benefit and non-worsening exits for changed windows. These
cases perform **5,535 accepted moves**. The test caps charged work at 6,000
checks for an eight-record window.

Portable API/CLI tests cover release/sanitizer determinism across all five
residency policies, owned assembly, duplicate policy errors and forced tiny
budget exhaustion. Failures preserve an existing output file; nonscheduling
policies are unaffected by the scheduler budget. The normal test target
includes the suite without adding an LLVM or datasheet dependency.

Both owned-IR and LLVM-IR execution routes pass, including PHIs, register
pressure, scratch clobbers, memory guards/tails, wide arithmetic, flag
sequences and bidirectional mixed AAPCS calls. The actual maze correctness
oracle passes. Driver/provenance tests, the owned pipeline and compact/model
regressions pass.

Final offline qualification covers **45 archived modules**:

- Disabled release and sanitizer output is byte-identical to qualified
  default assembly.
- Scheduled release/sanitizer output is identical.
- Existing allocation annotations, call/frame graphs and complete instruction
  multisets are preserved.
- There are **245 reordering moves**, **804 encountered modeled hazards** and
  **559 unfilled hazards**; maximum module work is **13,363 checks**.
- Additional register moves and spills are **zero**. The instruction
  multiset check includes original moves, loads, stores and branches.

Fresh corpus qualification preserves linked text/data/BSS and every function
address/size in **12 images**. The separately instrumented maze uses
`TILT_MAZE_COMPARE` and retains text **43,720**, data **16** and BSS
**160,448 bytes**. Scheduling can change instruction order and binary hashes,
but does not alter encoding selection, allocation or linked layout.

## Hardware and host qualification

The first hardware attempt timed out in the baseline control's second
role-swapped round at the existing collector's 60-second deadline. This is
retained as [failed initial evidence](../../build/datasheet/d05-hardware-results.json),
not hidden or treated as a candidate performance result. Both complete
16 MiB flash images were restored and independently hash-verified.

The final attempt completed a full two-role control campaign, then the first
candidate round again hit that deadline. Both complete 1,792-byte report
files had already been saved. Independent decoding validates their fresh
nonce, CRCs, source identity, clock configuration, correctness fingerprints,
kernel guards and work counts. The exact host cleanup stall is not diagnosed;
this is not silently converted to a successful campaign. The failed
[final hardware report](../../build/datasheet/d05-hardware-final-results.json)
and [validated partial data](../../build/datasheet/d05-partial-hardware-results.json)
remain separate.

That one physical role gives the following provisional median changes.
LLVM image/source/seed/clocks and board assignment match the control; LLVM
medians are unchanged for these four workloads.

| Workload | Control range, ticks | Scheduled range, ticks | Median change |
| --- | ---: | ---: | ---: |
| strings | 16,449..16,480 | 16,334..16,389 | -0.693% |
| memory | 201,442..201,591 | 200,054..200,209 | -0.733% |
| control-flow | 69,979..70,011 | 69,472..69,494 | -0.729% |
| graphics | 89,767..89,799 | 88,963..88,992 | -0.884% |

Each configuration has seven samples for twelve workloads and two compilers
(168 workload timings). Other same-role changes are near zero, including
small increases of 0.044% in cross-file ABI and 0.043% in signed-div/rem.
This is **not** the required repeated, role-swapped candidate campaign or a
default-promotion result. It does not measure actual pipeline-stall cycles,
and no new maze physics/full RAM drawing hardware timing was collected.

After the final failure, both original complete 16 MiB flash images were
again restored and independently hash-verified. Both badges remain in
BOOTSEL. No further blind hardware retry was made.

Five alternating 45-input CLI batches after restoration measured median
wall totals of 375.2 ms for the control and 331.2 ms for scheduling. Ranges
overlap (335.5..387.6 ms and 323.8..365.7 ms); startup and input/output I/O
are included, so this is not an in-process compiler speedup claim. Both
median maximum individual-process RSS values are 5,898,240 bytes. CPU
measurements round to zero at 10 ms resolution: unresolved, not free work.
The fixed window, explicit work budget and tested 6,000-check window ceiling
bound the additional analysis independently of these coarse host readings.

## Decision

Keep `thumb-schedule` explicit and defaults unchanged. Its bounded structured
effects and conservative readiness gates are reusable across the compiler
corpus, not a maze-specific rule. Implementation, correctness, corpus/layout
preservation and host qualification are complete. The historical D05
status is `offline-qualified-hardware-incomplete` because of collector timeouts.
D09 subsequently fixes teardown and completes the repeated role-swapped
suite gate, but retains the candidate pending production-runtime coverage,
not speed-default-qualified.
D04 remains an independent size-oriented
policy; any future composition needs separate flag/width/layout qualification.
D06 may investigate structured branch/alignment/literal effects without
relaxing these dependency and side-effect contracts.

[D05 results](../../build/datasheet/d05-results.json) retain input/output,
source, executable and validation-log hashes. Initial offline evidence remains under
[D05 initial results](../../build/datasheet/d05-initial-results.json); its
earlier scheduling counts are superseded by the refined exit-safe algorithm.
Raw badge flash snapshots remain local recovery data, not publication artifacts.
