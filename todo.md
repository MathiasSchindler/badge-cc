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
Raw HT20, extended reception, per-TID receive/CCMP state, gated responses
and bounded per-category access exist. Association-response/runtime peer
profiling and bounded station RX staging are implemented. Opt-in non-HT
WMM negotiation, AP-derived EDCA policy, protected QoS exchanges and bounded
protected A-MSDU receive software are implemented. Bounded Block ACK agreement,
A-MPDU stream parsing and ciphertext reordering are native-tested with WPA2
delivery. Partial multi-MPDU radio capture is demonstrated in the finite
diagnostic; complete live aggregate reception, Immediate BA and station/controller integration
remain open. Receive completion/
discard handling, radio override ownership and atomic backoff publication
have been corrected and regression-tested. Local-AP reply delivery and
intermittent HT receive corruption remain qualification limits.
Owned station TX cleanup is implemented and regression-tested.
Negotiated station operation and its mandatory MAC prerequisites are not
yet qualified.
See [implementation and remaining gates](clean-rx/dev-doc/HT20_IMPLEMENTATION.md#remaining-d2-work).
Follow-up receive investigations are recorded in
[the development record](clean-rx/dev-doc/AP_AUTHENTICATION_INVESTIGATION.md).
The [remaining RF-hint evaluation](clean-rx/dev-doc/RF_HINT_FOLLOWUP.md)
tracks isolated PHY/gain controls and TX-cleanup verification.
The [controlled independent HT receive investigation](clean-rx/dev-doc/CONTROLLED_HT_RECEIVE.md)
verifies small-frame vendor-to-clean MCS0/MCS7 reception on both badges;
receive reliability and mandatory HT MAC integration remain open.
See the [aggregate radio investigation](clean-rx/dev-doc/AMPDU_RADIO_INVESTIGATION.md)
for current capture evidence and remaining hardware gates.
The [next-MPDU interface investigation](clean-rx/dev-doc/AMPDU_INTERFACE_INVESTIGATION.md)
records the latest diagnostic progress and unresolved terminal-frame boundary.
The [Block ACK implementation record](clean-rx/dev-doc/BLOCK_ACK_IMPLEMENTATION.md)
separates tested software from the remaining live receive/response work.

- [ ] Complete HT capability/operation negotiation, mixed-mode TX/RX, airtime
  and MCS configuration while preserving explicit legacy fallback.
- [ ] Integrate actual WMM/EDCA, protection, QoS and required receive-size,
  Block ACK/A-MPDU/reordering obligations jointly with D3.
- [ ] Verify negotiated association and actual HT OTA framing against an
  independent HT20 peer, including positive, negative and recovery cases.

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
