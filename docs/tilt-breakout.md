# Tilt Breakout

## Artifact

Build with `make`, then install:

```text
build/universe26/uf2/tilt_breakout.uf2
```

This is a separate freestanding C firmware image. It uses no libc, Pico SDK,
operating system, or third-party runtime library.

## Controls

The game is controlled entirely by the LSM6DS3TR-C:

- tilt the badge left to move the paddle left;
- tilt the badge right to move the paddle right; and
- tilt the top edge forward, away from the holder, to release the ball.

The launch gesture follows the physically confirmed sensor orientation:
forward tilt produces negative Y acceleration.

The paddle has a small dead zone around level. Beyond it, X-axis tilt uses a
quadratic response curve: small tilts allow precise movement, while stronger
tilts increase speed rapidly up to nine pixels per frame. Paddle position and
velocity use Q8 subpixel precision, avoiding the coarse one-pixel speed steps
of the initial implementation. Input uses a short low-pass filter to reduce
sensor jitter.

## Launch gesture

After boot, losing a ball, or clearing all bricks, the ball is attached to the
paddle and the screen displays `TILT FORWARD TO LAUNCH`.

Launch is edge-like rather than level-triggered:

1. Y acceleration must first return above approximately -122 mg.
2. It must then cross below approximately -305 mg.

This neutral-to-forward sequence prevents a badge that remains tilted from
immediately launching a replacement ball.

## Gameplay

- The board contains 32 bricks in four colored rows.
- Each brick is worth 10 points.
- The player starts with three lives.
- Paddle impact position and paddle motion influence the outgoing horizontal
  ball velocity.
- The initial ball speed is approximately 1.9 horizontal and 2.7 vertical
  pixels per frame.
- Every destroyed brick increases ball speed slightly, up to bounded
  horizontal and vertical limits.
- Clearing the board creates a fresh board and waits for another forward-tilt
  launch.
- Losing all lives resets the score, lives, bricks, paddle, and ball.

Ball collision uses a circle-versus-rectangle test. Top and bottom brick
impacts reverse vertical motion; side impacts reverse horizontal motion.

## Rendering and timing

The game targets a 60 Hz loop and reads the IMU once per frame. Actual cadence
cannot exceed the time needed for sensor access, game logic, and display
updates.

Normal frames redraw only:

- the union of the old and new paddle bounds;
- the union of the old and new ball bounds;
- a removed brick;
- the score/lives header when changed; and
- the launch prompt when its state changes.

Full-screen redraws occur only on initial startup, board completion, or a
complete game reset.

## Tuning and extension points

Gameplay constants are grouped near the top of `src/tools/tilt_breakout.c`.
They cover:

- paddle dead zone, full-tilt point, and minimum/maximum speed;
- launch gesture thresholds;
- initial and maximum ball velocities; and
- per-brick acceleration.

The implementation keeps sensor filtering, paddle control, ball physics,
collision detection, game state transitions, pixel composition, and dirty
region transfer in separate functions. Visual changes can therefore be made
primarily in `game_pixel()` and the layout constants without rewriting input
or physics. New mechanics can be added around `update_ball()` and the main
state-transition loop without replacing the renderer.

## Error handling

The game stops with an explicit screen error if:

- the internal GPIO18/19 I2C bus does not initialize;
- the sensor identity is not the expected LSM6DS3TR-C identity; or
- a sensor read fails.
