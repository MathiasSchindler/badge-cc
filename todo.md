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
delivery. Complete three-MPDU radio capture and bad-FCS continuation are
demonstrated in the finite diagnostic at all MCS in both directions;
general live aggregate reception, Immediate BA and station/controller integration
remain open. A separate gated per-MPDU DMA/F2 experiment now delivers the
first two complete diagnostic MPDUs to the RP2350 at MCS0/3/7 in both
directions. The final byte-evidence campaign has 6/7 normal two-MPDU
deliveries, one retained RF miss and two successful bad-middle rejections;
the last DMA MPDU remains missing. An alternative retained-SHM/SRAM/F2
path now delivers all three fixture MPDUs: its initial MCS0..7 matrix passes
16/16 fresh trials in both directions, exactly three deliveries and zero duplicates. It is
also tested with bad first/middle/last FCS and all-bad aggregates; failed
initial receptions are retained, with successful fresh repeats. It is
still a fixed-size, one-PPDU diagnostic. A subsequent explicit PSM state-16
terminal lease removes dependence on later ordinary traffic, including
bad-last and all-bad aggregates. Its current matrix covers all MCS/direction
cells, with RF misses/partials retained and successful fresh repeats;
it does not establish loss-free reception. This is not production live
reception, BA or mandatory receive/protection qualification. Receive completion/
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
records the fixed next/last-frame diagnostic handoff and remaining live/BA gates.
The [Block ACK implementation record](clean-rx/dev-doc/BLOCK_ACK_IMPLEMENTATION.md)
separates tested software from the remaining live receive/response work.

- [ ] Complete HT capability/operation negotiation and PHY configuration:
  - [x] Prepare a conservative, unadvertised HT20/800-ns/BCC/one-stream
    capability element, with optional PHY modes disabled.
  - [x] Parse AP HT capabilities/operation and WMM independently of legacy
    association; reject malformed optional elements for HT without
    invalidating an otherwise usable legacy response.
  - [x] Constrain the candidate PHY profile by the accepted response and
    subsequent beacon changes, including channel, basic MCS and protection.
  - [x] Select a common MCS under an explicit ceiling; validate exact
    per-frame MCS requests without silently changing the requested rate.
  - [x] Implement bounded/raw HT mixed-format TX/RX at MCS0..7 while keeping
    legacy PHY formats and receive metadata distinct.
  - [x] Use one validated configuration for HT PHY controls, symbol count,
    mixed-format airtime and L-SIG length; apply airtime to TX deadlines.
  - [x] Preserve legacy-only association and explicit fallback reasons,
    including disabled local HT/extended-RX builds and missing mandatory MAC.
  - [ ] Activate the candidate through atomic station/controller policy and
    actual HT association advertisement only after mandatory MAC/BA,
    receive-size and protection obligations are implemented and qualified.
    - [x] Make existing station/controller policy completion acknowledge an
      applied, readback-verified policy rather than deferred queueing; keep
      pending requests single-flight and report failures explicitly.
    - [ ] Integrate and qualify live aggregate delivery/Immediate BA and
      mandatory receive/protection before adding real HT policy activation
      or association advertisement; the commit barrier alone is insufficient.
      - [x] Deliver all three complete fixture MPDUs through retained
        SHM/SRAM/F2, with independently checked saved bytes and no duplicates
        at MCS0..7 in both directions; keep the incomplete DMA path separate.
      - [x] Replace ordinary held-frame dependence with an explicit PSM
        terminal commit/claim/release; verify bad-last/all-bad handoff and
        avoid duplicate ordinary publication, without enabling production BA or HT.
      - [ ] Generalize receive lengths, admission, repeated aggregates and
        agreement/key/reorder lifecycle beyond the finite diagnostic.
        - [x] Establish a focused unchanged-vendor/AP unicast reference
          with a passive clean observer in both badge role assignments:
          actual received-BA counter deltas and CRC-valid A-MPDU-format
          HT prefixes, not merely enabled flags or submitted bursts.
          See [reference results and limitations](clean-rx/vendor-burst/README.md).
          Complete multi-MPDU body delivery remained unproven in that
          initial reference; this does not close the parent item.
        - [x] Correlate fresh RX epochs/HT prefixes with later MAC headers
          and exact F2 publication IDs; independently validate complete
          variable-length ciphertext MPDUs/FCS from the nonce-derived
          vendor MAC, including A-MPDU-format receptions in both roles.
          Retain one-size controls as failed qualifications. Larger live
          PSDUs still lack terminal commits; multiple MPDUs from one live
          aggregate, sustained reliability and production lifecycle remain open.
        - [x] Add default-off bounded exit/progress records before pending
          ownership is cleared: length/short rejection, abort/fault, watchdog
          and RX/prefix replacement. Observe event 6, good-only bit 8,
          terminal bit 14, raw registers and first-hint RX counts without
          changing the receive handoff or enabling TX.
          Larger live 1598/1342-byte prefixes reach early good-only hints
          then abort at RX counts 560/304; they remain unattributed.
          Five later variable-length controls pass across both roles,
          but do not reproduce those larger exits. Short-exit coverage
          is native-only; see the reference's build/evidence distinctions.
        - [x] Implement default-off complete early-MPDU retention with
          bounded SHM, commit-last fresh-prefix identity and independent
          host FCS validation. Build `e9008554` retains one complete
          170-byte MPDU at nonterminal hint status `0x1922` from a live
          478-byte A-MPDU-format PPDU, vendor B/passive A. Native bounds
          cover every length 44..1000; oversize candidates are counted.
          No early EOI, advance, TX or production HT gate is enabled.
        - [ ] Qualify repeated early retention, other body sizes and both
          roles: five other final-build controls lack a qualified early
          sample. Preserve those failures; one positive is not reliability.
        - [ ] Qualify a justified next-frame/EOF handoff: two independently
          FCS-valid, correctly bounded, nonduplicate MPDUs from one fresh
          vendor PPDU, then repeated aggregates and both roles. The
          observed 478-byte/170-byte early case is a bounded starting point,
          not proof of the second body or a general advance interface.
      - [x] Transmit a default-disabled, explicitly armed PSM compressed BA
        for the fixed aggregate and receive matching complete BA bytes over
        the air; independently verify addresses, bitmap and FCS. All normal
        MCS/direction cells have full delivery/return controls after repeats.
      - [ ] Complete production PSM Immediate BA/admission and independently
        measure EOF-to-air SIFS. The fixed diagnostic does not qualify
        either; bad-final reverse MCS7 still lacks a successful capture.
        Retain mandatory receive-size/protection qualification as a gate.
  - [ ] Qualify interleaved legacy/HT traffic, rate/mode transitions and
    recovery against an independent peer; raw mixed-format captures alone
    do not qualify negotiated mixed traffic.
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
