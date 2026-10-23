# Source-built Wi-Fi firmware milestones

Scope: dependency-free CYW43439-family controller firmware and its RP2350
host in `clean-rx/`. This file records milestones, acceptance criteria and
current status, not a claim of enabled support.

## Mandatory directory boundary

- Keep firmware, milestone-specific host support, tests and documentation
  under `clean-rx/`. This root roadmap is the status-only exception.
- Leave unrelated firmware and experiment directories unchanged.
- Existing shared infrastructure and independent vendor-firmware peers may
  be used without modification. Obtain approval before expanding that scope.

## Definition of done

A milestone is complete only when it is integrated, regression-tested and
verified on hardware with independent peer/capture evidence. Native tests,
mailbox counters and local TX completion alone are not OTA qualification.
Retain failures and exclusions, protect credentials, and back up/restore
borrowed badge flash with verified readbacks.

Implementation details, build IDs, measurements, findings and experiments
belong in [development documentation](clean-rx/dev-doc/README.md).
Earlier detailed records are preserved in
[milestone history](clean-rx/dev-doc/MILESTONE_HISTORY.md).

## A. Reliable ordinary Wi-Fi MAC operation

**Status: partially implemented; RF qualification remains open.**
See [MAC implementation and evidence](clean-rx/badge/README.md#milestone-a-mac-implementation-experimental-not-complete).

### A1. Correct medium access

Bounded backoff/timeout implementation and native NAV/EIFS/overlap controls
are recorded in [MAC reliability recovery](clean-rx/dev-doc/MAC_RELIABILITY_RECOVERY.md).
Controlled RF contention and timing qualification remain open.

- [ ] Complete carrier/NAV sensing, interframe spacing, random backoff,
  freezing/resumption and contention-window updates.
- [ ] Test idle/busy/overlapping traffic, NAV expiry and retry exhaustion.
- [ ] Independently verify RF channel access under controlled contention.

### A2. Reliable ACKs and retransmissions

Fired-request ownership and explicit, bounded recovery of quiescent
TX-completion timeouts are implemented and hardware-tested; see
[recovery evidence and unresolved causes](clean-rx/dev-doc/MAC_RELIABILITY_RECOVERY.md).
Failed attempts remain TIMEOUTs, not successful completions.
Completed automatic-response status is never credited as Data success.
Follow-up localization fixes timer reference, response-retirement ownership
and missed PHY-start observations; completion waits are elapsed-time bounded.
Live failure snapshots are explicitly labelled, and the fixture keeps RX
active for the peer's bounded final-reply retries.
The A2 follow-up adds invariant-checked register-staged ACK suppression,
sticky first-fault operands, saved ACK/FCS/PHY/TSF validation and both-role
closing service. An observed CCK bug is fixed: completion resampling no longer
replaces the physical-tail ACK clock with PHY start while keeping the shorter
window. A real sum-mismatch fault with otherwise consistent retained counters
also prompted register-only comparisons and explicit computed-sum/limit
evidence. This does not prove the earlier spontaneous suppression cause fixed.
See the [A2 follow-up and retained failures](clean-rx/dev-doc/MAC_RELIABILITY_RECOVERY.md#a2-follow-up-suppression-invariants-and-cck-ack-clock-ownership).
The CCK follow-up preserves prepare/armed/fired/error ownership evidence and
retires known `0x0010`/`0x0020`/`0x0030` idle incomplete transmissions through
checked abort/rearm, still as failed attempts/TIMEOUTs. Stable sticky error
baselines and both preambles are covered; unknown bits and unsafe ownership
remain fatal. Build `a4eefd5a` recovers 12 real errors, passes all 16 tested
legacy PHY/role controls, 5,000 cyclic-size MSDUs per direction, 1,000
two-/sixteen-fragment MSDUs per direction and both exhaustion negatives.
Its 34-case campaign has 29 positive pairs passing and **3 failed positive
pairs**, now all no-first-RX/initial ACK exhaustion without controller/PHY
faults, not the former fatal CCK completion error. Later replays expose a
stale first-error sample followed by additional known bits; final source
`460b7228` fixes that sampling case while retaining unknown-error evidence.
They also expose an unqualified `0x0028` baseline / `0x0038` error (bit
`0x0008`), which remains fatal, and ACK exhaustion after initial deliveries.
Final-source focused controls pass 1,000 cyclic 2-Mbps and 100 short-preamble
MSDUs/direction; its reverse exhaustion control records five suppressions for
eight attempts and is not a controlled-negative pass.
See [CCK provenance, retirement and retained failures](clean-rx/dev-doc/MAC_RELIABILITY_RECOVERY.md#cck-phy-error-retirement-and-retained-provenance).
A2 remains open for the remaining PHY/RX/ACK failures and independent RF
timing/full-credit qualification; the silicon meaning/cause of the observed
PHY status is not established.
The follow-up observation campaign reproduces an eight-attempt exhaustion with
only six eligible receiver suppressions and healthy suppression bookkeeping.
No matching rejected Data prefix is captured; ambient bad-FCS totals cannot
be attributed to the missing test frames. New first-`0x0008` and rejected-Data
prefix evidence narrows future captures without widening the recovery mask.
Targeted build `77bae4fe` passes four 2,000-MSDU/direction 2-Mbps stress pairs
and four exact exhaustion negatives, with 21 known-status retirements.
The earlier inference of 62-byte test-frame truncation is withdrawn:
short rejected frames can expose a previous header in SHM. A native regression
reproduces that diagnostic misattribution; capture now requires active
streaming and its prefix threshold. An opt-in poisoned-scratch/66-byte probe
investigates actual copy provenance. Physical RX loss and unreproduced
`0x0008` remain open, as do the initial failures.
The follow-up probe captures raw next-length `0x8000` with copy-register zero
and an unwritten poisoned tail. A proposed minimum-length guard passed native
tests but failed all five RF controls with zero receptions and no eligible
suppression; it was withdrawn. The original zero-permitted branch is retained
and tested over all 65,536 operands. These register values alone do not prove
an invalid RF frame or a faulty copy.
Final first-any probe build `571d4b98` passes five bounded controls: two exact
eight-suppression negatives, 2,000 and 100 cyclic MSDUs/direction in opposite
roles, and 100 32-byte MSDUs/direction. Its independent oracle verifies 4,400
received MSDUs and two known-status retirements. This does not erase the
initial startup failure, reproduce bit `0x0008` or complete A2/A3 qualification.
Default build `66bf3b9c` also passes both exact exhaustion negatives and both-role
100-MSDU/direction cyclic controls (400 received MSDUs). Four positive-control
TIMEOUTs remain unexplained after subtracting intentional peer suppressions
and the one known-status retirement; successful retries are not proof that the
underlying missing receptions/ACKs are fixed.
See [PHY/RX provenance and retained failed controls](clean-rx/dev-doc/MAC_RELIABILITY_RECOVERY.md#phy-bit-0x08-and-missing-rx-provenance-follow-up).

- [ ] Complete response-rate/preamble selection, turnaround, bounded retries
  and meaningful completion reporting.
- [x] Preserve sequence/fragment identity and protected bytes/PN on retries.
  Native CCMP tests verify unchanged protected bytes/PN and successful
  authentication after Retry; legacy RF fragmentation tests check retained
  sequence/fragment/body identity. This is not protected RF timing qualification.
- [ ] Independently verify ACK/SIFS timing, missing/late ACKs and full-credit
  behavior without silently discarding received frames.

### A3. Receive correctness and interoperability

Bounded legacy fragmentation/retry implementation and paired-badge evidence
are recorded in [fragmentation qualification](clean-rx/dev-doc/LEGACY_FRAGMENTATION_QUALIFICATION.md).
This is partial A2/A3 progress, not the sustained/interoperability gate below.
Follow-up [recovery trials](clean-rx/dev-doc/MAC_RELIABILITY_RECOVERY.md)
passed 1,000 maximum-size MSDUs per direction for both two- and
sixteen-fragment profiles, including explicit completion-timeout recovery.
Earlier sixteen-fragment and 5,000-per-direction mixed-size controls passed;
the 100,000-per-direction gate failed. Follow-up trials localized mailbox
stalls, mid-run ACKed-but-undelivered replies and premature final-reply
shutdown. A later exact long replay reached 64,003 deliveries per endpoint
before ACK exhaustion, with unexpected ACK-suppression state on its peer;
it is not a passing 100,000-frame gate. First-no-reception behavior also
remains unresolved. A1/A2/A3 are not complete.
Build `a4eefd5a` passes 5,000 mixed-size exchanges per direction, 1,000
maximum-size MSDUs per direction with both two and sixteen fragments, and
lost-final-ACK closing controls in both roles. Its broader campaign still has
three initial no-reception failures; this is not a sustained/interoperability
pass or a new 100,000-frame qualification.

- [ ] Complete duplicate, fragmentation, RTS/CTS and protection handling.
- [ ] Verify malformed frames, bad FCS, saturation and DMA/credit boundaries.
- [ ] Verify at least 100,000 bidirectional application frames per direction
  across small, Ethernet-sized and maximum payloads, with zero corruption,
  duplicate delivery or unexplained transport loss; compare a vendor control.

## B. Sustained station operation

**Status: B1's opt-in legacy/open lifecycle and bounded qualification pass.
B2 networking is implemented, but its 60-minute qualification failed.
WPA2 persistence remains open.**

### B1. Connection lifecycle

- [x] Implement persistent discovery, authentication, association and recovery
  for the explicitly legacy/open B1 diagnostic profile.
- [x] Handle beacon loss, departure, AP restart, channel changes and cancellation.
  Beacon loss/restart and connected-session cancellation have RF coverage;
  departure, channel-IE handling and all phase-cancellation gates also have
  native regression coverage.
- [x] Verify ten consecutive cold connections and five AP-loss/recovery cycles
  for the legacy/open profile, with independent association/application evidence.

Mode 32 adds bounded phases, channel scanning, cold-controller recovery,
state/secret cleanup and fresh application-echo evidence. Native sanitizer
tests cover recovery, cancellation, wrap-safe deadlines, malformed channel
IEs and stale/corrupt echoes. A separate `clean-rx/peer` AP fixture uses
unchanged vendor radio firmware and independently verifies association and
application data; see [usage and qualification boundaries](clean-rx/badge/README.md#b1-persistent-open-station-lifecycle).
The `b1-20261022/calibrated-*` campaign passed ten fresh cold connections
(115 verified echoes total) and five real AP power-loss/reload cycles
(six data-verified connection epochs, 68 echoes, five beacon-loss recoveries).
Neither run hid authentication/association failures. Failed precursor runs
remain retained: the AP fixture needed generation-bound snapshot republication
and the badge's crystal-reference timer setup, plus a USB-SOF timing guard.
The independent archive oracle's reserved-footer assertion was corrected to
check the actual stored station MAC; all eleven captured pairs were rechecked.
Cold controls still recorded seven MAC timeouts, six application echo timeouts
and thirteen busy returns. Those counters are not erased: this is lifecycle
qualification, not A1/A2/A3 loss-free MAC or B2 continuous-data qualification.
A final fresh cold pair passed the corrected runner end-to-end (13 echoes);
terminating a second, independently confirmed connected session stopped both
collectors and returned both badges to BOOTSEL without reporting a false pass.

### B2. Continuous application data

- [x] Provide bounded Ethernet/MSDU queues with backpressure and host servicing
  in the opt-in legacy/open B2 profile.
- [x] Exercise ARP/IP/UDP with documented static addressing.
- [ ] Pass a 60-minute bidirectional soak including idle and host stalls,
  without corruption, stuck state, unrecovered disconnects or unbounded queues.

Mode 33 implements four-slot Ethernet TX/RX rings and independent bidirectional
UDP requests, alternating 90-byte and full-sized 1514-byte Ethernet frames.
Static addresses are AP `192.0.2.1`, station `192.0.2.2`, UDP `30526`.
Native sanitizer tests cover a virtual hour, backpressure/resumption, timer
wrap, checksums, corrupt binding, stale/reconnect epochs and retry exhaustion.
The first full-sized 30-second RF control passed: 108 station-originated and
100 AP-originated messages, including 54/50 full-sized requests; all were
independently delivered and acknowledged, with zero loss, corruption,
RX overflow or abandoned work. It exercised scheduled idle and 3/5 actual
host stalls; 6/8 application retries remain visible. This is not a 60-minute
qualification. See [usage](clean-rx/badge/README.md#b2-continuous-static-address-networking)
and [evidence](clean-rx/dev-doc/MILESTONE_HISTORY.md#b2-continuous-application-data).

The full-sized `b2-20261023/hardware/soak02-*` real 3600-second run was
**rejected**, not qualified: station 13,214 generated / 13,211 acknowledged
(3 lost), AP 13,259 / 13,235 (21 lost, 3 abandoned work items).
Both ran all 180 scheduled idle intervals and 287/500 host stalls.
Corruption and RX overflow were zero; TX/RX queue high-water marks stayed
at 2/1. The station had five beacon-loss recoveries, six associations but
only five data-verified epochs, with no authentication/association failures
or fatal radio error. Preserve this failed evidence. Next investigate the
beacon/reception outages and application continuity across controller reloads;
do not relax the oracle or erase losses. Both original images were restored
and full-readback-verified; the Pico was not modified.

### B3. Reproducible station qualification

- [x] Verify the controlled AP and two additional AP configurations: the
  vendor-firmware Pico 2 W on channels 1/6 (legacy/open) and 11 (mixed-HT/open).
  This covers three configurations of one AP implementation, not three stacks.
- [ ] Verify open and software-WPA2 operation, including separate mesh nodes;
  do not equate association with roaming support.

The reproducible B3 matrix passed three fresh connections per configuration:
9/9 independently data-verified sessions and 162 application echoes.
Channel, PHY mode and disabled security are read back from the AP; the oracle
checks raw evidence, target identities, fresh sessions and counts. Six MAC
timeouts, four echo timeouts and seven BUSY returns remain visible; this does
not close A or the failed B2 soak. Software-WPA2 persistence (after E),
separate mesh nodes, additional AP implementations and roaming remain unqualified.
See [B3 usage and boundaries](clean-rx/badge/README.md#b3-reproducible-ap-configuration-qualification)
and [hardware evidence](clean-rx/dev-doc/MILESTONE_HISTORY.md#b3-reproducible-station-qualification).

## C. Complete 802.11b operation

### C1. All legacy DSSS/CCK rates

**Status: rate/preamble support implemented; full-sized 1-Mbps RX remains open.**
See [implementation, matrices and investigation](clean-rx/dev-doc/MILESTONE_HISTORY.md#c1-all-legacy-dssscck-rates).

- [ ] Complete all-rate TX/RX, including full-sized 1-Mbps reception.
- [x] Implement PLCP, airtime and permitted long/short preamble handling.
- [x] Verify actual OTA rate/preamble behavior independently.

### C2. Basic-rate and mixed-network compatibility

- [ ] Complete b-only rate negotiation, response selection and b/g protection.
- [ ] Verify b-only and mixed b/g AP traffic, including long-preamble peers.

### C3. Rate/size qualification matrix

- [ ] Verify all rates, both directions and small/Ethernet/2,304-byte MSDUs,
  with at least 1,000 frames per cell and independent payload/FCS checks.
- [ ] Require zero corruption, duplicate delivery or controller/transport
  faults; record loss/retries and repeat OFDM regressions.

## D. 802.11n

### D1. Confirm the implementation target

**Status: complete for the conservative control/framing target.**
This is not HT-station or receive-reliability qualification, nor identification
of the badge's exact silicon version/revision.
See [target mapping and evidence](clean-rx/dev-doc/HT20_IMPLEMENTATION.md).

- [x] Map HT20, one-stream MCS0..7, BCC and 800-ns GI to controls, framing,
  receive status and firmware requirements.
- [x] Keep optional capabilities separate and unadvertised until implemented
  and verified; do not assume HT40 or additional spatial streams.

### D2. HT PHY and association

**Status: in progress; HT station advertisement remains disabled.**
Live aggregate reception, mandatory MAC integration and negotiated operation
are not yet qualified. Implementation details and qualification limits are in
the [D2 development record](clean-rx/dev-doc/HT20_IMPLEMENTATION.md#d2-qualification-checkpoint-2026-10-19).

#### Completed foundations

- [x] Prepare an unadvertised HT20, one-stream, BCC, 800-ns-GI capability profile.
- [x] Parse AP HT/WMM capabilities and constrain the profile on association and beacon updates.
- [x] Implement common-MCS selection, exact rate requests, PHY configuration and airtime deadlines.
- [x] Implement bounded mixed-format HT TX/RX at MCS0..7 and explicit legacy fallback.
- [x] Build and hardware-test an autonomous two-badge HT20 button messenger with application acknowledgements, bounded retries and visible recovery; retain startup failures without claiming D2 reliability.
- [x] Measure messenger timing on both badges, correct its timer timebase, and reduce ACK/display/retry latency with matched hardware comparisons; preserve failed campaigns and the display-regression fix.
- [x] Implement non-HT WMM/EDCA, protected QoS, per-TID CCMP and bounded A-MSDU receive software.
- [x] Implement native-tested Block ACK agreements, A-MPDU parsing and ciphertext reordering.
- [x] Implement readback-verified atomic station/controller policy commits.
- [x] Deliver fixed three-MPDU aggregates with explicit terminal ownership and bad-FCS continuation.
- [x] Transmit and independently receive compressed Block ACKs in the fixed diagnostic.
- [x] Implement passive vendor/AP reference captures, fresh-frame correlation and receive-exit tracing.
- [x] Implement bounded early-MPDU retention with independent FCS validation.
- [x] Implement a default-disabled, one-shot next-body advance diagnostic.
- [x] Retain two complete, FCS-valid MPDUs from one live vendor aggregate in both size orders.
- [x] Capture and enforce reconciled controller/host receive and delivery diagnostics.
- [x] Trace receive/copy/FIFO state across live aggregate handoffs.
- [x] Implement size-selectable early-only captures and stopped clock/PSM diagnostics.
- [x] Repeat complete FCS-valid 170- and 298-byte early captures in both badge roles.
- [x] Repeat complete FCS-valid 554-byte nonterminal early MPDUs in both badge roles.
- [x] Implement fixed two-body RF stimulus with exact-body checks and interleaved legacy/HT loss accounting.
- [x] Implement exact-size handoff stimulus, bad-frame/malformed cases and fresh second-prefix discrimination.
- [x] Instrument bounded second-body overwrite and preserve stopped controller-fault evidence.
- [x] Implement guard-body/copy-end/stream-boundary controls and retain abort continuation snapshots.
- [x] Independently decode complete controlled aggregate bodies/FCS with vendor RX in both roles/orders.
- [x] Implement a causal bounded streaming/handoff repair; repeat exact pairs and bad-first/last recovery in both roles/orders.
- [x] Trace completed/stopped DMA bytes: confirm first-body streaming and narrow continuation loss to the upstream handoff.
- [x] Separate continuation streaming: reproduce threshold-dependent copy truncation in both roles, with fresh-count/epoch proof.
- [x] Verify copy base/end and command readbacks; exclude immediate copy retrigger as a streaming-stall repair in both roles.
- [x] Prove post-stall replay overwrites the SHM prefix with offset-128 source bytes in both roles; retain abort/recovery losses.
- [x] Verify bounded destination displacement in both roles: preserve 160 canonical bytes, but retain abort/bad-FCS failures.
- [x] Exclude remaining-length replay endpoint as a repair: termination changes, but streaming still limits fresh copying to 32 bytes.
- [x] Use a focused copy microscope to retain complete FCS-valid 298-byte continuations; preserve native count/FCS/F2 rejection.
- [x] Separate quieting from copy replay with matched original-destination controls; isolate replay-dependent count restart and preserve native rejection.
- [x] Isolate the mid-body replay FCS-state effect with bad-body controls; retain raw copy counts and quarantine mismatched partial FIFO exports.
- [x] Isolate next-MPDU command semantics: repeat exact streaming 170/298-byte FIFO pairs with native length/GoodFCS; retain reliability and malformed-input gaps.
- [x] Test bounded ordered-flush and instrumented stuck-prefix diagnostics; preserve event-10 failures and distinguish injected execution from unproven physical recovery.
- [x] Extend the c3 boundary microscope to variable/odd MPDU lengths through 554-byte continuations; test both roles/orders and isolate the short-first native-stream-state dependency without normalizing counts or hiding losses.
- [x] Verify remaining vendor hints: reject general SHM-only capture with matched RF aborts; implement gated short-first streaming and delimiter-status exclusion, plus native-count quarantine for every variable handoff; retain reliability failures.
- [x] Implement gated passive-observer FIFO retention and bounded stop/drain.
- [x] Implement acknowledged repeated early-MPDU FIFO/F2 delivery and complete packet archives; preserve final RF misses as unqualified.
- [x] Implement full fixed-source vendor raw archives; identify monitor cutoff/timeouts and exclude them as a qualified RF-loss baseline.
- [x] Reproduce zero-HT reception with actual PSM-state and PHY-control diagnostics.
- [x] Implement SOF-calibrated receive timelines, decision-latched rearm diagnostics and conditional-event regression tests.
- [x] Implement gated one-shot RXE/PHY recovery controls, preserved first-fault diagnostics and framebuffer-free badge status.
- [x] Correct receive/TX ownership, discard handling and backoff publication.

#### Remaining work packages

- [ ] **1. Qualify early-capture reliability:** qualify FIFO-backed delivery
  and resolve persistent RXE busy, pre-prefix and truncated-body losses;
  repeat loss-accounted tests with confirmed multi-body stimulus.
- [ ] **2. Qualify aggregate handoff:** generalize the bounded next-MPDU
  streaming repair beyond the bounded short-first fix;
  validate full-body ownership/FIFO publication beyond one-shot variable fixtures;
  complete loss-accounted both-role/order qualification and
  malformed/truncated-frame and abort recovery.
- [ ] **3. Receive repeated variable-length aggregates:** remove fixed-slot
  assumptions and qualify bounded buffering, transport backpressure,
  malformed/truncated input and recovery across successive PPDUs.
- [ ] **4. Integrate secure Block ACK lifecycle:** wire agreements/windows,
  admission, reordering, duplicate/replay checks and key changes into actual
  station RX; handle teardown, timeout and reassociation without stale state.
- [ ] **5. Complete Immediate BA and ACK responses:** drive PSM responses from
  admitted frames, validate returned bytes/bitmaps across supported MCS and
  bad-frame cases, and independently measure EOF-to-air SIFS.
- [ ] **6. Qualify mandatory receive and protection:** integrate required frame
  sizes, A-MSDU/QoS, WMM/EDCA and HT/legacy protection; verify protected traffic,
  AP policy changes and recovery jointly with D3.
- [ ] **7. Activate HT association:** after the mandatory MAC, receive-size and
  protection gates pass, advertise the supported HT profile and atomically
  apply negotiated station/controller policy with explicit legacy fallback.
- [ ] **8. Qualify negotiated interoperability:** verify association, actual HT
  OTA framing, interleaved legacy/HT traffic, rate/mode changes and recovery
  against an independent HT20 peer, including negative and fallback cases.

### D3. HT MAC and performance

- [ ] Complete aggregation, Block ACK agreements/windows, receive reordering,
  loss recovery and replay/duplicate interactions with bounded sizes.
- [ ] Verify every supported MCS in both directions at multiple payload sizes,
  at least 1,000 frames per rate/size cell, without corruption, duplicate
  delivery or reordering errors outside the documented interface.
- [ ] Measure sustained throughput, loss and CPU/transport limits, including
  mixed legacy/HT traffic and a sustained station soak.

## E. Complete WPA2 operation

**Status: E1 bounded handshake qualification passes; group traffic, rekey and
sustained protected operation remain open.**
Scope is WPA2-Personal/CCMP, not Enterprise/EAP or obsolete ciphers.

### E1. Robust four-way handshake

- [x] Complete bounded timers/retries and duplicate/replay/MIC/nonce/RSN handling.
- [x] Support passphrase and raw 256-bit PSK provisioning without stale secrets.
- [x] Verify wrong credentials and malformed/duplicate handshake traffic without
  key reinstallation.
- [x] Verify ten consecutive protected connections and the B3 AP configurations.

The E1 implementation has an absolute eight-second handshake deadline, four
500-ms local retries per M2/M4 phase, authenticated advanced-counter M3
retransmissions without reinstalling keys/resetting PNs, and failure wiping.
Passphrases and raw 64-hex-digit PSKs share private, one-use provisioning.
Native crypto/protocol/file-parser/oracle tests pass. The protected Pico AP
matrix requires ten channel-1 connections, then three each on channels 6/11
and three wrong-password controls. Campaign `matrix03` passed all 16 positive
connections, with 16 independently verified protected echoes, and all three
wrong-password controls. It alternated eight passphrases/eight raw PSKs and
checked raw target identities, configuration readbacks, build consistency and
fresh sessions. Both originals were fully restored/readback-verified.
This qualifies bounded E1 connections against one vendor AP implementation,
not mesh nodes, roaming, group/rekey traffic or the B2/E3 hour-long soak.
See [E1 commands and boundaries](clean-rx/badge/README.md#e1-protected-pico-ap-matrix).

### E2. Group traffic and rekey

- [ ] Complete GTK broadcast/multicast, key selection, replay and replacement.
- [ ] Complete bounded group/pairwise rekey without retransmission PN resets.
- [ ] Verify real group traffic and three group plus three pairwise rekeys
  during traffic without plaintext leakage or unrecovered failure.

### E3. Sustained protected operation

- [ ] Complete key teardown and explicit rejection of unsupported security modes.
- [ ] Verify MIC/replay rejection and PN exhaustion for supported traffic classes.
- [ ] Pass B2's protected 60-minute soak with rekeys and independent traffic evidence.

## F. Hardware crypto offload

**Status: raw hardware hooks exist; offload qualification remains open.**

### F1. Establish hardware semantics

- [ ] Establish key slots, header/cipher setup, PN/MIC/FCS ownership and RX errors.
- [ ] Preserve authenticated delivery and replay checks with a software reference.

### F2. Functional equivalence

- [ ] Integrate key lifecycle and supported traffic/rekey operations.
- [ ] Compare independent known-answer ciphertext, tags and plaintext across
  header, size, PN and key boundaries.
- [ ] Verify rejection, retries, replacement and reset before plaintext delivery.

### F3. OTA and resource qualification

- [ ] Repeat encrypted unicast/group, E rekey and B2 soak qualification.
- [ ] Measure CPU, throughput, latency, stack/RAM and image size against software.
- [ ] Enable offload by default only after equivalent correctness and recovery.

## Sequencing

Establish A and B's legacy foundations before enabling negotiated HT. Complete
software-backed WPA2 lifecycle before qualifying hardware offload. C may
progress alongside legacy work; D2 and D3 share mandatory HT MAC prerequisites.
Record evidence and investigation details in `clean-rx/dev-doc/` before marking
a milestone complete here.
