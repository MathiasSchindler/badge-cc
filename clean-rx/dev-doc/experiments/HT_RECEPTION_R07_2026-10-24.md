# R07 intermittent HT reception, 2026-10-24

## Scope and outcome

The [R07 investigation](todo.md) adds trustworthy nonaggregate evidence and
a third, passive source-built Pico receiver. It does **not** fix or close
intermittent reception, qualify vendor interoperability, or establish
physical ACK/SIFS timing.

The campaign consumed all **44 authorized trials**: 28 initially authorized
trials, one additional vendor-monitor calibration, then the remaining 15
with the explicitly approved passive source-built Pico observer.
Every attempt and fresh repeat remains separate. All three complete original
flashes were restored, independently byte/hash verified and left in BOOTSEL.
No further RF trials were run under that authorization.

The first source-observer calibration succeeded. That phase passed all
12 initial target pairs and its one fresh repeat. The Pico independently
observed 11/12 initial HT fixtures and the repeat. An omitted-RX control
also independently witnessed TX without a returned ACK. Conversely, the
Pico missed one fixture that both target endpoints proved successful.
No complete independent legacy ACK observation was obtained.

Post-run analysis found an overlapping legacy beacon table that could
overwrite the new timing/optional ACK diagnostics. The final ownership
repair is built and native-tested, **not hardware-rerun**: the trial budget
was exhausted. Its RF-tested predecessor and exact provenance are retained.

The separately authorized [32-trial loss-stage follow-up](HT_ROOT_CAUSE_2026-10-24.md)
subsequently RF-tested that ownership repair and added correlated F2 evidence.
Its new failures and remaining causal limits are recorded separately.

Evidence root: `build/radio-regression-r07-20261024/`.

## Original failures, not retrospectively reclassified

The original [hardware regression](HARDWARE_REGRESSION_2026-10-24.md)
had these failures and separate passing fresh repeats:

| Cell | Initial challenge | Fresh passing repeat |
| --- | --- | --- |
| Data28 A to B, MCS0 | `4caeaef6506afc1d8ce9aa1c0000` | `3b0735d7bd121a6c824ae31c0000` |
| A-MSDU3869 B to A, MCS0 | `74aaa52ce0154195ec8d741d0f00` | `f8f9df1bde697cedadb5011d0f00` |
| Clocked ACK60 B to A, MCS7 | `5afaa05e7d93e36819e5543c0007` | `a0a36fa46662c4bdb241213c0007` |

All three had local TX submission and no fresh target capture; the ACK cell
also lacked a returned ACK. There was no positive independent TX witness.
Their older 2,048-byte reports predate R03 binding. They do not prove that
the receiver, rather than transmission or observation, failed.

## Maintained diagnostic changes

- Modes 24-28 require complete frozen mailbox/multi/HT snapshots with the
  exact request nonce and controller generation. Missing evidence is
  unavailable/null, not zero reception or successful submission.
- The C collector supplies offline `--inspect-ht` decoding. The separate,
  dependency-free Node oracle compares raw reports, C JSON, fixture bytes,
  FCS, HT-SIG, submission and returned-ACK evidence. Long frames require the
  full on-device fixture check plus their archived prefix/FCS summary.
- Data/ACK pairing uses that oracle instead of a hard-coded successful
  summary. Missing TX versus RX stays unresolved without positive RF proof;
  corrupt correlated captures, unrelated captures and ACK failure stages
  have distinct classifications.
- Local setup/listen/command timing and desktop USB-submit brackets are
  retained. The existing 200 ms target RX lead is unchanged. It is not
  an RF-arm handshake. Independent clocks and handoff counters cannot prove
  RF overlap or transmission.
- `BADGE_PICO=1` is explicitly headless and receive-only: mode 25 only,
  qualified Pico system clock, no TFT access, TX fixture or configured ACK
  response. Ordinary badge behavior remains the default.
- A nonce-address/FCS-bound optional record retains a complete passive ACK
  if delivered. Its RF acquisition was not positively calibrated here.

There are no changes to controller RX/TX logic in `clean-rx/src/`, and no
new package/framework dependencies. See the maintained
[host-tool procedure](../guides/HOST_TOOLS.md#nonaggregate-ht-evidence)
for the layout, build and collection interfaces.

## Hardware phases

All trials used channel 6, fresh full challenges and fresh five-byte
radio identities. Positive matrices cover Data28, clocked ACK60 and
A-MSDU3869, MCS0/7, both badge directions.

| Phase | Positive target results | Independent observation | Controls/repeats |
| --- | --- | --- | --- |
| Initial, vendor witness, trials 1-16 | 11/12 | No fresh matched frame or ACK | Two omitted-TX; two omitted-RX |
| Fresh follow-up, trials 17-28 | 11/12 | No fresh matched frame or ACK | Separate complete 12-cell matrix |
| Polled vendor calibration, trial 29 | 1/1 | Monitor failed before TX | Stopped immediately for diagnosis |
| Source Pico, trials 30-43 | 12/12 | 11/12 fresh matched HT fixtures | One omitted-TX; one omitted-RX |
| Source Pico fresh repeat, trial 44 | 1/1 | Fresh matched A-MSDU3869 | B to A, MCS7; not an initial pass |

### Retained target misses

Trial 12, B to A A-MSDU3869 MCS7:
`6791f169c2f1bb2fae4fc01d0f07`. TX submitted; the frozen receiver
snapshot was complete, with zero fresh captures. Its fresh follow-up passed.

Trial 21, A to B clocked ACK60 MCS0:
`3bcae56810b0376826edcb3c0000`. TX submitted; the receiver had zero
fresh captures/responses and TX had no returned ACK. This was a **new
follow-up failure**, not an initial failure erased by a repeat.

Both remain `transmission_reception_unresolved`. Their uncalibrated vendor
witness did not establish absence of RF. The target timing records were
valid: RX service windows were approximately 2.508-5.009 seconds and
2.485-4.985 seconds from their respective probe starts. TX commands began
at 3.183 and 3.211 seconds on different clocks. Neither is a conspicuous
startup outlier; this is not proof of RF overlap or a timing root cause.
The stopped handoff counters were zero on successful trials too.

### Why the vendor monitor was not retained

Badge build `b1189170` was used for trials 1-29. The initial vendor witness
was `e1a8c578`; the polled calibration used `faeb6e65`.

The initial monitor windows almost always stopped accumulating at 32 raw
packets. Existing [legacy monitor history](../history/LEGACY_PHY_MILESTONES.md)
already documents this unpolled servicing limitation. The existing 100 ms
read-only GET_MONITOR polling option was then applied experimentally.

Trial 29's Data28 target pair passed, but its positive witness calibration
failed with vendor timeout error 6 around 1.94 seconds, before source TX.
It recorded 17 monitor polls and 46 arrivals before cleanup. The campaign
stopped rather than spending the remaining cells on an uncalibrated witness.
The vendor transport's 2,040-byte buffer and 11-bit available-length field
also do not establish complete 3,869-byte monitor delivery.

The experimental live-ready/final publication additionally exposed the
existing page protocol's lack of a changing snapshot epoch: pages share
the first 32 identity bytes but can mix old counters with later terminal
state. No RF absence conclusion is drawn from that record.

The unsuccessful vendor additions were removed from maintained peer source.
Exact source diffs, added files, binaries and manifests remain in
`provenance/final-image/` and `provenance/polled-witness/`. Read-only oracle
support remains so these historical observations are still auditable.
No speculative vendor-transport repair was shipped.

### Source Pico observations and controls

RF-tested badge build: `a5bf7992`; Pico observer: `2981b2a9`.
These are third-radio observations using the same source-built receiver,
not an independently implemented vendor PHY stack.

- Trial 30 positively calibrated Data28 observation.
- Trial 34, A to B ACK60 MCS0, passed target delivery, response and returned
  ACK, but the Pico had zero fresh HT captures. A silent observer therefore
  cannot establish absence of transmission.
- All four extended A-MSDU initial cells and the fresh repeat had complete
  on-Pico fixture validation and matching retained summaries.
- Trial 42 deliberately omitted TX: target and observer had no fresh fixture.
  This is `intentional_no_transmission`, not a naturally diagnosed TX failure.
- Trial 43 deliberately omitted RX: the Pico retained the exact ACK-requesting
  frame; TX received no ACK. Its classification is
  `witnessed_tx_without_receiver_or_ack`. The observer did not substitute an
  ACK response or turn the missing receiver into a successful exchange.
- All four positive clocked-ACK pairs returned valid ACKs to TX. However,
  the passive Pico retained no legacy 14-byte ACK. Independent ACK acquisition
  is **uncalibrated**, not proven absence of ACK transmission.

There are 13 positive independent HT observations across the 15 source-Pico
trials: 11 initial positives, the omitted-RX control and the fresh repeat.
No natural target miss was reproduced during this phase, so it cannot
retroactively assign a cause to the earlier failures.

## Discovered report-ownership defect

The legacy beacon table stores eight 40-byte entries in bytes 1640-1959.
Its fifth entry starts at 1800, overlapping the new HT timing record;
later entries also overlap optional ACK evidence and the observer marker.
Only aggregate modes had previously suppressed this table.

Thirteen of the 15 frozen Pico reports have explicitly invalid timing
records, including recognizable nearby AP names in the overlapping bytes.
The other two timing records and all 82 target timing records were valid.
Do not use the invalid fields or zero optional ACK records as negative RF
proof. The independently checked HT trace, delivered prefix/full-check
summary and bound final snapshot are outside the overwritten beacon slots;
their positive fixture evidence remains valid.

The final repair prevents beacon discovery/table writes in modes 24-28 as
well as 30/31, preserving station/legacy/peer behavior. A native regression
attempts all nine beacon insertions in every mode 0-31 and checks exact
preservation of the entire HT report versus the unchanged eight-entry legacy
table. Final builds `20e3bfc0` (badge) and `fb74c386` (Pico) pass offline
validation only. No claim is made that those exact final images were RF-run.

## Validation and restoration

The final native suites pass on both board builds, including the ACK byte/
FCS/address gate and beacon ownership regression. All 19 strict oracle tests
pass without skips: actual C decoding covers all eight MCS values and all
three endpoint/observer fixture types, unavailable/stale snapshots,
correlated bad PHY/HT-SIG, passivity, and distinct missing/started/witnessed
ACK classifications. ABI/PSM composition and isolation checks also pass.

`harness/r07-audit.cjs` independently checks all 44 unique challenges/radio
identities, raw/JSON/oracle agreement, 82 complete target snapshots and
82 target plus 15 source-observer native C replays. Archived per-phase
collectors preserve exact build/JSON interpretation. Provenance manifests
and full restoration readbacks are independently hashed and compared.

| Device | Serial | Complete original size | Restored SHA-256 |
| --- | --- | --- | --- |
| Badge A | `08DA26F86E57F5EA` | 16 MiB | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Badge B | `9BBF88DD17D66E7D` | 16 MiB | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |
| Pico 2 W | `6572BE3E4636AF51` | 4 MiB | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |

Each original was backed up twice, restored without executing it, then
independently read back in full and compared by size, bytes and hash.
Backups/readbacks have private permissions. All devices remain in BOOTSEL.

## Remaining acceptance boundary

The bounded investigation and diagnostic implementation are complete;
**R07 reliability/independent-ACK acceptance remains open**.
Further authorized work needs the final ownership-fixed image, positive
passive ACK calibration (or a different independently qualified observer),
and a naturally failed target cell captured with an independently witnessed
frame. Missing TX/RX without positive observation remains unresolved.
RF-arm synchronization, loss-free reception, vendor HT interoperability,
physical SIFS timing and broader retry/soak qualification are not established
by this campaign. Do not increase delays or change retry policy on this
evidence alone.
