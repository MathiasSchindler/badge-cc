# Responsive GPIO Monitor

## Artifact

Build with `make`, then install:

```text
build/universe26/uf2/gpio_monitor.uf2
```

## Monitored pins

The monitor observes GPIO0 through GPIO22. This is the historical discovery
range, not a set of unassigned pins: the schematic identifies LEDs, PSRAM CS,
I2C buses, and peripheral signals in it. GPIO23 through GPIO47 remain excluded;
known wireless, display, power, and analog signals occupy that range.

Every monitored pad has:

- digital input enabled;
- isolation removed;
- a weak internal pull-up; and
- its output driver forcibly disabled.

The monitor does not change the GPIO function or actively drive a monitored
pin, but its pull-ups and output disabling can affect attached peripherals.
Use it as standalone diagnostic firmware, not alongside PSRAM or other
drivers that own pins in its monitored range.

## Display

The high-resolution table has one persistent row per GPIO:

```text
PIN  EDGE  LEVEL COUNT  TIME MS
```

- `EDGE` is `FALL`, `RISE`, or `BOTH`. `BOTH` means both sticky edge flags
  accumulated before firmware serviced the pin.
- `LEVEL` is the raw pad level after the event was detected.
- `COUNT` is the accumulated number of edge flags, saturating at 9999.
- `TIME MS` is milliseconds since monitoring began. It is the time firmware
  detected the sticky flag, not a hardware-captured edge timestamp.

Only the changed row is redrawn. RP2350 sticky edge latches continue collecting
events during that short display update, avoiding the multi-second blind window
of the original guided mapper.

The monitor performs one small test redraw for each of its 23 table rows at
startup and records GPIOs that edge as a consequence of display traffic. It
clears those GPIOs immediately after later screen writes so the monitor does
not report its own activity. Those pins remain fully monitored between display
writes; only their display-correlated edges are suppressed.

The initial version physically showed GPIO15 after every confirmed front-button
event. This was initially classified as display-correlated because each event
caused a redraw before the scan reached GPIO15. The manufacturer schematic
supersedes that interpretation: GPIO15 is `SWITCH_INT`, the diode-combined
signal from all five front buttons. The existing display-edge suppression
can therefore hide genuine button-correlated events; it is not evidence of
display noise.

GPIO22 was subsequently physically identified as the rear Home button.
The schematic maps GPIO17 to `IR_RX`, GPIO13 to the CAP1208 `CAP_ALERT`,
and GPIO21 to the display `TE` signal (`LCD_VSYNC`). Isolated GPIO17 activity,
including activity during USB unplugging, does not establish USB detection;
that function is on GPIO12. See [the specification](spec.md).

## Accelerometer interrupt interpretation

The manufacturer schematic now confirms GPIO20 as `IMU_INT`. The monitor's
earlier Low startup observation on GPIO20 is therefore associated with the
LSM6DS3TR-C, although interrupt polarity and enabled sources remain untested.

First leave the badge untouched and note spontaneous rows. Then, without
touching a lower pad:

1. gently rotate the badge;
2. shake it;
3. tap the PCB or enclosure at several locations; and
4. compare the GPIO, edge type, count, and timestamp.

Next touch lower controls gently without moving the badge. Motion-correlated
events on GPIO20 can help determine the IMU interrupt configuration. GPIO13 is
the schematic-confirmed shared touch alert. Neither device's interrupt
configuration is established by this GPIO-only monitor; missing events do not
prove a missing device or touch. CAP1208 configuration and channel reads must
use the internal GPIO18/19 I2C bus.
