# Freestanding badge host for clean-rx

This opt-in RP2350 application cold-loads the source-built CYW43439 image,
receives raw 802.11 frames over Function 2, and reports evidence over the
project-owned USB control endpoint. Neither processor links libc, the Pico
SDK, a downloaded vendor executable, NVRAM, or CLM. The controller still
reads its on-chip ROM calibration tables.

This is a bounded radio test, not a WiFi connection manager. It does not
associate, implement a WPA2 handshake, or replace the default networking
targets. Host USB collection uses the repository's existing libusb dependency.

**Current qualification:** cold RX and single-frame **11 Mbps CCK OTA TX** are
verified between a Universe badge and a Pimoroni Tufty2350, in both directions.
The receiver independently checks the entire MPDU, FCS and fresh challenge.
The separate [Micro Signal OFDM profile](../../docs/micro-signal.md#80211g-legacy-ofdm)
now qualifies raw 6/9/12/18/24/36/48/54 Mbps legacy OFDM in both directions
between two Universe badges, at six MPDU sizes through 2,332 bytes.
Use `make micro-signal-g` for the normal fixed-6-Mbps button application or
`make micro-signal-g-test` for that all-rate sweep. This badge diagnostic
application still uses its original CCK challenge protocol.
The 1 Mbps experiments have not passed; do not generalize the qualification
to other CCK rates, automatic ACK timing, complete 802.11g MAC operation,
association or WPA2 connectivity.

## Build and test

From the repository root:

```sh
make clean-rx-badge
make -C clean-rx/badge test
```

Outputs are under `build/clean-rx/`; the badge image and collector are under
`build/clean-rx/badge/`. The image identity includes the linked controller
binary, host sources, relevant headers, collector and build rules.

For an isolated comparison with another source-built image implementing the
same mailbox ABI, set `CONTROLLER_BIN` to its absolute binary path and `BUILD`
to a separate absolute output directory. The default remains the firmware
built from `clean-rx/src`; a comparison image is not qualified by this option.
Use its matching collector, since the embedded firmware changes the build ID.
Do not use the vendor networking image with this custom mailbox loader.

The host qualifies gSPI, holds the controller CPU in reset, clears all
512 KiB of controller RAM, downloads and reads back the executable, initializes
the mailbox, and performs the firmware's HT-clock handshake. It requests
promiscuous reception with TX disabled by default. Burst accesses split at
64-byte boundaries, including across backplane windows.

Each request is bound to a fresh nonce and the compiled build identity.
Collection validates the report and every trace page's CRC/binding. This
image selects a 2,048-byte trace; existing applications retain their
256-byte default. Loader stages have a six-million-tick aggregate deadline,
and the badge schedules independent BOOTSEL recovery before USB startup.
Reception lasts 700,000 timer ticks; enabled-TX trials also have a
200,000-tick response window. These timer counts are not independently
calibrated wall-clock or RF timing measurements.

The 320x240 TFT shows test status and the received packet count. It uses the
existing RP2350 display driver; no display protocol is added to the WiFi chip.
The framebuffer adds 153,600 bytes of RP2350 RAM. A TX-confirmed screen requires
the same saved-frame validation as the collector, not just TX completion.

## Authorized physical test

Only run these commands against an explicitly authorized badge. Set `SERIAL`
to its 16-character uppercase BOOTSEL serial.

```sh
PICOTOOL=build/picotool-usb/picotool
SERIAL=YOUR_BADGE_SERIAL

# Preserve the installed program before the first trial.
$PICOTOOL save -p build/clean-rx/badge/original-badge.bin --ser "$SERIAL"

$PICOTOOL load -v -x build/clean-rx/badge/clean_rx.uf2 --ser "$SERIAL"
build/clean-rx/badge/collector "$SERIAL" build/clean-rx/badge/rx.bin 1

# Wait for independent recovery and confirm the same badge is in BOOTSEL.
sleep 9
$PICOTOOL info -a --ser "$SERIAL"

# Check full MPDU FCS and match the configured SSID locally.
build/clean-rx/badge/collector --inspect \
  build/clean-rx/badge/rx.bin ssid-data/ssid.txt
```

For sustained-receive failures, the separate
[RF-free transport test](../../docs/micro-signal.md#two-universe-investigation-and-rf-free-reproducer)
uses the same loader and SDIO publisher without starting D11/PHY or sending RF.
It preserves first-fault FIFO/DMA state before recovery and reproduces failures
independently of the radio application. Production now uses 32-byte SDIO DMA
bursts and one explicit host-consumption credit. The matching loader checks
post-read status and acknowledges only structurally valid, fully read packets.
It stops on faults without terminating a frame or guessing a credit.
New arrivals while credit is occupied are counted as drops.

The mailbox is now 456 bytes: offsets 0..447 are unchanged, offset 448 is the
host-written full-width consumed counter, and offset 452 is the controller's
full-width published counter. These counters wrap modulo 2^32 independently
of the older saturating diagnostic counts at offset 444. Controller comparison
images must implement this updated protocol; rebuild the host and controller
together. The diagnostic snapshot still captures the original 448-byte prefix.
See the [transport repair](../../docs/micro-signal.md#production-transport-repair)
for negative controls, limitations and hardware qualification.

The channel argument accepts 1..11. `--inspect` reads only the `SSID:` field;
it does not use or print the password. Binary captures contain received
network identifiers and should remain local.

Additional collector modes require a new load/execute and recovery:

- `--tx-one-probe`: enables TX and submits one 94-byte, unencrypted, 11 Mbps
  CCK probe request for the first FCS-validated beacon's SSID. Its source MAC
  is fresh and locally administered. It records command completion and
  controller status, then looks for a valid-FCS probe response to that MAC.
  The selected beacon is not necessarily the AP in the credential file;
  inspect the RX evidence first. This is not a traffic generator.
- `--tx-probe-1m`: one unicast, SSID-directed probe at 1 Mbps, addressed to the
  first validated beacon's BSSID. It contains no vendor padding and has total
  template length `52 + SSID length` bytes, including PLCP and FCS.
- `--tx-probe-broadcast-1m`: the same compact SSID-directed request with both
  destination and BSSID set to broadcast, as in normal active scanning.
  Confirmation remains bound to the first validated beacon's AP and fresh MAC.
- `--tx-one-auth`: one padded 94-byte, 11 Mbps CCK open-system authentication
  request addressed to the first validated beacon's BSSID.
- `--tx-auth-1m`: the padded authentication request at 1 Mbps CCK.
- `--tx-auth-ofdm`: the padded authentication request at 6 Mbps OFDM.
- `--tx-auth-short`: an exact-length 40-byte template at 1 Mbps CCK:
  6-byte PLCP, 30-byte authentication request, and 4-byte MPDU FCS.
- `--tx-auth-rearm`: the short authentication request followed by an explicit
  RXE/PSM restart. It requires PSM READY before resuming MAC reception; timeout
  or fault reports controller error `0x84`. The restart resets PSM discard
  counters, so their final values are not whole-trial totals.
- `--tx-auth-match`: the short authentication request with hardware receive
  address matching programmed to its fresh source MAC and selected BSSID.
  It does not change the SHM automatic-ACK address.
- `--tx-auth-match-rearm`: combines those two receive-path experiments.
  Neither option changes the default TX resume path or RF gains/power.
- `--reject-tx`: leaves TX disabled and submits the same command to verify
  rejection. Collector success here requires controller error `0x81`,
  address `0x20018`, ERROR state, and no completed TX command.

Every enabled-TX AP-response mode sends exactly one request with a fresh locally
administered source MAC. Collector exit status is nonzero unless it obtains
over-the-air confirmation, even if the TX command completes. Evidence is saved
before this decision. Confirmation requires all three TXE/IFS completion
conditions, no controller error, and a saved valid-FCS response received after
TX: either the expected management reply with matching receiver, transmitter,
and BSSID, or a 14-byte ACK addressed to the fresh source MAC. An ACK has no
transmitter/BSSID field; it is bound by its receiver address and this single
fresh request. These checks are not cryptographic sender authentication.
Authentication transaction 2 is evidence of reception even if its status
rejects authentication. A bound deauthentication/disassociation reply also
qualifies as reception evidence. Retry, power-management and More Data flags
do not invalidate an otherwise matching management reply; fragmented,
protected, DS-addressed and ordered/HT-control variants are not accepted by
this legacy-frame parser. None of these observations establishes association
or WPA2 connectivity.

`--inspect` reports separate probe/authentication/ACK FCS checks and
`tx_over_air_confirmed`. Its exit status checks the saved SRAM and Function-2
frames' FCS, not TX confirmation. Read the JSON boolean for the latter.
The AP chosen for TX is still the first validated beacon, not an SSID selected
from the credential file. Only use these modes where that AP is authorized.

`CFG_TX_ENABLE` now gates both explicit TX commands and automatic ACKs in
the controller. With TX enabled, automatic ACKs remain available for frames
addressed to its configured station MAC. A rejected TX command preserves the
ERROR state through cleanup rather than publishing RX_DONE.

Restore the saved program **without executing it**, then confirm BOOTSEL:

```sh
$PICOTOOL load -v build/clean-rx/badge/original-badge.bin --ser "$SERIAL"
$PICOTOOL info -a --ser "$SERIAL"
```

## Source-only peer TX/RX

The local Tufty2350 board header confirms the same WiFi GPIO wiring used by
this host: enable 23, data 24, CS 25 and clock 29. The test does not use an
accelerometer or buttons. Both tested boards have RP2350-A4/QFN80 processors.
No separate Tufty networking executable, vendor WiFi image or retained radio
initialization is used: both run the same image and cold-clear controller RAM.

After preserving each device's installed program separately, load the image
onto both **without** `-x`, then reboot the staged images. Immediate parallel
flash/execute attempts caused intermittent host USB discovery failures;
staging first and rebooting sequentially worked for the qualified trials.
Always select the serial explicitly and restore the correct backup to each.

```sh
PICOTOOL=build/picotool-usb/picotool
COLLECTOR=build/clean-rx/badge/collector
TX_SERIAL=YOUR_TRANSMITTER_SERIAL
RX_SERIAL=YOUR_DIFFERENT_RECEIVER_SERIAL

$PICOTOOL load -v build/clean-rx/badge/clean_rx.uf2 --ser "$TX_SERIAL"
$PICOTOOL load -v build/clean-rx/badge/clean_rx.uf2 --ser "$RX_SERIAL"

# Use a new 112-bit random challenge for every paired trial.
CHALLENGE=$(openssl rand -hex 14)
$PICOTOOL reboot --ser "$TX_SERIAL"
$PICOTOOL reboot --ser "$RX_SERIAL"
$COLLECTOR "$RX_SERIAL" build/clean-rx/badge/peer-rx.bin 6 \
  --peer-rx "$CHALLENGE" > build/clean-rx/badge/peer-rx.json &
RX_PID=$!
sleep 0.1
$COLLECTOR "$TX_SERIAL" build/clean-rx/badge/peer-tx.bin 6 \
  --peer-tx "$CHALLENGE" > build/clean-rx/badge/peer-tx.json
TX_STATUS=$?
wait "$RX_PID"
RX_STATUS=$?
printf 'TX submission=%s, independent RX proof=%s\n' "$TX_STATUS" "$RX_STATUS"

# Independently recheck the saved receiver frame using the same challenge.
$COLLECTOR --inspect-peer build/clean-rx/badge/peer-rx.bin "$CHALLENGE"
sleep 9
$PICOTOOL info -a --ser "$TX_SERIAL"
$PICOTOOL info -a --ser "$RX_SERIAL"
```

Peer modes accept exactly 28 hexadecimal challenge characters:

- `--peer-tx`: one 94-byte, 11 Mbps CCK broadcast wildcard probe request,
  including PLCP and FCS. A vendor IE contains a local OUI, `CRXP` tag and the
  14-byte challenge. The source MAC and sequence are also challenge-derived.
  It does not require an AP beacon to construct the frame.
- `--peer-tx-1m`: the same MPDU at 1 Mbps CCK; currently **unqualified**.
- `--peer-rx`: RX-only, with explicit TX and automatic ACKs disabled. It
  listens for 2,500,000 host timer ticks and retains any exact challenge frame
  in the saved Function-2 evidence. The ordinary host deadline and independent
  BOOTSEL recovery still apply.

A peer transmitter's exit 0 means **command submission/completion only**,
not OTA success. `peer_rx_confirmed:true` and receiver exit 0 require an
independently received 88-byte MPDU matching the expected request byte-for-byte,
including valid FCS, challenge, addresses, frame control and sequence, plus a
successful RX-only controller run. `peer_frames` counts matches, not all RX.
The USB report and every trace page remain nonce/build-bound; the JSON now
includes the selected device serial and challenge. Verify distinct serials,
matching build/channel/challenge and successful TX completion in the paired
reports. Do not reuse challenges.

The existing `tx_over_air_confirmed` field retains its narrower **AP reply or
fresh-MAC ACK** meaning; peer proof is separate and may succeed when that field
is false. `--inspect-peer` exits nonzero for no match, stale/wrong challenge,
corruption, controller error, or a TX-requested trace. Neither receiver
confirmation nor a successful AP probe response establishes association.

## Physical results, 2026-10-14

One RP2350-A4 badge was tested with serial-bound operations and independent
BOOTSEL checks. These are newly acquired cold-source results, not
vendor-retained initialization.

| Trial | Result |
| --- | --- |
| Initial cold boot | 18,960 executable bytes read back; RX_DONE with no controller error; 10 captures and 10 DMA completions |
| Full-frame/Function-2 test | 9 captures, 9 DMA completions, 9 Function-2 packets; host-validated FCS for the saved SRAM and Function-2 frames; local AP SSID matched on channel 1 |
| Enabled wildcard probe | Command completed; TX marker `0x5555005e`, status `0x04c10022`; no matching AP response in the extended response trial |
| Enabled SSID-directed probe | 18 captures/DMA completions/Function-2 packets; same TX marker/status; no matching AP response |
| Disabled-TX rejection, build `bd682752` | Error `0x81` at `0x20018`, controller ERROR state; no completed TX command |
| Final RX-only repeat, build `bd682752` | 12 captures, 12 DMA completions, 12 Function-2 packets; both saved frames pass host FCS checks; local AP SSID matched; no TX requested |

Evidence binaries and JSON reports are in `build/clean-rx/badge/`, including
`full-ch1.*`, `probe-retry-ch1.*`, `directed-ch1.*`, `reject-ch1.*`, and
`final-rx-ch1.*`. An initial response-window trial hit the shorter collector
deadline; that deadline was corrected to cover the loader's aggregate bound,
and the subsequent trial completed. Failed and successful reports are not
interchangeable.

The original 312,560-byte badge program was backed up, restored with write
verification, and left in BOOTSEL.

**Established:** cold source execution, local-AP beacon reception, independent
FCS verification, DMA/Function-2 transport, explicit TX command execution,
and disabled-TX rejection.

**Not established:** successful over-the-air TX, measured RF power/spectrum
or regulatory qualification, automatic ACK timing/reliability, CCMP,
association, WPA2 connectivity, large-frame transport, or long-run DMA-ring
reliability. A completed TX command is not evidence of AP reception.

## Follow-up TX verification

The original PLCP CRC loop was incorrect: it mixed an MSB-first polynomial
with a right-shifting input byte. It now uses reflected CRC-16 (`0x8408`),
initial value `0xffff`, final complement, and little-endian output. The
88-byte/11 Mbps known-answer PLCP is `6e 04 40 00 7f ee`.
Host tests also cover OFDM SIGNAL length/parity, short authentication framing,
and response address/FCS binding.

Corrected probes and padded/exact-length authentication requests were tested
at 1 Mbps CCK, 11 Mbps CCK, and 6 Mbps OFDM. The TX engine reported all three
completion conditions, but no trial acquired a matching management response
or fresh-MAC ACK. Exhausted TX polling now fails explicitly with firmware
error `0x82`; invalid TX lengths fail with `0x83`.

Final image `e0b3f797` cold-loaded and read back 19,176 controller bytes:

| Trial | Result |
| --- | --- |
| Exact-length authentication | 53 captures, 53 DMA completions, 53 Function-2 packets; TX marker `0x55550028`, status `0x04c10022`, completion bits `7`; no matching reply or ACK; collector exit 1 and `tx_over_air_confirmed: false` |
| RX-only regression | 28 captures/DMA completions/Function-2 packets; both saved frames pass independent host FCS checks; local SSID matched; collector exit 0 |
| Disabled-TX regression | Error `0x81` at `0x20018`, ERROR state, no completed TX command; collector exit 0 for the expected rejection |

Final evidence is local under `build/clean-rx/badge/`: `final-auth.*`,
`tx-final-rx.*`, and `tx-final-reject.*`. Native tests, ASan/UBSan host tests,
and existing USB/WiFi-firmware regressions pass. Both device images remain
freestanding, with no undefined link symbols.

**Over-the-air TX remains unconfirmed.** Controller completion and historical
TX evidence from another image are not substitutes for a reply to this image.
Without an independent RF observer, these results do not distinguish an
invalid/absent transmission from a missed AP response.

## Further diagnosis: PHY timing, mesh nodes, and reference firmware

Before issuing TX, the RP2350 now reads the complete staging buffer back and
compares it byte-for-byte. Firmware captures TXE/IFS status before, during and
after TX, PHY/radio/IQ/TX-register readbacks, PHY error status, CPU ticks, and
MAC hardware microsecond-counter timestamps. These are internal observations,
not measured RF power or an over-the-air capture. Successful trials measured:

- Short 1 Mbps authentication: 466-467 us between trigger and completion.
- Compact 1 Mbps probe with a seven-byte SSID: 621 us.
- Completion to MAC RX enable: 7-8 us.
- TXE busy asserted and then cleared, completion asserted, no PHY TX error.
- Intended manual gains, power-control-off setting, TX enables, Farrow
  parameters and identity IQ/baseband multipliers read back correctly.

The timing is consistent with the requested long-preamble frames, but does not
prove that a valid waveform left the antenna or that RX was ready at SIFS.
`fresh_mac_frames` counts any FCS-valid post-TX frame addressed to the fresh
source MAC, including frames not recognized as the expected reply. All new
trials reported zero, so stricter subtype/flag validation alone did not explain
the lack of confirmation. The Retry-flag rejection was nevertheless a real
parser bug and is covered by native tests.

RX-only scans found two matching-SSID BSSIDs on channel 1 and another on
channel 6; channel 11 yielded no frames in its bounded observation window.
Both authentication and compact unicast probes were tried against the
channel-6 node. In the final compact-probe trial, 75 captures, 75 DMA
completions and 75 Function-2 packets were reported, without a fresh-MAC reply.
These tests do not establish which mesh node was physically nearest.

For a control, the earlier `open-rx` C implementation was rebuilt locally from
source into a separate badge image: 47,028 controller bytes, no undefined
symbols, no vendor image. Its continuous PSM image is byte-identical to the
2,360-byte PSM image in `clean-rx`. Cold-loaded short and padded authentication
trials against the same channel-6 AP also produced no fresh-MAC frame. Its
`fw_substage` value `17` is a legacy boot diagnostic, **not** this firmware's TX
completion bitmask; the collector was not weakened to declare it qualified.

Thus the failure is reproducible with the earlier cold-initialization code,
not just the simplified `clean-rx` implementation. Common template-TX, RF,
PSM/RX and AP-response behavior remain candidates. No analog gains or power
tables were swept or raised. An independent RF observation is still needed
to separate absent/invalid TX from a missed reply.

Evidence remains local under `build/clean-rx/badge/`: `tx-diag.*`,
`tx-reply-flags.*`, `compact-probe.*`, `mesh-rx-6.*`, `mesh-rx-11.*`,
`mesh-auth-6.*`, `mesh-probe-6.*`, `control-auth-6.*`, and
`control-auth-padded-6.*`. The separate comparison badge is under
`build/clean-rx/control-badge/`.

Final default image `4f27c413` read back 19,980 controller bytes. Its RX-only
channel-6 regression delivered 40 frames with valid saved-frame FCS. Its final
compact-probe trial delivered 52 RX frames, measured 621 us TX / 8 us return
to MAC RX enable, and still had no fresh-MAC reply; the collector exited 1.
Disabled TX again reported `0x81` and the rejection check exited 0. Sanitized
host tests and shared USB/WiFi regressions pass. After these trials, the
312,560-byte original badge program was restored, read back and compared
byte-for-byte; the badge was left in BOOTSEL without executing that program.

### Badge-only receive-path investigation

Three additional cold-loaded, single-request trials targeted the channel-6
AP with fresh source MACs. None received a valid-FCS frame to that MAC.

| Trial | Captures / DMA / Function-2 | TX duration | Completion to MAC RX enable |
| --- | --- | --- | --- |
| Explicit RXE/PSM restart, build `0c1a22f6` | 43 / 43 / 43 | 466 us | 28 us |
| Hardware source/BSSID matching, build `2937666e` | 49 / 49 / 49 | 466 us | 8 us |
| Both, build `6bd496ec` | 35 / 35 / 35 | 466 us | 29 us |

All reported TX completion bits `7`, no controller error, and zero fresh-MAC
ACKs, management replies or other valid frames. The combined trial's saved
SRAM and Function-2 frames passed independent FCS validation and its beacon
matched the local SSID. MAC RX enable is not a measurement of analog receiver
readiness or proof that a SIFS ACK could have been received.

The matching-only trial paused PSM in receive-copy state `11`, whereas the
combined trial started and resumed in event-wait state `3`. Restarting from
the known PSM entry sequence did reach READY within the bounded poll; a missed
reply is therefore not explained simply by failure to restart that sequence.
This does not exclude other receive-path problems.

The combined trial sampled PHYDEBUG `0x00000844` just after the TX trigger.
Defined bit 2 confirms that the MAC instructed the PHY to transmit. The
earlier `phy_debug_before_cmd` read was subsequently found to be misplaced
before cold initialization; its zero value is not valid carrier evidence.
That placement is corrected in the phase-tagged trials below. PHYDEBUG does
not measure radiated RF power, frame validity or AP reception.

The diagnostic is now `TXD3`, still 180 bytes. PHYDEBUG replaces the old staged
CRC field: CRC over a frame containing its FCS is not a fresh-frame challenge.
The host still checks the staged template byte-for-byte before issuing TX.
The existing PSM fault-recovery path now shares the bounded restart helper
and explicitly fails instead of silently continuing after a READY timeout.

Evidence is local in `build/clean-rx/badge/tx-rx-rearm.*`,
`tx-rx-match.*`, and `tx-rx-match-rearm.*`. These receive-path experiments did
not confirm OTA TX; they remain opt-in rather than speculative default fixes.

Final image `5eb3e077` read back 20,132 controller bytes and passed native,
AddressSanitizer/UndefinedBehaviorSanitizer and shared USB/WiFi tests. Its
channel-6 regressions delivered 30 RX-only frames, 65 frames in normal short
authentication, and 45 with matching plus rearming. Saved SRAM/Function-2
frames passed FCS checks in all three trials, and each trial recorded the local
AP SSID. Both TX trials measured
467 us and still had zero fresh-MAC frames (collector exit 1). Normal RX resume
took 7 us, versus 28 us with the restart. Disabled TX again failed explicitly
with `0x81`, and its expected-rejection check exited 0. Final evidence uses the
`rx-investigation-final-*` prefix. The original 312,560-byte badge program was
restored and independently read back/compared byte-for-byte; the badge remains
in BOOTSEL.

### Separating post-TX RX from earlier queued traffic

Aggregate RX totals alone did not establish that the receiver recovered after
TX. The controller now tags each Function-2 packet when publishing it, using
the existing two SDPCM padding bytes, without extending the packet or changing
BDC payload offsets. Queued pre-TX packets retain their earlier tag.
The collector separately reports packets delivered after TX and packets
published by the controller after TX, with FCS and selected-AP checks.

Final build `73ea2c43` read back 20,164 controller bytes:

| Trial | Packets before TX | Valid controller-tagged post-TX packets | Selected-AP post-TX beacons | Fresh-MAC reply |
| --- | --- | --- | --- | --- |
| Short authentication, channel 6 | 38 | 11 | 3 | None |
| Authentication with matching/rearm, channel 6 | 41 | 11 | 4 | None |
| Compact broadcast probe, channel 6 | 44 | 13 | 2 | None |
| Compact broadcast probe, channel 1 | 29 | 8 | 4 | None |

Thus reception did not remain dead after transmission: several valid beacons
from the selected AP were received after each request. A resumed in-progress
PSM frame could account for at most one publication on the normal resume path;
the explicit restart also removes that ambiguity. These observations do not
establish receiver readiness within the immediate ACK/reply interval.
Authentication took 466 us; broadcast probes took 621 us. Correctly placed
pre-command PHYDEBUG samples were `0x00000040`, without the defined carrier
sense bit set. These single samples do not rule out subsequent collisions.

The compact broadcast probes remove the earlier unicast-probe addressing
question, but still produced no fresh-MAC frame. Both channel-1 mesh nodes
matched the local SSID. One earlier channel-1 cold start failed explicitly
before TX with `0x71`, PSM fault `3` (initial RXE flush); a new cold attempt and
the final repeat succeeded. This intermittent startup fault is not counted as
a transmitted request and is not hidden by a retry in the firmware.

Saved SRAM/Function-2 frames passed FCS inspection for all final RX/TX trials.
RX-only and disabled-TX regressions also passed. Native and sanitizer tests
cover phase tags, queued pre-TX exclusion, FCS rejection, short ACKs, AP binding
and the broadcast frame; shared USB/WiFi regressions pass. Evidence is local
under `phase-final-*`, `rx-phase-*` and `broadcast-probe-*` in
`build/clean-rx/badge/`. OTA TX remains unconfirmed. The original 312,560-byte
program was again restored, independently read back and byte-compared; the
badge was left in BOOTSEL.

## Two-radio physical results, 2026-10-15

Authorized devices:

- Universe badge `08DA26F86E57F5EA`.
- Pimoroni Tufty2350 `D41777A92225F996`.

Final build `44f23426` uses the unchanged 20,164-byte source-built controller
from the preceding investigation. Only the badge host, evidence matching and
collector were extended for peer tests; no RF power/gain settings were changed.

| Trial | Independent receiver result | TX duration |
| --- | --- | --- |
| Universe -> Tufty, 11 Mbps, initial build `8e9cbac5` | Exactly one fresh-challenge frame, full MPDU/FCS match | 264 us |
| Tufty -> Universe, 11 Mbps, initial build | Exactly one fresh-challenge frame, full MPDU/FCS match | 265 us |
| Universe -> Tufty, 11 Mbps, final build | Exactly one fresh-challenge frame, full MPDU/FCS match | 265 us |
| Tufty -> Universe, 11 Mbps, final build | Exactly one fresh-challenge frame, full MPDU/FCS match | 264 us |
| No transmitter, Tufty RX, final build | 48 ambient captures, zero peer frames; receiver exits 1 | No TX |
| Saved positive capture inspected with a new/wrong challenge | Rejected, exit 1 | Offline |
| Universe -> Tufty, 1 Mbps, final build | 89 ambient captures, zero peer frames; receiver exits 1 | 906 us |

The initial successful 11 Mbps trials also recorded matching fresh-MAC AP
probe responses on the transmitter. The final Tufty TX repeat did too; the
final Universe TX repeat did not, despite independent peer confirmation.
This demonstrates why absence of an AP response alone was not enough to
conclude that TX failed. AP observations and RF conditions changed between
days; these results do not establish the cause of the previous AP-only failures.

1 Mbps did not produce a matching frame in the earlier attempts in either
direction or the final Universe-to-Tufty repeat. One reverse-direction 1 Mbps
receiver run exhausted its host deadline and is not a clean negative RF result.
The clean negatives are still not enough to distinguish a 1 Mbps TX waveform
problem from a rate-specific receive-path issue; leave that rate unqualified.
Initial paired attempts with USB discovery/flash failures are also not counted
as OTA trials.

Native tests, ASan/UBSan and shared USB/WiFi regressions pass. Positive receiver
captures were re-inspected offline, and peer receivers had TX disabled in the
saved mailbox flags. Evidence remains local in `build/clean-rx/badge/`:
`peer-staged-u2t-*`, `peer-t2u-11-*`, `peer-final-*`, `peer-negative-*`, and
their `.challenge` files. The final peer trace mode is stored at byte 2000 and
its match count at 1996; the retained Function-2 frame uses the existing offset.

At the end of these peer trials, the original programs were restored and independently read back/compared:
312,560 bytes for the Universe badge and 419,560 bytes for the Tufty
(`wifi_handover`). Both devices were left in BOOTSEL without executing their
restored programs.

## Subsequent Micro Signal reliability investigation

The standalone [Micro Signal app](../../docs/micro-signal.md) now uses this
loader and source controller on both attached boards. Its complete guide
records successful and failed sustained-traffic trials; the earlier one-shot
OTA results above are not a sustained-operation qualification.

- Baseline: two initial-flush PSM failures in 40 controlled cold radio boots.
  Fault snapshots showed RXCTL `0x0804`; a later probe showed `0x0800`.
  The PSM's 255-poll flush countdown expired before hardware completion.
- Repair: the counter-load instruction now uses the existing signed `-1`
  encoding for 65,535 iterations. Branch targets, timeout/error paths and
  outer ARM deadline are unchanged. Subsequent controlled batches passed
  **80/80 radio boots**, without automatic boot retries.
- Multi-minute tests exposed TX staging readback corruption and a missing
  SPI bulk-backplane word-access flag. The loader now sets `0x8000` on aligned
  reads and writes, matching scalar I/O and the existing firmware loader.
- One **300.023-second** paired run passed: Universe 1,383 TX / 469 unique RX
  events / 5,271 published SDIO frames; Tufty 1,405 TX / 459 unique RX events /
  4,160 SDIO frames. Neither reported errors, drops, resyncs or sequence stalls.
- A repeat failed at **124.895 seconds**, with Tufty application error 6 and
  malformed SDPCM first word `0xfc1c013f`. A slower-SPI experiment also failed
  and was reverted. **Sustained reception is still not qualified.**

The controller retains PSM/RXE fault snapshots and periodically publishes
best-effort live snapshots in the existing mailbox diagnostic fields.
The test-only app/collector captures these plus exact staging mismatch bytes.
Native PSM tests verify the image identity, counter encoding, actual branch
execution, delayed completion and persistent timeout. Loader tests enforce
word-access flags, boundary splitting and interleaved staging/mailbox I/O.

The current production Micro Signal UF2 is 63,488 bytes, with a 31,588-byte
RP2350 flash image embedding the 20,500-byte controller. The normal app still
has no USB or recovery watchdog. Updated images supersede the restored
original programs mentioned in the historical trial above; original backups
remain preserved.
