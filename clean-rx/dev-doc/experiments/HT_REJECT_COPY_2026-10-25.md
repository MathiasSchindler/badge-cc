# Rejected HT copy-state investigation, 2026-10-25

## Outcome and scope

Eight fresh ACK60/MCS7/channel-6 attempts were made on the two badges with
the Pico 2 W receiving passively. The first four used defective diagnostic
prototypes and reproduced the B-to-A miss. Four subsequently authorized
corrected-layout controls all delivered, returned an ACK and were observed
by the Pico. Their eight receiver trace records stayed correctly zeroed.
**No corrected-layout rejection was captured; rejected-byte copy provenance
and the radio root cause remain unresolved.**

The subsequent [controlled bad-FCS calibration](HT_REJECT_BAD_FCS_2026-10-25.md)
exercised the corrected active rejection arm with a complete programmed-byte
match. It does not retroactively qualify either defective prototype, establish
general copy freshness or resolve the original miss.

The user authorized up to 12 attempts, both badge directions, with full
backup/restoration and early stopping. After three attempts, the originals
were restored and an initial SRAM collision was identified. The user then
authorized up to six additional attempts within that original cap to test a
repair. Its first attempt exposed further collisions and stopped the campaign.
No further RF trials ran under that authorization. Unused slots are not an
authorization to resume.

The user subsequently authorized a fresh campaign of up to four attempts,
alternating directions with the same traffic and restoration bounds.
It used all four slots without a delivery or instrumentation failure.
No further RF work is authorized by unused earlier slots.

All three full originals were restored and independently byte/hash verified
after **each of the three** phases. Final device probes confirmed all three
in BOOTSEL.

## Devices and observed traffic

A remains badge `08DA26F86E57F5EA`; B is `9BBF88DD17D66E7D`.
The passive Pico is `6572BE3E4636AF51`. Each attempt uses a new challenge and
radio identity, checked against the preceding R07/loss-stage campaigns.
Clocked ACK60 TX, the 200-ms receive lead, retry policy and collection bounds
were unchanged. The Pico's retained reports prove mode 25, its observer
marker, zero TX attempts/submissions and zero configured responses.

| Attempt | Prototype | Direction | Challenge | Observed target delivery / returned ACK | Passive complete fixture |
| --- | --- | --- | --- | --- | --- |
| 1 | Initial | B to A | `6621527da48c15878365763c0007` | Yes / yes | Yes |
| 2 | Initial | A to B | `3425db3e59d1c33c27a8be3c0007` | Yes / yes | Yes |
| 3 | Initial | B to A | `c3cdcd08aea7c341cf36b43c0007` | No / no | No |
| 4 | Partial SRAM repair | B to A | `16de179abd68863641fd6b3c0007` | Yes / yes, reconstructed offline | Yes |
| 5 | Final layout | B to A | `0dde91bff8d680d9f43a673c0007` | Yes / yes | Yes |
| 6 | Final layout | A to B | `9a8172e221c4869647e8713c0007` | Yes / yes | Yes |
| 7 | Final layout | B to A | `e8bb3fb7fb1e37b2d3058e3c0007` | Yes / yes | Yes |
| 8 | Final layout | A to B | `4999fb31ea25c79ab80a5e3c0007` | Yes / yes | Yes |

Attempts 1-4 are observations under defective diagnostic prototypes.
Neither their results nor four passing corrected controls are a reliability
acceptance rate. Attempt 4's hardware harness rejected the
instrumentation before generating a pair verdict. Its retained raw reports
were independently decoded afterward; the original failed outcome remains
unchanged.

Attempt 3 has submitted TX, no correlated target/Pico F2 arrival and a target
early HT-SIG candidate with terminal bad-FCS outcome and RXE count 66.
Both receivers retained stable repeated 66-byte SHM rejection reads, but their
bytes differ between receivers. The first fixture mismatch is MPDU offset 10
in both: the matching six PLCP bytes plus ten MPDU bytes are only a prefix,
not a complete-frame identity/FCS proof. Later Pico candidates also overwrite
its separate last-candidate early trace.

The apparent copy-register operands `[0x0800,0x0800,0x0f40,0,0x8000]` and
128-byte threshold are retained, **unqualified** measurements. The collisions
below prevent treating record binding/stability as successful instrument
qualification. Independently, the existing receive path still skips the final
GoodFCS copy on rejection; 66 bytes is below its ordinary streaming threshold.
Bad software FCS on these SHM reads remains no proof of RF damage.

## Instrumentation defects and repair

The new trace initially used SHM `0x770..0x77f` and SRAM
`0x20500..0x20523`. Literal-address searches missed derived allocations in the
existing unicast contract:

| Existing owner | SHM interval, end exclusive | SRAM interval, end exclusive |
| --- | --- | --- |
| TX completion and handoffs | Other SHM state | `0x204f0..0x20514` |
| ACK suppression evidence | `0x76c..0x776` | `0x20514..0x20534` |
| ACK deadline evidence | `0x776..0x77e` | `0x20534..0x20554` |
| ACK arithmetic | `0x77e..0x781` | `0x20554..0x20560` |
| TX-select operands | `0x781..0x783` | Other SRAM state |

Consequently, apparently uncommitted copy records contained ordinary ACK
state. For example, their supposed stream threshold was the ACK-test epoch
(1 or 2), not 128; their supposed timestamp included the frame-control word.
The trace could also overwrite ACK state and ordinary publication records.
This is a defect of the **new opt-in diagnostic**, not an explanation of the
earlier [R07 loss-stage failures](HT_ROOT_CAUSE_2026-10-24.md).

The first repair moved SRAM to `0x20520`, clearing the TX completion record
but still overlapping derived ACK allocations and leaving SHM unchanged.
The follow-up's explicit zero-state check caught the remaining fault on its
first attempt, before any further RF work.

The final repair places:

- SHM at `0x790..0x79f`, after all ACK arithmetic/select operands and before
  the existing `0x7a0` diagnostic area.
- SRAM at `0x20600..0x20623`, after PHY-edge evidence. This intentionally
  reuses the aggregate-probe slot only in the diagnostic nonaggregate profile;
  aggregate/compile-time-observer combinations remain rejected.

Compile-time checks now include the derived allocations, not just literal
neighbors. Native tests seed ACK state and verify rejection capture preserves
it, check publication isolation in both write orders, and reject both former
SRAM layouts and SHM overlaps.

Final-layout controller, badge and Pico builds and badge integration checks
pass, as do 24 HT oracle tests and 14 profile/boundary/documentation tests.
All three default-off controller images and their PSM objects remain
byte-identical to the pre-change baselines. All three sealed RF archives and
all restoration readbacks pass their independent audits.

## Corrected-layout hardware controls

Attempts 5-8 used the final allocation repair and unchanged ACK60/MCS7/channel-6
traffic, receiver lead and retry policy. All four pairs delivered the complete
fixture with valid FCS and a returned ACK; the passive Pico observed all four.
The campaign stopped at its authorized four-attempt cap.

All eight active/passive receiver records had the expected trace marker and
zero count, cause, timestamp, length, threshold and operand arrays. In
particular, the ACK-test epoch no longer appeared in the uncommitted trace.
This qualifies the **untouched-record path for these controls**, not the
rejection-capture path: there were no committed rejection records.

Twelve complete raw reports replay against the exact archived badge/Pico
collectors and independent Node oracle. The audit also checks fresh identities,
image IDs, passive zero-TX/response counters, raw/JSON agreement and full
restoration. No evidence-collection or cleanup error occurred. Four positive
controls do not establish that the intermittent miss is repaired.

## Additional offline observation

Comparing attempt 3's two retained rejection buffers finds the same incorrect
eight bytes, `5c2c84abcfabc74f`, at MPDU offsets 29-36 in both receivers.
The comparison checks the sealed input hashes and reconstructs the expected
fixture; its reproducible script/output are retained in
`build/d2-forensics-20261025/`.

This is only a comparison fingerprint for future qualified captures.
The prototype ownership defects and unproven full current-frame copy still
apply. Shared wrong bytes do **not** prove transmitted RF corruption or a TX
fault, nor exclude shared source-built receiver behavior.

## Provenance, audit and restoration

| Flashed image | Badge build | Pico build | Controller SHA-256 |
| --- | --- | --- | --- |
| Initial | `356bc6c6` | `5d83c269` | `02f6ba4d960b07096303645c152b49a4b670d3e1dacbce8645e46b8029b729df` |
| Partial SRAM repair | `3523642d` | `f422af00` | `30205d4f37f01267dfdb8dd32e8594202bb5b01a352727b124e4a59f87e08e8e` |
| Final layout | `e2b4abe5` | `90d1fa48` | `c5768bf3180d5b11939da3cf51e8c5a32a5a0e600b5774ee152b609db351088d` |

Evidence roots are `build/d2-copy-rf-20261025/`,
`build/d2-copy-rf-fixed-20261025/` and `build/d2-final-rf-20261025/`.
Each retains image/collector/source
provenance, immutable trial identities, complete raw reports/stdout/stderr,
exit/outcome records, independent audit output and a sealed evidence manifest.
All 24 raw reports replay against their exact archived native collectors and
the independent Node oracle. The first two audits explicitly mark prototype
copy-state evidence unqualified; the second preserves its failed harness
validation. The final audit records eight zero-state traces and zero committed
rejections, without claiming copy freshness.

Before the initial flash, two independent full current-image reads per device
were byte/hash compared. Before the second phase, fresh full reads again
matched those retained originals; the third phase repeated those comparisons.
All three restoration rounds loaded the full originals and independently
saved/compared the complete restored flash:

| Device | Bytes | Original/restored SHA-256 |
| --- | --- | --- |
| Badge A | 16777216 | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Badge B | 16777216 | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |
| Pico | 4194304 | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |

The second cleanup logged a preliminary application-stop failure because
badge A was not enumerated at that instant. Its harness therefore exited
nonzero even though all three later full restores, readbacks and BOOTSEL
probes succeeded. The log/error is retained; final restoration is established
by the independent byte/hash audit and fresh all-device BOOTSEL probe.

## Remaining question

These eight attempts qualified only the corrected instrument's untouched-record
path. The subsequent [bad-FCS control](HT_REJECT_BAD_FCS_2026-10-25.md)
exercised the active rejection arm with a complete programmed-byte match;
passive rejection capture and spontaneous failed-frame provenance remain open.
Establish complete current-frame provenance for those failures, or use a
positively calibrated independent receiver/RF capture. The existing same-stack Pico
cannot by silence alone distinguish transmitted waveform failure from shared
receiver behavior. [D2 work package 1](../../../todo.md#remaining-work-packages)
and the radio root cause remain open.
