# Pico 2 W HTTP / LoRa monitor (Meshtastic and LoRaWAN)

This opt-in application joins an existing WPA2 Wi-Fi network and serves a
small browser dashboard from the Pico itself. It listens for matching
EU868 LoRa packets, retains eight recent packets in RAM and shows raw hex,
RSSI/SNR, uptime timestamps and the plain Meshtastic or LoRaWAN header
fields. The live browser viewer accumulates the latest 1,000 packets in
the tab instead of replacing its list with each device snapshot. It
decodes public/default-key Meshtastic payloads locally, with optional
browser-only private keys. Every retained frame is also available over USB to the native
[`picoctl listen`](picoctl.md#listen-to-meshtastic-traffic) command, which
decrypts Meshtastic channel traffic on the host.
There is no cloud service, CDN, SDK, libc, lwIP, allocator or filesystem on
the Pico.

## What it monitors

Two receive profiles exist; the default is **Meshtastic EU_868 LongFast**:

| Profile | Frequency | SF / BW / CR | Preamble | Sync word |
| --- | --- | --- | --- | --- |
| `meshtastic` (default) | 869.525 MHz | SF11 / 250 kHz / 4/5 | 16 | private `0x2b` (`0x24b4`) |
| `lorawan` | 868.1 MHz | SF7 / 125 kHz / 4/5 | 8 | public `0x34` (`0x3444`) |

Both use explicit headers, payload CRC, normal IQ and SX126x RX boosted
gain (register `0x08ac = 0x96`, as Meshtastic does). While the radio is idle
in RX, the firmware samples the instantaneous channel RSSI (`GetRssiInst`)
every 250 ms and keeps the latest, minimum and maximum value since the last
configuration. They appear as "Noise now / min / max" on the page, as
`noise_qdb`, `noise_min_qdb`, `noise_max_qdb` and `noise_samples` (quarter dBm)
in `/api/status`, and in the `picoctl listen` status line. A floor near the
SX1262 sensitivity limit (about -120 dBm at 125 kHz) that never moves points at
a missing antenna or an open RF switch; see [rf-field-notes.md](rf-field-notes.md). Frequency,
SF7-SF12, bandwidth 125/250/500 kHz and the profile can be changed from the
browser. Changes are RAM-only and affect future reception; previously
captured packets retain their original profile.

### Live packet viewer

Open the Pico's root URL (currently `http://192.168.178.68/`). No host
server or CLI is needed. The default radio configuration remains
869.525 MHz, SF11, BW250; all Meshtastic channel hashes on that radio
profile are received together.

- New packets append at the bottom once per second without removing
  earlier rows or collapsing expanded details. The list retains up to
  1,000 observations in browser memory, including repeated packets heard
  through different relays. Repeated HTTP snapshots do not duplicate rows.
- Each expandable row includes sequence, device uptime, estimated local
  reception time, build ID, frequency/SF/bandwidth/profile, payload length,
  packet RSSI, despread signal RSSI, SNR, radio flags, CRC-error status and
  complete raw hex. Meshtastic metadata includes sender/destination,
  packet ID, channel hash, hop limit/start, estimated hops used, ACK/MQTT
  flags, next hop and relay byte. A relay byte is not a complete node ID.
- Public-key decoding shows text, positions, node names/hardware/role,
  device and environment telemetry. The decoded Data protobuf, plaintext
  hex and uninterpreted payload fields remain available in details;
  unknown application types retain their payload hex.
- Optional key lines use `CHANNEL_NAME BASE64_PSK`. The public `LongFast`
  key is always present. Additional keys can be 16/32 bytes or a one-byte
  Meshtastic default-key selector. Keys stay only in this tab's memory:
  they are not uploaded, stored in browser storage, or included in exports.
  Loading keys re-decodes retained history. Reloading forgets both history
  and keys.
- **Pause display** keeps collecting while freezing the list; resume
  renders retained arrivals. A hex hash filter affects display only.
  **Follow newest** controls automatic scrolling. **Clear list** does not
  replay old device packets. **Export JSON** saves retained packets with
  decoded data, raw data and loss counters, but no key material.
- Channel activity counts every observation (not unique senders or
  messages). Hashes can collide and never establish a channel's name.

Decoding uses dependency-free JavaScript AES-128/256-CTR and bounded
protobuf parsing in the browser, so it works on this plain HTTP origin
without WebCrypto's secure-context requirement. It does not run on the
RP2350; the firmware remains freestanding C. Successful decoding is a
plausible interpretation, **not cryptographic authentication**. Unknown
keys and possible PKI messages remain opaque; CRC-failed payloads are
never decoded. Never infer a channel name or trustworthy sender identity
just from a matching hash or a successful decode.

The device retains only eight packets for HTTP snapshots (the USB radio
ring holds sixteen). The browser reports sequence gaps, connection
failures, detected uptime/build resets, and eviction beyond 1,000 entries.
Packets before opening, during closed/background-throttled tabs, after
long outages or overwritten between polls may be lost. Loss across
restarts cannot be counted exactly; uptime/build reset detection is best
effort, not a persistent boot identifier. Display timestamps are estimates
from device uptime and the computer clock, not GPS or exact RF arrival
times. Export contains decoded positions/messages: treat it as private.

In the Meshtastic profile, frames with at least 16 bytes expose their
plaintext header (destination, sender, packet ID, hop limit/start, flags,
channel hash, next hop and relay node). The encrypted payload never gets
decrypted on the Pico, and the Pico never stores a channel key.

In the LoRaWAN profile, IQ is fixed to the normal (uplink) polarity. Standard LoRaWAN downlinks
normally use inverted IQ and are not received by this initial application.
Frames without a payload CRC can be accepted unchecked by the radio; the
SX1262 does not report CRC presence, only CRC errors. The packet count
therefore means received without a reported CRC error, not authenticated
or necessarily CRC-verified LoRaWAN packets.

Frames whose payload CRC fails are still stored in the history, marked
`"crc_error":true` and "CRC error" in the table. Their RSSI, SNR and raw bytes
help to tell weak or distant transmitters from a configuration mismatch.
They are never parsed or decoded, and they count towards `crc_errors`, not
`received`.

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

### Opt-in 868 MHz activity survey

The dashboard's **Start broad survey** button starts a repeating receive-only
scan. **Stop and restore listening** restores the exact profile active before
starting. Scanning is RAM-only and off after reboot. It interrupts continuous
LongFast reception: the single SX1262 cannot listen on several settings at once.
Manual profile changes and the USB one-shot RSSI survey are rejected while it
owns the radio (busy error `-3013`).

Each sweep visits 65 settings for at least 5 seconds each:

- 863.000 through 870.000 MHz inclusive, at 125 kHz spacing (57 settings).
- Then exact centers 867.1, 867.3, 867.7, 867.9, 868.1, 868.3, 868.950,
  and 869.525 MHz. Other common uplink centers already fall on the grid.
- Public LoRaWAN sync/preamble, BW125, SF7 on the first sweep, then SF8
  through SF12 on subsequent sweeps before repeating.
- The dedicated 869.525 MHz setting always uses Meshtastic LongFast
  SF11/BW250/private sync. All compatible logical channel hashes are heard
  together. Other Meshtastic presets are not systematically tested.

A sweep takes about 5.5 minutes plus scheduling/tuning overhead; six sweeps
take roughly 33 minutes. No inverted-IQ LoRaWAN downlink or FSK decoding is
added. Arbitrary matching LoRa frames are not proof of LoRaWAN.

**Energy measurements:** instantaneous in-band RSSI is sampled at a target
10 ms interval, independently of packet decoding. The lower RSSI quintile
of the first 500 ms (at least eight samples) estimates a per-dwell baseline,
with 1 dB histogram resolution. A rise of at least 6 dB starts a burst.
Ending requires RSSI below baseline +3 dB for at least 30 ms; duration ends
at the first observed low sample. Dwell/stop/fault boundaries truncate an
active burst. Durations are first/last **observed** timing, not exact RF
airtime. Sample counts and largest actual sample gaps expose scheduling
limits; short bursts between samples can be missed. Continuous/busy signals
can contaminate the learned baseline, and energy during warmup is not
classified as a burst.

The frequency table accumulates tuned observation time, RSSI samples,
peak RSSI, burst counts and received packet/CRC-error counts. The baseline
is for the most recent visit; the peak is cumulative for the scan run, so
do not subtract these as though they came from the same dwell. Expand
individual events for frequency/SF/BW/profile, uptime start, duration,
baseline/peak/excess (quarter-dBm / quarter-dB units), sample gaps,
packet sequence references, and truncation.

Repeated energy starts on the same frequency yield an **observed
start-to-start gap**, plus `blind_ms` spent off that frequency between the
starts. This is not a proven transmitter period: transmissions may occur
while tuned elsewhere, different sources may share a frequency, and later
visits use different SFs. Neighboring bins may see the same wide signal.
Sample gaps are additional uncertainty, not included in off-frequency time.

Packets decoded while a burst is open are counted in that burst; this is
time coincidence, not identification of the energy's source. Packets decoded
without an active burst get a separate packet-only event, including weak
LoRa below the energy threshold. Their duration is unknown (encoded as
zero); the RSSI field is packet RSSI rather than an instantaneous burst
peak. A missing baseline is JSON `null`, never a fabricated noise-relative
strength. Decode timestamps are when firmware reads the packet, not its
on-air start; decoding can complete after an energy burst has closed.
CRC-failed packets are separate from packets without a reported CRC error.
Full raw packets remain in the existing packet viewer and USB stream.

`GET /api/activity` returns status, current profile, per-frequency totals,
an ongoing burst if present, and the latest eight completed events.
`channels` uses compact rows described by `channel_columns` to keep all
65 entries plus events below the 15,360-byte response limit. The device
retains 32 events internally; `overwritten` counts ring evictions, not
browser losses. The browser accumulates the latest 1,000 events, reports
sequence gaps and its own evictions, and **Export activity JSON** saves
that history plus the latest totals. Reloading/closing loses browser
history. Starting a new scan clears device totals/history and increments
`session`; the browser keeps older-run events with distinct run IDs.

Control is `POST /api/activity`, body exactly `action=start` or
`action=stop`, with the same `X-Pico-Request: 1` header as profile changes.
Malformed/unguarded requests get 400/403; ownership conflicts get 409;
radio/restore failures get 503. Survey `error` and `restore_error` remain
visible in status. On a scan failure firmware stops and attempts to restore
the saved profile. A failed restoration is not reported as healthy listening;
inspect the radio error and explicitly reapply a profile or restart.

### Waveshare hardware assumptions

The monitor deliberately configures the SX1262's DIO3 TCXO control to
1.7 V with a 5 ms startup interval, as in the manufacturer's linked demo,
and DIO2 as RF-switch control. This is different from the readback-only
probe. Confirm that the board is the Waveshare Pico-LoRa-SX1262-868M.
The RF switch supply is selected by a 0 ohm resistor marked `ANT_SW`
(A: 3V3, B: GP22), hidden under the Pico when the HAT is fitted. Since
build `c3747393` the receiver drives GP22 high (push-pull) while it owns
the radio, so the switch is powered with either fit; with A fitted, GP22
is simply unconnected. Release and faults float GP22 again. The board used
here is fitted with A. The
readback-only probe never touches GP22. On-air results are in the
[RF field notes](rf-field-notes.md).

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
| `GET /` | Live dashboard HTML |
| `GET /dashboard-core.js` | Browser decoder and capture-history logic |
| `GET /dashboard.js` | Browser UI and polling |
| `GET /api/status` | Active profile, counters and recent-packet JSON |
| `POST /api/config` | Validate and apply a new receive profile |

Configuration uses `Content-Type: application/x-www-form-urlencoded`,
the custom header `X-Pico-Request: 1`, and this exact body order:

```text
frequency_hz=869525000&sf=11&bandwidth_khz=250&profile=meshtastic
```

`&profile=meshtastic|lorawan` is optional; when omitted the profile is
`lorawan` (compatible with earlier clients). Status JSON reports
`"profile"` in `config` and in every packet, and a `"mesh"` object (or
`null`) with the plaintext Meshtastic header fields.

Invalid settings return HTTP 400. Missing write header returns 403.
Radio configuration failure returns 503 and retains explicit error status;
it is not reported as a successful settings change. Unsupported routes
return 404. Responses contain Content-Length and Connection: close.
Status timestamps are polling-time uptime milliseconds, not precise on-air
timestamps or UTC. RSSI and SNR JSON
values are signed quarter-decibels (`rssi_qdb`, `signal_rssi_qdb` and
`snr_qdb`); the browser converts them to dBm and dB. Packet `flags` exposes
the receiver flags, including bit 7 for CRC failure. Existing status
fields and USB behavior are preserved.

HTTP is **unauthenticated and unencrypted**. Anyone on the reachable LAN
can read packets and change receiver settings. Do not expose or forward
port 80 to the Internet or put this on an untrusted network. Strict Host
matching, no CORS and the custom POST header reduce browser-origin mistakes;
they do not authenticate clients.

### Bounded network stack

- Ethernet, ARP, IPv4, ICMP echo and DHCP (including lease renewal/expiry).
- TCP port 80 only, four fixed client slots (including closing connections),
  MSS up to 1024 bytes, one
  outstanding segment per client, retransmission and finite deadlines.
- Parallel tabs, browser preconnections or rapid API clients can exhaust
  that pool temporarily. The viewer times out and retries failed polls,
  keeps its history, and reports connection failures and sequence gaps.
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
