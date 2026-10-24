# Historical qualification boundaries

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## Remaining D2 work

Source: the final [HT20 receive/copy checkpoint](../experiments/ht20/NEXT_MPDU_AND_VARIABLE_LENGTH.md).
This is its historical boundary summary, not a parallel current task list.

Use the [consolidated D2 work packages](../../../todo.md#remaining-work-packages)
for the current checklist. The detailed qualification boundaries follow.

The matched next-MPDU `0xc3` diagnostic now delivers complete 170/298-byte
streaming continuations through SHM/SRAM/F2 with correct native count and
GoodFCS, without mid-body replay. The older segmented microscope still
quarantines its mismatched native lengths rather than exporting partial
frames. Neither path qualifies repeated variable-length reception,
general larger-body streaming ownership, generalized delimiter/EOF recovery or
receive reliability. Ordered-flush execution is proven only under
software-fault injection; physical-wedge recovery and a natural
stuck-prefix trigger remain unproven.
The variable-length extension in that record proves selected odd/long-body pairs
and exposes a short-first initial-stream-state dependency; it does not
remove the one-shot/480-byte early-slot boundary or qualify repeated
N-body buffering and backpressure.

The isolated retained-SHM/SRAM/F2 diagnostic now delivers all three actual
MPDUs at every MCS0..7 in both directions, without duplicates in its finite
matrix. Controlled bad first/middle/last frames are rejected while other
MPDUs reach the badge. See the
[full delivery evidence and limitations](../experiments/ampdu/INTERFACE_AND_DELIVERY.md#retained-shm-to-f2-delivery-2026-10-19).
This does not resolve the separate DMA/EOF experiment, generalize the
three fixed slots, or implement Immediate BA or mandatory protection.
The subsequent
[explicit terminal-ownership campaign](../experiments/ampdu/INTERFACE_AND_DELIVERY.md#explicit-terminal-ownership-2026-10-19)
replaces ordinary held-receive polling with a PSM state-16 commit/claim/
release, including bad-last/all-bad controls. Copying no longer depends on
a later good ordinary frame. Failed/partial RF receptions remain visible;
the measured local TSF lease intervals are not qualified EOF-to-air SIFS.

The subsequent default-disabled
[compressed BA return campaign](../experiments/ampdu/INTERFACE_AND_DELIVERY.md#diagnostic-compressed-ba-return-2026-10-19)
adds a real PSM BA response and sender turnaround for the fixed fixture.
All normal MCS/direction cells now have a complete delivery plus actual
matching on-air return after repeats; 28 returned BA frames are
independently validated across 48 trials, including one partial normal
response. Single-bad controls exclude the invalid body's receipt bit.
Bad-final reverse MCS7 remains unqualified after four no-capture trials.
Command 17 arms this diagnostic, not a production agreement. The hardware
SIFS control selection and observed IFS timestamps do not independently
qualify RF EOF-to-air SIFS. HT advertisement remains disabled.

- Integrate the [native-tested Block ACK/reordering software](BLOCK_ACK_SOFTWARE.md)
  with actual PSM aggregate boundaries, retained multi-MPDU capture and
  hardware-timed Immediate BA; wire station agreement/key lifecycle before
  enabling negotiation.
- Qualify [AP-driven non-HT WMM/EDCA integration](../experiments/mac/EDCA_AND_WMM.md#ap-driven-non-ht-wmm-integration)
  beyond its bounded open/WPA2 exchanges, including runtime policy
  changes and recovery; implement HT protection and qualify the gated ACK
  prototype's returned responses, SIFS timing and EOF anchoring.
- Qualify the implemented per-TID receive/CCMP and protected A-MSDU software
  over the air, and integrate negotiated aggregate operation; preserve
  legacy fallback.
- Meet HT receive-size and required MAC/Block ACK obligations jointly with
  D3 before activating/advertising the prepared HT association target.
- Extend the implemented non-HT WMM association-response/runtime policy
  activation to actual HT capability/operation negotiation only after
  the required MAC obligations are met.
- Verify negotiated association and actual HT OTA framing with an independent
  HT20 implementation, including explicit legacy fallback.
- Diagnose remaining RX losses; no all-rate reliability threshold is passed.
- Extend shared-medium access/operand-visibility qualification beyond the
  bounded counter-repair trials; retain the fail-closed guard and evidence.


## Next D2 work

Source: the [finite aggregate interface checkpoint](../experiments/ampdu/INTERFACE_AND_DELIVERY.md).
These proposals describe the boundary at that checkpoint.

1. Generalize the demonstrated one-copy continuation to per-MPDU lengths
   and explicit bounded buffer ownership; do not turn the fixed diagnostic
   into production reception by merely removing its gates.
2. Verify the completion/terminal handshake across variable MPDU sizes,
   bad FCS, delimiter errors, truncation and abort, ideally with an
   independent aggregate source/capture.
3. Replace the fixed diagnostic slots with bounded production retained
   credits and proven delivery before admitting frames to a hardware BA
   bitmap; repairing the separate DMA experiment is not itself a requirement
   if an alternative path meets all receive/retention obligations.
4. Install atomic agreement state and implement/measure real SIFS-timed
   compressed BA at qualified EOF/BAR events. Then integrate station
   lifecycle, reordering/CCMP and negotiate HT last.


## Required next evidence

Source: the [initial finite aggregate campaign](../experiments/ampdu/INITIAL_RADIO.md).
Later finite returned-BA evidence is in the aggregate interface record;
the prerequisites below are preserved as originally proposed.

Before implementing or activating an Immediate BA path, establish and
independently verify:

1. The exact radio's per-MPDU admission/advance and aggregate EOF events,
   including bad MPDU FCS, delimiter errors and truncation.
2. Several distinct fresh MPDUs from one PPDU retained without host
   servicing, with bounded credits and no falsely acknowledged drops.
3. Atomic controller/PSM agreement state and an actual compressed BA
   transmission with independently measured SIFS timing and bitmap.
4. Live station ADDBA/BAR/DELBA, reordering/CCMP, teardown/rekey and loss/
   saturation qualification. Enable negotiated HT only after these pass.

See [the bounded BA software implementation](BLOCK_ACK_SOFTWARE.md)
for the already-tested recipient and encrypted ordered-release adapters.


## Remaining limits

Source: [MAC completion/recovery](../experiments/mac/COMPLETION_AND_RECOVERY.md).
These are the limits recorded at its closing checkpoint.

- The missing TXE done-event cause is unresolved. The fixed behavior is safe,
  explicit failed-attempt recovery, not removal of the hardware event loss.
- The first no-bound-peer-reception failure and the final exhaustion trial's
  two missing eligible receptions remain unexplained. Startup readiness is
  only a hypothesis; a verified synchronization mechanism remains open.
- No new vendor control, independent sniffer, calibrated RF timing, controlled
  hidden-node/overlap/NAV injection or CCK fragment RF matrix.
- No passing 100,000-MSDU-per-direction mixed-size gate.
- No automatic station fragmentation, SIFS fragment burst or new protected/
  QoS/HT fragment RF qualification.


## Remaining qualification

Source: [legacy fragmentation](../experiments/mac/LEGACY_FRAGMENTATION.md).
The unchecked A1/A2/A3 references below describe that campaign's status.

- Retain/investigate both the unexplained first post-fix timeout and the
  longer-run TX-completion fault; passing shorter trials do not resolve them.
- No vendor-firmware control, third-party sniffer or SIFS/NAV timing capture.
- No sustained 100,000 application frames per direction, saturation matrix,
  malformed-fragment RF injection or CCK fragment RF qualification.
- No SIFS-timed fragment burst, automatic station fragmentation, or protected,
  QoS/HT fragmented RF qualification.
- Existing native malformed/order/expiry tests remain useful but do not
  substitute for those hardware gates. A1/A2/A3 checkboxes remain unchecked.


## Why Immediate BA is still blocked

Source: the earlier [Block ACK software checkpoint](BLOCK_ACK_SOFTWARE.md),
before the later [finite returned-BA campaign](../experiments/ampdu/INTERFACE_AND_DELIVERY.md#diagnostic-compressed-ba-return-2026-10-19).
This is the recorded rationale for deferring production integration, not a
claim that no subsequent diagnostic BA was transmitted.

At that checkpoint the [PSM receive/response program](../../src/psm_ack.h) captures and
holds one MPDU until Cortex consumption. Its HT response filter rejects
the aggregation flag and only implements the existing ACK/CTS response
templates. It has no verified per-subframe aggregate boundary or
end-of-aggregate response event. The controller publishes that same single
capture; a host parser cannot recover subframes already lost in hardware.

Public D11-family references are useful but not badge-specific proof:
[Linux v6.12 d11.h](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h?h=v6.12)
defines aggregate classifications in the firmware RX header.
Those definitions do not establish raw PSM aggregate event bits.
[Linux v6.12 ampdu.c](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/net/wireless/broadcom/brcm80211/brcmsmac/ampdu.c?h=v6.12)
updates a BA template's transmitter address and microcode watchdog settings;
it does not provide the needed clean PSM receive/response implementation.
The badge's exact radio version/revision is still unidentified.

The remaining work is:

1. Establish actual aggregate/subframe/EOF behavior on the radio with an
   independent finite aggregate source.
2. Implement PSM-owned multi-MPDU capture/admission with retained credits.
   The PSM BA bitmap must reflect retained MPDUs, never dropped captures.
3. Implement atomic agreement install/teardown and compressed BA
   template/bitmap updates, then measure an actual SIFS-timed BA response.
   Cortex-delayed raw TX is not Immediate BA.
4. Wire station actions, BAR, aggregate transport, CCMP/reordering and
   lifecycle reset together; send positive ADDBA only after verified setup.
5. Qualify loss, duplicates, gaps, resource exhaustion, replay/MIC failure,
   recovery and legacy regressions over the air. Enable negotiated HT last.

The earlier [independent HT receive campaign](../experiments/rx/CONTROLLED_HT_RECEIVE.md)
used small nonaggregate multicast frames. It cannot serve as aggregate,
ADDBA or BA timing qualification.


## Remaining D2 gates

Source: [EDCA/WMM integration](../experiments/mac/EDCA_AND_WMM.md).
This is its recorded qualification boundary.

- RF qualification of runtime AP policy changes and error/recovery behavior
  beyond the bounded non-HT WMM trials in that record.
- General QoS TX queueing/admission behavior, HT protection and independently
  measured SIFS/ACK timing.
- Negotiated protected aggregates, required A-MPDU reception, immediate
  Block ACK sessions/windows, bounded receive reordering and recovery.
- Independent HT peer association/framing with explicit legacy fallback.

