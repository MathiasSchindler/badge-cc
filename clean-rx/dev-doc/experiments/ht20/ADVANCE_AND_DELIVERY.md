# Bounded advance and delivery-state controls

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## D2 qualification checkpoint (2026-10-19)

The root [D2 checklist](../../../../todo.md#d2-ht-phy-and-association) separates
completed foundations from eight remaining work packages. This checkpoint
preserves the implementation and qualification context removed from that
checklist; it does not enable HT association or close its mandatory MAC gates.

Software foundations include extended RX, per-TID receive/CCMP state, gated
responses, bounded per-category access, association/runtime peer profiling
and station RX staging. Non-HT WMM negotiation, AP-derived EDCA, protected QoS
exchanges and bounded protected A-MSDU receive are implemented. Block ACK
agreements, A-MPDU stream parsing and ciphertext reordering have native WPA2
integration tests, not production live-radio qualification. Readback-verified
policy commits are a prerequisite, not actual HT policy activation.

The [aggregate interface record](../ampdu/INTERFACE_AND_DELIVERY.md) keeps the
separate receive paths and their evidence:

- The per-MPDU DMA/F2 experiment delivers the first two diagnostic bodies
  at MCS0/3/7 in both directions. Its final byte-evidence campaign has 6/7
  normal two-body deliveries, one retained RF miss and two successful
  bad-middle rejections. The last DMA body remains missing; finishing this
  alternative is not a prerequisite if the retained-SHM path is used.
- The retained-SHM/SRAM/F2 path delivers all three fixed fixture bodies.
  Its initial MCS0..7 matrix passes 16/16 fresh trials across both directions,
  exactly three deliveries and no duplicates. Bad first/middle/last and
  all-bad controls retain failed initial receptions and successful repeats.
- Explicit PSM state-16 terminal commit/claim/release removes dependence on
  later ordinary traffic, including bad-last/all-bad cases. Its matrix covers
  every MCS/direction cell after repeats, with misses and partials retained.
- Diagnostic compressed BA transmission and independently validated returned
  bytes exist for every normal MCS/direction cell after repeats. Bad-final
  reverse MCS7 remains unqualified. Neither configured hardware SIFS controls
  nor local timestamps independently establish physical EOF-to-air SIFS.

These are fixed-size, bounded diagnostics, not generalized live reception,
loss-free operation, production admission/BA or mandatory receive/protection
qualification. Receive completion/discard, radio override ownership, station
TX cleanup and atomic backoff publication have been corrected and regression-
tested. Local-AP reply delivery and intermittent HT receive corruption remain
qualification limits. Related investigations are recorded in
[AP authentication/RX staging](../rx/AP_AUTHENTICATION.md),
[RF-hint follow-up](../rx/RF_HINT_CONTROLS.md) and
[aggregate radio capture](../ampdu/INITIAL_RADIO.md).

The [controlled independent receive record](../rx/CONTROLLED_HT_RECEIVE.md) covers
small-frame vendor-to-clean MCS0/MCS7 reception on both badges and the later
passive vendor/AP experiments. Fresh-prefix/MAC/F2 correlation validates
complete variable-length ciphertext/FCS samples in both roles, but does not
prove multiple complete bodies from one live PPDU. Exit tracing observes
1598/1342-byte advertised PSDUs reach early good-only hints and then abort
at RX counts 560/304; those prefixes remain unattributed. Later passing
variable-length controls do not reproduce those aborts.

Early-retention build `e9008554` preserves one complete FCS-valid 170-byte
MPDU at hint status `0x1922`, bound to a CRC-valid 478-byte A-MPDU-format
prefix with vendor B/passive A. Native bounds cover every length 44..1000;
oversize candidates are counted. Five other final-build controls fail early
qualification. This is one early-body preservation result, not repeated/
both-role early reception, next-frame advance, two-body delivery or physical
EOF timing. That build enables no early-event acknowledgement, advance or TX.
The controlled receive record links the experiment's commands, ABI, build
IDs, counters, failed controls and independently verified flash restoration.

The [Block ACK implementation record](../../history/BLOCK_ACK_SOFTWARE.md) separates
native-tested software from pending live agreement/key/reorder integration.
The remaining packages retain general receive-size/protection obligations,
production Immediate BA and independent timing, actual HT activation, and
negotiated mixed-traffic interoperability. D3 retains the full rate/size,
throughput and sustained-soak acceptance work.

## Bounded live next-body advance diagnostic (2026-10-19)

This records the initial one-order diagnostic. The subsequent
[live two-body capture](#first-complete-live-two-body-capture-2026-10-19)
supersedes its base hypothesis, buffer layout and no-live-advance result.

`CLEAN_RX_HT_ADVANCE` defaults off and requires the complete early snapshot.
The vendor/AP reference enables it without enabling observer TX, association,
keys or HT advertisement. This is a diagnostic implementation, not completion
of D2 work package 2.

The one-shot gate targets the previously observed 478-byte advertised PSDU:
a retained 170-byte first MPDU, stable RX count 176 and raw next length 298.
It also requires matching prefix ownership, GoodFCS and no current
terminal/abort bit. After retaining the first body, it acknowledges event 6,
clears receive-context bit 7, arms the proven direct-body copy controls and
resumes at body PC163. The continuation base is `FRAME+3`, testing whether
direct-body MAC bytes land at `FRAME+4` while preserving the three HT-SIG
words. That physical layout remains a hypothesis until a live advance occurs.

The advance profile reserves four additional snapshot words, reducing the
maximum retained early body from 1000 to 992 bytes without crossing SHM
`0x600`. It records the one-shot marker, next length and TSF. Diagnostic F2
tagging protects the candidate continuation sample from later same-size
replacement. The host now derives two-body success rather than hardcoding
it false: both full bodies must independently validate, share the fresh
prefix/epoch and addresses/TID, have different unfragmented sequence numbers,
fit the exact delimiter/padding/PSDU boundary and reach a good terminal
with status bit 14. Neither a marker nor duplicate first-body bytes suffice.

Six build `fe06ab77` trials, `build/clean-rx/vendor-advance/bounded-r0` through
`bounded-r5`, alternate vendor B/observer A and the reverse. They retain
44 valid HT prefixes, 39 with aggregation format, and 12 independently
correlated full ordinary MPDUs. Vendor received-BA counters increase in
every trial. None retains an eligible early snapshot; every advance marker
is zero. All six correctly fail the early/two-body qualification predicate.
They therefore do not exercise the physical advance or qualify its recovery.
The tested images, controller blob and collector remain beside the evidence.

ASan/UBSan tests cover every early length 44..992, exact SHM bounds,
register/control preservation, one-shot gating and no TX. Explicitly modeled
second-body good/bad/abort/watchdog paths test terminal ownership, frozen
first-body retention and the next RX epoch, not physical DMA behavior.
Host tests reject copied bodies, bad sizes/FCS/epochs, fragment-only sequence
changes, address/TID/DS mismatches, incorrect PSDU boundaries, missing EOF
and missing advance provenance. Proof sample retention is native-tested.
Default and early-save-only controller regressions pass. Final host/predicate
build `d4873fdc` has a byte-identical controller to the RF-tested build; the
host-only changes were not radio-tested.

The immediate blocker remains repeatable early capture of a real multi-body
PPDU. Keep work packages 1 and 2 open; do not generalize receive, integrate
live BA or activate HT on the strength of a native model or these failures.
The [reference experiment record](../../../vendor-burst/README.md#bounded-next-body-advance)
documents the ABI and retained controls.

## First complete live two-body capture (2026-10-19)

Final build **`d97844a9`**, trial
`build/clean-rx/vendor-pairs/final-r0`, demonstrates two complete,
independently FCS-valid MPDUs under one fresh live vendor PPDU. It is one
bounded positive control, not completion of handoff qualification or
production aggregate delivery.

### Stimulus and diagnosis

The original 128-frame rotating-size burst exposed very few eligible early
bodies. The focused vendor experiment now groups four 1024-byte frames,
four 128/256-byte pairs, then four 512-byte frames, repeated eight times.
It retains exactly 32 fixtures at each size and the six-second submission /
1.2-second drain bounds, while requesting vendor `ampdu_mpdu=2`.
The vendor firmware itself is unchanged, and the clean observer never TXs.
Submission order and limits remain stimulus, not aggregation proof.

New host-call timing instrumentation measures about 130.5 ms for 128 writes,
minimum 260 us and maximum 2020..2023 us. Buckets `<=200`, `201..400`,
`401..800`, `>800` us contain `[0,29,33,66]` frames. These observations
describe transport pacing, not physical airtime or SIFS.

Both build `d9d43b6c` controls `paired-r0` (B/A) and `paired-r1` (A/B)
preserve a full FCS-valid **298-byte early body** under a 478-byte prefix,
status `0x1922`, raw next length 170. The old gate only allowed 170 then
298. The gate now accepts both exact orders; arbitrary sizes remain excluded.

Build `42dde6ec`, `reverse-r0`, exercises the first real advance, but its
continuation is bad-FCS. Build `330f04e6`, `frozen-r1`, retains the complete
failed candidate: duplicated initial MAC bytes and corrupted later header
bytes. No contiguous 170-byte window, including the saved live prefix,
passes independent bitwise CRC32. This is not merely missing transport
or stale host attribution. The `FRAME+3` base hypothesis was not validated.

The continuation base now uses aligned `FRAME`, matching earlier fixed-
fixture prior art. No byte shifting, reconstructed prefix or forced-good
FCS is used. Build `ad73353b` controls still fail: `aligned-r0` has no vendor
prefix, `aligned-r1` reaches count 306 beyond that build's 304-byte
snapshot limit, and `aligned-r2` freezes a bad continuation at count 178.
The final raw window also retains these two-extra-byte failures.

### Evidence retention and qualification

ABI 7 splits the existing 1024-byte dump into two disjoint SHM halves,
without extra USB pages or concurrent live object-selector reads:

- First half `0x400..0x4ff`: 16-word header and up to 480 early-body bytes.
- Second half `0x500..0x5ff`: 12-word header and a fixed 300-byte raw
  continuation window. Header fields are commit-last ID, epoch, count,
  status1/status2, TSF, three live prefix words, hardware good/bad outcome,
  and actual base register. Only the bounded count-derived prefix is a
  candidate frame; stale suffix bytes are not evidence.
- Only an attempted advance with matching owner/epoch may commit one
  continuation; missing/wrong ownership, short/oversize input or an
  existing commit cannot replace it. Good/bad logging does not admit a
  bad frame as good F2 data.
- The default-disabled and early-save-only profiles retain their previous
  behavior; the latter still has its 1000-byte early-body capacity.

The final `final-r0` positive, vendor B / passive A, has:

- Fresh prefix ID **4**, epoch **132**, SIG `00de010fb003`, PSDU **478**.
- A **170-byte early MPDU**, independently valid FCS, nonterminal `0x1922`.
- One advance to a **298-byte** candidate, base `0x800`.
- A complete **298-byte good continuation**, RX count **304**, status
  **`0x4122`**, independently valid FCS, and actual tagged ordinary F2
  publication. Its frozen raw bytes match the complete F2 body.
- Matching nonce-derived TA, addresses and TID. Unfragmented Sequence
  Controls are **`0x04a0`** and **`0x0490`**: distinct bodies, not proof of
  ordered delivery or a correct secure reorder lifecycle.
- Exact boundary **`4 + align4(170) + 4 + 298 = 478`**.

The existing strict collector exits successfully with both pair and
complete-multi-MPDU predicates true. An independent bitwise CRC32 readback
check also verifies both FCS values, exact lengths, addresses, distinct
sequences, boundary and equality of frozen/F2 continuation bytes.

Three additional final-build controls retain failures:
`final-r1` (A/B), `final-r2` (B/A), `final-r3` (A/B) capture no eligible
early candidate, hence no advance or pair. This is **one success in four**,
not repeatable/both-role handoff. Intermediate bad-continuation trials
resume later good receptions; physical abort recovery after advance remains
unqualified. `frozen-r0` was instead a host readiness failure: badge B
disappeared from USB, and the user reconnected it in BOOTSEL before
`frozen-r1`. It is not RF evidence.

All raw trials and tested images/collectors are preserved by build ID.
ASan/UBSan cover both size orders, every early length 44..480, complete
bounded continuation copying, negative commit/owner/epoch/length guards,
immutable first-body retention, modeled good/bad/abort ownership and no TX.
Default/original-image and 1000-byte save-only regressions pass.

Work packages 1 and 2 remain open for repetition across roles, lengths,
terminal/bad/abort recovery and capture-loss diagnosis. The next production
step is not HT activation: repeated aggregate buffering/backpressure,
secure agreement/reorder/key lifecycle, Immediate BA and mandatory
receive/protection obligations remain gated.

Both reference images currently compile the portable PIO FIFO loop without
`BADGER_FAST_BYTES`. The existing fast path in
[`wifi_pio.c`](../../../../src/platform/badger/wifi_pio.c) is a concrete next
transport-pacing control to test independently. The submission timings do
not yet establish it as the cause or cure of capture losses.

Both full 16-MiB originals were backed up before this campaign and matched
to the previous verified originals. After the trials both were restored,
independently reread as `vendor-pairs/restored-{A,B}-full.bin`, and
byte/hash-compared against `vendor-pairs/original-{A,B}.bin`. Both original
SHA-256 values match; both badges are left in BOOTSEL.

## Shorter live retention and transport pacing control (2026-10-19)

The vendor-only fast-FIFO control, build `00ece242`, reuses the existing
RP2350 assembly FIFO/setup/cleanup helpers and byte-zero routine only for
the vendor's PIO translation unit. The observer retains its portable PIO
path, and its clean controller byte-compares equal to `d97844a9`.
Both role arrangements (`build/clean-rx/vendor-fifo/fast-r0`, `fast-r1`)
submit all 128 fixtures and retain a good 298-byte early body, but their
170-byte continuations fail FCS, at RX counts 176 and 178 respectively.

Submission totals are **130334/130315 us**, versus about **130467 us**
for the prior portable-path control; the bucket distribution stays
`[0,29,33,66]`, and minimum/maximum calls remain about 259/2019 us.
This does not demonstrate a useful pacing improvement. The extra assembly
linkage and vendor-only compiler flag were removed rather than retained as
an unqualified optimization. Its tested executables remain as evidence.

The next change shortens first-body preservation itself. With advance
enabled, exact 170/298-byte bodies use 85/149 direct SHM moves, sharing the
85-word prefix and conditionally copying the remaining 64 words. Other
lengths keep the guarded, odd-tail-safe path. Commit-last ownership,
epoch/SIG/RX-count revalidation, GoodFCS/EOF/abort gates and all proof
predicates are unchanged. Unsupported-length guarded blocks are omitted
only from the advance profile, whose maximum remains 480 bytes; the
1000-byte save-only profile remains byte-identical.

Native full snapshot-to-timeout paths execute **181/247 modeled
instructions**, bounded by tests to 200/300 for 170/298 bytes. This is not
physical RF timing. All lengths 44..480 still pass byte-for-byte copy,
odd-tail, exact memory-boundary, register/control-preservation and stale-
copy rejection tests. The controller shrinks **2936 bytes**, from 63096
to 60160 text bytes; the observer shrinks by the same included-blob amount.

Final build **`2e8ac062`**, `build/clean-rx/vendor-fifo/copy-r0`, vendor B /
passive A, gives the first independently verified **reversed-order pair**:

- Fresh ID **2**, epoch **18**, valid 478-byte HT-SIG prefix.
- Complete **298-byte early body**, nonterminal `0x1922`, followed by
  one advance and a complete **170-byte good continuation**, count **176**,
  terminal `0x4122`.
- Both complete FCS values pass independent bitwise CRC32; frozen
  continuation bytes equal the actual protected F2 sample.
- Addresses/TID match, Sequence Controls **`0x00b0` / `0x0080`** differ,
  and **`4 + align4(298) + 4 + 170 = 478`**.
- The unchanged strict collector succeeds. Together with the earlier
  forward-order success this demonstrates both bounded size orders,
  not repeated or ordered station delivery.

All six final-build controls are retained:

| Trial | Vendor / observer | Result |
| --- | --- | --- |
| `copy-r0` | B / A | 298/170 pair passes |
| `copy-r1` | A / B | No vendor HT prefixes |
| `copy-r2` | A / B | No vendor HT prefixes |
| `copy-r3` | B / A | Ordinary frames, no eligible early candidate |
| `copy-r4` | A / B | No vendor HT prefixes |
| `copy-r5` | B / A | Good 298-byte early body; bad continuation, count 177 |

The preserved **unmodified `d97844a9` executables and collector** were also
retested in the missing-capture direction as `baseline-r0`: they likewise
capture zero vendor HT prefixes, despite vendor received-BA activity.
Thus those zero-capture controls cannot be attributed solely to the new
copy path. The cause remains unresolved; no failed trial is relabeled
as a passing PHY reception.

In `copy-r5`, the failed advance is prefix ID 2 / epoch 44. Later good
prefixes resume at epoch 47, and independently valid ordinary F2 samples
refer to later IDs 9 and 11. This is live bad-frame-path recovery evidence,
not a physical abort or loss-free-reception qualification.

Only **one of six** optimized-copy trials passes the two-body predicate.
The software optimization is bounded and native-verified, but these samples
do not establish a reliability improvement or prove that copy latency is
the corruption's cause. Recorded copy-to-arm TSF deltas remain about
4..5 us across passing and failing controls; they are software observations,
not independent SIFS measurements.

Default and save-only builds/tests pass and byte-compare equal to the
previous corresponding controller binaries. Runtime code remains C/nolibc,
no dependencies or observer TX are added, and the minimal USB main stays
102 lines. The next qualification problem is the intermittent/missing
capture and corrupt continuation, not a reason to enable HT advertisement.

Both originals were fully backed up in `build/clean-rx/vendor-fifo/` before
flashing and matched to the previously verified originals. After all
controls both 16-MiB flashes were restored, independently reread and
byte/hash-compared with those backups. Both original SHA-256 values match;
both serial-matched badges remain in BOOTSEL.

## Frozen delivery-state and reverse-role pair (2026-10-19)

The saved zero-HT controls are not all the same receive failure. In the
previous `vendor-fifo/baseline-r0`, `copy-r1` and `copy-r2`, the frozen PSM
good-completion counts are 1/4/1, with 69/77/62 bad-FCS discards and only
1/4/1 ordinary F2 deliveries. In `copy-r4`, 257 good completions and 254
ordinary F2 deliveries coexist with zero HT starts and six bad-FCS discards.
The saved readout lacks the complete transport accounting needed to assign
a cause; supported vendor rate/channel queries show no configuration
discrepancy. These observations do not establish a calibration, RF, filtering
or delivery-stall diagnosis.

Build **`d6c2e83c`** adds observer ABI 8 in existing unused trace space.
After controller `RX_DONE`, it retains all 480 mailbox bytes, the 40-byte
capture/discard/DMA header and host receive/credit state. Two bounded ordinary
F2 summaries include short control frames, computed/received FCS and up to
40 MAC bytes. Only the first two delivered frames incur this extra CRC work.
The collector checks the one-frame credit bound and reconciles controller
consumption, host consumption and ordinary delivery counts. Existing prefix,
early/continuation snapshots and strict two-body admission are unchanged.
The controller synthesizes the mailbox PSM-state entry as captured after
any successful capture; that entry is not an instantaneous held-state flag.
The trace stays 5888 bytes; the clean controller byte-compares identical to
the preserved `vendor-fifo/2e8ac062-controller.bin`. No radio, channel, TX
power, antenna or calibration setting was changed.

Six fresh bounded trials, with all failures retained under
`build/clean-rx/vendor-receive-state/`:

| Trial | Vendor / observer | HT starts | F2 posted/consumed | Transport drops | Bad-FCS discards | Early/handoff result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `state-r0` | B / A | 7 | 272 | 5 | 8 | Good 298-byte early body; bad 170-byte continuation |
| `state-r1` | A / B | 9 | 505 | 10 | 125 | Complete 298/170-byte pair; collector passes |
| `state-r2` | B / A | 5 | 267 | 10 | 7 | No eligible early body; one oversize early candidate |
| `state-r3` | A / B | 7 | 810 | 14 | 79 | No eligible early body |
| `state-r4` | B / A | 8 | 1361 | 17 | 255 | No eligible early body |
| `state-r5` | A / B | 11 | 1308 | 16 | 348 | Good 170-byte early body; bad continuation, RX count 306 |

All six stop at controller state 10 with zero controller error, scanner
target `0xffffffff`, zero F2 pending and zero host resync/stall/retry state.
Independent reconstruction of each split mailbox verifies
`captured = posted + dropped` and `posted = consumed = host F2 count`.
These are finite diagnostic transport drops, not loss-free production RX.
Vendor received-BA deltas are 115/119/111/116/117/111; `state-r3` also has
one vendor TX failure and one retry. All runs submit the same 128 fixtures.
The zero-HT condition did not recur: no cause or fix for it is claimed.

`state-r1` is the first complete live pair with vendor A / observer B:
prefix ID 1, epoch 22, 478-byte advertised PSDU, good 298-byte early body
at RX count 304 / status1 `0x1922`, followed by a good 170-byte continuation
at count 176 / status1 `0x4122`, RX base `0x800`. A separate Node bitwise
CRC32 implementation verifies both complete bodies, matching addresses,
distinct sequence controls `0x00b0`/`0x0080`, exact
`4 + align4(298) + 4 + 170 = 478` boundary and equality of the frozen
continuation with the actual tagged F2 body. This supplements prior B/A
positives; it does not demonstrate both size orders in both roles.

Independent bitwise checks also confirm the good early/bad continuation
distinction in `state-r0` and `state-r5`. After the failed handoffs,
ordinary F2 reception recovers: `state-r0` has a complete FCS-valid
298-byte sample at ID 6 / epoch 204, after failed epoch 179; `state-r5`
has complete 298/170-byte samples at IDs 7/10, epochs 124/182, after failed
epoch 12. Each later sample matches its fresh terminal MAC snapshot.
This extends live bad-frame-path recovery evidence to both roles, not
physical abort recovery or ordered station delivery.

Only **one of six** new trials passes. HT reception now has a positive in
the previously missing direction, but repeated early capture, handoff
reliability, variable-length/repeated aggregate delivery and physical
abort/SIFS qualification remain open. The new counters distinguish explicit
transport drops from corrupt or absent early/continuation bodies; they do
not locate the underlying RF/receive-state cause.

The reference build/native sanitizer tests pass, including summary lengths
4..3896, short-frame padding, invalid lengths, corrupt FCS and output
boundaries. Controller native tests, freestanding no-undefined-symbol links,
runner syntax and whitespace checks pass. Text/data/BSS are
244504/28/40004 for vendor, 74772/28/23232 for observer (including controller)
and 60160/0/6408 for controller. The observer increment is 412 text and
16 BSS bytes; no new dependency, display, station or BA application is linked.

Before the campaign, fresh full-flash backups matched the previous originals
byte-for-byte and by expected SHA-256. After all six trials, both complete
16-MiB originals were restored and independently reread into
`vendor-receive-state/restored-A-full.bin` and `restored-B-full.bin`.
Both readbacks byte-compare equal to their fresh backups and retain the
original SHA-256 values. Both serial-matched badges are verified in BOOTSEL.


