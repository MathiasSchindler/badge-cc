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

### HT aggregate and Block ACK

**Status:** not integrated/qualified for general station operation.
Negotiated HT advertisement remains disabled. Raw HT v1, the direct messenger
and finite retained-SHM/DMA/F2 fixtures are narrower interfaces.
General live A-MPDU reception, agreement/key/reorder integration and
independently qualified SIFS-timed Immediate BA remain open.

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

**Status:** reproducible acceptance failure in the
[2026-10-24 regression](../experiments/HARDWARE_REGRESSION_2026-10-24.md#experimental-aggregate-and-ba-failures).
SHM/BA receivers exhaust the cumulative six-second loader budget before
completing their final snapshots, including fresh channel-1 controls.
Valid independently returned BA frames do not make the incomplete receiver
reports pass. DMA also exposed a separate controller `0x8d` fault.

**Next evidence:** correct and test the operation/snapshot deadline accounting
without removing bounded recovery, then rerun matched positive and bad-FCS
controls with valid frozen payload/lease evidence. Retain the DMA fault as
a separate investigation. General HT/BA and physical timing gates stay open.

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
