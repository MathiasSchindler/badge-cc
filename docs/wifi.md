# Project-owned, no-libc Wi-Fi roadmap

## Current status — 2026-09-22

The freestanding RP2350 gSPI/SDPCM/CDC/BDC host and ARP/static-IPv4/UDP
path work with **vendor CYW43439 firmware**. Three two-badge baseline
sessions completed **600/600 echo transactions**; see the
[measurements and limits](#static-ip-udp-echo-and-measurements). The
milestones and dated implementation record below retain their original
scope; early "not implemented" results do not describe the later UDP image.

Separate [source-radio research](cyw43/handoff.md) has original Cortex-M3
cold initialization/calibration and PSM components. Vendor-retained source
PSM now commits checked six-byte CCK prefixes, including three guarded
captures with the six cold candidate-inhibit readbacks. Upstream FIFO
contents remain unexcluded: this is not proof of fresh on-air RX under
inhibition, a source-only fresh frame, full-frame RX, or RF/safe-RX
qualification. Cold-source no-event remains unresolved.
The owner has deferred RF measurement for explicitly authorized bounded
experiments under `--experimental-rf-unverified`; use the
[current research workflow](cyw43/workflow.md), not these host-network
milestones, for its testing/recovery policy.

## Goal and boundary

Build a narrowly scoped CYW43439 host implementation for the RP2350 badge:

```text
Project-owned application and four-player protocol
                      |
              ARP / IPv4 / UDP
                      |
        Ethernet frames and radio control
                      |
      Project-owned gSPI / GPIO / PIO / DMA
                      |
       CYW43439 vendor firmware and configuration
```

RP2350 target code must use no Pico SDK, libc, lwIP, BTstack, or third-party
host driver. Keep project-owned startup, linker script, UF2 generation,
display, timers, fixed buffers, and explicit error reporting. Host compilers,
linkers, test libraries, and optional logic-analyzer tools are development
tools, not target dependencies. Limited standard-library Python and existing
legacy Ruby host tools do not introduce target runtimes or new target
dependencies.

The **CYW43439 is a separate processor**. Its vendor firmware, board
configuration, and regulatory data are an implemented explicit binary/data
exception. Replacing the radio's internal firmware is not part of this
roadmap; the separate [current source-radio handoff](cyw43/handoff.md)
and [original research plan](cyw43-driver.md) cover that work without
blocking this host/networking track.
The first register-access milestone needs none of these assets and
must build without even reading the vendor directory.

Moving SDK/vendor source to a different directory is not independence.
Reference implementations may establish hardware/protocol contracts, but a
retained source port must be labeled as such and retain its applicable
license notices. Do not claim a clean-room implementation: the existing
driver has been inspected as a reference.

This work is a separate freestanding target. Preserve the SDK-based
[handover lab](wifi-handover.md) as a reference and later comparison tool.
The SDK lab's results are not measurements of this host. The freestanding
two-badge application RTT measurements below are not a comparative SDK
benchmark or a one-way latency claim.

## Scope and effort

The initial scope is local IPv4 with static addresses, ARP and UDP; ICMP
echo for diagnosis remains planned and is rejected by the current UDP image.
No TCP, DHCP, DNS, IPv6, routing, internet services, or
general-purpose socket API is required for four-player Pong. Reject
unsupported/malformed packets explicitly and count errors.

Indicative effort for one experienced embedded developer with hardware:

| Scope | Planning range |
| --- | --- |
| Remove libc while retaining SDK and libraries | 1-3 weeks, after dependency audit |
| Remove SDK/libc by porting selected existing libraries | 4-8 weeks; not the final project-owned goal |
| Project-owned connected BLE subset plus radio firmware | 6-12 weeks |
| Project-owned Wi-Fi STA/AP and static-IP UDP plus radio firmware | 8-16 weeks |
| Both protocols, four-badge handover, recovery and hardening | Roughly 3-6 months overall, potentially longer |

These are uncertain engineering estimates, not commitments. The common
radio bus/boot work overlaps. Physical-debugging time, firmware quirks,
coexistence, and recovery dominate the uncertainty; host tests cannot prove
electrical timing. Removing SDK layers does not itself guarantee lower RTT.

## Milestones and acceptance gates

These are the original staged goals, not a list of wholly unfinished work.
M1a, M1b, paired WPA2 association and the two-badge ARP/UDP slice have
physical evidence below; the broader acceptance gates remain distinct.

### M0: Establish the build and evidence boundary

- Add an opt-in freestanding target and host-test harness.
- Record the 2026 radio signals from the schematic: GPIO23 `WL_ON`,
  GPIO24 shared data/IRQ, GPIO25 CS, GPIO29 clock.
- Separate wire encoding, board access, bring-up state, and display.
- Verify dependency output and ELF symbols: no vendor includes/objects,
  standard-library calls, or unresolved compiler runtime helpers.
- Retain USB boot recovery and avoid unrelated GPIOs.

**Gate:** reproducible UF2, host tests, and an inspectable dependency closure.

### M1a: Prove radio register access before loading firmware

- Cold-reset only the radio; hold data low during power-up as in the
  reference startup, then release the shared data/IRQ line while idle.
- Implement conservative software-clocked gSPI for small register transfers.
- Read the fixed `0xFEEDBEAD` test register in reset-time 16-bit ordering.
- Change to 32-bit ordering, verify the configured bus control, and read
  the fixed test register repeatedly.
- Configure/verify function-1 response padding, request the ALP clock, and
  inspect the chip-common backplane ID.
- Display the exact stage, values, retries, and failures; offer user-triggered
  restart and radio-off controls. Never show network-ready for bus success.

**Gate:** physical repeatability on the 2026 prototype, including cold USB
boot, retry, and data-direction/clock inspection if reads fail. A plausible
backplane value is an observation, not independently verified chip identity.

This is the first implementation slice. **Do not download firmware or add
networking on the assumption that an untested bus is correct.**

### M1b: Load firmware and obtain a valid controller response

- Establish the exact approved CYW43439 firmware, NVRAM, and CLM assets;
  pin lengths/hashes and retain applicable redistribution notices.
- Keep asset packaging separate from host-side C code.
- Implement backplane windowed bulk transfers and readback verification.
- Hold/reset the device cores correctly; load firmware and board data,
  publish the NVRAM length token, release the CPU, and wait for readiness.
- Implement SDPCM/control framing, flow control, and a bounded version/status
  query. Load regulatory data before enabling RF.

**Gate:** correct firmware readback, a validated controller response, bounded
timeout/error recovery, and proof that the image still links without SDK or
libc. A firmware upload alone is not a successful Wi-Fi connection.

Do not bypass regulatory configuration, alter RF limits, overwrite unknown
memory, or use a GPIO success indicator as a substitute for a protocol reply.

### M2: STA connection and static-IP UDP echo

- Configure a supported WPA2 station connection through the radio firmware.
- Interpret association/authentication/link-loss events accurately.
- Receive/transmit Ethernet frames with bounded fixed-size packet pools.
- Implement ARP cache/expiry, IPv4 validation/checksums, UDP and ICMP echo.
- Use explicit static network settings for this milestone, not a hidden
  dependency on DHCP or DNS. Do not expose credentials in diagnostics.
- Drop unsupported IP fragments/options according to a documented policy.

**Gate:** application UDP echo with a host computer on a known AP, packet
capture verification, malformed-packet tests, and no success before both
association and application traffic work.

The current closed-loop fixture implements the data-plane slice between
the two attached badges instead: real ARP/static-IP UDP echo and RTT/loss
results are recorded [below](#static-ip-udp-echo-and-measurements).
External-host/AP interoperability, independent packet capture and ICMP
echo remain additional work; they are not implied by the two-badge result.

### M3: Badge-hosted network and four participants

- Start a local WPA2 SoftAP and join first one, then three clients.
- Assign unique static addresses and slots with bounded admission.
- Reuse the project-owned handover experiment's UDP message concepts,
  sequencing, tokens, online masks, and normalized edge positions without
  importing its SDK transport.
- Keep authoritative state at the host; send latest-value unicast updates.
- Measure client-to-host, host-to-client, and application-relayed
  client-to-client paths.

**Gate:** consistent four-paddle state on all boards, verified addressing,
stale-client handling, and recorded measurements for 2/3/4 badges.
Ball physics/scoring is a later application task, not evidence of radio
correctness.

### M4: Performance and fault recovery

- Replace a proven slow bus with project-owned PIO/DMA where necessary.
- Keep clock calculations explicit; record actual bus speed and timeouts.
- Exercise radio reset, client disappearance, AP loss, saturation,
  malformed traffic, and allocation exhaustion.
- Compare power-save settings, median/p95/p99/max RTT, loss, joining time,
  sustained throughput, and energy use.
- Validate multiple boards and crowded 2.4 GHz conditions.

**Gate:** sustained tests with no wedged display/radio loop, bounded memory,
explicit errors, reproducible recovery and measured performance. Report
tail latency, not just averages.

### M5: Project-owned BLE and credential handover

- Reuse the established CYW43 backplane/shared-bus foundation; this board's
  current Bluetooth path is not an exposed HCI UART.
- First implement HCI initialization, command credits, advertising/scanning
  and events.
- Then add connection handling, ACL credits/reassembly, required L2CAP
  signaling, and the ATT/GATT subset needed for provisioning.
- Preserve the controller's responsibility for link-layer timing and
  frequency hopping.
- Add versioned credentials/session selection and coordinate Wi-Fi/BLE
  lifecycle without resetting their shared radio unexpectedly.

**Gate:** BLE discovery and handover to the no-SDK Wi-Fi path on physical
badges. Unpaired/unencrypted provisioning must remain a clearly labeled
controlled-lab mode. Secure pairing/authentication/bonding is additional
scope, not implied by WPA2 on the later Wi-Fi link.

## Engineering rules

- Static buffers and narrow APIs; no allocator unless a measured need arises.
- Integer-only formatting and arithmetic where practical. Inspect emitted
  code for implicit `memcpy`, division, floating-point, or other runtime
  helper dependencies.
- Every command has validation, a deadline, and an explicit failure result.
- A stopped timer must not produce an infinite polling loop.
- Bus failure releases chip select/data safely; restart begins with a radio
  reset rather than assuming the previous byte-order state.
- Do not drive GPIO8/PSRAM CS, sensor buses, unknown pins, or shared power
  GPIO41 merely to probe the radio. The display owns its usual pins/rail.
- Do not silently enable RF or join an arbitrary network.
- Preserve existing firmware outputs and document intentional differences.

## Reference evidence

- [2026 hardware specification](spec.md) and manufacturer schematic.
- Raspberry Pi RP2350 register documentation for SIO/pads/timers.
- `vendor/pico-sdk/src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c`
  and its PIO description: gSPI word ordering, command packing, reset and
  turnaround.
- `vendor/pico-sdk/lib/cyw43-driver/src/cyw43_spi.h`: bus registers/test value.
- `vendor/pico-sdk/lib/cyw43-driver/src/cyw43_ll.c`: function-1 windowing,
  clock request and later firmware/control initialization.
- `vendor/pico-sdk/lib/cyw43-driver/firmware/README.md`: firmware/CLM layout.
  [Asset packaging and provenance](wifi-assets.md) records the implemented
  inclusion and unresolved distribution review. Check the actual asset
  rights as well as the driver's `LICENSE` and `LICENSE.RP`; no legal
  clearance is inferred merely from this directory layout.

The reference files are research inputs, not dependencies of the first
freestanding probe.

## Implementation record

Work started on 2026-09-14 with M0/M1a. At that initial checkpoint firmware
loading, association, SoftAP, UDP and BLE were subsequent milestones.
Later sections record working vendor-firmware startup, association and UDP;
project-owned BLE remains separate unfinished work.

The first reported physical run failed at the reset-order test with
`TEST PATTERN MISMATCH`. The owner transcribed the reset value as
`5FFFFFFFF`; the display field is eight hexadecimal digits, so the exact
32-bit value was not inferred from that transcription. Subsequent direct
USB collection established the exact value as `0x5fffffff`.

To shorten this debugging loop, the [USB bootloader report test](usb-report.md)
adds a known-record SRAM/picoboot readback milestone. Its nonce/checksum
round trip was physically attempted on 2026-09-14: the badge ran the test
and returned to BOOTSEL, but RAM readback was zero and failed validation.
The RP2350 ROM clears memory exposed to USB boot, so this is not a usable
retained-SRAM feedback path on the tested A4. A project-owned USB report
interface while the application is running was the proposed replacement;
the [live USB interface](usb-live.md) now implements that replacement and
has returned fresh, checksummed known reports on the physical badge.
It now also runs the radio probe on request and returns its real stage,
error and register values. The initial failure was subsequently resolved
on the attached 2026 badge on 2026-09-15; see the investigation below.

Run the automated radio test with the attached badge in BOOTSEL:

```sh
bash src/host/tools/usb_live_run.sh 08DA26F86E57F5EA wifi
```

Three initial reports independently confirmed `0x5fffffff`, ten reset-read
attempts, zero successful reads, and failure at `RESET ORDER TEST`. Neither
a falling-edge receive experiment nor a shorter half-clock delay fixed
the failure; both were reverted. See [the USB evidence](usb-live.md#first-physical-radio-reports).
That 2026-09-14 result established autonomous collection, not radio-bus
success. It is retained as the baseline for the following investigation.

### 2026-09-15: register/backplane probe passes

The newly attached badge enumerated as serial **`9BBF88DD17D66E7D`**,
different from the prior day's badge. It was selected by chip/USB serial,
not USB port. Running the old image on this second 2026 badge reproduced
`0x5fffffff` three times (`run.xP53seKm`).

A nonce/build/CRC-bound trace now records the first transaction's submitted
bytes, raw response bytes, GPIO configuration and state snapshots, and
timer ticks measured over USB frame intervals. Trace storage is RAM-only;
there is no USB/display traffic inside a gSPI transaction.

| Experiment | Observation |
| --- | --- |
| Baseline trace (`run.7I0UI0cE`) | Software command `a0 04 40 00`; data output enabled while CS high; final reset read still `5fffffff` |
| Assert CS before driving data (`run.foRjYKwL`) | Removes idle-drive overlap; does not alone fix the response |
| PIO clock/sample engine with original startup (`run.hkVomIGt`) | Still fails, returning `5fff7fff`; deterministic clocks alone are insufficient |
| Software TX pad sampling (`run.hn6pV0tg`) | Actual sampled TX bits match `a0044000`; final reset read still fails |
| Hold data low during radio power-up (`run.gyhOvNYH`) | Response changes to a one-bit-shifted form of the expected pattern (`fdda7d5b`) |
| Correct startup plus PIO sampling (`run.Z57zi5i9`) | Reset test returns `feedbead`; next failure is legacy delay-byte configuration readback |
| Per-function delay configuration plus PIO (`run.qHmIhaPb`) | Entire probe passes on three radio resets |
| Sample software receive data before rising clock (`run.kn0qfn7a`) | Entire software probe also passes on three radio resets |

Working changes:

1. Hold GPIO24 low while powering up the CYW43; release it after the startup
   delay. Merely releasing it with a weak pull-down did not produce the
   same behavior on these tests. This matches the reference driver's
   output-low startup, without claiming an independently documented internal
   strapping mechanism.
2. Assert chip select before taking ownership of shared data. Release data
   for receive and idle.
3. With the software engine, sample receive data before the next rising
   clock edge, rather than after a software delay on the high phase.
   The PIO engine obtains the corresponding edge-synchronous sample.
4. Request bus control `0x000200a3`, not `0x000204a3`. The legacy response
   delay byte read back as zero; it is now left zero. Function-1 padding is
   separately written to `0x1d` and verified as 16 before any F1 reads.
   The other configuration checks remain intact.

Both engines observed:

```text
reset test:     FEEDBEAD
normal test:    FEEDBEAD
bus control:   000200A3
ALP CSR:       48
chip-common:   1545A9AF
normal reads:  16 / 16
```

The first reset-time transaction can return `03 03 03 03`; on these runs
the second bounded attempt returns the expected constant. The trace captures
the first transaction, while the result report records the final reset-test
value. A transient first read is not replaced with synthetic success data.

USB SOF comparison recorded roughly **15,280 timer ticks over 32 ms**
(32 full-speed USB frames). Thus an inherited timer tick is approximately
2.09 us on this boot configuration, not a calibrated microsecond. Clock
registers and timer/watchdog dividers are exported; the probe does not
silently change system/timer clocks or claim RTT from these counts.

The software backend remains the default. The PIO backend uses PIO0/SM0,
divider 8, MSB-first 32-bit FIFO words, and no DMA/SDK/PIO assembler build
dependency. It is limited to small register transfers and owns/reset PIO0
only in these isolated probe applications. At this register-probe checkpoint
it was not yet a bulk firmware-loader transport; the M1b section records the
later bounded FIFO-streaming implementation.

```sh
# Repeated software-backed probes, with trace pages:
bash src/host/tools/usb_live_run.sh 9BBF88DD17D66E7D wifi
# Optional deterministic-clock comparison:
bash src/host/tools/usb_live_run.sh 9BBF88DD17D66E7D wifi-pio
```

This establishes M1a register/clock/backplane operation on the selected
badge across repeated **radio** power resets. A full unplug/replug test of
the fixed image, the previous badge, and other revisions remains useful
before advancing. No firmware blob was loaded and no association, AP,
network packet, or RF performance was tested.

Final selectable-backend image `8d6c9fd0` passed another three software
runs (`run.59FKwjMD`) and three PIO runs (`run.3gUKtcm1`), each with
16/16 normal-order reads and the same backplane ID. Each collector cycle
also verified rejection of a corrupted USB request and a clean return
to BOOTSEL. The logs remain under `build/usb-live-runs/`.

### First target: `wifi_bringup`

```sh
make wifi-bringup
make test-wifi-bringup
```

Output: `build/universe26/uf2/wifi_bringup.uf2`.

This is an **opt-in** target, not part of default `make`, so it cannot
silently change the scope of existing diagnostic builds. It uses only
project-owned sources/headers and the existing LLVM/UF2 toolchain. No
firmware blob, SDK source, generated SDK header, or radio-library object is
an input.

The implementation is split into:

| File | Responsibility |
| --- | --- |
| `src/radio/wifi_gspi.c` | Register command encoding, reset/normal word order, bounded bit-level half-duplex transfers |
| `src/radio/wifi_probe.c` | Cold-reset/test/configuration/clock/backplane probe with stage reporting |
| `src/radio/wifi_trace.c` | Nonce-bound USB trace page codec |
| `src/platform/badger/wifi_board.c` | Direct SIO/pad access for the four radio pins and GPIO7/9 buttons |
| `src/platform/badger/wifi_pio.c` | Optional register and bounded FIFO-streaming PIO transfer engine |
| `src/tools/wifi_bringup.c` | Standalone startup UI, run/retry/off controls, changed-tile display transfers |
| `src/tools/wifi_bringup_view.c` | Pure screen rendering for hardware and host previews |
| `src/host/tests/wifi_bringup_test.c` | Wire-level simulated device, framing, sequencing, timeout/fault and rendering tests |

The software clock uses a minimum two timer ticks per half-period, with
clock low when idle and receive sampling before the rising edge.
GPIO/callback overhead further reduces the rate. The timer's measured
scale is reported above rather than assuming a 250 kHz clock. The radio's
high-speed sampling mode is deliberately **disabled**; unlike the SDK
high-speed PIO path, this probe adds no extra turnaround clock.

Commands and values use the reset-time adjacent-byte swap before switching
to normal ordering. Each byte is shifted most-significant bit first.
Register reads clock four response bytes even for 1/2-byte registers.
Function-1 reads additionally discard 16 response-delay bytes.

The requested bus control value is `0x000200a3`: 32-bit word mode, the
reference normal-order setting, active-high shared IRQ, wake request,
zero legacy response delay, and interrupt-with-status policy. The
readback comparison excludes the wake-request bit rather than requiring a
transient request to remain set. Function-1 delay is separately set and
verified at register `0x1d`.

The fixed test register is checked once in reset ordering (up to ten
attempts), then sixteen times in normal ordering. The probe requests ALP,
verifies the three window bytes for `0x18000000`, and reads that chip-common
ID register twice. Zero, all-ones, or inconsistent values fail. A stable
nontrivial value is displayed as **CHIP ID OBSERVED**; the probe does not
guess an expected ID/revision from the board's product name.

Software transfers have a 100,000-tick deadline, in addition to bounded buffer/loop
sizes. Clock delays detect a timer that stops advancing. ALP polling is
limited to 100 attempts or 100,000 ticks, whichever is reached
first. Failures deassert CS, lower clock, release shared data, power the
radio off, and retain the failing stage and last value on screen.

Display updates occur only outside gSPI transactions. Total elapsed time
includes reset delays and stage-display updates; it is not a Wi-Fi latency
measurement. Right/off is serviced between bounded probe runs, not as an
interrupt while a transfer is in progress.

### First physical test

1. Initially use a **2026 badge** with the confirmed radio wiring.
2. Enter USB boot mode and install the UF2. Expect a `WIFI BUS BRINGUP`
   screen with radio off. Nothing is transmitted or associated.
3. Press Left (GPIO7) to run. Watch the displayed stages.
4. Record the final stage/status, both test-register values, bus control,
   ALP CSR, observed chip ID, last read, attempt count, and successful-read
   count. Photograph the screen even if the first attempt fails.
5. If all checks pass, expect both tests to be `FEEDBEAD` and 16 repeated
   reads, with `BUS CHECKS OK / NOT WIFI READY`.
6. Press Right (GPIO9) to power the radio off. Release, then press Left
   again; retry always performs a fresh radio reset.
7. Repeat after removing power and reinstalling/booting. Only after
   repeatability should the bus be used for M1b firmware downloads.

If the fixed register fails, investigate supply/reset, command bytes,
shared-data direction, clock phase/turnaround, and actual timing with a
logic analyzer. Do not try random pins, accept a different expected test
pattern, or proceed to firmware loading after failure.

The firmware does not alter PSRAM/flash, SDK experiments, or the
freestanding display path. GPIO41 stays enabled for the display; Right
controls GPIO23, not the shared peripheral rail. Hold Boot while connecting
USB to recover as usual. A timer failure requires checking the boot/timer
contract; `TIMER NOT ADVANCING` is not a successful radio observation.

### Host verification status

The wire-level model verifies explicit expected command/response byte
vectors, MSB-first bits, data-direction turnaround, chip-select ownership,
function-1 padding, cold-reset retry, 32-bit configuration readback, clock
readiness, backplane window/readback, and failure cleanup. Cases include
all-zero/all-one reads, inconsistent ID, delayed response, timer wrap,
stopped timer, and transfer deadline expiry. UI rendering is also exercised
under address/undefined-behavior sanitizers.

These tests simulate the device boundary. Hardware evidence is recorded
separately above. **M1a now passes on the selected badge with both
software and PIO transfers. The opt-in M1b implementation is described below;
networking is not implemented by this register-probe image.**

Initial verification on 2026-09-14 (before the later trace/timing changes):

- `make wifi-bringup test-wifi-bringup` passes, including ASan/UBSan tests.
- A clean build with `RP2350_CC='clang -nostdinc'` produces the same UF2
  byte-for-byte; the include closure contains only project headers.
- The ELF has no undefined symbols and links with `-nostdlib`.
- The 5,836-byte image produces 23 UF2 blocks (11,776 bytes). Payload,
  padding, RP2350 Arm Secure family ID, initial stack and reset vectors
  were checked.
- The mock-rendered screen was inspected. Its example chip ID is synthetic,
  not a measurement from a badge.
- `make all test-diagnostics` still passes. Existing applications and shared
  hardware drivers were not modified by this milestone.

## M1b implementation: firmware startup only

This is a **separate opt-in image**, not an extension that silently embeds
vendor data in the existing register probe or default applications:

```sh
make usb-wifi-firmware test-wifi-firmware
bash src/host/tools/usb_live_run.sh 9BBF88DD17D66E7D wifi-fw
```

Use the selected badge's actual BOOTSEL serial. The firmware-mode script
collects one fresh report per flash/run/recover cycle. The installed image
is `build/universe26/uf2/usb_wifi_firmware.uf2`; its host collector is
`build/host-tools/usb_wifi_firmware_host`. Both retain the existing known
report and register-probe modes, but use their own matching build fingerprint.
The original `usb-live-report` image rejects firmware-loading requests.

The RP2350 code remains project-owned freestanding C plus a small assembly
data container: no Pico SDK driver, libc, lwIP, allocator, filesystem, DMA
library, or network stack. **Vendor radio firmware, board NVRAM and CLM are
an explicit data exception**, not a claim of an entirely dependency-free
radio. Packaging/provenance and licensing boundaries are documented in
[wifi-assets.md](wifi-assets.md). No RF calibration values are changed.

### Loader and control contract

`src/radio/wifi_firmware.c` performs:

1. Asset CRC/length/bounds checks and the existing reset/register probe.
   Reject anything other than chip ID low word `0xa9af` (43439).
2. Confirm the radio ARM core is held in reset; initialize SOCSRAM and its
   remap bank through the documented wrapper registers.
3. Check a few actual firmware words with isolated RAM writes/reads, then
   write firmware to radio RAM in at most **64-byte SPI function-1 blocks**.
   The separate function-2/CLM chunk limit is not a backplane block limit.
   Never cross a
   32-KiB backplane window within a transfer.
4. Read back and compare **every padded firmware byte**, stopping at the
   first mismatch with its absolute address and expected/actual byte.
5. Write and fully read back NVRAM at the top of the 512-KiB radio RAM,
   then write/read the complemented NVRAM-length token.
6. Release the radio ARM core only after verification. Await HT clock and
   function-2 readiness, with explicit timer and iteration bounds.
7. Send an SDPCM/CDC `GET_VAR ver` request, validate its response envelope,
   request ID, command, status and bounded version string.
8. Upload CLM in at most 512-byte data chunks with zeroed eight-byte payload
   alignment, require successful control replies, and
   query `clmload_status`. Success requires status zero.
9. Power the radio off on success, failure, cancellation or timeout.

No `WLC_UP`, association, scanning, AP enablement, Ethernet traffic, IP, or
UDP commands are sent. A successful version/CLM query proves controller
startup, **not a usable Wi-Fi connection or measured network latency**.

`src/radio/wifi_control.c` owns the narrow SDPCM/CDC framing. It validates
length/complement, channel, header offset and payload bounds, tracks peer
credits/flow control, and processes incoming frames while awaiting the
matching control response. This is not a general asynchronous event/data
stack; that belongs to M2.

Bulk transfers use the project-owned PIO0/SM0 engine with bounded FIFO
feeding/draining, not the slow software register backend. It owns PIO0
exclusively in this standalone image. Readback is the hardware acceptance
test for transfer correctness; host models cannot establish wire timing.

USB is polled between chunks/control waits, never inside a gSPI transaction.
The whole operation has a 3,000,000 inherited-timer-tick deadline, plus
bounded wait loops. The pre-existing ROM BOOTSEL watchdog is **not fed,
disabled or extended**. Elapsed values remain ticks, not calibrated
microseconds. The usual physical Boot procedure remains the fallback.

### Verification boundary

Sanitizer-enabled tests cover simulated core/RAM access, full readback,
NVRAM placement/token, control version/CLM exchange, bad asset CRC, first
readback mismatch, missing HT readiness, nonzero controller error, wrong
request ID, cancellation, malformed packets and report corruption.
Both USB builds test request selection, status-stage deferral, stale
generation rejection and result-command isolation. These models exercise
the project implementation; they are not independent controller emulators.

The widened PIO engine still passed three fresh physical register probes
on serial `9BBF88DD17D66E7D` (`build/usb-live-runs/run.JRto3Py2`), with
`FEEDBEAD`, 16/16 repeated reads and chip-common `1545a9af`.
Firmware startup results follow. The separate full-badge unplug/replug
check remains outstanding.

### M1b physical results

**Passed on the selected 2026 badge, serial `9BBF88DD17D66E7D`.**
Final image build `ab16f8f1` passed three separate automated
flash/run/report/BOOTSEL cycles:

- `build/usb-live-runs/run.nu412ybR`
- `build/usb-live-runs/run.DaSsNBzA`
- `build/usb-live-runs/run.UsOcAL0c`

Each returned a fresh nonce/build/CRC-validated report with:

| Check | Physical result |
| --- | --- |
| Padded firmware download/readback | 224,192 bytes; CRC32 `e627c344` |
| Padded NVRAM download/readback | 744 bytes; CRC32 `fa352d20` |
| Total byte-for-byte verification | **224,936 bytes** |
| NVRAM length token | Written and read back before releasing radio CPU |
| HT clock and function-2 readiness | Passed |
| Controller version query | Valid matching command/request/interface and zero status |
| CLM data | 984 bytes; CRC32 `29aa78fd`; two acknowledged uploads |
| `clmload_status` | **0** |
| Control traffic | Four transmitted, six received SDPCM packets |
| Final state | `FIRMWARE READY / OK`, radio powered off, host-requested BOOTSEL confirmed |

Returned version (trailing line ending removed for display):

```text
wl0: Nov 29 2021 22:49:09 version 7.95.49 (2271bb6 CY) FWID 01-c51d9400
```

Elapsed operation values were 1,469,437 / 1,469,225 / 1,469,201 inherited
timer ticks, including CRC validation, radio reset, download, readback and
control queries. They are **not Wi-Fi packet latency measurements**.

The final UF2 has 240,496 payload bytes and 940 UF2 blocks. A forced
rebuild with `RP2350_CC='clang -nostdinc'` produced the same UF2 SHA256:

```text
8a86735b03e548385005c49fc0411faa47e10714acc148bfe89dd21575f6039f
```

It links with `-nostdlib`, has no unresolved symbols, and includes no
SDK/third-party host driver code. Vendor-data packaging is separate;
distribution/license review is still required before shipping those data
assets with badges.

### Two-badge USB setup confirmed

On 2026-09-15 both known 2026 badges were attached together and discovered
in BOOTSEL. Each passed the same `ab16f8f1` M1b image with full 224,936-byte
verification, version `7.95.49`, zero CLM status and USB-requested recovery:

| USB/chip serial | Evidence directory | Planned first network role |
| --- | --- | --- |
| `9BBF88DD17D66E7D` | `build/usb-live-runs/run.HBkVCILI` | SoftAP |
| `08DA26F86E57F5EA` | `build/usb-live-runs/run.sduJSgsT` | Station/client |

At this startup-only checkpoint these were planned roles, not an established
Wi-Fi link; the later paired results below supersede that limitation. Both
badges were confirmed back in BOOTSEL after testing. Select by serial, never by a
remembered bus/address or cable position.

An initial attempt to run both flash/test scripts concurrently failed to
open the second BOOTSEL device (`run.4cVeAaMh`), while the first completed.
Retrying the second alone passed. The precise host-access cause has not
been isolated; serialize bootloader discovery/flashing/recovery for now.
Concurrent radio operation and simultaneous live-report collection were
subsequently exercised by the paired tests below. Neither requires
concurrent bootloader access.

### Failures that led to the working implementation

- The first 512-byte function-1 transfers returned repeated `00001bc5`
  words instead of the firmware prefix (`run.hDNk6hK5`, `run.acIDQjU6`).
  Isolated RAM word writes/readbacks passed, while bulk download failed.
  Toggling the 32-bit access flag on reads did not fix it
  (`run.Y32A8HnU`). The SPI reference defines a **64-byte backplane block
  limit**, not 512. Applying that limit yielded full firmware/NVRAM
  verification and controller startup (`run.pSBL8BuS`). The read-flag
  experiment was reverted; no mismatch was accepted or skipped.
- The first strict version-string parser rejected the firmware's trailing
  line ending. It now accepts only a NUL-terminated printable string plus
  optional trailing CR/LF, strips that ending, and still rejects embedded
  control/nonprintable characters and unterminated strings.
- Requiring the CDC SET request bit to be echoed rejected an otherwise
  successful CLM reply (`run.DVEBaHR1`). The actual controller clears that
  bit in SET_VAR responses. Responses are instead matched by command,
  request ID, interface and error/status; the fixture reproduces this.

These cases are retained as evidence and regression tests. The current
host model enforces the 64-byte function-1 limit, backplane window
boundaries, padded variable-offset CDC headers and actual response
semantics. Passing host models alone was not sufficient.

The subsequent paired experiment below establishes station/SoftAP
association as a control-plane prerequisite, followed by the separate
static-IP UDP and application RTT results. BLE provisioning in this
freestanding track, the full physical unplug/replug check and other badge
models remain unverified or unimplemented.

## Paired AP/station control experiment

The two attached badges are now the development fixture for a separate
`usb-wifi-link` image. This is an 802.11 association/authorization slice,
not completion of the static-IP/UDP milestone:

```sh
make usb-wifi-link test-wifi-link
bash src/host/tools/usb_wifi_pair_run.sh 9BBF88DD17D66E7D 08DA26F86E57F5EA
```

The runner checks both explicit serials before flashing, then loads them
sequentially to avoid overlapping bootloader access. One host process
opens both live interfaces, starts the AP, waits for its configured
`WAIT PEER` state, and then starts the station with the same fresh session
challenge. It polls their independently validated reports. The first argument is
the AP; the second is the station. Default applications and M1b retain
their separate build targets.

The intended network is an isolated channel-1 WPA2/AES SoftAP using
worldwide regulatory configuration and the already-verified CLM data.
Each session derives a temporary SSID and passphrase from the fresh
challenge. The full challenge/passphrase must not be logged: unlike the
M1b nonce, this challenge also determines credentials. Only the SSID,
roles, MACs, states and status codes are printed. Each badge receives a
distinct locally administered MAC derived from its ROM identity.

The TFT shows **AP HOST** or **CLIENT**, serial, session SSID, current
firmware/network stage, peer MAC/count and authorization status. Amber
indicates work/waiting, green a confirmed connection state, and red an
error. It deliberately says **NO IP/UDP YET**. No passphrase appears on
the screen.

The paired collector requires both badges to report associated and
authorized peers, with each peer MAC matching the other badge's local
MAC, over twenty consecutive polling observations. An accepted AP/SSID
configuration command is not a successful connection. No Ethernet,
ARP, IP, UDP, BLE provisioning or latency benchmark is implemented here.

This image retains the radio after verified startup, services USB between
bounded operations, and polls link state. Only this experiment renews
the ROM BOOTSEL watchdog after foreground progress, no more than once
per 500,000 timer ticks. It enforces an absolute 15,000,000-tick lifetime
from boot and an 8,000,000-tick join deadline starting after each role's
configuration completes (firmware download is not charged to that join
window). Under the measured inherited
timebase these are about 31 and 17 host seconds, not calibrated guarantees.
A stuck foreground loop stops renewals; host BOOTSEL commands can end the
experiment sooner. The original M1b image still does not renew its watchdog.

### Paired physical results

**WPA2-authorized association now passes in both role assignments.**
Final image `ed36aed2` produced matching authorized peer reports for twenty
consecutive host observations, then returned both badges to BOOTSEL:

| AP serial | Station serial | Final-image evidence |
| --- | --- | --- |
| `9BBF88DD17D66E7D` | `08DA26F86E57F5EA` | `build/usb-live-runs/pair.MhaAuQsa` |
| `08DA26F86E57F5EA` | `9BBF88DD17D66E7D` | `build/usb-live-runs/pair.dSqBFIy3` |

MACs are `02:9b:bf:d6:6e:7d` and `02:08:da:57:f5:ea`, respectively.
Each side reported the other MAC as its peer; each had one peer and
authorization true. This is stronger than a successful join command or
nonzero BSSID alone: polling checks associated/authorized flags in
`sta_info`. The final collector returned:

```json
{"test":"wifi-link-pair","association_ok":true,"both_authorized":true,"stable_observations":20}
```

Earlier corrected builds also passed in the original role assignment
(`pair.VUE3CtnI`, `pair.huPw9t2L`, `pair.w51O2Iup`). These are bounded
nearby-badge tests, not an endurance or congested-conference RF campaign.

The TFT role/status path is active during these tests: AP/client labels,
firmware stages, waiting, peer MAC and green `CONNECTED` / `WPA2 AUTHORIZED`
state. Display and USB show only completed link-poll snapshots, not a
half-updated association query.

The final 244,652-byte payload (956 UF2 blocks) rebuilt identically with
`RP2350_CC='clang -nostdinc'`; it links with `-nostdlib` and no undefined
symbols. UF2 SHA256:

```text
e1d93001adb67fdd942de9f95d655d1e5b33f10135157b2ff16d80c44623c03f
```

ASan/UBSan tests cover the protected DOWN/configuration ordering, WPA2
request shapes, peer/status parsing, pending/error handling, retained
radio sessions, omitted CDC interface fields, stale request rejection and
atomic polling snapshots. Existing M1b, USB, register-probe, diagnostic
tests and the default build still pass.

An additional no-collector test returned the AP badge automatically to
BOOTSEL (`build/usb-live-runs/idle.Wbzgqi38`). The host observed recovery
after 37 seconds **including flashing, launch and polling**, not 37 seconds
of measured application uptime. This verifies unattended return without
removing the separate watchdog and absolute-lifetime safeguards.

### Paired bring-up failures and fixes

- The first pair failed with controller `-5` during configuration
  (`pair.f7P7TI3Q`). Explicit `WLC_DOWN` before protected initialization
  settings allowed configuration to proceed.
- A successful interface-1 `cur_etheraddr` reply omitted the CDC interface
  field (`pair.Z6frfszZ`). The controller also omits SET in replies.
  Requests still select the interface; replies remain bound to the sole
  outstanding command/request ID, with unexpected nonzero interface fields
  rejected. The AP MAC is read back and verified, and the pair collector
  cross-checks both reported peers rather than trusting the acknowledgement.
- Initially USB/TFT callbacks exposed temporary cleared state between the
  two queries making up a link poll (`pair.yafseq8S`). This falsely looked
  like repeated disconnects. A committed snapshot now remains visible until
  the whole poll finishes; a regression test covers this behavior.
- Starting both roles simultaneously could make the station scan before
  the AP was ready (`pair.qiRs267F`). The host now gates station startup on
  AP readiness. The join deadline was also moved after configuration:
  charging both firmware download and setup to the AP's peer-wait window
  left too little time for the subsequently started station
  (`pair.gDb5u5Hk`). The corrected reverse-role run passed without weakening
  authorization checks or removing recovery bounds.

No application Ethernet/IP/UDP payloads or measurements are claimed by
the association-only image. Its separate data-plane successor follows.

## Static-IP UDP echo and measurements

**Implemented and physically verified on the two 2026 badges.** The
`usb-wifi-udp` image adds a narrow project-owned data path while keeping
the association-only and M1b images separate:

```sh
make usb-wifi-udp test-wifi-udp
bash src/host/tools/usb_wifi_pair_run.sh 9BBF88DD17D66E7D 08DA26F86E57F5EA udp
```

Swap the serial arguments to reverse AP/station roles. The runner still
selects devices explicitly, flashes sequentially, starts the AP before
the station and verifies authorization on both. Only then does it arm the
packet test. There is no dependence on a conference network or internet.
Vendor firmware, NVRAM and CLM are unchanged pinned data exceptions;
no SDK, libc, lwIP or third-party host driver is linked into the RP2350.

### Data path and accepted packet subset

- SDPCM data-channel TX/RX with BDC framing and interface selection.
  A four-frame queue retains Ethernet packets arriving during control
  requests. Overflow/invalid-BDC drops are explicit counters, not silent
  loss. The main loop drains bounded batches before producing more traffic.
- Static IPv4: AP `192.168.77.1`, station `192.168.77.2`, UDP port **30526**.
  No DHCP, routing, TCP, DNS, IPv6, VLAN, IP options, fragments, or general
  socket API. ICMP is currently rejected rather than pretending to support
  ordinary host `ping`.
- One-entry ARP resolution/cache restricted to the authorized peer's MAC
  and expected IP. A valid ARP exchange must establish the mapping;
  knowing the Wi-Fi MAC does not itself count as resolved. Requests are
  bounded to five attempts, with a 250,000-tick resolution deadline and
  2,000,000-tick cache lifetime during an active test.
- Ethernet/IP/UDP length, address and checksum validation. IPv4 must have
  version 4, IHL 5, nonzero TTL and a valid header checksum; DF is allowed,
  fragmentation is not. UDP lengths/ports must match and a **nonzero,
  valid UDP checksum is required** by this lab's deliberately narrow
  policy. Ethernet padding is excluded from checksum/payload validation.
- Explicit reject categories cover length, address, unsupported protocol,
  checksum, payload and stale/duplicate traffic.

Each badge both initiates and answers echo requests. The 32-byte payload
contains `U26E`, request/reply type, original sender role, length, sequence,
sender timer stamp and a deterministic verification pattern. Replies must
match the outstanding sequence, timestamp and full pattern. No Wi-Fi
challenge/passphrase is put in the packet. Zero-time-resolution synthetic
RTTs are represented as one tick; zero sample slots mean loss.

Each direction sends 100 requests, with at most one outstanding request.
The minimum scheduling interval is 10,000 timer ticks, and each request
has a 100,000-tick response deadline. There is no application-level retry
after a UDP timeout; normal radio-layer retries may still occur. `lost`
therefore measures **round-trip echo timeouts**, not separately localized
request-versus-response or 802.11 frame loss.

Finishing one's own 100 requests does not stop replies to the other badge.
After both directions settle, the host freezes each result before reading
its 100 samples. It checks counts, sample sum/min/max, matching report
identity and per-page CRCs. A partial or stale report cannot become a
successful completed benchmark.

### Timing and TFT behavior

Each badge measures its timer against at least 128 USB SOF intervals
before packet traffic. Conversion on the host is:

```text
RTT milliseconds = RTT ticks * measured USB-frame count / calibration ticks
```

Do not assume that a tick is one microsecond, or that both badges have
exactly the same timer rate. The host reports min, mean, p50, p95, p99 and
max, using nearest-rank percentiles over received replies only, alongside
the raw 100-slot sample array and loss count.

These measurements include application scheduling, GPIO/PIO bus work,
controller queues, echo processing, periodic link-status queries and
USB/TFT servicing. They are **not one-way latency, radio airtime, or an
optimized SDK/driver comparison**. The bus and inherited startup clocks
remain conservative. The test is instrumented, and its tail latency should
not be hidden by reporting only averages.

The TFT retains the AP/client label and adds static IP, UDP phase, sent/
received/lost counts and latest RTT explicitly labeled in **timer ticks**.
The UDP image reuses the existing changed-tile display pattern instead of
rewriting the entire screen for each counter update.

### Physical UDP results

Build **`4c6ff5b1`**, three fresh sessions, 100 requests per direction:

| Run | Initiating role / badge suffix | Replies | Mean RTT ms | p95 ms | Maximum ms |
| --- | --- | ---: | ---: | ---: | ---: |
| `pair.oGuFy6Su` | AP / `6E7D` | 100/100 | 8.879 | 11.069 | 67.824 |
| `pair.oGuFy6Su` | Station / `F5EA` | 100/100 | 9.241 | 16.224 | 47.907 |
| `pair.z3mul6zV` | AP / `6E7D` | 100/100 | 8.278 | 11.079 | 50.457 |
| `pair.z3mul6zV` | Station / `F5EA` | 100/100 | 8.804 | 11.056 | 64.202 |
| `pair.LISPCeE7` | AP / `F5EA` | 100/100 | 8.232 | 9.640 | 52.265 |
| `pair.LISPCeE7` | Station / `6E7D` | 100/100 | 8.370 | 9.478 | 40.669 |

Evidence directories are under `build/usb-live-runs/`. Each retained
`reports.jsonl` contains all samples and calibration values. Across these
runs: **600/600 completed echo transactions**, zero observed losses,
duplicates, rejected frames or radio queue drops. Each badge also answered
100 peer requests per run; ARP exchanges and raw Ethernet TX/RX counts
were nonzero and consistent. Both badges returned to BOOTSEL after each
session. This is short-run evidence on nearby badges, not an endurance
test, congested-conference result or guarantee of zero loss.

The image has 250,216 payload bytes, 978 UF2 blocks, and rebuilt identically
with `RP2350_CC='clang -nostdinc'`. It links with `-nostdlib` and no
undefined target symbols. UF2 SHA256:

```text
3e60f5b8ef1d7b25b87fb95f091ec617c5b637b3a4ffdca1615e3afe9ad874e3
```

Sanitizer tests cover independent packet/checksum vectors, malformed and
truncated frames, timer wrap, two-engine exchange with losses, 100-loss
completion, mixed data/control RX queuing, queue overflow accounting,
frozen result pages, challenge validation and cancellation. Existing
association, M1b, USB, register, diagnostic tests and the default build pass.

### Recovery and remaining work

Only the UDP image extends the absolute application lifetime to
30,000,000 inherited ticks (about 63 seconds on the tested host badge).
The packet engine has its own 15,000,000-tick deadline. Progress-gated
watchdog renewal and host-requested BOOTSEL remain enabled; the host
collector has a 90-second process alarm. Association-only and M1b images
retain their previous bounds.

An unattended test of this image returned to BOOTSEL approximately
**64 host seconds after launch**, without a collector
(`build/usb-live-runs/udp-idle.4EEQWd9N`; 69 seconds including flashing).
This is an observation with host polling granularity, not a precise timer
guarantee or a fault-injection campaign.

Next work is multi-peer addressing/admission and four-player state
exchange, plus throughput, longer-run loss and tail-latency investigation.
External AP/host interoperability, independent packet capture, ICMP,
other badge models, full physical power-cycle testing and BLE provisioning
remain unverified or unimplemented as applicable.
