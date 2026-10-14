# Boat: accelerometer-driven sloshing demo

## Goal and physical interpretation

Entry point: [boat.c](../../src/tools/boat.c). A side-on sea-like tank fills
the lower screen, with a small floating boat. Gravity in screen coordinates
changes as the upright badge rotates left/right. Water redistributes through
flow, retains momentum, reflects at hidden walls and gradually settles.
The boat responds through buoyancy, inertia, drag and pitching, not by being
attached to a drawn wave.

The first model is a bounded shallow-water approximation, not a full
Navier-Stokes solver. Moderate in-plane rotation is supported; breaking waves,
spray and overturning are out of scope for that model. The separate B5
particle experiment below removes the single-height surface restriction.
Upside-down poses and arbitrary 3D motion remain out of scope.
Pixel units and tuned damping provide a demonstration, not calibrated
hydrodynamic measurements. Water affects the hull, but hull displacement and
reaction forces on water are deferred.

Firmware remains freestanding/no-libc C on one Cortex-M33, without SDK, heap,
floating-point runtime, PSRAM or external target dependencies. Reuse the
existing LSM6DS3, I2C, display, timer and framebuffer presenter. Preserve the
running nametag and do not flash either badge without a separate deployment
decision.

## Milestones

Current status: **B0-B2 initial implementation and host/ARM checks pass**;
basic output and acceleration signs are owner-confirmed. **B3 input fusion
and sensor replay tests pass on the host; corrected calibration and smooth
response without flicker are owner-confirmed**. Quantitative holding-range
and physical gyro-sign qualification remain open. **B4's scripted ten-minute
hardware invariant/cadence gate now passes**, including p99 frame work <=25 ms;
convergence and physically driven long-run qualification remain open.

### B0 - Model and platform contract

- Define upright screen coordinates, gravity sign and supported moderate tilt.
- Reuse measured crystal/TIMER0 setup, sensor axis mapping and bounded recovery.
- Establish 10 ms physics ticks, bounded catch-up and separate display cadence.
- Record the approximation and memory budget instead of claiming full fluids.

**Verify:** no-libc ARM linkage; host arithmetic/invalid-input tests; physically
check upright gravity and left/right signs before accepting on-device behavior.

### B1 - Conservative water and host tests

- 80 four-pixel columns with Q8 water depth and Q16 interface displacement/flow.
- Closed boundaries, gravity-driven flow and surface-pressure restoring force.
- Exchange exactly equal volume across each shared interface, with donor and
  capacity bounds. Damping removes velocity, never deletes water.
- Fixed-step deterministic reference tests for rest, tilt, reversal, waves,
  volume conservation, dry/full bounds and long runs.

**Verify:** exact total depth through every tested step; finite bounded states;
tilted equilibrium trends and residual motion after returning upright.
Check numerical damping and convergence before describing realism as qualified.

### B2 - Buoyant hull and first firmware

- Two hull support samples generate buoyancy and torque; vertical/horizontal
  inertia, fluid drag and wall collisions remain separate from surface height.
- Draw water, highlights and a small hull/mast in a side view.
- Read acceleration at sensor cadence; use a short filter, reject unsupported
  pose with a visible prompt and freeze rather than invent sensor values.
- Errors explicitly stop the demo with an error screen and BOOTSEL recovery.

**Verify:** displaced hull returns toward floating equilibrium, unequal support
causes pitching, render clipping, sensor failure paths and ARM SRAM/stack budget.

### B3 - Orientation estimation and interactive qualification

- Add gyro bias calibration while stationary, with a visible startup prompt.
- Fuse three-axis rotation with acceleration-derived gravity;
  distinguish rotation from transient translation, gate implausible samples.
- Verify gyro sign physically; do not assume it from an acceleration mapping.
- Add reset/recenter and optional HOME sleep using the nametag's qualified path.
- Build a host preview/replay for identical synthetic sensor trajectories.

**Verify:** slow/fast turns, holds, reversals, shake rejection, return to neutral
and repeated pauses. Test on one serial-bound badge with preserved original
backup and physical BOOTSEL recovery; do not replace the other running tool.

### B4 - Smoothness, numerical and physical qualification

- Measure sensor, physics, rendering and presentation separately with the
  qualified timebase. Target 30 presented frames/s; do not assume it from timer
  deadlines. Retain measured skips and cap simulation catch-up.
- Test both turn directions, settling, water conservation and stable hull
  response over ten minutes, including stalled rendering and timer wrap.
- Compare smaller physics steps/spatial grids to expose numerical artifacts.
- Consider nonlinear advection, improved buoyancy, two-way boat coupling and
  cost-aware display updates only after the simple control is verified.

**Exit:** useful believable moderate-tilt demo, recorded limits and measured
cadence; no claim of battery life, tear-free output or full-power startup
without separate evidence.

### B5 - Two-dimensional breaking-water experiment

Owner chose **actual breaking/curling waves**, not just a foam overlay.
Preserve the accepted `boat` firmware; build [boat-breaking.c](../../src/tools/boat-breaking.c)
as an explicit experimental alternative.

- Fixed equal-mass particles move freely in both screen axes.
- Spatial bins bound neighbor searches to the smoothing-radius vicinity.
- Predict motion, relax density/near-density pressure, apply symmetric
  viscosity and contain particles at tank walls.
- Render a two-dimensional density field, leaving air beneath overhanging
  water instead of filling every column below its highest particle.
- Add shaded sky/water, advected flow tracers and impact/surface-motion foam.
- Reuse gyro calibration, pose gates, HOME reset and visible recovery errors.

**Verify:** attached overhangs form dynamically after releasing a dam; isolated
spray does not satisfy the attached-overhang gate; every particle stays bounded
through ten simulated minutes. Fixed particle mass is not a claim of exact
geometric volume. Test complete reconstructed panels and the classic boat
regressions; no-libc ARM linkage and SRAM/stack checks.

### B6 - Breaking-water hardware and quality gates

- Measure real particle-step, field-render and presentation costs on one
  serial-bound badge, retaining the accepted boat image for rollback.
- Record dropped physics ticks and sample gaps instead of claiming the
  old model's 30 fps qualification applies to the new one.
- Check slow/fast tilt and reversals, foam at wall impacts, visible circulation,
  calibration and HOME reset. Restore the accepted image if not useful.
- Compare particle spacing, relaxation counts and timestep, measuring density
  variation and visual occupancy. Assess the numerical speed/displacement
  limiters and artificial surface tension.
- Add proper two-way hull displacement/reaction only after the fluid is
  stable and affordable; current immersed-density buoyancy is one-way.

**Exit:** useful measured interactive breaking-water demo, with documented
compressibility/performance limits. Host CPU timing is not RP2350 evidence.

## Initial implementation

The initial B0-B2 prototype used acceleration-only input. The current image
adds the B3 input update described below. Build `make boat`, test `make test-boat`.
The UF2 is `build/universe26/uf2/boat.uf2`. The first prototype was subsequently
warm-launched on the owner-authorized attached badge; see the deployment record.
Startup asks for stationary holding to calibrate gyro bias. Unsupported
poses and unreliable motion pause physics with separate visible prompts.
HOME resets the water and boat; RESET restarts the firmware. HOME is not a
sleep control in this prototype. The internal sensor bus explicitly uses
GPIO18/19, not the bit-bang helper's GPIO4/5 default.

The water update uses a damped, variable-depth linear pressure/flow model,
with bounded interface travel per tick. Sequential conservative flux limiting
can add directional numerical bias; B4 must assess convergence, especially near
dry columns. Hull support is a spring-like immersed-depth approximation to
Archimedes buoyancy, with rotational inertia and damping. The prototype is
not yet physically qualified.

### Verification record

`make boat test-boat` builds the no-libc ARM image and runs four host suites
with address/undefined-behavior sanitizers:

- equilibrium at rest; exact volume after every step; both small tilt signs;
  large tilt/reversal, residual flow after releasing tilt and settling;
- displaced-hull restoring behavior and immediate unequal-support pitching;
- a ten-minute simulated run and alternating dry/full columns;
- explicit invalid input/state rejection without partial mutation;
- mocked full application: fixed-step progress, exact reconstructed panel,
  initialization errors, mid-run IMU read failure, unsupported/extreme poses,
  and clipping at extreme water/hull positions;
- explicit internal I2C pin assertions and display-on after the first frame.
- sensor bias/movement calibration, partially upright poses, exact pose-gate
  hysteresis, opposite-vector recovery, timed rotations on all three axes,
  slow/fast synthetic replays, irregular sample intervals and timestamp wrap;
- calibration/pose/unreliable HUD states, HOME reset without recalibrating,
  simulated synchronous display costs and a 250 ms foreground stall.
- a full 60,000-step scripted diagnostic with reconstructed panel output,
  timing-counter/histogram checks and explicit volume/USB invalidation;
- 16 host report tests for schema, counters, bounds, timer/SOF agreement,
  complete histogram counts and exact nearest-rank 25 ms p99 decisions.

The flow model bounds interface travel to half a pixel per step. Q16 momentum
avoids rounding small gravity inputs away. Clamped transfer preserves water
exactly even at dry/full limits, but numerical damping and limiter artifacts
remain part of the approximation.

Basic visible output and tilt direction are owner-confirmed on one badge.
Orientation range, animation cadence and watchdog recovery are **not yet
qualified for this tool**. Fusion reduces transient translation influence,
but cannot completely separate gravity from translation. No ten-minute hardware or
small-step convergence gate is passed by the host long-run test.

Initial B0-B2 ARM image: **9,528 flash bytes** (9,520 text/rodata plus 8 initialized
data), **312,892 BSS bytes**, with no unresolved compiler/runtime helpers.
Most SRAM is the current and last-presented 153,600-byte framebuffers; physics
state is 668 bytes. Compiler-reported frames are 56 bytes for the entry loop,
36 for a physics step and 16 for the display callback; these are individual
frames, not stack high-water measurements. Build outputs stay optional and
separate from the default firmware and continuous nametag.

### First on-device launch, 2026-10-12

The owner authorized flashing the one attached BOOTSEL badge. Read-only
inventory identified **`08DA26F86E57F5EA`**, A4/QFN80 with 16 MiB flash.
A fresh full original backup matched SHA-256
`bd08ade342890e595007af2f74adbf765f785653539d587b873576998fb4fba6`.
Firmware was loaded with verification, then independently read back over all
16 MiB before application launch: firmware bytes matched exactly and all
bytes beyond its erase sectors matched the original.

Evidence and original backup remain in `build/boat/deploy-20261012-01/`.
Deployed UF2 SHA-256:
`fd1d6e25ad4f9c41262a28fbfbf6fc9e51ad2ac919f417a40d00b7fd5fb58bc8`.
The owner confirmed water/boat output and correct response toward the lower
side on gentle tilt. They also reported that the upright-pose warning is
too picky when the badge is only partially upright. This led to the B3
input update below; its wider tolerance still needs physical checks,
not a claim that B3 is complete.

This badge is intentionally left running the boat image; it is not restored.
The nametag badge was not flashed or otherwise changed. Use physical
BOOT/HOME + RESET to enter BOOTSEL for subsequent flashing or restoration.

## B3 input update

[boat_input.c](../../src/shared/boat_input.c) implements a portable fixed-point
gravity-vector estimator; the conservative water/hull solver is unchanged.
The shared IMU configuration remains 104 Hz, +/-2 g and 245 dps. The
[ST LSM6DS3 datasheet](https://www.st.com/resource/en/datasheet/lsm6ds3.pdf)
specifies 0.061 mg/LSB and 8.75 mdps/LSB. Gyro integration uses a rounded
conversion of `raw * dt_us / 100000` into Q16 radians, with trapezoidal rate
integration, a gravity cross product and normalization. Accelerometer
correction has an approximately 500 ms time constant. Mathematical sign
tests do **not** qualify the board's physical gyro direction.

### Holding and controls

- Hold still at startup for at least 500 ms and 32 samples. Calibration can
  take longer when synchronous presentation reduces the sampling rate.
  Motion restarts the window: gyro magnitude must stay below 2000 raw counts
  per axis, gyro span below 200 and accelerometer span below 600. The screen
  explicitly says `HOLD STILL - CALIBRATING GYRO`.
- Partial upright holding, including a 60-degree forward/backward lean, is
  accepted in synthetic tests. Entry needs screen-plane gravity >=6500/16384;
  exit is below 5500/16384, approximately 67/70 degrees of out-of-plane lean
  at zero roll. Near-horizontal and inverted poses still pause.
- In-plane roll enters below approximately 36 degrees and exits above
  approximately 42 degrees, with 100 ms transition hysteresis. The solver
  still receives `gx` bounded to +/-96/256 and `gy` to 192..256.
  Additional roll therefore saturates water forcing rather than expanding
  the model's qualified numerical range.
- Screen-plane gravity is **projected and normalized**. Leaning backward
  does not weaken the demonstration as it would in a real shallow tank.
  This is a deliberate side-view visualization choice, not general 3D fluids.
- HOME resets only the water/hull and retains calibration; RESET restarts
  firmware/calibration. HOME sleep is still optional/unimplemented for boat.

### Timing and unreliable input

Sensor reads have their own minimum 10 ms deadline, independent of physics
catch-up; each read uses its actual completion timestamp. A catch-up batch
reuses one gravity estimate and never integrates that gyro sample again.
Unsigned elapsed times handle TIMER0 wrap. Samples closer than 5 ms are not
integrated or counted toward calibration.

A gap over 50 ms explicitly pauses and invalidates the prediction. A following
credible sample realigns gravity; stale angular velocity is never extrapolated
through a long stall. Gyro clipping likewise invalidates the estimate.
Accelerometer magnitudes outside 0.75..1.25 g are ignored; after 250 ms without
credible acceleration, physics pauses with `MOTION UNRELIABLE - HOLD STILL`.
A steady, opposing gravity observation can re-anchor after 250 ms, avoiding
the exact-antipode failure of normalized vector blending. This also permits
recovery after starting inverted without trusting an instantaneous flip.

Norm gating is not a complete shake detector. A plausible 0.5 g lateral
translation lasting 100 ms produces a measured 20/256 horizontal input in
the synthetic test (about 4.5 degrees), then returns near zero. Sustained
acceleration, temperature-dependent bias, sensor aliasing and rotations above
the gyro's range are not solved by this complementary filter.

### Host evidence and remaining gates

[boat_input_test.c](../../src/host/tests/boat_input_test.c) replays +/-30-degree
turns with a 60-degree lean, using 8 s and 0.8 s periods (peak rates about
24 and 236 degrees/s). At 10 ms sampling, maximum projected `gx` errors
are 3/256 and 6/256; alternating 10/20/40 ms intervals give 2/256 and
6/256. Tests require <=6/256 in every replay, bounded solver inputs,
supported status throughout and return to neutral. These are synthetic
estimator tests, not measured hardware accuracy or frame-rate claims.

Current ARM build: **12,772 flash bytes**, **313,024 BSS bytes**, with no
unresolved runtime/compiler helpers. Entry and input-update compiler frames
are 80 and 72 bytes; physics remains 36 bytes and display callback 16 bytes.
These are not whole-program stack high-water measurements. Target code
still has no floating-point runtime; trigonometry is used only by host fixtures.

B3 is not fully accepted yet: physical gyro sign/fast-turn response and
partial-upright warning checks remain open. Synthetic replay exists in the
host suite; a graphical host preview, optional sleep and B4 timing/convergence
qualification remain future work.

### First fusion deployment and calibration blocker

`build/boat/deploy-20261012-02/` preserves the previous complete flash and the
first fusion image, with exact 16 MiB readback and unchanged tail beyond
12,288 bytes. It was started on `08DA26F86E57F5EA`; the nametag badge was
untouched. **The owner reported calibration never clears.** This image is
not physically accepted. Its UF2 SHA-256 is
`1b13117441a7e551e63d8101516d908ca0e3f2eaf13b23d99e0da159a7b1b7f7`.

Investigation found the original 500-count calibration rate ceiling rejects
stationary gyro offsets above 4.375 degrees/s, less than the datasheet's
typical +/-10 degrees/s zero-rate level. The correction permits up to 2000
counts (17.5 degrees/s) while retaining accelerometer/gyro window-span
checks. A host regression now calibrates a stationary 1200-count offset
and verifies neutral gravity afterward. This is a demonstrated software
defect, **not yet a measurement of this badge's actual blocker**.

The next image also displays calibration reason, sample count, maximum raw
gyro rate and last sample gap in milliseconds. It distinguishes implausible
acceleration, excessive rate, varying readings and >50 ms sample gaps; no
timeout silently bypasses calibration. A steady slow rotation can resemble
a constant bias, so the owner must actually hold the badge still.
The corrected/diagnostic build passes all three sanitizer suites and the
no-libc ARM link.

### Corrected fusion deployment

After the owner returned the boat badge to BOOTSEL, the corrected image was
serial-bound flashed onto `08DA26F86E57F5EA` and started. Evidence is in
`build/boat/deploy-20261012-03/`: previous full flash, exact independent
16 MiB readback and unchanged tail beyond the 16,384-byte erase boundary.
UF2 SHA-256:
`cb00fd7beed59585775ad9291447888755e51e6d54f87c76a9a9aa5e696f7fb4`.

The owner confirmed calibration clears, the boat responds, and motion is
"nice and smooth" with no flicker. They sometimes see `LIFT SCREEN A LITTLE`
while moving. That warning still gates weak screen-plane gravity or steep
roll; it is not evidence that all intended holding angles have been measured.
The earlier calibration blocker is resolved on this badge, but raw readings
from the failing image were unavailable, so its precise hardware cause is
not asserted. Detailed fast-turn gyro signs, HOME reset and B4 hardware
timing/long-run checks are not claimed from the general smoothness report.

The corrected boat image is intentionally left running. The nametag badge
was not changed. Both the original flash and previous boat images remain
available; physical BOOT/HOME + RESET still provides reflashing/restoration.

## B4 timing and long-run diagnostic

`make boat-profile` builds a **separate, bounded laboratory image**,
`build/universe26/uf2/boat_profile.uf2`, and the serial-bound collector
`build/host-tools/boat_profile_host`. The ordinary `make boat` image has no
USB service or profiling overhead and retains sensor-driven behavior.

[boat_profile.c](../../src/tools/boat_profile.c) compiles the same application
renderer/presenter and physics loop with optional measurement hooks. The
first full frame is recorded separately, then measurement starts from a
reset water/hull state. An explicit screen heading identifies the scripted
test. Gravity forcing performs 50 identical 12-second cycles: an eight-second
sweep through +/-80/256 with `gy=240/256`, then four seconds upright. At zero
horizontal forcing, `gy=256/256`. The physical sensor is still read and its
calibration/status/bias recorded, but does **not** drive the qualification
water. This permits repeatable real rendering/transfer load without asking
the owner to rotate the badge for ten minutes.

Each of 60,000 physics steps checks exact initial water volume, closed
boundaries, depth and hull bounds. Physics deadlines and five-step catch-up
are unchanged; lost ticks are counted, not concealed. The collector requires
zero physics skips and completion within 599..601 nominal seconds. A 630 s
device deadline and 650 s host deadline bound failure paths.

Recorded timing groups cover:

- I2C sensor read and gravity-input update separately;
- individual physics steps, excluding diagnostic invariant-check overhead;
- renderer and complete differential presenter, including last-success commit;
- writer callbacks, including diagnostic USB service inside transfers;
- complete render+present work, and intervals between completed frames.

Every event contributes count/sum/min/max, not just a short sample reservoir.
Frame-work p99 is bracketed by a 64-bin, 1 ms histogram; the last bin holds
>=63 ms. An independent exact count of work >25 ms determines whether
nearest-rank p99 <=25 ms, without confusing histogram rounding with the
actual threshold. Completed-frame intervals >40 ms, missed render deadlines,
panel payload/regions/full frames and maximum sensor gap are retained.

USB frame numbers provide an independent nominal-millisecond comparison for
the crystal timer, not external frequency calibration. The diagnostic
reuses the tested M2 **paged transport envelope**, CRC/build/nonce checks
and control endpoints; its payload uses a distinct boat magic/schema and
its dedicated collector rejects nametag payloads. This does not change the
shared USB driver or claim that a boat report passed nametag qualification.
No report is expected to survive a BOOTSEL reboot; the host retrieves it
before requesting recovery. Cancellation, USB loss and invariant failures
publish explicit error status or stop watchdog renewal.

After backing up and serial-bound flashing/verification, collect with:

```sh
build/host-tools/boat_profile_host 08DA26F86E57F5EA > measurement.json
node src/host/tools/boat_profile_report.mjs measurement.json > report.json
```

The collector requests BOOTSEL after collecting the report. Restore the
continuous boat image with full readback/tail verification before leaving
the badge running. Do not leave this lab image in place of the interactive
demo or flash the nametag badge.

### Numerical characterization

The host additionally compares a 60-second scripted replay against its
opposite-tilt, mirrored-column counterpart. Volume and boundaries remain
exact in both directions, but maximum mirrored depth disagreement is
**399/256 = 1.56 pixels**. This exposes the sequential flux update's
directional bias; it is characterization, not a convergence acceptance
gate. Smaller timestep/grid comparisons, dry-front realism and two-way
hull coupling remain unqualified.

### Baseline hardware measurement, 2026-10-12

`build/boat/campaign-20261012-profile-01/` preserves the full pre-test flash,
diagnostic/continuous images, independent readback and nonce/build-bound
measurement. Diagnostic build `b93ca481`, UF2 SHA-256:
`e1f855a1b72dc5a826dffbee17fa8288fe875c2af9a88ae5e65d2830fffeb57e`.
The report passes payload, counter, bounds and timer/SOF validation:

- **599.990 s**, **60,000 physics steps**, **18,000 completed frames**
  (**30.0005 frames/s**), zero missed physics/render deadlines;
- exact water volume after every step, zero volume violations;
- water depth 47.66..114.33 pixels, hull within modeled bounds;
- mean sensor read **1.584 ms**, physics step **0.124 ms**;
- mean rendering **18.437 ms**, presentation **7.893 ms**,
  combined frame work **26.330 ms**;
- frame-work p99 in **[27,28) ms**, maximum **29.130 ms**;
  all measured frames exceed 25 ms, so that target **fails** even though
  the average completed cadence meets 30 frames/s;
- no completed-frame intervals >40 ms; timer/SOF ratio about 1.00001.

Real IMU samples were collected but the resting badge remained in an
unsupported pose after calibration. This is a solver/rendering workload
pass, not a ten-minute fused-input acceptance check.

The collector produced the verified complete report but subsequently hit
its 650 s deadline during the BOOTSEL handoff; read-only inventory then found
the selected badge in BOOTSEL. That failure is retained, not presented as a
successful collector command. The diagnostic now mirrors the existing
nametag protocol's 10 ms status-stage grace before USB disconnect/recovery;
a host regression checks that recovery is not requested before the grace.

### Renderer optimization under measurement

The baseline identified rendering, not physics, as the dominant cost.
The interactive and diagnostic renderer now fills contiguous column-major
sky/water/floor spans instead of clearing the complete frame and then
overpainting the sea. The hull/HUD draw order and final floor occlusion are
preserved. Host tests compare every background pixel against the previous
graphics-based implementation at rest, alternating dry/full depths and
irregular bounded depths. The water/hull solver is unchanged.

The optimized diagnostic uses **20,248 flash bytes**, **316,192 BSS bytes**
and the same no-libc link. The ordinary image remains USB-free. Hardware
results must determine the speedup and 25 ms gate; fewer framebuffer writes
alone are not performance evidence.

### Optimized hardware gate and final deployment

`build/boat/campaign-20261012-profile-02/` contains the optimized image,
complete readback, verified measurement/report and final deployment evidence.
Diagnostic build `41cf62b2`, UF2 SHA-256:
`5d5c742b92d371efc5781bba2d73a161e38c93c98e258d7ba01bbae4c4fa2849`.
The collector exited **0** and returned the selected badge to BOOTSEL.

| Measurement | Baseline | Optimized |
| --- | ---: | ---: |
| Mean sensor read | 1.584 ms | 1.599 ms |
| Mean physics step | 0.124 ms | 0.124 ms |
| Mean rendering | 18.437 ms | 6.654 ms |
| Mean presentation | 7.893 ms | 7.920 ms |
| Mean render + present | 26.330 ms | 14.574 ms |
| Frame-work p99 bracket | [27,28) ms | [15,16) ms |
| Maximum frame work | 29.130 ms | 16.941 ms |
| Completed frames | 18,000 | 18,000 |
| Completed cadence | 30.0005/s | 30.0005/s |
| Physics/render deadline skips | 0 / 0 | 0 / 0 |
| Completed intervals >40 ms | 0 | 0 |
| Frames with work >25 ms | 18,000 | 0 |

Both runs cover approximately 599.990 nominal seconds and 60,000 scripted
physics steps, with exact volume after every step and no invariant failures.
The optimized run reaches the **30 completed frames/s** and **p99 work
<=25 ms** gates for this workload. Rendering fell about **64%**, combined
frame work about **45%**, without changing background pixels or physics.
This is improved headroom, not a frame-rate increase beyond the 30 Hz cap.

The input monitor sampled 36,000 times rather than 18,000: mean sampling
cadence improves from about 30 to 60 Hz as rendering stops occupying most
of the frame. It is still below the sensor's configured 104 Hz. The largest
recorded sample gap is 73.426 ms including startup/calibration HUD changes;
no steady-state-only maximum is claimed. The resting badge calibrated but
remained unsupported, so the numerical workload deliberately continued with
scripted gravity. A measured steady gyro bias around -602 raw counts on one
axis is consistent with the earlier 500-count calibration-limit defect.

Presentation, at about 7.92 ms, is now the largest measured frame component.
Total writer time averages about 0.91 ms per frame, including USB service;
about 7.01 ms remains in comparison/planning/commit and measurement boundaries.
Further work should investigate presenter CPU cost and synchronous IMU gaps,
not optimize the already-small physics step by guesswork.

Before final deployment, **all 20,480 bytes touched by the larger diagnostic
were restored from the pre-test flash**. The optimized continuous image was
then programmed and independently read back across all 16 MiB. Its 12,772
firmware bytes match exactly, and every byte beyond the 16,384-byte erase
boundary matches both pre-test and original factory backups. This removes
diagnostic residue rather than merely loading a smaller image over it.

Final continuous UF2 SHA-256:
`d32056b1398a9f5e65d5afb5bd87c04c04ace6060e1c72b6e19a6aa6cce3fd8e`.
It was started on `08DA26F86E57F5EA` and intentionally left running.
The owner confirmed calibration clears, normal tilt response and **HOME
reset works**. The nametag badge remains untouched. The diagnostic's USB
service/invariant checks are not included in this interactive build.

B4 is only partly accepted: these results establish this scripted hardware
workload's invariants and cadence, not tear-free output, physical gyro signs,
sensor-driven ten-minute behavior, smaller-grid/timestep convergence,
full-power startup or calibrated hydrodynamic accuracy.

## B5 first particle prototype

Build `make boat-breaking`; validate with `make test-boat-fluid test-boat`.
The experimental UF2 is `build/universe26/uf2/boat-breaking.uf2`.
The default `make boat` continues to use the accepted shallow-water model.
No original nametag assets or sensor configuration are changed.

[boat_fluid.c](../../src/shared/boat_fluid.c) is an original fixed-point
implementation in the density/near-density relaxation family; background:
[Clavet et al., Particle-based Viscoelastic Fluid Simulation (2005)](https://www.ligum.umontreal.ca/Clavet-2005-PVFS/pvfs.pdf).
It is not a full implementation of that paper and does not implement its
viscoelastic spring model.

### Model and visible behavior

- **576 particles**, initial 48x12 lattice with 6.5-pixel spacing, Q8
  coordinates/velocities and a 10-pixel interaction radius.
- Three Jacobi-style relaxation passes, followed by pairwise viscosity.
  Uniform gravity retains fractional acceleration so gentle tilt does not
  disappear through integer rounding. Tiny velocities are not rounded away
  by multiplying a damping coefficient on every step.
- Pair corrections are equal and opposite before displacement/velocity
  limiters and wall containment. Bins are rebuilt after corrections; a
  precomputed squared-distance lookup avoids a square root per neighbor.
- Each particle always exists with the same mass. Pressure relaxation is
  **compressible and approximate**; unlike the height-map model, this does
  not conserve an exact sum of column depths or geometric occupied area.
- Tank boundaries stop particles rather than deleting spray. There is no
  separate decorative splash-particle pool: detached drops are actual water
  particles. Air pressure, entrainment/bubbles, calibrated surface tension,
  full Navier-Stokes and guaranteed incompressibility are not implemented.
- A 160x120 density field is expanded to the 320x240 panel. It can display
  several separated wet intervals in one column and connected crests with
  air underneath. Smoothing/thresholding can create or erase sub-grid holes;
  this is not a particle-scale accurate air-interface reconstruction.
- Sky and water have vertical color gradients; the water shading is a
  lighting cue, not a calculation of optical depth. A sparse subset of
  advected particles reveals internal flow.
- Foam follows particles involved in fast wall impacts or sparse,
  fast-moving surface flow. It fades over 25 simulated ticks and is drawn
  only along wet/air edges. It is a visual cue derived from physical state,
  not a multiphase foam or spray-air simulation.
- The hull samples immersed local density and nearby current for bobbing,
  pitch and drift. It does not exclude fluid from the hull or push water
  back; two-way rigid-body coupling remains B6 work.

The tool retains the calibrated moderate-tilt input range. Physics nominally
advances in 10 ms steps, but **only one expensive particle step is allowed per
foreground pass**. Overdue ticks are explicitly counted/dropped; the prototype
will run in slow simulation time if it cannot keep up. The HUD shows last
step/frame-work microseconds and missed ticks. Existing >50 ms input-gap
and unsupported-pose protection remain active rather than trusting stale
orientation to make a slow simulation look responsive.

### Host evidence and previews

The sanitizer tests run:

- 3,000 tilt/reversal/settling steps, then 60,000 alternating tilt/rest
  steps with every particle/hull position and velocity checked;
- an upright settling check with no false attached overhang;
- an eight-second dam-collapse replay, sampled every tenth tick;
- invalid input/particle-state rejection without partial mutation;
- full experimental application, exact panel reconstruction, calibration,
  HOME reset, unsupported-pose freeze, stalls and all existing sensor/error
  recovery paths. The classic model's host suites still pass.

The dam release produces an attached overhang in **44 sampled frames**, with
a peak of **18 two-pixel columns**. The topology test requires at least a
six-pixel air gap below a four-pixel-thick crest connected to bulk water;
disconnected spray is explicitly excluded. This is a demonstrated 2D topology
change, **not proof of quantitatively realistic wave breaking**.

Reproducible water-only preview:

```sh
make boat-fluid-preview
build/host-tools/boat_fluid_preview dam 200 output.ppm
```

`rest`, `dam` and `tilt` scenarios use the exact target solver and renderer.
They can export any step 0..60000 without target dependencies. Initial
previews remain in `build/boat/fluid-preview/`; `dam-200.png` shows the
wall-impact crest and air gap, and `tilt.png` shows a reversal wave and
advected tracers. These previews omit the application's hull and HUD.

### Build budget and qualification state

Experimental image: **20,104 flash bytes**, **402,392 BSS bytes**; particle
state including field/scratch/lookup storage is **90,028 bytes**. The ARM
link has no unresolved runtime/compiler helpers or floating-point runtime.
Compiler frames: application 80 bytes, fluid step 104, neighbor pass 108,
field construction 68 and renderer 56. These are not stack high-water measurements.

The host gates pass, but **the first hardware trial fails the interactive
performance gate**. Density convergence, breaking behavior under real tilt
and physically driven long-run acceptance are not yet qualified.
The existing optimized boat remains the accepted fallback.
Use serial-bound backup/readback and remove any extra experimental flash
sectors when restoring the smaller accepted image; do not merely overwrite
its prefix. The nametag badge must remain untouched.

### First hardware trial: performance failure

Installed on boat badge `08DA26F86E57F5EA` after a verified full-flash backup
matching the accepted boat readback. Independent readback verified every
UF2 payload, zero-filled final-sector padding and unchanged flash beyond
**20,480 bytes**. Backup, programmed readback and verification details are
in `build/boat/breaking-deploy-20261012-01/`.

Owner reports heavy stuttering, a flickering “lift screen a little” warning,
and these approximate HUD readings:

| Measurement | Owner reading |
| --- | --- |
| Last particle step | 103,377 us |
| Last render + presentation work | 33,400 us |
| Cumulative skipped physics ticks | 13,200 |

These are manually observed last-operation readings, not a USB campaign,
percentiles or a measured completed-frame rate. **Do not reuse the classic
boat's 30 fps claim.**

A particle step alone exceeds the input estimator's 50 ms sample-gap
ceiling. Since the foreground loop reads the IMU between work batches,
such a step necessarily makes the next sample stale. The estimator then
clears alignment and supported-pose hysteresis; after realignment, the
pose-entry delay can show the “lift” prompt even in a supported physical
pose. Heavy work therefore harms both cadence and input continuity.
Do not hide this by removing the freshness gate or suppressing the prompt.

Before another acceptance test, reduce measured solver/render costs and
keep IMU service timely during expensive work. Profile the repeated
neighbor sweeps and display work rather than assuming a desktop speed
or a particular inherited CPU frequency. Any changed resolution,
relaxation scheme or clock needs fresh topology, numerical and hardware
checks. Owner chose to **keep this experimental image for now**; this is
not smoothness acceptance. The accepted boat can be restored from the
backup, including all 20,480 experimentally touched bytes.
