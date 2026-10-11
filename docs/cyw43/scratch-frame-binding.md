# Complete known frames in RXE scratch

[Guide](README.md) · [Receive path](rx-path.md) · [Detailed experiment](../../experimental/cyw43/r3/rxe-observe/frame-binding.md)

**The RXE input scratch has now been bound to 28 distinct complete known
vendor-received frames**, including the MAC header, full nonce/payload and
802.11 FCS, across three controlled packet lengths.

This page records **vendor-firmware/vendor-PSM** reception. The later
[original-PSM prefix experiments](rx-path.md#versioned-post-request-readback-and-retained-takeover)
use vendor-retained initialization but capture only a checked six-byte PLCP
prefix. They do not inherit these28 frames' nonce/FCS binding, even when
the scratch address or a PLCP value matches.
The separate [corrected original-PSM fresh-frame profile](rx-path.md#fresh-frame-attribution-under-guarded-retained-initialization)
has its own three complete post-ready challenge-bound frames and controls;
it still inherits vendor initialization and is not a cold-source result.

## Measured geometry

The copy destination remains SHM word `0x620`, with configured length 180
bytes. For every successfully bound plain CCK data frame:

| Byte offset within scratch | Measured content |
| ---: | --- |
| 0–1 | Duplicate of the first PLCP signal/service word; purpose still unknown |
| **2–7** | Complete six-byte CCK PLCP, including CRC16 |
| **8–31** | Complete 24-byte AP-to-station MAC header |
| 32–39 | LLC/SNAP and lab EtherType |
| 40 onward | Exact nonce-labelled lab payload, including its own CRC32 |
| End of MPDU | Four-byte 802.11 FCS, independently verified |

The verifier derives the MAC start from the actual nonce location and legal
header forms, then requires exact addresses, LLC, all payload bytes, FCS and
PLCP. It does not assume offset 8 or accept a nonce match alone.

| Ethernet bytes | MPDU including FCS | PLCP length field | Distinct fully bound frames |
| ---: | ---: | ---: | ---: |
| 66 | 88 bytes | 64 us | **10** |
| 98 | 120 bytes | 88 us | **11** |
| 130 | 152 bytes | 111 us | **7** |

All three sessions ran on channel 6 with fresh nonces and length-bound
normal vendor AP/station builds. There were 144 full-scratch bursts;
39 nonce-bearing candidates were retained and **11 rejected**, rather than
normalized or treated as complete frames.

Raw bindings:
[66 bytes](../../experimental/cyw43/r3/rxe-observe/runs/scratch-66-ch6-hmonb0fd/binding-summary.json),
[98 bytes](../../experimental/cyw43/r3/rxe-observe/runs/scratch-98-ch6-mo7xfgth/binding-summary.json),
[130 bytes](../../experimental/cyw43/r3/rxe-observe/runs/scratch-130-ch6-viwjc22c/binding-summary.json).

## Capture is not atomic

The observer reads all 180 bytes using halfword SHM accesses, preserving
the object selector and ARM interrupt mask. Only nonce-bearing candidates
are exported; bytes past the expected FCS end are zeroed to avoid unrelated
stale tail content.

In one 130-byte case, the early RX-length register sample was **110**,
but the later scratch copy contained the complete **152-byte MPDU** and
valid PLCP/FCS. More bytes arrived during acquisition. The other 27 bound
records had RX length equal to PLCP plus MPDU. This illustrates why a
single length read or unchanged control value is not an atomic snapshot.

The complete-byte/FCS match establishes the copied frame, not universal
byte-ready timing, observer overhead, or the exact effects of precopy
commands `0x40`/`0x20c0`.

## What this closes—and what it does not

It closes the **input-scratch-to-known-frame binding for these tested plain
CCK frames**. It does not establish QoS/aggregation/decryption cases, a
source-only receiver, the separate output-header/FIFO/DMA prefix at offset
38, physical DMA ownership or RF silence. The PSM/PHY source implementation
still requires qualified cold startup and a fresh-copy producer.

The observer stays inside the existing R1 reservation: **6,424 / 6,592
bytes**, with no trace BSS and a checked 408-byte static stack bound.
Original vendor microcode/template/NVRAM/CLM and normal radio policy remain
unchanged. The source recovery baseline remains **5,840 bytes** and
reset-held; later larger source startup images have separate ownership and
qualification contracts. Current original-PSM prefix captures are a
separate vendor-retained milestone, not a complete cold source receiver.

Both badges recovered to BOOTSEL after all sessions. The
[final source-baseline recovery](../../experimental/cyw43/r3/rx-memory/runs/memory-1.xQXYcIYy/report.jsonl)
passed independently of these vendor measurements.
