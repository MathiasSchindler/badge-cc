# Vendor raw reference and deferred streaming

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Independent vendor raw receiver (2026-10-20)

The preceding boundary campaign could not distinguish wrong on-air bytes
from a clean receive/configuration defect. This campaign replaces only the
listener with the existing production vendor firmware, while retaining the
exact version-7 MCS0/channel-6 source controller and staged fixtures.
The source controller byte-compares with
`terminal-boundary/baseline-images/source-controller.bin`.

### Capability, implementation and qualification gate

The existing vendor peer had only queried GET_MONITOR. A bounded new
`--monitor-probe` mode records GET_MONITOR, radio up, channel 6, enable,
enabled readback, disable and disabled readback. Badge A passes all seven
operations, with monitor state 0/1/0, build `50ff7cae`. This result alone
does not qualify raw receive.

`reference-handoff-monitor` now pairs the unchanged clean source with
`vendor-burst/monitor.c`. The listener uses no clean controller/receive
program, credentials, association, scan, Ethernet TX or requested data
transmission. It disables MPC/PM, enables/readback-checks monitor mode and
verifies actual/target channel 6. Capture has a calibrated twelve-second
bound; normal freeze performs a 300-ms drain, disables/readback-checks
monitor mode and powers off. The existing TFT identifies passive RX.

An opt-in callback in the shared vendor transport exports complete
validated non-control SDPCM packets, including packets received while
waiting for CDC responses. This is separate from Ethernet decoding and
cannot be enabled together with the Ethernet receive queue. Raw mode
permits uninterpreted non-control channels; ordinary parsing retains its
original channel limit. Flow and credit handling remain active.

The raw trace is a distinct `MON1` format, not clean observer ABI 21.
It retains nine setup/cleanup CDC records, channel and selection counts,
and eight immutable complete challenge-bearing SDPCM packets, each up
to 640 bytes. Unrelated packet contents are not saved. Oversize and
selection-full exclusions are explicit and do not masquerade as successful
captures or clean F2 drops. Monitor mode or generic Ethernet delivery
alone cannot qualify a trial.

The collector checks every source descriptor/staging record, final staging,
raw length/complement/header/CRC and selection conservation. Its success
gate requires complete exact legacy and single-HT controls and both
complete aggregate bodies at a common transmitted index, **including
the exact FCS bytes**. Payload-only matches remain diagnostic-only.
There are no retries of individual PPDUs in this stimulus.

### Eight-trial result

Evidence: `build/clean-rx/independent-rx/`, RF build **`dc77ae04`**.
Matching UF2s, collector, build ID and source controller are archived in
`monitor-images/`; the earlier capability-only images are in `probe-images/`.

| Vendor listener | Body order | Trials | Complete exact saved MPDUs | Complete aggregate pairs |
| --- | --- | ---: | ---: | ---: |
| A | 170 to 298 | 2 | 16 | 4 |
| A | 298 to 170 | 2 | 16 | 4 |
| B | 170 to 298 | 2 | 16 | 4 |
| B | 298 to 170 | 2 | 16 | 4 |
| **Total** | Both | **8** | **64** | **16** |

Every trial retains the first eight challenge-bearing packets: exact
legacy controls at source indices 0/3, single-HT controls at 1/4, and
both aggregate bodies at indices 2/5. Their complete MAC bytes and FCS
match the independently reconstructed fixtures. All retained RF data
arrives on SDPCM channel 2; an ordinary Ethernet decoder is not used.

The 32 raw reports include 192 source records. Across the eight trials,
240 non-control SDPCM packets reach the callback. Of 166 packets carrying
the full challenge, 64 are saved and 102 intentionally exceed the
eight-slot selection; none exceed 640 bytes. Another 74 packets lack
the challenge and are counted without retaining content. These counts
are not a complete on-air receive denominator. Vendor-internal filtering
or drops are unknown, and the campaign does **not** qualify receive
reliability or claim lossless transport.

The independent ignored `audit.js` reconstructs every source fixture,
uses a separate bitwise IEEE CRC32, checks all 32 report bindings/CRCs,
192 source staging CRCs, final staging, all 64 complete raw bodies/FCS,
selection/channel accounting, four repeated cells and original restoration.
`node build/clean-rx/independent-rx/audit.js` produces retained `audit.jsonl`:
eight trials, 64 exact bodies and 16 exact pairs.

One attempted second B/170 trial fails BOOTSEL readiness before flashing
or RF. The successful repeat has a new `-retry` prefix. Badge A exposes
mass storage but not the vendor USB interface, reproducing the prior macOS
ownership quirk. After explicitly mapping A to its disk/mount, the archived
monitor UF2 is copied only to A's volume. The existing collector's
missing-live-peer cleanup returns it to BOOTSEL without starting RF.
Readiness-only and recovery-only failures remain separate from the eight
RF trials; no unrelated USB device or process is reset/terminated.

### Interpretation, validation and restoration

The same two physical radios independently decode the controlled second
body in both roles and both size orders when running vendor receive.
Together with unchanged source controller bytes and exact staging checks,
this excludes systematically incorrect second-body transmission for the
tested fixture. The observed clean second-body divergence at byte 8 is now
localized to the clean receive/configuration path, rather than an inherent
radio inability to receive the source. It is **not** yet a proof of one
particular FIFO/end/EOI rule: receiver PHY/MAC setup, RXE context, streaming,
copy ownership and early-retention timing still need causal isolation.
No new guessed register masks or purported receive repair are promoted.

ASan/UBSan tests cover raw versus ordinary channel admission, malformed
envelopes, raw callbacks during CDC waits, RX-mode exclusion, session reset,
no Ethernet TX, exact 640-byte storage, overflow/oversize accounting,
immutable slots, CRC rejection, payload truncation, byte-8 corruption,
bad/missing FCS and the exact common-index/FCS qualification gate.
The original vendor peer and ordinary clean handoff/copy profiles retain
native tests and freestanding links with no undefined runtime symbols.
Final shared harness outputs are left on ordinary `reference-handoff-copy`.
Its source and probe-observer controllers remain byte-identical to the
preceding RF archives; the default production controller also remains
byte-identical to the prior verified default.

Fresh complete originals are saved before any flashing. Both full
16-MiB flashes are restored afterward and independently saved as
`restored-{A,B}-full.bin`, byte-compared and SHA256-verified against those
backups. Hashes retain A `8cda7a00...23628` and B `bc3f972d...21e2`.
`restore-{A,B}.log`, `readback-{A,B}.log` and `bootsel-{A,B}.log` record
restoration and final serial-targeted BOOTSEL checks. No private AP
credentials are read or provisioned.

This supplies the missing independent multi-body stimulus proof. It does
not mark D2 complete: clean handoff/abort recovery, repeated variable-size
buffering, secure BA lifecycle, Immediate BA/SIFS, mandatory protection and
negotiated HT interoperability remain open.

## Deferred pre-handoff streaming repair (2026-10-20)

The independent vendor receiver established correct transmitted second-body
bytes. The next causal control keeps the exact same source and changes
only the clean receiver's streaming threshold. The working 80-byte fixed
diagnostic is below the ordinary 128-byte threshold; the failing 170/298
bodies cross it. The earlier clear-before-EOI test happened **after**
streaming had already started and did not test this distinction.

`CLEAN_RX_HT_COPY_DEFER_STREAM=1` is a default-disabled control in
`frame_limits.h`/`ht_observer.h`. The `reference-handoff-defer` profile uses
384 bytes instead of 128, keeping both bounded bodies below the threshold
while remaining below the existing 506-byte staging-overflow bound.
It requires the ordinary non-wait copy probe and excludes end bias,
stream clear and guard-body geometry. No source, copy-end, EOI, reset,
cleanup, payload/FCS predicate or receive-size bound changes.

### Matched causal result

Evidence: `build/clean-rx/stream-threshold/`, with matching RF
`defer-images/` and `baseline-images/` archives. The 60,372-byte receiver
controllers have exactly one differing byte, at offset 34,806:
baseline `0x10`, deferred `0x30`. That changes one bit encoding the
128-to-384 comparison immediate in the existing streaming instruction.
Their source controllers byte-compare with each other and the independently
decoded vendor-monitor source. All 1,132 staging bytes remain regenerated,
written and verified; every source record passes independent reconstruction.

The campaign runs deferred, matched baseline, then deferred again,
covering both badge roles and both body orders:

| Good fixture | Trials | Exact complete valid pair | Full qualification |
| --- | ---: | ---: | ---: |
| Baseline, threshold 128 | 4 | 0 | 0 |
| Deferred, A receives 170 to 298 | 2 | 2 | 0 |
| Deferred, A receives 298 to 170 | 2 | 2 | 0 |
| Deferred, B receives 170 to 298 | 2 | 2 | 2 |
| Deferred, B receives 298 to 170 | 2 | 2 | 2 |

All twelve good trials retain the exact first body at source index 2.
All four baselines reproduce second-body divergence at byte 8: three
bad-FCS exits and one explicit abort. All eight deferred trials instead
retain the exact second body/FCS at its correct logical RXE count:
304 for a 298-byte body and 176 for a 170-byte body, with good outcome.
Two pre-handoff hardware samples show RX control `0x800` rather than
baseline `0x802`. Owner, epoch and handoff phases also match.

The collector requires that readback evidence for deferred-profile success;
a missing or differently owned trace cannot qualify. It reports the
threshold, enabled control and readback verification explicitly. The
archived images were tested before the final host-only helper/test
extraction; final source and receiver controller bytes still compare
identical to those RF archives.

The four good A-listener trials remain **failed full trials** because
interleaved controls are missing. One also lacks a required late recovery
control. These losses are not relabelled as successful receive reliability
or hidden by the complete pair. The full good-trial gate is unchanged.

### Bad-frame and malformed-input controls

The same deferred receiver is exercised once in every role/order for each
of bad-first, bad-last, bad second delimiter and an eight-byte-overlong
second delimiter claim. Only source aggregate index 2 carries the fault.

| Case | Trials | Later/exact good pairs | Strict negative qualification |
| --- | ---: | ---: | ---: |
| Bad first FCS | 4 | 4, all at source index 5 | 4 |
| Bad last FCS | 4 | 0, intentionally invalid second | 4 |
| Bad second delimiter | 4 | 4, at source index 5 or 8 | 0, diagnostic-only |
| Overlong second-body claim | 4 | 1, at source index 5 | 0, diagnostic-only |

All four bad-first trials retain the rejecting bad-FCS terminal record,
then capture an exact later pair and required early/late recovery controls.
All four bad-last trials retain the complete deliberately corrupted
second bytes at the exact length, with matching owner/epoch and bad-FCS
outcome, and receive the required recovery controls. A generic bad-FCS
exit is still insufficient. Other missed controls remain reported; the
existing negative gate requires its defined early/late recovery set,
not every ordinary good-fixture control.

Three overlong-claim trials retain source index 2's first body but correctly
do not advance, because the decoded next length differs from the admitted
170/298-byte geometry. Their absent advance/readback is not interpreted as
measured active streaming. The fourth misses the initial candidate and
captures a later good pair. Malformed-delimiter and overlong-claim
negative qualification remain unsupported and always return nonzero;
one trial of each also misses required recovery controls. These do not
prove general abort/truncation recovery.

### Accounting, validation and restoration

There are **28 RF trials, 112 reports and 672 source records**. All
**953 completed captures** reconcile through posted/consumed/F2 counters
without delivery drops. This does not count RF/pre-prefix losses as
delivered. The independent ignored `audit.js` reconstructs all good/fault
fixtures and staging CRCs, validates every report binding/CRC, all retained
first/second bytes and independent bitwise FCS, hardware threshold
readbacks, one-byte controller difference and full restoration.
It finds 17 exact valid pairs (including later recovery pairs), four exact
deliberately invalid second bodies and 25 committed continuation snapshots.
Twelve trials pass existing strict gates: four good and eight bad-FCS
negative controls. All failures remain retained.

One B/298 attempt fails BOOTSEL readiness before flashing/RF; its repeat
uses a new `-retry` prefix. Badge A's mass-storage-only/vendor-interface
ownership quirk is recovered only after verifying its current serial to
disk/mount mapping. The archived observer is copied to that badge's
volume; missing-peer collector cleanup returns it to BOOTSEL without RF.
Readiness/recovery-only failures are excluded from the 28 RF trials.

Native ASan/UBSan tests exercise every RX count and 256 control-state
patterns around the exact streaming threshold, preserved unrelated bits,
idempotence, snapshot ownership/bounds and no TX. Added host tests reject
active-stream readbacks, wrong/missing owner or epoch, incomplete phases
and malformed headers. The default production controller and ordinary
128-byte baseline remain byte-identical to prior verified images.

Fresh complete originals precede all writes. After the campaign both
16-MiB originals are restored, independently saved as
`restored-{A,B}-full.bin`, byte-compared and SHA256-verified against those
backups. A retains `8cda7a00...23628`; B retains `bc3f972d...21e2`.
Final serial-targeted `bootsel-{A,B}.log` checks confirm both in BOOTSEL.
No AP credentials are read or provisioned.

This demonstrates that **pre-handoff streaming is causal** for the tested
clean SHM-only continuation failure. It strongly implicates FIFO/copy
ownership interaction rather than wrong transmitted bytes; it does not
establish the undocumented internal DMA/FIFO mechanism. Merely clearing
the stream field after first-body retention was too late in the rejected
earlier control. The 384-byte control remains gated: it is not a general
solution for 554-byte bodies, variable-length/long aggregates or the
ordinary production DMA receive path. D2's loss accounting, malformed/abort
recovery, general buffering, BA/protection and negotiated HT gates remain.

## Streaming byte destination and DMA discriminator (2026-10-20)

The threshold control above proves a streaming-dependent failure but does
not locate the missing bytes. The ordinary Cortex receive path retained
only a completed descriptor's 38-byte header before clearing its entire
buffer. Inspecting only those old headers could not distinguish a broken
SHM copy from bytes already transferred elsewhere.

Added default-off `CLEAN_RX_HT_COPY_DMA_PROBE=1` and the matched
`reference-handoff-dma` / `reference-handoff-defer-dma` profiles. This is a
read-only data-path diagnostic, not a receive repair. It changes no PSM
instruction, RXE command, DMA ring programming, on-air fixture or cleanup
sequence. Existing DMA shutdown gains a diagnostic readback check; an
unstopped engine produces a controller error instead of a purported scan.

Each completed descriptor is examined before its existing clear/repost.
The first experiments showed why this was insufficient: an A-listener
baseline abort had **no completion after the handoff**. The final diagnostic
also examines all 32 whole buffers after verified radio/DMA shutdown, so
unfinished descriptors cannot escape simply because no header completed.
Shutdown status/control read back zero in every full-ring RF trial.

The core scans all 3,968 bytes of each descriptor for the fresh 14-byte
challenge copied from the retained first MPDU. It counts matching first
and second QoS prefixes and checks their actual FCS before selection limits.
It saves two 768-byte windows, preferring an aggregate over ordinary
traffic and a second body over a first in the second slot. Matched-window
metadata includes descriptor index, completion count, DMA status, current
handoff owner/epoch, held state, original header length and window offset.
Frozen matches are not labeled completed packets. Current handoff context
does not assign the DMA packet to that handoff; independently reconstructed
source bytes and FCS establish actual identity.

The 1,728-byte version-5 `DMO1` trace occupies the former MPDU proof-sample
area at 2560; observer ABI becomes 22 only for this profile. FIFO delivery
and accounting still run, but the ordinary proof-sample count stays zero.
**Every trial remains diagnostic-only with exit 1.** In particular,
`exact_pair:false` is intentional for this profile even when separate
early/continuation SHM snapshots contain an exact pair. This does not
silently relax or replace the existing strict F2 pair/control gate.

Evidence: `build/clean-rx/stream-dma/`. Twenty-eight RF trials include the
initial completed-only, counter-qualified, stopped-ring, priority-retention
and final first/second-FCS variants. No failed trial is removed:

- 112 complete reports and 672 source records;
- 757 completed captures conserved through F2 with zero delivery drops;
- 1,404 counted buffer examinations, including repeated completed/frozen
  inspections, **not a wireless receive denominator**;
- 14 saved challenge-bearing DMA windows, each containing independently
  reconstructed complete source bytes and valid FCS;
- 14 exact retained-SHM pairs across the deferred diagnostic trials;
- final baseline/deferred builds `c84deea2` / `74910108`.

Final version-5 result, repeated across both roles and both body orders:

| Listener / first body | Baseline captures | Baseline terminal | Baseline valid first / second QoS bodies in DMA | Deferred captures | Deferred SHM second |
| --- | ---: | --- | ---: | ---: | --- |
| A / 170 | 3 | abort, RXE 190 | 0 / 0 | 38 | exact 298 + valid FCS |
| A / 298 | 3 | abort, RXE 66 | 0 / 0 | 37 | exact 170 + valid FCS |
| B / 170 | 41 | bad FCS, RXE 304 | 7 / 0 | 36 | exact 298 + valid FCS |
| B / 298 | 34 | bad FCS, RXE 178 | 7 / 0 | 35 | exact 170 + valid FCS |

All eight retain the exact first body at source index 2. Every baseline
continuation first diverges at byte 8. On B, the first retained DMA example
is the exact later legacy control; the prioritized example is the exact
first MPDU of aggregate 5, at physical DMA offset 44 (38-byte offset plus
six-byte PLCP). The per-buffer counters independently count all seven
later aggregate first bodies as valid and **zero second-body prefixes**,
rather than inferring absence from the two saved windows.

On A, baseline DMA buffer contents are all zero after the abort and there
are no later completions to inspect. On B, later legacy/HT controls and
aggregate first bodies really do stream through DMA. This excludes a
generally broken or permanently stalled DMA engine as the explanation
for B's reproducible continuation corruption. It does not explain A's
stronger abort/recovery failure.

Every deferred case has an exact valid SHM second body. No challenge reaches
DMA there: these 170/298-byte bodies never reach the 384-byte streaming
threshold. That is expected for this SHM-only control, not evidence DMA
independently received the pair. Both A-listener final deferred trials
still miss required interleaved controls; the two B-listener controls
receive them all. None is promoted to production/D2 qualification.

**Causal interpretation:** the first-body stream path itself can deliver
correct aggregate bytes. The broken second prefix/body does not merely
reappear as a complete second body in the inspected DMA stream. Together
with independent vendor reception and the one-bit threshold reversal,
this narrows the remaining defect to RXE/copy/streaming ownership or byte
position at the aggregate handoff, upstream of DMA completion. It does
not prove the undocumented internal RX-buffer pointer, first-body FIFO
publication or latch semantics. Intermediate bytes flushed before a
completion or before shutdown cannot be recovered by this scan.

The next discriminator must separate streaming read-position/context
changes from first-body publication timing without reviving the rejected
80-byte stream-after-arm/header/EOI controls as purported fixes. Any
per-MPDU finalization experiment needs its own bounded progress, exact
byte/FCS and error/recovery proof. The 384-byte gate still cannot be
generalized blindly to 554-byte bodies or long aggregates.

Native ASan/UBSan tests cover full-buffer challenge boundaries, no-match/
partial-match exclusion, immutable slots, aggregate/second-body priority,
both FCS lengths, corrupted FCS, malformed metadata/counters and shutdown
readbacks. Both C3/RP images link freestanding. Three invalid gate
configurations fail compilation. The instrumented baseline PSM object
equals the uninstrumented one. The two final receiver images differ only
at zero-based byte 35762, `0x10` versus `0x30`; the source controller is
byte-identical to the independently vendor-proven image. Production
default firmware remains byte-identical. The independent bitwise-CRC
`audit.js` checks all reports/source records, retained bodies, accounting,
one-byte control and restoration; `audit.jsonl` passes.

Fresh complete 16-MiB originals precede all writes. Both are restored,
independently reread, byte-compared and SHA256-verified: A
`8cda7a00...23628`, B `bc3f972d...21e2`. Final serial-targeted checks
confirm both badges in BOOTSEL. No credentials are read or provisioned.
D2, malformed/abort recovery and HT association remain open.


