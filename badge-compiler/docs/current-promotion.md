# PO18: cached qualification and default promotion

PO18 promotes the independently measured **combined PO11-PO17 policy**.
The root firmware Makefile still defaults to LLVM; this changes the owned
compiler's shared API/CLI defaults and drivers that leave policies unspecified.

## Declared policies

| Policy | Previous owned default | Promoted owned default |
| --- | --- | --- |
| Frontend expression lowering | `ssa` | `ssa` |
| Optimizer | `mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory` | Previous pipeline plus `loop-scalar,cross-function` |
| Backend | `direct/global-compact/thumb-calls/word` | `direct/global-compact/thumb-widths/word` |

`thumb-widths` inherits effects, finalized frames, control and operand
selection. It does not enable the rejected compact flag-setting arithmetic,
local scheduling, arbitrary outlining or a new allocator. The loop/helper
passes retain their bounded PO14 final-byte/frame admission contracts.
Older policies remain explicitly selectable.

The standalone optimizer's default is now target-costed, not a promise to
optimize arbitrary LLVM IR independently of the badge backend. In particular,
forced `alwaysinline` and `musttail` contracts diagnose explicitly. The owned
C frontend already excludes those contracts. A directly related regression
test checks the diagnostic and output preservation, and verifies that the
previous explicit six-pass pipeline remains available for standalone IR.
Backend/work-budget failures are errors, not silent profitability fallbacks.

## Physical evidence

The [capture runner](../tools/current-promotion.mjs) declares the current
control and full candidate before capture. It runs two suites, two runtime
seeds, two repetitions and both physical role assignments per campaign:
**16 campaigns, 32 rounds and 64 raw reports**. No historical profile matrix
is repeated. All raw semantic results, USB records, clocks/XIP settings,
nonces and board assignments match; there are no transport retries or
resolved regressions.

The exact integer-rational gate includes sample spreads, same-board repeated
control drift and paired LLVM drift. A regression in any observation rejects
speed promotion; averages cannot conceal it. Every listed workload improves
outside that gate for both seeds, repetitions and physical roles.

| Reached application workload | Elapsed-time reduction versus previous owned default |
| --- | ---: |
| Maze generation | 27.85-27.87% |
| Physics and scripted input | 26.90-27.49% |
| RAM rendering | 13.54-13.79% |
| Display transfer | 44.26-44.27% |
| Reached maze frame | 39.59-41.07% |
| Production diagnostics/graphics | 15.29-15.45% |

The general timing suite also qualifies all twelve workloads. Reductions
range from 4.95% for a branch/PHI observation to 47.60% for a cross-file-call
observation; signed/unsigned division, ordinary memory, arithmetic, strings,
control flow, graphics and ABI paths all improve. These are **combined**
benefits against the old owned default, not additive per-pass percentages
or a claim that the owned compiler universally beats LLVM.

Measurements are warm-workload timer ticks, not controlled cold-XIP timings
or arbitrary interrupt/peripheral certification. Scripted input is not a new
physical IMU test. Existing mixed-ABI and resource evidence is reused.

## Delay and I/O qualification

The shared display's unserviced initialization previously relied on compiler-
sensitive loop counts. Both entry points now use the existing **150,000-tick
ROM-inherited running timer** after reset and sleep-out. Native sanitizer
tests verify the exact minimum, unsigned wraparound and callback cancellation.
This firmware mitigation is separate from the compiler-speed comparison:
both timed profiles use the same driver and exclude initialization delays.

The owner confirmed the complete screen-test pattern, then removed **all**
power and confirmed cold-boot drawing with both profiles on both badges,
including the physical-role swap. USB reports alone were not treated as
optical proof. These checks reach unserviced timer initialization and the
actual LCD command/pixel strobes.

The freestanding LCD retains three explicit NOPs between WR low/high and two
after high. At the supported maximum 150 MHz the three low-period NOPs alone
take at least 20 ns; the high period also includes the following instructions
and stores. No compiler transformation may remove these volatile assembly
effects or reorder volatile GPIO transactions. This is functional validation
at the captured clock settings, **not oscilloscope certification of every
panel waveform or permission to overclock**. I2C and radio wire deadlines
already use timers or hardware PIO; USB ownership keeps its explicit twelve
NOPs and memory clobber. Live USB initialization/reporting/teardown is reached
throughout the campaign.

The compact red-square startup does not establish its own timer contract.
Instead its separate reset/sleep-out primitive now retains six explicit
NOPs per iteration: even the shorter four-million-iteration wait supplies
24 million cycles, at least **160 ms at 150 MHz**, irrespective of loop
optimization. Both LLVM and owned production assembly retain that sequence;
the native/target oracle still verifies exact drawing and GPIO operations.
This changes demo safety, not the measured compiler workloads.

## Reuse, production sizes and resources

The optional [image cache](../tools/compiler-image-cache.mjs) keys source,
headers, assembly, scripts, linker scripts, build settings and tool binary
hashes. Runtime seeds and board roles do not affect image identity. Missing,
extra or corrupt outputs cause explicit errors; there is no stale-image
fallback. The experiment builds **six immutable images once**, reusing them
on **26 subsequent configuration requests**.

PO17's 37-module/560-function corpus is reused. All 37 new default backend
emissions equal the qualified explicit assembly, except that the changed
display source is compared with its separately built, cold-boot-tested
candidate. The default optimizer output for that source equals its explicit
eight-pass output. No production object is reassembled during finalization:
36 unchanged objects and the already built display object are reused.
Only eleven affected images are relinked; idle is reused unchanged.
The emission/equality/relink commands total approximately 0.41 seconds,
excluding the earlier image builds and physical flash preservation.

| Production image | PO17 candidate loaded bytes | Promoted, timer-safe loaded bytes |
| --- | ---: | ---: |
| button mapper | 9,220 | 9,176 |
| diagnostics | 41,580 | 41,536 |
| GPIO events | 7,672 | 7,628 |
| GPIO monitor | 6,064 | 6,020 |
| idle | 388 | 388 |
| IMU demo | 22,484 | 22,440 |
| lower mapper | 9,424 | 9,380 |
| peripheral probe | 11,292 | 11,248 |
| screen test | 4,212 | 4,168 |
| tilt breakout | 15,344 | 15,300 |
| tilt maze | 23,824 | 23,780 |
| USB/Wi-Fi/UDP | 326,236 | 326,192 |

Loaded ELF/raw bytes and physical linker budgets pass; data/BSS do not grow.
Final frame/save/outgoing reservations do not grow for surviving display
functions, and unchanged objects retain the existing allocation/call evidence.
This is not an invented whole-program stack bound: indirect calls,
interrupts and recursion retain their recorded uncertainties.

The separately mitigated compact red-square now measures **992 owned bytes
versus 568 LLVM bytes**, reusing identical startup/boot objects. Its delay
routine grows from 30 to 40 owned bytes and from 10 to 20 LLVM bytes;
alignment absorbs two of the ten added bytes in each loaded image.
No new red-square hardware-size record is claimed.

## Persistent verification and restoration

- [Final default/resource/provenance report](../build/po18-qualified-defaults/results.json).
- [Raw matched hardware capture](../build/po18-current-promotion/results.json).
- [Owner-confirmed cold-boot observations](../build/po18-optical/results.json).
- [Persistent verifier](../tools/current-promotion-evidence.mjs).

```sh
node badge-compiler/tools/current-promotion-evidence.mjs \
  --verify badge-compiler/build/po18-qualified-defaults/results.json
make -C badge-compiler test-current-promotion
```

Ten focused cache/policy/protocol/mock-runner checks passed before flashing.
Default wiring subsequently passed sanitizer optimizer/backend APIs and
40 focused Node selectors, including metrics/API/driver equality and the new
timing, standalone-contract and provenance checks. Historical exhaustive
encoding/ABI matrices were not rerun after host-only/documentation edits.

Both hardware sessions preserved complete 16 MiB originals. Every restoration
was independently read back and SHA256-verified, and both badges were left
in BOOTSEL. The original hashes are
`8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d`
and `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff`.
