# Bounded two-board SX1262 link test

This separate opt-in application tests direct LoRa communication between two
Pico 2 W boards with Waveshare Pico-LoRa-SX1262-868M HATs and attached 868 MHz
antennas. It is not Meshtastic, LoRaWAN or the CYW43439 clean-rx radio lab.
The existing probe and monitor remain non-transmitting.

## Safety and limits

- No startup, automatic, echo or periodic transmissions. Each transmission
  requires a host USB command in this application.
- Fixed 869.850 MHz, SF7, BW125, coding rate 4/5, explicit header, eight-symbol
  preamble, normal IQ, payload CRC, private LoRa sync word `0x12` (`0x1424`).
- Fixed nominal **0 dBm** output, 200 us PA ramp. Actual radiated power and
  spectrum have not been calibrated or RF-conformance qualified.
- A 36-byte packet has calculated LoRa airtime about **77.056 ms**.
  SetTx has a hardware timeout of **150 ms** and firmware additionally bounds
  TxDone polling. Every attempt consumes the budget, including failures.
- At least **20 seconds** before the first attempt and between attempts on
  each board; at most **six attempts per boot**. This leaves margin below
  1% duty cycle even at the configured hardware timeout. Rebooting clears
  the RAM budget; do not use resets to circumvent airtime limits.
- Use only where this frequency, power, antenna and activity are permitted.
  Confirm antennas before enabling TX; separate nearby antennas rather than
  placing them against one another.
- Every attempted transmission restores the private-sync receive configuration.
  Failure to restore is explicit in USB status; a fault releases radio pins.
  No keys, Wi-Fi firmware, credentials, IP stack, SDK or target C library
  are used.

This is a short functional test, not a range, sustained reliability or
regulatory qualification.

## Build

From the repository root:

```powershell
python pico2w\tools\build.py all --app link-test --confirm-waveshare
```

Outputs are in `pico2w/build/link-test/`, including a verified UF2 and
manifest with build ID and SHA256. Building does not flash or transmit.
The same image runs on both boards; chip IDs distinguish the senders.
The optional owned pipeline uses `--toolchain owned --owned-tools badge-compiler\build`.

Build the Windows host controller with `pico2w/tools/build-host.ps1`.
If local policy disallows PowerShell scripts, compile it directly:

```powershell
cc -std=c11 -Wall -Wextra -Werror -O2 -Ipico2w\src\include -municode pico2w\tools\picoctl.c pico2w\tools\picoctl_protocol.c pico2w\tools\meshtastic.c pico2w\tools\lorawan.c pico2w\src\usb\report.c -lsetupapi -lcfgmgr32 -lwinusb -lbcrypt -lole32 -o pico2w\build\host\picoctl.exe
```

Flash only explicitly selected devices, with permission to replace their
applications. Use picotool `load -v ... --ser SERIAL` and `reboot -a --ser SERIAL`.
The generic `picoctl upload` expects monitor status; do not use its status
verification for this application.

## Check and run

The following commands address the two boards attached on 2026-10-22,
with the diagnostic image actually flashed that day:

```powershell
.\pico2w\build\host\picoctl.exe link-status --serial 6D64320DF2BB57D8 --build-id ee949855
.\pico2w\build\host\picoctl.exe link-status --serial 7C4918BAB9492C77 --build-id ee949855
.\pico2w\build\host\picoctl.exe link-test --serial 6D64320DF2BB57D8 --peer 7C4918BAB9492C77 --build-id ee949855
```

Use the manifest's build ID after rebuilding/flashing a different image.
`link-status` never transmits. It verifies chip identity, nonce/build
integrity, status CRC, configuration, and radio health. An initialization,
transmit or restore error is an error exit, not a healthy status.

`link-test` opens and verifies both distinct selected devices before sending.
It waits 20 seconds initially, then alternates three challenges each way
with at least 10.2 seconds between sends. The test takes roughly 71 seconds.
The firmware independently enforces each board's 20-second spacing and
per-boot budget.

Each over-the-air packet contains a magic, sender chip ID, firmware attempt
sequence, fresh 16-byte random challenge and application CRC32. The host
requires TxDone and a matching received packet on the other device, checking
the complete payload, frequency/SF/BW, radio CRC-error flag and USB record
integrity. Successful rows report direction, sequence and RSSI/SNR.
An old packet or a TxDone alone cannot establish a passing link.
This is unencrypted and unauthenticated lab traffic; do not send private data.
USB transports the trigger and observations, not a substitute received packet.

On success both radios remain listening on the private test profile, with
no automatic transmissions. They are **not** restored to the Wi-Fi/LongFast
monitor, which must be explicitly reflashed if desired.

## USB surface

- Standard identity/recovery requests remain available.
- Application status `0x56` uses magic `P2WL`, version 1, length 64,
  build ID, uptime, radio state/error, attempt and TxDone counts,
  transmit/restore errors, received/CRC counters, fixed frequency,
  minimum spacing, maximum attempts, then CRC32 at offset 60.
- `0x58` exposes link-specific radio diagnostics: last opcode, raw
  reply, last status, device errors and application error. It is not a
  Wi-Fi diagnostic in this application.
- A transmit request is **`0x5b`**, OUT, value/index zero, exactly 64 bytes:
  the existing nonce/build/CRC-checked report format with **test ID 3**.
  The request is enabled only by this application; ordinary probe requests
  are disabled here. Other applications stall `0x5b` by default.
- Submission is queued only after the USB status stage completes.
  No radio SPI is performed in a USB request handler. Completion is read
  using `0x55`, with the same nonce and build, success/failure and error.
- Received raw frames use the existing `0x59` packet ring protocol.

## Initial trial, 2026-10-22: blocked on new-board SPI contact

On 2026-10-22 both selected boards were identified as RP2350 A2/QFN60,
4 MiB flash, and both flashed images were verified. Backups were explicitly
declined by the user. Both enumerated as the runtime USB device.

| Board serial | Observation |
| --- | --- |
| `7C4918BAB9492C77` | SX1262 initialized and entered private-profile receive mode; zero transmit attempts. |
| `6D64320DF2BB57D8` | SX1262 initialization failed with `-3005`: GetStatus (`0xc0`) returned all-zero bytes. Same result after a controlled restart; zero transmit attempts. |

The second Pico uses modular/unsoldered header contacts. A power/contact/SPI
problem is plausible, not proven; an all-zero reply does not identify the
failed component. The user deferred securing the contacts. **No SX1262
transmissions occurred and no over-the-air direction has been verified.**
The software/USB qualification is not a passing radio-link test.
Native regression suites (including USB command gating, payload/CRC,
transmit limits and failure/restore tests) and all 31 Python tests passed.
LLVM image `ee949855` is installed on both boards; the owned compiler
also built a valid image (`70af6c8a`), but that image was not flashed.

Before retrying, disconnect power and inspect HAT alignment, header contacts,
3V3/GND, GP3 CS, GP10 SCK, GP11 MOSI, GP12 MISO, GP15 RESET and GP20 BUSY.
Keep the antenna attached. Do not reseat or move a HAT while powered.
Reconnect normally: BOOTSEL is unnecessary to run the installed test.
If a device is already in BOOTSEL, use an explicitly selected picotool
application reboot, not another flash, to start the installed image.
Run both `link-status` checks before the paired test. A reliable soldered
header/Pico 2 WH is preferable to pressure contacts for repeatable testing.

## Successful bidirectional trial, 2026-10-23

The user attached two Pico 2 WH boards with SX1262 HATs and authorized
the bounded transmit/receive test. The new board `76EDD29A5E2CD5B4`
replaced the earlier modular-contact board `6D64320DF2BB57D8`; it was
flashed and verified with LLVM image `ee949855`. The original board
`7C4918BAB9492C77` runs the same image. Both enumerated over USB and
initialized their SX1262 receivers without error.

The native host paired test passed with fresh 36-byte challenge payloads,
verified sender identities, sequences and application CRCs, using
869.850 MHz, SF7/BW125, private sync and nominal 0 dBm:

| Direction | Verified packets | Receiver RSSI | Receiver SNR |
| --- | --- | --- | --- |
| `76EDD29A5E2CD5B4` to `7C4918BAB9492C77` | 3/3 | -11 dBm | 12.25-13 dB |
| `7C4918BAB9492C77` to `76EDD29A5E2CD5B4` | 3/3 | -10 dBm | 12.75-14 dB |

Post-test identity/status checks confirmed each board had three transmit
attempts, three completed transmissions, three received packets and zero
CRC errors. Both were left in the private-profile receive state with no
automatic transmissions. Neither is running the Wi-Fi/LongFast monitor.

This establishes working over-the-air transmission and reception in both
directions for this short local test. The very strong reported RSSI
does not constitute calibrated power, range or receiver-linearity
measurement; greater antenna separation is preferable for subsequent
tests. No distance, obstruction or sustained reliability qualification
was performed.

To repeat on this pair with the installed build:

```powershell
.\pico2w\build\host\picoctl.exe link-test --serial 76EDD29A5E2CD5B4 --peer 7C4918BAB9492C77 --build-id ee949855
```

Three of the six per-boot attempts on each board remain after this test.
