# Hardware regression follow-up work

Work items from the [2026-10-24 hardware regression](HARDWARE_REGRESSION_2026-10-24.md).
R01-R03 are implemented and validated offline and on hardware. The [R03 rerun](HARDWARE_REGRESSION_R03_2026-10-24.md) exposed separate
aggregate failures. R04 is implemented with offline failure-path validation
and was used in the completed
[R05 acceptance rerun](HARDWARE_ACCEPTANCE_R05_2026-10-24.md):
42/44 initial passes, then 2/2 passing fresh repeats. Intermittent reception
remains unresolved. R06 is implemented and bounded-hardware validated.
The [R07 investigation](HT_RECEPTION_R07_2026-10-24.md) is implemented and
its 44-trial campaign and separate
[32-trial loss-stage follow-up](HT_ROOT_CAUSE_2026-10-24.md) complete; reliability/independent-ACK acceptance
remains open. [R08 measurement tooling](RETRY_AND_STALL_R08_2026-10-24.md)
is implemented; its bounded campaign stopped after a network startup failure.
Loaded pause comparisons and physical RF-loss correlation remain open.
R09-R10 remain pending.
This is the campaign-specific follow-up list, not a
replacement for the [functional roadmap](../../../todo.md).

Fix the diagnostic infrastructure before drawing further conclusions about
aggregate reliability.

| ID | Priority | Work item | Acceptance criteria |
| --- | --- | --- | --- |
| R01 | High | **Correct aggregate operation deadlines. Implemented; native and hardware validated.** Account for startup, the 4.2-second receive window, shutdown and report collection, not simply an arbitrary larger timeout. | Native tests cover slow startup and deadline boundaries; healthy aggregate runs finish within a documented finite budget. Cancellation and watchdog recovery remain bounded. |
| R02 | High | **Make fault snapshots work after an operation timeout. Implemented; native and hardware validated.** Use a separate, bounded recovery/snapshot allowance while preserving the original error. | Injected operation timeout still produces either a valid snapshot or an explicit snapshot failure; the original failure is never overwritten. |
| R03 | High | **Make incomplete evidence unambiguous. Implemented; native and hardware validated.** Distinguish unavailable capture/lease fields from valid zero values in raw reports, collector JSON and pair verification. | Missing, partial and stale snapshots cannot appear successful or imply "no reception." Tests reject misleading delivery counters and invalid lease evidence. |
| R04 | High | **Harden USB orchestration. Implemented; offline failure-path validated.** Replace fixed enumeration sleeps with bounded readiness checks; impose real subprocess deadlines on picotool and collectors. | Delayed enumeration, hung reboot, disconnect and cancellation terminate predictably, retain logs and stop dependent operations. No unbounded subprocess waits. |
| R05 | High | **Repeat aggregate acceptance after R01-R04. Completed bounded rerun.** SHM 18/18, BA 16/18 initially plus 2/2 passing fresh repeats, both bad-FCS controls 4/4. All 44 cells have a strict passing attempt; initial failures remain retained, not loss-free acceptance. DMA was excluded under R06. | Independently validate delivered bytes, exact bitmaps, duplicate counts and terminal lease release. All-bad reception must complete with zero delivery and zero BA, not merely time out. Preserve every failed attempt. |
| R06 | High | **Repair the separate DMA `0x8d` fault and missing final body. Implemented; bounded hardware validated.** Correct format-zero admission, the DMA next-MPDU command and its explicit physical layout; freeze descriptor/header/ownership/completion state. | Native regressions reproduce the rejected header and wrong command/layout. Independent three-body checks pass at MCS0/3/7 both directions, with bad-middle/all-bad controls. Retained empty captures and fresh repeats are not loss-free acceptance. |
| R07 | Medium | **Investigate intermittent HT reception. Implemented; bounded campaigns complete, acceptance still open.** Strict frozen/F2 evidence and a calibrated passive source Pico cover the original fixture classes. The ownership repair is RF-tested. Original misses and three new B-to-A MCS7 misses remain unresolved; rejected-byte freshness, TX versus common RX behavior and independent ACK acquisition remain open. | Separate missing transmission, missing reception, corrupt capture and missing ACK using independent evidence. Report first-attempt results separately from successful repeats; do not turn absent/uncalibrated observations into zero RF. |
| R08 | Medium | **Measure retry and host-stall behavior. Implemented; partial bounded validation.** The 31 historical retries are accounted for; 120/120 fresh raw exchanges and one network control passed. A retained controller startup failure stopped the remaining matrix. Desktop pause and device service-gap measurements are separate; retry policy is unchanged. | Report per-link first-attempt success, retry distribution, latency and stall duration against predefined limits. Demonstrate whether stalls correlate with RF loss; loaded pause comparisons and independent RF-loss correlation remain open. |
| R09 | Medium | **Add diagnostic-selection preflight.** Detect missing feature gates and image/collector mismatches before starting a positive RF trial. | Fixed-probe tests require the explicit probe gate; intentional disabled-gate controls remain supported and clearly classified. |
| R10 | Follow-up | **Extend qualification beyond smoke tests.** Longer networking runs, repeated cold boots/recovery, broader channel/rate/length coverage and remaining runtime acceptance. | Agree measurable thresholds first; retain independent peer evidence. Physical SIFS timing, general HT/BA integration and production security remain separate gates. |

## Suggested sequence

R01-R05 are complete within their recorded scopes. Use the shared bounded
orchestration for further authorized trials. R06's bounded DMA repair is
also complete; address R07-R10 without treating a successful finite fixture
as general qualification.

## R01 implementation

Aggregate RX retains the original six-second startup limit. Only after
successful startup and BA setup does it admit the complete receive operation,
with a fixed total budget of 11.4 seconds from loader initialization:
six seconds startup, 4.2 seconds reception, 0.2 seconds stop and one second
of report-readback reserve. It does not reset elapsed time or renew the
operation deadline on each service call. Other modes keep their existing
budgets, and the desktop collector retains its 15-second alarm.

Aggregate RX now uses the existing long-operation watchdog refresh: rearm
eight-second BOOTSEL recovery every four seconds while servicing USB.
Cancellation still aborts loader work. Operation expiry cannot renew the
operation deadline; only the separately bounded R02 fault readback may
resume servicing, so recovery cannot be refreshed indefinitely.

The existing normal and strict loader suites exercise slow startup up to
one microsecond before expiry, full receive/stop/readback, exact startup
and total deadline boundaries, timer wrap and cancellation in each phase.
This is offline validation, not a new RF result. Timeout-safe fault snapshots
are covered by R02 below; incomplete-report validity is covered by R03. The original
[hardware regression](HARDWARE_REGRESSION_2026-10-24.md) is unchanged.

## R02 implementation

Failed operations now use an independent loader context with one second
for fault readback, including any attempted stop. Its deadline is fixed
at snapshot entry, not renewed by individual reads, stop retries or
service calls. Healthy operations retain their original deadline.
The original error, address, actual value, stage and verification count
remain intact even when readback times out, fails on the bus or is cancelled.
An already cancelled operation performs no recovery I/O; cancellation
during recovery stops further access. Snapshot completion checks the
deadline again so a late final transfer cannot be reported complete.

R02 introduced a 2,068-byte badge/collector report: the original 2,048 bytes
remain at the same offsets, followed by an explicit snapshot result.
It distinguishes completed readback, failed readback and the deliberately
skipped generic snapshot in lifecycle/network modes. Failures carry their
own error/address/actual values and produce a collector stderr diagnostic,
without replacing the operation failure. Offline readers and paired
station verifiers accept both the historical and extended report sizes.
See the [host-tool format](../guides/HOST_TOOLS.md#fault-snapshot-recovery).

Normal and strict native loader tests inject operation expiry and cover
successful readback, partial bus failure, recovery expiry at the exact
boundary, a late final transfer, timer wrap, cancellation and preservation
of each original failure class. Collector inspection tests exercise the
explicit failure diagnostic and reject malformed/truncated result records.
R03 below adds capture freshness and lease/evidence validity checks.
Neither native result establishes RF acceptance. No hardware run was
performed as part of R02.

## R03 implementation

Current 2,092-byte reports extend the R02 trailer with complete-read section
bits, the exact request nonce and the controller generation. A section is
marked available only after its complete read; failed mailbox, multi-capture,
aggregate and interface reads clear their partial destination. Snapshot
status, frozen-state evidence, mailbox identity/generation and nonce binding
are checked before interpreting aggregate fields. Failed or stale evidence
produces explicit availability flags and JSON `null`, not zero delivery or
an empty lease presented as a measurement.

Aggregate RX initializes its delivery record only after startup succeeds
and the receive consumer begins. This makes a completed zero-delivery
control distinct from an absent record. Reported totals must agree with
the independently checked payload bitmap and duplicate count.

The paired runner now uses the independent
[`ampdu_verify.cjs`](../../tools/ampdu_verify.cjs) oracle. It checks both raw
reports against the fresh challenge, complete/frozen readback, exact
payloads/FCS, good/bad retained bodies, delivery counters and applicable
lease/BA state. All-bad acceptance requires actual fresh bad-FCS bodies,
zero delivered bodies, terminal release and zero transmitted/returned BA;
a missing receiver report cannot substitute for that control.

Normal/strict native and collector/oracle tests cover incomplete and stale
snapshots, partial-read clearing, wrong counters, invalid release evidence
and valid zero-delivery controls. Historical 2,048/2,068-byte files remain
readable by generic inspectors; they cannot pass the new aggregate oracle.
The user separately authorized a full hardware regression after R03,
including experimental aggregate/DMA diagnostics and full-flash restoration.

That [rerun](HARDWARE_REGRESSION_R03_2026-10-24.md) completed. It also found
and fixed a generic-packet/BA-record buffer overlap: aggregate modes now own
their report regions exclusively, and BA senders initialize explicit zero-count
receipt records. Missing receipts remain `null`, not evidence of no BA.
All 48 final aggregate pairs had valid complete readback on both endpoints;
all-bad controls passed 4/4. SHM/BA intermittency and DMA failures remain
separate from report validity. Every original flash was restored and verified.

## R04 implementation

The repository runners now share a dependency-free Node subprocess supervisor
and a libusb readiness probe. BOOTSEL discovery, reboot, application protocol
readiness, flashing and collectors all have finite deadlines. Readiness
checks configure USB if needed and read `LIVE_USB_INFO` without starting RF.
Both endpoints are prepared and immediately rechecked before paired radio
windows start; fixed USB enumeration sleeps are removed. Deliberate radio
staging delays remain distinct from enumeration.

Timeouts and cancellation terminate the owned child process group with
TERM/KILL escalation, retain endpoint/readiness/boot logs and suppress
dependent commands. Pair failures stop the other endpoint; matrices stop
at the failed trial. Expected wrong-password exit 1 remains distinct from
timeout 124. The legacy WPA2 entry point delegates to the hardened runner
instead of maintaining another USB startup implementation.

Offline tests inject delayed enumeration, protocol mismatch, a vanished
endpoint, hung info/reboot, failed flashing, cancellation during flashing
and collection, TERM-resistant descendants and disconnects. They exercise
all runner entry points, evidence preservation, credential cleanup and
known negative-control exits. The real C readiness probe is tested against
USB stubs and built against existing libusb. No hardware was accessed or
reflashed for R04; this does not add an RF qualification result.
See [bounds and operator behavior](../guides/HOST_TOOLS.md#bounded-usb-orchestration).

## R05 implementation

The [authorized R05 campaign](HARDWARE_ACCEPTANCE_R05_2026-10-24.md)
used R04's repository runners for 44 SHM/BA/bad-FCS cells and two fresh
repeats. Independent raw/JSON verification confirmed exact payloads, bitmaps,
duplicates and terminal leases for every accepted cell. All-bad controls
observed the actual three bad bodies and completed with zero delivery/BA.
All 92 endpoint snapshots were valid and all 46 challenges unique.

Two initial BA reception failures remain retained: an empty A-to-B MCS1
capture and three nonmatching retained observations B-to-A at MCS7.
Both fresh repeats passed; this is not a 44/44 first-attempt reliability
result. R06 DMA and R07 reception investigation remain separate.
Both complete original badge flashes were restored, independently reread
and byte/hash verified; both badges were left in BOOTSEL. The Pico was
not accessed.

## R06 implementation

The [R06 investigation](DMA_REPAIR_R06_2026-10-24.md) isolated two defects:
valid aggregate PHY format 0 was rejected as `0x8d`, and DMA continuation
used body-start `0x83` instead of next-MPDU `0xc3`. That transition retains
a six-byte zero prefix, now checked explicitly with native length 86 and
header tag `0xda16`. Complete MAC/FCS checks and exact three-body acceptance
remain strict. No SHM substitution or crypto-reset bypass was used.

The controller freezes first-fault descriptor/header, ring indices, DMA
status and PSM/RXE/copy state before reclamation/reset, with a generation-bound
record. The badge/collector preserve it without enlarging the report.
Native regressions include the actual retained continuation and stale-state
rejection; unaffected PSM/ABI/source-boundary checks remain in force.

The authorized campaign used 31 trials. The first repaired image passed
all six MCS0/3/7 direction cells. The exact final image passed 5/6 initially
and its one fresh repeat; bad-middle/all-bad controls passed 4/4 each.
The unchanged-SHM PSM control passed 3/4 initially plus its one fresh repeat.
Both empty captures remain R07 evidence, not first-attempt success.
All 18 distinct repaired/control cells have independent passing evidence;
24 accepted pairs and all seven failed attempts are retained.
Both complete original badge flashes were independently restored and
byte/hash verified; both badges were left in BOOTSEL.

## R07 implementation and bounded investigation

The [canonical R07 record](HT_RECEPTION_R07_2026-10-24.md) retains all 44
authorized trials and separates initial cells, fresh repeats, the failed
vendor-witness calibration and the source-built Pico phase. Nonaggregate
snapshots/offline verification now reject stale, incomplete and mismatched
evidence. A receive-only, headless Pico build independently observed fresh
HT fixtures without transmitting responses.

Target results were 11/12 initially, 11/12 in the separate fresh follow-up,
1/1 during failed vendor calibration, then 12/12 with the source observer
plus its 1/1 fresh repeat. Omission controls remain separate. Both complete
badge flashes and the Pico original were independently restored and verified.
No first-attempt failure was erased by a later success.

The final native-tested repair also prevents legacy beacon-table writes
from corrupting nonaggregate HT timing/optional ACK records. That exact
final image was not RF-run after the trial budget was exhausted.
Independent passive ACK acquisition was not positively calibrated; the
original misses still cannot be attributed to TX or RX alone. R07's
reliability/independent-ACK acceptance stays open, with no additional
hardware authorization implied.

The separately authorized [loss-stage follow-up](HT_ROOT_CAUSE_2026-10-24.md)
subsequently used all 32 new slots and RF-tested the ownership repair, with
92 valid snapshots/timing records and verified restoration of all originals.
Initial acceptance was 9/12; four additional ACK matrices passed 16/16.
Three B-to-A MCS7 misses had matching early HT-SIG/bad-FCS candidates on both
receivers but no correlated F2. The ACK rejection's stable SHM bytes lack
complete current-copy provenance. Root cause and independent ACK acquisition
remain open; this is not a firmware reliability fix.

## Further qualification

Retries, correctly rejected disabled commands and passing short security
trials are not themselves firmware defects. They respectively require
reliability characterization, better test selection and broader qualification.

Further hardware runs require fresh authorization under the
[hardware trial protocol](../guides/HARDWARE_TRIALS.md).
