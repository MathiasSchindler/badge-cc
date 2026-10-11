# Standalone badge diagnostics

Build and install:

```sh
make diagnostics
```

Copy `build/universe26/uf2/diagnostics.uf2` to the badge's USB boot drive.
The image is also included in the default `make` build.

This is freestanding C for the RP2350B. It uses the project's startup,
linker script, display, and bit-banged I2C code, plus project-owned
CAP1208, ADC, GPIO, PWM, and graphics support. It does not link libc,
Pico SDK, BTstack, CYW43 firmware, or any other external target dependency.
The host compiler and LLVM cross-toolchain remain build tools.

## Navigation and feedback

Left and Right change pages; Home returns to Overview. Enter performs the
action described at the bottom of the current page. Up and Down provide
page-specific actions.

The Buttons page opens in **browse mode**: Left/Right continue changing
pages and Home returns to Overview, just like elsewhere. Press Enter to
start an explicit button test. This clears press counts and seen history;
release Enter before testing.

Only while **button test mode** is active is navigation paused, allowing
all six buttons to be tested independently. Hold Left and Right together,
or hold Home, for one second to end the test. This returns to browse mode
on the same page, retaining the results rather than jumping to Overview.
Release all buttons before navigating again. The current mode and controls
are shown on screen.

The separate Reset button still reboots the badge; it is not captured as an
ordinary application button.

- Green indicates a successful read, detected input, or completed local test.
- Blue identifies informational state or a command sent to hardware.
- Amber indicates a limitation, pending human check, or unmeasured quantity.
- Red identifies an explicit initialization, communication, or conversion error.

There is no blanket "board passed" verdict. An LED output command is not an
optical measurement, generating IR does not prove reception, and successful
I2C communication does not validate every sensor function.

## Pages

| Page | Tests and feedback | Actions |
| --- | --- | --- |
| Overview | Internal bus, IMU, touch-controller, ADC, and IR-generator status | Enter retries peripheral initialization |
| Buttons | Left/Right/Enter/Up/Down/Home, pressed/released state, debounced counts, per-button seen history, combined GPIO15 level and edge flags | Left/Right browse normally; Enter starts a fresh test; hold Left+Right or Home for one second to end the test |
| Capacitive touch | Eight CS1-CS8 tiles labeled with confirmed physical positions, touch state, signed signal deltas, seen history, detected address/identity/revision, GPIO13 alert level | Enter retries; Up clears seen history |
| Motion sensor | Acceleration in mg with signed bars, gravity-vector magnitude, raw gyroscope X/Y/Z, GPIO20 level | Enter retries |
| Power and light | Battery/core voltage estimates, raw ADC values, light bar, GPIO12 VBUS state | Enter retries; cover the light sensor to observe changes |
| Infrared | Carrier estimate, commanded transmit trains, RX level/edge flags, timed pulse capture | Enter sends one short train; Up captures for 250 ms; Down clears results |
| Case lights | Four individually controlled white LEDs on GPIO0-3 | Enter advances one LED at a time, then off; Up starts a 10-second chase; Down turns all off |
| Display | Exact RGB565 primary-color bars, large and one-pixel checkerboards, white/black patterns, grayscale ramp, optional tearing-output edge flags | Enter changes pattern; Up toggles TE; Down blanks backlight for 500 ms |
| External I2C | Explicit GPIO4/5 read-probe scan of addresses `0x08` through `0x77`; responding addresses highlighted | Enter starts a scan |
| System memory | Walking-one, alternating-bit, and address-pattern checks in an owned 8 KiB SRAM buffer; read-only linked-image CRC32 | Enter runs checks |
| Test coverage | Functions deliberately not exercised by this image | Read the limitations before interpreting results |

Input is debounced for 20 ms. Sensor sampling is requested every 50 ms and
screen refresh every 100 ms, with earlier refresh on button presses.
These are scheduling targets, not measured frame rates. The renderer uses
RGB565 canvases and transfers only changed 16x16 tiles. It preserves the
existing display's column-major traversal and GPIO timings.

Sensor failures remain visible rather than displaying stale samples as
successful reads. Enter on Overview or a sensor page retries initialization.
Touch `NOT READY` is retried automatically because calibration and changing
channel status are normal transient conditions. It is shown in amber for
up to two seconds, then red if it persists. Noise-flagged touch channels
display `NOISE` instead of a valid delta/touch indication.
The bus and peripheral waits are bounded; other pages remain available after
a sensor error.

## Hardware contracts and precautions

Wiring comes from the [2026 specification](spec.md), not assumed 2025 pin
assignments.

### Touch and motion

The LSM6DS3TR-C and CAP1208 share GPIO18/19 with external pull-ups to
`3V3_SW`. Display initialization enables that rail using GPIO41. Touch
initialization verifies device identity before writing configuration; the
driver reports communication, identity, configuration, and sensor faults.

The [Microchip CAP1208 datasheet DS00001570C](https://ww1.microchip.com/downloads/en/DeviceDoc/00001570C.pdf)
documents CAP1208-1/-2 addresses `0x28`/`0x29` (p.10), product ID `0x6b`
and manufacturer ID `0x5d` (p.49), and status/delta/interrupt behavior
(pp.24-27). The driver probes only those two addresses, validates both IDs,
enables all eight channels, disables repeat/multiple-touch blocking and
power-button qualification, and reads configuration back. Thresholds and
sensitivity are preserved. Startup includes a 15 ms communications delay;
initial conversion/calibration can take 200 ms.

The driver brackets delta reads with status reads and retries changes at
most three times. A successful burst is not a simultaneous eight-channel
sample: the controller scans channels sequentially. INT is cleared only
after a complete read, using a fresh read/modify/write of main control.

The owner confirmed all eight touch sensors working on 2026-09-14 and
mapped their positions from the front of the badge:

| Channel | Control |
| --- | --- |
| CS1 | Up square |
| CS2 | Right square |
| CS3 | Down square |
| CS4 | Left square |
| CS5 | Right slash-shaped control |
| CS6 | Left slash-shaped control |
| CS7 | Upper-right round control |
| CS8 | Lower-left round control |

Tiles remain in channel order CS1-CS8, not a spatial replica of the badge;
each now shows its physical-position label. Round/slash names intentionally
avoid unconfirmed A/B or Select/Start assignments. Touch each control and
check its labeled tile. Retry initialization with pads untouched if
calibration or startup needs investigation.

The IMU retains its existing 104 Hz, +/-2 g and 245 dps setup. The GPIO20
display is only an input-level observation; the app does not configure
motion-triggered interrupts or claim a gyro calibration.

### Power sensing

The ADC uses the crystal clock without changing the system clock. If the
oscillator is not already enabled, it is started in the 1-15 MHz range with
a bounded stability wait. Only the mapped analog pins GPIO40, GPIO42, and
GPIO43 are configured for ADC use; GPIO41 remains the shared power output.
The first conversion after each channel selection is discarded.

Voltage estimates assume a 3300 mV ADC reference and a 12-bit conversion.
The battery estimate includes the schematic's equal 750 kOhm divider.
Core voltage is measured on GPIO42; light is shown as a raw ADC value,
not lux. These quantities are not calibrated. A missing battery may leave
the battery/charger node at a voltage that does not represent a usable cell.
The app does not infer battery presence, percentage, health, or capacity.

VBUS is a digital observation of GPIO12, not a USB-voltage measurement.
Unplugging USB can turn off or reboot the badge if battery power is absent.
GPIO41 is never switched off as a test because it also powers the screen
and internal sensors.

### Infrared

GPIO16 controls the transmitter's MOSFET; GPIO17 observes the receiver.
PWM slice 0 produces a nominal 38 kHz carrier at approximately one-third
duty cycle. Its counter rate is estimated against the existing microsecond
timer rather than assuming a boot-time system clock frequency.

Each Enter press requests twelve 600 us bursts with 5400 us gaps, lasting
about 72 ms overall. There is no held-button auto-repeat or continuous
transmission. GPIO16 is returned to SIO-low after each burst. The waveform
is a diagnostic stimulus, not a remote-control protocol or an authenticated
badge-to-badge message.

For a two-badge test, select Infrared on both, point their optical hardware
toward each other, start the 250 ms capture on the receiving badge, then
press Enter on the transmitting badge during that window. Alternatively
hold a remote-control button while capturing. Repeat if the windows did
not overlap. Reflections or a local transmitter may also be detected;
isolate the sender when testing range.

Idle edge counts use sticky hardware flags and are a **lower bound**:
multiple transitions can coalesce during screen or I2C work. The dedicated
250 ms capture polls without screen/I2C transfers, records transitions and
complete low-pulse widths, and discards partial pulses at the window edges.
It still is not a logic analyzer, protocol decoder, or calibrated latency
measurement. No reception is not by itself proof of defective hardware.

### Other diagnostics

LED chase ends after ten seconds. Leaving the LED page turns all case LEDs
off. Leaving the Display page disables TE. Backlight blanking automatically
ends after half a second.

The external scan is user-triggered because even a read can have side
effects on an attached device. Onboard sensors are on the other bus and are
temporarily paused during scanning. Completion, cancellation, or failure
restores the internal bus; restoration errors are visible. An empty external
connector is normal, not a failed badge.

GPIO15 is the diode-combined front-button interrupt. No display-noise
suppression is applied to it. GPIO8/PSRAM CS, radio GPIOs, unknown pins, and
the shared peripheral supply are not repurposed as test outputs.

The SRAM check modifies only its statically allocated scratch buffer, never
the running stack or another subsystem's storage. The flash CRC reads only
the linked image, without erasing or writing flash. It is an image
fingerprint without an expected reference value, not proof of flash
integrity or its full 16 MiB capacity.

## Coverage limits

This image does **not** test Wi-Fi/Bluetooth operation or the charger-status
signal connected to radio-module GPIO2. That is an image-specific coverage
limit, not a claim that Wi-Fi requires an SDK: the separate
[freestanding Wi-Fi host](wifi.md) has working ARP/IPv4/UDP with vendor radio
firmware. [Wireless Lab](wireless-lab.md) remains a separate SDK/BTstack
Bluetooth app. Neither result establishes charger-status coverage here or
the [source-radio receiver](cyw43/handoff.md).

PSRAM is not initialized or capacity-tested. USB application data,
flash erase/write, whole-SRAM coverage, battery capacity, and calibrated
power consumption are also not tested. Bootloader installation/recovery
and Reset can be checked physically without claiming application USB
support. These limits are listed on screen, not reported as passing.

## Validation

```sh
make diagnostics
make test-diagnostics
```

Host tests use mocked peripheral access and address/undefined-behavior
sanitizers. They exercise touch-driver transactions, debouncing and timer
wrap, integer formatting/conversion, navigation and exit gestures, visible
error latching/retry, external-bus restoration, bounded LED chase, scratch
memory checks, and clipped/dirty-tile rendering. Host test binaries may use
the host C library; the UF2 does not.

The target build uses strict warnings, `-ffreestanding`, `-fno-builtin`, and
`-nostdlib`, with an undefined-symbol check at link time.

Host validation on 2026-09-14 passed the sanitized tests, all default
freestanding builds, and UF2 family/header/payload/vector checks. The
diagnostic image has no undefined symbols, and its include dependency
closure contains only project files. Mock-rendered previews of all eleven
pages were inspected; this validates layout, not the physical display path.

On 2026-09-14 the owner confirmed all eight capacitive-touch sensors working
and supplied the mapping above. The labels added after that report have
host-test/rendering coverage; this is not a second hardware test.

Other new peripheral paths still require physical validation: ADC readings
against a meter, the optical IR path, LED order, and TE output. The touch
report does not establish measured sensitivity, multi-touch behavior, or
GPIO13 interrupt timing. Earlier IMU/display and Pong observations do not
constitute a whole-app validation.

Register references: [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
for GPIO, ADC, PWM, clocks, and resets; the manufacturer schematic for all
board wiring. No vendor register headers or source files are build inputs
to this firmware.
