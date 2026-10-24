# HT loss-stage investigation, 2026-10-24

## Scope and outcome

This follows the [original R07 campaign](HT_RECEPTION_R07_2026-10-24.md)
under separate, explicit authorization for offline investigation and **up to
32 new hardware trials**, using both badges and a passive source-built Pico,
with full restoration afterward. All 32 slots were consumed; this report
does not authorize further RF work.

The investigation narrowed the next discriminating experiment, but did
**not** establish or repair the reception root cause:

- Initial target matrix: **9/12**, retaining three B-to-A MCS7 failures.
- Four additional, fresh ACK matrices: **16/16**. These are not passing
  repeats of the failed Data28 or A-MSDU3869 cells.
- The passive Pico witnessed 23/28 positive fixtures and both omitted-RX
  controls. It missed all three failed target fixtures and two successful
  ACK exchanges. No passive legacy ACK was acquired.
- All 92 endpoint/observer snapshots and timing records were valid.
  The previously native-only beacon/report ownership repair is now RF-run.
- All three complete original flashes were independently restored,
  byte/hash verified and left in BOOTSEL.

Evidence root: `build/ht-root-cause-20261024/`. Original R07 results remain
separate and unchanged.

## Instrumentation and provenance

The controller binary is byte-identical to R07:
`b2fa5c8b6b3175ce4f5e6eb7c6908376fded3dff502db1ace7e0c65ebdc48546`.
No controller/PSM receive logic, PHY settings, RF retries or host deadlines
were changed. The existing 200-ms RX lead remains unchanged.

The badge-side diagnostics now retain the first nonce-address/sequence-bound
F2 arrival independently of normal fixture validation. They record metadata,
local time, the first 64 MPDU bytes and calculated/received full-packet FCS.
Later arrivals increment a counter without replacing the first observation.
This can distinguish a missing packet from a delivered packet rejected for
bad metadata or fixture bytes. Long frames still require the full on-device
fixture check; their retained prefix is not a complete packet archive.

RF-tested image IDs:

| Role | Build ID |
| --- | --- |
| Both badges | `3750ee27` |
| Receive-only Pico | `f7c4f912` |

The Pico admits only receive mode 25; every trial's report has its explicit
observer marker, zero TX attempts/submissions and zero configured responses.
The beacon-table exclusion protects all new HT records.

The [host-tool guide](../guides/HOST_TOOLS.md#locating-a-nonaggregate-receive-loss)
documents `--diagnose`. Exact flashed images, collectors, source diff, new
source files and the harness are hashed in `provenance/final/manifest.json`.
After RF work, host-only analysis was clarified to label correlated rejected
SHM separately from a proven current-frame FIFO capture. No image was changed
or reflashed for that clarification. The audit replays the archived diagnosis
exactly and separately verifies the qualified maintained analysis.

## Planned trials and retained results

All cells use channel 6 and fresh challenges/radio identities, also checked
against all 44 original R07 identities. A/B retain their original meanings:
A is badge `08DA26F86E57F5EA`; B is `9BBF88DD17D66E7D`.
Each four-cell matrix covers MCS0/7 in both directions.

| Trials | Matrix | Target acceptance | Independent fixture captures |
| --- | --- | --- | --- |
| 1-4 | Data28, including positive observer calibration | 3/4 | 3/4 |
| 5-8 | A-MSDU3869 | 3/4 | 3/4 |
| 9-12 | Clocked ACK60, round 0 | 3/4 | 2/4 |
| 13-16 | Fresh ACK round 1 | 4/4 | 4/4 |
| 17-20 | Fresh ACK round 2 | 4/4 | 4/4 |
| 21-24 | Fresh ACK round 3 | 4/4 | 3/4 |
| 25-28 | Fresh ACK round 4 | 4/4 | 4/4 |
| 29-32 | Two omitted-TX and two omitted-RX controls | Separate negative controls | Both omitted-RX fixtures captured |

Omitted-TX controls have no target/observer fixture. Omitted-RX controls have
independently witnessed TX and no returned ACK. They do not count as positive
pair passes. All 19 successful ACK exchanges retain strict returned-ACK
evidence at TX; that does not qualify passive ACK acquisition or physical SIFS.

### Natural target misses

| Trial | Fixture, all B to A MCS7 | Challenge | RXE terminal byte count, both receivers |
| --- | --- | --- | --- |
| 4 | Data28 | `18237c3c987edfd0b09dc31c0007` | 34 |
| 8 | A-MSDU3869 | `76c8c529e252a6b9ba57a61d0f07` | 3875 |
| 12 | Clocked ACK60 | `d2d235a4422e11699536cd3c0007` | 66 |

Each has local TX submission, complete successful frozen snapshots, zero
fresh target HT captures and zero correlated target/Pico F2 arrivals.
Both receivers retain a valid early HT-SIG matching the requested rate/length,
with terminal outcome 2, the bad-FCS path. These are last-candidate records,
not nonce-bound proof that either receiver captured this exact fixture.
All three therefore remain `transmission_reception_unresolved`, not proven
receiver-only failures or positively witnessed valid transmissions.

Trial 12 additionally retains one target SHM rejection: cause 2, 66 bytes,
two identical reads, and bad software FCS. Its PLCP and first eight MPDU bytes
match the requested fixture; differences begin at MPDU byte 8 and include
the transmitter address and sequence. The strict identity check fails.
Neither a partial near-match nor stable repeated reads upgrades it to
independently proven fixture corruption.

There is a further staging limitation: the tested `ht-response` selection
uses a **128-byte** stream threshold, not the optional 64-byte threshold.
The 66-byte rejected ACK60 is below it, and rejected frames bypass the
final good-frame SHM copy. The record does not establish freshness of every
SHM byte. Its computed FCS cannot be treated as an on-air FCS measurement.
The [existing receive-staging record](mac/COMPLETION_AND_RECOVERY.md)
documents this provenance hazard. Stable SHM reads are not proof of a
completed current-frame copy.

### Observer misses despite successful target exchanges

Trials 9 and 22, ACK60 MCS0 in opposite directions, passed both target
endpoints including returned ACK verification. The Pico had no correlated
capture. Its valid timing records are useful diagnostics, not synchronized
RF-arm/PHY timestamps. A silent observer still cannot prove absent RF.

## Conclusions and rejected shortcuts

1. **The evidence is no longer lost to the beacon table.** All 92 timing
   records survive the exact RF-tested host images and native C replay.
2. **No natural failure was a correlated F2 arrival rejected by the host.**
   The three target failures have matching early PHY candidates instead.
   Host metadata/fixture rejection is not the observed explanation for them.
3. **PHY/RXE rejection is the strongest lead, not a proven causal location
   for the nonce-bound fixture.** Simultaneous target/Pico misses, matching
   valid HT-SIGs and terminal bad-FCS outcomes motivate investigating the
   transmitted waveform and common receive/staging path before host retries.
   Both receivers share the source-built stack; they cannot independently
   distinguish a TX defect from a common RX implementation defect.
4. **No specific TX-register fault was established.** Retained MCS7 TX
   controls and sampled IQ values agree across failed/successful controls,
   with zero sampled PHY error. Radio register `0x34` varies between boots,
   including passing ones; its variation alone is not a causal explanation.
5. **Bad software FCS on an unqualified SHM copy is not proof of RF damage.**
   A complete rejected-byte capture needs explicit copy-completion provenance.
6. **Neither longer timeouts nor retries is justified as a root-cause fix.**
   Later passing ACK rounds do not erase the three initial failures.

The next bounded experiment should first establish complete, current rejected
bytes at both receivers without enabling observer responses, then compare
simultaneous failed captures. A positively calibrated receiver using a
different PHY implementation, or a suitable independent RF capture, is needed
to separate TX waveform failure from shared RX behavior. Further RF work
requires fresh authorization. R07 reliability and independent-ACK acceptance
remain open.

## Validation and restoration

Both badge/Pico builds and native suites pass, including first-arrival
retention of bad-FCS packets, all-mode beacon ownership and strict passive
ACK checks. All 22 oracle tests pass against each native collector.
Isolation, report-contract, composition and boundary checks pass.

`harness/audit.cjs` validates all 32 unique fresh requests, exact pair results,
archived and qualified stage analysis, 92 native collector replays, 92 complete
snapshots/timing records, passive configuration, unchanged controller,
provenance hashes and full original restoration. The original 44-trial audit
also still passes with the additive maintained oracle.

Each original was saved twice before flashing. After testing, each complete
original was loaded without executing it, independently read back, compared
byte-for-byte and SHA-256 checked:

| Device | Full flash | Verified original SHA-256 |
| --- | --- | --- |
| Badge A | 16 MiB | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Badge B | 16 MiB | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |
| Pico `6572BE3E4636AF51` | 4 MiB | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |

All three final BOOTSEL checks passed. Full-flash files remain private local
evidence, not repository assets. No dependencies were installed and no commit
was made.
