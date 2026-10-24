# HT20 target and initial implementation

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

Development record: implementation milestones, experiments, failures and open
qualification work. For the current interface, use the
[HT20 reference](../../doc/HT20.md).

Status: **D1's conservative target/control mapping is complete**, with
bounded fresh HT20 captures at every MCS0-MCS7 in both badge directions.
This is **not an enabled or RF-qualified HT station**. Losses remain in the
single-frame trials. D2 implements the general PHY and
association path; D3 implements and qualifies the HT MAC. All implementation
and tests belong in `clean-rx/`; other experiment directories are not targets.

## Evidence and limits

The campaign's datasheet snapshot
`Infineon_CYW43439_DataSheet_v03_00_EN-3074791.pdf`
([manufacturer documentation](https://www.infineon.com/part/CYW43439))
identifies itself internally as document 002-30348 Rev. *B, despite the
filename. References below use its printed page numbers:

- Page 1 and section 1.2: single-band 2.4 GHz b/g/n and a shared antenna.
  Page 1 explicitly lists RX STBC and explicit transmit beamforming.
  Those hardware features are not implemented capabilities of this firmware.
- Section 14.2, Table 25, page 59: mixed-mode, 20-MHz reception at MCS0-MCS7,
  with the sensitivity test defined at 800-ns GI. MCS0-MCS7 are the
  single-spatial-stream HT rates.
- Section 14.3, Table 26: transmitter modulation/coding performance includes
  64-QAM 5/6. This is hardware evidence, not proof of our MCS7 TX.
- Section 11.2, page 44: the device ARM and D11 microcode implement protocol
  and time-critical processing. Host-side rate selection alone cannot add HT.

The datasheet is not a D11/LCN20 programming manual. Public Linux
`brcmsmac` provides useful related-family definitions and algorithms, pinned
to revision `58161ead29e50d70a317ea26b472e0c7508e69da`:

- [d11.h](https://github.com/torvalds/linux/blob/58161ead29e50d70a317ea26b472e0c7508e69da/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h):
  `txe_phyctl`, `txe_phyctl1`, `PHY_TXC1_*`, `FT_*`, `PRXS0_*`,
  `MIMO_PLCP_*` and `d11txh`.
- [rate.c](https://github.com/torvalds/linux/blob/58161ead29e50d70a317ea26b472e0c7508e69da/drivers/net/wireless/broadcom/brcm80211/brcmsmac/rate.c)
  and [rate.h](https://github.com/torvalds/linux/blob/58161ead29e50d70a317ea26b472e0c7508e69da/drivers/net/wireless/broadcom/brcm80211/brcmsmac/rate.h):
  `mcs_table`, rate specifications and modulation/coding control bytes.
- [main.c](https://github.com/torvalds/linux/blob/58161ead29e50d70a317ea26b472e0c7508e69da/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c):
  `brcms_c_compute_mimo_plcp`, `brcms_c_phytxctl1_calc`,
  `brcms_c_d11hdrs_mac80211` and `brcms_c_calc_lsig_len`.
- [ieee80211-ht.h](https://github.com/torvalds/linux/blob/58161ead29e50d70a317ea26b472e0c7508e69da/include/linux/ieee80211-ht.h):
  HT capability/operation layouts and aggregate-size encodings.

These references do **not** establish exact LCN20 behavior on this
CYW43439/D11 revision 39. In particular, Linux's FIFO TX descriptor and
microcode interface is not our direct-template TX interface. No Linux code
or vendor executable is added to either freestanding image.

HT-SIG CRC bit ordering was independently checked against
[OpenOFDM's CRC RTL](https://github.com/jhshi/openofdm/blob/ea638eb2d3abd6c6d9c0c87a430f393fc3ffc859/verilog/ht_sig_crc.v)
and [SIGNAL documentation](https://github.com/jhshi/openofdm/blob/ea638eb2d3abd6c6d9c0c87a430f393fc3ffc859/docs/source/sig.rst).
No implementation code was copied.

## Selected baseline

- 2.4 GHz, HT20, one spatial stream, MCS0-MCS7.
- HT mixed-format PPDU, 800-ns GI, BCC, one HT-LTF, no extension streams,
  no STBC or sounding.
- Begin with bounded, nonaggregated raw MPDUs within the existing
  2,400-byte storage limit. This is a PHY bring-up profile, **not a complete
  HT station profile**. Retain legacy b/g TX/RX and legacy basic-rate control
  responses.
- Do not advertise an HT association profile until its QoS, receive-size,
  protection and Block ACK obligations can actually be met.

HT20 carries 52 data subcarriers, unlike legacy OFDM's 48. Reusing a legacy
rate's airtime, SIGNAL layout or ACK-rate index for an MCS is incorrect.
The following combine specification/reference values with the chip controls
verified by fresh 88-byte PSDUs in both directions. Larger lengths have
native framing tests, not HT RF qualification:

| MCS | Modulation | Code rate | Data bits / 4-us symbol | PHY Mbps (800-ns GI) | Verified TX PHYCTL1 |
| --- | --- | --- | --- | --- | --- |
| 0 | BPSK | 1/2 | 26 | 6.5 | `0x0002` |
| 1 | QPSK | 1/2 | 52 | 13 | `0x0802` |
| 2 | QPSK | 3/4 | 78 | 19.5 | `0x0a02` |
| 3 | 16-QAM | 1/2 | 104 | 26 | `0x1002` |
| 4 | 16-QAM | 3/4 | 156 | 39 | `0x1202` |
| 5 | 64-QAM | 2/3 | 208 | 52 | `0x1902` |
| 6 | 64-QAM | 3/4 | 234 | 58.5 | `0x1a02` |
| 7 | 64-QAM | 5/6 | 260 | 65 | `0x1c02` |

PHYCTL1 values combine public `mcs_table[].tx_phy_ctl3 << 8` with
20-MHz bandwidth value 2 and SISO mode 0. They remain separate from the
legacy TX tables and accessible only through the gated diagnostic. A shared PHYCTL1
value with a legacy rate does not select the same PPDU format.

For this restricted BCC/non-STBC baseline, an L-byte PSDU including FCS
needs `Nsym = ceil((16 + 8*L + 6) / Ndbps)`. Mixed-format airtime is
`36 + 4*Nsym + 6` us in 2.4 GHz, including the 6-us signal extension.
The corresponding legacy 6-Mbps L-SIG protection length is `3*Nsym + 9`
bytes for this one-HT-LTF baseline. These formulas now have independent
boundary/known-answer tests in [ht20.h](../../src/ht20.h) and
[loader_test.c](../../badge/loader_test.c), but do not drive ordinary MAC
deadlines or NAV. They do not describe SGI, STBC, LDPC or aggregated frames.

## Confirmed direct-template mixed-mode interface

The earlier probes incorrectly wrote a serialized L-SIG across the two
mixed-mode registers. On hardware, writing packed `0x0bab` to `D11+0x510`
read back as `0x000b`. The new strict readback check rejected this before
arming TX (`0x8d`), rather than reporting a local submission.

For the tested profile the working interface is:

- `+0x50c = 0x0042`: selected HT format and existing antenna control.
- `+0x514 = PHYCTL1` from the verified MCS table.
- `+0x510 = 0x000b`: separate legacy 6-Mbps RATE, **not packed SIGNAL bits**.
- `+0x512 = 3*Nsym + 9`: separate mixed-mode protection LENGTH, **not the
  high byte of serialized L-SIG**. For the 88-byte test PSDU, MCS0-MCS7
  lengths are `93,51,39,30,24,21,21,18`.
- The template remains six HT-SIG bytes followed by the MPDU including FCS.
  All four control registers and the complete template are read back before
  TX. Control mismatch and nonzero PHY TX error are explicit failures.

An isolated CRC-only experiment zeroed HT-SIG bits 34..41 **after** validating
the host's exact synthetic frame, and verified the altered template readback.
The receiver still returned the fresh MPDU and valid, nonzero HT-SIG CRC.
Thus hardware regenerates the CRC in this direct-template path; software
still constructs/validates it for the input guard, RX and native vectors.
Software supplies zero tail bits and the restricted non-sounding profile.
Neither CRC regeneration nor register readback alone is OTA proof: the
fresh peer captures below provide that evidence.

## Mapping to the implementation

| Surface | Current source-built path | HT requirement / verification gap |
| --- | --- | --- |
| RF/channel | [radio_phy.c](../../src/radio_phy.c): existing calibrated Radio 20691/LCN20 channel-1, OFDM IIR/Farrow and TX sequencing path produced fresh HT captures at all eight rates | No speculative PHY register writes were needed for this bring-up. Channel/size coverage, power/EVM, loss and sustained operation remain unqualified. No HT40 channel programming. |
| TX format | [main.c](../../src/main.c), `d11_transmit_template_frame`: legacy formats unchanged; opt-in D11 `+0x50c = 0x42` verified by matching HT20 captures | Public `FT_HT=2`, `FT_N=3` and SHORT_HDR `0x10` are not interchangeable. Earlier `0x43` failures also had the incorrect mixed-mode fields, so do not prove that selector unusable; it is not retained or needed for this target. |
| TX rate | D11 `+0x514` receives validated legacy controls from [phy_rates.h](../../src/phy_rates.h), or separate HT20 controls from [ht20.h](../../src/ht20.h) | All eight HT words read back and produced fresh HT-SIG/MPDU captures in both directions. MCS7's 5/6 coding is not legacy 54 Mbps. General rate adaptation remains D2/D3 work. |
| TX framing | [ht20.h](../../src/ht20.h) constructs six HT-SIG bytes, software CRC/zero tail and a fixed synthetic MPDU; existing template readback and D11 `+0x56a` byte count retained | MCS/BW occupy byte 0, length bytes 1..2, flags byte 3, extension streams/CRC/tail bytes 4..5. Hardware CRC regeneration was separately tested; ordinary input validation remains strict. No general HT command/profile. |
| Mixed-mode protection | Public map identifies `txe_mmplcp0/1` at `+0x510/+0x512`; hardware requires separate RATE and LENGTH, not serialized L-SIG | `+0x510 = 0xb`, `+0x512 = 3*Nsym+9` works for the restricted profile. D11 `+0x508/+0x50a` remain watermark registers, not L-SIG registers. Broader profile rules are unimplemented. |
| Commands | Opcodes 5/10 remain legacy; opcode 11 is a default-disabled, exact one-frame HT diagnostic | Build-time gate, station-disabled requirement, exact command/length, HT-SIG and synthetic frame validation precede TX. This is not a general HT TX API. |
| RX framing | [psm_ucode.c](../../src/psm_ucode.c): six-byte prefix at SHM `0x101..0x103`, MPDU begins at `0x104`; streaming, GoodFCS and size bounds retained | All eight MCS values produced six-byte HT-SIG prefixes and RXE length 94 for 88-byte PSDUs; external MCS1 also established 63/57. Larger frames and broader profiles remain unqualified. |
| RX evidence | [main.c](../../src/main.c) snapshots the same completed capture's four held PHY words, prefix and up to 96 MPDU bytes; PSM also records early/header and terminal/error evidence | Successful controlled captures had terminal PHY status0 `0x0002`; external MCS1 had `0x0102`. Low format bits 2 are established at completion, not at the early prefix stage where PHY bits may be stale. High bits/format 3 remain unassigned. MCS/GI come from validated HT-SIG. |
| Host metadata | Packet byte 15 and byte 16 currently encode legacy rate/channel/flags; [phy_rates.h](../../src/phy_rates.h) marks formats 2/3 unsupported (`0x40`) | Design explicit/versioned MCS, bandwidth, GI, mixed-mode and aggregation evidence in D2. Do not alias HT onto OFDM flag `0x10` or treat CCK short-preamble flag `0x20` as SGI. Preserve fragment metadata. |
| ACK/CTS/IFS | [psm_ack.h](../../src/psm_ack.h) rejects HT/QoS by default; a separately gated normal-ACK prototype adds QoS policy and HT-to-legacy response-rate tables | Qualify returned ACKs/SIFS, HT protection/NAV, EOF anchoring and retries. Local response starts are not RF timing qualification. |
| Association/QoS | [station.h](../../badge/station.h) emits SSID, legacy rates and optional RSN; HT/WMM peer IEs do not enable local capabilities. Shared per-TID duplicate/reassembly state is integrated with existing protected QoS RX; plaintext station negotiation/delivery remains gated | D2 still needs AP-driven EDCA, HT responses/protection, Block ACK/reordering and negotiated QoS/HT operation. A receive state machine or WMM IE alone is insufficient. |
| Buffers/aggregation | [frame_limits.h](../../src/frame_limits.h) retains the default 2,400-byte MPDU bound; the opt-in extended RX profile supports 3,896 bytes and three F2 fragments | Minimum A-MSDU validation/deaggregation and bounded protected station reception are implemented below. Bounded A-MPDU parsing, per-MPDU CRC, BA agreement/BAR/window/reordering and ordered WPA2 delivery are now native-tested; [live aggregate capture and Immediate BA remain unimplemented](BLOCK_ACK_SOFTWARE.md). F2 fragmentation is neither 802.11 fragmentation nor aggregation. |

Important association constraint: an HT Capability element with the maximum
A-MSDU bit clear still declares a 3,839-byte A-MSDU limit, not "disabled".
A-MPDU exponent 0 means 8,191 bytes (`2^(13+0)-1`), not "no aggregation".
The extended profile below removes the raw minimum-A-MSDU storage blocker;
it does not supply A-MPDU or Block ACK operation. Buffer/streaming design and the mandatory
HT receive/Block ACK requirements must be resolved before claiming a
standards-complete HT station, even if raw nonaggregated HT works first.

## Optional capabilities: separate, disabled targets

| Capability | Evidence | Firmware policy |
| --- | --- | --- |
| Short GI (400 ns) | Not established by the cited 800-ns datasheet test; public-family fields exist | No SGI20/SGI40 advertisement or TX; separate airtime/RX verification required. |
| RX STBC | Explicit hardware feature on datasheet page 1 | Keep RX STBC capability zero until exact coding/status, extra HT-LTF and reception are implemented/tested. Do not infer TX STBC or multiple spatial streams. |
| Explicit transmit beamforming | Explicit hardware feature on datasheet page 1 | No beamforming capability advertisement, sounding, feedback processing or steering until separately implemented and verified. |
| HT greenfield, LDPC | Not established for this implementation | No capability advertisement or TX. |
| A-MPDU / Block ACK / A-MSDU | Public D11-family support is not a chip-specific, implemented receive path | No aggregation mode claims or ADDBA enablement. Resolve minimum HT receive obligations as above; D3 owns implementation and qualification. |
| HT40, MCS8+, MCS32, 5 GHz | Outside the selected datasheet-grounded HT20/single-stream/2.4-GHz target | Not enabled or advertised; do not infer support from generic public-driver fields. |

## Tests now in place

[loader_test.c](../../badge/loader_test.c) independently checks HT-SIG CRC,
airtime and L-SIG vectors, all supported lengths/MCS values, corrupted
headers, and every bit of the fixed probe command/frame. Disabled and
station-enabled gates reject before reading the frame. RX accepts both
smoothing values; TX keeps the deliberately fixed profile. Example vectors:

| MCS | PSDU including FCS | HT-SIG hex | Airtime us | L-SIG length |
| --- | --- | --- | --- | --- |
| 0 | 88 | `005800072400` | 154 | 93 |
| 7 | 88 | `075800078003` | 54 | 18 |
| 0 | 2332 | `001c09078803` | 2918 | 2166 |
| 7 | 2332 | `071c09072c00` | 330 | 225 |

[loader_test.c](../../badge/loader_test.c) tests all 32,768 raw PHY-status words
with format 2/3, at channels 1/6/14, with and without the legacy OFDM
fallback status bit: 196,608 combinations must remain unsupported, never
OFDM or short CCK. Open and WPA2-CCMP beacon fixtures include HT Capability,
HT Operation and WMM parameters; subsequent association requests remain
byte-for-byte legacy-only, with an explicit IE whitelist.

[psm_ucode_test.c](../../tests/unit/psm_ucode_test.c) verifies formats 2/3 do not
transmit an ACK or CTS, including conflicting short-header and legacy OFDM
status bits and a valid legacy SIGNAL value. Existing legacy ACK/CTS and
PHY tests continue to run. These exercise current conservative boundaries,
not HT decoding, peer interoperability, or independent RF timing.
Additional native tests exhaust all 65,536 early PHY words, preserve
legacy PLCP filtering, and check good/bad terminal evidence, mismatched
headers, completed-record preservation and 16-bit counter wrap. Probe
trace matching tests require terminal format 2, validated HT-SIG and the
entire fresh synthetic MPDU, including FCS. A valid RX smoothing-zero
profile is accepted. The CRC experiment tests exactly which template bits
may change and verifies that its altered header is still rejected as host
input.

Run:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx all test
make -C clean-rx/badge all test
```

## Bounded diagnostics

Default firmware rejects opcode 11 with error `0x85`, without TX submission.
An explicitly built `CLEAN_RX_HT_PROBE=1` controller permits exactly one
88-byte synthetic broadcast MPDU, no ACK, no association, MCS0-MCS7.
No existing TX command selects HT. Build both isolated images and their
matching collector:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ht-probe \
  EXTRA_CFLAGS=-DCLEAN_RX_HT_PROBE=1 all
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/ht-probe \
  CONTROLLER_BIN=../../build/clean-rx-ht-probe/cyw43_rx_fw.bin all
```

Back up both badges before installing
`build/clean-rx/badge/ht-probe/clean_rx.uf2` with picotool; verify and restore
their original contents afterwards. With both matching badges in BOOTSEL:

```sh
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_EVIDENCE_PREFIX 0
```

The script generates a fresh challenge, uses channel 1, launches badges
serially and collects bounded TX/RX reports. Collector options are
`--ht-probe-tx CHALLENGE_HEX_28 MCS`, `--ht-probe-rx CHALLENGE_HEX_28 MCS`
and `--ht-observe`; the last is a passive 2.5-second observation and also
works with default firmware. RX/passive modes disable TX.
Local submission is reported separately from fresh matching RX.
`fresh_ht_trace` identifies an exact fresh controller capture;
`fresh_ht_rx` additionally requires matching F2 delivery. Each individual
report leaves `tx_over_air_confirmed` false because it does not combine
both peers. A successful script writes `PREFIX-pair.json` with
`tx_over_air_confirmed:true`. `ht_interoperability_qualified` stays false:
paired diagnostics are not independent implementation qualification.

The 120-byte trace at controller SRAM `0x20300` has magic `0x31525448`,
capture count, RXE length, four PHY words, six HT-SIG bytes and 96 MPDU
bytes. Snapshotting replaces badge detail bytes 520..639 only in modes
21..23. The normal packet format and unsupported-HT metadata remain intact.
Only completed GoodFCS captures update it; it does not diagnose early/bad
PLCPs. The latest capture may overwrite an earlier one. Passive FCS
validation requires the entire MPDU to fit the 96-byte trace; a negative
report is not proof that no HT frame arrived.

The additional 36-byte diagnostic at SRAM `0x20380`, magic `0x33445448`,
is copied to detail bytes 640..675 in modes 21..23. It contains:

- A 20-byte early/terminal PSM trace from SHM `0x6b0..0x6b9`: 16-bit
  candidate count, raw early PHY word, six prefix bytes, initial/final
  RXE counts, two raw RX status words and outcome (0 not observed on the
  instrumented terminal paths, 1 GoodFCS path, 2 bad receive path).
- Four read-back TX words: PHYCTL, PHYCTL1, mixed-mode RATE, mixed-mode LENGTH.
- A flag reporting the CRC-only template experiment.

Early candidates use a prefix MCS/BW byte of 0..7, not the unreliable early
PHY format bits; CRC validation is reported separately. Candidates are not
necessarily valid HT frames. Terminal evidence requires format 2/3 and
the same full six-byte prefix; strict fresh matching accepts only the
verified terminal format 2. Early aborts before the prefix stage are not
captured, and outcome 0 is not a diagnosis of the cause of loss. A new
candidate can replace the old record. Global discard counters are also
reported, but include unrelated ambient traffic.

To reproduce only the CRC ownership experiment, use a **different**
controller build directory so changed compiler flags cannot reuse stale
objects; then rebuild the matching probe badge/collector:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ht-crc \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_PROBE=1 -DCLEAN_RX_HT_CLEAR_SIG_CRC=1' all
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/ht-probe \
  CONTROLLER_BIN=../../build/clean-rx-ht-crc/cyw43_rx_fw.bin all
```

Flash these matching probe images and use the same script. The second flag
cannot compile without the explicit probe gate. It only clears bits 34..41
while uploading the already-validated, fixed one-frame template; no MPDU,
tail, timing or other command restriction changes. Rebuild using the
ordinary probe commands afterwards. Both flags default to zero.

## Hardware evidence

Private raw evidence is retained under `build/clean-rx/badge/`; reports do
not print peer addresses or payloads.

- `d1-probe-mcs0-*` (`d7da4df1`, selector `0x42`) and
  `d1-probe-ft3-mcs0-*` (`0bba992b`, selector `0x43`) completed local MCS0
  TX with PHY error zero, but no fresh probe reception. Neither proves HT TX.
- During the latter trial both badges captured external HT frames, not
  fresh probes. HT-SIG `013900061402` decodes as HT20, MCS1, 800-ns GI,
  BCC, no aggregation/STBC, PSDU 57 bytes; RXE length was 63.
  HT-SIG CRC and MPDU FCS were independently verified. PHY status0 was
  `0x0102`; smoothing was zero, a valid RX value now covered by a regression
  vector. This is partial RX mapping evidence, not controlled peer
  interoperability or proof of all MCS values.
- Final retained selector `0x42`, opt-in build `6af99b15`:
  `d1-final-probe-forward-*` and `d1-final-probe-reverse-*` both submitted
  locally without PHY error but received no fresh matching HT frame.
- Default build `e07ad45f`, `d1-default-guard.*`: expected opcode-11
  rejection, firmware error `0x85`, no TX submission.
  Six complete passive trials (`d1-passive-{0,1,2}-{A,B}.*`) had no HT
  captures or controller errors. Uncontrolled ambient traffic absence is
  not a measured HT RX failure. A separate launch abort was excluded.
- Same-default-build cyclic-size legacy controls `d1-legacy-2-long-*` and
  `d1-legacy-ofdm6-*`: each passed 30 delivered/ACKed frames per direction,
  zero failures and zero PHY mismatches.

The above records describe the earlier, blocked mapping. Subsequent
hardware work resolved the mixed-mode interface:

- `d1-next-mcs0-*`, build `3b2757e9`: readback caught packed L-SIG
  `0x0bab -> 0x000b` at `+0x510`, error `0x8d`, **no TX submission**.
- `d1-split-mcs0-*`, build `bb663065`: separate rate/length read back as
  `[0x42,0x0002,0x000b,93]`; first fresh, complete MCS0 capture.
- `d1-rates-m{0..7}-d{0,1}-r{0..2}-*`, build `4bc556b3`: 48 separately
  challenged one-frame trials, three per rate/direction. Every cell
  produced fresh HT-SIG/GoodFCS MPDU evidence. There were 39 exact fresh
  controller captures and 38 full F2 deliveries; one GoodFCS fresh frame
  was present in the controller trace but absent from host delivery.
  HT-SIG CRC and MPDU FCS were independently checked, not just trusted
  from local TX status.

| MCS | Forward F2 matches / 3 | Reverse F2 matches / 3 |
| --- | --- | --- |
| 0 | 2 | 3 |
| 1 | 3 | 3 |
| 2 | 3 | 3 |
| 3 | 3 | 3 |
| 4 | 2 | 3 |
| 5 | 1 | 2 |
| 6 | 2 | 1 |
| 7 | 2 | 2 |

The extra raw-only fresh frame was reverse MCS5. Of the other nine
unmatched trials, four had no early candidate, three reached a bad receive
path with valid HT-SIG and RXE count 94, and two retained early evidence
without an instrumented terminal outcome. These are observations, not an
attribution of all losses to RF or one firmware defect.

- `d1-crc-m0-d{0,1}-r{0..2}-*`, build `636a7d0a`: verified zero CRC bits
  in the TX template, yet four fresh raw captures (three also delivered
  over F2) returned valid nonzero CRC `005800072400`. Both directions
  were represented. Independent CRC/FCS checks passed.
- `d1-final-split-*`, final normal probe build `c81def60`: no CRC
  alteration, MCS0 and MCS7 in both directions, three trials per cell;
  respectively 5/6 and 2/6 full fresh captures. Pair-summary output passed.
- `d1-end-default-guard.*`, final default build `b5ab34c5`: firmware error
  `0x85`, no HT submission. `d1-end-legacy-2-long-*` and
  `d1-end-legacy-ofdm6-*` each passed cyclic-size 30 delivered/ACKed frames
  per direction, zero failures and zero PHY mismatches.

This establishes D1's bounded control/framing map, **not HT reliability,
throughput, large-frame support, association or independent-peer
interoperability**. D2/D3 stay open.

Native loader/HT tests also passed with AddressSanitizer/UndefinedBehaviorSanitizer.
Both controller variants, both native loader variants, PSM/MAC, WPA2,
secret handling and existing Micro Signal tests/builds passed. Final images
remain freestanding. Final default controller text is 34,032 bytes;
normal opt-in text is 35,144 bytes; CRC-experiment text is 35,200 bytes,
with 4,736-byte BSS in each.

After these tests badge `08DA26F86E57F5EA` was restored and independently
compared over its original 33,604-byte application range (not full flash).
Badge `9BBF88DD17D66E7D` had its modified prefix restored, then all 16 MiB
independently compared against its original full-flash backup. Both were
left in BOOTSEL; the Mac's network configuration was unchanged.

## D2/D3 entry and remaining qualification

1. Turn the fixed diagnostic into a versioned, validated general HT packet
   API with explicit MCS/BW/GI evidence, without changing legacy metadata.
2. Extend channel-1/88-byte bring-up to independent HT20 peers, multiple
   sizes/channels, protection/timing, power/modulation quality and all-rate
   loss/error-path testing. Diagnose incomplete RX and F2 delivery losses
   separately. Do not infer remaining PHY-status fields from early samples.
3. Qualify sustained all-rate operation at D3's required frame counts,
   with explicit legacy fallback and regression controls.
4. Keep association legacy-only until QoS/EDCA, HT protection, receive-size
   obligations and required HT MAC behavior are implemented. D2 and D3
   must share this gate rather than treating association success as proof.

The existing full-size 1-Mbps CCK receive-truncation blocker is separate;
neither this mapping nor future HT success closes it.

## D2 implementation: bounded packet API and peer profiling

D2 is **in progress**, not a qualified HT association implementation. The
ordinary station path remains legacy-only. No HT, WMM, short-GI, STBC,
beamforming or aggregation capability has been added to association requests.
