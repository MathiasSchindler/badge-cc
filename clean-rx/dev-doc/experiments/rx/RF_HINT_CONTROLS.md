# Remaining RF hints: isolated experiments and ownership

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

This is a development record, not a claim of HT interoperability or identification
of the badge's Wi-Fi silicon revision. It continues the
[accepted RX/radio/backoff investigation](AP_AUTHENTICATION.md).

## Evidence and controls

The fresh, verified full-flash backups for this run are
`build/clean-rx/badge/rf-followup-start-{A,B}-full.bin`, each 16,777,216 bytes.
Their SHA-256 hashes are:

- A, `08DA26F86E57F5EA`:
  `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B, `9BBF88DD17D66E7D`:
  `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

The immutable accepted full-WMM image, `41a565c8`, is the screen's baseline.
Each register/gain trial changes one parameter or one complete matched tuple.
Baseline trials are interleaved because MCS7 outcomes vary between boots.
All trials use channel 1 and the same saved challenges:

- MCS7 normal ACK, both directions: `1385e94b8d97b06e19f73e3c0007`.
- 1-Mbps long-preamble CCK, 2,304-byte MSDU, target 16 deliveries per
  direction and one deliberately suppressed ACK: `8db624ccb0c1cf62e1bbde32e104`.

Reused challenges are controlled comparisons, not independent replay-freshness
qualification. A successful HT fixture also does not qualify negotiated HT,
protection, A-MPDU, Block ACK, or reordering.

The first screen stopped after an unbounded `picotool reboot` readiness hang,
before its long-CCK RF trial began. Its two completed HT trials both passed,
but that incomplete sequence is not combined with the bounded screen.
The resumed evidence prefix is `build/clean-rx/badge/rf-followup-bounded-`.
An external wrapper bounds only `info`/`reboot` to 15 seconds; flash writes,
verification, and full-flash restoration are never subject to that timeout.
The paired runners accept this executable through `CLEAN_RX_PICOTOOL`.

## 1A: repeated corruption signatures

Reconstructed all six cited fixtures with the current C HT encoder and MAC FCS
implementation, using the recorded challenge, rather than inferring expected
bytes from an adjacent capture. The reconstruction ran with ASan/UBSan and
agreed exactly with the original MPDU bit-error counts.

Word indices below are zero-based little-endian words in the full 66-byte
HT-SIG-plus-MPDU record.

| Capture prefix under `build/clean-rx/badge/` | MPDU bit errors | Cited XOR matches | Transmitted FCS bytes preserved |
| --- | ---: | --- | --- |
| `d2-queued-baseline-m7-t0-ab` | 199 | word 20: `1509` | No |
| `d2-queued-baseline-m7-t4-ba` | 207 | word 7: `8b22`; 12: `6d95`; 20: `1509`; 22: `6d09` | No |
| `d2-finish-bb16-repeat-t4-ab` | 45 | word 22: `6d09` | Yes |
| `d2-edca-final-ba-tid4-mcs7` | 200 | word 7: `8b22` | Yes |
| `d2-replay-baseline-t4-ba` | 171 | word 12: `6d95` | No |
| `d2-rx-final-11-6-0-forward` | 5 | None of the four listed words | Yes |

The two independently read copies of each bad record match exactly. Thus the
repeated XOR observations are real, and some failures preserve the transmitted
FCS bytes despite corrupting the body. This is evidence worth retaining, not
a unique proof of a particular Viterbi error, descrambler mechanism, PA clipping,
or exclusion of every possible upstream memory fault. No PHY EVM measurement,
decoder trace, or calibrated power measurement was made.

## 1B, 2, 3, and 4: accepted changes

The previous accepted implementation releases the automatic radio overrides,
acknowledges prefix copy completion, requires a fresh completion event as well
as terminal status, promptly handles terminal bad FCS, and does not treat
external condition 5 alone as an RX abort. It also caches verified station/peer
identity, publishes backoff while only PSM is paused, and retains completed
post-backoff state. The original full watchdog remains for incomplete RX.

Those changes and their legacy/WPA2 regressions are recorded in the linked
investigation. The SHM-collision explanation itself remains a hypothesis;
the defensive ownership changes do not establish that proposed mechanism.

## 1C: complete matched gain tuples

Read-only inspection of the supplied candidate table gives:

| Entry | PHY `4b5` | PHY `4fb` | BBMULT |
| --- | --- | --- | ---: |
| 40 | `0f06` | `ff22` | 38 |
| 50 | `0f06` | `ff1c` | 32 |
| 60 | `0f06` | `ff16` | 28 |
| 70 | `0f06` | `ff16` | 21 |

All four tuples were built as isolated variants, not just a BBMULT substitution.
This table is not a substitute for the vendor's calibration lifecycle, IQ/LO
calibration, per-board power limits, or identification of the actual chip
revision. No vendor executable code is incorporated.

## 1D: individual PHY register differences

The reference values below were checked against both supplied dumps before
building the eight isolated variants.

| Variant | Register | Vendor snapshot | Cold snapshot | Trial operation |
| --- | --- | --- | --- | --- |
| `phy-1` | `637` | `3000` | `0000` | masked `f000` to `3000` |
| `phy-2` | `4d0` | `0144` | `0000` | masked `7fe4` to `0144` |
| `phy-3` | `4d7` | `2145` | `0000` | write `2145` |
| `phy-4` | `4d9` | `132a` | `132a` | masked `000e` to `000a` |
| `phy-5` | `574` | `4000` | `0000` | set `4000` |
| `phy-6` | `807` | `0009` | `0008` | set bit 0 |
| `phy-7` | `846` | `6008` | `6992` | write `6008` |
| `phy-8` | `ada` | `565e` | `5e5e` | write `565e` |

A running vendor snapshot is not proof that every value can safely replace cold
initialization before calibration. The experiments do not assume otherwise.

### Bounded initial screen

All 22 image-pair trials completed. A pass requires correct HT-SIG, complete
FCS-valid fixture reception, and the transmitter's fresh return ACK, with no
host/controller error. Decode-only success and local TX completion are not
counted as passes.

| Image | Badge build | MCS7 complete exchanges | Long-CCK deliveries, A/B |
| --- | --- | ---: | --- |
| Interleaved baseline, eight image-pair trials | `41a565c8` | 10/16 | 0/0 in every trial |
| `phy-1` | `4cecbdcf` | 2/2 | 0/0 |
| `phy-2` | `2264fef6` | 2/2 | 0/0 |
| `phy-3` | `104ba145` | 2/2 | 0/0 |
| `phy-4` | `b705868f` | 1/2 | 0/0 |
| `phy-5` | `1d133205` | 2/2 | 0/0 |
| `phy-6` | `d6f88cac` | 0/2 | 0/0 |
| `phy-7` | `478da53f` | 2/2 | 0/0 |
| `phy-8` | `408c68c6` | 2/2 | 0/0 |
| `gain-40` | `e846e9a4` | 2/2 | 0/0 |
| `gain-50` | `f69c87c3` | 1/2 | 0/0 |
| `gain-60` | `eaa6f79d` | 0/2 | 0/0 |
| `gain-70` | `4c932993` | 0/2 | 0/0 |
| `cleanup1` | `a382845d` | 1/2 | 0/0 |
| `cleanup2` | `11937857` | 2/2 | 0/0 |

All HT failures in this screen have zero controller error; none is reclassified
as a register fix based on one successful boot. In particular, lowering the
complete gain tuple to entries 60/70 did not fix MCS7. No tested change fixed
the maximum-size 1-Mbps failure.

### Repeated candidate controls

Four additional rounds compared baseline, `phy-1`, `phy-3`, and `gain-40`,
each with both badge directions and the same MCS7 challenge. Trial order was
reversed on rounds 2 and 4. Evidence is under
`build/clean-rx/badge/rf-followup-repeat-ROUND-VARIANT-m7-DIRECTION-*`.

| Image | Complete MCS7 exchanges in the four repeat rounds |
| --- | ---: |
| Baseline | 3/8 |
| `phy-1` | 6/8 |
| `phy-3` | 6/8 |
| `gain-40` | 4/8 |

The candidate register settings sometimes performed better in this small sample,
but neither removed corruption, and the matched gain tuple did not provide
reliable reception. These results are not qualification of new initialization,
calibrated power, or a general MCS7 fix. None is retained.

## 5: station TX cleanup and proposed PHY pulse

The old nonstation path clears TXE auxiliary control, TXE selection and crypto
state after TX; the live station path deliberately did not perform those Cortex
writes because PSM can already own an autonomous ACK/CTS or incoming RX.

Two isolated cleanup implementations were built:

- `cleanup1`: PSM-owned TXE/crypto cleanup.
- `cleanup2`: the same cleanup plus the proposed clock-control pulse.

Completed Cortex-originated station TX posts a separate SHM request while only
PSM is paused. This does not overwrite the autonomous-response state. The next
TX defers while that request remains outstanding, before touching the template
or crypto registers. PSM prioritizes a new RX event and defers cleanup for active
RX, MAC TX busy, missing MAC drain completion, or active PHY TX. ACK/CTS
completion uses the same cleanup owner.

The request and count occupy words `6ac` and `6ad`, between the bounded
12-word RX trace and HT early evidence at `6b0`; layout assertions cover this.
They are initialized before enabling the station.

The public Linux
[D11 register definitions](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h)
identify offset `492` as `psm_phy_hdr_param`, with reset bit 0, clock-enable bit 1,
and force-clock bit 2. They do not establish the meaning of bit 3 on this badge.
Therefore `| 000e` is not described here as a demonstrated PHY reset.
The pulse variant saves the complete original register, sets bits 1..3, executes
ten neutral instructions, and restores the original value; it does not blindly
write `0002` over unrelated bits.

Native tests execute the actual emitted instructions for busy/active/drain and
RX-priority gates, state consumption, one-shot cleanup, crypto/TXE reset values,
preservation of the PHY register, and the ten intervening neutral instructions.
Those tests are not a measurement of RF turnaround or silicon cycle timing.
The separate OFDM/HT guard-shortening experiment in the earlier investigation
was not retained. The existing conservative guard is not shortened as part of
these cleanup experiments.

## 6: long CCK, timer, and PLCP-derived length

The previous `442` low-byte `62` to `ff` experiment did not move the failure.
Both supplied dumps also contain the original complete value `0662`.
The arithmetic coincidence in the hint therefore does not establish that this
register causes a fixed 1,590-byte cutoff. The final accepted trace from the
previous investigation reached 1,818 bytes; it is not a fixed-cutoff proof.

The supplied packed reference microcode was checked specifically around the
PLCP parser and body-copy setup. Its paths at `886..89b` derive an expected
CCK/OFDM/HT length, but the actual copy-count write at `8b5` still takes the low
12 bits of `spr02b`, not that parsed expected length. There are additional
format/receive-status checks between those operations. This distinction matters:
the reference is not evidence that simply replacing a zero body-copy count with
the PLCP length fixes the failure.

The earlier partial 1-Mbps decoder did not fix long reception and was removed.
A full early PSM parser is not retained: early PHY format is demonstrably stale,
CCK signal `0a` collides with OFDM12, and the hardware copy-count semantics and
fresh format discriminator need verification before an all-rate replacement.
The existing C helpers validate PLCP/rate/length for transmitted frames and
terminal supported HT reception. They do not implement the proposed early
CCK/OFDM/HT copy-count substitution.

## Fresh-fixture runner defect found during final qualification

The fixed challenges used for the isolated screen and repeated comparisons have
ACK-fixture TID 0 and were valid. A subsequent fresh-challenge control exposed a
different problem: the runner filled nonce byte 6 randomly, but the diagnostic
normal-ACK fixture uses its low nibble as a TID and supports only 0..7. Values
8..15 caused firmware configuration rejection, `0x85`, before RF transmission.
This is a restriction of the diagnostic fixture, not of all firmware QoS receive
contexts or the Wi-Fi TID namespace.

Four of the first eight final fresh HT trials and three of six first low-rate
controls were rejected this way. They are excluded from RF pass/failure counts.
The original reports remain under `rf-followup-final-accepted-ht-*` and
`rf-followup-lowrate-control-*`, rather than being overwritten.

The runner now clears bit 3 of that nibble for fresh ACK challenges, preserving
the upper nonce bits and selecting a valid TID. Explicit replay challenges with
TID 8..15 fail validation before hardware access; explicit valid TID selection
and replay mismatches retain their existing behavior.

A no-USB test invoked the actual runner with a deterministic `od` test double
and a failing picotool executable. It exercised all 16 generated low nibbles and
verified mapping to 0..7, preservation of upper nonce bits and exact MCS/length.
All eight unsupported explicit replay nibbles exited 2 before producing a
configuration or running a USB operation. Corrected fresh OTA trials use the
separate `rf-followup-valid-tid-*` evidence prefix.

## Retained implementation and final build/legacy/protected checks

Only the ownership-gated PSM cleanup is retained from the new firmware trials.
The eight register overrides, four gain-tuple selectors and the PHY pulse trial
have been removed from source. The accepted automatic radio release and bounded
16/64 amplitude profiles remain. Neither a speculative full PLCP copy-count
replacement nor an unverified calibration sequence is presented as implemented.
Interactions between all possible register/calibration combinations were not
exhaustively tested.

Both controller and badge native suites and freestanding builds pass in these
final configurations; all six resulting ELFs have no undefined symbols:

| Configuration | Badge build |
| --- | --- |
| Default | `7da674bf` |
| Full gated HT/EDCA/WMM | `b078bf65` |
| Extended HT/EDCA without WMM | `5de11c6f` |

The final emitted-instruction tests cover RX/TX/drain deferral, RX-event priority,
scheduled-response ownership, one-shot request consumption, crypto/TXE values,
completion-count wrap and absence of PHY clock/reset writes. The badge suites
also cover the existing loader, secrets, WPA2 and HT helpers. Shell syntax checks
pass for all three paired runners.

Exact final default-image legacy results, under
`build/clean-rx/badge/rf-followup-final-accepted-*`:

| Trial | Deliveries and ACKs per direction | TX attempts, initiator/responder | Suppressed ACKs per endpoint |
| --- | --- | --- | --- |
| CCK1, 1,500-byte MSDU, A initiates | 32/32 | 45/44 | 1 |
| CCK1, 1,500-byte MSDU, B initiates | 32/32 | 33/33 | 1 |
| OFDM6, 32/1,500/2,304-byte cycle | 100/100 | 101/102 | 1 |

All six endpoint records have zero corruption, duplicate/out-of-order delivery
and controller error. Challenges are freshly generated for these final trials.

The exact final full-WMM image passed three separately configured vendor-AP
WPA2/WMM trials and a wrong-password negative control. Every positive recorded
one M1, one M2, one validated M3, one M4 and one protected echo; WMM negotiation
and EDCA were enabled. The independent vendor AP confirms authorization and
protected RX/TX. In the negative, association and M2 occur, but no validated
M3/M4, authorization, protected echo, or vendor-AP authorization occurs.
The vendor executable is unchanged and test credentials are private temporary
files removed by the runner.

The independent positive AP reports contain 49, 49 and 50 stable observations,
respectively.

### Corrected fresh HT and matched MCS0 controls

All eight corrected final fresh HT configurations have valid TIDs and zero
host/controller error. Complete exchanges are **3/4 at MCS0** and **2/4 at MCS7**.
The genuine MCS0 failure is a full 66-byte repeated-identical bad-FCS record,
with 16 MPDU fixture bit errors, before any response ACK is started. It is not
one of the rejected configurations and is not omitted from the results.
The challenge is `85f9115fb065077c4b729c3c0000`.

Two further matched rounds replayed exactly that challenge in both directions,
with the old accepted baseline and final image, reversing image order on the
second round. Final firmware passed **4/4** complete exchanges; the old baseline
passed **3/4**. The old-baseline failure had no early HT candidate, rather than
reproducing the exact 16-bit corrupt record. Evidence is under
`rf-followup-m0-matched-ROUND-{baseline,final-gated}-DIRECTION-*`.
This establishes a baseline MCS0 miss in the same bounded comparison, not
universal MCS0 reliability or a complete explanation of either failure.

Fresh valid-TID low-rate controls additionally found:

| Trial image | MCS0 complete exchanges | Observation |
| --- | ---: | --- |
| `phy-6`, PHY `807` bit 0 set | 0/2 | No early HT candidate in either direction, no controller error |
| `gain-60` | 2/2 | Complete RX and return ACK |
| `gain-70` | 2/2 | Complete RX and return ACK |

In contrast, those two lower gain tuples passed 0/2 MCS7 exchanges each in the
fixed-challenge initial screen. This is a rate-dependent observation, not proof
of a calibrated gain/power or clipping mechanism. It also illustrates why a
running vendor register value cannot simply be treated as a safe cold override.

HT reception remains a qualification limit, including the observed MCS0 miss;
no HT capability advertisement, protection, aggregate, Block ACK, or reordering
qualification is claimed.

### Exact final maximum-size CCK negative

The final full-WMM image was also tested with a fresh 2,304-byte, 1-Mbps
long-preamble MAC-soak challenge, target 16 deliveries per direction. Both
endpoints delivered zero, with zero controller error. The initiator exhausted
eight TX attempts; the responder never delivered the request and submitted
no response. This failure remains open.

Evidence is `build/clean-rx/badge/rf-followup-final-max-*`.
The responder's retained CCK trace is
`[1034,18656,25796,32768,6,6144,64,43941,18656,56835,1590,8437]`;
its sampled RX count is 1,590 bytes on this run. The earlier accepted run's
1,818-byte sample still prevents treating every failure as a proven universal
fixed 1,590-byte cutoff. Both endpoint FIFO-fault records are all zero.

## Full-flash preservation completed

After all trials, both fresh backup hashes were checked again. Both original
16-MiB images were restored with verification, followed by separate complete
`picotool save -a -v` reads and successful byte-for-byte `cmp` against the
backups:

- A: `build/clean-rx/badge/rf-followup-restored-A-readback.bin`.
- B: `build/clean-rx/badge/rf-followup-restored-B-readback.bin`.

Each independent readback is 16,777,216 bytes and matches the corresponding
original hash listed at the beginning of this record. Restoration/readback logs
are `rf-followup-{restore,readback}-{A,B}.log` in the same build directory.
No experimental or final clean image is left installed in place of either
original flash, and no healthy flash write was interrupted.


