# Open feature and reliability questions

The C01-C08 cleanup reorganizes source ownership and enforces offline
compatibility. It does **not** establish parity with vendor firmware,
production radio reliability, or completion of the D2/D3 qualification
milestones. Do not infer new Wi-Fi/BT or power-management capabilities
from the badge hardware or a passing cleanup check.

## Unresolved or deliberately limited behavior

- Missing AP authentication replies and corrupt HT captures are not fully
  explained or repaired. Bounded RX staging and MAC-control fixes reduce
  hazards without proving a general receive-reliability solution.
  See [the AP/RX investigation](AP_AUTHENTICATION_INVESTIGATION.md).
- General negotiated HT operation is not enabled/qualified. Raw HT v1,
  controlled fixtures and the direct messenger are narrower interfaces,
  not a fully interoperable HT station.
  See [HT20 development](HT20_IMPLEMENTATION.md).
- General live station A-MPDU reception, production agreement/reorder
  integration and SIFS-timed Immediate Block ACK are not implemented.
  Finite retained-SHM and DMA/F2 fixtures do not close those gaps.
  See [Block ACK limits](BLOCK_ACK_IMPLEMENTATION.md) and
  [aggregate-interface evidence](AMPDU_INTERFACE_INVESTIGATION.md).
- Retry/completion ownership, ACK/CTS timing, fragmentation and recovery
  have bounded software/model and historical campaign evidence, with
  retained failures and qualification limits. Native success is not
  independent physical timing or loss evidence.
  See [MAC reliability](MAC_RELIABILITY_RECOVERY.md) and
  [legacy fragmentation](LEGACY_FRAGMENTATION_QUALIFICATION.md).
- Runtime command admission and the unsuppressed ACK path deliberately
  changed in C05. Later byte-preserving reorganizations do not qualify
  those changes on silicon.
  See [the outstanding C05 hardware gate](C05_DIAGNOSTIC_PROFILES.md#separate-hardware-acceptance-gate-not-run).

## Evidence limits

The C01 diagnostic baseline remains immutable. Ordinary runtime comparisons
use C05 instead, because C05 intentionally changed ordinary behavior.
Offline acceptance preserves these distinctions and never treats a
disabled/unselected model as an executed diagnostic pass.

The one-time C04 hardware check used one badge in receive-only scope,
restored its full flash backup and byte-verified the restoration; it did
not qualify TX, other devices, or later profile changes. No new C05-C08
flashing, device execution or RF qualification was performed.
Future campaigns require new authorization and matched host/controller
profiles, flags, hashes, independent evidence and backup/restore safeguards.
