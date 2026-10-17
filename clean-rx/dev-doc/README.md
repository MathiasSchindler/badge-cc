# clean-rx development documentation

This directory is for contributors tracking implementation work, experiments,
failures, open questions and qualification. Its historical records describe
the builds and conditions named in each entry, not necessarily the current
firmware.

- [HT20 implementation and investigations](HT20_IMPLEMENTATION.md):
  D1/D2/D3 milestones, API development, receive failures, contention
  diagnostics, watchdog work and remaining qualification.
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

Keep experiment commands, build IDs, trial counts, logs, unresolved behavior
and proposed changes here. Document current user-visible behavior and
interfaces in [doc](../doc/README.md).
