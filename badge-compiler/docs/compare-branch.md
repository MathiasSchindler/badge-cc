# PO03: adjacent compare/branch selection

PO03 implements the first general lowering mechanism selected by
[PO02's fixed-IR investigation](backend-attribution.md). It removes an
unnecessary materialized boolean when a supported i32 comparison feeds only
its immediately following conditional branch. No source name, game, seed,
loop bound or expected answer is recognized by the compiler.

The candidate is **opt-in**:

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-branch \
  bash badge-compiler/scripts/build-owned.sh
```

Its base is `thumb-layout`, not a combination of the D04 compact, D05
scheduling or D06 relaxation candidates. The default remains
`direct/global-compact/thumb-layout/word`, with the existing C06 passes.
Surpassing LLVM remains the goal; this change is not an LLVM imitation or
a claim that the remaining gap is acceptable.

## Legality, effects and bounded work

[Selection](../src/codegen_select.c) requires:

- An adjacent, same-block, exact supported i32 `icmp` followed by the terminal
  conditional `br`.
- Exactly one use of the i1 result over the whole function, including PHI
  incoming operands. Stored, returned, call and extra boolean uses retain
  materialization.
- Integer constants or SSA operands, and one of `eq`, `ne`, `slt`, `sle`,
  `sgt`, `sge`, `ult`, `ule`, `ugt`, `uge`.
- `global-compact` residency. Its existing value index covers i1 values,
  unlike the i32-only global/global-flex indexes. Those policies, local and
  stack residency conservatively retain the old path; allocation is not
  changed just to broaden this candidate.
- No inline assembly anywhere in the function. Narrow/wide comparisons,
  `samesign`, nonadjacent/cross-block consumers and other selectors retain
  their existing lowering.

[Emission](../src/codegen.c) reads the original operands into the same
reserved r0/r1 scratch registers, compares them once, and immediately consumes
APSR with the local conditional veneer. The predicate/inverse mapping is
shared with ordinary comparison lowering. Existing successor selection,
fallthrough layout and parallel PHI-copy transport remain in use; no edge
copy can run before the conditional consumes APSR. Distant successor targets
still use the existing unconditional wide branch machinery.

The existing structured scratch-effect classifier continues to classify i32
comparison and branch work without new r4/r5 clobbers. The shared
[RP2350 cost model](../src/target_cost.c) checks the known positive encoding
and baseline issue cost of the redundant ALU comparison before selecting
fusion. The rewrite removes additional boolean operations without adding
operand reads, allocation, PHI work or memory operations. This local cost
check does **not** predict branch history, memory waits, cold-XIP behavior or
whole-workload ticks. Scheduling is not enabled for this selector; no new
flag-bearing schedulable instruction representation is invented.

Allocation and frame planning still model the original compare definition.
Skipping its write preserves a source value if the dying operand and compare
result share a register or compact home. There is no intervening instruction.
The original result's unused assignment/home may remain: removing it is a
separate PO04 allocation change, not a hidden part of this experiment.

Volatile loads execute once, in their original order, before the comparison.
No load folding, MMIO removal, access-width change or memory-motion
optimization is included. Constant/address materialization reuse remains
deferred: it needs a live-value/barrier/spill proof, and D06 already showed
that smaller encodings need not be faster. Aggregate copy/fill expansion
keeps the existing alignment, overlap, tail and freestanding contracts;
neither memory lowering nor libc dependencies are changed.

Preparation reuses the bounded function-use scan, adjacent-pair checks and
existing selection hash table. Token/hash/probe work is charged to the
existing **33,554,432-step** register/selection limit. There is no CFG search
or new fixed-point iteration. A sanitizer build with a **1,024-step** limit
tests explicit exhaustion and preservation of an existing output file.
Unsupported/duplicate options and invalid API selections also fail explicitly.

## Exact static evidence

- [Final 37-module / 12-image corpus qualification](../build/po03-release-corpus/offline.json).
- [Historical-IR replay and per-comparison assembly traffic](../build/po03-release-traffic/results.json).
- [Exact PHI-edge, call and memory-traffic audit](../build/po03-edge-audit.log).
- [Focused sanitizer and Cortex-M33 tests](../build/po03-tests.log).
- [Callable API checks](../build/po03-api-tests.log).
- [Application protocol, restoration and exact-noise tests](../build/po03-protocol-tests.log).
- [Actual-source application semantic execution](../build/po03-application-oracle.log).

All **11** replayed baseline objects are byte-identical to PO02's archived
objects. Normal and attribution-traced objects are identical for both roles.
For that exact IR, the candidate fuses **458** comparisons and removes
**3,206** emitted static instructions, seven per pair. Across its **140**
functions, register assignments and frame/saved-register sizes are unchanged,
calls are identical, memory traffic does not increase, and all post-Bcc
PHI/edge instructions and local veneer targets are identical.

Examples from actual production code:

| Function | Emitted static instructions before/after | Fused comparisons |
| --- | ---: | ---: |
| `generate_maze` | 1,863 / 1,527 | 48 |
| `physics_step` | 314 / 272 | 6 |
| `badger_gfx_clear` | 40 / 33 | 1 |

These are attribution-trace assembly counts, not disassembler counts that
can include executable-looking pool words, and not retired instructions.
For the graphics clear loop, the same original operand preparation remains:

```asm
mov r0, r5
movw r1, #11264
movt r1, #1
cmp r0, r1
blo.w .Lcg_edge5
b.w .Lcg_1_b4
```

The former `mov.w`/IT/conditional move, i1 `ubfx`, result/condition copies and
second `cmp` disappear. The four full clears execute 307,204 comparison
visits, so the static seven-instruction reduction corresponds analytically
to 2,150,428 removed emitted instruction visits. Conditional execution means
this is **not** an on-target retired-instruction measurement.

The fresh corpus has **2,140** fused comparisons across 37 C modules,
**554** frame/call checks per profile and 12 linked firmware images.
Both LLVM-optimized and owned-optimized semantic routes, bidirectional mixed
LLVM/owned AAPCS calls and the actual maze oracle pass. Linked flash/RAM
budgets fit; data/BSS, call edges and allocation counts remain unchanged.
The 2,179 unallocated value assignments per profile are metadata counts,
not dynamic eviction counts. No image grows; `hello_idle` is unchanged.

| Ordinary linked image | Baseline text bytes | Candidate text bytes |
| --- | ---: | ---: |
| `tilt_maze` | 41,284 | 37,068 |
| `diagnostics` | 66,264 | 60,920 |
| `usb_wifi_udp` | 395,124 | 378,092 |

Ordinary maze linked text falls **10.21%**. The separately instrumented
maze falls from 43,508 to 39,136 bytes, with unchanged 16-byte data and
160,448-byte BSS. Neither is the application harness size.

Earlier static captures are retained as superseded evidence; only the
`po03-release-*` reports above describe the final cost-gated candidate.

## Real-badge qualification

The preservation-wrapped comparison uses ordinary Clang 22.1.8 `-Oz`,
not the fixed-IR LLVM backend denominator from PO02. Two seeds, two repeats
per seed and independent baseline/candidate campaigns each contain two
swapped physical rounds. Each report retains seven raw warm samples per
workload, clocks, nonces, guards, source identity and exact executed images.

Timing acceptance uses the existing exact integer-rational per-role/repeat
envelope: both raw spreads, repeated same-board baseline drift and paired
LLVM drift. Every physical observation must show improvement outside its
envelope for a workload to qualify; a mean cannot hide a regression.
Default promotion remains a separate PO09 decision.

[Complete hardware capture and independently verified full-flash restoration](../../build/application-benchmark-runs/run.gF9LoT/results.json)
contains **eight campaigns, 16 rounds, 32 raw reports and 1,344 workload
timing samples**. All fingerprints, guards, work counts, clocks, nonces and
executed-image checks pass. Capture-time source hashes still match.

All **48** workload/seed/repeat/owned-board observations qualify as improvements
outside their exact envelopes. Every workload is repeatably improved for each
seed; there are **no regressions**. The largest observed envelope is 0.208%,
well below even the smallest improvement.

The following figures summarize eight paired median observations per workload.
Lower elapsed time is better. LLVM ratios greater than one mean owned output
is still slower; they use the ordinary whole-source Clang pipeline, not
PO02's backend-only reference. No cross-workload score is computed.

| Reached workload | Elapsed-time reduction vs baseline | Baseline / LLVM | Candidate / LLVM |
| --- | ---: | ---: | ---: |
| Maze generation | 19.06% | 9.644x | 7.806x |
| Sensor processing, physics and collision | 16.46% | 10.599x | 8.855x |
| Full RAM rendering | 25.86% | 4.252x | 3.152x |
| Actual panel transfer | 5.11% | 5.727x | 5.435x |
| Reached frame policy and panel writes | 8.83% | 5.802x | 5.290x |
| Production diagnostics and clipped graphics | 29.84% | 4.629x | 3.248x |

Generation medians fall from **79,737-79,772** to **64,530-64,569** raw ticks.
Dense-level physics falls from **274,178-274,195** to
**231,297-231,362** ticks; intermediate-level physics falls from
**105,116-105,136** to **86,962-86,974** ticks. These are timer0 raw ticks,
not externally calibrated microseconds.

Both original full 16 MiB images were restored and independently reread:

| Badge | Original/restored SHA-256 |
| --- | --- |
| `9BBF88DD17D66E7D` | `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff` |
| `08DA26F86E57F5EA` | `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d` |

Both are left in BOOTSEL. PO03's semantic, budget, eliminated-traffic and
paired-benefit gates are complete. `thumb-branch` remains explicit, with
default promotion and interactions with other candidates reserved for PO09.
LLVM remains substantially faster on these workloads; PO04 and subsequent
general improvements are still needed.

[PO04's scalar-home and PHI follow-up](scalar-homes.md) now qualifies a
separate `thumb-homes` candidate against this PO03 baseline. Its measurements
are incremental, not replacements for the figures above.

## Reproduction

Run from the repository root with LLVM/LLD/QEMU available:

```sh
make -C badge-compiler test-compare-branch
node --test badge-compiler/tests/application-protocol.test.mjs \
  badge-compiler/tests/datasheet_qualification_test.mjs

node badge-compiler/tools/qualify-datasheet.mjs --offline-branch NEW_CORPUS_DIR
make -C badge-compiler build/badge-codegen-attribution
node badge-compiler/tools/compare-branch-evidence.mjs --capture NEW_TRACE_DIR \
  badge-compiler/build/po02-release-attribution/results.json
node badge-compiler/tools/compare-branch-evidence.mjs --verify NEW_TRACE_DIR/results.json

BADGE_CODEGEN_SCALAR_SELECTION=thumb-branch \
  bash badge-compiler/tests/application-oracle.sh
node badge-compiler/tools/application-benchmark.mjs --measure-branch SERIAL SERIAL
node badge-compiler/tools/application-benchmark.mjs --verify REPORT.json
```

Use fresh output directories. The hardware command takes two fresh full
16 MiB snapshots before any programming and restores both in `finally`,
independently rereading and hashing every flash byte, including after failure.
It explicitly selects each profile rather than inheriting accidental
environment policy overrides. Original PO01 and fixed-IR modes retain their
baseline-only report contract. `--verify-current` additionally checks
capture-time source hashes; later source changes are drift, not permission to
rewrite historical provenance.

Physical IMU acquisition/motion, visual/electrical panel certification,
controlled cold-XIP/startup, arbitrary-program performance and interrupt-
inclusive production stack safety remain unqualified. Scripted frame
measurements exclude sensor I2C acquisition and live frame pacing. The
isolated benchmark's 64 KiB stack is not a production-stack certification.
