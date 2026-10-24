# Diagnostic profile navigation

Use [the profile catalogue](../../src/profiles.mk) for exact named selections
and [the HT20 reference](../../doc/HT20.md) for current command/descriptor
contracts. This guide locates development evidence; it does not duplicate
flag definitions, wire layouts or campaign results.

Select diagnostics explicitly and keep host/controller/collector images
matched. Offline validation is described in [the development guide](DEVELOPMENT.md).
Hardware work additionally requires [the trial protocol](HARDWARE_TRIALS.md).

## Aggregate and copy profiles

The fixture helper
[`clean_ht_handoff_frame()`](../../src/diagnostic/ht_command.h) constructs eight
legacy/single-HT/aggregate cycles. Only the aggregate uses version 7; controls
retain ordinary nonaggregate descriptors and the selected first-body length.
Exact admission and version-8 guard geometry are specified in the HT20
reference rather than repeated here.

| Question or profile family | Authoritative experiment record |
| --- | --- |
| Initial three-MPDU stimulus and rejected continuation approaches | [Initial aggregate radio](../experiments/ampdu/INITIAL_RADIO.md) |
| Three-body handoff, DMA versus retained SHM and terminal ownership | [Aggregate interface and delivery](../experiments/ampdu/INTERFACE_AND_DELIVERY.md) |
| Bounded advance and frozen delivery state | [Advance and delivery](../experiments/ht20/ADVANCE_AND_DELIVERY.md) |
| RXE drain and buffer controls | [Drain controls](../experiments/ht20/RXE_DRAIN_CONTROLS.md) |
| Rearm decisions, fault recovery and parked PHY reset | [RXE recovery](../experiments/ht20/RXE_RECOVERY.md) |
| Fixed stimulus, observer FIFO and no-credit retention | [Early capture and FIFO](../experiments/ht20/EARLY_CAPTURE_AND_FIFO.md) |
| Guard body, sentinels, fresh-count wait and copy-end bias | [Handoff and copy wait](../experiments/ht20/HANDOFF_AND_COPY_WAIT.md) |
| Independent raw vendor reception, deferred streaming and DMA destinations | [Vendor reference and streaming](../experiments/ht20/VENDOR_REFERENCE_AND_STREAMING.md) |
| Moving stream thresholds, immediate/delayed replay and destination/endpoint controls | [Copy progress and placement](../experiments/ht20/COPY_PROGRESS_AND_PLACEMENT.md) |
| Epoch-held stream quieting, replay fields, native counts and FCS | [Epoch replay and FCS](../experiments/ht20/EPOCH_REPLAY_AND_FCS.md) |
| Next-MPDU field, ordered/prefix recovery, variable lengths and short-first controls | [Next-MPDU and variable lengths](../experiments/ht20/NEXT_MPDU_AND_VARIABLE_LENGTH.md) |

## Compressed BA

Command 17 is a finite, explicitly armed diagnostic, not a production BA
agreement interface. The [compressed-BA campaign](../experiments/ampdu/INTERFACE_AND_DELIVERY.md#diagnostic-compressed-ba-return-2026-10-19)
owns its actual returned-frame evidence, failed controls and timing limits.
Current integration work is tracked in [issues](../issues/README.md).

## Report interpretation

See [host tools](HOST_TOOLS.md#desktop-report-fields) for collector fields.
Retained PSM snapshots, delivered frame bytes and independent peer receipt
are different evidence. A successful finite profile does not qualify
general N-body reception, negotiated HT, reliable recovery or SIFS timing.
