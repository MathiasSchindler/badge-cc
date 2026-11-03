# Logitech MX Keypad investigation

First milestone: identify the attached keypad and provide a macOS C probe
before writing any badge USB-host firmware. Default modes are read-only.
The later, explicit `--navigation` mode temporarily changes page-button
reporting, then restores it. `--lcd-test` uploads temporary numbered key images.
The [badge trace firmware](#badge-usb-trace-viewer) provides device-mode replay;
the [experimental native-host firmware](#experimental-native-host-firmware)
reads a directly attached, externally powered keypad without the Mac.
No build or test command flashes the badge.
The separate [Waveshare PIO host](../logi-host/README.md#bounded-keypad-hid-sessions)
now shares the verified descriptor/input helpers and supports bounded
full-speed HID input capture without using the Mac as the peripheral host.
Its explicit [numbered LCD test](../logi-host/README.md#numbered-lcd-test) now
uploads prepared JPEGs via HID SET_REPORT: all nine images/27 fragments and
a 30-second keepalive hold passed. PIO-driven appearance still needs visual
confirmation; navigation diversion on that host remains future work.

## What this device does

This is the **MX Keypad** from the Logitech **MX Creative Console**:

- Nine customizable LCD keys in a 3 x 3 grid, plus two page-navigation buttons
  (the physical arrangement described for this unit).
- The LCD keys associate visual labels/icons with application actions.
  Logitech's normal customization path is Logi Options+ on the computer.
  Application actions and profile/page management should not be assumed to
  execute independently inside the keypad.
- The companion **wireless dialpad** is a different device. Its dial, roller,
  Bluetooth connectivity and batteries are not features of this USB keypad.
- The keypad connects and powers up over USB-C. USB-C describes the connector,
  not the USB data speed or a guarantee of host/dual-role capability.

Manufacturer references:
[product overview](https://www.logitech.com/en-us/shop/p/buy-mx-creative-console)
and [keypad setup](https://www.logitech.com/en-us/setup/mxsetup/creative-console/keypad-setup.html).
They establish the product roles and normal setup, not the vendor report
protocol. Button mappings are physically verified below. LCD geometry and
JPEG framing come from independent protocol investigations and have passed
live transfer tests; the user also confirmed the numbered LCD test worked.
Brightness
commands and standalone page behavior have not been qualified here.

## Build and use on this Mac

```sh
make -C logi-keypad
make -C logi-keypad test
make -C logi-keypad inspect

# From the repository root; the capture ends automatically.
build/logi-keypad/keypad-probe --watch 30

# Opt in to temporary navigation-button reporting; see the explanation below.
build/logi-keypad/keypad-probe --watch 30 --navigation | tee build/logi-keypad/navigation-enabled.txt

# Temporarily replace the LCD icons with numbered test patterns.
build/logi-keypad/keypad-probe --lcd-test 60
# Also enable navigation reporting during the display test.
build/logi-keypad/keypad-probe --lcd-test 60 --navigation

# Select one unit explicitly if several are attached.
build/logi-keypad/keypad-probe --inspect --serial 346F35813233
build/logi-keypad/keypad-probe --watch 30 --serial 346F35813233
```

The [probe](keypad-probe.c) is C11 and uses **only macOS-provided IOKit,
CoreFoundation, CoreGraphics, ImageIO and the system C library**. A compiler
and Apple SDK are required, but no Homebrew packages, hidapi, libusb, Pico SDK, Logitech SDK or
third-party source are needed. This is a **host-only runtime exception**:
it is not freestanding firmware, and none of it is linked into the badge.
Future badge code must retain the project's no-libc/no-external-dependencies
requirements.

Default/`--inspect` mode reads cached registry properties and descriptors
without opening the USB device. `--watch SECONDS` opens only the matching HID
device, non-exclusively, and subscribes to input reports for 1..3600 seconds.
Without `--navigation` or `--lcd-test`, it does not initialize vendor features
or call `SetReport`. Only `--lcd-test` uploads images. No mode requests feature
reports, grabs unrelated keyboards, or synthesizes keystrokes.
The USB configuration is obtained using Apple's documented cached-descriptor
API, not by detaching the HID driver.

Input lines contain relative host time, the callback's report ID, callback
length, and the unmodified bytes supplied by IOKit. The report ID is printed
separately as metadata; **do not prepend or strip a byte based on assumptions
about another HID library's buffer convention**.
The verified single-key event shape also produces a `decoded:` line with
one-based key/row/column values or a release indication. Other reports remain
visible verbatim and are explicitly labeled `unknown`; raw input lines retain
their original format.

Exit codes: `0` for successful inspection/capture, `1` for device/runtime/output
failure, `2` for invalid command-line arguments. A successful capture with zero
reports means only that the subscription ran, not that any button works.
Disconnects fail the capture rather than silently reconnecting to another unit.
Capture is bounded; Ctrl-C can also terminate the process.
With `--navigation` or `--lcd-test`, setup and cleanup add time outside the requested
capture interval. HID++ replies are logged as raw input but explicitly marked
as command replies, not button events; the summary separately counts decoded
LCD-key and navigation state reports.

If opening fails, inspect the printed IOKit error and macOS **Privacy &
Security > Input Monitoring** permissions for the terminal/VS Code process.
Do not grant Accessibility or root access by default. Logitech software might
compete for access or influence the reports; record whether it is running.
If there are no reports while pressing buttons, that does not prove a faulty
device: vendor event subscription/initialization may still be necessary.
For silent navigation buttons specifically, use the opt-in mode below.

## Numbered LCD test

```sh
make -C logi-keypad
build/logi-keypad/keypad-probe --lcd-test 60 --navigation | tee build/logi-keypad/lcd-visible.txt
```

After upload, look for a large white digit, a white border and a colored
checkerboard on each LCD key:

```text
1 2 3
4 5 6
7 8 9
```

Digits are drawn directly in C as seven-segment rectangles, with no fonts,
downloaded assets or third-party image library. Apple's built-in ImageIO
encodes each pattern as an RGB JPEG at quality 0.85. Tests decode every JPEG
back to an image and check its exact **118 x 118** dimensions. The pattern and
packet builders are in [keypad-lcd.h](keypad-lcd.h).

The candidate physical geometry is a shared 480 x 480 panel with nine key
windows: each 118 x 118 image starts at
`x = 23 + column * 158`, `y = 6 + row * 158`, using zero-based row/column.
Check that each digit is upright, inside the intended key, not
cropped, and that all nine images remain visible for the full hold interval.
The host can check bytes and acknowledgments, but cannot read the visible
pixels back from the device.

The test discovers keepalive feature `0x0008` (index `0x04` on this unit),
sends function 1 with a 3000-ms interval, and refreshes it approximately every
second while uploading and holding the images. It does **not** repeatedly
repaint, change brightness, guess a display-claim feature, or write flash.
Input capture remains active. Add `--navigation` for page-button events;
LCD uploads do not enable navigation diversion implicitly.

**Temporary does not mean original-icon backup:** current icons are replaced.
There is no implemented image-readback operation, so the previous icons
cannot be restored exactly. On timeout, Ctrl-C or SIGTERM, the test stops its
keepalive; firmware is expected to reclaim the panel and return to its idle
logo. Options+ can subsequently redraw its own icons. The idle transition
and long-term display ownership still need physical observation on this unit.
Navigation settings, when requested, retain their separate verified
save/restore behavior. No background service remains after the test exits.

### LCD transport

The implementation uses protocol facts from the
[independent macOS investigation](https://github.com/shensquared/mx-creative-console/blob/8c29376f63802bb96fb6de143c9d488d16da5ce1/docs/PROTOCOL.md)
and the
[community image writer](https://github.com/Julusian/node-logitech-mx-creative-console/blob/82e66c7f5571dd2fd569733867fd29ae9823c94d/packages/core/src/services/imageWriter/imageWriter.ts),
not copied third-party implementation code or linked dependencies.

Each output is a zero-padded **4095-byte report `0x14`**:

| Offset | First fragment | Continuation |
| --- | --- | --- |
| 0..3 | `14 ff 02 2b` | Same |
| 4 | Part index OR `0x20`, OR `0x80` if first, OR `0x40` if last | Same flags, advancing index |
| 5..6, 7..8 | Two big-endian `0x0100` fields | JPEG payload begins at 5 |
| 9..16 | Big-endian x, y, width, height | Payload |
| 17 | Zero | Payload |
| 18..19 | Big-endian JPEG length, maximum **65535** | Payload |
| 20 onward | Up to 4075 JPEG bytes | Up to 4090 JPEG bytes starting at offset 5 |

Fragments are serialized, acknowledged and spaced by at least 5 ms.
On this unit, a response is report `0x13`, prefix `13 ff 02 2b`, with the
image status at raw offset 5. Status `00` means accepted; `fb` means busy.
Busy images are retried from their first fragment, at most three attempts.
Other statuses, OS write failures or a missing reply within one second fail
the test explicitly.

The reply's sequence byte is **not a literal echo of output flags**:
the observed request fragment values `a1,22,63` produce replies `c1,c2,c3`.
Match feature/function/software ID while allowing only one outstanding
request; do not reject the first reply because `c1 != a1`. Command replies
are never decoded as key presses.

On the [Waveshare full-speed PIO host](../logi-host/README.md#numbered-lcd-test),
each complete 4095-byte report uses HID SET_REPORT on EP0, with 64-byte data
packets and a final 63-byte packet. Simply streaming the report through its
64-byte interrupt OUT endpoint did not produce a reliable complete upload.
The same project-owned wire builder is shared by the Mac and target tools;
the target embeds JPEGs prepared at build time rather than linking an encoder.

### LCD validation: 2026-10-29

- Normal and address/undefined-behavior sanitizer builds passed rendering,
  exact digit-segment checks, JPEG encode/decode, CLI validation and packet
  reconstruction tests at 1, 4075, 4076, 8165, 8166 and 65535 bytes.
- Live sanitized upload accepted **all nine images and all 27 fragments**:
  three fragments per key, with JPEG sizes ranging from 8308 to 9342 bytes.
  Every fragment returned status `00`; the five-second keepalive hold ended
  normally with `LCD images acknowledged: 9/9`.
- A separate normal-build **60-second** test with `--navigation` also completed
  with all nine images acknowledged and continued keepalive replies. It
  restored both navigation controls to their original `diverted=0` state.
  No physical button presses were supplied for this run.
- An initial experiment incorrectly required the ACK sequence to echo output
  flags and timed out despite receiving `c1`. The matcher was corrected using
  the observed response, and the complete upload then passed.
- The user subsequently confirmed: **"It worked fine."** This qualifies the
  numbered-image test visually, in addition to the recorded acknowledgments.
  Brightness control and exact restoration of the previous icons remain untested.
- ImageIO is host-only. A future freestanding badge implementation must use
  project-owned encoding or prepared JPEG data, not link Apple frameworks.

## Live observations: 2026-10-29

Evidence: the probe above, `ioreg`, and `hidutil list` on this computer
(macOS 27.2, build 26B123). These values describe this unit in its current
high-speed configuration, not every firmware revision or USB speed.

| Property | Observed value |
| --- | --- |
| Manufacturer / product | `Logitech` / `MX Keypad` |
| USB VID:PID | `046d:c354` |
| Serial | `346F35813233` |
| Current location ID | `0x01100000` (topology-dependent, not a persistent identity) |
| Negotiated link speed | **480,000,000 bit/s: USB high speed** |
| USB version / device revision fields | `bcdUSB=0x0200`, `bcdDevice=0x0200` |
| Endpoint 0 maximum packet size | 64 bytes |
| Configurations | One; configuration value 1 |
| Configuration attributes | `0x80`: bus-powered, no remote-wakeup capability advertised |
| Configuration power | `bMaxPower=180`, **360 mA** in USB 2.0 units |
| Interface | Number 0, alternate 0, class `0x03` HID, subclass 0, protocol 0 |
| HID descriptor | HID 1.11, country 0, report descriptor length 140 |
| Primary HID usage | Vendor page `0xff43`, usage `0x1a02` |
| Interrupt IN | Endpoint `0x81`, maximum packet 32 bytes, `bInterval=1` |
| Interrupt OUT | Endpoint `0x01`, maximum packet **1024 bytes**, `bInterval=1` |
| macOS maximum report sizes | Input 32, output 4095, feature 1 byte |

At the observed **high speed**, interrupt `bInterval=1` means one 125-us
microframe, not 1 ms. These are endpoint scheduling capabilities, not measured
event rates. The 360 mA is a declared configuration power requirement
(about 1.8 W at 5 V), **not measured current**, idle consumption or inrush.
The device-revision field is not a verified firmware version string.

The badge was independently visible as `RP2350 Boot`, VID:PID `2e8a:000f`,
serial `C31AAADD5199B2C7`, with the `RP2350` BOOTSEL volume mounted.
It was not opened or written by the probe.

### Cached configuration descriptor

All 41 bytes, grouped at descriptor boundaries:

```text
09 02 29 00 01 01 04 80 b4
09 04 00 00 02 03 00 00 00
09 21 11 01 00 01 22 8c 00
07 05 81 03 20 00 01
07 05 01 03 00 04 01
```

### HID report layout

The descriptor declares these reports. Payload sizes below exclude the
one-byte report ID; the complete HID report includes it.

| ID | Direction | Payload | ID + payload | Declared usage |
| --- | --- | ---: | ---: | --- |
| `0x11` | Input and output | 19 bytes | 20 bytes | Vendor `0xff43:0x0002`, application `0x1a02` |
| `0x13` | Input and output | 31 bytes | 32 bytes | Vendor `0xff43:0x0008`, application `0x1a08`; buffered bytes |
| `0x14` | Output only | 4094 bytes | 4095 bytes | Vendor `0xff43:0x0010`, application `0x1a10`; buffered bytes |
| `0x04` | Input | 1 byte | 2 bytes | Generic Desktop System Control; 2-bit array for Sleep/Power Down/Wake Up, 6 padding bits |
| `0x05` | Input | 1 byte | 2 bytes | Consumer Control `AC Back` (`0x0224`), 7 padding bits |

There is **no Feature main item** in this descriptor. macOS publishing a
maximum feature-report size of 1 does not establish a usable feature command.
Nor do the System Control/AC Back collections establish which physical
buttons, if any, generate those reports.

This is a HID device, but **not a boot-protocol keyboard** and not a standard
keyboard-key bitmap. A generic keyboard host driver will not interpret its
nine display keys. Vendor usage IDs and report sizes do not reveal command
semantics; the `0x14` image-upload protocol was established separately above.
A HID report can span several USB packets;
4095-byte reports do not imply a 4095-byte endpoint packet.

All 140 report-descriptor bytes:

```text
06 43 ff 0a 02 1a a1 01 85 11 95 13 75 08 15 00
26 ff 00 09 02 81 00 09 02 91 00 c0
06 43 ff 0a 08 1a a1 01 85 13 96 1f 00 75 08 15 00
26 ff 00 09 08 82 02 01 09 08 92 02 01 c0
06 43 ff 0a 10 1a a1 01 85 14 96 fe 0f 75 08 15 00
26 ff 00 09 10 92 02 01 c0
05 01 09 80 a1 01 85 04 95 01 75 02 15 01 25 03
09 82 09 81 09 83 81 00 75 06 81 03 c0
05 0c 09 01 a1 01 85 05 95 01 75 01 15 00 25 01
0a 24 02 81 02 95 07 81 03 c0
```

### Verification and present limits

- Normal build: C11, `-Wall -Wextra -Werror`.
- Hardware-independent tests cover duration parsing/bounds/overflow,
  number-property type checks, little-endian decoding, configuration walking,
  malformed descriptor lengths and invalid CLI arguments. Input-decoder tests
  include all 18 reports from the user's capture, opaque-counter wrap,
  unexpected headers/lengths/key values/tail data and exact event-label output.
  Navigation tests cover control lists, releases, command-reply exclusion,
  discovered feature indices, response correlation and HID++ error replies.
- Live descriptor inspection succeeded, including explicit serial selection.
- An absent serial was rejected with exit code 1.
- Non-exclusive live input capture ran for 3 seconds, with **zero reports**;
  no controlled button presses were supplied in that initial test.
- A subsequent user-operated 30-second capture recorded **18 reports: nine
  presses and nine releases**, covering the nine LCD keys in row-major order.
  It contains one complete pass, not a second pass. See the mapping below.
  The probe sent no initialization commands; prior device initialization and
  whether Options+ was running were not recorded.
- AddressSanitizer/UndefinedBehaviorSanitizer builds also completed live
  descriptor inspection and a 1-second idle capture without reported errors.
- The sanitized `--navigation` mode was tested on this unit: feature `0x1b04`
  resolved to index `0x0b`; both controls initially read `diverted=0`, read
  `diverted=1` after enabling, and returned to `diverted=0` after cleanup.
  All eleven request/response exchanges completed successfully. No physical
  navigation presses were supplied during that 1-second validation.
- A separate live capture was interrupted with SIGINT after setup. It exited
  successfully, restoring and reading back both original `diverted=0` states.
- The subsequent user-operated `--navigation` capture verified **all eleven
  physical buttons**: LCD keys 1..9 once, then left/right navigation twice.
  Its 37 input reports comprise 18 LCD-key states, 8 navigation states and
  11 command replies. Both navigation controls were restored to their original
  `diverted=0` state with successful readback.
- Visual LCD appearance/alignment, cold-start initialization, held and simultaneous presses,
  full-speed enumeration using the badge's native controller and badge host operation remain **unverified**.

### Why the navigation buttons were silent

The user's subsequent left/right-navigation capture completed with **zero
raw input reports**. This was not a decoder rejection: the raw callback runs
before button interpretation. Both controls share the existing HID interface;
adding a keyboard/consumer-usage filter would not enable them.

Two independent community implementations describe the missing configuration:
the page buttons are **HID++ reprogrammable controls** and must be temporarily
**diverted to software** before they emit notifications. No Logi Options+
process was found running during this investigation. This is consistent with
the default read-only probe receiving LCD keys but no navigation reports.
The subsequent enabled capture physically confirmed the navigation mapping
and reporting mechanism on this unit.

Protocol references (no third-party code or library is linked into the probe):

- [HID++ feature 0x1b04 specification, version 2](https://lekensteyn.nl/files/logitech/x1b04_specialkeysmsebuttons.html).
- [Independent macOS protocol investigation](https://github.com/shensquared/mx-creative-console/blob/8c29376f63802bb96fb6de143c9d488d16da5ce1/docs/PROTOCOL.md).
- [Keypad initialization in node-logitech-mx-creative-console](https://github.com/Julusian/node-logitech-mx-creative-console/blob/82e66c7f5571dd2fd569733867fd29ae9823c94d/packages/core/src/models/mx-creative-keypad.ts).

Use:

```sh
make -C logi-keypad
build/logi-keypad/keypad-probe --watch 30 --navigation | tee build/logi-keypad/navigation-enabled.txt
```

Wait for `Watching for 30 seconds`, then press/release **left**, then **right**.
The explicit flag permits only these reporting-related operations:

1. Discover the index of feature `0x1b04` via the HID++ root feature. Do not
   assume the index remains `0x0b` across firmware revisions.
2. Read the original temporary-diversion bits for controls `0x01a1` and
   `0x01a2` (physically verified left and right).
3. If needed, set only the temporary-diversion bit and its validity bit
   (`0x03`), leaving persistent diversion, remapping and other flags untouched.
4. Confirm acknowledgments and read back the enabled state before capture.
5. On timeout, partial setup failure, normal exit, Ctrl-C or SIGTERM, attempt
   to restore every possibly changed control to its original diversion bit
   and confirm readback. An originally diverted control is left diverted.

Requests use 20-byte output report `0x11`; successful OS writes alone are not
treated as protocol success. Responses must match device index, discovered
feature, function and nonzero software ID; missing replies and HID++ errors
are reported explicitly. Each reply wait is bounded to one second.

Verified single-button navigation notifications have the complete-report layout
`11 ff <feature-index> 00 <CID-high> <CID-low> ...`, with zero-padded
big-endian control IDs: `0x01a1` for left and `0x01a2` for right. An all-zero
control list is release. The decoder also accepts a list containing both as
both active, based on the community protocol; simultaneous presses have not
yet been physically tested here.
The decoder requires the discovered feature index and distinguishes a
notification's function/software byte `00` from command replies such as
`3d`. Unknown control IDs and malformed lists remain raw/unknown.
The LCD-key decoder is unchanged.

The verified capture's navigation sequence is exactly
`left,release,right,release,left,release,right,release`, from +7.970434 to
+10.630408 seconds. All eight notifications are 20-byte report `0x11`:

```text
Left:
11 ff 0b 00 01 a1 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Right:
11 ff 0b 00 01 a2 00 00 00 00 00 00 00 00 00 00 00 00 00 00
Release:
11 ff 0b 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Thus all eleven button inputs work in the same capture without suppressing
LCD-key reports. This does not yet qualify held-key repeats, simultaneous
presses, cold-start operation or behavior at another USB speed.

**Recovery limits:** SIGKILL, process crashes, host shutdown and physical
disconnect cannot guarantee restoration. If restoration fails, the tool
returns an error and asks for an unplug/replug to clear temporary diversion.
Avoid running Options+ or another configuration writer concurrently: they
can race these temporary settings. This mode does not change flash, upload
images, initialize keep-alive or install Logitech software.

### Verified LCD-key input mapping

The user pressed the keys from upper-left to lower-right as requested. The
saved capture starts with key 1 at +1.322719 seconds and ends with key 9's
release at +5.083805 seconds. All events are report ID `0x13`, callback length
32. For **this macOS callback**, the raw buffer includes the report ID at
offset 0.

```text
Physical layout / observed key value:

  1  2  3
  4  5  6
  7  8  9

First press:
13 ff 02 00 c7 01 01 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

First release:
13 ff 02 00 c8 01 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Offsets in the complete 32-byte raw report:

| Offset | Observed value | Interpretation / limit |
| --- | --- | --- |
| 0 | `13` | HID report ID |
| 1..3 | `ff 02 00` | Constant prefix in this event shape; semantics not established |
| 4 | `c7,c8,...,cf,c0,...,c8` | Low nibble increments modulo 16 in this capture; treated as opaque, not as the key |
| 5 | `01` | Constant in the captured single-key events; meaning not established |
| 6 | `01`..`09` on press, `00` on release | One-based key value; not a bitmask |
| 7..31 | All zero | No other nonzero data in this capture |

The byte-6 sequence is exactly
`1,0,2,0,3,0,4,0,5,0,6,0,7,0,8,0,9,0`. These values are key identifiers:
for example, `3` means the upper-right key, **not** keys 1 and 2 together.
The release reports do not identify the released key; its identity can only
be inferred from preceding events under the tested one-key-at-a-time
procedure. The decoder deliberately does not fabricate a released-key number.

Recognition is conservative: exact 32-byte length, report ID/prefix, byte 5
equal to 1, key value 0..9 and zero tail. Different shapes, including possible
multi-key or navigation events, are labeled unknown rather than interpreted
using an unverified protocol. Byte 4 is not used to reject or deduplicate
events because its meaning has not been established.

## Can the badge's USB-C port become a host?

**The RP2350 controller supports host mode. The assembled badge is not a
software-switchable USB-C host port as documented.** There are independent
data, connector-role, and power requirements.

### Controller and speed

The [RP2350 datasheet](../pimoroni/RP-008373-DS-3-rp2350-datasheet.pdf),
section 12.7.1 (printed page 1058 / PDF page 1059), explicitly permits host
communication with **low-speed (1.5 Mb/s) and full-speed (12 Mb/s)** devices.
It cannot operate at the keypad's currently observed 480 Mb/s high speed.

The keypad's **full-speed configuration is now advertised and recorded below;
actual operation at full speed remains untested**. The high-speed 1024-byte OUT endpoint cannot simply be
copied into a full-speed host implementation; full-speed interrupt endpoints
allow at most 64 bytes per packet. USB 2.0/bcdUSB alone does not demonstrate
that this unit's required functions work at that speed.

A high-speed hub does **not** turn the RP2350 into a high-speed host. Physically
qualify full-speed input and output on a known full-speed-only USB path.
Failure would require a separate suitable USB
host controller or a different device/architecture, not merely a firmware
register change.

#### Standard other-speed descriptor read

```sh
make -C logi-keypad
build/logi-keypad/keypad-probe --other-speed --serial 346F35813233
```

This explicit mode opens the USB device non-exclusively and issues only standard
`GET_DESCRIPTOR` requests for device qualifier (type 6) and other-speed
configuration (type 7). It does not reset, reconfigure, seize or force another
link speed. The existing HID driver remains attached.

On the attached **high-speed** keypad, the qualifier was:

```text
0a 06 00 02 00 00 00 40 01 00
```

The other-speed configuration (therefore **full speed** in this observation) was:

```text
09 07 29 00 01 01 04 80 b4
09 04 00 00 02 03 00 00 00
09 21 11 01 00 01 22 8c 00
07 05 81 03 20 00 05
07 05 01 03 40 00 05
```

It advertises interrupt IN `0x81`, **32 bytes**, interrupt OUT `0x01`,
**64 bytes**, both at **5 ms** intervals, and the same **360 mA** declared
power budget. This is positive descriptor evidence, not proof of working input,
4095-byte HID output-report assembly or LCD transfers at 12 Mb/s.
At full speed, a 4095-byte HID report requires assembly across USB packets.
The RP2350 hardware, CC and VBUS gates still apply.
The later [direct Waveshare qualification](../logi-host/README.md#pio-lcd-qualification)
separately established full-speed HID input transport and acknowledged LCD
uploads over HID SET_REPORT; it does not qualify the badge's USB-C host wiring.

### USB-C connector role and 5 V power

The [manufacturer schematic](../pimoroni/github_badge_2026_schematic.pdf)
provides stronger evidence than the connector shape:

- **Sheet 2:** J1 D+/D- connect to the RP2350 USB pins through 27.4-ohm R9/R10.
  However, CC1 and CC2 have fixed **5.1-kilohm pull-downs** R2/R3 to ground:
  the connector advertises a **sink**, not a source or software-controlled
  dual-role port. Those resistors are not changed by the USB host-mode bit.
- **Sheet 4:** VBUS feeds board power through D5 and supplies the battery
  charger. The battery/VBUS selection circuit is not a 5 V USB output.
  No battery-to-5-V boost converter or controlled VBUS source is shown.
- **Sheet 4:** GPIO41 / `SW_POWER_EN` controls U9's **3.3 V peripheral rail**.
  It is not a USB VBUS switch. GPIO12 is only VBUS detection through a divider.

Consequently, a normal C-to-C cable and a host-mode register write are not
enough. A future hardware setup needs an electrically reviewed host
connector/CC arrangement and a regulated, current-limited, reverse-current-
protected 5 V source with sufficient keypad and board power budget.
Do not connect two VBUS sources, inject 5 V into GPIO/3.3 V, drive the keypad
from the raw LiPo, or assume a powered hub safely back-powers its upstream
port. A powered hub can help peripheral power only in a correctly designed
host topology; it does not fix the badge's CC wiring by itself.
Battery runtime and thermal/current limits have not been qualified.

### Device versus native-host firmware

Existing [USB firmware](../src/usb/live_usb_hw.c) implements **device mode**.
BOOTSEL also makes the badge a USB device; it is not an application host
stack. A no-SDK/no-libc host implementation needs bus reset, enumeration,
endpoint-0 control transfers, address/configuration assignment, HID descriptor
handling, interrupt polling, report assembly, timeouts and detach recovery.
Start with direct attachment and known descriptors; hub support is separate
work. The same native controller cannot remain the Mac's USB device while
also hosting the keypad; plan a separate debugging/recovery connection.
BOOTSEL remains a recovery mode after reconnecting to the Mac.
The experimental native-host build below implements a restricted direct-device
path. It does not remove any of the electrical or physical-qualification gates.

## Badge USB trace viewer

This first badge utility deliberately uses **USB device mode**, not host mode.
The Mac replays previously recorded raw keypad reports over the badge's existing
USB-C connection. The TFT shows the event number, capture timestamp, decoded
LCD/navigation event, raw bytes and history counters. The same conservative
[input decoder](keypad-input.h) is used by the Mac and badge. Unknown reports
and command replies remain in the history rather than being dropped.

**The user has flashed and visually checked the trace viewer.** This test needs
no electrical changes. The keypad cannot yet plug directly into the badge.
This milestone exercises the decoding,
logging, TFT and USB-debugging path without attempting to source VBUS or
changing the badge's USB-C connector role. It does not initialize Wi-Fi/radio
hardware or upload keypad images.

### Build and offline validation

From the repository root:

```sh
make -C logi-keypad firmware
make -C logi-keypad test-firmware

build/logi-keypad/firmware/keypad-replay --validate build/logi-keypad/capture.txt
build/logi-keypad/firmware/keypad-replay --validate build/logi-keypad/navigation-enabled.txt
```

Outputs:

- `build/logi-keypad/firmware/keypad-trace.uf2`: RP2350 ARM firmware.
- `build/logi-keypad/firmware/keypad-trace.elf`: ELF and symbols for debugging.
- `build/logi-keypad/firmware/keypad-replay`: native Mac replay/validation tool.

The firmware is project-owned freestanding C, built with `-nostdinc`,
`-ffreestanding`, `-fno-builtin` and `-nostdlib`; the linker check requires
**zero undefined symbols**. No Pico SDK, TinyUSB, target libc, external JPEG
library or radio blob is used. It reuses the repository's USB-device controller,
startup, clock, ROM-recovery, display and graphics code. Building requires the
existing Clang/LLVM cross-compilation tools; the Mac sender uses Apple's built-in
IOKit/CoreFoundation and system libc, not libusb.

With the matching UF2 running and the badge connected to the Mac, replay with:

```sh
build/logi-keypad/firmware/keypad-replay \
  --replay build/logi-keypad/navigation-enabled.txt --badge C31AAADD5199B2C7
```

The sender requires an exact serial match and the matching firmware build ID
before writing. It **clears previous badge history**, validates the complete
capture before opening USB, sends records immediately, and checks badge-side
processed counts after each record. Original timestamps are retained, but
original time delays are not reproduced. Failure, disconnect, malformed data
or a mismatched firmware build is an error, not reported as successful delivery.
Readback confirms processing, **not physical TFT pixels**.

Keep the capture's `HID++ feature 0x1b04 discovered at index ...` line when
replaying navigation traffic. That supplies the observed feature index; it is
not guessed from unrelated command responses. Missing metadata leaves those
reports raw/unknown. The parser accepts raw reports of 1..32 bytes, bounds
captures to one million reports, and rejects malformed or oversized input.

### Badge controls and history

| Badge control | Function |
| --- | --- |
| Left / right | Browse older / newer records and hold selection |
| Enter | Toggle hold / follow latest |
| Up | Clear history, counters and current selection |
| Down | Resume following the latest record |
| Home | Disconnect USB and return to BOOTSEL |

The ring retains **64** raw records. Total and unknown counters cover the whole
replay, while the overwritten counter identifies records no longer retained.
Holding freezes the **view**, not recording: if the selected record is
overwritten, selection advances to the oldest retained record. Avoid clearing
the badge during replay; sequence/count checks will reject the interrupted
session. A subsequent replay starts with an explicit clear.

The startup arms an eight-second BOOTSEL recovery watchdog before changing
clocks. The main loop renews recovery every half second, including when
disconnected, so history remains viewable. A stalled firmware stops renewing
recovery; Home and USB transfer failures also request BOOTSEL. Initialization
failures before TFT setup may only expose the debugger error symbol
`keypad_trace_error`; the physical BOOT button remains the fallback.

### Debug transport, version 1

Experimental lab identity: VID:PID **`cafe:4026`**, product **`U26 Keypad Trace`**.
It has one vendor interface and endpoint 0 only, not HID or a serial console.
Other badge experiments reuse these lab IDs, so the sender also verifies the
trace protocol, CRC and exact build ID. All vendor requests use `wValue=0`,
`wIndex=0`, `wLength=64`:

- `0x40 / 0x70`: OUT, submit one record or clear.
- `0xc0 / 0x71`: IN, get status.

Both packets are exactly 64 bytes. Multi-byte integers are little-endian;
bytes 60..63 contain the project's CRC-32 of bytes 0..59.

| Record bytes | Meaning |
| --- | --- |
| 0..3 | ASCII `KTR1` |
| 4, 5 | Raw byte length (1..32), report ID (must equal raw byte 0) |
| 6, 7 | Command 1 = record; source 1 = recorded replay, 2 = native-host capture |
| 8..11 | Capture timestamp in milliseconds |
| 12..15 | Sequence, starting at 1 after clear |
| 16..47 | Full raw report, including report ID; unused bytes zero |
| 48 | Discovered navigation feature index, or zero if unavailable |
| 49..59 | Reserved, zero |

Clear uses command 2 at byte 6, with bytes 4..59 otherwise zero.
Source 2 is used internally by the native-host ring; the device-mode USB
submission endpoint still accepts source 1 only.
Malformed submissions stall EP0 and increment its rejection counter.
Records become available to the application **only after the USB status ACK**;
an interrupted transfer cannot inject a partially committed event. A one-record
queue prevents overwriting an unconsumed committed record. Bus reset and
configuration changes cancel pending transfers, not the retained history.
Sequence errors are reported in status. Successful status polling may initially
see the previous processed count; the sender waits for the application readback.

Status starts with ASCII `KTS1`; successive 32-bit fields at offsets
4, 8, 12, 16, 20, 24, 28, 32 and 36 are total/last sequence, retained,
overwritten, unknown, selected sequence, hold flag, application error
(0 = none, 1 = malformed record, 2 = sequence), build ID and EP0 rejections.
Bytes 40..59 are zero.

### Qualification so far

- Sanitized tests cover shared decoding, malformed records/CRC, exact ring
  order after overwrite, browsing limits, hold and clear.
- Tests exercise the actual USB control state machine: delayed commit,
  aborted/short transfers, queue backpressure, reset and deconfiguration.
- A mocked native-USB sender round trip through that same state machine
  checks status polling, clearing, build/CRC rejection and transport errors.
- The saved LCD-button capture validates **18/18** decoded records.
  The all-button capture validates **37** raw records: **26** button records
  and **11** command/unknown records. The saved LCD upload log retains all
  **36** command/unknown reports.
- Initial ELF footprint: **10,060 bytes** code/constant data, **159,604 bytes**
  zero-initialized RAM (mostly the TFT framebuffer), no initialized data and
  no undefined symbols. The UF2 uses RP2350 ARM family **`0xe48bff59`**.
- **Confirmed on badge hardware, 2026-10-29:** after the user flashed the UF2,
  the Mac sender received acknowledgment for all 37 records. The user read
  `TOTAL 37`, `UNKNOWN 11`, `OVERWRITTEN 0`, `FOLLOW`, and event 37 at
  **30553 ms** on the TFT. Browsing showed events 1..7 as unknown/command,
  event 8 as LCD key 1, and event 9 as an LCD release without encoded identity,
  followed by the subsequent key events. These match the saved capture.
  The 11 non-button records are seven discovery/diversion setup replies
  (events 1..7) and four restoration/readback replies (events 34..37), not
  eleven failed button decodes. Navigation notifications occupy events 26..33.
- **Still unqualified on badge hardware:** navigation-event text, hold/follow
  toggling, clear, overwrite under load and watchdog/BOOTSEL recovery.
  Native hosting is a separate experimental build, described below.

## Experimental native-host firmware

This is a **separate, unflashed and physically unqualified UF2**, not a new
mode of the Mac replay firmware. It runs the RP2350 USB controller as a
full-speed host and reads the keypad directly. No Mac connection is used
while hosting. It is a first diagnostic, not a generic HID stack.

### Connection prerequisite: firmware cannot supply USB power

**Do not expect a normal C-to-C cable to work.** Both the badge and keypad
are USB-C sinks, and the badge has no USB 5 V output. Software cannot change
the CC resistors or create the missing supply.

A physical test requires an **electrically reviewed, externally powered,
direct USB 2.0 data connection** with the correct connector/CC arrangement,
regulated/current-limited 5 V and reverse-current protection. It must supply
the keypad's declared 360 mA budget **plus** badge/charging power as applicable.
The keypad needs valid VBUS even if the badge runs on its battery. No wiring
changes or power injection were performed during this implementation.

An ordinary powered hub is **not** a substitute in this build: hub enumeration
and downstream-port management are deliberately unsupported. Do not rely on
upstream back-powering, connect two VBUS sources, use the battery as USB VBUS,
or connect an improvised power-injection cable without reviewing its schematic.
USB-C adapter shape alone does not establish a safe topology.

The firmware:

- Starts **unarmed**. It does not start host transactions on boot.
- Requires **Enter** to arm, and checks GPIO12 for external VBUS first.
  That check detects VBUS presence only; **it does not prove correct voltage,
  available current, isolation, USB-C role or adapter safety**. A Mac can also
  make this input high, so disconnect the Mac before arming.
- Never enables USB VBUS output or the device pull-up, and does not configure
  any GPIO as a USB power-source switch.
- Waits for a stable full-speed device before bus reset or SOF generation.
  An unpowered keypad or incorrect connection normally leaves it waiting.
- Stops on loss of VBUS/device, unsupported speed/device/descriptors, stalls
  or transfer errors. It exposes the error code, SIE status and buffer word
  rather than silently treating errors as no input.

### Build

```sh
make -C logi-keypad host-firmware
make -C logi-keypad test-host-firmware
```

Outputs:

- `build/logi-keypad/host-firmware/keypad-host.uf2`
- `build/logi-keypad/host-firmware/keypad-host.elf`
- `build/logi-keypad/host-firmware/keypad-host.map`

No automatic flash target is provided. Keep the known-working replay UF2
available for recovery. After a hardware setup is reviewed, manually flash
the **host** UF2, disconnect the Mac, establish that setup and press Enter.
It should progress through bus reset, device/address/configuration reads,
HID descriptor verification and `LIVE LCD KEYS`.

### Scope and badge controls

- One **directly connected, full-speed** `046d:c354` keypad.
- Bounded EP0 transfers with 8/16/32/64-byte packets, address assignment,
  full configuration parsing and exact verification of the **observed
  140-byte HID report descriptor**. Changed descriptors are rejected explicitly.
- Software polling of the advertised interrupt IN endpoint and its interval.
  NAK means normal idle, not an error. NAKs and errors never advance data PID.
  Polling is also serviced between TFT columns; this remains a polling
  diagnostic, not a qualified low-latency input implementation.
- Raw reports, conservative LCD/navigation decoding, boot-relative
  millisecond timestamps, 64-record history, overwrite/unknown counters,
  controller error words and a normal-idle NAK counter.
- **LCD keys work without enabling navigation diversion.** While live,
  Enter discovers feature `0x1b04`, reads both original navigation settings,
  enables temporary diversion only where needed, and verifies ACK/readback.
  Press Enter again to restore the original settings. Preexisting diversion
  is preserved rather than forcibly disabled.
- Home attempts restoration before BOOTSEL. Transfers that may have changed a
  setting are tracked before sending. Disconnect, watchdog reset or failed
  cleanup cannot guarantee restoration; if warned, **power-cycle the keypad**
  before retrying. Original settings are not retained across badge reboot.

| Badge control | Native-host function |
| --- | --- |
| Enter, unarmed/error | Arm or retry; requires external VBUS |
| Enter, live | Enable navigation / restore original navigation settings |
| Left / right | Browse history and hold selection |
| Down | Toggle hold / follow latest |
| Up | Clear recorded history/counters; USB remains configured |
| Home | Attempt navigation restoration, stop host, return to BOOTSEL |

Wait for `LIVE LCD KEYS` before pressing keypad buttons. First test keys 1..9,
then opt into navigation with Enter and test left/right. History holding does
not stop input capture. An error stops the session; retry requires Enter,
not an automatic loop that repeatedly resets an unknown device.

The eight-second BOOTSEL recovery watchdog is renewed every half second,
including while waiting or making transfers. The native application does
not enumerate as a Mac USB device. Use Home (or the physical BOOT recovery
procedure), then reconnect to the Mac for another flash.

### Implementation and evidence

The project-owned [host driver](host-firmware/usb_host.c) is based on the
RP2350 datasheet, especially sections **12.7.3.7** (DPRAM/buffer ownership),
**12.7.3.9** (host transactions) and the USB register tables. EPx buffer control
is at DPRAM `0x80`, EPx control at `0x100`, and packet data at `0x180`
(table 1303). Setup is sent separately from the data and status stages.
Buffer-available and start writes are separated from other fields by at least
one timer microsecond, exceeding the documented two-USB-clock requirement.
Reset waits 50 ms, then enables SOFs and allows 10 ms recovery.

RP2350's stop-on-NAK facility prevents an idle interrupt endpoint from
monopolizing the controller. Transfers have timeouts and cancellation.
A failed controller stop poisons further transfers until the next hardware
reset; rearming resets USB clocks/controller through the shared clock helper.
The device-mode clock sequence is reused without enabling a device pull-up.
No TinyUSB, Pico SDK, target libc, external implementation code or radio
blob is linked.

Verified before handing over the UF2:

- Sanitized modeled-controller/device tests cover register-write ordering,
  **no VBUS output**, EP0 packet sizes, short packets, address/configuration
  commit timing, full-speed descriptors, unsupported hubs/devices/speeds,
  NAK/stall/CRC/timeout/cancellation/detach and stuck-stop handling.
- The modeled keypad produces **37** reports with **26** button records and
  **11** command records. Navigation tests preserve preexisting diversion,
  restore after an acknowledged write with a missing reply, and expose
  readback/restoration failures.
- Headless tests execute the actual application step and TFT renderer:
  no automatic arming, rejection without VBUS, VBUS-loss shutdown,
  history/buttons, text bounds and framebuffer ordering, Home recovery,
  and visible watchdog-renewal failure.
- Both the native descriptor check and test fixture match **all 140 bytes**
  read from the keypad attached to this Mac.
- Strict freestanding build, zero undefined symbols; current footprint is
  **13,096 bytes** code/constant data, **4 bytes** initialized data and
  **158,544 bytes** zero-initialized RAM. UF2 family is `0xe48bff59`.

**Not yet verified:** any real RP2350 host transaction, keypad enumeration at
12 Mb/s, live key reports, navigation restoration or host-mode recovery on
the badge. Register/device models cannot substitute for that physical test.
This firmware does **not** support hubs, low/high-speed devices, Bluetooth,
Wi-Fi, keypad image uploads, brightness changes or simultaneous Mac USB debug.

## Next milestones

1. **Extend input qualification.** All eleven buttons are mapped above.
   Separately test held keys, simultaneous LCD keys, both navigation buttons
   together and mixed LCD/navigation presses, using `--navigation`. Record
   the order and whether Options+ is running. Preserve raw bytes
   for new event shapes before extending the decoder.
2. **Finish physical trace-viewer qualification.** Replay counters, LCD-key
   decoding, TFT text and history browsing are confirmed. Check navigation
   events 26..33, hold/follow, clear, overwrite and BOOTSEL recovery.
   The Mac LCD test is already visually confirmed.
3. **Review external power/CC hardware before connecting the native host.**
   This remains a physical-test go/no-go gate; firmware does not solve it.
4. **Physically qualify the native-host prototype.** Record enumeration
   progress/errors, full-speed input, nine LCD keys, navigation and cleanup.
   Add keypad LCD updates only after the simpler path is stable.
