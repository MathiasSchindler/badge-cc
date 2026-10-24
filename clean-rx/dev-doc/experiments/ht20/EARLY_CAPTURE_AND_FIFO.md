# Fixed-stimulus early capture and FIFO

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Fixed-stimulus early-capture qualification (2026-10-19)

**Result: complete 554-byte nonterminal early bodies now repeat in both
roles with independently checked bytes and multi-body geometry. General
receive reliability and physical busy recovery remain unqualified.**

Evidence, matching UF2s/controllers/collectors, failed trials and the
independent bitwise-CRC/fixture audit are retained under
`build/clean-rx/early-reliability/`. `audit.js` regenerates the fixed source
without calling firmware helpers; `audit-final.jsonl` contains each result.
Role 0 observes on A; role 1 observes on B.

### Deterministic source and evidence

The vendor's focused submissions still take roughly 1.8..2.7 ms per
512-byte Ethernet frame. An aggregation request and received BA count
cannot establish a multi-body PPDU. The new separate clean source sends
eight cycles of:

1. One 554-byte legacy OFDM control at 6 Mbps.
2. One 554-byte nonaggregated HT20/MCS0 control.
3. One HT20/MCS0 aggregate containing two complete 554-byte MPDUs.

Forty calibrated milliseconds separate transmissions. The aggregate PSDU
is 1118 bytes: delimiter + 554-byte body + two padding bytes + delimiter +
554-byte terminal body. Source addresses, sequence numbers, payload nonce,
deterministic bytes, delimiter CRCs, HT-SIG CRC and both MPDU FCS values
are checked. Frames are broadcast-addressed, unprotected raw PHY fixtures,
not a negotiated unicast A-MPDU exchange; this does not qualify protected
AP traffic or either body's general delivery.

`CLEAN_RX_HT_STIMULUS` defaults off and requires HT data/extended RX.
Command-12 version 6 admits only this exact fixture with one attempt, no
extra flags and no station mode. Native tests cover MCS0..7; RF uses MCS0.
The existing finite aggregate and new source share HT-SIG construction.
The source wrapper runs a separate clean controller, not vendor firmware,
and does not read or provision credentials. The observer stays TX-inhibited
and passive. Neither default station behavior nor HT advertisement changes.

Every source attempt exports its command, raw host ticks, TSF interval,
logical length, descriptor CRC, PLCP, sampled TXE controls, three actual
completion conditions and zero PHY error. Staging/template/control checks
precede transmission; the final complete aggregate descriptor is retained.
These records establish checked source execution, **not independent OTA
receipt for every attempt**. Actual multi-body reception is established by
the separate badge's matching HT-SIG/1118-byte geometry and a complete,
nonce-bound, FCS-valid, byte-for-byte expected nonterminal body.
There is no third receiver or independently measured per-attempt RF loss
denominator. A second complete body is not claimed.

The observer validates every delivered control's complete bytes and
passive metadata, retains a cumulative index bitmap and duplicate/invalid
counts, and separately saves a complete legacy wrapper/body. The collector
checks that saved body independently and correlates single-HT prefix
bytes with scheduled indices. Prefix-log overflow disqualifies absence
inferences. The stronger diagnostic pass requires all sixteen controls
and one exact early body; this conservative pass gate is not a general
zero-loss RF specification.

### Fixed matrix and retained failures

| Corrected fixed-source profile | Trials per role | Exact early body, both roles | All-control pass |
| --- | ---: | ---: | ---: |
| Strict rearm, first matrix | 4 | 8/8 | 4/8 |
| Natural PHY-reset gate | 4 | 8/8 | 4/8 |
| Injected PHY-reset branch | 2 | 4/4 | 3/4 |
| Injected park-only branch | 1 | 2/2 | 1/2 |
| Final strict/reporting check | 1 | 2/2 | 1/2 |
| **Total** | **12** | **24/24** | **13/24** |

Both badges therefore retain an exact early body in 12/12 corrected
fixed-source boots. One body is retained per boot; this is not 192/192
aggregate-body retention or a per-frame capture probability.
The stricter all-control passes are 3/12 observing on A and 10/12 observing
on B. That finite asymmetry does not establish a defective badge.

Ten earlier fixed trials remain explicit instrumentation failures:
two pilot collectors wrongly required sampled TXE `0x56a` to equal the
logical template length; eight subsequent trials wrongly required the
ordinary negotiated HT metadata in the deliberately passive observer.
The final collector retains `0x56a` raw without assigning undocumented
length semantics, and validates the actual passive metadata explicitly.
All ten nevertheless contain independently valid 554-byte early bodies.
They are excluded from the corrected reliability matrix, not relabelled
as passing runs or silently discarded.

### Loss isolation

Across the 24 corrected trials, complete control delivery is 180/192
legacy and 181/192 single HT, with no corrupt or duplicate control
accepted. These are device-validated full-frame bitmaps, not independent
host captures of all 384 frames; the saved complete bodies and native
validators provide separate checks.

Ten missing single-HT controls also lack their expected HT prefix in a
nonoverflowed log. One has a matching prefix but no complete F2 delivery:

- `strict-v3-role1-r0`: control 19 has no HT prefix; all eight legacy
  controls arrive, including neighboring indices 18 and 21. There is no
  exhausted-rearm fault. This isolates a selective pre-prefix miss from
  the old globally stuck `0x804` condition and downstream USB delivery.
  It does not identify RF interference, PHY detection or pre-log receive
  ownership as the unique physical cause.
- `reset-role0-r0`: control 16 reaches prefix 10/epoch 38 with complete
  560-byte RXE length and terminal good outcome 1, but not the control
  delivery bitmap. Frozen accounting is captures 47 = posted 45 + dropped
  2, consumed 45, pending zero. This places that miss after prefix/good
  capture and before host delivery; it does not uniquely assign either
  aggregate drop to that specific frame. Controls 13 and 22 separately
  lack prefixes. No PHY recovery activates in this trial.

The live RP2350 fixture comparator adds host-side CPU work. These diagnostic
loss counts must not be extrapolated to ordinary station reception;
instrumentation overhead remains a candidate for post-capture losses.

Thus not every HT-only miss is an RXE flush timeout, and not every missing
delivered control is a failed HT PHY decode. Extending the flush wait or
repeating the PHY reset cannot, by itself, explain both classes.

### Bounded reset and park-only controls

All four injected PHY controls execute exactly one checked reset, retain
TX-inhibit masks 15/15 and subsequently capture the exact HT early body.
The early epochs are later than the preserved injected first fault;
PSM restart attempts remain zero. This proves reception after actual
reset execution, not recovery from a natural physical busy state.

`CLEAN_RX_HT_REARM_PARK` adds a separate default-disabled no-reset
control using the same handoff, TX guards and strict retry. It performs
no PHY-reset writes, waits at least 16 TSF units with a 65,535-poll bound,
and requires unchanged PHY readbacks. The two injected park trials record
completion state 2, actual reset count zero, guards 15/15, and a later
exact early body in both roles. Full service takes 29 TSF units, compared
with 16 for injected reset: **not an exactly time-matched control**.
Observer ABI20/WND9 adds modes 13/15; matching archived ABI19/WND8
collectors remain necessary for earlier trials.

A separately fixed vendor/AP matrix has eight natural reset trials and
four natural park-only trials:

- Three real reset activations (`vendor-reset-role1-r0/r2/r3`) exhaust
  their first waits at RXCTL `0x804`, RXE 924/982/986 and raw status
  `0x4000/0x4140`. A reset executes once, readbacks/guards pass, but RXCTL
  stays `0x804` and the bounded retry fails with reason 3.
- One real park activation (`vendor-park-role1-r0`) has RXE 978 and the
  same raw status group. Actual reset count stays zero, complete service
  takes 30 TSF units, and the retry likewise faults. These raw groups are
  not assigned undocumented hardware meanings.
- The remaining eight do not activate recovery. One,
  `vendor-park-role0-r1`, independently captures another protected
  554-byte vendor early body: prefix 3, epoch 266, PSDU 1118, valid FCS,
  fresh nonce MAC, matching HT-SIG/CRC and nonterminal geometry.
  It is not evidence of park recovery, which never ran.

No new naturally busy state recovers. The earlier single midstream
recovery remains valid evidence for that episode only. Persistent
startup-like busy handling still needs a hardware-grounded RXE-specific
solution; enabling repeated resets or PSM restarts would hide the failure.

### Audit, validation and preservation

All 46 actual trials, including instrumentation failures, are retained.
The independent audit checks 276 nonce/build-bound report CRCs and 881
window rows with no window-capacity loss. Forty-two successful frozen
exports reconcile 5643 captures, including 130 explicit drops; the four
genuine faulted captures remain errors.

Native ASan/UBSan tests cover all source rates/indices, exact delimiters,
padding, HT-SIG, FCS, disabled/station gates, every command bit and every
byte mutation of a representative aggregate, passive control metadata,
corrupt control/prefix rejection, and sender evidence thresholds.
PSM reply success/rejection/absence/boundary and strict retry tests remain
enabled. Builds, unresolved-symbol checks and native tests pass for the
ordinary, window, reset, park and fixed-source profiles. The ordinary
controller is byte-identical to the preceding verified default image.

The final strict observer and fixed-transmitter controller binaries also
byte-compare with their archived RF-tested images; the final natural-reset
controller matches its RF-tested image.

The original full flashes were saved fresh before testing. Both complete
originals are now restored and independently reread:
`restored-A-full.bin` and `restored-B-full.bin` each have 16777216 bytes,
byte-compare with the fresh originals, and have SHA256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

Both complete load/verify operations succeed, and final serial-targeted
checks confirm both badges in BOOTSEL.

The first D2 work package stays open for persistent busy recovery and
remaining pre-prefix/post-capture losses. Repeating the 554-byte body and
providing confirmed multi-body stimulus are now completed foundations.

## Passive observer FIFO qualification (2026-10-19)

Evidence: `build/clean-rx/observer-fifo/`. The independent `audit.js`
checks all 28 RF trials / 168 raw reports, source fixture CRCs and saved
descriptor bytes, exact early MPDU/SIG/epoch ownership, frozen transport
counts and final full-queue stop retention. `audit.jsonl` retains per-trial
failures and cohort totals. Seven additional BOOTSEL-readiness failures
never reached RF and remain in their readiness logs; macOS USB ownership
was released by the user, not by terminating applications.

### Ownership fix

The passive observer previously released a completed capture even if F2
posting returned zero for unavailable credit or DMA descriptors. Its
saturating drop counter recorded that loss, but no controller FIFO held
the frame. The station path already had an eight-slot SRAM FIFO.

`CLEAN_RX_HT_OBSERVER_FIFO=1` now reuses that FIFO. It defaults off and
requires the passive observer. When full, the PSM's completed SHM capture
remains held instead of being overwritten. A gated 16-byte side array
latches each slot's prefix correlation before releasing SHM; delayed F2
publication no longer reads the live next-frame correlation. Ordinary
station accounting is not applied to these passive frames.

Stop freezes MAC/PSM reception, copies a completed held-SHM frame when
space becomes available, and drains all queued captures. It does not
restart the PSM. The RP continues receiving while waiting for the stopped
controller and consumes the last posted packet before saving diagnostics.
FIFO reports require exact `captured == posted == consumed`, with no
dropped frames or outstanding packets. Controller draining has both
500000-microsecond and 1000000-poll limits; the host has independent raw
timer/poll limits. Invalid occupancy and exhausted bounds fault explicitly.
An unfinished PPDU can still be aborted at stop; this is not a promise to
finish an arbitrary in-progress radio reception.

### RF matrix and retained failures

All source runs attempted the same 24 one-shot channel-6 transmissions:
eight each of legacy OFDM, single HT20/MCS0 and exact two-by-554-byte
aggregates. No vendor image, association or credential provisioning was
used. Both badge roles were exercised.

| Cohort | RF trials | Controller captures | Exact 554-byte early body | All 16 controls |
| --- | ---: | ---: | ---: | ---: |
| Initial FIFO, four trials per role | 8 | 484 | 8/8 | 4/8 |
| Timed host pause, two per role | 4 | 176 | 4/4 | 0/4 |
| Initial stop while full, two per role | 4 | 58 | 4/4 | 0/4 |
| Stale-profile trials, excluded from normal qualification | 4 | 105 | 4/4 | 0/4 |
| Final flag-bound normal FIFO, two per role | 4 | 264 | 4/4 | 0/4 |
| Final stop including held SHM, two per role | 4 | 68 | 3/4 | 0/4 |

Every one of the **1155 completed controller captures** was posted and
consumed, with zero transport drops, duplicate controls, corrupt accepted
controls or final pending packets. No strict first fault or PSM restart
was recorded. These totals include failed trials; they do not establish
that every transmitted RF frame reached controller capture.

Normal FIFO trials repeat exact nonterminal early bodies in **12/12
boots, six per role**, while the stricter all-controls gate passes only
4/12. They receive 88/96 legacy and 87/96 single-HT controls. Seven missing
single-HT controls have no logged matching prefix. Two more match a logged
prefix but exit with bad FCS and short RXE lengths:

- `verified-fifo-role0-r0`, control 13: prefix 7, epoch 41,
  outcome 2, RXE length 51 instead of 560.
- `verified-fifo-role1-r0`, control 13: prefix 7, epoch 56,
  outcome 2, RXE length 83 instead of 560.

Those two are **pre-capture radio exits**, not losses of completed
controller frames on F2. Matching HT-SIG and saved body-prefix bytes bind
them to the controlled source. The evidence does not distinguish
interference, PHY truncation or an internal receive-abort cause, and no
undocumented status-bit meaning or reset mask is inferred.

The timed-pause profile withholds host frame polling for 600000
SOF-calibrated microseconds after control 1. All four trials observe all
eight SRAM slots occupied, one F2 credit outstanding, and cached PSM state
14 holding the next frame. After host polling resumes, every completed
controller capture drains. The intentional RF misses remain strict-gate
failures rather than being relabelled as a full RF success.

The full-stop profile withholds polling until stop. In the four final
trials, the last pre-stop timeline counts are 16, 14, 20 and 14; frozen
counts become 17, 15, 21 and 15. Each conserves the additional completed
held-SHM frame, not just the eight SRAM slots. All four pass the explicit
full-queue ownership proof. The last trial misses every complete early
aggregate body, which is retained as a failure rather than omitted.

### Build-profile correction and validation

Four trials labelled `final-fifo-*` unexpectedly reused the preceding
stop-test image. Their source flags changed only host code, leaving the
controller bytes and old build-ID inputs unchanged. Build-ID-dependent
RP objects and collector were consequently stale. Three actually ran
full-stop pauses; the fourth never triggered its pause. These trials
remain available but are excluded from normal-FIFO qualification.

Build IDs now include `REF_FLAGS` and `REF_STIMULUS`. A regression switches
normal FIFO to full-stop FIFO and verifies different build IDs and RP
observer objects while the C3 controller remains byte-identical to the
final RF-tested ownership code. Both profiles run native ASan/UBSan
tests. Existing non-FIFO frozen-report rules remain tested, while FIFO
tests additionally reject dropped or unconsumed captures. Queue tests
cover immutable IDs across 16/32-bit counter wrap, full occupancy,
one-credit draining, invalid occupancy, exact deadline boundaries and a
nonadvancing clock bounded by polls.

An initial pause build exposed an unwanted `__aeabi_uldivmod` dependency.
The calibrated pause threshold now uses bounded 32-bit arithmetic; final
RP ELFs pass the existing unresolved-symbol checks and remain nolibc.
The ordinary controller passes native tests and remains byte-identical
to `phy-reset-investigation/verified-default/cyw43_rx_fw.bin`.

Fresh full originals were saved before experimental flashing. The final
restoration/readback evidence is `final-restore-{A,B}.log`,
`final-readback-{A,B}.log`, `final-restored-{A,B}-full.bin` and
`final-bootsel-{A,B}.log`. Both complete 16-MiB readbacks byte-compare
with the fresh originals and retain the original SHA256 values recorded
above. Both final serial-targeted checks confirm BOOTSEL.

This completes a bounded passive-transport foundation, not D2 work
package 1 or 3. Persistent RXE busy recovery, pre-prefix and truncated-body
losses, repeated variable-length aggregate reception, aggregate handoff
negative cases and mandatory station MAC integration remain unqualified.
HT association advertisement stays disabled.


