# Wireless Lab

`wireless_lab.uf2` is a combined BLE exploration tool for two Universe 2026
badges. It uses the CYW43439 Bluetooth controller through Pico SDK 2.3.0 and
BTstack. Unlike the freestanding firmware, this target intentionally depends on
the Pico SDK and BTstack.

For Wi-Fi rather than this BLE lab, see the
[freestanding host results](wifi.md) (vendor CYW43439 firmware) and the
[separate source-radio handoff](cyw43/handoff.md). BLE discovery/gameplay
and SDK results do not establish source-only Wi-Fi reception.

## Modes at a glance

| Mode | Radio behavior | Purpose |
| --- | --- | --- |
| Scanner | Active scanning | Observe all advertisements, badge advertisements, addresses, and RSSI |
| Beacon | Non-connectable advertising | Measure discovery and duplicate/loss behavior without a connection |
| Peer | Connectable advertising plus scanning, followed by GATT | Validate deterministic role election and manual bidirectional messages |
| Metrics | Same direct GATT peer link plus ping/ack instrumentation | Measure application loss, RTT, jitter, and connection-profile effects |

## Controls

- Left: previous mode.
- Right: next mode.
- Enter in Beacon mode: increment the advertised counter immediately.
- Enter in Peer mode: send the next message after a connection is ready.
- Enter in Metrics mode: start or stop a 100-packet benchmark after both
  devices are ready.
- Up in Metrics mode: request the BLE minimum 7.5 ms interval with zero
  peripheral latency.
- Down in Metrics mode: request a 30 ms interval with peripheral latency 4.

## Scanner

Scanner mode performs active BLE scanning. The display shows:

- the total number of advertising reports, including duplicates;
- reports containing the experimental badge service UUID `0xFF26`;
- the address suffix and RSSI of the strongest report observed since entering
  the mode.

The report count measures radio reports, not unique devices. Duplicate
advertisements are deliberately retained so this mode can expose advertising
frequency and loss behavior.

The display runs on the RP2350's second core, isolated from the CYW43439 and
BTstack callbacks on the first core. It is drawn once in full when a mode is
entered. Live values then use three small dirty-row transfers at up to four
updates per second. This prevents radio interrupt activity from splitting the
software-driven LCD bus transaction. The GPIO write strobe uses the same
setup, low-pulse, and hold timing as the physically validated display demos;
shortening that cycle causes corrupted columns and partial letter fragments.

## Beacon

Beacon mode emits non-connectable legacy advertisements. The payload contains:

- general-discoverable and BLE-only flags;
- the local name `U26 LAB`;
- experimental service UUID `0xFF26`;
- manufacturer data containing an experimental company ID (`0xFFFF`),
  protocol version 1, current mode, a 16-bit counter, and the badge address
  suffix.

The counter advances once per second. These advertisements are public,
unauthenticated, lossy, and spoofable. UUID `0xFF26` and company ID `0xFFFF`
are for local experiments only and are not registered assignments.

## Peer

Peer mode advertises and scans concurrently. When two lab badges find each
other, the badge whose full Bluetooth address sorts first becomes the central;
the other remains the peripheral. This deterministic election prevents both
badges from waiting indefinitely in the same role.

The central opens a direct BLE connection, discovers service `0xFF26` and
message characteristic `0xFF27`, and enables notifications. Enter sends a
seven-byte message:

1. message type (`1` manual, `2` metrics ping, or `3` metrics acknowledgement);
2. two-byte sender address suffix;
3. four-byte little-endian sequence number.

Central-to-peripheral messages use acknowledged GATT writes. Peripheral-to-
central messages use GATT notifications. The screen displays the local role
and the latest transmitted and received sequence numbers. No BLE pairing,
bonding, encryption, application authentication, or replay protection is used
in this first raw transport experiment.

The service and characteristic are represented in the GATT database as full
Bluetooth-base 128-bit UUIDs (`0000FF26-0000-1000-8000-00805F9B34FB` and
`0000FF27-0000-1000-8000-00805F9B34FB`). Client discovery therefore uses
BTstack's UUID128 APIs; the 16-bit `0xFF26` form is used only in the compact
advertising payload.

## Metrics

Metrics mode establishes the same direct peer connection as Peer mode. Press
Enter on either ready badge to make that badge the benchmark initiator. Run
only one initiator at a time.

The initiator sends 100 application-level ping messages. It keeps only one
ping outstanding, waits up to one second for the matching application-level
acknowledgement, and inserts a 50 ms gap before the next ping. Either role can
initiate:

- a central sends pings using acknowledged GATT writes and receives ack
  notifications;
- a peripheral sends ping notifications and receives ack writes.

The display reports:

- sent packets and received acknowledgements;
- timed-out packets and integer loss percentage;
- the negotiated connection interval (`CI`) and peripheral latency (`LAT`)
  before a run starts;
- `R`, the latest round-trip time;
- `N`, the minimum round-trip time;
- `A`, the arithmetic mean round-trip time; and
- `X`, the maximum round-trip time.

All latency values are milliseconds measured by the initiating badge's
monotonic BTstack clock. For peripheral-initiated samples, timing begins when
BTstack accepts the notification for transmission rather than when the
application first requests permission to send. "Loss" means that a matching application
acknowledgement was not received within one second. It therefore includes
radio loss, link failure, or a peer that stopped responding; it is not a raw
count of over-the-air BLE retransmissions. Locally busy acknowledgement paths
are retried. A locally blocked initiating path stops with `LOCAL SEND ERROR`
instead of being counted as radio loss. Stopping a run with Enter reports
`STOP` and counts any already-transmitted outstanding ping as lost.

For comparable results, run the test once from the central and once from the
peripheral at each distance or orientation. Record role, distance, obstructions,
sent, acknowledgements, loss, and the four RTT values.

The Up and Down controls allow direct comparison of two connection profiles:

| Control | Requested interval | Requested peripheral latency | Purpose |
| --- | ---: | ---: | --- |
| Up | 7.5 ms | 0 | Experimental minimum-latency game link |
| Down | 30 ms | 4 | Lower-duty-cycle baseline |

The peer may reject or alter a request. Wait for the displayed `CI` and `LAT`
to change before starting the next run; those displayed values come from the
controller's connection-complete or connection-update event. The third line
reports `REQUEST ULTRA` or `REQUEST BASE` immediately after the button press,
then `REQUEST ACCEPTED`, `PROFILE APPLIED`, `PROFILE REJECTED`, or
`LOCAL REQUEST ERR`. This distinguishes button handling from peer/controller
acceptance.

## Two-badge test

1. Build with `make wireless-lab`.
2. Flash the same `wireless_lab.uf2` onto both badges.
3. Use Left or Right on each badge to select Scanner and Beacon. Confirm that
   the scanner's badge-report count rises and note RSSI at several distances.
4. Select Peer on both badges at approximately the same time.
5. Wait until both displays report a role and `READY`.
6. Press Enter alternately on each badge. The other badge's RX number should
   follow the sender's TX number.
7. Select Metrics on both badges, wait for `READY`, and press Enter on one
   badge. Record its results after all 100 pings complete.
8. Repeat with the other badge as initiator.
9. Move the badges apart or shield one, then repeat while recording discovery
   latency, connection behavior, loss, and RTT.

The current build is an instrumentation baseline, not the final shake-to-pair
protocol. Experimental Wi-Fi monitor mode, Bluetooth Classic, and richer BLE
discovery remain separate follow-up investigations.

## Physical validation

The combined firmware has been tested on two Universe 2026 badges:

- Beacon advertisements were detected by Scanner mode in both badge-role
  directions, and the displayed address suffix matched the advertising badge.
- With local address suffixes `1B26B9` and `1B26EF`, deterministic election
  selected `1B26B9` as central and `1B26EF` as peripheral.
- Both devices reached `READY`.
- Enter-triggered messages updated TX on the sender and RX on the receiver in
  both directions.

The first adjacent-badge 100-packet measurements, taken before separating the
peripheral notification queue delay from over-the-air RTT, were:

| Initiator | Acknowledged | Loss | Latest RTT | Minimum RTT | Average RTT | Maximum RTT |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Central (`1B26B9`) | 100/100 | 0% | 55 ms | 34 ms | 45 ms | 62 ms |
| Peripheral (`1B26EF`) | 100/100 | 0% | 152 ms | 49 ms | 148 ms | 294 ms |

These results demonstrate zero observed application-level loss in each
100-packet sample, but they do not establish a zero loss probability. They
also expose substantial role-dependent latency and jitter. The refined
firmware displays the negotiated connection parameters and excludes the
peripheral's local notification-queue wait from RTT so the next runs can
separate connection scheduling from local send scheduling.

### Controlled connection-profile comparison

Adjacent badges were subsequently tested with the refined timing and both
selectable connection profiles. Every run delivered 100/100 application
acknowledgements with zero measured loss:

| Profile | Initiator | Latest RTT | Minimum RTT | Average RTT | Maximum RTT | Loss |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Fast: 15 ms, latency 0 | Central | 30 ms | 18 ms | 30 ms | 34 ms | 0/100 |
| Fast: 15 ms, latency 0 | Peripheral | 30 ms | 17 ms | 27 ms | 32 ms | 0/100 |
| Base: 30 ms, latency 4 | Central | 146 ms | 133 ms | 147 ms | 168 ms | 0/100 |
| Base: 30 ms, latency 4 | Peripheral | 37 ms | 34 ms | 44 ms | 60 ms | 0/100 |

The fast profile made the two directions nearly symmetric and reduced average
RTT to 27-30 ms. The base profile retained zero observed loss but made
central-initiated RTT much larger because the acknowledgement originates at a
peripheral permitted to skip four 30 ms connection events. Peripheral-
initiated RTT remained lower because its timing begins when its notification
is accepted for transmission and the central does not use peripheral latency.

For an interactive two-player game, the validated 15 ms fast profile is
already a suitable default. The 7.5 ms ultra profile has now also passed the
initial adjacent-badge latency/loss test and is the best measured profile for
fast-paced play. Longer-duration stability, power, range, and coexistence
behavior still need characterization. The base profile remains useful during
idle or discovery phases when reduced radio duty cycle matters more than
response latency.

### Ultra-profile result

The controller accepted the BLE minimum 7.5 ms interval with peripheral
latency 0 on both badges. Adjacent-badge results were:

| Profile | Initiator | Latest RTT | Minimum RTT | Average RTT | Maximum RTT | Loss |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Ultra: 7.5 ms, latency 0 | Central | 12 ms | 11 ms | 14 ms | 20 ms | 0/100 |
| Ultra: 7.5 ms, latency 0 | Peripheral | 14 ms | 10 ms | 13 ms | 22 ms | 0/100 |

The ultra profile delivered 200/200 total acknowledgements, reduced average
RTT by approximately half relative to the 15 ms profile, and remained nearly
symmetric between initiator roles. Its 10-11 ms observed minima show that the
complete application ping/ack can finish within roughly one to two 7.5 ms
connection intervals.

Zero losses in these two 100-packet samples do not prove a zero loss
probability. Longer runs and increasing-distance tests are still required,
particularly because the ultra profile doubles connection-event frequency
relative to the 15 ms profile.

## Latency improvement opportunities

The measured 27-30 ms average RTT is not the lowest BLE latency theoretically
available on this hardware/software path. That result used a 15 ms connection
interval. Up now requests the BLE minimum 7.5 ms interval (`0x0006` units of
1.25 ms), so the next controlled experiment is:

1. Completed: request a 7.5 ms interval and peripheral latency 0.
2. Completed: verify the controller-confirmed `CI`/`LAT`.
3. Completed: repeat 100-packet runs in both directions.
4. Pending: characterize longer runs, range, coexistence, and power
   consumption.

A ping and its application acknowledgement generally need opportunities in
both directions. The measured 10-22 ms range confirms that a 7.5 ms interval
can move these short-packet round trips into the expected one-to-two-event
region, with occasional additional host/controller scheduling delay.

The central currently sends with an ATT Write Request, which has its own ATT
response even though Metrics adds a separate application acknowledgement.
Adding `WRITE_WITHOUT_RESPONSE` to the characteristic and using BTstack's
can-write-without-response flow would remove that redundant protocol response.
Sequence numbers, timeouts, and application acknowledgements would continue to
provide measurement and recovery semantics. This should particularly help the
central-to-peripheral direction.

Other possible optimizations are secondary:

- request and verify the controller's fastest supported PHY before assuming
  LE 2M is available; for seven-byte messages, connection scheduling is likely
  more important than airtime;
- avoid unnecessary display or application work on the radio core;
- measure one-way timing with synchronized clocks if direction-specific
  transport latency becomes important;
- measure longer runs, because 0 losses in 100 packets only bounds the
  underlying loss probability weakly.

For actual gameplay, do not make every frame wait for a round trip. Send
compact, sequenced input or state updates continuously; discard stale packets,
interpolate remote movement, predict short gaps, and use occasional reliable
state snapshots. With that design, perceived control latency follows one-way
delivery rather than the complete 27-30 ms ping/ack RTT.

Latency improvements trade against energy use and radio coexistence. A 7.5 ms,
latency-0 link wakes twice as often as the current fast profile and much more
often than the 30 ms/latency-4 base profile. A practical game should use the
low-latency profile during a match and return to a lower-duty-cycle profile
while idle.

This confirms the CYW43439 BLE transport, simultaneous Peer discovery,
central/peripheral election, GATT service discovery, acknowledged writes, and
notifications on the physical hardware. Range, discovery latency, packet-loss
behavior, reconnect behavior, and operation with more than two badges remain
to be characterized.

## Gameplay follow-up

On 2026-09-14 the owner reported playing the separate
[BLE tilt Pong firmware](ble-tilt-pong.md#physical-validation) on two badges,
with great speed and no significant perceived lag. This is a positive
qualitative gameplay result, not another Wireless Lab benchmark. No numerical
Pong latency or packet-loss measurements were reported, and the RTT tables
above should not be treated as measurements of Pong input-to-display latency.
