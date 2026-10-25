# Initial-SHM baseline and dual-receiver HT rejection, 2026-10-25

## Outcome

The eighth attempt reproduced a normal **A-to-B** ACK60/MCS7/channel-6
delivery failure. This time **both badge B and the passive Pico retained
bound cause-2 rejection records** with the corrected diagnostic.
They share 32 identical incorrect MAC bytes, including a contiguous
15-byte run, but differ from each other at 13 MAC offsets.

The new stopped-MAC baseline shows that **all 60 retained MAC bytes changed
from each receiver's own initial SHM contents**. Thus rereading untouched
startup-buffer contents does not explain this miss. This does not exclude
stale content from intervening candidates or establish all rejected bytes
as the intended on-air frame.

Both records have the requested fresh receiver address and HT-SIG, but
damaged transmitter/sequence identity. The conservative verifier does not
promote them to complete fixture identities or a proven transmitted waveform.
TX/RF versus shared source-built RX/copy behavior remains unresolved.

The campaign stopped immediately after attempt eight of twelve authorized.
All three complete original flash images were restored and independently
verified byte-for-byte and by hash; all devices were left in BOOTSEL.
There were no collection or restoration errors.

## Question and diagnostic

The [preceding spontaneous miss](HT_SPONTANEOUS_REJECTION_2026-10-25.md)
retained damaged bytes but no initial-buffer comparison. Source tracing
found that the explicit SHM initialization in
[PSM startup](../../src/runtime/d11_start.inc) stops at word `0x7ff`, before
the receive payload at `0x801..0x821`. The controller-SRAM cold clear does
not establish D11 SHM contents. This was a hypothesis to test, not grounds
for adding an unproven buffer-clearing fix.

The new default-off `CLEAN_RX_HT_REJECT_BASELINE=1` adds only a badge/Pico
host-side diagnostic. Before enabling RX, it checks controller state 8,
host command 1, request generation and stopped MAC/PSM bits; reads the
33 SHM words; repeats the guards; and commits a generation-bound baseline.
Both before/after MACCONTROL samples were `0x84020400` in every capture.
It never writes receive-buffer bytes or forces a copy.

The [host-tool guide](../guides/HOST_TOOLS.md#rejected-ack60-copy-state-trace)
documents the 92-byte record, fixed report offsets, timing/ownership checks
and required-baseline verifier option. ACK60 receive modes 25/27 alone
reuse unused packet-report tail space. Unexpected packet overlap fails
explicitly without overwriting packet evidence. Other modes/lengths do
not capture a baseline.

The normal controller firmware and PSM object rebuilt byte-identically to
the calibrated trace image. The RF campaign reused the exact calibrated
normal/bad-FCS controllers, not newly modified receive programs. Only the
host images/tools changed. The extra pre-RX reads perturb startup timing:
9,120-9,248 us on the badges and 3,478-3,491 us on the Pico in this campaign.
All baseline reads ended before their recorded receive-window start.

## Authorization and attempts

The user explicitly authorized implementation/offline validation followed
by up to twelve fresh attempts: two ordinary directional controls, one
deliberate bad-FCS calibration, then at most nine ordinary alternating
attempts. Stop on unexpected control failure, the first normal delivery
failure/useful rejection, or collection error; verify/restore all originals
and leave BOOTSEL. The existing channel, MCS, one-shot submissions, 200-ms
receive lead and collection bounds were retained.

Devices remain badge A `08DA26F86E57F5EA`, badge B `9BBF88DD17D66E7D`,
Pico `6572BE3E4636AF51`.

| Attempt | Direction | Control | Fresh challenge | Delivery / returned ACK | Passive full fixture |
| --- | --- | --- | --- | --- | --- |
| 1 | B to A | Normal | `3d75ba9e91b4260c26b9ca3c0007` | Yes / yes | No |
| 2 | A to B | Normal | `abd65852ae2c64642279c03c0007` | Yes / yes | No |
| 3 | B to A | Bad FCS | `edb4eff3b7aaa01ca5f6583c0007` | No / no | No |
| 4 | A to B | Normal | `2a96b3d7fa89a22ed8fdd63c0007` | Yes / yes | Yes |
| 5 | B to A | Normal | `8ee127073d94e36f52d8873c0007` | Yes / yes | No |
| 6 | A to B | Normal | `755c26615b25b6d03873683c0007` | Yes / yes | Yes |
| 7 | B to A | Normal | `bc6ab14c2bebd744d9e9a13c0007` | Yes / yes | Yes |
| 8 | A to B | Normal | `73c71eee86c90522c8a7183c0007` | No / no | No |

Six of seven normal target attempts delivered complete F2 fixture/FCS
matches and returned ACKs. The intentional negative is separate, not an
ordinary loss. Four authorized slots remained unused. Together with the
preceding sixteen separately authorized attempts, this D2 copy
investigation has made twenty-four attempts. These first-failure campaigns
are not a reliability acceptance rate.

All sixteen receive baselines contained nonzero initial bytes. The
uncommitted rejection records remained entirely zero in thirteen receive
snapshots; three records committed: the active negative control and both
receivers on attempt eight.

### Byte-exact negative control

Attempt three retained all 66 programmed negative-fixture bytes exactly
in badge A, with matching repeated reads and cause-2/copy-record binding.
Calculated good CRC was `0x7d635de6`; retained/programmed FCS was
`0xfd635de6`, exactly the intended `0x80000000` XOR.

Sixty-five raw bytes differed from the initial baseline. The one unchanged
byte, raw offset 18, also matched the programmed fixture. This illustrates
why equality with an initial byte alone must not be labelled stale.
No incorrect programmed MAC byte matched its initial baseline.
The passive Pico retained no corresponding rejection.

This re-exercises the active arm with the additional host diagnostic.
It does not supply a byte-exact passive negative calibration.

## Spontaneous dual-receiver record

Both attempt-eight records freeze HT-SIG `073c0007fc03`, MCS7/length 60,
RXE count 66, cause 2, PHY value 2 and identical repeated reads.
Their respective copy records bind to their own rejection cause, TSF
and length:

| Field | Active badge B | Passive Pico |
| --- | --- | --- |
| Rejection TSF | `1035385` | `2420105` |
| Initial/final status1 | `0x4062` | `0x4022` |
| Initial/final status2 | `0x0840` | `0x0840` |
| MAC bytes differing from intended fixture | 44/60 | 42/60 |
| MAC bytes unchanged from own initial baseline | 0/60 | 0/60 |
| Calculated CRC of retained body | `0x5db4696e` | `0x7f7cd9f2` |
| Retained FCS | `0xceaae41a` | `0xceaae41a` |
| Requested fresh receiver address matches | Yes | Yes |
| Complete MAC identity matches | No | No |

The expected normal FCS is `0xcea8bd25`. Neither retained body validates
against the retained FCS. TSFs are independent device clocks, not synchronized
RF timestamps; raw RXE status fields are not assigned undocumented meanings.

Both copy records have threshold 128 and identical before/after operands
`[0x0800,0x0800,0x0f40,0x0000,0x8000]`. Those values also occur in the
byte-exact controlled rejection. Their completion semantics remain
unqualified; neither a short length nor stable operands prove freshness.

Expected MAC60:

```text
88002c000273c71eee060273c71eee860273c71eeec63077050073c71eee86c90522c8a7183c000773c71eee86c90522c8a7183c000773c725bda8ce
```

Badge B retained MAC60:

```text
88002c000273c71eee06e43b221a8332d335494a585760463a52aa6ff2f48fd30c7b94047a5ea0557ac71eee8649331077b4eb87380733821ae4aace
```

Pico retained MAC60:

```text
88002c000273c71eee060273c71a8332d335497a9f4b60463a52aa6ff2f48fd30c7b94047ae2bb8c60c71eee8649331077b4eb87386f908f1ae4aace
```

### Shared incorrect bytes

The receivers share these incorrect runs; offsets are zero-based within
the MAC MPDU and the end is exclusive:

| Start | End | Shared incorrect bytes |
| --- | --- | --- |
| 13 | 19 | `1a8332d33549` |
| 22 | 37 | `60463a52aa6ff2f48fd30c7b94047a` |
| 45 | 53 | `49331077b4eb8738` |
| 56 | 59 | `1ae4aa` |

That is 32 identical incorrect bytes, including a 15-byte contiguous run.
Their MAC arrays differ at offsets
`10,11,12,19,20,21,37,38,39,40,53,54,55`.
The full received byte arrays are therefore **not identical**.
The initial baselines were different, and none of these incorrect MAC bytes
equals its own receiver's initial value.

Both records retain the complete requested receiver address
`0273c71eee06` and another fresh matching payload run at offsets 41-44.
This is stronger partial identity than an unrelated same-rate/length
candidate, but damaged transmitter/sequence fields still fail the complete
identity oracle. The generic freshness result deliberately remains false.

### What this narrows

- The retained MAC arrays are not unchanged startup SHM, in either receiver.
  Clearing startup SHM is not an evidence-backed fix for this miss.
- A normal A-to-B miss now occurs as well as the earlier B-to-A misses.
- Both PSMs took the eligible bad-FCS branch and retained damaged candidate
  bytes before correlated F2 delivery. An explanation confined to losing a
  host report, F2 delivery or the returned ACK is insufficient for these records.
- A common error pattern exists across two corrected diagnostic records with
  different initial buffers. A byte-exact negative control still succeeds.
- This is the first nonzero passive rejection record with the corrected
  allocation, not a byte-exact passive calibration or an independent receiver
  implementation.

Intervening-candidate SHM updates, common receive-copy/PHY behavior, actual
TX waveform corruption and RF conditions are not separated by these facts.
Shared wrong bytes are not an independent observation of the transmitted
waveform. The intended normal command/template remains valid; the pair's
over-air verdict remains unresolved.

## TX diagnostic comparison

The new byte-exact negative control reports `phy_debug_first=00000006`;
the new spontaneous failure reports `00000046`. The preceding campaign's
spontaneous failure had `00000006`, while its byte-exact negative control
had `00000046`. Neither value is a unique signature of spontaneous failure,
and `00000006` alone cannot establish incomplete programmed-byte reception.
No undocumented meaning is assigned to their differing bit.

The new negative/spontaneous TX records report PHY error `0000`, one
submitted attempt, MAC TX durations 84/79 us and reported MAC RX turnaround
406/410 us. As established in the
[preceding report](HT_SPONTANEOUS_REJECTION_2026-10-25.md#tx-comparison-and-timing-interpretation),
the latter field includes ACK waiting before the final timestamp, rather
than isolating first hardware RX readiness. It does not establish the cause.

## Implementation validation, evidence and restoration

Matched host builds:

| Image | Build |
| --- | --- |
| Normal badge / active receiver | `56457f3c` |
| Deliberate bad-FCS transmitting badge | `1b5fd060` |
| Passive Pico | `bf0a5d90` |

Exact reused controller SHA-256:

- Normal: `c5768bf3180d5b11939da3cf51e8c5a32a5a0e600b5774ee152b609db351088d`.
- Bad-FCS TX: `c249924118698ec7f936e11533ab41dfc89f139dcff0bda5878fa0a2d151564a`.

Enabled and default-off host/native suites pass, including sanitizer-backed
report ownership tests and all 26 HT oracle tests with native C replay.
Tests cover exact 33-word extraction without SHM writes; wrong state,
command, generation and active/late-active MAC; bus failure/cancellation;
publication neighbors, role/length and packet-overlap guards; required
baseline absence; raw/JSON, timing, generation and reserved-word tampering;
and unchanged versus changed rejected bytes without freshness promotion.
Profile restrictions and freestanding/runtime-development boundary checks
pass. All 36 prior corrected-layout diagnoses remain unchanged under the
new optional-baseline oracle.

Evidence root: `build/d2-baseline-20261025/`. Its independent audit verifies
all 24 raw reports against the exact native collectors and archived Node
oracle, sixteen baseline records, thirteen zero rejection records, three
bound rejections, six normal delivery/ACK pairs, three passive complete
fixtures and the byte-exact negative control. The cap, fresh identities,
image/source provenance and immediate stopping are checked independently.

Fresh full preflash reads matched all protected originals. After stopping,
full originals were loaded and independently saved and compared for exact
size, bytes, hash and private permissions. Final per-device and all-device
probes confirmed BOOTSEL. Original sizes/hashes remain in the
[initial provenance record](HT_REJECT_COPY_2026-10-25.md#provenance-audit-and-restoration).
All five preceding evidence seals were verified unchanged; the new archive
was sealed only after campaign exit and completed restoration.

## Next discriminating work

The startup-only stale-buffer hypothesis is narrowed for this case, not all
copy provenance. Distinguishing intervening candidate reuse requires a
per-candidate ownership/timing argument, not another pre-enable baseline.
Separating TX/RF from the shared receive implementation still needs a
qualified independent receive/waveform oracle or a carefully controlled
causal comparison. Do not infer an RF-only fault from the common bytes or
enable negotiated HT based on this campaign. D2 work package 1 remains open.
Any further instrumentation or RF campaign needs fresh bounded authorization.
