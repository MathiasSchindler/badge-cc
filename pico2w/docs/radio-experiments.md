# Wi-Fi-managed LoRa experiments

Both Pico 2 WH / Waveshare Pico-LoRa-SX1262-868M nodes run the `remote`
application. Wi-Fi/DHCP/HTTP is the management path; LoRa is an independently
configurable experimental path. No physical access is required to change a
working node's LoRa settings while its Wi-Fi remains available.

The current DHCP addresses are:

| Device | Serial | Dashboard |
| --- | --- | --- |
| Original / usually local | `7C4918BAB9492C77` | `http://192.168.178.68/` |
| New / movable | `76EDD29A5E2CD5B4` | `http://192.168.178.80/` |

Addresses can change; use the router's DHCP list, USB `picoctl status`, or
compatible LoRa health reports to find them.

## Workflow and recovery

1. Open each node's dashboard in a separate browser tab. The page displays
   its chip serial and IP so the target is explicit.
2. Fill the same compatible profile on both pages, then **Apply to this Pico**
   on each. The returned revision/configuration is the node's accepted
   configuration, not a prediction based on the form.
3. Check both reported profiles before transmitting. Applying one node
   does not automatically change the other. Wi-Fi stays available even
   while LoRa settings are incompatible.
4. On the sender, choose one to three packets and **Queue test batch**.
   Watch queued/attempt/TxDone counts and the test token.
5. On the receiver, find the same token in `runs`. Inspect unique ordinals,
   duplicates, RSSI/SNR and the configuration recorded when receiving.
6. **Cancel unsent packets** before changing settings during a batch.
   **Restore known-good profile** explicitly returns a node to baseline.

There is **no automatic rollback timer**. Experimental settings are RAM-only:
reboot restores **869.850 MHz, SF7, BW125, CR4/5, preamble 8**. Reboot also
clears all counters and test history. Restore baseline on both nodes to
reestablish the known-good link.

This implementation intentionally uses two explicit per-device dashboards,
not a distributed apply transaction or a cross-origin browser controller.
A failed change on one device must be resolved through that device's page.
Preset buttons fill the form only; they do not transmit or apply automatically.
Background polls do not overwrite edited form fields. **Load current settings
into form** explicitly reloads the last read-back.

## Supported controls

| Control | Supported values |
| --- | --- |
| Frequency | Integer Hz whose occupied bandwidth fits either **868.0-868.6 MHz** or **869.7-870.0 MHz** |
| Spreading factor | SF7-SF12 |
| Bandwidth | 125, 250 or 500 kHz |
| Coding rate | 1=4/5, 2=4/6, 3=4/7, 4=4/8 |
| Preamble | 8-32 symbols |

The center must leave half the bandwidth on both sides within one allowed
band. For example, 869.850 MHz works with BW125/BW250, but **not BW500**;
868.300 MHz supports all three bandwidths.

Fixed settings: LoRa modulation, private sync `0x12` (`0x1424`), explicit
header, payload CRC, normal IQ, nominal **0 dBm** and 200 us PA ramp.
GFSK and adjustable transmit power are not implemented in this version.
Transmit and receive use the selected coding rate and preamble; low-data-rate
optimization is derived from SF/bandwidth. Radio rearm, fault retries and
post-transmit receive restoration reapply the full selected private profile.

Only the opt-in `remote`/link-test applications link transmit support.
The original `monitor` and `probe` remain non-transmitting.

## Airtime, queued tests and results

All packets are 36 bytes. The API calculates LoRa airtime using explicit
headers, CRC, coding rate, preamble and low-data-rate optimization.
Hardware timeout is rounded-up airtime plus 50 ms; software polling has
another 50 ms bound.

Manual tests and automatic health reports share **one transmit budget**.
After each attempt the minimum interval is:

```text
max(60 seconds, 100 * hardware_timeout)
```

Failed attempts consume the same budget. Profile changes, baseline restore
and batch cancellation do **not** clear outstanding airtime debt. A slower
profile can require several minutes between packets. The first transmission
also waits at least 60 seconds after boot. These conservative limits are
not RF conformance certification: use only where the band, antenna and
operation are permitted.

Baseline airtime is **77.056 ms**, timeout **128 ms**, minimum interval
**60 seconds**. The slowest allowed configuration (SF12/BW125/CR4/8,
preamble32) has **3547.136 ms** airtime, **3598 ms** timeout and **359.8 seconds**
between attempts. Five-minute health heartbeats can therefore be delayed
by the airtime budget or an active manual batch.

The sender processes a bounded queue, not an unbounded transmitter:
one request queues at most three packets; another request or profile
change while queued gets HTTP 409. Errors stop the remaining batch.
Cancellation affects only unsent packets. TxDone confirms the chip's
completion, **not delivery**.

Each test packet carries sender chip ID, global test sequence, `LAB1`
marker, request token, ordinal, expected batch count and application CRC32.
Tokens are correlation identifiers, not authentication or secrets.
The receiver validates application/radio CRC errors and records at most
eight received runs, each with its receive configuration, unique ordinal
bitmask, duplicate count and last RSSI/SNR/time.

`not_yet_observed` is the expected count minus received unique ordinals,
not a proven packet-loss count: the sender might still be queued. A run
with zero observations cannot appear on the receiver. Compare sender
and receiver counters/token after the queue finishes.
Repeated snapshots do not count as duplicates; separate receptions do.
Raw packets and global radio CRC/header error counts remain in `/api/status`.
Old run settings remain attached to history after a profile change.

Autonomous Wi-Fi failure reporting and heartbeat logic continue. Health
reports use the **selected profile**, not a hidden baseline fallback.
Changing profiles can therefore also break LoRa diagnostics. If both Wi-Fi
and the compatible LoRa path fail, remote recovery is not guaranteed;
power cycling restores the baseline.

## HTTP interface

All writes require `X-Pico-Request: 1`. Bodies are strict form-urlencoded
ASCII in the shown order; unknown fields/trailing bytes are rejected.

| Method / path | Body / result |
| --- | --- |
| `GET /api/radio` | Accepted config, revision, airtime/timeout, current wait, queue/counters/errors and received runs |
| `POST /api/radio/config` | `frequency_hz=868300000&sf=8&bandwidth_khz=250&coding_rate=2&preamble=12` |
| `POST /api/radio/reset` | Empty body; restore baseline, preserving airtime debt/history |
| `POST /api/radio/send` | `count=1&token=123456` (count 1-3, nonzero uint32 token) |
| `POST /api/radio/cancel` | Empty body; discard remaining queue |
| `GET /api/health` | Independent Wi-Fi/DHCP/radio and automatic-health diagnostics |
| `GET /api/status` | Full receiver diagnostics/raw packet snapshot |

HTTP 400 is invalid input, 403 is a missing guard header, 409 is a queue
conflict, and 503 is a radio/configuration error. A failed profile change
attempts to restore the previous profile and exposes both `config_error`
and `restore_error`. Invalid input never touches radio hardware.
Runtime `tx_error` and `restore_error` remain visible in radio status.

The HTTP service is local, unencrypted and unauthenticated, like the
existing monitor. Do not expose it to the Internet. The required custom
header is a browser-origin guard, not user authentication. LoRa packets
are also unencrypted/unauthenticated.

## Build and verification

Use the existing `--app remote --confirm-waveshare` build with private
Wi-Fi inputs from the [remote health guide](remote-health.md). Images
embed plaintext Wi-Fi credentials and must not be published.

Native tests cover profile/frequency validation, exact baseline/worst-case
airtime, dynamic timeout bytes, radio failures/restoration, strict HTTP/CSRF,
queue/cancellation, budget preservation across changes, timer wrap,
receive correlation, duplicate/CRC filtering and bounded history.
Python tests check API shapes, page safety and JavaScript syntax.

### Hardware qualification, 2026-10-23

Both selected Picos were flashed with final LLVM build **`34b551d3`**.
Both independently associated Wi-Fi, obtained the addresses above and
reported healthy radio/HTTP status. Owned build **`cdda87fa`** also passed
layout/unresolved-symbol checks but was not flashed.

- An alternate profile, **868.300 MHz / SF8 / BW250 / CR4/6 / preamble12**,
  was applied and read back on both devices. Two-packet batches arrived
  **2/2 in each direction**; observed receive gaps were **60,001 ms** and
  **60,031 ms**, checking the shared minimum-spacing requirement.
- After the UI error-handling refinement, the same alternate profile
  again delivered one packet in each direction. Tokens/sender identities,
  application CRCs and receiver-side profile records were checked.
- The final driver includes the SX1262 BW500 TX modulation-quality
  workaround. On final build `34b551d3`, **868.300 MHz / SF7 / BW500 /
  CR4/5 / preamble8** delivered **1/1 each direction**, with zero missing
  batch ordinals or duplicates. Its calculated 36-byte airtime is
  **19.264 ms**, versus baseline **77.056 ms**.
- Explicit baseline restore succeeded on both devices without clearing
  outstanding airtime debt. History retained the original experimental
  receive configuration rather than relabelling it as baseline.
- Rebooting the new node while it was deliberately configured at the
  alternate profile restored baseline settings/revision and cleared
  RAM-only history. Wi-Fi/DHCP returned automatically.
- The browser form successfully applied a profile; an invalid occupied
  band/BW500 combination returned HTTP 400. The error stayed visible
  through subsequent successful polls and cleared after explicit restore.
- Native regression suites and **38 Python tests**, including browser
  request-serialization/error tests, passed. Worst-case SF12 airtime,
  timeout, low-data-rate optimization and budget are covered synthetically;
  this was not a sustained SF12 RF test.

Both devices were left on baseline with empty manual queues. Their normal
autonomous health reports remain enabled. The two-packet batches preceded
the final BW500-specific driver refinement; final-build BW500 reception
was separately verified after flashing that refinement.
Intermittent HTTP stalls/retries occurred as with the earlier monitor:
the result does not establish lossless networking or radio capture.
