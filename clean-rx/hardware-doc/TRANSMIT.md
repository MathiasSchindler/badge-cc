# Transmit and medium access

[Manual contents](README.md)

## Transmit path and ownership

```text
CPU/PSM preparation -> D11 template/response storage -> TXE -> PHY -> radio
```

TXE is a shared resource. Controller-triggered packets and PSM-generated
ACK/CTS responses must not replace each other's controls or templates.
An ownership protocol is required before either processor updates them.

Transmitting a raw frame does not automatically implement association,
rate adaptation, retries, security, aggregation or regulatory policy.
Those functions require correct software and hardware configuration.

Use only frequencies, output settings and packet types permitted for the
device and its operating region.

## Template programming

The CPU template pointer is at `0x18001130`, with the 32-bit data port
at `0x18001134`.

1. Establish exclusive template/TXE ownership.
2. Set the template pointer.
3. Write a complete word-aligned template, initializing tail padding.
4. Reset the pointer and read back the template.
5. Program the count and PHY controls.
6. Select the template and trigger only when medium policy permits.

In the raw template mode described here, the template includes the
six-byte PHY prefix followed by the MPDU, including its supplied FCS.
Automatic FCS insertion modes are not specified here.

The selection setting `0x0826` at D11 offset `0x568` is used with this
template path. D11 offset `0x56a` holds total template byte count;
`0x572` receives that count shifted left by 4, with `0x570` set to zero.
These count settings must agree with the actual selected template.

## PHY controls

| Mode | PHYCTL at D11 `0x50c` |
| --- | --- |
| DSSS/CCK, long preamble | `0x0040` |
| CCK, short preamble | `0x0050` |
| Legacy OFDM | `0x0041` |
| HT20 mixed mode | `0x0042` |

PHYCTL1 is at D11 offset `0x514`.

### Legacy DSSS/CCK rates

| Rate, Mbit/s | PLCP SIGNAL byte | PHYCTL1 |
| --- | --- | --- |
| 1 | 10 | `0x0002` |
| 2 | 20 | `0x0802` |
| 5.5 | 55 | `0x1002` |
| 11 | 110 | `0x1802` |

The 1-Mbit/s rate uses long preamble in this configuration.
The PLCP duration is expressed in microseconds and must agree with
the payload rate/length. The 11-Mbit/s length-extension rule and PLCP
CRC remain part of correct header construction.

### Legacy OFDM rates

| Rate, Mbit/s | SIGNAL rate nibble | PHYCTL1 |
| --- | --- | --- |
| 6 | `0x0b` | `0x0002` |
| 9 | `0x0f` | `0x0202` |
| 12 | `0x0a` | `0x0802` |
| 18 | `0x0e` | `0x0a02` |
| 24 | `0x09` | `0x1002` |
| 36 | `0x0d` | `0x1202` |
| 48 | `0x08` | `0x1902` |
| 54 | `0x0c` | `0x1a02` |

The SIGNAL length, parity and reserved/tail bits must be consistent.
Six bytes of template prefix are used even though the serialized
legacy OFDM SIGNAL field occupies fewer bytes.

### HT20, one spatial stream, long guard interval

| MCS | Data bits per symbol | Rate, Mbit/s | PHYCTL1 |
| --- | --- | --- | --- |
| 0 | 26 | 6.5 | `0x0002` |
| 1 | 52 | 13 | `0x0802` |
| 2 | 78 | 19.5 | `0x0a02` |
| 3 | 104 | 26 | `0x1002` |
| 4 | 156 | 39 | `0x1202` |
| 5 | 208 | 52 | `0x1902` |
| 6 | 234 | 58.5 | `0x1a02` |
| 7 | 260 | 65 | `0x1c02` |

For payload length `L`, including FCS:

```text
symbols = ceil((22 + 8*L) / data_bits_per_symbol)
mixed-mode L-SIG length operand = 3*symbols + 9
```

The mixed-mode PLCP registers accept separate operands:

| D11 offset | Value |
| --- | --- |
| `0x510` | Legacy rate operand `0x0b` |
| `0x512` | Calculated L-SIG length operand |

These are not a packed serialized L-SIG word. HT signaling flags, length
and CRC must also be correct in the selected HT template.
This table does not specify HT40, short GI, STBC, LDPC or multiple streams.

## Trigger and completion

D11 auxiliary control at offset `0x502` uses trigger setting `0x00d0`.
Triggering is distinct from actual RF start.

Completion involves at least:

1. TX status `0x50e` mask `0x0080` no longer busy.
2. TX status `0x50e` mask `0x0400` completion indication.
3. IFS status `0x690` mask `0x0100` no longer PHY TX-active.

Capture transient completion events while the operation owns TXE;
a later read may not retain the same event context.
Do not recycle the template based solely on one instantaneous register
sample.

MAC drain can precede the end of the transmitted PHY waveform.
Preserve the RF path until the PHY tail is complete.

### Airtime bounds

For legacy OFDM at rate `R` Mbit/s:

```text
N = ceil((16 + 8*L + 6) / (4*R))
airtime_us = 20 + 4*N + 6
```

The final 6 microseconds account for the 2.4-GHz ERP signal extension.
For the HT20 mixed-mode configuration above:

```text
airtime_us = 42 + 4*symbols
```

For DSSS/CCK:

```text
body_us = ceil(8*L / rate_Mbps)
airtime_us = body_us + (short_preamble ? 96 : 192)
```

Computed airtime is useful for bounded ownership guards. It is not an
independent measurement of physical RF start/end.

## Medium access

The PSM can combine IFS/carrier/NAV status, microsecond timer reads and
software-selected timing parameters to implement medium access.

Contention operation must:

- Establish the required uninterrupted idle interval.
- Count complete backoff slots only while eligible.
- Freeze residual backoff during busy periods.
- Re-establish the required interframe interval after activity.
- Apply EIFS after the applicable invalid reception.
- Preserve a completed post-backoff rather than endlessly reloading it.
- Give eligible receive responses priority over a pending data template.

Slot settings of 9 and 20 microseconds belong to different WLAN timing
configurations. They are not interchangeable.
The contention window, random draw, retry count and access-category
policy are software-controlled choices, not fixed SRAM hardware values.

## ACK/CTS response path

PSM-controlled responses use dedicated TX header words and TXE control.
The response header window at D11 offsets `0x5c0..0x5ce` contains:

| Offset | Meaning in this response configuration |
| --- | --- |
| `0x5c0`, `0x5c2`, `0x5c4` | Three PLCP halfwords |
| `0x5c6` | Frame control |
| `0x5c8` | Duration |
| `0x5ca`, `0x5cc`, `0x5ce` | Response receiver address |

The PSM must validate the received frame, FCS, address, frame class and
response policy before scheduling a response. Hardware presence alone
does not mean every received unicast is automatically acknowledged.

Response scheduling and template ownership extend through physical
transmit tail. Cleanup must defer for new RX, pending MAC transmission
or active PHY transmission.

A sampled timer value or chosen SIFS control setting does not establish
an exact RF EOF-to-response-start delay. Such physical timing guarantees
are unspecified here.

## Crypto controls

The raw AES-CCMP transmit configuration uses:

| D11 offset | Setting/use |
| --- | --- |
| `0x7c8` | Zero key index/control setup |
| `0x7c2` | IV offset appropriate to the supplied header |
| `0x7c6` | Key configuration `0x0110` |
| `0x7c4` | Eight successive 16-bit key words |
| `0x7c0` | Crypto mode `0x1170` |
| `0x7d4` | Zero length setup |
| `0x7d6` | Total template length |
| `0x7d0` | Crypto operation `0x0305` |

The offsets include the selected template's prefix and header layout.
Do not apply an IV offset from one header type to another.

Non-crypto preparation uses `0x0100` at `0x7c0` and `0x0300` at `0x7d0`.
Receive-restoring cleanup uses `0x8300` and `0x0300`, respectively.
These are mode settings, not a complete crypto-register bit map.

Key negotiation, packet-number allocation, replay rejection, rekey and
key lifetime remain protocol responsibilities. A working AES engine
does not implement them automatically.

## Safe completion cleanup

Before clearing auxiliary/select state:

1. Exclude a new receive start and live RX activity.
2. Require MAC transmit drain.
3. Require inactive PHY transmission.
4. Clear TX auxiliary and TX selection.
5. Restore the intended crypto and RX/TX override state.
6. Return ownership to the receive path.

Do not reset PHY clocks simply to clear a software transmit request.
On timeout, report an incomplete operation rather than assuming that
the requested frame was sent or acknowledged.
