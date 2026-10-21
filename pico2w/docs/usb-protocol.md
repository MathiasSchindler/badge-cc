# Pico 2 W USB report protocol v1

Full-speed USB, lab-only VID:PID `cafe:4027`, one vendor-specific interface
(class `ff`), endpoint zero only, maximum packet size 64 bytes. These IDs are
not a production allocation. There are no bulk, interrupt, CDC, or HID endpoints.
The USB serial is the uppercase, 16-digit ROM chip ID: high word then low word.

All transfers below have `wValue = 0` and `wIndex = 0` and require the USB
configuration to be selected. Malformed/unsupported requests stall EP0.
Standard enumeration requests remain available before configuration.

Firmware device release 1.01 advertises a Microsoft OS 1.0 string at index
`ee`, vendor request `57`, WINUSB compatible ID (device IN, index 4), and
the `DeviceInterfaceGUIDs` extended property (interface IN, index 5).
The application GUID is `{D17518E7-BD1D-4C3D-81A0-5605CAFE4027}`.
The 146-byte property descriptor uses successive 64/64/18-byte packets
with alternating DATA1/DATA0 PIDs. A new SETUP or bus reset cancels a
partially sent descriptor. Diagnostic report formats remain unchanged.

| Operation | bmRequestType | bRequest | wLength | Payload |
| --- | --- | --- | --- | --- |
| INFO | `c0` | `53` | 12 | IN: `P2WL`, u16 version 1, u16 report size 64, u32 build ID. |
| SUBMIT | `40` | `50` | 64 | OUT: request record. |
| REPORT | `c0` | `51` | 64 | IN: response for the latest successfully committed submission. |
| BOOTSEL | `40` | `52` | 0 | OUT: request recovery, committed after status ACK. |
| LORA_SUBMIT | `40` | `54` | 64 | OUT: opt-in non-transmitting SX1262 probe, test ID 2. |
| LORA_REPORT | `c0` | `55` | 64 | IN: pending/completed LoRa probe response. |
| APP_STATUS | `c0` | `56` | 64 | IN: persistent monitor status, if running that application. |

Numeric fields in the table are hexadecimal except lengths. Record integers
are unsigned little-endian. Obtain the expected build ID from local
`build/manifest.json`, then verify INFO against it before submitting requests.
INFO is identification, not authentication.
The LoRa extension has a separate response layout and submission sequence;
see [LoRa probe](lora-probe.md). It does not change the test-ID-1 records below.
The [persistent monitor](monitor.md) disables LORA_SUBMIT to prevent
resetting its running receiver. APP_STATUS is not supported by the
short-lived probe image.

## 64-byte records

| Offset | Bytes | Request | Response |
| --- | --- | --- | --- |
| 0 | 4 | ASCII `P2WQ` | ASCII `P2WR` |
| 4 | 2 | Version 1 | Version 1 |
| 6 | 2 | Length 64 | Length 64 |
| 8 | 4 | Expected build ID | Running build ID |
| 12 | 4 | Test ID 1 | Test ID 1 |
| 16 | 16 | Fresh, nonzero nonce | Echoed nonce |
| 32 | 4 | Zero | Status 1 (success) |
| 36 | 4 | Zero | Chip ID low word |
| 40 | 4 | Zero | Chip ID high word |
| 44 | 4 | Zero | Committed submission count (u32, wraps) |
| 48 | 4 | Zero | Known value `01234567` |
| 52 | 4 | Zero | Known value `89abcdef` |
| 56 | 4 | Zero | Commit tag `c001cafe` |
| 60 | 4 | CRC-32 over bytes 0-59 | CRC-32 over bytes 0-59 |

CRC is CRC-32/ISO-HDLC: reflected polynomial `edb88320`, initial value
`ffffffff`, final XOR `ffffffff`. The check value for ASCII `123456789` is
`cbf43926`.

A collector must generate a new unpredictable nonce for each request and check
the response's exact length, magic, version, build, test, echoed nonce, status,
chip ID against the selected serial, known values, commit tag, sequence, and
CRC. The device rejects all-zero nonces but does not maintain a replay cache;
freshness is verified by the collector. CRC and nonces are not authentication.

Starting a valid SUBMIT setup invalidates the old report. The replacement
becomes readable only after the submission's status ACK. A new SETUP cancels
an incomplete submission, address change, configuration change, or BOOTSEL
request. Bus reset or configuration changes invalidate reports. Repeated
REPORT reads return the same committed response; they do not increment the
submission count.

## Qualification boundary

This protocol and its pure-C state machine have host tests. On 2026-10-20,
LLVM images on Pico `7C4918BAB9492C77` (RP2350 A2) physically passed
enumeration, automatic WinUSB binding, fresh chip-ID reports, APP_STATUS,
explicit BOOTSEL recovery, and picotool-verified reprogramming.
See the [qualification record](picoctl.md#physical-qualification-2026-10-20).
The USB driver first configures a 120 MHz system clock: RP2350-E12 requires
clk_sys to exceed the 48 MHz USB clock by at least 10%. The earlier
low-clock startup lost SETUP events and failed enumeration.
This does not qualify arbitrary faults, all host controllers, or radio operation.
The optional Windows
[LoRa collector](../tools/lora_probe.py) includes the chip-ID diagnostic as an
identity check; it does not install drivers or flash/reset the Pico.

## Persistent monitor status

APP_STATUS requires configuration and exact length 64. Its snapshot has
no nonce and is diagnostic, not a freshness/authentication assertion.
All integers are little-endian:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `P2WS` |
| 4 | 2 | Version 1 |
| 6 | 2 | Length 64 |
| 8 | 4 | Running build ID |
| 12 | 4 | Wi-Fi stage |
| 16 | 4 | Signed Wi-Fi error |
| 20 | 4 | DHCP state: 1 discover, 2 request, 3 bound, 4 renewing, 5 failed |
| 24 | 4 | Network error: 0 none, 1 DHCP timeout, 2 transmit failure, 3 IP conflict |
| 28 | 4 | IPv4 address as numeric `a<<24 \| b<<16 \| c<<8 \| d`, zero until bound |
| 32 | 4 | Signed LoRa receiver error |
| 36 | 4 | Received LoRa packet count |
| 40 | 4 | Network dropped-packet count |
| 44 | 4 | Network transmitted-frame count |
| 48 | 4 | Uptime milliseconds, wrapping u32 |
| 52 | 4 | Wi-Fi connected flag |
| 56 | 4 | Last Wi-Fi controller status |
| 60 | 4 | CRC-32/ISO-HDLC over bytes 0-59 |

The host collector's `--status` mode first checks the normal test-ID-1
chip-ID report against the selected build/serial, then reads and validates
APP_STATUS. It does not submit a radio probe or reboot the device.

## Wi-Fi operation diagnostics

Configured monitor firmware also accepts device-IN vendor request `58`,
value/index zero, exact length 64. This read-only snapshot is exposed by
`picoctl wifi` after the same fresh identity/build check as `status`.
It does not scan, join, reboot, expose credentials or change the radio.
Like APP_STATUS it is CRC-checked, not nonce-bound.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `P2WD` |
| 4 | 2 | Version 1 |
| 6 | 2 | Length 64 |
| 8 | 4 | Running build ID |
| 12 | 4 | Initialization operation step |
| 16 | 4 | Last register address or failing asset byte address |
| 20 | 4 | Expected register/readback value |
| 24 | 4 | Observed register/readback value |
| 28 | 4 | Completed bytes in last firmware/NVRAM transfer pass |
| 32 | 4 | Last CDC command number, never its payload |
| 36 | 4 | Wi-Fi stage |
| 40 | 4 | Wi-Fi error |
| 44 | 4 | Last CDC controller status |
| 48 | 4 | Last subscribed firmware event type; `ffffffff` before any event |
| 52 | 4 | Event status |
| 56 | 4 | Event reason |
| 60 | 4 | CRC-32/ISO-HDLC over bytes 0-59 |

Steps: 0 pre-load, 1 core reset, 2 RAM setup, 3 firmware write,
4 firmware read-only verification, 5 NVRAM write, 6 NVRAM verification,
7 NVRAM token, 8 ARM start, 9 HT clock, 10 function-2/keep-awake,
11 firmware version, 12 regulatory data, 13 station configuration,
14 asynchronous join/service. Failure latches the operation metadata.
Event type 0 is SET_SSID: status 0 means success, 3 means no matching
network found. The event is diagnostic; WPA2 readiness still requires the
separate associated/authorized station flags, followed by DHCP.
