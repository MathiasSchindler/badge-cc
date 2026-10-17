# Source-built Wi-Fi firmware milestones

Scope: the dependency-free CYW43439 firmware and RP2350 host in `clean-rx/`.
Vendor firmware may be used as an independent test peer, not as the
implementation being qualified. This is a roadmap, not a claim of support.

## Mandatory directory boundary

- All Wi-Fi-chip firmware implementation, refactoring, firmware-specific host
  support, tests and supporting documentation for these milestones must live
  under `clean-rx/`. This root-level roadmap records their progress.
- Put controller firmware and portable shared C/headers in `clean-rx/src/`;
  keep the associated RP2350 diagnostic host, collector and native tests in
  `clean-rx/badge/`.
- Other firmware and experiment directories, including `experimental/cyw43/`,
  are unrelated to this work and must be left alone. Do not modify, reorganize,
  migrate or extend them, or use them as alternative implementation targets.
- Existing shared repository infrastructure and independent vendor-firmware
  test peers may be used without modification. If a milestone appears to
  require changes outside `clean-rx/`, stop and obtain explicit approval rather
  than expanding its scope.

Existing foundations include legacy OFDM at 6-54 Mbps, qualified 11 Mbps CCK
raw exchanges, transport for full 2,304-byte MSDUs, and an experimental
WPA2-PSK/software-CCMP protected round trip. Existing hardware-crypto hooks
are not yet qualified. See [current status and limitations](clean-rx/badge/README.md).

## Definition of done

Every milestone must satisfy all three gates:

- **Implemented:** integrated into the actual firmware/host path, with explicit
  failures for unsupported or invalid operations and updated documentation.
- **Tested:** focused native regression tests, sanitizers where applicable,
  freestanding builds without unresolved runtime dependencies, and real
  hardware tests covering positive, negative and recovery cases.
- **Verified:** independent peer/capture evidence demonstrates the claimed
  behavior. Record firmware build IDs, peer configuration, channel, rates,
  frame sizes, durations, counters and failures. A mailbox counter, template
  submission, association indication or local TX completion alone is not proof.

Keep credentials and keys out of public reports and captures published with
results. Remove temporary credentials; back up and restore badge applications
with verified readbacks when borrowing hardware for qualification.

Unchecked items remain open even when experimental code already exists.

## A. Reliable ordinary Wi-Fi MAC operation

**Status: partially implemented, not complete or RF-qualified.** The
experimental station profile now integrates carrier/NAV/backoff handling,
bounded ACK/retry accounting, credit/DMA-safe capture retention, ERP-safe CCK
fallback, receiver-side RTS/CTS, long/short control-response selection,
non-QoS duplicate suppression, authenticated/unprotected reassembly, and
queued basic-rate/slot updates from bound AP beacons. Profile 3 now moves
carrier/NAV arbitration, explicit DIFS, slot countdown, EIFS and final template
trigger into the PSM using the hardware TSF clock; profile 2 retains the
earlier Cortex scheduler. Native tests pass.
Build `2269c6c0` completed 5,000 ACK-confirmed raw application frames per
direction between two clean badges, including 12-bit sequence wrap, in
225.05/228.27 seconds. Payloads alternated 32/1,500/2,304 bytes at 6 Mbps,
channel 1, with zero corruption, duplicate delivery or ordering errors.
This is not a vendor-control comparison or the required 100,000-frame run.
The continuation adds multi-wrap runtime accounting, controlled missing-ACK
recovery/exhaustion, and a late-retry duplicate-cache fix. Final build
`9c6fb6d6` passes a 100-frame-per-direction controlled-loss smoke test.
Two intermediate 10,000-target runs failed, first with duplicate application
delivery and then with a controller TX-completion fault. The latter remains
unresolved; this is progress, not extended-soak qualification.
On that build, the 15-trial vendor-AP runner confirmed 3/10 protected
exchanges at both endpoints, both wrong-password controls and all three open
controls. Three more station reports validated protected echoes but lacked
the required independent AP report; four protected trials lacked usable
station reports or AP readiness. USB/report/startup failures remain unresolved,
all seven unconfirmed trials are retained, and the runner exited unsuccessfully.
Earlier build `5eeed6e7` completed independently confirmed protected
exchanges in 9 of 10 fresh controlled vendor-AP attempts. In the unconfirmed
attempt, the clean station authenticated/associated but received no EAPOL M1;
the independent vendor peer reported `RADIO ERROR` (firmware error 7) and
produced no final AP observation. This is not a confirmed protected exchange
or evidence that the clean MAC caused the failure. Two wrong-password controls
were valid and three open-AP controls passed. No attempts have been excluded.
RTS/short-preamble/fragmented RX and dynamic policy changes still need OTA
qualification. Precise independent RF timing, controlled late-ACK/credit tests,
the required full OTA soak/vendor comparison and earlier unexplained no-ACK
authentication failures remain open. Initiator RTS chaining is not implemented;
ERP protection uses the advertised long-preamble CCK fallback instead. See the
[implementation, evidence and limitations](clean-rx/badge/README.md#milestone-a-mac-implementation-experimental-not-complete).
Neither native 100,000-frame simulations, the bounded connection runner nor
the shorter two-clean-badge soak satisfies the OTA acceptance test below.

### Implemented and regression-tested progress

These completed subitems do **not** close the A1-A3 acceptance gates:

- [x] Consume valid local ACKs directly in the PSM, without DMA publication
  or host-credit dependence; test out-of-window ACKs, counter wrap and
  100,000 simulated immediate RX rearms.
- [x] Correct all six RA/TA checks that used two SHM input operands in one
  instruction, an invalid D11 encoding. Add an ISA-wide operand-limit test
  and flip all 96 RA/TA bits in ACK/CTS rejection tests. The new ISA test
  failed before the fix. Older local ACK counters are not proof of correct
  peer filtering; independently confirmed protected echoes remain valid.
- [x] Separate a MAC retry from a new management request after an ACKed
  request. Preserve no-ACK retry identity, protected retry payload/PN and
  zero-attempt BUSY state; use fresh sequences for new requests. Keep the
  four-submission/1.6-second management-phase limits unchanged.
- [x] Prevent matching-BSSID non-beacons from replacing a valid discovery
  result with a beacon-parser error; test probe/data/auth/association frames
  and truncation. This fixes a demonstrated code defect, not a traced
  diagnosis of the earlier pre-station failure.
- [x] Publish response-class, bound transport-publication and invalid-FCS
  diagnostics, with explicit startup reset and layout guards.
- [x] Add `clean-rx/badge/mac_trials.sh`: ten protected trials, two
  wrong-password controls and three open controls, retaining failures and
  requiring independent matching endpoint evidence. It does not flash or
  change the host network and is not a continuous traffic-soak fixture.
- [x] Add experimental PSM-clocked profile 3, with physical/virtual carrier,
  explicit 28/50-us DIFS, 9/20-us slots, frozen residual backoff, bad-FCS and
  bad-length EIFS, receive priority and a PSM-owned final template trigger.
  Exercise all 4,096 lower IFS status combinations and timing/wrap boundaries
  natively. Hardware contention timing is not independently qualified.
- [x] Use the same TSF clock domain for PSM deadlines and Cortex comparisons.
  Distinguish LP-PHY OFDM using PHY status rather than the G-PHY-only flag;
  reject unsupported HT response formats. Prepare complete response templates
  before arming/triggering TX, and hold TXE ownership until the response tail
  completes, preventing Cortex template preparation from interrupting it.
- [x] Add a continuous two-clean-badge raw-unicast fixture with fresh challenges,
  exact payload/FCS validation, bounded retries, explicit failures, independent
  endpoint counts and USB/watchdog servicing. Verify 5,000 frames each way,
  including sequence wrap and repeated watchdog renewals; report 169/64 ACK
  timeouts and 1/1 suppressed MAC duplicates, not application losses.
- [x] Replace the fixture's 32-bit elapsed ceiling with 64-bit accumulated
  microseconds and a two-hour radio deadline; keep ordinary diagnostics'
  six-second default. Add exact-boundary, two-wrap and cancellation tests,
  52-byte `MSK2` status, matching host/loader margins and 250-ms soak polling.
  Multi-wrap correctness is natively tested, not a completed two-hour RF run.
- [x] Add profile-3-only bounded suppression of 0..16 otherwise eligible ACKs,
  with explicit invalid-input errors and actual-drop diagnostics. Test all
  12 rates, pinned-address rejection, exhaustion/resumption, no TX trigger
  while dropping and unaffected CTS. Verify missing-ACK recovery and explicit
  eight-attempt exhaustion on both badges; ordinary station paths stay unchanged.
- [x] Fix delivered-frame duplicate identities expiring after 512 ms during
  longer retry exchanges. The regression failed before the fix; retain
  delivered identities until history replacement/fresh non-Retry reuse,
  while separately expiring/restarting incomplete assemblies. Test timer wrap,
  sequence reuse, completed-cache preservation and existing 100,000 native
  deliveries. This does not establish RF qualification of late ACKs.
- [x] Pass controller/badge native tests, configured ASan/UBSan regressions,
  all ten existing Micro Signal builds and unresolved-symbol checks for
  both freestanding ELFs.

### Hardware evidence and outstanding work

- Intermediate `06cdb3cc` and `0847d369` each passed 29/30 protected trials;
  do not pool their results with the retained build. `0847d369` trials 13
  and 16 recovered after a second ACKed association request; trial 10
  exhausted authentication retries without ACKs.
- `851d14b4` passed 7/10 protected trials: one pre-station `CLEAN_INPUT`
  failure (error 6, AP profile `-1`, no management submissions) and two
  four-attempt authentication failures. Its two wrong-password and three
  open controls were valid. The pre-station error was not a timeout.
- `5eeed6e7` passed 9/10 protected trials plus both wrong-password controls
  and all three open controls. Every station report authenticated/associated;
  protected trials 6/10 and open trial 2 needed additional association
  requests. The runner correctly exited unsuccessfully for the unconfirmed
  vendor-peer-error attempt. Evidence prefix:
  `build/clean-rx/badge/mac-a-regression-5eeed6e7`.
- Four valid unchanged-vendor association/authorization controls passed;
  two other attempts failed to launch and were not RF trials. These were
  not protected-data soaks. The fixture was vendor build `0353f1de`,
  channel 1; clean protected requests carried 32-byte plaintexts in
  76-byte MPDUs at 6 Mbps OFDM.
- Experimental profile 3 replaces timing-critical Cortex polling with
  PSM/IFS/TSF scheduling. Its 5,000-frame result is retained at
  `build/clean-rx/badge/mac-a-soak-owner`; the two endpoints reported
  5,169/5,064 actual TX attempts, 6,231/3,619 zero-attempt deferrals, and
  exactly 5,000 received and ACK-confirmed frames each. Global bad-FCS
  diagnostics include unrelated channel traffic, not just bound-peer loss.
- Retain failed development runs: before response-TXE ownership was fixed,
  `a5c9577d` stopped after 2,615 frames each way with a controller TX-completion
  error; a `54054044` 100,000-target attempt stopped near frame 523 with a
  missing initiator USB report and responder retry exhaustion. These are
  failures, not successful qualification runs or evidence for the later build.
  The earlier 3,500-second runtime ceiling has now been replaced by a
  7,200-second radio deadline with multi-wrap accounting, but the full
  100,000-target run has still not passed.
- Controlled-loss development evidence is retained under `build/clean-rx/badge/`.
  Build `5d7008c7`, prefix `mac-a-fault-long`, failed a 10,000 target after
  initiator 483 ACK-confirmed/482 received, including one duplicate application
  delivery; responder 482/483 ended in retry exhaustion. The duplicate-cache
  expiry mismatch was reproduced natively and fixed. Intermediate `3414b640`,
  `mac-a-duplicate-recovery`, failed the repeat at initiator 449/448 and
  responder 448/449, with no corruption/duplicate delivery/order errors.
  Responder controller error `0x82`, substage 5, address `0x1800150e`, value
  `0x00c1002f` is a TX-completion-condition failure and remains unresolved;
  the initiator subsequently timed out. Neither failed run is excluded.
- Final build `9c6fb6d6`, `mac-a-final-recovery`, passed exactly 100
  ACK-confirmed/received frames each way with two deliberately suppressed ACKs
  each, 117/115 actual attempts, 17/15 ACK timeouts, 67/40 deferrals and
  7/4 suppressed MAC duplicates. Corruption, duplicate application delivery and
  ordering errors were zero. Same-build `mac-a-final-exhaustion` correctly
  failed at index zero after exactly 8 actual attempts/8 ACK timeouts each,
  with no ACK confirmation and one incoming application delivery each.
  Actual suppressed ACKs were 7/3, not the configured eight each.
  These exact-assertion-checked smoke/negative tests are not a replacement
  for either failed extended run, nor evidence of late-ACK/SIFS/credit timing.
- Latest vendor-AP regression evidence is retained under
  `build/clean-rx/badge/mac-a-psm-vendor`, build `2269c6c0` against unchanged
  vendor `0353f1de`, channel 1. The six complete positive station reports
  authenticated/associated on their first requests and validated protected
  echoes, but only three had the required independent AP observation.
  Missing reports, USB transfers and AP startup must be made reproducible
  before this can be used as a reliable connection qualification result.
- Independent RF qualification is still required.
  Precise contention/SIFS timing, controlled late ACKs and
  occupied-credit RF tests, response/fragment/policy OTA matrices, and the
  100,000-frame-per-direction OTA run and continuous vendor-control peer have
  **not** been completed. Earlier unexplained failures are not declared
  resolved merely because they were absent from the latest small sample.
- No extra timing hardware was available. The Mac's networking was left
  unchanged as requested; no host monitor-mode capture was attempted.
- After testing, badge `08DA26F86E57F5EA` had its original 33,604-byte
  application restored and independently read back/compared; only its
  application was backed up, not the entire flash. Badge `9BBF88DD17D66E7D`
  had its original 16 MiB flash restored and independently read back/compared
  against its full backup. Both were left in BOOTSEL.
  This was repeated after the profile-3/continuous tests, with readbacks
  `mac-a-psm-restored-A.bin` and `mac-a-psm-restored-B-full.bin` under
  `build/clean-rx/badge/`; both comparisons passed.
  Restoration was repeated after the controlled-loss continuation with
  `mac-a-final-restored-A.bin` and `mac-a-final-restored-B-full.bin`;
  independent application/full-flash comparisons passed, and both remain BOOTSEL.

### A1. Correct medium access

- [ ] Implement and document physical carrier sensing, virtual carrier sensing
  through Duration/NAV, applicable interframe spacing, random contention
  backoff, backoff freezing/resumption, and contention-window updates.
- [ ] Test idle, busy and overlapping traffic, including NAV expiry and retry
  exhaustion. Use deterministic native tests for scheduling arithmetic.
- [ ] Verify channel-access behavior with independent, sufficiently precise
  captures under controlled contention; do not infer RF timing from CPU delays.

### A2. Reliable ACKs and retransmissions

- [ ] Complete correct response-rate/preamble selection, RX-to-TX turnaround,
  ACK timeout handling, bounded retries and retry-status reporting.
- [ ] Preserve sequence/fragment identity across retransmissions. For protected
  retries, preserve the encrypted payload and PN; update Retry/FCS correctly.
- [ ] Verify ACK RF/SIFS timing with an independently validated timing source.
  Exercise missing ACKs, delayed responses, unrelated RX and occupied host
  credits. No captured or acknowledged frame may be silently discarded.

### A3. Receive correctness and interoperability

- [ ] Implement duplicate suppression, sequence-number wrap handling, supported
  fragmentation/reassembly, and applicable RTS/CTS and protection handling.
- [ ] Test malformed/truncated frames, bad FCS, duplicates, fragments,
  saturation and DMA/credit boundary conditions without hangs or corruption.
- [ ] Verify at least 100,000 bidirectional unicast application frames per
  direction across small, Ethernet-sized and maximum supported payloads.
  Count loss, retries and duplicates explicitly; require zero corruption,
  zero duplicate delivery and zero unexplained transport drops. Report RF
  loss separately and compare it with a vendor control under the same setup.

## B. Sustained station operation

### B1. Connection lifecycle

- [ ] Implement a persistent station state machine: discovery, AP selection,
  authentication, association, connected operation, departure and recovery.
- [ ] Manage beacon loss, deauthentication/disassociation, AP restart,
  channel changes and cancellation without stale state or leaked resources.
- [ ] Verify 10 consecutive cold connection attempts and five AP-loss/recovery
  cycles. Require fresh valid connection evidence and working data each time.

### B2. Continuous application data

- [ ] Provide an actual Ethernet/MSDU send/receive interface with bounded
  queues, backpressure, meaningful errors and continuous USB/host servicing.
- [ ] Exercise it with a small ARP/IP/UDP test path on the RP2350, using DHCP
  or explicitly documented static addressing. An IP stack is a host-software
  task, not a prerequisite for raw MAC operation.
- [ ] Complete a 60-minute bidirectional traffic soak, including idle periods,
  full-sized frames and host stalls. Require no crash, watchdog reset,
  memory corruption, stuck credit, unrecovered disconnect or unbounded queue.
  Report delivery, loss, retries, latency and memory/stack bounds.

### B3. Reproducible station qualification

- [ ] Repeat connection/data tests against the controlled vendor AP and at
  least two additional AP implementations/configurations available for testing.
- [ ] Verify open operation and, after E, sustained WPA2 operation. Include
  separate nodes of the local mesh without claiming roaming support merely
  because association with individual nodes works.

## C. Complete 802.11b operation

### C1. All legacy DSSS/CCK rates

- [ ] Implement/repair TX and RX configuration for 1 and 2 Mbps DSSS and
  5.5 and 11 Mbps CCK. Retain existing 11 Mbps qualification as a regression.
- [x] Implement correct PLCP construction/validation, airtime calculations,
  and long/short preamble behavior where permitted by the standard.
- [x] Test rate/preamble boundary cases and verify actual OTA modulation and
  rate independently, not just requested rate fields or successful TXE status.

**C1 progress: implemented and substantially tested, but not fully closed.**
The first checkbox remains open because full-sized 1-Mbps reception still
fails; small and 1,500-byte exchanges at that rate work. Do not interpret the
rate/preamble results below as full 802.11b qualification.

Implementation is entirely under `clean-rx/`:

- Shared [DSSS/CCK PHY helpers](clean-rx/src/phy_rates.h) select all four
  rates, generate/validate complete six-byte PLCP headers, check CRC/SERVICE/
  length extension, and calculate long/short airtime. Short 1 Mbps is rejected.
  TX PHYCTL1 now follows the validated SIGNAL rate, rather than a fixed
  11-Mbps value. Command 10 requests short CCK without repurposing any
  existing command-5 flags.
- Slow CCK ACK waits anchor to the first observed physical TX-active-to-idle
  transition without enlarging the existing 398-us ACK allowance. Completion
  polling accounts for slow full-sized frames; OFDM scheduling is unchanged.
- Receiving rate/preamble evidence is taken from the separate receiving
  chip's PLCP and held PSM PHY header. An intermediate test exposed a stale
  DMA-header association; the final implementation snapshots the status
  belonging to the held capture instead. Both transport fragments preserve
  metadata, and the fixture rejects modulation/rate/preamble mismatches.
- [Native tests](clean-rx/badge/loader_test.c) exhaust MPDU lengths 14..2400
  at all four rates, independent fixed CRC vectors, every header-bit
  corruption, all seven legal preambles, illegal preambles, fragmented RX
  metadata and every explicit fixture PHY/payload selection. Controller,
  loader/strict-loader, WPA2/crypto and secret tests pass with sanitizers;
  PSM image identity, single-SHM inputs and PHY-header latch checks pass.
  All ten existing Micro Signal modes build and their native regression
  tests pass. Both freestanding ELFs have no unresolved symbols.

**Final paired OTA evidence, build `9c3deabc`, channel 1:** each passing cell
below exchanged 100 fresh, ACK-confirmed frames **per direction**, with
independent payload/FCS and receiving-PHY checks.

| PHY/preamble | 32-byte MSDU | 1,500-byte MSDU | 2,304-byte MSDU |
| --- | --- | --- | --- |
| 1 Mbps long | Pass | Pass | **Not passed** |
| 2 Mbps long | Pass | Pass | Pass |
| 5.5 Mbps long | Pass | Pass | Pass |
| 11 Mbps long | Pass | Pass | Pass |
| 2 Mbps short | Pass | Pass | Pass |
| 5.5 Mbps short | Pass | Pass | Pass |
| 11 Mbps short | Pass | Pass | Pass |
| 6 Mbps OFDM regression | Pass | Pass | Pass |

The 23 passing cells total 4,600 application deliveries/ACK-confirmed TXs,
4,600 receiver PHY checks, 4,639 TX attempts and 39 ACK timeouts. They have
zero corruption, duplicate application deliveries, ordering errors,
PHY mismatches or controller faults. Retransmissions are not counted as new
deliveries. Evidence is retained under
`build/clean-rx/badge/c1-final-PHY-SIZE-*`, including configuration,
endpoint reports and logs.

**Remaining blocker:** full 2,304-byte MSDUs at 1 Mbps long. Inspection of
`c1-final-1-long-2304-run.log` corrected the earlier characterization of the
final fixed-size cell: picotool aborted during reboot/BOOTSEL launch
(`cancelled_exception`); this was not an RF trial or a measured loss rate.
Earlier retained cyclic-size trials (`c1-tail-1-long`, build `b678cb3e`)
delivered 32/1,500-byte frames but exhausted eight attempts on the full-sized
frame. A hardware-clock RX-deadline experiment did not repair reception and
was removed. No speculative PHY-register write or increased ACK allowance
is retained.

**Follow-up receive-truncation investigation, build `956a3e85`:**
paired channel-1 tests retained as `build/clean-rx/badge/c1-cause-retained-*`
reproduce the failure independently of the aborted launch:

- `full`: a 2,304-byte MSDU has a 2,332-byte MPDU. The receiver decodes the
  correct PLCP `0a04e048c464` (18,656-us LENGTH), but its RXE count reaches
  only 1,589 bytes at the PSM no-GoodFCS expiry. The transmitter consumes
  the complete staged template (end pointer 585 words, remaining count 0),
  reports no PHY TX error, and shows about 19,219 us of TX-active time.
  These counters do not independently prove the complete OTA waveform.
- `reverse`: swapping badge roles passes 1,500 and 1,550-byte MSDUs in
  both directions, then fails at 1,600 bytes. That failing receiver decodes
  the correct PLCP `0a04e03219b8` (13,024-us LENGTH), but again stops at
  RXE count 1,589. Thus the problem starts below the maximum MSDU and below
  the F2 fragmentation boundary; it is not solely a 2,304-byte buffer issue.
- Other trials observe 1,590 bytes. Same-attempt PSM tracing measures
  approximately 16,429 us from decoded PLCP to the normal discard.
  A removed, SHM-backed two-pass wait experiment (`e24cdff6`,
  `c1-cause-count-shm`) extends this to about 32,814 us, yet RXE still
  reports only 1,590 bytes. Waiting longer alone does not repair reception.
  Earlier traces that mixed a first-prefix timestamp with a later retry's
  failure timestamp are not valid elapsed-time evidence.
- Removing PLCP SERVICE Locked Clocks, and separate receiver-only changes
  to PHY `0x42c` (2590 to 5120) and `0x465` (10 to 20), did not help.
  The latter register names come from related LP-family public definitions,
  not verified LCN20 semantics. All those experiments were removed.
- On this same retained build, `2-long`, `11-long` and `ofdm6` each pass
  30 fresh cyclic-size frames per direction (32/1,500/2,304 bytes):
  180 total verified deliveries, with no fixture failure.

The verified failure is incomplete PHY/RXE reception, followed by no valid
FCS, no published MPDU and consequently no ACK. The initiating PHY/RXE
condition or exact control remains **unresolved**; a receive-duration limit
is a hypothesis, not an identified hardware watchdog. C1 stays open.
Retained diagnostics include a boundary fixture, retry-aligned PSM trace
and stopped-failure snapshots; see the
[diagnostic layout](clean-rx/doc/HARDWARE_AND_FIRMWARE.md#1-mbps-receive-truncation-diagnostics).
Native controller/loader/PSM tests, Micro Signal regressions, freestanding
ELF closure and whitespace checks pass.

Intermediate `c1-matrix-*` evidence belongs to build `786ad5ee`, including
one stale-PHY-metadata rejection and an interrupted OFDM full-size trial;
it is not pooled with final-build results. This work does not complete C2,
C3's at-least-1,000-per-cell matrix, milestone A timing/soak gates, calibrated
RF waveform/EVM checks or AP interoperability.

Both badges were restored and independently compared after testing:
`08DA26F86E57F5EA` matches its original 33,604-byte application backup
(not a full-flash backup); `9BBF88DD17D66E7D` matches its entire original
16-MiB flash. Both were left in BOOTSEL. The Mac's networking was unchanged.
See [fixture usage and detailed evidence](clean-rx/badge/README.md#dssscck-rate-and-preamble-fixture).

### C2. Basic-rate and mixed-network compatibility

- [ ] Negotiate appropriate supported/basic rates, including a usable
  802.11b-only association path instead of requiring an AP to advertise 6 Mbps.
- [ ] Integrate ACK/control-response selection and applicable b/g protection.
- [ ] Verify connection and bidirectional traffic against a b-only AP and a
  mixed b/g AP, including peers that require long preambles.

### C3. Rate/size qualification matrix

- [ ] Verify each rate in both directions using small frames, Ethernet-sized
  frames and full 2,304-byte MSDUs, with fresh payloads and independent FCS/
  payload checks. Run at least 1,000 frames per matrix cell.
- [ ] Record loss/retries and require zero corruption, zero duplicate delivery
  and no controller/transport faults. Re-run the existing OFDM regression.

## D. 802.11n

### D1. Confirm the implementation target

- [ ] Map the datasheet-supported HT20, single-stream MCS0-MCS7 capabilities to
  actual PHY/MAC controls, framing, receive status and firmware requirements.
- [ ] Document optional capabilities separately. Do not advertise short GI,
  STBC, beamforming or aggregation modes until their exact support is
  established and implemented. Do not assume HT40 or additional spatial streams.

### D2. HT PHY and association

- [ ] Implement HT capability/operation negotiation, HT mixed-mode TX/RX,
  correct lengths/airtime and MCS configuration without breaking legacy peers.
- [ ] Implement the QoS/WMM and HT MAC prerequisites required by the selected
  profile; parsing a QoS header alone is insufficient.
- [ ] Verify association capabilities and actual OTA HT framing against an
  independent HT20 peer, with explicit legacy fallback.

### D3. HT MAC and performance

- [ ] Implement the aggregation, Block ACK negotiation/window management and
  receive reordering required by the supported HT profile. Define and test
  aggregate/MPDU size limits, loss recovery and replay/duplicate interactions.
- [ ] Verify supported MCS rates in both directions at multiple payload sizes,
  with at least 1,000 frames per rate/size cell and zero corruption or
  reordering/duplicate-delivery errors outside the documented interface.
- [ ] Measure sustained payload throughput, loss and CPU/transport limits.
  Distinguish PHY rate, bus rate and application throughput. Include mixed
  legacy/HT traffic and a sustained station soak.

## E. Complete WPA2 operation

Scope: complete WPA2-Personal with CCMP for the supported station profile.
WPA2-Enterprise/EAP and obsolete cipher compatibility are separate projects.

### E1. Robust four-way handshake

- [ ] Harden timers, bounded retransmissions, duplicate handling, replay
  counters and AP compatibility while retaining MIC/ANonce/RSN validation.
- [ ] Support the documented passphrase and raw 256-bit PSK provisioning
  formats without logging secrets or retaining stale provisioning state.
- [ ] Verify wrong credentials, malformed key data, altered MIC/nonce/RSN,
  unexpected transitions and duplicate M3 without key reinstallation.
- [ ] Complete 10 consecutive fresh protected connections against the
  controlled AP, followed by the additional AP configurations from B3.
  Record association failures separately from handshake failures.

### E2. Group traffic and rekey

- [ ] Implement GTK-backed broadcast/multicast decryption, key-index selection
  and replay state, including GTK replacement and permitted overlap.
- [ ] Implement the group-key handshake and pairwise rekey with bounded
  retries, correct key activation and no PN reset under retransmission.
- [ ] Verify real broadcast/multicast traffic and at least three forced group
  rekeys and three pairwise rekeys during traffic, without plaintext leakage,
  duplicate delivery, incorrect key reuse or unrecovered link loss.

### E3. Sustained protected operation

- [ ] Integrate secure key teardown on disconnect, reset, cancellation and
  failure; reject unsupported security modes explicitly.
- [ ] Verify CCMP MIC rejection, replay rejection and PN exhaustion behavior
  for unicast and group traffic, including supported traffic classes.
- [ ] Pass B2's 60-minute soak under WPA2 with rekeys and idle periods.
  Require protected traffic evidence at both endpoints, not just authorization.

## F. Hardware crypto offload

### F1. Establish hardware semantics

- [ ] Document D11 key installation, cipher/header configuration, key slots,
  PN/MIC/FCS handling and RX completion/error indications.
- [ ] Define which layer owns each counter and validation step. Hardware
  offload must not bypass replay checks or expose unauthenticated plaintext.
- [ ] Keep the verified software-CCMP path as an explicit reference/backend
  during qualification; never silently fall back to unencrypted transmission.

### F2. Functional equivalence

- [ ] Integrate hardware TX/RX crypto and key lifecycle with the station,
  including pairwise/group keys and rekey transitions supported by E.
- [ ] Compare hardware ciphertext, tags and decrypted payloads with independent
  known-answer vectors and the verified software path across supported frame
  headers, traffic classes, payload sizes and PN boundaries.
- [ ] Verify bad MICs, replay, wrong keys/key indices, retries, key replacement
  and reset. Require rejection before plaintext delivery and correct PN state.

### F3. OTA and resource qualification

- [ ] Verify encrypted unicast/group exchanges with an independent AP and
  repeat E's rekey tests plus B2's sustained protected soak.
- [ ] Measure RP2350 CPU time, throughput, latency, stack/RAM and firmware-size
  changes against software CCMP under identical conditions.
- [ ] Enable offload by default only after equivalent correctness and recovery
  are demonstrated; document any remaining hardware/backend restrictions.

## Sequencing

1. Establish A's MAC correctness and B1/B2's connection/data foundations.
2. Complete E's software-backed WPA2 lifecycle, then B3's protected station
   qualification. B and E are iterative, not a circular completion dependency.
3. C can proceed alongside that work, but final qualification depends on A.
4. Start D after reliable legacy operation; HT requires additional MAC work,
   not merely a new PHY rate setting.
5. F1 and standalone F2 vectors can proceed independently. Final F2/F3
   integration depends on E and sustained station tests.

For each completed milestone, append the evidence paths, build IDs, measured
results, remaining limitations and verification date here or in the linked
feature documentation before checking its final verification item.
