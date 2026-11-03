# Waveshare RP2350-USB-A: independent USB host development

This directory is for the **Waveshare RP2350-USB-A**, not the Universe badge.
The eventual goal is an owned PIO USB host for a mouse, then the Logitech
keypad, with events relayed to the badge over a separate connection.

**Current milestone: experimental full-speed PIO transport and a bounded
USB device-descriptor probe, with passive diagnostics by default.**
The 144 MHz clock, active empty-port reset/send/timeout and passive recovery
are physically verified. A directly connected keypad also returned the first
eight device-descriptor bytes at full speed, but its OUT status stage keeps
returning NAK: **complete enumeration is not yet working**.
There is no HID driver, hub support,
keypad input or LCD output yet. The Mac diagnostic connection uses the
separate native USB controller; it does not host the peripheral on behalf
of the Waveshare board.

The target is freestanding C without Pico SDK, TinyUSB, Pico-PIO-USB, libc,
or downloaded implementation dependencies. It reuses project startup, ROM
recovery, clock setup, USB device control and CRC/byte helpers.
The Mac monitor uses the built-in IOKit and CoreFoundation frameworks;
its timed USB transport is also shared with the badge replay tool.

## Board wiring and prerequisites

Sources: [manufacturer wiki and FAQ](https://www.waveshare.com/wiki/RP2350-USB-A)
and [schematic](https://files.waveshare.com/wiki/RP2350-USB-A/RP2350-USB-A.pdf).

| Connection | Wiring |
| --- | --- |
| USB-C | Native RP2350 USB D+/D-, with fixed USB-C sink CC resistors |
| USB-A D+ | GPIO12 through R12, 27 ohms |
| USB-A D- | GPIO13 through R11, 27 ohms |
| R13 | 1.5 kilohms from D+ to 3.3 V, a device-mode pull-up |
| R10 | Optional D- pull-up, marked NC on the schematic |
| USB-A VBUS | Connected to USB-C VBUS and VSYS |
| Flash / oscillator | 2 MiB / 12 MHz |

The manufacturer specifically says to remove R13 for low-speed devices
and host hotplug. Whether R13 is populated on this particular board has
**not** been visually verified. With the mouse unplugged, D+ still reads
high despite the diagnostic pull-down, strongly supporting a board-side
pull-up consistent with R13. See the unplugged comparison below.

R13 removal is **not established as a prerequisite for every host
experiment**. With a permanently connected full-speed peripheral, its
pull-up and R13 both bias D+ high. Such a setup can be investigated
without removal, although the extra pull-up is not standard host wiring
and electrical compliance remains unverified. It cannot
reliably detect an empty port from that idle state. Low-speed peripherals
instead pull D- high; with R13 present, both lines can then be high.
Omitting hotplug does not solve that separate low-speed problem.

Do not treat this board as a protected, independently switched USB power
source. The schematic does not show a dedicated USB-A current-limit/power
switch. The 500 mA regulator specification concerns **3.3 V**, not an
allocation of 500 mA of peripheral VBUS current. Avoid externally feeding
USB-A VBUS while USB-C is connected to the Mac. A powered hub is not
automatically a safe power fixture and would also need a future hub driver.

Do not flash the badge display firmware on this board: its GPIO wiring
and flash capacity differ.

## Build and tests

From the repository root, with the existing Clang/LLVM tools and macOS SDK:

```sh
make -C logi-host all test
```

Outputs:

- `build/logi-host/logi-host.uf2`: Waveshare-only ARM Secure UF2.
- `build/logi-host/logi-host.elf`: debugger image and symbols.
- `build/logi-host/logi-host-monitor`: native Mac EP0 diagnostic collector.

The firmware uses `-ffreestanding -nostdinc -fno-builtin -nostdlib`,
project-owned integer headers, a 2 MiB flash linker map and no unresolved
library symbols. The UF2 family is `0xe48bff59`. The initial diagnostic
image contained 6,512 bytes of flash payload in 26 UF2 blocks; the active
driver build is approximately 16 KiB. Critical receive/validation/ACK code
is copied to SRAM at boot rather than depending on flash-cache latency.

Sanitized tests exercise the PIO instructions against a modeled FIFO,
input-only pads, chronological mixed samples, every line state,
histograms, transitions, CRC/status fields, bounded reset/FIFO failures,
counter saturation, configured/valid EP0 access, rejected malformed and
unrelated requests, reset cleanup and deferred BOOTSEL.
Monitor tests cover bounded duration parsing, CRC/build mismatch and
inconsistent sample counts. Existing badge USB and replay tests also pass.
Additional full-speed tests cover known CRC vectors, all 2,048 token CRC5
inputs, payloads from zero through 64 bytes, stuffing across byte boundaries,
invalid CRC/PID/line states, all EP0 packet sizes, duplicates, NAK limits,
STALL and deadline failures. A cycle-level interpreter executes the actual
32 PIO instruction words with varied input phase and one-clock jitter,
including partial FIFO trailers, EOP validation and the ACK entry/gap.
MMIO transport tests verify that bad CRC, EOP and FIFO errors suppress ACK.
They cover immediate decoding of full FIFO words before EOP as well as
buffered reception after EOP, for every payload size from zero to 64 bytes.
Reset tests cover delayed input settling after release and a line that
never returns to idle.
Monitor tests reject stale probe generations. **These models are not proof
of electrical USB compliance or real peripheral ACK turnaround timing.**

## Native USB diagnostics

The diagnostic firmware enumerates as:

- VID:PID `cafe:4027` (experimental laboratory IDs).
- Product `U26 Logi Host Diagnostics`.
- Serial derived from the chip ID; the physically tested board is
  `B72817DA2FE6A757`.

There is **no CDC serial port**. Use the monitor, not a serial terminal.
It opens only the exact serial and VID/PID, without seizing a device.
Each control transfer has a one-second timeout. The matching build ID,
CRC, mode, pin mapping and sample-count invariants are checked before
printing data or requesting BOOTSEL.

```sh
# One snapshot:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757

# Bounded polling (1..3600 seconds):
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --watch 10

# Request software recovery after verifying a valid status:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --bootsel
```

Keep the UF2 and monitor from the same build. A protocol/build/CRC mismatch
or a firmware PIO/USB error is an explicit failure, not a successful empty
capture.

### Passive sampling

[board.c](board.c) owns PIO0 SM0 and GPIO12/13. Both pads have physical
output-disable set, input-enable, Schmitt input and weak internal
pull-downs. No USB-A pin is driven. Those weak pull-downs are diagnostic
biasing, **not qualified USB host termination resistors**.

The four-instruction PIO program samples both pins sixteen times, pushes
one 32-bit word, then waits. CPU polling restarts it for the next burst.
The oldest two-bit sample occupies bits 1:0; bit 0 is D+ and bit 1 is D-.

This is **uncalibrated, intermittent line sampling**, not continuous
USB packet capture. The transition counter also includes comparisons
across gaps between bursts; it is not a USB packet or bit-transition count.

| State | Interpretation |
| --- | --- |
| `0`, SE0 | Both low; no pull-up detected |
| `1`, D+ high | Full-speed idle **or R13** |
| `2`, D- high | Low-speed idle candidate |
| `3`, SE1 | Both high; investigate R13 plus a peripheral pull-up |

The attached/unplugged comparison below found the same D+ high state in
both cases. This prevents passive full-speed attach detection, but does
not by itself rule out a permanently connected full-speed experiment.
Determine the peripheral's speed before deciding whether a hardware
change is needed. For host hotplug/low-speed operation, follow the
manufacturer's R13 removal requirement.

Any board modification must be done with USB-C, USB-A and all
other power disconnected, by someone equipped for small SMD components.
Removing R13 disables the board's existing PIO-port device-mode pull-up;
the separate native USB-C diagnostic port does not use it.

After modification, repeat the empty-port test: both lines should read
low before attaching a peripheral. Do not simulate removal by holding D+
low with a GPIO output; that would contend with a device's signaling and
is not a substitute for correct host wiring. The bounded SE0 bus reset in
the active driver is different: both lines are driven low for reset and
then released. Removing R13 alone does not qualify the weak pull-downs.

### EP0 status record

Configured vendor-IN request `c0/72`, value/index zero, length exactly 64:

| Offset | Little-endian field |
| --- | --- |
| 0 | Magic `LHD1` |
| 4 | Build ID |
| 8 | Uptime milliseconds |
| 12, 16, 20 | Captures, samples, observed changes |
| 24, 28, 32, 36 | Histograms for SE0, D+, D-, SE1 |
| 40 | Last 16-sample PIO word |
| 44 | Signed PIO error (`0` means none) |
| 48 | Native USB hardware error count |
| 52..55 | Current GPIO line state, mode `1` passive, D+ pin `12`, D- pin `13` |
| 56 | PIO error count |
| 60 | CRC32 over bytes 0..59 |

Errors: `-4401` PIO reset timeout, `-4402` receive timeout,
`-4403` PIO FIFO/debug error, `-4404` exhausted counters.
Sampling stops on a PIO error; native USB still exposes the failure.
Only passive status, full-speed probe/result, build info (`53`) and BOOTSEL
(`52`) vendor requests are accepted. Radio, replay and other badge commands
are blocked.

## Experimental full-speed PIO driver

- [fs_wire.c](fs_wire.c): token/data/handshake/SOF construction, CRC5/CRC16,
  NRZI, bit-stuffing, differential-state validation and streaming decode.
- [fs_pio.c](fs_pio.c): independent PLL_SYS setup at 144 MHz, GPIO12/13,
  PIO0 SM0 transmitter and SM1 receiver. Native USB keeps its own 48 MHz PLL.
- [fs_host.c](fs_host.c): bounded EP0 probe and diagnostic result.

**No active USB-A signals are sent automatically at boot.** The default
remains passive. Start a probe only with a directly connected, full-speed-
capable peripheral powered solely from the board's USB-A port, or an empty port
for a deliberate timeout test. Do not use the low-speed mouse or a hub for
this first driver qualification. Do not attach an additional VBUS source.
An idle D+=1/D-=0 check is required, but R13 means it cannot prove attachment.

```sh
# Explicitly authorize USB-A output drive, reset and descriptor requests:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --probe-full-speed

# Read the last result without starting another probe:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --fs-status
```

The start request is committed only after the native USB status ACK;
an aborted request cannot arm GPIO output. The monitor verifies the
matching firmware first and checks that the result generation advanced.
An idle report or unsuccessful probe exits with status 1, with explicit
stage/error output. `--fs-status` also exits 1 if no successful descriptor
probe is recorded. BOOTSEL remains available after a failed probe.

The one-shot probe:

1. Requires the measured 144 MHz system clock and a full-speed-compatible
   idle line state; then drives SE0 for 20 ms, verifying both pins read low
   during reset, and releases the pins. It waits at most 10 us for the
   synchronized input to return to J instead of assuming immediate readback.
2. Allows 10 ms recovery while sending SOFs.
3. Reads the first eight device-descriptor bytes at address zero and
   validates EP0 maximum packet size (8, 16, 32 or 64).
4. Completes SET_ADDRESS to address 1 and waits 2 ms.
5. Reads all 18 device-descriptor bytes with DATA0/DATA1 tracking and
   status stages, checking consistency with the initial bytes.
6. Disables the active PHY, sets both pads output-disabled, and restores
   passive sampling on **success or failure**. The last result is retained.

A valid first eight-byte descriptor prefix is retained even if its status
transaction fails. It records received data, not successful enumeration;
the error, stage and control phase must still be checked.

This is partial enumeration, not SET_CONFIGURATION or a working HID device.
Passive counters restart after an active probe. Another probe begins with
a bus reset; after a successful one-shot probe the device is not kept
configured or continuously polled.

TX uses twelve 144 MHz system clocks per bit. SETUP/OUT token plus DATA
are pre-encoded into one PIO stream, including the interpacket gap.
RX samples both lines at the same bit cadence, validates two SE0 bit
samples followed by J, and streams eight sampled bits per FIFO word.
The CPU performs table-based NRZI/unstuffing and streaming CRC in SRAM.
Before EOP, full FIFO words are decoded immediately rather than holding one
word back. GPIO line checks use one SIO snapshot, and an already completed
PIO EOP check does not incur an extra timer read before ACK.
ACK is preloaded but enabled only after validation; PIO enforces the
minimum gap before the ACK entry. Actual maximum ACK turnaround and
sustained receive FIFO service must still be measured with real devices.
No automatic ACK is sent for corrupt packets.

Each PHY exchange has a 150 us timeout. The probe has a 300 ms deadline;
NAKs have at most 128 attempts/100 ms per transaction, and repeated
duplicate data toggles are bounded. Other wire errors stop the probe.
SOF scheduling is software-driven, not a separately qualified continuous
USB frame scheduler. The GPIO electrical interface and weak internal
pull-downs remain experimental, not USB compliance-qualified.

### Full-speed result record

Configured vendor-OUT `40/73`, value/index/length zero, starts one probe.
Configured vendor-IN `c0/74`, value/index zero, length 64 reads the result:

| Offset | Little-endian field |
| --- | --- |
| 0, 4 | Magic `FSP1`, build ID |
| 8 | Signed error, zero on success or before the first probe |
| 12, 16 | Host packets requested/completed ACKs, received packets |
| 20, 24, 28 | Timeouts, NAKs, wire errors |
| 32..35 | Current line state, stage, descriptor length, EP0 packet size |
| 36..53 | Device descriptor, up to 18 bytes |
| 54..55 | Result generation, incremented after each probe, skipping zero |
| 56, 58 | Last received PID, qualified 144 MHz clock flag |
| 57 | Reset levels: before in bits 1:0, during SE0 in 3:2, after release in 5:4; bits 7/6 mark drive/release |
| 59 | Last control phase: 0 idle, 1 SETUP, 2 descriptor IN, 3 descriptor OUT status, 4 SET_ADDRESS IN status |
| 60 | CRC32 over bytes 0..59 |

Stages: `0` not run, `1` reset, `2` recovery, `3` initial descriptor,
`4` address, `5` complete descriptor, `6` success.
Errors: `-4501` timeout, `-4502` buffer limit, `-4503` encoding/EOP,
`-4504` CRC, `-4505` PID, `-4506` length, `-4507` STALL,
`-4508` toggle, `-4509` descriptor, `-4510` line/reset level,
`-4511` clock, `-4512` FIFO, `-4513` deadline/retry limit.

## Flash, preservation and recovery

The existing host-side `picotool` is optional deployment tooling, not a
target dependency. If available, explicitly select the chip serial:

```sh
build/picotool-usb/picotool info -a --ser B72817DA2FE6A757
build/picotool-usb/picotool load -v -x build/logi-host/logi-host.uf2 \
  --ser B72817DA2FE6A757
```

Alternatively, after identifying the correct board in BOOTSEL, copy
the UF2 to its RP2350 volume. Never assume a volume name alone identifies
the Waveshare if another badge is also attached.

Before the first installation, the complete original 2 MiB flash was
saved and verified against the device:

- Local, ignored backup: `build/logi-host/original-B72817DA2FE6A757.bin`.
- SHA256: `1b90c7066b977f42d163b59d115742dbb56ba618f5e76862f91f2a490733b474`.
- Original program: `usb_device`, SDK 2.2.0, `pico2` board configuration.

Keep this backup; `make` does not overwrite or delete it. To restore it,
first enter BOOTSEL and verify the checksum and serial, then:

```sh
build/picotool-usb/picotool load -v -x \
  build/logi-host/original-B72817DA2FE6A757.bin \
  --ser B72817DA2FE6A757
```

Recovery paths:

1. The monitor's `--bootsel` command waits for the control status ACK
   before the firmware disconnects and returns to the ROM bootloader.
2. With no native USB setup activity for 60 seconds, firmware returns
   automatically to BOOTSEL. A continuing monitor keeps it active.
3. An eight-second recovery watchdog is armed before clock setup and
   renewed during the main loop, covering startup failures and hangs.
4. Hardware BOOT + RESET remains available.

To restart the installed diagnostic after recovery, use RESET without
BOOT, reconnect USB-C without holding BOOT, or:

```sh
build/picotool-usb/picotool reboot --ser B72817DA2FE6A757
```

## Physical qualification and remaining work

Tested on the attached RP2350 **A4**, chip ID `B72817DA2FE6A757`,
with 2 MiB flash:

- Full-flash backup verified before writing; diagnostic flash readback
  verified afterward.
- Native USB EP0 status passed exact identity/build/CRC checks.
- Over three seconds, samples advanced from 33,008 to 57,392;
  `PIO=55555555`, D+=1, D-=0 throughout.
- No observed PIO errors, USB hardware errors, or line changes.
- Acknowledged software BOOTSEL return verified against the same chip ID.
- After restarting, a status read followed by 63 seconds without opening
  the device verified the 60-second inactivity return to BOOTSEL.

### Unplugged comparison, 2026-11-03

The user then unplugged the mouse. The existing diagnostic was restarted
without reflashing and observed for five seconds:

- Samples advanced from 7,920 to 48,528.
- Every sampled state was D+=1, D-=0 (`PIO=55555555`).
- No observed transitions, PIO errors or native USB hardware errors.
- Software BOOTSEL recovery again succeeded for the same chip ID.

Thus the high D+ state does not depend on the mouse. It strongly supports
a board-side pull-up consistent with schematic R13, but is not a visual
inspection or resistance measurement proving that exact component.
The manufacturer FAQ explicitly requires R13 removal for host hotplug
and low-speed keyboards/mice. The identified A4 revision also matches
the manufacturer's statement that the older RP2350-E9 GPIO issue is fixed.
The ROM revision was checked again and reports A4; a pictured A2 example
does not establish the silicon revision of the attached board.

The user requires a permanently connected mouse, not hotplug. Therefore
the next decision was its actual USB speed, not automatic R13 removal.
The earlier attached-state observation also had D- low; that does not
establish a working, powered full-speed peripheral or successful USB
enumeration.

No peripheral identity, speed or mouse report was established by the
Waveshare diagnostic.
No component has been removed and the diagnostic still never drives
USB-A. The board was left in BOOTSEL, with the diagnostic installed.

### Mouse identification through the Mac hub, 2026-11-03

The user connected the mouse to the Mac through a USB hub. macOS IORegistry
reports:

| Property | Observed value |
| --- | --- |
| Manufacturer / product | Logitech / USB Optical Mouse |
| VID:PID | `046d:c077` |
| `UsbLinkSpeed` | `1500000` bits/s: **USB low-speed** |
| EP0 maximum packet size | 8 bytes |
| Parent hub | GenesysLogic USB2.1 Hub, `05e3:0610`, 480 Mbit/s upstream |

The parent hub's 480 Mbit/s link does not change the mouse's 1.5 Mbit/s
link speed. Thus this mouse falls under the manufacturer's **low-speed**
R13 warning even without hotplug. Its identity and speed are established
on the Mac, not by the Waveshare firmware; no Waveshare mouse transfers
or reports have been verified. The earlier D- low observation on the
Waveshare must not be interpreted as proof of full-speed operation.

### Keypad through the Mac hub

The working replacement cable enumerates the Logitech MX Keypad,
`046d:c354`, serial `346F35813233`, at **480 Mbit/s** on the Mac.
Its previously read other-speed descriptors advertise full-speed behavior,
but actual 12 Mbit/s operation has not been verified.

### Active-driver empty-port qualification

The full-speed build was flashed and readback-verified on the same board.
The keypad remained visible on the Mac during this empty-port test:

- Native diagnostics passed identity/build/CRC checks; the hardware clock
  measurement passed the 144 MHz range check.
- An explicitly armed probe passed reset (including SE0 pin readback),
  recovery and PIO TX, then reported `stage=3 error=-4501`,
  `tx=12 rx=0 timeouts=1`: the expected no-device response timeout.
- Repeated testing exposed an immediate post-release readback race. A
  bounded idle-state wait replaced the instantaneous check. The final
  build passed **four consecutive probes**, each with `reset_lines=d1`
  (J before reset, SE0 while driven, J after release), a fresh result
  generation and the expected response timeout.
- Passive capture resumed afterward with advancing counters and no
  passive PIO/native USB errors. Software BOOTSEL recovery succeeded.

The test does **not** establish valid USB waveforms or successful reception,
CRC checking or ACK timing on real peripheral traffic. No actual peripheral
descriptor has been read by this driver yet.

### Direct keypad qualification, 2026-11-03

The user connected the keypad directly to Waveshare USB-A with the working
cable, keeping USB-C attached to the Mac. The installed firmware was first
tested, then diagnostic/receive-path updates were built, sanitized and
flashed with readback verification:

- Reset readback was consistently `d1`: J, driven SE0, released J.
- The keypad ACKed SETUP and returned a valid DATA1 packet containing
  `12 01 00 02 00 00 00 40`: device descriptor, USB 2.0, EP0 maximum 64 bytes.
- Probes stopped at `stage=3 phase=3 last_pid=5a error=-4513`:
  the initial read's OUT status transaction repeatedly returned NAK.
  The final build reproduced this on two consecutive probes, each with
  `tx=305 rx=131 timeouts=0 NAKs=129 wire_errors=0`.
- Immediate FIFO decoding and a shorter pre-ACK path did not resolve the
  status-stage failure. ACK turnaround or waveform correctness must not be
  inferred from the valid received packet; the cause remains unproven.
- Passive sampling resumed without reported PIO/native USB errors; over
  two seconds its sample count advanced from 122,960 to 139,200.
- Acknowledged software BOOTSEL recovery was verified against chip ID
  `B72817DA2FE6A757`; the updated firmware remains installed.

This establishes actual full-speed packet reception from the connected
keypad, not a complete descriptor transfer, its VID/PID on the Waveshare,
or working HID input. No resistor was removed. Next: measure/qualify the
ACK and OUT-status exchange before address assignment, configuration, HID
polling, keypad protocol or a badge relay. For the identified low-speed
mouse, the separate R13 issue still applies.
