# LSM6DS3TR-C IMU Demo

## Artifact

Build with `make`, then install:

```text
build/universe26/uf2/imu_demo.uf2
```

The target is freestanding C compiled for Cortex-M33. It uses no libc, Pico
SDK, operating system, or third-party runtime library.

## Hardware path

The demo follows the manufacturer-supplied 2026 schematic:

| Function | Connection |
| --- | --- |
| Switched peripheral supply | GPIO41 high enables `3V3_SW` |
| Internal I2C SDA | GPIO18 |
| Internal I2C SCL | GPIO19 |
| Sensor address | `0x6a` |
| Sensor identity | register `0x0f` must return `0x6a` |
| Sensor interrupt | GPIO20; unused by this polling demo |

The display initialization drives GPIO41 high. After the rail settles, the
demo configures GPIO18/19 as open-drain bit-banged I2C. The bus is shared with
the capacitive-touch controller, so the code never actively drives either bus
line high.

## Sensor configuration

The demo configures:

- block data update and register auto-increment;
- accelerometer at 104 Hz and +/-2 g;
- gyroscope at 104 Hz and 245 dps.

It reads the 12-byte gyroscope/accelerometer output block after each display
update, with a minimum 10 ms interval.
Accelerometer values are converted with the +/-2 g sensitivity of
0.061 mg/LSB using integer arithmetic. Gyroscope values are shown as raw signed
16-bit samples.

Only the six numeric fields and changed visualization columns are repainted
during normal operation. The static labels and surrounding screen remain
untouched, avoiding the slow refresh that resulted from repainting most of the
display for every sample. Firmware waits 10 ms between iterations, close to
the sensor's 9.6 ms sample interval; actual screen cadence is limited by the
changed pixels that must be transferred.

## Orientation visualization

The lower-right quadrant contains a wireframe representation of the badge
plane and three device-axis arrows:

- red: positive X;
- green: positive Y;
- blue: positive Z.

The view derives roll and pitch from a low-pass-filtered gravity vector. It
follows the physically observed signs: screen-up is negative Z, screen-down is
positive Z, right tilt is positive X, and tilt toward the holder is positive
Y. The filter reduces stationary jitter without changing the displayed raw
values.

Gravity cannot determine rotation around the vertical axis, so the view does
not represent absolute yaw or compass heading. Linear acceleration also
temporarily affects an accelerometer-only tilt estimate. The gyroscope remains
displayed for inspection but is not integrated into the orientation estimate,
avoiding unbounded yaw drift in this simple demo.

## Display and errors

The normal display shows:

- the expected sensor identity;
- live X, Y, and Z acceleration in mg; and
- live X, Y, and Z raw gyroscope values.

If GPIO18/19 are not idle high, the sensor does not acknowledge, the identity
is not `0x6a`, or a sample read fails, the screen displays an explicit error
instead of silently substituting values.

## Physical validation

Identity `0x6a`, live samples, and badge-relative axis signs have been
physically confirmed. One stationary
observation produced approximately X = -17 mg, Y = -93 mg, and Z = -958 mg,
or a vector magnitude of about 963 mg. This is consistent with a stationary
sensor measuring gravity.
