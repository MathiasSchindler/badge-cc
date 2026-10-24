# RXE drain and receive-buffer controls

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Handoff-state controls and zero-HT delivery exclusion (2026-10-19)

The retained change is a read-only three-point handoff trace in the unused
tail of the existing continuation snapshot, SHM `0x5a2..0x5c9`. A committed
prefix ID/epoch and phase mask bind three 12-word observations: before early
retention, before event-6 acknowledgement and after next-body copy arm.
Each records TSF, RX count, both status words, copy control/base/end,
next length and raw SPRs `0x1002`, `0x1024`, `0x1078`. Phase masks 1/3/7
are committed only after the corresponding record is written. This adds
46 PSM instructions / 368 controller text bytes, no BSS or USB pages.
Register observations are sequential and add diagnostic-path latency;
they are not atomic PHY snapshots or independent timing measurements.

The initial trace-only RF control, **`e853d92f`** / ABI 9, shows the old
RX count still present immediately after rearm, while copy end has changed,
the raw next-length register becomes `0x8000`, and the good-only status bit
clears. The stream-enable bit remains set. This motivated bounded controls,
not an assumption that those observations alone identify the corruption.

All trials below are in `build/clean-rx/vendor-handoff-state/`. Even-numbered
trials use vendor B / observer A; odd-numbered use vendor A / observer B.
Each submits 128 fixtures, with unchanged AP/channel/rate and no clean TX.
Every tested UF2/controller/collector and every failed capture is retained.

| Build / trace ABI | Trials | Control | Advances exercised | Complete pairs |
| --- | --- | --- | ---: | ---: |
| `e853d92f` / 9 | `handoff-r0..r3` | Read-only three-point trace | 3 | 0 |
| `277f775d` / 10 | `stream-r0..r3` | Disable streaming before rearm; require a fresh count below the normal 128-byte prefix threshold before returning to the existing guard | 1 | 0 |
| `9965cf0c` / 10 | `fifo-r0..r3` | Also repeat the original per-frame SPR `0x1024` initialization | 3 | 0 |
| `f0259fa6` / 11 | `early-r0..r3` | Remove the extra FIFO initialization; enable streaming at a fresh 64-byte count, beyond the complete MAC/CCMP prefix | 2 | 0 |

Additional committed records in those temporary controls verify that the
fresh-count barrier actually executes: count 10 with streaming disabled,
and count 66 with streaming enabled in both exercised 64-byte controls.
Their bounded missing-count/EOF/abort exits also passed native tests.
The controls sometimes preserve a plausible complete MAC header, but
**none produces a valid continuation**. They are therefore removed from
the retained source rather than installed as an unqualified receive fix.
No PHY/radio/calibration or TX-power setting was changed.

A separate Node bitwise CRC32 implementation verifies all nine complete
early bodies and rejects all nine captured continuations. Scanning only
the bounded captured continuation bytes finds no complete FCS-valid
170/298-byte intended-length window at any offset. Neither dropping two
bytes nor shifting the body is an evidenced repair. Hardware bad-FCS
frames remain rejected; no software flag or length normalization promotes
them to good delivery.

Two trials reproduce the earlier zero-HT condition with the full frozen
delivery readout:

| Trial | HT starts | Captured / posted / consumed | Bad-FCS discards | Transport drops / pending | Vendor received-BA delta |
| --- | ---: | --- | ---: | --- | ---: |
| `stream-r3` | 0 | 3 / 3 / 3 | 68 | 0 / 0 | 122 |
| `early-r3` | 0 | 5 / 5 / 5 | 81 | 0 / 0 | 113 |

Both have zero controller error, zero host resync/stall/retry state, no
advance attempted and an active scanner that reaches the explicit stop.
This excludes a blocked F2/USB credit queue as the explanation for these
two zero-HT runs: the missing good captures occur upstream of delivery.
It does not distinguish RF/PHY decode loss from earlier MAC classification
or receive-state problems, and it does not establish a calibration cause.

Across all 16 trials, independent reconstruction of the complete split
mailbox verifies `captured = posted + dropped`,
`posted - consumed = pending` and `consumed = host F2 count`, with pending
within the one-frame credit window and no host resync/stall/retry.
`early-r1` has one pending packet at stop; the other 15 have none. It is
not silently counted as delivered. A `picotool info` readiness probe
segfaulted before `stream-r3`; a subsequent probe succeeded and all four
bound reports were collected. That host readiness event is separate from
the measured RF loss. The runner now retains every numbered readiness
attempt rather than overwriting earlier failures.

Final host/ABI cleanup build **`42424296`** uses ABI 12 and the original
three-point trace only. Its clean controller byte-compares identical to
the radio-tested `e853d92f` controller. The final RP2350/collector identity
rebuild was not an additional RF trial. Default and save-only controller
builds/tests pass and remain byte-identical to the previous corresponding
profiles. Reference ASan/UBSan/native tests cover the original guards,
record bounds, actual pre/post-arm field selection, modeled post-arm count
changes and trace immutability across terminal/new-RX paths. Runtime remains
C/nolibc; HT advertisement and production policy activation stay disabled.

The qualification frontier is now two distinct problems: upstream zero-HT
loss, and corrupt within-PPDU continuation despite successful fresh-count
and stream-control transitions. Neither is solved by this campaign. D2
work packages 1/2 remain open, as do repeated variable-length delivery,
secure BA integration and physical response/recovery qualification.

Both complete original 16-MiB flashes were restored after this campaign.
Independent readbacks `vendor-handoff-state/restored-A-full.bin` and
`vendor-handoff-state/restored-B-full.bin` under `build/clean-rx/` each
contain 16777216 bytes and byte-compare with the fresh pre-test originals.
Their SHA-256 values are respectively
`8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
and `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.
Final serial-targeted checks confirm both badges remain in BOOTSEL.

## One-shot RXE event-drain/reflush experiment (2026-10-19)

The [genuine exhausted `RXCTL = 0x804` decision](RXE_RECOVERY.md#fixed-rf-comparison-and-a-genuine-exhausted-rearm)
motivates a separate,
default-disabled `CLEAN_RX_HT_REARM_RECOVER` control. It requires the strict
passive window observer; ordinary RX, TX and advertised capabilities are
unchanged. On the first busy decision, the PSM copies decision words 1..15
to SHM `0x7c1..0x7cf`, then commits `0x7c0 = 2` last. This constant nonzero
commit also bounds recovery to one attempt even if the main sequence wraps.
The record retains the exact busy operand, remaining wait, later RXCTL,
GoodFCS sample, RX epoch and counters. It is never overwritten by the retry.

The PSM clears the transient reason, acknowledges only ext3/5/6/11/13/14
and jumps to the original RXCTL flush at PC274. That reloads the strict
65,535-poll bound. A second busy decision takes the original fatal path;
there is no PSM restart, counter reset or unlimited retry. This tests whether
stale receive conditions plus a fresh flush can restore progress; it does
not establish which external condition, if any, causes a physical timeout.

`CLEAN_RX_HT_REARM_INJECT`, also default-disabled and dependent on recovery,
forces bit 2 only in the first **latched SHM decision operand**, not hardware
RXCTL. It makes branch execution measurable without waiting for an
intermittent timeout. A forced first fault may therefore retain `0x804`
while later hardware RXCTL is `0x800` and the wait counter is not exhausted.
These controls must never be counted as natural busy-timeout recoveries.

ABI18/WND7 adds a separate 32-byte immutable first-fault block to the
96-byte window header and a first-fault commit to each 160-byte sample.
There are at most 35 samples. Recovery modes are 0 (strict/no recovery),
1 (natural recovery enabled), and 3 (injected); mode 2 is invalid.
The C3 independently checks the main and first-fault publications.
A committed first-fault block does not make error-cleanup mailbox data
coherent, nor make separately cached fields simultaneous hardware state.

Native tests execute the emitted instruction words across the existing
2,048 busy/delay/event cases and all 64 combinations of the six drained
events, with both successful and persistently busy retries. They verify
first-fault immutability, explicit second failure, selective event clearing,
unchanged unrelated events and later RXCTL changes after decision latch.
Protocol tests cover recovery modes, disabled payloads and publication
boundaries. Display timing/cancellation and existing graphics-client tests
also pass with ASan/UBSan.

The RP2350 streams the existing project-owned glyphs directly to the TFT,
without a framebuffer. Role, operation and experimental mode appear before
radio start; stopped/error state, capture count and first-fault RXCTL appear
after observer stop. There are no active-observer display updates or added
C3 display messages. Physical screen appearance is not independently
verified; `RADIO STOPPED` is not a qualification indicator.

The first four attempted injected controls (`injected{256,512}-role{0,1}-r0`)
failed before USB discovery, with no RF reports. The newly added main-loop
error conversion had treated normal pre-configuration `ref_service() = 0`
as fatal. Restoring the existing pre-configuration polling behavior fixes
this; display-initialization/clear cancellation still aborts explicitly.
These failed attempts remain archived, separate from subsequent RF trials.

### Injected execution controls

After the USB fix, four fixed mode-3 trials with RF wrapper `6d4ba8e4`
exercise recovery at both sizes in both roles. All retain a committed first
fault (`0x804`, later hardware `0x800`, nonzero wait remaining), then a
successful main decision (`0x800`, outcome 1). First-fault commit 2 is
already visible in the first timeline sample and remains unchanged.
Capture counts continue without PSM restart or controller error:

| Trial | First to last sampled captures | Final HT starts |
| --- | --- | --- |
| `injected-fixed256-role0-r0` | 2 to 311 | 2 |
| `injected-fixed256-role1-r0` | 1 to 209 | 4 |
| `injected-fixed512-role0-r0` | 9 to 557 | 0 |
| `injected-fixed512-role1-r0` | 7 to 563 | 5 |

All four reconcile frozen capture/post/drop/consume accounting, with no
pending frames. None qualifies an early body. The zero-HT role-0 512-byte
trial still captures 557 frames and ends with a successful rearm decision;
HT-only misses therefore remain a separate failure category, not evidence
that every miss is a persistent RXE flush timeout. Three final C3 cached
snapshots are incoherent; this does not invalidate the separately committed
first-fault records or frozen accounting, and is not relabelled as coherence.
These trials establish real PSM branch execution and subsequent receive
progress, **not natural-timeout recovery qualification**.

### Strict controls and USB readiness losses

Four fixed 512-byte controls use the same screen-enabled wrapper,
`9c377cb3`, with recovery disabled. Both role-0 trials finish without
controller fault and see HT, but retain no qualifying early body.
Both role-1 trials genuinely exhaust the wait before the first window
sample: they stop after 1 and 3 captures respectively. Their committed
decisions have sequences 4 and 8, exact/later RXCTL `0x804`, zero wait
remaining, outcome 2, GoodFCS sample 0 and no restart. RXE counts are
984/982, status1 `0x4000`, status2 `0x4140`; both report controller
error `0x88`, SHM `0x602 = 3`. These are real timeouts, not the corrected
conditional-drain fall-through. Neither faulted frozen capture is counted
as a successful delivery export.

All six immediately following natural-mode invocations fail their bounded
BOOTSEL readiness checks before loading or collecting RF evidence.
Their logs remain under `natural{256,512}-role*-r*`; there are no matching
RF JSON reports to count. A further 30-second serial-targeted readiness
check also fails. IORegistry identifies the cause: Google Chrome exclusively
owns badge A's BOOTSEL USB connection. This is a host-access failure,
not radio evidence or slow PSM recovery. The browser is not terminated.
After the user releases the badges, both serial-targeted checks succeed
and the unchanged six-trial matrix runs with fresh `natural-ready*` prefixes,
not by overwriting failures or retrying an RF trial until it qualifies.

A concurrent attempt to restore the accessible badge B aborts at 87% with
a picotool connection error during this intervention. It is not counted
as a completed restoration; a complete rewrite and independent readback
are required after the remaining trials. Fresh full original files remain
unchanged.

### Natural timeout result and first both-role 554-byte proofs

The six accessible trials use natural mode 1 and RF wrapper `edd21932`.
Five complete radio capture without controller error or PSM restart.
One activates the recovery on a **real** exhausted busy condition:
`natural-ready256-role1-r0`. Its immutable first fault retains RXCTL
`0x804`, wait remaining 0, RXE count 982, status1 `0x4000`, status2
`0x4140`, GoodFCS 0, bad-FCS count 1 and RX epoch 2.

The retry **does not recover** this hardware state. The later main decision
is sequence 6, RXCTL still `0x804`, remaining 0 and outcome 2; TSF-low
advances from 19961 to 25696 while RXE count, RX epoch and bad-FCS count
remain unchanged. First-fault commit 2 is already present at the first
timeline sample. All 30 samples retain one captured frame, controller
error `0x88`, reason 3 and zero PSM restarts. The first fault and subsequent
fatal decision survive separately. This is one physical activation and
one failed recovery, not a successful hardware fix. Event acknowledgement
plus another identical flush is insufficient for this observed state;
more waiting and a software-only execution control cannot establish a
remedy. A stronger bounded RXE/PHY reset or revised abort sequence needs
independent hardware grounding and separate qualification before use.
The same 982-byte RXE count occurs in a 256-byte requested-size trial,
with zero HT starts and an already-faulted first sample. Together with
the strict controls, this argues against attributing these timeouts
specifically to the requested 512-byte stimulus or an observed HT aggregate.

Two of the four natural 512-byte trials now qualify complete **554-byte**
nonterminal early MPDUs, one in each role:

| Trial | Prefix / RX epoch | MPDU / HT PSDU bytes |
| --- | --- | --- |
| `natural-ready512-role0-r0` | 2 / 121 | 554 / 1118 |
| `natural-ready512-role1-r1` | 1 / 24 | 554 / 1118 |

An independent bitwise CRC32 verifies each full body. Nonce-derived
transmitter address, data-frame type, epoch/prefix linkage, matching
HT-SIG bytes, HT-SIG CRC and nonterminal geometry are checked separately:
the first delimiter plus padded body is 560 bytes, strictly below the
1118-byte PSDU. Both collector runs also retain vendor Block ACK activity
and accounted prefix exits. No recovery activates in either proof trial,
so the improved capture result is **not attributable to recovery**.
These are first both-role proofs at this size, not repeated reliability,
second-body delivery or production aggregate qualification.

Across the fixed RF matrix there are 14 actual trials (4 injected,
4 strict, 6 natural), excluding the four pre-enumeration failures and
six host-readiness failures. All 84 raw reports independently pass
length and bitwise CRC checks. All 430 WND7 rows are retained with zero
capacity loss; 210 cached publications are coherent and 220 explicitly
incoherent. Eleven successful frozen captures reconcile captured,
posted, dropped, consumed and pending counts. Three real faulted captures
remain explicit errors rather than successful delivery summaries.
The default-disabled recovery remains experimental; D2, recovery
reliability, repeated 554-byte capture and HT-only miss isolation remain
open. Normal HT advertisement stays disabled.

Fresh full backups for this campaign are
`build/clean-rx/rearm-recovery-investigation/original-{A,B}.bin`,
independently matched to the preceding verified originals before testing.
After all trials, both complete flashes are restored and independently
reread as `restored-{A,B}-full.bin` in the same directory. Each readback
is exactly 16777216 bytes, byte-compares with its fresh original and
matches the verified SHA256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

The final complete B rewrite supersedes the interrupted 87% attempt;
both final load/verify operations and independent readbacks succeed.
Serial-targeted final checks confirm both badges in BOOTSEL.

## Alternate receive-buffer control (2026-10-19)

The next bounded control tests whether immediately reusing the first
body's SHM destination alone explains corrupt continuation. Build
**`b48c71dc`**, ABI 12, retains the original early-copy and handoff guards
but arms the second body at SHM `0x900` instead of `0x800`. Both bounded
300-byte body regions are disjoint and below `0x1000`. Only after terminal
EOF does an unrolled 150-word copy move the actual second-body bytes to
the conventional `0x804` MAC-body region for existing snapshot/F2 handling.
The original aggregate HT-SIG is retained. This moves bytes without
changing length, status, FCS or admission; it never enables clean TX.

Before RF, ASan/UBSan/native tests exercise all RX counts 50..306,
non-EOF, original-base, missing-advance, abort, short and oversize cases,
source/destination bounds, prefix/register preservation and no TX.
The valid modeled copyback takes at most 180 instructions, not a physical
timing measurement. The temporary native interpreter also stops at the
existing PC-266 rearm boundary. These are control tests, not RF proof.

Eight trials, `build/clean-rx/vendor-alt-buffer/alt-r0..r7`, alternate
vendor B / observer A on even numbers and the opposite roles on odd
numbers. All use the unchanged 128-fixture vendor/AP stimulus.

| Trial | Early / requested next body | Measured post-arm base | Terminal RX count | Result |
| --- | --- | --- | ---: | --- |
| `alt-r1` | 170 / 298 bytes | `0x900` | 302 | Valid early body; bad continuation |
| `alt-r4` | 170 / 298 bytes | `0x900` | 306 | Valid early body; bad continuation |

The other six trials lack an eligible early body; `alt-r3` and `alt-r5`
have zero HT starts and never advance. No trial qualifies a complete
pair. An independent bitwise CRC32 implementation verifies both early
bodies, rejects both continuations and finds no FCS-valid contiguous
170/298-byte window anywhere in their bounded 300-byte captures.
Both the post-arm trace and frozen terminal headers report `0x900`.
This shows that same-destination reuse alone is not sufficient to explain
these two failures. It does not rule out other copy/FIFO/context bugs,
early-retention latency or upstream RF/PHY loss, and it does not qualify
both size orders.

All eight full frozen delivery accounts reconcile. They contain 2..10
explicit transport drops per trial, not silently counted as delivered.
`alt-r4` has one pending frame at stop; the other seven have none.
All have zero host resync/stall/retry state. These observations are not a
claim of lossless transport. Collector exit status 1 in all eight trials
correctly records incomplete RF qualification; all raw phase reports
and errors are retained.

The unsuccessful alternate-buffer behavior and its temporary native
hooks/tests were removed. The exact tested controller/UF2s/collector,
build ID, and tested observer-header/native-test sources are retained in
the campaign directory. The retained controller byte-compares identical
to the preceding trace-only `42424296` image; default and save-only
profiles also remain unchanged.

The retained host improvement enforces the full frozen capture/post/drop/
consume accounting instead of checking only the pending window and
consumed totals. It validates both mailbox magic values, saturated posted
and dropped halves, capture/post ordering, the one-frame credit window,
both host consumed totals and the summary bound. Saturation is checked
against the full counters; a pending frame or recorded drop is not
relabelled as delivery. Native tests cover zero, one-pending, saturation
boundaries, maximum nonwrapped counters and malformed reports; the same
guard accepts all 30 preserved captures from this and the preceding two
campaigns. Final build **`8bbed93e`**, ABI 12, passes the reference
native/sanitizer tests and preserves the 244504/75140/60528 text-byte
footprints. Its controller is byte-identical to `42424296`; the RP2350
identity/host-guard rebuild is not an additional RF trial.
D2 packages 1/2 remain open.

Both complete original flashes were restored after the controls.
Independent `vendor-alt-buffer/restored-A-full.bin` and
`vendor-alt-buffer/restored-B-full.bin` readbacks under `build/clean-rx/`
each contain 16777216 bytes and byte-compare with the fresh full backups.
The SHA-256 values match the preceding original-image hashes
(`8cda7a00...23628` for A and `bc3f972d...21e2` for B).
Final serial-targeted checks confirm both badges remain in BOOTSEL.

## Early-capture causes and hardware controls (2026-10-19)

This investigation separates an **observed eligible early-body opportunity**
from a trial that merely contains A-MPDU-format headers or vendor BA activity.
The latter can contain only singleton bodies. It also keeps early retention
separate from the still-unqualified next-body handoff.

### Retrospective opportunity audit

The 30 full captures in `vendor-receive-state/`, `vendor-handoff-state/`
and `vendor-alt-buffer/` contain 182 retained prefix records:

- 23 bounded multi-body good-hint candidates occur across 14 trials.
  All 14 trials retain their first eligible candidate.
- Nine subsequent candidates are intentionally ignored after immutable
  first-multi retention, not failed copies.
- 127 records are terminal-only singleton A-MPDU-format receptions without
  a nonterminal good hint. Other records include singleton good hints and
  bodies outside the diagnostic storage bound.

Thus the earlier unsuccessful-trial count was not a retention-loss rate.
This is an audit of the bounded saved log, not all transmissions: its
16-record limit and overflow can hide later opportunities.

The collector now reports `early_capture_reason` for each record, including
unsupported MCS, missing nonterminal hint, singleton geometry, storage cap,
inconsistent body geometry, first-multi retention and deliberately ignored
later candidates. Singleton classification checks both lower and upper
delimiter/padding bounds; a body larger than its PSDU is not a singleton
success. An otherwise eligible unretained record remains an explicit
investigation result, not a silently successful trial.

### Size-isolated, receive-only campaign

`reference-early` enables HT RX, the passive observer and early save,
**without next-body advance**. Its controller byte-compares identical to
the previously tested save-only image. No M3, PSM, PHY/radio initialization
or clean TX behavior was changed for this investigation.

The optional runner selector submits 128 equal-size Ethernet fixtures:
128/256/512 bytes, corresponding to measured 170/298/554-byte protected
MPDUs. The profile is nonce/session-bound. Focused qualification requires
the exact requested early length, a genuine nonterminal multi candidate,
fresh transmitter/prefix/epoch correlation, independently valid complete
FCS, vendor BA activity and accounted exits. It does not demand two
unrelated ordinary F2 sizes or advance to a second body.

All evidence is under `build/clean-rx/early-capture-causes/`:

| RF build | Trial prefixes | Purpose | Qualifying trials |
| --- | --- | --- | ---: |
| `db3cd284`, ABI 13 | `size{128,256,512}-role{0,1}-r{0,1}` | Constant-size baseline; stopped digital hardware endpoints | 7/12 |
| `1a2ea0e4`, ABI 13 | `prepared512-role{0,1}-r{0,1,2}` | Eight prepared fixtures; measured submission intervals | 1/6 |
| `1a2ea0e4`, ABI 13 | `prepared256-role1-r{0,1,2}` | Repeat the role with an earlier zero-HT boot | 1/3 |
| `17612e0b`, ABI 14 | `stopped256-role1-r{0..5}`, `stopped256-role0-r{0,1}` | Actual stopped PSM state, not normalized historical mailbox state | 8/8 |

Role 0 is vendor B / observer A; role 1 is vendor A / observer B.
A is `08DA26F86E57F5EA`, B is `9BBF88DD17D66E7D`.
All runs used the approved AP's channel 6, requested MCS0 and clean TX off.
Exact RF collectors, images and build identities are archived separately.
Final native-validated builds `86101c9a` (early-only) and `a24b1a8c`
(default advance) additionally exclude unused preparation storage from
the advance build; they are not additional RF trials. The default controller
byte-compares identical to the prior retained `8bbed93e` controller.

Across all 29 trials:

| Ethernet fixture / early MPDU | Role 0 | Role 1 |
| --- | ---: | ---: |
| 128 / 170 bytes | 2/2 | 2/2 |
| 256 / 298 bytes | 4/4 | 8/11 |
| 512 / 554 bytes | 1/5 | 0/5 |

All 17 qualifying early bodies independently pass a separate bitwise CRC32
implementation, fresh nonce-derived TA, exact size, epoch and prefix checks.
All 116 raw phase files have the expected 5952-byte length and valid report
CRC. The 257 retained headers contain 56 logged multi candidates:
17 retained first candidates and 39 intentionally ignored later candidates.
The remainder comprises 152 terminal singleton records, 20 singleton good
hints, 28 non-A-MPDU records and one invalid HT-SIG.
No logged eligible first-multi retention failure was found.
All 59 old/new full captures also pass the native/sanitized complete
capture/post/drop/consume guard. Explicit drops and pending frames are not
counted as successful delivery.

The 12 nonqualifying trials divide into nine without a logged eligible
multi-body opportunity and three with **zero HT starts**. All three
zero-HT cases are role 1: `size256-role1-r0`,
`prepared512-role1-r1`, `prepared256-role1-r2`.
They deliver only 4/4/2 ordinary frames, with no transport drops or pending
frame, and no host resync/stall/retry state. Their bad-FCS counters are
64/98/70, and the controller records over two million receive polls.
This distinguishes them from an early-copy rejection and demonstrates
some bad-frame processing; it does not prove continuous receive readiness.
The subsequent eight stopped-state controls all qualify, so they provide
healthy comparison data, **not** a reproduced zero-HT state or a fix.

### Submission pacing is measured, not aggregation proof

At 512 bytes, the baseline's shortest host write is 836 us and a complete
128-frame burst takes about 137.8 ms. Preparing just the first eight frames
and prefix digests before submission removes repeated payload/digest work
from that part of the burst, using 8224 bytes of focused vendor BSS.
Measured prepared first-eight intervals are 6771..6783 us, versus
8376..8385 us for the following eight generated frames; complete bursts
take 136090..136966 us. Minimum write time remains 836 us.

A 554-byte MCS0 HT20 body has nominal PHY airtime around 724 us before
contention/response overhead. Submission can therefore limit queue buildup,
but the vendor/AP may also batch, retry or defer traffic. One prepared
role-0 trial retains a complete valid 554-byte early body, proving the
save-only path works beyond the advance profile's 480-byte cap.
This is neither repeatable in both roles nor proof that preparation caused
that capture; the encrypted body is not attributed to one of the first
eight submitted fixtures. No unsupported vendor aggregation-control write
or speculative bus-clock increase was used.

### Hardware and calibration boundaries

The [board record](../../../../docs/cyw43/hardware.md) identifies the RM2 module,
always-on 3.3-V radio supply and shared WLON/BTON on GPIO23. GPIO24 carries
data/IRQ, GPIO25 CS and GPIO29 the host bus clock. GPIO41 switches a
peripheral/display rail, not the radio supply. The configured 37.4-MHz
RF reference is not the host bus clock or an oscillator measurement.
The campaign's CYW43439 datasheet snapshot
(`Infineon_CYW43439_DataSheet_v03_00_EN-3074791.pdf`, available through
[manufacturer documentation](https://www.infineon.com/part/CYW43439))
is not a D11/LCN20 register-programming specification.

ABI 13 snapshots six raw words before receive and after explicit controller
stop, only while selector ownership is safe: ChipCommon ID, ChipCommon
clock control/status, D11 clock control/status and PMU status/resources/
pending. Every before/after vector in all 29 trials is identical:
`[356886959,720960,118161986,62,2130329079,0]`.
Chip word `0x1545a9af` identifies CYW43439, revision 5, package code 4;
the separately established D11 revision is 39. Neither is the RP2350's
A4 revision.

The existing [calibration record](../../../../docs/cyw43/calibration.md) and
initialization code show readiness/completion checks for PLL, RC, VCO and
DC. These are not an analog-quality test. The saved RC values vary by
badge and overlap successful and failed reception. In older captures,
the exact tuple `[4871,4292,9,5,255,4095]` occurs in both zero-HT `alt-r5`
and HT-positive `fifo-r3` with ten HT starts. Consequently these reported
values cannot alone identify a bad calibration. The all-ones DC values
are completion words checked by the initialization predicate, not measured
residual DC offsets.

ABI 14 additionally preserves raw D11 PSMDEBUG and actual SHM
`0x600..0x60b` after stop. This avoids mistaking the controller's normalized
historical mailbox state for the actual final state. PSMDEBUG remains
unmasked; one stopped sample is not a retired-PC or continuous stuck-state
trace. These reads do not touch PHY/radio selectors or change the receive
timing path.

| Possible cause | What this investigation establishes | What remains |
| --- | --- | --- |
| Incorrect opportunity denominator | Singleton receptions and immutable later-candidate policy explain many apparent missing copies | Bounded logs do not establish all on-air opportunities |
| Wrong size/admission profile | Save-only admits 554 bytes; advance remains capped at 480 | Repeat larger early bodies in both roles |
| Failed first-body copy | All 17 observed first eligible candidates are fresh and FCS-valid | No general lossless/rate threshold is qualified |
| F2/USB delivery loss | Full accounts reconcile; explicit drops remain visible; zero-HT trials have no final backlog/drop | Endpoint accounting does not exclude transient receive inhibition/backpressure |
| Cold PHY/calibration or receive state | Zero-HT is upstream of recorded HT starts, not a rejected saved body; reported calibration tuples do not separate outcomes | Reproduce a zero-HT boot with the actual stopped-state probe; compare controlled receiver initialization |
| Badge-specific RF/power/clock problem | Both badges work with vendor firmware and both clean roles have valid early captures; digital endpoints agree | Rail ripple, RF-reference jitter, antenna/desense and transient gating are not physically measured; roles also couple transmitter and placement |
| Next-body FIFO/copy context | Early-only success does not exercise advance; earlier alternate-buffer failures remain separate evidence | D2 package 2 remains open |

Do not alter RF tuning or declare a defective badge from these observations.
The next discriminating control is a zero-HT reproduction with actual
stopped PSM state, followed by controlled receiver-initialization and
physical-position comparisons. Independent air capture is needed to
distinguish absent multi-body stimulus from missed RF opportunities;
physical rail/reference measurements are needed for analog claims.
Package 1 remains open for reliability/zero-HT diagnosis, and HT station
advertisement remains disabled.

Both fresh complete original images were preserved before the campaign
and compared with the previously verified originals. After all 29 trials,
both full flashes were restored and independently reread as
`early-capture-causes/restored-A-full.bin` and
`early-capture-causes/restored-B-full.bin` under `build/clean-rx/`.
Each 16777216-byte readback byte-compares with its fresh backup and has
the expected SHA-256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

Final serial-targeted device checks confirm both badges are in BOOTSEL.
Both final profiles pass controller tests and native ASan/UBSan tests; all final
ELFs have no undefined symbols, runner syntax and whitespace checks pass,
and the runner's invalid-size rejection and collector's advance-build
focused-profile rejection precede their respective USB operations.

