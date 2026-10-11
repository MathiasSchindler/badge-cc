# Lower-Control GPIO Mapper

> Historical diagnostic: the schematic now identifies the lower controls as
> CAP1208 touch channels on the internal GPIO18/19 I2C bus, with a shared
> alert on GPIO13. This GPIO-only mapper does not configure the CAP1208 or
> read its individual channels. See [the current touch plan](plan.md#phase-4-touch-control-discovery).
> All eight channels were subsequently reported working and physically
> mapped; use [the standalone diagnostics](diagnostics.md) for touch testing.

## Artifact

Build with `make`, then install:

```text
build/universe26/uf2/lower_mapper.uf2
```

## Operation

The historical mapper prompts using these provisional game-control names:

1. A
2. B
3. Select
4. Start
5. Up
6. Down
7. Left
8. Right

The A/B and Select/Start labels do not establish assignments for the round
and slash-shaped controls. Use the confirmed physical-position mapping in
[the specification](spec.md#physically-confirmed-touch-channel-mapping).

For each `NEXT 5 SECONDS` prompt, touch and release only the named control once.
Record every GPIO row on the following `OBSERVED` page, including edge counts.
The final eight-row summary remains on screen.

## Electrical behavior

The mapper:

- enables digital input and weak pull-up on candidates;
- forces the pad output driver off;
- does not select a new GPIO function;
- observes raw pad levels; and
- never drives an unknown candidate.

It excludes:

- confirmed front buttons GPIO6, GPIO7, GPIO9, GPIO10, and GPIO11;
- schematic-confirmed wireless pins GPIO23, GPIO24, GPIO25, and GPIO29;
- display/control pins GPIO26 through GPIO41, including protected GPIO40; and
- GPIO42 through GPIO47; the schematic identifies GPIO42 as `1V1` sense and
  GPIO43 as light sense, while GPIO44-47 have no assigned nets on sheet 2.

The remaining candidates are GPIO0 through GPIO5, GPIO8, and GPIO12 through
GPIO22.

This original candidate set includes now-identified peripheral signals,
including PSRAM CS on GPIO8 and the internal I2C bus. It is not an
unassigned-pin list. Pull-ups and forced output disabling can affect attached
peripherals even without actively driving a pin.

## Interpretation

The original physical test found no events from ordinary touches. The
schematic now establishes the serial interface: CAP1208 channels CS1-8 connect
to CAP1-8, and `CAP_ALERT` is shared on GPIO13. An alert event alone cannot
identify which control was touched. No event does not establish that a pad
is absent; controller configuration and channel reads require I2C access.

GPIO observation cannot identify accelerometer sample data. At most, movement
could reveal the confirmed `IMU_INT` line on GPIO20. Manufacturer documentation
now identifies the accelerometer as an LSM6DS3TR-C on the separate internal
GPIO18/19 I2C bus.

## Report

Please report all rows for every prompt and the final summary. Do not report
only the top-ranked row if multiple GPIOs appear.
