# MAC completion ownership and explicit timeout recovery

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

Date: 2026-10-22. Follow-up to
[bounded fragmentation qualification](LEGACY_FRAGMENTATION.md).
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

## A2 follow-up: suppression invariants and CCK ACK clock ownership

Evidence is retained under `build/clean-rx/a2-20261022/`, with matched
controller, UF2 and collector builds. Earlier failures above remain failures.

### Guarding deliberate ACK loss

A native reproduction initializes limit 2, remaining 1 and dropped 2.
The earlier implementation suppresses another ACK without an error.
`suppression-repro.log` retains that failing assertion. This establishes a
missing fail-closed invariant, not the spontaneous cause of the historical
9-versus-2 suppression count.

Suppression now checks the configured maximum and
`remaining + dropped == limit` before changing a counter. A violation retains
the first frame/sequence, before/after and prior dropped count, disables DCF
and increments the ACK error once. It does not clamp/reset counters, clear a
received capture or credit a reply as an ACK. Configuration is readback-checked
and deferred to a paused, rechecked PHY-idle boundary with no active
transaction. A faulted configuration is collected and the controller restarted;
command 9 reports error `0x87` rather than indefinitely deferring a fault.

The first RF build, `17038141`, passes ten positive paired controls
(20 endpoint records): zero loss in both roles, two-loss OFDM in both roles,
one-MSDU final-ACK loss in both roles, 100 two-/sixteen-fragment MSDUs,
and separate 5,000-per-direction cyclic-size controls with zero/two deliberate
losses. Both long controls cross sequence wrap. Attempts/timeouts are
5,039/39 and 5,090/90 for zero loss, and 5,101/101 and 5,046/46 for two losses.
All passing endpoints have exact counts, healthy budgets, frozen complete
snapshots, zero application corruption/duplicate delivery/order errors and
valid saved returned ACKs. `guard-oracle.log` independently checks ACK CRC32,
RA, timestamp window, PHY, budget, attempt accounting and binary/JSON agreement.
This is endpoint evidence, not independent SIFS measurement.

Its exhaustion controls both stop after eight actual attempts/timeouts with
no false success. `exhaust-reverse` receives/suppresses eight eligible
fragments; `exhaust-forward` receives/suppresses only seven and is not a
controlled eight-lost-ACK pass. Its fourteen CCK controls fail, usually after
one delivered frame, despite exhausting the requested two-loss budget.

### Proven CCK clock overwrite

Unmodified `4392ca2`, rebuilt locally as `9c5df18d`, also fails 2-Mbps
100-frame controls with zero and two losses. Thus the CCK failure is not
introduced by the new suppression checks. Historical CCK passes predate the
completion/timer changes; they do not qualify this baseline.
The baseline scratch logger mistakenly prints `exit=0` after a date command
substitution; the endpoint JSON and `*-run.log` explicitly report failure.
The logger was corrected before the observation/final campaigns.

Instrumentation build `a7602957` separates ARM expiry from PSM deadline
expiry and samples the last own-address, exact-length GoodFCS ACK before
the waiting gate. Its 11-Mbps-short failure records:

- PHY start 196,698 us (low word 90).
- Full PSM window 1,636 us; deadline low word 1,726.
- ARM clears waiting at 197,955 us (low word 1,347), using start 196,698
  and the shortened, post-tail window of 398 us.
- The ACK arrives at low word 1,449, **277 us before the actual deadline**,
  but waiting is already zero.

`station_service_tx()` correctly moves the ACK clock to the observed CCK
physical tail and shortens the window to 398 us. The later completion-status
resample then overwrites that clock with PHY **start**, without restoring
the full window. This causes premature expiry, particularly at slower rates
or larger payloads. The repair preserves an ended tail's ACK clock when
resampling the diagnostic PHY start. It does not extend the ACK allowance,
resurrect an expired wait or change TIMEOUT to ACKED.

Native tests cover all legal CCK rate/preamble combinations, 32/1,500/2,304-byte
payloads, TSF wrap, repeated completion resampling and exact timeout boundaries.
Build `27d2a2a6` passes fresh 100-frame 1-Mbps/1,500-byte controls in both roles,
2-Mbps cyclic-size controls in both roles, and several higher-rate controls.
The exact old challenge still fails twice with an initial reception/completion
stall; this is not a clean matched before/after success. Other controls retain
completion failures and a sixteen-fragment run stalls at 370 delivered MSDUs per
endpoint, without a suppression fault. These are not reclassified as fixed.

### A real first-fault capture narrows the arithmetic path

`clock-rate-5.5-long` on `27d2a2a6` catches fault 3 on badge B, followed by
controller error `0x87`. Its frozen first-fault report retains limit 2,
remaining 0, dropped 2, before/after 0, prior dropped 2, Retry Data `0x0908`,
sequence 0 and epoch 3. The retained counters satisfy the invariant despite
the sum-mismatch branch. The initial guard had staged the counters but still
compared the freshly computed GPR sum directly with SHM. It did not retain
the computed sum or the limit actually read by that comparison, so the exact
transient operand/dependency fault is not proven.

The final implementation also stages the limit in a GPR, temporarily saving
and restoring response airtime. All comparisons are register-only. Both the
computed sum and register limit are stored independently before the branch,
following the earlier RF-qualified DCF arithmetic/store/trace/branch pattern.
The raw operands are retained as a separate record; a recurrence can now
distinguish a bad computed value, bad sampled limit and unexpected branch.
Native tests enforce the instruction operands and separations, four guard
reasons, sticky first-fault frame/clock, unchanged invalid counters, CTS bypass,
all legacy response rates and existing 100,000-rearm behavior.

### Evidence records and acceptance

- `AST1`: SRAM `0x20514`, 32 bytes, soak detail 688. Expected/limit/remaining/
  dropped, fault/phase, before/after/prior dropped, low TSF, frame control,
  sequence and epoch. Phases 0 idle, 1 checking, 2 committing, 3 fault.
  Faults 1 remaining out of range, 2 dropped out of range, 3 sum mismatch,
  4 invalid limit. First-fault operands remain sticky until controller restart.
- `AKR2`: soak detail 800, 32 bytes. The existing ACK record now captures legacy
  as well as HT ACKs. Acceptance checks the complete 14-byte ACK/FCS, own RA,
  type/Duration, basic response rate/preamble and timestamp within the last
  ACKed submission. Counter/TSF wrap is covered; TIMEOUT never passes.
- `OBS1`: SRAM `0x20534`, 32 bytes, soak detail 832. Last ACK's low TSF,
  waiting flag, deadline, PHY start and full window; most recent expiry reason
  (1 ARM, 2 PSM), expiry/deadline low words, and full ARM clock/start/window.
  Observation and expiry can belong to different attempts; compare their
  deadlines before associating them. Live samples are not atomic.
- `SUM1`: SRAM `0x20554`, 12 bytes, soak detail 864. Saved response airtime,
  computed GPR sum, sampled GPR limit and reserved zero. Sum is meaningful
  after the sum step, not on an earlier range fault. Successful acceptance
  requires this record's sum/limit to equal the configured budget too.

Both endpoints now retain bounded 2.1-second final RX/automatic-response
service, covering final-request as well as final-reply retries. Endpoint ACK
bytes and timestamps are not an independent RF reference. Independent
ACK/SIFS, controlled full-credit/late-ACK RF evidence, initial access stalls,
the interrupted long gate and sustained interoperability remain open.

### Final RF qualification (`b0aedeee`)

`arith-*` retains all 26 trials: 21 passing positive pairs, **3 failed positive
pairs**, and two fully controlled exhaustion negatives. All 52 endpoint
records are present. The campaign exits 1 because of those three real failures.
The known 1-Mbps/full-size receive-truncation case is not retested here:
1-Mbps controls use 1,500-byte payloads, not cyclic 2,304-byte payloads.

| Control | Result |
| --- | --- |
| Exact earlier 5.5-Mbps sum-fault challenge, 100 cyclic MSDUs/direction, drop 2 | Pass; 102/103 attempts, 2/3 timeouts; computed sum/limit 2/2, no fault |
| Fresh eight-PHY matrix, both role assignments, 100 MSDUs/direction, drop 2 | 13/16 paired trials pass; all eight profiles have a successful final-build control |
| Zero-suppression OFDM, 100 cyclic MSDUs/direction, both roles | Both pass |
| One-MSDU OFDM, drop 1, both roles | Both pass; exactly two attempts/one timeout per endpoint |
| Two-fragment OFDM, 100 MSDUs/direction, drop 2 | Pass; 203/213 attempts, 3/13 timeouts |
| Sixteen-fragment OFDM, 100 MSDUs/direction, drop 2 | Pass; 1,624/1,613 attempts, 24/13 timeouts |
| Cyclic-size OFDM, 5,000 MSDUs/direction, drop 2 | Pass; 5,038/5,081 attempts, 38/81 timeouts |
| Exhaust first fragment with drop 8, both roles | Both controlled negatives: exactly eight eligible fragments suppressed and eight actual attempts/timeouts; CW 1023, no false success |

Every passing endpoint has exact counts, zero corruption/duplicate delivery/
ordering errors, valid returned ACK bytes/FCS/PHY/timestamps, healthy budget
and arithmetic records, frozen complete snapshots, reset CW 15 and zero
explicit engine-completion timeouts. Neither ARM handoff executes in these
final passing controls. The closing controls each handle one duplicate during
initiator closing; responder closing duplicates are zero, so the new
responder-specific retry case is not independently RF-demonstrated.

The final failures are `arith-rate-2-long`, `arith-rate-5.5-long` and
`arith-reverse-11-long`. Their responder stops with error `0x82` at
`0x1800150e`, accumulated completion-window status `0x00c1052f` and retained
completion `5`: MAC-done was not observed. The final current status is
`0x0001002f` (not currently TX-busy), while the PHY-error sample is `0x0010`.
Do not mistake the accumulated busy bit for current TX-busy, or attribute the
failure to the access/backoff wait. The incomplete-completion recovery requires
zero PHY error, so this nonzero sample explicitly excludes these attempts.
Its exact PHY-error meaning/provenance remains unqualified; ignoring it to
manufacture retryability would be unsafe.

These three trials deliver at most one application frame per endpoint and
have no suppression fault or over-budget drop count. Thus neither deliberate
loss bookkeeping nor the repaired ACK clock explains away all remaining
failures. The prior `27d2a2a6` 1,000-MSDU fragmented attempt still fails at 370;
the final 100-MSDU fragmented pass is not a new 1,000- or 100,000-frame pass.
The older 64,003-delivery anomaly is not replayed to completion.

`final-oracle.log` independently checks all 52 binary/JSON records, expected
challenge/addresses, ACK CRC32 with corruption negatives, exact attempts,
budgets, register operands, PHY/preamble, timestamp windows, frozen progress/
completion records and both exhaustion controls. It also reconstructs all
36 retained last-index fragment CRCs and four reassembled CRCs using independent
CRC32, including ERP signal extension, Duration and Retry state. It explicitly
reports **QUALIFICATION: FAILED**, not a passing aggregate campaign.
Default controller/badge, HT-response/WMM/EDCA and opt-in SHM Block-ACK builds
and native sanitizer suites pass. Native tests additionally cover every
suppression limit 0..16 with eight further ACKs after exhaustion.

A2 is not complete: the nonzero-PHY-error completion failures and independent
ACK/SIFS/missing-or-late-ACK/full-credit RF gates remain. Retry identity and
protected-byte/PN preservation have native coverage and legacy fragmented RF
regression evidence; they are not protected RF timing qualification.

Both full 16-MiB original flashes were restored after each campaign.
Final `*-arith-restored.bin` readbacks compare byte-for-byte with the established
originals and match SHA256
`02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` (A) and
`2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` (B).
Both badges are left in BOOTSEL. Pico and shared infrastructure are untouched.

## CCK PHY-error retirement and retained provenance

Evidence for this follow-up is under `build/clean-rx/cck-error-20261022/`.
The `observe-*`, `select-*` and `plcp-*` campaigns each retain three passing
and three failed positive pairs. They replay the three previous failing
challenges twice, rather than discarding unfavorable runs.

The phase trace proves a current Data attempt can begin with zero PHY error
and later report `0x0010`, with fired Data ownership, idle MAC/PHY and retained
completion 5 (missing MAC-done). The selector computed by PSM and read back
before triggering is correctly `0x0826`; the later `0x082c` value is engine
state, not evidence of an incorrectly constructed selector. Refreshing the
legacy PLCP registers explicitly did not eliminate failures and was removed.
`D11+0x3ee` is the PHY-TX-error register, but no verified meaning for its
individual bits has been established. Do not label `0x0010` an underrun.

The first guarded retirement (`abort-*`) passes all six exact/repeated
controls. One actual `0x0010` failure is retired on badge B in `exact-two`,
after which both endpoints finish 100 cyclic-size deliveries. Broader
testing of the long/fresh-error-only candidate (`692a5294`, `final-*`)
also exposes short-preamble errors, fresh `0x0020`, and a `0x0020` baseline
followed by `0x0030`. PHY error bits can remain latched across successful
packets; nonzero status alone does not prove a fresh fault.

### Guard and reporting

The new guard supersedes the blanket zero-error exclusion only for an owned,
quiescent **CCK** incomplete transmission with observed status limited to
`0x0010`, `0x0020` or `0x0030`, using either preamble. It requires:

- stable prepare/armed/fired error baselines containing no unknown bits;
- matching final and first quiescent-error samples within `airtime + 2,000 us`;
- unchanged fired CCK PHY/rate controls and exactly the current fired Data
  owner, without pending start, response or cleanup ownership;
- no current TXE busy/MAC-done or PHY TX/RX activity, retained completion
  exactly 5, and an observed physical CCK tail that has ended.

The paused-PSM cleanup handshake independently rechecks ownership and RX/TX
hazards before abort/rearm. This is a failed attempted submission: it increments
the engine timeout count, returns TIMEOUT, grows CW and consumes one of the
existing eight attempts. It does not invent Data completion, an ACK or delivery.
Existing sequence/fragment, Retry/FCS and protected-byte/PN retry behavior is
unchanged. Unknown bits, unsafe ownership, active hardware and non-CCK faults
remain fatal. This is bounded recovery, not proof that the underlying PHY
condition has been eliminated or its silicon cause identified.

`TXP1` at SRAM `0x20560`, detail 876, is 100 bytes. It retains validity,
computed/read-back selector, three PLCP words and four 20-byte phase samples:
prepare, armed, fired and first **quiescent** error. Each sample has a timestamp,
PHY error, TXE/IFS state, PHY controls, remaining template bytes, selector and
packed ownership. The last phase is not necessarily the first raw assertion
of an error bit. It is refreshed when known latched bits accumulate after
the initial quiescent sample. An observed unknown status is retained and
cannot be overwritten by a later known-only value.

`PTA1` at SRAM `0x205c4`, detail 1960, is 40 bytes and ends at detail 2000,
before fixture mode/channel. It retains a count and the first retired
attempt's fired timestamp, three baseline errors, completion and error phase
even after later successful retries replace ordinary TX diagnostics.
Collector acceptance requires valid report operands, count no greater than
engine/application timeouts, and exact attempts = ACKed MPDUs + timeouts.
Fragmented MSDUs therefore contribute their actual fragment count.

Default, HT-response/WMM/EDCA and opt-in HT/SHM Block-ACK controller/badge
builds and native sanitizer suites pass. Native tests cover known sticky/new
status combinations, both preambles, timestamp wrap, busy/response/cleanup
ownership, unknown bits, unstable baselines and malformed report accounting.
These are not independent RF timing measurements.

### Broad RF qualification (`a4eefd5a`)

`recover-*` retains all 34 trials: **29 passing positive pairs, three failed
positive pairs**, and two fully controlled exhaustion negatives. The three
failures are `exact-two`, `repeat-fivehalf` and `stress-two`. All exhaust
eight initial attempts without a peer application reception or returned ACK,
with zero controller fault, zero PHY error and no error retirement. They are
the separate no-first-RX failure, not another missing-MAC-done fatal error.
The entire campaign and independent oracle correctly exit 1.

Twelve actual known-status PHY failures are retired in passing trials.
Retained first-abort records include fresh `0x0010`, fresh `0x0020`, and
`stress-fivehalf`'s stable `0x0020` baseline followed by `0x0030`.
Both endpoints of that last trial finish 1,000 cyclic-size deliveries.
No controller-fatal completion failure remains in this matrix; that is not
proof of universal failure-free operation or an eliminated physical cause.

| Control | Result |
| --- | --- |
| Eight legacy PHY/preamble profiles, both role assignments, 100 MSDUs/direction, drop 2 | All 16 pairs pass; 1 Mbps uses 1,500-byte payloads, others cyclic sizes |
| 1,000 cyclic MSDUs/direction, 5.5 Mbps and reverse-role 11 Mbps | Both pass, including real error retirement; 2-Mbps trial fails before first RX |
| Cyclic-size OFDM, 5,000 MSDUs/direction | Pass; 5,118/5,121 attempts, 118/121 timeouts, including two explicit engine timeouts per endpoint |
| Two-fragment OFDM, 1,000 MSDUs/direction | Pass; 2,052/2,057 attempts, 52/57 timeouts, 1/3 engine timeouts |
| Sixteen-fragment OFDM, 1,000 MSDUs/direction | Pass; 16,113/16,138 attempts, 113/138 timeouts, 4/8 engine timeouts |
| Zero suppression, 100 cyclic OFDM MSDUs/direction, both roles | Both pass |
| One-MSDU OFDM closing control, drop 1, both roles | Both pass |
| Exhaust first fragment with drop 8, both roles | Both controlled negatives: exactly eight attempts/timeouts and eight eligible suppressions, no false success or controller fault |

Passing records retain exact counts, reset CW, valid returned ACK CRC/RA/PHY/
timestamps, healthy suppression accounting and frozen complete snapshots.
`recover-oracle.log` independently verifies all 68 raw binary/JSON endpoint
records, ACK single-bit-corruption negatives, abort evidence, exact attempts,
36 last-index fragment CRCs and four reassembled CRCs. It reports
**QUALIFICATION: FAILED**, retaining all three startup failures.

The 1-Mbps/2,304-byte truncation and the interrupted 100,000-frame gate are
not retested. A2/A3 remain open for these receive/sustained failures and
independent ACK/SIFS, late/missing-ACK, full-credit and interoperability
qualification. A later passing replay cannot erase the failed startup trials.

### Late-latching regression and additional exclusions

The unchanged `a4eefd5a` follow-up (`confirm-*`) has six positive passes and
one failure in seven pairs. The failing 2-Mbps stress replay stops at
272/271 deliveries: the responder's baseline and first quiescent error are
`0x0020`, but final error is `0x0030`. Keeping only the first quiescent sample
incorrectly excludes this later known-bit transition. The first-error slot
now resamples monotonic known-bit additions during the same bounded wait;
final/sample equality, ownership and timing checks are not weakened.
Native regression first reproduces the stale-sample rejection, then verifies
the resampled transition, wrap boundary and unknown-error preservation.

Candidate `711154c1` (`latch-*`) retains eight passing positive pairs, two
failed positive pairs and two controlled exhaustion negatives. A repeated
1,000-MSDU 2-Mbps trial completes in both directions with four retirements.
Two-/sixteen-fragment 100-MSDU controls, the exact earlier challenges and
11-Mbps reverse-role control pass. The first stress trial stops at 548
deliveries with **previously unqualified bit `0x0008`**: stable baseline
`0x0028`, final/sample `0x0038`, idle fired Data ownership and completion 5.
This is deliberately still fatal, not silently added to the known mask.
The other failed pair exhausts ACK retries after initial short-preamble
deliveries without a controller fault. Neither is excluded from qualification.

Final source `460b7228` also preserves an unknown error observed after an
earlier known sample, even if later reads contain only known bits.
Unknown observations cannot be erased to manufacture retryability.
Default, HT-response/WMM/EDCA and HT/SHM Block-ACK builds/native sanitizer
suites pass; the new `0x0028`/`0x0038` case has explicit rejection coverage.
The broader 34-case results above belong to `a4eefd5a`, not a full rerun on
this final source. All `confirm-*` and `latch-*` raw records and independent
oracle failures are retained.

The final-source `guard-*` focused campaign retains two positive passes
(1,000 cyclic-size 2-Mbps MSDUs/direction and 100 short-preamble MSDUs/direction),
one fully controlled forward exhaustion negative, and **one failed reverse
negative control**. The last initiator correctly stops at eight actual
attempts/timeouts, but the responder sees/suppresses only five eligible
fragments, leaving budget 3. This is not the required eight-suppression
negative; uncontrolled missing receptions cannot be reclassified as
intentional ACK loss. There is no invariant fault, false success or controller
fault in these four trials. No PHY retirement occurs in their positive
controls, so real recovery evidence remains in the earlier named campaigns.
`guard-oracle.log` verifies all eight raw endpoint records and reports
**QUALIFICATION: FAILED**. The final source has not passed a complete 34-case
rerun or the sustained/interoperability gates.

Every CCK campaign restored both full original 16-MiB images. The final
`guard-SERIAL-restored.bin` readbacks compare byte-for-byte with the pre-test
images and match the SHA-256 values below. Both badges are in BOOTSEL with
no remaining test/restore process. Pico and shared infrastructure are unchanged.

## PHY bit-0x08 and missing-RX provenance follow-up

The retained `0x0028` to `0x0038` failure adds **bit `0x0010`**, not
`0x0008`. The unqualified `0x0008` was already present before the failed
Data trigger. That distinction does not establish its meaning or make the
attempt safe to retire: the existing unknown-bit rejection remains unchanged.

Two bounded diagnostics now preserve evidence without changing completion,
ACK eligibility, delivery, the recovery mask or the eight-attempt limit:

- `phy_first_bit8` records the first sampled assertion of `0x0008`, with
  previous/current status, TSF, TXE/IFS/PHY controls, byte count and sampled
  request/pending/response/cleanup owners. The 20-byte record occupies
  controller `0x205ec..0x20600`, before the A-MPDU probe, and soak detail
  `1040..1060`, before the packet sample. Its timestamp is an ARM observation,
  not the exact silicon assertion time; ownership/register reads are not an
  atomic hardware trace. An absent observation is reported as JSON `null`.
- `soak_bad_fcs` reuses the rejected-authentication record for the first
  matching rejected Data prefix while DCF and ACK reception are enabled.
  It originally required at least 40 RXE bytes and either own RA or bound-peer
  TA. The freshness correction below additionally requires active receive
  streaming and its prefix threshold (normally 128 bytes).
  The existing authentication capture remains active. This path still
  discards the frame, does not ACK or deliver it, and preserves bad-FCS/EIFS
  handling. The 60-byte snapshot occupies soak detail `1616..1676`, after
  the complete 528-byte packet sample and before station detail at 1680.
  `raw` is exactly 40 bytes/80 hex characters: six PLCP bytes plus the first
  34 MPDU bytes. It cannot verify the FCS of an unseen full-length frame.

Compile-time layout checks, native edge-detection tests and simulated PSM
tests cover the `0x0004`-then-`0x0008` case, sticky/repeated status, Data
capture length boundaries, either address match, disabled modes, first-Data
record preservation and no ACK/delivery. Default, HT-response/WMM/EDCA and
HT/SHM Block-ACK controller/badge builds and sanitizer suites pass.

### Initial observation campaign

Evidence is retained under `build/clean-rx/phy08-20261022/observe-*`.
Of ten pairs, three positive pairs and five controlled exhaustion negatives
pass; one positive and one controlled negative **fail**:

- `negative-b1` has eight initiator attempts/timeouts but only six eligible
  suppressed receptions, leaving receiver budget 2. Both suppression reports
  are healthy, without a firmware/invariant fault. No matching rejected Data
  prefix is captured. This localizes the discrepancy before eligible ACK
  suppression; it does not distinguish missed PHY reception, earlier
  corruption/abort or capture unavailability.
- `stress-two1` exhausts the initial eight attempts with zero deliveries and
  no controller fault. Again there is no matching rejected Data prefix.
  Absence of a matching prefix is not evidence that the RF frames were good:
  a corrupted address/type or an earlier RX abort can evade this diagnostic.
- Repeated `stress-two2` and `stress-fivehalf` deliver 1,000 cyclic-size
  MSDUs per direction, with one real known-status retirement in each pair.
  The zero-injected-loss OFDM control delivers 100 per direction.

The successful 2-Mbps run has 1,717/1,797 unrelated filtered captures and
182/154 aggregate bad-FCS indications. These totals do not identify missing
test frames. Capture hold reaches 9,386 us; ambient capture pressure is a
hypothesis, not a demonstrated cause, and no early filter or weakened
software FCS/NAV validation has been installed.

Existing host-monotonic submission brackets and badge-relative ready/send
times estimate that the receiver was ready approximately 1.52 seconds before
the first send in `negative-b1` and 1.60 seconds before it in `stress-two1`.
The passing 2-Mbps replay has a comparable 1.55-second lead. These are
host-aligned estimates, not synchronized RF timestamps, but do not support
attributing these failures to the launcher's one-second startup sleep.
Failed-receiver capture-hold maxima are only 484/403 us, versus 9,386 us in
the passing replay: a large hold maximum is not itself a failure diagnosis.

The initial diagnostic captured the first *any-unknown* status instead of
specifically `0x0008`: `0x0004` was sampled during response ownership
(`owners=0x0200`) in the otherwise successful 2-Mbps run. That neither
decodes the bit nor proves response causality. It also consumes that
first-record slot, hiding any later `0x0008`; the final diagnostic therefore
targets `0x0008` specifically. Initial raw records retain the historical
`phy_first_unknown` field; new matching firmware/collector use
`phy_first_bit8`.

An independent binary/JSON oracle verifies all 20 initial endpoint records,
including exact diagnostic fields and suppression/count/frozen-state gates.
It retains both failed pairs and reports **QUALIFICATION: FAILED**.
Both original full flashes are restored and byte-for-byte readback-verified
after this campaign.

### Targeted bit-0x08 campaign (`77bae4fe`)

The nine `edge-*` pairs all pass their respective strict criteria: four
exhaustion negatives, four 2-Mbps cyclic-size stress pairs with 2,000 MSDUs
per direction each, and a 100-MSDU/direction zero-injected-loss OFDM control.
Both initiation roles are repeated. The stress pairs deliver 8,000 MSDUs per
direction in total and retire 21 actual known-status incomplete attempts.
All 18 frozen binary endpoint records agree with JSON and the independent
suppression/count oracle reports **QUALIFICATION: PASSED** for this campaign.
It does not erase the two initial failed pairs or close overall qualification.

No `0x0008` observation occurs in these trials. Its earlier origin and
silicon meaning are therefore still unresolved; the guard has not been widened.

The original rejected-prefix diagnostic saved three matching fixture headers
on the bad-FCS path. **The inferred association with the rejected RF frame is
withdrawn**: the freshness investigation below reproduces stale-SHM attribution.
These historical values are not proof of truncation of the matching fixture:

| Endpoint evidence | Fixture index/sequence | Expected RXE bytes | Reported RXE bytes |
| --- | --- | --- | --- |
| `edge-stress-a2-init` | 896 | 2338 | 62 |
| `edge-stress-b1-init` | 1575 | 66 | 62 |
| `edge-stress-b2-reply` | 1743 | 66 | 62 |

All three prefixes contain the expected bound addresses and `MSK1` marker;
the saved sequence equals the saved fixture index. Independent decoding
checks their PLCP CRCs: the first declares 9,328 us, matching a 2,304-byte
MSDU at 2 Mbps, and the others declare 240 us, matching 32-byte MSDUs.
All report `status1=0x4042`, `status2=0x0040`.
These are matching SHM headers sampled at rejection, not proven current-frame
bytes. Valid PLCP CRCs and fixture indices do not establish header freshness.
These later passing runs also do not explain the earlier six-of-eight
negative, which has no matching rejected prefix.

The discriminating measurement is RX-end/FIFO/copied-length provenance
and explicit scratch freshness, ideally with an independent RF witness.
Changing promiscuous filtering, dropping FCS/NAV validation or accepting
unknown PHY bits would not establish that cause and has not been done.

Both `edge-SERIAL-restored.bin` full-flash readbacks match the original images
and SHA-256 values below. Both badges are confirmed in BOOTSEL; no campaign
or restoration process remains. Pico and shared infrastructure are untouched.

### Freshness correction and copy-length hypothesis

The legacy stream threshold is normally 128 bytes. A rejected frame below
that threshold has not necessarily supplied a current MAC header to SHM;
the final good-frame copy/publication path is not run for a rejection.
The former 40-byte diagnostic gate could therefore attribute previous
receive bytes to an unrelated rejection. A native counterexample fails
before the correction and passes after requiring the stream-enable bit
and its configured prefix threshold for Data-header attribution.
This is a necessary provenance gate, not full-frame CRC proof.

`CLEAN_RX_REJECT_PROBE=1` is an opt-in legacy-only probe. It poisons the first
66 SHM receive bytes before the prefix-copy request, replays the original
RX-start control instruction, and snapshots the first rejected frame's
backing bytes plus raw next-length (`0x102b`) and requested-copy (`0x102a`)
registers. It borrows 28 bytes of unused legacy HT-failure storage and detail
`1060..1088`; layout assertions prevent overlap. It does not advertise a
matching peer frame: `soak_bad_fcs` is null and `rx_reject_probe` explicitly
contains generic scratch evidence, including any unwritten poison.
The probe changes RX instruction timing, remains disabled by default and
is compile-time incompatible with HT RX.

The initial matching-only poison candidate `79e7b554`, before the new length
guard, retains seven pairs under `build/clean-rx/rxend-20261022/`.
Both exhaustion negatives, three 2,000-MSDU/direction cyclic stress pairs and
the 100-MSDU/direction OFDM control pass. One repeated stress pair again fails
with zero initial deliveries; it is retained, not excused. The independent
14-endpoint oracle reports 12,200 received MSDUs, six known-status retirements
and **QUALIFICATION: FAILED**.

Two poisoned snapshots in the last stress pair still show matching old
fixture bytes, but both report raw next-length `0x8000`, copy-register
**zero**, RXE count 62 and an unwritten `ffffffff` tail. The
32-byte fixture's full 66-byte backing sample fails the independent MAC FCS
check. These are not qualified complete current-frame receptions.

This motivated a hypothesis, not a proven controller defect: PC 155 rejects
only value `1`, whereas zero reaches the body-copy setup. An experimental
guard rejected every value below the six-byte PLCP prefix through the
existing bad-length/EIFS path. Its native test passed all 65,536 operands,
but that proved only the proposed branch logic, not its silicon premise.

Candidate `f06364cc` in `build/clean-rx/rxend-20261022/final/` failed **all five**
RF pairs: both exact-eight-suppression negatives, both cyclic controls
(2,000 and 100 MSDUs/direction), and a 100-MSDU/direction 32-byte control.
Every sender exhausted eight attempts, and no receiver accepted a Data frame
or exercised a suppression. The receiver probes instead froze at RXE count
six with raw next-length `0x8000`, copy register six and all bytes after the
prefix still poisoned. The proposed guard was therefore withdrawn.
These controls do not establish what the silicon does with zero; they
explicitly disprove treating the native guard test as RF qualification.
The final native regression preserves the original zero-permitted branch
for all 65,536 operands and checks the original upper bound, bad-length
accounting, EIFS, deferred access and no ACK/delivery on that discard path.
The raw register field is named `next_length`, not a verified decoded PLCP
length. Historical candidate JSON used the less precise `decoded_length`.

After withdrawing the guard, first-any probe build **`571d4b98`** passes the
same five controls in `build/clean-rx/rxend-20261022/no-guard/`:

- Both controlled negatives record exactly eight attempts, eight TIMEOUTs
  and eight eligible receiver suppressions, not merely an exhausted sender.
- The 2-Mbps cyclic controls deliver 2,000 MSDUs/direction with A initiating
  and 100 MSDUs/direction with B initiating.
- The 2-Mbps, 32-byte control delivers another 100 MSDUs/direction.
- The independent 10-endpoint binary/JSON oracle verifies the build ID,
  frozen fields, all 66 probe bytes and metadata, exact output shape and
  delivery/integrity/suppression invariants: **4,400 received MSDUs**, two
  known-status incomplete-TX retirements, 1,080 aggregate bad-FCS counts and
  **QUALIFICATION: PASSED** for these bounded controls.

Eight generic rejection samples are retained. The first two are unrelated
beacon-looking scratch with raw next-length `0x8000`, copy register zero
and RXE count 134; neither is labelled matching Data. Successful controlled
traffic with this unchanged zero-permitted branch reinforces the rejection
of the minimum-length hypothesis. None of these controls captures bit `0x08`;
its silicon cause remains unobserved. The initial startup failure and all
earlier failed controls remain recorded, and this instruction-adding probe
is not independent RF-timing or overall A2/A3 qualification.

Default build **`66bf3b9c`**, without the instruction-adding probe, also passes
all four controls in `build/clean-rx/rxend-20261022/default-final/`: both exact
eight-suppression negatives and both-role 100-MSDU/direction cyclic 2-Mbps
controls. Its eight-endpoint oracle verifies **400 received MSDUs**, one
known-status retirement, 109 aggregate bad-FCS counts and
**QUALIFICATION: PASSED** for this bounded matrix.

Passing does not mean the unexplained losses disappeared. Across the four
positive default endpoints there are **four TIMEOUTs** beyond the eight
intentional peer suppressions and one known-status retirement. In particular,
`cycle-b` B/initiator takes 104 attempts for 100 acknowledged MSDUs, with
four TIMEOUTs, two actual peer suppressions and no known-status retirement.
The peer has only the two expected MAC-level duplicates and no matching
streamed rejected-Data snapshot. Those duplicate counts are independently
checked against the frozen binary fields. They do not by themselves identify
RF loss versus ACK decoding or RX copy/handoff timing; ambient bad-FCS totals
are still not attributed to these missing attempts. Per-attempt current-frame
evidence and the unobserved bit-`0x08` origin remain open.

The initial restoration was interrupted when the USB hub disappeared.
After reconnection both full originals were reloaded, verified and independently
read back; `SERIAL-restored-retry.bin` matches the established originals and
their SHA-256 values below. That USB interruption is separate from the earlier
RF trial failures and is not asserted to explain them.
The withdrawn-guard, final-probe and final-default campaigns each subsequently
restore and verify both full originals independently. The final-default
readbacks again match both original SHA-256 values byte-for-byte, and both
badges are confirmed in BOOTSEL. The Pico and shared firmware are untouched.

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


