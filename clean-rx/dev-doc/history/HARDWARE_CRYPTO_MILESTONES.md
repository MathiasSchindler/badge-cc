# Hardware crypto milestone history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## F. Hardware crypto offload

### F1. Establish hardware semantics

- Recorded open criterion: Document D11 key installation, cipher/header configuration, key slots,
  PN/MIC/FCS handling and RX completion/error indications.
- Recorded open criterion: Define which layer owns each counter and validation step. Hardware
  offload must not bypass replay checks or expose unauthenticated plaintext.
- Recorded open criterion: Keep the verified software-CCMP path as an explicit reference/backend
  during qualification; never silently fall back to unencrypted transmission.

### F2. Functional equivalence

- Recorded open criterion: Integrate hardware TX/RX crypto and key lifecycle with the station,
  including pairwise/group keys and rekey transitions supported by E.
- Recorded open criterion: Compare hardware ciphertext, tags and decrypted payloads with independent
  known-answer vectors and the verified software path across supported frame
  headers, traffic classes, payload sizes and PN boundaries.
- Recorded open criterion: Verify bad MICs, replay, wrong keys/key indices, retries, key replacement
  and reset. Require rejection before plaintext delivery and correct PN state.

### F3. OTA and resource qualification

- Recorded open criterion: Verify encrypted unicast/group exchanges with an independent AP and
  repeat E's rekey tests plus B2's sustained protected soak.
- Recorded open criterion: Measure RP2350 CPU time, throughput, latency, stack/RAM and firmware-size
  changes against software CCMP under identical conditions.
- Recorded open criterion: Enable offload by default only after equivalent correctness and recovery
  are demonstrated; document any remaining hardware/backend restrictions.

## Sequencing

1. Establish A's MAC correctness and B1/B2's connection/data foundations.
2. Complete E's software-backed WPA2 lifecycle, then B3's protected station
   qualification. B and E are iterative, not a circular completion dependency.
3. C can proceed alongside that work, but final qualification depends on A.
4. Start D after reliable legacy operation; HT requires additional MAC work,
   not merely a new PHY rate setting.
5. F1 and standalone F2 vectors can proceed independently. Final F2/F3
   integration depends on E and sustained station tests.

For each completed milestone, append the evidence paths, build IDs, measured
results, remaining limitations and verification date here or in the linked
feature documentation before checking its final verification item.


