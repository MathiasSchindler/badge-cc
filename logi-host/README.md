# Waveshare RP2350-USB-A: independent USB host development

This directory is for the **Waveshare RP2350-USB-A**, not the Universe badge.
The eventual goal is an owned PIO USB host for a mouse, then the Logitech
keypad, with events relayed to the badge over a separate connection.

**Current milestone: experimental full-speed PIO transport, device/configuration/
HID descriptors, SET_CONFIGURATION and bounded keypad input sessions,
plus numbered LCD test images and an interactive per-key inversion demo,
with passive diagnostics by default.**
The 144 MHz clock, active empty-port reset/send/timeout and passive recovery
are physically verified. The directly connected keypad now completes both
device-descriptor reads, their status stages and SET_ADDRESS at full speed:
**four consecutive final-build probes succeeded**, identifying `046d:c354`.
The former persistent OUT-status NAKs were caused by late host ACKs.
ACK turnaround is improved but still not qualified: the first captured ACK
starts about **8.0..9.0 bit times** after SE0-to-J, with sampling uncertainty
of +/-0.5 bit; USB permits at most 7.5. This remains an experimental transport,
not a compliant or complete USB host. A read-only HID++ feature query has
completed over interrupt OUT/IN, and incoming reports are captured and decoded
using the existing keypad helpers. Button-driven normal/inverted image changes
are now physically and visually confirmed. All nine numbered JPEGs have been
acknowledged by the keypad
over the PIO host, including a 30-second keepalive hold; visual confirmation
subsequently established **4..9 visible but 1..3 missing** on that build.
A startup-settling guard is now installed and protocol-tested; the user
subsequently confirmed **all nine images visible** with that guard.
The exact internal startup-rendering mechanism remains unproven.
There is no hub support or
navigation-diversion setup. The Mac diagnostic connection uses the
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
image contained 6,512 bytes of flash payload in 26 UF2 blocks. The later
descriptor-only milestone contained 16,456 bytes in 65 UF2 blocks; the HID
build contains 19,444 bytes in 76 UF2 blocks, with 163,880 bytes of BSS including the 128 KiB
differential-decoding table and a 32-record capture ring.
Critical receive/validation/ACK code
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
MMIO transport tests verify that bad SYNC, PID, CRC, EOP and FIFO errors
in any of the three active state machines suppress ACK.
They cover immediate decoding of full FIFO words before EOP as well as
buffered reception after EOP, for every payload size from zero to 64 bytes.
Reset tests cover delayed input settling after release and a line that
never returns to idle.
Monitor tests reject stale probe generations and check response-gap extraction
across trace-word boundaries, missing edges and invalid line states.
HID tests cover multi-packet configuration/report-descriptor reads at every
EP0 packet size, the actual full-speed endpoint intervals, NAK retries,
DATA toggles/duplicate suppression, read-only HID++ replies and errors,
malformed descriptors, and successful/failed deconfiguration. Monitor tests
reject stale/corrupt/overwritten ring records and missing query confirmation.
Native EP0 tests cover deferred session arming/abort, bounds and exclusion
of concurrent active probes.
**These models are not proof
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
Only passive status, full-speed probe/result/trace, keypad session/status/events,
build info (`53`) and BOOTSEL
(`52`) vendor requests are accepted. Radio, replay and other badge commands
are blocked.

## Experimental full-speed PIO driver

- [fs_wire.c](fs_wire.c): token/data/handshake/SOF construction, CRC5/CRC16,
  NRZI, bit-stuffing, differential-state validation and streaming decode.
- [fs_pio.c](fs_pio.c): independent PLL_SYS setup at 144 MHz, GPIO12/13,
  PIO0 SM0 transmitter, SM1 receiver and SM2 preloaded ACK transmitter.
  Input-only PIO1 SM0 independently records the first DATA EOP/ACK.
  Native USB keeps its own 48 MHz PLL.
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

The separate `--keypad-watch` mode below extends this sequence through
configuration and HID polling; the descriptor-only command retains its
original behavior.

TX uses twelve 144 MHz system clocks per bit. SETUP/OUT token plus DATA
are pre-encoded into one PIO stream, including the interpacket gap.
RX samples both lines at the same bit cadence, validates two SE0 bit
samples followed by J and rejects SE1 at release. Full eight-sample words
are decoded immediately. A single tagged EOP trailer combines the remaining
samples with the sample count, avoiding the old one-word lookahead that
delayed processing of the final CRC byte.
The CPU performs table-based differential/NRZI/unstuffing and streaming CRC
in SRAM. A separately preloaded SM2 starts ACK through one enable write,
only after SYNC, PID, CRC, stuffing, length, FIFO and EOP checks.
PIO enforces the minimum gap; the measured maximum response time is still
too long (see physical qualification). No automatic ACK is sent for corrupt
packets. The hot receive objects and LTO link use `-O2`; other target objects
retain `-Oz`. Sustained traffic and worst-case ACK timing remain unqualified.

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
| 59 | Last control phase: 0 idle, 1 SETUP, 2 descriptor IN, 3 descriptor OUT status, 4 control-write IN status, 5 HID SET_REPORT OUT data |
| 60 | CRC32 over bytes 0..59 |

Stages: `0` not run, `1` reset, `2` recovery, `3` initial descriptor,
`4` address, `5` complete descriptor, `6` success.
Errors: `-4501` timeout, `-4502` buffer limit, `-4503` encoding/EOP,
`-4504` CRC, `-4505` PID, `-4506` length, `-4507` STALL,
`-4508` toggle, `-4509` descriptor, `-4510` line/reset level,
`-4511` clock, `-4512` FIFO, `-4513` deadline/retry limit.

### Physical post-EOP trace

Configured vendor-IN `c0/75`, value/index zero, length exactly 64:

| Offset | Little-endian field |
| --- | --- |
| 0, 4 | Magic `FST1`, matching build ID |
| 8, 9 | Captured word count (0..8), first successfully received DATA PID |
| 10..11 | Generation matching the full-speed result |
| 12..15 | Reserved, zero |
| 16..47 | Up to eight chronological sample words, oldest two-bit sample in bits 1:0 |
| 48..59 | Reserved, zero |
| 60 | CRC32 over bytes 0..59 |

PIO1 observes the pins without changing their mux or output direction.
It detects receive SE0 and captures 128 samples at 24 MHz (half a full-speed
bit per sample), then stalls harmlessly in its joined RX FIFO.
The monitor reads this record alongside the result, checks build/CRC/generation
and prints `0` (SE0), `J`, `K`, or `X` (SE1). It measures the first SE0/J/K
response transition, reports +/-0.5-bit sampling uncertainty and warns when
that interval does not establish the 7.5-bit host limit. Missing transitions
are reported explicitly, not treated as a zero-delay response.

This captures only the first successful DATA transaction's EOP/ACK, not
every ACK or a continuous USB trace. A successful descriptor probe does not
mean timing compliance; its exit status describes the probe, while timing
qualification is reported separately.

## Bounded keypad HID sessions

The strict full-speed configuration parser and exact 140-byte HID descriptor
check are shared with the existing badge-native host through
[keypad-descriptors.h](../logi-keypad/keypad-descriptors.h), rather than copied
into a second driver. Input interpretation reuses
[keypad-input.h](../logi-keypad/keypad-input.h).
The badge-native USB transport itself is not used on the Waveshare.

```sh
# Restart the installed Waveshare firmware if it is in BOOTSEL:
build/picotool-usb/picotool reboot --ser B72817DA2FE6A757

# Explicitly authorize reset/configuration and capture for 1..30 seconds:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --keypad-watch 30
```

Only the verified `046d:c354` device with one supported HID interface is
configured. The driver reads the configuration header and full descriptor,
uses their actual endpoint addresses/sizes/intervals, sends SET_CONFIGURATION,
and reads/checks all 140 HID bytes across multiple EP0 packets.
The physical full-speed descriptors specify **IN `81`, 32 bytes, 5 ms** and
**OUT `01`, 64 bytes, 5 ms**, not the Mac's high-speed 1-ms/microframe or
1024-byte endpoint assumptions. Declared power is 360 mA, not measured current;
the board's unqualified VBUS path and R13 limitations remain unchanged.

During the session, the cooperative main loop services native Mac USB,
watchdog renewal and recovery between bounded PIO exchanges. SOFs continue;
interrupt endpoint intervals are measured from the preceding transaction's
completion, so OUT retries and IN polls never run sooner than advertised.
Idle IN NAKs are normal and counted, not treated as missing key events or
retried in a tight loop. DATA toggles are maintained; valid duplicates are
ACKed but not published twice. Repeated duplicates and all other transfer
errors stop the session explicitly.

One 20-byte HID++ request, `11 ff 00 01 1b 04 ...`, discovers feature
`0x1b04` without enabling diversion or changing settings. Its correlated
reply must arrive within one second. On the attached keypad it returns
feature index `0x0b`. Unknown reports remain visible verbatim.
LCD-key states report key 1..9, one-based row/column, or release; zero observed
button reports is explicitly **not** a successful physical-button test.

Every nonempty input report, including command replies, enters a 32-record
ring with generation, sequence, timestamp and CRC. The Mac drains it about
every 10 ms and fails explicitly on overrun, stale generation or corruption.
Stopping/crashing the monitor does not create an unbounded host: the firmware
still ends the requested session within 30 seconds. On completion/error it
attempts SET_CONFIGURATION(0), preserves any primary and cleanup error,
disables the PIO PHY and restores passive sampling. After stopping the capture
client, another monitor invocation can request BOOTSEL; early recovery cancels
the session.
Passive counters are frozen while PIO0 is hosting and restart afterward.

In `--keypad-watch`, no navigation reporting settings are changed, no display
keepalive is sent, and no image is uploaded. Display writes require the
separate explicit mode below.

## Numbered LCD test

```sh
# Restart the installed firmware if it is in BOOTSEL:
build/picotool-usb/picotool reboot --ser B72817DA2FE6A757

# Replace all nine icons, then keep them alive for 30 seconds:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --lcd-test 30
```

**This replaces the existing icons without backing them up.** The expected
pattern is white digits 1..9 in row-major order, on colored checkerboards
with white borders:

```text
1 2 3
4 5 6
7 8 9
```

The build-time [lcd-prepare.c](lcd-prepare.c) reuses the Mac test renderer and
built-in ImageIO JPEG encoder, verifies the decoded 118x118 dimensions, and
generates nine constant byte arrays. Only the prepared JPEG bytes are linked
into the freestanding firmware: no target image encoder, framework or libc
is added. [keypad-lcd-wire.h](../logi-keypad/keypad-lcd-wire.h) shares the
existing Mac tool's geometry, fragment flags, padding and report assembly.

The host discovers keepalive feature `0x0008` (index `04` on this device),
sets its timeout to 3000 ms, and refreshes it about once per second.
After the first successful keepalive, it allows one second for asynchronous
startup rendering to settle before uploading any image. This is a bounded
guard under investigation, not a decoded device-ready indication. USB polling,
SOFs, native diagnostics and recovery continue during this nonblocking pause.
Each 4095-byte LCD output report uses HID **SET_REPORT** over EP0:
`21/09`, value `0214`, interface zero, length `0fff`. On this keypad, the
data stage is 63 full 64-byte packets plus a final 63-byte packet, beginning
with DATA1 and alternating on acknowledged packets only, followed by a
zero-length DATA1 IN status stage. A report is not complete until that
status stage succeeds. Interrupt OUT remains in use for the 20-byte feature
and keepalive commands, with its own independent DATA toggle and 5-ms interval.

An earlier interrupt-OUT image experiment produced acknowledgments after
the first 64-byte packet, an unexpected HID++ error, then a timeout after two
images. Those early replies did not establish reception of a complete JPEG.
The final EP0 path completed all nine images; there is no silent fallback to
the failing interrupt path. Matching replies that arrive before USB transfer
completion are retained without advancing or overwriting the report buffer.

LCD fragments are serialized with at least 5 ms after acceptance. A `fb`
busy reply restarts the current image, with at most three attempts and a
100-ms pause; other rejection statuses fail explicitly. USB NAK retries retain
the exact data and PID. Queued reports and missing command replies each have
a one-second deadline, while native USB and watchdog servicing continue
between packets. The upload is bounded to 30 seconds; the requested **1..30
seconds is the hold after all nine images**, not the upload deadline. An
overall `31 + hold` second limit also applies.

The monitor requires nine accepted images, LCD state DONE, and successful
deconfiguration before reporting success. On completion/error, the host
attempts SET_CONFIGURATION(0), preserves primary/cleanup errors, restores
passive sampling and stops keepalives. It does not claim exact restoration
of previous icons or brightness; default animations may return afterward.
Navigation diversion is not changed. The arbitrary-image API and permanent
display ownership are outside this bounded test.

## Interactive LCD toggle demo

```sh
# Restart the installed firmware if it is in BOOTSEL:
build/picotool-usb/picotool reboot --ser B72817DA2FE6A757

# After the initial nine images, accept button input for 30 seconds:
build/logi-host/logi-host-monitor --serial B72817DA2FE6A757 --lcd-toggle 30
```

This explicit mode replaces the icons without backing them up, just like
`--lcd-test`. Wait for `LCD images accepted: 9/9`, then press LCD keys 1..9:

- A new press toggles **only that key** between its normal image and an
  RGB-inverted variant. Releasing it does not change the image.
- Repeated reports for the same held key do not toggle repeatedly. A release
  rearms the next press. Single-key use is the initial qualification target;
  simultaneous/overlapping keys are not yet physically qualified.
- Input during initial setup is retained; the initial grid is drawn first.
  Subsequent transfers redraw only keys whose requested state differs from
  the last acknowledged state, in round-robin order.
- A JPEG already being sent retains its key and variant until completion,
  even if another press arrives. Rapid changes are coalesced to the latest
  requested state rather than mutating an in-flight report or interleaving
  two images. Busy retries restart the same snapshot.
- Keepalives, HID input, native diagnostics and recovery continue while
  redrawing. The one-second startup guard is retained.

The **1..30-second input window starts after the initial grid**. At its end,
new presses stop changing requested state; pending changes drain before
deconfiguration, with at most one extra second. A drain timeout is an explicit
failure. The existing overall upload/session deadlines still apply. Neither
`--lcd-test` nor default read-only capture acquires this behavior implicitly.
Navigation diversion and page-button actions are unchanged.

The target embeds **18 JPEGs: nine normal and nine inverted**, prepared with
the shared Mac renderer/encoder at build time. Decoded images are checked for
118x118 dimensions and mean RGB error below 12 levels relative to their
expected pixels. No target JPEG encoder, heap or new dependency is added.
This establishes a button-to-state-to-redraw path; menus, counters, clocks and
other dynamic content are future extensions, not implemented modes.

### Interactive validation, 2026-11-04

- Sanitized tests reconstruct actual normal/inverted JPEG payloads for the
  requested key and exercise press edges, releases, repeated held reports,
  invalid inputs, static-mode isolation, input during startup, mid-transfer
  state changes, rapid-toggle coalescing, busy/NAK retries and stop/drain.
- Native EP0 tests cover both display commands, including bounds, wrong
  direction/index/length, deferred commit/abort/reset and concurrent rejection.
  Monitor tests accept mode 2 and reject incomplete/wrong-mode/failed results.
  Shared Mac keypad, badge-native host, trace/replay and USB regressions pass.
- The flashed/readback-verified firmware completed a **30-second interactive
  session**: 9/9 initial images, 27 fragments, 64 input reports, 6,285 IN polls,
  6,283 NAKs and zero duplicates, timeouts, wire errors or cleanup errors.
  A static display test, read-only capture and descriptor probe also passed;
  acknowledged exact-chip BOOTSEL recovery succeeded.
- No physical key presses were observed in that initial run. A subsequent
  user-assisted **30-second test** captured **23 presses and 23 releases**,
  with **23 acknowledged redraws**: 96 total fragments including the initial
  grid, 180 input reports, zero duplicates, timeouts, wire errors or cleanup
  errors. The user confirmed: **"bei jedem Druck wechselte das Bild korrekt."**
  This qualifies the real button-to-display toggle path visually as well as
  at the protocol level. Simultaneous/overlapping keys remain unqualified.
- Target: **181,800 bytes text, zero initialized data, 168,136 bytes BSS**, no
  undefined symbols. ACK timing remains experimentally 8.5..9.0 bit times,
  not compliance-qualified.

### Keypad diagnostic records

All requests require native USB configuration and index zero:

- `40/76`, value **1..30 seconds**, length zero: arm a session only after its
  status ACK. Aborted requests cannot drive USB-A; concurrent probes/sessions
  are rejected.
- `c0/77`, value zero, length exactly 64: session status.
- `c0/78`, value **0..31**, length exactly 64: read that ring slot without
  consuming it. Sequence `n` occupies slot `(n - 1) % 32`.
- `40/79`, value **1..30 hold seconds**, length zero: explicitly arm the LCD
  test after its status ACK, with the same abort/concurrency guards as `40/76`.
- `40/7a`, value **1..30 input seconds**, length zero: arm the interactive
  toggle demo with the same deferred commit/abort/concurrency guards.

Status fields (little-endian):

| Offset | Field |
| --- | --- |
| 0, 4, 8 | Magic `FKP1`, build ID, signed primary error |
| 12..13, 14, 15 | Generation, state, last phase |
| 16, 20, 24, 28 | Report count, IN polls, all NAKs including enumeration, duplicates |
| 32 | HID descriptor CRC32 |
| 36..38, 39..41 | IN address/size/interval, OUT address/size/interval |
| 42, 43 | Interface number, discovered feature index |
| 44 | Signed cleanup error |
| 48..49 | Declared configuration demand, mA |
| 50, 51 | Initial LCD images accepted (0..9), accepted LCD fragments (saturates at 255) |
| 52 | Decoded LCD-key state count, including releases |
| 56, 57 | Query reply confirmed, device still configured |
| 58, 59 | LCD mode (0 disabled, 1 static, 2 toggle), LCD stage |
| 60 | CRC32 over bytes 0..59 |

States: 0 idle, 1 enumerating, 2 polling, 3 complete, 4 failed.
Phases: 1 device descriptor, 2 configuration header, 3 full configuration,
4 SET_CONFIGURATION, 5 HID descriptor, 6 feature query, 7 input polling.
Additional errors: `-4514` invalid/error HID++ reply, `-4515` cancelled by
native USB deconfiguration/reset or recovery, `-4516` output queue busy,
`-4517` invalid/rejected LCD reply.
LCD stages: 0 disabled, 1 feature discovery, 2 keepalive, 3 upload, 4 hold,
5 done, 6 failed.

Event fields: magic `FKE1` at 0, build at 4, one-based sequence at 8,
generation at 12..13, raw length at 14, feature at 15, raw bytes at 16..47,
microseconds since polling began at 48, CRC32 at 60. Unused bytes are zero.
This is diagnostic capture, not Mac HID keyboard emulation.

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

At this passive-only milestone no peripheral identity, speed or mouse
report had been established by the Waveshare diagnostic. No component
had been removed and that diagnostic never drove USB-A.
The board was left in BOOTSEL, with the diagnostic installed.

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
Its previously read other-speed descriptors advertised full-speed behavior;
subsequent direct Waveshare tests below confirmed actual 12 Mbit/s operation.

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

That empty-port test did **not** establish valid USB waveforms or successful
reception, CRC checking or ACK timing on real peripheral traffic. The later
direct-peripheral tests below provide that separate evidence.

### Direct keypad qualification, 2026-11-03

The user connected the keypad directly to Waveshare USB-A with the working
cable, keeping USB-C attached to the Mac. Initial testing established:

- Reset readback was consistently `d1`: J, driven SE0, released J.
- The keypad ACKed SETUP and returned a valid DATA1 packet containing
  `12 01 00 02 00 00 00 40`: device descriptor, USB 2.0, EP0 maximum 64 bytes.
- Probes stopped at `stage=3 phase=3 last_pid=5a error=-4513`:
  the initial read's OUT status transaction repeatedly returned NAK.
  The earlier build reproduced this on two consecutive probes, each with
  `tx=305 rx=131 timeouts=0 NAKs=129 wire_errors=0`.

The subsequent independent PIO capture showed a correctly encoded ACK
starting approximately 17 bit times after the received packet's SE0-to-J.
USB 2.0 sections 7.1.18.1 and 7.1.19.1 specify a host response maximum
of 7.5 bit times and a device timeout between 16 and 18 bit times.
Thus the device could discard the late ACK and continue NAKing OUT status.
Moving ACK preparation out of the receive path, fixing the FIFO trailer,
and reducing receive/validation overhead eliminated the persistent NAKs.
The USB 2.0 base specification is available in the
[official USB-IF archive](https://www.usb.org/sites/default/files/usb_20_20250603.zip).

The final firmware was built, sanitized, flashed and readback-verified.
**Four consecutive probes** each produced:

```text
stage=6 phase=3 last_pid=d2 error=0
tx=32 rx=11 timeouts=0 NAKs=3 wire_errors=0 reset_lines=d1
12 01 00 02 00 00 00 40 6d 04 54 c3 00 02 01 02 03 01
VID:PID=046d:c354, EP0 maximum packet=64
```

Both GET_DESCRIPTOR status stages and SET_ADDRESS now succeed. Three
transient NAKs remain, but are handled by bounded retries; they no longer
prevent completion. The first DATA ACK measured 8.5 bit times in three
probes and 8.0 in the fourth, each with +/-0.5-bit sampling uncertainty.
This is below the device's timeout, but **does not meet/establish the
7.5-bit host response requirement**. No USB compliance claim is made.

Passive sampling resumed after all probes: over two seconds the sample
count advanced from 3,760 to 20,000 without PIO/native USB errors.
Acknowledged software BOOTSEL recovery was verified against the exact
chip ID `B72817DA2FE6A757`; the updated firmware remains installed.
No resistor was removed.

At that milestone the next steps were configuration and HID polling; the
bounded HID session above now implements those layers. The descriptor-only
successes did not establish working keypad buttons or LCD output. For the identified
low-speed mouse, the separate R13 issue still applies.

### Configured HID qualification

The final HID firmware was flashed with readback verification on the same
board and tested with successive **2-second, 30-second and 2-second** sessions:

- Every session read the full-speed configuration and all 140 HID bytes,
  yielding descriptor CRC32 `1bfc4b8e`, then successfully configured the device.
- All three completed the read-only HID++ feature query with reply
  `11 ff 00 01 0b 00 06 ...`. Each also captured two unsolicited
  `13 ff 02 10 ...` startup reports, preserved as unknown rather than buttons.
- The 30-second session performed **5,975 interrupt IN polls**, with 5,980
  total NAKs including enumeration, **zero duplicates, timeouts or wire
  errors**. Native Mac USB remained responsive throughout.
- Each session successfully sent SET_CONFIGURATION(0) and returned to passive
  mode. An additional descriptor-only probe still passed after all sessions.
- The first DATA ACK measured 8.0, 9.0 and 8.0 bit times respectively, each
  with +/-0.5-bit sampling uncertainty: the timing limitation remains.
- Passive samples then advanced from 3,840 to 20,480 over two seconds with
  no PIO/native USB errors. Exact-chip BOOTSEL recovery succeeded.
- A separate 30-second capture was interrupted by stopping its Mac client;
  while the device session was still active, a new monitor successfully
  requested acknowledged BOOTSEL recovery. The board remains in BOOTSEL with
  this firmware installed.

No human button presses were supplied: `LCD_states=0` is an explicit remaining
physical test, not evidence that the switches fail. Next, run the bounded
capture while pressing keys 1..9 and check presses/releases against the known
row-major mapping. Navigation diversion remains future work; LCD report
uploads were subsequently qualified below. ACK timing still needs correction
before claiming a compliant host.

### PIO LCD qualification

The HID SET_REPORT implementation passed a one-second hold, then the final
firmware was flashed/readback-verified on the same board and passed a
**30-second hold**:

- All **nine JPEGs and 27 fragments** received LCD status `00`. Upload completed
  in approximately **0.57 seconds**, before the requested hold began.
- The final session captured **63 reports**, performed **6,087 IN polls**,
  and counted 6,086 ordinary NAKs, with **zero duplicates, timeouts or wire
  errors**. Keepalive replies continued through approximately 30.15 seconds.
- Completion reported `LCD images=9/9 fragments=27 state=5`, with zero
  primary/cleanup errors and successful SET_CONFIGURATION(0).
- A subsequent two-second read-only HID session and descriptor-only probe
  both passed without timeouts/wire errors. Passive samples then advanced
  from 3,760 to 20,160 with no passive PIO/native USB errors. Acknowledged
  BOOTSEL recovery was verified against the exact chip ID; the installed
  firmware remains in BOOTSEL.
- First-DATA ACK timing was 9.0 bit times during the LCD session, 8.5 during
  the HID regression and 8.0 during the descriptor probe, each +/-0.5 bit.
  The existing 7.5-bit host-limit qualification gap remains.
- Sanitized tests reconstruct every JPEG over EP0 sizes 8/16/32/64 and cover
  SETUP/data/status NAKs, PID retention, early replies, busy restart,
  keepalive, rejection, STALL and deadlines. Native command tests cover
  deferred commit/abort/reset and isolation; monitor tests reject incomplete
  images, wrong mode/stage and cleanup failures. Mac keypad, badge-native host,
  trace/replay and shared native USB regressions pass.
- That target was **101,348 bytes text, zero initialized data and 168,124
  bytes BSS**, with no undefined symbols or target library dependencies.

These are real protocol acknowledgments, not proof of persistent pixels.
The user's earlier complete visual confirmation applies to the **Mac-driven**
test. No resistor, wiring or power-source changes were made.

### Missing upper row investigation, 2026-11-04

The user's PIO-test photograph shows correctly aligned digits **4..9** but
three dark upper keys. Thus the original PIO build is visually qualified for
**six images, not nine**, despite all 27 successful fragment replies.

One suspected cause is a race with asynchronous startup rendering: images
1..3 were accepted roughly 80..210 ms into the original session, while
unsolicited startup-associated reports continued at about 300 ms. Their
precise semantics are unknown; they are not treated as a readiness signal.
This timing is evidence for an experiment, not proof of the cause.

The candidate adds a single one-second settling pause after the initial
keepalive ACK, using the existing cooperative pause mechanism. A regression
model independently clears rendered pixels 300 ms after configuration,
without retracting successful report ACKs. Its all-nine-visible assertion
fails without the guard and passes with it; this models the suspected
hazard rather than claiming the device's exact internal behavior.

The candidate was built, sanitized, flashed and readback-verified:

- First LCD fragment reply at **1.045 s**, all nine images/27 fragments
  acknowledged by **1.572 s**, followed by the full **30-second hold**.
- **64 reports, 6,286 IN polls, 6,284 NAKs**, zero duplicates, timeouts,
  wire errors, primary errors or cleanup errors.
- Target: **101,360 bytes text**, zero initialized data, **168,124 bytes BSS**;
  no undefined symbols or new dependencies. Full host tests pass.
- Acknowledged BOOTSEL recovery succeeded with the candidate installed.

**Visual verification passed, 2026-11-04:** the user subsequently confirmed,
"Alle Bilder werden nun angezeigt." All nine images, including the formerly
missing upper row, are now visually qualified with the one-second guard.
This confirms the observed fix, not the device's exact internal startup
mechanism. Simultaneous/overlapping key input and the pre-existing ACK timing
limitation remain open.
