# Current issues and qualification gaps

The structural cleanup reorganizes source ownership and enforces offline
compatibility. It does **not** establish parity with vendor firmware,
production radio reliability, or completion of the D2/D3 qualification
milestones. Do not infer new Wi-Fi/BT or power-management capabilities
from the badge hardware or a passing cleanup check.

## Ownership

This is the current problem/qualification register, not a lab log or a second
roadmap. Each topic identifies what is unresolved, the next evidence needed
and its canonical records. Update it when evidence changes the conclusion.
Detailed functional checklists and acceptance thresholds belong in the
[functional roadmap](../../../todo.md); structural work belongs in
[cleanup](../../cleanup.md). Historical proposals are preserved separately in
[qualification history](../history/QUALIFICATION_BOUNDARIES.md).

## Unresolved or deliberately limited behavior

### RX reliability and missing AP replies

**Status:** unresolved. Bounded staging and MAC-control fixes reduce hazards,
but do not fully explain missing AP authentication replies, corrupt HT captures
or pre-prefix losses.

**Next evidence:** distinguish lost reception, stale/corrupt capture and
host/report failures with matched independent controls; qualify the retained
fixes against [A3](../../../todo.md#a3-receive-correctness-and-interoperability),
not merely another successful finite fixture.

**Records:** [AP/RX controls](../experiments/rx/AP_AUTHENTICATION.md),
[isolated RF hints](../experiments/rx/RF_HINT_CONTROLS.md) and
[HT20 investigation sequence](../experiments/ht20/README.md).

The [R07 investigation](../experiments/HT_RECEPTION_R07_2026-10-24.md)
retains two additional target misses without positive independent TX proof.
The replacement passive source Pico positively calibrated HT observation:
12/12 initial target pairs and 1/1 fresh repeat passed, with 11/12 initial
independent HT captures. The Pico also missed one fixture that both target
endpoints proved successful. A silent observer is not proof of no RF;
passive legacy ACK acquisition remains uncalibrated.

Nonaggregate snapshots/oracles now reject incomplete/stale evidence and
separate delivery, corrupt capture and ACK failure stages. Post-run analysis
also found legacy beacon entries overwriting HT timing/optional ACK records.
The separate [32-trial loss-stage follow-up](../experiments/HT_ROOT_CAUSE_2026-10-24.md)
RF-tested that repair: all 92 snapshots/timing records survived. Initial
target acceptance was 9/12, followed by 16/16 additional fresh ACK cells;
three B-to-A MCS7 misses remain. Both receivers retained matching early
HT-SIG/bad-FCS candidates but no correlated F2. The rejected ACK60 SHM
record does not prove complete current-frame copy or on-air corruption.
Next evidence must establish rejected-byte copy provenance and distinguish
TX waveform failure from shared source-built RX behavior. Independent ACK
acquisition remains uncalibrated; reliability and R07 acceptance remain open.

The opt-in [rejected ACK60 copy-state trace](../guides/HOST_TOOLS.md#rejected-ack60-copy-state-trace)
now freezes the first eligible record in either receive role and binds raw copy
register operands to its cause, TSF and RXE count. Native tests cover passive
capture, stable stale SHM, immutable first records and raw/JSON disagreement.
The [eight-attempt investigation](../experiments/HT_REJECT_COPY_2026-10-25.md)
reproduced the B-to-A miss but found that the new probe overlapped derived ACK
state in both SHM and SRAM. Its copy-state evidence is **not qualified**.
Both flashed prototypes and all failures are retained; all originals were
restored. Four subsequent corrected-layout controls delivered with returned ACKs
and passive Pico observations; all eight receiver traces stayed correctly zeroed.
The subsequent [controlled bad-FCS calibration](../experiments/HT_REJECT_BAD_FCS_2026-10-25.md)
retained a bound active rejection whose complete MAC bytes match the programmed
fixture, including exactly the deliberately changed FCS bit. This exercises
the active arm for one control, not general copy freshness or the radio root
cause. The passive Pico retained no corresponding rejection.
That complete matching 66-byte control is below the ordinary 128-byte streaming
threshold: the threshold and stable register operands alone prove neither
staleness nor freshness. The subsequent
[normal-traffic campaign](../experiments/HT_SPONTANEOUS_REJECTION_2026-10-25.md)
stopped on a fifth-attempt B-to-A miss with the calibrated, injection-disabled
images. Its active rejection binds to the copy record, but 54/60 retained MAC
bytes differ from the fixture and complete MAC identity fails; only a six-byte
fresh challenge run matches. General freshness remains unproven. The Pico
also missed an earlier frame that the target received and ACKed, so its
silence cannot establish transmitter silence. Full originals were restored
and independently verified. Next establish per-byte copy provenance and
qualify independent observation; this trace alone cannot localize TX versus
shared RX failure or close D2 work package 1.

The subsequent [initial-SHM comparison](../experiments/HT_REJECT_BASELINE_2026-10-25.md)
adds a default-off host-only stopped-MAC baseline; controller/PSM bytes are
unchanged. After a byte-exact negative control, attempt eight reproduced a
normal **A-to-B** miss with bound active and passive rejections. They share
32 incorrect MAC bytes, including a 15-byte run, but differ at thirteen
offsets. Every retained MAC byte differs from its own initial baseline in
both receivers: untouched startup SHM does not explain this miss. Both
requested fresh receiver addresses match, but complete MAC identity and
general freshness still fail. This exercises the corrected passive arm,
not byte-exact passive qualification. The campaign stopped and all originals
were restored/verified. Next separate intervening-copy/common RX behavior
from TX/RF with stronger per-candidate provenance and independent observation;
do not apply an unproven startup-clearing fix.

The new default-off [per-candidate pre-copy diagnostic](../experiments/HT_CANDIDATE_TRACE_2026-10-25.md)
snapshots the previous receive bytes before prefix-copy setup and freezes them
with the first rejection. Native PSM, report, startup and independent decoder
tests pass. Its first authorized attempt stopped on unavailable receiver
snapshots; missing initialization of the diagnostic's own SHM was repaired
offline. This is not a receive-buffer clearing fix or RF-qualified probe.
All originals were restored and independently verified; the failed attempt
is sealed. A separately authorized
[repaired-image campaign](../experiments/HT_CANDIDATE_REJECTION_2026-10-25.md)
stopped on its first normal control failure with valid snapshots and two bound
rejections. None of the active/passive incorrect MAC bytes equalled its own
pre-command snapshot. Both retained the requested FCS but neither complete
identity/body CRC validates. This narrows unchanged-preimage explanations,
not general copy or RF provenance. No positive/negative RF calibration
passed; probe timing perturbation remains possible. All originals were again
restored, fully verified and left in BOOTSEL. D2 package 1 remains open.

The subsequent [matched crossover](../experiments/HT_PROBE_CROSSOVER_2026-10-25.md)
keeps TX and Pico probe-off and changes only the active receiver's candidate
probe. Both states delivered complete bodies in 3/4 normal attempts. A
probe-off identity-correlated rejection has 15 incorrect MAC bytes; another
probe-off trial delivered the full body and started an ACK that the transmitter
did not acquire. Thus the new probe is not necessary for either failure stage,
but the small test does not establish that it is harmless. Three deliberate
bad-FCS fixtures were retained byte-exactly by the passive Pico, including two
simultaneous active/passive matches. Probe-on receiver B has a byte-exact
negative control; receiver A's counterpart remains missing. This is limited
physical-device calibration, not independent receiver-code or loss-free
observation qualification. All twelve attempts/36 reports are sealed and all
originals restored/full-verified in BOOTSEL. TX/RF versus shared RX/copy and
the independently unwitnessed ACK path remain unresolved.

### HT aggregate and Block ACK

**Status:** not integrated/qualified for general station operation.
Negotiated HT advertisement remains disabled. Raw HT v1, the direct messenger
and finite retained-SHM/DMA/F2 fixtures are narrower interfaces.
General live A-MPDU reception, agreement/key/reorder integration and
independently qualified SIFS-timed Immediate BA remain open.

The [secure BA software lifecycle](../guides/HOST_TOOLS.md#d2-secure-block-ack-software-lifecycle)
now binds reorder/stream storage to WPA key transitions, aborts and timeouts,
with deferred callback cleanup and pressure-safe repeated software PPDUs.
This does not add a station dispatcher or a controller BA-install/revoke
backend, and does not qualify general aggregate reception.
The default-off [repeated retained-SHM prototype](../guides/HOST_TOOLS.md#repeated-retained-shm-handoff-offline-prototype)
now rearms terminal leases after bounded SRAM admission, with native
controller/PSM pressure and wrap coverage. Its exported native execution
transcripts now pass an independent multi-PPDU model oracle, including
complete fixture bytes/FCS, lease transitions and tampered-transcript controls.
It retains the fixed fixture contract; live multi-PPDU collection, repeated
RF delivery and general RXE completion remain unqualified. Finite-fixture
acceptance explicitly rejects its marker.

**Next evidence:** follow the [D2 work packages](../../../todo.md#remaining-work-packages)
and [D3 acceptance](../../../todo.md#d3-ht-mac-and-performance).
Do not treat repeated fixed-slot results as N-body, secure lifecycle or
interoperability qualification.

**Records:** [Block ACK software](../history/BLOCK_ACK_SOFTWARE.md),
[aggregate delivery and returned BA](../experiments/ampdu/INTERFACE_AND_DELIVERY.md),
[variable-length and recovery controls](../experiments/ht20/NEXT_MPDU_AND_VARIABLE_LENGTH.md).

### MAC completion, ACK timing and fragmentation

**Status:** bounded fixes and tests exist; broader timing, loss and recovery
qualification remains open. Native/model success does not establish physical
ACK/CTS timing or explain every retained PHY/RX failure.

**Next evidence:** meet [A2](../../../todo.md#a2-reliable-acks-and-retransmissions)
and [legacy qualification](../../../todo.md#c3-ratesize-qualification-matrix),
retaining failed controls and independent physical evidence.

**Records:** [completion/recovery](../experiments/mac/COMPLETION_AND_RECOVERY.md),
[fragmentation](../experiments/mac/LEGACY_FRAGMENTATION.md) and
[EDCA/WMM/protected receive](../experiments/mac/EDCA_AND_WMM.md).

### Retry, desktop pauses and controller startup

**Status:** R08 measurements are implemented; acceptance remains partial.
The [R08 campaign](../experiments/RETRY_AND_STALL_R08_2026-10-24.md)
accounted for the 31 historical retried exchanges and completed 120/120 fresh
raw exchanges, 117 first-attempt. A passing 30-second pause-disabled network
pair still retried without any >=250 ms bound service gap. Historical
`host_stalls` were deliberate desktop polling pauses, not firmware stalls.

The pause-enabled station then failed at controller startup (loader stage 5,
error 5, mailbox state `0xff`) before networking or any station desktop pause.
The campaign stopped without repeats; both badge-B network controls were not
run. All original flashes were independently verified restored.

**Next evidence:** obtain new authorization to investigate that startup fault
and complete loaded pause-off/on comparisons on both badges. Independent
physical RF-loss evidence is still needed; MAC ACK timeouts and application
retries alone do not locate RF loss. Truncated event timelines are explicitly
unavailable for whole-run alignment. Do not change retry policy on this basis.

### Runtime profile hardware qualification

**Status:** partially exercised; the full acceptance gate remains open.
The [2026-10-24 regression](../experiments/HARDWARE_REGRESSION_2026-10-24.md)
exercised runtime 6 Mbps OFDM RX/F2 and TX/echo on both badges and the Pico.
C05 deliberately changed command admission and the unsuppressed ACK path;
this bounded raw-traffic result does not qualify all of that behavior.

**Next evidence:** obtain fresh authorization and execute the
[runtime profile acceptance procedure](../guides/HARDWARE_TRIALS.md#runtime-profile-acceptance).
The earlier one-device C04 receive-only test does not close this gate.

### Aggregate diagnostic host deadline and incomplete reports

**Status:** the original deadline/report-validity defects are fixed; aggregate
acceptance still has separate reliability failures. The
[2026-10-24 regression](../experiments/HARDWARE_REGRESSION_2026-10-24.md#experimental-aggregate-and-ba-failures)
recorded SHM/BA receivers exhausting the cumulative six-second loader budget before
completing their final snapshots, including fresh channel-1 controls.
Valid independently returned BA frames do not make the incomplete receiver
reports pass. DMA also exposed a separate controller `0x8d` fault.

The [R01 implementation](../experiments/todo.md#r01-implementation) now budgets
healthy aggregate reception and completion separately from the startup limit,
with native boundary/cancellation coverage and serviced watchdog recovery.
The [R02 implementation](../experiments/todo.md#r02-implementation) adds a separate,
fixed one-second fault-readback allowance and an explicit snapshot result,
while retaining the original operation failure and cancellation behavior.
The [R03 implementation](../experiments/todo.md#r03-implementation) adds
per-section validity, exact nonce/controller-generation binding and explicit
zero/missing receipt records, with exclusive aggregate report ownership.
The [hardware rerun](../experiments/HARDWARE_REGRESSION_R03_2026-10-24.md)
produced complete valid snapshots for all 48 final pairs on both endpoints,
including failed operations. Bad-middle and all-bad controls passed 4/4 each.
R03 initial strict acceptance was SHM 17/18, normal BA 16/18 and DMA 0/4;
fresh repeats did not clear every failure. DMA still exposed `0x8d` and
missing-third-body behavior in that R03 campaign.

**Next evidence:** investigate R07 reception and remaining intermittent fixture/BA
loss without weakening exact-byte, receipt or terminal-lease acceptance.
The subsequent [R05 bounded acceptance rerun](../experiments/HARDWARE_ACCEPTANCE_R05_2026-10-24.md)
completed all 44 authorized SHM/BA/bad-FCS cells: SHM 18/18, BA 16/18 initially
and both bad-FCS variants 4/4. Both failed BA cells passed their one fresh
repeat, with complete independently validated delivery/lease/BA evidence.
Initial empty/nonmatching captures remain evidence of intermittent reception;
this does not close R07 or qualify loss-free operation. DMA was not retested
in R05.

The [R06 DMA repair](../experiments/DMA_REPAIR_R06_2026-10-24.md) fixes the
format-zero `0x8d` rejection and the missing next-MPDU command/layout.
The first repaired image passed all six MCS0/3/7 direction cells; the exact
final image passed 5/6 initially and its one fresh repeat. Bad-middle and
all-bad DMA controls passed 4/4 each. First-fault descriptor/header,
ownership and completion evidence is retained in a generation-bound record.
One final-image DMA cell and one unchanged-SHM control had empty captures
before passing their fresh repeats; those reception failures remain open,
not evidence of loss-free operation. Both original badge flashes were
restored and verified after the 31-trial campaign.

R04 supplies
[shared bounded orchestration](../guides/HOST_TOOLS.md#bounded-usb-orchestration)
with offline failure-path tests; this is distinct from the earlier temporary
campaign guards and supplies no new RF evidence. General HT/BA and
physical timing gates stay open.

### Badge silicon identification

**Status:** exact badge-specific ordering code/version/revision has not been
established by the family datasheet or implementation naming.

**Next evidence:** establish a reproducible identification with provenance
before making badge-specific revision claims.
The [original identification note](../history/HARDWARE_BRINGUP.md#badge-silicon-identification)
records the uncertainty; LCN20, Radio 20691 and Arch15 names alone are not proof.

### Other functional work

Station lifecycle/soaks, full legacy-rate interoperability, WPA2/rekey and
hardware-crypto requirements remain owned by the
[functional roadmap](../../../todo.md), not by copied TODO lists here.
Consult its topic acceptance criteria before interpreting bounded experiments
as completion.

## Evidence limits

The C01 diagnostic baseline remains immutable. Ordinary runtime comparisons
use C05 instead, because C05 intentionally changed ordinary behavior.
Offline acceptance preserves these distinctions and never treats a
disabled/unselected model as an executed diagnostic pass.

The one-time C04 hardware check used one badge in receive-only scope,
restored its full flash backup and byte-verified the restoration; it did
not qualify TX, other devices, or later profile changes. The later
[2026-10-24 campaign](../experiments/HARDWARE_REGRESSION_2026-10-24.md)
adds bounded RX/TX evidence and records failed diagnostic acceptance;
it does not close all runtime, reliability or timing gates.
Any new campaign follows [the hardware protocol](../guides/HARDWARE_TRIALS.md);
no document reorganization supplies authorization or new RF evidence.
