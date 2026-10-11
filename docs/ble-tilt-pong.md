# BLE tilt Pong

`ble_tilt_pong` is a separate Pico SDK firmware for two RP2350B/Pimoroni
Tufty badges. Build it with:

```sh
make ble-tilt-pong
```

The resulting image is
`build/universe26/uf2/ble_tilt_pong.uf2`.

## Architecture

Both badges run the same image. Core 0 exclusively owns CYW43439 and BTstack
work. Core 1 owns the ST7789 LCD, GPIO18/19 bit-banged IMU bus, 60 Hz input
loop, game simulation, and rendering. Short SDK mutex-protected latest-value
mailboxes cross the core boundary. This separation avoids LCD bus corruption
from radio timing and IRQ activity.

The LCD GPIO mapping, initialization, `CASET=y`/`RASET=x` addressing,
`MADCTL=0x90`, and 4-cycle setup, 8-cycle WR-low, 4-cycle hold timing match
`src/sdk/wireless_lab/main.c`. The LSM6DS3TR-C uses address `0x6a`, GPIO18 SDA,
GPIO19 SCL, switched power GPIO41, and the shared driver’s 104 Hz setup.

After discovery, the numerically lower full six-byte Bluetooth address becomes
central. The central is authoritative for ball physics, paddle collisions,
scores, point pauses, serves, and match resets. The peripheral simulates only
its own tilt-controlled paddle and sends that local-space position and
velocity. Both sides request a 7.5 ms connection interval (value 6) with
peripheral latency 0 and mark it verified only after the controller reports
those values.

## Controls and gameplay

Hold each badge in its normal screen orientation. Right tilt moves the local
bottom paddle right; left tilt moves it left. Input uses the same filtered,
dead-zone plus quadratic speed curve as `tilt_breakout`: small intentional
tilts move slowly and larger tilts move progressively faster.

Play starts automatically when GATT setup completes. A bounded pause precedes
each serve. Paddle hit offset and paddle velocity steer the ball, and its
vertical speed increases modestly on each hit. A missed ball awards the other
player a point. The first player to 9 wins; after a three-second result screen,
scores reset and a new match starts automatically.

The UI explicitly reports radio startup, peer discovery, connection, GATT
configuration, reconnect, radio failure, and IMU failure. Disconnects
automatically return both devices to discovery. Rendering uses a full frame
for screen/game transitions, then redraws only dirty ball, paddle, score, and
status regions.

## Coordinates

The central’s 320x240 display coordinates are the shared world. Paddle
positions sent on the wire are each player’s **local** left edge.

For authoritative top-paddle collision, the central converts the peripheral
position:

```text
shared_top_x = 320 - peripheral_local_x - paddle_width
shared_top_velocity = -peripheral_local_velocity
```

The peripheral rotates the playable court 180 degrees for display. The
horizontal mirror axis is the full 320-pixel width; the vertical mirror axis
is 261 so the asymmetric screen HUD remains outside the court transform:

```text
display_x = 319 - shared_ball_x
display_y = 261 - shared_ball_y
```

Rectangles use the corresponding left-edge transform
`display_left = 320 - shared_left - width`. The vertical court bounds are
24 through 237, with paddles at y=30 and y=226. Thus each player sees their
own local paddle at the bottom, their opponent at the top, and ball contacts
at the same visible paddle edge on both displays.

## BLE profile and protocol

Advertising includes service UUID `0xFF28` and the name `U26 PONG`.

- Service: `0000FF28-0000-1000-8000-00805F9B34FB`
- Characteristic: `0000FF29-0000-1000-8000-00805F9B34FB`
- Properties: read, write, write without response, notify, dynamic

All multi-byte fields are little-endian. Messages fit the default 20-byte ATT
payload:

| Direction | Type | Bytes | Fields |
|---|---:|---:|---|
| Peripheral to central notification | `1` | 7 | type, sequence u16, local paddle x i16 pixels, local paddle velocity i16 Q8 |
| Central to peripheral write command | `2` | 18 | type, sequence u16, ball x/y i16 Q4, ball vx/vy i16 Q8, central/peripheral local paddle x i16, scores u8/u8, flags u8 |

Snapshot flags identify readiness, serve pause, game over, the winning role,
and verified low-latency parameters. Sequence comparison uses signed 16-bit
modular distance, so wrap is accepted while duplicates and older
out-of-order packets are rejected. There are no application frame ACKs.
When ATT transport is busy, each sender retains only the newest mailbox value;
new input or snapshots replace older unsent frames.

## Limitations and security

This experiment intentionally uses no pairing, bonding, authentication, or
encryption. Any nearby BLE device can observe advertisements, connect, read
the dynamic value, or inject characteristic writes. Use it only in a
controlled test setting. Role election assumes the two intended badges are
the only `FF28` peers nearby.

BLE remains an asynchronous, lossy transport. The peripheral renders the
latest authoritative snapshot without prediction, so interference can cause
visible ball jitter. The central clamps and validates received paddle
positions but this is not an anti-cheat or hostile-input boundary.

## Physical validation

On 2026-09-14 the project owner reported playing BLE tilt Pong on two
badges. Gameplay speed was described as great and latency as very good, with
no significant lag perceived during play.

This confirms successful two-badge gameplay and a positive subjective
responsiveness result. No instrumented timing or packet-loss measurements
were taken for this playtest. It does not establish a numerical latency
bound, measured frame rate, zero packet loss, or the negotiated connection
interval. The separate [Wireless Lab measurements](wireless-lab.md) are
transport benchmarks, not measurements of Pong's end-to-end input/display
latency.

The full procedure below remains a regression checklist. The report does not
claim that reconnect recovery, match reset, every coordinate/collision case,
or controller-parameter verification was individually checked.

## Two-badge test procedure

1. Run `make ble-tilt-pong` and verify the UF2 is produced at the path above.
2. Flash that UF2 to two Tufty badges, then boot them near one another.
3. Confirm both show searching, connecting, and configuring states before the
   court appears automatically.
4. Verify each badge’s own paddle is at the bottom. Tilt each badge right and
   left and confirm the local bottom paddle follows that direction while the
   peer sees it at the top with the correct 180-degree mapping.
5. Rally several times. Confirm paddle offset and motion steer the ball and
   speed increases modestly.
6. Miss alternately at each end. Confirm only the central decides points and
   both displays show matching scores and serve pauses.
7. Play to 9. Confirm both winner views agree, then reset and restart
   automatically after the match pause.
8. Power off one badge. Confirm the other explicitly reports connection loss
   and reconnecting. Power it on and confirm automatic recovery.
9. For interval verification, inspect an over-the-air trace or controller
   events and confirm interval 6 (7.5 ms) and latency 0. The firmware records
   the controller-reported values; the qualitative playtest above did not
   independently verify these parameters.
