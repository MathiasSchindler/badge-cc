# Waterfall: RX hardware investigation

## First standalone TFT application

The separate activity application displays **valid received WLAN frames per
second during each channel visit**, not RF energy, RSSI, dBm or a fine-resolution spectrum.
It runs independently of a laptop or USB collector.

```sh
make -C src/tools/waterfall -f app.mk all test
```

Badge-only image: `build/waterfall/activity/waterfall.uf2`.
Do not install this display application on a Pico.

- Eleven columns scan channels 1..11 (2412..2462 MHz center frequencies).
  This conservative first application does not use the out-of-band probe.
- Each complete sweep inserts a new row at the top of a 152-row history.
  Older sweeps move down. The top row also updates after each channel visit:
  gray cells have not yet been sampled in this sweep, and a white cursor marks
  the last completed channel. Rows are sweeps, not exact seconds.
- **Enter toggles 150-ms normal and 50-ms fast dwell**, effective at the next
  sweep boundary. Normal is the startup default. The button is debounced for
  20 ms; holding it does not repeatedly toggle. Both modes retain the 15-ms
  channel-settle/drain interval. Shorter visits miss more intermittent traffic.
- Colors use fixed, approximately logarithmic frames-per-second buckets:
  dark = 0, blue = 1, then 2..3, 4..7, 8..15, 16..31, 32..63,
  red = 64 or more. Rates use actual dwell duration, truncated to milliseconds,
  then rounded to the nearest whole frame/s and saturated at 65535.
  This makes colors comparable across modes, but shorter samples remain noisier.
- Every counted frame must have matching receive-channel metadata and an
  independently computed matching FCS. All frame types count; no payload
  or network-name interpretation is required. Duplicates are not deduplicated.
- No interpolation pretends these columns are narrow frequency bins.
  Overlapping channels, missed frames and short events between channel visits
  limit interpretation. A dark cell is not proof of RF silence.
- Hold rear **Home for 1.5 seconds** to power off the radio and enter BOOTSEL.
- RX-only startup passes TX permission zero and installs no station role,
  response policy or TX command. Fatal radio errors stop reception and appear
  visibly; they are not silently retried or drawn as empty activity.

The application reuses the ordinary source controller through a local
composition wrapper. Only channel-switch result publication is added at SRAM
`0x7f000..0x7f00f`: magic, requested channel, actual return value and sequence.
The host checks this record after every completed channel command. This avoids
assuming command completion means a successful tune and avoids racing the
controller on the shared indirect SHM selector. The probe and normal `clean-rx`
source files are unchanged.

The native ASan/UBSan test checks frame FCS/channel rejection, saturated counts,
rate normalization, button bounce/hold/timer wrap, progressive cells/cursor,
history ordering/scrolling, color buckets and rendering at the actual 320x240
dimensions. `build/waterfall/activity/layout.ppm` is an explicitly synthetic
layout test, not a screenshot of measured hardware activity.

Optional desktop verification:

```sh
build/waterfall/activity/activity_collect BADGE_SERIAL
build/waterfall/activity/activity_collect BADGE_SERIAL --fast
build/waterfall/activity/activity_collect BADGE_SERIAL --slow
build/waterfall/activity/activity_collect BADGE_SERIAL --bootsel
```

The collector checks exact app build, nonce, report/trace CRC, error status and
ongoing sweep/frame/full-render/channel-update progress for twenty samples.
Its nonce-bound trace reports channel-update count, last completed sweep
duration and current dwell. Mode requests apply at the next sweep boundary,
so the first report can still describe the previous mode. It does not cause
the badge to scan: scanning continues before, during and after collection.
Only the explicit `--bootsel` command stops the app.

### First application hardware control

Badge A `9BBF88DD17D66E7D` received a new independently verified 16-MiB backup
before installation. Build `df3c97c8` passed its first twenty-report live
control, reaching eight completed sweeps, 131 CRC-validated frames,
nine completed TFT updates, zero rejected frames and zero radio errors.
Raw desktop logs are in `build/waterfall/activity-trial/`.
This proves bounded integrated receive/sweep/display-write progress, not
calibrated RF power, a loss-free link or an independent camera observation
of the physical screen. Per operator request, a successful final application
is left installed and running on Badge A; Badge B and Pico are not modified.
The complete installed flash was then independently read back: its first
60,724 bytes exactly match the app binary, and everything beyond the fifteen
application flash sectors matches the original backup. After a fresh reboot
and thirty seconds without desktop USB polling, a second twenty-report control
reached 24 sweeps, 291 valid frames and 25 TFT updates, again without rejected
frames or radio errors. The application remains running after that control.

### Progressive/fast application hardware control

Build `22d495cf` was installed on Badge A after two matching full-flash backups
of the previous application. Twenty-report controls passed for normal, fast,
and return-to-normal operation, with real received frames, advancing progressive
updates and no rejected frames or radio errors. Measured completed sweeps were
approximately 1.92 seconds normal and 0.82 seconds fast (about 2.3 times faster).
The progressive row updates once per channel instead of waiting for a sweep.
The physical Enter switch still needs operator confirmation; native tests cover
its debounce logic and USB tests exercise both requested modes on hardware.

Full installed-flash readback matched all 61,496 application bytes, with the
remaining flash beyond the sixteen application sectors unchanged from backup.
After reboot and thirty seconds without host polling, another twenty-report
control reached 23 sweeps, 300 valid frames and 262 channel updates without
rejections or radio errors. Badge A remains running in startup-default normal
mode. Evidence is retained in `build/waterfall/progressive-trial/`; Badge B and
Pico were not modified.

## Separate RX tuning investigation

This directory also owns an isolated C, no-SDK/no-libc tuning investigation.
It is **not a spectrum analyzer or energy waterfall**.
The first probe measures programming readback, the existing VCO calibration
status indication and DC-calibration completion. None proves reception,
actual oscillator frequency, sensitivity or instantaneous measurement bandwidth.

## Current experiment

The source-built Cortex-M3 controller reuses `clean-rx` PMU/radio initialization,
but never loads PSM microcode, enables the MAC, starts DMA or configures TX.
It retains the existing explicit PHY/radio transmit-inhibit settings.
The RP2350 reuses the cold loader and USB reporting infrastructure.
Ordinary `clean-rx` sources, profiles and command admission are unchanged.

All fourteen existing synthesizer entries have frequency register `0x55`
equal to MHz, and registers `0x4e:0x4f` equal to
`floor(MHz * 60 * 65536 / 187)`. Native tests check every original entry.
This numerical agreement is a programming hypothesis outside the table,
not identification of the chip's full synthesizer architecture.

A fixed, bounded run contains:

1. All fourteen original channel frequencies, using the same arithmetic.
2. Sixteen requested frequencies, 2300 through 2600 MHz at 20 MHz spacing.
3. A final return to 2412 MHz.

Before each candidate, fully retune/calibrate the nearest known channel.
Keep that channel's analog tuning values, rewrite only the three
frequency/divider registers and repeat the existing VCO-calibration edges.
Read back the registers, VCO status, DC calibration and transmit-inhibit
settings. Stop at the first failure, retain it and power down the radio.
The unchanged analog and baseband timing settings may themselves limit results.
The last sample returns to 2412 MHz only if all preceding samples passed.

**Important:** this is an explicit research-only RX tuning path. The normal
channel table is not extended. A set lock bit may be stale or misleading;
it does not independently verify the requested RF frequency. Passing every
sample would not establish the tuning endpoints or exclude gaps in between.

## Build and offline validation

From the repository root:

```sh
make -C src/tools/waterfall all test
# Separate, headless Pico 2 W image. Never cross-flash board images.
make -C src/tools/waterfall BUILD=../../../build/waterfall/pico PICO=1 all test
```

Both target programs compile with `-ffreestanding -nostdinc -fno-builtin`
and link with `-nostdlib`; the links require zero unresolved symbols.
Host tests use the existing compiler's sanitizers. Only the desktop USB
collector uses the repository's existing libusb and host C library; neither
is a firmware dependency.

Outputs are `build/waterfall/probe/waterfall.uf2` and `collect`, or the
separate Pico build directory. The badge screen shows probe status, not
invented signal intensity. Each RP2350 schedules automatic BOOTSEL recovery
after eight seconds, and the collector requests BOOTSEL after collection.

## Authorized hardware procedure

Follow the repository [hardware protocol](../../../clean-rx/dev-doc/guides/HARDWARE_TRIALS.md).
Use exact USB serials. Back up the full flash, take a second independent
full readback and compare before flashing. Do not run picotool processes in
parallel: on this macOS host they contend for the shared USB discovery path.

For one authorized device at a time:

1. Load and verify the matching UF2, then reboot out of BOOTSEL.
2. Immediately run `collect SERIAL NEW_EVIDENCE.bin > NEW_EVIDENCE.json`.
3. Stop the campaign on a failed control, missing report or guard violation;
   do not turn retries into a passing campaign.
4. Read back the complete flash and identify changed 4096-byte sectors.
   Restore those sectors from the full original backup, independently read
   back the complete flash again and compare the entire image. Leave it in
   BOOTSEL. A completed write alone is not restoration verification.

The collector requires its exact build ID before submitting a request.
Evidence is created exclusively, never overwritten. It retains the request,
response and all raw CRC/nonce-bound USB trace pages, including failed records.
The offline decoder rechecks those pages:

```sh
build/waterfall/probe/collect --inspect NEW_EVIDENCE.bin
```

`tuning_control_passed` means only the complete programming/calibration control
passed. Both `reception_proven` and `bandwidth_proven` remain false.
Missing reports produce failed, incomplete archives, not successful defaults.

## What remains to answer

- Identify a packet-independent energy estimator or accessible I/Q samples.
  Do not substitute packet RSSI, packet counts or generic MAC busy state.
- Establish actual tuning using an independent known-frequency source.
  The two badges and Pico share this radio family and are not an independent
  out-of-band frequency reference.
- Measure the analog/digital passband with a swept or stepped reference.
  A 20-MHz WLAN mode neither proves an absolute 20-MHz hardware ceiling nor
  establishes an accessible wider-band sample path.
- Keep scan span, instantaneous bandwidth and frequency resolution separate.
  Retuning a 20-MHz path across a wider span does not observe that span
  simultaneously. Detecting a strong off-channel signal does not prove
  faithful wideband sampling.
- No out-of-band transmissions are authorized. Controlled reference TX, if
  used in a later experiment, is restricted to locally permitted WLAN channels.

## Hardware record: 2026-10-25

Full backups and second independent matching readbacks were taken for:

| Role | USB serial | Bytes | Original SHA-256 |
| --- | --- | ---: | --- |
| Badge A | `9BBF88DD17D66E7D` | 16,777,216 | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |
| Badge B | `08DA26F86E57F5EA` | 16,777,216 | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Pico 2 W | `6572BE3E4636AF51` | 4,194,304 | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |

Only Badge A was flashed or executed. Each failed campaign stopped, and
subsequent attempts had separate operator authorization. Evidence remains
in `build/waterfall/20261025-probe/`, including unsuccessful controls:

| Attempt | Observation | Interpretation |
| --- | --- | --- |
| `badge-a/trial01` | Collector report timeout at 100 ms | Incomplete USB evidence; no frequency result. The existing cold bus probe has an unserviced 250-ms power-on delay. Collector corrected to the repository's 1000-ms transfer timeout. |
| `badge-a/trial02` | Loader bus error, test-pattern mismatch, zero frequency records | The new host had omitted `wifi_board_init` and PIO selection. Both are now explicit. No RF or PLL conclusion. |
| `badge-a/trial03` | All 31 records independently replayed successfully | Programming/readback/calibration control only; no signal or bandwidth measurement. |

During attempt 2 restoration, full-image writes lost the USB connection at
9% and 63%. The operator reconnected the badges in BOOTSEL. An independent
full readback then matched Badge A's original backup exactly. These failed
write attempts remain in the logs; neither was treated as verified restoration.

Attempt 3 used badge build `e97a844a`, with a 10,564-byte source-built controller.
The complete operation took 2,212,514 host timer microseconds. All fourteen
known frequencies, sixteen requested 2300..2600-MHz points and the final
2412-MHz return passed register readback and the selected calibration checks.
Every record retained MAC enable/PSM bits clear and the selected TX-inhibit
readbacks. No reference transmitter was used, and there was no intentional TX.

**The VCO status was `0x0005` both before and after every candidate**, including
known-channel controls. DC status reads were `0x00ff` and `0x0fff`.
These completion indications have no independent negative/freshness calibration
in this experiment. In particular, do not report "2300..2600 MHz reception",
a measured continuous tuning range, an independently confirmed PLL lock,
or wider-than-20-MHz capture from this result.

The final restore wrote only the six changed sectors (24,576 bytes).
A fresh full 16-MiB readback matched the original SHA-256 above exactly.
Badge B and Pico were never flashed; final full readbacks also matched both
original backups exactly. All three devices were left in BOOTSEL.
The successful raw archive was additionally decoded with a separate Node
builtins implementation of page CRC, nonce/layout checks and BigInt divider
arithmetic; all 31 records agreed. Both earlier failed archives remain rejected.
The corrected Pico image is build/native-tested, not hardware-qualified.
The reuse of loader handshake states 8/9 is protocol compatibility only;
there is no running PSM or packet-receive path in this probe.
