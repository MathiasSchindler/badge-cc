# Project-owned live USB report interface

**Physically verified on the tested 2026 RP2350 A4 badges.** The host can
now obtain verified reports directly while the application is running,
without a target-side SDK, libc, TinyUSB, radio driver, or serial stack.

This replaces the failed [retained-SRAM approach](usb-report.md). Reports
are read before reboot, so BOOTSEL's RAM clearing is no longer a problem.
Nothing attempts to disable that clearing or alter security settings.

This guide covers the original known-report, bus-probe, vendor-firmware,
paired-link and UDP images; radio capabilities depend on the selected image.
The [working Wi-Fi host baseline](wifi.md#static-ip-udp-echo-and-measurements)
uses vendor CYW43439 firmware and completed 600/600 paired echoes. USB
report integrity is not evidence of source-only RX or RF qualification.
For source-radio and vendor-retained PSM experiments, use the
[current handoff](cyw43/handoff.md) and [research workflow](cyw43/workflow.md),
including its fixed catalogue, fresh per-trial boots and batch-boundary
frozen recovery. Do not apply the older image lifetimes or USB commands
below to those experiments.

## Run a complete automated cycle

Put one badge in BOOTSEL mode once, then run:

```sh
make picotool-usb
bash src/host/tools/usb_live_run.sh 08DA26F86E57F5EA
```

The serial above identifies the 2026 badge used for validation. Use the
actual serial for another device. The script requires explicit serial
selection and checks RP2350 identity before replacing its application.

The script:

1. Builds the live-report firmware and host collector.
2. Verifies the selected device is in BOOTSEL.
3. Loads/verifies `build/universe26/uf2/usb_live_report.uf2` and executes it.
4. Finds its live USB interface using the same chip-ID-based serial.
5. Checks the protocol/build identity.
6. Verifies that a corrupted request and a report read without a valid
   request are rejected, then checks recovery on the next control request.
7. Sends three independent fresh nonces and verifies three 64-byte reports.
8. Requests BOOTSEL and confirms that the selected badge returns.

No button press, unplugging, or photograph is required between successful
cycles. Logs and JSON Lines results are kept under
`build/usb-live-runs/run.*`. The report firmware remains installed; each
run replaces the selected badge's previous application.

To collect real Wi-Fi bus-probe results instead of known test values:

```sh
bash src/host/tools/usb_live_run.sh 08DA26F86E57F5EA wifi
```

Use `wifi-pio` instead of `wifi` to select the optional project-owned PIO
engine; `wifi` keeps the software engine. Both now pass the register/
backplane probe on the 2026 badge tested on 2026-09-15.

This also collects three fresh reports. **`verified: true` means that the
report's transport, build, nonce, and checksum passed validation;
`probe_ok` separately indicates whether the radio probe passed.** A
correctly collected radio failure is not a USB collection failure, so the
collector exits successfully while preserving `probe_ok: false` and the
exact stage/error/values. The app does not load radio firmware, associate
with a network, or enable a hotspot.

The separate M1b image explicitly loads vendor radio firmware and configuration:

```sh
make usb-wifi-firmware test-wifi-firmware
bash src/host/tools/usb_live_run.sh 9BBF88DD17D66E7D wifi-fw
```

This mode collects **one** fresh result per cycle, using a separately
fingerprinted image and collector. `verified` still means report integrity;
`firmware_ok` separately requires complete firmware/NVRAM readback, a valid
version response, and successful CLM load/status. A controller failure is
retained as evidence and is not a USB transport failure. No network is
joined or started. See [the M1b boundary](wifi.md#m1b-implementation-firmware-startup-only)
and [asset provenance](wifi-assets.md).

For separate build/test steps:

```sh
make usb-live-report
make test-live-usb
```

The host executable can also collect 1-10 reports from an already-running
device:

```sh
build/host-tools/usb_live_host 08DA26F86E57F5EA 3
```

Append `wifi` to select the bus probe:

```sh
build/host-tools/usb_live_host 08DA26F86E57F5EA 3 wifi
```

It requests BOOTSEL after collection. Host tools use libusb and the host
C library; neither is part of the RP2350 executable.

## Target implementation

| Component | Role |
| --- | --- |
| `src/usb/live_usb_control.c` | Portable endpoint-zero state machine, descriptors and vendor requests |
| `src/usb/live_usb_hw.c` | Direct-register PLL/USB/DPRAM configuration and polling |
| `src/tools/usb_live_report.c` | Application, display, chip identity and recovery lifecycle |
| `src/shared/usb_report.c` | Existing strict request/response codec and CRC |
| `src/arch/rp2350/bootrom_reboot.c` | ROM lookup, chip ID and BOOTSEL scheduling |
| `src/radio/wifi_usb_report.c` | Versioned, nonce-bound radio result codec |
| `src/radio/wifi_probe.c` | Existing bounded cold-reset/register/backplane probe |

This is a USB full-speed, vendor-specific interface with **endpoint zero
only**. No CDC serial port, bulk endpoint, interrupt handler, OS, allocator,
or filesystem is implemented on the badge.

Descriptors fit one 64-byte control packet. SET_ADDRESS and
SET_CONFIGURATION are committed after the status stage. Each new SETUP
aborts the prior endpoint-zero buffers and pending actions. Bus reset
invalidates address/configuration/report state. Unsupported or malformed
requests stall rather than returning success-shaped data.

The controller uses GPIO-independent native USB DP/DM hardware. The PHY
is selected internally, the device-mode VBUS-detect override is enabled,
and the internal full-speed pull-up is used. Known-report mode does not
initialize radio functions or sensor buses. An explicit Wi-Fi request
initializes only the existing probe's radio GPIOs and GPIO7/9 inputs;
it does not configure IMU, touch, ADC, IR, or PSRAM. The display retains
its existing driver and switched-rail behavior.

The USB clock is configured as 48 MHz from the board's 12 MHz crystal and
the USB PLL (feedback 100, post-dividers 5 and 5). The driver refuses to
reset that PLL if it is currently clocking the executing core. Clock/reset
waits and endpoint abort waits are bounded. Buffer ownership is handed to
the USB controller only after the payload/control fields have been written.

The USB serial comes from the ROM's `GS` chip-info query, not a hard-coded
badge identifier. On the tested A4 it matches the BOOTSEL serial.

### Recovery

Before configuring USB, the application schedules a ROM BOOTSEL reboot.
The requested delay is 8000 ms; it is not refreshed by USB traffic. This
limits the experiment's lifetime even without a collector.

**Do not treat that ROM delay as calibrated wall-clock time under our
minimal startup.** The no-collector hardware check observed BOOTSEL after
approximately 18 host seconds. The screen deliberately says automatic
return rather than promising an exact eight-second interval.

The host can request an earlier return through the vendor protocol. A
reboot is scheduled only after that request's status packet completes.
If the driver reports an error, it disconnects and requests BOOTSEL.
If recovery is not observed, the physical Boot procedure remains the
fallback. No watchdog hang/fault-injection campaign has been performed.

That statement describes this live-USB image's qualification, not a universal
recovery guarantee. Separate BA05 probes changed clocks/core state and failed
to return automatically despite successful ROM reboot scheduling; manual
BOOTSEL recovery and full flash restoration were required.
The corrected isolated BA05 ladder later passed scheduled-watchdog-only
recovery with inherited clocks and after a safe switch to XOSC. The full
bounded contention diagnostic also subsequently passed automatic BOOTSEL
return and verified original-flash restoration. Neither result qualifies
recovery from arbitrary faults.
The [runtime qualification guide](rp2350-runtime.md) records the clock-source
restriction and staged recovery tests. Do not transfer this image's successful
recovery to a different startup, clock-switch or multicore configuration.

The host waits up to roughly five seconds for live USB enumeration and
then uses bounded one-second control-transfer timeouts. Recovery polling
is separate. A 30-second host process alarm also bounds a stuck libusb
operation. Any missing device, wrong build, transfer error, malformed
report, or absent recovery results in a failing exit status.

## Protocol

The experimental lab VID/PID is **`cafe:4026`**. This is not a production
USB identifier allocation and must be replaced with appropriately assigned
identifiers before distribution. Device/interface identity, serial, and
protocol/build checks are all required; matching VID/PID alone is not enough.

All vendor requests require configuration 1 and `wIndex=0`. `wValue=0`
except for the trace-page selector described below:

| Request | bmRequestType | bRequest | Data stage |
| --- | ---: | ---: | --- |
| Submit request | `0x40` | `0x50` | Exactly 64 OUT bytes |
| Read report | `0xc0` | `0x51` | Exactly 64 IN bytes |
| Return to BOOTSEL | `0x40` | `0x52` | No data |
| Protocol/build information | `0xc0` | `0x53` | Exactly 12 IN bytes |
| Submit Wi-Fi probe | `0x40` | `0x54` | Exactly 64 OUT bytes, request test ID 2 |
| Read Wi-Fi probe result | `0xc0` | `0x55` | Exactly 64 IN bytes |
| Read first-transaction trace page | `0xc0` | `0x56` | Exactly 64 IN bytes; `wValue` = page 0-10 |
| Submit Wi-Fi probe using PIO | `0x40` | `0x57` | Exactly 64 OUT bytes, request test ID 2 |
| Submit firmware startup (separate image only) | `0x40` | `0x58` | Exactly 64 OUT bytes, request test ID 3 |
| Read firmware startup result (separate image only) | `0xc0` | `0x59` | Exactly 64 IN bytes |
| Start paired SoftAP (link image only) | `0x40` | `0x5a` | Exactly 64 OUT bytes, request test ID 4 |
| Start paired station (link image only) | `0x40` | `0x5b` | Exactly 64 OUT bytes, request test ID 4 |
| Read link-state snapshot (link image only) | `0xc0` | `0x5c` | Exactly 64 IN bytes |
| Arm UDP benchmark (UDP image only) | `0x40` | `0x5d` | Exactly 64 OUT bytes, same test-ID-4 challenge as active link |
| Read UDP progress (UDP image only) | `0xc0` | `0x5e` | Exactly 64 IN bytes |
| Read frozen UDP detail page (UDP image only) | `0xc0` | `0x5f` | Exactly 64 IN bytes, `wValue` page 0-21 |
| Freeze/read completed UDP result (UDP image only) | `0xc0` | `0x60` | Exactly 64 IN bytes |

Information bytes: ASCII `U26L`, version u16 = 1, report length u16 = 64,
source-build fingerprint u32; integers are little-endian.

The 64-byte request/report retains the [known-report format](usb-report.md#ram-layout-and-freshness):
version, length, source-build ID, test ID, fresh 128-bit nonce, status,
known values, and CRC32. The response is mirrored in reserved application
RAM, but the host reads it through the USB control endpoint, not after
reboot. The mailbox address field is a known test field; the interface
does not expose arbitrary memory reads/writes.

Submitting a new request invalidates the preceding report immediately.
A request with bad CRC/build/version/length is rejected. No report is
readable until a valid request has been processed. The host compares
the returned nonce with its current request, so previous successful
reports cannot pass as new results.

### Wi-Fi probe lifecycle and payload

The Wi-Fi request uses the same `U26Q` envelope as the known-value test but
with test ID **2**, not 1. The endpoint handler validates it, acknowledges
the status stage, and only then makes the probe available to the main loop.
The probe is never run inside the control OUT handler.

USB is serviced between probe stages, outside gSPI transactions. The
host retries a stalled result read while the bounded probe is not yet
finished. A new submit, deconfiguration, or USB reset invalidates the old
generation; a delayed old completion cannot publish a report for the new
request. Duplicate completion is also ignored. A pending reboot prevents
publishing a new result. The application powers the radio off after every
run, whether the probe succeeded or failed.

The radio response is a separate 64-byte `U26W` record:

| Offset | Field |
| ---: | --- |
| 0 | Magic `U26W` |
| 4 | Version u16 = 1 |
| 6 | Length u16 = 64 |
| 8 | Source-build fingerprint u32 |
| 12 | Test ID u32 = 2 |
| 16 | Echoed 16-byte nonce |
| 32 | Packed probe state u32, described below |
| 36 | Reset-order test register u32 |
| 40 | Normal-order test register u32 |
| 44 | Bus control readback u32 |
| 48 | Observed chip-common ID u32 |
| 52 | Last read value u32 |
| 56 | Elapsed timer ticks u32 |
| 60 | CRC32 over bytes 0-59 |

All integer fields are little-endian. Packed-state bits are stage `[3:0]`,
error `[7:4]`, reset-attempt count `[11:8]`, successful-read count `[16:12]`,
radio-powered flag `[17]`, and raw ALP clock CSR `[25:18]`; bits `[31:26]`
are reserved zero. The powered flag describes the end of the probe itself,
before the application's unconditional power-off cleanup.

Unreached fields stay zero and must be interpreted using the stage. The
timer is inherited from minimal startup, so elapsed values are exported
as `elapsed_ticks`, not claimed as calibrated microseconds or Wi-Fi RTT.
The host validates range/shape and consistency before printing JSON.
Radio error codes and labels are retained even when transport validation
succeeds.

The existing known-report commands remain available. Reading a radio
report through the known-report command, or vice versa, stalls instead
of returning a misleading payload.

### Firmware startup result and detail pages

Firmware requests use the `U26Q` request envelope with test ID **3**. They
retain the same deferred execution, nonce/build/CRC checks, cancellation
and generation rules. The original register-only image rejects both new
opcodes. The separate firmware image uses PIO for firmware loading even
though it also retains software/PIO register-only request modes.

The summary is `U26F`, version 1, length 64, test ID 3. Offsets 8 and 16
retain the build ID and nonce. All words are little-endian:

| Offset | Firmware result field |
| ---: | --- |
| 32 | Stage byte, error byte, two reserved zero bytes |
| 36 | Last/failing radio address |
| 40 | Expected value |
| 44 | Actual value |
| 48 | Fully verified firmware plus NVRAM byte count |
| 52 | Elapsed inherited timer ticks |
| 56 | Last controller error/status |
| 60 | CRC32 of bytes 0-59 |

Success requires stage `FIRMWARE READY`, error `OK`, complete readback,
nonempty bounded version and successful control exchanges. Failed stages
retain partial byte counts and the exact failure fields; zeros in
unreached fields are not passes.

The existing `0x56` command returns eleven nonce/build/test-bound `U26T`
pages containing a **firmware detail record**, not a first-transaction
wire trace, when the last request was test ID 3. Its 256-byte payload is:

| Offset | Detail field |
| ---: | --- |
| 0 | Format magic `0x31574657` |
| 4, 8 | Padded firmware length, CRC32 |
| 12, 16 | Padded NVRAM length, CRC32 |
| 20, 24 | CLM length, CRC32 |
| 28, 32 | Sent/received SDPCM packet counts |
| 36-63 | Last received control frame, or failing readback block prefix (zero-padded to 28 bytes) |
| 64-255 | NUL-terminated controller version, up to 191 characters |

On a readback failure, detail offsets 36-47 retain the first twelve
received block bytes; offsets 48-51 contain the gSPI interrupt status, and
52-63 contain isolated word reads at the block's first, second and last
word addresses. On a malformed control packet, the prefix retains twelve
SDPCM header bytes followed by sixteen CDC header bytes when in bounds.
A rejected version string instead retains up to 28 payload bytes.
Interpret these diagnostics using the summary stage/error, not as a
successful version or arbitrary memory dump.

Page transport uses the existing length/index/checksum validation. There
is no arbitrary address or memory access command. These pages only expose
fixed diagnostic fields, not network credentials.

### Paired link image

`make usb-wifi-link` builds a separate TFT/USB AP-station control experiment.
See [the paired runner and acceptance criteria](wifi.md#paired-apstation-control-experiment).
Its role-selection request uses test ID 4 and the same strict `U26Q`
envelope. Its challenge also derives temporary network credentials and
must not be printed in logs. The paired host does not log it.

Link snapshots are `U26N`, version 1, length 64, with build ID, test ID 4
and the echoed challenge at the normal envelope offsets:

| Offset | Link snapshot field |
| ---: | --- |
| 32 | Role byte: 0 AP, 1 station |
| 33 | Stage byte |
| 34 | Error byte |
| 35 | Authorization flag |
| 36 | Last controller command u32 |
| 40 | Controller status u32 (negative statuses retain their two's-complement bits) |
| 44 | Peer count u32 |
| 48 | Local MAC, six bytes |
| 54 | Peer MAC/BSSID, six bytes |
| 60 | CRC32 over bytes 0-59 |

Snapshots may update while the experiment runs, but an already-started
control IN transaction retains its copied payload. A multi-command link
poll keeps the preceding completed snapshot visible until all of its
queries finish; transient internal resets are not published as link loss.
A new submission,
USB reset/deconfiguration or reboot request cancels the old generation.
The paired image supports known reports but rejects the standalone
register/M1b start opcodes instead of running an unintended operation.

On a reported failure, the host also reads `0x56` diagnostic pages. Test
ID 4 uses detail magic `0x314b4e4c`; bytes 4/5 identify interface and SET,
byte 6 is the iovar-name length (at most 31), bytes 8-39 hold the bounded
name only, words 40/44 hold firmware error and verified byte count, and
48/52 hold expected/actual diagnostic values. Bytes 64-91 retain the
SDPCM/control-header prefix (on an interface mismatch, twelve SDPCM bytes
plus the decoded sixteen-byte CDC header). No iovar payload is included.
The host reads these pages after a terminal
failure, when the diagnostic content has stopped changing.

Only this image renews the ROM recovery watchdog after foreground
progress, with a separate absolute experiment lifetime. This is not an
unlimited USB keepalive. Host-requested BOOTSEL and stalled-loop recovery
remain required; see the paired experiment's tick-based bounds.
The paired host process has a 60-second alarm; the original single-badge
collector retains its 30-second alarm. The runner checks each selected
badge's BOOTSEL recovery separately and reports missing recovery as failure.

The final paired image `ed36aed2` passed with both AP/station role
assignments, including matched authorized peers and host-requested
recovery. A separate no-collector test also recovered automatically.
See [the exact evidence and limitations](wifi.md#paired-physical-results).

### UDP benchmark image and frozen results

`make usb-wifi-udp test-wifi-udp` builds the next opt-in image. Append `udp`
to the paired runner to use it:

```sh
bash src/host/tools/usb_wifi_pair_run.sh AP_SERIAL STATION_SERIAL udp
```

The host establishes and validates the WPA2 link first. It then submits
`0x5d` on both devices, using the active link's challenge. Work starts only
after the control status stage completes. A different challenge, CRC error,
reset, deconfiguration or superseding request cannot publish an old
benchmark as the new one. The normal host workflow uses one benchmark
per fresh link session; the challenge remains absent from its logs.

The `0x5e` snapshot is `U26U`, version 1, length 64, build ID, test ID 4 and
the echoed challenge in the usual first 32 bytes:

| Offset | UDP snapshot field |
| ---: | --- |
| 32 | UDP stage byte |
| 33 | UDP error byte |
| 34 | Role byte: 0 AP, 1 station |
| 35 | ARP-resolved flag |
| 36 | Sent count u16, received count u16 |
| 40 | Timed-out count u16, echoed-peer-request count u16 |
| 44 | Rejected-frame count u32 |
| 48 | Latest accepted RTT in timer ticks u32 |
| 52 | USB-frame calibration timer span u32 |
| 56 | Benchmark elapsed ticks u32 |
| 60 | CRC32 of bytes 0-59 |

Before completion, zero sample slots can mean either not yet attempted or
timed out; counts and stage disambiguate them. Completed success requires
100 sent and `received + timed_out = 100`, not merely an initialized socket
or a successfully queued transmission.

After **both** devices finish their own requests, the host calls `0x60`.
This freezes a coherent summary/detail pair; `0x5f` is unavailable before
freezing. The device continues answering the peer after finishing its own
requests until the host freezes the results. A fresh benchmark invalidates
the preceding frozen result.

Detail is 512 bytes carried in 22 `U26T` pages: the normal 32-byte identity
envelope, page index/count/payload length at 32-34, reserved zero at 35,
up to 24 bytes at 36-59, and CRC at 60. The final page has eight payload
bytes and sixteen zero-padding bytes. Every page must match the frozen
summary identity and expected index.

| Detail offset | Field |
| ---: | --- |
| 0 | Magic `0x31504455` |
| 4-7 | Role, stage, error, ARP-resolved bytes |
| 8 | Sample-slot count u32 = 100 |
| 12, 16, 20, 24 | Sent, received, timed out, peer requests echoed |
| 28, 32 | Rejected frames, duplicate counter |
| 36, 40 | ARP transmit/receive counts |
| 44, 48 | Calibration timer span, USB SOF interval count |
| 52 | Benchmark elapsed ticks |
| 56, 60, 64 | Ethernet TX, RX, radio RX queue/BDC drops |
| 68, 72, 76 | Accepted RTT min, max, sum (ticks) |
| 80 | Last packet-drop category |
| 84, 88 | Local and peer IPv4 values |
| 92-111 | Reserved zero |
| 112-511 | 100 little-endian RTT samples, zero for lost requests |

The host verifies sample counts and min/max/sum against the frozen
metadata. It converts each badge's samples using that badge's calibration
and reports mean and nearest-rank p50/p95/p99, as well as raw samples.
Failed or all-lost runs cannot produce fabricated RTT values.

This image uses a 30,000,000-tick absolute lifetime, a separate
15,000,000-tick packet-test deadline, progress-gated watchdog renewal and
a 90-second host-process alarm. A no-collector hardware test recovered
about 64 host seconds after launch. The existing shorter images keep
their previous behavior.

Three real two-badge runs completed 600/600 echo transactions with zero
observed loss and verified recovery. See [measurements and their
limitations](wifi.md#static-ip-udp-echo-and-measurements); USB collection
integrity, Wi-Fi authorization and actual UDP success are separate checks.

The final M1b image (`ab16f8f1`) passed three independent cycles on serial
`9BBF88DD17D66E7D`, including full firmware/NVRAM readback, version and CLM
queries, malformed-request rejection and BOOTSEL recovery.
See [exact results and retained failures](wifi.md#m1b-physical-results).

### First physical radio reports

The first integrated run returned three independently verified reports
from serial `08DA26F86E57F5EA`, all showing:

```text
stage:          RESET ORDER TEST
error:          TEST PATTERN MISMATCH
reset_pattern:  5fffffff
attempts:       10
passed_reads:   0
probe_ok:       false
```

Evidence: `build/usb-live-runs/run.JxgAhWGP`.

Two bounded timing experiments were also collected through the interface:

- Sampling after the falling edge returned `bffffffe`, still a mismatch
  (`run.aZcyV9q1`).
- Returning to the original sample point but shortening the half-clock
  delay returned `5fffffff`, still a mismatch (`run.suQ2uCh4`).

Neither experiment passed the fixed register check. Both changes were
reverted; no guessed expected value or firmware-loading bypass was added.
The USB feedback path is physically established, while radio bus
bring-up remained unresolved at that point. The subsequent 2026-09-15
trace-led investigation resolved it; see [the Wi-Fi results](wifi.md#2026-09-15-registerbackplane-probe-passes).

The final integrated image (`62a939e3`) was then checked in both modes:
three verified known-value reports in `run.h1DzE38X`, followed by three
verified radio reports in `run.DRgBeSJK`. The latter again recorded
`5fffffff` with the original timing. Both cycles returned to BOOTSEL
without manual intervention. Sanitizer regressions passed, and a clean
`-nostdinc` build was byte-identical with no undefined ELF symbols.

## First-transaction trace (added 2026-09-15)

The collector reads eleven trace pages after each validated Wi-Fi report.
Each page is a separate 64-byte `U26T` envelope with the same version,
length, build ID, test ID, and nonce as its parent report. Bytes 32-35
contain page index, page count (11), payload length (24, or 16 on the last
page), and reserved zero. Bytes 36-59 contain the trace slice, zero-padded
on the last page; bytes 60-63 hold CRC32. Invalid indices, wrong parent,
bad CRC, lengths, and nonzero padding are rejected. A new request/reset/
deconfiguration invalidates the previous trace. Pages are immutable
until the next request.

The assembled trace is 256 bytes, exported as 64 little-endian words in
`trace_words`, plus convenient `first_tx`, `first_rx`, `backend`, and
USB-frame calibration fields. Word layout:

| Word(s) | Meaning |
| --- | --- |
| 0 | Trace format magic `0x31525457` (`WTR1`) |
| 1 | Snapshot-valid bits 0-5; bit 31 marks completed first transfer |
| 2, 3 | Submitted TX and clocked RX byte lengths |
| 4 | First transfer-call elapsed timer ticks, including trace/backend setup |
| 5 | Backend: 0 software, 1 PIO |
| 6-7 | Up to eight submitted TX bytes in wire order |
| 8-12 | Up to twenty RX bytes before register byte-order conversion |
| 13, 14 | Timer ticks over the measured USB frame interval; frame count |
| 15-18 | Raw CLK_REF control/divider, CLK_SYS control/divider |
| 19-20 | Raw TIMER0 and watchdog tick dividers |
| 21-28 | GPIO control/pad pairs in signal order: enable 23, clock 29, CS 25, data 24 |
| 29-58 | Six snapshots, five words each: timer, SIO input, SIO output, SIO OE, GPIO24 status |
| 59 | TIMER0 tick control |
| 60-61 | Software-only sampled TX pad bits and bit count |
| 62-63 | Reserved zero |

Snapshots are radio enable rising, pre-transfer idle, CS asserted, data
output enabled, data released for receive, and transfer finished. PIO
performs turnaround autonomously, so it does not claim a CPU-captured
release snapshot: bit 4 remains clear. GPIO configuration words describe
initial SIO setup; the PIO backend temporarily selects PIO function 6.

This is digital instrumentation, not a logic analyzer: CPU snapshots are
sequential register reads and add overhead. Only the software backend
captures actual transmitted pad samples (word 60); `first_tx` is otherwise
the bytes submitted to the bus, not an independent analog measurement.
No USB/display servicing occurs during a transfer. Calibration uses 32
USB SOF intervals before the probe; a missing calibration is represented
explicitly by zero frame/tick counts, not an assumed clock rate.

The trace always captures the **first** read attempt. With the corrected
startup that attempt returned `03 03 03 03` in the observed runs, while
the second attempt produced `FEEDBEAD`. Do not confuse the trace's first
response with the report's final reset-test value.

Final validation on 2026-09-15 used serial `9BBF88DD17D66E7D` and image
`8d6c9fd0`. Software (`run.59FKwjMD`) and PIO (`run.3gUKtcm1`) each
returned three reports with `probe_ok: true`, matching test constants,
16 successful repeated reads, ALP `48`, and chip-common ID `1545a9af`.
Both cycles ended in BOOTSEL with all trace pages verified.

During development, one host collector stayed blocked after receiving
all reports and the device returning to BOOTSEL (`run.hn6pV0tg`). It was
terminated explicitly; that cycle is not recorded as a clean collector
exit. The device now allows a short post-status grace before USB
disconnect, and the host has a 30-second process deadline in addition
to individual libusb timeouts. Timeout exits are errors; the shell runner
still checks BOOTSEL recovery independently.

## Verification on 2026-09-14

On serial `08DA26F86E57F5EA`, RP2350 A4:

- First live cycle returned three fresh reports with verified build,
  nonce, CRC, and `feedc0de` payload, followed by host-requested BOOTSEL.
  Evidence: `build/usb-live-runs/run.T43KVNga`.
- Second cycle returned three more fresh reports and physically confirmed
  corrupt-request rejection, unavailable-report rejection, and endpoint
  recovery on subsequent requests. Evidence:
  `build/usb-live-runs/run.hQgViSTe`.
- The final image repeated both rejection/recovery checks and returned three
  more verified reports: nine fresh reports across three physical cycles.
  Final-image evidence: `build/usb-live-runs/run.pQ3kDGbI`,
  source-build fingerprint `b8388795`.
- With no collector running, the badge automatically returned to BOOTSEL;
  host observation was approximately 18 seconds after the reboot command.
- The target builds with `-nostdlib`, and a clean `-nostdinc` build produces
  the identical UF2. The ELF has no undefined symbols.
- Sanitized tests exercise descriptors, truncation, control phases,
  deferred address/configuration, cancellation, multiple fresh reports,
  bad lengths/CRC, unavailable reports, bus reset, and reboot-after-status.

These results establish a usable machine-readable USB feedback path on
this badge and Mac. Other board revisions, operating systems, hubs, long
runs and USB conformance remain untested.
