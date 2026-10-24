# R05 aggregate acceptance rerun: 2026-10-24

## Scope and outcome

The user explicitly authorized Badge A `08DA26F86E57F5EA` and Badge B
`9BBF88DD17D66E7D` for the 44-cell SHM/BA/bad-FCS matrix, complete verified
flash backups/restoration, and at most one fresh repeat per failed cell.
DMA was excluded under R06; the Pico was neither accessed nor reflashed.

The bounded R05 rerun is complete. Initial strict acceptance was **42/44**;
both failed cells passed their one fresh repeat. Every planned cell therefore
has a strict passing attempt, but this is **not 44/44 first-attempt success**.
All 46 attempts, including both failures, are retained.

| Variant | Initial strict acceptance | Fresh repeats | Build ID |
| --- | --- | --- | --- |
| SHM | 18/18 | None | `eb2b3dee` |
| Normal BA | 16/18 | 2/2 passed | `0a1ffb40` |
| Bad-middle BA | 4/4 | None | `b609ed59` |
| All-bad BA | 4/4 | None | `fa23e485` |

All **92/92 endpoint snapshots** were complete, frozen and bound to their
fresh request nonce/controller generation, including the two failed pairs.
The evidence infrastructure no longer prevents these failures from being
classified. Intermittent reception remains unresolved; no firmware reliability,
general HT/BA integration or physical SIFS qualification is claimed.

## Method and provenance

SHM and normal BA each covered MCS 0-7 on channel 6 and MCS0 on channel 1
in both directions: 18 cells per variant. Bad-middle and all-bad BA each
covered MCS0/7 on channel 6 in both directions: four cells per variant.
Repeats used fresh challenges and were reported separately.

All variants used diagnostic/ht-data and:

```text
-DCLEAN_RX_AMPDU_PROBE=1
-DCLEAN_RX_AMPDU_INTERFACE=3
-DCLEAN_RX_AMPDU_HANDOFF=2
-DCLEAN_RX_AMPDU_SHM=1
```

BA variants additionally enabled `CLEAN_RX_AMPDU_BA=1`; bad-middle and
all-bad selected `CLEAN_RX_AMPDU_BAD_FCS=2` and `7` respectively.
All four matched controller/badge variants built and passed their native,
strict, collector and independent-oracle tests. The 14 R04 orchestration
failure-path tests also passed before hardware access.

Image-build revision was `2a2baee8d8e82db479109dce82f4e10bc08d5448`
with the uncommitted R04 changes. The retained tracked-source diff has SHA-256
`cdb3d641b2eaf7558cb705de4d13029217a1ec70aa79f14f675d4cd72b0c564f`.
The revision alone does not reproduce these images: new R04 tooling sources
and the exact readiness executable are archived and hashed separately.
R05 documentation was written after the hardware run.

The campaign used the repository's hardened
[`ht_probe.sh`](../../tools/badge/ht_probe.sh) and
[bounded USB tooling](../guides/HOST_TOOLS.md#bounded-usb-orchestration),
not the former R03 timing workaround. Both applications were booted,
protocol-checked and immediately rechecked before either RF command.
RX then preceded TX by the runner's intentional 200 ms lead.

Every picotool operation was supervised with finite deadlines: two-second
BOOTSEL info attempts within a 30-second total, and 300 seconds for full-flash
save/load. The R04 runner bounded its own startup and collectors. The campaign
also imposed a 150-second outer trial bound. Infrastructure failures would
stop dependent work and enter restoration; the two observed RF acceptance
failures were retained as independent cells, not hidden by the batch controller.

No USB readiness failure, subprocess timeout or external cancellation interrupted
this campaign. Hardware use of the guards is not a replacement for the
offline injected hang/disconnect/cancellation tests.

## Independent acceptance

The archived [`ampdu_verify.cjs`](../../tools/ampdu_verify.cjs) checked raw
reports against JSON, the exact fresh challenge, all three fixture MPDUs/FCS,
delivered payloads, bitmaps, duplicate counts and terminal lease state.
An additional post-run audit rechecked the entire matrix, profile/build
identity, 46 unique challenges, all snapshot bindings, runner/oracle agreement,
artifact hashes and complete-flash restoration.

For every accepted good fixture, delivery was bitmap 7/count 3/duplicates 0.
Accepted BA fixtures additionally had one applicable BA attempt/start,
zero BA errors and an independently received exact BA with bitmap 7.
Every accepted lease had publication/release/claim counts 1/1/1 and final
claim state 16.

Bad-middle controls observed the three exact configured bodies, rejected the
deliberately bad middle body, delivered bitmap 5/count 2/duplicates 0 and
independently received BA bitmap 5.

All-bad controls observed all three exact deliberately bad-FCS bodies,
completed the operation, delivered bitmap 0/count 0/duplicates 0 and released
the terminal lease. BA bitmap/attempt/start/error counters were zero; each
sender had an initialized receipt record with count zero. An absent report,
missing fixture or timeout was not accepted as evidence of zero delivery.

## Retained failures and fresh repeats

### Normal BA, A to B, MCS1/channel 6

`trials/ba/ab-m1-ch6` completed both operations with error zero and valid
snapshots. The receiver had an initialized empty capture: zero observations,
zero matching fixture/delivery and no publication/release/claim.
No BA was attempted or returned. This failed strict acceptance; a known
empty report is not proof of successful reception.

Its sole fresh repeat, `ab-m1-ch6-fresh-repeat`, passed exact three-body
delivery, terminal lease and independent returned-BA validation.

### Normal BA, B to A, MCS7/channel 6

`trials/ba/ba-m7-ch6` also completed both operations with error zero and valid
snapshots. Three aggregate observations were retained, but none matched the
independently generated fresh good fixture. Delivery remained bitmap/count
zero. Publication/release/claim completed 1/1/1 with final state 16, but no
BA was attempted or returned.

This is not the same evidence as the empty MCS1 capture: retained observations
and lease release did not establish fresh payload integrity. The report does
not establish whether the mismatch originated on air, during capture or in
another receive stage.

Its sole fresh repeat, `ba-m7-ch6-fresh-repeat`, passed exact three-body
delivery, terminal lease and independent returned-BA validation.

These initial failures remain part of the denominator. Passing repeats do
not establish loss-free operation or close R07. The
[R03 campaign](HARDWARE_REGRESSION_R03_2026-10-24.md) remains unchanged;
its separate DMA faults/missing-body failures were not retested or resolved.

## Evidence and restoration

Local evidence is retained in `build/radio-regression-r05-20261024/`:

- `manifest.json` and `source.diff`: image/source provenance, four profiles,
  12 firmware/collector hashes and nine archived tooling inputs.
- `tool-sources/`: exact R04 readiness/supervision/runner and oracle inputs.
- `build-logs/`: matched native/strict/oracle and R04 failure-path results.
- `trials/`: all 46 configurations, raw reports, JSON, boot/readiness logs,
  stdout/stderr and strict pair results.
- `results.jsonl` and `final-audit.json`: chronological attempts, separate
  initial/repeat denominators and independent post-run verification.
- `harness/`: the one-time campaign and audit sources for provenance, not
  authorization to repeat hardware operations.
- `backups/` and `restoration/`: complete original saves, independently
  reread images, bounded operation logs and byte/hash comparison results.

Each badge's complete **16777216-byte** flash was saved twice and independently
compared before any test-image load. After all trials, both original flashes
were restored without execution, independently reread in full, byte-compared
and hash-verified. Both badges were left in BOOTSEL.

| Device | Original and restored SHA-256 |
| --- | --- |
| Badge A | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Badge B | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |

The post-run audit independently repeated the full-file comparisons.
No test image was left on either badge.
