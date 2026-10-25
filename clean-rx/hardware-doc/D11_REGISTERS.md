# D11 register reference

[Manual contents](README.md)

## Address and access conventions

D11 base is `0x18001000`. Offsets below are byte offsets from that base.
Use the stated access width. Two adjacent halfword registers are not
necessarily a safe 32-bit read/modify/write pair.

"Control" does not imply that every bit is read/write. "Status" does not
specify every clear-on-read or write-to-clear behavior. Operations and
fields not listed here are unspecified.

## Core and object-access registers

| Offset | Width | Function |
| --- | --- | --- |
| `0x00c` | 32 | BIST status |
| `0x100` | 32 | Interrupt control |
| `0x120` | 32 | MACCONTROL |
| `0x124` | 32 | MACCOMMAND |
| `0x128` | 32 | MAC interrupt status |
| `0x12c` | 32 | MAC interrupt mask |
| `0x130` | 32 | Template RAM pointer |
| `0x134` | 32 | Template RAM data |
| `0x154` | 32 | PSM debug observation |
| `0x158` | 32 | PHY debug observation; bit `0x2` is used as TX-active indication |
| `0x15c` | 32 | MAC hardware capabilities |
| `0x160` | 32 | Object address/selector |
| `0x164` | 32 or 16 | Object data, low halfword at `0x164` |
| `0x166` | 16 | Object data, high halfword |
| `0x180` | 32 | TSF timer low word |
| `0x190` | 32 or paired 16 | TSF CFP maximum-duration register |
| `0x1e0` | 32 | D11 clock control/status |

### MACCONTROL

| Mask | Meaning |
| --- | --- |
| `0x00000001` | MAC enable |
| `0x00000002` | PSM run |
| `0x00000004` | PSM jump/reset sequencing control |
| `0x00000400` | Internal hardware-register access enable |

The start-control sequence uses low-bit settings `0`, `4`, `6`, `2`,
preserving the selected stopped-MAC baseline. This starts PSM execution
without treating the low bits as an ordinary software state number.

Clearing PSM run pauses the PSM but need not disable reception. Clearing
MAC enable changes receive behavior. To update PSM-owned TX state while
keeping reception enabled, clear only PSM run, check receive/response
ownership, perform the update, then restore the prior control value.

Two useful complete operating settings are:

| Setting | MACCONTROL value |
| --- | --- |
| Stopped initialization baseline | `0x84020400` |
| Active MAC and PSM baseline | `0xc0020403` |
| Additional passive/promiscuous configuration mask | `0x01d00000` |

These values include fields not individually defined in this manual.
Do not substitute them for a field-preserving runtime update.

### Object access

Use selector `0x00010000 | (word_index >> 1)` for SHM halfword access,
choosing data offset `0x164` or `0x166` by index parity.
PSM program writes use 32-bit word indices without that SHM selector.
Always read back the selector before the data access.

Object-port selector ownership must be serialized across host and
controller. Pausing the PSM does not serialize two independent CPU/host
users of the object port.

## DMA register windows

| Offset | Width | D11 receive DMA |
| --- | --- | --- |
| `0x220` | 32 | Control |
| `0x224` | 32 | Posted descriptor pointer |
| `0x228` | 32 | Ring base, low |
| `0x22c` | 32 | Ring base, high |
| `0x230` | 32 | Status 0 |
| `0x234` | 32 | Status 1 |

Descriptor format and pointer interpretation are in [DMA engines](DMA.md).

## PHY and radio ports

| Offset | Width | Function |
| --- | --- | --- |
| `0x3d8` | 16 | Radio register selector |
| `0x3da` | 16 | Radio register data |
| `0x3e6` | 16 | TX-path configuration; operating setting `0x003d` |
| `0x3ee` | 16 | PHY error observation |
| `0x3fc` | 16 | PHY register selector |
| `0x3fe` | 16 | PHY register data |

A 32-bit write to `0x3fc` can combine selector in bits 15..0 and PHY
register value in bits 31..16. Do not apply that combined-write convention
to the radio port.

## Receive and copy registers

| Offset | Width | Function/known use |
| --- | --- | --- |
| `0x402` | 16 | Receive-copy SHM base |
| `0x404` | 16 | Receive-copy capacity/limit |
| `0x406` | 16 | Receive initialization control; pulse `1`, then `0` |
| `0x408` | 16 | RXE control, including stream and flush operations |
| `0x40c` | 16 | Receive setup; operating value `0x0018` |
| `0x412` | 16 | PHY status word 0 |
| `0x414..0x418` | 16 each | Adjacent receive/PHY words; `0x418` is live receive count |
| `0x41a` | 16 | RX status 1 |
| `0x41c` | 16 | RX status 2 |
| `0x420` | 16 | Address-match selector |
| `0x422` | 16 | Address-match data port |
| `0x448` | 16 | Receive FIFO/control configuration |
| `0x450` | 16 | Receive-copy setup word 0 |
| `0x452` | 16 | Receive-copy setup word 1 |
| `0x454` | 16 | Copy end/length operand |
| `0x456` | 16 | Copy next/decoded-length operand |
| `0x4d8` | 16 | Receive/PHY control configuration |
| `0x4e4` | 16 | Receive setup; operating value `0x0090` |
| `0x4f0` | 16 | Receive status observation |
| `0x4fe` | 16 | Receive setup; operating value `0x0000` |

The copy operands at `0x454/0x456` are not interchangeable with the live
count at `0x418`. Interpretation depends on the receive-copy phase.

Address-match programming selects `0x0020 | entry_offset` at `0x420`,
then writes three successive little-endian halfwords of a six-byte
address to `0x422`.

### Receive fields

| Register | Mask | Meaning |
| --- | --- | --- |
| RXE control `0x408` | `0x0002` | Streaming-enable control |
| RX status 1 `0x41a` | `0x1000` | Receive-active condition used by cleanup |
| RX status 2 `0x41c` | `0x2000` | OFDM indication used in legacy-format interpretation |
| PHY status 0 `0x412` | `0x0003` | Format field; not a standalone universal HT classifier |
| PHY status 0 `0x412` | `0x0080` | Short-preamble indication in the legacy CCK interpretation |

The `0x408` whole-register value `0x0014` is used for flush. Receive
status must still be checked for completion; writing this value is not
a guarantee that the pipeline is immediately empty.

## External-condition configuration

| Offset(s) | Width | Function |
| --- | --- | --- |
| `0x490` | 16 | PSM condition configuration |
| `0x4a0`, `0x4a2`, `0x4a4`, `0x4a6` | 16 | Condition branch-mask bank |
| `0x4a8`, `0x4aa`, `0x4ac`, `0x4ae` | 16 | Condition event/clear configuration |
| `0x4b0`, `0x4b2`, `0x4b6`, `0x4bc` | 16 | Additional condition-mask configuration |

An external-condition number has meaning only together with its mask
configuration and instruction polarity. It is not a universal packet
validity code.

## Transmit registers

| Offset | Width | Function/known use |
| --- | --- | --- |
| `0x500` | 16 | TX control |
| `0x502` | 16 | TX auxiliary/trigger control; trigger setting `0x00d0` |
| `0x508` | 16 | TX watermark/configuration |
| `0x50a` | 16 | TX watermark/configuration |
| `0x50c` | 16 | PHYCTL |
| `0x50e` | 16 | TX status |
| `0x510` | 16 | Mixed-mode PLCP rate operand |
| `0x512` | 16 | Mixed-mode PLCP length operand |
| `0x514` | 16 | PHYCTL1 |
| `0x54c` | 16 | TX template pointer/control |
| `0x568` | 16 | TX selection; template mode uses `0x0826` |
| `0x56a` | 16 | TX byte count |
| `0x570` | 16 | TX count extension/setup |
| `0x572` | 16 | TX count setup, total template length shifted left by 4 |
| `0x580..0x58e` | 16 each | Eight response mask words |
| `0x5c0..0x5ce` | 16 each | Response PLCP/header words |

### Transmit status fields

| Register | Mask | Meaning |
| --- | --- | --- |
| TX status `0x50e` | `0x0080` | MAC transmit busy |
| TX status `0x50e` | `0x0400` | Transmit completion indication |
| IFS status `0x690` | `0x0100` | PHY transmit-active indication |

A complete transmit decision must distinguish these states; do not use
one indication as a substitute for the others.

## Timers and medium state

| Offset | Width | Function |
| --- | --- | --- |
| `0x01c` | 32 | General microsecond timer used for raw-operation guards |
| `0x180` | 32 | TSF timer low word |
| `0x632` | 16 | TSF timer low halfword used for microsecond timing |
| `0x634` | 16 | Next TSF timer halfword |
| `0x688` | 16 | IFS/control selector used by response setup |
| `0x690` | 16 | IFS/medium status |
| `0x696`, `0x69a`, `0x69c`, `0x69e` | 16 | Timing/medium-control configuration |

IFS status bits `0x0001`, `0x0002`, `0x0100`, `0x0200`, `0x0400` and
`0x0800` participate in medium eligibility tests. Only `0x0100` has the
individual TX-active meaning specified here.

Sampled TSF values mark software/PSM observation time. They do not
automatically identify the physical end of a received frame or exact
start of RF transmission.
Do not assume that the general timer and the PSM-visible timer words are
interchangeable across configuration/reset transitions. For a changing
multi-word counter without a defined read latch, use a high-low-high
read and repeat if the high part changes.

## Crypto interface

| Offset | Width | Function |
| --- | --- | --- |
| `0x7c0` | 16 | Crypto mode/control |
| `0x7c2` | 16 | IV offset |
| `0x7c4` | 16 | Key-data port |
| `0x7c6` | 16 | Key configuration |
| `0x7c8` | 16 | Key index/control |
| `0x7d0` | 16 | Crypto operation control |
| `0x7d4` | 16 | Crypto length setup |
| `0x7d6` | 16 | Crypto frame length |
| `0x7d8` | 16 | Additional crypto/reset control |

The hardware includes an AES-CCMP packet-processing path. Key data,
IV placement, frame length and crypto mode must agree. A software key
stored in SHM is not automatically an installed hardware key.
See [transmit programming](TRANSMIT.md#crypto-controls).

## Coexistence controls

| Offset(s) | Width | Function |
| --- | --- | --- |
| `0x49c`, `0x49e` | 16 | RF switch/coexistence controls |
| `0x6b4`, `0x6b8`, `0x6ba`, `0x6bc` | 16 | Coexistence configuration |
| `0x6f0`, `0x6f2` | 16 | Additional coexistence configuration |

These registers interact with ChipCommon GCI. Full Bluetooth scheduling,
priority arbitration and antenna-sharing policy are unspecified.
