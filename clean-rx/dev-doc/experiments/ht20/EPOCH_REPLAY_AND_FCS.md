# Epoch ownership, replay and FCS controls

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Focused copy microscope and current-epoch stream ownership (2026-10-20)

The preceding 32-byte observation was a **canonical-prefix limit with
streaming active**, not a proven maximum transfer size. A small on-chip
PSM microscope now distinguishes live copy progress from terminal SHM
corruption. Its dedicated native executable also decodes saved reports;
the unchanged reference source and USB harness supply only stimulus and
transport. No new TX operation, library or radio reset is introduced.

`copy_scope.h` contains the probe program; `copy_scope_test.c` tests the
actual emitted PSM instructions and can inspect a raw capture directly.
Default-off `CLEAN_RX_HT_COPY_SCOPE` requires the existing mode-6
destination-shift replay, with the original endpoint unchanged:

| Scope mode | During the bounded observation | Later streaming in the same receive epoch |
|---|---|---|
| 1 / on | keep stream field 2 | original behavior |
| 2 / off | write stream field 0 before replay | original behavior can re-enable it |
| 3 / hold | write stream field 0 before replay | keep it 0 for the committed matching epoch |
| 4 / resume | write stream field 0 before replay | matched guard explicitly writes it back to 2 |

Modes 1/2 have identical instruction footprints and differ in one byte
of one stream-field write. Modes 3/4 share the same guarded program and
differ in one byte of its later stream-field write. The latter pair
therefore tests **re-enablement**, not a different replay or endpoint.

### Probe layout and boundedness

The probe takes three 26-word snapshots: before the existing +64 base
write/replay, just after replay, and after a bounded 32-TSF-us wait.
Each stores TSF low, count, status1/2, copy control, base, endpoint, raw
next/FIFO/context and two 16-byte SHM windows at MPDU offsets 128 and
160. EOF or abort ends the observation early; a 1,023-poll budget also
bounds a frozen TSF. Snapshot 2 temporarily supplies the elapsed/budget
slots, then overwrites them with final readbacks.

The 80-word scope occupies SHM `0x4b0..0x4ff`, entirely inside unused
first-body snapshot storage. Only scope profiles reduce the early-body
cap from 480 to 320 bytes; static assertions prevent overlap with the
retained first body and continuation header at `0x500`. The 170/298-byte
fixture remains unchanged. Header word 1 carries the epoch. Header word
0 commits only after all three snapshots, as `0x10 + reason`: 17 timer,
18 EOF, 19 abort, 20 exhausted. Copy arm clears both header words.

The ordinary copy-probe layout/version 6 stays unchanged. With the scope
enabled its post-action readbacks occur **after** the observation window;
they must not be interpreted as immediate-command timing. Separate
`copy-scope`/`copy-scope-record` JSON and validation establish the live
stream-on/off state rather than forcing a stream-off experiment through
the older stream-on validator. Commit 20 is explicitly nonqualifying.
All four profiles remain diagnostic-only regardless of their byte result.

The epoch guard requires advance 1, a committed scope header 17..20 and
the matching current epoch. Stale, pending or invalid headers and other
advances retain normal streaming. R3 is borrowed only with the existing
save/restore slot; every exit restores it, preserving all four native
GPRs. The epoch comparison uses a register and one SHM input, not an
unsupported two-SHM-input instruction.

### Physical results

Evidence: `build/clean-rx/copy-scope/`. **Twenty-four RF trials** cover all
four profiles in both roles/orders, with reverse-order long-body repeats.
On/off C3 images are 61,676 bytes with one differing byte at zero-based
60,959, in the field-write instruction at 60,956. Hold/resume images
are 61,756 bytes with one differing byte at 61,647, in the instruction
at 61,644. Independent decoding checks opcode `0x310`, the intended
0/2 immediate and the same copy-control register. Every source image
matches the independently monitored source fixture.

| Scope / 298-byte continuation | Trials | Canonical SHM bytes | Expected 298-byte FCS | Native outcomes | Normal exact SHM/F2 pair |
|---|---:|---:|---|---|---:|
| on | 4 | 160 | invalid | 3 aborts, 1 bad FCS | 0 |
| off, original re-enable | 4 | 256 | invalid | 2 aborts, 2 bad FCS | 0 |
| off, epoch-held quiet | 4 | **298, complete** | **valid** | 4 bad FCS, count 176 | 0 |
| off, matched re-enable | 4 | 256 | invalid | 2 aborts, 2 bad FCS | 0 |

All sixteen long-body trials first verify the same count-134/32-us stall
and fresh activation at count 130. Before replay, neither sampled window
matches the continuation. Immediately afterward, bytes 128..143 match
but 160..175 do not. At exactly 32 TSF microseconds after the second
snapshot, bytes 160..175 become correct in **every stream-off profile**,
but never in the stream-on control. This is live evidence of additional
copy progress, not a final-buffer inference.

Allowing the native path to turn streaming back on produces a new
256-byte canonical boundary. The matched current-epoch hold instead
retains **every byte of the 298-byte continuation, including the exact
transmitted FCS**, in both roles and reverse-order repeats. Independent
fixture generation checks the complete body/sequence/nonce, not just
the CRC or selected windows.

This rules out a fixed 32-byte copy-engine capacity under this geometry:
the truncation follows streaming ownership and re-enablement. It does
**not** identify an internal bank-pointer implementation or establish a
general segmented receive algorithm.

### Why this is still not valid delivery

All four complete held SHM bodies retain native **outcome 2**, count
**176**, and base `0x840`; none reaches the advanced normal F2 sample.
The 176 count corresponds numerically to the 170-byte remainder plus
six, not the complete 304-byte logical receive length. This is evidence
of restarted/local accounting, not yet a universally qualified rule.
The normal snapshot's FCS check uses its reported 170-byte MPDU length
and therefore remains false. The focused decoder separately checks the
known expected 298-byte body and reports its valid FCS, explicitly
alongside the native rejection and `delivery_qualified:false`.

Do not replace the native outcome, normalize its count blindly, waive
hardware GoodFCS, or advertise HT. The remaining investigation must
separate copy/count restart from hardware FCS/terminal ownership, then
publish a correctly bounded FIFO/header without duplicate or partial
delivery. Correct retained bytes alone do not satisfy those gates.

The first held A capture uses aggregate source index 5, not initial
index 2; its repeat retains index 2. The resumed B repeat also retains
index 5. Original initial-case misses/zero terminal fields are preserved.
Every other retained case is index 2. Eight short-body controls progress,
skip the microscope and deliver exact SHM/F2 pairs; six receive every
required interleaved control. All sixteen long-body native failures and
other control losses remain nonqualifying.

### Focused verification and restoration

`make -C clean-rx/src ... copy-scope-test` builds a small dedicated
executable. It executes only the emitted probe instructions for normal,
EOF, abort, frozen-timer and TSF-wrap cases, checks bounded writes and
waits, snapshot offsets, commit values and GPR preservation, and rejects
12 readback mutations per case. The hold/resume variants additionally
test seven fresh/stale/pending/invalid epoch/advance guard cases.
This is software-control verification, **not RF emulation**.

Running that executable with a capture filename validates the real
probe ownership/readbacks and separately reports expected-length FCS
versus native count/outcome. All 24 archived captures pass this final
ASan/UBSan recheck. Existing native receive, host fixture/mutation and
freestanding C3/RP builds also pass. Scope 5 and missing replay parents
reject compilation. Scope-disabled production and preceding mode-6 C3
firmware remain byte-identical. Final builds reproduce all four RF C3
images; RP binaries differ only in their archive-dependent build ID.

The independent bitwise-CRC/fixture audit verifies **96 raw reports,
576 source records, 16 three-snapshot probe runs, four complete
298-byte SHM bodies, 29 F2 samples and conserved accounting for
951 raw captures**. Raw captures are not a distinct-OTA denominator.
It also verifies both one-byte image pairs and all final C rechecks.

Fresh complete originals were saved before experimental flashing. Both full 16-MiB flashes
are restored and independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Both badges are confirmed
BOOTSEL. No credentials were read or provisioned. D2 and HT station
qualification remain open.

## Repeated early-MPDU FIFO delivery and complete packet archives (2026-10-20)

This implements a missing delivery foundation, **not D2 completion or
early-capture reliability qualification**. The new default-disabled
`CLEAN_RX_HT_EARLY_FIFO` profile delivers retained first nonterminal MPDUs
through the existing SRAM FIFO and F2 transport. It does not advance to
the next aggregate body, admit a station, enable TX or advertise HT.
Evidence is retained under `build/clean-rx/early-fifo/`.

### Committed retention, publication and acknowledgement

The new profile requires early save and the passive observer FIFO, and
rejects combination with next-body advance. Its retained header is
14 words, with a maximum 996-byte body; a compile-time bound keeps both
within the existing 512-word SHM export. The original twelve header
words retain their meanings. Word 12 preserves native PHY status, and
word 13 is a consumer acknowledgement.

PSM commits the owner last, after header/body copying. C3 verifies the
owner and epoch before and after reading into an unpublished FIFO slot.
Admission requires a nonterminal multi-body candidate, native GoodFCS
without EOF/abort, matching prefix epoch and HT-SIG, canonical aggregate
MCS0/HT20 geometry, the configured transmitter, a supported data frame
and complete software FCS. It does not waive native rejection or infer
successful reception from a length alone.

C3 acknowledges the retained owner only after publishing the complete
SRAM slot with barriers. PSM may then recycle it for a different receive
epoch. Missing/stale acknowledgements, the same epoch, absent pending
ownership and exhausted FIFO credit preserve retained bytes. Existing
singleton-to-first-multi replacement remains available. The emitted PSM
instructions are tested for all five recycle/ownership cases.

F2 correlation bit `0x4000` identifies this early path separately from
the existing `0x8000` next-body diagnostic. The host keeps exact source
receipt bits and duplicate counts. These counters occupy trace
5824..5835, beyond the complete early and controller-fault exports.
Initial placements at hardware snapshot word 84 and FIFO-stress word 16
were rejected by testing and corrected; their failed RF evidence remains
archived, not relabelled as passing.

### Pre-terminal PHY status was not a valid F2 classification source

The archive campaign exposed a second real integration defect:
`export-role-B-2` retained eight exact 554-byte bodies, but two F2 packets
were labelled CCK and rejected by the strict host parser. Native early
PHY format bits were observed as 0, 1 and 2 despite canonical HT-SIG and
correct complete HT body bytes. The terminal PHY-status header cannot
be assumed valid at this earlier event.

For this separately tagged and already HT-SIG-validated path only,
F2 now uses unsupported/raw HT metadata (`0x40 | channel`, `0x46` on
channel 6). Native PHY status remains unchanged in diagnostic storage.
The host rejects legacy/CCK metadata for the new packet type. This does
not set negotiated HT station metadata or alter normal packet
classification. Tests cover all four raw PHY format values.

### Full packet export, not only a final sample

The RP retains up to eight complete early F2 packets in a separate static
buffer. After receive stop, observer phase 5 exports that archive, leaving
the saved phase-2 capture and window evidence intact. There is no added
RP/controller communication during the radio copy.

At trace 128, the 16-byte `ERF1` header records stored/total counts and
overflow; eight 584-byte slots contain packet size, logical F2 ordinal,
parsed source index and complete packet bytes. A 554-byte body uses a
572-byte SDPCM/BDC packet. Strict validation checks envelopes, complement,
sequence/ordinal, tags/owner, metadata, nonce, full fixture/FCS, unique
source identity, receipt mask and zero unused slots. A valid partial
archive is not an all-bodies-received success.

The old metadata failure is preserved explicitly: source indices 11/23
have metadata `0x06`, parsed-index sentinel 24, and exact body bytes.
Only six packets were accepted; its original export validation failed.
The independent audit verifies those negatives as well as the complete
bytes, rather than retroactively accepting the malformed metadata.

### Both-role RF results, including final failures

The unchanged version-6 clean source sends 24 transmissions per trial:
eight cycles of legacy, single HT and a two-554-byte-body aggregate, at
MCS0/channel 6 with calibrated 40-ms gaps. Source descriptors, HT-SIG,
fresh nonce, complete received bytes and independent FCS establish the
observed multi-body stimulus. Source completion alone is not an
independent per-attempt RF denominator; there is no third receiver.

Earlier repeated profiles delivered 48/48 first bodies, with one ordinary
HT control miss. Earlier injected-reset tests delivered 16/16 with all
controls. Five old archive trials delivered all eight bodies and all
controls; the sixth exposed the metadata defect above. These are not
substitutes for the final corrected matrix:

| Final profile / receiver | First bodies delivered | Required ordinary controls | Strict trial result |
|---|---:|---|---|
| normal A0 / B0 | 8/8 each | all | pass / pass |
| normal B1 | 7/8 | all | fail |
| normal A1 | 7/8 | missing index 9 | fail |
| normal A2 | 8/8 | missing index 13 | fail |
| normal B2 | 8/8 | all | pass |
| injected reset A | 7/8 | missing indices 4/12 | fail |
| injected reset B | 7/8 | all | fail |
| pause-until-stop A / B | 2/8 each | later traffic intentionally lost | stress only |

Final normal delivery is **46/48**, with 94/96 ordinary controls; final
reset delivery is **14/16**, with 30/32 controls. No duplicate early
delivery occurs. All sixteen old/final archive exports preserve 112
complete exact bodies; all ten final archives validate strictly.

The four final non-stress missing early source indices are B1:23, A1:17,
reset-A:20 and reset-B:8. These trials contain only seven aggregate-prefix
records and seven count-560 good hints each, with no prefix-log overflow;
each recorded aggregate exits as outcome 5. Every observed eligible
aggregate prefix is matched by an exact early receipt. Thus the measured
shortfall is already present at the prefix-observation boundary, rather
than an extra observed eligible candidate disappearing in the new FIFO
handoff. Identical aggregate HT-SIGs and the lack of a third receiver
prevent assigning a more specific RF/PHY cause or a per-missing-attempt
on-air verdict.

Both final injected resets execute once, retain TX-inhibit value 15
before/after, and measure 16 TSF microseconds. That proves this bounded
injected integration, not recovery from every natural persistent-busy
state. Both final stop-stress trials reach eight queued slots, retain
2,328/2,479 full-held samples and drain exactly after stop. Their later
RF losses are expected backpressure consequences, not reliability passes.
Final reports show no controller error, pending/dropped F2 packet, host
resync/stall/retry or prefix mismatch.

### Verification, preservation and remaining gates

`reference-early-fifo`, `reference-early-fifo-reset-test` and
`reference-early-fifo-stop-test` build the three gated profiles.
`early-rx-test` exhausts every supported length 44..996, including odd
tails and output canaries, and mutates header/prefix/SIG/FCS, sample,
packet and archive evidence under ASan/UBSan. Existing PSM, MAC, Block ACK
and host protocol tests pass for the profiles. Invalid parents and
combination with advance reject compilation. Final freestanding C3/RP
builds pass; separate rebuilds reproduce all three final RF C3 images.
The default controller is byte-identical to the preceding production
image, and all ten source-profile C3 images are identical.

The independent audit verifies **33 trials, 181 full reports, 33 armed
reports, 792 source records, 33 exact retained SHM bodies, 31 exact early
F2 proof samples and 112 complete archived packets**, with conserved
accounting for **1,628 raw captures**. Raw captures include ambient traffic
and are not distinct transmitted opportunities. Bad-FCS discards remain
recorded, not misclassified as transport drops. Historical metadata,
counter-layout, RF and stress failures remain in the audit.

Fresh complete originals were saved before both flashing campaigns.
Both final full 16-MiB flashes were independently reread, byte-compared
and SHA256-verified after restoration: A `8cda7a00...23628`,
B `bc3f972d...21e2`. Both badges are confirmed BOOTSEL. No credentials were
used.

The finite sixteen-prefix log, fixed MCS0/554-byte stimulus and bounded
eight-packet export are not arbitrary sustained aggregate reception.
Persistent-busy/pre-prefix loss qualification, variable-length
continuation accounting and native FCS state, negotiated Block ACK/MAC,
protection and HT activation remain open. Do not advertise HT or mark
D2/early-capture reliability complete.

## Fixed-source vendor baseline and pre-prefix discriminator (2026-10-20)

The next bounded investigation bypasses the clean PSM/C3 receive path
while keeping the version-6 two-554-byte-body source unchanged.
**The vendor raw monitor is not yet a qualified loss baseline.**
Evidence, RF images, complete archives, failures and independent audit
are retained under `build/clean-rx/preprefix-baseline/`.

### Implemented control and complete archives

`reference-stimulus-monitor` uses the existing vendor raw-monitor setup
on channel 6 with the fixed source. Unlike the older 170/298 handoff
monitor's eight-packet selection, this profile retains up to **32 complete
nonce-bound SDPCM packets** in a separate bounded RP buffer. That covers
the sixteen ordinary controls and both bodies of eight aggregates.
It records capture/full/oversize/unmatched counts and keeps the first
eight packets in the ordinary stopped report for continuity.

After stop, phases 5..8 export four pages. At trace 128, a 16-byte `MRA1`
header contains magic `0x3141524d`, zero-based page, total retained count
and this page's count. Up to eight existing 656-byte raw packet slots
follow. Validation checks identity/counts, packet envelopes, complete
packet CRC and zero padding/unused slots. The collector then regenerates
every source body and compares all 554 bytes, including FCS, across all
pages. The new fixed-source pass requires all 32 expected bodies, no
duplicate/corrupt body and no archive overflow/oversize.

The old handoff monitor retains its original eight-slot behavior and
pair/control qualification. No shared driver or vendor firmware is
modified; no station credentials or additional on-air TX are introduced.
All source C3 images remain byte-identical to the preceding RF source.
The clean receiver C3 image also remains byte-identical.

An initial setup rejection incorrectly treated fixed-source nonce byte
10 as a handoff case selector. That byte is random for version 6.
The guard now distinguishes the profiles; the rejected setup remains
excluded from RF evidence. The native lifecycle test explicitly uses a
nonzero random byte there.

### Measured cutoff, not an RF-loss conclusion

Eight unpolled vendor trials across both roles/orders each report
**exactly 30 raw SDPCM packets**, including ambient packets. Their
nonce-bound selections vary from 8 to 17, for 108 complete exact packets
in total. They receive complete correct first/second bodies from the
same source aggregates in both roles, but stop reporting before the
source's finite sequence is fully represented. All eight fail the new
all-32 gate, without selection overflow, oversize or corrupt/duplicate
accepted bodies.

The separately gated `reference-stimulus-monitor-poll` performs read-only
get-monitor command 107 checks at calibrated 100-ms intervals. Two A
trials extend raw reception to 41/42 packets and preserve 19/11 exact
nonce packets. Both still fail all-32 qualification. This demonstrates
that host/control servicing affects the measured baseline; it does not
identify a unique internal vendor buffer, interrupt, credit or RF cause.

The 10-ms `reference-stimulus-monitor-poll-fast` discriminator instead
hits a control timeout after 60 successful checks and a failed 61st,
with 46 raw packets and 25 counted nonce packets. Its original code
could not export the partial archive after that error. Those 25 counted
packets are **not** included in the independently byte-verified packet
total. The failed capture and failed page request remain failures.

The final implementation freezes archive/counters and original firmware
error details on either normal stop or error. Post-fault diagnostic
pages can be retrieved without clearing or disguising the original
error; the trial's failed stop still prevents successful qualification.
ASan/UBSan tests execute this actual monitor lifecycle for every retained
count 0..32, including timeout preservation and all post-fault pages.
This post-fault path is native-tested, **not subsequently RF-qualified**.

### Clean control, exclusions and outcome

Four additional clean FIFO trials deliver **29/32** exact first bodies:
7, 7, 7 and 8. The three missing bodies again coincide with only seven
aggregate-prefix/count-560-hint records; every observed eligible
aggregate is delivered. No prefix overflow/mismatch, controller error,
pending/dropped F2 packet or host resync/stall/retry is reported.
Captured/posted/consumed accounting reconciles for 197 raw captures.
Three trials receive every ordinary control; the fourth misses controls
3/9/13. Thus ordinary control delivery is 61/64 and **none of the four
trials passes the complete strict fixture gate**.

An external process twice claimed B's BOOTSEL interface exclusively.
Testing paused, the user released/reconnected it, and restoration was
completed. Readiness-only attempts that never submitted a source burst
are not RF trials. A resumed batch also stopped at a transient readiness
check; its unstarted cells are not fabricated. Further flashing stopped
after the recurring exclusive-access failure; the planned B slow-poll
and final fast-poll RF cells remain unexecuted.

The independent bitwise-CRC/fixture audit verifies **16 submitted
sessions, 15 source RF trials, 111 full reports, four armed reports and
360 source records**. It independently checks **138 complete vendor
packets and 29 complete clean early packets**, preserving the setup and
timeout exclusions. All four final software profiles build freestanding;
the actual monitor lifecycle, page/count/padding mutation tests and
existing clean/handoff/native suites pass under ASan/UBSan. Invalid
polling modes/parents reject compilation.

Both original full 16-MiB flashes were saved before writes and finally
restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Both badges are BOOTSEL.
No further RF writes followed final restoration.

This campaign excludes using this vendor-monitor configuration as an
unqualified per-attempt RF denominator. It does not prove that the
clean pre-prefix misses are interference, reset failures or a specific
PHY defect. Early-capture reliability and D2 remain open; a sustained
independent receive baseline or simultaneous third-receiver evidence is
still needed before assigning that cause.

## Quiet-only versus copy replay at the original destination (2026-10-20)

Evidence: `build/clean-rx/replay-count/`. This bounded discriminator
separates stream quieting from replay without the previous 64-word
destination displacement. Both default-off profiles use scope 3,
stream-rearm mode 5, threshold 128, the same verified count-134/32-TSF-us
plateau, three scope records and matching-epoch stream hold:

- `reference-handoff-scope-replay` reissues the existing copy command.
- `reference-handoff-scope-no-replay` sets
  `CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY=1` and jumps directly to the next
  snapshot instead.

Replay suppression is boolean and requires exactly scope 3/rearm 5.
Existing displaced scope profiles and the default controller are unchanged.
The dedicated native scope interpreter counts zero or one actual replay
instruction, checks the original destination, guards, timer wrap,
EOF/abort/frozen-timer paths and register preservation. This is emitted
program/guard testing, not emulation of receive-engine count or FCS.

The two archived 61,756-byte receiver images differ in just seven bytes
of one eight-byte PSM instruction. PSM starts at image offset `0x7e44`;
action PC 3580 is at file offset 60964. The replay is
`op=0x376,a=0x1883,b=0x1004,d=0x1004`; its control is
`op=0x77f,a=0x1780,b=0x1780,d=3581`, jumping to the following instruction.
The comparison uses alignment relative to PSM, not file-offset alignment.
This is instruction matching, not a claim of equal hardware latency.
Source controllers byte-match the independently vendor-verified version-7
170/298-byte source.

Twelve RF trials cover both receiver roles, both size orders, and
reverse-order repeats of the long continuation:

| Profile / continuation | Trials | Live count after 32 TSF us | Retained bytes | Native terminal result | Exact F2 pairs | All controls |
| --- | ---: | --- | --- | --- | ---: | ---: |
| Quiet only / 298 | 4 | 134, unchanged | 128-byte canonical prefix | count 134, watchdog outcome 6 | 0/4 | 3/4 |
| Quiet + replay / 298 | 4 | 58, after 134 | all 170 remaining source bytes overwrite body offset 0 | count 176, bad-FCS outcome 2 | 0/4 | 0/4 |
| Quiet only / 170 | 2 | scope correctly skipped | complete FCS-valid 170-byte body | count 176, good outcome 1 | 2/2 | 1/2 |
| Quiet + replay / 170 | 2 | scope correctly skipped | complete FCS-valid 170-byte body | count 176, good outcome 1 | 2/2 | 0/2 |

Every executed scope retains original base `0x800`, end 298 and context
`0xf020`. Controls change from `0x802` to `0x800` in both profiles and stay
quiet. Every live post-action observation is 32 TSF units. Quiet alone
never progresses beyond the original 128 canonical bytes. Replay obtains
the entire expected source range `[128,298)` at destination `[0,170)`,
including the original full-body FCS bytes, but that is not a complete
298-byte body and cannot validate as one. The native count falls from
134 to 58 in the live scope and eventually reaches 176. These are raw
observations, not an adjusted length rule.

This isolates a replay-dependent restart/overwrite from quieting alone.
It reinforces why the previous displaced replay could reconstruct the
whole SHM body while native reception still rejected it. It does **not**
identify a particular undocumented control bit as the FCS reset, prove
that count compensation is safe, or provide a normal admission repair.
The next discriminator must separate the replay field's receive-state
effects or find a continuation operation that preserves native count/FCS.
Native GoodFCS and FIFO/F2 admission remain mandatory.

One old collector summary had a separate selection defect: normal-mode
`replay-B-128` retains the first exact body at source index 5, but
`case_terminal_*` only followed retained early records at index 2. Its
summary printed zero while the complete second snapshot, matching copy
probe and prefix record actually contain count 176/outcome 2. Normal
mode now follows any already validated exact early body; negative cases
remain pinned to their deliberately corrupted source index 2. The original
failed JSON is preserved. A small offline harness includes the actual
collector and reprocesses all twelve original report pairs under
ASan/UBSan: only those three fields change in that one summary. All
qualification booleans and failures remain unchanged.

The independent bitwise-CRC/fixture audit verifies **48 full reports,
288 source records, 16 full F2 samples and 621 conserved captures**.
It independently verifies all twelve first bodies, all four replay
tails, four short exact F2 pairs, all scope/native outcomes and all twelve
native capture-decoder rechecks. Ordinary controls total **168/192**;
only one short-body trial has a pair plus every control. These are
diagnostics, not strict qualification passes. No failure is discarded,
and bad-FCS discard counters are not treated as transport loss.

Both final profiles, the existing displaced held-scope profile and the
default production controller build and pass their native ASan/UBSan
suites. Five invalid replay/value/parent combinations reject compilation.
Final RF receiver/source controllers byte-match their archives; existing
held-scope and production images byte-match their previous versions.
Both complete original 16-MiB flashes were saved before writes and
restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. A needed a bounded BOOTSEL
reenumeration retry before restoration; both final serial-specific checks
report BOOTSEL. No RF writes followed final restoration.

## Compound replay command, FCS state and copy-local length (2026-10-20)

Evidence: `build/clean-rx/copy-field/`. The next investigation traces the
actual emitted instructions before changing timing or lengths. Production
PC 135 starts the six-byte prefix copy with control `0x40`; PC 161,
the first continuation after PLCP, and the aggregate's initial body-start
instruction use `op=0x376,a=0x1883,b=d=0x1004`. The post-stall replay had
reused that same compound command as if it were a resume operation.
Its eight-bit field update covers register bits 6..13, asserting
`0x20c0`: bits 6, 7 and 13 together, not a single resume bit.

Public [b43 register names](https://github.com/mbuesch/b43-tools/blob/master/debug/include/spr.inc)
identify the legacy FIFO control/count registers but do not document these
three bits for this Arch15 device. The
[Linux register structure](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h)
also leaves relevant offsets unnamed. Neither is treated as a definitive
CYW43439 bit specification. No public code is incorporated into the firmware.

### Matched field omissions

Three default-off profiles omit one already asserted bit:
`reference-handoff-scope-no-bit6` (`field=0x82`),
`reference-handoff-scope-no-bit7` (`0x81`) and
`reference-handoff-scope-no-bit13` (`0x03`).
They retain scope 3/rearm 5, original destination, the count-134 plateau,
32-TSF-us observations and matching-epoch stream hold. Each archived
61,756-byte receiver differs from the full-field reference by exactly
one byte of the same PSM instruction at PC 3580/file offset 60964.
The full-field controller byte-matches the previous replay campaign;
source controllers remain independently verified version 7.

The first eight RF trials cover all four profiles in both roles:

| Replay field | Omitted register bit | Count after 32 TSF us | Native terminal result | SHM result |
| --- | --- | --- | --- | --- |
| `0x83` | none | 134 -> 58 | 176, bad FCS/outcome 2 | entire remaining 170-byte tail overwrites prefix |
| `0x82` | 6 | remains 134 | 134, watchdog/outcome 6 | only original 128-byte prefix |
| `0x81` | 7 | 134 -> 58 | **176, GoodFCS/outcome 1** | entire remaining tail overwrites prefix |
| `0x03` | 13 | 134 -> 58 | 176, bad FCS/outcome 2 | entire remaining tail overwrites prefix |

This separates count restart from FCS state. Bit 6 is necessary for
progress in these replay combinations; bit 13 is not necessary.
Omitting bit 7 preserves native FCS success without preserving the old
count. The initial PLCP/body/next-MPDU starts remain unchanged: only the
mid-body replay field changes. This is not a general recommendation to
remove bit 7 at actual frame boundaries.

Four next RF controls deliberately corrupt only the final FCS byte of
source index 2's second body, comparing full-field and no-bit7 replay in
both roles. All four retain that first aggregate and native bad FCS.
The no-bit7 success is therefore not explained by simply leaving a stale
first-body GoodFCS flag or disabling final error detection.

### Complete bytes and native GoodFCS together

`reference-handoff-scope-no-bit7-shift` combines the same `0x81` replay
with the already verified 64-word destination shift, scope 3/rearm 6.
Before the admission guard below, this controller differs from the old
held-shift reference in just one byte of the replay instruction.
Four good trials across both roles/repeats retain **all 298 expected
bytes, valid full-body software FCS, native status `0x4100` and outcome
1**. Two bad-last controls retain the deliberately wrong FCS and native
status `0x4000`/outcome 2. The final guarded tests reproduce both outcomes.
Across the whole campaign, nine retained complete 298-byte bodies have
both native GoodFCS and independently valid software FCS.

The count still resets to 58 in the live scope and ends at 176: it
describes the restarted copy, not the complete reconstructed MPDU.
The fundamental error was treating a compound start/initialization as a
transparent mid-body resume. The matched bit-7 effect is consistent with
FCS reinitialization; an undocumented universal bit name is not claimed.
The byte count and FCS accumulator need separate ownership/accounting.
No raw count is changed to 304 or otherwise normalized.

### Unsafe partial export and explicit quarantine

Native GoodFCS exposed a second assumption: ordinary FIFO publication
uses native count minus six as the complete MPDU length. With count 176,
the shifted diagnostic consequently exported a 170-byte prefix of the
298-byte body. The full software FCS of that prefix fails. All four
complete shifted-good snapshots above therefore have failed delivery
integrity, not successful pairs. These four complete malformed exports
are independently checked and retained; software CRC fields calculated
over them are not mistaken for valid on-air FCS. Original-destination
tails lack the expected MAC header and do not provide equivalent archived
export evidence; absence from the selected samples is not proof of absence
from the wire.

Modified-field profiles now add a PSM admission guard **after** preserving
the immutable second/copy snapshot and before ordinary FIFO capture.
For the matching nonzero retained owner, current epoch and one-shot
advance, native success must also have `count == expected_body_bytes+6`.
A mismatch retains native count/status/outcome in the snapshot, but
records explicit length-reject outcome 4 and increments the existing
length-discard counter before rearm. It cannot enter SRAM/F2 publication.
Matching native geometry follows the original path. Bad FCS, EOF/abort/
watchdog handling, non-advanced frames and unrelated owners/epochs are
unchanged.

An initial guard lacked the owner/epoch restriction: eight failed RF
trials over-rejected later ordinary traffic, with 32..105 length drops.
They are preserved, not used as reliability successes. The final guard
has emitted-instruction tests for eleven cases, including complete/short
geometry, mismatch, bad/abort/watchdog, no advance, missing/wrong owner
and both stale-epoch directions. Tests check R2 restoration, unchanged
native count/status and unchanged snapshot bytes. Existing native
missing-owner/epoch/length/immutable-snapshot regressions also pass.

Eight final RF trials cover original/shifted good long bodies, shifted
bad-last in both roles, and both short-continuation controls:

- All four retained good long bodies remain native GoodFCS/count 176,
  but produce exactly one explicit length reject each and no partial
  advanced F2 sample. Both shifted bodies still have full valid FCS bytes.
- Both deliberately corrupted retained second bodies remain native
  bad FCS/outcome 2, with zero length rejects and no advanced delivery.
- Both short controls skip replay and deliver exact 170-byte F2 pairs,
  with zero length rejects.
- All eight have valid delivery integrity and zero invalid-control
  packets. Ordinary controls are **118/128**, with all controls in four
  trials. Early/control misses still prevent strict qualification.

The guard is compiled only for the modified-field discriminators.
Default, prior full-field, quiet-only, stream-on and displaced held-scope
controller binaries remain byte-identical to their previous versions.
Seven invalid field/value/parent combinations reject compilation; both
original-destination and shifted new profiles build freestanding and pass
native ASan/UBSan suites.

### Campaign accounting and boundary

The independent bitwise-CRC/fixture audit passes **34 RF trials,
136 full reports, 816 source records, 1,336 conserved captures and
34 actual capture-decoder rechecks**. It checks all complete first bodies,
all replay tails/full continuations, nine observed corrupted-last native
rejections, four malformed archived exports and 26 exact archived F2
samples. One intermediate bad-last trial retained source index 5 instead
of the intentionally corrupted index 2; it is explicitly not a bad-last
reception proof. The original failed collector summary is preserved.
Total ordinary controls are **399/544**, including the over-rejecting
guard trials; neither those losses nor the final residual misses are
hidden. Four short pairs exist across both guard revisions, but only one
has every ordinary control.

One argument-only setup failure and one BOOTSEL readiness failure submit
no source burst and are excluded from RF totals. A's USB interface was
claimed exclusively by an external process; testing stopped, B was
restored first, and the user released A before its restoration. Full
readback-verified originals were retained before each subsequent RF phase.
After final RF, both entire 16-MiB originals were restored, independently
reread, byte-compared and SHA256-verified: A `8cda7a00...23628`,
B `bc3f972d...21e2`. Both final serial-specific checks report BOOTSEL.
No RF writes followed final restoration.

This establishes the controlled mid-body FCS-state cause and safely
quarantines the newly reachable partial-export path. It does **not**
implement ordinary full-body FIFO publication from copy-local counters,
variable-length segmented admission, general receive reliability or HT
station activation. Next work must model the complete MPDU and its native
copy segments explicitly, or avoid the disruptive stream/replay handoff;
relabeling count 176 as a complete 298-byte receive is not a repair.


