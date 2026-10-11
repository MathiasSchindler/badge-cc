# Universe 2026 Badger

Bare-metal firmware experiments for the RP2350-based GitHub Universe 2026
badge prototype.

## Repository contents

This repository contains project-owned firmware, the owned compiler, host
tools, tests and documentation. Local experiments (`experimental/`), Wi-Fi
credentials (`ssid-data/`), generated output (`build/` at any level), Pimoroni
checkouts (`pimoroni/`), vendor dependencies (`vendor/`), virtual environments
and editor-local settings are intentionally excluded.

The freestanding maze and owned compiler do not need the excluded SDK
checkouts. Optional SDK applications and vendor-data Wi-Fi builds require
their dependencies to be obtained separately. See
[vendor asset provenance and redistribution limits](docs/wifi-assets.md)
before packaging or distributing vendor-dependent firmware.
Historical documentation links into excluded local directories refer to
local evidence or dependencies, not files distributed in this repository.

The authoritative confirmed hardware and firmware facts are in
`docs/spec.md`. Values listed there as unknown must not be inherited from the
2025 badge without new evidence. The specification distinguishes manufacturer
schematic evidence from physical tests, including the CAP1208 touch
controller, radio wiring, and external memory.

## Build

Clone the repository (GitHub access is required while it is private):

```sh
git clone https://github.com/MathiasSchindler/badge-cc.git
cd badge-cc
```

For the standalone maze, build with LLVM and run the host tests:

```sh
make tilt-maze test-tilt-maze
```

Alternatively, build the owned compiler and use its freestanding toolchain:

```sh
make -C badge-compiler all
make tilt-maze BUILD_DIR=build/owned-maze \
    RP2350_CC='bash badge-compiler/scripts/owned-cc.sh' \
    RP2350_OBJCOPY="$PWD/badge-compiler/build/elf2bin" \
    RP2350_NM="$PWD/badge-compiler/build/badge-elf-info" \
    RP2350_SIZE="$PWD/badge-compiler/build/badge-elf-info"
```

The Phase 1 build requires:

- a host C compiler;
- Clang with the `arm-none-eabi` target;
- `ld.lld`;
- `llvm-objcopy`;
- `llvm-nm`; and
- `llvm-size`;
- CMake and Ninja for the optional SDK-based radio demos;
- the Arm GNU embedded toolchain for the optional SDK-based radio demos.

Run:

```sh
make
```

To build the standalone hardware diagnostic app separately, run:

```sh
make diagnostics
```

This produces `build/universe26/uf2/diagnostics.uf2`. It provides screen-based
tests for buttons, CAP1208 touch channels, accelerometer/gyroscope, power and
light sensing, infrared, LEDs, display patterns, external I2C, and a bounded
SRAM/read-only image check. Left/Right select pages; each page lists its
controls. The Buttons page keeps Left/Right navigation until Enter starts an
explicit test; hold Left+Right or Home for one second to end that test, then
release the buttons to resume browsing on the same page.
This target has no SDK, libc, or external target dependencies. Radio, PSRAM,
and other unexercised capabilities are explicitly identified on screen.
See `docs/diagnostics.md` for safety, coverage, and physical validation.
Run `make test-diagnostics` for the host-side tests.

To start the project-owned no-SDK/no-libc Wi-Fi bring-up work:

```sh
make wifi-bringup
```

This produces `build/universe26/uf2/wifi_bringup.uf2`, an opt-in radio
register probe with TFT feedback. Left (GPIO7) runs/retries the cold-reset,
gSPI test-register, bus-configuration, and backplane checks; Right (GPIO9)
turns the radio off. It does not load a firmware blob, associate, start an
AP, or exchange network packets yet. See [the Wi-Fi roadmap](docs/wifi.md)
for milestones, dependency boundaries, and the required physical test.
Run `make test-wifi-bringup` for host wire/probe tests.

For a working project-owned, no-SDK/no-libc USB feedback path:

```sh
bash src/host/tools/usb_live_run.sh BOOTSEL_SERIAL
```

This builds and installs `usb_live_report.uf2`, reads three fresh
checksummed reports directly from the running badge, and returns it to
BOOTSEL. The live interface and host-requested recovery have been
physically tested on a 2026 badge. See [the live USB guide](docs/usb-live.md)
for setup, identifiers, recovery timing, and report formats.
Append `wifi` to run the software radio probe, or `wifi-pio` for its
project-owned PIO comparison backend. Both return stage/error/register
values and a nonce-bound first-transaction trace. `verified` describes
report integrity; `probe_ok` separately indicates radio success.
On 2026-09-15 both backends passed the full register/backplane probe on
the attached 2026 badge after startup and receive-alignment fixes.

The next milestone now also works on the attached 2026 badge:

```sh
make usb-wifi-firmware test-wifi-firmware
bash src/host/tools/usb_live_run.sh BOOTSEL_SERIAL wifi-fw
```

This separate image downloads the CYW43439 firmware and NVRAM, verifies
every byte, queries the controller version and loads/checks CLM regulatory
data. Three independent final-image cycles passed with BOOTSEL recovery.
`firmware_ok` is separate from USB report integrity. The RP2350 code uses
no SDK or libc; vendor firmware/configuration are an explicit
[data exception with provenance and distribution caveats](docs/wifi-assets.md).
That startup-only image does not enable networking.
See [M1b evidence and limits](docs/wifi.md#m1b-physical-results).

For the separate project-owned two-badge SoftAP/station experiment:

```sh
make usb-wifi-link test-wifi-link
bash src/host/tools/usb_wifi_pair_run.sh AP_SERIAL STATION_SERIAL
```

Both serials must identify attached BOOTSEL badges. The runner flashes
sequentially, starts the AP first, then starts the station after AP
readiness. TFT screens show **AP HOST / CLIENT**, connection stages, peer
MACs and WPA2 authorization. Both badges have now passed in both roles,
including matching authorized peers and USB recovery. This remains
no-SDK/no-libc on the RP2350, with the same vendor-data exception.
This image tests association only. See [paired results](docs/wifi.md#paired-physical-results).

The next, separate image adds real static-IP UDP echo and measurements:

```sh
make usb-wifi-udp test-wifi-udp
bash src/host/tools/usb_wifi_pair_run.sh AP_SERIAL STATION_SERIAL udp
```

It resolves the peer with ARP, sends 100 UDP echo requests from **each**
badge, verifies the replies, and returns all RTT samples and loss counts
over USB. The TFT shows packet counts and the latest RTT in timer ticks;
the host converts samples using each badge's USB-frame clock calibration.
Three physical runs, including reversed roles, completed **600/600 echo
transactions with zero observed loss**. Per-direction mean RTT was about
8.2-9.2 ms, with occasional much longer tails. These are instrumented
application round trips, not pure radio or one-way latency.
See [UDP scope, measurements and limits](docs/wifi.md#static-ip-udp-echo-and-measurements).

Research into code running inside the radio is tracked in the separate
[CYW43439 controller-firmware plan](docs/cyw43-driver.md). Its milestones
cover a diagnostic extension, a standalone RAM-only proof of execution,
and possible later Wi-Fi replacement work without blocking the working
UDP experiments.

To reproduce the earlier USB bootloader readback feasibility test without an SDK or libc on the
badge, run `make usb-report-test` and follow [the USB report guide](docs/usb-report.md).
The host collector supplies a fresh challenge, runs the known-report
firmware, waits for BOOTSEL, and validates the RAM response. **The physical
test on the RP2350 A4 failed because the report RAM was cleared on return
to BOOTSEL.** This is a feasibility test, not a working feedback channel.
It does not run the radio probe. See the guide for the failed test evidence
and the now-working application-side USB replacement.

To build the WiFi connectivity demo separately, run:

```sh
make wifi-demo
```

To build the interactive MAC-address flash tool, run:

```sh
make wifi-mac-flash
```

This produces `build/universe26/uf2/wifi_mac_flash.uf2`. After joining WiFi,
the screen shows the assigned IP address, the WiFi MAC address, and the current
flash rate. Up starts the sequence, Down stops it, Left slows it, and Right
speeds it up.

This produces `build/universe26/uf2/wifi_demo.uf2`. The demo initializes the
display and CYW43 radio, joins the network configured in
`ssid-data/ssid.txt`, waits for a DHCP address, resolves `example.com`, and
opens a TCP connection to port 80. The screen reports each phase and ends with
`INTERNET OK` only after both DNS and TCP succeed.

To build the combined BLE scanner, beacon, and direct-peer lab, run:

```sh
make wireless-lab
```

This produces `build/universe26/uf2/wireless_lab.uf2`. Left and Right select
Scanner, Beacon, Peer, or Metrics. Enter performs the mode action; in Metrics,
Up requests the 7.5 ms/latency-0 profile and Down requests the
30 ms/latency-4 profile. See `docs/wireless-lab.md` for the payload, GATT
protocol, measured latency/loss results, and two-badge procedure.

To build the separate two-badge tilt Pong game, run:

```sh
make ble-tilt-pong
```

This produces `build/universe26/uf2/ble_tilt_pong.uf2`. Flash the same image
to two badges; they automatically elect roles, connect, and start play. Tilt
the badge left or right to move your bottom paddle. See
`docs/ble-tilt-pong.md` for the architecture, BLE protocol, coordinate
mapping, limitations, and two-badge test procedure.

To build the separate mixed-badge BLE-to-Wi-Fi experiment, run:

```sh
make wifi-handover
```

This produces `build/universe26/uf2/wifi_handover.uf2`. One badge hosts a
temporary Wi-Fi network; up to three other badges select its session over
BLE and receive credentials and unique local addresses. The app exchanges
four edge-paddle positions and measures UDP RTT/loss, including client-to-
client relay through the host. It is a networking foundation for four-player
Pong, not the full game yet. It uses only the shared TFT, radio, and GPIO7/9
buttons, without an accelerometer or touch dependency.
See `docs/wifi-handover.md` for compatibility, controls, trust limitations,
and the four-board test procedure. This is an optional SDK-based radio
target, not a change to the default no-libc firmware.

The credential file has this format:

```text
SSID: your-network-name
Pass: your-network-password
```

Credentials and generated build files are ignored. The WiFi UF2 embeds the
network password, so keep it private.

This builds the host-side UF2 converter and emits:

```text
build/universe26/uf2/hello_idle.uf2
build/universe26/uf2/screen_test.uf2
build/universe26/uf2/button_mapper.uf2
build/universe26/uf2/gpio_events.uf2
build/universe26/uf2/peripheral_probe.uf2
build/universe26/uf2/lower_mapper.uf2
build/universe26/uf2/gpio_monitor.uf2
build/universe26/uf2/imu_demo.uf2
build/universe26/uf2/tilt_breakout.uf2
build/universe26/uf2/tilt_maze.uf2
build/universe26/uf2/diagnostics.uf2
```

Target code is compiled freestanding for Cortex-M33 and linked without libc,
the Pico SDK, or a target runtime for the default images above. Optional radio
targets are an explicit exception: they use Pico SDK, CYW43 support, newlib,
and BTstack for BLE. `hello_idle` initializes `.data` and `.bss`,
writes a marker to uninitialized SRAM for debugger inspection, and then waits
without touching any board GPIO.

Use `make clean` to remove generated artifacts.

## Install

1. Disconnect the badge from USB.
2. Hold its Boot button while connecting USB.
3. Release Boot after the RP2350 drive appears.
4. Copy `build/universe26/uf2/hello_idle.uf2` to that drive.

The bootloader installs the image and reboots the badge. Repeat the procedure
to replace it with another UF2.

`hello_idle` intentionally has no visible output. Its purpose is to prove the
minimal image path without assuming any 2026 GPIO assignments. Confirming that
it executes requires a debugger or the next display milestone; USB boot mode
remains the recovery path in either case.

`screen_test` uses the display mapping physically validated by the working 2025
flight simulator and AVIF firmware. It performs one static CPU-driven update,
showing a border, diagnostic text, and four color bars. It does not configure
or read any button GPIO.

The two button-discovery images observe raw pad input levels without selecting
a GPIO function or enabling any output driver. `gpio_events` now monitors only
the five confirmed front-button pins using RP2350 hardware edge latches and
updates immediately with the control name, GPIO, pressed/released state, and
event number. `button_mapper` remains the guided discovery tool and prompts for
Left, Right, Enter, Up, and Down in five-second windows.

`peripheral_probe` is a historical external GPIO4/5 I2C probe. It lists
responding addresses, checks an MSA301 candidate at `0x26`, and watches the
input registers of a QwST/TCA9555-style gamepad candidate at
`0x21`/`0x23`/`0x25`/`0x27`. These are not the onboard 2026 IMU/touch
devices: the schematic identifies an LSM6DS3TR-C and CAP1208 on GPIO18/19.
Read `docs/peripheral-probe.md` before flashing it.

`lower_mapper` is the historical guided GPIO test for the eight lower game
controls. It does not configure or read the schematic-confirmed CAP1208 touch
channels. See `docs/lower-mapper.md` for the original candidate set and its
limitations.

`gpio_monitor` is a responsive high-resolution table for GPIO0 through GPIO22.
It uses sticky hardware edge flags, per-pin millisecond timestamps and counts,
and partial-row screen updates. See `docs/gpio-monitor.md` for interpretation
and the accelerometer-interrupt test.

`imu_demo` enables the shared `3V3_SW` rail, accesses the confirmed
LSM6DS3TR-C over the internal GPIO18/19 I2C bus, and continuously displays
acceleration in mg plus raw gyroscope samples. A lower-right-quadrant
wireframe shows the filtered device tilt with red X, green Y, and blue Z axes.
See `docs/imu-demo.md` for the hardware contract, visualization limitations,
and physical validation procedure.

`tilt_breakout` is a separate accelerometer-controlled brick-breaking game.
Left/right tilt controls paddle direction and speed; a neutral-to-forward tilt
launches the ball after boot, a miss, or a cleared board. See
`docs/tilt-breakout.md` for controls and gameplay details.

`tilt_maze` is a separate accelerometer-controlled procedural marble maze.
Enter calibrates and starts/restarts, or advances after clearing a level;
Home resets progression. Levels become denser and introduce pits and fire.
Hazard contact returns to the level start with unlimited retries and a
continuing timer. A compact header leaves nearly the whole display for play.
Tilt accelerates the marble, opposite tilt brakes, and thin walls allow sliding.
Build with `make tilt-maze` and run host tests with `make test-tilt-maze`.
See [Tilt Maze](docs/tilt-maze.md) for controls, implementation and validation.

`wireless_lab` is an SDK-based BLE exploration firmware combining active
scanning, non-connectable advertising, direct GATT peer messaging, and
application-level loss/RTT measurement. It elects complementary
central/peripheral roles when two identical badges enter Peer or Metrics mode.
See `docs/wireless-lab.md`.

`ble_tilt_pong` is a separate SDK-based two-player game. The lower Bluetooth
address is authoritative, while each badge runs display, IMU, and gameplay
work on core 1 so CYW43/BTstack activity remains isolated on core 0. It does
not change `wireless_lab`. On 2026-09-14 the owner reported two-badge gameplay
with great speed and no significant perceived lag. This was a human
observation, not an instrumented latency measurement; see
`docs/ble-tilt-pong.md`.

## Source provenance

The startup approach, RP2350 picobin metadata, memory layout, and UF2 conversion
were adapted from the project owner's 2025 implementation at
`/Users/mathiasschindler/badger`. No code from that repository's `vendor`
subdirectory is used.
