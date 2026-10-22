# HT20 tilt Pong: proposed specification

Status: design proposal, not an implemented or qualified game.

Build a dependency-free C/nolibc two-badge Pong game using the source-built
[clean-rx firmware](../clean-rx/) rather than the vendor Wi-Fi firmware or
Pico SDK/BTstack. Preserve the gameplay and coordinate conventions of
[BLE tilt Pong](ble-tilt-pong.md), but make the Wi-Fi chip an autonomous
game-state communications coprocessor.

## Goals and non-goals

- Reduce fresh local-state publication to peer-state availability latency,
  jitter and dependence on RP2350 rendering work.
- Keep local steering responsive even when the radio is delayed.
- Prefer fresh state over reliable delivery of obsolete paddle positions.
- Preserve one authoritative ball simulation and consistent scoring.
- Expose stale input, loss, restarts and failures rather than masking them
  with prediction or resetting counters.
- Measure input, transport and display delays separately.

The first version is a fixed, controlled pair on a configured channel.
It is not an AP client, general multiplayer framework, BLE implementation,
secure network protocol or qualification of negotiated HT operation.
Neither sub-millisecond input-to-photon latency nor completion of D2 is
assumed.

## Recommended architecture

### RP2350: input, game and display

The RP2350 owns the accelerometer, input filtering, local paddle integration,
ball physics, collisions, scores, game phases and TFT rendering.
It publishes local state and consumes remote state through small mailboxes
over the existing host/radio bus.

Use one explicit owner of the radio bus. Input and rendering must not hold
that bus or delay servicing for a full display frame. A second RP2350 core
is optional, not a prerequisite; first establish bounded scheduling with
the simplest implementation.

### CYW43439: autonomous state transport

The chip's ordinary C firmware owns:

- Latest local and remote state records.
- Packet construction, admission, sequence checks and duplicate rejection.
- Autonomous transmit scheduling and bounded channel-access handling.
- Control-message ACKs, retry deadlines and peer-liveness monitoring.
- Transport diagnostics and publication of validated peer state.

After initial configuration, **RP2350 must not issue a transmit command,
upload a complete frame, or process an application ACK for each update**.
Receiving a packet must not require forwarding its raw bytes to RP2350.
Diagnostic raw-packet capture may exist separately and must not be required
on the normal gameplay path.

Keep game rules out of the Wi-Fi firmware. Its time-critical PSM/MAC
machinery remains responsible for radio operations, not game simulation.
Autonomous scheduling must preserve the controller's RX/TX ownership,
channel-access, bounded buffering and error-reporting rules.

```text
Accelerometer
    |
RP2350 input/game --> local-state mailbox
                              |
                    Wi-Fi C transport
                              |
                     short HT20 frames
                              |
                    Wi-Fi C transport
                              |
                      remote-state mailbox --> peer RP2350 game/display
```

Paddle state necessarily crosses the host/radio boundary once at each end.
The optimization removes packet-by-packet host supervision, not these
essential transfers.

## Host/radio interface

Provide versioned configuration, local publication, remote snapshot,
control-event and diagnostic operations.

Configuration includes role, expected peer identity, pair identifier,
channel, radio profile and operating limits. Reject incompatible versions,
invalid lengths and out-of-range values explicitly.

Local publication carries an RP2350 input sequence, position, velocity,
sample timestamp and validity flags. Remote snapshots include the admitted
peer session/sequence, state, freshness information and transport status.
The authority additionally publishes game snapshots and discrete events.

Mailboxes are **latest-value**, bounded records, not packet queues:

- Publishing new state replaces an older unsent value.
- A receiver can replace unread state with a newer valid value.
- Count replacements separately from transmission loss.
- A consumer can distinguish unchanged state from newly published state.
- A complete record is committed atomically from the consumer's perspective.
  Specify ownership, ordering and torn-read protection for the actual bus;
  this is not cache-coherent shared RAM.
- Support a consistent diagnostic snapshot without blocking the radio for
  unbounded periods.

Use explicit fixed-width serialization rather than C structure layout.
Memory, copies and bus transactions must be bounded. No heap or external
runtime is required on either processor.

## Gameplay and authority

For the first prototype, configure roles A and B explicitly. A is the game
authority; automatic discovery/election is a later feature.

Both badges immediately integrate and render their own tilt-controlled
paddle. B sends its paddle state to A. A alone decides ball motion,
collisions, scores, serves, pauses and resets, then publishes authoritative
snapshots. A also sends its paddle state as part of those snapshots.

Retain the BLE game's normal screen orientation, local bottom paddle,
dead zone and nonlinear steering curve, court geometry and 180-degree peer
view. In particular:

```text
shared_top_x = 320 - peer_local_x - paddle_width
shared_top_velocity = -peer_local_velocity
```

Retain first-to-9 play and the automatic post-result match restart unless
playtesting identifies a reason to change them.

Initially B renders authoritative ball snapshots without prediction.
After transport measurement, bounded interpolation/extrapolation may be
evaluated separately. It must never award points, conceal a stale peer or
be reported as reduced transport latency.

## Radio protocol

Start from the demonstrated short, unaggregated HT20 MCS0 profile, fixed
channel and experimental framing used by the
[button messenger](../clean-rx/messenger/README.md). Keep initial test frames
at its demonstrated 80-byte MPDU size, including FCS. Do not make aggregation,
Block ACK, high MCS rates or hardware SIFS responses prerequisites.

Define a separate Pong magic/version and reject messenger or unrelated
traffic. Before implementation, freeze byte offsets and native golden
vectors for the following proposed version-1 layout:

- Existing 24-byte MAC header and 8-byte LLC/SNAP framing.
- 24-byte common header: magic u32; version, kind, role and flags u8;
  source boot session, destination boot session, match ID and packet
  sequence u32.
- 20-byte kind-specific body.
- 4-byte FCS.

All multibyte values are little-endian. Reserved bits/bytes are zero.
Position uses signed i16 Q4 pixels; velocity uses signed i16 integer
pixels/second, with explicit game-specific range checks before use.

| Kind | Proposed 20-byte body |
| --- | --- |
| HELLO / HELLO_ACK | Pair identifier, capabilities, local challenge, echoed peer challenge, reserved: five u32 |
| PADDLE | Local x i16, velocity i16, input sequence u32, source RP2350 sample ticks u32, input flags u32, reserved u32 |
| SNAPSHOT | Ball x/y i16, ball vx/vy i16, both local paddle x values i16, scores u8/u8, phase u8, flags u8, authority publication sequence u32 |
| EVENT | Event ID u32, authority game tick u32, event kind u8, scores u8/u8, winner u8, kind-specific data: 8 bytes |
| EVENT_ACK | Event ID u32, event kind u8, reserved: 15 bytes |

Validate expected peer identity, pairing, both sessions, match, version,
kind, length, flags and value ranges before publishing state. Bad FCS or
malformed frames must not change game-visible state.

Establish the two current boot sessions through a challenge/echo handshake
before accepting gameplay. Only initial discovery permits an unknown
destination session. An unsolicited HELLO must not silently replace a live
peer. Reboots invalidate old state and pending control ACKs; a fresh match
is established explicitly. Do not treat a pair ID or challenge as
authentication.

Use wrap-safe packet sequencing within a session, with a defined maximum
unambiguous sequence distance. Distinguish input sequence from transmitted
packet sequence so coalesced host inputs are not miscounted as RF losses.
Old or duplicate state cannot move a paddle backwards in update history.

### Continuous state

PADDLE and SNAPSHOT are newest-wins. Send a fresh pending state promptly,
subject to a bounded rate and valid channel access. Do not wait for an
application ACK or retransmit an obsolete state. Periodically refresh
unchanged valid state for liveness; never falsely refresh an old sensor
sample's timestamp.

Begin transport experiments with 100 fresh publications/second per badge,
then sweep rates through 250/second. These are test settings, not a claim
that the current accelerometer provides 250 fresh samples/second.
Count publications, coalescing, transmit attempts and validated receptions
separately.

### Discrete events

Score changes, serve transitions and match resets use idempotent, identified
events with explicit ACKs and bounded retries. These ACKs are application
messages, not IEEE 802.11 ACK/SIFS qualification.

Allow at most one outstanding control event initially; enforce game-phase
barriers rather than silently dropping a second event. Continue fresh
paddle traffic while awaiting its ACK. Correlate ACKs with source/destination
sessions, match and event ID. Duplicate events may be ACKed again but must
not apply twice. Retry exhaustion is a visible protocol failure.
Snapshots repeat the resulting phase and scores for consistency, but
must not bypass an unacknowledged transition barrier.

## Input and display latency

The existing BLE configuration samples the IMU at 104 Hz and services input
at 60 Hz: approximately 9.6 ms and 16.7 ms between opportunities. Those
intervals can dominate a faster transport.

Measure sample readiness, sample age, filtering delay and input-loop
scheduling before choosing a faster supported IMU configuration. Decouple
input sampling, fixed-step game simulation, state publication and rendering.
Do not poll faster and call repeated copies of one sensor sample new input.

Retain dirty-region rendering for paddles, ball, scores and status. Full
frames are for transitions, not every packet. Respect the shared driver's
column-major canvas and LCD addressing; preserve a pixel-stream regression
test. Do not inherit the messenger's approximately 65-ms full-frame
completion as the intended Pong rendering strategy.

## Staleness, recovery and limitations

Initial safety policy, to be validated experimentally:

- Mark remote state stale after 50 ms without a fresh valid state refresh.
  A heartbeat alone does not make an invalid sensor sample current.
- The Wi-Fi chip independently ages host publications on its own clock.
  After 50 ms without a fresh valid host publication, mark outgoing state
  invalid; autonomous repetition must not conceal a stalled RP2350 or IMU.
  Receivers use advancing source input/publication sequences, not merely
  advancing radio packet sequences, to recognize fresh source state.
- Freeze/expose the remote paddle rather than extrapolating indefinitely.
- Pause authoritative play after 250 ms of missing usable peer state.
- Require a ready/session handshake before resuming; never invent a score
  for an outage.
- Show IMU faults, radio faults, reconnecting and control retry exhaustion.
  Count and retain radio resets and failed control events.

Final thresholds may change only with documented measurements. Full radio
reload/recovery is not normal low-latency operation.

Initial traffic is plaintext and unauthenticated, for controlled testing.
Nearby transmitters can observe or inject it. Peer/session checks prevent
accidental cross-talk and stale application traffic, not hostile spoofing.
Do not claim this completes the secure station lifecycle or unresolved
[D2 receive-reliability work](../todo.md).

## Measurement and acceptance

The previous game's approximately 10-ms observation is not automatically
comparable to the messenger's measured approximately 7-ms **round trip**.
Use the [messenger timing record](../clean-rx/messenger/LATENCY.md) as a
methodological baseline, not a one-way Pong result.

Record these separately:

1. Sensor sample readiness to RP2350 input consumption/publication.
2. RP2350 publication to Wi-Fi acceptance and radio scheduling.
3. Radio receive to remote mailbox publication.
4. Source RP2350 publication to peer RP2350 consumption.
5. Peer consumption to completion of the relevant LCD transfer.
6. Physical input to visible pixels, only with suitable external equipment.

RP2350s and Wi-Fi processors have distinct clock domains. Calibrate timer
rates, handle wrap and establish synchronization error before subtracting
timestamps across processors or badges. Prefer GPIO markers and a common
logic-analyzer timebase for one-way measurements. Software LCD completion
is not panel scanout. A separately instrumented echo gives RTT, not RTT/2
as a measured one-way delay.

Provisional performance goals at 100 fresh publications/second per direction:

- Publication-to-peer-consumption median at most 3 ms, p95 at most 7 ms and
  p99 at most 10 ms, with one-way timing uncertainty at most 0.1 ms.
- Report maximum delay, update-age distribution, gaps over 50 ms, loss,
  coalescing and processor/bus load alongside quantiles.
- Repeat with simultaneous bidirectional traffic, moving paddles and
  dirty-region rendering; report interference trials separately.
- Run at least 100,000 publications per direction and a 30-minute gameplay
  soak. For the controlled-channel acceptance run, require no crash,
  spontaneous radio reset, control-event exhaustion, score divergence or
  unreported stale-state interval.
- Report unmet targets and all failures. Do not exclude losses from the
  denominator or count fresh-state replacement as successful delivery.

These are engineering targets, **not existing measurements or guarantees**.
If one-way instrumentation is unavailable, report local intervals and RTT
but leave the one-way acceptance criteria unverified.

## Implementation and validation sequence

1. **Transport microscope:** synthetic RP2350 publications, autonomous
   Wi-Fi exchange, latest-state readback, explicit status/counters. Verify
   packet paths run without per-update host TX commands.
2. **Instrumented comparison:** compare the current host-driven path and
   autonomous path with identical frames, rates, channel and measurement
   endpoints. Add real display load before declaring improvement.
3. **Tilt paddle demo:** measure actual sensor freshness and both-direction
   paddle response; tune filtering separately from transport.
4. **Full Pong:** reuse the authority model, geometry and gameplay; add
   reliable transitions without blocking fresh-state traffic.
5. **Qualification:** native tests for serialization, range checks, torn
   publication, wrap, coalescing, duplicates, loss, stale sessions,
   idempotent events and retry exhaustion. Hardware-test simultaneous
   updates, packet loss, IMU failure, peer power cycles, radio recovery,
   matching scores, coordinate transforms and visible screen correctness.

Keep implementation and directly coupled tests under `clean-rx/`, reusing
shared badge helpers without changing unrelated applications. Define build
and flash commands when the implementation exists; no Pong UF2 or measured
autonomous-transport result is implied by this specification.
