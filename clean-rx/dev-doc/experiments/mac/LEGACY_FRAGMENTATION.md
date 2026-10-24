# Bounded legacy fragmentation qualification

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

Date: 2026-10-22. A2/A3 implementation progress; A1/A2/A3 remain incomplete.
Subsequent work adds
[completion-timeout recovery, failure localization and longer RF trials](COMPLETION_AND_RECOVERY.md),
including timer-reference, response-retirement, PHY-start and held-RX
completion fixes and a final-reply retry grace;
the original failures below remain part of the evidence.
All implementation, fixture, host and test changes stay under `clean-rx/`;
the root roadmap only links to this status.

## Implementation

- The existing reassembler supported sixteen fragments and 2,304-byte MSDUs,
  but the reliable controller TX admission path rejected nonzero fragment
  numbers and More Fragments before transmitting.
- A real two-fragment trial reproduced controller error `0x85`, address
  `0x0004a400`, value `0x000004a2` (the 1,186-byte PLCP plus fragment).
  A native assertion against `clean_station_tx_tid` reproduced the rejection.
- Header admission now permits bounded legacy To-DS Data fragments while
  preserving independent FCS, address, PLCP and reliable-command checks.
  Empty bodies, invalid fragment-15 continuation, Order, other DS layouts
  and fragmented management/QoS frames are rejected. Protected-fragment size
  accounting includes the existing 16-byte security overhead; protected
  fragmentation was not tested over RF.
- The fixture adds `frag2`/`frag16`: a full 2,304-byte MSDU in respectively
  two 1,152-byte or sixteen 144-byte payload fragments. PLCP, sequence/fragment,
  More Fragments, Duration and original FCS are generated per MPDU.
  Retry preserves sequence, fragment number and body, changing only Retry/FCS.
- Each fragment uses existing independently contended, bounded ACK/retry TX.
  This is not a SIFS-timed fragment-burst scheduler. Ordinary station callers
  do not start fragmenting automatically. No QoS/HT fragmentation is enabled.
- The receiver validates every bound-peer raw fragment's exact bytes, FCS,
  metadata and raw-FCS marker before calling the actual station adapter.
  That adapter handles incomplete assembly, duplicates and delivery.
  Completed fragmented MSDUs require its reconstructed-FCS marker.
- Existing `MSK3` status remains unchanged. `MSG1` at detail offset 584 adds
  25 little-endian words: magic, fragment count, ACKed MPDUs, checked raw MPDUs,
  reassembled MSDUs, last index, fragment bitmap, retry bitmap, sixteen
  original fragment FCS slots and reconstructed FCS. Last-index evidence
  complements receiver-side checking of every earlier index, not a retained
  capture of the entire run.
- Collector acceptance checks exact complete-MSDU and ACKed-fragment counts,
  all final fragment FCS values, reconstructed FCS, full bitmap and zero
  malformed/unsupported/expired/fragment-error MAC counts.
  The MAC fragment counter counts incomplete accepted assembly steps,
  therefore `COUNT * (fragments - 1)`, excluding duplicates.

## Native validation

Both controller and badge `all test` passed, including existing PSM, MAC,
Block ACK, loader/credit, crypto and shell syntax tests. MAC tests run with
ASan/UBSan.

New fixture coverage exercises two/sixteen fragments, all eight existing
PHY/preamble profiles, both roles, indices 0/4095/4096/99999, timer wrap,
per-fragment SC/More Fragments/Duration/FCS, exact full reconstruction,
Retry replay of every fragment, suppression and reconstructed-marker rejection.
Admission tests cover fragment-number/body-size/security-overhead boundaries,
invalid DS/Order flags, terminal-fragment continuation and other frame types.
Loader tests cover the new profiles and flipped reconstructed markers.
These native profiles are not RF qualification.

```sh
make -C clean-rx/src CC=clang \
  BUILD="$PWD/build/clean-rx/a3-fragments-20261022/controller" all test
make -C clean-rx/badge \
  BUILD="$PWD/build/clean-rx/a3-fragments-20261022/badge" \
  CONTROLLER_BIN="$PWD/build/clean-rx/a3-fragments-20261022/controller/cyw43_rx_fw.bin" \
  all test
```

## Hardware and retained evidence

Two badges on the laptop's USB hub:

- A: `08DA26F86E57F5EA`
- B: `9BBF88DD17D66E7D`

Both full 16 MiB flashes were backed up and verified before loading the
badge-specific diagnostic image. The Pico 2 W `6572BE3E4636AF51` was not flashed
or used by this task.

Evidence directory: `build/clean-rx/a3-fragments-20261022/` (local, ignored).
Each trial prefix retains config/challenge, two endpoint JSON reports,
2,048-byte detail binaries, collector stderr and BOOTSEL logs.
`badge/clean_rx.uf2` and its matching collector are the tested artifacts.
Use explicit serials for flashing and a fresh trial prefix.

For the table, counts and attempts are initiator/responder, and direction is
initiator -> responder. All RF trials used 6 Mbps OFDM on channel 1.
Do not pool results from different builds into one passing soak.

| Prefix | Build | Direction | Profile / target per direction / dropped ACKs per endpoint | ACKed complete MSDUs | Received complete MSDUs | Attempts | Outcome |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `control-20` | `c18ce8be` | A -> B | cycle / 20 / 0 | 20 / 20 | 20 / 20 | 22 / 21 | Pass |
| `frag2-first` | `c18ce8be` | A -> B | frag2 / 5 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | TX admission error `0x85`; responder inactivity |
| `frag2-fixed` | `60f95c36` | A -> B | frag2 / 5 / 0 | 0 / 0 | 0 / 0 | 8 / 0 | Eight ACK timeouts; no bound-peer fragments published; cause unresolved |
| `control-fixed` | `60f95c36` | A -> B | cycle / 5 / 0 | 5 / 5 | 5 / 5 | 5 / 5 | Pass |
| `frag16-first` | `60f95c36` | A -> B | frag16 / 5 / 0 | 5 / 5 | 5 / 5 | 80 / 80 | Pass |
| `frag2-reverse` | `60f95c36` | B -> A | frag2 / 5 / 0 | 5 / 5 | 5 / 5 | 10 / 10 | Pass |
| `frag2-100-drop2` | `60f95c36` | A -> B | frag2 / 100 / 2 | 100 / 100 | 100 / 100 | 214 / 203 | Pass |
| `frag16-100-drop2` | `60f95c36` | B -> A | frag16 / 100 / 2 | 51 / 50 | 50 / 51 | 823 / 815 | Controller TX fault; incomplete |
| `frag16-20-drop2` | `60f95c36` | A -> B | frag16 / 20 / 2 | 20 / 20 | 20 / 20 | 322 / 322 | Pass |

The passing 100-MSDU two-fragment retry trial ACKed exactly 200 distinct
MPDUs per endpoint. A/B recorded respectively 14/3 ACK timeouts, 3/11
suppressed received duplicates and exactly two deliberately suppressed ACKs
each. The passing twenty-MSDU sixteen-fragment trial ACKed 320 distinct MPDUs
per endpoint, each with two ACK timeouts, two suppressed received duplicates
and two deliberately suppressed ACKs. Both passed exact maximum-size payload
integrity and ordering with zero duplicate application deliveries.

The longer sixteen-fragment trial failed on A with controller error `0x82`,
substage 5, address `0x1800150e`, value `0x00c1002f`: the previously observed
TX-completion fault, not a newly qualified outcome. B then timed out awaiting
its next complete response. At failure B had ACKed 816 MPDUs and reassembled
50 MSDUs; A had ACKed 810 MPDUs and reassembled 51. Neither reported corrupt
application payloads. These partial counts do not make that trial a pass.

## Independent host cross-check

A Python standard-library oracle, without the C template/CRC helpers, checked
all eight endpoint records from the four passing fragmented trials. It
reconstructed the nonce fields, addresses, pattern, sequence/fragment numbers,
More Fragments, Retry and Duration and used `binascii.crc32`. It checked 72
original final-MSDU fragment FCS values and eight reconstructed FCS values
against both binary detail and JSON, plus counts/MAC errors and single-byte
corruption negative controls. Output: `independent-fragment-oracle.log`.

For independent reproduction, the nonce is the 14-byte config challenge with
bytes 8..11 replaced by little-endian COUNT, byte 12 by dropped ACKs, and
byte 13 by 40 (`frag2`) or 48 (`frag16`). The payload begins with little-endian
`0x314b534d` and the application index; subsequent byte `i` is
`nonce[i % 14] XOR ((index >> (8 * (i & 3))) & 255) XOR ((i * 37) & 255)`.
Use the sender/receiver role-derived addresses and final index `COUNT - 1`.
At 6 Mbps ERP-OFDM, airtime for `n` MPDU bytes is
`26 + 4 * ceil((16 + 8*n + 6) / 24)` microseconds, including signal extension.
ACK airtime is 50 us. Nonfinal Duration is
`30 + 2*50 + airtime(fragment_payload + 28)`; final Duration is 60 us.
The reconstructed header clears Retry/More Fragments and the fragment number,
but retains fragment zero's Duration; append the full payload before CRC32.

The first oracle drafts incorrectly assumed the MAC fragment counter included
final delivery and omitted ERP's six-microsecond signal extension. Assertions
rejected those assumptions; correcting the independent formulas passed against
the unchanged captured evidence. This did not require firmware changes.
This oracle checks final-index retained evidence, not every raw OTA fragment
or RF timing. Independent peer-side validation checks each earlier payload.

## Flash restoration

After the trials, both original full 16 MiB images were restored with explicit
serial selection and picotool verification. Separate full-flash readbacks
matched the original backups byte for byte with `cmp`; SHA-256:

| Badge | Original and restored-readback SHA-256 |
| --- | --- |
| A | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| B | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |

Evidence includes `<serial>-original.bin`, `<serial>-restored.bin`, backup,
restore and readback logs in the same directory. Final picotool enumeration
confirmed both badges and the untouched Pico 2 W in BOOTSEL. The restored
badge images are the previously installed three-device radio diagnostics,
not the new A3 fragment fixture.


