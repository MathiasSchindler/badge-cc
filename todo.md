# Source-built Wi-Fi firmware milestones

Scope: source-built CYW43439-family controller firmware and its RP2350 host.
This is the functional roadmap: current status, remaining deliverables,
dependencies and acceptance criteria, not a claim of enabled support.

Implementation, runtime integration, native coverage and hardware qualification
are separate states. A completed diagnostic fixture qualifies only its recorded
profile, workload and devices; it does not establish ordinary-runtime support.
Historical results do not automatically qualify a changed image.

## Mandatory directory boundary

- The controller must build from `clean-rx/src/` alone, without libc or inputs
  from the general repository source, host, tests or tools. This includes
  runtime and explicitly selected diagnostic firmware.
- Controller/PSM behavior and public ABI contracts belong in that source tree.
  RP2350 station/application support, host-only software crypto, independent
  peers, native tests and development tools have separate ownership.
  Dependencies may point from consumers to controller contracts, not back.
- Reuse shared infrastructure and the existing badge/Pico consumers where
  appropriate; the controller's standalone boundary does not require those
  consumers to be self-contained. Do not modify unrelated components or
  expand hardware scope without approval.
- Current interfaces belong in `clean-rx/doc/`; tools, procedures, issues and
  evidence belong in `clean-rx/dev-doc/`. This root roadmap owns functional
  checklists, not campaign chronology.

Structural work and its completed C01-C09 milestones are owned by
[cleanup](clean-rx/cleanup.md), not repeated here.

## Definition of done

- **Implemented:** the bounded behavior and error paths exist.
- **Integrated:** the intended runtime/host path uses that behavior, rather
  than only a model, synthetic fixture or dedicated test application.
- **Native coverage:** relevant protocol, ownership, bounds and failure paths
  have regression tests. This is not OTA qualification.
- **Hardware-qualified scope:** matched images pass the stated acceptance
  criteria with independent peer/capture evidence and retained failures.

Unchecked items below are remaining integration or qualification gates as
labelled; they do not imply that all underlying code is missing. Completed
bounded scopes are stated separately from broader open gates.

Count delivered application messages/MSDUs separately from MPDU fragments,
transmission attempts and retries. Retain zero-corruption/duplicate-delivery
requirements; successful RF retries do not erase unexplained transport loss.
Record workload, profile, rates, sizes, peer configuration and denominators.
Existing frame-count and soak thresholds remain goals, not authorization to
run hardware. Agree other measurable bounds before each campaign.

Protect credentials, retain failed controls and exclusions, and back up/restore
borrowed flash with independent readback verification. Follow the
[hardware trial protocol](clean-rx/dev-doc/guides/HARDWARE_TRIALS.md).

Builds, measurements, investigations and historical milestones remain in
[development documentation](clean-rx/dev-doc/README.md) and
[implementation history](clean-rx/dev-doc/history/README.md).
The [issue register](clean-rx/dev-doc/issues/README.md) owns current failure
descriptions; the [R01-R10 tracker](clean-rx/dev-doc/experiments/todo.md) owns
the bounded regression follow-up, not a competing functional roadmap.

## Baseline gates after structural cleanup

**Status:** source isolation and explicit runtime/diagnostic profiles are
implemented and offline-tested. Full post-C05 runtime acceptance remains open.
The later raw RX/TX campaigns provide narrower evidence.

- [ ] Complete [runtime-profile acceptance](clean-rx/dev-doc/guides/HARDWARE_TRIALS.md#runtime-profile-acceptance):
  RX/F2 integrity, normal legacy/HT v1 TX, ACK/CTS timing, station policy,
  passive fault/deadline evidence and recovery.
- [ ] Verify ordinary-runtime rejection of diagnostic commands without traffic
  injection. Do not infer runtime acceptance from diagnostic-profile results.
- [ ] Complete diagnostic-selection/image matching preflight before positive
  RF trials; its campaign-specific implementation is tracked as R09.
- [ ] Retain actionable startup-fault evidence and resolve unexplained startup
  failures before claiming sustained operation or repeatable cold recovery.
  The [R08 startup failure](clean-rx/dev-doc/experiments/RETRY_AND_STALL_R08_2026-10-24.md)
  remains open.

## A. Reliable ordinary Wi-Fi MAC operation

**Status:** substantial controller/PSM and portable host MAC implementation;
full runtime timing, reliability and interoperability qualification remain open.
See [implementation boundaries](clean-rx/badge/README.md#milestone-a-mac-implementation-experimental-not-complete).

### A1. Correct medium access

**Implemented/native coverage:** carrier/NAV handling, DIFS/EIFS, slot backoff,
freeze/resume and bounded contention controls.
**Integration/evidence:** pinned-station paths and bounded fixtures exist;
controlled RF contention and physical timing are not fully qualified.

- [ ] Qualify medium access and contention-window updates in the intended
  runtime path, completing any gaps exposed by that qualification.
- [ ] Verify idle/busy/overlapping traffic, NAV expiry, freeze/resumption and
  retry exhaustion with independent RF channel-access/timing evidence.

Record: [MAC completion and recovery](clean-rx/dev-doc/experiments/mac/COMPLETION_AND_RECOVERY.md).

### A2. Reliable ACKs and retransmissions

**Implemented/native coverage:** response selection, bounded retry/completion
ownership, explicit recovery and retained sequence/fragment/protected bytes/PN.
**Integration/evidence:** bounded legacy RF recovery and fragmentation controls
pass within their recorded scopes; missing RX/ACK, startup and PHY failures
remain. Diagnostic ACK suppression is not ordinary runtime behavior.

- [ ] Qualify response-rate/preamble selection, turnaround, missing/late ACKs,
  retry exhaustion and truthful completion reporting in runtime operation.
- [ ] Independently measure ACK/SIFS timing and full-credit behavior: received
  frames must not be silently discarded after an ACK.
- [ ] Resolve retained unexplained RX/ACK/PHY failures without treating a
  successful retry, stale capture or uncalibrated observer as causal proof.

Records: [recovery and retained failures](clean-rx/dev-doc/experiments/mac/COMPLETION_AND_RECOVERY.md),
[retry/latency measurements](clean-rx/dev-doc/experiments/RETRY_AND_STALL_R08_2026-10-24.md).

### A3. Receive correctness and interoperability

**Implemented/native coverage:** duplicate/sequence tracking, bounded
fragmentation/reassembly, authenticated receive and ownership/credit checks.
**Integration/evidence:** bounded both-role legacy fragmentation tests pass;
the required sustained gate failed. RTS/CTS/protection coverage is incomplete.

- [ ] Complete and qualify remaining RTS/CTS/protection behavior, including
  initiator RTS chaining; qualify existing duplicate/fragmentation paths.
- [ ] Verify malformed/truncated frames, bad FCS, saturation, DMA/credit
  boundaries and recovery in the integrated receive path.
- [ ] Verify at least 100,000 bidirectional application messages per direction
  across small, Ethernet-sized and maximum payloads, with zero corruption,
  duplicate delivery or unexplained transport loss; compare a vendor control.

Records: [legacy fragmentation](clean-rx/dev-doc/experiments/mac/LEGACY_FRAGMENTATION.md),
[MAC qualification history](clean-rx/dev-doc/history/MAC_MILESTONES.md).

## B. Sustained station operation

**Status:** bounded legacy/open lifecycle qualification exists; continuous
networking is implemented but its real 60-minute soak failed. Persistent
software-WPA2 station integration and broader interoperability remain open.

### B1. Connection lifecycle

**Implemented/native coverage:** bounded discovery, authentication, association,
beacon/departure/channel handling, cancellation and cold-controller recovery.
**Qualified scope:** the opt-in legacy/open diagnostic passed ten consecutive
cold connections and five AP-loss/recovery cycles with independent association
and application evidence. Phase cancellation/departure/channel handling have
native coverage; recorded RF exclusions and retry counters remain.

This bounded B1 gate remains complete, not a production-profile or persistent
WPA2 claim. Runtime qualification is owned by the baseline gates; continuous
data and protected persistence are owned by B2/E3.
See [B1 usage](clean-rx/badge/README.md#b1-persistent-open-station-lifecycle)
and [station history](clean-rx/dev-doc/history/STATION_MILESTONES.md).

### B2. Continuous application data

**Implemented/native coverage:** four-slot Ethernet TX/RX queues, backpressure,
static-address ARP/IP/UDP, bounded retries, timer wrap and reconnect handling.
**Integration/evidence:** opt-in legacy/open mode 33 has passing bounded RF
controls; its real 3600-second run retained loss, abandoned work and incomplete
data continuity across recovery, so the hour-long gate remains open.

- [ ] Preserve application continuity across beacon/reception outages,
  disconnects and controller reloads; account for loss and abandoned work.
- [ ] Pass a 60-minute bidirectional soak including idle and desktop polling
  pauses, without corruption, stuck state, unrecovered disconnects or unbounded
  queues.
- [ ] Measure actual desktop pause durations separately from device service
  gaps and retry/loss counters. Complete loaded pause-off/on controls; report
  RF-loss correlation as unavailable without adequate independent evidence.

Records: [B2 interface and usage](clean-rx/badge/README.md#b2-continuous-static-address-networking),
[failed soak](clean-rx/dev-doc/history/STATION_MILESTONES.md#b2-continuous-application-data),
[R08 controls and limits](clean-rx/dev-doc/experiments/RETRY_AND_STALL_R08_2026-10-24.md).

### B3. Reproducible station qualification

**Qualified scope:** three independently data-verified legacy/open sessions
per vendor Pico AP configuration: channels 1/6 legacy and 11 mixed-HT.
E1 separately qualified bounded WPA2 connections to those configurations.
These are configurations of one AP implementation, not three stacks.

- [ ] Qualify sustained open and software-WPA2 operation across additional
  independent AP implementations/configurations, using B2/E3 continuity gates.
- [ ] Define coverage of separate AP nodes in a mesh Wi-Fi system explicitly.
  Ordinary station connections to their BSSIDs do not establish roaming or
  802.11s mesh participation; those features need separate scope decisions.

Records: [B3 boundaries](clean-rx/badge/README.md#b3-reproducible-ap-configuration-qualification),
[station qualification](clean-rx/dev-doc/history/STATION_MILESTONES.md#b3-reproducible-station-qualification).

## C. Complete 802.11b operation

### C1. All legacy DSSS/CCK rates

**Implemented/native coverage:** rate/preamble framing, PLCP and airtime helpers.
**Qualified scope:** independent OTA rate/preamble controls exist; full-sized
1-Mbps RX is unresolved, so all-rate/full-size operation is not complete.

- [ ] Resolve full-sized 1-Mbps reception and qualify complete all-rate TX/RX
  without weakening exact-body, FCS or fresh-capture checks.

Record: [legacy PHY implementation and investigation](clean-rx/dev-doc/history/LEGACY_PHY_MILESTONES.md#c1-all-legacy-dssscck-rates).

### C2. Basic-rate and mixed-network compatibility

**Implementation/integration:** response/basic-rate and ERP fallback mechanisms
exist; complete b-only negotiation and mixed-network protection are unqualified.

- [ ] Complete and qualify b-only rate negotiation, response selection and
  b/g protection, including long-preamble peers.
- [ ] Verify actual b-only and mixed b/g AP traffic and policy changes.

### C3. Rate/size qualification matrix

**Status:** bounded rate/size controls exist, not the complete sustained matrix.

- [ ] Verify every supported legacy rate, both directions, permitted preambles
  and small/Ethernet/2,304-byte MSDUs, with at least 1,000 delivered MSDUs per
  rate/size/direction cell and independent payload/FCS checks.
- [ ] Require zero corruption, duplicate delivery or controller/transport
  faults; record loss/retries and repeat OFDM regressions.

## D. 802.11n

### D1. Confirm the implementation target

**Complete for the conservative control/framing target:** HT20 mixed mode,
one stream, MCS0..7, BCC and 800-ns GI are mapped to controls and frame/status
interfaces. This is neither general HT-station reliability qualification nor
identification of the badge's exact silicon revision.

Optional capabilities remain separate and unadvertised until implemented and
verified; do not assume HT40 or additional spatial streams.
Record: [HT20 target and evidence](clean-rx/dev-doc/experiments/ht20/README.md).

### D2. HT PHY and association

**Implementation/integration:** raw HT and finite aggregate/BA facilities exist;
negotiated HT station advertisement remains disabled.
**Evidence:** native protocol/policy/reorder tests and bounded RF fixtures do
not qualify a general secure HT station.

#### Completed foundations

Conservative PHY/frame interfaces, peer HT/WMM parsing, common-MCS selection,
legacy fallback and atomic policy commits are implemented. Portable WMM/EDCA,
QoS/A-MSDU, BA agreement and ciphertext-reorder software has native coverage.
The standalone two-badge HT messenger has bounded hardware evidence.

R05/R06 add strict fixed three-body delivery/BA controls and the DMA repair;
R07 adds reception/copy-provenance investigation. Initial misses remain
retained. These are foundations, not repeated-variable-aggregate or SIFS gates.
An opt-in first-rejected-ACK60 copy-register trace now covers active and passive
receive roles, with native/raw-report validation and passing hardware zero-state
controls plus one byte-exact active bad-FCS calibration. It does not prove
general SHM freshness, resolve the retained RF failures or close work package 1; see the
[copy-state diagnostic](clean-rx/dev-doc/guides/HOST_TOOLS.md#rejected-ack60-copy-state-trace).
Its [eight-attempt RF investigation](clean-rx/dev-doc/experiments/HT_REJECT_COPY_2026-10-25.md)
reproduced a miss but exposed probe/ACK-state collisions. The allocation fix
subsequently passed four delivery/ACK/passive-observer controls with eight
correctly zeroed traces. No corrected-layout rejection was captured in that run.
A subsequent [three-attempt control campaign](clean-rx/dev-doc/experiments/HT_REJECT_BAD_FCS_2026-10-25.md)
retained a complete matching active rejection of a deliberately bad-FCS fixture.
A subsequent [five-attempt normal campaign](clean-rx/dev-doc/experiments/HT_SPONTANEOUS_REJECTION_2026-10-25.md)
captured a spontaneous B-to-A miss with those calibrated images and injection
disabled. The active record binds to rejection metadata, but 54/60 MAC bytes
differ and complete identity fails; a matching fresh six-byte payload run
does not prove full-frame provenance. The Pico also missed a target-delivered,
ACKed normal frame. The campaign stopped immediately and restored all originals.
The next [eight-attempt baseline campaign](clean-rx/dev-doc/experiments/HT_REJECT_BASELINE_2026-10-25.md)
captured an A-to-B miss with bound active/Pico rejections sharing 32 incorrect
MAC bytes. All retained MAC bytes changed from each receiver's initial SHM,
excluding untouched startup content for that miss. The controller/PSM remained
byte-identical; the host-only baseline is default-off. The corrected passive
arm is now exercised, not byte-exactly calibrated. Intervening-copy provenance,
complete spontaneous-frame identity and TX/RF versus shared RX remain open;
all originals were restored and verified.
Methods, excluded repair hypotheses and microscope chronology belong in
[HT history](clean-rx/dev-doc/history/HT_MILESTONES.md) and
[canonical experiments](clean-rx/dev-doc/experiments/ht20/README.md).

#### Remaining work packages

- [ ] **1. Qualify complete HT reception:** resolve pre-prefix, RXE-busy,
  truncated-body and copy-freshness failures; verify complete independent
  bodies/FCS and end-to-end delivery without depending on a particular
  early-capture diagnostic as the product interface.
- [ ] **2. Qualify general aggregate continuation:** build on the repaired
  SHM/DMA ownership paths, not an assumed obsolete repair branch. Verify
  full-body ownership/publication across lengths and both roles/orders,
  including malformed/truncated input, aborts and recovery beyond fixed slots.
- [ ] **3. Receive repeated variable-length aggregates:** integrate bounded
  buffering and transport backpressure across successive PPDUs without
  one-shot/fixed-source assumptions.
- [ ] **4. Integrate secure Block ACK lifecycle:** wire agreements/windows,
  admission, ciphertext reordering, duplicate/replay checks and key changes
  into actual station RX; handle teardown, timeout and reassociation.
- [ ] **5. Complete Immediate BA and ACK responses:** drive responses from
  admitted frames, verify returned bytes/bitmaps across supported MCS and
  bad-frame cases, and independently measure EOF-to-air SIFS. Reuse A2's
  shared ACK ownership/timing gate rather than duplicating it.
- [ ] **6. Qualify mandatory receive and protection:** integrate required sizes,
  A-MSDU/QoS, WMM/EDCA and HT/legacy protection; verify protected traffic,
  AP policy changes and recovery.
- [ ] **7. Activate HT association:** only after mandatory MAC, receive-size
  and protection gates pass, advertise the supported profile and atomically
  apply negotiated policy with explicit legacy fallback.
- [ ] **8. Qualify negotiated interoperability:** verify association, actual
  HT OTA framing, interleaved legacy/HT, rate/mode changes, negative/fallback
  cases and recovery against an independent HT20 peer.

**Implementation progress (2026-10-25):** the secure BA software now has
explicit WPA-owned lifecycle binding, PTK-transition/abort/timeout wiping,
and callback-safe cleanup of queued ciphertext and partial streams.
A pressure-safe PPDU begin/retry path is native-tested at the exact 16-slot
pool limit and across three successive variable-length software PPDUs.
GTK changes and duplicate M3 messages retain unicast BA state; reassociation
cannot reuse an old agreement or its queued ciphertext.
See [the current API and offline tests](clean-rx/dev-doc/guides/HOST_TOOLS.md#d2-secure-block-ack-software-lifecycle).
This advances prerequisites for packages 3/4, **not** their live integration.
The canonical WPA receive entry point now dispatches bound active agreements
through ciphertext reorder and services BARs; normal polling services gaps and
agreement timeouts. Ordered EAPOL replies use one bounded output slot, stalling
before further authentication/PN changes until serviced. The badge caller retains
input on explicit retry and pauses output/delivery while a controller TX is
outstanding. Native regressions cover station preprocessing, pool saturation,
two ordered GTK replies, PTK invalidation, deferred cleanup and paused-TX timeout.
This connects the software ingress/caller portion of package 4, but the live
station still cannot bind/activate agreements: there is no general aggregate RX
or production controller BA-install/revoke backend. No BA storage is allocated
or agreement advertised implicitly. Default-runtime/extended-WMM WPA sanitizer
tests, shared authenticator regressions and linked badge/Pico firmware builds
pass; this software step did not flash devices or qualify live BA operation.
A subsequent default-off
[repeated retained-SHM handoff](clean-rx/dev-doc/guides/HOST_TOOLS.md#repeated-retained-shm-handoff-offline-prototype)
now reuses terminal leases with bounded SRAM ownership, explicit claim/release
checks and PSM rearm. Its actual controller code passes 100 successive
PSM-model leases, full-ring/all-bad controls and counter-wrap tests.
The default one-shot path remains; both paths still use the bounded
three-by-80-byte fixture, not general variable-length reception. A persistent
mode marker prevents finite-fixture tools from misqualifying repeated reports;
the native harness now exports persistent execution transcripts checked by an
[independent multi-PPDU model oracle](clean-rx/dev-doc/guides/HOST_TOOLS.md#independent-repeated-execution-oracle).
It verifies 114 modeled PPDUs and 115 complete delivered bodies, full-ring
pressure, no-credit bad-frame draining and both counter wraps, with tampered
transcript controls. This is offline fixed-fixture evidence; live multi-PPDU
collection, general RXE completion and hardware validation remain open.
Packages 1/2 remain the first qualification gates, and physical
response timing/protection/interoperability gates remain open. No checkbox is
closed and negotiated HT stays disabled.

The subsequent [per-candidate pre-copy diagnostic](clean-rx/dev-doc/experiments/HT_CANDIDATE_TRACE_2026-10-25.md)
adds default-off snapshots bound to the first rejection and independently
checks raw/JSON fields, preimage comparisons and counter/TSF wrap. Its first
authorized hardware attempt stopped on unavailable receiver snapshots.
Missing initialization of its diagnostic SHM was repaired and tested offline.
A separately authorized [repaired-image follow-up](clean-rx/dev-doc/experiments/HT_CANDIDATE_REJECTION_2026-10-25.md)
stopped on its first normal control failure with valid reports and two bound
rejections; no incorrect MAC bytes equalled their own pre-command snapshots.
Probe positive/negative calibration, complete identity and TX/RF versus shared
RX remain unresolved. Both attempts are separately sealed and all originals
were restored and fully verified after each run, with devices in BOOTSEL. No
independent timing equipment was available, so physical SIFS also remains
blocked. These diagnostics do not implement or close any remaining D2 package.

The separately authorized [twelve-attempt matched comparison](clean-rx/dev-doc/experiments/HT_PROBE_CROSSOVER_2026-10-25.md)
keeps TX/Pico probe-off while changing the target probe. Both probe states
delivered 3/4 complete normal bodies. A probe-off identity-correlated rejection
retains 15 wrong MAC bytes; another probe-off trial delivered the full body but
lost the returned ACK. The probe is not necessary for those failures, not
proven harmless. Three passive byte-exact negatives and one probe-on active
negative at receiver B add bounded calibration; receiver A's probe-on negative
was missed. All 36 reports were independently replayed, evidence sealed and
originals restored/full-verified. Root cause, independent receiver-code
observation, general integration and SIFS gates remain open.

Current evidence: [R05 aggregate acceptance](clean-rx/dev-doc/experiments/HARDWARE_ACCEPTANCE_R05_2026-10-24.md),
[R06 DMA repair](clean-rx/dev-doc/experiments/DMA_REPAIR_R06_2026-10-24.md),
[R07 loss-stage limits](clean-rx/dev-doc/experiments/HT_ROOT_CAUSE_2026-10-24.md).

### D3. HT MAC and performance

**Dependency:** D2 owns aggregation, BA/reorder/lifecycle implementation and
integration. D3 owns sustained correctness/interoperability and performance
qualification of that integrated path.

- [ ] Verify every supported MCS in both directions at multiple payload sizes,
  at least 1,000 delivered messages per rate/size/direction cell, without
  corruption, duplicate delivery or reordering errors outside the interface.
- [ ] Qualify loss recovery and replay/duplicate behavior under sustained load.
- [ ] Measure sustained throughput, loss and CPU/transport limits, including
  mixed legacy/HT traffic and a sustained station soak.

## E. Complete WPA2 operation

**Status:** E1 and E2 have bounded fixture qualification. Persistent integrated
protected operation remains open. Scope is WPA2-Personal/CCMP, not
Enterprise/EAP or obsolete ciphers. Software crypto currently belongs to the
RP2350 host, not the freestanding controller.

### E1. Robust four-way handshake

**Implemented/native coverage:** bounded timers/retries, duplicate/replay/MIC/
nonce/RSN handling, passphrase/raw-PSK provisioning, failure wiping and
install-once behavior.
**Qualified scope:** ten controlled channel-1 protected connections, three
each on channels 6/11, and three wrong-password controls against one vendor
AP implementation. This gate remains complete within that bounded scope.

Record: [E1 commands and boundaries](clean-rx/badge/README.md#e1-protected-pico-ap-matrix).

### E2. Group traffic and rekey

**Implemented/native coverage:** GTK slots/replay state, protected group traffic,
bounded group/pairwise rekey, duplicate/lost-response handling, PN continuity
and key replacement/wiping.
**Qualified scope:** the single-station legacy, readback-verified 1-Mbps,
PTK-first alternating fixture completed three group and three pairwise rekeys
during protected unicast/broadcast/multicast traffic on channels 1/6/11.

This bounded E2 gate remains complete. Earlier failed/group-first RF controls
remain retained; native coverage of both orders is not their RF qualification.
This is not arbitrary-authenticator, persistent-link, HT/BA or E3-soak support.
Record: [WPA2/rekey implementation and evidence](clean-rx/dev-doc/history/WPA2_MILESTONES.md).

### E3. Sustained protected operation

**Implemented/native coverage:** key/secret/plaintext wiping, unsupported-RSN
rejection, MIC/replay checks and PN-exhaustion protection already exist.
**Integration/evidence:** bounded E1/E2 fixtures do not establish protected
continuity across all disconnect, reload, key-change and traffic paths.

- [ ] Complete runtime lifecycle integration of teardown, unsupported-security
  rejection and clean reconnect/reload without stale keys or plaintext.
- [ ] Qualify MIC/replay rejection, PN exhaustion and retry/key-change behavior
  across supported traffic classes; extend native/integration coverage where
  the existing scoped tests do not cover a path.
- [ ] Pass B2's protected 60-minute soak with rekeys and independent traffic
  evidence, retaining failures and continuity accounting.

## F. Hardware crypto offload

**Status:** optional later optimization. Raw controller hardware hooks exist;
the station uses software crypto. Do not infer offload equivalence from key
installation or raw encrypted transmission.

### F1. Establish hardware semantics

- [ ] Establish key slots, header/cipher setup, PN/MIC/FCS ownership and RX errors.
- [ ] Preserve authenticated delivery and replay checks with a software reference.

### F2. Functional equivalence

- [ ] Integrate host/controller key lifecycle and supported traffic/rekey paths.
- [ ] Compare independent known-answer ciphertext, tags and plaintext across
  header, size, PN and key boundaries.
- [ ] Verify rejection, retries, replacement and reset before plaintext delivery.

### F3. OTA and resource qualification

- [ ] Repeat encrypted unicast/group, E rekey and B2 soak qualification.
- [ ] Measure CPU, throughput, latency, stack/RAM and image size against software,
  distinguishing RP2350, controller and transport costs.
- [ ] Consider default enablement only after equivalent correctness/recovery
  and a demonstrated benefit; default offload is not itself a requirement.

## Sequencing

1. Close baseline/profile/evidence gaps and investigate retained startup and
   receive/ACK failures; use R09/R10 for the associated bounded follow-up.
2. Qualify A and B's legacy foundations and sustained continuity. C may progress
   alongside them; keep existing supported behavior regression-tested.
3. Integrate and qualify persistent software-WPA2 under E3 before treating
   protected station operation as complete or qualifying hardware offload.
4. Continue gated HT investigations independently, but do not enable negotiated
   HT before D2's mandatory MAC/receive/protection gates. D3 follows the
   integrated path, not a separate duplicate implementation.
5. Evaluate F only as an explicit optimization after software correctness and
   lifecycle baselines exist.

No roadmap entry authorizes hardware trials or requires a new framework,
package manager or CI service. Record evidence in the canonical development
records before changing a qualification status here.
