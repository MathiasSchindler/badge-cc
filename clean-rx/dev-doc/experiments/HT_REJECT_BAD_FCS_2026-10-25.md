# Controlled HT rejection calibration, 2026-10-25

## Outcome

Two ordinary ACK60/MCS7/channel-6 controls passed, one in each badge
direction, including returned ACKs and passive Pico full-fixture observations.
The next, deliberately bad-FCS B-to-A probe produced a complete, fresh-identity
MAC-byte match in badge A's corrected rejection record, including exactly the
programmed FCS-bit change. The campaign stopped immediately after that third
attempt; the fourth authorized slot was not used.

This exercises the **active rejection-record path for one controlled frame**.
The passive Pico did not retain a corresponding rejection, and the earlier
spontaneous reception failure was not reproduced. **General rejected-copy
freshness, passive rejection capture and the radio root cause remain open.**

All three original flash images were restored and independently verified in
full, byte-for-byte and by hash. Final probes confirmed all three in BOOTSEL.
There were no collection or restoration errors.

## Question and bounded method

The [preceding investigation](HT_REJECT_COPY_2026-10-25.md) repaired the
diagnostic's SHM/SRAM ownership defects, then obtained four passing controls
with zeroed trace records. It had not exercised the corrected rejection arm.

The user expressly authorized a diagnostic change and up to four fresh
attempts: two directional positive controls followed by at most two
corrupted-FCS probes, with a passive Pico, full backup/restoration and early
stopping on useful rejection evidence, a failed control or collection error.
The badge serials remain A `08DA26F86E57F5EA`, B `9BBF88DD17D66E7D`;
the Pico is `6572BE3E4636AF51`.

All traffic retained ACK60/MCS7/channel 6, the 200-ms receive lead, clocked TX,
one submitted frame per command and the existing collection bounds. Each
attempt used a new challenge/identity checked against all preceding retained
R07/D2 ledgers.

The new, default-off `CLEAN_RX_HT_REJECT_BAD_FCS=1` control requires the
diagnostic rejected-copy trace. Only the transmitting badge/controller uses
it. For validated clocked ACK60 packets it XORs bit 7 of PLCP-inclusive
template byte 65, the last MAC FCS byte. Both template write and verified
readback use the same transformation; the input packet remains valid and
unchanged. Other packet versions, lengths and TX modes pass through unchanged.
There is no forced receive copy, changed RX threshold or receive-program
modification. Good and bad controller PSM objects are byte-identical.

Template programming alone was not an acceptance oracle: a valid delivered
frame would mean the intended negative control had not been achieved.
The independent Node fixture/CRC and retained receiver bytes determine what
was actually observed.

## Attempts

| Attempt | Direction | Control | Challenge | Target delivery / returned ACK | Passive full fixture |
| --- | --- | --- | --- | --- | --- |
| 1 | B to A | Normal | `508be1c9d5f15393792cc43c0007` | Yes / yes | Yes |
| 2 | A to B | Normal | `93a98e9834df96f0252a2d3c0007` | Yes / yes | Yes |
| 3 | B to A | FCS bit flipped | `a000b0abfda41619862a513c0007` | No / no | No |

These are two positive controls and one intentional negative probe, **not**
an intermittent-loss acceptance rate. Together with the preceding eight
attempts, the D2 copy investigation has made eleven attempts.

### Active rejection

Badge A retained:

- One cause-2 rejection, RXE count 66, matching MCS7/length-60 HT-SIG and
  fresh MAC identity; no correlated F2 delivery or returned ACK.
- A committed copy record bound to the same cause, TSF (`986973`) and length.
- Two identical complete 66-byte reads: the six HT-SIG bytes and all 60 MAC
  bytes match the programmed negative fixture.
- Calculated MAC CRC `0xdb20e7ed`, retained FCS `0x5b20e7ed`: XOR
  `0x80000000`, exactly the injected bit. No MAC-body byte differs.
- Stable before/after copy operands
  `[0x0800,0x0800,0x0f40,0x0000,0x8000]`, with threshold 128.
- Unchanged initial/final RXE length/status tuple
  `(66,0x4062,0x0840)`.

Thus a complete matching short bad-FCS fixture can be observed even below the
128-byte stream threshold and without changing the normal GoodFCS-copy policy.
The threshold, rejection branch or stable operand tuple alone cannot establish
that the retained body must be stale. Conversely, this one byte-exact control
does not prove that every spontaneous rejected-frame read is fresh.

The generic decoder deliberately continues to report
`shm_copy_freshness_proven:false`. The control-specific full programmed-byte
comparison is retained separately; it is not a new general freshness claim.
Nor does this control retroactively qualify the defective prototype captures.

### Passive limitation

The Pico observed both normal controls but had no correlated F2 or committed
rejection record on the negative probe. Its uncommitted record remained
entirely zero. Its separate last-candidate trace had 110 candidates and did
not retain the requested SIG; this is not a nonce-bound frame identity.

All retained Pico reports confirm mode 25, the observer marker, zero TX
attempts/submissions and zero configured responses. Its silence cannot
establish transmitter silence: the other receiver retained the complete
programmed bad-FCS fixture. This does not identify whether the Pico missed the
RF frame, lost an eligible candidate or followed a different receive path.

## Provenance, verification and restoration

Evidence root: `build/d2-bad-fcs-20261025/`. It retains separate good/bad
controller images, matched host images/collectors, source snapshots/diff,
flags/tool versions, exact trial configurations, raw reports, process exits,
the early-stop outcome, independent audit and a sealed evidence manifest.

| Role/image | RP2350 build |
| --- | --- |
| Normal badge and active receiver | `6ee74aa8` |
| Bad-FCS transmitting badge | `f5599a10` |
| Passive Pico | `7c9a28a9` |

Controller SHA-256:

- Normal: `c5768bf3180d5b11939da3cf51e8c5a32a5a0e600b5774ee152b609db351088d`.
- Bad-FCS TX: `c249924118698ec7f936e11533ab41dfc89f139dcff0bda5878fa0a2d151564a`.

The normal controller remains byte-identical to the preceding corrected-layout
image. All three default-off runtime/HT-runtime/diagnostic controller images
and PSM objects remain byte-identical to the pre-trace baselines.

Enabled and disabled badge sanitizer/integration tests pass, including the
exact one-bit transformation across all MCS, unchanged validated input,
other-mode/length/index exclusions and the HT oracle tests. Eight profile and
source-boundary tests pass; invalid/runtime/missing-trace control combinations
are rejected. All nine raw reports replay against the exact archived native
collectors and independent Node oracle. The audit records five entirely zero
receiver traces and one bound active rejection; the intentionally rejected
pair remains rejected in the ordinary delivery verdict.

Fresh full preflash reads matched the protected originals. After early
stopping, all three full originals were loaded and independently saved and
compared against the retained
[original sizes/hashes](HT_REJECT_COPY_2026-10-25.md#provenance-audit-and-restoration).
Final per-device and all-device probes confirm BOOTSEL. Earlier evidence
archives and their seals remain unchanged.

## Remaining work

The active rejection arm now has a byte-exact controlled calibration, not
just untouched-record controls. Next capture a spontaneous valid-input miss
with that corrected instrument, compare its entire retained body/FCS and
qualify passive/independent observation. Determine which receive-copy behavior
produces short rejected-frame bytes rather than inferring it from the stream
threshold. Independent RF/receiver evidence is still needed to localize a TX
waveform fault versus shared source-built RX behavior. D2 remains open.
