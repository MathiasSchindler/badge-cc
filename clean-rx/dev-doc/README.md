# clean-rx development documentation

This directory is for contributors tracking implementation work, experiments,
failures, open questions and qualification. Its historical records describe
the builds and conditions named in each entry, not necessarily the current
firmware.

The firmware and chip reference in `doc/` describes implemented behavior
only. Development procedures, validation, history, hypotheses, bugs, open
questions and requested changes belong here, not in that reference.

- [Development and offline acceptance](DEVELOPMENT.md): toolchain,
  native tests, validation commands, documentation scope/review and boundary
  enforcement.
- [Open feature and reliability questions](STATUS.md): unresolved behavior,
  missing capabilities and hardware qualification.
- [HT20 diagnostic development notes](HT20_DIAGNOSTIC_NOTES.md): copy,
  streaming and aggregate investigations moved out of the interface reference.
- [Host tools and paired WPA2 trials](HOST_TOOL_USAGE.md): desktop collection,
  private credential handling and paired hardware procedures.

- [C01 cleanup inventory and baseline](C01_BASELINE.md): file ownership,
  profile catalogue, dependency boundaries, reproducible offline capture,
  frozen hashes and explicit test-coverage gaps.
- [C02 controller source isolation](C02_ISOLATION.md): local types/byte
  helpers, standalone production builds and before/after controller/PSM
  comparison across retained reference selections.
- [C03 native test separation](C03_TEST_SEPARATION.md): unit/model versus
  badge/peer integration ownership, named diagnostic profiles, compatibility
  entry points and production builds without a test tree.
- [C04 contract and fixture separation](C04_CONTRACT_SEPARATION.md): portable
  operations versus wire/layout contracts, explicit diagnostic codecs,
  host-owned crypto/fixtures and frozen ABI compatibility checks.
- [C05 diagnostic profiles](C05_DIAGNOSTIC_PROFILES.md): ordinary versus
  diagnostic command/ACK behavior and the outstanding hardware gate.
- [C06 controller composition](C06_CONTROLLER_COMPOSITION.md): explicit
  state ownership and byte-preserving controller/PSM unity composition.
- [C07 tool/profile ownership](C07_TOOL_AND_PROFILE_OWNERSHIP.md): central
  selections, firmware-only targets and development-tool migration.
- [C08 final acceptance record](C08_OFFLINE_ACCEPTANCE.md): boundary
  mutation tests, final matrix results and unperformed hardware gates.
- [Cleanup roadmap](../cleanup.md): completed cleanup summaries and pending
  structural work; this is development documentation, not firmware reference.
- [HT20 implementation and investigations](HT20_IMPLEMENTATION.md):
  D1/D2/D3 milestones, API development, receive failures, contention
  diagnostics, watchdog work, association/runtime peer profiles and
  remaining qualification.
- [Hardware development notes](HARDWARE_DEVELOPMENT.md): the former mixed
  architecture/reference document, preserving hardware bring-up context,
  RX streaming investigations, CCK truncation and MAC experiments.
- [Host integration notes](HOST_INTEGRATION_NOTES.md): the former mixed
  integration guide, preserving transport experiments, qualification
  history and hardware-crypto procedures.
- [Project work tracker](../../todo.md): milestones and outstanding work.
- [Milestone history](MILESTONE_HISTORY.md): implementation details,
  measurements and experiment records moved out of the root tracker.
- [Bounded EDCA implementation](EDCA_IMPLEMENTATION.md): controller policy,
  per-category access, native tests, paired RF matrix and remaining D2 gates.
- [Bounded Block ACK software](BLOCK_ACK_IMPLEMENTATION.md): agreement,
  delimiter-stream, ciphertext reordering and WPA2 native integration;
  outstanding PSM aggregate capture and SIFS-timed BA requirements.
- [Finite A-MPDU radio investigation](AMPDU_RADIO_INVESTIGATION.md):
  delimiter-stripped first-MPDU capture, bounded diagnostic evidence,
  unsuccessful continuation trials and remaining aggregate/EOF requirements.
- [Next-MPDU/aggregate-terminal interface](AMPDU_INTERFACE_INVESTIGATION.md):
  separate early/terminal events, fixed one-copy handoff retaining all
  three diagnostic MPDUs at all MCS in both directions, controlled
  bad-FCS continuation, passive timelines and remaining live/BA limits.
- [AP authentication and RX staging investigation](AP_AUTHENTICATION_INVESTIGATION.md):
  independently observed missing replies, timing/rate controls, bounded
  station receive FIFO, legacy regressions, matched-amplitude HT replays
  and rejected authentication capture.
- [Bounded legacy fragmentation qualification](LEGACY_FRAGMENTATION_QUALIFICATION.md):
  reliable-TX fragment admission, two-/sixteen-fragment maximum-MSDU fixtures,
  forced ACK loss, independent CRC checks, retained RF failures and A2/A3 limits.
- [MAC completion ownership and timeout recovery](MAC_RELIABILITY_RECOVERY.md):
  fired-request cancellation races, per-submission completion evidence,
  response-retirement closure, qualified timer reference, PHY-start/held-RX
  observations, labelled live fault snapshots, bounded completion waits,
  both-role closing grace, invariant-checked register-staged ACK suppression,
  sticky first-fault evidence, returned-ACK bytes/PHY/TSF validation,
  native hazard controls and retained RF failures.

Keep experiment commands, build IDs, trial counts, logs, unresolved behavior
and proposed changes here. Document current user-visible behavior and
interfaces in [doc](../doc/README.md).
