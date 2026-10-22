# Universe presentation clicker

Freestanding C for the 2025 and 2026 RP2350B badges, without target libraries,
libc, Pico SDK or BTstack. This is an opt-in application: existing firmware
targets are unchanged.

**Current milestone: display and button diagnostic only. Bluetooth is not
implemented.** It does not advertise, pair, store bonds or send keyboard input
to a computer. The screen explicitly reports this. Local keyboard reports
exercise the intended HID input pipeline, not an actual Bluetooth transport.

## Build and tests

From the repository root on Windows:

```powershell
python clicker\build.py --test --board 2025
python clicker\build.py --board 2026
```

Host requirements: Python standard library and LLVM (`clang`, `ld.lld`,
`llvm-objcopy`, `llvm-nm`); `cc` builds the host tests and existing `bin2uf2`
utility. No package installation is required. Native C tests can use
`--cc <compiler>`; firmware compilation can use `--target-cc <clang>`.

Outputs are `build\clicker\2025\clicker-diagnostic.uf2` and the corresponding
`2026` path. These are different images, not automatic board detection.
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
nor Bluetooth has physical qualification from this diagnostic. The local
report tests do not establish actual computer-side key-release delivery.

## Bluetooth milestones and agreed behavior

The intended application retains bond information across RESET but reopens
pairing so another computer can connect. Only one computer is to receive
keyboard input at a time. Reconnection must not replay old clicks, and fresh
pairing needs visible feedback and deliberate confirmation rather than
silently replacing a trusted computer. Detailed pairing UX remains to be
settled before its implementation.

The next hardware gate is vendor Bluetooth firmware/controller initialization
and HCI command/event exchange, then advertising. The referenced vendored SDK
and Bluetooth assets are absent from this checkout; do not substitute the
existing pinned Wi-Fi-only image or alter its provenance contract.

Subsequent gates are ACL flow control, L2CAP, ATT/GATT, SMP with encryption and
bond persistence, and HID over GATT with host testing. Standard host support,
pairing compatibility, power-loss-safe bond writes, disconnect behavior and
key-release delivery all require actual tests. The anonymous unencrypted
protocol used by the SDK Pong example is not suitable for this keyboard.
