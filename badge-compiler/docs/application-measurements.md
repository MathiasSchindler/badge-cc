# PO01: reached application measurements

For the subsequent compiler change, see
[PO03's repeated baseline/candidate measurements](compare-branch.md#real-badge-qualification):
the same production-source workloads run with opt-in compare/branch fusion.
The figures below remain the historical PO01 baseline, not candidate results.

PO01 measures actual production maze and shared diagnostic/graphics code on
both attached RP2350 badges. It does not implement a compiler optimization or
claim that LLVM parity is a ceiling. See the
[pipeline plan and reproduction](pipeline-optimization.md#po01-implementation-and-reproduction).

## Qualified evidence

- [Complete capture, source hashes and full-flash restoration](../../build/application-benchmark-runs/run.ymQNpm/results.json).
- [Dense-level campaign](../../build/compiler-compare-runs/run.HHClk2/results.json),
  seed `3276377847`, starting level 4.
- [Intermediate-level campaign](../../build/compiler-compare-runs/run.OTiChB/results.json),
  seed `123456789`, starting level 2.
- [Application host/protocol/target tests](../build/po01-tests.log).
- [Legacy collector/protocol/qualification and maze target tests](../build/po01-legacy-tests.log).
- [Rejected initial campaign and verified restoration](../../build/application-benchmark-runs/run.P6rOuw/results.json).

The control is Homebrew Clang **22.1.8 `-Oz`**, freestanding Cortex-M33 Thumb,
not `-O3`. The owned compiler uses unchanged
`direct/global-compact/thumb-layout/word` and C06
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`.
The same source build identity, inputs and report format are used for both.
All builds are dependency-free firmware; no standard C library is linked.

Two distinct seeds, two swapped physical roles per seed, seven warm samples per
workload/badge/round produce **four rounds, eight reports, 336 timing samples**.
All CRC/build/nonce/order/count checks pass. Every application fingerprint,
guard lane, encounter count and goal count matches. Both input/physics and
frame workloads record a hazard respawn and a goal encounter for both seeds.
All 14 clock/configuration registers match across all reports and remain
unchanged within each report. Raw timer read overhead is 0-5 ticks; accepted
samples exceed 100 times their report's maximum overhead.

## Real-badge results

Each ratio is the geometric mean of four paired median owned/LLVM elapsed
ratios for that workload. Greater than 1 means owned output is slower.
Ranges describe the four observations, not statistical confidence intervals.
No cross-workload score is computed.

| Reached work | Calls/steps per timed batch | Owned/LLVM | Four-observation range |
| --- | ---: | ---: | ---: |
| Maze generation, cycling levels 1-4 | 16 | 9.644x | 9.644-9.644x |
| Maze sensor processing, physics, collision, hazards | 64 | 10.599x | 10.350-10.853x |
| Full maze board/header/overlay RAM rendering | 4 | 4.252x | 4.211-4.293x |
| Actual panel transfer, full/HUD/clipped/dirty regions | 4 scripted operations | 5.727x | 5.727-5.727x |
| Reached frame policy, input/physics and actual panel writes | 16 | 5.802x | 5.664-5.943x |
| Actual diagnostic formatting/buttons/sqrt and clipped graphics | 256 | 4.629x | 4.625-4.634x |

For example, dense-level physics medians are LLVM **26,488-26,490 ticks** and
owned **274,183-274,197 ticks** for 64 steps. Intermediate-level physics is
LLVM **9,686 ticks** and owned **105,111-105,123 ticks**. This is measured
runtime, not an inference from object sizes. Samples are **timer0 raw ticks**;
no external clock calibration justifies calling them microseconds.

The maximum within-report `(max-min)/median` is approximately **0.112%**.
Both physical role observations reproduce each seeded workload's ratio;
different levels legitimately change the workload and ratios. This establishes
repeatable reached-work timing, not a candidate promotion/noise threshold or
universal application performance.

Instrumented benchmark firmware linked text is **13,468 bytes LLVM** versus
**70,812 bytes owned**; both have 8 bytes data and 167,800 bytes BSS.
These include the harness/transport, not ordinary maze image sizes.

## What actually runs, and what timing includes

The [application harness](../../src/tools/compiler_test/application.c) includes
the production maze directly. Shared frame rendering is called by both
[normal game](../../src/tools/tilt_maze.c) and harness. No compiler recognizes
function names, paths, seeds or expected answers. Native and target tests are
semantic guards, not substitute speed evidence.

- **Setup outside intervals:** reset state, generate the initial maze, establish
  guards, draw the initial RAM board and capture last-drawn state.
- **Validation outside intervals:** full canvas and active maze/wall/state
  fingerprints, canvas canaries, logical cell/wall bounds, repeat-result checks.
- **USB and watchdog service outside intervals.** Display initialization is
  outside measurements; actual region commands, overlay pixel lookup and GPIO
  writes are inside transfer/frame measurements, using normal driver code.
- **Generation:** includes loop control and level selection around real DFS,
  goal-route, hazard and wall generation.
- **Physics:** includes reproducible sample preparation and scripted state
  placement around real input filtering, collision, hazards and time update.
  Scripted placement exercises goal, hazard and wall-overlap cases, not a
  physical player's reachable trajectory guarantee.
- **RAM drawing:** calls normal `draw_board` in each of the four application
  phases, including ready/calibrating overlays and header text.
- **Transfer:** full 320x240 frame, 320x14 HUD, clipped negative-origin region,
  separated dirty marble rectangles. This is not isolated GPIO instruction
  throughput; it includes the actual pixel lookup and region driver path.
- **Frames:** call the production redraw policy after scripted input/physics;
  force initial full redraw, subsequent goal/hazard/header changes and dirty
  regions. No benchmark timing overlay is drawn.
- **Unrelated helpers:** actual `diag_buttons_update`, `diag_sqrt`,
  `diag_decimal`, `diag_hex`, rectangle/text functions, with clipped geometry.
  A small digest update remains included in this combined helper workload.

The native ASan/UBSan test covers five seeds (including zero and UINT32_MAX),
deterministic repeated fingerprints and panel-stream hashes, valid region
bounds/counts, full/HUD/small paths, hazards/goals, damaged-canary rejection and
under-resolved timing rejection. The same source runs under both Cortex-M33
compiler paths in QEMU. Protocol tests cover corruption, stale nonces, wrong
builds/versions/counts, clocks, output differences and restoration failures.
The existing maze fingerprint/calibration/progression and collector suites
also pass. These tests preserve intended production behavior; they are not
proof for every C program.

## Explicit remaining coverage gates

| Surface | Evidence here | Not certified |
| --- | --- | --- |
| Sensor processing | Real production filtering and scripted accel samples | Physical I2C/IMU acquisition, actual tilt/motion or gyro integration |
| Display | Actual hardware driver and pixel writes run on both badges; stream semantics checked natively | Visual panel correctness, electrical waveform/setup/hold measurements |
| End-to-end frame | Production redraw decisions plus input/physics/time and panel paths | Real input acquisition, live button polling, backlog/pacing, complete interactive game latency |
| XIP | Warm-working-state batches; identical captured clock settings | Controlled cold-cache, startup or external oscillator calibration |
| Memory | Canvas canaries, logical bounds and native sanitizers | Whole-program stack high-water/cold-start proof; benchmark uses the existing isolated 64 KiB stack linker |

The initial 256-step dense physics batch was rejected by the one-million-tick
sample limit. No results from that capture are accepted; its full-flash
restoration is verified. The qualified harness uses 64 steps and retains
partial timing pages on future failures, rather than relaxing the bound or
calling an overlong sample successful.

## Flash preservation

Both complete 16 MiB images were freshly saved before writes, restored in
the preservation wrapper's `finally` path and independently reread:

| Serial | Original and restored SHA-256 |
| --- | --- |
| `9BBF88DD17D66E7D` | `733f21939a05380fef7d59e972b0eb7441a94e54a7f3f02175c5f4ff6d48b0ff` |
| `08DA26F86E57F5EA` | `8cd13a7197061455855ce6ac626c8bf021fd9f7536e086d2ebc544da62976e4d` |

Both are left in BOOTSEL. Backup/readback files remain with the capture.
No compiler defaults or opt-in candidates are promoted by PO01.
