# Milestone implementation and qualification history

This is the detailed roadmap record preserved on 2026-10-17 before separating
milestone status from implementation details. Build results, findings and
experiment descriptions below are historical evidence, not new qualification.
Commands and artifact paths are relative to the repository root.
The current milestone checklist is [todo.md](../../todo.md).

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
are not yet qualified. See [current status and limitations](../badge/README.md).

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
[implementation, evidence and limitations](../badge/README.md#milestone-a-mac-implementation-experimental-not-complete).
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

**Current implementation (2026-10-23):** mode 33 now provides the bounded
legacy/open Ethernet/ARP/IPv4/UDP path documented in
[the badge README](../badge/README.md#b2-continuous-static-address-networking).
It uses four TX/four RX slots, explicit backpressure/overflow, static
`192.0.2.1`/`.2` addresses and port `30526`. Both endpoints originate
fresh session/epoch/sequence-bound requests, alternating 90-byte and
1514-byte Ethernet packets. Four bounded application attempts preserve
loss, duplicate and abandonment evidence. Five-second idle intervals and
two-second host USB stalls do not stop firmware servicing.

Native ASan/UBSan tests pass a simulated bidirectional hour, including
full-sized requests, idle and independent counts/hashes; additional tests
cover timer wrap, full queues and resumed consumers, checksum/payload
corruption, validation-before-binding, reconnect and stale epochs,
retry exhaustion and FCS-valid wrong-address/LLC/flag MPDUs.
This is not real-time RF qualification.

The protected `build/clean-rx/b2-20261023/hardware/trial02-*` control passed
30 actual seconds on channel 1 against the unchanged vendor-radio AP.
Station build `22f389ff` independently delivered/acknowledged 108 requests
(54 full-sized); AP build `8595958f` delivered/acknowledged 100 (50 full-sized).
All paired counts and payload hashes matched. Loss, corruption, RX overflow
and abandonment were zero; retries were 6/8, duplicate deliveries were zero,
and queue high-water marks were TX 2/1 and RX 1/1. Both experienced one
scheduled idle interval and actual host stalls (station 3, AP 5).
Maximum application round trips were 318,242/473,253 microseconds, including
retries; maximum network service gaps were 29,978/98,183 microseconds.
Both original full flash images were restored, read back and compared.

An earlier small-packet control (`trial01-*`, builds `d3b17bf1`/`72bf7e6d`)
passed 109/104 messages but is not full-sized qualification. Its subsequent
long run (`soak01-*`) was deliberately cancelled after noticing the detailed
acceptance also requires full-sized packets. Both collectors stopped,
requested BOOTSEL and both original images were fully restored/readback-verified.
It is not a failed RF soak or a completed soak and must not be counted as either.
**Real 60-minute result: failed, acceptance remains open.** The retained
`b2-20261023/hardware/soak02-*` run used station `22f389ff` and AP `24957f3d`.
Both raw network reports match their JSON archives. Station generated 13,214,
acknowledged 13,211 and lost 3; AP generated 13,259, acknowledged 13,235,
lost 21 and abandoned 3 work items during epoch changes. Station/AP delivered
13,239/13,211 peer requests; full-sized generated counts were 6606/6629,
with 6604/6616 acknowledged. Sender/receiver application counts/hashes therefore
do not meet the strict delivery oracle. Do not count partial delivery as a pass.

There were zero corrupt packets, RX overflows or TX-full events. Both queues
remained bounded (TX high-water 2, RX 1), all 180 idle intervals occurred,
and actual two-second host stalls numbered 287/500. Application retry counts
were 510/542; maximum acknowledged round trips were 823,257/786,114 us,
and maximum service gaps were 41,835/97,911 us. Station lifecycle elapsed was
3,600,000,033 us; AP network elapsed was 3,601,999,957 us. This was a real
full-duration test, not simulated time or a short control.

The lifecycle journal identifies five **beacon-loss** recoveries, not departure,
channel-change, authentication or association failures. Six associations
produced only five data-verified epochs. The retained late-run journal shows
repeated beacon-loss/reset/reassociation around minutes 54-55. Local MAC
diagnostics retain 1088 TX timeouts and 9848 BUSY returns. No fatal loader/radio
error occurred, and the RP2350 kept servicing the network in under 42 ms.
Thus host queue growth or two-second USB collector stalls did not cause a
host-service lockup. The archives do not establish whether the beacon gaps
were physical RF outages or missed controller receptions: do not label them
false beacon losses without additional evidence.

Application continuity across B1 cold reloads also remains unqualified:
reloads take more than 2.5 seconds, while the current four-attempt application
window is about one second, and changing epochs explicitly accounts for
abandoned work rather than silently accepting old replies. Next instrument
beacon gaps against fresh validated application RX and preserve pending
application ownership across recovery before rerunning the strict hour.
Raising thresholds or dropping loss/abandonment checks is not a qualification fix.

After every campaign both original 16-MiB images were restored, fully read
back and compared. Final SHA-256 values remain
`02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`
(station `08DA26F86E57F5EA`) and
`2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3`
(AP `9BBF88DD17D66E7D`). Both returned to BOOTSEL; the Pico was untouched.
Default, WMM/HT-response and HT/SHM builds/native regressions passed.

- [x] Provide an actual Ethernet/MSDU send/receive interface with bounded
  queues, backpressure, meaningful errors and continuous USB/host servicing.
- [x] Exercise it with a small ARP/IP/UDP test path on the RP2350, using DHCP
  or explicitly documented static addressing. An IP stack is a host-software
  task, not a prerequisite for raw MAC operation.
- [ ] Complete a 60-minute bidirectional traffic soak, including idle periods,
  full-sized frames and host stalls. Require no crash, watchdog reset,
  memory corruption, stuck credit, unrecovered disconnect or unbounded queue.
  Report delivery, loss, retries, latency and memory/stack bounds.

### B3. Reproducible station qualification

**2026-10-23 implementation and open-configuration qualification:** the
Pico 2 W (`6572BE3E4636AF51`) now runs the unchanged vendor radio firmware
as an independent AP. Mode 5 configures channel 1/nmode 0, channel 6/nmode 0
and channel 11/nmode 1, all open. A callback adapter sets nmode while DOWN
and replaces the shared AP helper's channel-1 command without changing shared
or vendor code. Hardware/target channel, nmode and wsec are queried before
readiness. Native tests exercise the actual shared AP configuration through
that adapter, including mode-setting failures and wrong/short readbacks.

The Pico variant (`HT_PEER_PICO=1`) initializes reference and 120-MHz SYS
clocks before USB, reusing the existing Pico clock helper unchanged. The
first generic badge-peer attempt (`control01-*`) did not enumerate on the
Pico; this is not an RF failure. After the clock fix, channel 1 and 11 controls
passed (`control02-*`), but a between-trial BOOTSEL re-enumeration race skipped
channel 6. Bounded, logged reboot retries fixed that host-runner race.

The first three-round campaign (`qualified01-*`) completed five verified
sessions before all three USB boards disappeared, including the untouched
second badge. Restoration initially failed because the hub was unavailable.
After the user reconnected it, both original images were restored, fully
read back and compared before any further flashing. Preserve the interrupted
matrix and the initial restoration failures; it is not a qualified campaign.

The fresh `build/clean-rx/b3-20261023/hardware/qualified02-*` campaign passed
all nine cold connections: channel-1 echoes 14/18/15, channel-6 echoes 20/20/20
and channel-11 echoes 18/18/19, **162 total**. Clean badge build `80e8c0bd`
ran on `08DA26F86E57F5EA`; Pico AP build `eebb2313` independently confirmed
the same station, fresh session/epoch and application data in each trial.
Readback-verified configurations and both raw endpoint archives passed the
paired oracle. Authentication/association failures and disconnects were zero;
six MAC timeouts, four echo timeouts and seven BUSY returns were retained.

The matrix reports `open_configurations_qualified:true`,
`ap_implementations:1`, **`b3_qualified:false`**. These are three configurations
of one AP implementation; do not claim three AP stacks, sustained WPA2,
separate mesh-node coverage, negotiated HT, roaming or B2 soak qualification.
Existing bounded WPA2 controls do not replace E's persistence/rekey acceptance.
See [commands and boundaries](../badge/README.md#b3-reproducible-ap-configuration-qualification).

Final full readbacks matched the original SHA-256 values:
badge `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`,
Pico `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486`.
Both were restored and returned to BOOTSEL; the second badge was not flashed.

- [x] Repeat connection/data tests against the controlled vendor AP and at
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

- Shared [DSSS/CCK PHY helpers](../src/phy_rates.h) select all four
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
- [Native tests](../badge/loader_test.c) exhaust MPDU lengths 14..2400
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
[diagnostic layout](HARDWARE_DEVELOPMENT.md#1-mbps-receive-truncation-diagnostics).
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
See [fixture usage and detailed evidence](../badge/README.md#dssscck-rate-and-preamble-fixture).

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

- [x] Map the datasheet-supported HT20, single-stream MCS0-MCS7 capabilities to
  actual PHY/MAC controls, framing, receive status and firmware requirements.
- [x] Document optional capabilities separately. Do not advertise short GI,
  STBC, beamforming or aggregation modes until their exact support is
  established and implemented. Do not assume HT40 or additional spatial streams.

**D1 complete for the conservative control/framing target, not for a
general HT station or reliability qualification.** See the
[HT20 implementation target](HT20_IMPLEMENTATION.md).
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

- [ ] Implement HT capability/operation negotiation, HT mixed-mode TX/RX,
  correct lengths/airtime and MCS configuration without breaking legacy peers.
- [ ] Implement the QoS/WMM and HT MAC prerequisites required by the selected
  profile; parsing a QoS header alone is insufficient.
- [ ] Verify association capabilities and actual OTA HT framing against an
  independent HT20 peer, with explicit legacy fallback.

**D2 in progress; not an enabled HT station.** The following implementation
steps are complete, without closing the broader requirements above:

- [x] Add a separate, default-disabled, versioned HT20 payload API with
  explicit MCS/profile/length validation and the verified mixed-mode TX
  controls. Version 1 sends one non-QoS broadcast MPDU, 28..2,332 bytes,
  without ACK/retries or a station profile. Enforce the 2,304-byte MSDU
  limit independently of the larger raw storage capacity.
- [x] Add CRC-checked HT20 RX metadata without changing legacy metadata or
  MPDU offsets; preserve profile/MCS/channel through split F2 delivery.
  Retain completed captures across host/SDIO backpressure in the opt-in
  build rather than release an unposted frame.
- [x] Parse HT capability/operation and WMM AP EDCA parameters, validate
  basic MCS/channel/parameter consistency, and report explicit legacy
  fallback reasons. Preserve byte-identical legacy association requests
  for open/WPA2 peers, even after optional-element errors or WMM changes.
- [x] Preserve saved association-response evidence when publishing the HT
  peer report: compact version-2 report at offset 1040, with an integrated
  association-evidence regression.
- [x] Add a separately gated 3,896-byte MPDU receive layout, on-chip upper-SHM
  capacity/alias checks, relocated DMA buffers, three-fragment F2 transport
  and a checked host/controller layout manifest. Keep the default layout.
- [x] Implement full 3,839-byte plaintext A-MSDU validation and zero-copy
  deaggregation, rejecting malformed/truncated aggregates before exposing
  any subframe. Verify a fixed two-MSDU aggregate over the air through the
  extended receive path; retain the independent-peer/negotiation gate.
- [x] Implement native-verified per-TID QoS duplicate/fragment state and
  integrate authenticated QoS reassembly with the existing CCMP receive path.
  Preserve per-TID replay counters, require consecutive fragment PNs, reject
  protected A-MSDUs and unsupported ACK policies, and wipe expired plaintext.
- [x] Add a separately gated QoS/HT PSM normal-ACK response prototype,
  HT-to-legacy basic-rate tables, safe deferred policy updates and a fixed
  one-frame normal-ACK diagnostic. Native and on-chip response-start tests
  pass; these counters alone do not qualify returned ACKs or SIFS timing.
- [x] Add an isolated, clocked HT normal-ACK fixture using live PSM/RX,
  automatic PHY turnaround and the existing DCF/ACK-wait path. Save the
  actual returned ACK, FCS and legacy PLCP before internal consumption;
  require independent address/FCS/rate validation for paired-test success.
  This is bounded diagnostic evidence, not general HT station qualification.
- [x] Replace the failing DCF slot operand/decrement sequence with a staged
  register transition and a separated SHM-load/branch consumer. Add bounded
  counter checks, explicit controller faults and frozen transition evidence.
  Verify all 48 launches in three bounded MCS/direction sweeps; this does
  not close shared-medium access or HT receive-reliability qualification.
- [ ] Implement actual EDCA/QoS access and HT protection/response behavior,
  meet receive-size/required HT MAC obligations jointly with D3, then wire
  association negotiation and independent-peer verification.

Opcode 12 and its eight-byte versioned descriptor are documented in the
[D2 implementation/API notes](HT20_IMPLEMENTATION.md#d2-implementation-bounded-packet-api-and-peer-profiling).
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
successful exchanges. See the [detailed investigation](HT20_IMPLEMENTATION.md#further-investigation-stable-corruption-rx-aborts-and-idle-watchdog)
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

- [x] Harden timers, bounded retransmissions, duplicate handling, replay
  counters and AP compatibility while retaining MIC/ANonce/RSN validation.
- [x] Support the documented passphrase and raw 256-bit PSK provisioning
  formats without logging secrets or retaining stale provisioning state.
- [x] Verify wrong credentials, malformed key data, altered MIC/nonce/RSN,
  unexpected transitions and duplicate M3 without key reinstallation.
- [x] Complete 10 consecutive fresh protected connections against the
  controlled AP, followed by the additional AP configurations from B3.
  Record association failures separately from handshake failures.

#### E1 implementation and qualification, 2026-10-23

The portable supplicant now owns an eight-second absolute deadline and four
500-ms local retransmissions per M2/M4 phase. Polling continues even while a
controller submission is pending. Duplicate input cannot extend either budget.
M1 retransmissions may advance replay with the same ANonce; M3 retransmissions
must authenticate and preserve normalized key contents even when their replay
counter advances. Neither reinstalls PTK/GTK nor resets TX/RX packet numbers.
Zero or changed nonces, unsupported/malformed pinned RSN, stale replay, changed
keys, malformed key data and wrong MICs fail explicitly. Timeout/transport/
departure failures wipe software keys and fragment plaintext.

Passphrase and case-insensitive raw 64-hex-digit PSK inputs use the same owned,
private, two-line file reader. Invalid inputs wipe the PMK output. A fresh
OS-random SNonce and request/BSSID/channel binding retain one-use provisioning.
The exclusive mode-600 `--derive-psk` command lets the actual hardware runner
alternate both provisioning paths without logging secret values.

Native ASan/UBSan tests cover independent vectors, min/max lengths, every bad
raw-hex position, timer wrap, absent M1, lost M3/M4, duplicate retry budgets,
equal/advanced/stale counters, MIC/nonce/RSN/GTK rejection, and unchanged keys/
PNs under accepted M3 retransmissions. Host tests invoke the real collector
for private-file formats, malformed/NUL input, permissions and exclusive output.
The archive oracle checks independent AP/station raw counters and identities;
its matrix tests reject missing, reused and wrong-target sessions.

The Pico vendor peer adds opt-in protected mode 6 without changing shared
vendor/platform sources. It reads back channel, nmode, CCMP and WPA2-PSK auth,
provisions a raw PMK rather than a public-nonce-derived password, and echoes
only an exact, authorized, addressed 38-byte application challenge.

Initial hardware campaign `build/clean-rx/e1-20261023/hardware/matrix01-*`
rejected all 13 initial fixture trials before AP readiness. The new AP consumed
its secret too late: not-ready USB report polling intentionally stalls, and
the shared USB stall path wipes unconsumed provisioning. This was not an
over-the-air handshake failure. Consumption now occurs immediately on taking
the submitted request, before clock calibration or firmware loading can poll
USB. A native test exercises the actual provisioning/submit/not-ready-stall
sequence and proves early consumption, one-use binding and invalid-input
wiping. AP operation codes localize subsequent configuration/data failures.
Both borrowed originals were fully restored, read back and compared after
that failed campaign.

The qualification matrix separately requires ten controlled channel-1
connections, then three channel-6 and three channel-11 connections, alternating
passphrase/raw PSK. Three additional wrong-password controls must never
authorize or exchange protected traffic. One failed row fails the campaign;
retained successful rows are not spliced across campaigns.

Campaign `matrix02-*` then reached authentication, association and M2 in all
16 positive trials, but every M3 failed the old GTK parser's reserved-byte
check (`gtk_kde` examples `f60218`, `fe0218`, `2d0218`). The independently
authenticated/unwrapped KDE had the correct 24-byte length and key-ID byte
`02`; only its reserved byte was nonzero. The public
[hostap reference receiver](https://w1.fi/cgit/hostap/plain/src/rsn_supp/wpa.c)
(`wpa_supplicant_pairwise_gtk`) masks the key ID and skips the reserved byte
rather than rejecting it. The parser now ignores reserved bits/bytes only
after MIC and wrap-integrity verification, preserving length/uniqueness,
pinned RSN and GTK checks. Duplicate M3 is reauthenticated/unwrapped and
compared against the installed GTK/key ID, not a ciphertext fingerprint, so
changing ignored reserved fields cannot cause a false reinstall or failure.
Native tests reproduce byte `f6`, all reserved flag bits, and authenticated
changed GTK/key-ID rejection with unchanged PNs on accepted retransmissions.
All three wrong-password controls associated and submitted M2, then rejected
a new ANonce without authorizing or exchanging protected data. The oracle
accepts that explicit terminal rejection, not only timeout/MIC/departure.
Both originals were again fully restored/read back/compared after `matrix02`.

Campaign `build/clean-rx/e1-20261023/hardware/matrix03-*` passed all 19 trials:
ten consecutive controlled channel-1 protected connections, followed by three
channel-6 and three channel-11 protected connections, then one wrong-password
control per profile. All 16 positives independently verified their protected
echo and AP authorization; eight used passphrases and eight raw PSKs.
All three negatives associated/submitted M2 but installed no key, sent no M4,
and exchanged no protected data at either endpoint. The matrix retained five
timer-generated retry events (not necessarily all submitted on air).
No association/handshake/transport failure occurred in a positive trial.
Station build `5265041c` and Pico AP `93bf26c0` were consistent throughout.
The exact AP image and matching collector were preserved in
`build/clean-rx/e1-20261023/qualified03-peer/`.

The strengthened oracle re-read every raw pair and reproduced the successful
matrix byte-for-byte. Native negative tests reject missing/reused sessions,
wrong devices, changed builds, mismatched raw counters/configuration and
invalid private credential files, including exact maximum-length files.
The AP collector's no-report timeout now explicitly fails without writing
uninitialized archive buffers; this is a host error-path guard, not a radio
or handshake change.

Both borrowed full originals were restored and compared after `matrix03`:
badge `08DA26F86E57F5EA`, 16 MiB, SHA-256
`02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`;
Pico `6572BE3E4636AF51`, 4 MiB, SHA-256
`f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486`.
The other badge was not modified. E1 is qualified for these bounded legacy
connections; the E2/E3/B3 mesh/persistence and A/B2 reliability gates remain open.

The final host-only missing-report guard changed the aggregate AP build ID to
`e08283ff`, without changing radio/handshake behavior. The additional
`hardware/control04-*` trial passed with station `5265041c`: one M2/M3/M4,
one independently verified protected echo, no timer-generated retries.
Its final full 16 MiB/4 MiB readbacks matched the same original hashes above;
both devices were restored again after this last control.

These are bounded legacy-client connections against one vendor AP
implementation. They do not close B3's mesh/persistence requirements, E2 rekey/
group traffic, E3/B2 hour-long operation, or negotiated HT qualification.

### E2. Group traffic and rekey

- [x] Implement GTK-backed broadcast/multicast decryption, key-index selection
  and replay state, including GTK replacement and permitted overlap.
- [x] Implement the group-key handshake and pairwise rekey with bounded
  retries, correct key activation and no PN reset under retransmission.
- [x] Verify real broadcast/multicast traffic and at least three forced group
  rekeys and three pairwise rekeys during traffic, without plaintext leakage,
  duplicate delivery, incorrect key reuse or unrecovered link loss.

**2026-10-23 implementation:** four GTK slots select CCMP keys by the on-air
KeyID, maintain independent replay state for 16 QoS TIDs plus non-QoS, initialize
and monotonically advance RSC floors, and allow a two-second previous-slot
overlap. Same-key reinstallations preserve PNs; aliasing a live GTK into a
different slot is rejected. Expiry and terminal failure wipe retired keys.
Authenticated group traffic is decrypted separately from pairwise fragment/
reorder state; plaintext data is rejected and counted.

The RSN group-key exchange validates MIC, AES-wrap integrity, pinned RSN when
present, unique GTK KDE, RSC, replay and duplicate identity. G2 uses a protected
pairwise envelope. The authenticator owns G1 retransmissions; duplicate
responses have the existing eight-second/four-retry bounds, rather than an
invented third handshake message or an idle-link timeout.

Encrypted pairwise M1 starts a fresh SNonce/PTK epoch while old-key traffic can
continue. A secret HMAC-based per-session nonce sequence is seeded from the PMK
and initial OS-random SNonce, advances on fresh M1 only, and rejects wrap.
The PMK therefore remains private in RAM until explicit teardown/failure.
M2 and M4 use the new KCK but old data-key envelopes; accepted M3 commits the
new TK and its fresh PN epoch exactly once. The previous PTK/PN state remains
available only for bounded M3/M4 loss recovery until new-key data proves
completion, then is wiped. Same-key GTK PN state survives pairwise rekey.
Concurrent group/pairwise key exchanges are explicitly rejected; this is not
WPA3, enterprise, PMF, or HT qualification.

Native ASan/UBSan tests cover three group replacements, broadcast/multicast on
all 17 replay lanes, RSC carry/selection, overlap expiry, encrypted G1/G2,
same-key PN preservation, and three encrypted four-way rekeys during traffic.
They exercise lost M4 with old-key encrypted M3 retransmission, fresh nonces,
old/new MIC and envelope ownership, duplicate PN preservation, deadline wrap,
malformed/MIC/replay rejection and key/plaintext wiping. These are NOT RF.

Station mode 34 and AP mode 7 add a separate bounded protected-traffic fixture.
Mode 32 is already B1 lifecycle; the first `traffic01` attempt exposed that
mode collision and was rejected, not counted as security qualification.
A compile-time mode guard and a negative oracle fixture now prevent its reuse.
The AP final snapshot uses the same atomic report/trace publication as E1.
The E2 verifier binds raw identities, security readbacks, challenge tokens,
counts, duration and controller exits. Full E2 acceptance requires at least
three group plus three pairwise rekeys, each with its own subsequent protected
traffic proof; `--traffic-only` explicitly leaves the milestone unqualified.

**Hardware investigation and result:** `traffic02` independently received
26 protected unicast echoes, two broadcasts and three multicasts before the
old six-second whole-loader budget expired. E2 now explicitly sets a bounded
55-second budget, with native tests crossing six/35 seconds and checking its
exact expiration. `traffic03` then completed 35.000066 seconds with 346 echoes
and 70 group receptions, but correctly failed the host acceptance gate:
the shared association-evidence predicate still allowed only modes 16/18.
The protected-mode predicate now also covers mode 34; native tests assert
that 34 passes valid captured association evidence while B1 mode 32 does not.
The failed runs remain retained, not reclassified or spliced into a pass.

Campaign `build/clean-rx/e2-20261023/hardware/traffic04-*` passed the complete
35-second traffic-only control with station build `7904df1d` and Pico AP
`b91f9a24`: 348 independently echoed protected unicast challenges, 17 broadcast
and 52 multicast receptions. The AP submitted 68 broadcast and 68 multicast
challenges; this is group-delivery evidence, not a lossless-link claim.
Both endpoints retained authorization, no WPA or transport error occurred,
and plaintext data was not accepted. The separate `traffic04-e1-*` connection
also passed M1/M2/M3/M4 and a fresh protected echo with these same builds.

Both AP interval probes actually returned `BCME_UNSUPPORTED` (-23).
No group or pairwise RF rekey occurred. Running the full E2 oracle without
`--traffic-only` was explicitly verified to reject this archive because
GTK rekey control is unavailable. At that point E2's final RF checkbox remained
open: three forced rotations of each kind required a controllable authenticator.
The native rotations above are not substituted for this hardware requirement.

Full 16 MiB badge and 4 MiB Pico originals were restored/readback-compared
after every campaign, including the last `traffic04`/E1 control. Their SHA-256
hashes match the originals recorded under E1 above. The final campaigns
restore only the preflight-bounded flashed prefix, then read and compare the
entire flash; a full-image restore is retained as an explicit failure fallback.
The other badge was untouched. No commit or push was performed.

**2026-10-23 forced-rekey follow-up:** AP mode 8 now provides an explicit,
host-owned single-station authenticator rather than relying on the unsupported
vendor interval variables. Association/CCMP remain in the vendor AP, but its
PMK submission is deliberately suppressed for this profile. A private
OS-random seed drives fresh ANonces and GTKs; the RP2350 validates response
MICs/replay and installs GTK/PTK once after G2/M4. Port authorization follows
the first validated M4. Cached EAPOL retries use bounded budgets without
regenerating nonces or reinstalling keys. Terminal failure/teardown wipes
private state. The wire key layout follows the documented Broadcom
`wl_wsec_key` structure; no proprietary forcing API or firmware patch is used.

Initial hardware probes found `wpaie` GET unsupported and AP BSS-info without
RSN IEs. Those probes were removed, not replaced with a success-shaped
fallback. The explicit PSK-CCMP fixture uses the bundled firmware's observed
22-byte RSN (`0x000c` capabilities); M3's initial mismatch correctly failed
the supplicant's pinned-RSN check. The station now archives its public
observed RSN separately, and the full oracle requires that exact profile.
Unicast/group rate readbacks retain the vendor `0xc0000000` override flags
while verifying a 1 Mbps rate, following the existing rate-mask convention.

Group-first hardware attempts (`rekey06`, `rekey08`) completed one GTK
rotation but timed out before a pairwise M2 amid substantial receive/FCS
loss. A PTK-first alternating fixture passed `rekey09`. That diagnostic
pass predates the final stronger oracle: it is not substituted for the
final runs. The cause of the group-first RF failure is not established,
and that ordering remains outside the qualified RF scope. Native tests
exercise group-first rotation, old-key encrypted M3 retry after lost M4,
lost G2, per-epoch data proof, single installation and backend/MIC/timeout
failure wiping using the actual station state machine.

The final oracle additionally binds AP M2/M4/G2 and installed-epoch counters,
group challenge submission after each new GTK, protected station reception
after each new PTK, authenticator/controller status and pinned-rate readbacks.
Both endpoints must independently prove progress; plaintext application-frame
observations fail full acceptance. `--rekey` selects this mode and the full
gate; `--group-traffic` stays explicitly non-qualifying.

Final station `3b5748a1` and Pico AP `b40abc4f` passed all three retained
35-second RF runs under `build/clean-rx/e2-finish/hardware/`:

| Run | Channel | Unicast echoes | Broadcast RX | Multicast RX | Group / pairwise rekeys |
| --- | ---: | ---: | ---: | ---: | ---: |
| `rekey10` | 1 | 338 | 67 | 2 | 3 / 3 |
| `rekey11` | 6 | 339 | 68 | 10 | 3 / 3 |
| `rekey12` | 11 | 338 | 68 | 10 | 3 / 3 |

Every installed station epoch has subsequent protected traffic proof. The AP
independently confirms nine G2s, twelve M2/M4 pairs including the three initial
connections, nine post-GTK group probes and nine post-PTK protected receptions.
It receives/echoes the same 1,015 unicast challenges and submits 203 frames of
each group class; only 22 multicast receptions occur, so no lossless-link
claim is made. Zero plaintext application observations and zero terminal
WPA/transport errors occur. `rekey11-e1` passes a separate fresh E1 RF
regression with these exact builds.

Full 16 MiB badge and 4 MiB Pico backups/readbacks compare identically after
every follow-up RF campaign. An initial restoration-helper CLI ordering error
was corrected immediately and the originals then fully verified before any
further test. The other badge remains untouched. Station, Pico AP and default
badge AP builds/native tests all pass with ASan/UBSan. E2's bounded legacy RF
checkbox is now complete; this does not close E3, HT/BA, multi-station,
arbitrary-order/authenticator, persistent-link or hour-soak qualification.

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
