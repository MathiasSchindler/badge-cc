# HT milestone history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## D. 802.11n

### D1. Confirm the implementation target

- Recorded completed criterion: Map the datasheet-supported HT20, single-stream MCS0-MCS7 capabilities to
  actual PHY/MAC controls, framing, receive status and firmware requirements.
- Recorded completed criterion: Document optional capabilities separately. Do not advertise short GI,
  STBC, beamforming or aggregation modes until their exact support is
  established and implemented. Do not assume HT40 or additional spatial streams.

**D1 complete for the conservative control/framing target, not for a
general HT station or reliability qualification.** See the
[HT20 implementation target](../experiments/ht20/README.md).
The selected baseline is 2.4-GHz HT20 mixed mode, one spatial stream,
MCS0-MCS7, BCC and 800-ns GI. The repository datasheet's internal revision
is 002-30348 Rev. *B; Table 25 provides the mixed-mode MCS/GI evidence.
Pinned public D11 references and hardware readback identified the direct
template interface. The earlier failure was writing serialized L-SIG bits:
`0x0bab` at `+0x510` read back as `0x000b`. These registers require separate
RATE (`+0x510 = 0xb`, legacy 6 Mbps) and protection LENGTH
(`+0x512 = 3*Nsym+9`), not packed SIGNAL bytes. Selector `0x42` and all
eight MCS PHYCTL1 values now produce fresh HT20 captures in both directions.
Control/template readback and nonzero PHY errors are explicit failures.

Opcode 11 is an explicit, default-disabled diagnostic: an opt-in build
permits one fixed synthetic broadcast frame, without association or ACK.
Command/frame bit-corruption and station/default gates have native tests;
the final default gate also rejected TX on hardware with error `0x85`.
An isolated, compile-time CRC-only experiment zeroed HT-SIG CRC bits in
the readback-verified TX template; fresh peer captures still had valid
nonzero CRC. Hardware regenerates that CRC in the tested path; software
retains strict input/RX validation and zero tail bits.

Same-frame raw RX diagnostics captured external HT20 MCS1, 800-ns-GI
frames with valid HT-SIG CRC and MPDU FCS: six-byte HT-SIG
`013900061402`, PSDU 57 bytes, RXE 63 bytes, PHY status0 `0x0102`.
Controlled probes now extend this to all eight MCS values: six HT-SIG
bytes, RXE length 94 for an 88-byte PSDU, terminal PHY status0 `0x0002`.
Raw early PHY bits proved unreliable and are not used as a format
decision. New PSM traces distinguish candidate headers and good/bad
terminal paths; full-prefix matching prevents stale completion updates.
RX smoothing zero remains accepted and regression-tested.

All-rate build `4bc556b3` ran 48 fresh one-frame trials, three per
MCS/direction. Every cell had a fresh valid capture: 39/48 raw controller
captures, 38/48 full F2 deliveries. One exact fresh GoodFCS MPDU was
captured but not delivered over F2. Of the other nine unmatched trials,
four had no candidate, three reached a bad receive path with valid
HT-SIG and length 94, and two retained early-only evidence. These losses
remain unresolved; this is not a reliability pass.
The CRC experiment (`636a7d0a`) had 4/6 fresh raw captures, 3/6 F2
deliveries. Final normal build `c81def60` again captured MCS0 and MCS7
in both directions (5/6 and 2/6 full matches). Individual TX status is
still not OTA proof; the paired fresh-frame reports provide that proof.
Independent implementation interoperability remains a D2 gate.

Native tests now exhaust 196,608 nonlegacy RX metadata combinations, test
that open/WPA2 HT/WMM peer beacons do not enable local HT advertisements,
and extend PSM rejection checks for conflicting PHY flags. Ordinary
station operation stays legacy-only; no HT capabilities are advertised.
The target also records why minimum advertised HT aggregation/receive
sizes cannot be equated with the current 2,400-byte MPDU limit. D2's raw
PHY bring-up must not be mistaken for a complete HT station; QoS/EDCA and
the required HT receive/Block ACK path remain gates.

Validation: both controller builds, native suites, sanitized loader/HT
tests and existing Micro Signal regression builds passed. Default build
`b5ab34c5` passed cyclic-size 2-Mbps long-preamble and OFDM6 controls,
30 delivered/ACKed frames per direction each, zero failures or PHY
mismatches. Both badges' originals were restored and independently
compared: A's 33,604-byte application, B's full 16-MiB flash. Both are
left in BOOTSEL. No unrelated experiment directories were changed.

### D2. HT PHY and association

- Recorded open criterion: Implement HT capability/operation negotiation, HT mixed-mode TX/RX,
  correct lengths/airtime and MCS configuration without breaking legacy peers.
- Recorded open criterion: Implement the QoS/WMM and HT MAC prerequisites required by the selected
  profile; parsing a QoS header alone is insufficient.
- Recorded open criterion: Verify association capabilities and actual OTA HT framing against an
  independent HT20 peer, with explicit legacy fallback.

**D2 in progress; not an enabled HT station.** The following implementation
steps are complete, without closing the broader requirements above:

- Recorded completed criterion: Add a separate, default-disabled, versioned HT20 payload API with
  explicit MCS/profile/length validation and the verified mixed-mode TX
  controls. Version 1 sends one non-QoS broadcast MPDU, 28..2,332 bytes,
  without ACK/retries or a station profile. Enforce the 2,304-byte MSDU
  limit independently of the larger raw storage capacity.
- Recorded completed criterion: Add CRC-checked HT20 RX metadata without changing legacy metadata or
  MPDU offsets; preserve profile/MCS/channel through split F2 delivery.
  Retain completed captures across host/SDIO backpressure in the opt-in
  build rather than release an unposted frame.
- Recorded completed criterion: Parse HT capability/operation and WMM AP EDCA parameters, validate
  basic MCS/channel/parameter consistency, and report explicit legacy
  fallback reasons. Preserve byte-identical legacy association requests
  for open/WPA2 peers, even after optional-element errors or WMM changes.
- Recorded completed criterion: Preserve saved association-response evidence when publishing the HT
  peer report: compact version-2 report at offset 1040, with an integrated
  association-evidence regression.
- Recorded completed criterion: Add a separately gated 3,896-byte MPDU receive layout, on-chip upper-SHM
  capacity/alias checks, relocated DMA buffers, three-fragment F2 transport
  and a checked host/controller layout manifest. Keep the default layout.
- Recorded completed criterion: Implement full 3,839-byte plaintext A-MSDU validation and zero-copy
  deaggregation, rejecting malformed/truncated aggregates before exposing
  any subframe. Verify a fixed two-MSDU aggregate over the air through the
  extended receive path; retain the independent-peer/negotiation gate.
- Recorded completed criterion: Implement native-verified per-TID QoS duplicate/fragment state and
  integrate authenticated QoS reassembly with the existing CCMP receive path.
  Preserve per-TID replay counters, require consecutive fragment PNs, reject
  protected A-MSDUs and unsupported ACK policies, and wipe expired plaintext.
- Recorded completed criterion: Add a separately gated QoS/HT PSM normal-ACK response prototype,
  HT-to-legacy basic-rate tables, safe deferred policy updates and a fixed
  one-frame normal-ACK diagnostic. Native and on-chip response-start tests
  pass; these counters alone do not qualify returned ACKs or SIFS timing.
- Recorded completed criterion: Add an isolated, clocked HT normal-ACK fixture using live PSM/RX,
  automatic PHY turnaround and the existing DCF/ACK-wait path. Save the
  actual returned ACK, FCS and legacy PLCP before internal consumption;
  require independent address/FCS/rate validation for paired-test success.
  This is bounded diagnostic evidence, not general HT station qualification.
- Recorded completed criterion: Replace the failing DCF slot operand/decrement sequence with a staged
  register transition and a separated SHM-load/branch consumer. Add bounded
  counter checks, explicit controller faults and frozen transition evidence.
  Verify all 48 launches in three bounded MCS/direction sweeps; this does
  not close shared-medium access or HT receive-reliability qualification.
- Recorded open criterion: Implement actual EDCA/QoS access and HT protection/response behavior,
  meet receive-size/required HT MAC obligations jointly with D3, then wire
  association negotiation and independent-peer verification.

Opcode 12 and its eight-byte versioned descriptor are documented in the
[D2 implementation/API notes](HT20_FOUNDATIONS.md#d2-implementation-bounded-packet-api-and-peer-profiling).
The `CLEAN_RX_HT_DATA=1` build is separate from D1's fixed probe and CRC
experiment. Normal association remains legacy-only, and
`ht_advertised:false` plus the local-MAC fallback reason remain explicit.
WMM parsing is not EDCA execution; large-frame diagnostics are not HT
station receive-size or aggregation qualification.

**D3 prerequisite work pulled forward with approval:** the minimum-size
receive foundation above is now implemented, but D2 is still **not finished**.
The extended image uses `CLEAN_RX_HT_RX=1` together with `CLEAN_RX_HT_DATA=1`.
It verifies the upper SHM bank with MAC/PSM halted, supports three physical
F2 fragments per logical frame, and checks an eight-word SRAM layout
manifest before starting RX. Native tests cover every transport length
through 3,896 bytes, incomplete/mismatched third fragments, unchanged
credits on failure, and all eight manifest-field mismatches.

A separate version-2 diagnostic sends exactly one unprotected, No-Ack QoS
MPDU of 3,869 bytes: a 26-byte header, a 3,839-byte A-MSDU containing
2,304- and 1,505-byte MSDUs, and FCS. Version 1 retains its non-QoS
2,304-byte MSDU limit. Build `941546f0` completed all 16 MCS/direction
trials, with 12 fresh full deliveries; the four failures remain recorded.
Twelve bounded repeat trials produced seven fresh deliveries, establishing
at least one full aggregate delivery at every MCS in both directions
without erasing any failures. The first reverse-MCS0 failure had no early
candidate; the other three original failures reached an early receive
error without a completed capture. Their causes remain unresolved.
Final build `9a581374`, with explicit halted-bank checking and the manifest
also published by transport-only firmware, passed both-direction MCS0
aggregate smoke tests. Saved USB evidence remains a prefix, not a complete
external large-frame capture.

Default build `18882dbb` rejected HT data before TX (`0x85`) and passed
30 ACKed/delivered frames per direction at both OFDM6 and 2-Mbps long
preamble, cycling 32/1,500/2,304-byte payloads. Default/extended native
suites and Micro Signal regression builds passed. Both original full
16-MiB flashes were restored and independently compared against fresh
pre-test snapshots; both badges are in BOOTSEL.

**QoS receive continuation:** the shared receive core now has 16 independent
TID contexts, each with bounded duplicate history and one 16-fragment,
2,304-byte MSDU assembly. The existing WPA QoS path now uses those contexts
after MIC/replay validation instead of bypassing MAC duplicate/reassembly
state. Incomplete assemblies expire after 512 ms, including idle station
polling and timer wrap; plaintext is wiped on expiry, delivery and terminal
protocol failure. Protected A-MSDUs, Block ACK/No Explicit Ack policy,
four-address frames and HT Control remain unsupported, not silently enabled.
Plaintext A-MSDUs are validated before committing duplicate history.
Protected receive diagnostics sum legacy and all TID counters.

Default and extended native suites passed with sanitizers, including
16 interleaved TIDs each carrying 16 fragments at 32/1,500/2,304-byte MSDU
sizes, sequence/PN carry, duplicate retries, malformed headers, MIC and PN
continuity failures, overflow, expiry and wiping. Native tests also feed
the full 3,839-byte A-MSDU through the new QoS core. These are **not** OTA
QoS/fragment qualification.

Default build `5115ae14` passed three fresh protected station exchanges
against the unchanged vendor AP, each with a matching independent AP
observation, and one wrong-password negative control. It also passed
30 ACKed/delivered cyclic-size frames per direction at OFDM6 and 2-Mbps
long preamble. Extended build `dbb6d9e2` passed both-direction MCS0
3,869-byte A-MSDU smoke tests. Evidence is under `d2-qos-*` in
`build/clean-rx/badge/`. Default/extended badge BSS is 221,256/225,744 bytes;
the per-TID state is statically allocated, with no new dependencies.
Both original full 16-MiB flashes were restored and independently compared
against fresh backups; both badges are in BOOTSEL.

**HT/QoS response continuation:** `CLEAN_RX_HT_RESPONSE=1`, requiring the
extended receive layout, enables opcode 14 for pinned profile-3 radios.
Response changes defer until RX/response ownership is idle. QoS Data/Null
normal-ACK policy and HT20 mixed-mode MCS0-MCS7/BCC/800-ns-GI response
selection now run inside the final-GoodFCS PSM path. No-Ack/Block ACK
policies, four-address/HT Control and unsupported HT PHY profiles do not
trigger normal ACKs. HT response rates use the highest basic rate at or
below the non-HT reference rate, including CCK rather than reusing the
legacy same-modulation selector. The default response policy stays disabled.

The new API-version-3 fixture is exactly one nonce-bound, 60-byte,
unprotected, three-address QoS MPDU with normal ACK policy, TID 0..15 and
correct reserved ACK airtime for the diagnostic's OFDM basic set. It is
not arbitrary QoS injection or negotiated station TX.
Native tests cover all 4,095 nonempty basic sets at every MCS, all receive
lengths, all TIDs/ACK policies, malformed profiles/addresses and API gates.

Opt-in build `7b4c37c8` produced 13 fresh full deliveries and one error-free
PSM response start per delivery in a 16-cell MCS/direction matrix.
Three bounded repeats added two fresh deliveries; the reverse-MCS7 repeat
still failed. All failures are retained under `d2-response-*`. No return
ACK was observed by the transmitter in these 19 trials. PSM-local delays
of 1..2 us are not PHY-end-to-ACK SIFS measurements. **Neither reliable HT
ACK reception nor RF SIFS timing is qualified.** Earlier build `5c4921cb`
had 10/16 matching matrix deliveries and an ambient-RX policy-update
failure; the latter motivated deferred updates, not an RF-loss fix.
Final collector build `5ecb6600` makes a returned ACK mandatory for the
paired ACK test to succeed. A fresh MCS1 trial delivered the full frame
and started one error-free response, but correctly failed the paired test
because no return ACK was received. Local counters cannot pass this gate.

Default build `b83a299b` passed 30 ACKed/delivered cyclic-size frames per
direction at OFDM6 and 2-Mbps long preamble, plus a fresh vendor-AP WPA2
positive exchange and a wrong-password negative control with matching
independent AP observations. Both default/opt-in builds and native suites
pass, with no new firmware dependencies. Both complete original 16-MiB
flashes were restored and independently compared against fresh pre-test
snapshots; both badges are in BOOTSEL.

**Clocked HT ACK continuation:** the raw diagnostic halts PSM/RX and forces
TX, then waits conservatively after MAC byte drain before restoring RX.
That restoration is too late for an ordinary SIFS ACK exchange; byte drain
is not physical PPDU completion. API version 4 instead keeps PSM/RX live
and uses automatic PHY turnaround, PSM-clocked contention and bounded ACK
acceptance. It reuses the fixed 60-byte QoS fixture, permits exactly one
actual TX attempt, and requires pinned profile 3, response policy 3,
OFDM6/12/24 basic mask `0x150` and short slots. Raw version 3 remains a
separate control; no normal association or arbitrary QoS TX is enabled.

The PSM saves the expected, correctly addressed ACK at final GoodFCS before
consuming it internally. Version-2 evidence is 32 bytes, including TSF/PHY,
all 14 ACK bytes and the six-byte legacy PLCP. The host requires exactly one
attempt, controller ACKED, one fresh saved ACK, valid FCS/RA and the expected
6/12/24-Mbps response rate. Native tests cover descriptor/profile/policy
gates, all MCS/TIDs, evidence corruption, PLCP/rate checks and PSM
address/length/deadline gates. Default/opt-in native suites pass.

Intermediate build `4baa273b` completed 13/16 strict ACK exchanges. Build
`4f1bd5e9`, adding full PLCP evidence and exact basic/slot policy gates,
completed **12/16** strict exchanges: three failures made **zero actual TX
attempts** before the 200-ms access deadline, and one transmitted reverse
MCS7 frame timed out after an early RX error at its peer. All 12 successful
ACKs were independently checked with zlib FCS, fresh RA and complete PLCP
rate/length/parity checks, alongside matching peer delivery/response counts.
A raw forward-MCS0 control delivered the data and started a response but
missed the return ACK, supporting the RX-turnaround diagnosis.

Diagnostic build `393935f8` retains that controller and adds frozen
MAC/IFS/backoff snapshots on failure. Four bounded repeats added reverse
MCS2/7 ACK successes; forward MCS3 and reverse MCS0 still failed without TX.
Their snapshots have valid layout magic, no pending policy/NAV and idle
IFS samples, but remaining backoff counts of **18,000/21,300 with CW 15**.
This was an unresolved scheduler/counter fault, not evidence of a busy RF
channel; the continuation below investigates its operand dependencies. Do not hide
it by extending deadlines, masking the counter or counting BUSY as RF loss.
All original failures remain under `d2-clocked-*` in `build/clean-rx/badge/`.

Default build `be5b417d` passed 30 ACKed/delivered cyclic-size frames per
direction at both OFDM6 and 2-Mbps long preamble. WPA2 positive and
wrong-password controls passed against the unchanged vendor AP with
matching independent AP authorization/protected-traffic observations.
Both original full 16-MiB flashes were restored and independently compared
against this batch's pre-test backups; both badges are in BOOTSEL.
Returned ACKs are now demonstrated for the bounded clocked fixture;
reliable HT access, independently measured RF SIFS and interoperability
remain unqualified. HT/WMM advertisement stays disabled.

**DCF operand-visibility continuation:** instrumented the last configured
draw, PSM before/after values and TSF sample, without extending the access
deadline or allowing an extra actual TX attempt. A 20-launch spacing
experiment avoided the fault, but restoring the adjacent SHM decrement/
branch reproduced a zero-attempt failure with 15,574 remaining slots.
Register staging alone was also insufficient: build `5332172b` trapped
three invalid operand values (35,046/23,297/60,025) in 32 trials, before TX.
The actual SHM counter was zero in those frozen snapshots. That rules out
explaining these observations as ordinary contention or just a large
legitimate backoff; the flat native interpreter did not model this on-chip
operand-visibility behavior.

The retained sequence loads slots into a register, samples TSF independently
before the first register comparison, decrements/commits the staged value
and branches on that register rather than the just-written SHM word.
Structural native tests preserve this ordering. The exact silicon
writeback/visibility timing is not independently characterized; this is a
tested repair of the observed sequence, not a general ISA timing claim.
Values above the legal maximum 1,023 disable PSM contention and raise
controller error `0x8e` with the offending value. Error snapshots preserve
an already-faulted controller rather than trying to stop it as if still
running. The three intermediate hardware faults verified this fail-closed
path naturally, with zero actual attempts.

Builds `c933fd3f`/`2b7315ae` completed **48/48 actual one-attempt launches**
across three full MCS/direction sweeps, with zero access errors, corrupt
counter observations or range-guard trips. **37/48** completed strict
returned-ACK exchanges; every success passed independent zlib FCS,
fresh-address and complete PLCP/rate checks against its matching peer.
The 11 failures all transmitted: four had no early HT candidate, two
detected the expected HT-SIG but had no recorded terminal outcome, and five
reached the full-length terminal receive-error path. None delivered fresh
data or started a response. The earlier classification of six as having
no candidate incorrectly conflated a pending outcome with zero candidates.
They remain unresolved receive failures, not successful HT exchanges.
Final build `fb981a82` additionally invalidates stale counter-trace magic
at controller startup and passed both-direction MCS0 ACK smoke tests.

Default build `d575252e` passed 30 ACKed/delivered cyclic-size frames per
direction at OFDM6, 2-Mbps long preamble and OFDM6 with exactly three
deliberately suppressed ACKs per endpoint. All had zero corruption and
controller faults; retries/timeouts remain reported, not erased.
The earlier 16-suppression exhaustion trial did not pass the positive soak:
its initiator reached the existing eight-attempt budget; the peer then
stopped waiting for progress. Actual suppression was 6/8, not 16 each.
Final vendor-AP WPA2 positive and wrong-password controls passed with
matching independent authorization/protected-traffic observations.
Default/opt-in native suites pass, including all 16 CWmin draws, expanded
CW boundaries, 700-ms idle periods across TSF wrap and corrupt-value gates.
Both original complete 16-MiB flashes were restored and independently
compared against fresh pre-test backups; both badges are in BOOTSEL.
Evidence remains under `d2-backoff-*` in `build/clean-rx/badge/`.

**Focused investigation of the 11 receive failures:** added default-disabled,
policy-3 failed-frame evidence before RX rearm. It retains PLCP/MPDU bytes,
RX count, raw PHY/status words and TSF for terminal errors (cause 2) and
RX-event aborts (cause 3). The collector independently checks software FCS,
exact peer addresses and bit differences against the fresh fixture.
Native tests cover exact copies, both return paths, policy gates,
stale/mismatched prefixes and RX-count/declared-length boundaries.
No successful-frame acceptance or ACK criterion was relaxed.

Build `535e0ffe`, one actual MCS7 transmission per trial, both directions:

| Channel | Strict ACK exchanges | Full-length corrupt captures | No early HT candidate |
| --- | --- | --- | --- |
| 1 | 5/8 | 3 | 0 |
| 6 | 7/8 | 0 | 1 |
| 11 | 4/8 | 3 | 1 |

All six corrupt captures reached cause 2 with 66 received bytes and valid
expected HT-SIG. Independent zlib CRC32 and fixture reconstruction confirm
**57/89/194 bit differences on channel 1**, and **159/85/133 on channel 11**,
including header/payload corruption. The channel-1 reverse failure retained
the original FCS bytes but its corrupted data did not match that FCS.
Corrupted RA/TA means these are not exact-address candidate matches; surviving
fresh nonce-bearing payload supports correlation with the diagnostic.
TX template/control readback passed. This demonstrates corrupt received
data rather than just a lost F2 delivery or missed ACK counter; it does not
yet distinguish RF interference, analog calibration/EVM, TX/RX PHY behavior
or an internal receive-path defect.

MCS0 controls completed **2/4 on channel 1**, **4/4 on channel 6** and
**4/4 on channel 11**. Both MCS0 failures had no early HT candidate, so
losses are not exclusively a high-MCS terminal-error phenomenon.
These small, sequential samples do not establish channel 6 as a fix or
prove interference. `HT_PROBE_CHANNEL=1..11` now controls both endpoints
and all paired-script channel evidence, with channel 1 unchanged by default.
One successful channel-6 trial also had a later unrelated candidate replace
its early trace with outcome 0, proving that this latest-candidate snapshot
is not a per-fixture completion ledger. The two historical pending cases
remained unexplained in that batch; no cause-3 capture was obtained then.

The record retains the latest qualifying 60-byte failure, not a ring;
only its RX-count prefix is valid, even though the fixed buffer is copied.
Raw RXE status bits remain uninterpreted without verified register semantics.
Evidence: `d2-rx-investigate-*`, `d2-rx-channel-*` and `d2-rx-lowrate-*`
under `build/clean-rx/badge/`. Default and opt-in builds/native suites pass.
Default `484712e6` passed 12 ACKed/delivered cyclic-size frames per direction
at OFDM6, 2-Mbps long preamble and OFDM6 with three suppressed ACKs per
endpoint, with zero corruption/controller errors; retries remain reported.
Both original full 16-MiB flashes were restored and independently compared
against this investigation's fresh full backups; both badges are in BOOTSEL.
HT/WMM advertisements remain disabled; D2 reliability is not qualified.

**Further RX investigation:** expanded the gated failure record to two
independent copies plus before/after receive state, and moved terminal
capture before PHY-format classification. **17 new full-length failed
captures** independently reproduce identical bytes in both copies,
unchanged receive state, invalid zlib FCS and **5..236 fixture bit errors**.
Two take the RX-event abort path with an early candidate but outcome 0,
reproducing the pending-trace failure class. Both retain exact RA/TA;
one differs only by five BSSID bits with the original FCS intact.
This is evidence for an abort exit, not proof that the two old pending
cases had the same cause. The source of the stable corruption remains
unresolved; no speculative radio gain/calibration changes were made.

Found and fixed a separate **live-MAC idle-listening bug**: the original
one-shot watchdog faults PSM with reason 6 after 64 * 65,535 quiet polls.
Old-image receive-only controls reproduced this twice on badge A.
The final expiry-only hook restarts that idle wait for hardware DCF only,
preserving ordinary poll/slot timing, raw one-shot timeout and all other
watchdogs/deadlines. A full-loop native test covers the actual old budget
and subsequent RX; earlier hook-only idle tests missed the outer countdown.
Final `72496ec8` passed two no-peer channel-11 listening controls per badge,
without controller faults. This does not explain the original 11 failures,
which had controller error zero.

Final paired evidence is **12 completed cells: 10 launches, 3 strict ACKs,
5 stable corrupt captures, 2 no-candidate misses and 2 zero-attempt guard
faults**. No RX idle fault remained. Guard faults still fail closed with
`0x8e`; the earlier 48/48 launches did not establish a permanent repair.
All seven saved corrupt operands equal the final station-address word
(nonce bytes 3/4). New halted snapshots retain guard reason 1 and the
offending operand even though the stored slot count is zero. Changing the
Cortex's last publication read to the preceding address word did not move
this correlation; the temporary probe was removed. The mechanism remains
uncharacterized, not a generic SHM timing rule.

Evidence under `d2-rx-deeper-*`, `d2-rx-idle-*`, `d2-rx-idlefixed-*`,
`d2-rx-final-*` and `d2-rx-port-*` includes unsuccessful/intermediate
builds and one incomplete USB collection; none are silently counted as
successful exchanges. See the [detailed investigation](../experiments/ht20/CLOCKED_ACK_AND_RX_FAILURES.md#further-investigation-stable-corruption-rx-aborts-and-idle-watchdog)
for denominators and layout changes. Default `c42d0041` passed 12
ACKed/delivered cyclic-size frames per direction at OFDM6, 2-Mbps-long
and OFDM6 with three deliberately suppressed ACKs per endpoint, with
zero corruption/controller faults and all retries/timeouts reported.
Default/opt-in builds and native suites pass. Both original complete
16-MiB flashes were restored and independently matched to fresh backups;
both badges are in BOOTSEL. D2, receive reliability and HT/WMM gates remain open.

Still required before D2 closure: actual AP-driven EDCA/QoS contention,
longer access/operand-visibility qualification, HT ACK reliability/SIFS
qualification and protection behavior, negotiated/OTA
QoS and protected aggregate
integration, Immediate Block ACK agreements/windows/reordering and required
A-MPDU reception, then negotiated HT association and an independent HT20
peer. The new A-MSDU parser is wired into the bounded receive diagnostic,
not enabled as a negotiated station delivery path. Permanently declining
ADDBA is not a substitute for implementing mandatory HT Block ACK behavior.

Hardware progress: build `61902238` completed 47/48 planned paired trials
at MCS0-MCS7 and 60/512/2,332-byte MPDUs, with 32 fresh full host
deliveries. One incomplete collection is excluded, not treated as RF loss;
15 completed receive trials did not match. These failures are unresolved.
The final `c6915690` build also invalidates prepared transport state at
explicit raw capture-discard/rearm boundaries; its six full-size smoke
trials passed at MCS0/MCS1/MCS7 in both directions, each with an exact
2,304-byte payload and FCS. This is not an all-rate reliability pass or
proof that the earlier losses are fixed. Large payloads are checked in full
on the badge; USB evidence saves only a prefix, whose bytes and expected
FCS were independently reconstructed with zlib.

Native HT/peer-profile and loader/transport tests, PSM/MAC, WPA2/secret
tests and existing Micro Signal regression builds passed. D1's fixed probe
still passed in both directions. Default build `88cbc56d` rejected the
new opcode before TX (`0x85`) and passed 2-Mbps long-preamble CCK/OFDM6
controls, 30 ACKed/delivered frames per direction each, zero corruption,
duplicates or PHY mismatches. Both badges were restored and independently
compared over their complete 16-MiB flashes against fresh pre-test backups;
both remain in BOOTSEL. See the linked implementation notes for build IDs,
exclusions and remaining D2 gates.

### D3. HT MAC and performance

- Recorded open criterion: Implement the aggregation, Block ACK negotiation/window management and
  receive reordering required by the supported HT profile. Define and test
  aggregate/MPDU size limits, loss recovery and replay/duplicate interactions.
- Recorded open criterion: Verify supported MCS rates in both directions at multiple payload sizes,
  with at least 1,000 frames per rate/size cell and zero corruption or
  reordering/duplicate-delivery errors outside the documented interface.
- Recorded open criterion: Measure sustained payload throughput, loss and CPU/transport limits.
  Distinguish PHY rate, bus rate and application throughput. Include mixed
  legacy/HT traffic and a sustained station soak.


