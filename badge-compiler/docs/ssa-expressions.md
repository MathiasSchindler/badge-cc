# PO05: cleaner initial C expression IR

PO05 adds an explicit frontend policy, independent of the optimizer and
backend selectors:

```sh
badge-compiler/build/badge-ir --expression-lowering=ssa input.i -o output.ll

BADGE_IR_EXPRESSION_LOWERING=ssa \
  bash badge-compiler/scripts/build-owned.sh
```

`ssa` is now the default, queryable with `--default-expression-lowering`;
`legacy` remains explicitly selectable. See the
[authorized PO09 promotion](pipeline-promotion.md#authorized-default-promotion).
The experiments and unchanged-default statements below describe the original
PO05 qualification before that promotion.
The shared driver validates the environment before compilation and records
`frontend-expression-lowering.txt`. Stage capture replays the selected policy;
physical campaigns record `ownedExpressionLowering`. Historical reports that
predate the field denote legacy lowering, not an unspecified candidate.
No optimizer/backend default or supported language/platform changes.

## General lowering contract

[The C frontend](../src/badge_ir.c) uses its existing semantic types and
operation types, rather than recognizing applications or imitating Clang's
AST schema.

- Comparisons used as conditions emit their i1 result directly. Operand
  promotions, signed/unsigned predicates and pointer semantics still come
  from semantic analysis.
- C comparison and logical **values** retain their required C type. For
  example, `(a && b) + 7` still converts the boolean result to C `int`.
- Scalar conditional expressions cast each selected arm to the semantic
  result type and join with a PHI. The incoming block is the actual block
  after lowering the arm, including any nested conditional/short circuit,
  not merely the block initially assigned to that arm.
- Logical expressions retain explicit short-circuit control flow and join
  with an i1 PHI. Unselected effects never execute.
- Parentheses, selected `_Generic` arms, comma and logical negation can use
  condition lowering. Comma's left side still executes. Explicit casts and
  other expressions use ordinary value lowering before truth conversion:
  `(unsigned char)256` must remain false.
- Aggregate conditionals retain their existing temporary/copy route, so
  records have value/snapshot semantics rather than aliasing a selected
  original object. Void conditionals retain selected side effects and CFG.
- Bool memory storage remains i8; its SSA representation remains i1.
  Ordinary volatile access widths, evaluation order, AAPCS lowering, actual
  object lifetime/addressability and all existing conversions remain intact.

Floating truth uses `fcmp une`, including NaN-as-true. Floating scalar PHIs
are LLVM-valid, but this does **not** establish owned soft-float runtime
support. No new `nsw`, `nuw`, `noundef`, `inbounds`, alignment or alias
assumptions are added. Existing recursion/resource bounds still diagnose
invalid or excessively nested input and preserve pre-existing output.

For an ordinary condition, legacy

```llvm
%comparison = icmp slt i32 %a, %b
%integer = zext i1 %comparison to i32
%truth = icmp ne i32 %integer, 0
br i1 %truth, label %yes, label %no
```

becomes a direct branch on `%comparison`. In a C value context the extension
remains. Scalar `?:` and logical joins replace only private result
alloca/store/load sequences, not user-addressable objects.

## Real-source and semantic qualification

- [Initial/optimized IR, semantic captures and Clang references](../build/po05-expressions/results.json).
- [37-module, 12-image corpus, budgets, frame/call metadata and both execution routes](../build/po05-corpus-4/offline.json).
- [Focused target semantic tests and bounded-input checks](../build/po05-final-semantic.log).
- [Both-policy native IR sanitizer and LLVM verification](../build/po05-validation.log).
- [Production-source application execution](../build/po05-application-oracle.log).
- [SSA frontend O0/Oz host and mixed narrow/record ABI execution](../build/po05-frontend-abi.log).
- [Driver selection/provenance tests](../build/po05-driver-tests.log).
- [Stage capture and physical-runner policy tests](../build/po05-final-tests.log).
- [Protocol, historical report and exact-noise tests](../build/po05-report-tests.log).
- [Archived PO04 frontend preservation check](../build/po05-historical-frontend.log).

The maintained [expression evidence tool](../tools/expression-evidence.mjs)
feeds each of the **same 37 actual preprocessed modules** to legacy, SSA and
Clang, verifies IR, captures owned semantic analysis and records initial
operation counts and both owned optimized results. Default/explicit-legacy
replays match, and owned function signatures are unchanged across policies.
All 37 legacy modules also match the archived PO04 frontend output, after
normalizing only source filenames and the three documented Wi-Fi build-ID
arguments.

Clang signatures and semantic captures remain inspectable references.
Aggregate ABI/AST representation differences are not hidden by asserting
identical schemas. The independent Cortex-M33 oracle directly checks scalar
signatures against Clang and executes signed/unsigned promotion, wide and
narrow selection, bool storage, pointers, record/union size/alignment,
selected generic arms, nested effects, narrowing conditions, loops, continue,
goto, void arms and aggregate snapshots. It runs direct Clang C, raw IR via
LLVM O0, and both LLVM/owned optimizer results through both accepted backends.
Native positive, negative and mutation fixtures run both frontend policies
under ASan/UBSan and LLVM verification; legacy output-shape expectations
remain legacy checks, not constraints on cleaner candidate IR.

Across the real corpus, **36 of 37 modules** have changed initial IR:

| Initial IR metric | Legacy | SSA | Reduction |
| --- | ---: | ---: | ---: |
| Private/result allocas plus existing allocas | 3,304 | 2,207 | 1,097 |
| Loads | 10,206 | 9,109 | 1,097 |
| Stores | 6,307 | 4,113 | 2,194 |
| Comparisons | 5,719 | 2,618 | 3,101 |
| Boolean-to-integer extensions | 3,134 | 59 | 3,075 |
| PHIs | 0 | 1,097 | 1,097 added |

Branch count (7,039), calls (2,292) and ordinary arithmetic operation counts
remain unchanged in this capture. These are static initial-IR metrics, not
retired instructions or runtime savings. The default scalar-cleanup/mem2reg
passes already remove many legacy redundancies; downstream improvements are
therefore substantially smaller than the initial count changes suggest.

After the identical owned optimization pipeline, allocas (240), loads (2,382),
stores (1,604), PHIs (1,665), branches (7,041) and calls (2,294) are identical.
Surviving comparisons fall **3,497 to 2,620**, extensions **1,870 to 994**,
freezes **1,055 to 1,029** and truncations **315 to 312**. In particular,
boolean PHIs no longer force integer materialization before testing a joined
condition. This distinguishes frontend cleanup already recovered by mem2reg
from changes that survive into backend input.

In the actual maze module, allocas fall 208 to 137, loads 711 to 640, stores
435 to 293, comparisons 345 to 154 and boolean extensions 192 to 3. Scalar
joins add 71 PHIs; all 31 function signatures are preserved.

### Linked budgets and an explicit frame tradeoff

The comparison holds `direct/global-compact/thumb-homes/word` and the C06
default pass list identical. All **12 linked images** are smaller, by 16 to
3,064 bytes; data/BSS are unchanged and flash/main-RAM budgets pass.
Ordinary maze text is **35,868 to 35,460 bytes**, **1.14% smaller** than PO04.
The separately instrumented maze is 37,880 to 37,472 bytes.

Across **554 frame/call checks per policy**, two frames shrink and **one grows**:
`wifi_gspi_command` grows from 40 to 48 bytes including unchanged saves.
The summed reservation delta is -8 bytes. Calls, outgoing arguments and
conservative uncertainties remain unchanged; no module's unallocated-value
count grows. A new SSA live range/PHI transport is not assumed free merely
because it replaces a frontend temporary.

PO05's corpus report explicitly lists growing frames, spill counts and
images. The one growth prevents a no-frame-growth/promotion claim:
`memory_and_frames` is false and `promotion_eligible` is false for the SSA
candidate. Earlier same-IR milestones retain their strict no-growth gates;
only this explicitly different-IR experiment records tradeoffs for review.
Main-RAM headroom does not certify the production 4 KiB scratch stack,
recursion, indirect calls, interrupts or peripheral execution.

## Physical badge results

- [Complete frontend-only experiment and raw campaign references](../../build/application-benchmark-runs/run.nFRiiv/results.json).
- [Independent raw reports, source identity and full-flash verification](../build/po05-hardware-verification.log).
- [Capture log](../build/po05-hardware.log).
- [Historical PO04 report replay with the extended verifier](../build/po05-historical-report.log).

Eight fresh campaigns cover two seeds, two repeats, both frontend policies
and two physical role swaps each: **16 rounds, 32 reports, 1,344 warm samples**.
All production application fingerprints, guards, counts, nonce/build
identities and clocks match. Percentages below summarize the change in the
owned/ordinary-LLVM median ratio across campaigns; the qualified decisions
use exact integer-rational per-board/per-repeat envelopes, not those rounded
summary percentages.

| Reached workload | Less elapsed time vs PO04 legacy frontend | Exact qualification | SSA / ordinary LLVM |
| --- | ---: | --- | ---: |
| Maze generation | **6.83%** | Both seeds, boards and repeats | 7.127x |
| Physics/scripted input | 0.22% | Dense seed only | 8.687x |
| RAM rendering | **0.32%** | Both seeds, boards and repeats | 3.141x |
| Display transfer | 0.008% | Within noise | 5.434x |
| Full reached frames | 0.086% | Second seed only | 5.280x |
| Production diagnostics/graphics | **1.33%** | Both seeds, boards and repeats | 3.186x |

There are **no resolved regressions**. Physics and frames are not claimed as
uniform wins; panel transfer is not a qualified improvement. The remaining
ordinary LLVM gap is **3.141-8.687x** on the measured batches. Cleaner IR is
not a claim that all initial operation reductions survive optimization or
that LLVM has been beaten.

Both fresh original 16 MiB flash snapshots were restored and independently
reread/hash-verified; both badges are left in BOOTSEL. Original hashes:

| Badge | Restored full-flash SHA-256 |
| --- | --- |
| `9BBF88DD17D66E7D` | `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff` |
| `08DA26F86E57F5EA` | `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d` |

PO05 is complete as a general, semantic-validated, measured frontend
candidate. Defaults remain legacy frontend and thumb-layout backend;
PO09 must independently review the explicit frame tradeoff and physical/
scratch-stack coverage before promotion.

## Reproduction

From the project root, with LLVM and QEMU available:

```sh
make -C badge-compiler test-ssa-expressions
node badge-compiler/tools/qualify-datasheet.mjs \
  --offline-expressions badge-compiler/build/NEW-po05-corpus
node badge-compiler/tools/expression-evidence.mjs --capture \
  badge-compiler/build/NEW-po05-corpus/offline.json \
  badge-compiler/build/NEW-po05-expressions
node badge-compiler/tools/expression-evidence.mjs --verify \
  badge-compiler/build/NEW-po05-expressions/results.json
```

Use fresh directories; retained failed/superseded captures are not overwritten.
For physical testing, both explicitly selected badges must initially be in
BOOTSEL:

```sh
node badge-compiler/tools/application-benchmark.mjs \
  --measure-expressions SERIAL_A SERIAL_B
node badge-compiler/tools/application-benchmark.mjs --verify \
  build/application-benchmark-runs/NEW/results.json
```

This changes only legacy/SSA frontend lowering, holding the PO04 backend
identical. Ordinary freestanding Clang `-Oz` remains the independent reference.
The preservation wrapper takes fresh full 16 MiB snapshots before any write,
restores in `finally`, independently rereads/hash-checks both complete images
and leaves the badges in BOOTSEL. Timing uses two seeds, two repeats,
role-swapped boards and exact per-board/repeat uncertainty envelopes.

Physical IMU acquisition/motion, real input/pacing, cold-XIP/startup, visual
panel correctness/electrical waveforms and production scratch-stack high
water remain unqualified. The actual panel GPIO driver executes, but that
alone is not physical display certification.
