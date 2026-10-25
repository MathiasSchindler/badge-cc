# Per-candidate SHM diagnostic and stopped first attempt, 2026-10-25

## Outcome

The first normal B-to-A ACK60/MCS7/channel-6 attempt stopped on a
**diagnostic publication failure**, not a qualified HT reception failure.
Both receiver snapshots are explicitly unavailable. The TX snapshot is
valid and retains a returned ACK; it does not repair either invalid report.
No negative control or subsequent RF attempt ran.

All three full original flashes were restored and independently compared by
size, SHA-256 and every byte. Separate final exact-serial checks confirmed
BOOTSEL. Eleven of twelve authorized slots remained unused; the collection
error ended the campaign rather than authorizing a retry with changed images.

Offline inspection found that the new diagnostic's SHM allocation was not
initialized before PSM startup. Its commit word could therefore reject an
otherwise successful publication. The source now clears the diagnostic's
50 words before starting the PSM, with a poisoned-memory native regression.
**At this campaign's end the repair had not been RF-tested.** It does not clear the receive buffer
or resolve the older radio/copy-provenance failures.
The separately authorized [repaired-image follow-up](HT_CANDIDATE_REJECTION_2026-10-25.md)
retains its own controls, failure and restoration rather than resuming this run.

## Question and implementation

The [initial-buffer comparison](HT_REJECT_BASELINE_2026-10-25.md) excluded
untouched startup bytes for its dual-receiver miss, not retained content
from an intervening candidate. The new default-off
`CLEAN_RX_HT_CANDIDATE_TRACE=1` records SHM words `0x801..0x821` immediately
before the original PC131 prefix-copy setup. It preserves PC130's original
bit-clear instruction and all seven general PSM registers.

The 50-word region at SHM `0x400..0x431` holds a candidate sequence, adjacent
TSF halves, raw RXE length/status/PHY, five copy operands, 66 pre-command bytes,
and rejection binding/commit words. Each candidate replaces the preimage
until the first eligible ACK60 rejection freezes it. Binding uses that
rejection's cause, TSF and length, not the rolling last-candidate trace.
The sequence can wrap to zero; commit is separate.

After the MAC/PSM stops, the controller publishes a 104-byte magic-prefixed
record at SRAM `0x20800`, aliasing only the inactive aggregate interface.
The host retains it separately until packet collection finishes, then
publishes it at report offset 1216 only for ACK60 modes 25/27, correct magic,
commit 0/1 and no retained-packet overlap. Its SHM allocation excludes the
early FIFO; its profile also excludes copy-scope, aggregate and compile-time
observer selections. The [tool guide](../guides/HOST_TOOLS.md#per-candidate-pre-copy-trace)
documents decoding and limitations.

No copy command, receive-buffer byte, threshold or PHY setting is changed.
The additional PSM instructions **do perturb receive-path timing** and need
fresh positive and negative controls. A byte match is not proof of staleness,
and a difference is not proof of on-air provenance.

## Authorization and retained attempt

Authorization covered badges A `08DA26F86E57F5EA` and B `9BBF88DD17D66E7D`,
plus Pico `6572BE3E4636AF51`: fresh matched images, two normal directional
controls, one deliberate bad-FCS calibration and at most nine normal
follow-ups. Stop on unexpected control failure, first useful spontaneous
failure/rejection or collection error. Channel 6, MCS7, one-shot ACK60,
200-ms receiver lead and 25-second collector bounds were retained.
No credentials or network association were needed. No independent physical
timing equipment was available.

Fresh full preflash reads matched the protected originals. Normal badge
build was `9c86ac92`, Pico `9e5bd520`; the archived bad-FCS badge build
`ce4664c8` was not installed. Normal controller SHA-256:
`a2a85a3f4c301151fe2a7c4281d7a9197b7fad2eb4749acc483719051136dc8f`.
Sources, dirty diff, build flags, exact binaries, collectors, oracle and
external-tool hashes were archived before flashing.

| Attempt | Direction | Challenge | TX | Active RX / passive Pico |
| --- | --- | --- | --- | --- |
| 1 | B to A | `0c48f09cc75566f64246993c0007` | Valid snapshot, submitted, returned ACK | Both unavailable |

Both raw receiver reports have `CLEAN_READBACK=4`, address `1216`, snapshot
status 2/error 4. Their retained packet sizes are respectively 78 and 32
bytes, below the 112-byte publication limit. The collectors correctly emit
null reception/submission fields, not zero-frame observations.
The harness stops on its first required-field assertion because unavailable
JSON intentionally has no success-shaped `error` field.

Neither failed report contains the rejected candidate structure: the
publication guard refused to copy it. Consequently the exact rejected
magic/commit value cannot be recovered from this evidence. The missing
initialization is a concrete code defect and a plausible mechanism for the
guard failure, **not a measured SHM value or established RF root cause**.

## Offline repair and validation

Startup previously cleared `0x600..0x7ff`, not the new `0x400..0x431`
allocation. The repair zeros all 50 diagnostic words while stopped and
invalidates its SRAM publication magic before PSM startup. It leaves
`0x801..0x821` unchanged.

- Native ASan/UBSan PSM execution covers the actual emitted hook, unchanged
  input bytes/copy operands/general registers, first-rejection binding,
  candidate replacement, immutable freeze, and sequence/TSF wrap.
- The actual startup initialization block is executed with poisoned SHM in
  both flag states: all 50 words clear only when enabled; surrounding words
  and receive bytes remain untouched.
- Native report tests cover mode, size, magic, commit and packet-overlap
  guards without neighboring report changes.
- All 28 independent HT verifier tests pass against the real C collector:
  active/passive records, pending/committed states, modular TSF order,
  required presence, raw/JSON tampering and byte comparisons.
- All four profile/layout/dispatch/startup tests pass, including default-off
  runtime identity and isolation. The repaired controller/host build passes.

These are offline results. They neither qualify the new probe on hardware
nor establish general RXE completion/copy semantics.

## Evidence, restoration and remaining work

The immutable archive is `build/d2-candidate-20261025/`. Its independently
run `harness/audit.cjs` replays all three raw reports through the archived
native collectors, verifies unavailable-versus-zero handling and stopping
conditions, checks both full preflash and restoration readbacks against the
protected originals, and verifies each exact-serial final BOOTSEL log.
`audit.json` records one attempted/zero usable trials, zero negative controls,
three native replays and verified restoration. `sealed-evidence.json`
protects the archive and audit; earlier seals were also verified.
This brings the separately authorized D2 copy investigations to 25 attempts.

Fresh matched-image RF authorization is required to calibrate the repaired
diagnostic; do not reuse or overwrite this failed campaign. Complete
spontaneous-frame provenance, general aggregate reception and station BA
integration remain unresolved. Negotiated HT stays disabled, all eight D2
work packages remain open, and physical SIFS requires independent equipment.
