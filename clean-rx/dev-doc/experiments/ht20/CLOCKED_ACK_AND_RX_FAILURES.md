# Clocked ACK and initial receive failures

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

## Clocked HT normal-ACK fixture and returned-ACK evidence

The raw version-3 diagnostic is not an ACK-clocked transmitter. It halts
PSM/RX, forces transmit overrides and, after MAC byte drain, waits another
nominal PPDU before restoring RX. MAC drain is not physical PPDU completion.
This safely bounded raw path cannot provide the RX turnaround needed for
the expected SIFS response. A forward raw MCS0 control delivered the frame
and started a peer response, but the raw sender did not receive its ACK.

Opcode 12 API version **4** uses the same fixed, nonce-bound 60-byte QoS
MPDU as version 3, but enters the existing reliable profile-3 path:

- Requires the response/extended-RX build, pinned local/peer addresses,
  short slots, exact OFDM6/12/24 basic mask `0x150`, and desired response
  policy 3. Other profiles/policies and modified fixtures fail explicitly.
- The exact one-shot command is `12 | clean_tx_length_bits(66) | (1 << 19)`.
  No arbitrary frame, RF retry, association or HT advertisement is enabled.
- Keeps PSM/RX running and uses automatic PHY turnaround and PSM-clocked
  DCF/ACK acceptance. HT airtime is not passed through a CCK rate parser.
  The conservative ACK allowance is `10 + slot + 64 + 304` microseconds,
  added to HT data airtime; this is not an RF SIFS measurement.
- The badge retries only zero-attempt BUSY results under a 200-ms host
  access deadline. It regenerates the descriptor after draining RX, since
  the packet buffer is shared. At most one actual template trigger occurs.
  A transmitted timeout is terminal, not retried as a fresh success.

The PSM's expected-ACK final-GoodFCS path copies evidence before consuming
the ACK internally. Wrong address/length, an inactive/expired ACK wait or
disabled QoS response policy cannot populate this evidence. The counter
commits after copying. Stop command 3 snapshots this record:

| Location | Version-2 evidence |
| --- | --- |
| SHM `0x6f0..0x6fd` | Count, TSF low/high, PHY, ACK14 and legacy PLCP6 |
| SRAM `0x203e0..0x203ff` | Magic `0x32524b41`, followed by those 14 words |
| USB detail `720..751` | Complete 32-byte SRAM record |

Compile-time size/overlap checks preserve the adjacent layout manifest.
Default policy zero skips evidence recording. The host independently
requires controller ACKED/one attempt, exactly one saved ACK, valid complete
FCS and fresh RA, legacy OFDM format, and correct PLCP length, parity,
reserved fields and response rate: 6 Mbps for MCS0, 12 for MCS1/2, and
24 for MCS3..7. Local response starts cannot satisfy this gate.

Use the response-build commands above and matching images on both badges:

```sh
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX MCS --ack
# Retain the raw transmitter as a separately failing/control experiment:
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL OTHER_PREFIX MCS --ack-raw
```

Standalone roles are `--ht-ack-clocked-tx` (badge mode 28) and `--ht-ack-rx`
(mode 27), followed by the same challenge/MCS arguments. Raw TX remains
`--ht-ack-tx` (mode 26). Reports include `ack_result`, `tx_attempts`,
`ack_capture_count`, `ack_rx_time_us`, `ack_phy`, `ack_mpdu` and `ack_plcp`.
Clocked diagnostics also save MAC radio/trace state at detail offsets
800/864 and report access deferrals, CW/remaining slots, NAV, pending
policy, IFS and PSM polls. Failure snapshots stop the controller first,
preserving the original badge error instead of reporting a successful stop.

Evidence under `build/clean-rx/badge/d2-clocked-*`:

- Intermediate build `4baa273b`: 13/16 strict exchanges, using the earlier
  28-byte ACK record without PLCP. Every success had a matching fresh peer
  delivery/response and independently zlib-verified ACK FCS/RA.
- Full-evidence build `4f1bd5e9`: **12/16** strict exchanges. Forward MCS3
  and reverse MCS0/2 failed with BUSY, zero actual attempts and a host access
  deadline. Reverse MCS7 transmitted once and timed out; its receiver had
  an early RX error and no fresh delivery. These are three access failures
  and one transmitted-frame failure, not four RF losses.
- All 12 successful full-evidence ACKs independently passed zlib FCS,
  fresh RA and complete PLCP/rate checks, with matching full peer delivery,
  one response and no response error.
- Access-diagnostic build `393935f8`, same controller: four bounded repeats
  added reverse MCS2/7 successes. Forward MCS3 and reverse MCS0 still made
  no TX attempt. Valid frozen snapshots showed CW 15 but **18,000/21,300
  remaining slots**, zero pending policy/NAV and idle IFS `0x2f`. The
  counter/scheduler cause was unresolved in this build; see the continuation
  below. This is not justified as ambient
  contention; neither extending deadlines nor masking the counter is a fix.
- Default build `be5b417d`: exactly 30 ACKed/delivered frames per direction
  at both OFDM6 and 2-Mbps-long, cycling 32/1,500/2,304-byte payloads.
  Unchanged-vendor WPA2 positive and wrong-password controls passed, with
  independent AP authorization/protected-traffic observations.
- Default/opt-in native suites pass, including all-MCS descriptor/policy
  gates, ACK/PLCP corruption, PSM exact copying and address/deadline gates.
  Both original full 16-MiB flashes were restored, read back independently
  and compared against this batch's full pre-test backups; both are in BOOTSEL.

This demonstrates actual returned ACKs for the bounded clocked fixture,
not reliable ordinary HT access, independently measured PHY-end-to-ACK
SIFS, independent-peer interoperability or completed D2. Earlier failures
remain recorded. HT/WMM association advertisement remains disabled.

## DCF slot operand visibility and fail-closed counter handling

The zero-attempt failures were not justified as channel contention. Native
DCF tests had treated SHM/register writes as immediately visible. On chip,
the old slot decrement/SHM-branch sequence exposed operands far above the
configured CW. This is not necessarily persistent corruption of the stored
slot count: later halted evidence has an invalid latched operand but a zero
SHM counter. Transition instrumentation now records:

| Location | Counter evidence |
| --- | --- |
| SHM `0x6bb..0x6be` | Latest Cortex draw, PSM before/after values and TSF low sample |
| SHM `0x6bf` | Latched invalid-counter reason; not cleared by a new draw |
| SRAM `0x203b0..0x203bf` | Magic `0x32464344`, then draw/before/after/TSF, guard reason and actual slot count |
| USB detail `976..991` | Complete 16-byte SRAM record, separate from HT RX/ACK evidence |

Static checks preserve the adjacent HT response table, HT diagnostic,
layout manifest and unicast status. Magic is invalidated at controller
startup and published only with a hardware-profile snapshot. JSON fields
are `dcf_trace_valid`, `dcf_draw`, `dcf_before`, `dcf_after`, `dcf_clock`,
`dcf_guard_latched` and `dcf_final_slots`. The final snapshot is refreshed
after stopping PSM, including on a controller fault.
The draw is the latest configured window, including post-TX backoff; these
are diagnostic snapshots, not a complete timing trace or RF timestamps.

Controlled evidence under `build/clean-rx/badge/d2-backoff-*`:

- `d491fb59`: inserting transition samples launched all 20 bounded trials
  without the invalid counter. This alone was not accepted as a fix.
- `6ffcae89`: restoring the immediate SHM decrement/branch while retaining
  pre-transition samples reproduced a zero-attempt failure in 8 trials:
  draw 10, remaining/before 15,574.
- `5332172b`: register staging without load/first-consumer separation
  still trapped three faults in 32 trials. Frozen register operands were
  35,046/23,297/60,025 while the Cortex's SHM slot samples were zero.
  These three cases stopped before TX with error `0x8e`; this is a failed
  intermediate repair and a real hardware check of fault preservation.

The retained sequence is:

1. Load SHM slots into `r3`.
2. Save an independent TSF sample before the first `r3` comparison.
3. Reject values above 1,023, the shared software/PSM maximum CW.
4. Check/decrement the staged register, commit it to SHM and save the
   before/after values.
5. Branch on the staged register, not the just-written SHM operand.

The sequence removes both observed dependency patterns without redrawing,
masking an invalid value, extending the 200-ms access budget or retrying a
transmitted frame. Its native instruction/ordering assertions are
intentional: the flat interpreter cannot certify silicon operand timing.
The exact writeback/visibility mechanism has not been independently
characterized, so do not infer a generic D11 latency or SHM prohibition.

An invalid counter disables PSM contention, defers the request and latches
an error. Cortex publishes error `0x8e`, address `CLEAN_DCF_SLOTS` and the
offending value through the existing mailbox. The badge preserves an
already-faulted controller's evidence instead of attempting command 3 and
losing the snapshot. No invalid-counter path triggers TX.

With the separated load, `c933fd3f` completed **16/16 actual launches and
13/16 strict ACK exchanges**. Two fresh full sweeps in `2b7315ae`
completed **32/32 actual launches and 24/32 strict ACK exchanges**. Across
all 48 cells there were no access failures, controller errors, out-of-range
observations or range-guard trips; each saved transition ended at 1 -> 0.
All 37 successful ACKs independently passed complete zlib FCS, fresh RA and
PLCP/rate checks, with matching full peer delivery and one error-free
response. Reinspection of the 11 transmitted failures distinguishes four
with no early HT candidate, two with expected HT-SIG but no recorded terminal
outcome, and five full-length terminal receive errors; none delivered fresh
data or started a response. The earlier six-no-candidate classification
incorrectly treated outcome 0 as a zero candidate count.
This is not a receive-reliability pass. All failures remain recorded.

Final `fb981a82` adds startup invalidation of stale trace magic and passed
both-direction MCS0 ACK smoke tests. Default `d575252e` passed 30
ACKed/delivered cyclic-size frames per direction at OFDM6, 2-Mbps-long and
OFDM6 with three suppressed ACKs per endpoint, with zero corruption or
controller faults. These controls still had reported retries/timeouts.
The earlier 16-suppression trial correctly failed the positive soak:
the initiator exhausted its existing eight attempts and the peer stopped
waiting for progress; actual suppressed ACKs were 6/8, not 16 each.
Final unchanged-vendor WPA2 positive/negative controls independently
confirmed authorization/protected traffic or its absence, respectively.

Default/opt-in native suites pass. New cases include every CWmin draw,
expanded CW boundaries through 1,023, a 700-ms idle/no-request interval
across TSF wraps, exact transition ordering and injected corrupt values,
including all observed fault values. No dependencies were added.
Both original full 16-MiB flashes were restored and independently compared
against this batch's full pre-test snapshots; both badges are in BOOTSEL.
Longer access qualification, remaining RX losses, SIFS and the broader
EDCA/Block ACK/negotiation gates remain open.

## Focused receive-failure investigation

The default-disabled response build now captures failed fixed 60-byte
fixtures before flushing/rearming RX:

- SHM `0x700..0x74d`: count, cause, TSF low/high, RX count, raw status1/2,
  PHY, two 33-word copies of PLCP6 + MPDU60, then final count/status1/2/PHY.
  Cause 2 is the existing terminal receive-error path; cause 3 is the
  existing RX-event abort path. The second copy stages each word in a
  register and inserts a TSF read before storing it, unlike the first
  direct SHM-to-SHM copy.
- A halted snapshot publishes the 160-byte `clean_ht_failure_t` (magic
  `0x32464848`) at SRAM
  `0x20400`, copied to badge detail offset 1640. Magic is invalidated at
  controller initialization. Static assertions check region separation.
- Capture requires response policy 3, matching current/early PLCP words,
  declared HT length exactly 60 and RX count at most 66. It deliberately
  does not filter RA/TA, allowing header corruption to be diagnosed.
  It retains the latest qualifying failure, not a ring or a guaranteed
  fixture identity. Although the whole fixed buffer is copied, only the
  RX-count prefix contains valid received bytes.
- Host JSON exposes the valid prefix, metadata, exact-address match and
  software FCS result. A complete capture additionally reports Hamming
  distance from the expected fresh 60-byte fixture, including FCS.
  A distance is not proof of peer identity when the header is corrupt.
  `failure_repeated_raw`, `failure_raw_repeat_match`, `failure_time_us`
  and `failure_final_*` expose repeatability and state changes during copying.
  Successful-frame, response and returned-ACK acceptance are unchanged.
- Native tests verify both causes and their original return paths, all
  policy values, RX counts 0..67, declared-length boundaries including
  nonzero high bytes, stale/mismatched prefixes and exact raw copies.
  They do not certify PHY or RF behavior.

Build `535e0ffe` retained these bounded results, four MCS7 one-frame trials
per direction per channel, followed by two MCS0 trials per direction:

| Channel | MCS7 strict ACKs | MCS7 full-length corrupt captures | MCS7 no early candidate | MCS0 strict ACKs |
| --- | --- | --- | --- | --- |
| 1 | 5/8 | 3 | 0 | 2/4 |
| 6 | 7/8 | 0 | 1 | 4/4 |
| 11 | 4/8 | 3 | 1 | 4/4 |

All 36 trials made exactly one actual transmission without controller
errors. All six terminal captures have valid expected HT-SIG
`073c0007fc03`, cause 2 and RX count 66. Independent fixture reconstruction
and zlib CRC32 confirm 57/89/194 differing MPDU bits on channel 1 and
159/85/133 on channel 11. In one reverse channel-1 case, the original FCS
bytes survived unchanged but the corrupted data failed that FCS.
The RA/TA corruption means exact peer-address matching is false; surviving
fresh nonce-bearing payload supports fixture correlation.
Raw status2 was `0x0840` in five cases and `0x0940` in one; these raw RXE
register bits are not assigned DMA-descriptor meanings without verification.
No cause-3 failure record was reproduced in that initial batch.

The transmitter checks every template word and its PHY controls by readback.
The new evidence therefore rules out a purely host/F2-delivery or ACK-counter
explanation for the reproduced corrupt frames. It does not yet isolate
interference, analog calibration/EVM, TX/RX PHY behavior or an internal
receive-path defect. Low-MCS failures also occur: both MCS0 channel-1 misses
had no early candidate. Small sequential channel samples cannot establish
interference as the cause or channel selection as a reliability fix.
`HT_PROBE_CHANNEL` now selects channel 1..11 consistently for both collectors,
configuration and paired output; the default remains channel 1.

One successful channel-6 reception was followed by an unrelated early
candidate that overwrote the trace with outcome 0 and a different prefix.
Thus the early trace is a latest-candidate snapshot, not a per-fixture
completion ledger. The two historical expected-prefix/pending cases
remain unresolved; they cannot be relabeled as zero-candidate misses or
proven RX-event aborts.

Evidence is under `build/clean-rx/badge/d2-rx-investigate-*`,
`d2-rx-channel-*` and `d2-rx-lowrate-*`. Default/opt-in builds and native
suites pass. Default `484712e6` passed 12 ACKed/delivered cyclic-size
frames per direction at OFDM6, 2-Mbps-long and OFDM6 with three deliberately
suppressed ACKs per endpoint, with zero corruption or controller errors;
reported retries/timeouts remain visible. Both original complete 16-MiB
flashes were independently restored/compared against fresh full backups
and both badges left in BOOTSEL. HT/WMM advertisements and remaining
reliability/negotiation gates stay disabled/open, respectively.

### Further investigation: stable corruption, RX aborts and idle watchdog

Build `a7267511` moves failure capture before the terminal PHY-format
classification. Previously, a terminal PHY value below 2 could bypass the
failed-byte capture despite a saved matching HT-SIG. This was a diagnostic
coverage gap, not permission to accept such a frame. The two-copy record
also distinguishes stable receive-buffer contents from a transient copying
discrepancy. Native tests cover terminal PHY values 0..3 and the exact
independent-copy instruction sequences.

Across `d2-rx-deeper-*`, `d2-rx-final-*` and `d2-rx-port-*`, **17 complete
failed captures have identical first and second copies**, unchanged
count/status/PHY during copying, independently invalid zlib FCS, and
independently reconstructed fixture differences of **5..236 bits**.
Fifteen are terminal errors; two are RX-event aborts with one early candidate
and outcome 0. Both aborts preserve exact RA/TA and the expected fresh
diagnostic identity. In `d2-rx-final-11-6-0-forward`, only MPDU bytes 17/18
differ (five bits, in BSSID); the remainder including the original FCS
matches, but that FCS does not validate the corrupted bytes.
The raw abort status pairs are `0x4440/0x0540` and `0x4440/0x2840`;
no unverified bit meanings are assigned.
This reproduces a concrete pending-trace exit class. It does not prove
that either historical pending case took this exact exit.

Quiet channel-11 listening additionally exposed an independent real bug:
the original one-shot RX idle watchdog at PCs 118..129 expires after
64 * 65,535 idle polls, sets reason 6 and faults PSM. A live hardware MAC
must be allowed to listen without packets. A no-peer old-image control
faulted twice on badge A; badge B's ambient traffic prevented expiry.
An intermediate refresh-on-every-poll repair passed four receive-only
controls, but was replaced with a narrower **expiry-only** hook: PC 128
restarts the idle wait only if hardware DCF is enabled. Ordinary polling,
slot timing and scratch-register preservation remain unchanged; raw
one-shot RX retains its reason-6 timeout. Flush/control watchdogs and the
existing host/access deadlines remain unchanged.

The full-loop native test executes the original 4,194,240-poll budget,
confirms the one-shot fault, then proves that live DCF survives beyond
it and still takes an incoming RX event. Earlier 700-ms native tests
exercised the hook without the outer countdown and therefore missed this.
Final `72496ec8` passed two no-peer receive-only channel-11 runs on each
badge without controller faults. These are idle controls, not successful
frame receptions. This bug does not retroactively explain the original
11 failures, whose saved controller error was zero.

The same final build completed 12 paired MCS6/7 cells: **10 actual launches,
3 strict ACK exchanges, 5 stable corrupt captures (including one abort),
2 no-early-candidate misses and 2 zero-attempt access faults**. None had
the RX idle/controller fault. The zero-attempt cells raised `0x8e` and
remain excluded from receive-loss counts; the prior 48/48 launch result
was bounded evidence, not proof of a permanent counter repair.

A useful unresolved clue: all seven saved range-fault operands, including
three earlier backoff cases, equal nonce bytes 3/4 interpreted little-endian,
the final station-address word. Final halted snapshots confirm a real
PSM guard latch of 1 with that offending operand, while the actual slot
SHM word is zero. A temporary controlled change making the Cortex's last
publication read address word `0x613` rather than `0x614` did **not** move
the fault to nonce bytes 1/2: the probe fault still latched bytes 3/4.
That simple last-read hypothesis was not supported, and the probe read
was removed. The exact source/visibility mechanism remains unresolved;
no invalid value is masked or silently accepted.

The pre-fix `a7267511` tranche contains 22 complete paired collections
(8 strict ACKs, 8 receiver controller errors and 9 stable terminal captures)
plus one incomplete USB collection, which is not an RF-loss result.
The intermediate `a30846d7` tranche has three completed cells (one strict
ACK and one zero-attempt guard fault). The temporary `44011913` probe
has eight completed cells (seven launches, three strict ACKs, one guard
fault and three stable corrupt captures). Do not combine these into a
positive reliability qualification.

Default `c42d0041` passed 12 ACKed/delivered cyclic-size frames per direction
at OFDM6, 2-Mbps-long and OFDM6 with three suppressed ACKs per endpoint,
with zero corruption/controller errors and retries/timeouts retained.
Default/opt-in builds and sanitizer-backed native suites pass. No power,
gain, IQ-compensation or analog-filter settings were changed: the available
Linux LCN reference uses different multiplier packing/offsets and does not
verify LCN20 attenuation semantics.
Both original complete 16-MiB flashes were restored and independently
compared to this tranche's fresh full backups; both badges are in BOOTSEL.
HT/WMM advertisement remains disabled. Stable corruption is now demonstrated
inside the receive buffer, but its RF/PHY/internal root cause remains open.

## Build and bounded paired tests

```sh
make -s -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ht-data \
  EXTRA_CFLAGS=-DCLEAN_RX_HT_DATA=1 all
make -s -C clean-rx/badge BUILD=../../build/clean-rx/badge/ht-data \
  CONTROLLER_BIN=../../build/clean-rx-ht-data/cyw43_rx_fw.bin all test
```

After saving both originals, install the matching
`build/clean-rx/badge/ht-data/clean_rx.uf2` on both badges and leave them in
BOOTSEL. A fifth argument selects the versioned data API, without changing
the four-argument D1 probe:

```sh
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX MCS MPDU_BYTES
# Example: MCS0, a 2,304-byte payload plus header/FCS:
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 2332
```

The collector also accepts `--ht-data-tx`/`--ht-data-rx`, followed by a
28-hex challenge, MCS and MPDU bytes. Challenge bytes 11..12 encode length
and byte 13 encodes MCS; other bytes remain fresh. TX success is still
only local submission. Pair success requires the receiver's full matching
host delivery. Reports distinguish that full on-badge payload/FCS check
from the saved raw evidence: the controller trace only holds 96 MPDU bytes
and the USB detail only saves 510 MPDU bytes. `fresh_ht_trace` therefore
cannot prove a complete larger frame. The host independently checks the
saved prefix and expected FCS report, but larger frames are **not saved
in full** for an external verifier. No paired report claims independent
HT interoperability or reliability.

## D2 hardware results and regression controls

Evidence remains under `build/clean-rx/badge/`, not in source control:

- Initial `1f0bb7f5` data build: the first 88-byte trial passed, and
  `d2-size-*` passed 6/8 large-frame trials at MCS0/MCS7. MCS0 delivered
  1,528- and 2,332-byte MPDUs in both directions; MCS7 delivered both sizes
  in reverse. The first `d2-matrix-*` sweep completed 31 paired trials,
  with 26 fresh deliveries; 17 aborted launches/collections are excluded
  from RF statistics, not counted as packet losses.
- `61902238`, `d2-final-matrix-*`: 48 planned cells, MCS0-MCS7, MPDUs
  60/512/2,332, one trial per size/rate/direction. There were **47 complete
  pairs and 32 fresh full host deliveries**. One incomplete TX collection
  is excluded. All complete pairs had zero reported controller/transport
  errors and a submitted TX, but the 15 unmatched receive trials remain
  failures: 12 had no recorded early candidate, three retained early-only
  evidence and none had a completed HT capture. The saved
  `d2-final-matrix-summary.json` records every cell, including exclusions.
- Final `c6915690`, `d2-end-m{0,1,7}-d{0,1}-*`: after fixing prepared-capture
  invalidation at explicit raw discard/rearm boundaries, **all six**
  2,332-byte MPDUs delivered a fresh, exact 2,304-byte payload and valid
  FCS. Each of MCS0, MCS1 and MCS7 passed in both directions. This small
  smoke test is not a sustained loss-rate measurement or a controlled
  before/after explanation of the earlier losses.

For every fresh match in the 47-pair sweep and final six-frame smoke test,
an independent host reconstruction checked the saved 510-byte-or-shorter
MPDU prefix, metadata bytes and the expected FCS using Python/zlib, against
the badge's full-frame comparison/FCS result. This does not replace a saved
complete large MPDU or an independent HT receiver.

Final D1 probe build `561421ca` passed the original four-argument script
in both directions at MCS0, with exact raw and F2 freshness.
Final default build `88cbc56d` rejected opcode 12 on hardware with error
`0x85` and no submission. The same default build passed `d2-end-legacy-*`:
2-Mbps long-preamble CCK and OFDM6, cyclic 32/1,500/2,304-byte payloads,
30 ACKed/delivered frames per direction for each PHY, zero corruption,
duplicate delivery or PHY mismatch.

Validation also passed native HT API/peer-profile tests with ASan/UBSan,
both loader variants, separately sanitized loader/transport tests, PSM/MAC,
WPA2 and secret handling, Micro Signal tests and all existing Micro Signal
build variants. Default, data, fixed-probe and CRC-experiment controller
builds remain freestanding; invalid data-build gates and ungated CRC
experiments fail compilation. CLI bounds and shell syntax checks passed.
Final controller text sizes are 34,084 bytes default, 34,956 data,
35,040 fixed probe and 34,984 CRC experiment, with 4,736-byte BSS each.

Before testing, both complete 16-MiB flashes were saved and verified.
A's saved application prefix matched its original 33,604-byte backup;
B's entire saved flash matched its earlier original full-flash backup.
All trial UF2 extents stayed within the backed-up first 512 KiB. After the
controls, both prefixes were restored and **independent full 16-MiB
readbacks matched the respective pre-test full-flash backups exactly**.
Both badges remain in BOOTSEL. No Mac network settings or unrelated
experiment directories were changed.


