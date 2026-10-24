# Hardware regression follow-up work

Work items from the [2026-10-24 hardware regression](HARDWARE_REGRESSION_2026-10-24.md).
R01-R03 are implemented and validated offline and on hardware. The
[R03 rerun](HARDWARE_REGRESSION_R03_2026-10-24.md) performed R05's matrix,
but aggregate acceptance still has failures. R04 and R06-R10 remain pending.
This is the campaign-specific follow-up list, not a
replacement for the [functional roadmap](../../../todo.md).

Fix the diagnostic infrastructure before drawing further conclusions about
aggregate reliability.

| ID | Priority | Work item | Acceptance criteria |
| --- | --- | --- | --- |
| R01 | High | **Correct aggregate operation deadlines. Implemented; native and hardware validated.** Account for startup, the 4.2-second receive window, shutdown and report collection, not simply an arbitrary larger timeout. | Native tests cover slow startup and deadline boundaries; healthy aggregate runs finish within a documented finite budget. Cancellation and watchdog recovery remain bounded. |
| R02 | High | **Make fault snapshots work after an operation timeout. Implemented; native and hardware validated.** Use a separate, bounded recovery/snapshot allowance while preserving the original error. | Injected operation timeout still produces either a valid snapshot or an explicit snapshot failure; the original failure is never overwritten. |
| R03 | High | **Make incomplete evidence unambiguous. Implemented; native and hardware validated.** Distinguish unavailable capture/lease fields from valid zero values in raw reports, collector JSON and pair verification. | Missing, partial and stale snapshots cannot appear successful or imply "no reception." Tests reject misleading delivery counters and invalid lease evidence. |
| R04 | High | **Harden USB orchestration.** Replace fixed enumeration sleeps with bounded readiness checks; impose real subprocess deadlines on picotool and collectors. | Delayed enumeration, hung reboot, disconnect and cancellation terminate predictably, retain logs and stop dependent operations. No unbounded subprocess waits. |
| R05 | High | **Repeat aggregate acceptance after R01-R03. Matrix rerun; acceptance still failing.** Complete snapshots now distinguish intermittent SHM/BA loss and persistent DMA failures; both bad-FCS controls passed 4/4. | Independently validate delivered bytes, exact bitmaps, duplicate counts and terminal lease release. All-bad reception must complete with zero delivery and zero BA, not merely time out. Preserve every failed attempt. |
| R06 | High | **Investigate the separate DMA `0x8d` fault and missing final body.** Preserve the failing descriptor/header, buffer ownership and DMA completion state. | A regression test reproduces the fault mechanism; a subsequent fix passes independent payload checks. Do not count two-body delivery as three-body success. |
| R07 | Medium | **Investigate intermittent HT reception.** Start with the minimum-length Data, extended A-MSDU and clocked-ACK cells that failed initially. | Separate missing transmission, missing reception, corrupt capture and missing ACK using independent evidence. Report first-attempt results separately from successful repeats. |
| R08 | Medium | **Measure retry and host-stall behavior.** Investigate the 31 retried cross-board exchanges and networking stall events before changing retry policy. | Report per-link first-attempt success, retry distribution, latency and stall duration against predefined limits. Demonstrate whether stalls correlate with RF loss. |
| R09 | Medium | **Add diagnostic-selection preflight.** Detect missing feature gates and image/collector mismatches before starting a positive RF trial. | Fixed-probe tests require the explicit probe gate; intentional disabled-gate controls remain supported and clearly classified. |
| R10 | Follow-up | **Extend qualification beyond smoke tests.** Longer networking runs, repeated cold boots/recovery, broader channel/rate/length coverage and remaining runtime acceptance. | Agree measurable thresholds first; retain independent peer evidence. Physical SIFS timing, general HT/BA integration and production security remain separate gates. |

## Suggested sequence

Complete R01-R04 before R05. Investigate R06 independently, then address
R07-R10.

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

Retries, correctly rejected disabled commands and passing short security
trials are not themselves firmware defects. They respectively require
reliability characterization, better test selection and broader qualification.

Further hardware runs require fresh authorization under the
[hardware trial protocol](../guides/HARDWARE_TRIALS.md).
