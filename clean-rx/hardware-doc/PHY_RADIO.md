# PHY and radio

[Manual contents](README.md)

## Organization

The Wi-Fi RF path uses an LCN20 PHY and Radio 20691 register interface.
The documented configuration is for 2.4-GHz WLAN operation with a
37.4-MHz reference crystal.

PHY registers, PHY table entries and radio registers are different
address spaces. Equal numerical indices do not identify the same storage.

## Indirect register access

| Interface | Selector | Data | Register width |
| --- | --- | --- | --- |
| PHY | `0x180013fc` | `0x180013fe` | 16 bits |
| Radio | `0x180013d8` | `0x180013da` | 16 bits |

PHY read:

```text
write16(PHY_SELECTOR, register_index)
value = read16(PHY_DATA)
```

PHY write supports a combined 32-bit operation:

```text
write32(PHY_SELECTOR, register_index | (value << 16))
```

Radio read/write selects the index with a 16-bit selector write, then
accesses the 16-bit data port. Do not assume the PHY combined-write
operation applies to radio registers.

A masked modification is:

```text
new = (old & ~mask) | (value & mask)
```

Serialize the selector/data sequence. Preserve fields outside the mask.

## PHY table port

| PHY register | Function |
| --- | --- |
| `0x00d` | Table ID |
| `0x00e` | Entry offset |
| `0x456` | Data low word |
| `0x457` | Data high word for 32-bit entries |
| `0x9a5`, mask `0x0002` | Table-access clock override |

For table access:

1. Save PHY `0x9a5`.
2. Set mask `0x0002`.
3. Select table ID and entry offset.
4. Transfer entries sequentially.
5. Restore the saved clock-control value.

For 32-bit writes, write high halfword at `0x457` before low halfword at
`0x456`. The low-word operation advances the entry position in the
sequential write form.
Eight- and sixteen-bit table data use the low data port.

## Calibration ROM data

A 2,044-byte packed calibration-data span starts at `0x1e09020c`.
The following offsets are byte offsets relative to that base:

| Destination | ROM offset | Entries | Entry width |
| --- | --- | --- | --- |
| PHY table 2 | `0x446` | 128 | 16 bits |
| PHY table 1 | `0x546` | 64 | 16 bits |
| PHY table 20 | `0x5c6` | 256 | 8 bits |
| PHY table 23 | `0x6c6` | 154 | 16 bits |
| PHY table 0 | `0x36e` | 108 | 16 bits |
| Gain table 17 | `0x354` | 26 | 8 bits |
| Gain table 18 | `0x1d4` | 96 | 32 bits |
| Gain table 13 | `0x0a4` | 76 | 32 bits |
| Radio `0x0a9..0x0dc` | `0x070` | 52 | 8 bits, widened to register words |
| Radio `0x0dd..0x114` | `0x000` | 56 | 16 bits |

Multi-byte packed entries are little-endian.
These offsets describe data organization, not callable ROM functions.
They do not specify the entire ROM or calibration interchangeability
between silicon revisions.

## Channel centers

| Channel | Center, MHz | Channel | Center, MHz |
| --- | --- | --- | --- |
| 1 | 2412 | 8 | 2447 |
| 2 | 2417 | 9 | 2452 |
| 3 | 2422 | 10 | 2457 |
| 4 | 2427 | 11 | 2462 |
| 5 | 2432 | 12 | 2467 |
| 6 | 2437 | 13 | 2472 |
| 7 | 2442 | 14 | 2484 |

For channels 1..13, `f_MHz = 2407 + 5*channel`.
Channel 14 is a separate center and does not imply the same permitted
modulation/regulatory use as channels 1..13.

Channel spacing is not instantaneous receive bandwidth.
The HT20 programming described in this manual is a 20-MHz WLAN mode;
legacy DSSS/CCK has different spectral characteristics. Neither establishes
an arbitrary wideband capture mode.

## Radio synthesizer programming

Channel setup writes 31 radio registers in this order:

```text
62 53 54 57 58 64 51 5c 5d 5a 5b 63 56 59 61 55
5f 52 4e 4f 4d 4c 4a 4b 48 49 45 46 18 3c 86
```

All indices above are hexadecimal.
Register `0x55` holds the center-frequency number in MHz for these
settings. The pair `0x4e:0x4f`, high word first, uses:

```text
divider_word = floor(f_MHz * 60 * 65536 / 187)
R[0x4e] = divider_word >> 16
R[0x4f] = divider_word & 0xffff
```

This is the programming relation for the listed 37.4-MHz channel
settings. It is not a definition of the full synthesizer topology or a
specification of usable frequencies outside those settings.

### Channel-independent values

| Radio register | Value | Radio register | Value |
| --- | --- | --- | --- |
| `0x62` | `0x004b` | `0x53` | `0x0176` |
| `0x54` | `0x0013` | `0x57` | `0x3032` |
| `0x58` | `0x0016` | `0x64` | `0x0003` |
| `0x51` | `0x7e41` | `0x5c` | `0x0600` |
| `0x5d` | `0x0a00` | `0x5a` | `0x03d9` |
| `0x5b` | `0x0027` | `0x63` | `0x0025` |
| `0x56` | `0x0000` | `0x59` | `0x0991` |
| `0x61` | `0x0000` | `0x5f` | `0x0000` |
| `0x52` | `0x0000` | `0x4d` | `0x0008` |
| `0x4c` | `0x583f` | `0x4a` | `0x1212` |
| `0x4b` | `0x142f` | `0x48` | `0x1f1f` |
| `0x49` | `0x1f1f` | `0x18` | `0x0000` |

### Channel-dependent values

| Ch | `0x55` | `0x4e` | `0x4f` | `0x45` | `0x46` | `0x3c` | `0x86` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `0x096c` | `0x0305` | `0xe75b` | `0x5120` | `0x6127` | `0x6009` | `0x703f` |
| 2 | `0x0971` | `0x0307` | `0x820d` | `0x5120` | `0x6127` | `0x6009` | `0x703f` |
| 3 | `0x0976` | `0x0309` | `0x1cbf` | `0x5120` | `0x6127` | `0x6009` | `0x703f` |
| 4 | `0x097b` | `0x030a` | `0xb771` | `0x5120` | `0x6127` | `0x6009` | `0x703f` |
| 5 | `0x0980` | `0x030c` | `0x5223` | `0x5121` | `0x6127` | `0x6009` | `0x703f` |
| 6 | `0x0985` | `0x030d` | `0xecd5` | `0x5121` | `0x6127` | `0x6009` | `0x703f` |
| 7 | `0x098a` | `0x030f` | `0x8787` | `0x5121` | `0x6127` | `0x6008` | `0x703f` |
| 8 | `0x098f` | `0x0311` | `0x2239` | `0x5121` | `0x6127` | `0x6008` | `0x703f` |
| 9 | `0x0994` | `0x0312` | `0xbceb` | `0x5121` | `0x6127` | `0x6008` | `0x603f` |
| 10 | `0x0999` | `0x0314` | `0x579d` | `0x5121` | `0x6127` | `0x6008` | `0x603f` |
| 11 | `0x099e` | `0x0315` | `0xf24f` | `0x5121` | `0x6127` | `0x6007` | `0x603f` |
| 12 | `0x09a3` | `0x0317` | `0x8d01` | `0x5121` | `0x6127` | `0x6007` | `0x603f` |
| 13 | `0x09a8` | `0x0319` | `0x27b3` | `0x5121` | `0x6128` | `0x6007` | `0x603f` |
| 14 | `0x09b4` | `0x031d` | `0x015e` | `0x5121` | `0x6128` | `0x6007` | `0x603f` |

Frequency programming alone is insufficient: analog tuning, VCO
calibration, receive timing and DC calibration must agree.

## VCO calibration control

After channel-register programming:

1. Clear radio `0x3d` mask `0x1000`, `0x71` mask `0x1000` and
   `0x72` mask `0x2000`.
2. Set `0x8a` mask `0x0100`; clear `0x1b` mask `0x0700`.
3. Clear then set `0x51` bit 0, with a settling delay.
4. Set `0xa0` masks `0x0100` and `0x0200`; pulse the corresponding
   `0x42` masks `0x0010` and `0x0020` low then high.
5. Pulse `0x42` masks `0x0080` and `0x0040` high then low.
6. Allow calibration settling, then poll radio `0x65` bit 0 with a bound.
7. Clear `0x8a` mask `0x0100` after the completion check and settling.

Radio `0x65` bit 0 is the calibration-status predicate used by this
sequence. Its full semantics, freshness and relationship to independent
RF lock are unspecified. An asserted value does not by itself prove
the requested RF center, sensitivity or out-of-band reception.

## Receive timing

With center frequency `f` in MHz:

```text
mu = floor((f * 65536 + 2) / 5)
period = 1280 / gcd(1280, f)
```

| PHY field | Programming value |
| --- | --- |
| `0x9ad`, full word | `mu & 0xffff` |
| `0x9ae`, mask `0x03ff` | `mu >> 16` |
| `0x9ae`, mask `0x4000` | Clear |
| `0x9b1`, mask `0x1fff` | `period` |
| `0x9ac`, mask `0x0400` | Set |
| `0x657`, mask `0x0fff` | `floor((floor(f*4/5)+1)/2)` |
| `0x658`, mask `0x0fff` | `floor((floor(5242880/f)+1)/2)` |

These Farrow/timing values must track the selected center frequency.
Changing only the radio divider does not configure the complete receiver.

## RC and ADC calibration

Radio calibration uses two passes, for LPF and ADC:

| Field | LPF pass | ADC pass |
| --- | --- | --- |
| `0x1e`, mask `0x0030` | `0x0000` | `0x0010` |
| `0x8f`, mask `0xff00` | `0x1c00` | `0x7000` |
| `0x90`, mask `0x1fff` | `0x022d` | `0x00f0` |
| Scale numerator | 101541 | 102051 |

Set `0x1e` mask `0x0040`, pulse its bit 0 low/high, allow settling and
set `0x8f` bit 0. Poll `0x91` bit 0, rejecting an all-ones read.
Clear the trigger before reading the results:

```text
n0 = R[0x92] & 0x1fff
n1 = R[0x93] & 0x1fff
require n1 > n0
rc = floor(scale * (n1-n0) / (37400000 >> 12))
```

Radio `0x94` bits selected by `0x03e0`, shifted right 5, provide the
DAC-buffer calibration quantity.

The ADC result feeds coefficient programming; it is not an ADC sample
stream or a calibrated RF power value. Full ADC transfer characteristics
and every coefficient field are unspecified.

## DC calibration

DC setup uses PHY registers `0x965`, `0x969`, `0x975` and control
`0x440` mask `0x0004`. Pulse PHY `0x44a` bit 0, wait, then test:

| PHY register | Completion predicate |
| --- | --- |
| `0x978` | Low byte equals `0xff` |
| `0x96f` | Low 12 bits equal `0xfff` |

Bound repetitions and report failure if the predicates do not hold.
After calibration, clear PHY `0x440` mask `0x0004` and restore the
operating selection at `0x975`.

The completion words are not raw IQ samples and do not specify the
receiver's calibrated noise figure or sensitivity.

## Receive enable and AGC

PHY `0x440` bit 0 controls the receive-enable/deaf transition used during
tuning: clear before retune and set after receive-path setup.
PHY `0x44a` mask `0x0100` provides the radio-reset pulse used at cold start.

Gain configuration includes PHY tables 13, 17 and 18, analog gain fields
and additional PHY AGC registers. Complete gain-to-dB conversion, RSSI
calibration and adaptation-loop behavior are unspecified.

A cold start requires common/gain table loading, PHY defaults, radio
power sequencing, RC/ADC/TIA setup, channel/VCO programming, receive
sequencing, DC calibration and AGC configuration. The channel tables
alone are not a complete cold-start image.

## Transmit overrides

The explicit TX-inhibit state uses:

| Bank/register | Mask | Masked value |
| --- | --- | --- |
| PHY `0x44d` | `0x4000` | `0x0000` |
| PHY `0x44c` | `0x4000` | `0x4000` |
| PHY `0x939` | `0x0800` | `0x0000` |
| PHY `0x938` | `0x0800` | `0x0800` |
| Radio `0x79` | `0x0001` | `0x0000` |
| Radio `0xa4` | `0x0004` | `0x0004` |

Automatic PHY-sequenced turnaround releases owned overrides:

- Clear radio `0x79` mask `0x0007`.
- Clear radio `0xa4` mask `0x0004`.
- Clear PHY `0x44c` mask `0x4000`.
- Clear PHY `0x938` mask `0x0800`.

Forced enable and automatic sequencing are not equivalent.
Preserve unrelated bits. Configure transmit filters, gain and timing
before releasing the inhibit state.

## Transmit timing and gain

TX Farrow programming uses PHY `0x980/0x981` and `0x984/0x985` for
duplicate high/low timing values, with sample synchronization at `0x986`
and enable mask `0x0002` at `0x987`.

| Channel | TX timing value |
| --- | --- |
| 1 | 6677499 |
| 2 | 6663685 |
| 3 | 6649929 |
| 4 | 6636229 |
| 5 | 6622585 |
| 6 | 6608998 |
| 7 | 6595466 |
| 8 | 6581989 |
| 9 | 6568567 |
| 10 | 6555200 |
| 11 | 6541888 |
| 12 | 6528629 |
| 13 | 6515424 |
| 14 | 6483948 |

The synchronization value is `f / gcd(f, 1920)`.
The timing high fields use the low eight bits of the value shifted right
16; the low fields receive the low 16 bits.

PHY table 0 entries 96..99 hold an OFDM IQ/gain setting, and entries
112..115 hold the corresponding BPHY setting:

```text
OFDM: 0x0400, 0x0000, 0x0000, baseband_multiplier
BPHY: 0x4000, 0x0000, 0x0000, baseband_multiplier
```

Baseband multiplier is a digital amplitude setting, not an output-power
value in dBm. Antenna-port power, linearity and emission compliance are
not specified by that number.
