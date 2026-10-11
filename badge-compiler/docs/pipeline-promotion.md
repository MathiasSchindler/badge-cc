# PO09: independent pipeline qualification and promotion

**Status: complete; the independently qualified PO06+PO07 profile is now default.**
The full seven-profile correctness, budget and two-suite hardware gate passes,
including the independently measured PO06+PO07 combination. The archived gate
originally retained all six profiles; the authorized policy decision below
promotes the combination without rewriting those results. Production
peripheral/interrupt/whole-program stack certification remains outside the
measured coverage. Earlier failed captures remain archived below.

## Authorized default promotion

The selected owned default is now:

- C05: `ssa` expression lowering.
- C06: `mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory`.
- C07: `direct/global-compact/thumb-calls/word`.

This includes PO03 compare/branch fusion, PO04 home/PHI improvements, PO05
SSA expressions, PO06 bounded exact-pointer reuse and PO07 call transport.
Older alternatives remain explicit controls. D04-D07 and other mixed-result
experiments remain opt-in. The root firmware build still defaults to LLVM.

The user authorized this combination after review of the completed evidence:
all six application and twelve unrelated workloads improve on both seeds,
boards and repeats; no resolved regressions or frame/spill/image growth occur
against the previous default. Calls, outgoing reservations and uncertainty
metadata remain equivalent. No major downside was found in that coverage.
Whole-program stack, live peripherals and cold-XIP limits are shared
uncertainties, not evidence of a new regression or a claim of universal safety.
The benchmark's 64 KiB stack is not certification of the production 4 KiB
scratch stack.

Default queries, convenience backend APIs and the owned build driver now
inherit the selected profile. Historical application experiments explicitly
pin their old control so a changed default cannot invalidate their comparison
policy. Fresh/forced production builds are still required after policy changes.

Focused build/default/driver/SSA tests pass. A single replay of all 37 exact
archived preprocessed production C modules gives byte-identical frontend IR,
optimized IR, assembly **and object files** to the measured explicit combination:
[`po09-promoted-replay/results.json`](../build/po09-promoted-replay/results.json).
No compiler transformation or assembler/linker implementation changed; the
qualified image inputs and linker scripts remain the same. No hardware
campaign was repeated, and neither badge was modified for this promotion.
Measured maze text remains 34,804 versus 41,284 bytes for the old control
(15.70% smaller); the timing reductions below are reused measurements of
that identical profile, not new measurements or LLVM parity claims.

The historical reports retain `promoted:false` and their original source/tool
hashes; this policy decision supersedes their retain status, not their measured
results. Rebuilding shared compiler binaries changes their hashes, so historical
`--verify-current` is not expected to pass after promotion. Keep the reports,
raw captures and replay evidence; do not relabel an old capture as current-source
qualification. The maintained seven-profile gate still uses explicit historical
profiles and computes eligibility rather than issuing this separate approval.

[`pipeline-promotion.mjs`](../tools/pipeline-promotion.mjs) is the maintained
PO09 gate. It reuses the D09 exact-tick classifier and the PO01 reached
application suite, with fresh inputs rather than historical performance
claims. Default changes are not inferred from smaller code, a favorable mean
or independently measured components.

## Explicit profiles

Every profile uses `direct/global-compact/word` addressing, residency and
memory selection. The optimizer list is
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`,
with `local-memory` appended only where shown.

| Profile | Expression lowering | Scalar selection | Local memory reuse |
| --- | --- | --- | --- |
| Pre-promotion control | legacy | thumb-layout | No |
| PO03 | legacy | thumb-branch | No |
| PO04 | legacy | thumb-homes | No |
| PO05 | ssa | thumb-homes | No |
| PO06 | ssa | thumb-homes | Yes |
| PO07 | ssa | thumb-calls | No |
| PO06+PO07 (new default) | ssa | thumb-calls | Yes |

These are explicit full profiles, not six single flags independently applied
to the default. Later selectors include their implemented predecessor
behavior. PO06 and PO07 have the common PO05 base; the last profile combines
them and receives its own fresh builds, execution checks and measurements.
No D04-D07 candidate is silently enabled.

## Fresh offline qualification

[`po09-finished-corpus/offline.json`](../build/po09-finished-corpus/offline.json) archives seven
fresh full-corpus builds and seven separately instrumented maze builds.
Each profile enumerates 37 C modules and twelve ordinary production images.
Frontend and optimized IR pass LLVM verification; bidirectional mixed
LLVM/owned ABI and actual-maze execution pass under both optimizer routes.
Raw-PHI, loop, ordinary memory, hidden-return/composite, recursive and
indirect call checks are retained in the existing execution oracles.

All 554 function frame/call records per profile are checked, for 3,878 records
across seven profiles. Calls, outgoing argument reservations and uncertainty
metadata must agree after generated-helper renaming. Frame/spill/image growth
is retained as a static tradeoff, not hidden by rejecting the evidence
capture. Data/BSS and actual physical flash/RAM/mailbox budgets are checked.
Persistent verification hashes inputs and logs, reparses frame metadata,
rechecks ELF metrics and script budgets, and recomputes tradeoffs.

| Profile | Ordinary maze text bytes | Instrumented maze text bytes | Largest local frame + saves |
| --- | ---: | ---: | ---: |
| Current control | 41,284 | 43,508 | 1,568 |
| PO03 | 37,068 | 39,136 | 1,568 |
| PO04 | 35,868 | 37,880 | 1,544 |
| PO05 | 35,460 | 37,472 | 1,536 |
| PO06 | 35,380 | 37,392 | 1,536 |
| PO07 | 34,884 | 36,800 | 1,536 |
| PO06+PO07 | 34,804 | 36,720 | 1,536 |

No profile grows a frame, module spill count or ordinary image relative to
the fresh current control. PO03 and PO04 shrink eleven images; the other
four candidates shrink twelve. Ordinary maze data/BSS remains 16/160,440
bytes; instrumented data/BSS remains 16/160,448 bytes. Combined maze text
is 15.70% smaller than the current control. The minimum checked main-RAM
headroom across the corpus is 189,448 bytes for every profile, with the Wi-Fi
mailbox excluded from that region. This is memory placement, not stack proof.

Local frame maxima are **not** a whole-program stack bound. Nested calls,
recursion, callbacks and interrupts remain explicit uncertainties, and main
RAM headroom does not certify the 4 KiB scratch stack.

The separately captured combination also passes the maintained PO08
assembler/linker controls:
[`po09-combination-stages/results.json`](../build/po09-combination-stages/results.json)
and
[`po09-combination-boundaries/results.json`](../build/po09-combination-boundaries/results.json).
All 39 exact assembly inputs and twelve images pass the 72-link audit.
Owned and reassembled loaded images remain identical across both linkers;
resolved relocation values, function layouts, merged payloads and physical
budgets are independently verified.

## Hardware method and decision

The hardware gate runs both suites:

- Reached real maze generation, scripted input/physics, RAM rendering,
  display transfer and reached frames, plus diagnostic/graphics work.
- The unrelated six-module/six-kernel timing suite, including arithmetic,
  strings, memory, control flow, cross-file ABI, graphics, branch/PHI and
  signed/unsigned division.

Both seeds (`3276377847`, `123456789`) use two complete repeats. Each campaign
swaps LLVM and owned physical roles on both badges, retains raw reports,
fresh nonces, source-suite identities, image/tool hashes and clock settings,
and checks every fingerprint, guard and work count. LLVM is the ordinary
whole-source Clang `-Oz` freestanding Cortex-M33 reference, not a fixed-IR
backend crossover. All seven profiles receive independent campaigns.

For each workload, seed, board and repeat, the predeclared uncertainty is
the sum of control raw spread, candidate raw spread, same-board repeated
control drift and paired LLVM drift. Exact integer-rational tick inequalities
decide improvement or regression; equality is unresolved. A workload is a
repeatable win only if all role/repeat observations pass on **both seeds**.
Any resolved regression in either suite blocks speed promotion. No aggregate
mean may hide a bad physical role or unrelated workload.

The decision also checks recorded static tradeoffs. Eligibility for speed
review requires a repeatable reached-application win, no resolved hardware
regression and no frame/spill/image growth. It is not automatic promotion:
physical panel, live IMU/network, cold-XIP and whole-program interrupt/stack
coverage remain separate gates. The maintained tool deliberately retains
explicit profiles rather than manufacturing approval for those unmeasured
paths.

[`po09-finished-hardware/results.json`](../build/po09-finished-hardware/results.json)
is the completed capture and recomputed decision artifact.
A failed or partial run cannot
pass verification. The verifier re-decodes raw reports, checks requests,
policies, independent campaign inventory and artifacts, and recomputes every
decision instead of trusting reported percentages.

### Completed outcomes: 2026-10-11

All **56 campaigns, 112 role-swapped rounds, 224 raw reports and 14,112 timing
samples** pass. Every checked output matches. Both suites cover both seeds and
both repeats for all seven profiles. Exact per-role/repeat noise decisions
and full-flash restorations pass independent `--verify-current`.

| Profile | Repeatable application wins | Repeatable unrelated wins | Resolved regressions | Decision |
| --- | ---: | ---: | ---: | --- |
| PO03 | 6/6 | 12/12 | 0 | Retain opt-in; eligible for speed review |
| PO04 | 6/6 | 12/12 | 0 | Retain opt-in; eligible for speed review |
| PO05 | 6/6 | 12/12 | 0 | Retain opt-in; eligible for speed review |
| PO06 | 6/6 | 12/12 | 0 | Retain opt-in; eligible for speed review |
| PO07 | 6/6 | 12/12 | 0 | Retain opt-in; eligible for speed review |
| PO06+PO07 | 6/6 | 12/12 | 0 | Retain opt-in; independently eligible for speed review |

All decisions compare each full profile with the **current default control**,
not with its immediate predecessor. No additional combination benefit is
inferred by adding component results, and no candidate-versus-candidate
performance ordering is certified by these decisions.

Descriptive elapsed-time reductions for the combined profile, geometric means
of eight paired medians against the current default:

| Reached workload | Reduction |
| --- | ---: |
| Maze generation | 26.81% |
| Scripted input/physics | 18.68% |
| RAM rendering | 26.47% |
| Display transfer | 7.76% |
| Reached frames | 10.95% |
| Diagnostics/graphics | 33.17% |

Its unrelated workload reductions range from 3.00% to 35.12%. Percentages are
descriptive; qualification uses the exact tick-based strict gate on every
board/repeat and both seeds. This is not an averaged whole-badge score, a
claim of beating LLVM, or electrical panel/live-sensor/network certification.

The final capture inherits five validated campaigns from
[`po09-qualified-hardware/results.json`](../build/po09-qualified-hardware/results.json),
which stopped on a picotool **flash timeout before execution**. Their raw
artifacts, original firmware and every compiler/firmware source/tool hash
are verified; only the host promotion runner and its tests differ, with the
capture-time runner archived. The remaining **51 campaigns complete without
any retry** in the final capture. Earlier failed report reads are not imported.

The new tested retry guard recognizes only an explicit flash-command
`spawnSync ETIMEDOUT`, in addition to the existing explicit collector transport
failures. It retains failed attempts, requires BOOTSEL recovery and reflashes
the complete image before fresh collection, with at most three attempts.
Bad firmware, failed builds, CRC/fingerprint mismatches and partial successful
reports cannot qualify through this guard. No deadline is relaxed.

The historical I/O errors and flash stall are not assigned an unproven root
cause. Focused exact-firmware isolation passed, no competing collector was
found, and both badges were on separate USB controllers. Full measured
coverage now passes without suppressing those historical failures. Final
24-case promotion/protocol/runner validation also passes; capture-time
sanitizer/ABI/actual-source qualification remains bound to the offline report.

PO09's outcome is **retain opt-in pending production-runtime coverage**, not
automatic promotion. Every candidate has a specific persisted decision.
The milestone is complete because qualification and the retain decision are
verified; the separate future promotion gates remain explicitly unresolved.

## Badge preservation

Before any firmware writes, both complete original 16 MiB flash images are
saved and hashed. The runner restores originals even after a capture failure,
rereads each full flash image, checks its SHA-256 and leaves the badges in
BOOTSEL. Missing, incomplete or mismatched restorations block qualification.
Only the two explicitly selected badge identities are used.

The initial full hardware attempt remains
[`po09-hardware/results.json`](../build/po09-hardware/results.json), marked
failed after 29 completed campaigns. It is not accepted qualification.
Both full originals were restored and independently hash-verified after that
failure. One unrelated-suite collector saved both raw reports, then a
successful BOOTSEL request disconnected a device before interface release,
which returned `LIBUSB_ERROR_NO_DEVICE`. This was an explicit USB teardown
failure, not mismatched candidate results.

The collector now logs that specific release/disconnect race only after a
successful BOOTSEL request, and still requires observed removal before context
teardown. Other release errors, failed BOOTSEL and missing detach remain
failures. Nine sanitizer-backed lifecycle scenarios cover the race and its
negative cases. A completely fresh offline capture and hardware sequence
follow the correction; no failed campaign is retroactively accepted.

### Historical report-read blocker (superseded by completed capture)

This subsection preserves the blocked state at the time of those attempts;
the completed inventory and decisions above supersede its missing-coverage
status. None of the failed reports becomes accepted qualification evidence.

The second hardware attempt,
[`po09-final-hardware/results.json`](../build/po09-final-hardware/results.json),
failed after twenty completed application campaigns on an owned summary read
followed by a failed BOOTSEL transfer. Both originals were again restored.
The runner then added a bounded three-attempt retry only for explicitly
identified collector transport errors. CRC/fingerprint failures, incorrect
results, build failures, deadlines and incomplete successful reports are not
retry-success fallbacks.

The continuation preserves the twenty completed campaigns from that attempt
only after re-decoding their raw artifacts and verifying their exact compiler,
firmware, test and source provenance. The only allowed capture revision is
the host promotion runner and its own tests, archived separately. Fresh full
offline qualification passes with those changes. The initial 29-campaign
attempt is not imported: its collector source differs.

The continued capture reaches **22 complete application campaigns, 44 swapped
rounds, 88 raw reports and 3,696 timing samples**. It then fails application
seed `123456789`, repeat 1, PO03 on all three attempts: the LLVM 1,024-byte
report is saved, but the owned summary read returns `-1`. Cleanup succeeds
and the runner restores both full originals. This repeatability is unresolved;
it must not be labeled harmless noise, accepted timings or a compiler win.

All attempts and recovery logs remain in that failed capture. At that point six
application campaigns and all 28 unrelated-suite campaigns are still missing
from this qualification inventory. Therefore no cross-suite candidate
decision is published. Defaults remain unchanged. The next required step is
to isolate this report-read failure on the exact seed/profile/physical role,
then finish the missing inventory and persistent verifier before promotion
review. Both current original/readback pairs are 16 MiB and independently
SHA-256 verified; badges are left in BOOTSEL.

### Focused isolation follow-up: 2026-10-11

The previously failing firmware was replayed **without recompilation**, using
the exact archived LLVM and PO03 UF2 images from `run.ptcFx1`. The retained
failure seed `123456789` and nonce
`15cd5b0701773717fec502f77c50c1ab` were replayed deliberately, with both physical
board assignments. Repeated nonces here are diagnostic reproduction, not
accepted qualification campaigns.

The independently re-decoded
[`isolation summary`](../build/po09-isolation-summary.json) records:

| Experiment | Runs | Result |
| --- | ---: | --- |
| Archived firmware, fresh nonces, both seeds and board assignments | 6 | All outputs match |
| Exact failure nonce and archived firmware, original unmodified collector | 4 | All outputs match |
| Exact failure nonce and archived firmware, sixteen alternating board assignments | 16 | All outputs match |

These are **26 unmodified-firmware runs, 52 complete raw reports and 2,184
timing samples**. They are not 26 complete two-round qualification campaigns.
The original collector passes as well as the phase-logging collector, so
added diagnostic logging is not required to make this configuration succeed.
Both badges can execute owned PO03 for the retained seed. The original
failure is therefore **not established as a deterministic seed/profile/board
compiler bug**. Its USB I/O root cause remains unresolved; replay successes
do not retroactively qualify the failed full inventory.

The host collector now logs selected badge identity, submission and
summary-read phase, poll count and symbolic libusb error name. This improves
the next failing trace without retrying or accepting a short/error response.
The nine native collector lifecycle scenarios and ten reused qualification/
protocol tests pass, including the runner mock that previously timed out.
Those earlier timeout logs remain retained; their root cause is not claimed
fixed.

Four additional diagnostic-firmware runs with mailbox progress writes also
match, but post-BOOTSEL RAM reads are all zero and provide **no usable firmware
phase evidence**. No conclusion about a stuck module, watchdog expiry or
memory corruption is drawn from those reads. Diagnostic images/sources are
archived, and the firmware instrumentation is removed: the actual badge and
application sources again match their pre-investigation hashes.

Every replay batch saves and restores both original 16 MiB images, with
independent full-flash readback hashes. Both originals still match the
previously recorded hashes and both badges remain in BOOTSEL. Compiler
defaults and lowering are unchanged. Remaining work is a fresh full
qualification with the richer failure trace, or live progress diagnostics
if another read failure occurs; it is not safe to call this blocker repaired.

## Validation and reproduction

The maintained promotion target covers profile identity, two-suite/two-seed
decision completeness, one-bad-role rejection, exact one-tick ties, static
tradeoffs, failed artifacts and restoration failures. Existing targeted
sanitizer-backed compare/branch, homes, SSA, memory and call-transport tests,
collector/protocol checks and boundary tests run during offline qualification.
The archived validation log contains 64 passing test cases across the invoked
targets (including repeated reused checks), plus the sanitizer-backed native
collector lifecycle suite. Seven profiles each pass LLVM and owned optimizer
execution routes and the actual-maze oracle.
Correctness fixtures remain semantic guards, not performance evidence or
compiler-recognized cases.

After publication, two additional unchanged promotion-target reruns fail in
the reused compiler-runner mock with `spawnSync ETIMEDOUT`; their logs are
`po09-final-validation.log` and `po09-final-validation-2.log`. The archived
offline validation passed all invoked targets, and current offline artifact
verification still passes, but these later failures are retained rather than
claimed as passes. The incomplete hardware report is explicitly rejected by
the persistent verifier. No deadline is extended to conceal either blocker.

From the workspace root, with built compiler tools, LLVM/LLD, QEMU and the
existing USB collector/picotool available:

```sh
PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
  node badge-compiler/tools/pipeline-promotion.mjs --offline \
  badge-compiler/build/po09-new-corpus

PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH \
  node badge-compiler/tools/pipeline-promotion.mjs --measure \
  badge-compiler/build/po09-new-hardware \
  badge-compiler/build/po09-new-corpus/offline.json \
  9BBF88DD17D66E7D 08DA26F86E57F5EA

node badge-compiler/tools/pipeline-promotion.mjs --verify-current \
  badge-compiler/build/po09-new-hardware/results.json

make -C badge-compiler test-pipeline-promotion
```

Capture directories must be new; failed evidence is never overwritten.
`--verify` checks persisted artifacts and decisions without claiming that the
current workspace recompiles the measured firmware. `--verify-current`
additionally checks capture-time source/tool/test hashes against the workspace.
Changing a compiler policy, component combination or measured source requires
a fresh qualification, not reusing earlier component timings.

For a continuation, `--continue NEW_DIRECTORY OFFLINE.json FAILED_REPORT.json
CAPTURE_RUNNER.mjs SERIAL SERIAL` archives the failed report and its original
runner, verifies restored originals and all reusable completed campaigns, and
collects only missing identities. It never overwrites the failed report.
Failed attempts remain explicitly logged; three transport failures stop the
capture and trigger restoration rather than qualifying partial evidence.
