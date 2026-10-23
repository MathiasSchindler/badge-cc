# Autonomous Wi-Fi / LoRa health node

**Current firmware also supports Wi-Fi-managed radio configuration and
bounded tests on both nodes.** See [radio experiments](radio-experiments.md)
for the current dashboards, supported parameters and API. Historical
qualification entries below name the older fixed-profile images explicitly.

The opt-in `remote` application starts from flash when powered by a normal
USB supply. No laptop, BOOTSEL press, filesystem or host server is needed.
The initial deployment target was **76EDD29A5E2CD5B4** only.
The subsequent radio-experiment implementation was authorized on both
Picos, including **7C4918BAB9492C77**.

## Independent diagnostic paths

- Initialize the SX1262 before Wi-Fi. LoRa polling and the health scheduler
  continue through Wi-Fi's bounded initialization and controller waits,
  with a reentrancy guard preventing nested radio operations.
- Join WPA2 using the explicitly supplied vendor firmware and embedded
  credentials, obtain DHCP, serve the health dashboard at `/`, structured
  health at `/api/health`, and raw receiver diagnostics/packets at `/api/status`.
- Report Wi-Fi association/controller errors, link state, DHCP state/errors
  and IP address in LoRa health packets. No SSID, passphrase or vendor data
  is transmitted over LoRa.
- Report LoRa initialization/receive errors, health transmit errors and
  receive-restoration errors over HTTP. Retry a faulted radio no more than
  once per minute. Historical last transmit/restore results remain visible
  until the next attempt.
- Retry failed Wi-Fi or DHCP initialization after 120 seconds. Normal DHCP
  lease renewal remains handled by the existing network stack.
- USB status/recovery remains available when connected to a computer; USB
  configuration is not a prerequisite for radio or network startup.

The health page shows the device serial, uptime, Wi-Fi/DHCP state, IP,
radio state, health attempt/TxDone counts, last TX times/errors and last
valid peer-frame reception time. `peer_seen` means a matching lab packet
was received at least once, **not** a currently healthy bidirectional link.
Peer frames are unauthenticated; a magic and CRC identify the format, not
the sender's trustworthiness.

TxDone is not proof of reception. There is no automatic acknowledgement or
echo protocol; not hearing a packet does not prove an SX1262 hardware fault.
If both diagnostic paths are unavailable, remote reporting is impossible.
HTTP connection failures leave the last displayed snapshot visibly stale.

## Automatic LoRa reports and airtime

Uses the [link test](lora-link-test.md) private profile: **869.850 MHz,
SF7/BW125, CR4/5, preamble 8, normal IQ, sync `0x12`, nominal 0 dBm**.
This is not Meshtastic or LoRaWAN and does not monitor LongFast.
Attach a suitable 868 MHz antenna before powering the autonomous image.
Only use it where this frequency, power and operation are permitted.

- First health attempt no earlier than **60 seconds** after boot.
- Coalesce startup/status changes to the latest snapshot, with at least
  **60 seconds between attempts**, including failures.
- With no changes, send a heartbeat **300 seconds after the last attempt**.
- The 36-byte LoRa packet's calculated airtime is 77.056 ms; hardware TX
  timeout is 150 ms. Even a timeout every minute remains below 0.25% duty
  cycle. The 60-second boot holdoff also applies after power cycling.
- Unlike the finite link test, the autonomous node does not stop after
  six packets: it must keep sending five-minute health heartbeats.
- Every attempt restores private-profile continuous receive mode.
  The original fixed-profile image locked out profile changes and surveys.
  Current experiment firmware allows validated profile changes over Wi-Fi;
  surveys remain unavailable, and health reports follow the selected profile.

Startup success/failure is a delayed best-effort LoRa report, not a
guaranteed delivery. A failed radio cannot send its own status. Wi-Fi
initialization is bounded but not strictly nonblocking; scheduler and
radio processing share one core, so reception/timing are not lossless.
Changes during the one-minute holdoff can be coalesced, not individually logged.

## Build and deploy

```powershell
python pico2w\tools\build.py all --app remote --credentials ssid-data\ssid.txt --wifi-firmware pico2w\build\vendor-inputs\w43439A0_7_95_49_00_combined.h --wifi-nvram pico2w\build\vendor-inputs\wifi_nvram_43439.h --confirm-waveshare
```

All target C remains freestanding, without libc, SDK, allocator or lwIP.
The separate CYW43439 processor uses the same explicitly authorized
vendor-data exception as the [monitor](monitor.md).
Output is `pico2w/build/remote/`; optional owned output is under its
`owned/` subdirectory. Never publish these images: **they contain plaintext
Wi-Fi credentials**. They read no files from `ssid-data/` at runtime.
Use the manifest's build ID/hash and explicit serial with `picoctl upload
--wait-ip` as described in the [host tool guide](picoctl.md).

Only flash the remote node unless another board is separately authorized.
Building/tests alone do not open USB hardware or transmit.

## Listen locally and test the remote receiver

The explicit `ee949855` / `link-send` commands in this section document the
original mixed-firmware setup. With current firmware on both nodes, use
the [radio experiment dashboards](radio-experiments.md) for test sends;
USB `listen` still works with the current manifest's build ID (or omit
`--build-id` to discover it). `link-status`/`link-send` are only for the older
finite link-test application, not the Wi-Fi-managed application.

Rebuild the Windows `picoctl` host utility to obtain health packet decoding;
the original Pico's installed firmware needs no update:

```powershell
.\pico2w\build\host\picoctl.exe listen --serial 7C4918BAB9492C77 --build-id ee949855 --timeout 600
```

`HEALTH from=...` lines show sender, report sequence, remote uptime,
Wi-Fi stage/error, DHCP/network state, association flag, radio state/error,
IP address and the receiving Pico's RSSI/SNR. Reports are unencrypted and
unauthenticated. The IP address and chip ID are therefore observable by
other compatible LoRa receivers; keep this lab traffic non-sensitive.

For a fresh receive-path check on the remote:

```powershell
.\pico2w\build\host\picoctl.exe link-send --serial 7C4918BAB9492C77 --build-id ee949855
```

This waits 20 seconds then requests **one** numbered challenge at 0 dBm,
subject to the local board's existing six-attempt per-boot budget. It only
claims TxDone, not delivery. Check the remote `/api/status` raw packet list
and `/api/health` `last_peer_rx_uptime_ms` to establish reception over Wi-Fi.
The remote does not echo it or reset its heartbeat schedule.
The older paired `link-test` command requires the same test firmware/build
on both nodes; do not use it with this mixed-firmware pair.

To see radio health while Wi-Fi is unavailable, keep the local Pico and
host listener powered. Missing heartbeat(s) can mean RF loss, power loss,
radio failure or receiver trouble; they cannot distinguish those causes.
No host background listener is required to run the remote itself.
The DHCP address can change after a move/restart; read the next LoRa report,
check the router's lease list, or reserve an address for the remote's MAC.
All node counters and peer-reception history are RAM-only and reset on reboot.

## Validation scope

Native health tests cover the one-minute startup/spacing thresholds,
five-minute heartbeat, status-change coalescing, timer wrap, Wi-Fi and
DHCP failure packet contents, and radio-fault/error visibility in HTTP.
The transmit driver tests fixed power/timeout frames, failure handling
and receive restoration; the normal receive-only monitor tests still
forbid transmit opcodes.

These synthetic failure tests do not constitute physical AP-loss or
disconnected-HAT qualification. Never remove/reseat a powered HAT.
Charger-only operation and range in the other room require the user's
physical move; a controlled reboot can verify automatic startup but does
not prove supply quality at the intended location.

### Live checks, 2026-10-23

- Initial LLVM image **`c49ffa3e`** was flashed only to
  `76EDD29A5E2CD5B4`. The original local board still reports `ee949855`.
  The owned compiler produced a valid remote image **`4cac8386`**, not flashed.
- Wi-Fi association and DHCP succeeded automatically, with
  **`http://192.168.178.80/`** serving the health dashboard. Radio and
  transmit/restore errors were zero.
- A controlled application reboot without reflashing again joined Wi-Fi
  and obtained DHCP without host setup requests.
- During that boot the remote received the local board's single test
  challenge at uptime **19,610 ms**, before the DHCP-ready USB check at
  about 35 seconds. HTTP later exposed the raw 36-byte packet,
  RSSI **-10 dBm**, SNR **13.25 dB**, and no reported CRC error. This
  demonstrates radio reception during Wi-Fi startup on the actual device.
- The earlier startup-health test was received by the unchanged local
  board at remote uptime **60,000 ms**, reporting Wi-Fi connected,
  DHCP bound, IP address and no radio error. The host decoded the
  complete CRC-protected health payload.
- HTTP attempts to change the radio profile were rejected with **403**;
  the fixed private diagnostic profile remained active.
- On the final image after reboot, the unchanged local Pico received report
  sequence 1 at remote uptime **60,000 ms** and sequence 2 at **360,000 ms**:
  exactly **300,000 ms** between scheduled attempts. Both reported healthy
  Wi-Fi/DHCP and radio state. The second packet measured **-11 dBm RSSI,
  12.5 dB SNR**, with zero local CRC errors.
- Final HTTP and USB checks agreed: two completed autonomous transmissions,
  zero transmit/restore errors, one received local challenge, radio receiving,
  Wi-Fi connected and DHCP bound. The local board remained on `ee949855`,
  with four lifetime-since-boot test attempts (two remaining); it was not flashed.
- Native regression suites and **34 Python tests**, including health JSON
  and JavaScript syntax checks, passed. The ordinary receive-only monitor
  still builds without linking the transmit driver.

Intermittent HTTP timeouts occurred during validation and the page
recovered on subsequent polls. Do not treat HTTP or LoRa as lossless.

### Charger move exposed a cold-start defect

After the user moved the new Pico to a charger in another room, its old
Wi-Fi address stopped responding. The local receiver stayed healthy but
received no fresh frame during 320 seconds, nor during another 100 seconds
after a charger power cycle. Old packets printed when opening the listener
were explicitly excluded as evidence of new reception.

Reconnecting the remote to the laptop without holding BOOTSEL showed it
in the ROM bootloader. ROM diagnostics `0x500d` include IMAGE_LAUNCHED;
they do not indicate a rejected/corrupt firmware image. A small retained
SRAM read contained zero stage/error values, so the exact recovery error
from the failed boot was not recoverable.

Investigation found that USB initialization used TIMER0 for its mandatory
startup delay but never initialized TIMER0 or its tick generator. Successful
BOOTSEL-to-application restarts did not qualify true power-on startup:
they inherited different peripheral state.

The fix explicitly selects the 12 MHz crystal reference, releases TIMER0
from reset, configures its 12-cycle/1 MHz tick, unpauses it and verifies
time advances before USB setup. USB timing now reads raw low time.
LoRa's separate TIMER1 is untouched. Tick/timer initialization failures
return `-2007` rather than pretending USB setup succeeded.

- **Installed remote image: `f814ee57`**, only on `76EDD29A5E2CD5B4`.
- Owned compiler image `11be5cc2` built successfully, not flashed.
- Native hardware simulation now starts with TIMER0 stopped and covers
  explicit cold initialization and bounded tick-generator failure. All
  native suites and 34 Python tests passed.
- The user unplugged the new Pico for ten seconds and reconnected it to
  the laptop **without BOOTSEL**. It automatically enumerated as the
  application, initialized LoRa, associated Wi-Fi and obtained DHCP
  `192.168.178.80`; USB status was healthy at uptime about 35 seconds.
- Its post-cold-boot startup report appeared as new local packet #8,
  remote sequence 1, remote uptime 60,000 ms, RSSI -9 dBm, SNR 12.5 dB,
  reporting healthy Wi-Fi/DHCP/radio state. HTTP independently reported
  one successful health transmission and no transmit/restore errors.
- The original board remains on `ee949855`; no flash changes were made to it.

The user initially chose to leave both boards attached locally after this
check; charger-powered operation was not yet qualified at that point.

### Successful charger-powered test, 2026-10-23, about 16:44-16:47 CEST

The user then placed the new Pico in another part of the room on a USB
charger. Only the original `7C4918BAB9492C77` remained attached to the laptop.
The remote `76EDD29A5E2CD5B4`, running `f814ee57`, automatically joined
Wi-Fi and obtained `192.168.178.80`. Its health page was reachable and showed
healthy Wi-Fi/DHCP, radio receive state, and no transmit/restore errors.
Remote uptime around 77 seconds and reset counters confirmed a new boot.

| Direction | Verified observation | Receiver RSSI | Receiver SNR |
| --- | --- | --- | --- |
| Charger-powered remote to USB local | New local packet #9: remote startup health sequence 1, uptime 60,000 ms, correct chip ID/IP and no reported errors | -60 dBm | 12.75 dB |
| USB local to charger-powered remote | Local test sequence 5: correct sender `7C4918BAB9492C77`, 36-byte payload/application CRC checked through remote HTTP; peer timestamp also updated | -62 dBm | 11.75 dB |

The local receiver's packet counter increased from 8 to 9 after the move;
older ring entries printed when opening the listener were not counted as
fresh remote reception. The reverse-direction packet was explicitly
triggered once using `link-send` and verified at the destination, not
inferred from TxDone. Both radios reported zero CRC errors.

This qualifies autonomous startup from the tested USB charger, Wi-Fi/HTTP,
and successful LoRa delivery in both directions at the user's new placement,
using 869.850 MHz/SF7/BW125 and nominal 0 dBm. No measured distance,
wall penetration, maximum range or sustained packet-loss rate is established.
The original board has now used five of its six per-boot test attempts.
It remains in receive mode; the remote remains autonomous on the charger.

### One-room / one-floor placement, 2026-10-23, about 17:08 CEST

The user moved the remote one room and one floor away. It cold-booted
successfully on its USB supply and remained reachable at `192.168.178.80`
with healthy Wi-Fi, DHCP and radio state; remote uptime was about 70 seconds.

| Direction | Verified observation | Receiver RSSI | Receiver SNR |
| --- | --- | --- | --- |
| Remote to local | New local packet #14: startup health sequence 1 at remote uptime 60,000 ms; correct remote ID, IP and healthy status | -69 dBm | 12.5 dB |
| Local to remote | Test sequence 6, correct sender ID and application CRC verified through remote HTTP at remote uptime about 103 seconds | -73 dBm | 12.25 dB |

Both used the same 869.850 MHz/SF7/BW125/private-sync profile and nominal
0 dBm. The remote reported zero CRC errors. Older stored packets were
not counted as measurements at the new placement.
This confirms delivery in both directions for this short test, not a
measured distance, maximum range or sustained loss-rate qualification.
The local board has now used all six per-boot transmit attempts and will
reject more `link-send` requests until a controlled restart. Its receiver
remains active; the remote's autonomous heartbeat schedule is unaffected.
