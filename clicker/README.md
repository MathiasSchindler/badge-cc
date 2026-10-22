# Universe presentation clicker

Freestanding C for the 2025 and 2026 RP2350B badges, without target libraries,
libc, Pico SDK or BTstack. This is an opt-in application: existing firmware
targets are unchanged.

**Current milestone: controller HCI and non-connectable BLE advertising
physically verified on the 2025 badge. HID, pairing and bonds are not yet
implemented.** There are three isolated applications:

| App | Behavior |
| --- | --- |
| `input` (default) | Display/button diagnostic; no radio initialization |
| `controller` | Load vendor firmware/patch, perform five HCI queries, power radio off |
| `advertise` | Same queries, bounded non-connectable advertisement, disable advertising and power radio off |

None sends keyboard input to a computer. The local keyboard reports in the
input app exercise the intended HID pipeline, not an actual HID connection.

## Build and tests

From the repository root on Windows:

```powershell
python clicker\build.py --test --board 2025
python clicker\build.py --board 2026
# Explicit, hash-pinned download; ordinary builds never fetch from the network:
python clicker\vendor_assets.py --fetch
python clicker\build.py --board 2025 --app controller
python clicker\build.py --board 2025 --app advertise
```

Host requirements: Python standard library and LLVM (`clang`, `ld.lld`,
`llvm-objcopy`, `llvm-nm`); `cc` builds the host tests and existing `bin2uf2`
utility. No package installation is required. Native C tests can use
`--cc <compiler>`; firmware compilation can use `--target-cc <clang>`.

The input image is `build\clicker\2025\clicker-diagnostic.uf2`.
The other images are `build\clicker\2025\controller\clicker-controller.uf2`
and `build\clicker\2025\advertise\clicker-advertise.uf2`, with corresponding
`2026` paths. These are different images, not automatic board detection.
The builder rejects undefined symbols, checks the Arm Secure boot-block loop,
and checks every UF2 block and its reconstructed firmware payload.

## Controls and wiring

| Position | 2025 button / GPIO | 2026 button / GPIO | Action |
| --- | --- | --- | --- |
| Bottom left | A / 7 | Left / 7 | Previous slide: Page Up, usage `0x4b` |
| Bottom middle | B / 8 | Enter / 10 | Blank screen: B, usage `0x05` |
| Bottom right | C / 9 | Right / 9 | Next slide: Page Down, usage `0x4e` |
| Screen side, upper | Up / 10 | Up / 11 | Previous slide |
| Screen side, lower | Down / 6 | Down / 6 | Next slide |

The 2026 assignments come from the project's
[confirmed button mapping](../docs/button-mapper.md).
Do not substitute the retail Tufty 2350 profile for the 2025 conference badge.
The conference profile is corroborated by BadgeBoy's
[device pin dump](https://github.com/anxkhn/badgeboy/blob/7dbf4dcc418a12a4f51a7b20f06385d09444ecf3/docs/HARDWARE.md).
That source identifies 2025 PSRAM chip select as GPIO47, unlike GPIO8 on the
2026 board; this diagnostic never initializes PSRAM.

Only the five selected button inputs are configured, with active-low pull-ups
and output drivers disabled. RESET, HOME, radio, flash and PSRAM interfaces
are not repurposed. The existing shared parallel display driver is reused.

## Input/report contract

- Debounce each button independently for 10,000 timer ticks. This is nominally
  10 ms with the inherited 1 MHz timer, not a measured timebase guarantee.
- Require a stable release before arming each button after startup/connection.
- Emit one keyboard tap per debounced press, not auto-repeat while held.
- Each accepted press report is followed by an all-zero release report,
  before the next queued press, even when the physical button remains held.
- The eight-byte report contains modifiers, reserved byte and six key slots.
  No Report ID is prepended; a future GATT Report characteristic identifies it.
- A busy transport must retry `clicker_input_peek` and call
  `clicker_input_accept` only after accepting that report.
- Disconnect discards unsent clicks; reconnect starts with an all-up report.
  Clicks made offline are not replayed into a newly connected computer.
- A bounded queue holds 16 taps. Overflow latches an explicit error, clears
  queued actions and schedules all-up; further presses are disabled until
  RESET rather than silently losing input or a release.

The diagnostic consumes reports locally at a deliberately slow cadence so
press/release messages can be seen. It is not a Bluetooth latency benchmark.
Very rapid input can intentionally trigger the visible overflow error.
Screen highlighting follows the physical debounced button state, independently
of the short virtual keyboard tap.

## Screen

The original `clicker-load.png` is preserved. Its aspect ratio is exactly 4:3;
`clicker-load-320x240.png` is a Lanczos resize without cropping or stretching.
The build uses a standard-library PNG decoder and converts it to 76,800
column-major RGB565 pixels for the shared display driver. No PNG decoder or
image-processing library is linked into the badge.

RESET displays the entire artwork for 750,000 inherited timer ticks, then
overlays the lower part with explicit diagnostic status and button/report
feedback. The artwork's loading bar is static artwork, not measured progress.

## Hardware test and recovery

The owner authorized backing up and installing this diagnostic on the attached
2025 badge, serial `55F883B4BD0EB5CE`, on 2026-10-23. It reports RP2350 A4,
QFN80, 16 MiB flash and Arm Secure with secure boot disabled.

A full 16 MiB backup was saved and verified against the device before the
first flash:

```text
build\clicker\backups\55F883B4BD0EB5CE\before-clicker-20261023.bin
SHA256 8852f552b3962d7705e5b2921415285f8405485b580f0cb08ec843345d0d817d
```

Keep that ignored backup; it is not a generated build input. The official
Windows picotool 2.3.1 archive was checked against its release SHA256.
With the selected badge in BOOTSEL, recovery is:

```powershell
$p = ".\build\clicker\tools\picotool\picotool\picotool.exe"
& $p load -v "build\clicker\backups\55F883B4BD0EB5CE\before-clicker-20261023.bin" --ser 55F883B4BD0EB5CE
# Reboot only after the load and verification succeed.
& $p reboot --ser 55F883B4BD0EB5CE
```

The first diagnostic was flashed/readback-verified and the owner confirmed
correct artwork/display. It used the approved retail profile, which
physical testing disproved: A and Down matched, physical C
activated logical B, physical Up activated logical C, and physical B was
not detected. These observations establish GPIO7/9/10/6 for A/C/Up/Down,
respectively. The corrected conference profile adds GPIO8 for B using the
independent device dump above.

The corrected 2025 image was then rebuilt, flashed/readback-verified and
rebooted on the same selected badge. The owner confirmed:

- All five controls match their physical labels and intended actions.
- Holding a front button during RESET produces no unsolicited press;
  releasing and pressing it again works.

Host tests passed for both board profiles: all five key usages, exact
debounce threshold, startup-held suppression, press/release ordering under
backpressure, no held-key repeat, reconnect flushing, timestamp wrap and
overflow recovery. PNG tests cover all five filters and malformed inputs.
An independent Pillow comparison also checked the actual PNG decoder output
and all 76,800 packed RGB565 pixels against the image.

Both target builds pass without undefined symbols. The 2025 firmware is
156,660 bytes, and the 2026 firmware is 156,672 bytes. Neither the 2026 image
nor Bluetooth has physical qualification from this **input** diagnostic.
The separate controller/advertising results follow below. Local report tests
do not establish actual computer-side key-release delivery.

## Vendor Bluetooth assets and ownership

The radio apps reuse the project-owned gSPI/PIO driver and retained firmware
loader, with bounded backplane access sharing the loader's window state.
No vendor RP2350 driver, Pico SDK or BTstack code is compiled into the target.
The controller runs the explicitly allowed proprietary vendor binaries;
this is not a source-owned Bluetooth controller or a blob-free build.

[vendor-assets.json](vendor-assets.json) pins every original input by Git blob
identity and immutable upstream commit. Downloads go into ignored
`vendor\clicker-cyw43`, retaining the original notices and license files.
The combined Wi-Fi/Bluetooth header and patch come from cyw43-driver commit
`f6d06a70f9710f5854808a8a2803e93700e6038a`. The existing board NVRAM comes from
`cf924bb04c8984675ca0fc2178f082e404e048c3`.

| Original header | SHA256 |
| --- | --- |
| `wb43439A0_7_95_49_00_combined.h` | `6b4b9a717c3c5d669b56a3bc38b58df3901f63e3a7363ccf5d74706460762176` |
| `cyw43_btfw_43439.h` | `11fc817e33a875e4f619c6939f934e916be0dfa083ce4af073867f340779a6c9` |
| `wifi_nvram_43439.h` | `ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad` |

The host-only C packer includes only data headers, checks container lengths
and padding, and extracts separate binaries. Each build records source and
payload SHA256 hashes, lengths and CRC32 values in `vendor-provenance.json`.
Target startup validates payload CRCs before downloading. Payloads are:

| Payload | Padded bytes | CRC32 |
| --- | ---: | --- |
| Combined controller RAM firmware | 231,080 | `4a948da3` |
| Board NVRAM | 744 | `fa352d20` |
| CLM | 984 | `29aa78fd` |
| Bluetooth patch container | 6,164 | `b443f51f` |

This does not modify or relax the existing
[Wi-Fi-only asset contract](../docs/wifi-assets.md). Generated vendor binaries
are not committed. Retaining notices does not itself grant redistribution
rights in proprietary firmware: the same distribution-review boundary applies.

The shared-memory protocol was checked against Raspberry Pi SDK 2.3.0's
[bus implementation](https://github.com/raspberrypi/pico-sdk/tree/2.3.0/src/rp2_common/pico_cyw43_driver/cybt_shared_bus)
and the pinned vendor firmware format description. The new host implementation
uses fixed buffers, validates patch records before any patch write, preserves
unaligned neighboring patch bytes, checks ring indices/framing, handles wrap
and backpressure, and bounds all waits. Only immediate HCI Command Complete
operations are implemented; asynchronous connection/ACL host procedures remain
future work.

## Controller hardware result, 2026-10-23

The separate controller image was flashed and readback-verified on
`55F883B4BD0EB5CE`. The owner reported `HCI VERIFIED - RADIO NOW OFF` and:

| Field | Observed value |
| --- | --- |
| HCI version | `0x0b` (Bluetooth 5.2) |
| Manufacturer | `0x0131` |
| LE ACL buffers | 251 bytes, 8 packets |
| Commands / received events | 5 / 5 |
| Final opcode / status | `0x2003` / `0x00` |
| Public Bluetooth address | `28:CD:C1:18:0C:60` |

The five successful commands are Reset (`0x0c03`), Read Local Version
(`0x1001`), Read BD_ADDR (`0x1009`), LE Read Buffer Size (`0x2002`) and
LE Read Local Supported Features (`0x2003`). These are actual controller
responses, not fabricated test values. The controller image never enables
advertising. It powers the radio down before leaving its result screen.

## Bounded advertising and Windows observation

The advertising image requests `ADV_NONCONN_IND`, all three primary channels,
an interval of 160 units (100 ms requested), the verified public address,
flags `0x06`, and complete local name `Universe Clicker`. It makes **no HID
service advertisement**, accepts no connections and performs no pairing.
It rejects zero, all-ones, multicast and known vendor-fallback addresses.

The window is bounded by **25,000,000 inherited timer ticks**, not a calibrated
25 seconds. An early test label said `25S`; this was corrected rather than
claiming an unmeasured timebase. On completion or B cancellation it sends
LE Set Advertising Enable=0 and powers down the radio. A failed disable is
reported as a failure and still followed by radio power-off. Terminal success
shows `ADVERTISING DONE - RADIO NOW OFF`, TX/RX `00090009`, and final
opcode/status `200A0000`. B then requests BOOTSEL; RESET reruns the app.

On the first advertising run the built-in Windows Bluetooth stack captured
48 matching packets at `28:CD:C1:18:0C:60`, with the correct name and
`NonConnectableUndirected` type. The owner also confirmed the terminal
advertising-done/radio-off screen and B return to BOOTSEL. This is positive
over-the-air advertising evidence, not HID or pairing qualification, a measured
100 ms transmit interval, or an RF-silence measurement after shutdown.

The final rebuilt image was flashed/readback-verified and scanned again.
Windows captured **86** matching advertisements at the same address and with
the same name/type. The recorded reboot was
`2026-10-23T00:46:19.4317143+02:00`; the scan ended at
`2026-10-23T00:47:04.5537332+02:00`. The UF2 SHA256 was
`7bb74beb9a872079270b6a810c5861bf21bf3309eb8b031bc8676894c196af47`.
The owner then confirmed the final terminal screen, TX/RX `00090009` and
CMD/STAT `200A0000`. The badge was left on that radio-off result screen.

All three apps build for both board editions, with no undefined target
symbols. The controller and advertising images contain 409,072 and 409,508
firmware bytes, respectively. Rebuilding the input diagnostics produced
byte-for-byte identical UF2s to the preceding physically verified milestone.
Bluetooth host tests additionally exercise maximum-size ACL framing,
undersized receive buffers, patch truncation, controller status errors,
invalid queue pointers and explicit transport/cancellation failures.

[scan.ps1](scan.ps1) uses Windows' own WinRT Bluetooth APIs and .NET compiler;
no third-party host packages are installed. Its C# callback avoids unsupported
PowerShell WinRT event subscriptions. It captures only the clicker name and,
when specified, the expected address. The owner authorized a process-only
`RemoteSigned` execution policy; no saved policy is changed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File clicker\scan.ps1 `
    -Seconds 45 -ExpectedAddress "28:CD:C1:18:0C:60" `
    -FlashImage "build\clicker\2025\advertise\clicker-advertise.uf2" `
    -Serial 55F883B4BD0EB5CE
```

The scanner starts and checks readiness before flashing, verifies the image,
reboots only the selected serial, and records packet timestamps plus scan
start/end, reboot time and image hash in ignored build outputs. It fails
explicitly if the watcher stops or no matching packet is observed. Windows
can throttle/coalesce delivery, so callback count is not a radio packet-rate
measurement. No helper process is left running.

## Bluetooth milestones and agreed behavior

The intended application retains bond information across RESET but reopens
pairing so another computer can connect. Only one computer is to receive
keyboard input at a time. Reconnection must not replay old clicks, and fresh
pairing needs visible feedback and deliberate confirmation rather than
silently replacing a trusted computer. Detailed pairing UX remains to be
settled before its implementation.

Vendor Bluetooth firmware/controller initialization, HCI command/event
exchange and non-connectable advertising have passed their 2025 hardware gates.
The next milestone is a real BLE host connection and the required host protocols,
not merely changing this diagnostic to a connectable advertisement.

Subsequent gates are ACL flow control, L2CAP, ATT/GATT, SMP with encryption and
bond persistence, and HID over GATT with host testing. Standard host support,
pairing compatibility, power-loss-safe bond writes, disconnect behavior and
key-release delivery all require actual tests. The anonymous unencrypted
protocol used by the SDK Pong example is not suitable for this keyboard.
