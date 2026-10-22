# MAC completion ownership and explicit timeout recovery

Date: 2026-10-22. Follow-up to
[bounded fragmentation qualification](LEGACY_FRAGMENTATION_QUALIFICATION.md).
This advances A1/A2/A3; it does not complete their independent RF gates.

## Observed failure and investigation

The earlier `0x82`, substage 5, `0x1800150e`, `0x00c1002f` failure means the
controller observed MAC-not-busy and PHY-idle but not TXE's `0x0400` done
condition. Previously this terminated the controller, and the other endpoint
subsequently timed out. It is not evidence of payload corruption.

The controller polls status while the PSM handles RX and automatic responses.
A native regression reproduced the lack of a persistent Data-completion
handoff across response status reuse. An initial fix latched completion, but
long RF trials still failed. Those failures are retained, not attributed to
the original hypothesis:

- Ownership remained fired, PHY-start-pending was clear, and there was no
  automatic-response overwrite recorded for the failed submission.
- Separate sticky conditions, sampled before RX priority handling and in the
  ACK path, still retained only `5` (not busy / PHY idle), without MAC-done.
- The physical reason for the absent TXE done event remains **unknown**.
  The new recovery must not be described as fixing that silicon/event cause.

A separate real code defect existed at access cancellation: after pausing
PSM, the ARM cleared the request even if it had already fired during the
cancellation race, then returned it as a submitted transmission. Fired
ownership is now retained; only unfired requests are cancelled.

## Implemented behavior

1. A per-submission PSM completion word is reset immediately before the Data
   trigger. It retains MAC-not-busy, MAC-done and PHY-idle observations as
   separate bits. Sampling requires fired ownership, observed PHY start and
   no automatic response owning the engine. RX gets priority after sampling;
   response status cannot masquerade as Data completion. The follow-up below
   fixes a native-reproduced hole in that guarantee after response retirement.
2. Profile-3 completion requires all three retained conditions. Latching does
   not manufacture a missing done event or turn an ACK into local completion.
3. A narrowly classified incomplete attempt can return **TIMEOUT**, not ACKED
   or BUSY, when only MAC-done is missing: the Data request fired, PHY start
   was observed, MAC is not busy, PHY is no longer transmitting and there is
   no PHY error. Either no response owns TX and raw MAC-done is absent, or a
   completed response owns the raw MAC-done status. The latter is safe to
   clean up but is **not** evidence of successful Data completion. Active,
   incomplete or unknown response ownership remains fatal. The existing
   bounded drain still runs.
4. PSM cleanup request `2` is an explicit failed-attempt abort. PSM rechecks
   RX-event priority, active RX, MAC busy, PHY activity, fired ownership,
   PHY-start state and response ownership before resetting template/crypto
   control registers. It does not clear queued RX or capture bytes. The
   ordinary cleanup request `1` still requires MAC-done.
5. That attempt is charged once to the caller's actual attempt budget,
   increments MAC timeout/CW backoff and is returned as `CLEAN_UNICAST_TIMEOUT`
   even if an ACK was observed. The caller retains its existing eight-attempt
   bound and retries the same sequence/fragment/body. No new protected PN is
   allocated on a retry. Ordinary station/WPA retry regressions remain intact.
   New TX defers while PSM cleanup is pending.

The response engine, profile-3 carrier/NAV/DIFS/slot behavior and RX staging
remain active. There is no new SIFS fragment-burst scheduler, PHY timeout
extension, channel/rate workaround, reduced payload or relaxed integrity gate.

## Diagnostics and layout

The existing 180-byte TX diagnostic/mailbox layout and 64-byte `MSK3` fixture
record are unchanged. A separate 28-byte `MTC1` report at controller address
`0x204f0` is copied to detail offset 492 for station and soak modes. It fits
after the existing TX diagnostic and ends exactly before `MSK3` at 520.
Compile-time assertions protect those boundaries and the controller gap
before observer diagnostics.

Fields are seven little-endian 32-bit words:

| Word | Meaning / JSON field |
| --- | --- |
| 0 | `0x3143544d`; `mac_tx_completion_report` validates magic |
| 1 | Last submission's retained bits; `mac_tx_completion_latched` |
| 2 | Cumulative complete submissions whose MAC-done was latched but missed by ARM polling; `mac_tx_completion_preserved` |
| 3 | ARM's raw completion observations; `mac_tx_completion_raw` |
| 4 | Sampled request ownership; `mac_tx_completion_request` |
| 5 | Pipeline snapshot; `mac_tx_completion_pipeline` |
| 6 | Cumulative explicit incomplete-completion TIMEOUTs; `mac_tx_completion_timeouts` |

Completion bits are 1 = MAC not busy, 2 = MAC done, 4 = PHY idle,
8 = sampling closed before automatic-response reuse. Success requires
`(bits & 7) == 7`; closed incomplete evidence remains incomplete.
Pipeline bits 0..15 retain TXE status immediately before an automatic response
while fired ownership exists; bit 16 is PHY-start-pending, bit 17 records an
ARM PHY-start handoff, bits 18..19 are
response ownership, bits 20..21 are the cleanup request (0 = none, 1 = normal,
2 = failed-attempt abort), bit 22 records an ARM owned-completion handoff,
and bits 24..31 are PSM state.
These are diagnostic observations, not an RF waveform/timing capture.
Counters reset with station MAC configuration.

## Native tests

Both controller and badge `all test` passed, including ASan/UBSan MAC/PSM,
loader/credit, retry/protected-frame, crypto and shell syntax regressions.
The controller also passed `all test` in a separate opt-in build with
`CLEAN_RX_HT_RX=1`, `CLEAN_RX_HT_RESPONSE=1`, `CLEAN_RX_EDCA=1` and
`CLEAN_RX_WMM=1`; those feature gates were not installed on the RF badges.
New tests cover:

- all 65,536 request-word values at cancellation, preserving only fired state;
- completion across RX priority, asynchronous MAC-done/PHY-idle observations,
  response reuse and a fresh submission clearing previous completion;
- busy MAC, missing done, active PHY, not-yet-started PHY, response owner and
  unfired/stale-owner controls;
- exact incomplete-attempt classification, including the long-soak
  completed-response state, refusal to borrow response completion as Data
  success, and rejection of active/incomplete/unknown response ownership;
- normal cleanup still rejecting missing done;
- explicit abort cleanup deferring for each RX/TX/ownership hazard, retaining
  a held capture and its bytes, without PHY-register writes.

Existing native DCF tests exercise NAV/EIFS expiry, wrap, idle/busy overlap,
slot freezing, CW reset/limits and Retry identity. These are not controlled
RF contention or independent SIFS measurements.

## Builds and RF evidence

Local evidence: `build/clean-rx/mac-reliability-20261022/`.
Every prefix retains config/challenge, endpoint JSON/detail binaries/stderr,
BOOTSEL logs and a run log. Intermediate UF2s are retained by build ID.
Both endpoints are separate badges:
A = `08DA26F86E57F5EA`, B = `9BBF88DD17D66E7D`.
All trials use 6 Mbps OFDM, channel 1. Counts below are initiator/responder.

| Prefix | Build | Initiator | Target / profile / ACK suppression per endpoint | ACKed MSDUs | Received MSDUs | Outcome |
| --- | --- | --- | --- | --- | --- | --- |
| `frag16-100-drop2` | `ca8a9427` | B | 100 / frag16 / 2 | 100 / 100 | 100 / 100 | Pass, simultaneous-condition latch only |
| `frag16-1000-drop2` | `ca8a9427` | A | 1000 / frag16 / 2 | 248 / 247 | 247 / 248 | Fatal missing completion; latch alone insufficient |
| `frag16-owned-1000-drop2` | `d6072a5c` | A | 1000 / frag16 / 2 | 31 / 30 | 30 / 31 | Fatal missing completion with fired ownership retained |
| `frag16-instrumented-1000-drop2` | `c06f5953` | A | 1000 / frag16 / 2 | 109 / 109 | 109 / 109 | Fatal missing completion; no response overwrite recorded |
| `frag16-sticky-1000-drop2` | `a9c9cdfc` | A | 1000 / frag16 / 2 | 4 / 4 | 4 / 4 | Fatal missing done; retained bits `5` |
| `frag16-recovery-1000-drop2` | `7f4875b7` | A | 1000 / frag16 / 2 | 1000 / 1000 | 1000 / 1000 | Pass, four explicit completion timeouts recovered at each endpoint |
| `exhaust-frag2` | `7f4875b7` | A | 1 / frag2 / 8 | 0 / 0 | 0 / 0 | Negative: eight attempts/timeouts, CW 1023; no eligible peer ACKs suppressed |
| `frag2-reverse-1000-drop2` | `7f4875b7` | B | 1000 / frag2 / 2 | 1000 / 1000 | 1000 / 1000 | Pass; two explicit completion timeouts recovered on A |
| `cycle-5000-drop2` | `7f4875b7` | A | 5000 / cycle / 2 | 5000 / 5000 | 5000 / 5000 | Pass across sequence wrap; one explicit completion timeout recovered at each endpoint |
| `exhaust-frag16-reverse` | `7f4875b7` | B | 1 / frag16 / 8 | 0 / 0 | 0 / 0 | Expected negative: eight eligible ACKs suppressed on A, eight failed attempts on B, CW 1023, no controller fault |
| `cycle-100000-drop2` | `7f4875b7` | A | 100000 / cycle / 2 | 48894 / 48894 | 48894 / 48895 | Failed: completed response owned raw done status; not a passing sustained gate |
| `frag16-final-100-drop2` | `08ff63a8` | B | 100 / frag16 / 2 | 100 / 100 | 100 / 100 | Pass; one explicit completion timeout recovered on B |
| `cycle-final-100000-drop2` | `08ff63a8` | A | 100000 / cycle / 2 | 0 / 0 | 0 / 0 | Failed: eight initial attempts exhausted without eligible bound-peer receptions |
| `frag16-final-1000-drop2` | `08ff63a8` | B | 1000 / frag16 / 2 | 1000 / 1000 | 1000 / 1000 | Pass; eight/one explicit completion timeouts recovered |
| `cycle-final-5000-drop2` | `08ff63a8` | A | 5000 / cycle / 2 | 5000 / 5000 | 5000 / 5000 | Pass across sequence wrap; one/zero explicit completion timeouts recovered |
| `exhaust-final-frag16` | `08ff63a8` | B | 1 / frag16 / 8 | 0 / 0 | 0 / 0 | Bounded failure: eight attempts/timeouts, CW 1023, only six eligible ACKs suppressed on A |

The `7f4875b7` passing sixteen-fragment trial ACKed 16,000 distinct MPDUs per endpoint;
attempts were 16,059/16,042, total timeouts 59/42, suppressed received
duplicates 19/14, and deliberate ACK suppression exactly 2/2. The passing
two-fragment trial ACKed 2,000 distinct MPDUs per endpoint; attempts were
2,007/2,016, timeouts 7/16, suppressed duplicates 3/4 and ACK suppression 2/2.
Both had zero corrupt payloads, duplicate application deliveries, ordering
errors, MAC reassembly errors or controller faults. CW returned to 15.
Explicit engine-completion timeouts are included in total timeouts, not extra
attempts or successes.

The first exhaustion negative is **not** a controlled eight-lost-ACK result:
neither endpoint reported an eligible suppressed ACK. It verifies the bounded
failure path and final CW, but the lack of bound-peer receptions remains
unexplained, as in an earlier first fragmented trial. Do not discard this
failure or count its missing receptions as intentional ACK suppression.
The follow-up sixteen-fragment exhaustion trial independently confirmed eight
raw fragments checked and eight eligible ACKs suppressed at A, zero ACKed
MSDUs at either endpoint, exactly eight attempts/timeouts at B and CW 1023.
The responder's incomplete-MSDU inactivity failure is expected in this negative
test; it is not a passing full-transfer fixture.

The `7f4875b7` 5,000-per-direction cycle used alternating 32/1,500/2,304-byte MSDUs,
crossed 4,096-sequence wrap and passed in 115.53/117.54 seconds. Attempts
were 5,032/5,021, total timeouts 32/21 and suppressed received duplicates 5/5,
with two deliberate ACK suppressions and one explicit engine-completion timeout
per endpoint. All application counts/integrity checks passed and CW reset to 15.

An independent standard-library CRC32 oracle checked final-index evidence for
both passing 1000-MSDU fragmented trials, without calling C frame/CRC helpers.
It checked 36 original fragment FCS values, four reconstructed FCS values,
nonce/pattern/addresses/sequence/Duration, all-count/error/timeout invariants,
binary completion reports and reset CW. Single-byte corruption controls
failed CRC comparison as expected. Output: `independent-fragment-oracle.log`.
The receiver validates every earlier fragment; individual earlier captures
are not all retained on the host.

### Sustained failure and final recovery boundary

The first 100,000-per-direction cycle stopped after about 1,131 seconds.
Initiator/responder attempts were 49,191/49,145, timeouts 297/251 and explicit
engine-completion timeouts 15/19. There was no application corruption,
duplicate delivery or ordering error, but the target was not reached.
The initiator failed with `0x82`, address `0x1800150e`, value `0x04c1002f`,
retained conditions `5`, raw conditions `7`, fired request `2` and pipeline
`0x0e080001`. A completed automatic response owned the raw done status.

Build `08ff63a8` extends only the failed-attempt classifier to this quiescent
completed-response case. It neither marks Data complete nor credits a received
ACK. Native tests reproduce the exact state and reject neighboring unsafe
response states. This state has not been independently re-observed and
identified as the recovered state in an RF trace on the final build.

The exact-challenge 100,000 retry exhausted eight attempts in 12,879 us with
zero received/ACKed MSDUs and zero eligible ACK suppressions; the responder
reported inactivity after about three seconds. This is an unresolved
first-no-reception failure, not intentional ACK loss. The paired runner's
fixed one-second gap is a possible startup/readiness problem, **not a proven
cause**. The shared USB report API publishes only completed probe results;
there is no radio-ready handshake in this fixture. No arbitrary delay,
retry-budget increase or acceptance relaxation was introduced.

### Final-build focused regression

On `08ff63a8`, the reversed-role 1,000-MSDU sixteen-fragment trial ACKed 16,000
MPDUs per endpoint. Attempts were 16,579/16,630, total timeouts 579/630,
suppressed duplicates 17/12 and engine-completion timeouts 8/1. Each endpoint
deliberately suppressed two ACKs. Elapsed times were 95.86/95.52 seconds.

The final 5,000-per-direction mixed-size trial crossed sequence wrap in
122.86/124.90 seconds. Attempts were 5,536/5,429, timeouts 536/429, suppressed
duplicates 2/90 and engine-completion timeouts 1/0, with two deliberate ACK
suppressions per endpoint. Both final positive trials had zero application
corruption, duplicate delivery, ordering errors, MAC reassembly errors or
controller faults, and CW reset to 15. Timeouts were substantially more
frequent than in the earlier build's trials; these sequential, uncontrolled
RF runs do not establish either a performance regression or its absence.

The final exhaustion trial checked/suppressed six eligible raw fragments on
the responder, not eight. The initiator still correctly reported eight failed
attempts/timeouts, no ACKed MSDU, CW 1023 and no controller fault. Two attempts'
missing eligible receptions remain unexplained. Do not count this as a
controlled eight-suppressed-ACK result; that control passed only on the earlier
build, as retained above.

An independent standard-library oracle checked the final positive trials'
count/timeout/MAC-error/CW invariants and binary-to-JSON completion records.
For the fragmented trial it recomputed all 32 retained original fragment
CRC32s and both reconstructed CRC32s, exact frame fields and corruption-negative
controls, using the collector's encoded count/drop/profile challenge.
Output: `final-independent-oracle.log`. This supplements receiver validation;
it is not an independent sniffer or a capture of every earlier fragment.

## Follow-up: locating the failing stages

Evidence directory: `build/clean-rx/failure-localization-20261022/`.
Earlier failing images, matching collectors where available, binary/JSON
reports, native failing reproductions and build logs are retained.
The timing-only image is `79cd5144`; its original collector was not archived.
The subsequent matched image/collector pairs are `25e19eda` (`fixed`),
`76d1458f` (`handoff`) and `3bb01fa7` (`heldrx`).
The closing-grace follow-up is `d4ae73fb` (`close`).

### Verified defects and changes

1. **The badge timebase was not crystal-referenced.** Device "microseconds"
   advanced at about 0.486 times host monotonic time. The measured startup
   reference selector was 0 and TIMER0 tick cycles were **24**, not 12.
   Startup now calls the existing shared `nametag_clock_reference()` without
   changing shared code or reconfiguring PLL_SYS. Reports require reference
   selector 2, timer cycles 12 and a successful SYS frequency measurement.
   The SYS measurements are about 37--46 MHz; no fixed 120-MHz CPU assumption
   is made. A boot/clock failure is latched in `clean_boot_error`.
   This fixes a real timebase defect, not a demonstrated RF-reception cause.
2. **Retired response status could contaminate Data completion.** A failing
   native reproduction changed incomplete Data evidence from 5 to 7 after
   response ownership cleared, borrowing the response's raw done bit.
   `TX_COMPLETION_CLOSE_PROGRAM` now snapshots Data and sets sticky closed
   bit 8 before ACK/CTS/Block-ACK TXE reuse. Closed evidence survives response
   retirement; only a fresh Data trigger clears it. The failed-attempt
   classifier accepts closed incomplete evidence only as TIMEOUT, never
   success. The final decision samples owned evidence after PHY drain.
3. **RX priority could skip a real PHY-start observation.** Build `25e19eda`
   failed at MSDU 867, fragment 9 with pending PHY start still set, although
   ARM's first IFS sample contained TX-active (`0x100`). A native reproduction
   also showed that simultaneous RX priority skipped the start pulse.
   PSM now observes start before RX dispatch. A guarded ARM handoff can
   publish its actual observed start timestamp under PSM pause, only for
   fired, pending, response-free, unclosed Data ownership. It does not
   manufacture completion. All `76d1458f` RF handoff counters were zero:
   that campaign did not demonstrate the ARM start handoff executing.
4. **A failed controller stop erased the useful fault snapshot.** The
   `25e19eda` long replay timed out on a mailbox command; zero diagnostics
   resulted from skipped reads, not demonstrated controller reset.
   A failed stop now permits bounded, explicitly labelled live reads while
   preserving both the original failure and the stop error. Live snapshots
   are not atomic and never satisfy positive acceptance.
5. **Profile-3 completion had three iteration-count waits for the same
   retained word.** Servicing RX/ACKs inside each iteration meant these were
   not microsecond limits and could obstruct host RX draining. In
   `76d1458f`'s A-initiated control, the responder timed out sending index 67.
   Its live snapshot showed firmware running (9), held RX state 14,
   retained Data bits 5, fired request 2 and one engine timeout. Maximum
   capture hold was 71,402 us. This identifies a TX/RX wait interaction;
   the non-atomic snapshot does not prove every sampled field came from
   the same instant/submission.

   Build `3bb01fa7` uses one elapsed-time-bounded profile-3 completion wait
   (`airtime + 2,000 us`), retaining an iteration safeguard and the existing
   PHY drain. Legacy non-profile-3 polling is unchanged. While PSM holds an
   RX capture, ARM may hand over **actual complete raw Data status** only
   under PSM pause, after rechecking held state, fired ownership, observed
   start, no response owner, no closed evidence, MAC-not-busy, MAC-done and
   PHY-idle. It also checks after drain. A response's raw status cannot pass
   those guards. This does not set ACK-seen, clear a capture, raise the
   mailbox timeout, enlarge retry limits or convert incomplete evidence
   into success. Native tests cover each ownership/status guard and the
   exact elapsed-time boundary, including timer wrap.
6. **The fixture stopped RX as soon as the initiator received the last
   reply.** On `3bb01fa7`, `startup-heldrx-a-100` delivered all 100 replies
   to the initiator, which reported success, but the responder exhausted
   eight attempts for reply index 99 and ACKed only 99. There was no engine
   fault. Source inspection confirmed no opportunity to handle a
   retransmitted last reply after completion. This closing race is separate
   from the earlier mid-run loss.

   Build `d4ae73fb` keeps the initiator's RX/automatic ACK service alive
   after its final delivery for the existing two-second peer retry bound
   plus one 100-ms command bound. It still checks/duplicate-filters incoming
   traffic, never counts duplicates as new deliveries, does not increase
   sender retries/timeouts, and records phase 6 (closing) until finished.
   Native tests cover the exact closing deadline and wrap. A one-MSDU
   trial suppressing one ACK at each endpoint specifically exercises the
   closing case: the first reply is delivered but its ACK is deliberately
   withheld, so the responder must retransmit after initiator delivery.

### Progress and snapshot contract

Soak reports carry a separate `MSP1` record at detail offset 720. The current
record is 80 bytes (20 little-endian uint32 words), ending at 800:
magic; ready/first-send/first-result/first-valid-peer-RX times; phase,
application index, fragment, attempts, result; reference/tick cycles before
and after initialization; measured SYS kHz Q5; snapshot flags and stop error;
PHY-start and completion handoff counters; duplicates suppressed specifically
during closing. A per-fragment result begins as
`0xffffffff`, not the previous fragment's ACKED result.
First-send means command preparation began, not exact on-air trigger time.
First-RX includes valid raw fragments before MSDU reassembly.

Snapshot flags are frozen = 1, mailbox read = 2, full diagnostics = 4.
Successful acceptance requires flags 7, no stop error, valid progress and
qualified timer reference. A complete live snapshot has flags 6, not 7.
Collector reports also retain host monotonic submission/report timestamps
and the original error address. Cumulative ARM handoff counters at controller
addresses `0x2050c` and `0x20510` reset with station configuration.
The 180-byte TX diagnostics, 28-byte `MTC1` and 64-byte `MSK3` layouts remain
unchanged. Use the collector matching each image/build ID.

### Localization trials before the held-RX change

Counts below are initiator/responder; all are 6-Mbps OFDM on channel 1.
Positive runs suppress two eligible ACKs per endpoint. Each failed trial
remains a failure, not pooled with controls.

| Prefix / build | Initiator / target | ACKed MSDUs | Received MSDUs | Result |
| --- | --- | --- | --- | --- |
| `startup-fixed-a-100` / `25e19eda` | A / 100 cycle | 0 / 0 | 0 / 0 | Eight initial attempts, no eligible peer reception |
| `startup-fixed-b-100` / `25e19eda` | B / 100 cycle | 100 / 100 | 100 / 100 | Pass |
| `frag16-fixed-1000` / `25e19eda` | B / 1000 frag16 | incomplete | incomplete | Fatal `0x82` at initiator index 867, fragment 9; missed PHY start |
| `cycle-fixed-100000` / `25e19eda` | A / 100000 cycle | incomplete | incomplete | Mailbox-command timeout near index 1573; snapshot skipped |
| `startup-handoff-a-100` / `76d1458f` | A / 100 cycle | 68 / 67 | 67 / 68 | Responder send-command timeout at index 67; labelled live snapshot |
| `startup-handoff-b-100` / `76d1458f` | B / 100 cycle | 100 / 100 | 100 / 100 | Pass |
| `frag16-handoff-1000` / `76d1458f` | B / 1000 frag16 | 76 / 75 | 75 / 76 | Responder exhausts eight attempts at index 75, fragment 10; no engine fault |
| `cycle-handoff-100000` / `76d1458f` | A / 100000 cycle | 496 / 496 | 495 / 496 | Both become inactive; one ACKed reply not delivered to initiator |
| `exhaust-handoff-frag16` / `76d1458f` | B / 1 frag16, suppress 8 | 0 / 0 | 0 / 0 | Controlled negative: eight eligible ACKs suppressed, eight attempts/timeouts, CW 1023 |

The clock-qualified `startup-fixed-a-100` timing places responder readiness
about **1,564,612 us before** the first sender command preparation, while
the responder remains in its receive loop. Startup lateness is therefore
ruled out **for that reproduced no-reception trial**, not for every
historical failure. Earlier unqualified device times cannot simply be
added to host microseconds.

The `76d1458f` long failure is distinct from a fatal TX-completion problem:
both endpoints have frozen snapshots, no controller error, complete local
TX evidence and no engine timeouts. The responder claims reply index 495
ACKed; the initiator has delivered only through 494. ACKed-but-not-delivered
RX admission/capture/credit/duplicate handling and possible ACK attribution
still need investigation. Do not claim that the timer, closure or start
fixes explain this loss, or that a later short control qualifies the
100,000-per-direction gate.

Default controller and badge `all test`, ASan/UBSan regressions, separate
HT-response/EDCA/WMM and SHM Block-ACK controller builds all pass on
`3bb01fa7`. These native checks are not RF qualification.
The same default, badge, HT-response/EDCA/WMM and SHM Block-ACK build/test
combinations also pass after the `d4ae73fb` closing change.

### Held-RX bounded-wait controls (`3bb01fa7`)

The B-initiated 100-cycle control passed in both directions. The A-initiated
100-cycle control exposed the final-reply closing race above: ACKed counts
100/99, receive counts 100/100, responder eight-attempt exhaustion at index 99.
That paired trial remains a failure even though its initiator reports valid.

The B-initiated 1,000-MSDU sixteen-fragment control passed: 16,000 ACKed
MPDUs per endpoint, 1,000 reassemblies and no application corruption,
duplicate delivery or ordering error. Explicit incomplete-completion
TIMEOUTs were 3/11 and elapsed times 203.74/205.29 seconds. Both ARM
handoff counters were zero: the completion handoff is guard-tested natively
but was not observed executing in these controls.

An independent standard-library oracle reconstructed the encoded challenge,
addresses, sequence/fragment fields, Duration, body pattern and Retry state.
It checked all 32 retained last-index fragment CRC32 values, both
reassembled CRC32 values, corruption negatives, count/attempt/timeout
invariants and binary-to-JSON completion/progress records, including the
qualified timer reference. These are 34 independent CRC comparisons, not
an external RF capture or retained captures of every earlier fragment.

### Exact long replay: failure is now ACK exhaustion, not a fatal engine

`cycle-heldrx-100000` reuses the exact encoded challenge/profile/count of
the original 100,000-cycle failure. It ran for about 3,225 seconds before
the initiator exhausted eight ACK attempts for Data index 64,002:

| Endpoint | ACKed MSDUs | Received MSDUs | Attempts / timeouts | Explicit engine TIMEOUTs |
| --- | --- | --- | --- | --- |
| A, initiator | 64,002 | 64,003 | 64,888 / 886 | 38 |
| B, responder | 64,003 | 64,003 | 64,692 / 689 | 31 |

The initiator had already received valid reply 64,002 while retrying its
own Data; the responder had accepted that Data, ACKed its reply and was
waiting for request 64,003. Both snapshots are frozen/complete (flags 7),
controller errors are zero, last local completion is 7, and there is no
application corruption, duplicate delivery or ordering error. CW is
1023/15. Both ARM handoff counters remain zero.

Crucially, the responder reports **9 suppressed ACKs despite being configured
for 2**; its final ACK stage is 4 (suppressed) and frame control is `0x0908`
(Retry Data). The initiator still reports the expected 2 suppressions.
This is not nine deliberately requested losses, nor evidence that the
final eight attempts were all absent at the peer. It narrows this failure
to unexpected suppression/ACK-state behavior; the record does **not**
contain the remaining-suppression word or an atomic transition trace, so
the cause of that state change is not established. Earlier
`76d1458f` failures retained the expected suppression counts and should
not be retroactively attributed to this newly observed condition.

This long attempt is a failure, not a 100,000-per-direction pass. The
bounded-wait change demonstrably permits many explicit failed-attempt
recoveries without a mailbox/engine fatal in this trial, but one longer
run does not prove all earlier transport failures fixed.

`exhaust-heldrx-frag16` separately passed the controlled negative:
eight raw fragments checked and eight eligible ACKs suppressed at A,
eight attempts/timeouts at B, zero delivered MSDUs, CW 1023 at B, no
controller/engine error and frozen complete snapshots at both endpoints.

The next long-run diagnosis should capture remaining/expected/dropped
suppression state at the first mismatch and compare a zero-suppression
control, rather than increasing retry limits or crediting a received
application reply as a MAC ACK. Independent RF ACK/SIFS evidence is still
needed. No received capture or credit is discarded to force progress.

### Final closing-grace controls (`d4ae73fb`)

| Prefix | Initiator / target | ACKed MSDUs | Received MSDUs | Outcome |
| --- | --- | --- | --- | --- |
| `closing-after-one` | A / 1 cycle, suppress 1 | 1 / 1 | 1 / 1 | Pass; one duplicate handled specifically during initiator closing |
| `closing-after-reverse-one` | B / 1 cycle, suppress 1 | 1 / 1 | 1 / 1 | Pass; same closing behavior in reverse |
| `startup-close-a-100-1` | A / 100 cycle | 100 / 100 | 100 / 100 | Pass |
| `startup-close-a-100-2` | A / 100 cycle | 100 / 100 | 100 / 100 | Pass |
| `startup-close-a-100-3` | A / 100 cycle | 0 / 0 | 0 / 0 | First-no-reception failure; eight attempts, no eligible ACK suppression |
| `startup-close-b-100` | B / 100 cycle | 100 / 100 | 100 / 100 | Pass |
| `frag16-close-1000` | B / 1000 frag16 | unavailable / 210 | unavailable / 210 | Initiator USB device disappears; responder becomes inactive |
| `cycle-close-5000` | A / 5000 cycle | 5000 / 5000 | 5000 / 5000 | Pass across sequence wrap |
| `exhaust-close-frag16` | B / 1 frag16, suppress 8 | 0 / 0 | 0 / 0 | Eight attempts/timeouts and CW 1023, but only seven eligible ACKs suppressed |

The one-MSDU controls each use exactly two attempts/one timeout per endpoint
and exactly one deliberately suppressed ACK per endpoint. Initiator
`closing_duplicates_suppressed` is 1 in each direction, with no duplicate
application delivery. This directly exercises a lost final ACK and a
successful retry after delivery while RX remains active. The older
`closing-before-one` control on `3bb01fa7` failed before any eligible peer
reception (zero ACKs suppressed): it is **not** a controlled before/after
demonstration of the closing race. The earlier 100-cycle final-reply
failure and the new phase-specific retry evidence remain separate facts.

The final 5,000-cycle run uses attempts 5,037/5,043, timeouts 37/43,
explicit engine TIMEOUTs 3/5 and elapsed times 250.08/249.54 seconds.
Both endpoints have zero application corruption, duplicate delivery,
ordering error or controller fault, complete frozen snapshots, the expected
two ACK suppressions and CW reset to 15. Neither ARM handoff executed.

The final fragmented trial is **not** a 1,000-MSDU pass. Its initiator has
no final JSON/binary report: stderr records `LIBUSB_ERROR_NO_DEVICE (-4)`.
The responder's frozen report has 210 ACKed/received MSDUs, 3,367 checked
raw fragments, two explicit engine TIMEOUTs and no controller error before
inactivity at index 210. The USB disappearance is not sufficient evidence
to distinguish watchdog/reset, USB transport or another hardware failure.
The earlier `3bb01fa7` 1,000-MSDU pass does not erase this failure.

The final exhaustion negative received/suppressed seven eligible fragments,
not eight. It verifies the sender's exact eight-attempt/CW bound and no
false success, but is not a controlled eight-suppressed-ACK result.
That full control passed on `3bb01fa7`, as recorded above.

`final-localization-oracle.log` independently checks all twelve endpoint
records from the six successful final trials: exact counts, zero errors,
attempt/timeout accounting, binary/JSON agreement, timer qualification,
frozen snapshot validity, reset CW, exact ACK suppression and the
phase-specific closing duplicates. It also checks both exhaustion
negatives separately, retains the 64,003-delivery failure and reproduces
the qualified 1,564,612-us first-failure readiness lead.
The sustained/interoperability gate, initial no-reception failures,
unexpected suppression state and USB disappearance remain open.

## Borrowed hardware restoration

Both full 16 MiB original flashes were backed up and verified before testing.
They were restored after the sustained attempts and again after the final
focused trials, with independent full-flash readbacks and byte-for-byte
comparison. Final SHA-256 values:

- A: `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`
- B: `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3`

Evidence includes each serial's `-restore-final.log`,
`-restored-readback-final.log` and `-restored.bin`. BOOTSEL readiness is polled
before restoration, rather than assuming the watchdog reboot is immediate.
Both badges are left in BOOTSEL with their original three-device diagnostic
images restored. The Pico 2 W was not changed during this work.

The localization campaigns also restored both original 16 MiB images,
including after `3bb01fa7` and the final `d4ae73fb` controls. Independent
full readbacks compare byte-for-byte with the originals and have the same
SHA-256 values above. The latest evidence is each serial's
`-close-restore.log`, `-close-readback.log` and `-close-restored.bin` under
`build/clean-rx/failure-localization-20261022/`. Both badges are left in
BOOTSEL, with no outstanding flash restore or test process. Pico remains
untouched.

## Reproduction

```sh
make -C clean-rx/src CC=clang \
  BUILD="$PWD/build/clean-rx/mac-reliability-20261022/controller" all test
make -C clean-rx/badge \
  BUILD="$PWD/build/clean-rx/mac-reliability-20261022/badge" \
  CONTROLLER_BIN="$PWD/build/clean-rx/mac-reliability-20261022/controller/cyw43_rx_fw.bin" \
  all test

# First back up/verify both borrowed full flashes, install matching badge UF2s
# with explicit serials, and leave both boards in BOOTSEL.
CLEAN_RX_COLLECTOR=build/clean-rx/mac-reliability-20261022/badge/collector \
  bash clean-rx/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL \
  build/clean-rx/mac-reliability-20261022/NEW_PREFIX 1000 2 ofdm6 frag16
```

## Remaining limits

- The missing TXE done-event cause is unresolved. The fixed behavior is safe,
  explicit failed-attempt recovery, not removal of the hardware event loss.
- The first no-bound-peer-reception failure and the final exhaustion trial's
  two missing eligible receptions remain unexplained. Startup readiness is
  only a hypothesis; a verified synchronization mechanism remains open.
- No new vendor control, independent sniffer, calibrated RF timing, controlled
  hidden-node/overlap/NAV injection or CCK fragment RF matrix.
- No passing 100,000-MSDU-per-direction mixed-size gate.
- No automatic station fragmentation, SIFS fragment burst or new protected/
  QoS/HT fragment RF qualification.
