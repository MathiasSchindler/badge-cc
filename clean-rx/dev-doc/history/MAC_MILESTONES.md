# Ordinary MAC milestone history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

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
[implementation, evidence and limitations](../../badge/README.md#milestone-a-mac-implementation-experimental-not-complete).
Neither native 100,000-frame simulations, the bounded connection runner nor
the shorter two-clean-badge soak satisfies the OTA acceptance test below.

### Implemented and regression-tested progress

These completed subitems do **not** close the A1-A3 acceptance gates:

- Recorded completed criterion: Consume valid local ACKs directly in the PSM, without DMA publication
  or host-credit dependence; test out-of-window ACKs, counter wrap and
  100,000 simulated immediate RX rearms.
- Recorded completed criterion: Correct all six RA/TA checks that used two SHM input operands in one
  instruction, an invalid D11 encoding. Add an ISA-wide operand-limit test
  and flip all 96 RA/TA bits in ACK/CTS rejection tests. The new ISA test
  failed before the fix. Older local ACK counters are not proof of correct
  peer filtering; independently confirmed protected echoes remain valid.
- Recorded completed criterion: Separate a MAC retry from a new management request after an ACKed
  request. Preserve no-ACK retry identity, protected retry payload/PN and
  zero-attempt BUSY state; use fresh sequences for new requests. Keep the
  four-submission/1.6-second management-phase limits unchanged.
- Recorded completed criterion: Prevent matching-BSSID non-beacons from replacing a valid discovery
  result with a beacon-parser error; test probe/data/auth/association frames
  and truncation. This fixes a demonstrated code defect, not a traced
  diagnosis of the earlier pre-station failure.
- Recorded completed criterion: Publish response-class, bound transport-publication and invalid-FCS
  diagnostics, with explicit startup reset and layout guards.
- Recorded completed criterion: Add `clean-rx/badge/mac_trials.sh`: ten protected trials, two
  wrong-password controls and three open controls, retaining failures and
  requiring independent matching endpoint evidence. It does not flash or
  change the host network and is not a continuous traffic-soak fixture.
- Recorded completed criterion: Add experimental PSM-clocked profile 3, with physical/virtual carrier,
  explicit 28/50-us DIFS, 9/20-us slots, frozen residual backoff, bad-FCS and
  bad-length EIFS, receive priority and a PSM-owned final template trigger.
  Exercise all 4,096 lower IFS status combinations and timing/wrap boundaries
  natively. Hardware contention timing is not independently qualified.
- Recorded completed criterion: Use the same TSF clock domain for PSM deadlines and Cortex comparisons.
  Distinguish LP-PHY OFDM using PHY status rather than the G-PHY-only flag;
  reject unsupported HT response formats. Prepare complete response templates
  before arming/triggering TX, and hold TXE ownership until the response tail
  completes, preventing Cortex template preparation from interrupting it.
- Recorded completed criterion: Add a continuous two-clean-badge raw-unicast fixture with fresh challenges,
  exact payload/FCS validation, bounded retries, explicit failures, independent
  endpoint counts and USB/watchdog servicing. Verify 5,000 frames each way,
  including sequence wrap and repeated watchdog renewals; report 169/64 ACK
  timeouts and 1/1 suppressed MAC duplicates, not application losses.
- Recorded completed criterion: Replace the fixture's 32-bit elapsed ceiling with 64-bit accumulated
  microseconds and a two-hour radio deadline; keep ordinary diagnostics'
  six-second default. Add exact-boundary, two-wrap and cancellation tests,
  52-byte `MSK2` status, matching host/loader margins and 250-ms soak polling.
  Multi-wrap correctness is natively tested, not a completed two-hour RF run.
- Recorded completed criterion: Add profile-3-only bounded suppression of 0..16 otherwise eligible ACKs,
  with explicit invalid-input errors and actual-drop diagnostics. Test all
  12 rates, pinned-address rejection, exhaustion/resumption, no TX trigger
  while dropping and unaffected CTS. Verify missing-ACK recovery and explicit
  eight-attempt exhaustion on both badges; ordinary station paths stay unchanged.
- Recorded completed criterion: Fix delivered-frame duplicate identities expiring after 512 ms during
  longer retry exchanges. The regression failed before the fix; retain
  delivered identities until history replacement/fresh non-Retry reuse,
  while separately expiring/restarting incomplete assemblies. Test timer wrap,
  sequence reuse, completed-cache preservation and existing 100,000 native
  deliveries. This does not establish RF qualification of late ACKs.
- Recorded completed criterion: Pass controller/badge native tests, configured ASan/UBSan regressions,
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

- Recorded open criterion: Implement and document physical carrier sensing, virtual carrier sensing
  through Duration/NAV, applicable interframe spacing, random contention
  backoff, backoff freezing/resumption, and contention-window updates.
- Recorded open criterion: Test idle, busy and overlapping traffic, including NAV expiry and retry
  exhaustion. Use deterministic native tests for scheduling arithmetic.
- Recorded open criterion: Verify channel-access behavior with independent, sufficiently precise
  captures under controlled contention; do not infer RF timing from CPU delays.

### A2. Reliable ACKs and retransmissions

- Recorded open criterion: Complete correct response-rate/preamble selection, RX-to-TX turnaround,
  ACK timeout handling, bounded retries and retry-status reporting.
- Recorded open criterion: Preserve sequence/fragment identity across retransmissions. For protected
  retries, preserve the encrypted payload and PN; update Retry/FCS correctly.
- Recorded open criterion: Verify ACK RF/SIFS timing with an independently validated timing source.
  Exercise missing ACKs, delayed responses, unrelated RX and occupied host
  credits. No captured or acknowledged frame may be silently discarded.

### A3. Receive correctness and interoperability

- Recorded open criterion: Implement duplicate suppression, sequence-number wrap handling, supported
  fragmentation/reassembly, and applicable RTS/CTS and protection handling.
- Recorded open criterion: Test malformed/truncated frames, bad FCS, duplicates, fragments,
  saturation and DMA/credit boundary conditions without hangs or corruption.
- Recorded open criterion: Verify at least 100,000 bidirectional unicast application frames per
  direction across small, Ethernet-sized and maximum supported payloads.
  Count loss, retries and duplicates explicitly; require zero corruption,
  zero duplicate delivery and zero unexplained transport drops. Report RF
  loss separately and compare it with a vendor control under the same setup.


