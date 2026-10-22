# Native Windows Pico control tool

`picoctl` is a project-owned C host program. It uses the host C runtime,
SetupAPI/Configuration Manager, WinUSB and Windows CNG. It has no Python,
libusb or other third-party library dependency. Verified flashing delegates
to an explicitly selected, separately installed `picotool` executable.
None of these host libraries is linked into the freestanding Pico firmware.

This first version targets the persistent monitor and its existing protocol.
It is not a replacement USB firmware driver, a SWD debugger, or a universal
flasher. Host tests pass on Windows ARM64; actual device reset, flash,
WinUSB transfer and network-status collection were subsequently exercised
on the selected Pico; see the [physical qualification](#physical-qualification-2026-10-20).

## Build

From the repository root, with LLVM-MinGW `cc` available:

```powershell
.\pico2w\tools\build-host.ps1 -Test
```

This only builds the host tool and runs hardware-free C tests. It does not
download/install picotool, install drivers, open a Pico, reset or flash one.
Use `-Compiler clang` to choose another MinGW-compatible compiler.
MSVC's command-line syntax is not supported by the build script.

If local PowerShell execution policy disallows scripts, keep that policy
and compile directly:

```powershell
New-Item -ItemType Directory -Force pico2w\build\host | Out-Null
cc -std=c11 -Wall -Wextra -Werror -O2 -municode `
  -Ipico2w\src\include `
  pico2w\tools\picoctl.c pico2w\tools\picoctl_protocol.c pico2w\tools\meshtastic.c pico2w\tools\lorawan.c `
  pico2w\src\usb\report.c `
  -lsetupapi -lcfgmgr32 -lwinusb -lbcrypt -lole32 `
  -o pico2w\build\host\picoctl.exe
```

The existing `python pico2w\tools\build.py test` also runs the portable
protocol tests and, on Windows, the Windows C tests. Python is a test/build
runner here, not part of the resulting CLI or deployment process.

## Discover and inspect

```powershell
.\pico2w\build\host\picoctl.exe list
.\pico2w\build\host\picoctl.exe status `
  --serial 7C4918BAB9492C77 --build-id d1828acd --timeout 10
.\pico2w\build\host\picoctl.exe wifi `
  --serial 7C4918BAB9492C77 --build-id d1828acd
```

`list` enumerates native USB device instances with Raspberry Pi's VID or the
project's diagnostic VID/PID. It does not open them or send USB requests.
An unknown device with a failed device-descriptor request has no usable
chip identity and will not be listed as a selected Pico.

`status` discovers the selected chip's application interface, opens WinUSB,
checks device/protocol/build identity, and obtains a fresh nonce-bound chip-ID
report before validating the monitor's APP_STATUS snapshot and CRC.
Only a matching chip can receive subsequent control requests.
It does not reset or probe the SX1262.

`wifi` performs the same fresh chip/build identification and reads the
[Wi-Fi operation diagnostic record](usb-protocol.md#wi-fi-operation-diagnostics).
Its JSON includes initialization step, register/readback metadata, last CDC
command, and last association event type/status/reason. It never prints the
command payload or credentials. It does not accept `--wait-ip`.

The build ID is the eight hexadecimal digits in the chosen build's
`manifest.json`. Do not assume the sample above identifies later builds.
The build ID is not an image hash or authentication.

Add `--wait-ip` to wait for successful Wi-Fi association and a usable DHCP
lease. A reported network fault exits immediately with the status JSON.
A readiness timeout prints the last validated snapshot, if any, and fails.
LoRa errors are reported separately and do not hide an available HTTP IP.
This checks network state, not end-to-end browser/HTTP reachability.

Machine-readable status goes to stdout; diagnostics go to stderr. For example:

```json
{"serial":"7C4918BAB9492C77","build_id":"d1828acd","identity_verified":true,"wifi_stage":7,"wifi_error":0,"dhcp_state":3,"network_error":0,"lora_error":0,"ip":"192.168.178.50","received":0,"network_drops":0,"network_tx":4,"uptime_ms":12000,"wifi_connected":true,"wifi_controller_status":0,"network_ready":true,"url":"http://192.168.178.50/"}
```

This is illustrative, not an IP observed on the real Pico.
APP_STATUS v1 itself is not nonce-bound: `identity_verified` describes the
fresh chip-ID exchange, not a new freshness guarantee for its status snapshot.
CRC/serial/nonce checks protect against mistakes and stale identity reports,
not a malicious device or application.

### WinUSB discovery and binding

Automatic discovery tries the project's application GUID
`{D17518E7-BD1D-4C3D-81A0-5605CAFE4027}`, standard USB-device and WinUSB
interface GUIDs, and the ROM picoboot GUID. It matches the chip serial in the device's PnP
ancestry, never a cached COM number or USB bus address.
For a driver that registers a different application interface GUID, supply:

```powershell
.\pico2w\build\host\picoctl.exe status `
  --serial 7C4918BAB9492C77 --build-id d1828acd `
  --interface-guid '{YOUR-EXISTING-DRIVER-INTERFACE-GUID}'
```

Alternatively pass `--device-path` with an existing WinUSB interface path.
Even an explicit path must pass the chip-ID check before a reset.
An old application path is not reused after reboot/upload: when using this
override, obtain the new path if Windows changes it.

Current firmware advertises Microsoft OS descriptors for automatic WinUSB binding.
This host tool does not install a driver or alter Windows registry bindings.
A descriptor failure must be fixed in firmware/hardware before driver binding
can help. Discovery cannot make non-enumerating firmware accessible.

## Listen to Meshtastic traffic

```powershell
.\pico2w\build\host\picoctl.exe listen --serial 7C4918BAB9492C77 `
  --psk-file ssid-data\meshtastic.txt [--timeout SECONDS] [--count N] [--hex]
```

`listen` streams every frame the monitor receives over USB (vendor request
`59`, see [USB protocol](usb-protocol.md#lora-packet-stream)) and prints one
line per packet: local time, sequence, frequency, SF/BW, RSSI, SNR and
length, then the decoded Meshtastic header and payload. It polls every
200 ms when idle, reports packets lost to ring overwrites, reconnects after
USB resets and notices firmware restarts. It runs until Ctrl+C, `--count`
packets, or `--timeout` (1..86400 s for `listen`). `--build-id` is optional.

The Pico only sees ciphertext. Decryption is done here with the project's
own AES-CTR (128/256) and protobuf decoder: nonce = packet ID (u64 LE),
sender (u32 LE), 32-bit big-endian block counter, exactly as Meshtastic.
The public default `LongFast` channel (`AQ==`, hash `0x08`) is always tried.
Add private channels in a git-ignored file, one per line, `#` comments allowed:

```text
Potsdam <BASE64-PSK>
```

Keys never leave the host and are never printed; only channel names and
their 8-bit hashes are. A packet is decoded when its channel hash
matches **and** the decrypted bytes parse as a strict `meshtastic_Data`
protobuf. The hash mixes channel name and key, so a channel that reuses a
known key under another name has an unknown hash. For such packets every
configured key and the public default key are tried as well; a result is
accepted only on a strict parse with a known port number and is shown as
`[other channel, default key]` or `[other channel, key of NAME]`.
Otherwise a packet is shown as `encrypted, N bytes (no matching channel
key)`, or as a PKI direct message (channel hash 0), which requires the
recipient's private key and is never decrypted. Decoded ports include text
messages, position, node info and device/environment telemetry; other
ports show their name and payload size. Example output:

```text
14:03:12 #12 869.525 MHz SF11/250 RSSI -97.2 dBm SNR 6.50 dB 58 B | !a1b2c3d4 -> ^all id=0x1a2b3c4d hop=3/3 ch=0xe3 relay=0xd4 | [Potsdam] TEXT_MESSAGE_APP "Hallo Potsdam"
```

(Illustrative line; the exact decoded fields depend on the port.)

In the LoRaWAN profile the same command prints the unencrypted MAC header
instead: message type, DevAddr with its NetID type/NwkID and operator (TTN
is NetID `000013`, DevAddr `26xxxxxx`/`27xxxxxx`), FCnt16, FPort, FOpts and
ADR/ACK flags; join requests show JoinEUI, DevEUI and DevNonce. The MIC is
not verified and FRMPayload stays encrypted.

Frames that failed the radio's payload CRC are printed as `CRC ERROR`,
with their RSSI and SNR and always with a hex dump, and are not decoded.

Right after connecting and then every 60 s, `listen` writes a status line
to stderr, so silence can be told apart from a dead RF path:

```text
14:51:00 radio: rx=0 crc_err=0 hdr_err=0 noise now -118.5 min -121.0 max -104.5 dBm (2400 samples since config)
```

Noise values come from the firmware's 250 ms `GetRssiInst` sampling.

To switch the radio profile over HTTP from PowerShell, use `curl.exe`, not
`Invoke-WebRequest`: Windows PowerShell 5.1 sends `Expect: 100-continue`,
which the monitor's strict HTTP parser rejects as malformed.

```powershell
curl.exe -s -X POST -H "X-Pico-Request: 1" `
  -H "Content-Type: application/x-www-form-urlencoded" `
  --data "frequency_hz=868100000&sf=7&bandwidth_khz=125&profile=lorawan" `
  http://192.168.178.68/api/config
```

Receive-only: the SX1262 never transmits, so nodes do not learn about
this listener and no ACKs are sent.

## Survey the spectrum

`survey` answers "does the antenna path receive anything at all?" It
reads the RSSI at every MHz from `--from` to `--to` (default 700-960, valid
150-960) and prints mean and maximum, a bar and the German allocation of
each step. LTE 800 (791-821 MHz) and GSM/LTE 900 (925-960 MHz) downlinks
transmit continuously, so with a working antenna they stand well clear of
the floor even indoors.

```powershell
.\pico2w\build\host\picoctl.exe survey --serial 7C4918BAB9492C77
.\pico2w\build\host\picoctl.exe survey --serial 7C4918BAB9492C77 --from 791 --to 821
```

The summary compares the floor (25th percentile of the means) with the
strongest signal at least 3 MHz wide, scored by the weakest mean of 3
adjacent steps. At least 15 dB above the floor means the path receives;
6-15 dB means weak signals only; less means the RF path is suspect.
Single-MHz spikes, here on multiples of 16 MHz, are on-board clock
harmonics; they are reported separately and do not count as reception. The
radio returns to the configured profile after each step; it never
transmits. A full 261-step sweep takes about 16 seconds.

## Restart into BOOTSEL

```powershell
.\pico2w\build\host\picoctl.exe bootsel --serial 7C4918BAB9492C77
```

This is an explicit device-state change. It verifies a fresh chip-ID report
against the requested serial, permits the currently running protocol-compatible
build, sends the existing ACK-gated BOOTSEL request, closes WinUSB, then waits
for that same serial as ROM `2e8a:000f`.
If the chip is already in BOOTSEL it reports that state without resetting it.

It does not try the 1200-baud CDC convention, because this firmware has no CDC
interface. A hung/missing USB application requires physical BOOTSEL or SWD;
there is no USB-only guarantee of recovery from an arbitrary firmware fault.
BOOTSEL clears relevant SRAM: do not interpret zero-filled RAM as the
application's retained diagnostic result.

## Verified upload and runtime check

Use a USB-enabled RP2350-capable
[official picotool release](https://github.com/raspberrypi/pico-sdk-tools/releases).
The official Windows archive is x64; its execution/USB access on Windows ARM64
was qualified for ROM access and verified flashing on this Windows ARM64 host.

Read `build_id` and `uf2_sha256` from the matching local manifest. Pass them
explicitly to avoid adding a general JSON parser or silently trusting an
unrelated manifest. The SHA256 must come from the intended build, not from
an untrusted image received alongside its own checksum.

```powershell
.\pico2w\build\host\picoctl.exe upload `
  --serial 7C4918BAB9492C77 `
  --build-id d1828acd `
  --uf2 pico2w\build\monitor\pico2w.uf2 `
  --sha256 f7d1570b7226536f2f9ad1d42a4549d1295fc98c18bce367c0cb083d020b1cb5 `
  --picotool 'C:\Tools\picotool\picotool.exe' `
  --wait-ip --timeout 60
```

**This overwrites the explicitly selected Pico's application.** Back up any
firmware/data that must be preserved before authorizing it. The tool does not
make backups automatically, erase the entire flash, alter OTP/security settings,
ignore partitions, or silently fall back to another device/transport.

The workflow:

1. Opens the UF2 with writes/deletion denied for the entire deployment.
2. Checks its SHA256 with CNG and validates every 512-byte block: exact
   RP2350 Arm Secure family, contiguous flash addresses, 256-byte payloads,
   count/order/magic/flags and 4 MiB flash limit. This intentionally accepts
   only the dense UF2 layout produced by this project's builder.
3. Enters/observes selected-device BOOTSEL.
4. Executes `picotool load -v IMAGE --ser SERIAL`, without a shell.
5. Only after verification succeeds, executes `picotool reboot --ser SERIAL`.
6. Waits for the expected application build, fresh chip-ID validation and a
   valid monitor status; with `--wait-ip`, additionally requires network ready.

UF2 shape/hash checks do not prove executable correctness or hardware success.
The expected build ID and image hash are independent inputs; a mismatch is
detected at the runtime gate, not inferred from embedded binary contents.
The probe-only firmware has no APP_STATUS and will not pass the monitor
runtime gate.

`check-image --uf2 PATH --sha256 HEX64` performs only step 2 and opens no USB
device. This is useful before an authorized hardware trial.

Each process/discovery/readiness phase uses `--timeout` (1-300 seconds,
default 60), not one global deadline. USB control transfers have a separate
two-second timeout and cancellation. Child processes run in a kill-on-close
Windows job; nonzero exit, launch failure or timeout prevents the next step.
Picotool output is forwarded to stderr to preserve CLI JSON on stdout.
An interrupted/failed flash may be partial and requires recovery, not an
automatic claim that the previous firmware remains intact.

Exit codes: 0 successful requested operation, 1 operational/protocol failure,
2 invalid command/options/path. Plain `status` succeeds for a valid report even
if Wi-Fi/LoRa is faulty; `--wait-ip` fails unless network readiness is reached.
An upload can therefore verify flash successfully yet exit 1 because startup
or networking fails. These are intentionally distinct outcomes.

## Validation boundary and next hardware step

C tests cover malformed serial/hash input, every-byte report corruption,
wrong chip/build/nonce, status readiness/error handling, UF2 block mutations,
SHA256 mismatch, truncated images and holding the image read-only.
Windows process tests use the test executable as a fake child, checking Unicode,
spaces, quotes, trailing backslashes, shell metacharacters, nonzero exits and
bounded timeout termination. They do not substitute for a real picotool/USB
cycle or exercise SetupAPI/WinUSB with simulated devices.

The first physical trials qualified a bounded known-report USB image and its
driver binding, then BOOTSEL transitions and verified uploads, before retrying Wi-Fi.
For independent recovery and fault inspection, use a Debug Probe/spare Pico
with SWD and RP2350-aware OpenOCD. No SWD backend, new watchdog policy or UART
logger is implemented. Firmware USB descriptor/clock fixes are described below.
See the [USB protocol](usb-protocol.md) and [monitor guide](monitor.md).

Firmware images and backups can contain plaintext Wi-Fi credentials.
Keep them local/private; do not publish them with deployment logs.

## Physical qualification: 2026-10-20

The owner authorized firmware replacement on the BOOTSEL-connected Pico
`7C4918BAB9492C77`. Official picotool 2.3.1 identified RP2350 A2/QFN60,
4 MiB flash, Arm, secure boot disabled and debug enabled. The official x64
host package ran on Windows ARM64. Its archive SHA256 was checked against
the official release's digest before execution.

The installed previous monitor was independently verified against its UF2.
A complete private 4 MiB backup was saved before diagnostic writes. A
temporary SRAM-executed ROM flash writer stored startup evidence in the final
4 KiB sector because BOOTSEL clears SRAM. That sector was restored from the
backup with picotool readback verification before installing the monitor.
No deployed probe/monitor writes diagnostic flash at runtime.

The startup evidence showed lost USB SETUP events with the inherited slow
system clock, despite valid chip ID and locked USB PLL. The
[RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
erratum E12 (PDF pages 1367-1368 in RP-008373-DS-3) requires clk_sys at least
10% faster than clk_usb. USB initialization now safely switches away from
the active auxiliary clock, configures PLL_SYS at 120 MHz, waits for lock,
and selects it before using USB. Failure to establish that clock returns
`-2001` without enabling the application USB pull-up. Controller write-clear
status updates use the hardware atomic-clear alias.

After that change, enumeration produced the correct product/serial rather
than a failed descriptor. Microsoft OS 1.0 descriptors and multi-packet
control-IN support then enabled automatic Windows WinUSB binding. No custom
INF, driver replacement utility or host registry edits were used.

Physically observed:

- Picotool load with flash verification and application reboot.
- Bounded probe return to BOOTSEL.
- Application enumeration and WinUSB service with Windows problem code 0.
- Native C chip-ID challenge and explicit BOOTSEL request, followed by the
  same ROM chip serial reappearing without manual intervention.
- Native C upload of the persistent LLVM monitor, verified flash, reboot,
  expected build/chip identification and repeated valid live APP_STATUS.
- Readiness correctly fails rather than reporting a fabricated IP:
  Wi-Fi stage 8/error 4, DHCP state 0, address `0.0.0.0`, disconnected;
  SX1262 error `-3005` (invalid command response).

Wi-Fi had progressed through bus probing into firmware loading before
failing. This evidence does not identify the exact failing radio operation
or prove AP association, DHCP, HTTP serving or LoRa reception.
Radio initialization is the next separate debugging task. USB status and
recovery remain available while these initialization errors are latched.
Repeated power-cycle, injected-hang, all-controller and long-duration
qualification are still outstanding; do not generalize the successful
normal recovery into an arbitrary-fault recovery guarantee.

### Follow-up: Wi-Fi, DHCP, HTTP and SX1262

Later owner-authorized trials on the same device resolved the above
initialization failures:

- Firmware/NVRAM verification is now read-only rather than rewriting each
  block immediately before checking it.
- The firmware version validator reuses the existing parser accepting a
  trailing CR/LF before the terminating NUL.
- GET_BSSID now supplies the six-byte firmware reply budget instead of
  zero; station authorization queries allow the full 512-byte reply.
- The private credential file used `SSID`/`PASS` labels. Those had been
  included in the SSID/key; the resulting SET_SSID event reported no matching
  network. The build now accepts a complete labelled pair as well as the
  original raw two-line format, without modifying or printing the private file.
- ROM readiness now requires opening the actual picoboot WinUSB interface,
  not merely observing the composite USB parent. This removes an observed
  enumeration/driver-readiness race before starting picotool.

WPA2-associated/authorized station flags and DHCP bound state were physically
verified; the assigned address was **192.168.178.68**. Actual HTTP
`GET /`, `GET /api/status` and receive-profile `POST /api/config` succeeded.
The browser displayed the page served by the Pico, not the localhost fixture.
Repeated deployments reacquired the same lease. It remains a DHCP address,
not a guaranteed permanent assignment.

SX1262 startup returned status `2a` and device error `0020`:
XOSC_START_ERR before DIO3 had powered the TCXO. Startup now permits only
that expected error in RC standby, configures the TCXO, clears the error,
recalibrates, and requires error-free RX. Other errors and a command failure
without the matching startup error remain fatal. Status reserved bits 7/0
are ignored, as specified in the SX1262 status-byte definition; actual
GetDeviceErrors responses had bit 7 set. Floating-bus, timeout, command
and mode checks remain in place.
The TCXO selector was also corrected from `02` (1.8 V) to `01` (1.7 V),
matching the manufacturer-linked board code and datasheet table 13-35.

The real radio reached RX mode 5 with zero device errors and continued
successful IRQ polling. HTTP changed 868.1 MHz/SF7 to 868.3 MHz/SF8 and
restored the original profile without losing Wi-Fi. No LoRa TX command was
issued. No matching over-the-air packet was captured, so antenna/RF-switch
wiring, sensitivity and packet reception are not yet qualified. TCP has two
fixed client slots; browser polling/preconnect and concurrent clients can
contend and a concurrent profile request timed out during testing.
