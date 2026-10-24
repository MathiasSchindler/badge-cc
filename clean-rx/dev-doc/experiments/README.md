# Experiment records

These records own methods, tested images/profiles, observations, failed
controls, rejected hypotheses, evidence paths and restoration results.
They are historical evidence, not live TODO lists or current operating guides.
Follow [the hardware protocol](../guides/HARDWARE_TRIALS.md) before any new run.
Use [current issues](../issues/README.md) for what remains open.

## Cross-board and capability regressions

- [R08 retry and pause measurements](RETRY_AND_STALL_R08_2026-10-24.md):
  original retry reanalysis, 120 fresh raw exchanges, measured desktop/device
  timing, one passing network control, retained startup failure and restoration.
- [HT loss-stage follow-up](HT_ROOT_CAUSE_2026-10-24.md):
  32 separately authorized trials, retained B-to-A MCS7 misses, correlated
  F2 diagnostics, rejected-SHM freshness limits and verified restoration.
- [R07 intermittent HT investigation](HT_RECEPTION_R07_2026-10-24.md):
  strict nonaggregate evidence, passive Pico calibration, retained misses,
  beacon/report ownership fix, independent-ACK limitations and restoration.
- [R06 aggregate DMA repair](DMA_REPAIR_R06_2026-10-24.md):
  format-zero fault, next-MPDU command and physical DMA layout, preserved
  ownership/completion state, strict three-body/bad-FCS checks and restoration.
- [R05 bounded aggregate acceptance rerun](HARDWARE_ACCEPTANCE_R05_2026-10-24.md):
  44 planned SHM/BA/bad-FCS cells, two retained initial failures and passing
  fresh repeats, independent evidence audit and verified badge restoration.
- [R03 hardware rerun](HARDWARE_REGRESSION_R03_2026-10-24.md):
  explicit fresh evidence, corrected report ownership, final RX/TX and
  aggregate matrices, remaining failures and full-flash restoration.
- [Campaign follow-up work](todo.md): R01-R10 work items and acceptance criteria
  from the 2026-10-24 regression, separate from historical experiment reports.
- [2026-10-24 badge/Pico RX/TX regression](HARDWARE_REGRESSION_2026-10-24.md):
  raw cross-board exchanges, badge legacy/HT and station/security tests,
  experimental aggregate/BA failures and complete flash restoration.

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
