# PO07: ABI-safe call transport

PO07 originally introduces opt-in `--scalar-selection=thumb-calls` /
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-calls`. It includes PO04 `thumb-homes`
and PO03 `thumb-branch`, with two independently measured transport changes.
The qualified control and candidate both use PO05 SSA expression lowering
and unchanged default optimizer passes. PO06 `local-memory` is **not** enabled
in this experiment: benefits from independent candidates are not added
together or treated as combination qualification.

PO09 now promotes `thumb-calls` together with SSA lowering and PO06 local
memory reuse. See the [authorized promotion](pipeline-promotion.md#authorized-default-promotion).
The independent experiments and unchanged-default statements below describe
the original PO07 campaign, not the new combined default.

## Implementation and ABI contract

1. Capture incoming non-byval register argument words directly from r0-r3
   into their existing allocated register or scalar home, instead of
   first copying each word to r5.
2. Emit an outgoing stack-argument word directly with
   `str.w r5, [sp, #offset]` when the aligned nonnegative byte offset fits
   the Thumb unsigned immediate form. Offsets through 4092 qualify;
   4096 and above retain the original r12 address materialization.

Both changes are active only with `global-compact` residency. Other residency
policies retain byte-identical `thumb-homes` emission. The shared API,
CLI validation/default query, build drivers/provenance, corpus tooling,
benchmark identity and hardware verifier recognize the new selector.

The incoming-argument loop still follows the same staged AAPCS cursor:
double-word alignment holes, split register/stack arguments, hidden result
pointers, small integer normalization and wide word order are unchanged.
Allocated destinations are preserved registers, not other incoming argument
registers. Narrow normalization may modify its own incoming register only
after its word is captured; no later argument needs that word.
Byval arguments retain the existing object snapshot and r5 scratch route.
No below-SP storage, altered outgoing reservation, stack alignment change,
different frame base or callee computation is introduced.

The outgoing change only replaces the address for an existing word store.
It does not change argument evaluation, signed extension, byval reads,
store order/count, current SP, call target or return transport. The caller
does not rely on r12 surviving a public call. Indirect calls continue through
r12; callback and recursion semantics remain explicit.

Result copies, conservative saves, scratch/clobber masks, allocation
priorities and interference graphs are **unchanged**. In particular, r4-r8
and the selected r10/r11 save set cannot simply be removed because they
appear unused in source: memory lowering, PHI cycles, incoming snapshots
and inline assembly have hidden requirements. Keeping those policies
fixed isolates this experiment and avoids an unproved ABI optimization.

## Actual-source attribution and inlining decision

[Maintained call evidence](../tools/call-evidence.mjs) recompiles all 37
actual optimized modules through both selectors using the exact same
input for each pair. Attribution comments must not change executable
objects; normal candidate assembly must replay the corpus capture.
Its verifier asserts unchanged:

- Register assignments, value/object homes, PHI transport and frame/save bytes.
- Every IR operation and every noncall assembly sequence.
- Every call target, instruction after the call, intrinsic and inline-assembly
  sequence, including result transport and outgoing-stack restoration.
- Emitted load/store counts; no function instruction count may grow.

Each function report separates entry instructions, pre-call argument
marshaling, call targets, post-call result/stack restoration, intrinsic
effects and noncall lowering. These are static emission counts, **not**
isolated retired-cycle measurements. Noncall lowering includes the return
epilogue; it is not a claim that every such instruction computes source
arithmetic.

For example, actual maze `physics_step` has 19 pre-call argument instructions
and eight post-call transport instructions across ten direct sites; its
227 noncall instructions, 16-byte frame and 32-byte saves are unchanged.
`draw_board` has 199 pre-call instructions and 32 post-call transport
instructions across its graphics/geometry/text calls. `render_frame` loses
one entry copy; `draw_marble` loses two. Full function records, not these
selected examples, determine the corpus result.

Bounded inlining was investigated but is not selected. The reached
zero-argument `random_next` receives neither transport change; it has 51
noncall/return instructions and a four-instruction entry. The renderer
reuses geometry and public graphics callees at multiple sites, while
callback/indirect edges and recursive behavior are not a whole-program
graph. Duplicating bodies would introduce code growth and live-range/frame
changes that this transport candidate neither needs nor qualifies.
The remaining inlining opportunity requires its own measured suitable
callee set, growth accounting and call-graph/semantic implementation;
no application-name recognizer or speculative whole-program promise was
added to win this milestone.

## Static qualification

Final corpus:
[offline qualification](../build/po07-final-corpus/offline.json).
Same-input attribution:
[call evidence](../build/po07-final-call-evidence/results.json).

| Measure, across 37 modules / 554 functions | Control | Candidate |
| --- | ---: | ---: |
| Entry instructions | 4,510 | 3,521 |
| Call/intrinsic transport instructions | 13,774 | 12,528 |
| Noncall lowering/return instructions | 72,494 | 72,494 |
| Total emitted instructions | 90,778 | 88,543 |
| Emitted loads | 8,423 | 8,423 |
| Emitted stores | 5,107 | 5,107 |
| Sum of private frames | 8,904 bytes | 8,904 bytes |
| Sum of callee saves | 16,704 bytes | 16,704 bytes |

The 2,235-instruction reduction consists of 989 incoming copies and 623
outgoing address materializations, each of the latter removing two
instructions. All 554 frame/call checks are unchanged, including maximum
outgoing reservations and conservative uncertainties. Allocation counts
and memory traffic do not grow.

| Ordinary firmware | Control text | Candidate text | Change |
| --- | ---: | ---: | ---: |
| Button mapper | 13,428 | 12,992 | -436 |
| Diagnostics | 58,844 | 57,540 | -1,304 |
| GPIO events | 11,204 | 10,772 | -432 |
| GPIO monitor | 8,800 | 8,660 | -140 |
| Hello idle | 568 | 568 | 0 |
| IMU demo | 30,016 | 29,320 | -696 |
| Lower mapper | 13,660 | 13,224 | -436 |
| Peripheral probe | 16,772 | 16,248 | -524 |
| Screen test | 5,980 | 5,856 | -124 |
| Tilt breakout | 23,260 | 22,976 | -284 |
| Tilt maze | 35,460 | 34,884 | -576 |
| USB Wi-Fi UDP | 369,228 | 366,808 | -2,420 |

Eleven images shrink; none grows. Ordinary maze text is **1.62% smaller**
against the independent PO05 control. Data is 16 bytes, BSS 160,440 bytes
and main-RAM headroom 363,832 bytes, unchanged. The instrumented maze is
37,472 to 36,800 text bytes with unchanged 16-byte data / 160,448-byte BSS.
Different retained instrumentation paths make its text delta different.
All linked flash/RAM budgets pass; actual objects, not instruction-count
estimates, determine these sizes.

## Correctness and coverage

[Focused transport tests](../tests/call-transport.test.mjs) cover unchanged
allocation/computation, exact SP offsets 4092/4096, large direct aggregates,
byval split-word snapshots, inactive residency policies, sanitized/normal
object equality, duplicate/invalid policies, charged work exhaustion and
output preservation on failure.

[The target oracle](../tests/codegen_oracle.sh) runs both optimizer routes
with the candidate, LLVM callers into owned callees, owned callers into
LLVM callees, and all-owned execution. Existing oracles exercise signed and
unsigned small integers, bool/narrow returns, 1/2/3-byte records, register
composites, hidden aggregate returns, large stack arguments, double-word
alignment, incoming offsets above 64 KiB, callback-live values, inline
assembly and aliased memory operations.

[Additional ABI execution](../tests/fixtures/call_transport_abi_fixture.c)
uses runtime-sampled depth and volatile function targets. The owned oracle
must retain actual recursive and indirect call instructions; matching
outputs after accidental devirtualization alone is not sufficient.
These fixtures are semantic boundary tests, not synthetic performance
workloads or compiler recognizers. Performance uses actual production
maze, shared graphics and diagnostics code.

The existing native sanitizer matrix also passes: 33 positive, 27 negative
and 113 mutation fixtures, API/CLI equivalence, repeated calls, resource
bounds and output preservation/alias checks. All 35 selected benchmark,
build-driver, hardware-protocol and runner-mock tests pass, including
`thumb-calls` provenance; the owned pipeline policy/stage-order tests pass.

## Qualified physical results

Final restored run:
[raw application campaign](../../build/application-benchmark-runs/run.kNEdDz/results.json).
Eight campaigns contain sixteen role-swapped rounds, 32 badge reports and
**1,344 matched raw samples**. Both seeds (3276377847 and 123456789), both
boards and both repeats use the same C sources, SSA policy and owned pass
list; only the backend selector changes.

| Production workload | Elapsed reduction | Exact gate on both seeds |
| --- | ---: | --- |
| Maze generation | 0.102% | Within noise; not a qualified win |
| Physics/input | **1.608%** | Qualified improvement |
| RAM render | **0.452%** | Qualified improvement |
| Display transfer | **2.777%** | Qualified improvement |
| Reached frame | **2.143%** | Qualified improvement |
| Diagnostics/graphics | **2.880%** | Qualified improvement |

Percentages summarize paired owned medians geometrically; they do not
determine acceptance. Unchanged exact integer-rational envelopes include
sample spread, repeated same-board control drift and paired LLVM drift.
Every physical role/repeat must pass. **No resolved regressions** occur on
either seed, and five workloads qualify on both seeds. The small generation
mean is not promoted into a claimed speed improvement.

LLVM's reference here is ordinary freestanding whole-source Clang `-Oz`,
not PO06's owned-initial-IR optimizer crossover or PO02's backend-only
reference. Candidate owned/LLVM ratios are respectively 7.120x, 8.548x,
3.127x, 5.283x, 5.167x and 3.094x for the rows above. The remaining gap
is substantial; the transport win is not evidence that LLVM has been
matched or that call overhead explains the whole gap.

Both original full-flash images were independently snapshotted before
writes, restored and completely reread/hash-verified afterward:

- `9BBF88DD17D66E7D`:
  `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff`.
- `08DA26F86E57F5EA`:
  `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d`.

Both badges are left in BOOTSEL. Current-source hashes, raw protocols,
firmware images, roles/nonces, clocks and full-flash restoration pass
the persistent verifier. PO07 is **qualified, retained opt-in**;
defaults and unqualified physical/stack limits remain unchanged.

## Reproduction

```sh
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
  LLVM_CLANG=/opt/homebrew/opt/llvm/bin/clang \
  LLVM_OPT=/opt/homebrew/opt/llvm/bin/opt \
  make -C badge-compiler test-call-transport

node badge-compiler/tools/qualify-datasheet.mjs \
  --offline-calls NEW_CORPUS_DIRECTORY
node badge-compiler/tools/call-evidence.mjs \
  --capture NEW_EVIDENCE_DIRECTORY NEW_CORPUS_DIRECTORY/offline.json
node badge-compiler/tools/call-evidence.mjs \
  --verify NEW_EVIDENCE_DIRECTORY/results.json

node badge-compiler/tools/application-benchmark.mjs \
  --measure-calls SERIAL_A SERIAL_B
node badge-compiler/tools/application-benchmark.mjs \
  --verify-current COMPLETED_RUN_DIRECTORY/results.json
```

Build tools before capture, use fresh evidence directories and do not
edit source/tool files during a campaign. The physical harness independently
snapshots/restores both full 16 MiB flash images and leaves them in BOOTSEL,
including failed campaigns. Retain rejected/failed evidence rather than
rewriting provenance.

## Limits and promotion

No default promotion, bounded inlining, caller-saved allocation, leaf-save
elision or tail-call optimization follows from this implementation.
The initial oversized-byval probe exposed an existing source-load offset
limit: byval reads beyond the supported immediate range are not made
supported by optimizing their outgoing destination addresses. That failed
probe is retained in [the initial test log](../build/po07-call-tests.log); the 4092/4096 destination
boundary is qualified with a large direct aggregate instead. Arbitrary
LLVM-generated nonstandard integer widths remain outside the existing
backend input subset; runtime-sampled semantic tests avoid presenting
unaccepted LLVM induction types as a call-transport success.
Frame metadata is not whole-program stack certification: recursion,
indirect targets, interrupts and production 4 KiB scratch-stack demand
remain separate gates. The hardware benchmark has an isolated large stack.
Warm scripted motion and actual GPIO transfer do not qualify physical IMU
acquisition, real pacing/input, cold-XIP/startup, visual/electrical panel
correctness or network timing. These remain PO09 responsibilities.
