# Vendor/AP HT reference experiment

Two dedicated, freestanding C/nolibc RP2350 applications:

- **Vendor station:** loads the unchanged supplied vendor Wi-Fi firmware,
  joins the approved WPA2 AP, requests HT20 MCS0, queries actual supported
  interfaces and submits a bounded, known unicast burst.
- **Clean observer:** runs a default-disabled passive PSM HT lifecycle logger
  on the station's actual channel. TX is disabled; it does not associate,
  install a key or transmit competing ACK/BA responses.

Neither application links the clean station/WPA2 stack or previous
aggregate/BA badge experiments. Both use the existing display driver and
project-owned glyphs for a streamed, framebuffer-free status screen.
Shared board, USB, transport and loader code
is reused. The host collector uses the repository's existing libusb transport.
Vendor assets are not dependencies of the clean controller.

## Next-MPDU command and RX recovery diagnostics

The default-disabled next-MPDU discriminator changes only the observer's
actual continuation-start command from field `0x83` to `0xc3`.
Initial body commands and mid-body replay remain unchanged:

```sh
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/NEW_C3 reference-handoff-next-c3
CLEAN_RX_REFERENCE_BUILD=build/clean-rx/NEW_C3 \
  bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128 good
```

Use `256` for the reverse size order, or the existing named negative
cases for deliberately damaged source aggregate 2.
`reference-handoff-defer-next-c3` is the matched deferred-stream control.
`CLEAN_RX_HT_NEXT_FIELD` defaults to `0x83`; selecting `0xc3` requires
the bounded advance/copy profile. Six ordinary-threshold good trials
retain exact 170/298-byte pairs with native GoodFCS, correct native
length and exact advanced F2 delivery. Four matched `0x83` trials fail
the continuation. Residual control losses, missed first bodies and
unqualified malformed/truncated recovery remain failures.

Separate fixed-554-byte early-FIFO profiles investigate rearm recovery:

| Target | Diagnostic |
| --- | --- |
| `reference-early-fifo-recovery` | Existing one-shot rearm recovery control |
| `reference-early-fifo-ordered` | Ordered RXE/crypto flush at every rearm |
| `reference-early-fifo-prefix` | Above plus 250-us count-2/RX-active prefix recovery |
| `reference-early-fifo-ordered-retry` | Ordered flush only after the existing first rearm fault |
| `reference-early-fifo-prefix-retry` | Fault-only flush plus prefix recovery instrumentation |
| `reference-early-fifo-ordered-retry-test` | Fault-only flush with the existing software-latched fault injection |

Build with a fresh `REF_BUILD`, select that same directory through
`CLEAN_RX_REFERENCE_BUILD`, and run with `NEW_PREFIX - 512` and **no
handoff-case argument**. This source does not accept `128`/`256`.
No credentials are read.

`CLEAN_RX_HT_ORDERED_FLUSH` defaults off: 1 means every rearm, 2 means
fault-only. It requires passive timeline/recovery and excludes host PHY
reset. Event-10 polling is bounded at 65,535; expiration stops with
explicit PSM reason 9. `CLEAN_RX_HT_PREFIX_RECOVERY` also defaults off
and requires ordered flush. It records count/status/PHY-control/elapsed
time before the PHY pulse; it does not reinterpret RX count or FCS.
Its fallback prefix budget increases from 1,023 to 65,535 polls so
the separate 250-us TSF deadline can operate.

WND9 flags 16 and 32 distinguish ordered and prefix diagnostics. The last
16 words of the existing rearm export hold counters/trigger evidence
instead of the mutually excluded host-reset record.
Collector JSON reports `ordered-rx-recovery`, including attempts, phase,
busy polls, count-2 observations and actual prefix-recovery triggers.
Phase 3 means the sequence reached normal RXCTL verification, not proof
of physical-wedge recovery; phase 4 is the bounded event-10 timeout.

Proactive ordered flush still causes early event-10 timeouts and is
**not a generally usable rearm replacement**. The fault-only injected
tests execute the sequence on both badges with exact delivery, but
the injected fault is software state, not a physical `0x804` wedge.
No natural 250-us prefix recovery triggered. All these profiles remain
diagnostic; HT association stays disabled.
See the [matched boundary and recovery evidence](../dev-doc/HT20_IMPLEMENTATION.md#next-mpdu-command-and-bounded-rx-recovery-experiments-2026-10-20).

## Variable-length boundary microscope

`reference-handoff-variable` enables a separate version-9 source fixture and
bounded two-body observer using `0xc3`. It does **not** enable an N-body
receiver or HT association:

```sh
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/NEW_VARIABLE reference-handoff-variable
CLEAN_RX_REFERENCE_BUILD=build/clean-rx/NEW_VARIABLE \
  bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128 good 0
```

The final argument is a rotation selector, `0..7`, not a byte length:

| Selector | Source aggregate 2: first / second MPDU bytes |
| --- | --- |
| 0 | 80 / 554 |
| 1 | 127 / 381 |
| 2 | 170 / 298 |
| 3 | 298 / 170 |
| 4 | 257 / 255 |
| 5 | 479 / 554 |
| 6 | 80 / 80 |
| 7 | 321 / 173 |

Use `256` instead of `128` to reverse each aggregate's body order. Subsequent
aggregates rotate through the remaining pairs; the sixteen legacy/single-HT
controls keep the original 170- or 298-byte size. The immutable early slot
retains at most one eligible aggregate's first body, up to 480 bytes.
A later selected source index is **not** proof of reception of aggregate 2;
in particular, a 554-byte first body cannot fit this slot. Continuations
up to 554 bytes are compared in full through the archived F2 sample.
The separate terminal SHM snapshot remains limited to 300 bytes; its
`fcs_valid:false` is not a full-body FCS verdict for a larger continuation.

The observer requires a fresh matching owner/count, native GoodFCS, a
non-busy next-length register, bounded lengths, no EOF/abort, and exact
`PSDU = 8 + align4(first) + second` before advancing once. Deliberately
malformed delimiter and truncated-body cases are retained as evidence,
not treated as qualified recovery. Use the existing case names and
selector 4 to test odd-sized bad first/last bodies.

Matched experimental controls, all default off:

- `REF_NEXT_STREAM=1` or `2` adds one preserved-bit OR of `SPR0x004` bit 1
  immediately before or after the next-body command. Both failed to fix
  the 80/554 case in both badge roles.
- `REF_VARIABLE_STREAM64=1` starts normal streaming at native count 64
  instead of 128. This produces exact 80/554 pairs in both roles, without
  changing initial-body commands, replay, count or FCS policy. Repeats still
  miss controls/eligible bodies and one trial records an invalid control;
  this is a causal diagnostic, **not** a qualified replacement threshold.

The 50-trial campaign retains all failures: 39 exact selected pairs,
726/800 controls, one delivery-integrity failure, and only 13 existing
collector-policy passes. See the
[variable-length evidence](../dev-doc/HT20_IMPLEMENTATION.md#variable-length-boundaries-and-initial-stream-state-2026-10-20).
General N-body delimiter/EOF handling and repeated buffering/backpressure
remain open.

### Verified hint follow-up: bounded short-first streaming

The follow-up tests the hints' SHM-only proposal directly. Disabling the
mid-body stream instruction does **not** support general long captures:
80/554, 127/381, 257/255 and 479/554 abort reproducibly in both roles.
The measured cumulative native counts cluster around 512 despite a much
larger SHM window. SHM address space does not prove upstream capacity.
`reference-handoff-shm-only` retains this default-off diagnostic; do not
use it as a firmware repair.

The supported experimental improvement is narrower:

```sh
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/NEW_SAFE_VARIABLE reference-handoff-safe-variable
CLEAN_RX_REFERENCE_BUILD=build/clean-rx/NEW_SAFE_VARIABLE \
  bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128 good 0
```

This combines:

- `CLEAN_RX_HT_SHORT_STREAM=1`: start at native count 64 **only** for a
  pending first aggregate body whose current copy length is 64..121 bytes.
  Ordinary/single-HT reception, larger first bodies and continuations keep
  the normal 128-count threshold. Initial and continuation copy commands,
  replay, native counts and GoodFCS policy are unchanged.
- `CLEAN_RX_HT_DELIMITER_GUARD=1`: exclude `SPR0x00e` bit 4 before advancing,
  matching the locally verified vendor instruction. Emitted-program
  fault tests verify exclusion. Terminal snapshots of the real malformed
  fixtures do not retain that bit; physical CRC-error mapping and complete
  malformed-input recovery remain unqualified.
- All variable handoff builds now enforce native continuation count
  `saved_next_length + 6` before FIFO admission, with matching owner/epoch.
  Mismatches are explicit outcome 4, not normalized counts. Native count,
  status and the bounded snapshot remain available. This extends the
  earlier replay-only quarantine after a new pre-fix trial exported a
  malformed 237-byte prefix despite native GoodFCS.

`reference-handoff-short-stream` selects the short-first path alone;
`REF_DELIMITER_GUARD=1` adds the status guard. Both gates default off.
`REF_SHM_ONLY=1` cannot be combined with short-first/global-64 streaming.

The final build has exact selected pairs in **16/16** good trials,
including all eight pairs in both badge roles. Its full 24-trial campaign
has **364/384 controls**, zero invalid/duplicate controls, and only
**10 collector-policy passes**. All earlier failed and malformed exports
remain in the 64-trial evidence; the final native-count guard is also
tested against the reproduced 243-versus-387 count mismatch.
This is not N-body buffering, all-rate qualification or permission to
advertise HT. See the
[hint follow-up evidence](../dev-doc/HT20_IMPLEMENTATION.md#verified-hint-follow-up-shm-limits-short-first-streaming-and-admission-2026-10-20).

## Experimental observer FIFO

`reference-stimulus-fifo` enables `CLEAN_RX_HT_OBSERVER_FIFO=1`, which
defaults off. It uses the existing eight-slot controller SRAM FIFO instead
of dropping a completed passive capture when the single F2 credit is busy.
Full queues retain the next completed frame in SHM. Each queued frame keeps
its own passive correlation ID, including the advance tag.

```sh
make -f clean-rx/vendor-burst/Makefile reference-stimulus-fifo
bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL \
    build/clean-rx/vendor-burst/NEW_FIFO_PREFIX - 512
```

This is the fixed clean RF source, not the vendor/AP source; `-` is only
the credential-position placeholder. No AP credentials are read.
`reference-stimulus-fifo-test` deliberately stops host frame polling for
600000 SOF-calibrated microseconds after single-HT control 1.
`reference-stimulus-fifo-stop-test` keeps that pause until the stop request.
Both continue cached timeline sampling and USB service. They must actually
observe eight queued frames, an outstanding F2 credit and cached
`PSM_STATE_CAPTURED` before reporting `stress_ownership_proven`.
These stress profiles deliberately miss later RF frames; their ordinary
all-controls qualification remains a failure, not a substituted success.

Stop freezes MAC/PSM reception, drains SRAM and any completed held-SHM
frame, and acknowledges only after all captures are posted. The host also
consumes the final F2 packet before freezing its report. The controller
has a 500000-microsecond / 1000000-poll drain bound; the host has a
1000000-raw-tick / 10000-poll bound. A stalled clock, invalid occupancy or
failed drain is an explicit error. An unfinished radio frame can still be
aborted by stop; only completed retained captures are conserved.

Observer ABI 21 uses bytes 16/20 for stress peak occupancy/held-snapshot
count and byte 52 for pause state: 0 not triggered, 1 active, 2 timed pause
finished, 3 cancelled by stop. Error reports reuse bytes 16/20 for loader
stage/address and are not qualification evidence. FIFO frozen reports
require `captured == posted == consumed`, zero dropped frames and zero
duplicate/corrupt controls. Non-FIFO profiles retain their previous
accounting rules. WND9 is unchanged.

Profile flags now participate in the build ID so host-only pause switches
cannot reuse another profile's RP images or collector.
See the [FIFO qualification record](../dev-doc/HT20_IMPLEMENTATION.md#passive-observer-fifo-qualification-2026-10-19)
for retained failures, pre-prefix losses and short bad-FCS exits. This gate
does not fix persistent RXE busy states or qualify general aggregate RX.

## Controlled 170/298-byte handoff

The default-disabled version-7 source removes vendor aggregation timing
from the existing one-shot next-body diagnostic:

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff
bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128 good
```

Use `128` for 170-to-298-byte MPDUs or `256` for the reverse order.
The optional sixth argument is `good` (default), `bad-first`, `bad-last`,
`bad-delimiter` or `truncated-body`. These arguments require this profile;
they are not interchangeable with the fixed 554-byte source. The fourth
argument is only a placeholder: no AP credentials are read or provisioned.

Each run sends eight cycles of legacy OFDM, single HT and a 478-byte
two-MPDU HT PPDU, on channel 6/MCS0, with calibrated 40-ms gaps.
Only aggregate index 2 has the selected fault. The truncated-body case
increases the second delimiter's claimed length by eight bytes without
changing the actual finite PPDU or HT-SIG; it does not force TX underrun.
The final source sets More Data only in the first unicast FromDS MPDU,
so a genuine second prefix differs from an old first prefix.

The source controller enables `CLEAN_RX_HT_HANDOFF_STIMULUS=1`; the
observer enables passive early retention, one-shot advance and the
eight-slot FIFO, without timeline sampling. The source stages and
readback-checks all 1132 bytes, including the zero tail. This is not
general A-MPDU TX, negotiated station operation or a BA agreement.

`controlled-handoff-result` requires independently correlated, complete,
exact first and second MPDUs for good-pair qualification. Bad-first needs
a later exact pair; bad-last additionally needs the complete, deliberately
corrupted second bytes with matching owner/epoch, terminal status and
invalid FCS (`exact_bad_second`), not just a generic bad-FCS exit.
Negative qualification also requires later controls and clean delivery
accounting. Delimiter/truncation cases remain diagnostic-only and cannot
return a successful negative qualification.

The ordinary 128-byte-streaming baseline reproduces a bad/abort continuation
after a fresh second eight-byte prefix in both roles/orders, not a complete
pair. The separate [deferred-streaming repair](#deferred-streaming-handoff-repair)
now restores exact bounded pairs. Baseline failures and trials missing
required controls still return nonzero and retain their reports.
See the [controlled handoff evidence](../dev-doc/HT20_IMPLEMENTATION.md#controlled-handoff-stimulus-and-reproducible-continuation-failure).
Do not reuse an evidence prefix or rebuild the shared images during a run.
Save full badge originals before flashing; restore and independently
readback-verify both complete flashes afterward.

### Independent vendor raw receiver

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-monitor
bash clean-rx/tools/vendor-burst/run.sh CLEAN_TX_SERIAL VENDOR_RX_SERIAL NEW_PREFIX - 128 good
```

Use `256` instead of `128` for 298-to-170 rather than 170-to-298.
Only the good, version-7 two-body fixture is admitted. The source controller
and all generated bytes remain unchanged; this replaces only the listener.
As with other profiles, rebuilding overwrites shared build outputs, so
archive matching images/collector/build identity before another build.

The vendor listener does not read credentials, associate, scan, enable the
Ethernet data API or request data TX. It disables MPC/power saving, brings
the radio up, sets channel 6, enables monitor mode and verifies monitor
state plus actual/target channel. It drains bounded input before disabling
and readback-checking monitor mode and powering off. The common TFT labels
this role passive. A separate capability-only probe is available through
`make -f clean-rx/peer/Makefile ht-peer ht-peer-test` and that collector's
`SERIAL NEW_EVIDENCE_BIN --monitor-probe` arguments.

The shared vendor transport has an explicit raw-receive alternative to
Ethernet delivery. It validates SDPCM envelopes without interpreting
non-control channels, preserves flow/credit processing, and supplies every
non-control packet to the callback, including during CDC commands.
Normal sessions still reject channels above 2; raw and Ethernet receive
cannot be enabled together. No clean RXE/PSM code is used by this listener.

The independent trace uses magic `MON1` at offset 64, not the clean observer
ABI. It retains setup/cleanup command results and non-control channel
counts. Only packets containing the complete fresh 14-byte challenge are
saved: eight slots at offset 640, each a 16-byte length/time/channel/CRC32
header followed by at most 640 complete SDPCM bytes. Oversize and deliberate
selection-full exclusions are counted, not truncated or labelled F2 loss.
Other packets are counted without retaining their contents.

`independent-monitor-result` validates all 24 source records/final staging,
raw envelopes/CRCs and capture accounting. Success requires exact complete
legacy and single-HT controls plus both complete aggregate MPDUs, including
their FCS bytes and a common transmitted index. Payload-only matches are
reported separately and cannot qualify the trial.

Eight retained trials yield 64 complete exact MPDUs and 16 aggregate pairs
with independently verified FCS, in both badge roles and both orders.
The source controller byte-compares with the preceding failing clean-RX
campaign. This excludes incorrect source second-body bytes for this tested
waveform, not all possible source failures. It narrows the continuation
defect to clean receive/configuration; it does not identify a particular
RXE register or qualify general aggregation, loss rates, BA or HT negotiation.
Vendor-internal drops remain unknown. See the
[full evidence and restoration record](../dev-doc/HT20_IMPLEMENTATION.md#independent-vendor-raw-receiver-2026-10-20).

### Deferred-streaming handoff repair

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-defer
bash clean-rx/tools/vendor-burst/run.sh CLEAN_TX_SERIAL CLEAN_RX_SERIAL NEW_PREFIX - 128 good
```

Use `256` for the reverse body order. The default-disabled
`CLEAN_RX_HT_COPY_DEFER_STREAM=1` control raises the ordinary streaming
threshold from 128 to 384 bytes, without changing the source, copy end,
EOI, cleanup or stream-field masks. This keeps the 170/298-byte handoff
below the streaming threshold while retaining the existing less-than-506
staging-overflow guard for larger frames. It requires the ordinary copy
probe and excludes wait, end-bias, stream-clear and guard-body controls.
It is not a general receive-size or production DMA fix.

The collector reports `stream_prefix_threshold`, `stream_defer_enabled`
and `stream_defer_readback_verified`. Qualification additionally requires
the matching owner/epoch, complete handoff phases and streaming bit clear
in both pre-retention and pre-EOI hardware samples. No advance means this
readback condition is unproven, not that streaming was measured enabled.

Eight good-fixture trials capture exact complete/FCS-valid pairs, twice
per role/order. All four matched ordinary-threshold trials fail after the
correct first eight second-body bytes. Their receiver controllers differ
by one encoded threshold bit; all source controller bytes remain identical.
Only four good trials pass the full control-receipt gate; the other four
miss controls and stay failed trials despite their valid pair.

Bad-first and bad-last each pass existing exact-byte/rejection/recovery
criteria in all four role/order cells. Bad-last qualification requires the
complete deliberately corrupted second body, not a generic bad-FCS exit.
Malformed-delimiter and overlong-body cases remain diagnostic-only, with
all failures and later-control losses retained. D2 and HT association
remain open. See the
[complete causal campaign](../dev-doc/HT20_IMPLEMENTATION.md#deferred-pre-handoff-streaming-repair-2026-10-20).

For matched A/B runs, `CLEAN_RX_REFERENCE_BUILD=ARCHIVED_IMAGE_DIRECTORY`
selects the archived matching `vendor.uf2`, `observer.uf2` and collector.
The default remains `build/clean-rx/vendor-burst`. Always use a new evidence
prefix; a build override does not waive preservation/restoration requirements.

### DMA byte-destination diagnostic

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-dma
# Archive the matching images/collector, then build the matched control:
make -f clean-rx/vendor-burst/Makefile reference-handoff-defer-dma
```

Both profiles set default-off `CLEAN_RX_HT_COPY_DMA_PROBE=1`, which requires
the FIFO-backed copy probe. The second additionally defers streaming to
384 bytes. No PSM instruction or source-controller change is introduced by
DMA inspection; the ordinary programmed DMA ring and shutdown remain.

Before clearing/reposting a completed descriptor, the controller examines
its whole buffer for the fresh retained 14-byte challenge. After the existing
radio/DMA shutdown it verifies disabled control and stopped status, then
examines all 32 buffers, including unfinished ones. Failed shutdown readback
is a controller error, not a successful empty scan. A stopped-buffer match
is explicitly marked and is not a DMA-completion claim.

Observer ABI 22 replaces the ordinary complete-MPDU sample area at trace
2560 with a 1,728-byte `DMO1` version-5 diagnostic. Normal sample count is
zero. The 128-byte header records saved/matched/inspected/unmatched counts,
the challenge readback, first inspection context and 24-byte DMA header,
separate first/second QoS-prefix and valid-FCS counters, and shutdown
status/control. Two 800-byte records retain 32-byte contexts plus 768-byte
windows around a matching challenge. The packed final context word carries
the original DMA-header length and window start; record flags distinguish
frozen inspection, aggregate prefix and second body. The second saved slot
prefers an aggregate over ordinary traffic and a second body over a first;
counters run before this selection limit. Payload windows are retained only
when the full fresh challenge is present.

The collector checks ownership context, descriptor bounds, completed versus
frozen status, window bounds and counter conservation, then compares retained
windows with every exact transmitted legacy/single-HT/aggregate fixture.
Current handoff ownership describes inspection context, not DMA packet
ownership; only exact challenge-bound bytes/FCS prove a source match.
All trials are deliberately diagnostic-only and exit nonzero: replacing the
F2 proof samples means `exact_pair` cannot qualify, even with an exact pair
in the independent retained SHM snapshots.

The final eight role/order/control cells find 14 valid later aggregate first
bodies in inspected DMA buffers and no second-body prefixes. All four
deferred controls retain exact valid second bodies in SHM. Early controls
and abort/recovery losses remain visible, and ordinary/default firmware is
unchanged. See the
[complete investigation and limits](../dev-doc/HT20_IMPLEMENTATION.md#streaming-byte-destination-and-dma-discriminator-2026-10-20).

### Matched continuation-streaming controls

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-stream-hold
make -f clean-rx/vendor-burst/Makefile reference-handoff-stream-resume
make -f clean-rx/vendor-burst/Makefile reference-handoff-stream-late
```

Archive each matching image/collector set separately. All three require
the deferred non-wait copy probe and use 384 for the first body.
Default-off `CLEAN_RX_HT_COPY_STREAM_PHASE` selects 1 (hold384),
2 (resume128), or 3 (resume256). After the existing one-shot advance
becomes exactly 1, that threshold applies to the continuation **and
subsequent traffic**, not just a single body. Matched receiver C3 images
differ only in the encoded threshold immediate; source bytes are unchanged.

Phase profiles use copy-probe version 3 with first-activation count
before/after, RX-control/status readbacks, TSF, commit marker and epoch.
The collector emits `handoff-stream-activation`; only a committed event
matching the retained continuation's epoch proves that body streamed.
Later unrelated events in hold/short-body trials are labeled explicitly.
An eligible watchdog also retains bounded SHM bytes but keeps outcome 6,
normal cleanup and rejection. Abort keeps outcome 5. Normal F2 proof
samples remain available; no success condition is replaced by activation
proof.

Across both badge roles, a 298-byte continuation activated at count 130
under resume128 stops at 134, with the first 128 bytes canonical.
Resume256 activates at 258 and moves the canonical-byte cutoff to 252.
The hold384 control retains all ten tested pairs. A 170-byte continuation
can stream at 128 and still complete exactly. This excludes stale
first-body count as the cause of the observed activations, without
identifying the undocumented internal RXE/FIFO mechanism.

Twenty-six RF trials retain sixteen exact pairs; twelve also pass every
control-receipt gate. Control losses, watchdogs and an explicit abort stay
failed trials. These controls do not generalize streaming-safe delivery
to larger bodies or qualify HT association. See the
[phase/copy-boundary evidence](../dev-doc/HT20_IMPLEMENTATION.md#continuation-streaming-and-moving-copy-boundary-2026-10-20).

### Streaming copy-state and immediate retrigger control

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-stream-rearm-control
make -f clean-rx/vendor-burst/Makefile reference-handoff-stream-rearm
```

Default-off `CLEAN_RX_HT_COPY_STREAM_REARM` requires resume128 phase 2.
Mode 1 records state with a no-op; mode 2 executes the existing
continuation-copy instruction once immediately after streaming starts.
Both have the same instruction count and all other C3 bytes; only the
action instruction differs. No new register mask, reset, copy geometry
or transmitted bytes are introduced.

Copy-probe version 4 extends the record to 54 words, ending at SHM
0x600 and immediately before controller-fault report storage. Words
44..48 retain control/base/end/raw-next/raw-context before streaming;
49..53 retain count/control/base/end/context after the action.
`handoff-stream-rearm` reports and checks the readbacks; matching
ownership/epoch/commit and original exact-body/FCS/F2/control gates
remain mandatory. A committed first activation suppresses subsequent
command replay even when later packets clear their streaming bit.

Twelve RF trials show command bits latch (`0x0802` becomes `0x08c2`)
but no improvement in the 298-byte continuation: all eight command/
control long-body trials watchdog at count 134 with exactly 128
canonical bytes. Programmed base/end and sampled context stay correct.
All four short-body trials retain exact pairs; only one receives every
required control. This is a failed immediate-retrigger discriminator,
not a recovery repair or a test of later post-stall rearm.
See the [copy-state evidence and limits](../dev-doc/HT20_IMPLEMENTATION.md#streaming-copy-state-and-immediate-retrigger-discriminator-2026-10-20).

### Measured post-stall replay

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-stall-control
make -f clean-rx/vendor-burst/Makefile reference-handoff-stall-rearm
```

`CLEAN_RX_HT_COPY_STREAM_REARM` modes 3/4 extend the preceding gate with
a measured wait: require count 134 unchanged for 32 TSF microseconds,
then no-op or issue the existing copy command once. A shared 1,023-poll
budget also bounds a frozen timer. Progress, EOF, abort or exhaustion
skip the action; final checks run immediately before command issue.
Other/default modes remain unchanged.

Copy-probe version 5 keeps 54 words but reinterprets words 44..53 as
observed count, remaining budget, TSF low/start elapsed time, decision,
and post-action count/control/base/end/context. Decision values are
1 stable, 2 progress/change, 3 EOF, 4 abort, 5 exhausted.
`handoff-post-stall` labels valid diagnostics, stable proof and command
issue separately. Normal F2/byte/FCS and control gates still apply.

Twelve RF trials show a distinct delayed-replay failure in both roles:
after the verified 32-us plateau, MPDU bytes 0..31 at the unchanged SHM
start are replaced by exactly transmitted bytes 128..159, then RX
aborts. Matched no-ops instead retain the original 128-byte prefix and
watchdog at 134. None of these eight long-body trials qualifies.
Four short-body trials detect progress, skip the command, and pass
complete-body/FCS/F2/control checks; they do not prove stall recovery.

The count retreat after delayed replay required a version-5 activation
validator correction. Original failed JSON reports are preserved;
raw-report rechecks and independent byte mapping verify activation
without converting any aborted body to success. This demonstrates
separate source/read progress and SHM write placement, not a safe
destination adjustment or segmented receive repair. See the
[post-stall evidence](../dev-doc/HT20_IMPLEMENTATION.md#measured-post-stall-replay-and-suffix-overwrite-2026-10-20).

### Bounded copy-destination placement

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-destination-control
make -f clean-rx/vendor-burst/Makefile reference-handoff-destination-shift
```

Modes 5/6 of `CLEAN_RX_HT_COPY_STREAM_REARM` use the same measured
count-134/32-us stall and existing copy replay as above. Immediately
before replay, both write SPR `0x001`: add zero or add 64. Only the
immediate differs between matched receiver images. Progress, EOF,
abort or budget exhaustion skips both the destination write and command.
The endpoint remains unchanged; no GPR or reset is added.

Copy-probe version 6 retains the 54-word version-5 layout. Validators
require base `0x840` only for a confirmed mode-6 action, otherwise
`0x800`; wrong versions, offsets and shifted skip paths are rejected.
`handoff-post-stall` adds `destination_probe` and `base_shift_words`.
Both profiles report `diagnostic_only:true` and **always remain
nonqualifying**, so the runner returns 1 even for an exact short-body
pair. This is not a new production receive mode.

Twelve RF trials establish the placement effect in both badge roles:
adding 64 moves the newly read source bytes 128..159 to SHM offsets
128..159 and preserves the preceding 128 bytes. All four shifted long
bodies therefore retain 160 canonical bytes, but still fail FCS.
Zero-offset controls overwrite the start. Across eight long-body trials,
five abort and three reach a bad-FCS terminal; none delivers an exact
pair. All four short-body controls skip the action and retain exact
pairs, two with all required controls.

This verifies the tested destination displacement, not endpoint/count
units, safe segmented recovery or D2 completion. Adding zero is itself a
register write, not a hardware no-op. See the
[placement evidence](../dev-doc/HT20_IMPLEMENTATION.md#bounded-copy-destination-placement-2026-10-20).

### Replay endpoint and remaining-byte control

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-remaining-control
make -f clean-rx/vendor-burst/Makefile reference-handoff-remaining
```

Modes 7/8 keep the verified +64 destination displacement and identical
stall/replay path, then write endpoint SPR `0x02a` by subtracting zero
or 128. The specific hypothesis is endpoint 298 versus remaining 170.
Both writes have the same instruction footprint. Endpoints outside
129..300 produce decision 6 and skip all geometry writes and replay.
Progress/EOF/abort/budget skips remain unchanged.

Copy-probe version 7 retains the 54-word layout and adds no report
storage. Validators require the exact action-dependent base/endpoint;
`handoff-post-stall` reports `endpoint_probe` and `endpoint_subtract`.
Both profiles remain diagnostic-only: runner exit 1 is intentional
even for correct short-body controls.

Twelve RF trials show that endpoint 170 changes termination, but all
eight long-body cases still retain exactly 160 canonical bytes.
Unchanged-endpoint controls finish near count 304 with bad FCS;
remaining-endpoint A trials finish at 178 with bad FCS, and B aborts
at 68/69. All four short controls skip the action and deliver exact
pairs with all required controls. This remaining-length adjustment is
not a repair; the 32-byte fresh-copy limit remains unexplained. See the
[endpoint evidence](../dev-doc/HT20_IMPLEMENTATION.md#replay-endpoint-versus-remaining-bytes-2026-10-20).

### Focused copy microscope

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-on
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-off
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-hold
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-resume
```

`CLEAN_RX_HT_COPY_SCOPE` modes 1/2 compare stream field 2/0 before the
same mode-6 replay. A small PSM program records registers and SHM windows
at body offsets 128/160 before replay, just after it and after a bounded
32-us observation. Modes 3/4 then compare holding the stream off for
the committed current epoch versus re-enabling it. Each matched pair
differs in exactly one receiver-image byte.

The 80-word scope uses only SHM `0x4b0..0x4ff`; these profiles cap early
retention at 320 bytes. Static assertions and focused tests enforce the
separate first-body/continuation storage. EOF/abort exits and a 1,023-poll
fallback bound the wait. Commit values are 17 timer, 18 EOF, 19 abort,
20 exhausted. Stale/pending/invalid scope epochs cannot suppress normal
streaming; borrowed R3 is restored on every guard path.

The small `controller/copy_scope_test` executable runs the dedicated
emitted-instruction tests without arguments. Given one recorded
observer-capture filename, it validates the actual probe and reports
expected-body FCS separately from native receive count/outcome. It never
qualifies delivery. The standalone Make target is `copy-scope-test`.

Twenty-four RF trials establish canonical-prefix lengths of 160 bytes
with streaming on, 256 after a temporary pause/re-enable, and a complete
**298-byte, FCS-valid SHM continuation** when held quiet, repeated in both
roles. The native path still rejects all four complete held bodies with
count 176; none reaches normal advanced F2 delivery. All eight short
controls skip the probe and retain exact pairs, six with all controls.
These remain diagnostic-only profiles with runner exit 1.

This is a causal streaming-ownership result, not a fixed 32-byte copy
capacity or D2 completion. Native count/FCS lifecycle and safe FIFO
publication remain unresolved. See the
[focused microscope evidence](../dev-doc/HT20_IMPLEMENTATION.md#focused-copy-microscope-and-current-epoch-stream-ownership-2026-10-20).

### Nonterminal guard-body and copy-end controls

`reference-handoff-guard` enables the default-disabled
`CLEAN_RX_HT_HANDOFF_GUARD=1` source/observer geometry. Version 8 appends
two alignment bytes, a valid delimiter and an 80-byte, FCS-valid guard
MPDU to the original pair, giving an actual 564-byte PSDU. The first and
second MPDU bytes are unchanged for the same nonce/index. The guard has
its own sequence and body marker. HT-SIG, command admission, source
readback, observer length gate and collector all use the new geometry;
version 7 remains the ordinary two-body fixture.

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-guard
bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128 good
```

This tests a second body before the PPDU ends, not three-body delivery.
The observer still advances only once. The harness permits only `good`:
the two-body `bad-last` and physical-truncation labels do not describe a
pair followed by a guard. Exact raw version-8 fault fixtures are covered
by native tests, not qualified as RF negative cases. Both role/order
cells still abort before EOF; moving EOF later does not fix the failure.

`reference-handoff-end6` instead keeps the version-7 source byte-identical
and enables `CLEAN_RX_HT_COPY_END_PLUS6=1`. It adds six to the saved
second-body length when writing RXE copy end, testing the PLCP-count
boundary hypothesis with one changed PSM instruction. It requires the
copy probe and excludes the rejected wait. It also fails in both
roles/orders and must not be used as a receive correction. One trial
reports count 310, outside the unchanged 306-byte snapshot ceiling;
its full continuation sample is deliberately not committed.

`reference-handoff-quiet` keeps the same version-7 source and adds
`CLEAN_RX_HT_COPY_QUIET=1`. After retaining the first body, it clears only
RX control's existing low two-bit streaming field before EOI, records
the immediate before/after readbacks and leaves the ordinary 128-byte
streaming threshold in place. It requires the non-wait, unmodified-end
copy probe; no flush or RXE/PHY reset is introduced. This tests a boundary
between the previously working sub-128-byte fixed diagnostic and these
larger bodies. All four RF trials verify `0x802 -> 0x800`, but the second
body still fails. It is not a handoff fix or a general FIFO exclusion.

The copy probe now snapshots eligible explicit aborts before the normal
exit cleanup, preserving outcome 5, owner/epoch, count and status.
It does not publish aborted data or convert it into bad-FCS/valid-frame
evidence. `terminal_sample_committed` distinguishes an actual terminal
sample from an armed-only probe; `copy_end_plus6_enabled` identifies
the length control. `stream_quiet_enabled` and
`stream_control_before`/`stream_control_after` report the separate
stream-field experiment. Its readbacks occupy probe words 42/43;
the probe is now 44 words within the unchanged early SHM export.
Mask zero without a committed sample is not overwrite evidence.

See the [terminal-boundary campaign](../dev-doc/HT20_IMPLEMENTATION.md#nonterminal-second-body-and-copy-end-boundary-controls-2026-10-20)
for the 20 retained RF trials, USB-only exclusions and complete
restoration checks. No complete pair or D2 handoff qualification resulted.

### Copy-progress probe and rejected wait control

`reference-handoff-copy` adds `CLEAN_RX_HT_COPY_PROBE=1`, default off
and requiring the passive one-shot advance. It uses the same source and
runner arguments as `reference-handoff`. After retaining the complete
first body, it seeds/readback-checks four `0xa55a` words at MPDU byte
offsets 8, 32, 64 and 128, then performs the original EOI/rearm.
It records terminal RXE registers, those four words and an independent
24-byte prefix before taking the existing full continuation snapshot.
This changes the first live buffer and adds pre-EOI instructions; use
baseline controls, not a claim of nonintrusive general receive support.

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-copy
bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128 good
```

`handoff-copy-probe` reports owner/epoch, seed readback, phase and
`retained_canary_mask`. Mask zero means all four locations changed,
**not** that the expected second body arrived. `terminal_prefix_stable`
compares the independently sampled 24 bytes with the later continuation
snapshot. It is not a complete-body/FCS check.

`reference-handoff-wait` additionally enables the default-disabled
`CLEAN_RX_HT_COPY_WAIT` control. After EOI, it waits for a count of at least
six bytes smaller than the retained first count, with a finite 16-bit
countdown and explicit EOF/abort/timeout failure. Failure records SHM
reason 9 and faults the controller; no restart or stronger reset is tried.
**This control failed in both roles/orders and is not a recovery fix:**
the first count remained latched until EOF without the original rearm.

Copy-probe fault handling pauses PSM and uses a separate diagnostic loader
to export SHM/mailbox state before power-off, preserving the original
receive error. The host emits `handoff-copy-fault` even though phase
completion still fails. Collection success/failure and its own error are
separate fields; neither a stopped fault snapshot nor queued captures
constitute a successful stop/drain or normal delivery qualification.

Observer ABI 21's existing 512-word early SHM export contains the probe at
word `0x5ca`: owner, epoch, phase and version, four seed readbacks,
12 terminal register words, four terminal samples, 12 prefix words and
six wait fields. Phase 1 is armed, 3 is a committed terminal snapshot,
5 is explicit wait failure; version 1 is the probe, 2 is the wait control.
The RP-only `FCP1` fault record occupies trace bytes 5792..5823, beyond
that export. It does not change normal F2 or station receive ABI.
See the [copy-progress campaign](../dev-doc/HT20_IMPLEMENTATION.md#second-body-overwrite-probe-and-rejected-fresh-count-wait).

## Build and run

From the repository root:

```sh
make -f clean-rx/vendor-burst/Makefile reference reference-test
bash clean-rx/tools/vendor-burst/run.sh VENDOR_SERIAL OBSERVER_SERIAL \
    build/clean-rx/vendor-burst/NEW_TRIAL_PREFIX
```

Both serial-matched badges must be in BOOTSEL. The optional fourth argument
selects a private credential file; the default is `ssid-data/ssid.txt`, with
`SSID:` and `Pass:` fields. Credentials are provisioned over USB into RAM,
wiped after use and never included in image assets, reports or command lines.
An ASCII WPA2 passphrase of 8..63 bytes is required.

For early-body qualification without the next-body advance, use:

```sh
make -f clean-rx/vendor-burst/Makefile reference-early
bash clean-rx/tools/vendor-burst/run.sh VENDOR_SERIAL OBSERVER_SERIAL \
    build/clean-rx/vendor-burst/NEW_EARLY_PREFIX ssid-data/ssid.txt 256
```

The optional fifth argument is `mixed` (default), `128`, `256` or `512`.
Constant-size vendor/AP profiles require an early-only profile; the collector
rejects them in the ordinary advance build. The controlled handoff profile
above is the explicit exception. The shared output directory holds one profile at
a time: do not rebuild it while a campaign is loading its images.
`reference-phy` additionally enables stopped Cortex-M3 PHY-control snapshots;
the probe remains disabled in the ordinary early-only and advance builds.

For a passive receive timeline, use `reference-window`. For the deliberately
strict flush-timeout control, use `reference-rearm`. Both accept the same
constant-size runner arguments. The latter waits up to 65,535 PSM polls,
checks RXCTL bits 2/4 before draining stale events, and reports a PSM fault
instead of automatically restarting RX. **It is a diagnostic, not a qualified
recovery fix.** The original strict control falsely faulted when GoodFCS
was absent; the corrected control has an explicit resume branch and
event-aware native tests. See the correction below.
Both gates default off, and the window and PHY-snapshot profiles are mutually
exclusive.

`reference-recover` enables an experimental **one-shot** event-drain/reflush
after a strict busy timeout. It preserves the first fault, acknowledges only
ext3/5/6/11/13/14 and retries the original flush once. A subsequent timeout
faults explicitly; there is no PSM restart or counter reset.
`reference-recover-test` additionally injects a busy bit into the first
latched decision operand, **not hardware RXCTL**, to exercise the branch.
Injected mode is labelled in both telemetry and the screen; it is not evidence
of a natural hardware timeout or successful physical recovery.
Both recovery gates default off and require the strict passive profile.

`reference-reset` adds the default-disabled `CLEAN_RX_HT_REARM_RESET`
variant; `reference-reset-test` also injects the first latched busy decision.
After preserving the first fault and draining events, the PSM requests
one C3 PHY reset and waits at most 65,535 bounded reply iterations.
C3 pauses PSM execution, rechecks the request and four TX-inhibit controls,
and applies the existing six-operation startup receive-reset sequence.
The PSM resumes only after successful PHY/control readbacks; rejected
requests and reply timeouts fail explicitly (SHM reasons 8 and 7).
The subsequent original RXE flush still has its own strict bound.
No core/PSM restart, diagnostic reset or normal-profile change is included.

`reference-park` selects a default-disabled `CLEAN_RX_HT_REARM_PARK`
control using the same one-shot handoff, guards and strict retry. C3 parks
the PSM for at least 16 TSF units, bounded by 65,535 polls, without executing
any PHY-reset writes. Completion requires unchanged PHY readbacks and TX
inhibition; actual reset count remains zero. The measured complete service
takes 29..30 TSF units, versus 16 for the reset trials: this is a separate
pause/delay control, **not an exactly time-matched experiment**.

### Fixed two-body stimulus

The default-disabled fixed-source profiles remove dependence on vendor
queue timing:

```sh
make -f clean-rx/vendor-burst/Makefile reference-stimulus
bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 512
```

`reference-stimulus-reset` enables natural one-shot PHY recovery;
`reference-stimulus-reset-test` injects the first latched busy decision.
`reference-stimulus-park-test` instead exercises the injected park-only
control. All profiles require the explicit `512` runner argument and use
channel 6/MCS0. They do not associate or read/provision AP credentials;
the fourth argument is only a positional placeholder.

The source badge runs a separate clean controller with
`CLEAN_RX_HT_STIMULUS=1`, `CLEAN_RX_HT_DATA=1` and extended RX. It sends
eight cycles of a 554-byte legacy OFDM frame, a 554-byte single HT frame,
and a fixed two-by-554-byte HT aggregate, with 40-ms calibrated gaps.
There are exactly 24 single-attempt transmissions, no controller ACK wait
or retries. These are broadcast-addressed, unprotected raw PHY fixtures,
not a negotiated unicast A-MPDU exchange. The aggregate has PSDU length
1118: two four-byte delimiters,
two complete MPDUs and two padding bytes after the first body.
Nonce-derived source addresses, per-frame sequence numbers, deterministic
body bytes and independent FCS values bind the whole experiment.
These fixtures are **unprotected**, unlike the vendor/AP protected frames.

Command-12 version 6 accepts only that exact finite aggregate fixture.
Other lengths, bytes, extra flags, station mode and disabled gates are
rejected. Version 1 and the existing legacy path carry the controls.
The sender verifies SRAM staging; the controller verifies template and HT
controls and requires all three completion conditions with no PHY error.
Each transmission exports a 64-byte record at trace `128 + 64*index`:
index/command, raw RP start/end ticks, TSF trigger/completion, PHY error,
completion conditions, logical template length, whole-descriptor CRC,
TX diagnostic magic, six PLCP bytes, four sampled TXE controls and channel.
The complete last aggregate descriptor is also retained at trace 4096.
The sampled TXE `0x56a` is exported raw, not interpreted as the logical
template length or used to infer aggregation.

The observer compares every complete control frame against the exact
fixture, including FCS and passive-observer metadata. Trace words 40/44/48
hold a received-index bitmap, duplicate count and invalid-control count.
The first complete legacy wrapper/body is retained at trace 3664, outside
the ordinary counted HT sample. The collector independently regenerates
the complete early body, the saved legacy body and all sender descriptors.
It distinguishes missing single-HT prefixes from prefixes without F2
delivery. Prefix-capacity loss explicitly disqualifies absence inferences.
Ordinary HT metadata remains deliberately unsupported/unadvertised in this
passive observer; it is not the negotiated version-1 station metadata.

Success requires one exact nonce-bound nonterminal 554-byte early body,
all sixteen complete controls without corrupt/duplicate delivery, and
accounted prefix exits. Failed trials remain failures even when their
early body is valid. Sender completion alone is not an OTA proof: the
independent receiver's HT-SIG, complete FCS-valid body and nonterminal
geometry establish the actual multi-body opportunity. There is no third
receiver, no per-attempt independent RF denominator and no qualification
of the second body's reception or protected AP interoperability.

The [fixed-stimulus campaign](../dev-doc/HT20_IMPLEMENTATION.md#fixed-stimulus-early-capture-qualification-2026-10-19)
records 24/24 corrected trials with an exact early body, 13/24 passing the
stricter all-control gate, and continuing natural RXE recovery failures.

### Original-destination quiet/replay discriminator

The default-off matched profiles hold streaming quiet for the committed
current epoch, without displacing the copy destination:

```sh
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/quiet-control reference-handoff-scope-no-replay
make -f clean-rx/vendor-burst/Makefile \
  REF_BUILD=build/clean-rx/replay-control reference-handoff-scope-replay
CLEAN_RX_REFERENCE_BUILD=build/clean-rx/quiet-control \
  bash clean-rx/tools/vendor-burst/run.sh TX_SERIAL RX_SERIAL NEW_PREFIX - 128
```

Use a fresh evidence prefix per run; select the other archived build for
replay. Argument `128` sends first/continuation bodies of 170/298 bytes;
`256` reverses them as a short-continuation/no-stall control. Neither
profile needs credentials or qualifies negotiated HT.

Both use scope 3/rearm 5 and differ by one emitted PSM instruction:
reissue the existing copy command or jump to the next snapshot.
`CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY=1` is allowed only for this original-base,
epoch-held scope. Scope JSON explicitly reports `replay_suppressed` and
`destination_shift_words`; existing shifted scope profiles are unchanged.

Twelve RF trials show quiet-only retaining 128 canonical continuation
bytes and ending count 134/watchdog, whereas replay copies the full
170-byte remaining tail over the original destination, rewinds live
count to 58 and ends count 176/bad FCS. All four short controls deliver
exact pairs, but only one receives every ordinary control. No long
continuation reaches normal F2; native admission is not waived.
See the [matched evidence and collector correction](../dev-doc/HT20_IMPLEMENTATION.md#quiet-only-versus-copy-replay-at-the-original-destination-2026-10-20).

### Compound replay field and native-length quarantine

These default-off targets isolate the already asserted register bits;
they are diagnostics, not alternate station profiles:

```sh
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-no-bit6
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-no-bit7
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-no-bit13
make -f clean-rx/vendor-burst/Makefile reference-handoff-scope-no-bit7-shift
```

Use a separate `REF_BUILD` directory per target and select that archive
with `CLEAN_RX_REFERENCE_BUILD` when running `run.sh`. Arguments remain
`TX_SERIAL RX_SERIAL NEW_PREFIX - 128`; `256` reverses body sizes.
The optional case argument is a **name**, such as `bad-last`, not a number.
No credentials are used. The source remains the unchanged version-7
fixture; only the first aggregate carries the selected corruption.

`CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD` defaults to `0x83` and permits
only `0x82`, `0x81` or `0x03` as omissions of one asserted command bit.
Modified fields require epoch-held scope 3/rearm 5. Only `0x81` also
permits shifted rearm 6. Replay suppression and modified fields cannot
be combined. Initial PLCP/body/MPDU-start commands are unchanged.
Scope JSON exports `replay_command_field`, `native_length_guard`,
`replay_suppressed` and `destination_shift_words`.

Omitting bit 6 stalls; omitting bit 13 still restarts count and fails
FCS. Omitting bit 7 preserves native GoodFCS, while deliberately bad
second bodies still fail. With the 64-word destination shift, complete
298-byte bodies have both native GoodFCS and correct software FCS.
Native count nevertheless remains 176 after replay; it is not rewritten.

The new profiles therefore guard ordinary FIFO admission after the
immutable snapshot. A matching retained owner/epoch with native success
but inconsistent complete-body length produces explicit length outcome
4 and a length-discard increment, not an incorrectly sized F2 packet.
Snapshot outcome 1/status `0x4100` still records native FCS success.
Unrelated frames and exact short continuations retain ordinary behavior.
No normal full-body F2 repair or D2 qualification is claimed. See the
[field, corruption and guarded RF evidence](../dev-doc/HT20_IMPLEMENTATION.md#compound-replay-command-fcs-state-and-copy-local-length-2026-10-20).

### Fixed-source vendor raw control

To bypass the clean receiver with the unchanged version-6 554-byte source:

```sh
make -f clean-rx/vendor-burst/Makefile reference-stimulus-monitor
make -f clean-rx/vendor-burst/Makefile reference-stimulus-monitor-poll
make -f clean-rx/vendor-burst/Makefile reference-stimulus-monitor-poll-fast
```

Use separate `REF_BUILD` directories, select one with
`CLEAN_RX_REFERENCE_BUILD`, and invoke the runner with `- 512`.
No credentials are needed. The latter two are explicitly gated read-only
get-monitor servicing discriminators, at 100/10 ms; they are not fixes
to the vendor driver or the clean firmware.

This monitor retains 32 complete nonce-bound raw packets, with explicit
overflow/oversize accounting. After stop, phases 5..8 write
`PREFIX-observer-raw-page-0.bin` through page 3. Each full report has a
16-byte header at trace 128: magic `0x3141524d` (`MRA1`), zero-based page,
total retained and page count. Up to eight existing 656-byte packet slots
follow; unused slots/padding are zero. The ordinary capture still includes
the first eight packets. Fixed-source success requires all sixteen
controls and both complete bodies from every aggregate, with exact FCS
bytes, no duplicates/invalid bodies and no selection loss.

Post-fault pages preserve the original error and cannot turn a failed
trial into a successful one. The dedicated `monitor_test` executes the
actual lifecycle/timeout/page path under ASan/UBSan; existing protocol
tests cover all 0..32 page geometries and corrupted packet/header/padding
evidence. The older `reference-handoff-monitor` remains eight-slot.

**Not a qualified loss baseline:** eight unpolled RF trials all stop at
exactly 30 raw packets. Two 100-ms trials extend this to 41/42 but still
miss expected bodies; a 10-ms test times out. Therefore these captures
cannot attribute clean pre-prefix misses to RF interference or prove
absence over the complete source sequence. See the
[baseline evidence and exclusions](../dev-doc/HT20_IMPLEMENTATION.md#fixed-source-vendor-baseline-and-pre-prefix-discriminator-2026-10-20).

### Repeated early FIFO delivery

For complete first-body delivery without next-body advance:

```sh
make -f clean-rx/vendor-burst/Makefile reference-early-fifo
make -f clean-rx/vendor-burst/Makefile reference-early-fifo-reset-test
make -f clean-rx/vendor-burst/Makefile reference-early-fifo-stop-test
```

Use a separate `REF_BUILD` directory per profile and set
`CLEAN_RX_REFERENCE_BUILD` to that directory when invoking `run.sh`.
The reset target injects one bounded PHY-reset test; the stop target
pauses host consumption until stop to exercise full-credit ownership.
Both are diagnostics, not normal reception qualification.

`CLEAN_RX_HT_EARLY_FIFO` requires early save and the passive observer FIFO,
and excludes advance. Its 14-word retained header adds native PHY status
and a publication acknowledgement; the maximum retained body is 996 bytes.
C3 publishes only after native nonterminal GoodFCS, prefix/HT-SIG/epoch,
transmitter, geometry and complete software-FCS validation. PSM recycles
only an acknowledged owner for a different epoch. Exhausted FIFO credit
preserves the unacknowledged snapshot.

F2 correlation tag `0x4000` distinguishes early packets from existing
`0x8000` advanced packets. Their channel-6 metadata is `0x46`, derived
from validated HT-SIG, not the pre-terminal native PHY-status word.
Native diagnostic words and native rejection gates remain unchanged.
Trace 5824/5828/5832 stores accepted early count, source bitmap and
duplicate count. The finite source expects all eight indices
2/5/8/11/14/17/20/23, all sixteen ordinary controls and no invalids or
duplicates. Partial reception fails that gate.

After stop, phase 5 writes `PREFIX-observer-early-packets.bin`. At trace
128 its `ERF1` header has four little-endian 32-bit words: magic
`0x31465245`, stored count, total tagged packet count and overflow.
Eight 584-byte slots follow: full packet length, logical F2 ordinal,
parsed source index, and up to 572 complete SDPCM/BDC/MPDU bytes.
Unused slots are zero; invalid parsed source uses sentinel 24.
Validation checks full envelopes, sequence/ordinal, tags, metadata,
fresh fixture/FCS, uniqueness and receipt accounting. A valid partial
archive does not mean all eight source bodies arrived.

The focused `early-rx-test` target tests all supported body lengths and
corrupted admission/sample/archive evidence under ASan/UBSan. Final
both-role RF tests delivered 46/48 first bodies normally and 14/16 with
injected reset; only three of six final normal trials also received all
controls. Four non-stress missing bodies coincide with four absent
aggregate-prefix observations, not extra observed good candidates lost
by the FIFO path. This does not identify their RF cause. Both final
full-credit stop tests preserve queued ownership and drain exactly;
later RF losses remain expected stress failures.

Historical counter-layout and early PHY-classification failures are
retained. Collector output keeps `reliability_qualified:false` and
`ht_station_qualified:false`, including finite fixture passes. The
sixteen-prefix observer limit and fixed MCS0/554-byte source do not
qualify sustained/variable aggregate reception. See the
[complete campaign and audit](../dev-doc/HT20_IMPLEMENTATION.md#repeated-early-mpdu-fifo-delivery-and-complete-packet-archives-2026-10-20).

The screen shows badge role, operation and experimental mode before radio
start, then stopped/error status, capture count and first-fault RXCTL after
observer stop. There are no display writes during active observer capture,
no framebuffer and no added display communication with the Wi-Fi chip.
Green `RADIO STOPPED` is an operational result, not early-body qualification.

**Back up both entire flashes first. The runner replaces firmware and does
not back up or restore originals.** Use a fresh prefix; partial/failed trials
are retained. Raw evidence files are opened exclusively, not overwritten.
The collector asks both devices to return to BOOTSEL, including after an
ordinary failure. An absolute device watchdog bounds abandoned sessions;
the host also has a USB deadline.

The staged radio handoff is deliberate:

1. Associate and authorize the vendor station; discover the actual channel.
2. Load, arm and explicitly start the clean receiver; require `RX_ACTIVE`.
3. Snapshot vendor counters, submit 128 frames and drain for 1.2 seconds;
   snapshot counters again and stop the vendor radio.
4. Issue the clean controller's stop command and require `RX_DONE`.
   Then read the immutable PSM log, mailbox and early-HT diagnostics.

Stopping the PSM alone is insufficient for readout: the running Cortex-M3
also uses the D11 object selector. Controller stop acknowledgement avoids
cross-core selector races before shared-memory reads.

Window profiles add phase 3 immediately before vendor submission to arm
sampling, and phase 4 after receiver stop/radio power-off to export it.
The phase-3 `observer-armed.bin` contains only the 64-byte report.
`observer-window.bin` contains the report plus the usual 5888-byte trace.
Sampling reads cached Cortex-M3 SRAM/mailbox fields and raw PSMDEBUG, not
live D11 object/PHY selectors. A failed capture still exports diagnostic
data and returns failure; it is not counted as a successful capture.

## Fixture and evidence

The burst contains 32 frames of each Ethernet length: 128, 256, 512 and
1024 bytes. Each 16-frame group contains four 1024-byte frames, four
128/256-byte pairs, then four 512-byte frames. Eight groups keep the total
at 128 frames. Large frames aim to build a queue before the small pairs;
the requested vendor `ampdu_mpdu` limit is two. Neither submission order
nor that limit proves actual aggregation. The earlier campaigns below used
the rotating four-size pattern and a limit of four.
It uses experimental EtherType `0x88b5`, unicast to the AP,
with a nonce-derived local MAC, `HTREF001`, sequence number, challenge,
deterministic payload and trailing CRC32. Submission is bounded to six
seconds. A rolling digest of pre-trailer CRCs detects fixture changes;
CRC32 over a message plus its own CRC would instead have a constant residue.
This is not a broadcast/IP flood or continuous RF transmission.

The focused profiles submit 128 fixtures of one Ethernet size, corresponding
to 170-, 298- or 554-byte protected MPDUs in the measured AP exchange.
Nonce byte 11 selects mixed/128/256/512 as 0/1/2/3; bytes 0..10 remain
random. The profile is included in the fixture and session binding.
Only the first eight focused fixtures and their rolling prefix digests are
prepared before submission: 8224 bytes of vendor staging, not a whole-burst
cache. Mixed mode keeps generating frames in the original order.
The advance build excludes this staging from its linked BSS.
Timing reports retain per-write buckets, first-eight/next-eight submission
intervals and preparation time; shorter host intervals alone do not prove
on-air multi-body aggregation.

Each raw phase file except the window arm acknowledgement contains a
64-byte build/nonce-bound, CRC-checked report and 5888-byte trace.
Trace pages are independently bound to that report.
The collector writes JSONL, stderr and loading/readiness logs alongside them.
Readiness logs append every numbered attempt, including failures, rather
than replacing earlier attempts when a later probe succeeds.
Device-reported submission is not treated as OTA payload verification.

### Vendor queries

The measured firmware supports:

- `nmode`, `nrate`, `2g_rate`, `mimo_bw_cap`;
- `ampdu`, `ampdu_tx`, `ampdu_rx`, `ampdu_mpdu`, `ampdu_ba_wsize`;
- `amsdu` GET, `wme`, `chanspec`, `sta_info`, and `counters`.

`amsdu` SET, `ampdu_counters` GET and `ampdu_dump` GET return controller
status `-23` on this firmware. The unsupported SET was removed; optional
unsupported GETs remain explicit records, not zero-valued successes.
The final command status may therefore be `-23` while the experiment itself
completed successfully.

The observed `counters` structure is version 10, length 848. The decoder
rejects other layouts and retains raw replies. Offsets were checked against
the complete public `wl_cnt_ver_ten_t` layout, including six `rxuflo` entries:

| Counter | Byte offset | Interpretation |
| --- | ---: | --- |
| `txframe` | 4 | Driver data-frame count, not aggregation proof |
| `txerror` | 16 | Driver TX errors |
| `txallfrm` | 196 | MAC transmitted frames, including control traffic |
| `rxback` | 416 | Received Block ACKs |
| `txfail` | 432 | MIB TX failures |
| `txretry` | 436 | MIB TX retries |
| `txfrmsnt` | 472 | MIB transmitted-frame count |

Reference: [public Infineon WHD ioctl header](https://github.com/Infineon/wifi-host-driver/blob/master/WHD/COMPONENT_WIFI6/src/include/whd_wlioctl.h).
No public vendor implementation was copied into the clean firmware.

### Passive lifecycle logger and correlation ABI

`CLEAN_RX_HT_OBSERVER=1` requires extended HT RX, excludes the aggregate
fixture/interface/delivery profiles and defaults off. It retains at most 16 prefixes
in SHM `0x100`; overflow is counted and retained entries never overwritten.
The observer trace has ABI 21 at byte 64. The 16-word SHM header holds session
identity, permit/count/overflow, pending/held IDs, RX epoch, progress and the
first good-only hint's RX count. Each 47-word record has an append-only
prefix and a one-shot terminal/exit snapshot:

| Record words | Contents |
| --- | --- |
| 0..9 | Prefix TSF, raw PHY/RXE status, HT-SIG and working-register value |
| 10 | Prefix RX epoch |
| 11 | Outcome: 0 open, 1 good, 2 bad FCS, 3 mismatch/short, 4 length rejection, 5 abort/fault, 6 watchdog, 7 new RX, 8 new prefix |
| 12..17 | Terminal TSF, RXE length, PHY and status |
| 18..37 | First 40 MAC/CCMP/ciphertext bytes, only after terminal checks |
| 38..39 | Terminal epoch and record ID |
| 40 | Observed progress: 0 no body wait, 1 body wait, 2 event 6, 3 event 6 with good-only status1 bit 8, 4 observed status1 bit 14 |
| 41..45 | Raw SPR `0x1004`, `0x1002`, `0x102a`, `0x102b`, `0x10af` |
| 46 | RX count at the first event-6/good-only-bit-8 observation; zero if absent |

ABI 8 adds frozen receive/delivery diagnostics in previously unused trace
space, without changing PSM logging, radio configuration or the trace size:

| Trace bytes | Contents |
| --- | --- |
| 80..103 | ABI 13: stopped before-RX ChipCommon ID, ChipCommon/D11 clock control, PMU status/resources/pending |
| 104..127 | ABI 13: the same six raw words after controller stop |
| 128..255 | Existing first 128 bytes of the controller mailbox |
| 256..291 | Existing early-HT controller diagnostics |
| 292..319 | ABI 14: stopped raw D11 PSMDEBUG, followed by actual SHM `0x600..0x60b` |
| 320..671 | Remaining 352 bytes of the complete 480-byte mailbox |
| 672..711 | Capture target/count, discard counters and final DMA status |
| 712..743 | Host consumed count, RX status, resync/stall/retry/credit state, idle polls and summary count |
| 744..871 | First two ordinary F2 summaries: length, PHY metadata, computed/received FCS, at most 40 MAC bytes and ordinal |
| 872..903 | ABI 15: opt-in PHY-control commit/phase header and six before/after 16-bit values; zero when disabled |

All controller memory is read only after `RX_DONE`. The two ordinary
summaries are independent of vendor attribution, preserve short ACK/control
frames, and add no per-frame CRC work after the first two deliveries.
The collector validates the one-frame credit bound and reconciles controller
consumption, host consumption and ordinary F2 count. Its delivery-state
JSON distinguishes controller discard, transport drop/pending and host
resync/stall counters; the 16-bit posted/dropped counters saturate.
Ordinary summaries are diagnostics, not new admission or aggregate proofs.
Only selected header/metadata fields, not SSID or payload bytes, are printed.

Clock/PSM endpoints are read only while the controller/PSM is stopped or
paused, without PHY/radio selector access. The opt-in Cortex-M3 PHY probe
uses existing 16-bit selector/read helpers before RX enable and after
MAC/PSM disable, never a packed 32-bit PHY configuration write.
There is no timing-critical receive-path sampling or PHY/radio tuning
change. Raw PSMDEBUG is not masked into an
assumed PC. The separate actual SHM snapshot matters because the controller
normalizes two words of its mailbox's historical PSM snapshot after capture.
Calibration JSON reports existing RC/VCO/DC completion words; those values
and equal clock/PMU endpoints do not certify analog rail, oscillator or RF
quality, or exclude transient receive inhibition.

ABI 12 additionally uses bytes 5604..5683, after the continuation's raw
window, for a three-point handoff trace. Its four-word header holds committed
prefix ID, epoch, phase mask and reserved zero. The mask advances through
1/3/7 as these 12-word records become valid:

1. Before retaining the first multi-MPDU candidate.
2. Immediately before acknowledging event 6 for an admitted advance.
3. Immediately after writing the next-body copy control.

Each record contains TSF low/high, RX count, status1/status2, copy control,
RX base, copy end, raw next length and raw SPRs `0x1002`, `0x1024`, `0x1078`.
The collector prints committed phases only and reports whether the owner
matches the retained early body. These are sequential register observations,
not an atomic PHY snapshot or an EOF-to-air measurement. Instrumentation adds
instructions in the diagnostic handoff path; it is a state-transition
control, not a claimed latency or reception improvement.

Every RX start logs any pending exit before clearing ownership and advancing
the epoch. Every new prefix, including legacy, similarly records the boundary
before invalidating old ownership and classifying the new prefix.
An HT prefix gets a bounded record ID. At the later good/bad terminal hook,
the logger requires the same epoch, all six signature bytes and sufficient
length before copying any MAC bytes. Mismatches/short frames are explicit
rejections. Bad frames cannot acquire a delivery ID. Completed snapshots
are not rewritten by duplicate terminal events.

Length rejection, short completion, the existing abort/fault branch and
watchdog expiry snapshot the pending record before returning to their
unchanged original continuation. They never grant a delivery ID. The
completion logger alone observes event 6 and status bits without acknowledging
early events, advancing the receiver or transmitting. The separately gated
[bounded advance](#bounded-next-body-advance) may acknowledge and advance once,
but never transmits. Bit 8 is a good-only hint,
not a general subframe-ready indication. Progress describes observations,
not proof of physical EOF or its absence: a normal good completion can
follow a recorded phase 3 without an intervening logged phase 4.

The logger runs after the established HT-prefix copy. The earlier PC153
handoff was experimentally unsuitable: incoming PHY status could indicate HT
while shared-memory prefix and MAC bytes still belonged to an earlier RTS.
The final logger does not copy or attribute those stale MAC bytes. Likewise,
PHY format bits need not yet indicate HT at the actual prefix-copy point.
The host independently checks HT20/MCS0..7, reserved/tail bits and HT-SIG CRC8,
then inspects the HT-SIG aggregation bit.

The good terminal's held ID is propagated in reserved SDPCM bytes 10..11
of the existing F2 publication, only in this diagnostic build. Ordinary
profiles still write zero there. The PSM remains held until publication,
and fragmented packets retain the same ID. No extra live backplane polling
or unsafe concurrent SHM readout is needed.

The RP2350 retains full ciphertext/FCS samples for at most two distinct
vendor-TA MPDU lengths, replacing a same-size candidate with the latest
tagged candidate. Each 1104-byte slot holds a 16-byte descriptor and up to
1088 MPDU bytes. The sample pool starts at trace byte 2560; the SHM log ends
at byte 2560, exactly before the sample pool; the two ordinary samples end
at byte 4768. A separate 1024-byte frozen diagnostic block occupies bytes
4768..5791; the advance profile splits it into first/continuation snapshots.
Compile-time checks enforce these boundaries. Oversize candidates
are counted explicitly. Full-F2 counts are retained even when a sample is
not saved.

The host independently requires:

- A bounded ID referring to an actual retained prefix;
- Valid HT-SIG CRC/profile and a PSDU length consistent with the MPDU;
- Good terminal outcome, matching prefix/terminal epochs and record ID;
- Exact agreement between the terminal's 40-byte snapshot and the F2 bytes;
- The fresh nonce-derived vendor TA and a complete valid MPDU FCS;
- Exact terminal RXE length equal to delivered MPDU length plus six.

Only a fully checked ordinary or early sample makes its prefix
`transmitter_attributed:true`.
Other records remain unattributed, even when their MAC snapshot looks plausible.
The collector passes only with vendor received-BA activity, two different
fully correlated MPDU lengths and at least one correlated A-MPDU-format
reception. This is not timestamp-only or stale-header correlation.
The current early-snapshot experiment additionally requires a fully
correlated early sample; ordinary-reference success alone is insufficient.
The focused constant-size profile instead requires a complete, independently
FCS-valid, fresh nonterminal **multi-body** early candidate of exactly the
requested size, vendor BA activity and accounted exits. It does not require
two unrelated ordinary F2 lengths or a second-body advance.
Valid prefixes without an exit must still be the pending prefix at host
stop; unexplained lost ownership fails qualification. JSON includes named
exit reasons, terminal status/TSF, raw registers and first-hint RX counts.

An A-MPDU-format PPDU can contain a single MPDU. Correlation of one complete
MPDU does not prove reception of multiple bodies from that PPDU.
Per-header `early_capture_reason` distinguishes terminal-only single bodies,
single-body good hints, unsupported profiles, storage limits, inconsistent
body geometry, retained multi candidates and later candidates intentionally
ignored by immutable-first-multi retention. `eligible_not_retained` identifies
a logged candidate needing investigation, not a proven RF loss. The finite
16-record log and its overflow count are not an air-traffic census.

## Initial uncorrelated reference result

Final RF build `fd5025e1`, actual AP channel 6, both physical role assignments:

| Vendor / observer | Submitted | Delta `rxback` | Valid HT prefixes | A-MPDU-format prefixes | Ordinary F2 frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| A / B | 128 | 103 | 7 | 6 | 348 |
| B / A | 128 | 97 | 5 | 5 | 571 |

A is `08DA26F86E57F5EA`; B is `9BBF88DD17D66E7D`.
Both runs were authorized and HT-capable, with zero `txerror`, `txfail`
and `txretry` deltas, no prefix overflow and clean observer TX disabled.
`txfrmsnt` increased by 130 in each run; `txallfrm` increased by 238/219.
The fixture submitted 32 of each requested length in both runs.
Rate readback was `0xc1010000`, and captured valid headers were MCS0.
Final evidence prefixes:

- `build/clean-rx/vendor-burst/final-ab`
- `build/clean-rx/vendor-burst/final-ba`

Thus the reference demonstrates actual vendor/AP Block ACK activity and
independent on-channel A-MPDU-format HT headers, not just enabled flags or
submitted bursts. The original collector required both positive observations to pass.
It does **not** claim RF transmitter attribution, decrypted fixture payloads,
complete multi-MPDU reception, production clean BA or EOF-to-air SIFS timing.
One reverse prefix advertises a 478-byte PSDU, consistent with aggregation
of small fixtures, but its MPDU bodies were not captured/validated.

Earlier failed evidence is retained as `trial-ab-r0` through `trial-ab-r5`:
unsupported `amsdu` SET; receiver left at `PSM_READY`; shared object-selector
readout race; stale controller build from a missing header dependency; and
the stale PC153 prefix/MAC assumption. Those issues were corrected rather
than attributed to unexplained RF loss. Successful precursor runs are
`trial-ab-r6` and `trial-ba-r0`. The PSM make rules now track the observer
header for both target objects and native tests.

## Fresh-prefix/MAC/F2 result

Final correlation RF build **`4e209642`**, MCS0, actual AP channel 6:

| Vendor / observer | Fully checked MPDU lengths | Correlated A-MPDU-format samples | Received-BA delta | Matching vendor F2 frames |
| --- | --- | ---: | ---: | ---: |
| A / B | 170, 298, 554 | 1 | 110 | 4 |
| B / A | 170, 298, 1066 | 3 | 112 | 10 |

Each run submitted 128 fixtures, 32 of each Ethernet length. Across the two
roles the four observed MPDU lengths are consistent with those Ethernet
lengths plus 42 bytes of encapsulation/encryption/FCS. This length relation
is not a claim to have decrypted the payload. Every listed sample has its
full captured bytes, valid FCS, matching terminal header and independently
validated prefix/epoch/ID association. Both collectors exit zero.

Evidence:

- `build/clean-rx/vendor-correlate/rotated-ab-r0`
- `build/clean-rx/vendor-correlate/rotated-ba-r0`

The A/B run has six CRC-valid prefixes; four reach good terminal snapshots.
Two larger A-MPDU-format PSDUs, **2110 and 862 bytes**, have fresh valid
prefixes but no observed good/bad terminal commit before subsequent RX.
They remain unattributed and have no invented body delivery. This narrows
the next live-interface investigation to these missing terminal/advance paths;
it does not prove absence of a physical EOF.

The B/A run has eleven prefix candidates, ten with valid CRC. A bad-terminal
candidate with invalid HT-SIG is retained and rejected, not counted as a
correlated reception. Neither run has prefix-log overflow, signature/epoch
mismatches or oversize samples. Vendor TX retry deltas are 3/0; TX error and
failure deltas are zero. Observer TX remains disabled.

Earlier correlation controls are preserved in the same directory:
`trial-{ab,ba}-r0` passed on build `c5b1c57d`; `final-ab`, `qualified-ab-r0`
and `qualified-ab-r1` failed the distinct-length criterion after retaining
only 170-byte MPDUs, despite successful identity/FCS correlation. The
corresponding B/A controls passed. Rotating the stimulus removes its
fixed-first-size bias, but these few controls do not establish causality,
loss-free reception, a receive-corruption fix or a performance qualification.
In particular, 4/10 matching vendor F2 frames from 128 submissions is not
a successful sustained-reception result.

Native ASan/UBSan tests cover new RX without a new prefix, intervening legacy
traffic, each signature word, epoch mismatch, short/bad frames, duplicate
terminal events, sample ID/length/header/FCS corruption and balanced rotated
fixture counts. Default PSM reconstruction identity and non-observer
controller tests still pass.

## Live aggregate exit investigation

The passive logger now accounts for exits instead of treating every absent
good/bad commit as unexplained loss. Initial exit-trace build **`95e7676c`**
(ABI 3, 46-word records) passed both role assignments:

| Evidence suffix under `build/clean-rx/vendor-exits/` | Checked MPDU lengths | Received-BA delta | Valid / A-MPDU prefixes |
| --- | --- | ---: | ---: |
| `trace-ab-r0` | 170, 298 | 112 | 8 / 7 |
| `trace-ba-r0` | 170, 1066, 298 | 96 | 11 / 10 |

In `trace-ba-r0`, two larger CRC-valid A-MPDU-format prefixes have explicit
abort/fault exits, not a normal good/bad terminal commit:

| Prefix ID | PSDU bytes | Prefix-to-exit us | Exit RX count | Observed phase | Status1 / status2 | Raw `0x102a` / `0x102b` |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 2 | 1598 | 1347 | 560 | 3 | `0x9922` / `0x0a43` | 554 / 170 |
| 4 | 1342 | 1016 | 304 | 3 | `0x9922` / `0x0a41` | 298 / 554 |

Both retain raw `0x1004=0x0802`, `0x1002=0x0f40` and `0x10af=0x1000`.
They reached event 6 and the good-only bit-8 hint before the existing
abort/fault path. Exit RX counts are consistent with candidate first
MPDUs plus six bytes, but no complete first-body/FCS snapshot was retained.
These prefixes remain **unattributed**. Raw registers do not independently
establish next-frame ownership, a FIFO/DMA root cause or physical EOF.
Waiting only for terminal status is now a concrete suspect, not a proven
cause of the aborts.

Build **`7f1e3e90`** adds the first-hint RX count and ABI 4. Five subsequent
bounded controls pass:

| Evidence suffix | Checked MPDU lengths | Received-BA delta | Valid / A-MPDU prefixes |
| --- | --- | ---: | ---: |
| `hint-ab-r0` | 170, 298 | 112 | 6 / 6 |
| `hint-ba-r0` | 298, 554, 170 | 113 | 8 / 7 |
| `hint-ab-r1` | 170, 298 | 112 | 5 / 5 |
| `hint-ba-r1` | 170, 298 | 108 | 5 / 4 |
| `hint-ba-r2` | 170, 554 | 124 | 3 / 2 |

All have zero unaccounted exits, overflow and pending-at-stop IDs. A
fully correlated 298-byte MPDU in `hint-ab-r0` records its first good-only
hint at RX count 304. However, these five controls did **not** reproduce
the larger abort cases. Their new first-hint field therefore cannot be
retroactively assigned to the ABI-3 abort observations.

Keep `trace-ab-r1` as a failed control: received-BA delta 116 but **zero**
captured prefixes and no correlated MPDUs. It correctly exits nonzero.
A transient host `picotool info` segmentation fault during readiness
polling was visible before a later successful retry; both subsequent
collectors passed. These results do not establish reception reliability.

The exit-only follow-up build **`d7519182`** additionally routes the
existing sub-14-byte completion rejection through the explicit short-exit
logger, returning to the original PC263. It has not been radio-tested.
Native tests cover this branch, length/abort/watchdog and replacement
exits, immutable completed records, first-hint retention when later counts
change, and rejection of every non-good outcome by full-sample correlation.

No early handoff is enabled. The next controlled step is to preserve a
complete variable-length candidate at the early event, independently check
its FCS and boundaries, and only then test the existing advance primitive
under a bounded default-off diagnostic. Success still requires at least
two distinct complete FCS-valid MPDUs bound to **one fresh PPDU**, not two
prefixes, good-only status hints or received-BA counters. Bad subframes,
whose bit 8 may be clear, must remain part of the handoff design.

## Complete early-event MPDU preservation

Build **`e9008554`** adds `CLEAN_RX_HT_EARLY_SAVE=1`, default off outside
this focused experiment. It copies complete MCS0 A-MPDU-format candidates
without acknowledging the early event, advancing the receiver or enabling
TX. The minimal USB main is unchanged.

The snapshot occupies SHM words `0x400..0x5ff`, immediately after the
prefix records and strictly before the live control/status words at
`0x600`. Its maximum body is **1000 bytes**, not the ordinary receiver's
larger limit. This covers the 170/298/554-byte fixtures but deliberately
excludes 1066-byte MPDUs; excluded candidates are counted with saturation
instead of truncated. Direct, length-guarded PSM word moves avoid an
unverified indirect-memory interface. Native tests cover every length
44..1000, including odd final words and both exact memory boundaries.

| Snapshot words | Contents |
| --- | --- |
| 0 | Commit-last prefix ID; zero means absent or incomplete |
| 1..2 | RX epoch and RX count |
| 3..4 | Hint-time status1 and snapshot status2 |
| 5..6 | Snapshot TSF |
| 7..9 | Retained HT-SIG |
| 10 | Candidate kind: 1 single-MPDU-sized, 2 advertised PSDU exceeds this candidate's padded MPDU plus delimiter |
| 11 | Oversize-candidate counter |
| 12..511 | Up to 1000 complete MPDU bytes, including FCS |

The first retained candidate is immutable unless a later multi-candidate
replaces a single-sized candidate. The first retained multi-candidate is
then immutable. RX count, epoch and all HT-SIG words must remain unchanged
across the copy before its ID is committed. Scratch registers are restored.
Native negatives cover missing ownership/event/hint, legacy/other MCS,
short/oversize frames, stale copies and repeated retention.

The hint's raw status is read at the hint hook, before the longer copy
setup. It temporarily reuses the global outcome word; every terminal or
exit entry assigns its own outcome before consuming that word. This avoids
mixing a late terminal status read with an earlier event observation.
A native race test changes live status to terminal after the hint and
verifies that the retained hint status is unchanged.

The host independently requires bounded identity/length, matching fresh
prefix epoch and all HT-SIG bytes, valid HT-SIG CRC/profile, MCS0 and
aggregation, a consistent candidate-size classification, nonterminal/
nonabort good-only hint status, the nonce-derived vendor TA, data-frame
type, and a valid FCS over the complete saved bytes. A later abort need not
invalidate a previously complete early body. Invalid/incomplete snapshots
never qualify, and the ordinary F2 correlation remains a separate predicate.

### Positive radio result and limits

`build/clean-rx/vendor-early/hint-ba-r0` passes the full collector:

- B runs unchanged vendor firmware; A is strictly passive.
- Prefix **ID 4, epoch 74**, CRC-valid HT-SIG `00de010fb003`, advertises a
  **478-byte** A-MPDU-format PSDU at MCS0.
- The independent early snapshot retains a complete **170-byte MPDU**,
  RX count 176, with valid FCS and the fresh vendor TA.
- Hint status1 is **`0x1922`**: good-only bit 8 set, terminal bit 14 and
  abort bit 15 clear. Snapshot status2 is `0x0a41`.
- Snapshot TSF is 407517 versus prefix TSF 407296. These are diagnostic
  PSM observations, not externally measured RF/SIFS timing.
- The later good terminal still has RX count 176; raw `0x102b` is 298.
  **No second complete MPDU was captured or independently validated.**
- Ordinary saved MPDUs are 170/298 bytes, received-BA delta is 101, and
  there is no prefix overflow, unaccounted exit or pending ID at stop.
- One oversize early candidate is explicitly counted.

This closes one concrete early-byte preservation test on a larger live
PPDU. It does not qualify repeated early capture, both physical roles,
other early lengths/MCS, next-frame advance, or two-body reception.
“Nonterminal” refers to the captured hint status; it does not establish
copy completion before physical EOF or an EOF-to-air timing bound.

All six final-build controls are retained, including failures:

| Evidence suffix under `build/clean-rx/vendor-early/` | Ordinary checked lengths | Received-BA delta | Valid / A-MPDU prefixes | Early qualification |
| --- | --- | ---: | ---: | --- |
| `hint-ab-r0` | 554, 170 | 108 | 5 / 5 | No retained early sample; fail |
| `hint-ba-r0` | 170, 298 | 101 | 15 / 14 | Complete 170-byte multi-candidate; pass |
| `hint-ab-r1` | 170, 298 | 111 | 6 / 5 | No retained early sample; fail |
| `hint-ba-r1` | 170 | 112 | 5 / 4 | No early sample and only one ordinary length; fail |
| `hint-ab-r2` | 170, 298 | 125 | 3 / 2 | No retained early sample; fail |
| `hint-ba-r2` | 170 | 111 | 3 / 3 | No early sample and only one ordinary length; fail |

Earlier build `d4ec91aa`, `trial-{ab,ba}-r{0,1}`, also remains as failed
evidence. B/A retained complete FCS-valid 170-byte snapshots, but the
status was sampled too late and already indicated terminal completion
(`0x4122`). They correctly failed early qualification; moving the status
read, not weakening the predicate, fixed that instrumentation problem.
The initial A/B trial captured no prefixes at all. These trials do not
support loss-free reception or a general receive-corruption fix.

The justified next control is bounded preservation plus a single
next-body advance, starting with the observed 478-byte case whose sizes
are consistent with 170/298-byte MPDUs. The second body's actual bytes,
FCS, boundaries and nonduplicate identity must be demonstrated under the
same fresh prefix before claiming two-MPDU delivery. The bounded diagnostic
below implements that attempted handoff; the physical continuation remains
unqualified.
Production admission, bad-subframe handling, BA/reorder/key lifecycle and
physical SIFS qualification remain later gates.

## Bounded next-body advance

This section records the initial ABI-6 implementation. The current
[paired-stimulus/ABI-7 update](#paired-stimulus-and-live-two-body-capture)
below supersedes its one-order gate, buffer partition and base hypothesis.

The reference now enables `CLEAN_RX_HT_ADVANCE=1`; the flag defaults off in
the clean firmware and requires `CLEAN_RX_HT_EARLY_SAVE=1`. It is deliberately
limited to the observed 478-byte PSDU / 170-byte first body / raw next length
298 case, one advance per experiment. It requires a committed first body,
matching current owner and RX length, GoodFCS, and clear terminal/abort bits
immediately before arming. Observer TX remains disabled.

After recording the attempt it acknowledges event 6 once, clears context
bit 7, sets `SPR1001=FRAME+3`, writes copy end 298, arms the existing
direct-body controls, replenishes the watchdog and resumes at PC163 without
classifying another prefix. `FRAME+3` is an explicit hardware-layout
hypothesis: the direct MAC body should begin at `FRAME+4`, retaining HT-SIG.
No RF trial below reaches this action, so the layout is not yet proven.

ABI 6 retains all previous trace offsets. With advance enabled the early
snapshot has a 16-word header and a **992-byte** body, still exactly within
SHM `0x400..0x5ff`. Without advance it retains the original 12-word header
and 1000-byte body. Additional words are:

| Early snapshot words | Contents |
| --- | --- |
| 12 | One-shot advance marker: 0 not armed, 1 attempted |
| 13 | Raw next length saved before destructive action |
| 14..15 | Advance TSF |
| 16..511 | Complete early body, up to 992 bytes, including FCS |

F2 diagnostic ID bit `0x8000` marks a terminal sample whose held prefix
matches the attempted advance. The RP2350 removes that bit from the ID,
stores descriptor metadata bit `0x100`, prioritizes that sample over an
ordinary full pool and prevents subsequent replacement. Ordinary same-size
replacement is unchanged. There is no new USB polling or larger trace.

`complete_multi_mpdu_delivery_proven` is now computed, not fixed false.
It requires independent complete FCS-valid early/F2 correlations under the
same fresh prefix/epoch, the attempt and saved next length, distinct
unfragmented sequence numbers, matching addresses/DS/TID, good terminal EOF,
and `4 + align4(170) + 4 + 298 == 478`. Marker-only, duplicate-body,
fragment-only sequence changes and individually valid but incompatible
frames cannot qualify. With advance enabled the collector exits nonzero
unless this pair predicate passes, in addition to its existing checks.

### Retained bounded controls

Build `fe06ab77`, evidence under `build/clean-rx/vendor-advance/`:

| Trial | Vendor / observer | Valid HT headers | Aggregation-format headers | Correlated ordinary MPDUs | Early / advance / pair |
| --- | --- | ---: | ---: | ---: | --- |
| `bounded-r0` | B / A | 10 | 10 | 2 | None; fail |
| `bounded-r1` | A / B | 9 | 8 | 2 | None; fail |
| `bounded-r2` | B / A | 3 | 1 | 2 | None; fail |
| `bounded-r3` | A / B | 7 | 6 | 2 | None; fail |
| `bounded-r4` | B / A | 6 | 6 | 2 | None; fail |
| `bounded-r5` | A / B | 9 | 8 | 2 | None; fail |

Each trial submits all 128 fixtures, observes vendor received-BA activity,
retains two complete valid variable-length ordinary samples and has zero
unaccounted prefix exits. None retains an eligible early body or arms the
advance. These are retained negative controls, not evidence of two-body
delivery or working physical terminal/abort recovery after advance.

Final build `d4873fdc` adds host-only address/fragment/PSDU/abort negative tests and
extracts the unchanged sample-selection policy into a native-tested helper.
Its controller binary byte-compares equal to the RF-tested controller.
The tested images, controller and collector are preserved with `fe06ab77-`
filenames. The final host-only changes have native validation, not another
radio campaign.

Both original 16-MiB flashes were backed up before this campaign to
`build/clean-rx/vendor-advance/original-{A,B}.bin` and matched to the previous
verified originals. After the six trials both were fully restored, then
independently reread as `restored-{A,B}-full.bin` and byte/hash-compared with
those backups. Both original hashes match the preservation record below;
both badges remain in BOOTSEL.

## Paired stimulus and live two-body capture

The current grouped stimulus requests `ampdu_mpdu=2` and the bounded gate
accepts both 170/298-byte orders within a 478-byte PSDU. The continuation
base is aligned `FRAME`, superseding the unqualified `FRAME+3` hypothesis.
No byte shifting, prefix reconstruction or forced-good FCS is used.
Submission-call timing metrics at trace offset 736 retain count,
elapsed/minimum/maximum microseconds and four buckets (`<=200`, `<=400`,
`<=800`, larger). Counts and bounds are checked; they are not RF airtime.

### Current ABI-7 snapshot layout

The existing 1024-byte dump is unchanged in size:

| SHM words / trace bytes | Contents |
| --- | --- |
| `0x400..0x4ff` / `4768..5279` | First snapshot: 16-word header plus up to 480 body bytes |
| `0x500..0x5ff` / `5280..5791` | Continuation: 12-word header plus a fixed 300-byte raw body window |

The non-advance, early-save-only profile retains its original 12-word header
and 1000-byte capacity. For the advance profile, first-snapshot words
12..15 still hold attempt/next length/TSF. Continuation words 0..11 hold
commit-last ID, epoch, RX count, status1/status2, TSF, three live prefix
words, hardware good/bad outcome, and actual `SPR1001`. A fixed window is
copied so failed/truncated candidates retain their bytes; only the bounded
RX-count-derived length is eligible for an FCS check. Stale suffix bytes
never prove a frame. Both buffers are disjoint and end before live controls
at `0x600`. No extra USB pages or Cortex/PSM object-selector races are added.

Only an owned attempted advance can freeze one continuation. Its good/bad
outcome is retained without turning bad frames into good F2 deliveries.
Wrong owner/epoch, short/oversize input and a previously committed snapshot
cannot replace the evidence. The first snapshot remains immutable.

### Positive live result and retained final controls

Build **`d97844a9`**, `build/clean-rx/vendor-pairs/final-r0`, vendor B /
passive A, retains complete **170/298-byte** FCS-valid bodies under one
fresh **478-byte** PPDU. The second body also reaches normal tagged F2.
Matching addresses/TID, distinct unfragmented sequences and exact aggregate
boundaries pass the strict collector; an independent bitwise CRC32 check
verifies both full bodies and equality of frozen/F2 continuation bytes.

| Final-build trial | Vendor / observer | Early / advance / pair |
| --- | --- | --- |
| `final-r0` | B / A | 170 / yes / **pass** |
| `final-r1` | A / B | None / no / fail |
| `final-r2` | B / A | None / no / fail |
| `final-r3` | A / B | None / no / fail |

This is **one bounded live two-body success**, not repeatable/both-role
handoff, loss-free reception, generalized aggregate delivery or production
HT operation. Bad-continuation controls resume later good receptions,
but physical abort recovery after advance remains unqualified. SIFS,
Immediate BA by the clean observer, secure reorder/key lifecycle and actual
HT association remain gated. All failed controls are retained.

Tested intermediate and final images/collectors are retained by build ID
beside the raw evidence. Native tests cover both size orders, every early
length 44..480, exact continuation copy bounds, commit/owner/epoch/length
guards, immutable first-body retention, modeled terminal/bad/abort ownership,
and no TX. Default-image and 1000-byte save-only regressions pass.

The paired campaign's full originals in `build/clean-rx/vendor-pairs/`
were restored and independently reread/byte/hash-verified after the trials.
Both original hashes match the preservation record below; both badges
remain in BOOTSEL.

Full intermediate controls, corrupted-byte diagnosis, timing measurements,
exact positive-frame identity and USB readiness failure are recorded in the
[D2 development record](../dev-doc/HT20_IMPLEMENTATION.md#first-complete-live-two-body-capture-2026-10-19).

### Exact-size retention follow-up

Build **`2e8ac062`** adds direct 170/298-byte early-copy paths while
retaining guarded copies for other lengths. The existing snapshot bounds,
stale-copy rejection and strict two-body proof predicates are unchanged.
Unreachable large-copy blocks are omitted from the advance profile only.

`build/clean-rx/vendor-fifo/copy-r0` captures the reversed **298/170-byte**
pair with both FCS values independently verified and the actual continuation
F2 bytes equal to the frozen snapshot. This extends the earlier forward-
order demonstration. Only one of six follow-up trials passes; neither
both-role repeatability nor physical abort recovery is qualified.

A vendor-only assembly FIFO control showed no useful submission-pacing
gain and was removed. The preserved old baseline also reproduces the
missing-capture direction. Detailed controls, limits and live bad-frame
recovery are in the
[retention follow-up record](../dev-doc/HT20_IMPLEMENTATION.md#shorter-live-retention-and-transport-pacing-control-2026-10-19).

Both complete originals were restored after this follow-up, with independent
16-MiB readbacks and byte/hash comparisons against the verified
`build/clean-rx/vendor-fifo/` backups. Both badges remain in BOOTSEL.

### Frozen delivery-state follow-up

Build **`d6c2e83c`** adds the ABI-8 readout and two ordinary-frame summaries.
Its clean controller is byte-identical to `2e8ac062`. Six bounded trials in
`build/clean-rx/vendor-receive-state/` all receive HT traffic and reconcile
captured frames with posted/dropped transport counts, with no pending credit
or host resync/stall at stop.

`state-r1`, vendor A / observer B, captures a complete **298/170-byte** pair,
independently bitwise-FCS checked, with frozen continuation equal to tagged
F2 bytes. This is the first complete live pair in that role assignment.
Only one of six trials passes; other runs lack an eligible early body or
retain a corrupt continuation. Earlier zero-HT loss did not recur and
remains unexplained. Neither this result nor transport accounting qualifies
repeatability, physical abort recovery or production aggregate reception.
See the [frozen delivery-state record](../dev-doc/HT20_IMPLEMENTATION.md#frozen-delivery-state-and-reverse-role-pair-2026-10-19).
Both complete originals were restored and independently reread/byte/hash-
verified against fresh full-flash backups after all six trials. Both badges
are in BOOTSEL.

### Handoff-state controls

Final trace-only build **`42424296`** retains the three-point handoff logger.
Sixteen new bounded trials exercise nine advances, all with good early
bodies but bad continuations. Fresh-prefix, FIFO-init and earlier-streaming
controls were tested and removed because none qualified a complete pair.
Zero-HT loss recurs twice with no transport drop, pending credit or host
resync, locating that failure upstream of F2/USB delivery. No RF fix or D2
completion is claimed. Detailed controls and limits are in the
[handoff-state record](../dev-doc/HT20_IMPLEMENTATION.md#handoff-state-controls-and-zero-ht-delivery-exclusion-2026-10-19).

Both complete original flashes were restored and independently reread as
`build/clean-rx/vendor-handoff-state/restored-{A,B}-full.bin`.
Each 16777216-byte readback byte/hash-matches its fresh pre-test original.
Both badges remain in BOOTSEL.

### Alternate receive-buffer control

Eight `b48c71dc` trials use a separate `0x900` second-body destination
with EOF-only copyback. Two advances, one per badge role, retain valid
early bodies but corrupt continuations despite the measured separate
destination. No complete pair qualifies. Same-destination reuse alone
does not explain these failures; the unqualified control was removed.
Exact tested images/source and all failed evidence are preserved in
`build/clean-rx/vendor-alt-buffer/`.
See the [alternate-buffer record](../dev-doc/HT20_IMPLEMENTATION.md#alternate-receive-buffer-control-2026-10-19).

The retained collector now enforces complete capture/post/drop/consume
accounting, including magic values, packed counter saturation and the
one-frame pending bound. Explicit drops and pending frames remain visible.
Native corruption/boundary tests pass, and the guard accepts all 30
preserved full captures from the latest three campaigns.
Final build `8bbed93e` retains ABI 12 and the original trace-only controller
byte-for-byte. This host-guard/identity rebuild is not another RF trial.
Both complete originals were restored and independently reread as
`build/clean-rx/vendor-alt-buffer/restored-{A,B}-full.bin`.
Both 16777216-byte readbacks byte/hash-match their fresh backups, and
both badges remain in BOOTSEL.

### Early-capture cause investigation

The [development record](../dev-doc/HT20_IMPLEMENTATION.md#early-capture-causes-and-hardware-controls-2026-10-19)
documents the 182-header retrospective audit, 29 focused RF trials,
calibration comparisons and hardware limits. Both badge roles repeatedly
retain valid complete 170/298-byte nonterminal bodies; one role also
retains a 554-byte body. All 17 logged first eligible captures are fresh
and independently FCS-valid. Nine other trials lack a logged eligible
multi-body opportunity; three role-1 trials have zero HT starts.
The later eight stopped-PSM controls all qualify but do not reproduce or
solve the intermittent zero-HT condition. No analog hardware cause has
been ruled out by the digital endpoint snapshots.
Both complete original flashes were restored after the campaign.
Independent 16777216-byte readbacks in
`build/clean-rx/early-capture-causes/restored-{A,B}-full.bin`
byte/hash-match the fresh verified backups; both badges remain in BOOTSEL.

### Zero-HT follow-up

The [follow-up record](../dev-doc/HT20_IMPLEMENTATION.md#zero-ht-reproduction-and-phy-control-comparison-2026-10-19)
adds 20 complete RF trials and reproduces zero-HT reception with actual
stopped-state evidence. Three low-RX cases on B differ from one HT-only
miss on A with 818 ordinary deliveries. Final PSM states/debug overlap
HT-positive runs; the opt-in PHY controls also match successful boots.
These endpoints do not identify a fix or exclude transient/acquisition
problems. Two USB readiness failures are retained separately from RF trials.

Both complete original flashes were restored. Independent 16777216-byte
readbacks in `build/clean-rx/early-capture-followup/restored-{A,B}-full.bin`
byte/hash-match the fresh verified backups; both badges remain in BOOTSEL.

### Burst-window and RXE rearm investigation

The [development record](../dev-doc/HT20_IMPLEMENTATION.md#burst-window-diagnosis-rxe-rearm-and-fault-recovery-2026-10-19)
retains 23 completed capture trials, five deliberately non-restarting fault
controls and one separate readiness failure in
`build/clean-rx/burst-window-investigation/`.
Two original-rearm trials reproduce low-RX zero-HT reception with persistent
RXCTL `0x804/0x806`, stale receive data and continuing PSM/Cortex-M3 activity.
An audit of 79 preceding captures finds that signature in all eight low-RX
zero-HT cases, but also finds HT-positive busy-bit counterexamples. A bit
sample alone is not a diagnosis, and ordinary-RX HT-only misses remain
separate.

The original rearm loop silently continues after 255 polls. The first eight guarded
trials retained existing automatic PSM fault restart, hiding recovery in
their final counters. Three fixed no-restart controls then fail explicitly.
Two further controls export actual SHM `0x602` reason **3**, controller error
`0x88` and zero restart attempts, already at the first window sample.
Their fault-latched RXCTL is subsequently `0x800`.
**Those guard faults were subsequently traced to an experimental software
bug:** an absent GoodFCS event fell through the relocated conditional
acknowledgement into its adjacent fault block. The native model incorrectly
made that conditional branch unconditional. These trials do not show that
a longer hardware wait is insufficient. The corrected guard explicitly
resumes on both event outcomes and tests the actual conditional behavior.
See the [decision-level correction](../dev-doc/HT20_IMPLEMENTATION.md#rearm-decision-capture-and-conditional-drain-correction-2026-10-19).

Seven new retained nonterminal bodies independently pass FCS/session checks:
six 298-byte bodies and one 554-byte body on observer A. The 554-byte byte
proof remains valid despite automatic recovery in its trial; it does not
qualify reliability.

#### Timer units and current window encoding

The raw RP2350 TIMER0 counter was incorrectly assumed to count microseconds.
Saved USB SOF calibration measures about 15,091..15,290 ticks per 32 ms on
these boots. ABI 16 exports the calibration in report bytes 40/44, covered
by the report CRC. The collector converts scalar submission/window times
using `ticks * sof_frames * 1000 / calibration_ticks` with 64-bit arithmetic.
Write-duration bucket boundaries remain explicitly labelled raw timer
ticks. No clock programming or repository-wide timeout retuning is included.
Historical uncalibrated values, including the previously reported 836-us
512-byte minimum, are raw timer counts and cannot be precisely converted
without their missing calibration.

The current separate window trace starts at byte 128:

| Window offset | Contents |
| --- | --- |
| 0..23 | Magic `WND9` (`0x39444e57`), retained count, capacity misses, nominal 100000-us interval, calibration ticks/SOF frames |
| 24..31 | Strict decision-trace enable (0/1), recovery mode (0 strict/no recovery, 1 reflush, 3 injected reflush, 5 PHY reset, 7 injected PHY reset, 13 park only, 15 injected park) |
| 32..63 | Last cached 16-word PSM decision trace; all zero when disabled |
| 64..95 | Immutable first-fault 16-word record; zero without recovery |
| 96..127 | Sixteen-word PHY reset/handoff record; zero without reset |
| 128 onward | At most 35 records of 160 bytes |
| Record 0..15 | Raw elapsed/read ticks, captured count, raw PSMDEBUG |
| Record 16..99 | Cached mailbox bytes 60..143 |
| Record 100..107 | Direct controller state/error |
| Record 108..139 | Seqlocked cached HT/log/early metadata, ending with restart-attempt count |
| Record 140..147 | Publication sequence before/after reads |
| Record 148..155 | Direct controller error address/value |
| Record 156..159 | First-fault commit (16 bits), PHY reset state (16 bits) |

Three equal nonzero even sequences qualify only the cached publication,
not a simultaneous hardware snapshot. Final/error cleanup invalidates it.
Error fields and raw PSMDEBUG remain separately identified direct reads.
Archived WND1 uses a 16-byte header and uncalibrated timing; WND2 has the
24-byte calibrated header; both retain 92 mailbox bytes per 148-byte row.
WND3 replaces the last eight mailbox bytes with controller state/error.
WND4 changes the final live-metadata word from early length to restart
attempts. WND5 appends the explicit error address/value. Keep each archived
collector with its matching format. WND6 has a 64-byte header and 36 rows
of 156 bytes; WND7 has a 96-byte header and adds the first-fault block and
per-sample 32-bit commit. WND8 has a 128-byte header and splits that final
sample word into commit/reset state. Always use the matching collector.
WND9 keeps that layout and adds explicitly labelled park-only modes.
The `rearm-decision` JSON record exports raw words and separately reports
PSM commit validity and C3 cache coherence. Its exact word layout is in the
development record; the trace ends in success (1) or genuine busy-bit fault
(2), with both branches testing the retained RXCTL operand.
The `rearm-phy-reset` JSON record reports raw reset words. State is 0 unused,
1 requested, 2 completed/readback-checked, 3 rejected/readback failure,
4 PSM reply timeout, or `0xffff` for an invalidated cached copy.
Execution count, original MACCONTROL, three PHY registers before/after,
TX-inhibit masks, elapsed TSF and RXCTL before/after follow that state.

### Corrected rearm decision experiment

Twelve fixed trials in `build/clean-rx/rearm-decision-investigation/` compare
four original-rearm 256-byte controls (`0f33f4ae`) with eight corrected
strict controls (`5381cb74`, both roles and 256/512-byte sizes).
All four baselines and seven strict trials finish capture without controller
fault or recorded restart. Three strict trials independently qualify fresh
298-byte early bodies; none of the four strict 512-byte trials qualifies
a 554-byte early body.

The remaining strict trial now identifies a **real** exhausted rearm:
after 611 captures, the PSM-retained branch operand and later RXCTL are
both `0x804`, the remaining wait count is zero, outcome is busy fault,
and the controller reports SHM reason 3 with zero restart attempts.
This is distinct from the previous false conditional-drain faults.
Full counters, qualification limits and the next recovery test are in the
[decision-level record](../dev-doc/HT20_IMPLEMENTATION.md#fixed-rf-comparison-and-a-genuine-exhausted-rearm).
All 72 raw reports pass independent CRC/length checks; 360 timeline rows
are retained without capacity loss. The one faulted frozen capture remains
an explicit failure. No production recovery fix is claimed.

### One-shot recovery and both-role 554-byte captures

The preceding gated experiment is recorded in the
[one-shot recovery development record](../dev-doc/HT20_IMPLEMENTATION.md#one-shot-rxe-event-drainreflush-experiment-2026-10-19).
Four injected RF controls (`6d4ba8e4`) execute recovery and resume reception
without restart; they do not simulate a physical busy RXE.
Four strict 512-byte controls (`9c377cb3`) include two real exhausted
`0x804` faults. Six natural-mode trials (`edd21932`) include one actual
recovery activation: both flush attempts exhaust, preserving the first
fault and stopping explicitly. **Event drain plus reflush did not recover
that physical state.** Five other natural trials complete without fault.

Two natural trials independently qualify complete 554-byte nonterminal
early bodies: `natural-ready512-role0-r0` (prefix 2, epoch 121) and
`natural-ready512-role1-r1` (prefix 1, epoch 24). Each has a 1118-byte HT
PSDU, verified body FCS, nonce-derived transmitter, HT-SIG CRC and matching
prefix/epoch. Neither activates recovery, so these results cannot be
attributed to it. Repeatability and physical RXE recovery remain open.
An injected trial also retains zero HT starts despite 557 ordinary captures
and successful rearm, so HT-only losses remain separately unresolved.

The 14 actual RF trials retain 84 valid raw reports and 430 window rows
with no capacity loss. Eleven successful frozen captures reconcile delivery;
three genuine faulted captures remain errors. Four initial USB-discovery
failures and six Chrome-exclusive BOOTSEL-access failures are archived
separately, not counted as RF trials. Source, evidence and version-matched
image archives are under `build/clean-rx/rearm-recovery-investigation/`.
Fresh full originals there match the preceding verified backups.
After the matrix, both whole flashes are restored; independent
16777216-byte `restored-{A,B}-full.bin` readbacks byte/hash-match those
originals, and final serial-targeted checks confirm both in BOOTSEL.
This includes a complete successful rewrite of B after its interrupted
earlier restoration attempt.

### Parked PHY reset: one real recovery, two persistent failures

The [instrumented PHY-reset record](../dev-doc/HT20_IMPLEMENTATION.md#parked-one-shot-phy-receive-reset-2026-10-19)
adds four injected execution controls, four reflush controls and ten
natural reset trials. Three natural busy timeouts invoke the new reset:
one clears RXCTL `0x804` to `0x800` and resumes reception, while two remain
busy and stop explicitly after the bounded retry. The successful trial
retains coherent samples showing capture growth from 327 after activation
to 350, with no PSM restart or counter reset. It still sees no HT starts.
This is a first physical recovery proof, **not a general reliability fix**.

All four injected controls execute exactly one checked reset and resume
capture. The fixed matrix adds no 554-byte early proof: the retained
HT headers show only nonaggregates or single-body A-MPDU format, not a
confirmed multi-body capture opportunity. Block ACK activity alone is
not sufficient stimulus qualification.

All 108 raw reports and 554 WND8 rows independently validate; fourteen
successful frozen exports reconcile delivery and four genuine faults
remain failures. Evidence and matching RF images/collectors are in
`build/clean-rx/phy-reset-investigation/`. Normal firmware stays unchanged,
all reset gates remain off by default, and D2 stays open.
After the campaign, both entire original flashes are restored. Independent
16777216-byte `restored-{A,B}-full.bin` readbacks in that directory
byte/hash-match the fresh originals, and both badges remain in BOOTSEL.

## Footprint and validation

Final native-validated ABI-19 sizes (`llvm-size`); these identity rebuilds
are not additional RF trials:

| Profile / image | Text | Data | BSS |
| --- | ---: | ---: | ---: |
| Default advance `12ce0843`: vendor RP2350 | 246560 | 28 | 40012 |
| Default advance: observer RP2350 | 77236 | 28 | 23240 |
| Default advance: included clean controller | 60528 | 0 | 6408 |
| Early-only `140fc51d`: vendor RP2350 | 246756 | 28 | 48236 |
| Early-only: observer RP2350 | 78072 | 28 | 23240 |
| Early-only: included clean controller | 61364 | 0 | 6408 |
| PHY probe `0d78175f`: vendor RP2350 | 246756 | 28 | 48236 |
| PHY probe: observer RP2350 | 78032 | 28 | 23240 |
| PHY probe: included clean controller | 61300 | 0 | 6408 |
| Window `3ced6a9e`: vendor RP2350 | 247420 | 28 | 48236 |
| Window: observer RP2350 | 79944 | 28 | 28972 |
| Window: included clean controller | 61484 | 0 | 6408 |
| Strict rearm `aee44b59`: vendor RP2350 | 247428 | 28 | 48236 |
| Strict rearm: observer RP2350 | 80232 | 28 | 28972 |
| Strict rearm: included clean controller | 61736 | 0 | 6408 |
| Natural reflush `613e410c`: vendor RP2350 | 247428 | 28 | 48236 |
| Natural reflush: observer RP2350 | 80460 | 28 | 28972 |
| Natural reflush: included clean controller | 61964 | 0 | 6408 |
| Injected reflush `ed4f0d14`: vendor RP2350 | 247432 | 28 | 48236 |
| Injected reflush: observer RP2350 | 80488 | 28 | 28972 |
| Injected reflush: included clean controller | 61988 | 0 | 6408 |
| Natural PHY reset `2ab745a2`: vendor RP2350 | 247428 | 28 | 48236 |
| Natural PHY reset: observer RP2350 | 81644 | 28 | 28972 |
| Natural PHY reset: included clean controller | 63148 | 0 | 6408 |
| Injected PHY reset `1dcc7ae0`: vendor RP2350 | 247428 | 28 | 48236 |
| Injected PHY reset: observer RP2350 | 81668 | 28 | 28972 |
| Injected PHY reset: included clean controller | 63172 | 0 | 6408 |

The observer already includes the clean controller blob; do not add its
size a second time. The vendor image includes the unchanged vendor assets.
All eighteen RP2350 ELFs have no undefined symbols. Neither RP2350 application
contains clean station, clean WPA2 or previous BA application symbols.
The TFT uses streamed glyph rows, not the 153600-byte canvas; BSS changes
only by 32 bytes versus WND7 in window profiles for the revised trace.
The ordinary controller remains 41388 text / 0 data / 4912 BSS.
Build identities include input paths, so private-output validation wrappers
have different IDs from shared-output RF wrappers even with identical
controller bytes.

Validation includes ASan/UBSan fixture/digest/counter/HT-SIG tests, captured
RF HT-SIG vectors, PSM observer tests, default-controller regression and
original-image reconstruction identity, plus runner syntax and whitespace.
ABI-8 summary tests cover every MPDU length 4..3896, short-frame zero padding,
corrupt FCS, invalid lengths and untouched output boundaries.
The previous uncorrelated reference ended with host-only rebuild `f8916d0c`.
Correlation build `4e209642`, exit/first-hint build `7f1e3e90` and early
snapshot build `e9008554` were radio-tested in both roles, with their
different success criteria and qualification limits recorded above.
Final tests also cover the default profile and the exit-only observer
with early preservation disabled. That early-retention radio build remains `e9008554` after its native race
test. The later bounded-advance controls and host-only follow-up are
separately identified above. Advance-profile tests cover all lengths
44..480, modeled continuation ownership for good/bad/abort/watchdog exits,
no TX, one-shot and EOF/GoodFCS gates, proof-sample retention and strict
same-PPDU pair positives/negatives.
Window tests cover malformed encodings, capacity, ordered timestamps,
zero tails, torn publications and invalid SOF calibration. Rearm tests
execute the actual instruction words over busy-bit combinations and
completion delays through the 65,535-poll boundary, with all ext3/ext6
combinations and correct conditional acknowledgement/fall-through.
Recovery profiles additionally check one-shot success and persistent failure
for every combination of the six drained events, immutable first-fault
publication and preservation of unrelated events. Shared display timing
and graphics-client regression tests pass.
Ordinary, early-only, PHY, original-rearm window and advance controllers remain byte-identical to their prior verified
images. The final strict-rearm controller is byte-identical to the
corrected RF controller `5381cb74`, not the superseded guard `ce436e87`.
All 168 raw phase reports from the preceding window campaign
independently pass length/CRC checks; 778 timeline rows are retained.

## Preservation and next investigation

Fresh full backups are `build/clean-rx/vendor-burst/original-{A,B}.bin`,
each 16777216 bytes, independently matched to the previously restored images.
Restore each serial separately using:

```sh
build/picotool-usb/picotool load --ignore-partitions -v ORIGINAL.bin \
    -t bin -o 0x10000000 --ser SERIAL
build/picotool-usb/picotool save -a RESTORED.bin -t bin --ser SERIAL
cmp ORIGINAL.bin RESTORED.bin
```

Leave BOOTSEL after the independent full reread/compare. Original SHA-256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

After this campaign both complete originals were restored, independently
reread as `restored-{A,B}-full.bin`, byte-compared and hash-checked against
the backups. Both devices were left in BOOTSEL.

Fresh backups for the correlation follow-up are
`build/clean-rx/vendor-correlate/original-{A,B}.bin`, each independently
matched to the prior restored originals. They have the same hashes above.
After the follow-up both complete flashes were restored, independently
reread as `build/clean-rx/vendor-correlate/restored-{A,B}-full.bin`,
byte-compared and SHA-256 matched to those backups. Each reread contains
16777216 bytes. Both devices were left in BOOTSEL.

Fresh backups for the exit investigation are
`build/clean-rx/vendor-exits/original-{A,B}.bin`, independently matched to
the prior restored originals and using the same hashes above.
Both complete originals were restored after the trials, independently
reread as `build/clean-rx/vendor-exits/restored-{A,B}-full.bin`, byte-compared
and SHA-256 matched. Every backup and reread is 16777216 bytes. Both badges
were left in BOOTSEL.

Fresh prefix-to-MAC-to-complete-variable-length-MPDU correlation and
explicit larger-prefix abort observations are now demonstrated, together
with one complete early-event MPDU from a larger live PPDU. Repeating
that capture and qualifying a bounded next-frame handoff remain the next
D2 investigations. Admission-derived BA and agreement/key/reorder lifecycle
integration remain later gates. Do not enable clean negotiated HT from
these results.

For the early-preservation campaign, fresh full backups are
`build/clean-rx/vendor-early/original-{A,B}.bin`, independently compared
with the prior restored originals before experimental writes. Both full
originals were restored afterward and independently reread as
`build/clean-rx/vendor-early/restored-{A,B}-full.bin`. All four files are
16777216 bytes; byte comparisons and SHA-256 match the original hashes
above. Both badges are left in BOOTSEL.

For the burst-window/rearm campaign, fresh full backups are
`build/clean-rx/burst-window-investigation/original-{A,B}.bin`.
After all 28 capture/fault-control trials, both complete originals were
restored and independently reread as
`build/clean-rx/burst-window-investigation/final-restored-{A,B}-full.bin`.
Both 16777216-byte readbacks byte/hash-match their fresh verified originals
and the hashes above. Final serial-targeted checks confirm both badges
remain in BOOTSEL.

For the corrected decision-level matrix, fresh complete originals are
`build/clean-rx/rearm-decision-investigation/original-{A,B}.bin`.
After all 12 fixed trials, both complete flashes were restored and
independently reread as
`build/clean-rx/rearm-decision-investigation/restored-{A,B}-full.bin`.
Both 16777216-byte readbacks byte/hash-match the verified originals and
the hashes above. Both badges remain in BOOTSEL.
