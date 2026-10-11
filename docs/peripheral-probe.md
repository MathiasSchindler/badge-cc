# External I2C and Lower-Control Probe

> This is a historical external-bus probe. The manufacturer schematic
> identifies the onboard LSM6DS3TR-C and CAP1208 on the separate internal
> GPIO18/19 bus. Neither is reachable through this GPIO4/5 probe.
> Use `imu_demo.uf2` for the motion sensor; touch-controller discovery
> requires an internal-bus implementation.

## Artifact

Build with `make`, then install:

```text
build/universe26/uf2/peripheral_probe.uf2
```

## Safety model

The probe accesses GPIO4 SDA and GPIO5 SCL, now schematic-confirmed as the
external Qw/ST connector bus. This bus was physically scanned with no devices
responding on the tested prototype. The schematic shows its supply and
10 kOhm pull-ups on `3V3_SW`, enabled by GPIO41.

Before changing either GPIO, it reads both raw pad levels. If either line is
not already high, it displays `BUS NOT IDLE HIGH / SCAN ABORTED` and does not
drive the bus.

If both are high, it uses open-drain signaling only: it drives zero or releases
the line and never drives a logic high. The initial address scan uses read
transactions, not register writes.

## Historical hypotheses

- Pimoroni's public Tufty 2350 board definition maps I2C0 to GPIO4/5.
- Its firmware uses that bus for a PCF85063 RTC at address `0x51`.
- Pimoroni supports the MSA301 accelerometer at fixed address `0x26`; its part
  ID register is `0x01` and should return `0x13`.
- Pimoroni's QwSTPad game controller uses a TCA9555-compatible register map at
  `0x21`, `0x23`, `0x25`, or `0x27`.

These prior-board references are retained only to explain the probe's checks.
The MSA301 and TCA9555/QwSTPad hypotheses are superseded for the onboard
2026 sensors/controls. A `PAD CAND` label would describe an external-bus
candidate, not identify the built-in CAP1208 touch controller.

## Display

The top `DEV` row lists every responding 7-bit I2C address, up to eight.
Report this row exactly.

If address `0x26` responds, the probe reads:

- part ID register `0x01`; and
- raw X/Y/Z registers `0x02` through `0x07`.

`MSA301 ID 13` in green plus changing X/Y/Z values while rotating the badge is
strong identification evidence.

If `0x21`, `0x23`, `0x25`, or `0x27` responds, it is labeled `PAD CAND`. The
probe repeatedly reads its two input registers:

- `RAW` is the current 16-bit value;
- `CHG` accumulates every bit that has changed since boot.

If a compatible external gamepad is connected, press its controls separately
and report the `RAW` value before, during, and after each press. Also report
the final `CHG` value. These instructions do not apply to the badge's onboard
touch pads. Do not infer an external gamepad's button mapping from the
QwSTPad public mapping until the physical values match.

## Report

For an external-device investigation, provide:

1. the complete `DEV` address list;
2. the MSA301 ID and whether X/Y/Z change when the badge is rotated;
3. the pad candidate address, if shown;
4. idle `RAW`;
5. pressed `RAW` for A, B, Select, Start, Up, Down, Left, and Right; and
6. final `CHG`.
