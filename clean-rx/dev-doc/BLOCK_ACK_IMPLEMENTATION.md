# Bounded Block ACK software and remaining hardware integration

## Status

The recipient-side agreement, reorder, delimiter-stream and protected
delivery software is implemented and native-tested. **Live A-MPDU reception
and SIFS-timed Immediate Block ACK are not implemented.** Negotiated HT
remains disabled. This work does not complete D2 or D3.

A subsequent [finite radio investigation](AMPDU_RADIO_INVESTIGATION.md)
retained the first delimiter-stripped MPDU of a three-MPDU diagnostic
aggregate. It did not recover the others or establish an EOF response
event. Its finite probe is retained; unsuccessful rearm variants are not.
The newer [interface campaign](AMPDU_INTERFACE_INVESTIGATION.md) separates
early MPDU completion from the later terminal latch. Its one-copy handoff
now retains all three complete diagnostic MPDUs at MCS0..7 in both
directions, with controlled bad-FCS rejection and continuation. This is
still not live recipient/transport integration or SIFS-timed BA.

The subsequent default-disabled
[per-MPDU DMA/F2 experiment](AMPDU_INTERFACE_INVESTIGATION.md#per-mpdu-dma-to-f2-delivery-follow-up)
does deliver two distinct complete MPDUs to the RP2350 from one PPDU.
Its six MCS0/3/7 direction controls all miss the final MPDU; it remains
a fixed fixture, not the production BA recipient. Bad-middle controls
reject that MPDU without delivery. No BA or protection qualification
is implied, and the live integration prerequisite remains unchecked.
The final independently saved-byte campaign validates two deliveries in
6/7 normal trials, retains one RF miss, and validates both bad-middle
rejections. Its host independently checks all 14 saved SDIO MPDUs and FCS;
no trial delivers the complete aggregate.

There is no controller BA-install command, station ADDBA activation, PSM
aggregate response path or independent on-air aggregate qualification.
The new software is not called by the live station receive dispatcher.
Merely linking its source into the builds does not activate it; unreferenced
functions and storage are removed by section garbage collection.

## Implemented components

- [block_ack.h](../src/block_ack.h) and
  [block_ack.c](../src/block_ack.c): a caller-owned recipient for one pinned
  station/AP pair, all 16 TIDs, an eight-entry per-TID window and a shared
  pool of 16 complete ciphertext MPDUs. Pool exhaustion returns explicit
  backpressure without setting a receipt bit for the rejected frame.
- Immediate-policy ADDBA request validation and a positive response
  serializer. Parsing only prepares the request. Explicit activation is
  required before a positive response can be constructed. A live caller
  must first install and verify the corresponding hardware response state.
  Matching retransmissions are idempotent; conflicting active renegotiation
  is rejected. A-MSDU-in-A-MPDU and delayed BA are not negotiated.
- Single-TID compressed BAR and a 32-byte compressed BA serializer, including
  FCS. The receipt bitmap is separate from the delivery head, so delivered
  MPDUs remain acknowledged. These functions do not transmit responses.
- Bounded sequence/window movement, duplicate filtering, 12-bit wrap,
  a 100-ms gap timeout, negotiated inactivity timeout and DELBA handling.
  DELBA stops new admission and uses polling to flush admitted frames in
  sequence order before freeing the agreement, respecting sink backpressure.
  Explicit session reset instead wipes queued ciphertext.
- Incremental A-MPDU parsing through the 8,191-byte exponent-zero bound,
  using one MPDU scratch buffer rather than an aggregate-sized allocation.
  It checks delimiter CRC/signature/reserved bits, lengths, per-MPDU FCS,
  padding, null delimiters and truncated input. A bad MPDU FCS is counted and
  skipped at its trusted delimiter boundary. A bad delimiter aborts
  explicitly; speculative byte resynchronization is not implemented.
- [wpa2.c](../badge/wpa2.c) adapters connect the delimiter stream to the
  bounded ciphertext queue and ordered release to the actual CCMP path.
  Per-TID PN replay checks and authenticated duplicate/delivery processing
  run on ordered release, not out-of-order arrival.
  [mac.c](../src/mac.c) has a separate authenticated BA-policy entry;
  ordinary receive paths still reject unsupported BA policy.

The shared pool bounds total storage, not 16 independent full windows.
Admission can fail when other TIDs occupy the pool. Such a frame must not
be acknowledged by a future hardware bitmap implementation.
Individual MPDUs retain the selected build's 2,400- or 3,896-byte bound;
the 8,191-byte aggregate limit does not increase that individual bound.

## API and lifecycle requirements

Initialize BA state with the actual local MAC and associated AP. Route
validated action frames through `clean_ba_action`; do not call
`clean_ba_activate` in production until controller/PSM state and resources
are installed and read back. No live caller currently does so.

For encrypted traffic, `clean_wpa_ba_receive`, `clean_wpa_ba_bar` and
`clean_wpa_ba_poll` use the installed WPA state and verify matching pinned
addresses. `clean_wpa_ampdu_feed` connects a prepared delimiter stream to
that same path. The ordinary protected receiver remains unchanged.
Fragmentation, HT Control and A-MSDU-in-A-MPDU are excluded from this path.

Sink results are 1 for consumed, 0 for backpressure and -1 for abort.
Do not reset or reenter the queue from its callback. A sink error is
propagated, not converted to successful delivery. A MIC failure wipes the
WPA key state through its existing failure path; the outer session owner
must then reset BA state after the callback returns.

The sink's timestamp is the **release** time. Stored arrival times are
used only for gap expiry. Passing an older arrival time into the live WPA
expiry code after a later release could incorrectly expire another TID's
fragment assembly.

Call polling regularly for gaps, timeouts and DELBA flushing. Incoming
data/BAR also checks inactivity expiry, so late traffic cannot revive an
expired agreement simply because polling was delayed.

Reset and wipe all agreements/stream state on departure, cancellation,
key replacement, peer change or failure. BA storage is caller-owned and
does not independently track key epochs. The station/controller integration
must supply this lifecycle before enabling negotiation.

For stream backpressure, retain the stream and retry only the unconsumed
tail indicated by `*used`, including a zero-byte tail if the final complete
MPDU is waiting on admission. Never silently discard a pending stream or
acknowledge its unadmitted MPDU.

## Native validation

[block_ack_test.c](../src/block_ack_test.c) covers:

- ADDBA preparation/activation, matching retransmissions, conflicts,
  malformed policy/fragment/FCS fields and all TIDs;
- wrap, BAR, separate BA receipt history and bitmap bit 63;
- full windows/shared pool, backpressure, explicit sink errors and wiping;
- timer wrap, receive/BAR inactivity expiry and DELBA flushing under stall;
- 1,024 shuffled eight-frame windows: 8,192 ordered deliveries and 8,192
  duplicates, crossing the 12-bit sequence space twice;
- multiple maximum-sized MPDUs, chunk sizes from one byte through the
  aggregate bound, and an exact 8,191-byte/128-MPDU stream;
- independent bit-order CRC calculation, invalid delimiters, truncation,
  oversized input, bad per-MPDU FCS and final-tail resumption.

[wpa2_test.c](../badge/wpa2_test.c) uses keys from its actual four-way
handshake and real encrypted MPDUs. It tests higher PN arriving first,
ordered PN commitment, sequence wrap, independent TIDs, duplicates,
replayed PN at a fresh sequence, gap/BAR release and explicit MIC failure.
It also runs chunked delimiter stream -> ciphertext reorder -> CCMP ->
authenticated delivery end to end.

Default and extended HT/EDCA/WMM builds use the existing controller and
badge native suites, including AddressSanitizer and UndefinedBehaviorSanitizer.
Representative isolated commands from the repository root:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ba-default all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/ba-default \
  CONTROLLER_BIN=../../build/clean-rx-ba-default/cyw43_rx_fw.bin all test

make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ba-gated \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_RESPONSE=1 -DCLEAN_RX_EDCA=1 -DCLEAN_RX_WMM=1' all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/ba-gated \
  CONTROLLER_BIN=../../build/clean-rx-ba-gated/cyw43_rx_fw.bin \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_RESPONSE=1 -DCLEAN_RX_EDCA=1 -DCLEAN_RX_WMM=1' all test
```

Logs are retained under `build/clean-rx/badge/ba-{default,gated}-{controller,badge}.log`.
These are software tests, not independent OTA evidence. No badge flash was
written for the software-only stage. The subsequent radio campaign and
full-flash preservation are recorded in the linked investigation.

## Why Immediate BA is still blocked

The current [PSM receive/response program](../src/psm_ack.h) captures and
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

The earlier [independent HT receive campaign](CONTROLLED_HT_RECEIVE.md)
used small nonaggregate multicast frames. It cannot serve as aggregate,
ADDBA or BA timing qualification.
