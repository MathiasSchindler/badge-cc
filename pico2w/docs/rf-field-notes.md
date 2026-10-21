# RF field notes: Potsdam, October 2026

Log of on-air reception trials with the Pico 2 W and the Waveshare
Pico-LoRa-SX1262-868M HAT. The firmware is receive-only throughout.

## Setup

- Location: Potsdam (Bornstedt area), indoors, USB-powered Pico 2 W.
- Radio settings: TCXO 1.7 V on DIO3, DIO2 as RF switch, explicit header,
  payload CRC, normal IQ, RX boosted gain from build `377bf7f6` onwards.
- Host: `picoctl listen` over USB request `59`. Profile changes go through
  `POST /api/config` with `curl.exe`.

## Nearby infrastructure

### Meshtastic

The Potsdam community channel runs on EU_868 / LongFast: 869.525 MHz,
SF11, 250 kHz bandwidth, sync word `0x2b`. The channel is called `Potsdam`
and uses a 256-bit PSK, which is stored privately in
`ssid-data/meshtastic.txt` and never committed.

### The Things Network (TTN) gateways

All three gateways belong to TTN, NetID `000013`:

| Gateway | EUI | Location |
| --- | --- | --- |
| potsdam-58a0cbfffe805673 | `58A0CBFFFE805673` | 52.424, 13.045 |
| stadtteilladen (Stadtteilladen Bornstedt) | `60C5A8FFFE78FD55` | 52.4138, 13.0493 |
| adaptiveagrotech-gateway01 | `A84041FFFE225BEF` | Potsdam |

Gateways do not transmit on their own; they relay device uplinks to the
network. What can be heard near them:

- **Uplinks from LoRaWAN end devices.**
  - Channels 868.1, 868.3 and 868.5 MHz plus 867.1 to 867.9 MHz, SF7 to
    SF12 at 125 kHz, normal IQ, public sync word `0x34`.
  - The `lorawan` profile receives these. `picoctl` decodes the plaintext
    MAC header; FRMPayload stays encrypted with keys we do not have.
  - TTN devices have DevAddr `26xxxxxx` or `27xxxxxx`.
- **Downlinks and join accepts from the gateways.**
  - These use inverted IQ: RX1 on the uplink channel, RX2 on 869.525 MHz
    SF12.
  - Not receivable with the current normal-IQ profiles.
  - They are rare, because TTN fair use allows about 10 downlinks per day
    per device.
- **Our own traffic.** Transmitting requires registering our own device on
  TTN, an OTAA join, a TX path in the firmware and a LoRaWAN MAC. Limits:
  - the EU868 1% duty cycle;
  - TTN fair use of 30 s uplink airtime per device per day.

  None of this exists yet, and the firmware is deliberately receive-only.

## Observations

| Date/time (CEST) | Build | Profile and settings | Duration | Packets | CRC errors | Header errors |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-10-21 | `175f982f` | Meshtastic 869.525 MHz SF11/250 | 4 min | 0 | 0 | 0 |
| 2026-10-21 | `175f982f` | Meshtastic 869.525 MHz SF11/250 | about 26 min in total | 0 | 0 | 1 |
| 2026-10-21 14:09 to 14:19 | `377bf7f6` | LoRaWAN 868.1 MHz SF7/125 | 10 min | 0 | 0 | 0 |
| 2026-10-21 14:19 to 14:29 | `377bf7f6` | LoRaWAN 868.3 MHz SF7/125 | 10 min | 0 | 0 | 0 |
| 2026-10-21 14:29 to 14:39 | `377bf7f6` | LoRaWAN 868.5 MHz SF7/125 | 10 min | 0 | 0 | 0 |
| 2026-10-21 14:44 to 14:56 | `b6454c1a` | Meshtastic 869.525 MHz SF11/250 | 12 min | 0 | 4 | 0 |
| 2026-10-21 14:56 to 15:00 | `b6454c1a` | LoRaWAN 868.3 MHz SF7/125, noise -116 to -113 dBm | 4 min | 0 | 0 | 0 |
| 2026-10-21 15:02 to 15:32 | `549a301b` | Meshtastic 869.525 MHz SF11/250, noise -117 to -110 dBm | 30 min | 0 | 0 | 0 |

Noise floor with build `b6454c1a` on Meshtastic 869.525 MHz, 250 kHz, measured
2026-10-21 from 14:40 to 14:44 (754 samples at about 4 per second):

| Latest | Minimum | Maximum |
| --- | --- | --- |
| -111.0 dBm | -117.0 dBm | -110.0 dBm |

The theoretical thermal floor at 250 kHz with about a 6 dB noise figure is
about -114 dBm (-174 + 54 + 6). The measured floor sits right there and
never moved by more than 1 dB above it in about 4 minutes. That is what the
receiver's own noise looks like with no antenna signal reaching it. It is
not proof, because 250 ms sampling can miss short bursts, but it is
consistent with an open RF path.

The radio itself works:

- SPI, BUSY, reset, TCXO startup, calibration and RX mode are all healthy.
- The receiver stays in RX and polls normally.
- One Meshtastic header error shows that the demodulator saw at least one
  LoRa preamble once.

Even so, nothing at all decoded, despite three TTN gateways and an active
Meshtastic community within about 1 km. That is very unlikely on a working
RF path, at least on 868.1 MHz SF7, the busiest LoRaWAN channel.

## Signs of life

The receiver chain does pick up LoRa frames:

- One Meshtastic header error, seen with build `175f982f`.
- Four Meshtastic CRC errors on build `b6454c1a`, between 14:44 and 14:56,
  while no listener was attached. The counters are cumulative since boot.
- A CRC error means the radio matched the preamble and sync word `0x2b`, and
  decoded the header with its own checksum, but the payload failed its CRC.

So frames with Meshtastic's sync word reach the SX1262. They are either
marginal, for example distant nodes near the sensitivity limit, or damaged
by interference. Build `549a301b` keeps CRC-failed frames, so their RSSI and
SNR can be compared with the sensitivity limit. For SF11 that limit is an
SNR of about -17.5 dB.

## Spectrum survey: does anything arrive at all?

Build `6e23cf8c` added a receive-only RSSI survey (`picoctl survey`, USB
request `5A`). Cellular downlinks transmit continuously, so they are a
reference that does not depend on any LoRa node being active. Sweep
700-960 MHz in 1 MHz steps, 250 kHz bandwidth, antenna fitted as before:

| Region | Measured | Expected with a working antenna, indoors in Potsdam |
| --- | --- | --- |
| Floor (25th percentile) | -113 dBm | about -114 dBm thermal + noise figure |
| LTE 800 downlink, 791-821 MHz | -110 dBm mean, broad hump of +3 to +5 dB around 806-820 MHz, max -103 dBm | roughly -60 to -90 dBm per 250 kHz |
| GSM/LTE 900 downlink, 925-960 MHz | -113 to -110 dBm, faint bumps at 948 and 954 MHz | similar to LTE 800 |
| 820-880 MHz | -110 to -109 dBm, about 3 dB above the rest | the 868 MHz matching passband lifting the receiver's own noise |
| Single-MHz spikes | -108 to -99 dBm at 704, 720, 736 ... 864, 880, 896, 960 MHz | multiples of 16 MHz: on-board clock harmonics |

Conclusions:

- The radio tunes correctly (the spikes move exactly with the 16 MHz
  harmonic grid) and its receiver works: it hears the near-field
  harmonics and the passband noise hump.
- Real off-air signals barely arrive. The LTE 800 downlink is visible only
  as a 3-5 dB rise, where tens of dB are normal. The antenna path is losing
  roughly 40 dB or more.
- That explains 90 minutes without a single decoded LoRa frame, and the
  few CRC errors: only very strong or very close frames make it through.
- The RF switch supply is ruled out: `ANT_SW` is fitted to 3V3
  (hypothesis 4).
- Most likely causes, in order: an SMA versus RP-SMA mismatch between the
  pigtail and the antenna (no centre contact; very common), an IPEX
  connector that is not fully seated, a damaged pigtail, or the antenna on
  the wrong connector.

A quick A/B check: run `picoctl survey --from 791 --to 821` with the
antenna fitted, then removed. If both runs look the same, the antenna is
not connected electrically.

## Resolved: faulty pigtail or antenna (2026-10-21, 16:02)

With a different IPEX-to-SMA cable and a different antenna, build
`c3747393` gives:

- **Survey:** LTE 700 (758-788 MHz) and LTE 800 (791-821 MHz) downlinks now
  read -100 to -86 dBm with sharp band edges, 20+ dB above the -112 dBm
  floor. Verdict: "clear signals". GSM/LTE 900 is still faint, which is
  plausible for the 868 MHz matched front end and local tower coverage.
- **Meshtastic (LongFast, 869.525 MHz SF11/250):** 7 frames in 3 minutes,
  0 CRC errors. Six decrypted on the default LongFast key (positions and
  telemetry), one on another channel (`0x38`) without a key. SNR -12 to
  -7 dB, RSSI around -111 dBm. These are weak but well within SF11's -17.5
  dB limit, relayed over up to 3 hops.

So the earlier ~90 minutes of silence was the old pigtail or antenna,
not the firmware. The noise floor did not change (it is the receiver's
own), which is why the noise statistics alone could not reveal the fault;
the spectrum survey did.

## EU868 scan with the working antenna (2026-10-21, 16:09-16:37)

18 settings, 90 s each, receive-only. The antenna in use is a 5-6 GHz
type, so 868 MHz sensitivity is still below what a matched antenna gives.

| Setting | Frames | CRC / header errors | Noise now / max (dBm) |
| --- | --- | --- | --- |
| Meshtastic LongFast, SF11/250 | 2 | 0 / 0 | -111 / -110 |
| Meshtastic MediumFast SF9, ShortFast SF7, MediumSlow SF10 (250 kHz) | 0 | 0 / 0 | -112 / -110 |
| LoRaWAN SF7/125 on 868.1, 868.3, 868.5, 867.1-867.9 | 0 | 0 / 0 | -115 to -114 / -113 to -112 |
| LoRaWAN SF9/125 on 868.1, 868.3, 868.5 | 0 | 0 / 0 | -115 to -113 / -113 |
| LoRaWAN SF12/125 on 868.1 and 868.5 | 0 | 0 / 0 | -115 to -114 / -113 to -112 |
| LoRaWAN SF12/125 on 868.3 | 0 | 0 / 0 | -114 / **-104** |

- Potsdam's Meshtastic mesh uses LongFast only. Over the afternoon,
  frames arrived about every 30-60 s, from at least 9 nodes, relayed over
  up to 5 hops. Besides the default key there were channels `0x38`, `0x03`
  and `0x30`, which we have no key for. The Potsdam channel (`0xe3`) was
  not heard yet.
- Every packet RSSI is about -111 dBm, the noise floor. That is expected
  at negative SNR: packet RSSI measures signal plus noise.
- No LoRaWAN uplinks. That is unsurprising: a typical end device sends
  every 5-60 minutes, picking one of 8 channels and an ADR-chosen SF, so a
  90 s dwell on one channel/SF catches a given device only a few percent of
  the time.
- One energy burst 9 dB above the floor on 868.3 MHz (SF12 step) did not
  decode. It may be LoRa at another SF or sync word, FSK, or another SRD
  device.

## Better antenna (2026-10-21, 16:52)

A different antenna, in place of the 5-6 GHz one:

- **Survey:** LTE 800 is +34 dB above the floor, up from +21 dB, with
  peaks around -74 dBm; GSM/LTE 900 is now visible (935, 955 MHz).
- **LongFast, 5 minutes:** 8 frames, 0 CRC errors.
  - Strongest frames: SNR +4 dB, RSSI -102 dBm, where earlier every frame
    sat at -7 to -12 dB. That is about 11 dB more signal.
  - Weakest frames decoded at -21.75 and -19.25 dB SNR, below the nominal
    SF11 limit.
  - The same packet (`0xc9b32c4f`) arrived twice, via two relays: once at
    -21.75 dB and once at +3.75 dB.
  - New channel hashes: `0xae`, plus `0x03`, `0x30` and `0x38` seen before.
    Still no Potsdam channel (`0xe3`).

## Other Meshtastic channels and presets (2026-10-21, 17:00-17:22)

EU868 Meshtastic uses 869.4-869.65 MHz. All 250 kHz presets share 869.525
MHz; 125 kHz presets use one of two slots chosen by a hash of the channel
name. 3 minutes each, receive-only:

| Setting | Frames | Noise max (dBm) |
| --- | --- | --- |
| LongFast SF11/250, 869.525 | 3 new (13 in ring) | -101 |
| LongModerate SF11/125, 869.4625 and 869.5875 | 0 | -100 |
| LongSlow SF12/125, 869.4625 and 869.5875 | 0 | -104 / -101 |
| ShortTurbo SF7/500 and LongTurbo SF11/500, 869.525 | 0 | -103 / -99 |

The noise maxima in the other presets are LongFast transmissions from the
overlapping channel, not separate traffic. Potsdam runs on LongFast only.

Channels seen on LongFast (by channel hash):

| Hash | Readable | Notes |
| --- | --- | --- |
| `0x08` | yes, default LongFast | positions, telemetry, node info; most traffic |
| `0x57` | yes, default key under another name | node info from one node; found by the key fallback |
| `0x03`, `0x30`, `0x38`, `0xae` | no | private keys; one or two nodes each |
| `0xe3` (Potsdam) | - | not heard yet |

`picoctl listen` now tries every known key, including the public default
key, on packets with an unknown hash and accepts only strict protobuf
parses on known ports. The private channels cannot be read without their
PSKs.

## Hypotheses, in order of likelihood

1. **Weak signals at an indoor location.**
   - Meshtastic nodes are probably sparse and distant.
   - LoRaWAN end devices are not necessarily near the gateways' location
     either; a gateway near us only means the devices it hears are within
     a few kilometres.
   - SF7 is the least sensitive LoRaWAN setting; devices further away use
     SF9 to SF12, which a single SF7 listener never hears.
2. **Antenna performance.** The HAT uses an IPEX-to-SMA pigtail with a small
   antenna. Check that the IPEX connector has clicked in fully and that the
   antenna is an 868 MHz type, not a 433 MHz or 2.4 GHz one.
3. **TCXO voltage.** Meshtastic uses 1.8 V; Waveshare's demo and this
   firmware use 1.7 V. Calibration succeeds, so this is unlikely to matter.
4. **Ruled out: RF-switch supply on GP22.** With the HAT removed
   (2026-10-21), the board shows `ANT_SW  A 3V3  B GP22` under the Pico,
   with a 0 ohm resistor (first taken for an LED) selecting the supply. It
   sits on **A**, so the PE4259 switch is always powered from 3V3. Build
   `c3747393` drives GP22 high anyway, which supports boards fitted with
   option B and is harmless with A (GP22 is then unconnected).

Audit of the radio configuration against the SX1262 datasheet, all correct:

- Frequency word: f x 2^25 / 32 MHz.
- Image calibration for 863 to 870 MHz: `0xd7 0xdb`.
- Modulation parameters and LDRO.
- Packet parameters: explicit header, CRC on, normal IQ.
- IQ errata register `0x0736`.
- Sync-word register `0x0740`.
- DIO2 as RF switch and TCXO on DIO3.
- Continuous RX.

## Diagnostics added

- **Noise floor** (from the build after `377bf7f6`).
  - The firmware samples `GetRssiInst` every 250 ms while idle in RX. It
    keeps the latest, minimum and maximum value since configuration.
  - Visible on the web page, in `/api/status` and every 60 s in
    `picoctl listen`.
  - With a working antenna, indoor 868 MHz noise usually sits clearly above
    the SX1262's internal floor, and the maximum jumps when nearby devices
    transmit.
  - A flat floor near -120 dBm or lower that does not change when the
    antenna is touched or removed points at an open RF path (hypothesis 1
    or 2).
- **CRC-failed frames are kept** (from build `549a301b`). They are flagged and
  never decoded, and they show RSSI, SNR and hex in `picoctl listen` and on
  the page.
- **LoRaWAN header decoding in `picoctl listen`.** Shows the message type,
  DevAddr with NetID and operator, FCnt16, FPort, FOpts, flags, and the EUIs
  of join requests.

## Improvements made

- RX boosted gain (`0x08ac = 0x96`), matching Meshtastic.
- Host-side LoRaWAN MAC header decoder, which recognises TTN DevAddrs.
- `picoctl listen --timeout` accepts up to 86400 s for long captures.
- Noise-floor statistics: firmware, dashboard, USB record and `picoctl`.
- Receive-only spectrum survey, 150-960 MHz: firmware, USB request `5A`
  and `picoctl survey` with a carrier-versus-spur verdict.
- Documented that `Invoke-WebRequest` in Windows PowerShell 5.1 sends
  `Expect: 100-continue`, which the strict HTTP parser rejects. Use
  `curl.exe`.

## Proposed next steps

1. Done: the antenna path was fixed by replacing the pigtail and antenna.
   Repeat the LoRaWAN 868.1/868.3/868.5 MHz sweep, now on a working path.
   Optionally test the old cable and antenna separately to find which one
   was faulty.
2. Listen for longer, for example overnight. Meshtastic node info is sent
   every few hours and position about every 15 minutes, so a small mesh can
   be quiet for long stretches.
3. Add a USB vendor request to change the radio profile from `picoctl`, so
   that sweeps do not depend on Wi-Fi or HTTP.
4. Add optional channel hopping: cycle 868.1/868.3/868.5 MHz and SF7 to
   SF12 with a configurable dwell time.
5. Add an inverted-IQ option to observe LoRaWAN downlinks on RX2 (869.525
   MHz SF12) and RX1.
6. Optionally tolerate `Expect: 100-continue` in the HTTP parser by sending
   `100 Continue` or ignoring the header.
