# Animated nametag implementation plan

Status: **M0 and M1 complete; M2 diagnostic implemented and warm-launch
measurements verified on both badges**, 2026-10-12. The owner selected
**B, Cascadia Mono, natural coverage, 8-bit atlas** after host preview.
M2 cold-boot observation is deferred because full power removal is not
established. M3 has a continuous single-core prototype warm-launched on the
primary badge; its qualification gates remain open, and M4-M5 remain planned.
Exact-pixel renderer acceleration and an
explicitly prepared scene cache are implemented and measured on both badges
(about 6.7x faster complete rendering than the original). Differential display
presentation is also implemented and measured: transition-midpoint
render-plus-present time falls from about 65-66 ms to 43-44 ms after presenter
optimization. The 80-second animation completes about 27.3 frames/s.
The bootable M2 laboratory image is not
the production nametag, and it does not meet the proposed M3 frame-rate target.

The application entry point is [badge-nametag.c](../../src/tools/badge-nametag.c). Firmware
must remain freestanding Cortex-M33 C, without libc, the Pico SDK, an OS or
external target libraries. Start with a build-time profile, while preserving
the ability to replace profile data later over USB, Wi-Fi or Bluetooth.
Transport implementation and persistent configuration are not v1 requirements.

## Experience and example profile

This is a readable name badge first, an animation second. Keep the name visible
and stationary; animate supporting information and a restrained background.
The initial mockup is a direction to explore, not a frozen specification:
cream name text, gold supporting text, near-black background and square-like
halftone marks concentrated toward the lower right.

| Field | Initial value |
| --- | --- |
| Name | Mathias Schindler |
| Role | Senior Manager Developer Policy |
| Employer | GitHub |
| GitHub username | @mathiasschindler |
| Location | Potsdam, Germany |
| Interests | CRA; copyright; AI; open data; FOIA |

Keep these as separate semantic fields, not one preformatted image or string.
Layout determines line breaks and interest grouping from measured text widths.
Long names and roles must have a defined fit policy; do not silently clip or
shrink text below an agreed readable minimum.

Explore three alternatives at native 320 x 240 resolution:

1. **Editorial halftone:** large two-line name, stationary role/employer,
   diagonal breathing halftone, footer dissolving between username, location
   and grouped interests. Closest to the mockup.
2. **Identity and topic cards:** stable name and username, with a lower panel
   cycling role, location and interests using a short fade or vertical motion.
3. **Quiet identity:** stable name, role and username, animation mostly at the
   right edge, and individual interests appearing near the bottom.

Evaluate an eight-second background period separately from the information
carousel. Readable dwell times may require a longer carousel. The proposed
wave travels from lower right to upper left; its direction and amplitude must
be verified in the preview rather than inferred from a formula.

## Platform baseline and open assumptions

Use the [hardware specification](../spec.md) for wiring and the
[runtime qualification guide](../rp2350-runtime.md) for clock, memory and
multicore boundaries.

| Surface | Existing evidence | Nametag implication / remaining check |
| --- | --- | --- |
| Display | 320 x 240, RGB565, working 8-bit parallel writes, ST7789-compatible commands | Reuse initialization and orientation; measure actual transfer cost |
| Pixel traversal | X outside, Y inside | Assets/composition must respect the column-major canvas and panel traversal |
| Graphics | Shared 153,600-byte canvas and uppercase 5x7 diagnostic font | Reuse useful primitives; add separate proportional coverage-font support without changing diagnostic typography |
| Main SRAM | Linker exposes 512 KiB; two additional 4 KiB scratch banks | Budget static data, buffers, workspaces and stacks explicitly |
| Flash | Both attached devices report 16 MiB; ordinary linker permits 4 MiB | Keep v1 within the existing link limit; do not assume spare sectors for configuration |
| PSRAM | Schematic says 8 MiB; freestanding initialization unqualified | Do not rely on PSRAM |
| Timing | Minimal startup inherits clocks; raw ticks are not automatically microseconds | Establish a measured timebase before claiming seconds or fps |
| Display transfer | Current shared driver is synchronous CPU-driven GPIO | A second core alone does not accelerate the bus |
| Panel TE | GPIO21 wiring and enable command exist; timing/use unqualified | Probe before making tear-free claims |
| Core 1 | BA05 launch and bounded synchronization passed on one identified A4 badge | Reuse prior art, but qualify the application's ownership, stacks and recovery separately |
| Power | No nametag battery-life or brightness measurement | Do not infer battery life from frame rate or CPU count |

Relevant existing code:
[display](../../src/platform/badger/display.c),
[graphics](../../src/platform/badger/graphics.c),
[startup](../../src/arch/rp2350/start.c) and
[linker](../../src/arch/rp2350/memmap_flash.ld).
The [maze](../tilt-maze.md) provides prior art for background restoration and
partial display updates, but its nominal cadence is not a nametag benchmark.

### Read-only device inventory, 2026-10-12

The owner authorized testing the two badges attached in BOOTSEL during this
planning session. `build/picotool-usb/picotool info -a` identified:

| Chip identity | Reported revision/package | Flash | State |
| --- | --- | --- | --- |
| `9BBF88DD17D66E7D` | A4 / QFN80 | 16 MiB | BOOTSEL |
| `08DA26F86E57F5EA` | A4 / QFN80 | 16 MiB | BOOTSEL |

Both reported Arm Secure installed images, secure boot disabled and ROM
revision `0xa8bfe860`. This was discovery only: neither was flashed or rebooted.
It does not qualify display cadence, timers, TE, PSRAM or the nametag.
USB bus addresses and volume names are transient, not device identities.
This record is not blanket authorization for unrelated or future experiments.

## Rendering and data design

### Profile and update boundary

Use bounded UTF-8 profile fields and a bounded interest list. Build-time
defaults feed the same validation/layout path as future transport updates.
Define field limits, supported characters and fit behavior before implementing
the format. Report invalid encoding, unsupported characters and unrenderable
content explicitly.

Prepare and validate a complete replacement profile before applying it between
frames. Do not allow a transport to mutate strings while rendering uses them.
Keep transport, persistence, layout and animation independent; initially only
the defaults provider is implemented. A future versioned wire/storage format
can map into this profile without becoming the renderer's internal format.

### M0 agreed profile contract

The owner accepted these limits and behavior on 2026-10-12. Byte limits exclude
the terminating NUL; each fixed-capacity string needs one additional byte.

| Field | Maximum UTF-8 bytes |
| --- | ---: |
| Name | 128 |
| Role | 192 |
| Employer | 96 |
| GitHub username, including `@` | 40 |
| Location | 128 |
| Each interest | 64 |
| Interest count | 8 |

Initial supported repertoire:

- Printable ASCII `U+0020-U+007E`.
- Latin-1 `U+00A0-U+00FF`.
- `U+0152/U+0153` (OE ligature), `U+0160/U+0161` (S with caron),
  `U+017D/U+017E` (Z with caron), and `U+0178` (Y with diaeresis).
- Curly single/double quotation marks `U+2018/U+2019/U+201C/U+201D`,
  en/em dash `U+2013/U+2014`, ellipsis `U+2026`, and euro `U+20AC`.

Use strict UTF-8: reject overlong encodings, surrogates, out-of-range scalars,
embedded NUL, control characters and unsupported codepoints. This repertoire
uses precomposed accents; arbitrary combining sequences, normalization,
complex-script shaping and bidi are not promised. Treat nonbreaking space as
spacing and soft hyphen as a discretionary layout character, not a missing-glyph
fallback. M1 must verify actual font coverage and implement those layout rules.

Wrap at words, then try smaller approved native font sizes. The agreed minima
are **24 px for the name and 12 px for supporting text**, expressed as the
renderer pixel-size parameter, not a claim about visible glyph height.
Never scroll the name, truncate text or replace overflow with ellipsis.
If the complete profile still cannot fit the selected layout/carousel, reject
the replacement explicitly and retain the previous active profile. All three
preview alternatives remain available. The owner selected B during M1.

These were contracts at M0; M1 now implements validation/rendering and measures
the example's fit, as recorded below.

### Reuse the project-owned TTF renderer

The owner's renderer was located in `~/newos/src/shared/fontrender`, not the
initially mentioned `~/noewos` path. Its project license is CC0. It provides
memory-backed TTF loading, platform hooks, glyph metrics and 8 x 8 coverage
sampling. Import the required source and provenance into this repository;
builds must not depend on another checkout in the user's home directory.
Check and preserve the selected font file's separate license.

Initially run it as a host-side font compiler and emit per-glyph coverage
bitmaps, bearings, advances and line metrics at a small set of native sizes.
Generate an agreed character repertoire, not only characters in the example
profile. The badge then lays out arbitrary supported strings at runtime.
Changing names within that repertoire must not require regenerating glyphs.

Start with 8-bit grayscale coverage for correctness and visual comparison.
Evaluate packed 4-bit coverage only after measuring appearance and asset size.
Blend coverage against the current background, including during crossfades;
precompositing every glyph against black would produce halos over moving dots.

The inspected renderer skips glyph hinting instructions. Its full backend has
large fixed caches, dynamic allocation and some double-precision raster bounds
calculations using project math helpers. Host reuse is not proof of a cheap
drop-in MCU port. On-device TTF rasterization is a later option, not necessary
for editable profiles backed by an embedded glyph atlas.

For small-screen typography compare native pixel sizes, weights, integer
baselines and coverage/contrast curves at 1:1 scale. Supersampling is not
hinting. Avoid LCD subpixel rendering without panel-order qualification.
Non-Latin shaping is a separate requirement; Unicode glyph lookup alone does
not establish script shaping, kerning or bidirectional layout.

### M0 font selection and provenance

The owner selected [Microsoft Cascadia Code/Mono](https://github.com/microsoft/cascadia-code).
Use release **v2407.24**, commit
`56bcca3f2c1e4cb19458954f0e2bb4635960df91`, as the initial pinned source.
The [release](https://github.com/microsoft/cascadia-code/releases/tag/v2407.24)
contains `CascadiaCode-2407.24.zip`.

The [license at that revision](https://github.com/microsoft/cascadia-code/blob/v2407.24/LICENSE)
explicitly states **SIL Open Font License 1.1**, Microsoft copyright and
Reserved Font Name **Cascadia Code**. GitHub's API reports `NOASSERTION`, so the
license identification here comes from reading the license, not that label.
Preserve copyright/OFL notices with distributed font assets; font software
does not become CC0 because the rasterizer is CC0. Respect the reserved-name
restriction for modified font software, including renamed derived font assets
where applicable.

Start by comparing Cascadia Mono Regular/Bold for supporting text/name and
Cascadia Code as a preview alternative. Do not rely on Code's programming
ligatures: the current renderer does not supply OpenType text shaping.
M1 must select concrete archive members, check `glyf` compatibility/static
instances, verify the full repertoire, and record archive/font SHA-256 hashes.
No font binary was imported or qualified by M0. M1 subsequently imported the
four static files and verified their coverage and provenance, as recorded below.

The inspected CC0 renderer checkout was clean for the relevant sources at
commit `894e674fce0f2192f8017e414917792fa6479743`. Recheck source hashes when
importing; the local path is provenance, not a future build dependency.

### Reusable-code inventory established in M0

| Need | Existing source / test | Reuse boundary |
| --- | --- | --- |
| TTF parsing, outlines, rasterization | `~/newos/src/shared/fontrender/{fr_ttf.c,font_backend_truetype.c,fr_raster.c}` and public headers | Import portable CC0 core in M1; do not import the whole OS |
| Platform adapter | `~/newos/src/shared/fontrender_runtime.c` | Example of hook installation; allocation/files/logging use that OS's runtime and need a host adapter |
| Font smoke fixture | `~/newos/tests/fixtures/fontrender/fonttest.c`, `build/fonttest` rule in that checkout's Makefile | Reports glyph metrics/coverage; not a comprehensive small-display regression suite |
| Raster math | `~/newos/src/shared/math.{c,h}` | Raster bounds use `math_floor`/`math_ceil`; isolate required helpers instead of bringing all runtime code |
| Display/canvas | [display](../../src/platform/badger/display.c), [graphics](../../src/platform/badger/graphics.c) | Preserve existing diagnostic font and column-major traversal |
| USB clock/SOF reference | [USB hardware](../../src/usb/live_usb_hw.c), [live report app](../../src/tools/usb_live_report.c) | USB PLL setup and SOF sampling are prior art, not a calibrated nametag timebase |
| Safe system clock switch | [BA05 diagnostic](../../badge-compiler/tests/fixtures/synchronization_badge.c) | `enable_xosc`/`configure_clock` preserve live AUXSRC and wait for mux selection |
| Core launch/park/ordering | [BA05 diagnostic](../../badge-compiler/tests/fixtures/synchronization_badge.c), [assembly probe](../../badge-compiler/tests/fixtures/synchronization_probe.S), [qualification harness](../../badge-compiler/tools/synchronization.mjs) | Reuse FIFO launch, separate vectors/stack, bounded waits and stop/park; not generic production C atomics |
| Report integrity | [codec](../../src/shared/usb_report.c), [endpoint state machine](../../src/usb/live_usb_control.c), [collector](../../src/host/tools/usb_live_host.c) | Already validates build identity, fresh nonce, CRC, known payload and malformed-request recovery |
| BOOTSEL recovery | [ROM reboot](../../src/arch/rp2350/bootrom_reboot.c), [live USB guide](../usb-live.md) | Scheduled recovery plus host-triggered return; no claim of universal fault recovery |
| Backup/restoration precedent | [system-instruction harness](../../badge-compiler/tools/system-instructions.mjs) | Full backup hash, serial-selected restore and independent readback; its diagnostic flash-sector protocol is not needed here |

### M0 reproducible hardware control

[nametag_control.mjs](../../src/host/tools/nametag_control.mjs) wraps the existing
known-report firmware/collector; it adds no new target runtime or radio mode.
The separate runner is needed because the older live-USB wrapper leaves the
test image installed and does not back up both badges.

Run the offline checks first:

```sh
make test-nametag-control test-live-usb
```

With both identified badges in BOOTSEL and explicit owner permission:

```sh
mkdir -p build/nametag-m0
node src/host/tools/nametag_control.mjs \
  9BBF88DD17D66E7D 08DA26F86E57F5EA \
  build/nametag-m0/NEW-UNUSED-RUN-NAME
```

The first serial is the experiment badge; the second is read-only control.
Never substitute a USB volume or remembered bus address. Run only one hardware
campaign at a time. The runner requires a new directory beneath ignored
`build/`, creates it with private permissions, and builds into its own directory.
`PICOTOOL` may point to an explicit alternative executable.

Before flashing, both devices must identify as A4/QFN80 RP2350, Arm, secure
boot disabled, 16 MiB flash and BOOTSEL. The runner saves the entire
`0x10000000-0x11000000` range for each badge, checks length, hashes each backup,
and verifies it against its selected device. It records source, runner,
Node/picotool, image, collector, ELF and UF2-converter hashes plus command logs
and the generated protocol build ID.

On the experiment badge only, it performs two bounded controls:

1. Flash/verify/execute the known-report image, collect three distinct
   nonce-bound reports, and observe the host-requested BOOTSEL return.
2. Flash the same image again without a collector, observe departure from
   BOOTSEL, and require automatic return. Record host elapsed time without
   interpreting the ROM's requested delay as calibrated time.

Every subprocess has an independent deadline. Recovery polls are bounded by
45 host seconds and 90 attempts, with a maximum five-second subprocess per
poll. No clock-switch or core-1 experiment is performed. The existing USB image
does initialize its USB PLL, so this control is not a clock-free experiment.

Once any load is attempted, failure paths also attempt full primary restoration.
Restoration verifies the original backup hash, re-identifies the recovered
device, loads the complete original flash without rebooting the application,
then saves and hashes an independent full readback. The control badge is
read back and checked against its own original hash; it is never flashed.
Success requires both badges in BOOTSEL, primary restoration and unchanged
control, all three reports and no-collector recovery. A failed test remains
failed even if restoration succeeds.

If automatic recovery fails, stop and use the selected badge's physical Boot
procedure. Resume restoration with the preserved run directory:

```sh
node src/host/tools/nametag_control.mjs --restore build/nametag-m0/RUN-NAME
```

This verifies identity and backup integrity again; it does not turn the failed
experiment into a pass. Preserve `results.json`, logs and private backups if
anything fails. Do not publish full flash or treat a missing report as success.

### Animation and frame ownership

Generate halftone marks procedurally with fixed-point phase/geometry and a
small wave lookup table if useful. Fractional edge coverage should avoid
one-pixel size popping. Keep pattern contrast low behind text.

Use elapsed time and absolute frame deadlines. Skip stale presentation frames
after a stall; do not stretch the loop by counting rendered frames. Bound
catch-up work and handle tick wraparound. Compose a complete background and
text result for every changed region so fades leave no trails.

Start single-core with one RGB565 framebuffer and measured full-frame
transfers. Dirty-region updates are an optimization to evaluate against the
actual animated area, not an assumption that this background is mostly static.

One frame is 153,600 bytes. Full-frame pixel payload is 4.608 MB/s at 30 fps
and 9.216 MB/s at 60 fps, excluding commands and software overhead. Two buffers
consume 307,200 bytes before fonts, profile, workspaces or stacks.
Double buffering prevents renderer/transmitter buffer conflicts; it does not
automatically prevent panel tearing.

If measurements justify multicore, let one core own display I/O and the other
render an exclusively owned buffer. Specify publication ordering, queue depth,
backpressure, timeouts and shutdown before launch. `volatile` is not an
ownership protocol. Production C atomics are not qualified by BA05 alone.
PIO/DMA is a separate possible transfer optimization, with its own pin,
strobe-timing and buffer-lifetime qualification.

## Implementation milestones

Milestones are gated by evidence, not just code completion. M1 and M2 can
proceed independently after M0; both feed M3.

### M0 - Freeze contracts and establish safe test infrastructure

- Confirm profile limits, initial character repertoire, font license, fit
  policy and the three preview variants. Preserve full semantic field values.
- Locate reusable font tests, math/platform helpers, clock setup, USB report,
  recovery and BA05 core-launch code before adding replacements.
- Re-identify each badge before a run. Make a fresh complete flash backup for
  each device, hash it, and establish serial-selected restoration/readback.
  Backups may contain private data; keep them local and ignored.
- Use fresh evidence directories, source/tool/image hashes and nonce-bound
  reports. Stage experiments on one badge first; retain the other as control.
- Verify a bounded reporting/recovery control before clock/core experiments.
  Stop on timeout or fault; missing reports never count as success.

**Exit:** agreed contracts and a reproducible, device-bound test/recovery path.

**Completed 2026-10-12:** profile/repertoire/fit contracts accepted, Cascadia
license and release pinned, reusable-code inventory recorded, and the
serial-bound control runner implemented with 14 passing offline tests.
The existing live-USB host tests also passed under address/undefined-behavior
sanitizers.

Physical campaign `build/nametag-m0/control-20261012-m0-01` ran from
07:25:54 to 07:30:52 UTC. Its local `results.json` records exact source/tool/image
hashes, command logs, backups and nonce-bound reports. Do not overwrite this
directory or distribute its flash contents.

| Check | Observed result |
| --- | --- |
| Experiment badge | A4/QFN80 `9BBF88DD17D66E7D`; fresh full 16 MiB backup verified before flashing |
| Control badge | A4/QFN80 `08DA26F86E57F5EA`; fresh full 16 MiB backup verified; never flashed |
| Known-report control | 3/3 reports validated, distinct nonces, build ID `5afee618`, expected value `feedc0de` |
| Protocol rejection/recovery | Collector verified malformed-request and unavailable-report stalls, then a valid request |
| Host-requested return | BOOTSEL observed in approximately 0.84 host seconds after collection |
| No-collector return | Departure and automatic BOOTSEL return observed in approximately 18.13 host seconds after load |
| Primary restoration | Full 16 MiB independent readback matched the original SHA-256 |
| Control integrity | Full 16 MiB final readback matched its original SHA-256 |
| Final device state | Both independently rediscovered in BOOTSEL; original applications not executed after restoration |

The tested UF2 SHA-256 was
`f16f040a0dffa8bcaddb8e030cd6befab3131c12b97ec4cb0e5dad4c73bb33e2`.
Original/restored primary flash SHA-256:
`91f0dfce98cb2bea2fa0bbb11a27ac04f59dce17a3b4b960517d0fef157a14cd`.
Original/final control flash SHA-256:
`bd08ade342890e595007af2f74adbf765f785653539d587b873576998fb4fba6`.

The no-collector timing agrees with earlier live-USB observations; it does not
calibrate the ROM delay or timer ticks. This run qualifies the bounded known-USB
control on the primary badge, not arbitrary fault recovery, clock switching,
core 1, TE, fonts, frame rate, or the future nametag. The second badge is a
verified unchanged control, not a second execution of the USB experiment.

### M1 - Portable font assets and visual preview

- Import the minimal CC0 renderer source with license/provenance and implement
  a host platform adapter without bringing in the OS or a target dependency.
- Build a deterministic glyph-asset generator with explicit malformed-font,
  missing-glyph, size and allocation errors.
- Add measurement, clipping and RGB565 coverage composition using runtime
  strings. Keep the old diagnostic font unchanged.
- Render A/B/C through shared platform-independent layout/animation code.
  Provide native-size stills and a local timed preview, not generated video
  that might hide firmware limitations.
- Compare small text and dots at real pixel scale; inspect the fade midpoint,
  wave direction and period boundary. Select a layout with the owner.

**Tests:** generator repeatability; glyph bounds/metrics; clipping on all edges;
UTF-8 rejection; repertoire coverage; long fields; exact alpha endpoints;
background restoration; phase/wraparound/stall behavior; sanitizer checks.

**Exit:** a selected readable layout and reproducible assets, with measured
flash/RAM estimates. Editable supported text is demonstrated without rebuilding
the atlas.

**Completed 2026-10-12:** deterministic assets, shared C layout/animation,
editable native-resolution previews and no-libc Cortex-M33 link checks pass.
The owner accepted **B (identity and topic cards), Cascadia Mono, natural
coverage, 8-bit storage**. Keep A/C and the other font/coverage options for
comparison; do not substitute packed coverage in firmware without acceptance.
This is host readability acceptance, not physical panel qualification.

#### M1 source and license boundary

The portable core is incorporated under
[src/shared/fontrender](../../src/shared/fontrender/), with its
[CC0 license](../../src/shared/fontrender/LICENSE). It comes from the M0-pinned
owner checkout, commit `894e674fce0f2192f8017e414917792fa6479743`:
`fr_platform`, `fr_ttf`, `fr_raster`, `font_backend_truetype` and their headers.
The only imported-core adaptation replaces the external project's
`math_floor`/`math_ceil` raster-bound helpers with standard host `floor`/`ceil`.
[font_platform.c](../../src/host/tools/font_platform.c) supplies host allocation,
memory, file and diagnostic hooks. No build references the external checkout.
The TTF parser, dynamic allocation, raster caches and floating-point math remain
**host-only**; the target consumes generated coverage/metrics.

[vendor/cascadia-code](../../vendor/cascadia-code/) contains unmodified
`ttf/static` members of release v2407.24 and its
[SIL OFL 1.1 license](../../vendor/cascadia-code/LICENSE). Archive SHA-256:
`e67a68ee3386db63f48b9054bd196ea752bc6a4ebb4df35adce6733da50c8474`.

| Imported member | SHA-256 |
| --- | --- |
| [CascadiaMono-Regular.ttf](../../vendor/cascadia-code/CascadiaMono-Regular.ttf) | `06520d032ec274fa5040b22c6f4a1d829081b24ba40b2da56dae89bf10c7b481` |
| [CascadiaMono-Bold.ttf](../../vendor/cascadia-code/CascadiaMono-Bold.ttf) | `b22cb603ed23cac36e8444846e1841caca21719a5e852780a8d37ae7a49b0a36` |
| [CascadiaCode-Regular.ttf](../../vendor/cascadia-code/CascadiaCode-Regular.ttf) | `c33ef522cdfeff99907fb54f3e97152bb18bfb9b56ec2fef4d4ceec51c8974a4` |
| [CascadiaCode-Bold.ttf](../../vendor/cascadia-code/CascadiaCode-Bold.ttf) | `58197c5c3ba2967d4780b0ef8aee72d0cbbfa42885ddede9ec96fa7a1df5aead` |
| [LICENSE](../../vendor/cascadia-code/LICENSE) | `51882cd3cdba4e16f220f44ddb08a635c38c44ea6e0975db2574f4be6f958238` |

Generated banks are named **Nametag Mono/Code**, acknowledge their Cascadia
sources, and embed the full Microsoft copyright/OFL text through the bank's
license pointer. The font software and derivative banks remain OFL, not CC0.
The ignore rule exposes only this font directory, not other vendor contents.

#### M1 rendering and assets

[nametag_font_compile.c](../../src/host/tools/nametag_font_compile.c) generates
206 codepoints at each of ten sizes per family: Regular 12/14/16/18 and Bold
24/28/32/36/40/44. Real cmap coverage is checked before rasterization;
the imported backend's question-mark fallback cannot masquerade as coverage.
NBSP uses space metrics and soft hyphen is zero-width discretionary metadata.
Generation fails explicitly for malformed/truncated fonts, missing inputs,
required glyphs, allocation failure or incomplete output.

[bitmap_font.c](../../src/shared/bitmap_font.c) supplies strict UTF-8,
measurement, clipped mask/RGB565 drawing and optional contrast adjustment.
[nametag.c](../../src/shared/nametag.c) supplies bounded profile validation,
measured wrapping/native-size fallback, pending/active replacement, layout and
elapsed-time animation. Unsupported or unfit replacements preserve the active
scene. Neither the diagnostic font nor the hardware display driver changed.

Dots use fixed-point phase and fractional edge coverage, with an 8,000 ms wave
travelling toward decreasing x+y (lower right to upper left). The pattern is
excluded from the name rectangle, preserving its pixels exactly. Pages dwell
for 5,000 ms, then transition for 1,000 ms. Masks are combined before blending
against the current background, avoiding overlap dimming and black halos.
B adds an eight-pixel vertical transition with Q8 position/interpolation.
Complete-frame/background regeneration prevents trails.

| Default profile | Name size / lines | Pages | Complete loop |
| --- | --- | --- | --- |
| A | 44 px / 2 | 4 | 24,000 ms |
| B, selected | 40 px / 2 | 3 | 72,000 ms |
| C | 44 px / 2 | 3 | 72,000 ms |

Complete-loop duration is the least common multiple of the 8-second background
and 6-second page carousel; the combined animation is not an 8-second loop.
The uint32 clock helper handles tick wrap and elapsed-time stalls, but its input
must be calibrated milliseconds in M2, not unqualified raw hardware ticks.

#### M1 reproducible commands and resource measurements

Use the existing host C/LLVM toolchain and Node built-ins; no npm packages or
target runtime libraries are introduced.

```sh
make nametag-assets nametag-stills test-nametag test-nametag-target
make NAMETAG_COVERAGE=4 nametag-assets nametag-stills test-nametag test-nametag-target
make test-nametag-preview
make nametag-preview
# If the default loopback port 8766 is occupied:
NAMETAG_PREVIEW_PORT=8873 make nametag-preview
```

The preview builds both coverage formats and serves only on `127.0.0.1`.
It shows three 320 x 240 canvases, profile edits, Mono/Code, natural/crisp
coverage, 8-/4-bit storage, pause/seek and fade-boundary controls. Inspect at
1x before using 2x enlargement. The browser displays shared C RGB565 results;
its frame cadence is not a badge benchmark. Profile edits validate all three
layouts without regenerating the atlas.

Generated C/headers are in `build/nametag/assets-{8,4}`. Native RGB PPM stills
are in `build/nametag/preview-{8,4}`: 36 per format, covering A/B/C, both
families, both curves and 0/5,500/6,000 ms. For example,
`B-mono-natural-0.ppm` is the selected baseline.
[nametag_preview.c](../../src/host/tools/nametag_preview.c) also accepts
`--still B 5500 mono natural OUTPUT.ppm [PROFILE.txt]`, or
`--frame B 5500 mono natural` with a profile on stdin. Profile files contain
newline-terminated name, role, employer, username, location, interest count
and each interest; no trailing input is accepted.

| Measurement | 8-bit | Packed 4-bit |
| --- | ---: | ---: |
| Coverage bytes, all 20 fonts / both families | 1,186,698 | 593,890 |
| Selected Mono ARM link proof, flash `.text` including read-only data | 645,804 | 349,404 |
| ARM link proof `.data` / `.bss` | 0 / 208,492 | 0 / 208,492 |

Packed coverage starts each glyph on a byte boundary, high nibble first;
nearest quantization and expansion by 17 have a tested maximum error of 8/255.
Flash figures include code, metrics and license for the retained Mono bank;
the unreferenced Code bank is garbage-collected. Both fit the current 4 MiB
link allowance. Firmware initialization, display/timebase code and any second
framebuffer are not included.

On 32-bit ARM, a canvas is 153,600 bytes, workspace 40,960, scene 6,964 and
profile 1,116. Canvas + workspace + active/pending scenes total 208,488;
the proof adds a four-byte status word. Profiles are already inside scenes.
Host scenes are 7,072 bytes because pointers are wider. Compiler stack-usage
reports show a 1,664-byte preparation frame and 136-byte rendering frame;
these are individual frames, not measured whole-call-chain/interrupt peaks.
M3/M4 must budget and qualify actual stacks and total firmware SRAM.

[nametag_target.c](../../src/host/tests/nametag_target.c) links the selected B
path for Cortex-M33 with the ordinary freestanding/no-libc flags and linker.
Both formats have no unresolved symbols, including memory/compiler helpers.
Explicit byte-zero helpers avoid aggregate initialization introducing
`__aeabi_memclr4`. This is **not bootable**: no vectors, PICOBIN, initialization
or UF2. M1 did not flash or execute either attached badge.

#### M1 verification evidence

- Both coverage variants pass ASan/UBSan tests for metrics/repertoire, strict
  UTF-8, clipping/extreme coordinates, alpha endpoints, fit/field limits,
  NBSP/soft hyphens, rejected-update preservation and rendering restoration.
- Imported core tests exercise TTF/glyf parsing, malformed/truncated inputs,
  metrics/cache/fallback preservation, allocation failure and fractional
  rasterization. Sanitized generation verifies pinned hashes, repeatability,
  every font's quantization bound and missing-required-glyph rejection.
- Loop, wraparound, stall, diagonal phase sign, fade midpoint, fractional
  B motion, fixed name pixels and identical-text crossfades are checked.
- HTTP integration tests start an isolated ephemeral-port server, compare
  native frame bytes directly with CLI output for A/B/C in both formats,
  validate an accented alternate Code profile without changing binaries,
  and reject invalid profiles/options/requests explicitly.
- Browser verification used 1x 320 x 240 canvases: start/midpoint differed,
  family/curve/depth changes applied, an accented alternate name and `Engineer`
  role rendered,
  and an unsupported emoji produced a contextual error while retaining the
  previous pixels. A repeated exactly at 24 seconds and all three at
  72 seconds. The original profile and baseline options were restored.

### M2 - Timebase, display and TE qualification

- Measure inherited clock selectors/divisors on both badges; never assume
  150 MHz. Use the safe clock-switch sequence from existing qualification.
- Establish a known timer source and check its rate against an independent
  reference such as USB SOF. Document the reference's precision; distinguish
  configured nominal frequency from measured elapsed time.
- Benchmark identical full frames, representative partial regions, pattern
  rendering and text blending separately. Report distributions and command
  overhead, not only average fps.
- Probe GPIO21 TE with bounded edge capture before enabling synchronization.
  Record edge period/polarity, missed-edge behavior and scan/transfer relation.
  A measured TE edge alone is not proof that a whole update avoids tearing.
- Preserve display bus pulse timing when changing system clocks. Do not enable
  a faster byte writer merely because its throughput is attractive.
- Use high-contrast moving patterns and the actual nametag to check tearing,
  corruption and cold-boot initialization on both devices. Human observation
  or camera capture is required for visual claims; USB reports cannot see pixels.

**Exit:** measured timebase, transfer/render budgets and a documented TE result.
An eight-second loop should measure within 1% over at least ten periods using
the chosen reference. If that fails, fix timing before labelling it eight seconds.
TE may remain unsupported, but that limitation must be explicit.

#### M2 implemented diagnostic and safety boundary

[nametag_m2.c](../../src/tools/nametag_m2.c) is a separate bootable laboratory
image, not the M3 application. It uses selected B/Mono/natural/8-bit assets,
the ordinary single-core display writer and no target library, radio, PSRAM,
DMA, fast-byte writer or second core.

[nametag_clock.c](../../src/arch/rp2350/nametag_clock.c) records SYS/REF
selectors, dividers, selected-source acknowledgements, TIMER0/watchdog tick
settings and XOSC state. If SYS follows the inherited undivided ROSC reference,
it is first handed to the same-frequency direct ROSC AUX source, without
changing its divider or a live AUX selection. REF can then switch safely to
the 12 MHz crystal without accelerating SYS or changing the writer's pulse
sequence. Existing independent SYS AUX sources are retained. Unsupported
inherited reference configurations fail explicitly.

TIMER0 receives XOSC/12 ticks, then fresh 128-interval USB SOF samples must
confirm the nominal microsecond rate within 1%. The hardware frequency counter
records SYS and REF relative to nominal 12 MHz XOSC. These are not claims of
150 MHz CPU operation or metrology-grade absolute calibration. Another SOF
sample and complete clock snapshot close the run; the host rejects drift,
changed clock state, incomplete loop evidence and invalid sample shapes.

The diagnostic collects:

- 32 raw samples each of identical full-frame transfer, 320 x 64 footer,
  64 x 64 tile, address-window/command overhead, background composition, text
  composition, complete rendering and rendering plus transfer.
- GPIO21 levels/edges with TE disabled, then bounded rising-period/high-width
  capture with vertical-only TE enabled. Sixteen bounded rising-edge waits
  bracket ordinary full-frame writes. TE is disabled again before the
  animation; synchronization is **not enabled** or qualified by these probes.
- Ten elapsed-time 8-second background-period markers against accumulated USB
  SOF, while displaying the actual nametag for 80 seconds, followed by
  12 seconds of moving high-contrast bars. A serial label identifies the
  active screen. Presentation uses absolute deadlines and skips stale frames.

Transfers service USB/recovery every 16 columns; their reported distributions
include this instrumentation overhead. Text/background are separately exposed
through the shared renderer, with unchanged complete-frame results. Quantile
reporting uses nearest-rank min/median/p99/max; with only 32 samples, p99 is the
observed maximum, not a long-run M3 performance qualification.

Recovery is armed before USB initialization. There is no renewal without an
accepted nonce-bound experiment. During the qualified experiment only, it is
renewed at most every 500 ms after foreground and USB SOF progress, under an
absolute 180-second timer deadline. Frozen/lost SOF, reset/deconfiguration,
replacement requests, errors or expiry stop the experiment. No collector
means no renewal. This intentionally differs from M0's unrenewed USB control.
The collector has a 215-second process alarm and bounded individual transfers;
the campaign applies an independent 225-second subprocess deadline.

#### M2 build, collection and verification

```sh
make nametag-m2 test-nametag-m2 test-live-usb test-nametag-control
mkdir -p build/nametag-m2
node src/host/tools/nametag_m2_campaign.mjs \
  9BBF88DD17D66E7D 08DA26F86E57F5EA build/nametag-m2/NEW-RUN
# Physical Boot recovery, if required, then:
node src/host/tools/nametag_m2_campaign.mjs --restore build/nametag-m2/NEW-RUN
```

[nametag_m2_campaign.mjs](../../src/host/tools/nametag_m2_campaign.mjs)
reuses M0 identity/backup/deadline/restoration helpers, builds in its own fresh
private evidence directory, backs up and verifies all 16 MiB of both badges
before either load, and hashes inputs and artifacts. It tests the primary,
verifies no-collector recovery and restores/readbacks its original flash before
testing the other badge. The not-currently-tested badge is independently
checked as the unchanged control. Failure/partial-load paths restore every
attempted device; a failed campaign never becomes a pass merely by restoration.
Both original flashes receive final independent readbacks and stay in BOOTSEL.

[nametag_measure](../../src/shared/nametag_measure.h) and the isolated
`LIVE_USB_NAMETAG_M2` control extension carry a frozen 512-word record in
86 CRC-protected pages. Every page binds schema, build, test, fresh nonce,
page/count/length and reserved padding. The existing USB modes retain their
previous structure/behavior when the flag is absent.

Sanitizer-backed C tests cover paging, CRC/nonce/build/padding rejection,
nearest-rank statistics, status-ACK gating, cancellation and USB reset. Thirteen
offline campaign tests cover exact evidence gates, a paired successful run,
failed backup, stale build, partial load, failed restoration, cold staging,
collection failure, cancellation, trace-read failure without preventing
restoration, and flagged cache timing/storage metadata. All fourteen
M0 runner tests and the existing live-USB tests remain passing. Both 8-/4-bit
renderer suites pass; all 72 native 8-/4-bit preview stills are byte-identical
to the pre-M2 results after separating background/text rendering.

The pre-optimization diagnostic with startup telemetry linked without unresolved helpers:
655,404 bytes flash, 0 initialized data and 205,260 bytes BSS, plus the
report-mailbox reservation and stacks. The warm-campaign image used 655,244
bytes flash and 205,212 bytes BSS. The build keeps the existing `all` default;
M2 is opt-in.
These are build measurements, not physical success or a frame-rate result.

Software `load -x` exercises application/display reset initialization, **not**
a power-off cold boot. Power-cycle observations cannot be inferred from USB
reports and remain a separate acceptance gate.

#### M2 physical results, 2026-10-12

Private evidence is retained under
`build/nametag-m2/campaign-20261012-01/`; do not publish original-flash backups.
The paired warm campaign passed with build `00e91dd3`, UF2 SHA-256
`8be57dbf3d03aca739166a3be47246488fb04b9fe5a31dda4d37953836b13f8a`
and ELF SHA-256
`d5e1bd778f720d1ed27977a49caf5750a5a14ff975c5ff5249407c915fe66473`.
Both original 16 MiB flashes were restored, independently read back and
verified; both devices were left in BOOTSEL.

In the following tables, primary means `9BBF88DD17D66E7D` and other means
`08DA26F86E57F5EA`.

| Timing observation | Primary | Other |
| --- | ---: | ---: |
| SYS frequency-counter kHz, relative to nominal XOSC | 45,776 | 45,301 |
| REF frequency-counter kHz, relative to nominal XOSC | 12,001 | 12,001 |
| Inherited TIMER0 ticks per USB SOF millisecond | 476.7578125 | 471.7109375 |
| Qualified initial ticks per SOF millisecond | 1000.015625 | 1000.015625 |
| Qualified final ticks per SOF millisecond | 1000.0078125 | 1000.0078125 |
| Mean of ten background-period SOF durations, ms | 8017.5 | 8017.6 |
| Skipped presentation deadlines | 2186 | 2186 |
| Presented frames, animation plus contrast | 582 | 582 |
| No-collector return to BOOTSEL, host ms | 18,101.43 | 18,286.39 |

SYS was already independent ROSC AUX (`CTRL=0x41`), divider 1, and stayed
unchanged. Inherited REF was ROSC/4 and TIMER0 used divider 24. Qualified REF
was XOSC/1 and TIMER0 used divider 12. The watchdog divider stayed 24.
Consequently the requested 8000 ms ROM recovery interval was not eight
calibrated wall-clock seconds; measured no-collector recovery is recorded above.

The ten-period average is about 0.22% above 8000 SOF milliseconds, passing
the 1% gate. USB SOF is the independent elapsed-time reference, not a calibrated
laboratory clock. The first foreground marker took 8176/8178 SOF milliseconds;
later periods were 7991-8009/7991-8004. These markers include blocking render
and transfer latency and are not exact interrupt or panel-presentation timestamps.

| Operation, microseconds | Primary median / p99 | Other median / p99 |
| --- | ---: | ---: |
| Full transfer | 40,533 / 40,546 | 40,957 / 40,972 |
| Footer 320 x 64 transfer | 10,994 / 11,003 | 11,109 / 11,117 |
| Tile 64 x 64 transfer | 2,203 / 2,204 | 2,226 / 2,231 |
| Commands | 5 / 14 | 5 / 14 |
| Background composition | 32,074 / 32,557 | 32,413 / 32,900 |
| Text composition | 106,376 / 108,874 | 107,474 / 109,909 |
| Complete rendering | 138,538 / 141,371 | 139,885 / 142,721 |
| Rendering plus transfer | 179,219 / 182,067 | 181,036 / 183,821 |

These are 32-sample distributions with the ordinary writer and progress
instrumentation, not production performance guarantees. Full transfer alone
exceeds a 30 Hz frame budget; text composition dominates total cost. The
proposed M3 30 presented fps / p99 <=25 ms target is **not met**. M3 needs an
evidence-led rendering/update strategy, not simply a second core or a nominal
timer change.

TE disabled produced zero edges on both badges. Enabled capture produced
118/120 edges in one second, rising-period medians 16,927.74/16,645.74 us
(about 59.1/60.1 Hz), and high-width medians 1163.98/1143.98 us. All sixteen
rising waits per badge succeeded. Aligned full writes still took about
40.5/41 ms, longer than two panel periods. Scan/transfer phase was not
established: **tear-free synchronization is not qualified**, and TE remains
disabled during animation.

The owner reported that both screens were readable/correct and no tearing
was noticed. There was brief, small flickering in the first seconds at the
first role/location transition, not noticed later. This is an observation,
not proof of tear-free operation or a diagnosed cause. The newer diagnostic
keeps its composite benchmark at the 5500 ms midpoint instead of deliberately
jumping among footer phases; other component measurements retain representative
phases. That adjustment has not been physically shown to resolve the flicker.

#### Cold-boot boundary and deferred observation

The runner can stage a fresh candidate without executing it, then collect
after an independently confirmed full power cycle:

```sh
node src/host/tools/nametag_m2_campaign.mjs --cold-boot-stage RUN_DIRECTORY SERIAL
# Remove every power source, then reconnect USB with Boot released.
node src/host/tools/nametag_m2_campaign.mjs \
  --cold-boot-collect RUN_DIRECTORY SERIAL --power-cycle-confirmed
# If full power removal is unavailable or the test is cancelled:
node src/host/tools/nametag_m2_campaign.mjs --restore RUN_DIRECTORY
```

Unplugging USB alone is **not** a cold boot when an internal battery remains
connected. Do not require disassembly for this experiment or infer a power
disconnect from a screen turning off. No externally accessible control is
qualified here as removing MCU power; GPIO41 only switches the peripheral rail.
A hardware reset would be a separate reset-startup test, not full power removal.

Two staged primary collection attempts failed to enumerate the diagnostic;
both restored and independently verified both original flashes. The second
candidate, build `c1b25353`, reserved a 64-byte early startup trace at
`0x2007f100`. All returned bytes were zero, with no matching build signature.
SRAM retention across BOOTSEL is not qualified, so this does not locate the
failure or establish whether firmware ran. The existing USB PLL safety guard
is not implicated by a matching trace; no speculative clock/PLL fix was made.

The owner subsequently clarified that only USB was unplugged and battery power
may remain; battery disconnection requires disassembly, which the owner prefers
not to do. Neither attempt therefore establishes a true cold boot or a
flash-startup defect. Screen behavior during those attempts was not observed.
Keep the failed collection records and the successful warm measurements;
restoration does not change either into a cold-boot pass.

**M2 outcome:** implemented diagnostic, qualified warm timebase, measured
render/transfer budgets and bounded TE evidence on both devices. Verified
cold-start display remains **deferred on both badges**. Do not mark that
observation gate complete or claim production smoothness.

#### Exact-pixel renderer acceleration, 2026-10-12

The owner requested a drastic reduction in rendering cost. Two measured passes
were made without changing the selected design, coverage format, MCU clocks,
ordinary display writer, USB/recovery instrumentation, core ownership or memory
model:

- [bitmap_font.c](../../src/shared/bitmap_font.c) clips each glyph once with
  wide-coordinate bounds checks, then traverses the canvas in its native
  column-major order or a mask in row-major order. Transparent pixels do not
  read/blend/write the canvas. Opaque pixels bypass RGB channel arithmetic.
- The shared alpha helper uses exact shift/add rounding for bounded products,
  replacing division by 255 without approximating coverage or color. Natural
  coverage, crisp coverage, opacity and dissolve rounding remain unchanged.
- [nametag.c](../../src/shared/nametag.c) resolves footer interpolation rows
  outside the pixel loop, skips empty coverage and avoids preparing/sampling
  the unused incoming mask during dwell. It still rebuilds page masks from the
  current scene; there is no hidden cache invalidation or stale-profile behavior.
- Halftone rasterization visits only intersecting pixels. Its nonoverlapping
  squares blend with the uniform fill using a 256-entry color table prepared
  once per frame, rather than recomputing RGB channel arithmetic at every dot
  pixel. This adds 512 bytes of temporary stack storage, not another framebuffer.
- `NAMETAG_RENDER_FLAGS` selects `-O2` only for the bitmap-font and nametag
  translation units in the diagnostic and ARM link proof. The display writer
  retains its existing flags and qualified pulse sequence.

Regression verification compared **312 pre/post native preview frames** across
both 8-/4-bit atlases, all A/B/C layouts, Mono/Code, natural/crisp, dwell,
transition edges, fractional-motion phases and loop closure. All were
byte-identical. Persistent sanitizer tests exhaustively check the bounded
alpha-rounding domain and every 5-/6-bit channel pair at every alpha. They also
check every opacity, overlapping synthetic glyphs, mask sentinels and the footer
against the original scalar interpolation formula. Both renderer/asset suites,
both no-libc ARM link proofs and preview HTTP integration passed.

The first optimized paired campaign, build `f10aed9d`, is retained in
`build/nametag-m2/campaign-20261012-render-01/`. It measured about 29-30 ms
median text and 48 ms median complete rendering. The second pass, build
`716da69c`, passed on both badges and is retained in
`build/nametag-m2/campaign-20261012-render-02/`. Its UF2 SHA-256 is
`e41e8f25c9d89f68b57dc5d80d0c990deceeacf11c015cee7c71156894907480`
and ELF SHA-256 is
`962e52f3e230e8c339798946d32b54f939a343863c537a0b5032b1ef5d2cdbe7`.
Both campaigns made fresh full backups and restored both originals with final
independent readbacks; both devices ended in BOOTSEL.

| Operation, microseconds | Final primary median / p99 | Final other median / p99 | Median speedup vs original |
| --- | ---: | ---: | ---: |
| Background composition | 8,317 / 8,502 | 8,404 / 8,589 | 3.9x |
| Text composition | 19,374 / 36,992 | 19,576 / 37,360 | 5.5x |
| Complete rendering | 27,920 / 45,585 | 28,198 / 46,009 | 5.0x |
| Full transfer | 40,531 / 40,548 | 40,951 / 40,974 | unchanged |
| Rendering plus transfer, fixed transition midpoint | 86,237 / 86,427 | 87,097 / 87,217 | see scope below |

Text and isolated rendering retain the original representative 0/5500/6000 ms
phase sampling. The combined benchmark uses the fixed 5500 ms transition
midpoint introduced after the original flicker observation; do not treat its
comparison with the original phase-mixed combined benchmark as an identical
workload. Quantiles still use only 32 samples, so p99 is the observed maximum.

Final CPU measurements were 45,780/45,316 kHz relative to nominal XOSC,
not accelerated CPU settings. Qualified timer rates were
1000/1000.0078125 ticks per SOF millisecond. Ten background periods averaged
8003.1/8003.2 SOF milliseconds, within 1%. Animation plus contrast presented
1084/1018 frames and skipped 1679/1746 deadlines; those mixed-workload counts
are not a steady-state nametag frame-rate guarantee. No new human visual or
cold-power observation is claimed by these machine measurements.

The final diagnostic links without unresolved helpers at 661,872 bytes flash,
0 initialized data and unchanged 205,260 bytes BSS, plus reserved mailbox and
stacks. Compiler-reported individual stack frames are 672 bytes for background,
104 for text, 24 for the render wrapper, and 176 for the glyph draw helper;
individual frame sizes are not whole-call-chain stack measurements.

**Remaining bottleneck:** a full transfer alone still exceeds the 33.3 ms
30 Hz budget, while observed transition rendering alone can take 46 ms.
The 30 fps / p99 <=25 ms render-plus-transfer target remains unmet. M3 should
measure cached static/profile text and changed-region updates before adding
cores or changing the bus. A faster transfer path would require its own timing,
ownership and recovery qualification; these rendering changes do not qualify it.

#### Prepared scene cache, 2026-10-12

The follow-up request for further speed led to an additional, explicitly
prepared renderer path in [nametag.c](../../src/shared/nametag.c). The original
stateless renderer remains available as a pixel reference and continues to
drive the host preview.

`badger_nametag_cache_prepare()` captures a complete scene snapshot and renders
the name once into bounded RGB565 storage. The name's background is invariant:
halftone squares are excluded from its clip, and any divider is stationary.
Per-column name bounds avoid copying unused columns/rows each frame. Copying
the precomposed colors preserves sequential glyph-overlap rounding; it does
not approximate the name as a combined alpha mask.

Two footer-mask slots retain the current and next pages, with per-row nonzero
coverage bounds. Page changes reuse the already cached next page and prepare
the following one. Arbitrary seeks can rebuild either or both slots, including
backward time changes and last-page wrap. Fractional vertical interpolation
uses the union of the contributing rows' bounds, not a clipped approximation.
Empty rows/columns no longer run the compositor. Roles and usernames still
blend against the changing background each frame; no incorrect opaque cache
is used behind that text.

Ownership/update contract:

- Allocate `badger_nametag_cache_t` statically, not on the target stack.
- Cache preparation uses the supplied canvas as scratch and publishes readiness
  only after successful preparation. Failures return an explicit font/argument
  result and invalidate the cache being prepared; there is no fallback image.
- The cache owns copied profile/layout/text state. Fonts remain immutable and
  must outlive the cache. Editing or replacing the source scene cannot silently
  change an active cache.
- Reprepare explicitly for a new profile, font family, layout or crisp setting.
  Prepare separate pending storage if an unsuccessful update must retain the
  existing live cache, then apply the successful cache between frames.
- Use `badger_nametag_cached_render()` for complete frames.
  `badger_nametag_cached_text()` requires the cache scene's own freshly rendered
  background; unlike the stateless text helper, it is not an arbitrary-background
  overlay API.

Sanitizer tests compare **3,960 cached/stateless frame pairs**, plus 120
snapshot-isolation pairs, across both atlas formats, Mono/Code, A/B/C,
natural/crisp, accented/NBSP/soft-hyphen profiles, zero/one-page and maximum
ten-page profiles, transition boundaries, backward seeks and full-loop/last-page
wrap. Preparation failure and reprepare paths are explicit tests. The shared
stateless compositor still matches all 312 original preview fingerprints.
Both ARM link proofs, preview HTTP integration and all thirteen campaign
tests pass.

The diagnostic now uses the cached path. Report flag bit 0 identifies this
mode; words 53/54 record preparation ticks and cache bytes. The collector
summary reports the mode and preparation separately from frame timings.
Cached text and isolated render samples retain the representative
0/5500/6000 ms sequence, so their measurements include slot refreshes caused
by those deliberate seeks, not only ideal warmed-cache frames. The combined
benchmark retains its fixed 5500 ms transition midpoint.

Paired warm campaign `build/nametag-m2/campaign-20261012-cache-01/` passed
with build `780e1148`, UF2 SHA-256
`ea3149ced615eab8463163ca9e292ae5fd5161952bd3b305b6ffc86258c41bff`
and ELF SHA-256
`c499855624efa6e104bfa136bc2ad0b649dd5eb3ef866a3ea4ec9132ccba1126`.
Both original flashes were restored and independently read back against the
fresh full backups; both devices ended in BOOTSEL.

| Operation, microseconds | Cached primary median / p99 | Cached other median / p99 |
| --- | ---: | ---: |
| Background composition | 8,318 / 8,423 | 8,407 / 8,515 |
| Text composition | 12,318 / 16,675 | 12,447 / 16,838 |
| Complete rendering | 20,734 / 25,120 | 20,947 / 25,405 |
| Full transfer | 40,524 / 40,555 | 40,951 / 40,969 |
| Rendering plus transfer, fixed transition midpoint | 65,529 / 65,667 | 66,225 / 66,327 |

Compared with the preceding optimized pass, median rendering cost falls about
26%, observed render p99 about 45%, median text about 36%, and combined
midpoint cost about 24%. Compared with the original uncached measurements,
median rendering is about 6.7x faster and text about 8.6x faster. The
32-sample p99 remains the observed maximum, not a production tail guarantee.

Cache preparation cost 33,798/34,093 us on the two badges, separate from profile
validation/layout and display initialization. The ARM cache
occupies 118,840 bytes, including its scene snapshot, name pixels, masks and
bounds. It replaces the 40,960-byte diagnostic workspace, so diagnostic BSS
increases by **77,880 bytes** to 283,140 bytes. Flash is 664,740 bytes and
initialized data remains zero; the image links without runtime helpers or
PSRAM. The cache contains only two page slots regardless of profile page count.
Individual compiler-reported stack frames are 96 bytes for preparation,
120 for cached text, 16 for cached rendering and 104 for the footer compositor.

CPU counter measurements were 45,757/45,298 kHz; no clocks or display pulses
were changed. Qualified timer rates were 1000.0234375/1000.015625 ticks per
SOF millisecond. Ten background periods averaged 8001.9/8005.3 SOF milliseconds,
within 1%. Animation plus contrast presented 1354/1347 frames, skipping
1409/1417 deadlines; these are mixed-workload counts, not a 30 fps claim.
No new visual or full-power cold-boot observation is inferred.

**Remaining limit:** full-screen transfer still costs about 41 ms. Cached
rendering helps, particularly transitions, but the 30 fps / p99 <=25 ms
render-plus-transfer target remains unmet. The next substantial step is
changed-region transfer or a separately qualified faster bus path, with an
explicit SRAM budget for caches, update staging and any retained framebuffer.

#### Differential display presentation, 2026-10-12

The next optimization adds [frame_diff.h](../../src/shared/frame_diff.h) and
[frame_diff.c](../../src/shared/frame_diff.c), a portable presenter that sends
changed rectangles using the existing display writer. Rendering still produces
the exact same complete RGB565 frame; no animation or font approximation is
introduced. Clocks, GPIO pulses and core ownership are unchanged.

The presenter retains the last successfully sent framebuffer. A fixed 4 x 4
grid, shifted by two pixels to match the halftone centers, classifies changes
using aligned two-pixel comparisons. The 81 x 61 grid includes two-pixel edge
cells. Dirty horizontal runs are merged vertically into nonoverlapping
rectangles. An invalid baseline, or more than 3,705 of the 4,941 cells changing,
uses one full-frame write; exactly 3,705 remains partial. Unchanged frames
require no panel writes. This threshold is a bounded fallback, not a claim
that every smaller dirty pattern is faster than a full transfer.

Ownership and failure contract:

- Allocate `badger_frame_t` and `badger_frame_presenter_t` statically. The union
  gives aligned canvas/word views without pointer reinterpretation casts.
- Keep the input frame immutable until `badger_frame_present()` returns;
  do not pass the presenter's own retained frame as input.
- A writer reports success only after completing the rectangle. Commit the
  retained pixels and traffic counters only after that successful callback.
- Any writer failure returns an explicit I/O error and invalidates the entire
  baseline. The next presentation forces a full refresh, including after a
  partially transmitted rectangle.
- Call `badger_frame_invalidate()` after panel reset or any external display
  write. The diagnostic's direct probes and recovery-error display do this.
  Existing USB/recovery error details remain intact on presentation failure.

Both atlas sanitizer suites reconstruct a mock panel for all **3,960**
cached/reference frame cases, including profile changes, transitions, seeks
and wraps. Every reconstructed panel and committed baseline matches the
complete reference frame. Separate tests cover unchanged frames, edge cells,
no duplicate writes, the exact fallback threshold, moving stripes, partial
callback failure and forced-full recovery. Both Cortex-M33 link proofs pass,
as do all **fourteen** campaign tests and the paged protocol/control tests.

Report flag bit 1 identifies differential presentation and requires the cache
flag. Words 55-63 describe benchmark and animation traffic, presenter storage
and grid geometry; words 460-491 contain 32 presenter-only timing samples.
The collector validates those counters and reports scan/write/commit/service
cost separately from complete render-plus-present time.

The combined benchmark now establishes 5,467 ms outside the timed interval,
then renders and presents 5,500 ms inside it. This is a real 33 ms change at
the transition midpoint, not repeated presentation of an identical frame.
The presenter timing includes classification, region commands, pixel writes,
baseline commits and USB/recovery service. Full-transfer probes remain full
transfers. The 80-second animation traffic/frame totals exclude the separate
12-second contrast phase; overall frame and skip counts still include both.
Completed animation frames include possible no-op presentations and do not
alone establish a physical panel refresh rate.

The presenter occupies **158,544 bytes**, including its 153,600-byte retained
frame. Current diagnostic BSS is **441,684 bytes**, initialized data zero and
flash **668,428 bytes**, with no libc/runtime helpers or PSRAM. The ARM proof
that also retains the stateless workspace uses 485,876 BSS bytes. Compiler
stack frames are 192 bytes for presentation, 72 for animation, 32 for the
region writer and 16 for its wrapper; these are not stack high-water marks.
Only about 78 KiB of the 508 KiB application SRAM remains in the diagnostic.
Two full prepared caches plus retained display history do **not** fit in this
configuration. M3 must explicitly budget update staging, reduce storage or
design validated buffer reuse; the earlier separate-pending-cache contract
must not be assumed achievable with the current allocations.

Paired warm campaign `build/nametag-m2/campaign-20261012-diff-01/` passed
with build `52d90b45`, UF2 SHA-256
`9b908a4c8c50889b53990c7556ec0be8d28661c18bef9e5585e7c6972d78a180`
and ELF SHA-256
`d26acbb8356ddb83a838e1c06df2513035f11e076794920e1e9663ec773a47c9`.
Both no-collector recovery checks passed. Both complete original flashes were
restored and independently read back against the fresh full backups; both
badges ended in BOOTSEL.

| Operation, microseconds | Differential primary median / p99 | Differential other median / p99 |
| --- | ---: | ---: |
| Background composition | 8,324 / 8,447 | 8,407 / 8,526 |
| Text composition | 12,917 / 16,946 | 13,062 / 17,116 |
| Complete rendering | 21,376 / 25,550 | 21,565 / 25,729 |
| Full-transfer control | 40,552 / 40,590 | 40,959 / 40,981 |
| Differential presentation, 5,467 to 5,500 ms | 21,159 / 21,643 | 21,381 / 21,816 |
| Render plus differential presentation | 46,386 / 47,054 | 46,875 / 47,523 |

The midpoint benchmark sends 10,160 pixels in 221 rectangles on each badge,
13.23% of a full frame, with no full fallback in those 32 samples. Compared
with the preceding full-transfer cache campaign, combined midpoint time
falls about **29%**, or about **1.4x faster**. This is a same-current-pose
comparison, but the new measurement explicitly establishes a prior pose and
includes comparison/commit overhead. The 32-sample p99 is still only the
observed maximum, not a production tail guarantee.

Over the separate 80-second nametag animation:

| Measurement | Primary | Other |
| --- | ---: | ---: |
| Completed frames | 1,587 | 1,534 |
| Completed frames / second | 19.84 | 19.18 |
| Pixel payload | 8,423,776 | 8,317,116 |
| Rectangles sent | 414,184 | 407,776 |
| Full-frame fallbacks | 0 | 0 |
| Payload versus full transfer for those frames | 6.91% | 7.06% |

Thus pixel payload falls about **93%**, but many small rectangles,
classification, commits and service calls still have real cost; payload
savings must not be equated with elapsed-time savings. The mixed animation
plus contrast totals were 1,768/1,714 completed frames and 996/1,048 skipped
deadlines, versus 1,354/1,347 frames in the preceding full-transfer campaign.
Those totals are not animation-only frame-rate measurements.

Measured CPU counters were 45,742/45,289 kHz and qualified timer rates
1000.0078125/1000.03125 ticks per SOF millisecond. Ten background periods
averaged 8003.5/8000.1 SOF milliseconds, within 1%. Maximum SOF service gaps
were 49/50 ms. No new full-power cold-boot or tear-free visual qualification
is inferred. The **30 fps / p99 <=25 ms target remains unmet**: further work
must measure rectangle/service overhead and composition rather than assume
that reducing pixel traffic alone meets it.

#### Presenter bottleneck investigation and contiguous scanning, 2026-10-12

Profiling now separates complete region callbacks from the rest of presentation.
Optional report flag bit 2 requires cached differential mode; words 492-494
contain aggregate writer ticks, writer-side service ticks and service-call
count for the 32 midpoint samples. The host checks that writer time fits
inside total presenter time, service time fits inside writer time, and service
counts fit the transmitted regions/payload. The summary reports arithmetic
means, not percentiles of the individual components. Service time is a
**subset** of writer time and must not be added twice.

Profiling is enabled only during the changing-frame combined benchmark.
The animation and direct probes pass no profiling context; mandatory USB
polling, cancellation, SOF checks and recovery renewal remain in place.
The residual includes comparison, rectangle planning, retained-frame commit
and outer service calls. It is not a measurement of classification alone.
Callback timers themselves add cost, so instrumented and earlier
uninstrumented timings are distinguished below.

The initial primary report measured about **13.26 ms residual presenter CPU**
versus **9.10 ms writer time**, including **1.32 ms writer-side service**.
There were 458 service calls for 221 rectangles. This identifies presenter
CPU as a substantial cost; simply reducing USB polling would not remove most
of it. Rendering at the transition midpoint still costs about 25 ms.

An initial clear probe used the diagnostic scalar `badger_gfx_clear()`, not
the actual renderer's inline `badger_pixels_fill()`. The host correctly
rejected the supposed clear/background relationship. That probe was removed:
target assembly shows the real renderer already performs packed, unrolled
stores. `campaign-20261012-profile-01/` remains **failed**, with its primary
raw report and verified original restoration retained; the control was not
flashed. The writer/residual timing fields in that raw report validate under
the corrected profiling schema, but this is not a passed paired campaign.

The first candidate moved the existing tile traversal into column-major order
and separated classification/commit from the planner to reduce register
pressure. Paired `campaign-20261012-presenter-02/` passed recovery/restoration
but **did not improve speed**: presenter medians were 22.49/22.72 ms and
animation completed only 17.61/16.39 frames/s. That classifier was replaced,
not kept on the strength of a smaller assembly listing.

The retained implementation scans each framebuffer column contiguously.
It handles the two-pixel top/bottom cells separately, compares two aligned
words for each interior four-pixel cell, and marks the existing dirty grid
only on a detected difference. Each cell contributes to the dirty count once,
even if multiple columns differ. No per-cell X/Y clipping or variable-height
comparison loop remains. Classification and commit stay out of the rectangle
planner's register allocation. The grid, rectangle order/coalescing, strict
75% fallback, successful-write commit and error invalidation are unchanged.

All 3,960 panel-reconstruction cases pass for both atlas formats, together
with the existing failure/threshold tests. Another 200 single-pixel updates
exercise both low/high RGB565 bits, both halves of aligned words, interior
cell boundaries and all edge shapes; each changes exactly one grid cell.
Both ARM proofs and all **fifteen** campaign tests pass. Diagnostic BSS remains
441,684 bytes, initialized data zero, flash 667,312 bytes. Individual stack
frames are 160 bytes for the planner, 56 for classification and 40 for commit;
these nested frames are not a reduced whole-call-stack or high-water claim.

The first contiguous-scanner campaign, `campaign-20261012-presenter-03/`,
collected a valid primary report (27.38 completed animation frames/s) but
failed on a picotool timeout during the no-collector BOOTSEL-return check.
The original was restored; final readbacks matched both original hashes and
the control was not flashed. That campaign remains failed. The unchanged
firmware was repeated with fresh backups/nonces in
`build/nametag-m2/campaign-20261012-presenter-04/`.

The repeat uses build `c0732d2f`, UF2 SHA-256
`c721af2f7ad673e4afd8d5bb8e8e30ef9c976680d5d8d7c9b5e48d84b25884ee`
and ELF SHA-256
`e3124a852ca8fefe22b5e741a84bacbdc4ac024a722cb3b065dc148ee1e37493`.
The repeat **machine-passed on both badges**, including both no-collector
recovery checks. Both complete original flashes were restored and independently
verified against the fresh backups; both devices ended in BOOTSEL.

| Measurement | Primary | Other |
| --- | ---: | ---: |
| Presenter median / observed p99, us | 17,755 / 18,182 | 17,929 / 18,368 |
| Writer mean, us | 8,503 | 8,581 |
| Writer-side service mean, us (included above) | 1,001 | 1,005 |
| Residual presenter CPU mean, us | 9,288 | 9,361 |
| Render plus present median / observed p99, us | 43,124 / 43,631 | 43,536 / 44,036 |
| Isolated render median / observed p99, us | 21,516 / 25,508 | 21,731 / 25,806 |
| Full-transfer control median, us | 40,532 | 40,950 |
| Completed nametag frames in 80 seconds | 2,190 | 2,186 |
| Completed nametag frames/s | 27.38 | 27.33 |
| Pixel payload versus full writes for those frames | 5.67% | 5.67% |
| Animation plus contrast frames / skipped deadlines | 2,488 / 274 | 2,484 / 278 |

The midpoint still sends the same 10,160 pixels in 221 regions with 458
writer-side service calls. Relative to the initial instrumented primary
profile, residual CPU falls about 30% and total presenter time about 20%.
Compared with the preceding uninstrumented differential campaign, combined
midpoint cost falls about 7%, while completed animation cadence improves
about **38-42%**. The two comparisons have different profiling overhead and
must not be conflated. No full fallback occurred during either 80-second
animation; absolute-time scheduling and frame skipping still apply.

CPU counters were 45,798/45,307 kHz. Qualified timer rates were
1000/1000.015625 ticks per SOF millisecond; ten background periods averaged
8002.5/8002.6 SOF milliseconds. Maximum SOF service gaps remained 49/50 ms.

**Next measured optimization priorities:** transition rendering (~25 ms),
residual presenter CPU (~9.3 ms) and actual region writing (~8.5 ms, including
~1 ms service). Investigate footer interpolation/blending and static coverage
work first, then finer classification/planning/commit profiling or
cost-aware rectangle coalescing. More coalescing trades extra unchanged pixels
for fewer commands; it needs elapsed-time evidence, not just region counts.
Keep the qualified writer and single-core control until a different bus path
is independently verified. The ten-minute **30 fps / p99 <=25 ms** gate,
full-power cold boot and tear-free visual acceptance remain unqualified.

### M3 - Single-core nametag firmware

**Continuous prototype implemented, 2026-10-12.** Build with
`make NAMETAG_COVERAGE=8 badge-nametag`; the result is
`build/universe26/uf2/badge-nametag.uf2`. It uses the accepted Mono B/natural
profile, prepared cache and contiguous-scanning presenter. It initializes
the display, presents a complete first frame, then explicitly enables
display output/backlight before continuing partial updates. It has no serial
overlay, contrast test, collector requirement or fixed demonstration lifetime.

Timing uses the nominal 12 MHz crystal/TIMER0 setup already measured by M2,
with a fractional-microsecond accumulator that preserves remainder and handles
32-bit timer wrap. Host tests compare repeated wrap/large-delta/fractional
updates against a 64-bit reference and verify invalid arguments preserve
state. The loop retains absolute 33,333 us deadlines and skips obsolete frames.
No USB, second core, PSRAM or new bus pulse sequence is involved. Battery life
and full-power startup are not inferred.

A BOOTSEL recovery watchdog is armed before initialization and renewed every
500 ms only after completed frames. Normal operation runs indefinitely;
startup failure, foreground stall or explicit error stops renewal. When the
display is initialized, errors are shown on screen and the error code is
retained in `badge_nametag_error`; progress/skips/phase also have volatile
debug symbols. This is not an independently qualified production watchdog.
The initial image omitted the display-on/backlight call and appeared blank;
the corrected first-frame and error-display paths now make that call.

The owner requested that one badge be left running. Corrected UF2 SHA-256
`a33006b7e6b546f535ec2c2572e639f6d6a8b3576c4f38dc5f75dfb79aa48590`,
ELF SHA-256
`cf32f55b5ad30c2ebeac74e8c702186066c6be751b103491c9f392d007a3646f`
was serial-bound loaded with verification onto **`9BBF88DD17D66E7D`**.
The owner confirmed that the name is visible and animation is running.
This badge is intentionally left running the continuous image, not restored.
The other badge, **`08DA26F86E57F5EA`**, was not flashed and remains in BOOTSEL
with its original firmware.

Its fresh original 16 MiB backup is retained at
`build/nametag-continuous/deploy-20261012-01/9BBF88DD17D66E7D-original.bin`
and matches the original hash recorded above. The initial staged image had an
independent full-flash readback: program bytes matched and flash outside its
erase sectors was unchanged. Corrected image/evidence is retained separately
in `build/nametag-continuous/deploy-20261012-02/`.
The continuous image has no USB recovery endpoint: use physical BOOT/reset
controls to enter BOOTSEL before another load or full-original restoration.

The corrected image links without runtime helpers: flash **659,988 bytes**,
initialized data zero and BSS **437,984 bytes**, using the ordinary flash
linker and scratch-bank stack. Both atlas sanitizer suites pass, including
exact rendering/panel reconstruction and fractional timer-wrap tests.
Warm launch and owner-visible animation are confirmed on **one** badge.

#### BOOT/HOME light sleep, 2026-10-12

The owner selected **HOME to sleep, HOME again to wake, RESET to restart**,
using light sleep rather than clock shutdown. The rear control is the existing
active-low GPIO22 input. [home.c](../../src/platform/badger/home.c) configures
only that pad, with input enable, weak pull-up, isolation removed and output
disabled. It does not touch flash/QSPI, RESET or shared peripheral power.
The existing 20 ms diagnostic debounce filters both sleep and wake gestures.

On a sleep press:

1. Pause the recovery watchdog through its enable-bit clear alias; normal
   foreground recovery must not reboot a deliberately sleeping badge.
2. Turn the backlight off, send display-off/sleep-in and respect the 150 ms
   command delay. GPIO41 stays enabled; this is not a board power cut.
3. Wait for a debounced release, then a new debounced press. While the input
   is stable, CPU0 waits with `WFE`, not a polling/render loop. Both GPIO22
   edges use IO_BANK0/core-0 IRQ21. Its handler acknowledges those edges and
   sends an event, including an edge between the level check and `WFE`.
   Only the continuous application's startup builds this interrupt vector.
4. Disable that wake interrupt, rearm recovery, issue sleep-out and wait
   150 ms. Invalidate display history and redraw the complete frame before
   turning display output/backlight on.

The wake press is already consumed by the debouncer, so holding it does not
immediately put the badge back to sleep. Animation phase and fractional timer
remainder are retained; the timer baseline and frame deadline are reset at
wake. Sleep duration does not cause catch-up frames or a 32-bit timer-wrap
jump. The crystal, timer and peripheral clocks are left running: **no deep
sleep, sleep current or battery-life claim is made**.

Official RP2350 register definitions were checked for GPIO22 edge bits,
core-0 interrupt offsets, IRQ21 and watchdog enable bit 30:
[IO_BANK0](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2350/hardware_regs/include/hardware/regs/io_bank0.h),
[interrupts](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2350/hardware_regs/include/hardware/regs/intctrl.h),
and [watchdog](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2350/hardware_regs/include/hardware/regs/watchdog.h).
These are reference facts, not a target SDK dependency.

`make test-home test-display-timing` passes GPIO input/polarity, edge-mask,
IRQ/event cleanup, light-sleep selection, LCD command order, explicit
backlight-on and timer-wrap delay tests. Existing diagnostic debounce tests
pass. The ARM build has no unresolved runtime helpers; the binary IRQ21 vector
was checked against `bare_gpio_wake_irq`. Current flash is **660,672 bytes**,
BSS **438,032 bytes**, initialized data zero.

The HOME image was serial-bound loaded only onto **`9BBF88DD17D66E7D`**, with
independent full-flash readback: firmware bytes matched and all bytes beyond
its erase sectors matched the original backup. Evidence is retained in
`build/nametag-continuous/deploy-20261012-home-01/`.
UF2 SHA-256:
`c51d98c249d7d95e97a3c1f15e044299147c306d31e1eaa016dee9a2edd438ca`;
ELF SHA-256:
`97dcf51da211ea0e1736275d3345e1fc5ca253b2875e095159bb2f91d9c4ad65`.
The owner confirmed:

- display remained off for at least **35 seconds** without rebooting;
- HOME woke it and animation resumed;
- holding HOME for about two seconds on wake did not immediately resleep;
- RESET alone restarted the nametag.

The primary is intentionally left running. The control remains untouched in
BOOTSEL, and the original backup remains preserved. The owner chose not to
interrupt it for a final BOOT/HOME + RESET retest; that is **not a failed
recovery observation**, but the combination is not newly qualified on this
sleep-enabled image. Physical BOOTSEL recovery itself is not altered by these
firmware controls.

This does not complete the following M3 qualification work:

- Keep rendering logic host-testable; separate hardware entry/transport code.
- Establish startup/error display behavior, static memory budget and bounded
  frame scheduling. Do not rely on PSRAM or inherited panel state.
- Build with LLVM and the owned compiler in separate build directories;
  inspect unresolved helpers and loaded/RAM sizes. Resolve tightly coupled
  compatibility gaps without silently substituting an SDK/libc implementation.

**Exit:** correct cold-boot display on both badges and passing host/build tests.
Initial performance target: 30 presented frames/s for the selected animation,
with p99 render-plus-transfer time <= 25 ms over a ten-minute instrumented run
(8.33 ms nominal scheduling headroom). Record missed deadlines and worst case.
If unmet, continue M4 or explicitly agree a different target; average fps alone
does not pass. Confirm instrumented timings against the ordinary build.

### M4 - Evidence-driven smoothness optimization

Depends on M3 measurements, not a requirement to use both cores.

- First optimize expensive composition and measured useful dirty regions.
- If rendering dominates, evaluate render/display overlap with two buffers
  and core 1. If bus transfer dominates, investigate a separately qualified
  PIO/DMA writer; measure SRAM/XIP contention in either approach.
- Retain a working single-core control. Compare the same frames and workload,
  including profile replacement, induced stalls and full-buffer backpressure.
- Test ownership/order, stack high-water marks, deliberate timeouts, frame
  integrity and recovery on both badges. Recheck TE/scan behavior after changes.
- Consider 60 fps only after the 30 fps contract passes with memory headroom.

**Exit:** measured improvement without corruption, unsafe handoff or changed
animation timing. Keep the simpler path if concurrency adds no material benefit.

### M5 - Wearable acceptance and future-update seam

- Check name readability at an agreed wearing distance, footer dwell times,
  title wrapping, brightness and distraction with the owner.
- Run at least 30 minutes on each badge, ten physical cold boots per badge,
  and host tests for timer wraparound and repeated profile replacement.
  Record faults, missed deadlines and visual issues, not just completion.
- Inject a validated replacement profile through a test provider without a
  new glyph atlas; reject an invalid replacement without modifying the active
  profile. USB/radio implementation remains separate.
- Measure power only if suitable equipment is available. Label battery life
  unmeasured otherwise; do not treat USB current as battery-runtime evidence.
- Update this guide with exact build/install commands, selected design,
  measured results, known limitations and supported characters.
- Restore/hash-verify original flash after experiments and leave devices in
  BOOTSEL unless the owner chooses to retain the final nametag.

**Exit:** accepted appearance and reproducible firmware, with evidence attached
to specific devices/builds and a usable future profile-update boundary.

## Evidence and scope rules

Keep host/model results, configured values, measured physical behavior and
owner visual acceptance separate. Store machine-readable run results locally
under an ignored build directory; summarize non-sensitive results here.
Never publish flash backups, private configuration or success-shaped defaults.

The first delivery does not include a radio stack, arbitrary uploaded fonts,
on-device TTF rasterization, persistent profile storage, PSRAM support or a
general-purpose GUI framework. Revisit these only when a concrete requirement
and measurements justify them.
