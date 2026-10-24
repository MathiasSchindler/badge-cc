# Handoff, overwrite and copy-wait controls

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Controlled handoff stimulus and reproducible continuation failure

This campaign implements a deterministic source for the existing bounded
170/298-byte next-body diagnostic, not a generalized receive engine or HT
station. The previous historical vendor pair captures remain valid;
repeatability, malformed-input recovery and negotiated operation are still
open. The current campaign does not reproduce a complete valid pair.

### Implemented source and evidence guards

`CLEAN_RX_HT_HANDOFF_STIMULUS`, default zero, enables exact command-12
version 7. It requires HT data and extended RX, rejects station mode and
extra flags, and regenerates and compares every one of the 1132 staged
bytes. Construction reuses the existing fixed-source scratch buffer.
The 478-byte physical PPDU contains delimiter + padded first body +
delimiter + second body, in 170/298 or 298/170 order. The logical
descriptor is 492 bytes; the source stages/readback-checks the zero tail
as well. HT-SIG always describes the actual finite PPDU.

The source interleaves eight legacy/single-HT/aggregate cycles, with
calibrated 40-ms gaps. Cases are good, bad first FCS, bad last FCS,
bad second delimiter CRC and a second delimiter claiming eight bytes
beyond the physical remainder. Only index 2 is faulted; subsequent
aggregates remain good. The malformed case does not transmit an
incorrect HT-SIG or deliberately underrun the TX engine.

The new `reference-handoff` profile wires source selection through RP
staging, controller admission, sender records, passive observer byte
validation, frozen sample retention, collector and runner. Use the
[profile instructions](../../../vendor-burst/README.md#controlled-170298-byte-handoff)
and [API contract](../../../doc/HT20.md#finite-aggregate-diagnostic).
The observer reuses the FIFO and existing one-shot advance; no receive
register/reset experiment or per-frame TFT operation was added.

An initial collector weakness could count a generic second bad-FCS
outcome plus recovery controls as a bad-last success, even though good
controls also failed there. The corrected guard requires complete exact
bad-second bytes, invalid FCS and matching early/second owner and epoch.
The native tests exercise acceptance and ownership, length, terminal,
body and FCS mutations in both orders. Old apparent bad-last passes are
retained as insufficient instrumentation, not negative qualification.
Bad-first qualification still requires a later complete valid pair.
Bad-delimiter/truncated-body cases intentionally cannot pass negative
qualification until their complete expected behavior is established.

Native tests cover both orders, five cases, eight MCS and all 24 indices,
including delimiter/FCS geometry, zero tails, source/receive validation,
disabled/station/command-bit rejection and every staging-byte mutation
of representative aggregate descriptors. Controller and protocol native
tests run with ASan/UBSan. The normal controller remains byte-identical
to the preceding verified default. The old fixed-source FIFO profile
also builds/tests and its controller remains byte-identical to the
previous verified FIFO image. Matching final handoff images/collector
were rebuilt and byte-compared to their RF archives.

### Retained hardware campaign

Evidence is under `build/clean-rx/controlled-handoff/`: raw reports,
JSONL, stderr, loader logs, archived source/observer images and collectors,
fresh full originals, restore readbacks, and independent `audit.js` /
`audit.jsonl`. Role 0 is B source/A observer; role 1 reverses them.
All controlled-source RF trials use MCS0; eight-rate native coverage is
not an eight-rate RF qualification.

| Cohort | Trials | Completed captures | Exact early bodies | Advances | Bad/abort continuations | All controls received |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial broadcast fixtures | 4 | 239 | 4 | 4 | 4 | 3 |
| Unicast pilots | 12 | 817 | 12 | 10 | 10 | 4 |
| Strict five-case/two-order/two-role matrix | 20 | 1405 | 20 | 16 | 16 | 7 |
| Distinct-prefix unicast follow-up | 4 | 296 | 4 | 4 | 4 | 2 |
| Vendor/AP controls, mixed sizes | 4 | 1304 | 1 | 1 | 1 | Not comparable |
| Total | 44 | 4061 | 41 | 35 | 35 | |

The independent audit verifies all 176 raw report headers/build IDs/
session nonces, all 960 controlled-source transmission records against
an independently reconstructed fixture, the final staged descriptors,
complete early bytes/FCS and fresh owner/epoch correlation. All 4061
completed controller captures were posted and consumed, with zero F2
drops. Every populated retained sample has matching stored/computed FCS,
and none is tagged as advanced. All 35 continuation snapshots match their early owner/epoch
and have outcome 2 (bad/abort); no exact valid second body or exact
intentionally bad last body is established.

This conservation result is not zero RF loss: only 7/20 strict-matrix
trials received every interleaved control, and only 12/20 received the
selected recovery controls. All 40 controlled-source trials retain an
exact first body, but the diagnostic retains one early body per boot;
that is not evidence of receiving every aggregate or every first body.
The four vendor controls use the existing independent vendor/AP path
with a byte-identical C3 observer; one reproduces the bad continuation.
They weaken a clean-transmitter-only explanation but do not independently
prove that either source's complete second body was correct over the air.

### Fresh prefix discriminator and narrowed boundary

In all 30 original controlled-source advances, exactly the first eight
snapshot bytes matched the expected second body, then diverged. Those
eight bytes were also identical in the first body, so they could not
prove that a new second prefix had arrived. To remove that ambiguity,
the final source sets the legal More Data bit in the first FromDS frame
and clears it in the second. Addresses and TID remain unchanged; this
does not invent fragments, protected frames or a new receive policy.

All four follow-ups, covering both roles/orders, retain first FC
`88 22`, then a fresh second FC `88 02` and the correct remaining
eight-byte prefix. Thus the second snapshot is not just the unchanged
first header. In every case the first mismatch is byte 8; second RXE
counts are 304/306 or 176/178, versus expected 304 or 176 including
the six-byte receive header. The outcome remains bad/abort and the
remaining bytes do not establish the expected MPDU.

This narrows the observed boundary to reception/copy/terminal handling
after a fresh next-body prefix, rather than missing the entire aggregate,
failure to take the advance, host credit loss or stale early ownership.
It does **not** prove that the radio payload physically corrupts at byte 8:
an invalid/aborted snapshot need not contain a completed hardware copy.
It also rules out treating an isolated final-FCS overwrite as sufficient
explanation for the observed non-FCS byte mismatches.

The actual PSM image was decoded through copy setup, body loop and
GoodFCS completion. Event-drain instructions at PCs 163/164 are
conditional; their fall-through is already represented by existing
event-aware native tests. No new event-semantics defect was demonstrated,
so no speculative receive-loop patch, delay, undocumented TX control
or stronger reset was introduced. The next discriminator is the
next-body copy-engine progress/terminal transition, or an independent
complete on-air second-body capture, not more identical blind repeats.
Correct live multi-body handling remains the blocker for work package 2.
HT association, BA lifecycle/Immediate BA and protection gates remain off.

The final marked-prefix build is `8cdf0e3a`. Its 60004-byte observer
controller has SHA256
`c070b8ad26798a9996be960fe0d1ee0e9d21d388cd939cd1a274ce0958d7daec`;
the identical complete binary is embedded in the archived vendor/AP
observer UF2. Fresh 16-MiB originals were saved before experiments.
After the last marked-prefix trial, both entire originals were restored,
independently saved as `restored-final-{A,B}-full.bin`, byte-compared
and SHA256-verified. Both were left in BOOTSEL; final info logs are
`bootsel-final-{A,B}.log`.

| Badge | Original and final full-readback SHA256 |
| --- | --- |
| A | `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628` |
| B | `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2` |

## Second-body overwrite probe and rejected fresh-count wait

This follow-up distinguishes a stalled live-SHM copy from a changing
diagnostic snapshot, then tests the specific stale-count rearm hypothesis.
It does not repair or qualify the second-body receive path.

### Bounded implementation and fault evidence

The default-disabled `CLEAN_RX_HT_COPY_PROBE` gate requires passive early
retention and one-shot advance. After the complete first body is saved,
it writes and reads back four `0xa55a` sentinel words in the live receive
buffer at MPDU offsets 8, 32, 64 and 128. The original EOI/base/end/copy
sequence then runs. At the continuation terminal entry, a separate
snapshot records 12 existing RXE/TSF registers, the sentinel locations and
24 prefix bytes, before the existing complete continuation snapshot.
Owner/epoch, seed readback and commit phase are checked; no new register
mask, radio reset or source payload is introduced. The probe adds
pre-EOI instructions and changes live first-body bytes after retention,
so it is a controlled intervention, not passive timing qualification.

The dependent `CLEAN_RX_HT_COPY_WAIT` experiment tests whether arming
against the previous body's count causes the failure. It acknowledges
EOI, then waits for a latched count in `[6, first_count)`, checking
EOF/abort and a 65,535-decrement budget before original rearm. Failure
records phase 5/reason 9 and takes the existing PSM fault path; C3 reports
error `0x88` instead of automatically restarting. This gate defaults off
and is deliberately **not** promoted as a fix.

The first four wait trials exposed a diagnostic gap: the RP powered off
after a controller error without exporting stopped SHM. They remain
retained but cannot establish the exact fault state. The copy-probe
error path now pauses PSM and uses an isolated diagnostic loader to read
SHM/mailbox evidence before power-off. It never clears the original
loader/receive failure. Separate collection success/error and reason
fields remain available even though the requested phase still fails.
Two further trials verify the collector's structured `handoff-copy-fault`
output against the raw retained snapshot. Error snapshots are not
success-shaped reports or normal stop/drain evidence.

The probe fits unused SHM words `0x5ca..0x5f3` already included in the
existing 512-word export. The RP-only fault metadata occupies trace bytes
5792..5823. No normal capture/F2 layout is expanded.
See [profiles and field layout](../../../vendor-burst/README.md#copy-progress-probe-and-rejected-wait-control).

### Hardware comparisons and independent audit

All evidence is under `build/clean-rx/copy-progress/`, with fresh full
originals, archived matching images/collectors, source records, raw
reports/JSONL, failed trials, build logs and `audit.js` / `audit.jsonl`.
Source B/observer A is role 0; role 1 reverses them. All RF uses MCS0
and both 170/298-byte orders.

| Cohort | Trials | Completed captures | Result |
| --- | ---: | ---: | --- |
| Ordinary handoff baseline | 4 | 167 | Fresh prefix; bad continuation |
| Initial sentinel probe | 4 | 390 | All sentinel locations overwritten; bad continuation |
| Repeated good/bad-last probe | 8 | 361 | Same overwrite result; no exact good or injected-bad second body |
| Initial wait faults, before SHM export fix | 4 | Not qualified | Original controller failure retained; detailed fault unavailable |
| Wait faults with stopped SHM export | 4 | 9, not drain-qualified | EOF with unchanged first count, reason 9 |
| Structured fault-output verification | 2 | 3, not drain-qualified | Same failure; raw and JSON fields agree |

The independent audit validates 104 raw report headers/build/session
bindings and 624 source records against independently constructed
fixtures, including the final full staged descriptor per run. Every
source-controller binary is byte-identical across these experiments.
The baseline observer controller is byte-identical to the previous
marked-prefix campaign. Normal and final probe controller binaries
are compared against their RF archives after rebuilding.

All 918 completed captures in the 16 normally stopped trials were
posted and consumed, with zero F2 drops. Each of those trials has an
exact first body and a fresh second eight-byte prefix, but no valid
pair. Only 6/16 received every interleaved control: RF losses remain.
The 12 completed captures reported in stopped-fault snapshots are kept
separate: a controller fault prevents normal drain qualification.
The four original unexported fault trials are not assigned capture or
mechanism results merely from their sender completion.

Across all 12 sentinel trials, all four seeds first read back correctly
and all four terminal locations differed from the seeds. Every terminal
location agrees with the later full continuation snapshot, and all
24 independently sampled prefix bytes are stable between snapshots.
The expected body nevertheless fails after the fresh header, and
bad-last trials do not retain the exact intentionally corrupted body.
This excludes a wholly untouched receive buffer and weakens an
unstable diagnostic-copy explanation. It does not establish that every
byte was correctly copied or identify where physical corruption begins.

All six exported wait faults retain the first count (176 or 304), with
EOF status and nonzero remaining budget. EOF arrives after roughly
175..340 TSF units; this is not a countdown timeout. The hypothesized
fresh-count wait therefore prevents the original continuation and
cannot be used as a repair. A pre-arm count is not an independently
advancing freshness signal in this experiment. No undocumented
reset, delay or event-clear sequence was substituted.

### Validation and remaining boundary

Native ASan/UBSan tests cover both size orders, exact sentinel placement
and readback, first-body retention, terminal register/data snapshots,
unchanged owner/epoch, immutable records across new RX, bounds and no TX.
Wait tests model a fresh count, stale-count exhaustion, EOF and abort;
faults must take the explicit bounded failure path without arming.
Protocol tests reject missing owners, wrong epochs/versions/phases,
seed corruption and invalid terminal count geometry.
The ordinary handoff and production-default controller binaries remain
byte-identical to their verified predecessors. All builds remain
freestanding/nolibc; a compiler-generated struct-copy helper was replaced
with the existing byte-copy helper rather than adding a runtime dependency.

The current boundary is an active overwrite path yielding stable but
incorrect continuation bytes, not missing host credit, wholly stalled
copy or merely a moving terminal snapshot. Next work needs a
discriminator for receive/FIFO boundary/length handling versus incorrect
on-air second bytes; independent complete RF evidence remains valuable.
The wait experiment is a rejected hypothesis, not general recovery.
Work package 2, negotiated aggregation, Immediate BA and HT association
remain unqualified.

The final non-wait probe build is `337b6c20`; structured fault-output
verification used `1b01c4b9`. The shared build is left on the non-wait
probe, not the rejected wait control. After the last RF trial, both
original 16-MiB flashes from this campaign were restored and independently
saved as `restored-{A,B}-full.bin`, byte-compared and SHA256-verified
against the fresh backups (the A/B hashes listed above). Final
`bootsel-{A,B}.log` reads confirm both badges were left in BOOTSEL.
No AP credentials were read or provisioned in this campaign.

## Nonterminal second-body and copy-end boundary controls (2026-10-20)

This campaign tests whether the failing second MPDU depends specifically
on PPDU EOF, and whether the six-byte PLCP/count convention explains the
copy-end error. A follow-up tests the first-body streaming transition.
None of these controls repairs reception. They do not uniquely
distinguish RXE/FIFO corruption from incorrect transmitted second-body
bytes, and it does not complete D2 work package 2.

### Implemented controls and evidence repair

- `CLEAN_RX_HT_HANDOFF_GUARD`, default zero, selects exact command-12
  version 8 and a 564-byte physical PSDU. It appends two alignment bytes,
  a valid delimiter and an 80-byte FCS-valid guard to the original pair.
  For a fixed nonce/index, the first and second MPDU bytes are unchanged.
  The guard clones the second header/payload prefix, uses sequence
  `2*index+2`, body marker 2 and a recomputed FCS. HT-SIG describes the
  actual longer PPDU. Staging still verifies every one of 1132 bytes.
  Source, observer length admission, protocol correlation and build
  identity are wired together; version 7 stays unchanged with the gate off.
- `reference-handoff-guard` retains the existing one-shot advance and
  copy probe. It tests only the first two bodies, not delivery of the
  guard. The host rejects non-good case arguments before USB access:
  a second body followed by a guard is no longer the last body, and
  the old overlong-second claim is no longer a physical PPDU truncation.
  Exact raw fault fixtures remain native-tested, not RF-qualified cases.
  The guarded pair predicate requires the 564-byte source and a good,
  non-EOF/non-aborted second-body status; it cannot substitute a terminal
  snapshot or ordinary two-body geometry.
- `CLEAN_RX_HT_COPY_END_PLUS6`, default zero, requires the non-wait copy
  probe. `reference-handoff-end6` changes the copy-end instruction from
  saved next length to saved next length plus six. No source bytes,
  reset bits, delay or stronger event clearing are changed. It is a
  bounded perturbation of the PLCP-inclusive count hypothesis, not a
  proposed general length rule.
- `CLEAN_RX_HT_COPY_QUIET`, default zero, requires the unmodified-end
  non-wait copy probe. `reference-handoff-quiet` clears only the existing
  low two-bit streaming field in RX control after first-body retention
  and before EOI. It records the immediate before/after values in probe
  words 42/43, expanding the probe to 44 words inside the unchanged
  early SHM export. The ordinary 128-byte streaming threshold remains;
  no flush, reset or source change is added. This follows a code-path
  difference from the working fixed 80-byte aggregate diagnostic, which
  stays below that threshold. Native execution verifies the field clear,
  preserved surrounding control bits/registers, unchanged copy end and
  count, and no TX.
- Initial guarded trials exposed a missing evidence path: explicit
  aborts were logged but skipped the full continuation snapshot. With
  the copy probe enabled, outcome 5 now passes through the existing
  bounded snapshot, owner/epoch/length stability checks, and then the
  original abort cleanup. Aborted data is never delivered or recast as
  a good/bad-FCS completion. Wrong/stale owners, already committed
  snapshots and counts outside 50..306 still cannot commit.
  The collector exposes `terminal_sample_committed` and
  `copy_end_plus6_enabled`; an armed-only mask zero is not overwrite proof.

### Retained RF campaign and interpretation

Evidence is under `build/clean-rx/terminal-boundary/`, including matching
image/controller/collector archives, all failed raw reports, logs and an
independent `audit.js` / `audit.jsonl`. Role 0 remains B source/A observer;
role 1 reverses them. Every cohort covers both 170/298 orders and both
badge roles. RF stimulus is MCS0; native tests cover MCS0..7.

| Cohort | Trials | Completed captures | All controls received | Abort exits | Bad-FCS exits | Committed second snapshots |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial guard, before abort export | 4 | 106 | 2 | 4 | 0 | 0 |
| Guard with abort export | 4 | 107 | 2 | 4 | 0 | 4 |
| Two-body copy-end plus six | 4 | 72 | 1 | 2 | 2 | 3 |
| Matched two-body baseline | 4 | 74 | 2 | 3 | 1 | 4 |
| Two-body stream field clear | 4 | 132 | 4 | 2 | 2 | 4 |

All 20 trials retain an exact first body from source index 2 and perform
the one-shot advance. None establishes a complete valid pair. The eight
guarded exits have RX abort set and EOF clear, at counts
223/304/178/178 before abort export and 306/196/62/178 afterward. A
failure that requires reaching PPDU EOF is therefore insufficient to
explain this behavior; a nonterminal second body fails too.

All 15 committed continuation samples have the correct fresh first
eight bytes, with the first mismatch at byte 8. Independent terminal
prefix samples agree with the later full snapshots. Twelve samples
overwrite all four sentinels. Three short aborts, at counts 59/62/62,
retain the two higher sentinels: their mask is 12, not an all-buffer
overwrite. Bytes beyond the reported short count are stale diagnostics,
not received-body evidence. No complete exact second body, good or
deliberately bad, is inferred from these snapshots.

The copy-end control reads back the expected six-byte bias in every
trial. Its exits are counts 190/310/59/182, compared with matched
baseline counts 190/196/62/178. The 310-byte exit is preserved in the
ordinary terminal record but exceeds the unchanged continuation-copy
ceiling, so no full sample is committed. In the reverse-role short-body
cell, count changes from 178 to 182 while the body remains incorrect.
This rejects the specific plus-six fix; it does not establish a general
RXE count interpretation or rule out other FIFO/boundary mistakes.
The source controller is byte-identical across baseline/end6 and to
the preceding copy-progress campaign. Guard-source controllers are
byte-identical before and after the abort-export change.

All four stream-field trials independently record `0x802 -> 0x800`
before EOI, with the ordinary copy end unchanged. Counts
306/196/140/178 still end bad/abort; all four retained second prefixes
diverge at byte 8. The source controller remains byte-identical to
baseline/end6. Receiving every interleaved control in these four boots
is encouraging but is neither statistical recovery qualification nor
a handoff repair. This rejects the specific clear-before-EOI strategy,
not FIFO streaming/ownership as a class of causes.

Across 20 trials, all 491 completed controller captures are posted and
consumed, with zero F2 drops. Only 11/20 receive every interleaved control;
some aborts are followed by substantial RF loss. Conservation of
completed captures does not qualify receive recovery or early reliability.
The independent audit reconstructs all 480 transmitted fixtures and
checks 80 report headers/CRCs/builds/nonces, source record/staging/SIG
bytes, exact early bodies, owner/epoch, programmed copy ends/stream
readbacks, bounded
second snapshots, terminal outcomes and normal drain accounting.

Two attempted cells failed BOOTSEL readiness before flashing or RF and
have readiness logs only; successful repeats use new prefixes. Badge B
intermittently exposed its mass-storage interface while picotool reported
exclusive USB ownership. Serial-to-disk mapping was verified before
copying the existing auto-BOOTSEL observer UF2 to that badge's volume.
The existing collector's missing-peer cleanup then requested BOOTSEL
without starting an RF session. Both recovery-only failures are retained
separately and excluded from the 20 RF trials. No unrelated process was
terminated or USB device reset.

### Validation, restoration and next discriminator

Guard, end6, quiet, ordinary probe and rejected wait profiles run existing
native ASan/UBSan tests and freestanding C3/RP links. Tests exercise guard
delimiter/padding/FCS geometry, all supported fixture indices/rates,
exact staging admission and mutation rejection, nonterminal pair
status/geometry rejection, end-register values, abort snapshot
ownership/bounds/immutability, preserved abort cleanup and no TX.
Final guard, end6 and ordinary probe controller bytes are compared with
their RF archives; the quiet archive retains the exact native-tested
RF image. The ordinary production controller remains byte-identical
to the preceding verified default.

Fresh complete originals were saved before flashing. Both full 16-MiB
flashes were restored and independently verified after the first 16
trials; those fresh full readbacks also preserve the originals before
the quiet follow-up. After the final trial both entire originals were
restored again, independently reread into
`final-restored-{A,B}-full.bin`, byte-compared and SHA256-verified against the
fresh backups. They retain the A/B hashes recorded above.
`final-restore-{A,B}.log`, `final-readback-{A,B}.log` and `final-bootsel-{A,B}.log`
record the final restoration and BOOTSEL checks. No AP credentials
were read or provisioned.

Remaining discrimination needs either an independent complete on-air
second-body decode or a causal receiver-only change that restores exact
bytes/FCS with the same source. The evidence now excludes EOF-only
failure and rejects a simple plus-six copy-end repair; it does not
repair reception by clearing the first-body stream field either. It does not
justify guessed RXE reset masks, a larger successful-looking snapshot
bound, or HT association. D2 handoff, recovery, variable-length buffering,
secure BA lifecycle, Immediate BA/SIFS, mandatory protection and negotiated
interoperability remain open.


