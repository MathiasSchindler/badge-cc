# Copy progress, replay and destination placement

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Continuation streaming and moving copy boundary (2026-10-20)

The DMA experiment did not separate first-body streaming from continuation
streaming. Added default-off `CLEAN_RX_HT_COPY_STREAM_PHASE`, requiring the
existing deferred, non-wait copy probe. The first body always uses threshold
384. An exact one-shot advance state of 1 selects:

- phase 1, `reference-handoff-stream-hold`: remain at 384;
- phase 2, `reference-handoff-stream-resume`: resume at 128;
- phase 3, `reference-handoff-stream-late`: resume at 256.

Advance remains 1 after the retained handoff: the selected continuation
threshold also applies to subsequent traffic for the rest of the trial.
These are matched diagnostic controls, not a per-MPDU streaming repair.
Invalid advance states retain the initial threshold. All three execute
the same native PSM branches; only the comparison immediate changes.
No source-controller bytes, EOI, copy endpoint or RXE command change.

The initial eight trials retain exact pairs in all four hold cells and
both 170-byte resumed-continuation cells. Both 298-byte resumed
continuations instead watchdog at RXE count 134. The original terminal
copy probe does not snapshot watchdogs, so those two failures retain
their terminal log but no continuation-body snapshot. Their after-arm
readbacks still show the first-body count, making stale-count activation
a plausible hypothesis at that point, not an established cause.

Added phase-only version-3 copy diagnostics without replacing normal
F2 proof samples or changing observer ABI 21. Copy words 36..43 retain
the first streaming activation after advance:

| Word | Meaning |
| --- | --- |
| 36, 37 | RXE count immediately before and after the streaming write |
| 38, 39 | RX-control readback and status1 after the write |
| 40, 41 | TSF low/high before the write |
| 42, 43 | Commit marker 1 and receive epoch |

The marker is cleared at copy arm; subsequent stream events cannot
overwrite a committed event. A matching epoch is mandatory before
attributing activation to the retained continuation. A hold or short
256-threshold trial can legitimately record streaming of a later packet;
the collector explicitly labels that event as another epoch, not proof
that this continuation streamed.

Only phase profiles additionally retain a bounded continuation snapshot
on native watchdog outcome 6. Existing owner/epoch, 50..306-byte count
bounds and immutable terminal-copy commit guards still apply. The
watchdog returns to its original cleanup with outcome 6; it is never
converted to good completion or delivered. Explicit abort remains
outcome 5. Activation proof is separate from complete-body/FCS/F2 and
control-receipt qualification.

Evidence: `build/clean-rx/stream-resume/`. Twenty-six actual RF trials
include the initial eight, eight activation-instrumented hold/resume
cells, four 256-threshold cells and six reverse-order repeats. Five
BOOTSEL-readiness failures and one USB-only recovery attempt are also
retained, but are not counted as RF trials. All RF source controllers
are byte-identical to the independently vendor-verified source.

| Continuation threshold / second body | RF trials | Exact SHM/F2 pairs | Strict passes | Retained failure boundary |
| --- | ---: | ---: | ---: | --- |
| 384 / 170 or 298 | 10 | 10 | 7 | none in retained pairs; three control-receipt failures |
| 128 / 170 | 4 | 4 | 3 | one control-receipt failure |
| 128 / 298 | 6 | 0 | 0 | RXE 134; all four instrumented copies first differ at byte 128 |
| 256 / 170 | 2 | 2 | 2 | threshold not reached in the retained continuation |
| 256 / 298 | 4 | 0 | 0 | RXE 258; all four copies first differ at byte 252 |

Both badge roles reproduce every row. The reverse repeats change both
role order and threshold order. Every retained first MPDU is byte-exact
with valid FCS. One initial 170-byte resumed continuation belongs to
source index 5 rather than index 2; this is a valid later aggregate,
not proof that the initial aggregate was retained. All other retained
first bodies are source index 2. Twelve of the sixteen exact pairs also
receive every required interleaved control; the four control-loss
trials stay failed despite their valid pairs.

The instrumented 128-threshold trials consistently commit activation at
RXE **130 before and after** the write, with matching continuation epoch
and control `0x802`. Their 298-byte continuations stop at 134 with exactly
128 canonical bytes. Thus streaming did not activate from the stale
176/304 first-body count in these observations. The superficially
suggestive arithmetic `134 = 304 - 170` is not evidence of first-body
length subtraction.

Moving only the threshold to 256 commits matching-epoch activation at
**258 before and after**, and moves the canonical-byte boundary to
**252**, with terminal count 258. Three trials watchdog; one explicitly
aborts. Bytes beyond that boundary are not a valid continuation or FCS
and cannot be interpreted as newly received body data. In contrast,
the 170-byte continuation can stream at 128 and still complete exactly,
and hold384 retains every tested full pair.

**Causal conclusion:** loss of continuation-copy progress follows the
streaming transition itself, even after an unstreamed first body, and
the loss position moves when the threshold moves. A stale first-body
count is not needed to reproduce this failure. First-body streaming
cannot be its sole cause. This is stronger than a fixed-length,
bad-second-body or generally broken-DMA explanation, but does not
identify the internal RXE/FIFO read pointer, copy ownership or latch
semantics. In particular, the two thresholds do not establish one
universal count-to-copy offset. Do not invent a subtract-first-length
rule or treat streaming as categorically incompatible with all
continuations.

The independent bitwise-CRC audit verifies 104 raw reports, 624 source
records and final staged fixtures, exact first/second bodies and FCS,
normal retained F2 samples, activation epochs/readbacks, copy boundaries
and complete capture/post/consume conservation. All 896 raw captures
are conserved with no delivery drops; this is not a count of distinct
OTA MPDUs or an RF reliability denominator. Failed byte/FCS/control
trials remain separate from the twelve strict successes.

Native ASan/UBSan tests cover every RX count and 256 control states,
advance 0/1/2/0xffff, unrelated control-bit preservation, idempotence,
phase-specific watchdog snapshot/return behavior and invalid owner,
epoch, version, commit, count and streaming readbacks. All three C3/RP
profiles link freestanding; invalid phase 4 and missing parent gates
fail compilation. Default native tests pass and the production image
remains byte-identical.

Initial hold/resume receiver images differ only at zero-based byte
60310 (`0x30` versus `0x10`). Instrumented hold/resume/late images
differ only at byte 60334 (`0x30`, `0x10`, `0x20`); both hold comparisons
are single-bit reversals. Final host-gate validation builds reproduce
the corresponding RF C3 images exactly. The audit also checks these
identities, rather than trusting profile names.

Fresh complete 16-MiB originals were saved before all experimental writes.
Both full flashes are restored and independently reread, byte-compared
and SHA256-matched: A `8cda7a00...23628`, B `bc3f972d...21e2`.
Final serial-targeted checks confirm both in BOOTSEL. The macOS USB
ownership hiccup was recovered through the serial-mapped mass-storage
volume and bounded collector cleanup, without killing unrelated
processes. No credentials were read or provisioned.

The next repair must preserve both body copying and RX-buffer ownership
when streaming becomes necessary, rather than just moving this cutoff.
Hold384 still cannot cover a 554-byte body within the known staging
overflow limit. Loss-accounted longer-body delivery, repeated PPDUs,
malformed/abort recovery and mandatory HT MAC integration remain open.
D2 is not complete and HT station advertisement stays disabled.

## Streaming copy-state and immediate retrigger discriminator (2026-10-20)

The moving boundary above leaves two distinct questions: does the
streaming transition rewrite the programmed continuation-copy context,
and can an immediate replay of the existing copy command restore progress?
Added default-off `CLEAN_RX_HT_COPY_STREAM_REARM`, requiring phase 2
(resume128), with two instruction-count-matched profiles:

- mode 1, `reference-handoff-stream-rearm-control`: capture state and
  execute a jump to the next instruction;
- mode 2, `reference-handoff-stream-rearm`: capture the identical state
  and execute the existing continuation instruction
  `0x376, 0x1883, 0x1004, 0x1004` once after streaming starts.

No guessed register bits, copy length adjustment, reset or new on-air
fixture is introduced. The command uses the existing copy base/end.
The first activation's existing commit marker guards both diagnostics
and command replay; later frames cannot retrigger it even if their
streaming bit clears. Instruction-count matching does not claim identical
hardware latency for a jump and a copy command.

Only these profiles expand the copy probe from 44 to 54 words and
version 3 to version 4. Words 44..48 record control, base, endpoint,
raw next-length and raw context before the streaming write. Words
49..53 record RXE count, control, base, endpoint and context after the
no-op or command. Existing words 36..43 still retain count/control
immediately after streaming and before this action, TSF, epoch and
final commit. The SHM record now ends exactly at 0x600; its report
export ends at trace 5792, immediately before existing controller-fault
evidence. Compile-time checks prevent overlap. Observer ABI 21,
normal F2 samples, terminal ownership and all good/bad/watchdog gates
are unchanged.

The collector emits `handoff-stream-rearm` and requires matching owner,
epoch, version and activation commit, stream-clear-before/active-after
readbacks, bounded count, and the unchanged programmed base/endpoint
before accepting the diagnostic proof. Activation JSON now also
requires a committed marker before labeling an epoch match. An exact
pair and all required controls remain separate strict-pass conditions.

Evidence: `build/clean-rx/stream-rearm/`. Twelve RF trials comprise both
roles/orders with both profiles, then all four long-continuation cells
repeated in reverse role/profile order. The receiver binaries differ
in exactly one 8-byte PSM instruction (seven differing bytes at
zero-based 60476..60482). Source controllers are byte-identical to the
independently vendor-verified stimulus.

All twelve commit activation at count 130 and retain the exact first
body. Sampled copy state is consistent across roles and size orders:

| Readback | Matched no-op | Existing-command retrigger |
| --- | --- | --- |
| Control before streaming | 0x0800 | 0x0800 |
| Control immediately after streaming | 0x0802 | 0x0802 |
| Control after action | 0x0802 | 0x08c2 |
| Count before streaming / after streaming / after action | 130 / 130 / 130 | 130 / 130 / 130 |
| Base before / after action | 0x0800 / 0x0800 | 0x0800 / 0x0800 |
| Endpoint before / after action | actual second length / same | actual second length / same |
| Raw next / context before streaming | 0x8000 / 0xf020 | 0x8000 / 0xf020 |
| Context after action | 0xf020 | 0xf020 |
| Terminal control | 0x0802 | 0x0802 |

The request's 0x00c0 bits are visible after the retrigger and gone at
the terminal snapshot. This demonstrates a real control-register
effect, not a missing software branch; it does **not** prove the
internal copy engine accepted or completed another transfer.

| Profile / second body | Trials | Exact SHM/F2 pairs | Strict passes | Terminal |
| --- | ---: | ---: | ---: | --- |
| no-op / 298 | 4 | 0 | 0 | all watchdog, count 134, first byte mismatch 128 |
| retrigger / 298 | 4 | 0 | 0 | all watchdog, count 134, first byte mismatch 128 |
| no-op / 170 | 2 | 2 | 0 | good, count 176; required controls missing |
| retrigger / 170 | 2 | 2 | 1 | good, count 176; one control-loss trial |

Thus immediate command replay does not extend the canonical 128-byte
prefix or repair the long continuation. It also does not corrupt the
complete short continuation in these trials. The sampled base,
endpoint and context are not accidentally rewritten by streaming.
This narrows the search beyond a simple missing immediate copy-command
replay or visibly wrong programmed base/end, without proving
undocumented pointer/ownership semantics or excluding fast transient
state between samples.

The experiment acts immediately at count 130, before the later stall
at 134. A command ignored while an engine is already active, or a
different recovery after independently confirmed stalled progress,
is not excluded. Do not present this failed immediate retrigger as
testing every possible rearm timing. A future control must distinguish
those cases with a bounded progress/timeout decision and preserved
failure bytes, not by blindly repeating the command.

The independent audit reuses the previous campaign's pure independent
fixture/bitwise-CRC functions, not production validators. It verifies
48 raw reports, 288 source records and final fixtures, all before/after
readbacks, exact first bodies and short continuations, all eight
128-byte copy cutoffs, retained F2 samples and command-only image
differences. All 410 raw captures are conserved through post/consume
with no delivery drops; they are not a distinct-OTA receive denominator.
All eleven strict failures remain retained. No control-loss result is
upgraded because its pair is valid.

Native ASan/UBSan tests cover every RX count and 256 control states,
advance 0/1/2/0xffff, preservation outside the intentional command field,
idempotence and committed/invalid nonzero markers after streaming is
cleared. Host tests mutate fifteen ownership/version/marker/readback
fields and reject unsupported modes. Invalid mode 3 and absent phase
parent fail compilation. Both C3/RP profiles build freestanding; final
validation reproduces the RF controller images. Production default
and the earlier resume128 C3 image remain byte-identical.

Fresh complete originals were independently saved and matched to the
previous restored originals before any write. Both full 16-MiB flashes
are restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Final serial-targeted checks
confirm both badges in BOOTSEL. No USB recovery was required and no
credentials were read or provisioned. D2, larger-body streaming,
loss-accounted recovery and HT association remain open.

## Measured post-stall replay and suffix overwrite (2026-10-20)

Immediate replay did not establish whether the copy request was ignored
while reception was still progressing. Extended the default-off
`CLEAN_RX_HT_COPY_STREAM_REARM` gate with modes 3/4, requiring the same
resume128 phase and existing copy/observer gates:

- `reference-handoff-stall-control` (mode 3): await a measured plateau,
  then execute the matched no-op;
- `reference-handoff-stall-rearm` (mode 4): await the identical plateau,
  then issue the existing continuation-copy command once.

The new path awaits count 134, requires it to remain unchanged for
32 TSF microseconds, and checks count/abort/EOF again immediately before
the action. One shared 1,023-poll fallback bounds both waiting stages,
including a stopped TSF. Count progress, explicit abort, EOF or exhausted
budget skip the command. The wait does not use or alter native GPRs,
the outer receive watchdog or any RXE register; only the selected final
action writes the existing copy command. The native EOF/FCS/abort and
ownership paths resume afterward without outcome conversion.

Both profiles use copy-probe version 5 with the same 54-word storage,
ending at SHM 0x600 / trace 5792 before controller-fault evidence.
Words 36..43 retain the prior independent activation proof. Version-5
words 44..53 instead encode:

| Word | Meaning |
| --- | --- |
| 44 | Count sampled when beginning the stability test, or initial count if skipped earlier |
| 45 | Remaining shared poll budget |
| 46, 47 | TSF low at test start and modular elapsed microseconds |
| 48 | Decision: 1 stable, 2 progress/change, 3 EOF, 4 abort, 5 exhausted |
| 49, 50 | Count and control after the command or skip |
| 51..53 | Base, endpoint and raw context after the action |

`handoff-post-stall` distinguishes valid diagnostics, a verified stable
interval, and actual command issue. A progressing short body is not
mistaken for a stall: its command is skipped. Poll exhaustion cannot
qualify as a success. Normal proof samples and exact-byte/FCS/F2/control
gates remain mandatory; observer ABI 21 is unchanged.

Evidence: `build/clean-rx/post-stall/`. Twelve RF trials cover both
roles/orders and four reversed role/profile repeats of the long body.
Two failed BOOTSEL readiness checks and two USB-only serial-mapped
recovery attempts remain archived but are not RF trials. Source
controllers remain byte-identical to the independently vendor-verified
source. The two receiver controllers differ only in one 8-byte action
instruction (seven bytes at zero-based 60716..60722).

All eight long-continuation trials verify count 134 held for exactly
32 us, with 587..597 polls remaining. All retain the correct copy base
0x0800, endpoint 298 and sampled raw context 0xf020 after the action.

| Profile / second body | Trials | Action | Terminal and bytes | Exact pairs / strict passes |
| --- | ---: | --- | --- | --- |
| delayed no-op / 298 | 4 | no command after verified plateau | watchdog, count 134; first 128 bytes canonical | 0 / 0 |
| delayed replay / 298 | 4 | command after verified plateau | explicit abort; A count 62, B count 68; suffix overwrites SHM start | 0 / 0 |
| delayed no-op / 170 | 2 | skipped on progress | good, count 176, complete valid FCS/F2 | 2 / 2 |
| delayed replay / 170 | 2 | skipped on progress | good, count 176, complete valid FCS/F2 | 2 / 2 |

The command readback again becomes 0x08c2 rather than the no-op's 0x0802,
with count still 134 immediately afterward. Unlike immediate replay,
the subsequent failure now changes: **SHM MPDU bytes 0..31 are exactly
transmitted second-body bytes 128..159**, reproduced twice in each badge
role. A search across every offset of all source bodies for the trial
finds this fresh 32-byte sequence only in the retained continuation at
offset 128. It is not a first-body, stale-trial or arbitrary corrupt
prefix match.

Only that 32-byte shifted span is demonstrated. The next byte diverges
from the expected suffix. Later SHM bytes include unchanged parts of
the old prefix and other noncanonical contents; they are not labeled
newly received data. Neither the terminal count nor a 32-byte suffix
match is complete-MPDU or valid-FCS evidence.

The no-op preserves the original 128-byte prefix instead. One B no-op
repeat retained source aggregate 5 rather than the initial aggregate 2;
its complete first body, failed continuation and ownership are verified
against that actual source index. Its absent initial-case terminal log
is not turned into evidence that aggregate 2 completed.

The A delayed-abort trials capture only two/three raw frames and miss
required later controls; B captures 35 and receives the required
controls. This role-dependent recovery failure remains explicit.
Four short-body successes do not qualify recovery from a stalled
long body: their commands were never issued.

**Causal conclusion:** after a measured stall, replay does access fresh
continuation data beyond the preserved prefix, but places it at the
unchanged SHM start and destroys that prefix. The request is not merely
ineffective in the same way as immediate replay. Source/read progress
and SHM write placement are therefore distinct pieces of state that a
safe continuation must preserve. This does not identify the undocumented
internal pointer/bank or prove which earlier operation advanced it.
It does not justify blindly adding 128 to a destination register,
changing endpoint units, or treating counts 62/68 as usable body lengths.

The new count retreat exposed an old host-proof assumption: terminal
count below the streaming threshold had been treated as an unstreamed
short body. Four original delayed-abort JSON reports consequently say
`stream_phase_readback_verified:false` despite their independently
committed activation and plateau. They remain unchanged. The final
validator permits this bounded version-5 count retreat only with
matching committed activation/epoch and active stream readbacks; it
does not weaken byte, FCS, owner or terminal-good checks. A sanitizer
`recheck.c` tool validates all twelve archived raw reports using the
corrected helpers; `activation-recheck.jsonl` records the result.
All four aborted bodies remain failures, not newly qualified pairs.

The independent bitwise-CRC/fixture audit verifies 48 raw reports,
288 source records and final fixtures, twelve exact first bodies,
all eight timing decisions, four uniquely mapped suffix overwrites,
14 retained F2 samples and complete accounting for 356 raw captures.
The capture count is not a distinct-OTA denominator. It also checks
the original rejected phase flags, corrected activation rechecks,
command-only image delta, final validation-image identity and full
restoration.

Native ASan/UBSan tests cover all RX counts and 256 control states
through the stall entry, then execute the actual bounded wait for
13 normal/fault cases: progressing/decreasing count, EOF/abort,
nonadvancing count, frozen TSF, TSF wrap and last-moment changes or
budget exhaustion before action. All four native GPRs are preserved.
Host tests reject 16 mutated fields, invalid reasons/modes, command
bits on a skipped action, premature/excessive elapsed time and
invalid post-replay count/activation combinations. A redundant nested
test loop introduced in the prior work is moved outside the count
loop without reducing its cases. Both new C3/RP profiles link
freestanding; at that checkpoint, out-of-range mode 5 and missing parents
fail compilation.
The production image and previous immediate-replay C3 image remain
byte-identical.

Fresh complete originals preceded all writes. Both full 16-MiB flashes
are restored and independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Final serial-targeted checks
confirm BOOTSEL. No credentials were read or provisioned.

The next discriminator should separate write-destination placement
from source progress, preserving both old prefix and newly read bytes
under explicit bounded geometry. Destination/end/count units must be
verified before attempting a segmented repair. D2, larger-body
streaming, repeated aggregates and reliable abort recovery remain open;
HT station advertisement remains disabled.

## Bounded copy-destination placement (2026-10-20)

The preceding delayed replay exposes source bytes 128..159 at the wrong
SHM destination. This experiment tests write placement independently:
after the same verified count-134/32-us plateau, both profiles write the
copy-base register and replay the same existing copy command. Only the
added destination offset differs. There is no endpoint change, guessed
reset, repeated command or new production receive path.

Default-off `CLEAN_RX_HT_COPY_STREAM_REARM` now accepts:

- **5:** add zero to SPR `0x001`, then replay.
- **6:** add 64 to SPR `0x001`, then replay.

Both require the existing phase-2 resume128 profile. The shared
1,023-poll budget, progress/EOF/abort checks and final pre-action checks
remain unchanged. Skipped actions neither write the destination nor
issue the command. No native GPR is borrowed. The bounded 300-byte
snapshot plus candidate displacement stays within RX scratch storage.
Adding zero is still a register write, **not** a hardware no-op; the
matched comparison isolates its offset, not possible side effects of
writing the base itself.

Copy-probe **version 6** retains the 54-word version-5 layout and
decisions. Validators require the exact mode/version combination and
post-action base: `0x840` only for mode 6 with a confirmed stable
decision, otherwise `0x800`. Count-retreat activation validation also
recognizes version 6 without relaxing full-body/FCS checks.
`handoff-post-stall` adds `destination_probe` and `base_shift_words`;
the controlled result explicitly reports `diagnostic_only:true`.
Both destination profiles are deliberately excluded from qualification,
including short-body trials where the action is skipped.

Evidence: `build/clean-rx/copy-destination/`. Twelve RF trials cover both
roles/orders and reverse-order long-body repeats. The receiver C3 images
are each 60,884 bytes and differ in **one byte**, zero-based offset
60,718, inside the same eight-byte add instruction at 60,716. Independent
instruction decoding checks SPR `0x001` as source/destination and
immediates zero/64, followed by the identical existing copy command.
Both source-controller binaries exactly match the independently
monitored fixture image.

| Profile / second body | Trials | Retained canonical prefix | New source-128 span | Terminal result | Exact SHM/F2 pairs / with all controls |
|---|---:|---:|---|---|---:|
| base + 0 / 298 | 4 | 0 bytes | bytes 0..31 | 2 aborts, 2 bad-FCS completions | 0 / 0 |
| base + 64 / 298 | 4 | 160 bytes | bytes 128..159 | 3 aborts, 1 bad-FCS completion | 0 / 0 |
| base + 0 / 170 | 2 | complete 170 bytes | action skipped | good, count 176 | 2 / 1 |
| base + 64 / 170 | 2 | complete 170 bytes | action skipped | good, count 176 | 2 / 1 |

All eight long-body trials activate streaming at fresh count 130, then
verify count 134 unchanged for exactly 32 TSF microseconds. Immediate
post-action count is still 134; base readback is exactly `0x800` or
`0x840`, endpoint remains 298, and raw context remains `0xf020`.
Post-action control reads `0x0802` in this campaign, rather than the
earlier command-only campaign's `0x08c2`; command bits are not required
to remain latched. The executed instruction path and newly placed
source bytes supply the action evidence, not an assumption about
hardware bit-clear timing.

In **all four shifted trials**, original bytes 0..127 remain correct,
and exactly 32 newly placed bytes extend the canonical prefix through
159. The first mismatch is offset 160. In all four zero-offset controls,
the same source span replaces bytes 0..31; the first mismatch is offset
zero. Searching every 32-byte window of every transmitted source body
uniquely identifies each new span as the retained continuation's bytes
128..159. Searching the complete 300-byte snapshot finds it only at
the corresponding destination offset, zero or 128.

This establishes that **adding 64 to this copy-base register moves this
write by 128 SHM bytes**, under the tested geometry. It preserves the
old prefix but does not recover the rest of the body. Do not classify
bytes after the first mismatch as fresh data: stale/noncanonical bytes
remain in the snapshots.

Abort terminal counts are 62 on A, 68 or 78 on B. Three repeats instead
reach bad-FCS terminals: both zero-offset repeats at count 304 and the
shifted A repeat at 306. A terminal count near the intended length is
therefore **not** evidence of a correct body. Every long-body fixture
FCS fails independently, and no long pair reaches the normal exact
SHM/F2 gate. The first shifted B trial retains source index 5 rather
than initial index 2; its initial-case terminal fields remain zero.
Later control losses are retained, not converted to successful recovery.

All four short-body trials detect progress, leave base `0x800`, skip
the command and retain exact FCS-valid SHM/F2 pairs. Only the two B
trials receive every required control. These are compatibility controls,
not evidence that a stalled transfer recovered; all twelve runner exits
remain nonqualifying because the profiles are explicitly diagnostic-only.

Native ASan/UBSan tests exercise the exhaustive streaming/once gates and
all 13 bounded stall scenarios for each profile. Tests require the
destination shift only on a confirmed mode-6 action; every skipped
path retains the old base, endpoint/context and four native GPRs.
Host tests reject wrong offsets, shifts on skipped actions and wrong
mode/version combinations, while keeping existing mutation gates.
At that checkpoint, mode 7 and a mode-6 profile without its parents
reject compilation.
Both C3/RP profiles link freestanding. Production default and prior
mode-4 C3 firmware remain byte-identical.
Final validation rebuilds reproduce both RF C3 images exactly; RP
binaries differ only in their four-byte archive-path-dependent build ID.

The independent bitwise-CRC/fixture audit checks 48 raw reports,
288 source records, twelve exact first bodies, eight uniquely mapped
suffix spans, 14 retained F2 samples and conserved accounting for
342 raw captures. Raw captures are not a distinct-OTA denominator.
Both fresh complete 16-MiB originals are restored and independently
reread, byte-compared and SHA256-verified: A `8cda7a00...23628`,
B `bc3f972d...21e2`. Both are confirmed BOOTSEL. No credentials were
read or provisioned.

The destination-placement hypothesis is now directly supported, but
placement alone is insufficient. The next discriminator must explain
why only 32 new canonical bytes survive, including bad-FCS cases whose
terminal count advances to 304/306: verify endpoint/count semantics
versus copy-bank/stream ownership before attempting further segmented
transfers. D2, variable-length aggregate reception and reliable abort
recovery remain open; HT station advertisement remains disabled.

## Replay endpoint versus remaining bytes (2026-10-20)

The bounded destination test preserves 160 canonical bytes but leaves
the original endpoint 298 in place after source offset 128. This
experiment tests a specific remaining-length interpretation: with
destination displacement fixed at the verified +64 units, compare
endpoint 298 with `298 - 128 = 170` immediately before the same replay.
It does not assume that endpoint/count units or copy-bank ownership
are already understood.

Default-off `CLEAN_RX_HT_COPY_STREAM_REARM` modes **7/8** add matched
endpoint writes: subtract zero or 128 from SPR `0x02a`. Both retain the
same measured count-134/32-us stall, +64 destination write and existing
copy command. Both endpoint writes are real register writes, not no-ops.
They run only after all shared progress/EOF/abort/budget checks. A new
bounded geometry guard permits endpoints 129..300; otherwise decision
**6** skips both destination/endpoint writes and replay. Old modes
0..6 have no new guard or instruction.

Copy-probe **version 7** keeps the 54-word layout. Host validation
requires exact mode/version, action-dependent base and endpoint, and
verified stable timing for decisions 1 and 6. A skipped action cannot
change either register; an in-range endpoint cannot claim a geometry
rejection. `handoff-post-stall` adds `endpoint_probe` and
`endpoint_subtract`. All destination/endpoint profiles remain explicitly
diagnostic-only and cannot qualify station HT operation.

Evidence: `build/clean-rx/copy-remaining/`. Twelve RF trials cover both
roles/orders and reverse-order repeats of long-body cells. Receiver
C3 images are 60,924 bytes and differ in exactly one byte at zero-based
offset 60,758: the subtract immediate in the instruction at 60,756.
Independent decoding confirms the same SPR `0x02a` operands and the
identical copy command that follows. Source C3 images remain identical
to the independently monitored fixture image.

| Endpoint / second body | Trials | Canonical prefix | Terminal results | Exact SHM/F2 pairs / with all controls |
|---|---:|---:|---|---:|
| unchanged 298 / 298 | 4 | 160 bytes | bad FCS; counts 304, 304, 304, 306 | 0 / 0 |
| remaining 170 / 298 | 4 | 160 bytes | A: bad FCS/count 178 twice; B: abort/count 68 or 69 | 0 / 0 |
| unchanged / 170 | 2 | complete 170 bytes | progress, action skipped, good/count 176 | 2 / 2 |
| remaining / 170 | 2 | complete 170 bytes | progress, action skipped, good/count 176 | 2 / 2 |

All eight long-body cases activate streaming at count 130 and confirm
count 134 unchanged for exactly 32 TSF microseconds. Post-action
readback is base `0x840`, endpoint 298 or 170, context `0xf020`,
count 134 and control `0x08c2`. In every case the original 128-byte
prefix remains intact and exactly source bytes 128..159 extend it to
160 canonical bytes. Byte 160 still mismatches. Independent searches
across every transmitted source body and all destination windows
uniquely map those 32 bytes to source continuation offset 128 and
snapshot offset 128.

To distinguish missing bytes from a merely displaced later suffix, the
audit searches every 16-byte window of source offsets 160 onward across
the entire retained 300-byte snapshot. None appears in any long-body
trial. This is a bounded SHM observation, not a claim that later bytes
are absent from every other receive/DMA buffer.

Shrinking the endpoint changes termination but **does not extend the
correct bytes or yield a valid body**. The A count changes from
304-class to 178; B instead aborts early. This does not establish a
global endpoint/count unit rule or exclude every alternative length
interpretation. It excludes this specific remaining-length adjustment
as a sufficient repair under the tested geometry. Matching terminal
counts or latched command bits remain insufficient evidence of valid
receive data.

All retained early bodies are source index 2, including both B repeats;
there is no later-aggregate substitution in this campaign. Four short
controls skip the action without changing base `0x800` or endpoint 170,
and deliver complete FCS-valid pairs with every required control.
Eight long-body cases remain failures: six bad-FCS completions and
two explicit aborts. Lost interleaved controls remain recorded.
All runner exits remain nonqualifying by design.

Both profiles pass native ASan/UBSan tests, including the original 13
stall scenarios plus five exact geometry boundary cases: reject 128,
301 and 65535; accept 129 and 300. Rejected geometry preserves both
registers, command state and all four native GPRs. Host mutations cover
mode/version, wrong/subtracted endpoints, changed skipped registers,
in-range geometry rejection and premature decision-6 timing.
Mode 9 and missing mode-8 parents fail compilation. Final C3 images
reproduce both RF images; production default and preceding mode-6 C3
images remain byte-identical.

The final actual C validators, run under ASan/UBSan, recheck all twelve
archived captures without changing original RF JSON. The independent
bitwise-CRC/fixture audit verifies 48 raw reports, 288 source records,
eight uniquely mapped 32-byte spans, 16 retained F2 samples and conserved
accounting for 412 raw captures. These are not 412 distinct OTA MPDUs.
Fresh complete originals preceded all writes. Both full 16-MiB flashes
are restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`; both are confirmed BOOTSEL.
No credentials were read or provisioned.

The unresolved discriminator is now the **32-byte fresh-copy limit and
its producer/consumer ownership**, rather than treating a changed
termination count as repaired data. Inspect bounded copy progress and
bank availability before further segmented commands. D2, repeatable
variable-length delivery and HT station advertisement remain open.


