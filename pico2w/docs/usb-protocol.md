# Pico 2 W USB report protocol v1

Full-speed USB, lab-only VID:PID `cafe:4027`, one vendor-specific interface
(class `ff`), endpoint zero only, maximum packet size 64 bytes. These IDs are
not a production allocation. There are no bulk, interrupt, CDC, or HID endpoints.
The USB serial is the uppercase, 16-digit ROM chip ID: high word then low word.

All transfers below have `wValue = 0` and `wIndex = 0` and require the USB
configuration to be selected. Malformed/unsupported requests stall EP0.
Standard enumeration requests remain available before configuration.

| Operation | bmRequestType | bRequest | wLength | Payload |
| --- | --- | --- | --- | --- |
| INFO | `c0` | `53` | 12 | IN: `P2WL`, u16 version 1, u16 report size 64, u32 build ID. |
| SUBMIT | `40` | `50` | 64 | OUT: request record. |
| REPORT | `c0` | `51` | 64 | IN: response for the latest successfully committed submission. |
| BOOTSEL | `40` | `52` | 0 | OUT: request recovery, committed after status ACK. |

Numeric fields in the table are hexadecimal except lengths. Record integers
are unsigned little-endian. Obtain the expected build ID from local
`build/manifest.json`, then verify INFO against it before submitting requests.
INFO is identification, not authentication.

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

This protocol and its pure-C state machine have host tests. USB controller
timing, enumeration/driver binding on Windows, chip-ID matching on a real
Pico, report transfer, and automatic/explicit recovery still require a
permission-gated hardware trial. No host collector or automatic driver
installation is included in the initial standalone subset.
