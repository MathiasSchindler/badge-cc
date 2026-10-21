# Pico 2 W HTTP / LoRaWAN monitor

This opt-in application joins an existing WPA2 Wi-Fi network and serves a
small browser dashboard from the Pico itself. It listens for matching
EU868 LoRa packets, retains eight recent packets in RAM and shows raw hex,
RSSI/SNR, uptime timestamps and visible LoRaWAN data-frame header fields.
There is no cloud service, CDN, SDK, libc, lwIP, allocator or filesystem on
the Pico.

## What it monitors

The initial receive profile is **868.1 MHz, SF7, bandwidth 125 kHz**, coding
rate 4/5, public LoRaWAN sync word and explicit packet headers. Frequency,
SF7-SF12 and bandwidth 125/250/500 kHz can be changed from the browser.
Changes are RAM-only and affect future reception; previously captured
packets retain their original profile.

IQ is fixed to the normal (uplink) polarity. Standard LoRaWAN downlinks
normally use inverted IQ and are not received by this initial application.
Frames without a payload CRC can be accepted unchecked by the radio; the
SX1262 does not report CRC presence, only CRC errors. The packet count
therefore means received without a reported CRC error, not authenticated
or necessarily CRC-verified LoRaWAN packets.

This is a **single-profile receiver, not a gateway or spectrum analyser**.
It misses traffic on all other profiles and while it is reconfiguring.
Reception and Wi-Fi processing share the main loop, so captures are not
lossless. It does not join LoRaWAN, send acknowledgements, transmit LoRa
packets, decrypt payloads, verify MICs, reconstruct full frame counters, or
identify the sender's owner. DevAddr and FCnt16 are unverified visible
fields, not authenticated device identity or a complete frame counter.
Arbitrary matching LoRa packets are not necessarily valid LoRaWAN frames.
No LoRaWAN keys are requested.

Wi-Fi **does transmit**, including association, DHCP, ARP, ICMP and HTTP
responses. The receive-only restriction applies to the SX1262 radio.

### Waveshare hardware assumptions

The monitor deliberately configures the SX1262's DIO3 TCXO control to
1.7 V with a 5 ms startup interval, as in the manufacturer's linked demo,
and DIO2 as RF-switch control. This is different from the readback-only
probe. Confirm that the board is the Waveshare Pico-LoRa-SX1262-868M.
The board's RF-switch supply jumper may select GP22 instead of 3.3 V;
the default position is not verified. This software does not drive GP22,
so that jumper position may prevent reception. Check the schematic and
actual board before the hardware trial rather than guessing a GPIO level.

## Dependency and private-build boundary

The RP2350 application and network stack are project-owned freestanding C.
The CYW43439 wireless chip is a separate processor; this application has
an explicitly authorized exception for its vendor firmware, NVRAM board
configuration and regulatory data. Source-only CYW43439 radio research is
not used. Reused project-owned Wi-Fi host code is adapted locally into this
tree; no badge-only GPIO, display, button or SDK implementation is linked.
The first station implementation uses bounded software-clocked gSPI rather
than PIO/DMA and latches association/controller failures. It does not
automatically reconnect after losing the AP; inspect USB status and
explicitly reboot after correcting the network or credentials.

Credentials are read only by the host build tool from an explicitly supplied
file. They are **embedded in the resulting firmware, not encrypted**.
Generated headers, binary assets, ELF, binary and UF2 files therefore must
be treated as secrets. Do not publish or commit them. The ignored build
directory is not a secure vault; protect access and delete private outputs
when no longer needed. Do not put a password on the command line.

The repository's `ssid-data/ssid.txt` remains outside this standalone tree
and ignored. The build must be given that path explicitly; the source tree
does not depend on its location. No credentials are served by HTTP, printed
by the build tool or included as plain-text fields in the manifest.

Presence of vendor firmware in the repository does not grant distribution
rights. Follow the existing [asset provenance/licensing notes](../../docs/wifi-assets.md)
before distributing any generated image, and retain all applicable notices.
The generated private image is not a redistribution-ready release.

## Build and test

The credential file contains exactly two nonempty UTF-8 lines: SSID, then
WPA2 passphrase. A labelled pair (`SSID: network` and `PASS: password`,
or `SSID=network` and `PASS=password`, case-insensitive) is also accepted.
Labels and whitespace immediately after their separator are syntax, not
part of the value. Raw two-line files retain their exact whitespace.
Labels are recognized only as a complete pair. The station country is explicitly **DE**. This is not a
WPA3-only or enterprise-authentication implementation.

The packager accepts only the pinned original ASCII vendor headers, verifies
their SHA256 hashes, extracts firmware/CLM/NVRAM and generates local build
inputs. It does not compile any vendor host-driver source or download
anything automatically. The inputs used for the local builds were fetched
into ignored `build/vendor-inputs/` from:

- [Firmware header, pinned revision](https://raw.githubusercontent.com/georgerobotics/cyw43-driver/f6d06a70f9710f5854808a8a2803e93700e6038a/firmware/w43439A0_7_95_49_00_combined.h)
  - SHA256 `120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea`
- [Board NVRAM header, pinned revision](https://raw.githubusercontent.com/georgerobotics/cyw43-driver/cf924bb04c8984675ca0fc2178f082e404e048c3/firmware/wifi_nvram_43439.h)
  - SHA256 `ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad`

Original LICENSE and LICENSE.RP notices are retained beside those local
inputs. Other copies of these exact headers can be supplied explicitly;
the firmware source tree never depends on the badge's vendor directory.

From the repository root:

```powershell
python pico2w\tools\build.py all --app monitor `
  --credentials ssid-data\ssid.txt `
  --wifi-firmware pico2w\build\vendor-inputs\w43439A0_7_95_49_00_combined.h `
  --wifi-nvram pico2w\build\vendor-inputs\wifi_nvram_43439.h `
  --confirm-waveshare
```

For the owned target pipeline, add
`--toolchain owned --owned-tools badge-compiler\build` to the same command.
The host tools must already be built. All paths are resolved relative to
the invocation directory. A standalone copy can use external absolute paths
for the credential file, original data headers and owned tool executables.
No badge source is read by the monitor firmware build.

LLVM outputs go to `build/monitor/`; owned outputs to
`build/monitor/owned/`. Both contain `pico2w.uf2`, ELF, binary, map and
manifest, plus **private** generated assets/credential headers. The manifest
records source/image/toolchain hashes and asset provenance, marks
`credentials_embedded: true`, and does not record credential values or a
separate credential hash. Build IDs describe code/toolchain/assets, not a
unique credential set; rebuilding with other credentials can retain the
same build ID. Binary/UF2 hashes identify the actual image bytes.

The default command without `--app monitor` still builds the separate
short-lived probe. `python pico2w\tools\build.py test` runs host suites
without requiring private credentials or real vendor data.

## Local HTTP interface

After the Pico obtains an IPv4 address through DHCP, open `http://<Pico-IP>/`
on the same trusted LAN. Find the address in the router's DHCP leases or
read the monitor's USB status using an explicitly selected WinUSB interface.
There is no mDNS, DNS server, captive portal, HTTPS or Internet access.

The status command uses the existing lab USB `cafe:4027` vendor interface,
not a COM port. Current firmware advertises automatic binding to Windows'
built-in WinUSB driver. The native [picoctl C tool](picoctl.md) discovers
the application interface without a manually supplied device path.
The older Python collector below does not install drivers. Replace
the interface path below with the explicitly selected actual path:

```powershell
python pico2w\tools\lora_probe.py --status `
  --device-path '<actual WinUSB device-interface path>' `
  --serial 7C4918BAB9492C77 `
  --manifest pico2w\build\monitor\manifest.json
```

For an owned image, select `pico2w\build\monitor\owned\manifest.json`.
The tool checks device/build/chip ID before reading APP_STATUS and prints
the local URL only when Wi-Fi/DHCP/network status indicate availability.
It never requests the resetting radio probe in `--status` mode.

The page is embedded HTML/CSS/JavaScript with no external dependencies.
It polls JSON once per second, shows explicit radio/network errors and
keeps received hex as text rather than HTML.

| Request | Meaning |
| --- | --- |
| `GET /` | Dashboard HTML |
| `GET /api/status` | Active profile, counters and recent-packet JSON |
| `POST /api/config` | Validate and apply a new receive profile |

Configuration uses `Content-Type: application/x-www-form-urlencoded`,
the custom header `X-Pico-Request: 1`, and this exact body order:

```text
frequency_hz=868100000&sf=7&bandwidth_khz=125
```

Invalid settings return HTTP 400. Missing write header returns 403.
Radio configuration failure returns 503 and retains explicit error status;
it is not reported as a successful settings change. Unsupported routes
return 404. Responses contain Content-Length and Connection: close.
Status timestamps are polling-time uptime milliseconds, not precise on-air
timestamps or UTC. RSSI and SNR JSON
values are signed quarter-decibels (`rssi_qdb` and `snr_qdb`); the browser
converts them to dBm and dB.

HTTP is **unauthenticated and unencrypted**. Anyone on the reachable LAN
can read packets and change receiver settings. Do not expose or forward
port 80 to the Internet or put this on an untrusted network. Strict Host
matching, no CORS and the custom POST header reduce browser-origin mistakes;
they do not authenticate clients.

### Bounded network stack

- Ethernet, ARP, IPv4, ICMP echo and DHCP (including lease renewal/expiry).
- TCP port 80 only, two fixed client slots, MSS up to 1024 bytes, one
  outstanding segment per client, retransmission and finite deadlines.
- Requests limited to 1024 bytes, form bodies to 256 bytes, responses to
  16384 bytes; no dynamic allocation.
- HTTP/1.1 GET/POST only, no pipelining, keepalive, chunked bodies,
  WebSockets, SSE, TLS, TCP fast retransmit, SACK or window scaling.
- Unsupported IPv4 options/fragments/protocols and malformed checksums are
  rejected and counted. DHCP failure and address conflict are explicit.
- No routing, NAT, DNS, IPv6, public socket API or background Internet calls.

This is a deliberately narrow local diagnostic server, not a general
purpose or hardened Internet TCP/IP implementation. Slow or hostile clients
can occupy its two slots until timeout.

## Recovery and qualification

The monitor is persistent: unlike the original probe application, normal
operation does not schedule an eight-second reboot. Explicit USB BOOTSEL
recovery and physical BOOTSEL recovery remain available. The standalone
short-lived probe is a separate build mode; neither mode is flashed by
building or running host tests. USB LoRa-probe submissions are disabled in
monitor mode to avoid resetting an active receiver behind the dashboard.

Host tests and a browser fixture validate software behavior, **not physical
Wi-Fi association, HTTP service on the Pico, RF reception, antenna/switch
wiring, timing, or generated Arm execution**. The browser fixture uses
synthetic packets on localhost; it is not evidence of over-the-air traffic.
Installation replaces the existing application and needs a separate
explicitly authorized hardware trial.

The owner-authorized 2026-10-20 trial on RP2350 A2
`7C4918BAB9492C77` qualified LLVM startup, USB/WinUSB, native C status and
BOOTSEL recovery, and verified flashing. It did not qualify the radio/network
features: Wi-Fi initialization latched stage 8/error 4 before association,
DHCP remained idle with address `0.0.0.0`, and SX1262 configuration returned
`-3005`. The monitor remains USB-accessible with these errors.
See the [deployment qualification record](picoctl.md#physical-qualification-2026-10-20).

Follow-up trials fixed the version-string validator, GET reply sizes and
labelled credentials. The same Pico now has physically verified WPA2
authorization, DHCP lease **192.168.178.68**, and HTTP/dashboard access.
This address can change on a later lease; use live USB status to discover it.

SX1262 startup now recognizes the expected reset-time XOSC_START_ERR
before the TCXO is powered, while still rejecting other calibration errors.
Reserved status bits are ignored rather than assumed zero. The real radio
entered continuous RX, and HTTP receive-profile changes succeeded. The
default 868.1 MHz/SF7/BW125 profile was restored after testing. No
over-the-air packet has yet been captured: this qualifies control/RX-mode
operation, not RF reception.

`/api/status` includes `lora_state` (0 idle, 1 running, 2 fault),
`lora_status` (raw status byte; chip mode in bits 6:4), `lora_command`
(last opcode), `lora_device_errors` (last nonzero error, excluding the
successfully cleared expected startup error), `lora_polls`, and
`lora_response` (first four bytes of the last command reply, little-endian).
These fields supplement `lora_error` and packet counters; a running radio
with zero packets is not proof of RF-path integrity.
