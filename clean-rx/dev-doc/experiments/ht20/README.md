# HT20 investigation sequence

The former HT20 implementation log combined APIs, campaigns and future work.
Implementation history now lives separately; the investigations below retain
their original observations, conditions, negative results and evidence.
Dates and phrases such as "now", "final" or "remaining" refer to that entry,
not the current source. Consult [issues](../../issues/README.md) for current work.

## Implementation foundations

- [Target, public references and initial mapping](../../history/HT20_FOUNDATIONS.md).
- [Packet, receive and response APIs](../../history/HT20_PACKET_AND_RESPONSE_API.md).
- [Peer profiles, capability policy and policy commits](../../history/HT20_PEER_POLICY.md).

## Causal investigation order

1. [Clocked ACK, operand visibility and initial receive failures](CLOCKED_ACK_AND_RX_FAILURES.md).
2. [Bounded advance, complete two-body capture and frozen delivery state](ADVANCE_AND_DELIVERY.md).
3. [Handoff state, RXE drain and receive-buffer controls](RXE_DRAIN_CONTROLS.md).
4. [Pre-prefix losses, rearm decisions and parked receive recovery](RXE_RECOVERY.md).
5. [Fixed-stimulus early capture and observer FIFO](EARLY_CAPTURE_AND_FIFO.md).
6. [Controlled handoff, overwrite, fresh-count wait and end boundaries](HANDOFF_AND_COPY_WAIT.md).
7. [Independent vendor reference, deferred streaming and byte destinations](VENDOR_REFERENCE_AND_STREAMING.md).
8. [Moving copy boundaries, replay timing and destination/endpoint placement](COPY_PROGRESS_AND_PLACEMENT.md).
9. [Epoch stream ownership, replay fields, native counts and FCS](EPOCH_REPLAY_AND_FCS.md).
10. [Next-MPDU commands, bounded recovery and variable-length handoffs](NEXT_MPDU_AND_VARIABLE_LENGTH.md).

Use [diagnostic profile navigation](../../guides/DIAGNOSTIC_PROFILES.md) to
find evidence by control family rather than rereading the chronological log.
Current wire behavior belongs in [the reference](../../../doc/HT20.md).
