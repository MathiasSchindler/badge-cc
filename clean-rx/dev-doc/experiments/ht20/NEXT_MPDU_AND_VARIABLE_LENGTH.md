# Next-MPDU commands and variable-length reception

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Next-MPDU command and bounded RX recovery experiments (2026-10-20)

Follow-up to the vendor-reference hints distinguishes the command at a
**real MPDU boundary** from the mid-body replay studied above. Selected
instructions were independently decoded from the local packed vendor
image: the vendor changes its body-start field from `0x83` to `0xc3`
for subsequent subframes. This is evidence for a controlled experiment,
not an authoritative universal definition of register bit 12.

### One-byte next-MPDU discriminator

`CLEAN_RX_HT_NEXT_FIELD` defaults to `0x83`; `0xc3` requires the bounded
advance/copy diagnostic. Only `OBSERVER_ADVANCE_COPY` uses this flag.
Initial prefix/body commands and all mid-body replay fields are unchanged.
New `reference-handoff-next-c3` and
`reference-handoff-defer-next-c3` targets match the existing ordinary
128-byte and deferred 384-byte copy profiles respectively.

Evidence is retained in `build/clean-rx/next-command/`. Each matched
receiver pair has the same 60,372-byte size and differs at exactly one
byte, file offset 57152 in PSM PC 3103. Decoding that instruction confirms
the sole change is field `0x83` versus `0xc3`. All four version-7 source
controllers byte-match the independently vendor-verified source.

| Good-source cohort | Trials | Exact retained/F2 pairs | Every ordinary control |
| --- | ---: | ---: | ---: |
| Ordinary threshold, `0x83` | 4 | 0 | 0 |
| Ordinary threshold, `0xc3` | 6 | 6 | 2 |
| Deferred threshold, `0x83` | 4 | 4 | 4 |
| Deferred threshold, `0xc3` | 4 | 3 | 2 |

The ordinary-threshold cohorts cover both badge roles and both body
orders; the `0xc3` cohort adds long-continuation repeats in both roles.
The `0x83` failures retain bad-FCS/abort continuations. Every retained
good `0xc3` continuation has the exact complete 170- or 298-byte body,
native GoodFCS, valid full software FCS and native count **176 or 304**
respectively. Exact matching advanced F2 bytes are archived. After-arm
control changes from `0x802` to `0x1802`; the streaming low bits remain
enabled. No replay, SHM destination splice, count normalization or
relaxed FCS/length gate is needed for these pairs.

This identifies a missing next-MPDU command-field distinction as a
causal repair for the bounded streaming handoff. It supersedes segmented
reconstruction as the necessary repair for these particular fixtures.
It does not establish larger-body, repeated variable-length aggregate
or receive-reliability qualification. One deferred `0xc3` trial never
retains an eligible first body; it is a failed trial, not an exact pair.
Ordinary control misses remain visible in the successful-pair trials.

Eight additional `0xc3` trials cover bad-first, bad-last, bad delimiter
and truncated body in both roles. Both retained corrupted second bodies
have exact corrupted bytes, native bad FCS/count 304 and no advanced
export. Both bad-first source frames have a linked native bad-FCS exit;
the retained good pair comes from a later source aggregate. Only the
B-role bad-first/bad-last trials satisfy the existing full negative-case
recovery predicate. The A-role recovery controls are incomplete.
Malformed-delimiter trials retain later good source indices 11 and 8,
not the deliberately damaged aggregate's second body. Truncated-length
trials do not advance because the next length fails the fixed fixture
gate. **Neither malformed nor truncated case qualifies a generalized
delimiter/EOF recovery path.**

The independent fixture/bitwise-FCS audit checks all **26 RF trials,
104 full reports, 624 source descriptors, 5,132 conserved captures and
41 exact archived F2 samples**. There are 17 exact pairs across all
cohorts, including later good PPDUs in negative trials. Ordinary controls
are **347/416**. No invalid-control packet is hidden or admitted.

### Ordered flush and instrumented stuck-prefix recovery

New default-off diagnostics are implemented in `rx_recovery.h`:

- `CLEAN_RX_HT_ORDERED_FLUSH=1` runs an adapted vendor-ordered sequence
  at each rearm; value `2` restricts it to the existing one-shot rearm
  fault-recovery branch. Both require the passive timeline/recovery
  parent and exclude the separate host PHY-reset experiment.
- The sequence preserves FIFO-control high bits, selects low byte 2,
  clears the explicit copy length, issues `0x14` with a read barrier,
  clears SPR `0x048` bit 6, and waits for event 10 to deassert. This wait
  has a 65,535-poll bound. If it clears, the sequence writes crypto
  control `0x8300`, pulses `0x0309 -> 0x0300`, delays ten countdown
  iterations, issues a second barriered `0x14`, pulses SPR `0x1ec`
  `9 -> 0`, and returns to the existing RXCTL verification.
  Timeout records phase 4 and explicit PSM fault reason 9, then stops;
  it never bypasses the wait to claim recovery.
- `CLEAN_RX_HT_PREFIX_RECOVERY=1` instruments prefix entries and
  count-2/RX-active polls. Only that state persisting to a wrap-safe
  TSF elapsed time of at least 250 us triggers a recorded PHY-control
  `old | 0x000e` pulse, ten preservation instructions, restoration of
  the exact old value, and rearm. Trigger count, native count/status,
  saved PHY control and elapsed time are exported.
  The original prefix fallback is **1,023 polls**, not the 65,535
  assumed in the hints. This diagnostic deliberately uses 65,535
  polls so its TSF deadline has a larger fallback budget; this change
  is gated and is not a production timeout adjustment.

Diagnostic words occupy the existing rearm export's last 16 words,
mutually exclusive with the host-reset record. WND9 flags 16/32
identify ordered/prefix diagnostics; frame wrappers and normal RX
metadata are unchanged. The source-side fixture remains the unchanged
version-6 fixed 554-byte/two-body source, requiring argument `512`.

Evidence is retained in `build/clean-rx/rx-recovery/`. The initial
proactive profiles omitted the SPR `0x048` bit-clear; their failures
are preserved separately from the corrected `final-*` profiles.
The complete proactive sequence still stops at event 10 on five of
eight final trials, before reaching the crypto pulses. Across both
proactive revisions there are **ten bounded event-10 timeouts**.
This disproves treating the supplied sequence as an unconditional
drop-in rearm repair in the current clean RX environment.

The fault-only profiles preserve ordinary rearms:

| Final fault-only cohort | Trials | Natural recovery attempts | Early bodies / expected | Ordinary controls |
| --- | ---: | ---: | ---: | ---: |
| Ordered sequence | 4 | 0 | 32/32 | 61/64 |
| Ordered + prefix instrumentation | 4 | 0 | 31/32 | 60/64 |
| Software-fault injection + ordered sequence | 2 | Not a physical wedge | 16/16 | 32/32 |

The injected trials retain the deliberate software-latched busy fault,
show native RXCTL is actually clear at ordered-sequence entry, execute
one sequence to phase 3, then deliver all early/control frames in both
roles. They prove real-hardware execution and branch recovery, **not
repair of a physical `0x804` wedge**. Neither uninjected fault-only
cohort encounters that wedge. Two legacy-control trials retain native
rearm failures. Prefix count-2 observations occur, but **no natural
250-us prefix recovery pulse is triggered** anywhere in this campaign.
It therefore does not establish the cause of pre-prefix/zero-HT misses.

The recovery audit checks **31 RF trials, 186 full reports, 31 arm
reports and 744 complete source descriptors**. Twelve trials end in
controller errors and remain failed, including the ten event-10
timeouts and two legacy rearm failures. Among the 19 completed trials,
all **1,128 captures** are conserved, **142/152** expected early
packets have complete independently byte/FCS-checked archives, and
ordinary controls are **275/304**. These completed-only denominators
do not exclude the twelve failed trials from the campaign.
One unsupported-size collector invocation submits no source burst;
its empty report/stderr are retained, and the running fixtures are
recovered through a separately named explicit-512 capture.

Emitted-program ASan/UBSan tests cover flush write ordering/barriers,
ten-iteration drain, busy clear/stuck bounds, integration with the
one-shot first-fault path, prefix 249/250/251-us boundaries, wrong
count/inactive exclusion, TSF wrap, exact ten-instruction PHY pulse
and register restoration. Seven invalid gate combinations fail
compilation. Current default, `0xc3`, full-prefix and fault-only-prefix
controller validation builds byte-match their corresponding archived
images; the default also matches the pre-experiment controller.

Fresh complete originals were saved before the campaign. After the last
injected RF tests, both entire 16-MiB originals are restored, independently
reread, byte-compared and SHA256-matched: A `8cda7a00...23628`,
B `bc3f972d...21e2`. The authoritative final evidence is
`rx-recovery/last-restored-{A,B}-full.bin` and matching info/log files;
both devices are in BOOTSEL. No credentials were read or provisioned.

## Variable-length boundaries and initial stream state (2026-10-20)

The next small fixture generalizes body geometry before introducing an
N-body loop. `CLEAN_RX_HT_VARIABLE=1` selects source descriptor version 9;
all production gates and the earlier version-7/8 fixtures remain separate.
The shared source/controller validator reconstructs and compares the
entire 1,132-byte command, including unused zero tail. It explicitly builds
every body, payload, sequence, delimiter, padding and FCS rather than
moving a fixed 554-byte body and merely changing its advertised size.

Eight length pairs are rotated across the eight actual aggregates in
each 24-transmission burst:

`80/554`, `127/381`, `170/298`, `298/170`, `257/255`,
`479/554`, `80/80`, `321/173`.

Both orders and badge roles are exercised. A selector rotates which pair
is transmitted in source aggregate 2. Sixteen ordinary controls retain
their original per-order 170/298-byte sizes. Negative mutations affect
only aggregate 2, preserving later independent recovery controls.
Nonce profiles 4..19 encode the selector/order; other profiles remain
rejected by the version-9 builder. Source admission, controller command
validation, observer F2 classification, collector exact-pair correlation
and independent raw-source reconstruction use the same wire geometry.

The new observer path is still bounded to **one advance and one immutable
early slot**, not a descriptor ring. It retains the existing native
count/owner/epoch/GoodFCS checks and EOF/abort exclusions, rejects a busy
next-length register, bounds the first body to 44..480 bytes and the
continuation to 44..554 bytes, and requires:

`PSDU = 8 + align4(native_first_count - 6) + next_body_length`.

There is no count normalization, replay or destination splice. Odd first
bodies exercise padding and odd last bodies exercise complete software
FCS/F2 verification. The terminal snapshot still holds only the first
300 bytes of a continuation; larger-body integrity comes from the full
archived F2 sample, not reading beyond that snapshot. Bad-second exact
correlation explicitly refuses lengths exceeding its snapshot capacity.

### RF results and the newly exposed dependency

Evidence is under `build/clean-rx/variable-handoff/`, with immutable RF
profiles `dev`, `stream-before`, `stream-after` and `stream64`.

- The full original-threshold geometry matrix has **32 trials**. Thirty
  retain an exact pair, but only **25/32** retain an exact pair from the
  deliberately selected source aggregate 2. A 554-byte first body exceeds
  early-slot capacity; later eligible pairs must not be counted as success
  for that source. One other cell also retains a later source.
- Native GoodFCS, native count and full F2 bytes are exact for retained
  odd-length pairs in both roles, including 127/381, 257/255, 255/257,
  479/554 and 321/173. For the 554-byte continuation the native count is
  **560**, with no normalization.
- **80/554 fails in both roles** at native threshold 128. The 80-byte
  first body has native count 86 and exact software FCS; the continuation
  times out at native count **474**, not 560. Before the boundary,
  `SPR0x004` is `0x0800` rather than the `0x0802` seen for streamed first
  bodies; after `0xc3` it is `0x1800`, rather than `0x1802`. A short
  80/80 pair succeeds in both roles. Length alone and `0xc3` alone are
  therefore insufficient to establish general continuation behavior.
- A matched one-instruction bit-1 OR immediately **before** `0xc3` still
  fails in both roles, now timing out at count **86**. Moving the same OR
  immediately **after** `0xc3` fails in both roles at count **474**.
  The late control value is not equivalent to the prior copy/stream state.
- Lowering only the normal native stream threshold from **128 to 64**
  produces an exact **80/554** pair at source aggregate 2 in both roles,
  with native count **560** and `0x0802` before the boundary. Four targeted
  trials retain four exact pairs, but only **three** select aggregate 2;
  the fourth selects a later 127/381 pair. Canonical 170/298 pairs also
  remain exact in two threshold-64 regression trials. Five of the six
  threshold trials fail the existing collector policy because of missed
  controls or the invalid-control evidence below. Its one policy pass
  selects the later 127/381 pair, not the targeted 80/554 source aggregate.

These observations isolate a dependency on **initial native streaming
state/history**. They do not establish the internal hardware mechanism,
prove a safe universal replacement threshold, or justify replaying a
small first body or adding its count to a continuation.

### Negatives, losses and validation

Eight odd-length negative trials use selected 257/255 bodies in both
roles: bad first, bad last, malformed delimiter and truncated advertised
second length. Both bad-last trials preserve the exact corrupted 255-byte
body, native count **261**, native bad-FCS outcome 2, and no second-body
publication. Both bad-first trials preserve native rejection; only one
also has the required later pair/recovery controls. Only one bad-last
trial has its required later controls. Those **two** trials pass the
existing negative policy. Neither malformed-delimiter nor truncated-body
cell is promoted to qualified recovery; later good pairs do not repair
the deliberately damaged PPDU's qualification gap.

Across **all 50 RF trials** (not just successful cells):

- **200** full reports and **1,200** source descriptors independently
  match report/header CRC, source wire bytes/CRC, exact lengths, commands,
  HT-SIG, delimiter mutations, sequence and nonce.
- **2,530** total captures, including unrelated traffic, are conserved
  across controller/DMA/F2/host accounting.
- **39** retained pairs and **99** archived F2 bodies have independently
  checked complete expected bytes and software FCS; pairs also match
  native count/GoodFCS, owner and source index.
- Ordinary controls are **726/800**. One threshold-64 trial records
  **one invalid control**, zero duplicate controls, and fails delivery
  integrity despite having an exact target pair and all control-mask bits.
  No failure is hidden behind the exact-pair count.
- Only **13** trials pass the existing collector policy: eleven good
  and two negative. There is no all-rate/repeated-aggregate qualification.

`audit.js` and its passing `audit.jsonl` independently reconstruct every
source packet, compare complete F2 bytes/FCS and native terminal counts,
check every denominator and failure, and verify final flash restoration.
The 480-byte early slot and 300-byte terminal snapshot remain explicit
bounds, not inferred upstream staging capacities.

ASan/UBSan source tests cover **1,920** variable fixtures, both orders,
all pairs/indices/negative cases, payload/FCS/padding and exact command/F2
admission. Emitted-PSM tests cover **400** variable geometry cases:
44..480-byte first bodies, 44..554-byte continuations, low/high PSDU
mutations, busy delimiter, native GoodFCS, odd tails, register preservation,
and exact stream-probe instruction position/bit preservation.
Eight invalid gates/combinations fail compilation.
Fresh validation builds byte-match all four archived RF receiver images;
the source controller also byte-matches its RF archive. Default and
canonical `0xc3` builds byte-match their pre-extension controller images.

The matrix paused after a BOOTSEL readiness failure, without flashing
the next trial. The failed readiness log is retained separately. Badge B
was restored immediately; badge A required a user USB replug, then was
also fully restored/verified before further testing. After the final RF
trial both complete original 16-MiB flashes were restored, independently
reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`.
The authoritative final proof is
`variable-handoff/final-restored-{A,B}-full.bin` with matching info/log
files; both badges are in BOOTSEL. No credentials were accessed.

**Next implementation boundary:** qualify short-first initial/continuation
copy-state handling and loss accounting, then replace one-shot early
retention with bounded per-body ownership, delimiter/EOF advancement and
transport backpressure. No N-body ring or generalized malformed/abort
recovery was implemented or qualified by this fixture extension.
D2 remains incomplete and HT advertisement remains off.

## Verified hint follow-up: SHM limits, short-first streaming and admission (2026-10-20)

The remaining useful hints were tested against the new size matrix rather
than assuming register meanings or extrapolating from 170/298. Evidence is
under `build/clean-rx/hints-shm/`.
`vendor-instructions.jsonl` records a direct seven-byte big-endian decode
of selected instructions in the local vendor image, including
`0x08aa: op 0x504, a 0x100e`, the stream/split writes, and delimiter-ready/
EOF polling. This verifies the cited predicates, not every semantic label
or claimed timing/capacity in the hints.

### SHM-only proposal: tested and rejected for general reception

`CLEAN_RX_HT_SHM_ONLY=1` replaces only the PC-311 mid-body stream write
with a jump to the existing completion path. Initial commands, `0xc3`,
copy base/end, native count/FCS checks and final header publication remain
unchanged. The matched variable fixture was run in both badge roles:

| Selected first / second | Native first count | Terminal continuation count | Result in each role |
| --- | --- | --- | --- |
| 80 / 554 | 86 | 426 | Abort |
| 127 / 381 | 133 | 382 | Abort |
| 170 / 298 | 176 | 304 | Exact pair |
| 257 / 255 | 263 | 250 | Abort |
| 479 / 554 | 485 | 26 | Abort |
| 321 / 173 | 327 | 179 | Exact pair |

Thus all eight long-case failures reproduce with matching native counts
across both roles. The failing cumulative counts are 512, 515, 513 and
511; exact smaller pairs total 480 and 506. This is evidence of a bounded
upstream resource near that region, **not** proof of an exact universal
512-byte FIFO model: alignment/prefix bookkeeping and the internal
mechanism remain unresolved. The 4-KiB SHM address window does not remove
this limit. The hints' claim that disabling streaming permits full bodies
through 3,896 bytes is therefore not adopted.

### Implemented: short-first-only streaming and delimiter predicate

`CLEAN_RX_HT_SHORT_STREAM=1` uses a bounded, register-preserving PSM
threshold dispatcher. Only a pending **first aggregate body** with current
copy length 64..121 selects native count 64. It explicitly checks pending
ownership, no prior advance and the aggregate HT-SIG flag. All other
cases retain 128: ordinary/legacy frames, single HT frames, larger first
bodies and continuations. The 64-byte minimum remains beyond the required
MAC/CCMP prefix. Bodies shorter than 64 are not generalized by this change.
The initial command, continuation `0xc3`, copy geometry, replay and FCS
policy are unchanged; there is no counter reconstruction.

This makes the earlier global-64 diagnostic more surgical. Four targeted
80/554 trials of the isolated short-first path select aggregate 2 and
deliver both bodies exactly in both roles/repeats, with native count 560.
Its six canonical/80-byte regression trials also select exact pairs.
Ordinary losses remain visible rather than being inferred away from those
ten pair successes.

`CLEAN_RX_HT_DELIMITER_GUARD=1` adds the vendor's `SPR0x00e` bit-4 exclusion
at the actual next-body admission point, in addition to busy/length/
padding/EOF/abort/GoodFCS gates. It is verified by direct vendor decoding
and emitted-program fault tests with valid next length/native GoodFCS.
Real malformed-delimiter tests still show no bit 4 in their **terminal**
status-2 snapshots. Do not claim that a real latched CRC-error trigger or
general malformed recovery was demonstrated. Invalid/null delimiter
handling beyond this bounded two-body fixture remains open.

### Implemented: reject short native counts even when GoodFCS is set

A pre-fix `short-guard-A-128-p1` trial retains first body 127 and expected
continuation 381, but native terminal count is **243 instead of 387**,
with native status 1 `0x4100` (GoodFCS/EOF). The old copy admission accepts
this and publishes a **237-byte** advanced F2 prefix with invalid software
FCS. Its original source, snapshot, raw native status/count and invalid
F2 bytes are retained. This is a failed trial, not an exact-pair result.
It exposes a safety gap even without modified mid-body replay.

The existing owner/epoch-aware native-length quarantine now applies to
**all variable handoff profiles**, not just altered-replay profiles.
An advanced, owned GoodFCS completion must have native count
`saved_next_length + 6` before FIFO admission. Otherwise outcome 4 and
the existing length-rejection counter are recorded, without changing
native count/status or rewriting the earlier immutable snapshot's native
outcome. There is no software-FCS waiver or success-shaped fallback.
This reuses the established quarantine rather than adding another
publication path.

Emitted-program tests exercise exact counts and one-byte short/long,
stuck count 6 and the real 243-count failure across seven lengths:
**35** admission cases. All mismatches reject before FIFO; native
registers remain unchanged and snapshots preserve native data when their
bounded geometry permits a snapshot. Good exact-length bodies still pass.
No natural copy-count fault happened in the final RF matrix, so rejection
of that fault is proven by these emitted-program cases, not presented as
a physical-wedge recovery result.

### Final build, complete campaign and hardware restoration

The public `reference-handoff-safe-variable` target selects short-first
streaming plus the delimiter-status predicate; every variable build gets
the native-length quarantine. These remain opt-in diagnostics. RF images
are frozen under `safe-short/`.

The final matrix is **24 trials**:

- **16/16 selected good pairs**, all eight length pairs in both roles,
  with exact source index 2, complete bytes/software FCS, native GoodFCS
  and native continuation count. This includes 80/554 at count 560 and
  127/381 at count 387.
- Eight negative trials retain bad-first/bad-last rejection. Four pass
  the existing negative/recovery policy; malformed/truncated cases remain
  unqualified even where later source pairs arrive.
- **364/384 controls**, **837** conserved captures, zero invalid or
  duplicate controls. Only **10/24** pass the existing collector policy
  (six good, four negative); the exact-pair count is not reliability.
- No malformed archived F2 sample in the final profile. The malformed
  pre-fix prefix remains separately auditable.

Across **all 64** exploratory/final RF trials: **256** full reports,
**1,536** verified source records, **4,513** conserved captures,
**944/1,024** controls, **47** retained exact pairs and **126** exact
archived F2 bodies. The **one malformed archived F2 sample**, two
delivery-integrity failures and all control misses remain failures.
There are **20** existing collector-policy passes and seven qualified
negative-policy results, not 64 reliable receptions.

`audit.js` adapts the earlier independent fixture reconstruction with
explicit allowances only to **record** pre-fix malformed output and
non-exact native snapshots. It prohibits malformed archived output in
the final profile and preserves all denominators. Its passing
`audit.jsonl` also verifies raw report CRC/build/nonce/source geometry,
sample FCS/native count/owner, conservation and original-flash restoration.

ASan/UBSan builds test 1,920 source fixtures, **480** geometry/delimiter/
GoodFCS cases, the 35 native-admission cases and **44,880** exhaustive
short-first threshold/context cases. Ordinary and continuation thresholds
are tested unchanged. Default and the canonical non-variable `0xc3`
receiver images remain byte-identical to the earlier archives; both final
receiver/source controller binaries byte-match the final RF archive.
The final default comparison uses
`variable-handoff/validation-default/cyw43_rx_fw.bin` as its archive.
All 21 invalid-value, missing-parent and conflicting-gate compile cases
fail through the intended static assertions; `git diff --check` passes.

After 40 exploratory trials both full originals were restored. The final
matrix then paused on badge B's BOOTSEL readiness failure, before flashing
the next trial. Badge A was fully restored immediately; after the user's
USB replug, badge B was fully restored/verified before resuming.
Following the last trial both original 16-MiB flashes were again restored,
independently reread and byte/hash checked, with BOOTSEL confirmed.
Authoritative **last** evidence:
`hints-shm/last-restored-{A,B}-full.bin` and matching info/log files,
A `8cda7a00...23628`, B `bc3f972d...21e2`.
Earlier `final-restored-*` files precede the last RF matrix.
No credentials were read.

The hints' already-tested `0xc3` lead remains useful; ordered-flush and
250-us prefix recovery keep their earlier physical-recovery limitations.
Count normalization, presumed unlimited SHM-only capture, nonexistent
descriptor-ring symbols and the suggested incorrect MAC/BA offsets are
not adopted. N-body ownership/backpressure, all-rate receive reliability,
malformed/abort recovery and HT MAC/BA obligations remain open. HT
advertisement stays disabled and D2 is not complete.


