# Universe 2026 Badger Background

## Project context

GitHub Universe is GitHub's annual user conference. In 2025, attendees
received a programmable electronic badge ("badger") produced by Pimoroni under
the product name "Tufty." The badge used a Raspberry Pi RP2350 and included:

- a screen;
- physical buttons;
- Wi-Fi;
- Bluetooth; and
- infrared.

This repository targets a prototype of the 2026 Universe badger. It is related
to the 2025 hardware but has board changes, including an accelerometer.
Consequently, no 2025 GPIO assignment or peripheral wiring should be assumed
to remain correct without verification.

The display resolution remains unchanged from 2025: 320x240 physical pixels.
The 2025 software also used a 160x120 logical mode with 2x pixel doubling.

## Known user-facing hardware

The front of the 2026 prototype has five physical controls:

- left;
- right;
- Enter;
- up; and
- down.

Below the display is a Game Boy-style touch-sensitive control area:

- two round controls on the left: lower-left CS8 and upper-right CS7;
- two slash-shaped controls in the middle: left CS6 and right CS5; and
- four square directional controls on the right: up CS1, right CS2, down CS3,
  and left CS4.

The owner confirmed all eight sensors working and these positions on
2026-09-14. Earlier A/B and Select/Start descriptions were provisional
game-control names, not confirmed assignments to the round/slash controls.

The manufacturer schematic confirms an infrared transmitter and receiver,
four white case LEDs, a light sensor, an LSM6DS3TR-C IMU, and a CAP1208
touch controller. See [the specification](spec.md) for pin assignments and
the distinction between schematic evidence and physically tested behavior.

## Prior work

The repository at `/Users/mathiasschindler/badger` contains experiments and
firmware for the 2025 badger. It is a reference for implementation techniques,
RP2350 bring-up, multicore operation, display handling, firmware construction,
and project conventions, but not an authoritative source for the 2026 board
layout.

Notable prior work includes an AVIF decoder that ran successfully on the
RP2350 and experiments designed to use both RP2350 cores effectively.

## Engineering direction

The firmware is intended only for the 2026 badger. Portability and
interoperability with other boards are not requirements.

Preferred constraints:

- write the firmware in C;
- avoid external dependencies where practical;
- do not depend on a standard C library;
- retain direct control over startup, memory layout, hardware access, and
  generated firmware;
- use project-owned low-level tooling, including linker and file-generation
  tools, where practical; and
- optimize for a small, inspectable core rather than a general-purpose
  framework.

External documentation, SDKs, and existing code may be used to learn hardware
behavior, but should not become runtime or build dependencies unless a
dependency is demonstrably necessary.

These constraints apply by track, not as a blanket claim about every image.
The [freestanding Wi-Fi host](wifi.md) owns gSPI/SDPCM/CDC/BDC and
ARP/IPv4/UDP but downloads vendor CYW43439 firmware and configuration.
Optional [SDK radio apps](wireless-lab.md) deliberately retain their
SDK/lwIP/BTstack dependencies as applicable. Original freestanding
[source-radio research](cyw43/handoff.md) has cold initialization/calibration
and PSM components, but no source-only fresh-frame result. Host compilers,
limited standard-library Python and existing legacy Ruby tools are not
target dependencies. The dated log below is historical evidence, not the
current radio task list.

## Initial objective

The first phase is to identify and operate the accelerometer on the 2026
prototype. This requires establishing, rather than assuming:

1. the accelerometer manufacturer and part number;
2. its bus type and address or chip-select wiring;
3. the RP2350 GPIO pins used by that bus and any interrupt lines;
4. its power and reset behavior;
5. its device-identification register and minimum initialization sequence; and
6. a repeatable standalone test that reads identity and acceleration samples.

Early investigation may use temporary diagnostic firmware or external tooling.
The resulting permanent implementation should follow the dependency-free,
no-libc direction above.

Direct GPIO edge observation is expected to identify controls wired as digital
inputs. It is not sufficient by itself to identify devices behind I2C, SPI, a
touch controller, or an interrupt line. Capacitive controls and the
accelerometer therefore require bus-aware discovery after the direct GPIO map
is established.

## Hardware confidence rules

- Treat the physical 2026 prototype, its schematic, and its board files as
  authoritative.
- Treat `/Users/mathiasschindler/badger` as authoritative for prior techniques,
  not for the 2026 pinout.
- Record the source and confidence of discovered hardware facts.
- Keep confirmed facts separate from hypotheses.
- Prefer reversible, low-risk probing before driving unknown GPIO pins.

## Confirmed recovery procedure

Holding the prototype's Boot button while connecting it over USB enters the
RP2350 USB bootloader. The host then presents a drive that accepts a UF2 file.
After the copy, the badge reboots into that firmware and continues to run it
until USB boot mode is entered again and another UF2 is installed.

## 2026 physical validation log

- `hello_idle.uf2` built by this repository was accepted as a 512-byte UF2.
  The badge rebooted without visible output, as expected for firmware that
  deliberately does not initialize the display.
- The 2025 `flight_sim.uf2` and AVIF decoder/viewer UF2 images from
  `/Users/mathiasschindler/badger` render correctly on the 2026 prototype.
  This is strong physical evidence that the display controller, resolution,
  power-enable line, parallel data bus, and graphics path remain compatible.
- This repository's `screen_test.uf2` displayed `GITHUB UNIVERSE`, `2026`,
  `DISPLAY OK`, and `320 X 240`, followed by green, blue, purple, and
  mustard-yellow blocks. This physically confirms the complete minimal display
  path and its GPIO map.
- The 2026 Enter and Up controls may use different GPIO assignments from their
  2025 equivalents.
- Guided physical mapping observed the five front controls as Left GPIO7,
  Right GPIO9, Enter GPIO10, Up GPIO11, and Down GPIO6. The successful test
  used weak internal pull-ups.
- The first broad no-pull live scanner never observed front-button events. A
  later broad pull-up scanner observed Left as GPIO7 with three edges, but also
  showed unrelated GPIO candidates and occasional idle events roughly once per
  minute. Long all-GPIO collection windows are therefore retired for front
  button reporting in favor of hardware edge latches on only the five confirmed
  pins.
- The GPIO4/5 I2C probe found no responding devices. The prototype has a rear
  connector labeled I2C that is visibly not connected to anything. GPIO4/5 is
  therefore confirmed as an externally available empty bus in this prototype,
  not the internal touch or accelerometer connection.
- A responsive GPIO0-22 monitor confirmed the rear Home button on GPIO22. The
  separate rear Reset button resets the processor. The schematic also maps
  its `RESET_SW` signal to GPIO14; runtime monitoring of that signal remains
  untested.
- Unplugging USB produced activity on GPIO17, initially making it a USB or
  power-state candidate. The later schematic identifies it as `IR_RX`;
  the cause of the observed transition remains untested.
- With monitored pads configured as input-only with weak pull-ups, GPIO13,
  GPIO20, and GPIO21 read Low at startup; every other GPIO from 0 through 22
  read High.
- On 2026-09-06 the manufacturer supplied the RP2350 pinout and full schematic.
  They identify the motion sensor as an LSM6DS3TR-C at I2C address `0x6a`,
  with SDA on GPIO18, SCL on GPIO19, and its interrupt on GPIO20.
- GPIO41 enables the shared `3V3_SW` rail and must be driven high before the
  IMU is accessed. The internal I2C bus is shared with the capacitive-touch
  controller and has 10 kOhm pull-ups to the switched rail.
- The first physical `imu_demo.uf2` run returned identity `0x6a` and live
  accelerometer and gyroscope values. A stationary sample of about -17 mg,
  -93 mg, and -958 mg had a gravity-consistent magnitude of about 963 mg.
- Physical orientation testing established screen-up as negative Z,
  screen-down as positive Z, right/left tilt as positive/negative X, and
  toward/away tilt as positive/negative Y.
- Review of the supplied schematic on 2026-09-14 established additional
  documented wiring: CAP1208 on GPIO18/19 with alert on GPIO13; IR TX/RX on
  GPIO16/17; LCD TE on GPIO21; case LEDs on GPIO0-3; radio enable/data/CS/clock
  on GPIO23/24/25/29; battery sense on GPIO40; and light sense on GPIO43.
  Flash is specified as 16 MiB W25Q128JVPIQ and PSRAM as 8 MiB
  APS6404L-3SQR-ZR with CS on GPIO8. These are schematic facts, not new
  peripheral or memory-capacity tests.
- The schematic identifies GPIO15 as the diode-combined front-button
  `SWITCH_INT`. This supersedes the earlier interpretation that its
  button-correlated edges were merely display noise.
- On 2026-09-14 the owner reported playing BLE tilt Pong on two badges.
  Speed was great and latency was very good, with no significant perceived
  lag. This is a qualitative gameplay observation with no hard timing or
  packet-loss measurements; see [the playtest record](ble-tilt-pong.md#physical-validation).
- On 2026-09-14 the owner supplied a badge layout image and confirmed every
  capacitive-touch sensor working: CS1 up, CS2 right, CS3 down, CS4 left,
  CS5 right slash, CS6 left slash, CS7 upper-right round, and CS8 lower-left
  round. This physically maps the channels; it does not establish the
  installed I2C address, interrupt timing, or sensitivity measurements.
