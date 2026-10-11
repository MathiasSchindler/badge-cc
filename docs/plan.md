# Universe 2026 Badger Bring-up Plan

This is the staged badge bring-up plan, not the current source-radio task
list. Use the [documentation index](README.md) for working apps and the
[CYW43439 handoff](cyw43/handoff.md) for current radio research/testing.

## Goal

Produce small, standalone UF2 firmware images for the 2026 prototype, establish
safe display output, map its controls and board signals, and identify and
operate the accelerometer. The default firmware is freestanding C without
libc or a runtime SDK. Optional SDK radio apps remain separate from both the
[working freestanding Wi-Fi host](wifi.md), which uses vendor CYW43439
firmware, and [source-radio research](cyw43/handoff.md).

The 2025 project at `/Users/mathiasschindler/badger` is the implementation
reference. Its GPIO assignments are hypotheses until supported by the 2026
manufacturer schematic or physical tests. Keep those evidence levels distinct.

## Principles

- Keep every milestone independently flashable and easy to inspect.
- Preserve a reliable BOOTSEL/reset recovery procedure before hardware probing.
- Begin unknown GPIOs as high-impedance inputs without pulls.
- Never drive an unknown pin merely to determine what it is.
- Separate confirmed facts, observations, and hypotheses in the hardware map.
- Use the screen for diagnostics only after its complete bus and power wiring
  are verified.
- Keep display traffic out of GPIO observation windows so the diagnostic
  output does not become its own input.

## Phase 0: Establish recovery and reference facts

1. Confirmed: hold the prototype's Boot button while connecting USB. The
   RP2350 bootloader presents a drive; copying a UF2 to it installs and boots
   that firmware. Repeat the procedure to replace the image.
2. Save any factory firmware supplied with the prototype if it can be read
   without bypassing RP2350 security.
3. Record visible PCB markings and obtain schematics or board files if
   available.
4. Start a hardware map containing signal, GPIO, direction, active level, pull,
   source, confidence, and notes.

Exit criterion: a failed test image can always be replaced without relying on
the test firmware.

## Phase 1: Minimal UF2 toolchain

Status: complete. The UF2 delivery path and common startup/image path are
physically validated by the working static display firmware.

Build only the components needed for the first hardware tests:

1. freestanding Cortex-M33 compilation with Clang;
2. project-owned reset/startup code and vector table;
3. RP2350 picobin metadata;
4. a minimal flash/RAM layout;
5. raw binary extraction;
6. the project-owned `bin2uf2` host utility; and
7. a `Makefile` whose default target emits target-qualified UF2 files.

The first implementation should use `ld.lld` as a narrow, explicit final
linker. Reimplementing the linker is valuable but is not on the critical path
for discovering 2026 hardware. The project-owned linker can replace `ld.lld`
after the startup, image layout, and test binaries are known-good.

First images:

- `hello_idle.uf2`: boot safely and wait forever;
- `heartbeat.uf2`: provide a visible proof only after a safe LED GPIO is known.

`heartbeat.uf2` has not been implemented. The manufacturer schematic now
maps four active-high white case LEDs to GPIO0-3; dedicated physical LED
validation remains pending.

Exit criterion: deterministic UF2 output with no libc, SDK runtime, hidden
startup code, or target-side external dependency.

## Phase 2: Display proof

Status: complete. Both 2025 graphics firmware and this repository's standalone
static display test were physically validated on the 2026 prototype.

Start with the 2025 display facts as explicit hypotheses:

- ST7789-class 320x240 panel;
- 8-bit parallel data bus;
- a possible 160x120 pixel-doubled mode;
- a separate backlight signal;
- a board power-enable signal; and
- optional tearing-effect/vsync input.

The first display program should:

1. assert only previously verified power and display outputs;
2. initialize the panel conservatively;
3. fill the screen with solid diagnostic colors;
4. render a tiny built-in bitmap font;
5. show build identity and test status; and
6. stop updating unless content changes.

Static output is preferable during signal discovery because continuous screen
refresh creates GPIO activity and electrical noise.

Exit criterion: stable text and solid colors on the physical prototype, with
the confirmed display mapping written to the hardware map.

## Phase 3: Passive digital GPIO discovery

Status: front controls mapped by the guided physical test. Left is GPIO7,
Right GPIO9, Enter GPIO10, Up GPIO11, and Down GPIO6. The rear Home button is
GPIO22. All eight touch channels are now physically mapped; see Phase 4.
The manufacturer schematic
now identifies GPIO13 as `CAP_ALERT`, GPIO17 as `IR_RX`, GPIO20 as `IMU_INT`,
and GPIO21 as `LCD_VSYNC`. It also identifies GPIO15 as the combined
front-button interrupt, superseding the display-noise hypothesis. The causes
of the earlier startup levels and isolated transitions remain untested.

Do not indiscriminately reconfigure every RP2350 GPIO. Exclude:

- confirmed display, power, flash, PSRAM, Wi-Fi, and debug signals;
- pins needed to keep the board powered;
- pins whose external circuit is unknown and may not tolerate pulls; and
- any signal currently used to report results.

For remaining candidates:

1. configure SIO input mode with output disabled and pulls disabled;
2. capture an initial level bitmap;
3. count rising and falling edges per GPIO;
4. debounce slow control inputs in software;
5. associate physical actions with changed pins;
6. acquire events while display writes are stopped; and
7. update the screen in a separate, bounded reporting interval.

Useful display fields are GPIO number, current level, rise count, fall count,
time since last edge, and whether the line appears idle, periodic, or noisy.
A single-action capture mode should clear counters, prompt for one named
control, wait for activity, and rank candidate GPIOs.

This phase can map direct buttons, direct interrupt outputs, LEDs observed as
outputs under factory firmware, and other ordinary digital signals. It cannot
identify the meaning of an inactive bus or decode a peripheral.

Exit criterion: repeatable mappings for all directly wired controls, with
active levels and pull behavior recorded.

## Phase 4: Touch-control discovery

Status: the GPIO4/5 external I2C bus was physically scanned and contains no
devices on this prototype. Guided and responsive GPIO0-22 tests produced no
events from ordinary touches on the eight lower controls. The schematic now
identifies U10 as a CAP1208 on the internal GPIO18/19 I2C bus, sharing
`3V3_SW` and 10 kOhm bus pull-ups with the IMU, with `CAP_ALERT` on GPIO13.
CS1-8 connect to CAP1-8. On 2026-09-14 the owner confirmed all eight
working and mapped their positions: CS1 up, CS2 right, CS3 down, CS4 left,
CS5 right slash, CS6 left slash, CS7 upper-right round, and CS8 lower-left
round.

The freestanding [diagnostic app](diagnostics.md) now provides CAP1208
identity-checked configuration and live channel/status reads, along with
IMU, button, IR, LED, power/light, display, and external-bus test pages.
Touch operation and channel mapping are physically confirmed; other new
peripheral paths still require prototype validation.

Next steps:

1. Completed: verify the CAP1208 register/address and initialization contract
   against Microchip DS00001570C; implement and host-test the driver.
2. Completed for touch operation: all eight sensors reported working on the
   prototype; no blanket pass is implied for other diagnostics.
3. Confirm which of the supported CAP1208 addresses responds on the prototype.
4. Completed: record the owner-confirmed channel-to-control mapping in
   [the specification](spec.md#physically-confirmed-touch-channel-mapping).
5. Validate GPIO13 alert polarity, clearing, and configuration separately.

The historical GPIO mappers and external-bus probe are not a substitute for
this internal-bus test.

Exit criterion met for channel discovery: all eight controls are reported
working and uniquely mapped. Remaining characterization includes GPIO13
alert behavior, sensitivity, and simultaneous touches.

## Phase 5: Accelerometer discovery

Status: manufacturer documentation identifies the LSM6DS3TR-C on the internal
I2C bus at address `0x6a`, with SDA on GPIO18, SCL on GPIO19, and `IMU_INT` on
GPIO20. GPIO41 must be driven high to enable the shared `3V3_SW` rail first.
The dependency-free driver and live display demo are implemented. Identity
`0x6a` and gravity-consistent live samples are physically confirmed;
badge-relative X/Y/Z orientation is physically mapped, and the display includes
a live tilt view.

1. Completed: identify the part, internal I2C pins, address, interrupt, switched
   supply, and shared-bus topology from the manufacturer schematic.
2. Completed: implement the minimum driver and a standalone no-libc display
   demo.
3. Completed: flash `imu_demo.uf2` and confirm identity `0x6a`.
4. Completed: confirm changing accelerometer and gyroscope samples.
5. Completed: determine physical axis orientation.
6. Validate interrupt polarity/configuration on GPIO20 if interrupt-driven
   operation is needed.

Exit criterion: the screen reports stable X/Y/Z samples, the stationary vector
has approximately 1 g magnitude, axes react consistently to rotation, and the
identified part and wiring are recorded with evidence.

## Phase 6: Remaining peripherals

Map and test, in order of electrical risk:

1. validate the schematic-mapped case LEDs on GPIO0-3;
2. validate the infrared receiver on GPIO17;
3. validate the infrared transmitter on GPIO16 at low duty cycle;
4. validate battery sense on GPIO40, VBUS detection on GPIO12, and charge
   status via radio-module GPIO2; calibrate light sensing on GPIO43;
5. Completed: validate Wi-Fi initialization, association, DHCP, DNS, and TCP
   through the isolated Pico SDK/CYW43 build.
6. Completed baseline: validate Bluetooth with the combined
   `wireless_lab.uf2` scanner, beacon, and direct GATT peer modes. Two physical
   badges have confirmed advertising discovery in both directions,
   deterministic central/peripheral election, GATT connection, and
   bidirectional messages. Short adjacent-badge tests at a 7.5 ms interval
   and latency 0 recorded 13-14 ms average application RTT and 100/100
   acknowledgements per direction; see [Wireless Lab](wireless-lab.md).
   Range, longer-run loss/stability, reconnect behavior, coexistence, power,
   and operation with more than two badges remain characterization work.
7. Initial gameplay confirmed: the owner played BLE tilt Pong on two badges
   and reported great speed with no significant perceived lag on 2026-09-14.
   This is qualitative evidence, not a measured latency or frame-rate result.
   Continue with the [Pong regression checklist](ble-tilt-pong.md#two-badge-test-procedure).

The SDK radio targets described in this phase use Pico SDK, CYW43
support/firmware, newlib, and BTstack for BLE. Keep this exception isolated
from the default freestanding firmware. The later [project-owned Wi-Fi
host](wifi.md) and [source-radio research](cyw43/handoff.md) have different,
explicit dependency boundaries.

As of 2026-09-22 the freestanding host baseline has 600/600 paired UDP
echoes. Source cold initialization/calibration and PSM components exist, but
the checked six-byte prefixes are vendor-retained results, not source-only
fresh-frame reception. Cold-source no-event remains unresolved. The
[current workflow](cyw43/workflow.md), not this early bring-up sequence,
owns authorized bounded experiments with RF measurement explicitly deferred.

## Phase 7: Project-owned linker

Once several physical test programs are known-good, port the narrow linker work
from the 2025 project or implement the documented RP2350 linker contract here.
Validate its output byte-for-byte or behavior-for-behavior against the
`ld.lld` path before making it the default.

## Immediate implementation order

Historical initial sequence; completed steps are recorded above. This is
not the current Wi-Fi/source-radio task list.

1. Import the smallest reusable RP2350 startup, picobin, linker-script, and UF2
   conversion pieces from the 2025 project, retaining provenance.
2. Remove unrelated 2025 platform assumptions and GPIO constants.
3. Build and inspect `hello_idle.uf2`.
4. Verify BOOTSEL recovery on the physical prototype.
5. Add the static display proof using a provisional, clearly labeled pin map.
6. After physical display validation, add the bounded passive GPIO monitor.
