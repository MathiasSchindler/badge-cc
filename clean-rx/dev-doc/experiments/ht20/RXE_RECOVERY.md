# Burst windows, RXE rearm and parked recovery

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Zero-HT reproduction and PHY-control comparison (2026-10-19)

The follow-up preserves evidence under
`build/clean-rx/early-capture-followup/`. It completes **20 RF trials**:
ten baseline trials with `86101c9a`/ABI 14, then ten opt-in PHY-control
trials with `02242390`/ABI 15. Two additional baseline role-1/512 attempts
fail BOOTSEL readiness and are retained separately, not counted as RF
failures or submitted bursts.

The baseline reproduces the previously missing actual stopped-state
evidence. Four complete trials have zero HT starts:

| Trial | Observer | Ordinary F2 deliveries | Bad-FCS count | Actual SHM `0x601` | Raw PSMDEBUG |
| --- | --- | ---: | ---: | ---: | ---: |
| `baseline256-role1-r0` | B | 4 | 62 | 3 | 3637906 |
| `baseline256-role1-r6` | B | 4 | 88 | 11 | 3571880 |
| `baseline512-role0-r1` | A | 818 | 51 | 3 | 98958 |
| `phy256-role1-r4` | B | 2 | 69 | 3 | 2392718 |

These are **not one established failure mechanism**. The three B cases
have very low ordinary reception with ongoing bad-frame processing.
The A case delivers 818 ordinary frames despite no recorded HT start.
Its first two ordinary samples independently pass FCS. Zero HT starts
therefore must not be described as a dead receiver or, now, a B-only
symptom. Vendor counters confirm all four submitted 128 frames, received
104..124 BAs and had zero TX-error/TX-failure deltas; one has one retry.
None has a final F2 backlog/drop or host resync/stall/retry state.

Actual final PSM state/debug does not distinguish these cases from
HT-positive reception. For example, raw debug **3637906** also occurs in
`baseline256-role1-r4` with 18 HT starts; state **11** occurs in the earlier
qualifying `stopped256-role1-r5`. Raw debug **2392718** also occurs in the
HT-positive baseline repeat 7. These comparisons do not require an assumed
PC mask. They exclude a unique terminal-state signature, not every
transient or an unobserved in-window stall.

### Opt-in PHY endpoints

`reference-phy` adds only `CLEAN_RX_HT_PHY_SNAPSHOT=1` to the early-only
profile. The default-disabled controller probe reads six existing
initialization/control registers using the established **16-bit PHY
selector/read** helpers. It must not use a 32-bit write to the adjacent
PHY address/data pair: that is a packed PHY configuration write, not a
read-selector operation.

The Cortex-M3 records the controls before MAC RX enable and after MAC/PSM
disable, before the stop path reapplies TX inhibit. There is no per-frame
sampling, RP2350 access to a live PHY selector, PHY configuration write,
calibration retry or clean transmission. The 32-byte commit/phase/value
block fits the explicitly asserted SRAM gap `0x203e0..0x203ff`; the host
reads it only after `RX_DONE`, into trace bytes 872..903. ABI 15 requires
the expected magic and both committed phases when enabled; disabled
profiles report `enabled:false` and require the reserved bytes to be zero.

All ten instrumented trials, **including the low-RX zero-HT boot**, have
identical before/after controls:

| PHY register | Raw value |
| --- | ---: |
| `0x440` | `0x3e01` |
| `0x44a` | `0x0080` |
| `0x4b0` | `0x0080` |
| `0x43a` | `0x0040` |
| `0x4b1` | `0x0004` |
| `0x439` | `0x0bd0` |

In particular, the source's PHY-deaf bit (`0x440` bit 0 clear) is not set
and its radio-reset bit (`0x44a` bit 8) is clear at both endpoints.
Differences in these sampled latched controls do not explain the failed
boot. This does not measure dynamic AGC, OFDM/HT acquisition, antenna gain,
analog calibration quality, rail noise or transient resets.

All four zero-HT trials also have the same stopped clock/PMU vector as
HT-positive trials. Completion/calibration values remain nonseparating.
Do not replace these findings with a speculative PHY reset or calibration
write, or hide the failure by automatically retrying until a good boot.

### Capture and infrastructure accounting

The 189 saved headers contain eight logged multi candidates: three retained
first candidates and five deliberately ignored later candidates.
The three retained 298-byte bodies independently pass bitwise CRC32,
fresh nonce-derived TA, prefix, epoch and exact-length checks.
The other records are 141 terminal singletons, 31 singleton good hints
and nine non-A-MPDU headers. No logged first eligible retention failure
is observed. The four complete 512-byte fixture trials produce no retained
554-byte early body; finite logs and overflow still prevent a complete
on-air opportunity census.

All 80 raw phase reports have the expected 5952-byte length and independent
report CRC. Full delivery accounting passes for all 79 preserved old/new
captures under ASan/UBSan.

After the last baseline role-0 trial, A disappears from both BOOTSEL and
the live-report USB interface. A serial-targeted software BOOTSEL request
cannot find it. The user reconnects both badges in BOOTSEL, after which
the instrumented cohort completes. This is a separate USB/reboot-transition
failure, not proof that a cable caused the preceding RF miss. The reconnect
also prevents treating between-cohort success differences as a controlled
effect of instrumentation or fixed physical placement.

Final native-only identities are `8736ed06` (PHY probe), `a3d7caa4`
(early-only) and `e7fa68cc` (default advance). The additional commit/range
tests do not change the RF-tested PHY controller. The disabled early-only
and advance controllers byte-compare with their preceding images, and
the ordinary non-observer controller remains byte-identical too.
All three profiles pass controller and native/sanitizer tests.

The next discriminating measurements are **burst-window** receive readiness
and PHY-error activity, rather than only final state or totals over the
1.2-second drain; safe comparisons of OFDM/HT acquisition state for low-RX
boots; and independently confirmed multi-body stimulus for the 554-byte
case. The then-reported 836-us minimum 512-byte host write was in raw TIMER0
counts, not calibrated microseconds; see the correction below. Submission
speed remains worth testing separately from receiver initialization. Neither
a tuning fix nor a physical hardware defect is established.

Both complete original flashes were restored after this follow-up campaign.
Independent 16777216-byte readbacks at
`build/clean-rx/early-capture-followup/restored-{A,B}-full.bin`
byte-compare with the fresh originals and match their verified SHA256 hashes.
Serial-targeted checks confirm both badges remain in BOOTSEL.

## Burst-window diagnosis, RXE rearm, and fault recovery (2026-10-19)

**Correction from the subsequent decision-level investigation:** the first
strict guard had a conditional-drain fall-through bug. Its reason-3 faults
do not establish that the longer hardware wait failed. The old native model
incorrectly treated the conditional drain as an unconditional branch.
See [the corrected guard investigation](#rearm-decision-capture-and-conditional-drain-correction-2026-10-19).
Original-rearm low-RX/stale-data observations and independently verified
captured bytes below remain valid.

The next campaign uses fresh complete originals in
`build/clean-rx/burst-window-investigation/`, independently byte/hash-matched
to the preceding verified originals before experimental writes.
Role 0 is vendor B / observer A; role 1 is vendor A / observer B.
No credentials are included in the images or evidence.

### Passive window instrumentation and timing correction

Default-disabled `CLEAN_RX_HT_WINDOW=1` publishes cached HT/log/early metadata
through a seqlock in the existing 32-byte diagnostic gap at `0x203e0`.
The RP2350 samples this publication, cached receive mailbox, capture count,
controller errors and direct PSMDEBUG at approximately 100-ms intervals.
It does not touch the live D11 object or PHY selectors. Phase 3 arms the
timeline; separate phase 4 exports it after radio power-off without
overwriting the frozen capture report. Capacity is 36 samples with explicit
overflow accounting; final C3 cleanup invalidates the publication.
The PHY endpoint and window profiles cannot share the gap.

The first pilot exposed a measurement error: RP2350 TIMER0 counts were
labelled microseconds despite the inherited BOOTSEL clock state.
The application's already existing USB SOF calibration now appears in
CRC-bound report bytes 40/44. Across the calibrated cohort, about
15,091..15,290 ticks span 32,000 microseconds, not 15,091..15,290 microseconds.
ABI 16 converts scalar durations with checked calibration and 64-bit
arithmetic; bucket boundaries remain explicitly raw ticks. No clock or
RF configuration is changed. The separate 37.4-MHz RF reference is not
diagnosed as defective by this observation.

Historical timing such as the previously labelled 836-us minimum
512-byte write is uncalibrated raw TIMER0 counts. Exact retroconversion
is not possible without that boot's calibration. New calibrated 256-byte
minimum writes are 946..958 us; the guarded 512-byte minimum is about
1771 us, compared with approximately 724-us nominal MCS0 airtime for a
554-byte MPDU. This constrains queue buildup, but neither proves nor
excludes aggregation. Existing firmware transport deadlines remain raw
timer counts; this is measurement correction, not global timebase repair.

### Failure localized to receive processing, not stopped cores

Four fixed pilot role-1/256 trials include two valid 298-byte early
multi-body captures and one low-RX zero-HT failure. Eight planned calibrated
original-rearm trials complete seven RF exchanges: four more valid 298-byte
captures and another low-RX zero-HT failure. The eighth is a readiness
failure, retained separately.

In calibrated `timed256-role1-r3`, coherent samples show:

| Elapsed us | Captured | Bad-FCS counter | RXCTL |
| ---: | ---: | ---: | --- |
| 14 | 1 | 1 | `0x800` |
| 100053 | 2 | 4 | `0x806` |
| 200089 | 2 | 10 | `0x806` |
| 300111 | 2 | 12 | `0x806` |
| 2830766 | 2 | 90 | `0x804` |

The cached PLCP remains `403134010882`, RXE count remains 897, and HT
starts/headers/early identity remain zero. Cortex-M3 polls advance from
61,440 to 2,318,336 and direct PSMDEBUG changes. This excludes a permanently
halted C3/PSM core, not every transient stall. The rising bad-FCS count
can represent recycling of stale events rather than independent on-air
corrupt frames. A coherent empty pre-burst observer log also excludes a
previously consumed 16-prefix log as this failure's explanation.
Pilot `window256-role1-r2` similarly retains four ordinary frames while
bad-FCS advances from 10 to 93 and RXCTL stays `0x806/0x804`.

An independent audit of all 79 preceding captures finds:

| Capture group | Final RXCTL distribution |
| --- | --- |
| 68 HT-positive | 65 at `0x800`, two at `0x802`, one at `0x806` |
| Eight low-RX zero-HT | Five at `0x804`, three at `0x806` |
| Three ordinary-RX zero-HT | Three at `0x800` |

Thus the persistent busy/stale-data/no-progress combination is a strong
lead, but bit 2 alone is not a unique failure detector. HT-positive
`early-capture-causes/size128-role1-r0` ends at `0x806`, and new healthy
`timed256-role0-r2` samples `0x804` transiently. Ordinary-RX HT-only misses
remain a different unresolved class. Prior calibration values also overlap
healthy boots; no analog hardware defect or tuning remedy is established.

A disappears from USB after the third complete calibrated role-0 trial.
A serial-targeted software recovery finds no live interface. The user
reconnects both badges in BOOTSEL. The USB interruption is separate from
the RF failure; the reconnect prevents fixed-placement causal comparisons
between the pre/post cohorts.

### Concrete rearm path and the hidden-restart confound

Decoded original PSM startup and rearm paths differ:

- Startup PC80..87 requests RXCTL `0x14`, waits up to 65,535 polls and
  explicitly faults with SHM `0x602 = 3` on timeout.
- Rearm PC274..280 requests the same flush, waits only 255 polls, then
  silently proceeds to the external-condition drains even if bits 2/4
  still read busy. It subsequently restores the receive base/limit and
  returns to the receive loop.

Default-disabled `CLEAN_RX_HT_REARM_WAIT=1` extends the latter bound to
65,535 and verifies both bits before event draining, using the original
fault capture path with reason 3 on failure. Native tests execute the
real instruction words for all relevant bit combinations and completion
delays `0,254,255,256,4096,65534,65535,65536`. The original disabled image
is reconstructed byte-for-byte.

The first eight guarded trials all see HT, including a valid 554-byte
early body in `rearm512-role0-r0`. Four fixed original-rearm reversal
trials also all see HT, with bad-FCS counts 27..203 and no early bodies.
Neither cohort reproduces low-RX failure, so this comparison is not proof
of a repair.

More importantly, guarded bad-FCS counters decrease from one to zero in
some timelines. The existing non-station controller automatically calls
`d11_restart_rx_psm()` on PSM fault, resetting those diagnostics.
Consequently the eight guarded trials test **longer wait plus existing
fault-triggered restart**, not waiting alone. Their final zero counters
must not be presented as absence of errors or recovery.

The final strict profile disables that automatic RX-fault restart,
counts restart attempts, and exports the actual error address/value.
The separate restart call after an explicit TX/rearm command is not
reachable in this passive experiment: it boots with TX disabled and
checks that flag before arming. Other profiles retain existing behavior.

Three fixed `noretry256-role1-r0..2` controls all fail explicitly with
controller state 255 / error `0x88`, no HT starts and zero recorded restart
attempts. The next two fixed `fault256-role1-r0..1` controls export SHM
`0x602` reason **3**, with zero restart attempts, across all 60 retained
samples. Both have already faulted at the first sample, before the vendor
burst. Their PSM-latched and later RXCTL are `0x800`, so the busy condition
is not proved permanent: it can clear between the branch decision and
fault readout. Snapshot coherence is correctly false after fault cleanup;
direct error fields are not claimed to be coherent live RX snapshots.

The initial interpretation was that longer waiting alone failed.
The subsequent conditional-drain correction retracts that interpretation:
these five faults can be generated by the experimental guard itself.
They are not a transparent reproduction of the intermittent original-rearm
failure. The original RXE flush/stale-event lead still needs independent
qualification. Do not promote the flawed control into normal RX.

### Evidence, validation, and qualification boundary

The campaign retains 23 completed capture trials, five completed
deliberately non-restarting fault controls, and one separate readiness
failure. No failed trial is discarded:

| Prefix/profile | Build | Format | RF exchanges |
| --- | --- | --- | ---: |
| `window256-role1-r0..3` | `88aabd0c` | ABI15/WND1, raw ticks | 4 |
| `timed256-role1-r0..3`, `timed256-role0-r0..2` | `96cb83c4` | ABI16/WND2 | 7 |
| `rearm256-role1-r0..3`, `rearm256-role0-r0..1`, `rearm512-role{0,1}-r0` | `9d736374` | ABI16/WND3, automatic recovery | 8 |
| `reversal256-role1-r0..3` | `f1c25d92` | ABI16/WND3, original rearm | 4 |
| `noretry256-role1-r0..2` | `236d8fa9` | ABI16/WND4, no RX-fault restart | 3 |
| `fault256-role1-r0..1` | `ce436e87` | ABI16/WND5, actual fault reason | 2 |

Each profile's images, controller, build identity and collector are archived.
The [experiment README](../../../vendor-burst/README.md#timer-units-and-current-window-encoding)
specifies the versioned window layouts. There are 168 raw phase reports:
140 at 5952 bytes and 28 arm acknowledgements at 64 bytes. Independent
length/CRC checks pass; all 778 timeline records are retained with no
capacity loss. Frozen delivery accounting reconciles for the 23 completed
captures; the five fault controls are not successful frozen captures.

Seven fresh early nonterminal bodies independently pass bitwise FCS,
nonce-derived transmitter address, epoch and HT-SIG linkage checks:
six 298-byte bodies (`window` role1 r0/r3; `timed` role1 r1/r2 and role0
r1/r2), plus the 554-byte guarded role-0 body. The latter is valid byte
evidence despite the recovery confound, not reliability qualification.
The 279 retained prefix records classify as seven retained multi candidates,
six later prior-multi-already-retained cases, 196 terminal single bodies,
46 single-body good hints, 22 non-A-MPDU and two invalid HT-SIG records.
No logged first-eligible early-copy failure is demonstrated.

All five final reference profiles and the ordinary controller pass focused
build/native/ASan/UBSan tests. Ordinary, early, PHY and advance controller
bytes remain identical to their preceding verified images. The final strict
controller matches RF build `ce436e87`; final native-only wrapper identities
and footprints are in the README. No HT advertisement, negotiated aggregate
operation, physical-hardware remedy, reliability threshold or D2 completion
is claimed.

Both complete original flashes were restored after the final fault controls.
Independent 16777216-byte readbacks at
`build/clean-rx/burst-window-investigation/final-restored-{A,B}-full.bin`
byte-compare with the fresh originals and match their verified SHA256 hashes.
Serial-targeted checks confirm both badges remain in BOOTSEL. A transient
BOOTSEL discovery failure and a rejected readback-option ordering are
retained in the preservation logs; the corrected final verification passes.

## Rearm decision capture and conditional-drain correction (2026-10-19)

Tracing the strict control's exact instruction flow identifies a software
defect in that diagnostic, not a newly established hardware defect:

```
verify RXCTL bit 2 -> fault if set
verify RXCTL bit 4 -> fault if set
conditionally acknowledge GoodFCS/ext3 -> original PC281 if set
fault: reason = 3
```

When ext3 is absent, the conditional drain falls through into `fault` even
when RXCTL is `0x800`. In the original image the drain's branch destination
and its fall-through are both PC281. Appending a fault block immediately
after the relocated instruction changed this property. The previous native
rearm interpreter unconditionally assigned the branch target for opcode
`0x783`, so it could not detect the absent-event path.

The correction inserts an explicit resume jump after the conditional
acknowledgement. Native execution now models event presence, conditional
branching and acknowledgement separately, exercises all four ext3/ext6
combinations across the existing 32 busy-bit / eight delay cases, and checks
that successful rearm clears only ext3 while faults preserve pending events.
Each case also runs with live RXCTL clearing after the decision latch, so
the guard must still use the retained operand rather than the later read
(2048 modeled executions in total).
The disabled ordinary/early/PHY/advance behavior is not intentionally changed.
No speculative drain-order change is made before testing this correction.

The strict profile also retains 16 PSM decision words at SHM `0x7b0..0x7bf`:
sequence, exact latched RXCTL branch operand, remaining wait count, RXE byte
count, status1/2, copy length, PHY length, TSF low/high, success/fault outcome,
later RXCTL, non-clearing GoodFCS event sample, bad-FCS count, RX epoch and
retained-header count. Both guard tests use the same latched RXCTL value;
the later read is separately identified. An odd/even PSM sequence protects
updates; C3 marks a changed sequence invalid during its cached copy at
`0x20d00`, inside the existing C3 publication. Automatic RX-fault restart
remains disabled in the strict profile, so a true first fault is preserved.
This is cached diagnostic evidence, not a simultaneous PHY snapshot.

ABI 17 / WND6 extends the window header from 24 to 64 bytes, leaving the
36 existing 156-byte rows and 5888-byte transport unchanged. Header words
24/28 hold enabled/reserved fields; bytes 32..63 hold the last cached PSM
decision. Disabled profiles require zero decision bytes. Encoding, reserved,
disabled-data and torn-publication checks are covered by native tests.

### Fixed RF comparison and a genuine exhausted rearm

Fresh complete originals in
`build/clean-rx/rearm-decision-investigation/original-{A,B}.bin` are independently
byte/hash-matched to the preceding verified final readbacks before flashing.
Four original-rearm 256-byte trials (two per role) run first, followed by eight
corrected strict trials (two per role/256-byte and role/512-byte cell).
There are no retry-until-good trials or manual reconnects in this matrix.
The original-rearm controller byte-matches its previous window image.
RF wrappers are `0f33f4ae` (baseline) and `5381cb74` (corrected strict).

All four baseline trials finish radio capture without controller fault or
recorded restart, but none qualifies an early multi-body capture. All see HT.
Seven corrected strict trials also finish without controller fault or recorded
restart. Three qualify fresh 298-byte early bodies:

| Trial | Prefix / RX epoch | Independently checked body |
| --- | --- | --- |
| `rearm256-role0-r0` | 5 / 47 | 298 bytes |
| `rearm256-role1-r0` | 6 / 96 | 298 bytes |
| `rearm256-role1-r1` | 2 / 49 | 298 bytes |

Their complete-body bitwise FCS, nonce-derived transmitter address, HT-SIG
CRC, epoch/prefix linkage and nonterminal geometry are independently checked.
The corrected guard resumes with both absent and present GoodFCS events.
This removes the false pre-burst reason-3 failure; the old native model
and control are not used as evidence that waiting alone fails.

Crucially, `rearm512-role0-r0` now records a **genuine** exhausted rearm:

| Decision field | Retained value |
| --- | --- |
| PSM sequence | 1786, committed/even |
| Exact RXCTL tested by both guard branches | `0x804` |
| Wait counter remaining | 0 |
| Later RXCTL | `0x804` |
| Decision outcome | 2, busy-bit fault |
| RXE count / status1 / status2 | 444 / `0x5800` / `0x40` |
| GoodFCS event sample | 0 |
| Bad-FCS count / RX epoch / retained headers | 134 / 758 / 1 |
| Direct controller state / error / SHM reason | 255 / `0x88` / `0x602 = 3` |
| Recorded PSM restart attempts | 0 |

The last active sample at 2,900,831 us has 610 captured frames. At 2,921,820 us
the receiver has faulted after 611 frames and one HT start. This is not a
pre-burst startup fault or an inference from the later stopped RXCTL:
the decision itself retains the busy operand and exhausted wait counter.
The PSM record is committed; whole-mailbox C3 coherence is correctly false
after fault cleanup, not relabelled as a coherent live PHY snapshot.
One otherwise completed 256-byte trial also has a final cache-coherence
miss; its independently verified frozen early body does not depend on that
window snapshot.

This directly establishes that a longer wait can still exhaust under real
traffic, independently of the corrected software fall-through. It does not
prove that every historical zero-HT case has this cause, nor establish why
RXCTL bit 2 remains busy. Faults after hundreds of frames and transient
healthy busy-bit samples argue against diagnosing a simple permanently
broken radio. Preserve this first-fault record when testing one-shot
event-drain/flush recovery; then compare fixed controls and repeat recovery
under load before changing normal RX.

No 554-byte early body qualifies in the four corrected 512-byte trials.
Three finish without controller fault; the fourth is the genuine busy
timeout above. A stronger independently confirmed multi-body stimulus is
still needed, separately from receiver recovery. The four baseline plus
seven completed strict captures retain 68 classified prefixes: 36 terminal
single bodies, six single-body good hints, three retained multi candidates,
one later prior-multi-already-retained, 20 non-A-MPDU, one unsupported early
MCS and one invalid HT-SIG. The faulted trial's one header is visible only
in cached metadata, not miscounted as a successfully exported frozen log.

All 72 raw phase reports independently pass bitwise CRC and length checks:
60 full 5952-byte reports and 12 64-byte arm acknowledgements. All 360
window rows are retained without capacity loss. Eleven successful frozen
captures reconcile controller/post/drop/consume accounting, including
explicit drops and permitted pending frames; the faulted capture remains
an error, with no synthetic successful readout.

All five final reference profiles and the ordinary controller pass builds
and focused native/ASan/UBSan tests. Ordinary, early-only, PHY, advance and
original-rearm window controllers remain byte-identical to their preceding
verified images. The corrected strict controller matches RF `5381cb74`;
final native wrapper identities and measured footprints are in the README.
The strict diagnostic remains default-disabled. Neither recovery reliability,
554-byte repeatability, HT-only misses, normal HT advertisement nor D2
completion is claimed.

Both complete original flashes were restored after this fixed matrix.
Independent 16777216-byte readbacks at
`build/clean-rx/rearm-decision-investigation/restored-{A,B}-full.bin`
byte-compare with the fresh originals and match their verified SHA256 hashes.
Final serial-targeted checks confirm both badges remain in BOOTSEL.

## Parked one-shot PHY receive reset (2026-10-19)

The failed natural event-drain/reflush motivates a stronger, separately
gated control, not a change to normal RX. The public CYW43439 datasheet
does not specify the internal RXE command bits sufficiently to justify a
new guessed reset mask. The original PSM abort at PC266..269 resets
crypto controls (`0x7c0/0x7d0`), not the PHY. Reusing it is therefore not
an independent stronger PHY-reset experiment.

The selected recipe already appears in `radio_phy.c` during RX-path
configuration and AGC finalization, and in the separate source-backed
`r3_rx_path_configure`/AGC implementation:

1. Write PHY `0xa00 = 0`.
2. Clear PHY `0x440` bit 0 (deaf).
3. Set PHY `0x44a` bits `0x44`.
4. Write PHY `0x44a = 0x80`.
5. Write PHY `0xa00 = 0xff`.
6. Set PHY `0x440` bit 0 (undeaf).

This is repository-grounded startup prior art, **not prior qualification
as a live RXE timeout remedy**. The earlier authentication investigation
also tried a PHY reset without per-execution instrumentation and did not
establish a reliable fix. The current test instruments actual execution.

`CLEAN_RX_HT_REARM_RESET` defaults off and requires one-shot strict passive
recovery. The first busy decision remains immutable at SHM `0x7c0..0x7cf`.
After the existing selective event drain, the PSM writes a request at
`0x7d0` and enters a bounded 65,535-iteration reply loop. C3 services the
request on every RX poll, pauses the PSM without restarting it, rechecks
the request, and verifies TX is disabled and the four existing PHY TX
override controls remain inhibited. It permits at most one reset execution.
It applies the six operations and validates final PHY `0x440` bit 0,
`0x44a = 0x80`, `0xa00 = 0xff`, and unchanged inhibition before acknowledging.
All register operations are fixed-length, without unbounded I/O polling.

The PSM then retries its original bounded RXE flush. A busy retry still
faults with reason 3; C3 rejection/readback failure faults with reason 8;
a missing reply faults with reason 7. The reply check precedes the final
zero-count check, so a reply at the last modeled opportunity is accepted.
There is no core or PSM restart, counter clearing, TX or normal RX change.
The normal diagnostic restart-attempt counter must remain zero.

ABI19/WND8 retains 35 160-byte samples with a 128-byte header. Modes 5/7
identify natural/injected PHY reset; 0/1/3 retain previous meanings.
SHM `0x7d0..0x7df` is copied to header bytes 96..127, below the RX-header
and C3 capture-storage boundaries:

| Word | Reset record |
| --- | --- |
| 0 | State: 0 unused, 1 request, 2 complete, 3 rejection/readback failure, 4 reply timeout, `0xffff` invalid cached copy |
| 1 | Actual C3 execution count, at most one |
| 2..3 | Original MACCONTROL low/high |
| 4..6 | PHY `0x440/0x44a/0xa00` before |
| 7..9 | The same registers after |
| 10..11 | Before/after four-control TX-inhibit masks (15 means all four inhibited) |
| 12..13 | Elapsed C3 service TSF low/high |
| 14..15 | RXCTL before/after PHY reset, before retried flush |

The final sample word now holds the 16-bit first-fault commit and 16-bit
reset state separately. A completed PHY reset is not itself proof that
RXE flush recovers or that later frames are received; both are checked
independently. Injected mode still modifies only a latched decision, not
physical hardware RXCTL.

Native execution tests cover the emitted handshake instructions, successful
and persistently busy retry, rejected/missing replies and the exact final
reply opportunity/timeout boundary. The six-operation recipe is checked
against the startup sequence and modeled over varied initial values,
preserving unrelated PHY bits and TX overrides. Protocol tests cover mode
and reset-state encodings, disabled payloads, cached invalidation markers
and capacity. Both experimental targets retain the framebuffer-free screen.

### Instrumented injected execution

Fresh whole-flash originals in
`build/clean-rx/phy-reset-investigation/original-{A,B}.bin` are exactly
16777216 bytes each and byte/hash-match the preceding independently
verified readbacks before any experimental load.

Four fixed injected trials (`aad318de`, mode 7), at 256/512 bytes in both
roles, execute exactly one C3 reset each. All four report state 2, execution
count 1, before/after TX-inhibit masks 15, and valid final PHY readbacks.
The before and after values are `0x440 = 0x3e01`, `0x44a = 0x80` and
`0xa00 = 0xff`; intermediate reset writes are covered by the native recipe
test, while the execution count distinguishes running it from merely
observing unchanged final values. Service takes 16 TSF units in each trial.
The later PSM decision succeeds at RXCTL `0x800`.

| Trial | First to last sampled captures | Final HT starts |
| --- | --- | --- |
| `injected256-role0-r0` | 0 to 274 | 5 |
| `injected256-role1-r0` | 2 to 214 | 2 |
| `injected512-role0-r0` | 1 to 597 | 3 |
| `injected512-role1-r0` | 0 to 345 | 1 |

All four preserve the first forced fault, have no controller error or PSM
restart, and reconcile frozen delivery accounting. None qualifies an early
body. This verifies actual live reset execution and subsequent receive
progress from an injected decision, not recovery from physical busy RXE.

### Fixed natural controls and a real recovered timeout

The fixed matrix adds four 512-byte event-drain/reflush controls
(`9fd0f901`, mode 1) followed by ten natural PHY-reset trials
(`0157638f`, mode 5): one 256-byte trial per role, then four 512-byte
trials per role. No RF trial is retried until success.

Both role-0 reflush controls finish without controller fault but retain
zero HT starts (346 and 590 captures). Both role-1 controls activate on a
real timeout, exhaust the retry and stop after one capture, preserving
RXCTL `0x804`, zero wait remaining and SHM reason 3. Their first RXE
counts are 980/924, status1 `0x4000` and status2 `0x4140`.

Three of the ten natural PHY-reset trials activate on a real exhausted
wait. Each executes exactly one reset with valid readbacks and inhibited
TX, preserving both original and subsequent PSM decisions:

| Trial | First fault RXE count / status1 / status2 | RXCTL before/after PHY reset | Result |
| --- | --- | --- | --- |
| `reset512-role1-r0` | 980 / `0x4000` / `0x4140` | `0x804` / `0x804` | Retry exhausts; explicit reason-3 fault after one capture |
| `reset512-role1-r1` | 1301 / `0x5800` / `0x0040` | `0x804` / `0x800` | Strict retry succeeds; reception continues |
| `reset512-role1-r2` | 980 / `0x4000` / `0x4140` | `0x804` / `0x804` | Retry exhausts; explicit reason-3 fault after one capture |

The middle trial is a **real, instrumented recovered busy timeout**, not
an injected decision or an automatic restart. Its first-fault record has
zero wait remaining, outcome 2, RX epoch 330 and bad-FCS count 4.
C3 executes the reset in 17 TSF units and directly observes RXCTL change
from `0x804` to `0x800`, before reissuing the original flush. The final
main record is committed sequence 962, outcome 1, RXCTL `0x800`, RX epoch
355 and unchanged bad-FCS count 4.

The receive timeline independently brackets the activation:

| Sample | Calibrated elapsed us | Captured | First-fault commit | PHY reset state |
| --- | ---: | ---: | ---: | ---: |
| 24 | 2401564 | 323 | 0 | 0 |
| 25 | 2501582 | 327 | 2 | 2 |
| 26 | 2601589 | 330 | 2 | 2 |
| 30 | 2987803 | 350 | 2 | 2 |

These four samples have coherent cached publication, direct controller
state 9/error 0, and zero recorded PSM restarts. Frozen accounting retains
350 captures = 343 posted/consumed + 7 explicit drops, with no pending
frame. The increasing post-activation counts prove receive progress;
they do not prove HT reception: this trial has zero HT starts throughout.
This single episode tests the combined pause/reset/reflush sequence.
It does not isolate an individual reset write from PSM pause or the
additional delay; no time-matched parked/no-reset control is claimed.

The two failures have the earlier startup-like `0x4000/0x4140` status
pair and are already faulted at the first timeline sample. Their PHY
reset operations actually run and pass readback checks, but RXCTL remains
busy and the second wait really exhausts. The reset is therefore **not a
general or reliable remedy**. The differing status/count/timing groups
are evidence for separate failure states, not sufficient documentation
to assign undocumented RXE bit meanings or a unique root cause.
The next hardware investigation should distinguish this persistent
startup-like RXE state from the recoverable midstream state, retaining
pre/post reset evidence instead of increasing identical retry counts.

### Why 554-byte repeatability remains open

No new early body qualifies in this fixed matrix. Fourteen completed
frozen exports retain 28 valid HT headers, including seven A-MPDU-format
headers. Their exclusions are explicit:

- 21 are not A-MPDU format.
- Six end as terminal single bodies.
- One has an early single-body good hint.

The seven A-MPDU-format PSDUs are 302 or 558 bytes: they describe only one
body, not the earlier qualifying 1118-byte multi-body PSDU. All fourteen
completed vendor trials retain Block ACK activity, but that alone is not
proof of multiple bodies in any captured PPDU. Thus this campaign has no
observed qualifying multi-body opportunity to turn into a repeated
554-byte early proof. It neither retracts the preceding two independently
verified 554-byte bodies nor counts these single-body frames as repeats.
A reliably confirmed multi-body stimulus remains necessary.

Three otherwise completed trials see no HT starts despite continuing
ordinary capture and successful rearm decisions (the two mode-1 role-0
controls and the recovered mode-5 role-1 trial). HT-only loss remains
separate from persistent RXE timeout.

### Validation and preservation

All nine reference profiles and the ordinary controller pass builds and
native/ASan/UBSan tests. All seven pre-existing controller profiles and
the ordinary controller remain byte-identical to the preceding verified
images; only the newly gated reset profiles change C3/PSM firmware.
Final wrapper identities and footprints are in the reference README.

All 108 raw phase reports from 18 fixed RF trials independently pass
length and bitwise CRC checks. All 554 WND8 samples are retained with
zero capacity loss (309 coherent cached publications, 245 explicitly
incoherent). Four genuine faulted captures remain errors; the fourteen
successful frozen exports reconcile 5822 captured frames, including
163 explicit drops. No qualification failure is hidden or replaced by
a successful-looking empty export.

Final restoration initially fails its bounded readiness check before
any flash write because Google Chrome again exclusively owns badge A's
BOOTSEL USB interface. IORegistry identifies the owner; this is not a
radio failure. The user closes the browser windows, then restoration
resumes using the unchanged fresh original images.

Both entire original flashes are now restored. Independent readbacks
`build/clean-rx/phy-reset-investigation/restored-{A,B}-full.bin` each contain
exactly 16777216 bytes and byte-compare with the fresh campaign originals.
Their SHA256 values match the verified originals:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

Both complete load/verify operations and independent full readbacks
succeed. Serial-targeted final checks confirm both badges in BOOTSEL.
The reset remains default-disabled; one recovered episode does not
qualify general RXE recovery, HT reliability, repeated 554-byte capture,
normal HT advertisement or D2 completion.


