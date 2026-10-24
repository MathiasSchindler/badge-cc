# HT20 diagnostic development notes

Development records, campaign outcomes and open receive/copy questions moved
out of the firmware reference. The current wire interface is documented in
[HT20](../doc/HT20.md); these notes retain the conditions and limits of the
individual investigations.

## Desktop report fields

HT probe/data/ACK collector records expose `ht20_format`,
`ht20_width_mhz`, `ht20_gi_ns`, `ht20_streams`, `ht20_rate_kbps`,
`ht20_symbols`, `ht20_expected_airtime_us` and `ht20_lsig_length`.
These describe the requested configuration and calculated airtime, not
independently measured on-air duration. Passive monitoring does not invent
a requested configuration.

Peer reports expose `ht_peer_profile_source` and `ht_peer_runtime_updates`.
The selectable candidate is reported in `ht20_candidate_tx_mcs_mask` and
`ht20_candidate_max_mcs`; unavailable candidates use mask 0 and MCS -1.
The local width, GI, stream count, RX mask and serialized target IE do not
indicate negotiated HT operation. `ht20_active` and `ht_advertised` remain
false.

WMM collector fields are `wmm_report_valid`, `wmm_offered`,
`wmm_negotiated` and `wmm_policy_committed_updates`. The last counts applied,
controller-readback-verified policies, not queued requests.
`wmm_policy_queued_updates` is an equal compatibility alias.
`wmm_edca_enabled` uses controller EDCA status at USB detail offset 752.
`protected_amsdu_frames` and `protected_amsdu_subframes` count newly delivered
validated protected aggregates, not retries, F2 fragments or A-MPDUs.

### Finite aggregate diagnostic

The separate `CLEAN_RX_HT_HANDOFF_STIMULUS=1` gate, requiring HT data and
extended RX, permits command-12 **version 7** only for an exactly regenerated,
nonstation diagnostic aggregate. It defaults off and does not enable general
aggregate TX or station negotiation.

The additional default-disabled `CLEAN_RX_HT_HANDOFF_GUARD=1` geometry
selects exact version 8 instead: 564 physical bytes, with the same first
two bodies followed by alignment, a delimiter and an 80-byte valid guard
MPDU. This isolates a nonterminal second body. The paired source/observer
harness accepts only `good`; two-body negative-case labels are not RF
qualification for this layout. It is still a one-shot diagnostic, not
general three-MPDU receive support. The version-7 details below apply
when the guard gate is off.

- The physical PSDU is always 478 bytes: a four-byte delimiter, padded
  170/298-byte first MPDU, another delimiter and a 298/170-byte second MPDU.
  The final MPDU is not padded. HT-SIG describes the actual finite PPDU,
  including in malformed-body tests; there is no intentional TX underrun.
- Nonce byte 10 selects good, bad-first FCS, bad-last FCS, bad second
  delimiter CRC or an eight-byte-overlong second delimiter claim (0..4).
  Byte 11 selects 170-to-298 or 298-to-170 (1 or 2); byte 12 selects MCS0..7.
  Only aggregate index 2 carries the selected fault; later aggregates are good.
- Frames use nonce-bound, unprotected FromDS unicast QoS headers. The first
  MPDU sets More Data and the second clears it, distinguishing even their
  first eight bytes without changing receiver/transmitter addresses.
  These are raw diagnostic fixtures, not data under a negotiated BA agreement.
- `clean_ht_handoff_frame()` constructs eight legacy/single-HT/aggregate
  cycles. Only the aggregate uses version 7; controls retain ordinary
  nonaggregate descriptors and the selected first-body length.
- The logical descriptor is 492 bytes, but exact admission checks all
  1132 staging bytes, including the zero tail. Stage the entire buffer.
  Any command flag, descriptor, body, FCS or tail mutation is rejected.
  Intentionally invalid bytes are admitted only as part of the exact fixture.

The [controlled handoff profile](../vendor-burst/README.md#controlled-170298-byte-handoff)
uses MCS0 on hardware. Native tests cover both orders, all five cases,
all eight MCS and every transmission index. General clean aggregate
reception and malformed/abort recovery are **not qualified**; fresh
eight-byte second prefixes are not complete MPDU or valid-FCS evidence.
The separate [vendor raw monitor](../vendor-burst/README.md#independent-vendor-raw-receiver)
independently captured 16 complete pairs, including both FCS values, in
eight MCS0 trials across both badge roles and body orders with the unchanged
source. This confirms correct transmitted second-body bytes for the tested
stimulus; it does not repair clean receive or enable HT association.

The subsequent default-disabled `CLEAN_RX_HT_COPY_DEFER_STREAM` control
does repair bounded clean 170/298-byte handoff: it changes only the
streaming threshold from 128 to 384 bytes, below the existing staging
overflow bound. It requires the copy probe and excludes wait, end-bias,
stream-clear and guard geometry. Eight good trials retain exact pairs in
both roles/orders; four also receive every required control. Bad-first
and bad-last each pass the strict bounded recovery gate in all four cells.
The [causal evidence](../dev-doc/HT20_IMPLEMENTATION.md#deferred-pre-handoff-streaming-repair-2026-10-20)
does not generalize to longer aggregates or remove other receive losses.
Ordinary/default firmware keeps its original threshold and HT policy.

The separate default-off `CLEAN_RX_HT_COPY_DMA_PROBE` retains
challenge-bearing DMA windows before reclamation and scans all 32 buffers
after verified DMA shutdown. It leaves PSM instructions and transmitted
bytes unchanged. Matched tests find valid later aggregate first bodies in
DMA, but no second-body prefixes; deferred streaming preserves exact second
bodies in SHM without streaming these short bodies to DMA. This narrows the
failure upstream of DMA completion, not to a generally broken DMA engine.
The probe replaces the ordinary MPDU proof-sample area and is explicitly
diagnostic-only, never a D2 pass. See the
[byte-destination investigation](../dev-doc/HT20_IMPLEMENTATION.md#streaming-byte-destination-and-dma-discriminator-2026-10-20).

Default-off `CLEAN_RX_HT_COPY_STREAM_PHASE` now separates first-body and
continuation streaming with matched 384/128/256-byte controls. Across both
roles, a 298-byte continuation starts streaming at a fresh count of 130
under the 128-byte gate and retains exactly 128 canonical bytes before
watchdog. Moving only the gate to 256 moves activation to 258 and the
canonical-byte boundary to 252. The 384 control retains complete pairs;
a 170-byte continuation can also complete while streaming at 128.
Thus stale first-body count is not needed for the observed loss, and
first-body streaming cannot be its sole cause. Phase-only activation/
epoch readbacks and bounded watchdog snapshots preserve existing rejection
and normal F2 proof samples. Twenty-six RF trials retain sixteen exact
pairs, twelve with all required controls. This is a causal discriminator,
not a larger-body repair or D2 completion. See the
[moving-boundary investigation](../dev-doc/HT20_IMPLEMENTATION.md#continuation-streaming-and-moving-copy-boundary-2026-10-20).

The dependent default-off `CLEAN_RX_HT_COPY_STREAM_REARM` diagnostic
checks copy state and tests one immediate replay of the existing copy
command against a matched no-op. Twelve RF trials show the command's
bits latch but do not repair the long-body stall: all eight 298-byte
cases stop at count 134 with 128 canonical bytes, while sampled
base/end/context remain unchanged. Short bodies still complete; control
losses remain strict failures. This excludes immediate replay as a
repair, not every possible post-stall recovery timing. See the
[copy-state discriminator](../dev-doc/HT20_IMPLEMENTATION.md#streaming-copy-state-and-immediate-retrigger-discriminator-2026-10-20).

The subsequent modes 3/4 compare a measured 32-us count-134 plateau
followed by no-op or copy replay, with bounded progress/EOF/abort and
timer-failure exits. Across twelve RF trials, all four delayed-replay
long bodies replace SHM bytes 0..31 with transmitted bytes 128..159,
then abort; no-ops retain the original prefix and watchdog instead.
This exposes distinct source progress and write placement, not a
working recovery. All four short-body trials progress, skip replay
and pass; none proves long-body recovery. See the
[post-stall byte mapping](../dev-doc/HT20_IMPLEMENTATION.md#measured-post-stall-replay-and-suffix-overwrite-2026-10-20).

The subsequent diagnostic-only modes 5/6 isolate copy-destination
placement: after the same verified stall, add zero or 64 to the copy
base, then replay the same command. Twelve RF trials confirm that
adding 64 moves the new 32-byte source span by 128 SHM bytes, preserving
the old prefix and extending it to exactly 160 bytes in both roles.
All eight long-body trials still abort or fail FCS; none delivers an
exact pair. Short-body controls skip the action and remain intact.
Destination placement is verified, but endpoint/count semantics and
copy-bank/stream ownership still need investigation. Both profiles
are explicitly excluded from qualification. See the
[bounded placement evidence](../dev-doc/HT20_IMPLEMENTATION.md#bounded-copy-destination-placement-2026-10-20).

Diagnostic modes 7/8 then keep that destination fixed and compare
endpoint 298 against remaining-length candidate 170. Twelve RF trials
show changed termination, but no additional correct bytes: every long
body still has only 160 canonical bytes and aborts or fails FCS. All
four short-body controls skip the action and deliver exact pairs with
all controls. This excludes that specific endpoint adjustment as a
sufficient repair; copy-bank/stream ownership and the 32-byte fresh-copy
limit remain open. See the
[endpoint discriminator](../dev-doc/HT20_IMPLEMENTATION.md#replay-endpoint-versus-remaining-bytes-2026-10-20).

The subsequent small default-off copy microscope establishes that this
is not a fixed 32-byte copy-engine limit. Across 24 RF trials, matched
stream-on/off snapshots show later bytes arriving only with streaming
off; re-enablement imposes a new 256-byte boundary. Holding it off for
the committed current epoch instead retains all **298 continuation
bytes with their correct FCS**, four times across both roles/repeats.
Native accounting still reports count 176 and bad-FCS outcome 2, so no
long body is delivered through normal F2 and no rejection gate is
waived. Copy/count/FCS lifecycle and safe FIFO publication remain open.
See the [focused microscope](../dev-doc/HT20_IMPLEMENTATION.md#focused-copy-microscope-and-current-epoch-stream-ownership-2026-10-20).

An original-destination matched quiet/replay control now separates stream
quieting from copy replay. In four long-continuation trials per profile,
quieting alone stalls at native count 134/watchdog. Replaying the command
rewinds the live count to 58, writes the complete 170-byte remaining tail
over the original prefix and ends count 176/bad FCS. Four short-body
controls deliver exact pairs, but ordinary control misses remain.
`CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY` is default-off and restricted to scope
3/rearm 5. This isolates a replay-dependent receive-state effect, not a
count/FCS repair; normal HT admission stays disabled.
See the [matched discriminator](../dev-doc/HT20_IMPLEMENTATION.md#quiet-only-versus-copy-replay-at-the-original-destination-2026-10-20).

The compound replay field has now been separated into its asserted
register bits. Omitting bit 7 preserves native GoodFCS even as the count
restarts; deliberately corrupted second bodies still fail. With the
verified destination shift, complete 298-byte continuations now have both
native GoodFCS and valid software FCS. Initial body-start commands are
unchanged. This is evidence of a mid-body FCS-state initialization
mistake, not a published universal hardware bit definition.

`CLEAN_RX_HT_COPY_SCOPE_REPLAY_FIELD` is default `0x83` and restricts
modified values to the focused epoch-held diagnostics. Their ordinary
FIFO admission rejects mismatched copy-local length with explicit outcome
4, while preserving the native count/status/outcome in the retained
snapshot. Final tests eliminate malformed partial exports and retain
short-body delivery; ordinary receive losses and complete-body FIFO
accounting remain open. No count normalization or HT activation is used.
See the [fundamental field investigation](../dev-doc/HT20_IMPLEMENTATION.md#compound-replay-command-fcs-state-and-copy-local-length-2026-10-20).

A separate next-MPDU discriminator now changes only the actual boundary
command to `0xc3`, leaving initial commands and mid-body replay untouched.
Six ordinary-threshold good trials deliver exact 170/298-byte pairs,
with native GoodFCS and correct native count 176/304; the four matched
`0x83` trials fail. These pairs require neither segmented replay nor
count normalization. `CLEAN_RX_HT_NEXT_FIELD` remains `0x83` by default;
the alternative requires the bounded advance/copy diagnostic.
Control losses and malformed/truncated-input qualification remain open.

`CLEAN_RX_HT_VARIABLE=1` adds a separate version-9 two-body microscope:
eight rotating length pairs, both orders, exact padding/PSDU gates and
full continuation F2 correlation through 554 bytes. It retains one
480-byte early slot, not an N-body receiver. Of 50 RF trials, 39 retain
exact pairs but only 13 pass the existing collector policy; controls are
726/800, with one invalid-control trial retained as an integrity failure.
An 80/554 pair fails at native threshold 128 in both roles. A default-off
64-count threshold diagnostic produces that exact pair in both roles,
while simply enabling the stream bit at the boundary does not.
This isolates an initial copy-state/history dependency, not a qualified
general threshold change. See
[variable-length evidence and remaining ownership limits](../dev-doc/HT20_IMPLEMENTATION.md#variable-length-boundaries-and-initial-stream-state-2026-10-20)
and the [fixture invocation](../vendor-burst/README.md#variable-length-boundary-microscope).

The subsequent [verified hint follow-up](../dev-doc/HT20_IMPLEMENTATION.md#verified-hint-follow-up-shm-limits-short-first-streaming-and-admission-2026-10-20)
rejects SHM-only capture as a general repair: four long-pair shapes abort
in both roles near cumulative native count 512. It implements an opt-in
short-first streaming dispatcher that uses count 64 only for first
aggregate bodies of 64..121 bytes, preserving normal thresholds elsewhere,
plus the vendor's gated delimiter-status-bit exclusion.
All variable handoffs now also quarantine native continuation counts
that differ from the saved delimiter length plus 6, even with native
GoodFCS. This closes a measured malformed-prefix FIFO publication gap
without normalizing raw evidence.
The final build delivers all 16 selected good pairs exactly, but has
364/384 controls across its 24 trials. Reliability, N-body operation and
HT association remain unqualified. The
[safe-variable target](../vendor-burst/README.md#verified-hint-follow-up-bounded-short-first-streaming)
remains experimental and default off.

Default-off `CLEAN_RX_HT_ORDERED_FLUSH` selects proactive (1) or
one-shot fault-only (2) ordered RXE/crypto recovery.
`CLEAN_RX_HT_PREFIX_RECOVERY` adds count-2/RX-active instrumentation
and a 250-us TSF-triggered, restored PHY-control pulse.
Proactive flush repeatedly times out at event 10 before crypto reset.
Fault-only hardware execution succeeds under software-fault injection,
but no physical wedge occurs in the uninjected fault-only trials and
no natural prefix pulse triggers. Neither diagnosis closes reliability.
The [boundary/recovery evidence and diagnostic gates](../dev-doc/HT20_IMPLEMENTATION.md#next-mpdu-command-and-bounded-rx-recovery-experiments-2026-10-20)
record all 57 RF trials and retain failed controller reports.

The separate default-off `CLEAN_RX_HT_EARLY_FIFO` profile now repeatedly
publishes complete first nonterminal MPDUs through SRAM/F2, with explicit
PSM/C3 acknowledgement and no-credit retention. It validates native
GoodFCS, matching HT-SIG/epoch and complete software FCS before publication;
it does not advance to another body or enable HT station admission/TX.
Complete post-stop packet archives check every delivered fixture, not
only the final retained sample. Early packets use HT-SIG-derived raw HT
metadata because native terminal PHY status is not yet reliable.

Final both-role tests deliver **46/48** first bodies normally and **14/16**
with injected reset. Missing bodies coincide with fewer aggregate-prefix
observations; ordinary control misses also remain strict failures.
Bounded FIFO stop/drain and injected 16-us resets work, but do not qualify
natural persistent-busy recovery, variable-length continuation or D2.
See the [early FIFO delivery record](../dev-doc/HT20_IMPLEMENTATION.md#repeated-early-mpdu-fifo-delivery-and-complete-packet-archives-2026-10-20).

A subsequent independent vendor-monitor control retains complete fixed-source
packets in bounded post-stop pages. It verifies complete 554-byte aggregate
bodies in both roles, but is **not a qualified loss baseline**: all eight
unpolled trials stop at 30 raw packets, control servicing changes that
boundary, and faster servicing exposes a timeout. Four further clean
trials deliver 29/32 first bodies, with all observed eligible prefixes
reaching F2. Neither result identifies the cause of the pre-prefix misses.
See the [baseline discriminator](../dev-doc/HT20_IMPLEMENTATION.md#fixed-source-vendor-baseline-and-pre-prefix-discriminator-2026-10-20).

The passive advance diagnostic also has default-disabled
`CLEAN_RX_HT_COPY_PROBE` and dependent `CLEAN_RX_HT_COPY_WAIT` controls.
The non-wait probe additionally permits `CLEAN_RX_HT_COPY_END_PLUS6`,
a receiver-only copy-end bias experiment, not a corrected length rule.
It and the guard-body experiment failed to establish a valid second
body. Probe-enabled explicit aborts now retain eligible continuation
bytes before cleanup, with their original abort outcome and no delivery.
The separate `CLEAN_RX_HT_COPY_QUIET` control clears/readback-checks the
existing low stream field before EOI; it excludes wait/end-bias controls
and also failed to repair handoff. No RXE/PHY reset is added.
The former verifies overwrite of four live-SHM sentinel words and
compares two terminal prefix samples, without changing the source.
The latter tests a bounded fresh-count wait after EOI and fails explicitly
on EOF/abort/timeout. Hardware rejected that wait as a recovery strategy;
it is not enabled in the ordinary handoff profile. Neither flag expands
station or normal aggregate support. Their
[probe and stopped-fault formats](../vendor-burst/README.md#copy-progress-probe-and-rejected-wait-control)
are diagnostic evidence, not complete-body reception or a successful drain.

The separate `CLEAN_RX_HT_STIMULUS=1` gate, requiring HT data and extended
RX, permits command-12 version 6 for one exact two-by-554-byte aggregate:
HT20 mixed/BCC/800-ns GI, MCS0..7, unprotected nonce-bound QoS fixtures,
PSDU length 1118 and exactly one transmission. It rejects station mode,
extra flags and any descriptor/body change. Only MCS0 is RF-tested in the
current early-capture campaign. This does not extend normal station or
general aggregate TX support. The
[fixed-source reference profiles](../vendor-burst/README.md#fixed-two-body-stimulus)
interleave legacy/single-HT controls, retain complete early bytes and
explicitly distinguish TX completion from independent reception evidence.

The default-disabled `CLEAN_RX_AMPDU_PROBE=1` gate also requires
`CLEAN_RX_HT_DATA=1` and `CLEAN_RX_HT_RX=1`. It permits command 12 version 5
only for one exact, nonstation fixture: three 80-byte MPDUs in one HT20
aggregate. It does not enable general aggregate TX, ADDBA negotiation,
live multi-MPDU delivery or Immediate BA.

With matching diagnostic images and collector, use
`ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX MCS --ampdu-probe`.
The collector's standalone options are `--ht-ampdu-probe-tx` and
`--ht-ampdu-probe-rx`, each followed by a 28-hex-digit challenge and MCS0..7.
Reports distinguish first-MPDU capture, retained sequence bitmap and
all-subframes retention, while keeping BA/HT qualification false.
The additional default-disabled `CLEAN_RX_AMPDU_INTERFACE` gate selects
passive post-completion/body timelines (1/2) or an early-capture diagnostic
(3). With profile 3, `CLEAN_RX_AMPDU_HANDOFF=1` traces the old prefix
boundary; `=2` uses a one-copy continuation that retains all three MPDUs
at MCS0..7 in both directions and continues after bad FCS. These gates
remain default-disabled, fixed-size diagnostics, not general reception.
The optional three-bit `CLEAN_RX_AMPDU_BAD_FCS` mask (only with handoff 2)
injects explicit per-MPDU FCS faults; reports expose the mask and exclude
invalid snapshots from the retained sequence bitmap.
`multi_mpdu_radio_capture` reports this separately from live delivery,
complete-aggregate and BA qualification.
`CLEAN_RX_AMPDU_DMA=1`, requiring probe/interface-3/handoff-2, separately
tests actual per-MPDU D11 DMA/SRAM/F2 delivery. It currently delivers only
the first two fixture MPDUs, not the final MPDU. The RP2350 saves complete
delivered payloads at detail offsets `1664 + 80 * sequence` in the
2,048-byte USB report, replacing only this mode's beacon inventory; the collector
independently checks them and reports `delivered_subframe_bitmap`,
`delivery_payloads_valid`, `actual_multi_mpdu_delivery` and
`all_subframes_delivered`. These are distinct from retained PSM snapshots.
Early format-1 aggregate metadata requires the complete exact HT-SIG and
uses diagnostic F2 version 5, never the ordinary version-2 HT interface.
The gate remains default-disabled and does not enable general reception,
BA transmission, SIFS qualification, protection or HT advertisement.
The alternative default-disabled `CLEAN_RX_AMPDU_SHM=1` gate requires
probe/interface-3/handoff-2 without DMA publication. The PSM publishes a
dedicated state-16 terminal lease, including when the last/all bodies are
bad, instead of relying on a later ordinary held receive. The controller
copies the committed three-slot record only after pausing/verifying the PSM, validates
each actual body and sampled outcome, and publishes through the shared
eight-slot SRAM/F2 FIFO. All three fixture MPDUs are delivered at MCS0..7
in both directions, without duplicate delivery in the finite matrix.
It shares version 5 and the same saved-byte evidence ABI, but reports
`shm_delivery_enabled:true` and `dma_experiment_enabled:false`.
`shm_terminal` reports actual publication/claim/release counters, claimed
state, PSM TSF timestamps and controller snapshot hold time. Both timing
readers use the TSF domain; this is not measured RF EOF or SIFS.
Completed controls cover every MCS/direction; failed/partial receptions
and successful fresh repeats remain explicit in the campaign record.
It is consumed once per boot and does not generalize lengths, install BA
agreements, transmit Immediate BA, qualify SIFS or enable HT association.
See [the interface campaign and reproduction commands](../dev-doc/AMPDU_INTERFACE_INVESTIGATION.md).
See [builds, evidence layout and measured limitations](../dev-doc/AMPDU_RADIO_INVESTIGATION.md).

### Opt-in diagnostic compressed BA

`CLEAN_RX_AMPDU_BA=1` requires the SHM profile above and defaults to zero.
Command **17**, `17 | (mcs << 8)`, explicitly arms one unused pinned
three-by-80-byte capture at MCS0..7, station hardware profile 3 and basic
mask `0x150`. The PSM permit is written last after pause/readback-verified
response parameter installation. Disabled/invalid commands fail explicitly.
This is not a production BA agreement-install interface.

The PSM builds a real compressed BA from retained good-FCS, matching
fixture headers, waits for observed terminal status, and uses the hardware
SIFS TX-control path. The sender's automatic turnaround preserves the
actual returned response. Collector/pair `ba_experiment` reports the
bitmap, bounded errors and observed EOF/IFS timestamps;
`compressed_ba_return` in the pair contains actual sender receipt evidence.
Positive armed pairs require a complete valid returned BA with the same
bitmap as delivered bodies, not merely local TX counters.

The finite campaign independently validates actual returned BA frames,
but physical EOF-to-air SIFS and general recipient admission/lifecycle
are not qualified. `immediate_ba`, `sifs_timing_qualified` and
`ht_qualified` remain false; HT association remains disabled.
See [exact evidence ABI, failed controls and remaining gates](../dev-doc/AMPDU_INTERFACE_INVESTIGATION.md#diagnostic-compressed-ba-return-2026-10-19).
