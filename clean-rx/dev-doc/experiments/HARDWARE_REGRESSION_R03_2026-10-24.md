# R03 hardware regression: 2026-10-24

## Scope and conclusion

The user authorized the full documented RX/TX regression on both Universe
2026 badges and the Pico 2 W, including experimental aggregate/DMA diagnostics,
with complete flash backups, restoration and final BOOTSEL state.

R01-R03 now have hardware evidence: all 48 final aggregate pairs produced
complete, nonce/generation-bound snapshots on both endpoints, including
operations that failed. The original six-second diagnostic-report failure
no longer prevents classification. This is **not** a clean aggregate acceptance
result: DMA still fails, and isolated SHM/BA and ordinary HT/station trials
also failed. Every failed attempt was retained.

The [original regression](HARDWARE_REGRESSION_2026-10-24.md) is unchanged.
[Follow-up work](todo.md) and [current issues](../issues/README.md) own the
remaining work, rather than this historical record.

## Devices, images and provenance

| Role | Serial | Complete flash bytes |
| --- | --- | ---: |
| Pico 2 W | `6572BE3E4636AF51` | 4194304 |
| Badge A | `08DA26F86E57F5EA` | 16777216 |
| Badge B | `9BBF88DD17D66E7D` | 16777216 |

Local evidence is retained in
`build/radio-regression-r03-20261024/`:

- `manifest.json`: image-build revision, dirty-diff hash, profile flags and
  SHA-256 hashes of firmware, collectors and other built artifacts.
- `trials/`: fresh challenge/configuration, raw reports, JSON, stderr, process
  exits and independent pair-verification output, including failed attempts.
- `results.jsonl`: chronological phase/cell results. A logged disabled-probe
  rejection is an expected negative control, not a positive RF failure.
- `harness/`: retained one-time orchestration/readiness sources for provenance,
  not supported repository tooling or authorization to repeat the campaign.
- `cross-board-final/`: final three-node exchange evidence.
- `backups/` and `restoration/`: complete original saves, independent
  readbacks, bounded command logs and byte-comparison/hash results.

Image-build revision was `7b14ba062d5c2ae1450b6c300bd6cbb25c688b7b`
with uncommitted R01-R03 changes; this is not a claim that the revision alone
reproduces the images. The final image manifest records dirty-diff SHA-256
`bd6ad02fe3e67846a2e7cb692dbd76c0023b0e39e184466b5d5615174338db4a`.
Additional oracle regression tests and this documentation were added after
image building; firmware images were not changed afterward.

| Final badge variant | Selection/additional gates | Build ID |
| --- | --- | --- |
| Station/default | diagnostic/default | `1ac23e2e` |
| HT response | diagnostic/ht-response | `c8f15cf1` |
| Fixed HT probe | ht-response plus explicit probe gate | `3df04a13` |
| SHM aggregate | retained-SHM delivery | `817b7467` |
| Normal BA | retained-SHM delivery and BA | `43fd0e87` |
| Bad-middle BA | BA with bad-FCS mask 2 | `19eca9ae` |
| All-bad BA | BA with bad-FCS mask 7 | `e4080676` |
| DMA aggregate | DMA delivery | `e66c0c0a` |

Aggregate profiles use diagnostic/ht-data, the explicit aggregate gate,
interface 3 and handoff 2; exact flags are in the manifest.
The three-node radio application used build `054e69cc`.
The independent Pico vendor-AP fixture used build `a8b7510a`.
That AP is a station/security control, **not** evidence of source-built
Pico AP or WPA2 capability.

## Corrections made during the campaign

### USB readiness changed the probe timing

The initial one-time readiness wrapper was inserted into sequential probe
startup. Its approximately 2.14-second transmitter delay placed transmission
after the ordinary receiver's 2.5-second window. The controller image was
byte-identical to the previously successful controller image.

Those attempts were stopped and retained, not counted as firmware RF
failures. Subsequent HT/aggregate trials configured and verified **both**
USB endpoints before submitting either radio command; the receiver command
then started 200 ms before the transmitter.

The temporary guards bounded info/reboot to 15 seconds, load/save to
300 seconds and paired collectors to 25 seconds. They checked the actual USB
protocol/build identity. This one-time campaign harness does **not** implement
the repository-wide R04 orchestration work item.

### Controller generation binding

Initial R03 reports incorrectly bound their footer to USB generation.
Controller boot generation is the first little-endian nonce word OR 1.
Complete raw snapshots were correctly rejected by the freshness check,
despite real payload delivery.

Boot and report generation now share that derivation, with independent
negative tests. Earlier images, collectors, logs and manifest remain in
`pre-generation-fix/`; their rejected reports were not repaired.

### BA receipt ownership and explicit absence

The generation-corrected matrix exposed generic packet storage overlapping
the dedicated returned-BA record at offsets 1480-1519. A nonzero word there
was not proof of a received BA; generic capture could also overwrite an
actual receipt.

Aggregate modes now exclude generic packet capture and beacon inventory.
Aggregate BA transmitters explicitly initialize receipt magic, count zero
and an empty body before receiving. The collector distinguishes a valid
zero-count record from unavailable receipt evidence (`null`). The independent
oracle requires the initialized record and exact count, addresses, bitmap
and FCS where applicable. Native tests cover buffer ownership and zero/missing
receipt records.

The generation-corrected, pre-ownership-fix matrix was SHM 17/18, normal BA
18/18, bad-middle 4/4, all-bad 1/4 and DMA 0/4. Those are intermediate results,
not substitutes for the final matrix below. Its images/collectors/logs and
manifest remain in `pre-capture-ownership-fix/`.

## Final ordinary RX/TX results

| Test | Initial result | Fresh repeats / interpretation |
| --- | --- | --- |
| Three-node 6 Mbps OFDM request/echo | 330/330 | All six directed links, 55 exchanges each |
| Badge CCK 11 Mbps | 6/6 | Three each direction |
| HT 88-byte Data, MCS 0-7 | 16/16 | Both directions, channel 6 |
| HT 28/2332/3869-byte Data and clocked ACK, MCS 0/7 | 15/16 | A-to-B 28-byte MCS7 passed a fresh repeat |
| Disabled fixed-probe gate | 4/4 correctly rejected | Expected negative controls |
| Enabled fixed HT probe, MCS 0/7 | 4/4 | Both directions |
| Station lifecycle/network/security | 11/12 | Badge B lifecycle passed a fresh repeat |

The final cross-board campaign required one attempt for 300 exchanges,
two for 27 and three for 3. Maximum measured completion time was 645 ms.
This is 330 eventually verified exchanges, not 330 first-attempt successes.

Station tests covered lifecycle with one planned outage, 10-second networking,
WPA2 passphrase, raw PSK, wrong-password rejection and protected rekey on each
badge against the independent Pico AP. Badge B's initial lifecycle report
had four associations where the strict oracle expected two; that rejection
is retained separately from the passing repeat.

Both networking pairs delivered 35 station and 29 AP messages, including
17/14 large messages, with zero reported retries, one station host stall
and two AP host stalls. These are bounded smoke tests, not soak qualification.
Each protected-rekey trial verified three group and three pairwise changes;
Badge A/B recorded 338/336 protected echoes. Wrong-password trials passed
only the expected rejection control.

## Final aggregate matrix

SHM and normal BA covered MCS 0-7 on channel 6 plus MCS0/channel 1 in both
directions: 18 cells each. Bad-middle, all-bad and DMA covered MCS0/7 on
channel 6 in both directions: four cells each.

| Variant | Initial strict acceptance | Fresh repeats |
| --- | --- | --- |
| SHM | 17/18 | B-to-A MCS3 passed |
| Normal BA | 16/18 | B-to-A MCS2 passed; A-to-B MCS7 still failed |
| Bad-middle BA | 4/4 | Delivered bitmap 5, count 2 and returned BA bitmap 5 |
| All-bad BA | 4/4 | Three exact bad bodies observed; zero delivery/BA and terminal release |
| DMA | 0/4 | Not weakened to accept two-body delivery |

Every one of the 48 final receiver reports and 48 transmitter reports had
snapshot status 1, error zero, all six complete-read sections and valid
nonce/generation binding. This says readback succeeded, **not** that the
operation, fixture observation or terminal lease passed.

Specific remaining failures:

- SHM B-to-A MCS3 had a valid completed empty capture, no fresh fixture and
  no terminal publication/release; its fresh repeat passed.
- Normal BA A-to-B MCS7 initially had a failed sender operation
  (error 5, controller state 255) with complete fault readback. A fresh repeat
  completed transmission but still observed no fresh fixture at the receiver.
- Normal BA B-to-A MCS2 delivered all three exact bodies and released its
  lease; the receiver recorded one BA transmission but the sender did not
  receive a matching BA. Its fresh repeat passed.
- DMA A-to-B MCS0 and B-to-A MCS7 completed readback and reported only
  bitmap 3/count 2. The latter had three aggregate observations but still
  lacked the third independently validated body.
- DMA A-to-B MCS7 and B-to-A MCS0 preserved controller fault `0x8d`,
  original operation error 5 and valid complete snapshots. Each retained
  only the first fixture body; delivered values were unavailable, not zero.
  Fault addresses were 288422 and 292390 respectively, both with value 86;
  the diagnostic prefix bytes are retained in the receiver JSON/raw evidence.

R05's rerun was performed, but its full acceptance gate remains open.
R06 still owns DMA investigation; R07/R08 and broader qualification remain
open. There is no new general HT/BA, physical SIFS, vendor-parity or
production-security claim.

## Offline validation

All eight matched controller/badge variants built and passed their native,
strict, collector and independent-oracle suites. The existing development
tooling suite passed 38/38, including source-containment and dependency checks.
Known-zero and missing BA receipts were additionally exercised through
actual default and BA-enabled collectors. No package/framework/CI dependency
was added.

## Full-flash restoration

Before testing, each complete original flash was independently saved twice
and byte/hash compared. After all final trials and fresh repeats, each
original was loaded without execution, independently saved again and compared
byte for byte. All three devices were left in BOOTSEL.

| Device | Original and restored SHA-256 |
| --- | --- |
| Pico 2 W | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |
| Badge A | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Badge B | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |

`restoration/verified.jsonl` and per-device load/readback/BOOTSEL logs retain
the verification. Test firmware was not left on any device.
