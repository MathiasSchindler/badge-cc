# Implementation history

These are completed-change records and historical milestone/qualification
snapshots. They explain why source and tests changed; they are not current
build instructions or parallel task lists. Recorded open criteria describe
the snapshot, not a new authoritative assignment.

Current instructions belong in [guides](../README.md#current-guides);
unresolved problems are indexed in [issues](../issues/README.md).
The root [functional roadmap](../../../todo.md) owns milestone acceptance
criteria; [cleanup](../../cleanup.md) owns structural cleanup tasks.

## Structural cleanup records

- [C01 inventory and immutable baseline](cleanup/C01_BASELINE.md).
- [C02 standalone source isolation](cleanup/C02_ISOLATION.md).
- [C03 test ownership](cleanup/C03_TEST_SEPARATION.md).
- [C04 contracts, fixtures and host crypto](cleanup/C04_CONTRACT_SEPARATION.md).
- [C05 runtime/diagnostic profiles](cleanup/C05_DIAGNOSTIC_PROFILES.md).
- [C06 controller and PSM composition](cleanup/C06_CONTROLLER_COMPOSITION.md).
- [C07 tooling and profile ownership](cleanup/C07_TOOL_AND_PROFILE_OWNERSHIP.md).
- [C08 offline acceptance](cleanup/C08_OFFLINE_ACCEPTANCE.md).

The machine-readable [C01 baseline](../c01-baseline.json) intentionally stays
at its original path, byte-identical: it is immutable evidence consumed by
existing tools, not a mutable status document.

## Implementation and bring-up

- [HT20 target and initial implementation](HT20_FOUNDATIONS.md).
- [HT20 packet/receive/response APIs](HT20_PACKET_AND_RESPONSE_API.md).
- [HT20 peer policy](HT20_PEER_POLICY.md).
- [Block ACK software](BLOCK_ACK_SOFTWARE.md).
- [Hardware bring-up and register notes](HARDWARE_BRINGUP.md).
- [Host integration and transport notes](HOST_INTEGRATION.md).

The last two preserve older implementation descriptions and assumptions.
Do not use their sample addresses, command policies or claims as current
reference; use [firmware documentation](../../doc/README.md).

## Milestone snapshots

- [Original scope and definition of done](MILESTONE_CONTEXT.md).
- [Ordinary MAC](MAC_MILESTONES.md).
- [Station lifecycle and application data](STATION_MILESTONES.md).
- [Legacy PHY and boundary investigations](LEGACY_PHY_MILESTONES.md).
- [HT](HT_MILESTONES.md).
- [WPA2](WPA2_MILESTONES.md).
- [Hardware crypto and sequencing](HARDWARE_CRYPTO_MILESTONES.md).
- [Historical qualification boundaries](QUALIFICATION_BOUNDARIES.md):
  extracted limits and work proposals from completed reports; current
  acceptance criteria remain in the functional roadmap.
