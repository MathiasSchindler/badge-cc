# Badge documentation

The project separates freestanding badge apps, the project-owned Wi-Fi host,
source-radio research and optional SDK apps. Do not transfer a capability or
dependency claim from one to another.

| Track | Start here | Boundary |
| --- | --- | --- |
| Freestanding badge apps and diagnostics | [Hardware specification](spec.md), [diagnostic app](diagnostics.md), [USB workflow](usb-live.md) | Original RP2350 C, no target SDK/libc |
| Project-owned Wi-Fi host | [Wi-Fi results and roadmap](wifi.md), [radio assets](wifi-assets.md) | Freestanding gSPI/SDPCM/CDC/BDC + ARP/IPv4/UDP; vendor CYW43439 firmware; 600/600 baseline echoes |
| Source-owned CYW43439 research | **[Current engineer/LLM handoff](cyw43/handoff.md)**, [technical guide](cyw43/README.md) | Source cold initialization/calibration and PSM components exist; checked vendor-retained prefixes, not source-only fresh frames |
| Optional SDK radio apps | [Wireless Lab](wireless-lab.md), [handover lab](wifi-handover.md) | Separate Pico SDK/lwIP/BTstack dependency-bearing targets, as applicable |

## Handing work to another engineer or LLM

For the current source-radio work, start with
**[docs/cyw43/handoff.md](cyw43/handoff.md)**. It records the 2026-09-22
checkpoint, device roles and last state, evidence locations, offline commands,
physical-test prerequisites, unresolved questions and recommended next task.
Use it instead of reconstructing status from old conversation or logs.

Vendor-retained source PSM format4 commits checked six-byte CCK prefixes.
Three guarded captures coexist with all six cold candidate-inhibit readbacks;
upstream FIFO contents remain unexcluded, so this is not proof of fresh
on-air reception under inhibition, a full frame, RF silence or safe-RX
qualification. Cold-source no-event and source-only fresh-frame reception
remain unresolved.

The owner explicitly deferred RF measurement under
`--experimental-rf-unverified`; it is not a blocker to authorized bounded
experiments. The [current workflow](cyw43/workflow.md) owns the fixed
three-case catalogue, batch limits and independent frozen recovery policy.
Do not interpret the opt-in as RF qualification.

The [R3 evidence ledger](../experimental/cyw43/r3/references.json) owns exact
run identities/hashes. Source files alone may omit generated raw evidence
and frozen recovery artifacts; the handoff explains what must accompany a
transfer. No documentation page is blanket authorization to flash.

## Other badge work

- [RP2350 runtime and qualification](rp2350-runtime.md): clocks, memory,
  report persistence, recovery and the staged multicore qualification gate.
- [Buttons](button-mapper.md), [touch/peripheral probing](peripheral-probe.md),
  [IMU demo](imu-demo.md) and [GPIO observation](gpio-monitor.md).
- [Tilt Maze](tilt-maze.md) is a freestanding calibrated marble-maze game.
- [Red square](red-square.md) compares minimal cold-bootable LLVM/owned images
  and accounts for every loaded byte.
- [Tilt Breakout](tilt-breakout.md), [Wireless Lab](wireless-lab.md),
  [BLE tilt Pong](ble-tilt-pong.md) and [BLE-to-Wi-Fi handover](wifi-handover.md).
  The latter three are explicitly SDK-based radio apps, not source-radio proofs.
- [Background](background.md) and [bring-up plan](plan.md) retain earlier
  decisions. [The original controller plan](cyw43-driver.md) has a current
  status bridge followed by the historical proposal, not a current task list.

Keep schematic facts, software/model results, physical measurements and
unqualified hypotheses separate. The [specification](spec.md) owns board
wiring; topic guides and dated evidence own subsequent experiment results.

Historical isolation manifests include documentation hashes. This
documentation refresh is not a new build or hardware qualification and does
not refresh those baselines. Preserve historical hashes/evidence and consult
the current workflow's isolation checks before further experiments.
