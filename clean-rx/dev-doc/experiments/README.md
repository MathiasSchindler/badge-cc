# Experiment records

These records own methods, tested images/profiles, observations, failed
controls, rejected hypotheses, evidence paths and restoration results.
They are historical evidence, not live TODO lists or current operating guides.
Follow [the hardware protocol](../guides/HARDWARE_TRIALS.md) before any new run.
Use [current issues](../issues/README.md) for what remains open.

## Receive reliability and PHY controls

- [AP authentication and receive-service controls](rx/AP_AUTHENTICATION.md):
  local-AP replies, staging, amplitude/turnaround, live-RX and RF hints.
- [Controlled independent HT reception](rx/CONTROLLED_HT_RECEIVE.md):
  independent peer, cold-boot matrix and discarded harness hypotheses.
- [Isolated RF hint controls](rx/RF_HINT_CONTROLS.md):
  gain/register comparisons, cleanup/pulse/timer controls and final evidence.

## MAC, EDCA and fragmentation

- [Completion ownership and recovery](mac/COMPLETION_AND_RECOVERY.md):
  fired-request races, ACK clocks, PHY-error retirement and missing-RX provenance.
- [Legacy fragmentation](mac/LEGACY_FRAGMENTATION.md):
  bounded fragment/ACK-loss qualification, independent checks and failures.
- [EDCA, WMM and protected A-MSDU](mac/EDCA_AND_WMM.md):
  policy readback, paired trials, AP integration and protected receive controls.

## HT20 and aggregates

- [HT20 investigation sequence](ht20/README.md): the former large HT20 log,
  split into bounded causal investigations while retaining chronology.
- [Initial finite aggregate radio investigation](ampdu/INITIAL_RADIO.md):
  delimiter-stripped capture and unsuccessful continuation alternatives.
- [Aggregate interface, delivery and compressed BA](ampdu/INTERFACE_AND_DELIVERY.md):
  three-body handoff, DMA/SHM paths, terminal leases and returned BA frames.

Historical qualification boundaries extracted from these records are collected
in [history](../history/QUALIFICATION_BOUNDARIES.md). They are not a second
current task tracker.
