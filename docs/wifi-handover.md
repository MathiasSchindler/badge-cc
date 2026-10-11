# BLE-to-Wi-Fi handover lab

This is a **separate SDK-based experiment**, not a change to Wireless Lab,
BLE tilt Pong, or the no-libc diagnostic app.

The milestone is a four-player transport foundation:

1. Choose one badge to host; other badges discover and select its session
   over BLE.
2. The host starts a local WPA2 Wi-Fi software access point.
3. BLE provisions each selected-session client with the network credentials,
   channel, session ID, player slot, and token.
4. The badges exchange sequenced inputs and authoritative four-paddle state
   over local UDP. No existing Wi-Fi, internet, DNS, or DHCP server is needed.

There is deliberately **no ball, collision handling, scoring, or complete
four-player Pong game yet**. The square and four independently controlled
edge paddles demonstrate the state distribution needed for that later game.

## Build and installation

```sh
make wifi-handover
```

Flash `build/universe26/uf2/wifi_handover.uf2` onto all participating badges.
The same image supports either host or client roles. This target is not part
of the default freestanding build.

Dependencies are the existing vendored Pico SDK, CYW43 driver/radio firmware,
BTstack, lwIP, and the SDK C runtime. CMake, Ninja, Python 3 for GATT
generation, and the Arm GNU toolchain are used like the other radio
experiments. The dependency exception is
intentional; this image does not claim to be no-libc or dependency-free.
No external network credentials are read or embedded at build time.

## Mixed-badge compatibility

The intended test group is two 2026 Universe badges, one 2025 Universe badge,
and a compatible wireless Pimoroni Tufty2350. Only shared core hardware is
used:

| Function | Required connection |
| --- | --- |
| Processor | RP2350B, Arm Secure firmware |
| Display | 320x240 parallel ST7789-compatible TFT |
| Display controls | GPIO26 backlight, GPIO27 CS, GPIO28 DC, GPIO30 WR, GPIO31 RD |
| Display data | GPIO32-39 |
| Peripheral power | GPIO41 high |
| Radio | CYW43439/RM2; GPIO23 enable, GPIO24 data/IRQ, GPIO25 CS, GPIO29 clock |
| First control | GPIO7, active-low with pull-up |
| Second control | GPIO9, active-low with pull-up |

The front controls are called Left and Right on screen; their printed names
can differ on the older boards. Identify the buttons connected to GPIO7/9.
The 2025 project's button driver and both vendored Tufty board definitions
include those inputs, although they use different names for GPIO9.

The application never initializes the IMU, touch controller, internal/external
I2C buses, ADC, IR, case LEDs, PSRAM, or other model-specific GPIOs. Older
button-detection code that probed GPIO8/11 is not reused.

The common pin contract is grounded in the 2026 schematic, existing display
and radio code, the Tufty board definition, and the 2025 project. It is not
a claim of a completed physical compatibility test on all four units.
This image is not for RP2040 Tufty2040 or a board with different TFT/radio
wiring.

## Controls and session workflow

### Starting a group

1. On one badge, press **Left: Create session**.
2. Wait for the AP to become ready. The Session screen shows an eight-digit
   session code and opens BLE admission for 60 seconds.
3. On each remaining badge, press **Right: Find session**.
4. Left cycles through detected hosts; Right joins the selected host.
   Match the session code to the host's display.
5. Wait for BLE configuration, Wi-Fi association, and the UDP handshake.
   The client then shows its assigned edge paddle.
6. Repeat for up to three clients. Once all four players are online, BLE
   admission closes. It also closes when the 60-second window expires.
   The host can reopen it from Session with Right.

Host selection is explicit, not an automatic smallest-address election.
This avoids assuming all four badges start together and makes accidental
selection of a different nearby session easier to spot.

BLE roles differ from the Wi-Fi roles: the advertising host is a **BLE
peripheral**, and joining badges connect as **BLE centrals**, sequentially.
After provisioning, the host is the Wi-Fi AP and each joiner is a station.
The two kinds of role are independent.

### Connected screens

Hold **both buttons for one second** to cycle:

```text
Paddles -> Metrics -> Session -> Paddles
```

Release both before resuming input. Single-button menu actions are delayed
briefly to distinguish them from the chord. Paddle motion uses the debounced
held state rather than waiting for a menu action.

| Screen | Left | Right |
| --- | --- | --- |
| Paddles | Move your paddle toward the start of its edge | Move toward the end |
| Metrics | Start/stop a 1000-sample benchmark | Select another player as target |
| Session | Toggle local Wi-Fi power saving | Host: reopen BLE admission for 60 seconds |

Host starts on Session so its code is visible. Clients start on Paddles.
All displays use the same square orientation:

| Display player | Wire slot | Edge |
| --- | ---: | --- |
| P1 / host | 0 | Bottom |
| P2 | 1 | Right |
| P3 | 2 | Top |
| P4 | 3 | Left |

Horizontal paddles move left/right; vertical paddles move up/down. Positions
are normalized to `0..1000`. The host distributes the shared state and online
mask to the clients. Offline/stale players are drawn dimmed rather than
silently presented as live.

Before connection, holding both cancels and returns to role selection.
Recoverable errors offer Left to retry and Right to return to roles.
Radio-initialization failures require a reset and remain visible on screen.
Resetting the host creates a new session; there is no host migration.

## Handover and local networking

Every new host session generates a random SSID suffix, 32-hex-digit WPA2
password, session ID, and per-client tokens at runtime. These are not persisted
to flash. Advertisements carry session discovery information, not passwords.
The screen does not display the password or tokens.

GATT provisioning uses a long read, so the credentials need not fit in one
default 20-byte ATT value fragment. The client validates the complete
versioned record before using it. The host caches an immutable record for
the connection so offset reads cannot mix assignments.

The experiment uses service UUID `0000FF2A-0000-1000-8000-00805F9B34FB`
and provisioning characteristic
`0000FF2B-0000-1000-8000-00805F9B34FB`. The version-1 record is 73 bytes:
version, length, slot, and channel (one byte each), little-endian session
ID (4 bytes), little-endian token (8 bytes), zero-padded SSID (24 bytes),
and 32 hexadecimal password characters plus NUL (33 bytes).
The receiver requires contiguous fragments and successful query completion,
then checks the decoded session against the selected advertisement.

The browser holds up to four discovered hosts. Provisioning and initial
Bluetooth startup have 15-second deadlines. Cancel/retry waits for pending
BLE teardown instead of treating a busy request as a successfully opened
session or scan.

Player identity is reserved by the BLE address for the lifetime of the host
session. Retrying the same badge reuses its slot and credentials. Reserved
slots are not automatically reassigned to a different badge just because
Wi-Fi goes offline; reset the host to replace group membership.

The initial implementation uses channel 6 and statically assigned IPv4:

| Role | Local address |
| --- | --- |
| AP / P1 | `192.168.4.1` |
| P2 | `192.168.4.2` |
| P3 | `192.168.4.3` |
| P4 | `192.168.4.4` |

The subnet is `/24`. The slot delivered over BLE determines the client
address, avoiding DHCP and address collisions within the session. This is
a local-only AP: there is no NAT, internet uplink, or joining of a conference
network.

The client does not count association alone as success: an application
handshake must establish the UDP session. Session IDs, slot assignments,
tokens, source addresses, and packet sequencing distinguish this group and
reject unexpected/stale traffic. They are not a substitute for
cryptographic application authentication.

UDP uses port **4242** and fixed 44-byte, big-endian version-1 datagrams.
The fields are magic (2 bytes), version/type (1 byte each), session ID
(4), endpoint token (8), sequence/connection epoch/run-or-nonce/timestamp
(4 each), sender/target/origin/online mask (1 each), and four paddle
positions (2 each). Types are `HELLO`, `WELCOME`, `INPUT`, `STATE`,
`PING`, `PONG`, and `BENCH_ERROR`.

A `HELLO` carries a fresh request nonce; `WELCOME` echoes it and supplies
a host epoch challenge. Only an `INPUT` confirming that challenge commits
the new connection epoch. Repeated or delayed greetings therefore cannot
arbitrarily reset an active client's input sequence tracking. State updates
are sent at a nominal 30 Hz; clients older than three seconds are marked
stale. Peer-to-peer benchmark packets are explicitly relayed by the
application, not assumed to be automatically bridged by the AP firmware.

## Latency measurement

Metrics are application ping/echo RTT measured on the initiating badge's
microsecond clock. No clock synchronization between badges is assumed.

- A client targeting P1 measures its direct client-to-host path.
- P1 targeting a client measures the host-to-client path.
- A client targeting another client measures the application-relayed path
  through the host, including both Wi-Fi legs in each direction.
- Input/state traffic continues while measurements run.
- Only one ping is outstanding per initiating benchmark. A missing echo
  times out after one second, with a 20 ms gap between samples.
- Sent, received, timed-out, and local send-error counts are kept separate.
  Local queue/allocation/send errors are not mislabeled as radio loss.
- The screen's **Send errors** count also includes explicitly reported
  remote echo/relay send failures (`BENCH_ERROR`). Such reports are
  best-effort; a failure whose report cannot be delivered may still appear
  as a timeout to the initiator.
- Minimum, latest, median, p95, p99, and maximum RTT are in microseconds.
  Percentiles use nearest rank and are finalized when the run ends, not on
  every screen refresh.
- Stopping a run preserves partial results; results are session-local and
  are not saved across reboot. Cancellation or a send error does not
  classify an outstanding ping as radio loss, so partial sent/received/lost
  totals need not balance.

These are application measurements, not raw over-the-air retransmission
counts, one-way latency, or input-to-photon timing. A 1000-sample run is still
not evidence of guaranteed worst-case latency or zero underlying loss.

Wi-Fi power saving is **off by default**. Session can toggle the local mode
for a controlled comparison. Record which badges have it enabled; changing
only the AP's setting does not change every client's policy. Compare the
same direction, packet rate, distance, and group size.

The Session screen distinguishes Wi-Fi joining time from the client's
end-to-end BLE-to-UDP handover time. A Wi-Fi-only retry restarts the latter
timer at retry; it is not another full BLE discovery measurement.

## Runtime ownership

Core 0 owns CYW43, BTstack, lwIP, the session coordinator, and buttons.
It services `pico_cyw43_arch_lwip_poll` frequently from the main loop; no
blocking connection wait or rendering occurs there. Core 1 owns the LCD
and draws snapshots copied through a short mutex-protected mailbox.

The shared display driver is compiled with SDK timing only for this target:
4-cycle setup, 8-cycle WR-low, 4-cycle hold, and timed initialization delays.
The default freestanding images retain their original timing path.

BLE advertising stops after admission closes, and clients stop scanning/
disconnect after handover. Bluetooth controller power is not cycled while
Wi-Fi is active because the protocols share a chip. Reopening admission
introduces BLE coexistence traffic; record that fact during latency tests.

## Limits and trust model

This is a controlled-lab experiment. BLE provisioning is not paired,
encrypted, or authenticated. Someone nearby during admission could obtain
credentials or impersonate a participant. Matching a displayed session code
helps avoid mistakes but does not authenticate the peer. A random WPA2
password does not make an unauthenticated provisioning channel secure.

The application caps sessions at four badges, even though the SDK radio's
documented SoftAP limit may allow four clients plus an AP. Simultaneous
provisioning is intentionally serialized; a busy joiner may need to retry.
Mixed-generation GPIO compatibility, SoftAP behavior, BLE/Wi-Fi coexistence,
range, and real four-badge latency still require physical validation.

## Four-board validation procedure

1. Build and flash the same UF2 on the two 2026 badges, the 2025 badge, and
   compatible Tufty2350. Confirm display and GPIO7/9 controls on each.
2. Start with two badges. Create/select the same session and verify the
   progression BLE discovery -> configuration -> Wi-Fi -> UDP ready.
3. Confirm P1 and P2 see each other's paddle movement. Switch which board
   hosts and repeat, including the 2025 and Tufty units.
4. Add the other two badges and confirm unique P1-P4 slots/IPs, four edges,
   and consistent state on every display.
5. Let BLE admission close. Run benchmarks one initiator at a time:
   client-to-host, host-to-client, and client-to-client via host.
6. Record board models, roles, player count, channel, distance, obstruction,
   power-save settings, admission state, join times, sent/received/lost/local
   errors, and RTT percentiles. Repeat with 2, 3, and 4 badges.
7. Toggle client power saving and repeat, including a run after idle time.
8. Power off a client and confirm it becomes stale/offline. Reboot it,
   reopen host admission, and verify that rejoining uses its original slot.
9. Reset the host. Confirm clients report the loss rather than continuing
   to claim a valid session; create/select the new session.
10. Try an unavailable host, a busy BLE host, and an expired admission
    window. Confirm bounded errors and retry/cancel behavior.

Host tests:

```sh
make test-wifi-handover
```

This rebuilds and runs sanitizer-enabled tests for controls/rendering,
session/admission policy, credential encoding and fragmented reads, mocked
BTstack provisioning/cancellation/startup deadlines, UDP packets/statistics,
and mocked Wi-Fi handshakes, relay, and send-error handling. It uses the
host compiler and existing BTstack GATT generator; `HOST_PYTHON` can select
the Python 3 executable.

Passing host tests/builds is not a claim that this experiment has already
run on the four physical devices. Physical results should be recorded here
after testing.

### Host validation on 2026-09-14

- The SDK firmware builds with `-Wall -Wextra -Werror`.
- All six sanitizer-enabled host test binaries pass, including the separate
  BLE startup-timeout run.
- `picotool` identifies the image as RP2350 Arm Secure. The executable UF2
  payload matches the generated binary; the SDK's extra compatibility block
  is accounted for separately.
- Mock-rendered views were visually inspected, and the four displayed
  edge assignments are tested.
- Existing screen-test, IMU-demo, tilt-breakout, and diagnostic UF2 files
  rebuilt byte-for-byte identically after the shared display timing change.
- TinyUSB is absent in the vendored SDK; this target intentionally disables
  USB/UART stdio and uses the TFT for feedback.

No BLE-to-Wi-Fi handover, SoftAP, or multiplayer latency result on the
physical badges is claimed yet.
