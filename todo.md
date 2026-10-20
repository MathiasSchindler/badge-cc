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

- [ ] Complete carrier/NAV sensing, interframe spacing, random backoff,
  freezing/resumption and contention-window updates.
- [ ] Test idle/busy/overlapping traffic, NAV expiry and retry exhaustion.
- [ ] Independently verify RF channel access under controlled contention.

### A2. Reliable ACKs and retransmissions

- [ ] Complete response-rate/preamble selection, turnaround, bounded retries
  and meaningful completion reporting.
- [ ] Preserve sequence/fragment identity and protected bytes/PN on retries.
- [ ] Independently verify ACK/SIFS timing, missing/late ACKs and full-credit
  behavior without silently discarding received frames.

### A3. Receive correctness and interoperability

- [ ] Complete duplicate, fragmentation, RTS/CTS and protection handling.
- [ ] Verify malformed frames, bad FCS, saturation and DMA/credit boundaries.
- [ ] Verify at least 100,000 bidirectional application frames per direction
  across small, Ethernet-sized and maximum payloads, with zero corruption,
  duplicate delivery or unexplained transport loss; compare a vendor control.

## B. Sustained station operation

**Status: bounded station exchanges exist; persistent operation is open.**

### B1. Connection lifecycle

- [ ] Implement persistent discovery, authentication, association and recovery.
- [ ] Handle beacon loss, departure, AP restart, channel changes and cancellation.
- [ ] Verify ten consecutive cold connections and five AP-loss/recovery cycles.

### B2. Continuous application data

- [ ] Provide bounded Ethernet/MSDU queues with backpressure and host servicing.
- [ ] Exercise ARP/IP/UDP with DHCP or documented static addressing.
- [ ] Pass a 60-minute bidirectional soak including idle and host stalls,
  without corruption, stuck state, unrecovered disconnects or unbounded queues.

### B3. Reproducible station qualification

- [ ] Verify the controlled AP and two additional AP implementations/configurations.
- [ ] Verify open and software-WPA2 operation, including separate mesh nodes;
  do not equate association with roaming support.

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

**Status: software-CCMP protected exchanges exist; full lifecycle is open.**
Scope is WPA2-Personal/CCMP, not Enterprise/EAP or obsolete ciphers.

### E1. Robust four-way handshake

- [ ] Complete bounded timers/retries and duplicate/replay/MIC/nonce/RSN handling.
- [ ] Support passphrase and raw 256-bit PSK provisioning without stale secrets.
- [ ] Verify wrong credentials and malformed/duplicate handshake traffic without
  key reinstallation.
- [ ] Verify ten consecutive protected connections and the B3 AP configurations.

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
