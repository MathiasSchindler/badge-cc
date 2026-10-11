# Universe 2026 Badger Confirmed Specification

## Purpose and interpretation

This document is the authoritative record of facts confirmed on the GitHub
Universe 2026 badger prototype available to this project as of 2026-09-15.

For subsequent source-radio results, device roles, testing gates and next
steps, use the [current CYW43439 handoff](cyw43/handoff.md). Those later
experiments do not change the schematic-versus-physical distinction here.

Future developers and language models must observe these rules:

- A fact under **Confirmed** may be used without guessing.
- A fact under **Observed but not electrically mapped** confirms physical
  presence or behavior, not its GPIO or protocol.
- A fact under **Unknown** must not be inferred from the 2025 badge.
- The 2025 repository at `/Users/mathiasschindler/badger` is a valid source for
  reusable project-owned code and techniques. Its non-display GPIO assignments
  are not specifications for the 2026 prototype.

## Confirmation levels

| Level | Meaning |
| --- | --- |
| Physically confirmed | Firmware was installed and the expected behavior was observed on the 2026 prototype. |
| Schematic confirmed | The manufacturer schematic or pinout establishes a component or connection; operation on the assembled prototype is not implied. |
| Build confirmed | The generated artifact was inspected or reproduced on the development host. |
| Observed | A physical control or component is visibly present, but its electrical interface is not mapped. |
| Unknown | No reliable evidence has established the value. |

Schematic evidence below refers to the five-sheet
[manufacturer schematic](../pimoroni/github_badge_2026_schematic.pdf), generated
2026-09-03, and the accompanying
[RP2350 pinout](../pimoroni/Screenshot%202026-09-03%20at%2012.24.33.png).
Sheet references distinguish documented board design from physical tests.

## Confirmed processor and firmware target

| Property | Confirmed value | Evidence |
| --- | --- | --- |
| Microcontroller family | Raspberry Pi RP2350 | Prototype description and successful RP2350 UF2 boot |
| Microcontroller variant | RP2350B | Schematic sheet 2, MCU1 |
| Firmware ISA | Arm Cortex-M33, Thumb | Build-confirmed ELF and successful physical boot/display |
| RP2350 execution image | Arm Secure | Picobin image type `0x1021`; physical boot confirmed |
| Flash execution base | `0x10000000` | UF2 target and linked image physically boot |
| UF2 family ID | `0xe48bff59` | Generated RP2350 Arm Secure UF2 physically accepted |
| Default firmware runtime | Freestanding; no libc, Pico SDK, or operating-system runtime | Build inspection |
| Optional SDK radio runtime | Pico SDK, CYW43 support, and newlib; BLE targets also use BTstack | SDK build configuration and ELF inspection |
| Project-owned radio runtime | Freestanding no-SDK/no-libc host code; vendor firmware/NVRAM/CLM data exception | M1b and paired-link builds, physical validation |

The freestanding linker reserves the following RP2350 address ranges:

| Region | Start | Size |
| --- | ---: | ---: |
| Main SRAM | `0x20000000` | 512 KiB |
| Scratch X | `0x20080000` | 4 KiB |
| Scratch Y | `0x20081000` | 4 KiB |
| Initial stack top | `0x20082000` | N/A |

The freestanding linker currently allows up to 4 MiB from `0x10000000`.
This is a software build limit, not the schematic flash capacity. The SDK
board configuration declares 16 MiB. Neither setting is a physical capacity
measurement.

## Schematic-confirmed external memory

| Property | Schematic value | Evidence |
| --- | --- | --- |
| Flash | U1, W25Q128JVPIQ, 16 MiB (labeled 16 MB) | Sheet 2 |
| Flash interface | Dedicated QSPI clock, data, and chip-select signals | Sheet 2 |
| PSRAM | U18, APS6404L-3SQR-ZR, 8 MiB (labeled 8 MB) | Sheet 2 |
| PSRAM interface | Shares QSPI clock/data with flash; separate `PSRAM_CS` on GPIO8 | Sheet 2 and RP2350 pinout |

These are the documented parts and capacities, not JEDEC-ID readbacks or
full-range memory tests on the prototype. The freestanding firmware does not
initialize PSRAM. GPIO8 must not be treated as an unassigned probe/output pin.

## Confirmed USB boot and installation

1. Disconnect the badge from USB.
2. Hold the physical Boot button.
3. Connect USB while continuing to hold Boot.
4. The RP2350 bootloader presents a host-visible drive.
5. Copy an RP2350 UF2 file to the drive.
6. The badge installs the image and reboots into it.
7. Repeat this procedure to replace the installed firmware.

This recovery path was physically confirmed before and after installing custom
firmware.

### Project-owned application USB report interface

On 2026-09-14, `usb_live_report.uf2` was physically tested on the RP2350
A4 2026 badge with chip/USB serial `08DA26F86E57F5EA`. The application
enumerated as a full-speed vendor-specific USB device, accepted nonce-tagged
requests, and returned 64-byte reports with verified build identity, nonce,
CRC, and known payload. Repeated collection, malformed-request rejection,
endpoint recovery, and return to BOOTSEL were observed.

This is a separate project-owned no-SDK/no-libc control-endpoint image, not
USB support in every freestanding application. The test VID/PID `cafe:4026`
is lab-only, not a production allocation. See [the USB guide](usb-live.md)
for protocol and limits. No-collector recovery was observed after about
18 host seconds; the ROM-requested delay is not a calibrated timebase.

The previous attempt to retain a report in main SRAM across BOOTSEL reboot
failed because the readback was zero; [the failed-test record](usb-report.md)
is kept separately. Live reports avoid that retention requirement.

On 2026-09-15 another 2026 badge, serial `9BBF88DD17D66E7D`, also
successfully returned live USB reports and first-radio-transaction trace
pages. Device selection used its current chip/USB serial, not a remembered
port. Both project-owned software and PIO radio register engines then
passed: test register `0xfeedbead` in reset and normal ordering, sixteen
repeated normal reads, bus control `0x000200a3`, ALP CSR `0x48`, and
stable chip-common ID `0x1545a9af`. See [the radio investigation](wifi.md).
This is register/backplane evidence, not firmware loading or Wi-Fi
network operation; a full unplug/replug validation of the fixed image
remains separate from the repeated radio power-reset tests.

The separate M1b image subsequently passed firmware startup on that same
badge. Build `ab16f8f1` completed three independent flash/run/report/BOOTSEL
cycles: all 224,192 padded firmware bytes and 744 NVRAM bytes compared
correctly, the NVRAM token read back, HT/function-2 became ready, and the
controller returned version `7.95.49 (2271bb6 CY)`, FWID `01-c51d9400`.
CLM upload acknowledgements and `clmload_status=0` passed. This is
**controller startup/control evidence, not Wi-Fi networking**.
The project-owned RP2350 image links with no SDK or libc, but includes
explicit vendor binary/configuration data. See [M1b results](wifi.md#m1b-physical-results)
and [asset provenance/distribution limits](wifi-assets.md).

With both badges attached, the same M1b image also passed on
`08DA26F86E57F5EA`; both were confirmed back in BOOTSEL. The two-device
[baseline and planned roles](wifi.md#two-badge-usb-setup-confirmed) are
recorded separately from subsequent badge-to-badge networking.

The separate paired-link image then established channel-1 WPA2/AES
SoftAP/station associations on these two 2026 badges. Final build
`ed36aed2` passed with either badge acting as the AP: both sides reported
the other's expected MAC and `sta_info` association/authorization flags
for twenty consecutive host observations, followed by BOOTSEL recovery.
The TFT identifies each role and displays progress/peer/authorization
state. These are control-plane link results, not application IP/UDP data
or latency measurements. See [paired physical results](wifi.md#paired-physical-results).

The following project-owned UDP image (`4c6ff5b1`) then passed real
ARP/static-IPv4/UDP echo over that link. Three two-badge runs, including
reversed roles, completed 100 echo requests per direction per run:
**600/600 transactions**, no observed losses, rejected frames or radio
receive-queue drops. Mean application RTT ranged from 8.232 to 9.241 ms
across the six directional results; the largest observed RTT was 67.824 ms.
Each badge's inherited timer was calibrated against 128 USB SOF intervals.
This is a short, nearby-badge, instrumented test, not a crowded-RF latency
guarantee or a one-way measurement. See [the full UDP evidence](wifi.md#static-ip-udp-echo-and-measurements).

## Confirmed UF2 contract

The project's UF2 files use:

| Field | Value |
| --- | ---: |
| Block size | 512 bytes |
| Payload size per block | 256 bytes |
| First target address | `0x10000000` |
| Address increment | 256 bytes |
| Flags | `0x00002000` (`FAMILY_ID_PRESENT`) |
| Family ID | `0xe48bff59` |
| Start magic 0 | `0x0a324655` |
| Start magic 1 | `0x9e5d5157` |
| End magic | `0x0ab16f30` |
| Unused payload padding | `0xff` |

The raw binary begins with the vector table, followed by the first picobin
block. A linked picobin end block terminates the flash-backed image.

Physical evidence:

- The one-block, 512-byte `hello_idle.uf2` was accepted and rebooted the
  prototype.
- The seven-block `screen_test.uf2` was accepted, rebooted, and executed
  correctly.

## Confirmed display

### Panel behavior

| Property | Confirmed value |
| --- | --- |
| Physical resolution | 320 x 240 pixels |
| Interface | 8-bit parallel write bus |
| Controller command compatibility | ST7789 command set used by this project |
| Pixel format | RGB565, 16 bits per pixel |
| Pixel byte order | Most-significant byte first |
| Orientation value | `MADCTL = 0x90` |
| Column window | `CASET = 0, 0, 0, 239` |
| Row window | `RASET = 0, 0, 1, 63` (0 through 319) |
| Logical traversal used by working code | X from 0 through 319 outside; Y from 0 through 239 inside |

The exact display-controller silicon marking has not been read. The
specification therefore confirms ST7789 command compatibility, not the printed
part number.

### Working display and power GPIO map

Every signal in this table was configured by `screen_test.uf2`, which rendered
the expected complete image on the physical 2026 prototype. Signals required
to transfer or visibly present the image are marked functionally confirmed.
The schematic also maps these signals on sheets 2 and 5. A connection shown
there does not by itself prove an unexercised operation such as LCD reads.

| Signal | RP2350 GPIO | Direction | Working idle/initial state | Confidence |
| --- | ---: | --- | --- | --- |
| LCD backlight | 26 | Output | Low during initialization; high after first complete frame | Functionally confirmed |
| LCD chip select | 27 | Output | High when idle | Functionally confirmed |
| LCD data/command (`DC`, also called `RS`) | 28 | Output | High for pixel data | Functionally confirmed |
| LCD write strobe | 30 | Output | High when idle | Functionally confirmed |
| LCD read strobe | 31 | Output | High; reads are not currently used | Schematic confirmed; read operation not physically tested |
| LCD data bit 0 | 32 | Output | Parallel data | Functionally confirmed |
| LCD data bit 1 | 33 | Output | Parallel data | Functionally confirmed |
| LCD data bit 2 | 34 | Output | Parallel data | Functionally confirmed |
| LCD data bit 3 | 35 | Output | Parallel data | Functionally confirmed |
| LCD data bit 4 | 36 | Output | Parallel data | Functionally confirmed |
| LCD data bit 5 | 37 | Output | Parallel data | Functionally confirmed |
| LCD data bit 6 | 38 | Output | Parallel data | Functionally confirmed |
| LCD data bit 7 | 39 | Output | Parallel data | Functionally confirmed |
| Switched 3.3 V rail enable (`3V3_SW`) | 41 | Output | High | Schematic confirmed; display operation physically confirmed |

The working direct-GPIO driver configures these pins for SIO output with the
pad value `0x21` (8 mA drive and fast slew in the RP2350 pad register).

GPIO41 controls the AP22802 load switch that supplies `3V3_SW`. It must be
driven high before accessing peripherals on that rail, including the display,
LSM6DS3TR-C, capacitive-touch controller, and IR receiver. This is a shared
peripheral power rail, not an IMU-only enable.

Sheet 5 connects panel `TE` to `LCD_VSYNC` on GPIO21. This is a
schematic-confirmed display output/RP2350 input; timing and firmware use remain
untested. Panel reset is an RC network on `3V3_SW`, not a dedicated RP2350
reset GPIO. The AP2502 backlight driver is enabled by GPIO26.

### Working initialization sequence

The physically validated implementation performs the following sequence:

1. Drive power enable high.
2. Keep backlight low.
3. Configure control and data GPIOs.
4. Send `SWRESET (0x01)`.
5. Wait 6,000,000 CPU delay-loop iterations.
6. Send:
   - `COLMOD (0x3a)`: `05`
   - `PORCTRL (0xb2)`: `0c 0c 00 33 33`
   - `LCMCTRL (0xc0)`: `2c`
   - `VDVVRHEN (0xc2)`: `01`
   - `VRHS (0xc3)`: `0f`
   - `VDVS (0xc4)`: `20`
   - `PWCTRL1 (0xd0)`: `a4 a1`
   - `FRCTRL2 (0xc6)`: `0f`
   - `RAMCTRL (0xb0)`: `00 c0`
   - `GCTRL (0xb7)`: `35`
   - `VCOMS (0xbb)`: `1b`
   - `GMCTRP1 (0xe0)`:
     `f0 00 06 04 05 05 31 44 48 36 12 12 2b 34`
   - `GMCTRN1 (0xe1)`:
     `f0 0b 0f 0f 0d 26 31 43 47 38 14 14 2c 32`
7. Send `INVON (0x21)`.
8. Send `SLPOUT (0x11)`.
9. Wait 4,000,000 CPU delay-loop iterations.
10. Set the full display window and `MADCTL = 0x90`.
11. Send `RAMWR (0x2c)` and exactly 320 x 240 RGB565 pixels.
12. Send `DISPON (0x29)`.
13. Drive the backlight high.

### Physical display tests

`screen_test.uf2` displayed all of the following correctly:

- `GITHUB UNIVERSE`
- `2026`
- `DISPLAY OK`
- `320 X 240`
- four bottom color blocks in green, blue, purple, and mustard-yellow

The 2025 `flight_sim.uf2` and AVIF decoder/viewer UF2 files also run and render
correctly on the 2026 prototype. This confirms compatibility with both the
minimal direct-GPIO path and the more advanced 2025 graphics path.

## Confirmed front physical buttons

The guided mapper physically observed:

| Control | RP2350 GPIO | Active level | Working pull | Evidence |
| --- | ---: | --- | --- | --- |
| Left | 7 | Low | Weak internal pull-up | Guided mapping and named real-time press/release test |
| Right | 9 | Low | Weak internal pull-up | Guided mapping and named real-time press/release test |
| Enter symbol | 10 | Low | Weak internal pull-up | Guided mapping and named real-time press/release test |
| Up | 11 | Low | Weak internal pull-up | Guided mapping and named real-time press/release test |
| Down | 6 | Low | Weak internal pull-up | Guided mapping and named real-time press/release test |

The real-time monitor classified falling edges as `PRESSED`, rising edges as
`RELEASED`, and accumulated pairs as `TAP`; all five controls worked physically.

The first GPIO0-22 monitor also reported GPIO15 after every front-button
event. This was initially attributed to display traffic because the monitor
redrew each button row before checking GPIO15. Schematic sheet 3 now
establishes `SWITCH_INT` on GPIO15 as the diode-combined signal from all five
front buttons (`SW_A`, `SW_B`, `SW_C`, `SW_UP`, and `SW_DOWN`). A pressed
button can pull this common line low when it is pulled up.

The existing monitor suppresses display-correlated edges; that heuristic is
not evidence that GPIO15 is display noise and can suppress genuine combined
button events. Dedicated validation of the shared interrupt behavior remains
separate from the physically validated individual button inputs.

## Confirmed rear physical controls

| Control | RP2350 GPIO | Active level | Working pull | Evidence |
| --- | ---: | --- | --- | --- |
| Home / USB Boot (`USER_SW`) | 22 | Low in schematic; not separately measured | Weak internal pull-up used by monitor; schematic has external pull-up | Physical GPIO mapping; sheets 2 and 3 establish the switch circuitry |

Sheet 2 shows SW1 pulling `USER_SW` low, with a diode connection to flash
chip select for USB boot entry. Sheet 3 shows the separate Reset switch SW2
pulling `RESET_SW` low, with an external pull-up and a 74LVC1G57 short-pulse
generator driving `RUN`. `RESET_SW` is also connected to GPIO14 on sheet 2.
Resetting the processor is physically observed; using GPIO14 to monitor the
switch is not physically validated and must not be treated as an ordinary
front-button input.

## Confirmed external I2C connector

| Property | Confirmed value | Evidence |
| --- | --- | --- |
| SDA | GPIO4 | Physically gated open-drain probe using the Tufty reference mapping |
| SCL | GPIO5 | Physically gated open-drain probe using the Tufty reference mapping |
| Connector | J3 Qw/ST; SDA pin 3, SCL pin 4 | Schematic sheet 3 |
| Connector supply and pulls | `3V3_SW`; 10 kOhm pull-up on each bus line | Schematic sheet 3 |
| Idle state | Both lines high | Required gate passed before scanning |
| Connected devices | None on the tested prototype | Complete 7-bit scan from `0x08` through `0x77` returned no acknowledgements |

The rear connector is visibly labeled I2C and is not connected to another
board. This external GPIO4/5 bus is not the internal connection for the lower
controls or accelerometer on the tested prototype.

## Confirmed inertial measurement unit

The manufacturer-supplied schematic identifies the motion sensor as an
STMicroelectronics `LSM6DS3TR-C`, which contains a three-axis accelerometer and
three-axis gyroscope.

| Property | Confirmed value | Evidence |
| --- | --- | --- |
| Part | LSM6DS3TR-C | Manufacturer schematic |
| Supply | `3V3_SW`; enable by driving GPIO41 high | Manufacturer schematic and instructions |
| Interface | I2C, shared internal bus | `CS` tied high in manufacturer schematic |
| I2C SDA | GPIO18 | Manufacturer pinout and instructions |
| I2C SCL | GPIO19 | Manufacturer pinout and instructions |
| I2C address | `0x6a` | `SDO/SA0` tied low in manufacturer schematic |
| `WHO_AM_I` register | `0x0f`, expected value `0x6a` | ST LSM6DS3TR-C register driver |
| Interrupt | GPIO20 (`IMU_INT`) | Manufacturer pinout/schematic |
| Accelerometer demo mode | 104 Hz, +/-2 g, 0.061 mg/LSB | `CTRL1_XL = 0x40` |
| Gyroscope demo mode | 104 Hz, 245 dps | `CTRL2_G = 0x40` |

The internal I2C bus is also connected to the capacitive-touch controller and
has 10 kOhm pull-ups to `3V3_SW`. Firmware must therefore use open-drain bus
signaling and must not assume exclusive ownership of the bus.

Physical testing of `imu_demo.uf2` confirmed identity `0x6a` and continuously
changing accelerometer and gyroscope samples. One stationary observation
reported approximately X = -17 mg, Y = -93 mg, and Z = -958 mg. The vector
magnitude is approximately 963 mg, consistent with gravity plus ordinary
sensor offset and measurement tolerance. This confirms sample decoding and
the configured +/-2 g scale.

The physically observed accelerometer orientation is:

| Badge pose | Dominant acceleration |
| --- | --- |
| Lying flat, screen facing up | Z negative, approximately -1 g |
| Lying flat, screen facing down | Z positive, approximately +1 g |
| Right edge tilted down | X positive |
| Left edge tilted down | X negative |
| Near edge tilted toward the holder | Y positive |
| Far edge tilted away from the holder | Y negative |

## Confirmed capacitive-touch controller

| Property | Schematic value | Evidence |
| --- | --- | --- |
| Controller | U10, CAP1208 | Sheet 1 |
| Supply | `3V3_SW`, enabled by GPIO41 | Sheets 1, 2, and 4 |
| Bus | Shared internal I2C, GPIO18 SDA / GPIO19 SCL | Sheets 1 and 2 |
| Bus pull-ups | 10 kOhm on each line to `3V3_SW` | Sheet 3 |
| Alert | `CAP_ALERT` on GPIO13 | Sheets 1 and 2 |
| Touch channels | CS1 through CS8 connect to `CAP1` through `CAP8` and TP1 through TP8 | Sheet 1 |

The supplied symbol does not establish the configured I2C address, register
protocol, alert polarity, or initialization procedure. These require the
part documentation and prototype validation. The built-in controls are not
a TCA9555/QwSTPad on the external GPIO4/5 connector.

The new [diagnostic app](diagnostics.md) implements the separate datasheet
contract: CAP1208-1/-2 addresses `0x28`/`0x29`, product ID `0x6b`, manufacturer
ID `0x5d`, configuration readback, touch/status reads, and interrupt clearing.
The owner subsequently confirmed that all eight touch sensors work and
supplied their physical locations on 2026-09-14. This confirms channel
operation and the mapping below, but the installed variant/address and
interrupt behavior were not separately reported.

### Physically confirmed touch-channel mapping

Positions are viewed from the front of the badge, matching the owner's
supplied layout image and channel-by-channel description.

| Channel | Status bit | Physical control | Diagnostic label |
| --- | --- | --- | --- |
| CS1 | Bit 0 (`0x01`) | Up square in the directional cluster | `UP` |
| CS2 | Bit 1 (`0x02`) | Right square in the directional cluster | `RIGHT` |
| CS3 | Bit 2 (`0x04`) | Down square in the directional cluster | `DOWN` |
| CS4 | Bit 3 (`0x08`) | Left square in the directional cluster | `LEFT` |
| CS5 | Bit 4 (`0x10`) | Right of the two slash-shaped controls | `SLASH RIGHT` |
| CS6 | Bit 5 (`0x20`) | Left slash-shaped control, next to the round controls | `SLASH LEFT` |
| CS7 | Bit 6 (`0x40`) | Upper-right round control, next to CS8 | `ROUND RIGHT` |
| CS8 | Bit 7 (`0x80`) | Lower-left round control | `ROUND LEFT` |

All eight were reported working. A/B and Select/Start assignments for the
round and slash-shaped controls are not established by this report; use
physical-position names rather than assuming game-specific meanings.
This confirmation does not establish simultaneous-touch limits, measured
sensitivity, false-positive rates, or GPIO13 interrupt timing.

## Confirmed wireless path

Schematic sheet 3 identifies the radio module as A1, RM2. The working SDK
firmware uses CYW43439 support. Its board pin definitions agree with the
2026 schematic; these assignments are not inferred from the 2025 badge.

| Signal | RP2350 GPIO | Schematic connection |
| --- | ---: | --- |
| `WL_ON` | 23 | RM2 `WLON` and `BTON` |
| `WL_D` | 24 | Shared module data/IRQ path |
| `WL_CS` | 25 | Module chip select |
| `WL_CLK` | 29 | Module serial clock |

RM2 is supplied from the always-on 3.3 V path through the `VDDBAT` net;
GPIO41 is not its power enable. `CHARGE_STAT` connects to the module's
GPIO2, not RP2350 GPIO2.

Physical tests recorded in [Wireless Lab](wireless-lab.md) confirm
advertising, role election, GATT discovery, and messages in both directions.
Adjacent-badge 7.5 ms/latency-0 tests recorded average application RTTs of
14 ms from the central and 13 ms from the peripheral, with 100/100
acknowledgements in each run. These samples do not establish zero loss in
general.

On 2026-09-14 the owner reported playing [BLE tilt Pong](ble-tilt-pong.md)
on two badges with great speed and no significant perceived lag. This is
physical gameplay evidence, not a numerical latency measurement or
verification of the negotiated connection parameters for that playtest.

## Schematic-confirmed lighting, infrared, and sensing

The following connections are established by the schematic, not by dedicated
peripheral tests:

| Function | RP2350 GPIO | Schematic evidence |
| --- | ---: | --- |
| Four white case LEDs | 0, 1, 2, 3 | `CL0` through `CL3`, each through 330 Ohm to an LED and ground; active high (sheets 2 and 3) |
| Infrared transmit | 16 | `IR_TX` drives a DMN3731U low-side MOSFET and two series IR LEDs from 3.3 V; active high (sheets 1 and 2) |
| Infrared receive | 17 | `IR_RX` from VSOP38338 with PD70-01B/TR7 photodiode, powered by `3V3_SW` (sheets 1 and 2) |
| USB supply detection | 12 | `VBUS_DETECT`, nominal equal 5.1 kOhm divider from VBUS (sheets 2 and 4) |
| Battery voltage sense | 40 / ADC0 | `VBAT_SENSE`, equal 750 kOhm divider with 100 nF filter (sheets 2 and 4) |
| Core supply sense | 42 / ADC2 | Connected to `1V1`; not an unassigned output (sheet 2) |
| Ambient light sense | 43 / ADC3 | `LIGHT_SENSE`, PT19-21C/L41/TR8 phototransistor on `3V3_SW` with 1 kOhm to ground (sheets 2 and 5) |

Sheet 4 also shows an MCP73831/2-family LiPo charger, a 2.2 kOhm charge
programming resistor (annotated nominal current 0.455 A), a red charge LED,
and XB6096I2S battery protection. Battery capacity, calibrated ADC conversion,
actual charging behavior, LED brightness, and IR timing/range have not been
physically established by these drawings. Do not infer tested operating
limits from a component connection alone.

## Earlier GPIO observations

### Historical lower-control probe

Before the internal bus was identified, the guided GPIO0-22 mapper observed
no events from ordinary lower-control
touches. Hard repeated impacts around Up and Down did produce an event, but the
GPIO number was not recorded. This is evidence of an impact-sensitive signal,
not yet evidence that either control is directly wired. Candidate explanations
include the accelerometer interrupt on GPIO20, board flex/contact noise, or the
shared touch-controller interrupt. These historical results do not contradict
the later confirmation that all eight CAP1208 channels work.

### Other GPIO observations

| GPIO | Observation | Interpretation |
| ---: | --- | --- |
| 13 | Raw pad level remained Low at monitor startup despite the enabled weak pull-up | Schematic-confirmed CAP1208 `CAP_ALERT`; cause of startup level untested |
| 17 | Edge activity occurred when USB was unplugged | Schematic-confirmed `IR_RX`; the observed transition does not make it a USB-detect signal |
| 20 | Raw pad level remained Low at monitor startup despite the enabled weak pull-up | Confirmed `IMU_INT`; interrupt polarity/configuration not yet validated |
| 21 | Raw pad level remained Low at monitor startup despite the enabled weak pull-up | Schematic-confirmed `LCD_VSYNC`/panel `TE`; timing/configuration untested |

GPIO17 and GPIO22 each showed isolated activity in the first responsive test.
Later testing identified GPIO22 as the rear Home button; the schematic
identifies GPIO17 as `IR_RX`. The cause of the isolated GPIO17 activity
remains untested.

## Unknown: do not guess

The following are explicitly unresolved:

- installed CAP1208 variant/address, separately validated alert behavior, and
  sensitivity/multi-touch characterization (all eight controls and their
  channel mapping are physically confirmed);
- accelerometer interrupt polarity/configuration (axis orientation is physically mapped);
- validated IR carrier/protocol, receiver polarity, and range;
- validated LED drive levels, brightness, and power consumption;
- display tearing-effect/vsync timing and readback behavior;
- wireless range, long-duration loss/stability, coexistence, and multi-badge partner selection;
- physical identification and full-capacity testing of the schematic flash and PSRAM;
- battery capacity, charge-status interpretation, and calibrated voltage/light sensing;
- physical electrical validation of the schematic Home/Reset and combined-button interrupt behavior; and
- any GPIO not listed in a confirmed mapping.

Do not copy values for these items from the 2025 project into 2026 firmware
without a new physical test or authoritative 2026 schematic/board source.

## Superseded discovery hypotheses

The MSA301 at `0x26` and TCA9555/QwST-style controls at
`0x21`/`0x23`/`0x25`/`0x27` were early hypotheses based on other Pimoroni
hardware. They are not the onboard motion/touch configuration in the 2026
schematic. `peripheral_probe.uf2` retains these historical checks on GPIO4/5,
the external connector bus. It cannot discover the onboard LSM6DS3TR-C or
CAP1208 on GPIO18/19. Use the confirmed internal-bus topology for further
touch work rather than driving speculative GPIO candidates.

## Reproducible confirmed artifacts

Run `make` at the repository root to produce:

| Artifact | Purpose | Physical status |
| --- | --- | --- |
| `build/universe26/uf2/hello_idle.uf2` | Minimal boot image; touches no board GPIO | Accepted and rebooted; intentionally no visible output |
| `build/universe26/uf2/screen_test.uf2` | Static display and color proof | Fully physically validated |
| `build/universe26/uf2/button_mapper.uf2` | Guided front-button mapping | Fully physically validated |
| `build/universe26/uf2/gpio_events.uf2` | Real-time front-button events | Fully physically validated |
| `build/universe26/uf2/peripheral_probe.uf2` | Gated GPIO4/5 I2C scan | Physically validated; no devices found |
| `build/universe26/uf2/lower_mapper.uf2` | Guided lower-control GPIO mapping | Physically tested; normal touches produced no events |
| `build/universe26/uf2/gpio_monitor.uf2` | Responsive GPIO0-22 edge monitor | Physical observations recorded; display-edge suppression is not proof that GPIO15 is noise |
| `build/universe26/uf2/imu_demo.uf2` | Live LSM6DS3TR-C accelerometer/gyroscope display | Identity and live samples physically confirmed |
| `build/universe26/uf2/tilt_breakout.uf2` | Tilt-controlled brick-breaking game | Build confirmed; physical gameplay test pending |
| `build/universe26/uf2/diagnostics.uf2` | Standalone screen-based peripheral diagnostics; see [coverage and controls](diagnostics.md) | Build and host tests confirmed; all eight touch channels reported working and mapped; other new peripheral tests remain pending |

The default freestanding build uses Clang for
`--target=arm-none-eabi -mcpu=cortex-m33 -mthumb`, `ld.lld` as the temporary
final linker, `llvm-objcopy` for raw binary extraction, and the project-owned
`bin2uf2` utility.

The separate SDK targets are not built by default:

| Build command | Artifact | Physical status |
| --- | --- | --- |
| `make wireless-lab` | `build/universe26/uf2/wireless_lab.uf2` | Two-badge discovery/messages and short-run RTT/loss measurements confirmed |
| `make ble-tilt-pong` | `build/universe26/uf2/ble_tilt_pong.uf2` | Owner reported two-badge gameplay with no significant perceived lag on 2026-09-14; no numerical gameplay latency measurement |
| `make wifi-handover` | `build/universe26/uf2/wifi_handover.uf2` | Separate four-badge BLE provisioning/SoftAP/UDP experiment; physical validation pending; see [mixed-board contract](wifi-handover.md) |

The separate freestanding `make wifi-bringup` target produces
`build/universe26/uf2/wifi_bringup.uf2` for the [project-owned Wi-Fi
roadmap](wifi.md). It checks radio register/bus access without an SDK,
libc, or radio firmware blob. Host tests are not physical confirmation:
the corrected software and optional PIO bus paths passed physical
register/backplane tests on the selected 2026 badge on 2026-09-15.
Firmware loading/network connectivity are not implemented by this probe.

The separate `make usb-wifi-firmware` target produces
`build/universe26/uf2/usb_wifi_firmware.uf2`. It adds verified firmware/NVRAM
download and version/CLM control queries over the project-owned PIO bus,
with live USB reporting. It is not part of `make all`; neither the default
images nor `wifi-bringup` embeds its vendor data. M1b physical startup
passed; this startup-only image does not implement station/AP/network
operation. Separate `make usb-wifi-link` and `make usb-wifi-udp` images
subsequently passed paired WPA2 association and 600/600 ARP/static-IP UDP
echoes as recorded above. They still use vendor CYW43439 firmware.

Source-radio research has separate images and evidence. See the
[current handoff](cyw43/handoff.md) for cold initialization/calibration,
vendor-retained checked PSM prefixes and unresolved source-only reception;
these do not replace the working vendor-firmware network or establish RF
qualification.

## Evidence locations

- Display implementation: `src/platform/badger/display.c`
- Static physical test: `src/tools/screen_test.c`
- Startup and vectors: `src/arch/rp2350/start.c`
- Picobin metadata: `src/arch/rp2350/picobin.S`
- Link layout: `src/arch/rp2350/memmap_flash.ld`
- UF2 generator: `src/host/tools/bin2uf2.c`
- Build contract: `Makefile`
- Manufacturer schematic: `pimoroni/github_badge_2026_schematic.pdf`
- Manufacturer pinout: `pimoroni/Screenshot 2026-09-03 at 12.24.33.png`
- IMU driver: `src/platform/badger/lsm6ds3.c`
- Live IMU demo: `src/tools/imu_demo.c`
- BLE transport tests and measurements: `docs/wireless-lab.md`
- Two-badge gameplay and qualitative playtest: `docs/ble-tilt-pong.md`
- Standalone diagnostic coverage and validation: `docs/diagnostics.md`
- Chronological context and observations: `docs/background.md`
- Bring-up sequence and next work: `docs/plan.md`
