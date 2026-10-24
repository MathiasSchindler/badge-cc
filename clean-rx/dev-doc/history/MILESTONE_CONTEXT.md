# Milestone definitions and scope snapshot

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

This is the detailed roadmap record preserved on 2026-10-17 before separating
milestone status from implementation details. Build results, findings and
experiment descriptions below are historical evidence, not new qualification.
Commands and artifact paths are relative to the repository root.
The current milestone checklist is [todo.md](../../../todo.md).

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
are not yet qualified. See [current status and limitations](../../badge/README.md).

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


