# Matched probe crossover and passive rejection calibration, 2026-10-25

## Outcome

The new pre-command probe is **not necessary for the receive failure**.
A matched probe-off trial retained an identity-correlated bad-FCS rejection
with 15 incorrect MAC bytes. A different probe-off trial delivered the
complete correct body and started an ACK, but the transmitter received none.
These are separate body-reception and returned-ACK failures.

Both probe states delivered complete target bodies in 3/4 normal attempts.
Delivery plus returned ACK passed 2/4 probe-off and 3/4 probe-on attempts.
This small comparison does **not** establish a statistically meaningful
probe effect or prove that the probe is harmless.

The passive source Pico also retained three deliberately bad-FCS fixtures
byte-for-byte, its first such negative calibration in this investigation.
There is now bounded active/passive rejection calibration, not an independent
vendor receiver implementation or loss-free observer qualification.

All twelve authorized attempts completed with 36 valid frozen reports.
All three original flashes were restored and independently verified by
size, SHA-256 and every byte. Final exact-serial checks confirmed BOOTSEL.
No product receive/PHY fix was guessed; negotiated HT remains disabled.

## Question, authorization and matched inputs

The [preceding repaired-probe run](HT_CANDIDATE_REJECTION_2026-10-25.md)
stopped on its first normal control failure, before a positive or negative
probe calibration. The user explicitly authorized a new matched comparison:

- At most twelve one-shot ACK60/channel-6/MCS7 submissions: eight normal
  attempts balanced across badge direction/probe state, then four deliberate
  bad-FCS controls.
- Keep normal TX and the passive Pico probe-off; change the pre-command
  probe only on the active receiver.
- Retain normal reception misses as measured outcomes rather than stopping
  before the paired comparison can finish. Stop on collection/device errors
  or unexpected delivery/ACK in deliberate bad-FCS controls.
- Preserve the 200-ms receiver lead and 25-second collector bounds; retain
  every attempt and restore/full-verify all originals.

Badge A is `08DA26F86E57F5EA`, B `9BBF88DD17D66E7D`, passive Pico
`6572BE3E4636AF51`. No credentials, AP association, physical repositioning
or independent timing equipment were used.

The freshly built probe-off controller is byte-identical to the previously
calibrated normal controller. The bad-FCS TX controller is byte-identical to
the calibrated negative controller. The probe-on controller is byte-identical
to the preceding initialization-repaired probe image. All are diagnostic
`ht-response` selections with the same rejected-copy trace; the host-only
startup baseline is enabled on all receive images.

| Host image | Build ID | Use |
| --- | --- | --- |
| Probe-off badge | `1bac57e1` | All normal TX and probe-off active RX |
| Probe-on badge | `4f1f29f3` | Probe-on active RX only |
| Probe-off Pico | `149a1189` | All passive observations |
| Bad-FCS badge | `620fe7ac` | Last four TX submissions only |

Fresh full preflash reads matched the protected originals. Exact source,
dirty diff, flags, binaries, collectors, oracle, tool hashes and the fixed
trial plan were archived before flashing. Each attempt has a fresh challenge
and a fresh five-byte radio identity, checked against previous campaigns.
The new prefix never overwrote an earlier archive.

## Retained attempts

The normal pairs alternate probe order and repeat both directions. The four
negative attempts cover both directions and both active-receiver probe states.
Deliberately bad-FCS traffic is not counted as ordinary loss.

| Attempt | Direction | Target probe | Input | Complete target body | Returned ACK | Passive result |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | B to A | Off | Normal | Yes | Yes | Complete good fixture |
| 2 | B to A | On | Normal | Yes | Yes | No complete fixture |
| 3 | A to B | On | Normal | Yes | Yes | Complete good fixture |
| 4 | A to B | Off | Normal | Yes | Yes | No complete fixture |
| 5 | B to A | On | Normal | No | No | Bound corrupt rejection |
| 6 | B to A | Off | Normal | No | No | Bound corrupt rejection |
| 7 | A to B | Off | Normal | Yes | No | No complete fixture |
| 8 | A to B | On | Normal | Yes | Yes | Complete good fixture |
| 9 | B to A | Off | Bad FCS | Rejected, byte-exact | No | Byte-exact negative |
| 10 | B to A | On | Bad FCS | No corresponding rejection | No | Byte-exact negative |
| 11 | A to B | On | Bad FCS | Rejected, byte-exact | No | Byte-exact negative |
| 12 | A to B | Off | Bad FCS | Rejected, byte-exact | No | No corresponding rejection |

### Normal body failures: attempts 5 and 6

Attempt 5 has challenge `4746140bd2c486a23f71fb3c0007`. Both receive
devices retain bound cause-2, 66-byte rejections with matching repeated reads,
but complete MAC identity fails. The active body differs at all 60 MAC bytes;
the passive differs at 58. All 66 active rejected bytes differ from its own
pre-command snapshot. That observation does not promote a damaged,
identity-incomplete record into a proven transmitted frame.

Attempt 6 has challenge `5da5ad86c642163a847de23c0007`. The active
**probe-off** receiver retains requested receiver/transmitter/sequence
identity, with 15/60 incorrect MAC bytes and invalid body FCS. Its repeated
reads agree and its cause/TSF/length bind to the first-rejection copy record.
All 66 bytes differ from the initial stopped-MAC baseline. The passive
receiver retains a separate identity-incomplete rejection with 55 incorrect
MAC bytes.

Neither attempt has correlated F2 or an independently received complete good
fixture. These records establish corruption in retained receive evidence,
not whether it originated in TX/RF, physical reception or the common
source-built RX/copy path. The probe-off failure excludes the new probe as
the sole explanation; it does not remove other diagnostic perturbations.

### Delivered body, missing returned ACK: attempt 7

Challenge `4ef01b447b3467b1937fe83c0007` has complete 78-byte F2 evidence,
correct full MAC body/FCS and matching HT metadata in probe-off badge B.
It reports one ACK response started and zero response errors. Badge A's
clocked TX finishes with ACK result 2 and zero retained ACK captures.
The verifier classifies it as `ack_started_but_unwitnessed`.

Starting an ACK is not independent proof of its on-air bytes or physical
SIFS timing. The passive Pico did not acquire a complete fixture/ACK in
this attempt, so it cannot distinguish missing ACK transmission, late ACK
or failure to receive it. This failure must not be counted as a body miss.

## Negative controls and observer limits

Each deliberate negative flips only bit 7 of the final FCS byte after normal
TX command validation. Complete retained matching controls have normal
HT-SIG `073c0007fc03`, exactly the programmed 60-byte MAC body, matching
repeated reads, bound cause 2 and calculated-versus-retained CRC XOR
`0x80000000`. No negative was delivered or ACKed.

- Active exact negatives: attempts **9, 11, 12**.
- Passive exact negatives: attempts **9, 10, 11**.
- Both receivers exact in the same attempt: **9 and 11**.
- Probe-on active negative calibration passed for receiver B in attempt 11.
  Receiver A with the probe on did not retain the corresponding rejection
  in attempt 10; that role's negative calibration remains open.

Attempt 10 demonstrates a complete programmed negative at the Pico while
the active receiver has no corresponding rejection. It confirms the
stimulus reached that independently receiving device, not that both receive
locations necessarily saw the same waveform or that active firmware alone
caused the miss. Attempt 12 provides the converse: active exact rejection
with no passive counterpart.

The Pico received complete good fixtures in only attempts 1, 3 and 8.
It missed complete fixtures that the active target received and ACKed in
attempts 2 and 4, and the target-delivered body in attempt 7. Neither silence
nor the newly calibrated rejection path qualifies it as a loss-free witness.
It remains the same RX implementation on another device, not vendor
interoperability or an independent implementation.

## Independent audit, restoration and remaining gates

The immutable archive is `build/d2-probe-crossover-20261025/`.
`harness/audit.cjs` independently checks the twelve-entry plan, balanced
orders, fresh identities, fixed TX/Pico builds, embedded controller identities,
all 36 raw/JSON bindings and archived native-collector replays. It recomputes
requested bodies, good and programmed bad FCS, exact rejection bytes,
per-candidate comparisons and the separate body/ACK denominators.

The audit verifies all earlier seals, all fresh full preflash reads, all full
restoration reads, and separate final exact-serial BOOTSEL logs.
`audit.json` and its source/image/report inputs are protected by
`sealed-evidence.json`. There were no collection/startup/restoration errors.
The twelve attempts bring these separately authorized D2 copy investigations
to 38 attempts; this tiny crossover is not a reliability acceptance rate.

The experiment supplies limited usable probe controls and the first
byte-exact passive negative calibration, while reproducing two distinct
failure stages without the candidate probe. It does not identify a supported
product fix. The next discriminator is an independently calibrated receiver
implementation or RF capture for the probe-off corrupted-body and returned-ACK
cases, rather than assuming that another source receiver's silence proves
no transmission. General RXE completion, repeated variable-length aggregates,
station BA integration and independently measured physical SIFS remain open.
