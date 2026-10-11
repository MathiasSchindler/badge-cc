# PO06: bounded local scalar load reuse

PO06 adds the independent C06 `local-memory` pass. PO09 now appends it to the
default pipeline:
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory`.
See the [authorized combination promotion](pipeline-promotion.md#authorized-default-promotion).
The experiments and default-preservation statements below describe the original
PO06 campaign, before that promotion.

```sh
BADGE_IR_EXPRESSION_LOWERING=ssa \
BADGE_CODEGEN_SCALAR_SELECTION=thumb-homes \
BADGE_OPT_PASSES=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory \
  bash badge-compiler/scripts/build-owned.sh
```

The selected experiment uses PO05 SSA lowering and the PO04 backend on both
sides, changing **only** the final optimization pass. The pass also accepts
other supported IR producers; it does not depend on application names,
source idioms, PO05-specific tags or game fixtures.

## Investigation and selected scope

PO05's identical default optimizer outputs still contain repeated ordinary
scalar loads from the same address, including a reload immediately after a
store. Existing mem2reg addresses nonescaping private slots, not arbitrary
global/indirect memory. Existing local CSE deliberately excludes loads.

In production `random_next`, the control has **seven loads and three stores**
of the random-state global. They implement ordinary scalar XOR/shift updates,
not volatile hardware acquisition. The candidate retains four loads and all
three stores, reusing the three duplicate observations between writes.
The same general rule applies in other actual modules; there is no xorshift
recognizer.

This is deliberately narrower than general alias-aware memory optimization.
Safe expression reuse, CFG folding and more aggressive aggregate or loop
optimization remain separate candidates. No rejected loop motion, signed
division expansion, speculative algebra or inlining is revived.

## Legality and implementation

[The pass](../src/ir_local_memory.c) uses the existing typed instruction,
operand, definition/use and block analysis:

- Only ordinary i8/i16/i32 and pointer loads are considered. Wide,
  aggregate, i1 storage, floating and metadata-bearing accesses retain their
  original lowering. Unsupported atomics still diagnose an error at the
  established parser boundary; the pass does not reinterpret them.
- A single previous ordinary load is tracked per block. Reuse requires
  **exact SSA/global pointer identity and equal scalar access type**.
  It does not assume different pointers cannot alias, compare numeric
  addresses, infer offsets or derive alignment/alias metadata.
- Every store invalidates the prior observation, including a store through
  the very same pointer, an unrelated pointer or a different scalar type.
  Other reads replace or invalidate it conservatively. Calls, opaque operations, volatile accesses
  and block boundaries invalidate it. No access is moved across a barrier.
- A removed load's producer is at most **two original IR instructions**
  earlier. Every consumer must be in the same block within two instructions
  of that load, with no intervening call/opaque/control operation. A final
  direct return is permitted. PHI-edge and cross-block users are excluded.
  Repeated forwarding does not refresh the producer's age.
- The earlier load remains. Stores are never rewritten, deleted, reordered
  or merged. User-addressable objects and their lifetime remain untouched.
  Stored `undef` is never substituted at load consumers. No new definitions
  or `freeze` captures are introduced; existing values are reused.
- Load forwarding preserves the earlier observation and does not speculate a
  new access. Alignment/metadata assumptions are not strengthened. No
  `nsw`, `nuw`, `inbounds`, `noalias` or `noundef` promises are added.
- Rewritten consumers use already-resolved aliases, including pointer-valued
  load results. Store operands can reference the retained load's SSA value,
  but no stored observation becomes a forwarding representative.

The existing context allocations/IR verifier are reused. Scans of
instructions, access tokens, bounded use chains and rewrite tokens charge a
50,000,000-step budget; the budget test builds an eight-step variant.
Exhaustion diagnoses `local-memory work limit exceeded` and preserves
pre-existing output. The shared pipeline verifies before every pass and
after the final pass; the evidence tool independently LLVM-verifies every
candidate pipeline prefix.

### Profitability and target costs

Eliminating an IR load is not assumed to eliminate exactly one Thumb
instruction. Address materialization, private scalar homes, copy transport,
register pressure and memory waits are separate costs. The chosen candidate
introduces no stored-value capture or fresh SSA definition; that removes one
source of added transport/lifetime costs, but does not make reuse free.

The bounded producer/consumer window avoids unbounded live-range extension
and call-crossing saves. It is a conservative eligibility limit, **not** a
cycle predictor or proof that every isolated rewrite is profitable. Same-input
emission, per-function frame/call checks, module allocation metadata, linked
size and exact per-board/repeat hardware gates qualify the independently
selected candidate as a whole. Unknown memory waits are not converted into
invented fixed cycle savings.

No division/rem arithmetic is expanded. D07's power-of-two legality,
quotient-sharing and target-cost checks are unchanged; `local-memory` is not
silently composed with `target-strength-reduce`, local CSE or loop motion.
The chosen experiment appends only this pass to the unchanged defaults.

## Semantic and actual-source evidence

- [Final 37-module/12-image corpus and budgets](../build/po06-near-corpus/offline.json).
- [Same-input optimizer crossovers, all pass prefixes and emitted objects](../build/po06-near-evidence/results.json).
- [Pass-specific sanitizer, budget and Cortex-M33 semantic checks](../build/po06-near-tests.log).
- [Dependency-free owned-mode memory tests](../build/po06-owned-memory-tests.log).
- [Production application: owned candidate, ordinary Clang and identical-initial-IR LLVM](../build/po06-near-application.log).
- [Existing native optimizer regression and output preservation](../build/po06-near-native.log).
- [Fixed-IR and initial-IR LLVM reference execution tests](../build/po06-final-tests.log).
- [Raw protocol, archived-IR corruption, physical-role and exact-noise tests](../build/po06-final-protocol.log).
- [Default optimizer preservation across archived PO05 SSA modules](../build/po06-default-preservation.log).

Focused semantic tests cover exact load reuse, stores as barriers, retained
stored `undef`, pointer-value forwarding, indirect aliases, overlapping byte
writes, volatile reads, calls, metadata, wide accesses, long live ranges,
block boundaries, malformed/unsupported input, output aliases and
budget-exhaustion preservation. They execute both optimizer/backend routes
on Cortex-M33 under QEMU. These tests are correctness inputs, **not**
performance evidence. The latter comes from actual production sources and
the two physical badges.

Existing native sanitizer regressions pass: **27 positive, 12 negative and
186 mutation fixtures**, with LLVM verification/backend assembly and
pass-selection/output-preservation checks. The full corpus uses the same SSA
frontend input for control and candidate; only the three established Wi-Fi
provenance arguments and source filename are normalized for cross-build
identity. Semantic/ABI oracle routes pass under LLVM and owned optimization,
including bidirectional mixed calls and the production maze.
All 37 default optimizer outputs also match archived PO05 SSA results after
only that documented provenance normalization.

The maintained [memory evidence tool](../tools/memory-evidence.mjs) replays
the same 37 initial modules through default owned optimization, default plus
`local-memory`, and LLVM `default<Oz>`. It captures every owned candidate
pipeline prefix and LLVM-verifies each one, verifies final optimized IR and
emits owned/LLVM objects. Owned replays match the corpus captures. LLVM's
different optimization/code shape is retained as a reference, not forced
into the owned optimizer's design.

| Corpus metric | Control | Candidate | Change |
| --- | ---: | ---: | ---: |
| Optimized IR loads | 2,382 | 2,372 | 10 removed |
| Optimized IR stores | 1,604 | 1,604 | Unchanged |
| Optimized IR freezes | 1,029 | 1,029 | Unchanged |
| Emitted instructions | 90,778 | 90,733 | 45 removed |
| Emitted load instructions | 8,423 | 8,398 | 25 removed |
| Emitted store instructions | 5,107 | 5,102 | 5 removed |
| Emitted calls | 2,099 | 2,099 | Unchanged |

The emitted memory instructions include backend private homes and transport,
not just the source-level scalar memory accesses. Five emitted stores
disappear through changed residency/home transport; **no source IR stores
are removed**. These are static emitted counts, not retired instructions,
dynamic spills or stack high-water measurements.
Three modules lose optimized IR loads. No module's emitted instruction,
load-instruction or store-instruction count grows in the capture.

LLVM optimization of the same initial IR retains 1,857 loads and 2,079 stores
across the modules; lower counts alone are not a universal ranking. Its
inlining, CFG changes, memory shaping and backend are substantially different.
The emitted LLVM objects and the hardware optimizer crossover are retained
to assess actual recovery rather than comparing operation counts alone.

### Linked budgets and allocation

All **554 frame/call checks** have unchanged frames, outgoing argument
requirements, calls and conservative uncertainties. No module's
unallocated-value count grows; IMU demo's count falls from 233 to 228.
This metadata is not dynamic eviction traffic.

**Three of twelve images shrink; nine are unchanged; none grows.**
Data/BSS are unchanged and linked flash/main-RAM limits pass. Ordinary maze
text is **35,460 to 35,380 bytes**, another **0.23% smaller** than PO05.
The separately instrumented maze is 37,472 to 37,392 bytes. Different
garbage-collected instrumentation paths mean those two deltas need not match.

Graphics and diagnostics-logic module emission is unchanged, so no speed
benefit is inferred for those workloads from the maze's counts.
The production maze module loses four optimized IR loads. The capture
contains per-module emission and object outputs rather than inferring a
hardware saving from those four alone.

PO05's previously documented 8-byte Wi-Fi frame tradeoff remains a property
of the chosen frontend/backend baseline; PO06 does not hide or claim to fix
it. No **new** frame growth is introduced by the memory pass. Main-RAM
headroom does not certify the badge's production scratch stack, recursion,
interrupt or indirect/peripheral paths.

## Qualified physical results

- [Final near-load experiment](../../build/application-benchmark-runs/run.VxR4CU/results.json).
- [Independent raw/initial-IR/full-flash verification](../build/po06-near-hardware-verification.log).
- [Capture log](../build/po06-near-hardware.log).

The final experiment contains eight campaigns, 16 role-swapped rounds,
32 reports and **1,344 matched warm samples**. Initial IR is byte-identical
across all optimizer routes/campaigns; application fingerprints, guards,
counts, clocks and build/nonce identities match. Exact integer-rational
per-board/per-repeat uncertainty gates remain unchanged.

| Reached workload | Summary elapsed reduction | Qualified decision | Candidate / same-initial-IR LLVM |
| --- | ---: | --- | ---: |
| Maze generation | **0.84%** | Improvement on both seeds, boards and repeats | 6.757x |
| Physics/scripted input | 0.117% | Not a repeatable improvement on either seed | 8.610x |
| RAM rendering | -0.001% | Within uncertainty | 3.121x |
| Display transfer | -0.001% | Within uncertainty | 5.537x |
| Full reached frames | -0.036% | No resolved regression; not an improvement | 5.351x |
| Diagnostics/graphics | -0.006% | Within uncertainty | 3.079x |

Negative summary reductions are slower median-ratio summaries, **not**
resolved regressions. The exact gates report **no regressions** for either
seed. Physics is not claimed as a uniform win; only generation qualifies.
The ratios above compare LLVM optimization/backend of the **same initial
owned IR**, not ordinary Clang C. They must not be presented as an updated
ordinary LLVM gap or as a backend-only PO02 result.

Both complete original 16 MiB images were restored, independently reread and
hash-verified, and both badges are left in BOOTSEL:

| Badge | Original/restored full-flash SHA-256 |
| --- | --- |
| `9BBF88DD17D66E7D` | `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff` |
| `08DA26F86E57F5EA` | `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d` |

PO06 meets its bounded candidate acceptance: full supported corpus
correctness, strict budgets, and a resolved reached-workload benefit without
resolved regressions. Defaults remain unchanged. PO09 retains promotion
responsibility and the independently open physical/scratch-stack gates.

## Reproduction

With LLVM and QEMU available, from the project root:

```sh
make -C badge-compiler test-local-memory
node badge-compiler/tools/qualify-datasheet.mjs \
  --offline-memory badge-compiler/build/NEW-po06-corpus
node badge-compiler/tools/memory-evidence.mjs --capture \
  badge-compiler/build/NEW-po06-corpus/offline.json \
  badge-compiler/build/NEW-po06-evidence
node badge-compiler/tools/memory-evidence.mjs --verify \
  badge-compiler/build/NEW-po06-evidence/results.json
```

Use fresh directories. Existing failed/superseded captures are preserved;
they are not rewritten to look like the final candidate.

For physical measurement, both explicitly selected badges must initially be
in BOOTSEL:

```sh
node badge-compiler/tools/application-benchmark.mjs \
  --measure-memory SERIAL_A SERIAL_B
node badge-compiler/tools/application-benchmark.mjs --verify \
  build/application-benchmark-runs/NEW/results.json
```

This isolates default/default-plus-memory optimization of the same PO05 SSA
frontend with the same PO04 backend. The LLVM reference uses its `-Oz`
optimizer **and backend on the byte-identical initial owned SSA IR**.
It does not disable LLVM optimization as PO02 does, and it is not ordinary
Clang C compilation. The compiler runner archives all 11 initial application
modules, verifies equality across routes and campaigns, and verifies archived
hashes during replay. Ordinary Clang C remains an independent semantic
reference in the application oracle.

The preservation wrapper snapshots both full 16 MiB images before any write,
restores in `finally`, independently rereads/hash-checks complete flash and
leaves both badges in BOOTSEL. Two seeds, two repeats, role swaps and seven
warm timing samples per workload are assessed with exact per-board/repeat
noise envelopes. Physical sensor acquisition/motion, real pacing/input,
cold-XIP/startup, visual/electrical panel correctness and production
scratch-stack high water remain separate unqualified gates.

## Rejected broader candidate

An earlier candidate also forwarded stores to subsequent loads, introducing
one frozen stored-value capture per qualifying store. It removed more static
IR/emitted traffic and passed the semantic/corpus gates, but its complete
[first hardware experiment](../../build/application-benchmark-runs/run.Tu64D0/results.json)
flagged a full-frame regression on seed `123456789`.

Generation improved 2.44% and physics/input 0.80% across the summary, but that
does not waive the regression gate. One board/repeat's frame median increased
192 ticks (0.0567%), exceeding its exact uncertainty envelope (0.0428%).
The other three observations were within noise. The gate rejects **any**
resolved regression; these observations are not averaged into success.
Both originals were restored and independently verified after this run.

[Function-level emitted deltas](../build/po06-function-deltas.log) show no
instruction-count increases, so static emission does not conclusively
attribute the tiny frame timing change. Code placement, residency and
memory/pipeline interactions remain possible mechanisms, not proven causes.
Rather than asserting a cause or weakening uncertainty thresholds, the
retained implementation removes the store-forwarding/capture dimension and
qualifies repeated-load reuse independently.

That second eight-instruction-window load-only candidate also flagged a
second-seed frame regression in its complete
[hardware experiment](../../build/application-benchmark-runs/run.s3LUOX/results.json).
Its 1.59% generation and 0.80% physics summary improvements do not override
that gate either. Both originals were independently restored/hash-verified.
Its [source](../build/po06-window8-memory.c),
[binary](../build/po06-window8-memory-opt) and
[static evidence](../build/po06-load-only-evidence/results.json) remain retained.

The final refinement bounds both producer age and all consumer distances
to two instructions. This reduces live-range extension and limits changes
to the nearest reuse opportunities, without application names or address
placement padding. Static results are smaller than either broader attempt;
the same semantic/corpus and exact hardware gates are reapplied from fresh
snapshots. No regression is attributed conclusively to a particular memory,
register or code-layout mechanism by these experiments.

The broader [source snapshot](../build/po06-full-memory.c) and
[compiler binary](../build/po06-full-memory-opt) are retained alongside its
[static optimizer/object captures](../build/po06-release-evidence/results.json).
Its static captures reference the then-current compiler hashes; current
source/tool drift is not rewritten to appear identical. The immutable raw
hardware reports and archived original firmware remain independently
replayable with `--verify`, not `--verify-current`.

A subsequent narrowed-candidate offline attempt detected compiler hash drift
because validation rebuilt the optimizer while capture was taking its
initial provenance snapshot. That [failed capture](../build/po06-load-only-corpus/offline.json)
is retained. The final narrowed corpus was freshly captured with tools
already built and stable; no provenance or budget check was relaxed.
