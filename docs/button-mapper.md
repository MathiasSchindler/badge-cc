# Passive Button Mapper Procedure

## Artifacts

Build with `make`, then install:

```text
build/universe26/uf2/gpio_events.uf2
build/universe26/uf2/button_mapper.uf2
```

The mapper reads each RP2350 pad's raw `INFROMPAD` status. RP2350 pads start
digitally isolated, so the mapper enables their input paths, forces their pad
output drivers off, and removes isolation. It does not select a GPIO function
or enable an output driver.

Both modes enable weak pull-ups so the front buttons can be detected. An
earlier no-pull live build produced no events, while the guided pull-up build
successfully observed every front control. GPIOs used by the confirmed display
path and GPIOs corresponding to the 2025 wireless interface are conservatively
excluded.

## Mode A: real-time confirmed front buttons

Install `gpio_events.uf2`.

The current real-time image monitors only the confirmed front buttons:

| Control | GPIO |
| --- | ---: |
| Left | 7 |
| Right | 9 |
| Enter | 10 |
| Up | 11 |
| Down | 6 |

It enables weak pull-ups and polls the RP2350 hardware edge latches. The screen
starts with `REALTIME BUTTONS / READY`. Each transition updates the display
with:

- the control name;
- `PRESSED`, `RELEASED`, or `TAP`;
- its GPIO; and
- a cumulative event number.

Edges remain latched while the display is being redrawn. If both press and
release edges accumulate before they are processed, the screen reports `TAP`
and increments the event number by two. A 10 ms debounce delay is applied after
capturing and clearing each set of latched edges; any later transition remains
latched for the next update.

The earlier broad-scanning live versions are obsolete. The no-pull version
never saw the switches. The five-second pull-up version detected Left on GPIO7
but also accumulated unrelated candidates and delayed the result by several
seconds. Those observations motivated the narrowed real-time implementation.

## Mode B: guided mapping

Install `button_mapper.uf2`.

The mapper prompts in this order:

1. Left
2. Right
3. Enter
4. Up
5. Down

For each prompt:

1. Wait until `NEXT 5 SECONDS / PRESS <name>` is fully visible.
2. Press and release that control exactly once.
3. Do not touch any other control.
4. Record every GPIO row on the following `OBSERVED` page.

The result page remains visible for three seconds. After all five controls, a
summary remains on screen. The summary uses the highest-ranked button-like
candidate, preferring two edges, but it is evidence rather than an automatic
confirmation.

## Reporting results

Report both:

- the final summary; and
- all rows from each guided `OBSERVED` page, including edge counts.

A mapping becomes confirmed only when repeated captures associate one GPIO
with one control and idle captures do not produce the same candidate.

If `NO EVENTS` appears for a control, the control may not be a direct digital
GPIO. If several controls consistently produce the same GPIO, that GPIO may be
a controller interrupt and the controls may require a bus-aware probe.

## Rear controls in the continuous nametag

The rear **BOOT/HOME** input is active-low GPIO22, also used by the diagnostic
button code. The [continuous nametag](tools/nametag.md#boothome-light-sleep-2026-10-12)
uses a debounced press to enter light sleep and another press to wake.
RESET alone restarts the application. On the primary badge, the owner verified
at least 35 seconds of sleep without watchdog reboot, HOME wake, held-button
handling and RESET restart. This does not qualify sleep current or clock
shutdown, nor does it change the front-button mapper's scope.

The owner's BOOT/HOME + RESET combination enters BOOTSEL; firmware does not
repurpose RESET or flash/QSPI. The combination was not retested on the final
sleep image because the owner preferred to leave it running.

## Current exclusions

The mapper excludes the display/power bank, including a protected analog pin:

```text
26, 27, 28, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41
```

GPIO40 is excluded with the high display-bank mask even though it is not
driven by the display implementation. The 2026 schematic identifies it as
`VBAT_SENSE` (ADC0); it must remain protected from output configuration.

It also excludes GPIO23, GPIO24, GPIO25, and GPIO29. Initially protected based
on the 2025 wireless interface, these are now confirmed by the 2026 schematic
as `WL_ON`, `WL_D`, `WL_CS`, and `WL_CLK`, respectively.
