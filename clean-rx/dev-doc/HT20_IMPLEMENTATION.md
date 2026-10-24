# HT20 implementation (milestones D1/D2)

Development record: implementation milestones, experiments, failures and open
qualification work. For the current interface, use the
[HT20 reference](../doc/HT20.md).

Status: **D1's conservative target/control mapping is complete**, with
bounded fresh HT20 captures at every MCS0-MCS7 in both badge directions.
This is **not an enabled or RF-qualified HT station**. Losses remain in the
single-frame trials. D2 implements the general PHY and
association path; D3 implements and qualifies the HT MAC. All implementation
and tests belong in `clean-rx/`; other experiment directories are not targets.

## Evidence and limits

The repository's [CYW43439 datasheet](../../pimoroni/Infineon_CYW43439_DataSheet_v03_00_EN-3074791.pdf)
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
boundary/known-answer tests in [ht20.h](../src/ht20.h) and
[loader_test.c](../badge/loader_test.c), but do not drive ordinary MAC
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
| RF/channel | [radio_phy.c](../src/radio_phy.c): existing calibrated Radio 20691/LCN20 channel-1, OFDM IIR/Farrow and TX sequencing path produced fresh HT captures at all eight rates | No speculative PHY register writes were needed for this bring-up. Channel/size coverage, power/EVM, loss and sustained operation remain unqualified. No HT40 channel programming. |
| TX format | [main.c](../src/main.c), `d11_transmit_template_frame`: legacy formats unchanged; opt-in D11 `+0x50c = 0x42` verified by matching HT20 captures | Public `FT_HT=2`, `FT_N=3` and SHORT_HDR `0x10` are not interchangeable. Earlier `0x43` failures also had the incorrect mixed-mode fields, so do not prove that selector unusable; it is not retained or needed for this target. |
| TX rate | D11 `+0x514` receives validated legacy controls from [phy_rates.h](../src/phy_rates.h), or separate HT20 controls from [ht20.h](../src/ht20.h) | All eight HT words read back and produced fresh HT-SIG/MPDU captures in both directions. MCS7's 5/6 coding is not legacy 54 Mbps. General rate adaptation remains D2/D3 work. |
| TX framing | [ht20.h](../src/ht20.h) constructs six HT-SIG bytes, software CRC/zero tail and a fixed synthetic MPDU; existing template readback and D11 `+0x56a` byte count retained | MCS/BW occupy byte 0, length bytes 1..2, flags byte 3, extension streams/CRC/tail bytes 4..5. Hardware CRC regeneration was separately tested; ordinary input validation remains strict. No general HT command/profile. |
| Mixed-mode protection | Public map identifies `txe_mmplcp0/1` at `+0x510/+0x512`; hardware requires separate RATE and LENGTH, not serialized L-SIG | `+0x510 = 0xb`, `+0x512 = 3*Nsym+9` works for the restricted profile. D11 `+0x508/+0x50a` remain watermark registers, not L-SIG registers. Broader profile rules are unimplemented. |
| Commands | Opcodes 5/10 remain legacy; opcode 11 is a default-disabled, exact one-frame HT diagnostic | Build-time gate, station-disabled requirement, exact command/length, HT-SIG and synthetic frame validation precede TX. This is not a general HT TX API. |
| RX framing | [psm_ucode.c](../src/psm_ucode.c): six-byte prefix at SHM `0x101..0x103`, MPDU begins at `0x104`; streaming, GoodFCS and size bounds retained | All eight MCS values produced six-byte HT-SIG prefixes and RXE length 94 for 88-byte PSDUs; external MCS1 also established 63/57. Larger frames and broader profiles remain unqualified. |
| RX evidence | [main.c](../src/main.c) snapshots the same completed capture's four held PHY words, prefix and up to 96 MPDU bytes; PSM also records early/header and terminal/error evidence | Successful controlled captures had terminal PHY status0 `0x0002`; external MCS1 had `0x0102`. Low format bits 2 are established at completion, not at the early prefix stage where PHY bits may be stale. High bits/format 3 remain unassigned. MCS/GI come from validated HT-SIG. |
| Host metadata | Packet byte 15 and byte 16 currently encode legacy rate/channel/flags; [phy_rates.h](../src/phy_rates.h) marks formats 2/3 unsupported (`0x40`) | Design explicit/versioned MCS, bandwidth, GI, mixed-mode and aggregation evidence in D2. Do not alias HT onto OFDM flag `0x10` or treat CCK short-preamble flag `0x20` as SGI. Preserve fragment metadata. |
| ACK/CTS/IFS | [psm_ack.h](../src/psm_ack.h) rejects HT/QoS by default; a separately gated normal-ACK prototype adds QoS policy and HT-to-legacy response-rate tables | Qualify returned ACKs/SIFS, HT protection/NAV, EOF anchoring and retries. Local response starts are not RF timing qualification. |
| Association/QoS | [station.h](../badge/station.h) emits SSID, legacy rates and optional RSN; HT/WMM peer IEs do not enable local capabilities. Shared per-TID duplicate/reassembly state is integrated with existing protected QoS RX; plaintext station negotiation/delivery remains gated | D2 still needs AP-driven EDCA, HT responses/protection, Block ACK/reordering and negotiated QoS/HT operation. A receive state machine or WMM IE alone is insufficient. |
| Buffers/aggregation | [frame_limits.h](../src/frame_limits.h) retains the default 2,400-byte MPDU bound; the opt-in extended RX profile supports 3,896 bytes and three F2 fragments | Minimum A-MSDU validation/deaggregation and bounded protected station reception are implemented below. Bounded A-MPDU parsing, per-MPDU CRC, BA agreement/BAR/window/reordering and ordered WPA2 delivery are now native-tested; [live aggregate capture and Immediate BA remain unimplemented](BLOCK_ACK_IMPLEMENTATION.md). F2 fragmentation is neither 802.11 fragmentation nor aggregation. |

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

[loader_test.c](../badge/loader_test.c) independently checks HT-SIG CRC,
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

[loader_test.c](../badge/loader_test.c) tests all 32,768 raw PHY-status words
with format 2/3, at channels 1/6/14, with and without the legacy OFDM
fallback status bit: 196,608 combinations must remain unsupported, never
OFDM or short CCK. Open and WPA2-CCMP beacon fixtures include HT Capability,
HT Operation and WMM parameters; subsequent association requests remain
byte-for-byte legacy-only, with an explicit IE whitelist.

[psm_ucode_test.c](../tests/unit/psm_ucode_test.c) verifies formats 2/3 do not
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

### HT data API version 1

`CLEAN_RX_HT_DATA=1` enables a separate, opt-in controller build. It does
not require or weaken the fixed D1 probe gate. Opcode 12 accepts exactly
`12 | clean_tx_length_bits(6 + MPDU_bytes) | (1u << 19)`: one frame,
no retries, no ACK request, no extra flags and no station profile.
The staging buffer begins with this eight-byte descriptor:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 1 | API version, exactly 1 |
| 1 | 1 | Profile flags, zero: HT20 mixed, BCC, 800-ns GI, no aggregation/STBC |
| 2 | 1 | MCS, 0..7 |
| 3 | 1 | Reserved, zero |
| 4 | 2 | Little-endian PSDU/MPDU length, including FCS |
| 6 | 2 | Reserved, zero |
| 8 | 6 | Validated HT-SIG, consistent with the descriptor |
| 14 | variable | MPDU including software FCS |

This is a general **payload** diagnostic, not an unrestricted MAC injection
interface: version 1 requires an unprotected, non-QoS, unfragmented data
header, zero Duration, a locally administered unicast source and broadcast
RA/BSSID. Allowed MPDUs are 28..2,332 bytes: a 24-byte header, at most
2,304 payload bytes and four-byte FCS. The underlying default 2,400-byte storage
capacity does not enlarge that non-QoS MSDU limit. Reserved fields, invalid
length/MCS/CRC/FCS, unsafe headers, default builds and station profiles
are rejected with firmware error `0x85` before TX. The same template/control
readback and explicit PHY-error checks as D1 apply. The CRC-alteration
experiment cannot affect this command.

Opt-in RX retains the existing 18-byte packet header and MPDU offset:

- Byte 15: validated MCS index.
- Byte 16: `channel | 0x80 | 0x40`. The new `0x80` marks HT metadata;
  `0x40` remains set so legacy consumers reject it rather than interpret an
  MCS as a CCK/OFDM rate.
- Byte 17: metadata profile/version 1, establishing HT20 mixed/BCC/800-ns GI
  and nonaggregation from a CRC-checked six-byte HT-SIG and terminal PHY
  format 2. Unsupported HT profiles and format 3 stay unversioned/unsupported.
  Legacy metadata and the separate non-HT reassembly marker are unchanged.

The host verifies the profile, MCS/channel, full fresh payload and FCS after
one- or two-fragment F2 delivery (three for the extended A-MSDU fixture below).
Native tests exercise the exact physical
fragment boundary, not just small-packet framing helpers.

The opt-in controller also retains a completed capture across host-credit
or SDIO-descriptor backpressure, instead of releasing an unposted frame.
This addresses an identified drop path behind the D1 raw-capture/host-delivery
gap; it does **not** prove every D1 loss had this cause. While the single
capture is held, later RF frames may still be missed. This is not a receive
queue or a sustained-reliability qualification.
Explicit raw-mode TX/rearm, channel-change and crypto commands retain their
existing capture-discard semantics; they now also invalidate prepared
transport state so a later capture cannot publish stale saved bytes.

### HT/WMM peer profile and legacy fallback

The beacon parser now records HT Capabilities, HT Operation and WMM AP
parameters, including the receive/basic MCS masks, primary/secondary channel,
20/40-MHz operation, protection mode, A-MPDU exponent/spacing and all four
ACs' AIFSN, ACM, ECWmin/max and TXOP (32-us units). It accepts AC records
in any order, rejects duplicate/invalid AC records for HT eligibility and
distinguishes WMM Information from the EDCA Parameter element. WMM Information
alone does not provide usable AP EDCA parameters.

Malformed/duplicate optional HT/WMM elements disable HT eligibility without
preventing the unchanged legacy association. Beacon refresh records parameter
changes without resetting the legacy DCF merely because a WMM parameter set
changed. Association requests remain byte-for-byte legacy equivalents in
open and WPA2 fixtures, including after those updates.

Station reports include `ht_advertised:false` and the fallback reason bitmask:

| Bit | Reason |
| --- | --- |
| 1 | Missing HT capability/operation |
| 2 | Malformed, duplicate or inconsistent optional elements |
| 4 | Unsupported/mismatched primary channel |
| 8 | No usable MCS or unsupported/inconsistent basic MCS |
| 16 | Missing WMM EDCA parameters or BE admission control required |
| 32 | Local HT MAC/receive prerequisites not implemented |
| 64 | Peer protection requirements not implemented |

Bit 32 is currently unconditional. Parsing advertised EDCA parameters is
**not** EDCA execution, admission control or negotiated QoS operation.
The original compact 44-byte version-2 peer report starts at badge detail offset 1040,
after unicast status and before the saved-packet length. It does not overlap
the association response at offsets 800..991. An integrated native test
requires association evidence to remain valid after publishing the report.
The [association/runtime extension](#association-response-profiles-and-runtime-htwmm-updates)
uses the remaining four bytes before offset 1088 for a change counter.

### Extended receive profile and minimum A-MSDU diagnostic

The required D3 receive prerequisites are being pulled forward into D2.
`CLEAN_RX_HT_RX=1` selects 3,896-byte raw MPDU storage, sufficient for a
3,839-byte A-MSDU plus a 26-byte QoS header, CCMP overhead and FCS.
This is storage capacity, not a claim that protected aggregates or
negotiated QoS delivery are implemented.

The profile uses upper capture SHM at **word indices** `0x800..0xf9f`.
With MAC/PSM halted, startup writes distinct sentinels at words
`0`, `0x400`, `0x800`, `0xc00` and `0xfff`, reads all back to detect
bank aliasing/inaccessibility, then clears them before PSM startup.
Failure stops initialization with `0x8e`. This passed on both chips.
Expanded capture slots and DMA buffers are relocated as documented in the
[memory map](../doc/HARDWARE_AND_FIRMWARE.md#memory-layout).

An eight-word layout manifest at controller SRAM `0x203c0` is required by
the loader before RX starts. Default and extended images publish their
actual capacity, SHM/ring/staging bases, fragment limit and DMA buffer size;
transport-only firmware publishes the same manifest without accessing SHM.
Missing or mismatched fields fail boot with `CLEAN_INPUT`, never an assumed
legacy layout. The default build retains its existing addresses and sizes.
Rebuild older embedded controllers when using the updated loader.

The extended F2 path supports three physical fragments per logical frame.
Only complete, consistent assembly advances the logical-frame credit.
Native tests exhaust lengths through 3,896 bytes, rounded physical lengths,
credit wrap, incomplete delivery and third-fragment offset/sequence/
length/PHY/version corruption. All manifest-field mismatches are tested.
The native test commands now receive the selected build flags; compiler
flags are tracked to prevent stale controller/native objects. Badge build
IDs include host flags, and an explicitly supplied controller binary is
hashed/embedded without silently rebuilding it with different flags.

Opcode 12 also accepts **API version 2 only in the extended build**, with
the same eight-byte descriptor and zero flags/reserved fields. It permits
only the exact nonce-derived 3,869-byte fixture, not arbitrary QoS injection:

- Unprotected, unfragmented 26-byte QoS data header, TID 0, No Ack policy.
- Locally administered unicast RA/TA and BSSID, zero Duration.
- A-MSDU-present bit set; no HT-SIG A-MPDU aggregation bit.
- Exactly 3,839 aggregate bytes: 14-byte subframe header + 2,304-byte
  MSDU + two padding bytes + 14-byte header + 1,505-byte MSDU.
- Four-byte FCS, with the entire frame/profile/nonce validated before TX.

Version 1 remains byte-compatible and keeps its 2,304-byte MSDU limit.
Intermediate lengths 2,333..3,868 are rejected, rather than sending an
oversized non-QoS MSDU. RX metadata remains profile/version **1** because
the PHY is still HT20/BCC/long-GI/non-A-MPDU; descriptor API version 2 is
not a new PHY metadata version.

The shared `clean_amsdu_open` validates the complete plaintext aggregate
before `clean_amsdu_next` exposes any zero-copy subframe. It checks every
header, big-endian payload length, per-MSDU/aggregate bound, source
address and inter-subframe padding boundary, rejects trailing/truncated
subframes and RFC1042 LLC-as-DA confusion, and accepts arbitrary padding
contents. The diagnostic uses it after full-frame FCS/nonce checking.
It does not replace negotiated peer filtering, per-TID duplicate/reorder
state, CCMP MIC/replay validation or aggregate-injection mitigations in
the future protected station path.

Build the matching pair explicitly:

```sh
make -s -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ht-rx \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1' all test
make -s -C clean-rx/badge BUILD=../../build/clean-rx/badge/ht-rx \
  CONTROLLER_BIN=../../build/clean-rx-ht-rx/cyw43_rx_fw.bin \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1' all test
```

After preservation/installing that image on both badges:

```sh
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 3869 --extended-rx
# Existing non-QoS fixture through the same extended receive layout:
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 2332 --extended-rx
```

Build `941546f0`, `d2-amsdu-matrix-*`, completed 16/16 rate/direction
trials, with **12 fresh full host deliveries**. Reverse MCS0 had no early
candidate; forward MCS4/MCS5/MCS7 retained early error outcome 2 without
a completed capture. All four failures remain unresolved. The separate
12 `d2-amsdu-repeat-*` trials yielded seven fresh deliveries, giving at
least one full minimum-size aggregate at every MCS in both directions.
Those repeats are not a replacement for the original failed trials or
a reliability qualification. An initial MCS0 forward fixture also passed.
Independent Python reconstruction/zlib checked the saved prefix,
metadata and expected full-frame FCS for all 12 fresh matrix frames;
the badge checked the full payload and both subframes. USB still saves
only a prefix, not an externally inspectable complete aggregate.

Final build `9a581374`, after explicitly halting/verifying MAC/PSM for
the bank test and moving manifest publication to include transport-only
images, passed `d2-amsdu-final-{forward,reverse}-*` at MCS0. Default build
`18882dbb` rejected opcode 12 before TX (`0x85`) and passed the separate
OFDM6/2-Mbps-long controls: 30 ACKed/delivered frames per direction each,
cyclic 32/1,500/2,304-byte payloads. Native/default/extended suites and
Micro Signal regression builds passed. Both full 16-MiB original flashes
were restored and independently compared with fresh pre-test snapshots;
both badges are in BOOTSEL.

**D2 remains open:** actual EDCA/QoS scheduling, HT ACK/protection,
negotiated/OTA QoS and protected aggregate integration, Immediate Block ACK
negotiation/window/reordering, required A-MPDU reception, HT association
and independent HT20 interoperability remain prerequisites. Neither a
No-Ack A-MSDU diagnostic nor permanently declining ADDBA closes them.

### Per-TID QoS receive and CCMP integration

The shared MAC now provides `clean_qos_rx` and
`clean_qos_rx_authenticated`, backed by 16 independent TID contexts.
Each context reuses the legacy bounded history and fragment engine:
64 duplicate-history entries, one active MSDU, at most 16 fragments,
2,304 reassembled payload bytes and a 512-ms incomplete-assembly lifetime.
Sequence wrap and protected/unprotected cache domains remain distinct.
The existing non-QoS API still rejects QoS rather than aliasing its state.

This supports three-address QoS Data/Null and normal/No Ack policy.
HT Control, four-address and Block ACK/No Explicit Ack policy need separate
implementation and remain rejected. Unprotected A-MSDUs must pass complete
aggregate validation before their identity can enter the duplicate cache;
fragmented aggregates are invalid. The subsequent
[protected A-MSDU integration](EDCA_IMPLEMENTATION.md#protected-a-msdu-receive-integration)
adds bounded authenticated aggregate reception, explicit station admission
and aggregate-injection rejection. It does not qualify negotiated HT/BA
operation.

The existing software CCMP QoS path now authenticates/decrypts, enforces its
per-TID PN replay counter, reconstructs an internal plaintext FCS, then uses
the new authenticated MAC state. Fragments must have consecutive PNs within
their TID, consistent addresses/header identity and the expected fragment
number. Valid-MIC Retry identities with a newer PN are not delivered twice;
the replay counter still advances. Ordinary protected retries retain their
PN and are rejected as replays before they can affect assembly.

`clean_wpa_rx_expire` is serviced by station polling, so an incomplete
plaintext assembly does not need another packet on its TID to expire.
Expiry, terminal protocol failure and delivery wipe the corresponding
plaintext/fragment PN state. A terminal crypto/protocol failure clears all
TID plaintext assemblies. The fixed protected receive report keeps its
layout and sums legacy plus QoS counters. State is static; this increment's
default and extended badge BSS was 221,256 and 225,744 bytes respectively.
Later WMM/aggregate build sizes are recorded with their own evidence.
No allocator or new library dependency was added.

Native ASan/UBSan tests cover all 16 TIDs with interleaved 16-fragment
32/1,500/2,304-byte MSDUs, sequence wrap and PN carry, duplicate/replay
separation, source/header/MIC/PN continuity faults, overflow, expiry across
timer wrap and plaintext wiping. The extended native suite also sends the
full 3,839-byte plaintext aggregate through the QoS duplicate engine.

Hardware regression evidence under `build/clean-rx/badge/d2-qos-*`:

- Default build `5115ae14`: three fresh WPA2 connections/echoes against the
  unchanged vendor AP, each with a matching independent AP observation.
  One wrong-password trial associated/sent M2 but neither endpoint
  authorized or exchanged protected traffic.
- Same default build: OFDM6 and 2-Mbps-long cyclic-size controls, each with
  exactly 30 ACKed/delivered frames per direction and no reported failures
  or PHY mismatch.
- Extended build `dbb6d9e2`: fresh 3,869-byte MCS0 A-MSDUs in both
  directions through the retained large-receive path.
- Both original full 16-MiB flashes were restored and independently
  compared with fresh pre-test snapshots; both badges remain in BOOTSEL.

Those hardware runs regress existing encrypted station and raw HT behavior;
they do **not** qualify the new fragmented QoS path over the air. HT/WMM
association advertisement, EDCA scheduling, Block ACK and reordering remain
disabled/unimplemented gates, not implied by these tests. The separately
gated response prototype below does not remove its RF qualification gate.

### Gated QoS/HT normal-ACK response prototype

`CLEAN_RX_HT_RESPONSE` defaults to zero and requires `CLEAN_RX_HT_RX=1`.
Opcode 14, exactly `14 | (policy << 8)`, accepts policy 0..3 on an already
pinned profile-3 radio: bit 0 enables supported normal-ACK QoS frames,
bit 1 enables supported HT receptions. Zero disables both. Default builds,
other profiles and extra command bits fail explicitly. Configuration starts
with both bits clear. Updates use the existing deferred policy path, halting
MAC/PSM and checking RX/response ownership before replacing tables/policy;
ordinary basic-rate/slot updates preserve the selected response policy.

The final-GoodFCS hook preserves RA/TA pinning and adds:

- Three-address QoS Data/Null with normal ACK policy, all 16 TIDs.
  No-Ack, No Explicit Ack and Block ACK policy, four-address, Order/HT
  Control and unsupported subtypes do not trigger a normal ACK.
- HT20 mixed-mode MCS0..7, BCC, 800-ns GI, no STBC, sounding, extension
  streams or A-MPDU. Both smoothing values are accepted. HT-SIG length
  must agree with the final RXE count; unknown formats are rejected.
  PHY/header CRC acceptance still belongs to the existing hardware receive
  path, not a newly invented PSM CRC implementation.
- Separate six-word response records in SHM `0x6c0..0x6ef`, with policy at
  `0x6ba`; compile-time overlap checks cover the existing traces/captures.
  Each record carries PLCP, PHYCTL1, legacy PHY format and airtime.

Non-HT reference rates for MCS0..7 are 6/12/18/24/36/48/54/54 Mbps.
The highest BSS basic rate not exceeding that reference is selected by
numeric bitrate, including CCK; if none qualifies, mandatory OFDM fallback
is used. This is not the existing legacy same-modulation helper. The
[IEEE TGn CID 1064 resolution](https://mentor.ieee.org/802.11/dcn/09/11-09-0344-00-000n-sb1-mac-cid-proposed-resolutions.doc)
was checked directly for the basic/reference-rate selection rule.
CCK responses to HT use long preamble rather than treating HT PHY status
as a legacy short-preamble indication.

Opcode 12 API version 3 remains a one-frame diagnostic with no station TX
profile or retries. It requires the response build gate and exactly a
60-byte unprotected QoS MPDU, normal ACK policy, nonce-derived local/unicast
addresses, sequence, TID and complete payload/FCS. Duration reserves
SIFS plus the ACK for the fixture's OFDM6/12/24 basic set. The receiver pins
the same addresses, configures that set and requests policy 3. It does not
associate or advertise HT/WMM. Versions 1/2 retain their earlier gates.

```sh
make -s -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ht-ack \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RESPONSE=1' all test
make -s -C clean-rx/badge BUILD=../../build/clean-rx/badge/ht-ack \
  CONTROLLER_BIN=../../build/clean-rx-ht-ack/cyw43_rx_fw.bin \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RESPONSE=1' all test
# Install the matching images on both preserved badges before running:
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 --ack
```

Native instruction tests cover all 4,095 nonempty basic sets at every MCS,
every receive length, QoS TIDs/policies, both smoothing values, malformed
PHY/header/address cases and command gates. Fixed-fixture tests cover every
MCS/TID, descriptor/frame bit corruption and unsafe modifications even
with a recomputed FCS. The one-SHM-input invariant and legacy regressions
continue to pass.

Hardware evidence under `build/clean-rx/badge/d2-response-*`:

- Initial build `5c4921cb`: 10/16 matching matrix deliveries with one local
  response start each. One other cell failed during an ambient-RX
  configuration race; replacing immediate policy changes with deferred
  application fixes that setup race, not the remaining RX losses.
- Final build `7b4c37c8`: 13/16 fresh, fully checked 60-byte deliveries and
  one response started per delivery, no response errors. Forward MCS0/6
  and reverse MCS7 had no early candidate or delivered frame. Three bounded
  repeats added forward MCS0/6 successes; reverse MCS7 still had no candidate.
  Failures remain recorded; this is not an all-cell reliability pass.
- **No returned ACK was observed by the transmitting badge in these 19
  final-build trials.** The raw diagnostic TX path is not the station's
  clocked ACK-wait path. PSM-local response delay was 1..2 us, measured
  from its receive bookkeeping point, **not** independently from physical
  receive end to response start. Neither a counter nor that delay qualifies
  RF ACK delivery, SIFS timing or ordinary HT station operation.
- Default build `b83a299b`: OFDM6 and 2-Mbps-long controls each passed
  exactly 30 ACKed/delivered frames per direction across 32/1,500/2,304-byte
  payloads. One vendor-AP WPA2 positive and one wrong-password negative
  control passed with matching independent AP observations.
- Final collector build `5ecb6600`, `d2-response-strict-*`: fresh MCS1
  delivery, one response start and no response error, but no returned ACK.
  The paired test correctly exited with failure rather than count a local
  response as a completed exchange. The controller/PSM behavior is unchanged
  from the matrix build; this tightens the host-side evidence gate.
- Both original full 16-MiB flashes were restored, independently read back
  and compared against fresh full snapshots; both badges remain in BOOTSEL.

The collector exposes `ack_responses_started`, `ack_response_errors`,
`psm_response_delay_us` and `fresh_return_ack` separately. The current ACK
collector requires a fresh, FCS-valid, correctly addressed returned ACK
before its TX role succeeds; response counters alone cannot pass the
paired test. The earlier matrix measured delivery/response starts only,
not successful ACK exchanges. Even a returned ACK does not set
`sifs_timing_qualified` or `ht_interoperability_qualified`.

### Clocked HT normal-ACK fixture and returned-ACK evidence

The raw version-3 diagnostic is not an ACK-clocked transmitter. It halts
PSM/RX, forces transmit overrides and, after MAC byte drain, waits another
nominal PPDU before restoring RX. MAC drain is not physical PPDU completion.
This safely bounded raw path cannot provide the RX turnaround needed for
the expected SIFS response. A forward raw MCS0 control delivered the frame
and started a peer response, but the raw sender did not receive its ACK.

Opcode 12 API version **4** uses the same fixed, nonce-bound 60-byte QoS
MPDU as version 3, but enters the existing reliable profile-3 path:

- Requires the response/extended-RX build, pinned local/peer addresses,
  short slots, exact OFDM6/12/24 basic mask `0x150`, and desired response
  policy 3. Other profiles/policies and modified fixtures fail explicitly.
- The exact one-shot command is `12 | clean_tx_length_bits(66) | (1 << 19)`.
  No arbitrary frame, RF retry, association or HT advertisement is enabled.
- Keeps PSM/RX running and uses automatic PHY turnaround and PSM-clocked
  DCF/ACK acceptance. HT airtime is not passed through a CCK rate parser.
  The conservative ACK allowance is `10 + slot + 64 + 304` microseconds,
  added to HT data airtime; this is not an RF SIFS measurement.
- The badge retries only zero-attempt BUSY results under a 200-ms host
  access deadline. It regenerates the descriptor after draining RX, since
  the packet buffer is shared. At most one actual template trigger occurs.
  A transmitted timeout is terminal, not retried as a fresh success.

The PSM's expected-ACK final-GoodFCS path copies evidence before consuming
the ACK internally. Wrong address/length, an inactive/expired ACK wait or
disabled QoS response policy cannot populate this evidence. The counter
commits after copying. Stop command 3 snapshots this record:

| Location | Version-2 evidence |
| --- | --- |
| SHM `0x6f0..0x6fd` | Count, TSF low/high, PHY, ACK14 and legacy PLCP6 |
| SRAM `0x203e0..0x203ff` | Magic `0x32524b41`, followed by those 14 words |
| USB detail `720..751` | Complete 32-byte SRAM record |

Compile-time size/overlap checks preserve the adjacent layout manifest.
Default policy zero skips evidence recording. The host independently
requires controller ACKED/one attempt, exactly one saved ACK, valid complete
FCS and fresh RA, legacy OFDM format, and correct PLCP length, parity,
reserved fields and response rate: 6 Mbps for MCS0, 12 for MCS1/2, and
24 for MCS3..7. Local response starts cannot satisfy this gate.

Use the response-build commands above and matching images on both badges:

```sh
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX MCS --ack
# Retain the raw transmitter as a separately failing/control experiment:
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL OTHER_PREFIX MCS --ack-raw
```

Standalone roles are `--ht-ack-clocked-tx` (badge mode 28) and `--ht-ack-rx`
(mode 27), followed by the same challenge/MCS arguments. Raw TX remains
`--ht-ack-tx` (mode 26). Reports include `ack_result`, `tx_attempts`,
`ack_capture_count`, `ack_rx_time_us`, `ack_phy`, `ack_mpdu` and `ack_plcp`.
Clocked diagnostics also save MAC radio/trace state at detail offsets
800/864 and report access deferrals, CW/remaining slots, NAV, pending
policy, IFS and PSM polls. Failure snapshots stop the controller first,
preserving the original badge error instead of reporting a successful stop.

Evidence under `build/clean-rx/badge/d2-clocked-*`:

- Intermediate build `4baa273b`: 13/16 strict exchanges, using the earlier
  28-byte ACK record without PLCP. Every success had a matching fresh peer
  delivery/response and independently zlib-verified ACK FCS/RA.
- Full-evidence build `4f1bd5e9`: **12/16** strict exchanges. Forward MCS3
  and reverse MCS0/2 failed with BUSY, zero actual attempts and a host access
  deadline. Reverse MCS7 transmitted once and timed out; its receiver had
  an early RX error and no fresh delivery. These are three access failures
  and one transmitted-frame failure, not four RF losses.
- All 12 successful full-evidence ACKs independently passed zlib FCS,
  fresh RA and complete PLCP/rate checks, with matching full peer delivery,
  one response and no response error.
- Access-diagnostic build `393935f8`, same controller: four bounded repeats
  added reverse MCS2/7 successes. Forward MCS3 and reverse MCS0 still made
  no TX attempt. Valid frozen snapshots showed CW 15 but **18,000/21,300
  remaining slots**, zero pending policy/NAV and idle IFS `0x2f`. The
  counter/scheduler cause was unresolved in this build; see the continuation
  below. This is not justified as ambient
  contention; neither extending deadlines nor masking the counter is a fix.
- Default build `be5b417d`: exactly 30 ACKed/delivered frames per direction
  at both OFDM6 and 2-Mbps-long, cycling 32/1,500/2,304-byte payloads.
  Unchanged-vendor WPA2 positive and wrong-password controls passed, with
  independent AP authorization/protected-traffic observations.
- Default/opt-in native suites pass, including all-MCS descriptor/policy
  gates, ACK/PLCP corruption, PSM exact copying and address/deadline gates.
  Both original full 16-MiB flashes were restored, read back independently
  and compared against this batch's full pre-test backups; both are in BOOTSEL.

This demonstrates actual returned ACKs for the bounded clocked fixture,
not reliable ordinary HT access, independently measured PHY-end-to-ACK
SIFS, independent-peer interoperability or completed D2. Earlier failures
remain recorded. HT/WMM association advertisement remains disabled.

### DCF slot operand visibility and fail-closed counter handling

The zero-attempt failures were not justified as channel contention. Native
DCF tests had treated SHM/register writes as immediately visible. On chip,
the old slot decrement/SHM-branch sequence exposed operands far above the
configured CW. This is not necessarily persistent corruption of the stored
slot count: later halted evidence has an invalid latched operand but a zero
SHM counter. Transition instrumentation now records:

| Location | Counter evidence |
| --- | --- |
| SHM `0x6bb..0x6be` | Latest Cortex draw, PSM before/after values and TSF low sample |
| SHM `0x6bf` | Latched invalid-counter reason; not cleared by a new draw |
| SRAM `0x203b0..0x203bf` | Magic `0x32464344`, then draw/before/after/TSF, guard reason and actual slot count |
| USB detail `976..991` | Complete 16-byte SRAM record, separate from HT RX/ACK evidence |

Static checks preserve the adjacent HT response table, HT diagnostic,
layout manifest and unicast status. Magic is invalidated at controller
startup and published only with a hardware-profile snapshot. JSON fields
are `dcf_trace_valid`, `dcf_draw`, `dcf_before`, `dcf_after`, `dcf_clock`,
`dcf_guard_latched` and `dcf_final_slots`. The final snapshot is refreshed
after stopping PSM, including on a controller fault.
The draw is the latest configured window, including post-TX backoff; these
are diagnostic snapshots, not a complete timing trace or RF timestamps.

Controlled evidence under `build/clean-rx/badge/d2-backoff-*`:

- `d491fb59`: inserting transition samples launched all 20 bounded trials
  without the invalid counter. This alone was not accepted as a fix.
- `6ffcae89`: restoring the immediate SHM decrement/branch while retaining
  pre-transition samples reproduced a zero-attempt failure in 8 trials:
  draw 10, remaining/before 15,574.
- `5332172b`: register staging without load/first-consumer separation
  still trapped three faults in 32 trials. Frozen register operands were
  35,046/23,297/60,025 while the Cortex's SHM slot samples were zero.
  These three cases stopped before TX with error `0x8e`; this is a failed
  intermediate repair and a real hardware check of fault preservation.

The retained sequence is:

1. Load SHM slots into `r3`.
2. Save an independent TSF sample before the first `r3` comparison.
3. Reject values above 1,023, the shared software/PSM maximum CW.
4. Check/decrement the staged register, commit it to SHM and save the
   before/after values.
5. Branch on the staged register, not the just-written SHM operand.

The sequence removes both observed dependency patterns without redrawing,
masking an invalid value, extending the 200-ms access budget or retrying a
transmitted frame. Its native instruction/ordering assertions are
intentional: the flat interpreter cannot certify silicon operand timing.
The exact writeback/visibility mechanism has not been independently
characterized, so do not infer a generic D11 latency or SHM prohibition.

An invalid counter disables PSM contention, defers the request and latches
an error. Cortex publishes error `0x8e`, address `CLEAN_DCF_SLOTS` and the
offending value through the existing mailbox. The badge preserves an
already-faulted controller's evidence instead of attempting command 3 and
losing the snapshot. No invalid-counter path triggers TX.

With the separated load, `c933fd3f` completed **16/16 actual launches and
13/16 strict ACK exchanges**. Two fresh full sweeps in `2b7315ae`
completed **32/32 actual launches and 24/32 strict ACK exchanges**. Across
all 48 cells there were no access failures, controller errors, out-of-range
observations or range-guard trips; each saved transition ended at 1 -> 0.
All 37 successful ACKs independently passed complete zlib FCS, fresh RA and
PLCP/rate checks, with matching full peer delivery and one error-free
response. Reinspection of the 11 transmitted failures distinguishes four
with no early HT candidate, two with expected HT-SIG but no recorded terminal
outcome, and five full-length terminal receive errors; none delivered fresh
data or started a response. The earlier six-no-candidate classification
incorrectly treated outcome 0 as a zero candidate count.
This is not a receive-reliability pass. All failures remain recorded.

Final `fb981a82` adds startup invalidation of stale trace magic and passed
both-direction MCS0 ACK smoke tests. Default `d575252e` passed 30
ACKed/delivered cyclic-size frames per direction at OFDM6, 2-Mbps-long and
OFDM6 with three suppressed ACKs per endpoint, with zero corruption or
controller faults. These controls still had reported retries/timeouts.
The earlier 16-suppression trial correctly failed the positive soak:
the initiator exhausted its existing eight attempts and the peer stopped
waiting for progress; actual suppressed ACKs were 6/8, not 16 each.
Final unchanged-vendor WPA2 positive/negative controls independently
confirmed authorization/protected traffic or its absence, respectively.

Default/opt-in native suites pass. New cases include every CWmin draw,
expanded CW boundaries through 1,023, a 700-ms idle/no-request interval
across TSF wraps, exact transition ordering and injected corrupt values,
including all observed fault values. No dependencies were added.
Both original full 16-MiB flashes were restored and independently compared
against this batch's full pre-test snapshots; both badges are in BOOTSEL.
Longer access qualification, remaining RX losses, SIFS and the broader
EDCA/Block ACK/negotiation gates remain open.

### Focused receive-failure investigation

The default-disabled response build now captures failed fixed 60-byte
fixtures before flushing/rearming RX:

- SHM `0x700..0x74d`: count, cause, TSF low/high, RX count, raw status1/2,
  PHY, two 33-word copies of PLCP6 + MPDU60, then final count/status1/2/PHY.
  Cause 2 is the existing terminal receive-error path; cause 3 is the
  existing RX-event abort path. The second copy stages each word in a
  register and inserts a TSF read before storing it, unlike the first
  direct SHM-to-SHM copy.
- A halted snapshot publishes the 160-byte `clean_ht_failure_t` (magic
  `0x32464848`) at SRAM
  `0x20400`, copied to badge detail offset 1640. Magic is invalidated at
  controller initialization. Static assertions check region separation.
- Capture requires response policy 3, matching current/early PLCP words,
  declared HT length exactly 60 and RX count at most 66. It deliberately
  does not filter RA/TA, allowing header corruption to be diagnosed.
  It retains the latest qualifying failure, not a ring or a guaranteed
  fixture identity. Although the whole fixed buffer is copied, only the
  RX-count prefix contains valid received bytes.
- Host JSON exposes the valid prefix, metadata, exact-address match and
  software FCS result. A complete capture additionally reports Hamming
  distance from the expected fresh 60-byte fixture, including FCS.
  A distance is not proof of peer identity when the header is corrupt.
  `failure_repeated_raw`, `failure_raw_repeat_match`, `failure_time_us`
  and `failure_final_*` expose repeatability and state changes during copying.
  Successful-frame, response and returned-ACK acceptance are unchanged.
- Native tests verify both causes and their original return paths, all
  policy values, RX counts 0..67, declared-length boundaries including
  nonzero high bytes, stale/mismatched prefixes and exact raw copies.
  They do not certify PHY or RF behavior.

Build `535e0ffe` retained these bounded results, four MCS7 one-frame trials
per direction per channel, followed by two MCS0 trials per direction:

| Channel | MCS7 strict ACKs | MCS7 full-length corrupt captures | MCS7 no early candidate | MCS0 strict ACKs |
| --- | --- | --- | --- | --- |
| 1 | 5/8 | 3 | 0 | 2/4 |
| 6 | 7/8 | 0 | 1 | 4/4 |
| 11 | 4/8 | 3 | 1 | 4/4 |

All 36 trials made exactly one actual transmission without controller
errors. All six terminal captures have valid expected HT-SIG
`073c0007fc03`, cause 2 and RX count 66. Independent fixture reconstruction
and zlib CRC32 confirm 57/89/194 differing MPDU bits on channel 1 and
159/85/133 on channel 11. In one reverse channel-1 case, the original FCS
bytes survived unchanged but the corrupted data failed that FCS.
The RA/TA corruption means exact peer-address matching is false; surviving
fresh nonce-bearing payload supports fixture correlation.
Raw status2 was `0x0840` in five cases and `0x0940` in one; these raw RXE
register bits are not assigned DMA-descriptor meanings without verification.
No cause-3 failure record was reproduced in that initial batch.

The transmitter checks every template word and its PHY controls by readback.
The new evidence therefore rules out a purely host/F2-delivery or ACK-counter
explanation for the reproduced corrupt frames. It does not yet isolate
interference, analog calibration/EVM, TX/RX PHY behavior or an internal
receive-path defect. Low-MCS failures also occur: both MCS0 channel-1 misses
had no early candidate. Small sequential channel samples cannot establish
interference as the cause or channel selection as a reliability fix.
`HT_PROBE_CHANNEL` now selects channel 1..11 consistently for both collectors,
configuration and paired output; the default remains channel 1.

One successful channel-6 reception was followed by an unrelated early
candidate that overwrote the trace with outcome 0 and a different prefix.
Thus the early trace is a latest-candidate snapshot, not a per-fixture
completion ledger. The two historical expected-prefix/pending cases
remain unresolved; they cannot be relabeled as zero-candidate misses or
proven RX-event aborts.

Evidence is under `build/clean-rx/badge/d2-rx-investigate-*`,
`d2-rx-channel-*` and `d2-rx-lowrate-*`. Default/opt-in builds and native
suites pass. Default `484712e6` passed 12 ACKed/delivered cyclic-size
frames per direction at OFDM6, 2-Mbps-long and OFDM6 with three deliberately
suppressed ACKs per endpoint, with zero corruption or controller errors;
reported retries/timeouts remain visible. Both original complete 16-MiB
flashes were independently restored/compared against fresh full backups
and both badges left in BOOTSEL. HT/WMM advertisements and remaining
reliability/negotiation gates stay disabled/open, respectively.

#### Further investigation: stable corruption, RX aborts and idle watchdog

Build `a7267511` moves failure capture before the terminal PHY-format
classification. Previously, a terminal PHY value below 2 could bypass the
failed-byte capture despite a saved matching HT-SIG. This was a diagnostic
coverage gap, not permission to accept such a frame. The two-copy record
also distinguishes stable receive-buffer contents from a transient copying
discrepancy. Native tests cover terminal PHY values 0..3 and the exact
independent-copy instruction sequences.

Across `d2-rx-deeper-*`, `d2-rx-final-*` and `d2-rx-port-*`, **17 complete
failed captures have identical first and second copies**, unchanged
count/status/PHY during copying, independently invalid zlib FCS, and
independently reconstructed fixture differences of **5..236 bits**.
Fifteen are terminal errors; two are RX-event aborts with one early candidate
and outcome 0. Both aborts preserve exact RA/TA and the expected fresh
diagnostic identity. In `d2-rx-final-11-6-0-forward`, only MPDU bytes 17/18
differ (five bits, in BSSID); the remainder including the original FCS
matches, but that FCS does not validate the corrupted bytes.
The raw abort status pairs are `0x4440/0x0540` and `0x4440/0x2840`;
no unverified bit meanings are assigned.
This reproduces a concrete pending-trace exit class. It does not prove
that either historical pending case took this exact exit.

Quiet channel-11 listening additionally exposed an independent real bug:
the original one-shot RX idle watchdog at PCs 118..129 expires after
64 * 65,535 idle polls, sets reason 6 and faults PSM. A live hardware MAC
must be allowed to listen without packets. A no-peer old-image control
faulted twice on badge A; badge B's ambient traffic prevented expiry.
An intermediate refresh-on-every-poll repair passed four receive-only
controls, but was replaced with a narrower **expiry-only** hook: PC 128
restarts the idle wait only if hardware DCF is enabled. Ordinary polling,
slot timing and scratch-register preservation remain unchanged; raw
one-shot RX retains its reason-6 timeout. Flush/control watchdogs and the
existing host/access deadlines remain unchanged.

The full-loop native test executes the original 4,194,240-poll budget,
confirms the one-shot fault, then proves that live DCF survives beyond
it and still takes an incoming RX event. Earlier 700-ms native tests
exercised the hook without the outer countdown and therefore missed this.
Final `72496ec8` passed two no-peer receive-only channel-11 runs on each
badge without controller faults. These are idle controls, not successful
frame receptions. This bug does not retroactively explain the original
11 failures, whose saved controller error was zero.

The same final build completed 12 paired MCS6/7 cells: **10 actual launches,
3 strict ACK exchanges, 5 stable corrupt captures (including one abort),
2 no-early-candidate misses and 2 zero-attempt access faults**. None had
the RX idle/controller fault. The zero-attempt cells raised `0x8e` and
remain excluded from receive-loss counts; the prior 48/48 launch result
was bounded evidence, not proof of a permanent counter repair.

A useful unresolved clue: all seven saved range-fault operands, including
three earlier backoff cases, equal nonce bytes 3/4 interpreted little-endian,
the final station-address word. Final halted snapshots confirm a real
PSM guard latch of 1 with that offending operand, while the actual slot
SHM word is zero. A temporary controlled change making the Cortex's last
publication read address word `0x613` rather than `0x614` did **not** move
the fault to nonce bytes 1/2: the probe fault still latched bytes 3/4.
That simple last-read hypothesis was not supported, and the probe read
was removed. The exact source/visibility mechanism remains unresolved;
no invalid value is masked or silently accepted.

The pre-fix `a7267511` tranche contains 22 complete paired collections
(8 strict ACKs, 8 receiver controller errors and 9 stable terminal captures)
plus one incomplete USB collection, which is not an RF-loss result.
The intermediate `a30846d7` tranche has three completed cells (one strict
ACK and one zero-attempt guard fault). The temporary `44011913` probe
has eight completed cells (seven launches, three strict ACKs, one guard
fault and three stable corrupt captures). Do not combine these into a
positive reliability qualification.

Default `c42d0041` passed 12 ACKed/delivered cyclic-size frames per direction
at OFDM6, 2-Mbps-long and OFDM6 with three suppressed ACKs per endpoint,
with zero corruption/controller errors and retries/timeouts retained.
Default/opt-in builds and sanitizer-backed native suites pass. No power,
gain, IQ-compensation or analog-filter settings were changed: the available
Linux LCN reference uses different multiplier packing/offsets and does not
verify LCN20 attenuation semantics.
Both original complete 16-MiB flashes were restored and independently
compared to this tranche's fresh full backups; both badges are in BOOTSEL.
HT/WMM advertisement remains disabled. Stable corruption is now demonstrated
inside the receive buffer, but its RF/PHY/internal root cause remains open.

### Build and bounded paired tests

```sh
make -s -C clean-rx/src CC=clang BUILD=../../build/clean-rx-ht-data \
  EXTRA_CFLAGS=-DCLEAN_RX_HT_DATA=1 all
make -s -C clean-rx/badge BUILD=../../build/clean-rx/badge/ht-data \
  CONTROLLER_BIN=../../build/clean-rx-ht-data/cyw43_rx_fw.bin all test
```

After saving both originals, install the matching
`build/clean-rx/badge/ht-data/clean_rx.uf2` on both badges and leave them in
BOOTSEL. A fifth argument selects the versioned data API, without changing
the four-argument D1 probe:

```sh
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX MCS MPDU_BYTES
# Example: MCS0, a 2,304-byte payload plus header/FCS:
bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 2332
```

The collector also accepts `--ht-data-tx`/`--ht-data-rx`, followed by a
28-hex challenge, MCS and MPDU bytes. Challenge bytes 11..12 encode length
and byte 13 encodes MCS; other bytes remain fresh. TX success is still
only local submission. Pair success requires the receiver's full matching
host delivery. Reports distinguish that full on-badge payload/FCS check
from the saved raw evidence: the controller trace only holds 96 MPDU bytes
and the USB detail only saves 510 MPDU bytes. `fresh_ht_trace` therefore
cannot prove a complete larger frame. The host independently checks the
saved prefix and expected FCS report, but larger frames are **not saved
in full** for an external verifier. No paired report claims independent
HT interoperability or reliability.

### D2 hardware results and regression controls

Evidence remains under `build/clean-rx/badge/`, not in source control:

- Initial `1f0bb7f5` data build: the first 88-byte trial passed, and
  `d2-size-*` passed 6/8 large-frame trials at MCS0/MCS7. MCS0 delivered
  1,528- and 2,332-byte MPDUs in both directions; MCS7 delivered both sizes
  in reverse. The first `d2-matrix-*` sweep completed 31 paired trials,
  with 26 fresh deliveries; 17 aborted launches/collections are excluded
  from RF statistics, not counted as packet losses.
- `61902238`, `d2-final-matrix-*`: 48 planned cells, MCS0-MCS7, MPDUs
  60/512/2,332, one trial per size/rate/direction. There were **47 complete
  pairs and 32 fresh full host deliveries**. One incomplete TX collection
  is excluded. All complete pairs had zero reported controller/transport
  errors and a submitted TX, but the 15 unmatched receive trials remain
  failures: 12 had no recorded early candidate, three retained early-only
  evidence and none had a completed HT capture. The saved
  `d2-final-matrix-summary.json` records every cell, including exclusions.
- Final `c6915690`, `d2-end-m{0,1,7}-d{0,1}-*`: after fixing prepared-capture
  invalidation at explicit raw discard/rearm boundaries, **all six**
  2,332-byte MPDUs delivered a fresh, exact 2,304-byte payload and valid
  FCS. Each of MCS0, MCS1 and MCS7 passed in both directions. This small
  smoke test is not a sustained loss-rate measurement or a controlled
  before/after explanation of the earlier losses.

For every fresh match in the 47-pair sweep and final six-frame smoke test,
an independent host reconstruction checked the saved 510-byte-or-shorter
MPDU prefix, metadata bytes and the expected FCS using Python/zlib, against
the badge's full-frame comparison/FCS result. This does not replace a saved
complete large MPDU or an independent HT receiver.

Final D1 probe build `561421ca` passed the original four-argument script
in both directions at MCS0, with exact raw and F2 freshness.
Final default build `88cbc56d` rejected opcode 12 on hardware with error
`0x85` and no submission. The same default build passed `d2-end-legacy-*`:
2-Mbps long-preamble CCK and OFDM6, cyclic 32/1,500/2,304-byte payloads,
30 ACKed/delivered frames per direction for each PHY, zero corruption,
duplicate delivery or PHY mismatch.

Validation also passed native HT API/peer-profile tests with ASan/UBSan,
both loader variants, separately sanitized loader/transport tests, PSM/MAC,
WPA2 and secret handling, Micro Signal tests and all existing Micro Signal
build variants. Default, data, fixed-probe and CRC-experiment controller
builds remain freestanding; invalid data-build gates and ungated CRC
experiments fail compilation. CLI bounds and shell syntax checks passed.
Final controller text sizes are 34,084 bytes default, 34,956 data,
35,040 fixed probe and 34,984 CRC experiment, with 4,736-byte BSS each.

Before testing, both complete 16-MiB flashes were saved and verified.
A's saved application prefix matched its original 33,604-byte backup;
B's entire saved flash matched its earlier original full-flash backup.
All trial UF2 extents stayed within the backed-up first 512 KiB. After the
controls, both prefixes were restored and **independent full 16-MiB
readbacks matched the respective pre-test full-flash backups exactly**.
Both badges remain in BOOTSEL. No Mac network settings or unrelated
experiment directories were changed.

### Association-response profiles and runtime HT/WMM updates

The 2026-10-18 implementation adds independent, atomic association-response
HT/WMM parsing and fixes a real refresh-propagation bug:
`clean_station_refresh` copied HT/WMM-only beacon changes into its argument
but returned zero when ERP/basic-rate/slot fields were unchanged.
`station_receive` refreshed a temporary AP record and copied it back only
when that return value was nonzero, so those updates were discarded.
Refresh now compares every represented HT/WMM field, not just the WMM
parameter-set nibble or legacy fields. Same-counter AC changes and
parameter-set wrap are detected.

Accepted association-response optional IEs have their own snapshot. The
parser retains the existing address, subtype, fragment, FCS, AID, status
and complete IE-boundary gates. It commits the AID and snapshot only after
the entire successful response validates. Missing response elements remain
missing instead of inheriting the discovery beacon. Malformed/duplicate
optional elements mark HT fallback without preventing the existing legacy
association. Auth responses and rejected/malformed association responses do
not modify the saved output.

Subsequent matching beacon updates preserve the association snapshot and
increment a saturating HT/WMM change counter. The compact report uses
response capabilities/MCS constrained by the latest beacon and current
operation/protection/EDCA observations. It never increases the response MCS
set or adds 40-MHz capability. Source 0 means discovery, 1 means accepted
response, and 2 means response plus subsequent beacon changes. Departure
handling clears the retained association profile and counter.
The report is now 48 bytes at detail 1040..1087; byte 19 carries source
and bytes 44..47 carry the counter. Saved association evidence and the
packet-length field remain separate.

This does **not** activate HT/WMM advertisements or install AP-derived
command-15 policy into the legacy station. That would restrict its existing
legacy/WPA2 TX path to the clocked diagnostic fixture. Actual negotiated
HT/QoS enablement remains behind the outstanding MAC/receive gates.
Unexecutable WMM contention exponents also produce explicit WMM fallback.

Native ASan/UBSan cases cover:

- Successful response HT/WMM parsing and feeding its AC records into the
  shared EDCA engine.
- Every truncated IE boundary with reconstructed valid FCS, allowing only
  complete optional-element boundaries; wrong subtype, protected flags,
  RA/TA/BSSID, fragmentation and AID failures.
- Atomic rejection/status handling, unchanged outputs for authentication,
  absent response capabilities, duplicate HT/WMM and unsupported execution
  exponents.
- WMM-only changes, changed ACs with an unchanged counter, parameter-count
  wrap, MCS intersection, protection/channel fallback, removed capabilities,
  bad-FCS refresh atomicity, counter saturation and departure clearing.
- Existing open/WPA2 association-request identity and preserved association
  evidence after report publication.

Default and HT/extended-RX/response/EDCA badge suites pass, as do both
freestanding builds with no undefined symbols. Final badge builds are
`438812da` (default, text 79,728/BSS 221,312 bytes) and `e3bb50ff`
(gated, text 83,496/BSS 225,800 bytes). The compiled `run` stack frame is
904 bytes in both profiles. Explicit project byte-copy helpers avoid
compiler-generated `__aeabi_memcpy` dependencies from structure assignment.
Controller source/binaries were not changed for this increment.

Hardware attempts against two nodes matching the configured local SSID
were retained, without displaying or provisioning its password:

| Badge build / channel | Observation |
| --- | --- |
| Default / 1 | AP discovered; one request ACK observed, no validated authentication response; association not reached |
| Default / 11 | No usable target AP discovery; host input error, no controller error |
| Gated / 1 | AP discovered; four request ACKs observed, no validated authentication response; association not reached |

All three report `ht_advertised:false`, profile source 0 and zero runtime
association updates. These failures are not successful association,
negotiated-HT qualification or RF validation of the new source-1/2 paths.
They do not establish the cause of the missing authentication responses.
Evidence: `build/clean-rx/badge/d2-association-{default-ch1,default-ch11,edca-ch1}.*`.

Before writing badge A, its complete flash was freshly saved and verified
as `build/clean-rx/badge/d2-association-start-A-full.bin`. After all trials,
the full original flash was restored; the independent full 16-MiB readback
`build/clean-rx/badge/d2-association-restored-A-full.bin` matches exactly.
Badge B was not modified. Badge A remains in BOOTSEL.

### Subsequent AP reply investigation and bounded RX staging

The [AP authentication investigation](AP_AUTHENTICATION_INVESTIGATION.md)
records independent monitor evidence, negative radio/rate controls, the
eight-slot station RX FIFO and revised service diagnostics. Legacy fixtures
pass, but local-AP reply delivery remains unresolved. The final gated matrix
passed 10/16 cells; identical-payload old/new replays passed 2/4 each and
retain independently invalid, repeat-identical SHM captures. Neither
negotiated HT operation nor receive reliability is qualified.

The subsequent
[matched-amplitude and rejected-authentication controls](AP_AUTHENTICATION_INVESTIGATION.md#follow-up-amplitude-controls-and-rejected-authentication-capture)
retain the original default amplitude. They add exact challenge replay and
pre-host rejection evidence, but do not resolve receive reliability or
complete negotiated HT operation.

### Independent capability/MCS/airtime pieces (2026-10-18)

The first D2 roadmap item is now split into independently testable pieces.
The raw mixed-format engine and its airtime calculations already existed;
this change completes a conservative PHY candidate/configuration layer,
without declaring the mandatory live MAC ready.

- [ht20.h](../src/ht20.h) serializes the unadvertised HT20/800-ns/BCC/one-stream
  target capability IE. It disables optional PHY modes and encodes the
  minimum A-MSDU/A-MPDU target, conservative 16-us spacing, MCS0..7, a
  defined equal TX/RX MCS set and 65-Mbps maximum RX rate.
  Layout, rate units, SMPS and spacing encodings were checked against the
  pinned public HT definitions linked above; no external code was copied.
- `clean_ht20_select_mcs` handles all sparse masks and explicit ceilings.
  `clean_ht20_configure` validates exact per-frame MCS/length/mask inputs
  atomically, then derives controls, symbol count, PHY rate and airtime.
  [main.c](../src/main.c) now uses the result in actual HT template TX,
  including its existing completion/ACK/drain deadlines. Register
  readbacks, HT-SIG guards and legacy-format programming remain intact.
- [ht_station.h](../badge/ht_station.h) computes an explicit candidate from
  AP capabilities, operation, basic MCS, WMM and protection. Reserved SMPS
  and A-MPDU-parameter encodings fail HT planning without disabling legacy
  association. Local HT-data/extended-RX-disabled builds add fallback bit
  128, in addition to the unconditional mandatory-MAC bit 32.
- [station.h](../badge/station.h) shares one response/runtime constrained
  profile between the candidate API and the live peer report. Beacons
  cannot enlarge the accepted association's MCS set or 40-MHz capability;
  changed channel, missing operation, unsupported basic MCS and departure
  invalidate/recompute the candidate without activating HT.
- [collector.c](../tools/badge/collector.c) exposes the unadvertised target IE
  and candidate MCS mask/selection, always with HT inactive. Finite
  probe/data/ACK records also describe the requested format, width, GI,
  streams, rate, symbols, calculated airtime and L-SIG length.

The target is **not** a claim that its full MAC receive obligations can
currently be met. No association builder calls the capability serializer.
Association-frame identity/IE tests still require no HT capability or
operation advertisement. A candidate MCS is not a negotiated rate.
HT mixed-format means an HT PPDU with its legacy-compatible preamble;
it does not establish reliable interleaving of legacy and HT station traffic.

Validation: controller/badge builds and native suites passed in default,
HT-data/extended-RX, HT-data-only, HT/response/EDCA/WMM and finite one-copy
aggregate profiles. ASan/UBSan suites passed; both freestanding ELFs in
each profile have no undefined symbols. New tests cover all 2,048
mask/ceiling pairs and 2,048 exact-MCS/mask combinations, invalid-input
output preservation, every supported length at every MCS, independent
88-byte airtime/rate vectors, exact target-IE bytes and parsing, malformed
reserved encodings, sparse response/beacon intersections, constrained
ceilings, runtime channel/operation/basic-MCS changes and association reset.
Default/partial builds explicitly suppress candidates. Existing
legacy-association, protected RX, aggregate and response regressions pass.

Reproduction uses separate build directories, for example:

```
FLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1'
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-phy-plan-ht EXTRA_CFLAGS="$FLAGS" all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-phy-plan-ht \
  CONTROLLER_BIN=../../build/clean-rx-d2-phy-plan-ht/cyw43_rx_fw.bin \
  EXTRA_CFLAGS="$FLAGS" all test
```

Logs are `build/clean-rx/badge/d2-phy-plan-{default,ht,tx-only,gated,handoff}-{controller,badge}.log`.
There were **no hardware writes or new OTA qualification** in this step.
Earlier radio evidence remains scoped to the tested raw/diagnostic paths.
Actual association activation and independently qualified mixed traffic
remain unchecked in the root roadmap.

### Readback-verified station policy commits (2026-10-18)

The existing controller policy commands now complete only after application
and readback, not on deferred queueing. The station therefore does not publish
an accepted WMM/runtime profile or advance to its next command on queueing
alone. Pending commands are single-flight; readback, timeout, cancellation and
unexpected-command failures are explicit. PSM is paused while the response
tables, pinned addresses, QoS policy and contention controls are verified.
Held capture data is not rewritten by policy installation.

The [policy commit campaign](EDCA_IMPLEMENTATION.md#readback-verified-policy-commit-campaign-2026-10-18)
records three build/test profiles, exhaustive table/readback faults,
independent WPA2 positive/negative/legacy controls and all five final clocked
HT trials. Two HT exchanges completed; three did not. No SIFS, receive-size,
protection or live BA qualification follows from those results.

This closes a prerequisite in the root activation item, **not that item**.
No actual HT station/controller activation command or HT association offer
was added. Mandatory live aggregate delivery/Immediate BA and qualification
remain required; the unconditional mandatory-MAC fallback is unchanged.

### D2 qualification checkpoint (2026-10-19)

The root [D2 checklist](../../todo.md#d2-ht-phy-and-association) separates
completed foundations from eight remaining work packages. This checkpoint
preserves the implementation and qualification context removed from that
checklist; it does not enable HT association or close its mandatory MAC gates.

Software foundations include extended RX, per-TID receive/CCMP state, gated
responses, bounded per-category access, association/runtime peer profiling
and station RX staging. Non-HT WMM negotiation, AP-derived EDCA, protected QoS
exchanges and bounded protected A-MSDU receive are implemented. Block ACK
agreements, A-MPDU stream parsing and ciphertext reordering have native WPA2
integration tests, not production live-radio qualification. Readback-verified
policy commits are a prerequisite, not actual HT policy activation.

The [aggregate interface record](AMPDU_INTERFACE_INVESTIGATION.md) keeps the
separate receive paths and their evidence:

- The per-MPDU DMA/F2 experiment delivers the first two diagnostic bodies
  at MCS0/3/7 in both directions. Its final byte-evidence campaign has 6/7
  normal two-body deliveries, one retained RF miss and two successful
  bad-middle rejections. The last DMA body remains missing; finishing this
  alternative is not a prerequisite if the retained-SHM path is used.
- The retained-SHM/SRAM/F2 path delivers all three fixed fixture bodies.
  Its initial MCS0..7 matrix passes 16/16 fresh trials across both directions,
  exactly three deliveries and no duplicates. Bad first/middle/last and
  all-bad controls retain failed initial receptions and successful repeats.
- Explicit PSM state-16 terminal commit/claim/release removes dependence on
  later ordinary traffic, including bad-last/all-bad cases. Its matrix covers
  every MCS/direction cell after repeats, with misses and partials retained.
- Diagnostic compressed BA transmission and independently validated returned
  bytes exist for every normal MCS/direction cell after repeats. Bad-final
  reverse MCS7 remains unqualified. Neither configured hardware SIFS controls
  nor local timestamps independently establish physical EOF-to-air SIFS.

These are fixed-size, bounded diagnostics, not generalized live reception,
loss-free operation, production admission/BA or mandatory receive/protection
qualification. Receive completion/discard, radio override ownership, station
TX cleanup and atomic backoff publication have been corrected and regression-
tested. Local-AP reply delivery and intermittent HT receive corruption remain
qualification limits. Related investigations are recorded in
[AP authentication/RX staging](AP_AUTHENTICATION_INVESTIGATION.md),
[RF-hint follow-up](RF_HINT_FOLLOWUP.md) and
[aggregate radio capture](AMPDU_RADIO_INVESTIGATION.md).

The [controlled independent receive record](CONTROLLED_HT_RECEIVE.md) covers
small-frame vendor-to-clean MCS0/MCS7 reception on both badges and the later
passive vendor/AP experiments. Fresh-prefix/MAC/F2 correlation validates
complete variable-length ciphertext/FCS samples in both roles, but does not
prove multiple complete bodies from one live PPDU. Exit tracing observes
1598/1342-byte advertised PSDUs reach early good-only hints and then abort
at RX counts 560/304; those prefixes remain unattributed. Later passing
variable-length controls do not reproduce those aborts.

Early-retention build `e9008554` preserves one complete FCS-valid 170-byte
MPDU at hint status `0x1922`, bound to a CRC-valid 478-byte A-MPDU-format
prefix with vendor B/passive A. Native bounds cover every length 44..1000;
oversize candidates are counted. Five other final-build controls fail early
qualification. This is one early-body preservation result, not repeated/
both-role early reception, next-frame advance, two-body delivery or physical
EOF timing. That build enables no early-event acknowledgement, advance or TX.
The controlled receive record links the experiment's commands, ABI, build
IDs, counters, failed controls and independently verified flash restoration.

The [Block ACK implementation record](BLOCK_ACK_IMPLEMENTATION.md) separates
native-tested software from pending live agreement/key/reorder integration.
The remaining packages retain general receive-size/protection obligations,
production Immediate BA and independent timing, actual HT activation, and
negotiated mixed-traffic interoperability. D3 retains the full rate/size,
throughput and sustained-soak acceptance work.

### Bounded live next-body advance diagnostic (2026-10-19)

This records the initial one-order diagnostic. The subsequent
[live two-body capture](#first-complete-live-two-body-capture-2026-10-19)
supersedes its base hypothesis, buffer layout and no-live-advance result.

`CLEAN_RX_HT_ADVANCE` defaults off and requires the complete early snapshot.
The vendor/AP reference enables it without enabling observer TX, association,
keys or HT advertisement. This is a diagnostic implementation, not completion
of D2 work package 2.

The one-shot gate targets the previously observed 478-byte advertised PSDU:
a retained 170-byte first MPDU, stable RX count 176 and raw next length 298.
It also requires matching prefix ownership, GoodFCS and no current
terminal/abort bit. After retaining the first body, it acknowledges event 6,
clears receive-context bit 7, arms the proven direct-body copy controls and
resumes at body PC163. The continuation base is `FRAME+3`, testing whether
direct-body MAC bytes land at `FRAME+4` while preserving the three HT-SIG
words. That physical layout remains a hypothesis until a live advance occurs.

The advance profile reserves four additional snapshot words, reducing the
maximum retained early body from 1000 to 992 bytes without crossing SHM
`0x600`. It records the one-shot marker, next length and TSF. Diagnostic F2
tagging protects the candidate continuation sample from later same-size
replacement. The host now derives two-body success rather than hardcoding
it false: both full bodies must independently validate, share the fresh
prefix/epoch and addresses/TID, have different unfragmented sequence numbers,
fit the exact delimiter/padding/PSDU boundary and reach a good terminal
with status bit 14. Neither a marker nor duplicate first-body bytes suffice.

Six build `fe06ab77` trials, `build/clean-rx/vendor-advance/bounded-r0` through
`bounded-r5`, alternate vendor B/observer A and the reverse. They retain
44 valid HT prefixes, 39 with aggregation format, and 12 independently
correlated full ordinary MPDUs. Vendor received-BA counters increase in
every trial. None retains an eligible early snapshot; every advance marker
is zero. All six correctly fail the early/two-body qualification predicate.
They therefore do not exercise the physical advance or qualify its recovery.
The tested images, controller blob and collector remain beside the evidence.

ASan/UBSan tests cover every early length 44..992, exact SHM bounds,
register/control preservation, one-shot gating and no TX. Explicitly modeled
second-body good/bad/abort/watchdog paths test terminal ownership, frozen
first-body retention and the next RX epoch, not physical DMA behavior.
Host tests reject copied bodies, bad sizes/FCS/epochs, fragment-only sequence
changes, address/TID/DS mismatches, incorrect PSDU boundaries, missing EOF
and missing advance provenance. Proof sample retention is native-tested.
Default and early-save-only controller regressions pass. Final host/predicate
build `d4873fdc` has a byte-identical controller to the RF-tested build; the
host-only changes were not radio-tested.

The immediate blocker remains repeatable early capture of a real multi-body
PPDU. Keep work packages 1 and 2 open; do not generalize receive, integrate
live BA or activate HT on the strength of a native model or these failures.
The [reference experiment record](../vendor-burst/README.md#bounded-next-body-advance)
documents the ABI and retained controls.

### First complete live two-body capture (2026-10-19)

Final build **`d97844a9`**, trial
`build/clean-rx/vendor-pairs/final-r0`, demonstrates two complete,
independently FCS-valid MPDUs under one fresh live vendor PPDU. It is one
bounded positive control, not completion of handoff qualification or
production aggregate delivery.

#### Stimulus and diagnosis

The original 128-frame rotating-size burst exposed very few eligible early
bodies. The focused vendor experiment now groups four 1024-byte frames,
four 128/256-byte pairs, then four 512-byte frames, repeated eight times.
It retains exactly 32 fixtures at each size and the six-second submission /
1.2-second drain bounds, while requesting vendor `ampdu_mpdu=2`.
The vendor firmware itself is unchanged, and the clean observer never TXs.
Submission order and limits remain stimulus, not aggregation proof.

New host-call timing instrumentation measures about 130.5 ms for 128 writes,
minimum 260 us and maximum 2020..2023 us. Buckets `<=200`, `201..400`,
`401..800`, `>800` us contain `[0,29,33,66]` frames. These observations
describe transport pacing, not physical airtime or SIFS.

Both build `d9d43b6c` controls `paired-r0` (B/A) and `paired-r1` (A/B)
preserve a full FCS-valid **298-byte early body** under a 478-byte prefix,
status `0x1922`, raw next length 170. The old gate only allowed 170 then
298. The gate now accepts both exact orders; arbitrary sizes remain excluded.

Build `42dde6ec`, `reverse-r0`, exercises the first real advance, but its
continuation is bad-FCS. Build `330f04e6`, `frozen-r1`, retains the complete
failed candidate: duplicated initial MAC bytes and corrupted later header
bytes. No contiguous 170-byte window, including the saved live prefix,
passes independent bitwise CRC32. This is not merely missing transport
or stale host attribution. The `FRAME+3` base hypothesis was not validated.

The continuation base now uses aligned `FRAME`, matching earlier fixed-
fixture prior art. No byte shifting, reconstructed prefix or forced-good
FCS is used. Build `ad73353b` controls still fail: `aligned-r0` has no vendor
prefix, `aligned-r1` reaches count 306 beyond that build's 304-byte
snapshot limit, and `aligned-r2` freezes a bad continuation at count 178.
The final raw window also retains these two-extra-byte failures.

#### Evidence retention and qualification

ABI 7 splits the existing 1024-byte dump into two disjoint SHM halves,
without extra USB pages or concurrent live object-selector reads:

- First half `0x400..0x4ff`: 16-word header and up to 480 early-body bytes.
- Second half `0x500..0x5ff`: 12-word header and a fixed 300-byte raw
  continuation window. Header fields are commit-last ID, epoch, count,
  status1/status2, TSF, three live prefix words, hardware good/bad outcome,
  and actual base register. Only the bounded count-derived prefix is a
  candidate frame; stale suffix bytes are not evidence.
- Only an attempted advance with matching owner/epoch may commit one
  continuation; missing/wrong ownership, short/oversize input or an
  existing commit cannot replace it. Good/bad logging does not admit a
  bad frame as good F2 data.
- The default-disabled and early-save-only profiles retain their previous
  behavior; the latter still has its 1000-byte early-body capacity.

The final `final-r0` positive, vendor B / passive A, has:

- Fresh prefix ID **4**, epoch **132**, SIG `00de010fb003`, PSDU **478**.
- A **170-byte early MPDU**, independently valid FCS, nonterminal `0x1922`.
- One advance to a **298-byte** candidate, base `0x800`.
- A complete **298-byte good continuation**, RX count **304**, status
  **`0x4122`**, independently valid FCS, and actual tagged ordinary F2
  publication. Its frozen raw bytes match the complete F2 body.
- Matching nonce-derived TA, addresses and TID. Unfragmented Sequence
  Controls are **`0x04a0`** and **`0x0490`**: distinct bodies, not proof of
  ordered delivery or a correct secure reorder lifecycle.
- Exact boundary **`4 + align4(170) + 4 + 298 = 478`**.

The existing strict collector exits successfully with both pair and
complete-multi-MPDU predicates true. An independent bitwise CRC32 readback
check also verifies both FCS values, exact lengths, addresses, distinct
sequences, boundary and equality of frozen/F2 continuation bytes.

Three additional final-build controls retain failures:
`final-r1` (A/B), `final-r2` (B/A), `final-r3` (A/B) capture no eligible
early candidate, hence no advance or pair. This is **one success in four**,
not repeatable/both-role handoff. Intermediate bad-continuation trials
resume later good receptions; physical abort recovery after advance remains
unqualified. `frozen-r0` was instead a host readiness failure: badge B
disappeared from USB, and the user reconnected it in BOOTSEL before
`frozen-r1`. It is not RF evidence.

All raw trials and tested images/collectors are preserved by build ID.
ASan/UBSan cover both size orders, every early length 44..480, complete
bounded continuation copying, negative commit/owner/epoch/length guards,
immutable first-body retention, modeled good/bad/abort ownership and no TX.
Default/original-image and 1000-byte save-only regressions pass.

Work packages 1 and 2 remain open for repetition across roles, lengths,
terminal/bad/abort recovery and capture-loss diagnosis. The next production
step is not HT activation: repeated aggregate buffering/backpressure,
secure agreement/reorder/key lifecycle, Immediate BA and mandatory
receive/protection obligations remain gated.

Both reference images currently compile the portable PIO FIFO loop without
`BADGER_FAST_BYTES`. The existing fast path in
[`wifi_pio.c`](../../src/platform/badger/wifi_pio.c) is a concrete next
transport-pacing control to test independently. The submission timings do
not yet establish it as the cause or cure of capture losses.

Both full 16-MiB originals were backed up before this campaign and matched
to the previous verified originals. After the trials both were restored,
independently reread as `vendor-pairs/restored-{A,B}-full.bin`, and
byte/hash-compared against `vendor-pairs/original-{A,B}.bin`. Both original
SHA-256 values match; both badges are left in BOOTSEL.

### Shorter live retention and transport pacing control (2026-10-19)

The vendor-only fast-FIFO control, build `00ece242`, reuses the existing
RP2350 assembly FIFO/setup/cleanup helpers and byte-zero routine only for
the vendor's PIO translation unit. The observer retains its portable PIO
path, and its clean controller byte-compares equal to `d97844a9`.
Both role arrangements (`build/clean-rx/vendor-fifo/fast-r0`, `fast-r1`)
submit all 128 fixtures and retain a good 298-byte early body, but their
170-byte continuations fail FCS, at RX counts 176 and 178 respectively.

Submission totals are **130334/130315 us**, versus about **130467 us**
for the prior portable-path control; the bucket distribution stays
`[0,29,33,66]`, and minimum/maximum calls remain about 259/2019 us.
This does not demonstrate a useful pacing improvement. The extra assembly
linkage and vendor-only compiler flag were removed rather than retained as
an unqualified optimization. Its tested executables remain as evidence.

The next change shortens first-body preservation itself. With advance
enabled, exact 170/298-byte bodies use 85/149 direct SHM moves, sharing the
85-word prefix and conditionally copying the remaining 64 words. Other
lengths keep the guarded, odd-tail-safe path. Commit-last ownership,
epoch/SIG/RX-count revalidation, GoodFCS/EOF/abort gates and all proof
predicates are unchanged. Unsupported-length guarded blocks are omitted
only from the advance profile, whose maximum remains 480 bytes; the
1000-byte save-only profile remains byte-identical.

Native full snapshot-to-timeout paths execute **181/247 modeled
instructions**, bounded by tests to 200/300 for 170/298 bytes. This is not
physical RF timing. All lengths 44..480 still pass byte-for-byte copy,
odd-tail, exact memory-boundary, register/control-preservation and stale-
copy rejection tests. The controller shrinks **2936 bytes**, from 63096
to 60160 text bytes; the observer shrinks by the same included-blob amount.

Final build **`2e8ac062`**, `build/clean-rx/vendor-fifo/copy-r0`, vendor B /
passive A, gives the first independently verified **reversed-order pair**:

- Fresh ID **2**, epoch **18**, valid 478-byte HT-SIG prefix.
- Complete **298-byte early body**, nonterminal `0x1922`, followed by
  one advance and a complete **170-byte good continuation**, count **176**,
  terminal `0x4122`.
- Both complete FCS values pass independent bitwise CRC32; frozen
  continuation bytes equal the actual protected F2 sample.
- Addresses/TID match, Sequence Controls **`0x00b0` / `0x0080`** differ,
  and **`4 + align4(298) + 4 + 170 = 478`**.
- The unchanged strict collector succeeds. Together with the earlier
  forward-order success this demonstrates both bounded size orders,
  not repeated or ordered station delivery.

All six final-build controls are retained:

| Trial | Vendor / observer | Result |
| --- | --- | --- |
| `copy-r0` | B / A | 298/170 pair passes |
| `copy-r1` | A / B | No vendor HT prefixes |
| `copy-r2` | A / B | No vendor HT prefixes |
| `copy-r3` | B / A | Ordinary frames, no eligible early candidate |
| `copy-r4` | A / B | No vendor HT prefixes |
| `copy-r5` | B / A | Good 298-byte early body; bad continuation, count 177 |

The preserved **unmodified `d97844a9` executables and collector** were also
retested in the missing-capture direction as `baseline-r0`: they likewise
capture zero vendor HT prefixes, despite vendor received-BA activity.
Thus those zero-capture controls cannot be attributed solely to the new
copy path. The cause remains unresolved; no failed trial is relabeled
as a passing PHY reception.

In `copy-r5`, the failed advance is prefix ID 2 / epoch 44. Later good
prefixes resume at epoch 47, and independently valid ordinary F2 samples
refer to later IDs 9 and 11. This is live bad-frame-path recovery evidence,
not a physical abort or loss-free-reception qualification.

Only **one of six** optimized-copy trials passes the two-body predicate.
The software optimization is bounded and native-verified, but these samples
do not establish a reliability improvement or prove that copy latency is
the corruption's cause. Recorded copy-to-arm TSF deltas remain about
4..5 us across passing and failing controls; they are software observations,
not independent SIFS measurements.

Default and save-only builds/tests pass and byte-compare equal to the
previous corresponding controller binaries. Runtime code remains C/nolibc,
no dependencies or observer TX are added, and the minimal USB main stays
102 lines. The next qualification problem is the intermittent/missing
capture and corrupt continuation, not a reason to enable HT advertisement.

Both originals were fully backed up in `build/clean-rx/vendor-fifo/` before
flashing and matched to the previously verified originals. After all
controls both 16-MiB flashes were restored, independently reread and
byte/hash-compared with those backups. Both original SHA-256 values match;
both serial-matched badges remain in BOOTSEL.

### Frozen delivery-state and reverse-role pair (2026-10-19)

The saved zero-HT controls are not all the same receive failure. In the
previous `vendor-fifo/baseline-r0`, `copy-r1` and `copy-r2`, the frozen PSM
good-completion counts are 1/4/1, with 69/77/62 bad-FCS discards and only
1/4/1 ordinary F2 deliveries. In `copy-r4`, 257 good completions and 254
ordinary F2 deliveries coexist with zero HT starts and six bad-FCS discards.
The saved readout lacks the complete transport accounting needed to assign
a cause; supported vendor rate/channel queries show no configuration
discrepancy. These observations do not establish a calibration, RF, filtering
or delivery-stall diagnosis.

Build **`d6c2e83c`** adds observer ABI 8 in existing unused trace space.
After controller `RX_DONE`, it retains all 480 mailbox bytes, the 40-byte
capture/discard/DMA header and host receive/credit state. Two bounded ordinary
F2 summaries include short control frames, computed/received FCS and up to
40 MAC bytes. Only the first two delivered frames incur this extra CRC work.
The collector checks the one-frame credit bound and reconciles controller
consumption, host consumption and ordinary delivery counts. Existing prefix,
early/continuation snapshots and strict two-body admission are unchanged.
The controller synthesizes the mailbox PSM-state entry as captured after
any successful capture; that entry is not an instantaneous held-state flag.
The trace stays 5888 bytes; the clean controller byte-compares identical to
the preserved `vendor-fifo/2e8ac062-controller.bin`. No radio, channel, TX
power, antenna or calibration setting was changed.

Six fresh bounded trials, with all failures retained under
`build/clean-rx/vendor-receive-state/`:

| Trial | Vendor / observer | HT starts | F2 posted/consumed | Transport drops | Bad-FCS discards | Early/handoff result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `state-r0` | B / A | 7 | 272 | 5 | 8 | Good 298-byte early body; bad 170-byte continuation |
| `state-r1` | A / B | 9 | 505 | 10 | 125 | Complete 298/170-byte pair; collector passes |
| `state-r2` | B / A | 5 | 267 | 10 | 7 | No eligible early body; one oversize early candidate |
| `state-r3` | A / B | 7 | 810 | 14 | 79 | No eligible early body |
| `state-r4` | B / A | 8 | 1361 | 17 | 255 | No eligible early body |
| `state-r5` | A / B | 11 | 1308 | 16 | 348 | Good 170-byte early body; bad continuation, RX count 306 |

All six stop at controller state 10 with zero controller error, scanner
target `0xffffffff`, zero F2 pending and zero host resync/stall/retry state.
Independent reconstruction of each split mailbox verifies
`captured = posted + dropped` and `posted = consumed = host F2 count`.
These are finite diagnostic transport drops, not loss-free production RX.
Vendor received-BA deltas are 115/119/111/116/117/111; `state-r3` also has
one vendor TX failure and one retry. All runs submit the same 128 fixtures.
The zero-HT condition did not recur: no cause or fix for it is claimed.

`state-r1` is the first complete live pair with vendor A / observer B:
prefix ID 1, epoch 22, 478-byte advertised PSDU, good 298-byte early body
at RX count 304 / status1 `0x1922`, followed by a good 170-byte continuation
at count 176 / status1 `0x4122`, RX base `0x800`. A separate Node bitwise
CRC32 implementation verifies both complete bodies, matching addresses,
distinct sequence controls `0x00b0`/`0x0080`, exact
`4 + align4(298) + 4 + 170 = 478` boundary and equality of the frozen
continuation with the actual tagged F2 body. This supplements prior B/A
positives; it does not demonstrate both size orders in both roles.

Independent bitwise checks also confirm the good early/bad continuation
distinction in `state-r0` and `state-r5`. After the failed handoffs,
ordinary F2 reception recovers: `state-r0` has a complete FCS-valid
298-byte sample at ID 6 / epoch 204, after failed epoch 179; `state-r5`
has complete 298/170-byte samples at IDs 7/10, epochs 124/182, after failed
epoch 12. Each later sample matches its fresh terminal MAC snapshot.
This extends live bad-frame-path recovery evidence to both roles, not
physical abort recovery or ordered station delivery.

Only **one of six** new trials passes. HT reception now has a positive in
the previously missing direction, but repeated early capture, handoff
reliability, variable-length/repeated aggregate delivery and physical
abort/SIFS qualification remain open. The new counters distinguish explicit
transport drops from corrupt or absent early/continuation bodies; they do
not locate the underlying RF/receive-state cause.

The reference build/native sanitizer tests pass, including summary lengths
4..3896, short-frame padding, invalid lengths, corrupt FCS and output
boundaries. Controller native tests, freestanding no-undefined-symbol links,
runner syntax and whitespace checks pass. Text/data/BSS are
244504/28/40004 for vendor, 74772/28/23232 for observer (including controller)
and 60160/0/6408 for controller. The observer increment is 412 text and
16 BSS bytes; no new dependency, display, station or BA application is linked.

Before the campaign, fresh full-flash backups matched the previous originals
byte-for-byte and by expected SHA-256. After all six trials, both complete
16-MiB originals were restored and independently reread into
`vendor-receive-state/restored-A-full.bin` and `restored-B-full.bin`.
Both readbacks byte-compare equal to their fresh backups and retain the
original SHA-256 values. Both serial-matched badges are verified in BOOTSEL.

### Handoff-state controls and zero-HT delivery exclusion (2026-10-19)

The retained change is a read-only three-point handoff trace in the unused
tail of the existing continuation snapshot, SHM `0x5a2..0x5c9`. A committed
prefix ID/epoch and phase mask bind three 12-word observations: before early
retention, before event-6 acknowledgement and after next-body copy arm.
Each records TSF, RX count, both status words, copy control/base/end,
next length and raw SPRs `0x1002`, `0x1024`, `0x1078`. Phase masks 1/3/7
are committed only after the corresponding record is written. This adds
46 PSM instructions / 368 controller text bytes, no BSS or USB pages.
Register observations are sequential and add diagnostic-path latency;
they are not atomic PHY snapshots or independent timing measurements.

The initial trace-only RF control, **`e853d92f`** / ABI 9, shows the old
RX count still present immediately after rearm, while copy end has changed,
the raw next-length register becomes `0x8000`, and the good-only status bit
clears. The stream-enable bit remains set. This motivated bounded controls,
not an assumption that those observations alone identify the corruption.

All trials below are in `build/clean-rx/vendor-handoff-state/`. Even-numbered
trials use vendor B / observer A; odd-numbered use vendor A / observer B.
Each submits 128 fixtures, with unchanged AP/channel/rate and no clean TX.
Every tested UF2/controller/collector and every failed capture is retained.

| Build / trace ABI | Trials | Control | Advances exercised | Complete pairs |
| --- | --- | --- | ---: | ---: |
| `e853d92f` / 9 | `handoff-r0..r3` | Read-only three-point trace | 3 | 0 |
| `277f775d` / 10 | `stream-r0..r3` | Disable streaming before rearm; require a fresh count below the normal 128-byte prefix threshold before returning to the existing guard | 1 | 0 |
| `9965cf0c` / 10 | `fifo-r0..r3` | Also repeat the original per-frame SPR `0x1024` initialization | 3 | 0 |
| `f0259fa6` / 11 | `early-r0..r3` | Remove the extra FIFO initialization; enable streaming at a fresh 64-byte count, beyond the complete MAC/CCMP prefix | 2 | 0 |

Additional committed records in those temporary controls verify that the
fresh-count barrier actually executes: count 10 with streaming disabled,
and count 66 with streaming enabled in both exercised 64-byte controls.
Their bounded missing-count/EOF/abort exits also passed native tests.
The controls sometimes preserve a plausible complete MAC header, but
**none produces a valid continuation**. They are therefore removed from
the retained source rather than installed as an unqualified receive fix.
No PHY/radio/calibration or TX-power setting was changed.

A separate Node bitwise CRC32 implementation verifies all nine complete
early bodies and rejects all nine captured continuations. Scanning only
the bounded captured continuation bytes finds no complete FCS-valid
170/298-byte intended-length window at any offset. Neither dropping two
bytes nor shifting the body is an evidenced repair. Hardware bad-FCS
frames remain rejected; no software flag or length normalization promotes
them to good delivery.

Two trials reproduce the earlier zero-HT condition with the full frozen
delivery readout:

| Trial | HT starts | Captured / posted / consumed | Bad-FCS discards | Transport drops / pending | Vendor received-BA delta |
| --- | ---: | --- | ---: | --- | ---: |
| `stream-r3` | 0 | 3 / 3 / 3 | 68 | 0 / 0 | 122 |
| `early-r3` | 0 | 5 / 5 / 5 | 81 | 0 / 0 | 113 |

Both have zero controller error, zero host resync/stall/retry state, no
advance attempted and an active scanner that reaches the explicit stop.
This excludes a blocked F2/USB credit queue as the explanation for these
two zero-HT runs: the missing good captures occur upstream of delivery.
It does not distinguish RF/PHY decode loss from earlier MAC classification
or receive-state problems, and it does not establish a calibration cause.

Across all 16 trials, independent reconstruction of the complete split
mailbox verifies `captured = posted + dropped`,
`posted - consumed = pending` and `consumed = host F2 count`, with pending
within the one-frame credit window and no host resync/stall/retry.
`early-r1` has one pending packet at stop; the other 15 have none. It is
not silently counted as delivered. A `picotool info` readiness probe
segfaulted before `stream-r3`; a subsequent probe succeeded and all four
bound reports were collected. That host readiness event is separate from
the measured RF loss. The runner now retains every numbered readiness
attempt rather than overwriting earlier failures.

Final host/ABI cleanup build **`42424296`** uses ABI 12 and the original
three-point trace only. Its clean controller byte-compares identical to
the radio-tested `e853d92f` controller. The final RP2350/collector identity
rebuild was not an additional RF trial. Default and save-only controller
builds/tests pass and remain byte-identical to the previous corresponding
profiles. Reference ASan/UBSan/native tests cover the original guards,
record bounds, actual pre/post-arm field selection, modeled post-arm count
changes and trace immutability across terminal/new-RX paths. Runtime remains
C/nolibc; HT advertisement and production policy activation stay disabled.

The qualification frontier is now two distinct problems: upstream zero-HT
loss, and corrupt within-PPDU continuation despite successful fresh-count
and stream-control transitions. Neither is solved by this campaign. D2
work packages 1/2 remain open, as do repeated variable-length delivery,
secure BA integration and physical response/recovery qualification.

Both complete original 16-MiB flashes were restored after this campaign.
Independent readbacks `vendor-handoff-state/restored-A-full.bin` and
`vendor-handoff-state/restored-B-full.bin` under `build/clean-rx/` each
contain 16777216 bytes and byte-compare with the fresh pre-test originals.
Their SHA-256 values are respectively
`8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
and `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.
Final serial-targeted checks confirm both badges remain in BOOTSEL.

### One-shot RXE event-drain/reflush experiment (2026-10-19)

The [genuine exhausted `RXCTL = 0x804` decision](#fixed-rf-comparison-and-a-genuine-exhausted-rearm)
motivates a separate,
default-disabled `CLEAN_RX_HT_REARM_RECOVER` control. It requires the strict
passive window observer; ordinary RX, TX and advertised capabilities are
unchanged. On the first busy decision, the PSM copies decision words 1..15
to SHM `0x7c1..0x7cf`, then commits `0x7c0 = 2` last. This constant nonzero
commit also bounds recovery to one attempt even if the main sequence wraps.
The record retains the exact busy operand, remaining wait, later RXCTL,
GoodFCS sample, RX epoch and counters. It is never overwritten by the retry.

The PSM clears the transient reason, acknowledges only ext3/5/6/11/13/14
and jumps to the original RXCTL flush at PC274. That reloads the strict
65,535-poll bound. A second busy decision takes the original fatal path;
there is no PSM restart, counter reset or unlimited retry. This tests whether
stale receive conditions plus a fresh flush can restore progress; it does
not establish which external condition, if any, causes a physical timeout.

`CLEAN_RX_HT_REARM_INJECT`, also default-disabled and dependent on recovery,
forces bit 2 only in the first **latched SHM decision operand**, not hardware
RXCTL. It makes branch execution measurable without waiting for an
intermittent timeout. A forced first fault may therefore retain `0x804`
while later hardware RXCTL is `0x800` and the wait counter is not exhausted.
These controls must never be counted as natural busy-timeout recoveries.

ABI18/WND7 adds a separate 32-byte immutable first-fault block to the
96-byte window header and a first-fault commit to each 160-byte sample.
There are at most 35 samples. Recovery modes are 0 (strict/no recovery),
1 (natural recovery enabled), and 3 (injected); mode 2 is invalid.
The C3 independently checks the main and first-fault publications.
A committed first-fault block does not make error-cleanup mailbox data
coherent, nor make separately cached fields simultaneous hardware state.

Native tests execute the emitted instruction words across the existing
2,048 busy/delay/event cases and all 64 combinations of the six drained
events, with both successful and persistently busy retries. They verify
first-fault immutability, explicit second failure, selective event clearing,
unchanged unrelated events and later RXCTL changes after decision latch.
Protocol tests cover recovery modes, disabled payloads and publication
boundaries. Display timing/cancellation and existing graphics-client tests
also pass with ASan/UBSan.

The RP2350 streams the existing project-owned glyphs directly to the TFT,
without a framebuffer. Role, operation and experimental mode appear before
radio start; stopped/error state, capture count and first-fault RXCTL appear
after observer stop. There are no active-observer display updates or added
C3 display messages. Physical screen appearance is not independently
verified; `RADIO STOPPED` is not a qualification indicator.

The first four attempted injected controls (`injected{256,512}-role{0,1}-r0`)
failed before USB discovery, with no RF reports. The newly added main-loop
error conversion had treated normal pre-configuration `ref_service() = 0`
as fatal. Restoring the existing pre-configuration polling behavior fixes
this; display-initialization/clear cancellation still aborts explicitly.
These failed attempts remain archived, separate from subsequent RF trials.

#### Injected execution controls

After the USB fix, four fixed mode-3 trials with RF wrapper `6d4ba8e4`
exercise recovery at both sizes in both roles. All retain a committed first
fault (`0x804`, later hardware `0x800`, nonzero wait remaining), then a
successful main decision (`0x800`, outcome 1). First-fault commit 2 is
already visible in the first timeline sample and remains unchanged.
Capture counts continue without PSM restart or controller error:

| Trial | First to last sampled captures | Final HT starts |
| --- | --- | --- |
| `injected-fixed256-role0-r0` | 2 to 311 | 2 |
| `injected-fixed256-role1-r0` | 1 to 209 | 4 |
| `injected-fixed512-role0-r0` | 9 to 557 | 0 |
| `injected-fixed512-role1-r0` | 7 to 563 | 5 |

All four reconcile frozen capture/post/drop/consume accounting, with no
pending frames. None qualifies an early body. The zero-HT role-0 512-byte
trial still captures 557 frames and ends with a successful rearm decision;
HT-only misses therefore remain a separate failure category, not evidence
that every miss is a persistent RXE flush timeout. Three final C3 cached
snapshots are incoherent; this does not invalidate the separately committed
first-fault records or frozen accounting, and is not relabelled as coherence.
These trials establish real PSM branch execution and subsequent receive
progress, **not natural-timeout recovery qualification**.

#### Strict controls and USB readiness losses

Four fixed 512-byte controls use the same screen-enabled wrapper,
`9c377cb3`, with recovery disabled. Both role-0 trials finish without
controller fault and see HT, but retain no qualifying early body.
Both role-1 trials genuinely exhaust the wait before the first window
sample: they stop after 1 and 3 captures respectively. Their committed
decisions have sequences 4 and 8, exact/later RXCTL `0x804`, zero wait
remaining, outcome 2, GoodFCS sample 0 and no restart. RXE counts are
984/982, status1 `0x4000`, status2 `0x4140`; both report controller
error `0x88`, SHM `0x602 = 3`. These are real timeouts, not the corrected
conditional-drain fall-through. Neither faulted frozen capture is counted
as a successful delivery export.

All six immediately following natural-mode invocations fail their bounded
BOOTSEL readiness checks before loading or collecting RF evidence.
Their logs remain under `natural{256,512}-role*-r*`; there are no matching
RF JSON reports to count. A further 30-second serial-targeted readiness
check also fails. IORegistry identifies the cause: Google Chrome exclusively
owns badge A's BOOTSEL USB connection. This is a host-access failure,
not radio evidence or slow PSM recovery. The browser is not terminated.
After the user releases the badges, both serial-targeted checks succeed
and the unchanged six-trial matrix runs with fresh `natural-ready*` prefixes,
not by overwriting failures or retrying an RF trial until it qualifies.

A concurrent attempt to restore the accessible badge B aborts at 87% with
a picotool connection error during this intervention. It is not counted
as a completed restoration; a complete rewrite and independent readback
are required after the remaining trials. Fresh full original files remain
unchanged.

#### Natural timeout result and first both-role 554-byte proofs

The six accessible trials use natural mode 1 and RF wrapper `edd21932`.
Five complete radio capture without controller error or PSM restart.
One activates the recovery on a **real** exhausted busy condition:
`natural-ready256-role1-r0`. Its immutable first fault retains RXCTL
`0x804`, wait remaining 0, RXE count 982, status1 `0x4000`, status2
`0x4140`, GoodFCS 0, bad-FCS count 1 and RX epoch 2.

The retry **does not recover** this hardware state. The later main decision
is sequence 6, RXCTL still `0x804`, remaining 0 and outcome 2; TSF-low
advances from 19961 to 25696 while RXE count, RX epoch and bad-FCS count
remain unchanged. First-fault commit 2 is already present at the first
timeline sample. All 30 samples retain one captured frame, controller
error `0x88`, reason 3 and zero PSM restarts. The first fault and subsequent
fatal decision survive separately. This is one physical activation and
one failed recovery, not a successful hardware fix. Event acknowledgement
plus another identical flush is insufficient for this observed state;
more waiting and a software-only execution control cannot establish a
remedy. A stronger bounded RXE/PHY reset or revised abort sequence needs
independent hardware grounding and separate qualification before use.
The same 982-byte RXE count occurs in a 256-byte requested-size trial,
with zero HT starts and an already-faulted first sample. Together with
the strict controls, this argues against attributing these timeouts
specifically to the requested 512-byte stimulus or an observed HT aggregate.

Two of the four natural 512-byte trials now qualify complete **554-byte**
nonterminal early MPDUs, one in each role:

| Trial | Prefix / RX epoch | MPDU / HT PSDU bytes |
| --- | --- | --- |
| `natural-ready512-role0-r0` | 2 / 121 | 554 / 1118 |
| `natural-ready512-role1-r1` | 1 / 24 | 554 / 1118 |

An independent bitwise CRC32 verifies each full body. Nonce-derived
transmitter address, data-frame type, epoch/prefix linkage, matching
HT-SIG bytes, HT-SIG CRC and nonterminal geometry are checked separately:
the first delimiter plus padded body is 560 bytes, strictly below the
1118-byte PSDU. Both collector runs also retain vendor Block ACK activity
and accounted prefix exits. No recovery activates in either proof trial,
so the improved capture result is **not attributable to recovery**.
These are first both-role proofs at this size, not repeated reliability,
second-body delivery or production aggregate qualification.

Across the fixed RF matrix there are 14 actual trials (4 injected,
4 strict, 6 natural), excluding the four pre-enumeration failures and
six host-readiness failures. All 84 raw reports independently pass
length and bitwise CRC checks. All 430 WND7 rows are retained with zero
capacity loss; 210 cached publications are coherent and 220 explicitly
incoherent. Eleven successful frozen captures reconcile captured,
posted, dropped, consumed and pending counts. Three real faulted captures
remain explicit errors rather than successful delivery summaries.
The default-disabled recovery remains experimental; D2, recovery
reliability, repeated 554-byte capture and HT-only miss isolation remain
open. Normal HT advertisement stays disabled.

Fresh full backups for this campaign are
`build/clean-rx/rearm-recovery-investigation/original-{A,B}.bin`,
independently matched to the preceding verified originals before testing.
After all trials, both complete flashes are restored and independently
reread as `restored-{A,B}-full.bin` in the same directory. Each readback
is exactly 16777216 bytes, byte-compares with its fresh original and
matches the verified SHA256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

The final complete B rewrite supersedes the interrupted 87% attempt;
both final load/verify operations and independent readbacks succeed.
Serial-targeted final checks confirm both badges in BOOTSEL.

### Alternate receive-buffer control (2026-10-19)

The next bounded control tests whether immediately reusing the first
body's SHM destination alone explains corrupt continuation. Build
**`b48c71dc`**, ABI 12, retains the original early-copy and handoff guards
but arms the second body at SHM `0x900` instead of `0x800`. Both bounded
300-byte body regions are disjoint and below `0x1000`. Only after terminal
EOF does an unrolled 150-word copy move the actual second-body bytes to
the conventional `0x804` MAC-body region for existing snapshot/F2 handling.
The original aggregate HT-SIG is retained. This moves bytes without
changing length, status, FCS or admission; it never enables clean TX.

Before RF, ASan/UBSan/native tests exercise all RX counts 50..306,
non-EOF, original-base, missing-advance, abort, short and oversize cases,
source/destination bounds, prefix/register preservation and no TX.
The valid modeled copyback takes at most 180 instructions, not a physical
timing measurement. The temporary native interpreter also stops at the
existing PC-266 rearm boundary. These are control tests, not RF proof.

Eight trials, `build/clean-rx/vendor-alt-buffer/alt-r0..r7`, alternate
vendor B / observer A on even numbers and the opposite roles on odd
numbers. All use the unchanged 128-fixture vendor/AP stimulus.

| Trial | Early / requested next body | Measured post-arm base | Terminal RX count | Result |
| --- | --- | --- | ---: | --- |
| `alt-r1` | 170 / 298 bytes | `0x900` | 302 | Valid early body; bad continuation |
| `alt-r4` | 170 / 298 bytes | `0x900` | 306 | Valid early body; bad continuation |

The other six trials lack an eligible early body; `alt-r3` and `alt-r5`
have zero HT starts and never advance. No trial qualifies a complete
pair. An independent bitwise CRC32 implementation verifies both early
bodies, rejects both continuations and finds no FCS-valid contiguous
170/298-byte window anywhere in their bounded 300-byte captures.
Both the post-arm trace and frozen terminal headers report `0x900`.
This shows that same-destination reuse alone is not sufficient to explain
these two failures. It does not rule out other copy/FIFO/context bugs,
early-retention latency or upstream RF/PHY loss, and it does not qualify
both size orders.

All eight full frozen delivery accounts reconcile. They contain 2..10
explicit transport drops per trial, not silently counted as delivered.
`alt-r4` has one pending frame at stop; the other seven have none.
All have zero host resync/stall/retry state. These observations are not a
claim of lossless transport. Collector exit status 1 in all eight trials
correctly records incomplete RF qualification; all raw phase reports
and errors are retained.

The unsuccessful alternate-buffer behavior and its temporary native
hooks/tests were removed. The exact tested controller/UF2s/collector,
build ID, and tested observer-header/native-test sources are retained in
the campaign directory. The retained controller byte-compares identical
to the preceding trace-only `42424296` image; default and save-only
profiles also remain unchanged.

The retained host improvement enforces the full frozen capture/post/drop/
consume accounting instead of checking only the pending window and
consumed totals. It validates both mailbox magic values, saturated posted
and dropped halves, capture/post ordering, the one-frame credit window,
both host consumed totals and the summary bound. Saturation is checked
against the full counters; a pending frame or recorded drop is not
relabelled as delivery. Native tests cover zero, one-pending, saturation
boundaries, maximum nonwrapped counters and malformed reports; the same
guard accepts all 30 preserved captures from this and the preceding two
campaigns. Final build **`8bbed93e`**, ABI 12, passes the reference
native/sanitizer tests and preserves the 244504/75140/60528 text-byte
footprints. Its controller is byte-identical to `42424296`; the RP2350
identity/host-guard rebuild is not an additional RF trial.
D2 packages 1/2 remain open.

Both complete original flashes were restored after the controls.
Independent `vendor-alt-buffer/restored-A-full.bin` and
`vendor-alt-buffer/restored-B-full.bin` readbacks under `build/clean-rx/`
each contain 16777216 bytes and byte-compare with the fresh full backups.
The SHA-256 values match the preceding original-image hashes
(`8cda7a00...23628` for A and `bc3f972d...21e2` for B).
Final serial-targeted checks confirm both badges remain in BOOTSEL.

### Early-capture causes and hardware controls (2026-10-19)

This investigation separates an **observed eligible early-body opportunity**
from a trial that merely contains A-MPDU-format headers or vendor BA activity.
The latter can contain only singleton bodies. It also keeps early retention
separate from the still-unqualified next-body handoff.

#### Retrospective opportunity audit

The 30 full captures in `vendor-receive-state/`, `vendor-handoff-state/`
and `vendor-alt-buffer/` contain 182 retained prefix records:

- 23 bounded multi-body good-hint candidates occur across 14 trials.
  All 14 trials retain their first eligible candidate.
- Nine subsequent candidates are intentionally ignored after immutable
  first-multi retention, not failed copies.
- 127 records are terminal-only singleton A-MPDU-format receptions without
  a nonterminal good hint. Other records include singleton good hints and
  bodies outside the diagnostic storage bound.

Thus the earlier unsuccessful-trial count was not a retention-loss rate.
This is an audit of the bounded saved log, not all transmissions: its
16-record limit and overflow can hide later opportunities.

The collector now reports `early_capture_reason` for each record, including
unsupported MCS, missing nonterminal hint, singleton geometry, storage cap,
inconsistent body geometry, first-multi retention and deliberately ignored
later candidates. Singleton classification checks both lower and upper
delimiter/padding bounds; a body larger than its PSDU is not a singleton
success. An otherwise eligible unretained record remains an explicit
investigation result, not a silently successful trial.

#### Size-isolated, receive-only campaign

`reference-early` enables HT RX, the passive observer and early save,
**without next-body advance**. Its controller byte-compares identical to
the previously tested save-only image. No M3, PSM, PHY/radio initialization
or clean TX behavior was changed for this investigation.

The optional runner selector submits 128 equal-size Ethernet fixtures:
128/256/512 bytes, corresponding to measured 170/298/554-byte protected
MPDUs. The profile is nonce/session-bound. Focused qualification requires
the exact requested early length, a genuine nonterminal multi candidate,
fresh transmitter/prefix/epoch correlation, independently valid complete
FCS, vendor BA activity and accounted exits. It does not demand two
unrelated ordinary F2 sizes or advance to a second body.

All evidence is under `build/clean-rx/early-capture-causes/`:

| RF build | Trial prefixes | Purpose | Qualifying trials |
| --- | --- | --- | ---: |
| `db3cd284`, ABI 13 | `size{128,256,512}-role{0,1}-r{0,1}` | Constant-size baseline; stopped digital hardware endpoints | 7/12 |
| `1a2ea0e4`, ABI 13 | `prepared512-role{0,1}-r{0,1,2}` | Eight prepared fixtures; measured submission intervals | 1/6 |
| `1a2ea0e4`, ABI 13 | `prepared256-role1-r{0,1,2}` | Repeat the role with an earlier zero-HT boot | 1/3 |
| `17612e0b`, ABI 14 | `stopped256-role1-r{0..5}`, `stopped256-role0-r{0,1}` | Actual stopped PSM state, not normalized historical mailbox state | 8/8 |

Role 0 is vendor B / observer A; role 1 is vendor A / observer B.
A is `08DA26F86E57F5EA`, B is `9BBF88DD17D66E7D`.
All runs used the approved AP's channel 6, requested MCS0 and clean TX off.
Exact RF collectors, images and build identities are archived separately.
Final native-validated builds `86101c9a` (early-only) and `a24b1a8c`
(default advance) additionally exclude unused preparation storage from
the advance build; they are not additional RF trials. The default controller
byte-compares identical to the prior retained `8bbed93e` controller.

Across all 29 trials:

| Ethernet fixture / early MPDU | Role 0 | Role 1 |
| --- | ---: | ---: |
| 128 / 170 bytes | 2/2 | 2/2 |
| 256 / 298 bytes | 4/4 | 8/11 |
| 512 / 554 bytes | 1/5 | 0/5 |

All 17 qualifying early bodies independently pass a separate bitwise CRC32
implementation, fresh nonce-derived TA, exact size, epoch and prefix checks.
All 116 raw phase files have the expected 5952-byte length and valid report
CRC. The 257 retained headers contain 56 logged multi candidates:
17 retained first candidates and 39 intentionally ignored later candidates.
The remainder comprises 152 terminal singleton records, 20 singleton good
hints, 28 non-A-MPDU records and one invalid HT-SIG.
No logged eligible first-multi retention failure was found.
All 59 old/new full captures also pass the native/sanitized complete
capture/post/drop/consume guard. Explicit drops and pending frames are not
counted as successful delivery.

The 12 nonqualifying trials divide into nine without a logged eligible
multi-body opportunity and three with **zero HT starts**. All three
zero-HT cases are role 1: `size256-role1-r0`,
`prepared512-role1-r1`, `prepared256-role1-r2`.
They deliver only 4/4/2 ordinary frames, with no transport drops or pending
frame, and no host resync/stall/retry state. Their bad-FCS counters are
64/98/70, and the controller records over two million receive polls.
This distinguishes them from an early-copy rejection and demonstrates
some bad-frame processing; it does not prove continuous receive readiness.
The subsequent eight stopped-state controls all qualify, so they provide
healthy comparison data, **not** a reproduced zero-HT state or a fix.

#### Submission pacing is measured, not aggregation proof

At 512 bytes, the baseline's shortest host write is 836 us and a complete
128-frame burst takes about 137.8 ms. Preparing just the first eight frames
and prefix digests before submission removes repeated payload/digest work
from that part of the burst, using 8224 bytes of focused vendor BSS.
Measured prepared first-eight intervals are 6771..6783 us, versus
8376..8385 us for the following eight generated frames; complete bursts
take 136090..136966 us. Minimum write time remains 836 us.

A 554-byte MCS0 HT20 body has nominal PHY airtime around 724 us before
contention/response overhead. Submission can therefore limit queue buildup,
but the vendor/AP may also batch, retry or defer traffic. One prepared
role-0 trial retains a complete valid 554-byte early body, proving the
save-only path works beyond the advance profile's 480-byte cap.
This is neither repeatable in both roles nor proof that preparation caused
that capture; the encrypted body is not attributed to one of the first
eight submitted fixtures. No unsupported vendor aggregation-control write
or speculative bus-clock increase was used.

#### Hardware and calibration boundaries

The [board record](../../docs/cyw43/hardware.md) identifies the RM2 module,
always-on 3.3-V radio supply and shared WLON/BTON on GPIO23. GPIO24 carries
data/IRQ, GPIO25 CS and GPIO29 the host bus clock. GPIO41 switches a
peripheral/display rail, not the radio supply. The configured 37.4-MHz
RF reference is not the host bus clock or an oscillator measurement.
The [local CYW43439 datasheet](../../pimoroni/Infineon_CYW43439_DataSheet_v03_00_EN-3074791.pdf)
is not a D11/LCN20 register-programming specification.

ABI 13 snapshots six raw words before receive and after explicit controller
stop, only while selector ownership is safe: ChipCommon ID, ChipCommon
clock control/status, D11 clock control/status and PMU status/resources/
pending. Every before/after vector in all 29 trials is identical:
`[356886959,720960,118161986,62,2130329079,0]`.
Chip word `0x1545a9af` identifies CYW43439, revision 5, package code 4;
the separately established D11 revision is 39. Neither is the RP2350's
A4 revision.

The existing [calibration record](../../docs/cyw43/calibration.md) and
initialization code show readiness/completion checks for PLL, RC, VCO and
DC. These are not an analog-quality test. The saved RC values vary by
badge and overlap successful and failed reception. In older captures,
the exact tuple `[4871,4292,9,5,255,4095]` occurs in both zero-HT `alt-r5`
and HT-positive `fifo-r3` with ten HT starts. Consequently these reported
values cannot alone identify a bad calibration. The all-ones DC values
are completion words checked by the initialization predicate, not measured
residual DC offsets.

ABI 14 additionally preserves raw D11 PSMDEBUG and actual SHM
`0x600..0x60b` after stop. This avoids mistaking the controller's normalized
historical mailbox state for the actual final state. PSMDEBUG remains
unmasked; one stopped sample is not a retired-PC or continuous stuck-state
trace. These reads do not touch PHY/radio selectors or change the receive
timing path.

| Possible cause | What this investigation establishes | What remains |
| --- | --- | --- |
| Incorrect opportunity denominator | Singleton receptions and immutable later-candidate policy explain many apparent missing copies | Bounded logs do not establish all on-air opportunities |
| Wrong size/admission profile | Save-only admits 554 bytes; advance remains capped at 480 | Repeat larger early bodies in both roles |
| Failed first-body copy | All 17 observed first eligible candidates are fresh and FCS-valid | No general lossless/rate threshold is qualified |
| F2/USB delivery loss | Full accounts reconcile; explicit drops remain visible; zero-HT trials have no final backlog/drop | Endpoint accounting does not exclude transient receive inhibition/backpressure |
| Cold PHY/calibration or receive state | Zero-HT is upstream of recorded HT starts, not a rejected saved body; reported calibration tuples do not separate outcomes | Reproduce a zero-HT boot with the actual stopped-state probe; compare controlled receiver initialization |
| Badge-specific RF/power/clock problem | Both badges work with vendor firmware and both clean roles have valid early captures; digital endpoints agree | Rail ripple, RF-reference jitter, antenna/desense and transient gating are not physically measured; roles also couple transmitter and placement |
| Next-body FIFO/copy context | Early-only success does not exercise advance; earlier alternate-buffer failures remain separate evidence | D2 package 2 remains open |

Do not alter RF tuning or declare a defective badge from these observations.
The next discriminating control is a zero-HT reproduction with actual
stopped PSM state, followed by controlled receiver-initialization and
physical-position comparisons. Independent air capture is needed to
distinguish absent multi-body stimulus from missed RF opportunities;
physical rail/reference measurements are needed for analog claims.
Package 1 remains open for reliability/zero-HT diagnosis, and HT station
advertisement remains disabled.

Both fresh complete original images were preserved before the campaign
and compared with the previously verified originals. After all 29 trials,
both full flashes were restored and independently reread as
`early-capture-causes/restored-A-full.bin` and
`early-capture-causes/restored-B-full.bin` under `build/clean-rx/`.
Each 16777216-byte readback byte-compares with its fresh backup and has
the expected SHA-256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

Final serial-targeted device checks confirm both badges are in BOOTSEL.
Both final profiles pass controller tests and native ASan/UBSan tests; all final
ELFs have no undefined symbols, runner syntax and whitespace checks pass,
and the runner's invalid-size rejection and collector's advance-build
focused-profile rejection precede their respective USB operations.

### Zero-HT reproduction and PHY-control comparison (2026-10-19)

The follow-up preserves evidence under
`build/clean-rx/early-capture-followup/`. It completes **20 RF trials**:
ten baseline trials with `86101c9a`/ABI 14, then ten opt-in PHY-control
trials with `02242390`/ABI 15. Two additional baseline role-1/512 attempts
fail BOOTSEL readiness and are retained separately, not counted as RF
failures or submitted bursts.

The baseline reproduces the previously missing actual stopped-state
evidence. Four complete trials have zero HT starts:

| Trial | Observer | Ordinary F2 deliveries | Bad-FCS count | Actual SHM `0x601` | Raw PSMDEBUG |
| --- | --- | ---: | ---: | ---: | ---: |
| `baseline256-role1-r0` | B | 4 | 62 | 3 | 3637906 |
| `baseline256-role1-r6` | B | 4 | 88 | 11 | 3571880 |
| `baseline512-role0-r1` | A | 818 | 51 | 3 | 98958 |
| `phy256-role1-r4` | B | 2 | 69 | 3 | 2392718 |

These are **not one established failure mechanism**. The three B cases
have very low ordinary reception with ongoing bad-frame processing.
The A case delivers 818 ordinary frames despite no recorded HT start.
Its first two ordinary samples independently pass FCS. Zero HT starts
therefore must not be described as a dead receiver or, now, a B-only
symptom. Vendor counters confirm all four submitted 128 frames, received
104..124 BAs and had zero TX-error/TX-failure deltas; one has one retry.
None has a final F2 backlog/drop or host resync/stall/retry state.

Actual final PSM state/debug does not distinguish these cases from
HT-positive reception. For example, raw debug **3637906** also occurs in
`baseline256-role1-r4` with 18 HT starts; state **11** occurs in the earlier
qualifying `stopped256-role1-r5`. Raw debug **2392718** also occurs in the
HT-positive baseline repeat 7. These comparisons do not require an assumed
PC mask. They exclude a unique terminal-state signature, not every
transient or an unobserved in-window stall.

#### Opt-in PHY endpoints

`reference-phy` adds only `CLEAN_RX_HT_PHY_SNAPSHOT=1` to the early-only
profile. The default-disabled controller probe reads six existing
initialization/control registers using the established **16-bit PHY
selector/read** helpers. It must not use a 32-bit write to the adjacent
PHY address/data pair: that is a packed PHY configuration write, not a
read-selector operation.

The Cortex-M3 records the controls before MAC RX enable and after MAC/PSM
disable, before the stop path reapplies TX inhibit. There is no per-frame
sampling, RP2350 access to a live PHY selector, PHY configuration write,
calibration retry or clean transmission. The 32-byte commit/phase/value
block fits the explicitly asserted SRAM gap `0x203e0..0x203ff`; the host
reads it only after `RX_DONE`, into trace bytes 872..903. ABI 15 requires
the expected magic and both committed phases when enabled; disabled
profiles report `enabled:false` and require the reserved bytes to be zero.

All ten instrumented trials, **including the low-RX zero-HT boot**, have
identical before/after controls:

| PHY register | Raw value |
| --- | ---: |
| `0x440` | `0x3e01` |
| `0x44a` | `0x0080` |
| `0x4b0` | `0x0080` |
| `0x43a` | `0x0040` |
| `0x4b1` | `0x0004` |
| `0x439` | `0x0bd0` |

In particular, the source's PHY-deaf bit (`0x440` bit 0 clear) is not set
and its radio-reset bit (`0x44a` bit 8) is clear at both endpoints.
Differences in these sampled latched controls do not explain the failed
boot. This does not measure dynamic AGC, OFDM/HT acquisition, antenna gain,
analog calibration quality, rail noise or transient resets.

All four zero-HT trials also have the same stopped clock/PMU vector as
HT-positive trials. Completion/calibration values remain nonseparating.
Do not replace these findings with a speculative PHY reset or calibration
write, or hide the failure by automatically retrying until a good boot.

#### Capture and infrastructure accounting

The 189 saved headers contain eight logged multi candidates: three retained
first candidates and five deliberately ignored later candidates.
The three retained 298-byte bodies independently pass bitwise CRC32,
fresh nonce-derived TA, prefix, epoch and exact-length checks.
The other records are 141 terminal singletons, 31 singleton good hints
and nine non-A-MPDU headers. No logged first eligible retention failure
is observed. The four complete 512-byte fixture trials produce no retained
554-byte early body; finite logs and overflow still prevent a complete
on-air opportunity census.

All 80 raw phase reports have the expected 5952-byte length and independent
report CRC. Full delivery accounting passes for all 79 preserved old/new
captures under ASan/UBSan.

After the last baseline role-0 trial, A disappears from both BOOTSEL and
the live-report USB interface. A serial-targeted software BOOTSEL request
cannot find it. The user reconnects both badges in BOOTSEL, after which
the instrumented cohort completes. This is a separate USB/reboot-transition
failure, not proof that a cable caused the preceding RF miss. The reconnect
also prevents treating between-cohort success differences as a controlled
effect of instrumentation or fixed physical placement.

Final native-only identities are `8736ed06` (PHY probe), `a3d7caa4`
(early-only) and `e7fa68cc` (default advance). The additional commit/range
tests do not change the RF-tested PHY controller. The disabled early-only
and advance controllers byte-compare with their preceding images, and
the ordinary non-observer controller remains byte-identical too.
All three profiles pass controller and native/sanitizer tests.

The next discriminating measurements are **burst-window** receive readiness
and PHY-error activity, rather than only final state or totals over the
1.2-second drain; safe comparisons of OFDM/HT acquisition state for low-RX
boots; and independently confirmed multi-body stimulus for the 554-byte
case. The then-reported 836-us minimum 512-byte host write was in raw TIMER0
counts, not calibrated microseconds; see the correction below. Submission
speed remains worth testing separately from receiver initialization. Neither
a tuning fix nor a physical hardware defect is established.

Both complete original flashes were restored after this follow-up campaign.
Independent 16777216-byte readbacks at
`build/clean-rx/early-capture-followup/restored-{A,B}-full.bin`
byte-compare with the fresh originals and match their verified SHA256 hashes.
Serial-targeted checks confirm both badges remain in BOOTSEL.

### Burst-window diagnosis, RXE rearm, and fault recovery (2026-10-19)

**Correction from the subsequent decision-level investigation:** the first
strict guard had a conditional-drain fall-through bug. Its reason-3 faults
do not establish that the longer hardware wait failed. The old native model
incorrectly treated the conditional drain as an unconditional branch.
See [the corrected guard investigation](#rearm-decision-capture-and-conditional-drain-correction-2026-10-19).
Original-rearm low-RX/stale-data observations and independently verified
captured bytes below remain valid.

The next campaign uses fresh complete originals in
`build/clean-rx/burst-window-investigation/`, independently byte/hash-matched
to the preceding verified originals before experimental writes.
Role 0 is vendor B / observer A; role 1 is vendor A / observer B.
No credentials are included in the images or evidence.

#### Passive window instrumentation and timing correction

Default-disabled `CLEAN_RX_HT_WINDOW=1` publishes cached HT/log/early metadata
through a seqlock in the existing 32-byte diagnostic gap at `0x203e0`.
The RP2350 samples this publication, cached receive mailbox, capture count,
controller errors and direct PSMDEBUG at approximately 100-ms intervals.
It does not touch the live D11 object or PHY selectors. Phase 3 arms the
timeline; separate phase 4 exports it after radio power-off without
overwriting the frozen capture report. Capacity is 36 samples with explicit
overflow accounting; final C3 cleanup invalidates the publication.
The PHY endpoint and window profiles cannot share the gap.

The first pilot exposed a measurement error: RP2350 TIMER0 counts were
labelled microseconds despite the inherited BOOTSEL clock state.
The application's already existing USB SOF calibration now appears in
CRC-bound report bytes 40/44. Across the calibrated cohort, about
15,091..15,290 ticks span 32,000 microseconds, not 15,091..15,290 microseconds.
ABI 16 converts scalar durations with checked calibration and 64-bit
arithmetic; bucket boundaries remain explicitly raw ticks. No clock or
RF configuration is changed. The separate 37.4-MHz RF reference is not
diagnosed as defective by this observation.

Historical timing such as the previously labelled 836-us minimum
512-byte write is uncalibrated raw TIMER0 counts. Exact retroconversion
is not possible without that boot's calibration. New calibrated 256-byte
minimum writes are 946..958 us; the guarded 512-byte minimum is about
1771 us, compared with approximately 724-us nominal MCS0 airtime for a
554-byte MPDU. This constrains queue buildup, but neither proves nor
excludes aggregation. Existing firmware transport deadlines remain raw
timer counts; this is measurement correction, not global timebase repair.

#### Failure localized to receive processing, not stopped cores

Four fixed pilot role-1/256 trials include two valid 298-byte early
multi-body captures and one low-RX zero-HT failure. Eight planned calibrated
original-rearm trials complete seven RF exchanges: four more valid 298-byte
captures and another low-RX zero-HT failure. The eighth is a readiness
failure, retained separately.

In calibrated `timed256-role1-r3`, coherent samples show:

| Elapsed us | Captured | Bad-FCS counter | RXCTL |
| ---: | ---: | ---: | --- |
| 14 | 1 | 1 | `0x800` |
| 100053 | 2 | 4 | `0x806` |
| 200089 | 2 | 10 | `0x806` |
| 300111 | 2 | 12 | `0x806` |
| 2830766 | 2 | 90 | `0x804` |

The cached PLCP remains `403134010882`, RXE count remains 897, and HT
starts/headers/early identity remain zero. Cortex-M3 polls advance from
61,440 to 2,318,336 and direct PSMDEBUG changes. This excludes a permanently
halted C3/PSM core, not every transient stall. The rising bad-FCS count
can represent recycling of stale events rather than independent on-air
corrupt frames. A coherent empty pre-burst observer log also excludes a
previously consumed 16-prefix log as this failure's explanation.
Pilot `window256-role1-r2` similarly retains four ordinary frames while
bad-FCS advances from 10 to 93 and RXCTL stays `0x806/0x804`.

An independent audit of all 79 preceding captures finds:

| Capture group | Final RXCTL distribution |
| --- | --- |
| 68 HT-positive | 65 at `0x800`, two at `0x802`, one at `0x806` |
| Eight low-RX zero-HT | Five at `0x804`, three at `0x806` |
| Three ordinary-RX zero-HT | Three at `0x800` |

Thus the persistent busy/stale-data/no-progress combination is a strong
lead, but bit 2 alone is not a unique failure detector. HT-positive
`early-capture-causes/size128-role1-r0` ends at `0x806`, and new healthy
`timed256-role0-r2` samples `0x804` transiently. Ordinary-RX HT-only misses
remain a different unresolved class. Prior calibration values also overlap
healthy boots; no analog hardware defect or tuning remedy is established.

A disappears from USB after the third complete calibrated role-0 trial.
A serial-targeted software recovery finds no live interface. The user
reconnects both badges in BOOTSEL. The USB interruption is separate from
the RF failure; the reconnect prevents fixed-placement causal comparisons
between the pre/post cohorts.

#### Concrete rearm path and the hidden-restart confound

Decoded original PSM startup and rearm paths differ:

- Startup PC80..87 requests RXCTL `0x14`, waits up to 65,535 polls and
  explicitly faults with SHM `0x602 = 3` on timeout.
- Rearm PC274..280 requests the same flush, waits only 255 polls, then
  silently proceeds to the external-condition drains even if bits 2/4
  still read busy. It subsequently restores the receive base/limit and
  returns to the receive loop.

Default-disabled `CLEAN_RX_HT_REARM_WAIT=1` extends the latter bound to
65,535 and verifies both bits before event draining, using the original
fault capture path with reason 3 on failure. Native tests execute the
real instruction words for all relevant bit combinations and completion
delays `0,254,255,256,4096,65534,65535,65536`. The original disabled image
is reconstructed byte-for-byte.

The first eight guarded trials all see HT, including a valid 554-byte
early body in `rearm512-role0-r0`. Four fixed original-rearm reversal
trials also all see HT, with bad-FCS counts 27..203 and no early bodies.
Neither cohort reproduces low-RX failure, so this comparison is not proof
of a repair.

More importantly, guarded bad-FCS counters decrease from one to zero in
some timelines. The existing non-station controller automatically calls
`d11_restart_rx_psm()` on PSM fault, resetting those diagnostics.
Consequently the eight guarded trials test **longer wait plus existing
fault-triggered restart**, not waiting alone. Their final zero counters
must not be presented as absence of errors or recovery.

The final strict profile disables that automatic RX-fault restart,
counts restart attempts, and exports the actual error address/value.
The separate restart call after an explicit TX/rearm command is not
reachable in this passive experiment: it boots with TX disabled and
checks that flag before arming. Other profiles retain existing behavior.

Three fixed `noretry256-role1-r0..2` controls all fail explicitly with
controller state 255 / error `0x88`, no HT starts and zero recorded restart
attempts. The next two fixed `fault256-role1-r0..1` controls export SHM
`0x602` reason **3**, with zero restart attempts, across all 60 retained
samples. Both have already faulted at the first sample, before the vendor
burst. Their PSM-latched and later RXCTL are `0x800`, so the busy condition
is not proved permanent: it can clear between the branch decision and
fault readout. Snapshot coherence is correctly false after fault cleanup;
direct error fields are not claimed to be coherent live RX snapshots.

The initial interpretation was that longer waiting alone failed.
The subsequent conditional-drain correction retracts that interpretation:
these five faults can be generated by the experimental guard itself.
They are not a transparent reproduction of the intermittent original-rearm
failure. The original RXE flush/stale-event lead still needs independent
qualification. Do not promote the flawed control into normal RX.

#### Evidence, validation, and qualification boundary

The campaign retains 23 completed capture trials, five completed
deliberately non-restarting fault controls, and one separate readiness
failure. No failed trial is discarded:

| Prefix/profile | Build | Format | RF exchanges |
| --- | --- | --- | ---: |
| `window256-role1-r0..3` | `88aabd0c` | ABI15/WND1, raw ticks | 4 |
| `timed256-role1-r0..3`, `timed256-role0-r0..2` | `96cb83c4` | ABI16/WND2 | 7 |
| `rearm256-role1-r0..3`, `rearm256-role0-r0..1`, `rearm512-role{0,1}-r0` | `9d736374` | ABI16/WND3, automatic recovery | 8 |
| `reversal256-role1-r0..3` | `f1c25d92` | ABI16/WND3, original rearm | 4 |
| `noretry256-role1-r0..2` | `236d8fa9` | ABI16/WND4, no RX-fault restart | 3 |
| `fault256-role1-r0..1` | `ce436e87` | ABI16/WND5, actual fault reason | 2 |

Each profile's images, controller, build identity and collector are archived.
The [experiment README](../vendor-burst/README.md#timer-units-and-current-window-encoding)
specifies the versioned window layouts. There are 168 raw phase reports:
140 at 5952 bytes and 28 arm acknowledgements at 64 bytes. Independent
length/CRC checks pass; all 778 timeline records are retained with no
capacity loss. Frozen delivery accounting reconciles for the 23 completed
captures; the five fault controls are not successful frozen captures.

Seven fresh early nonterminal bodies independently pass bitwise FCS,
nonce-derived transmitter address, epoch and HT-SIG linkage checks:
six 298-byte bodies (`window` role1 r0/r3; `timed` role1 r1/r2 and role0
r1/r2), plus the 554-byte guarded role-0 body. The latter is valid byte
evidence despite the recovery confound, not reliability qualification.
The 279 retained prefix records classify as seven retained multi candidates,
six later prior-multi-already-retained cases, 196 terminal single bodies,
46 single-body good hints, 22 non-A-MPDU and two invalid HT-SIG records.
No logged first-eligible early-copy failure is demonstrated.

All five final reference profiles and the ordinary controller pass focused
build/native/ASan/UBSan tests. Ordinary, early, PHY and advance controller
bytes remain identical to their preceding verified images. The final strict
controller matches RF build `ce436e87`; final native-only wrapper identities
and footprints are in the README. No HT advertisement, negotiated aggregate
operation, physical-hardware remedy, reliability threshold or D2 completion
is claimed.

Both complete original flashes were restored after the final fault controls.
Independent 16777216-byte readbacks at
`build/clean-rx/burst-window-investigation/final-restored-{A,B}-full.bin`
byte-compare with the fresh originals and match their verified SHA256 hashes.
Serial-targeted checks confirm both badges remain in BOOTSEL. A transient
BOOTSEL discovery failure and a rejected readback-option ordering are
retained in the preservation logs; the corrected final verification passes.

### Rearm decision capture and conditional-drain correction (2026-10-19)

Tracing the strict control's exact instruction flow identifies a software
defect in that diagnostic, not a newly established hardware defect:

```
verify RXCTL bit 2 -> fault if set
verify RXCTL bit 4 -> fault if set
conditionally acknowledge GoodFCS/ext3 -> original PC281 if set
fault: reason = 3
```

When ext3 is absent, the conditional drain falls through into `fault` even
when RXCTL is `0x800`. In the original image the drain's branch destination
and its fall-through are both PC281. Appending a fault block immediately
after the relocated instruction changed this property. The previous native
rearm interpreter unconditionally assigned the branch target for opcode
`0x783`, so it could not detect the absent-event path.

The correction inserts an explicit resume jump after the conditional
acknowledgement. Native execution now models event presence, conditional
branching and acknowledgement separately, exercises all four ext3/ext6
combinations across the existing 32 busy-bit / eight delay cases, and checks
that successful rearm clears only ext3 while faults preserve pending events.
Each case also runs with live RXCTL clearing after the decision latch, so
the guard must still use the retained operand rather than the later read
(2048 modeled executions in total).
The disabled ordinary/early/PHY/advance behavior is not intentionally changed.
No speculative drain-order change is made before testing this correction.

The strict profile also retains 16 PSM decision words at SHM `0x7b0..0x7bf`:
sequence, exact latched RXCTL branch operand, remaining wait count, RXE byte
count, status1/2, copy length, PHY length, TSF low/high, success/fault outcome,
later RXCTL, non-clearing GoodFCS event sample, bad-FCS count, RX epoch and
retained-header count. Both guard tests use the same latched RXCTL value;
the later read is separately identified. An odd/even PSM sequence protects
updates; C3 marks a changed sequence invalid during its cached copy at
`0x20d00`, inside the existing C3 publication. Automatic RX-fault restart
remains disabled in the strict profile, so a true first fault is preserved.
This is cached diagnostic evidence, not a simultaneous PHY snapshot.

ABI 17 / WND6 extends the window header from 24 to 64 bytes, leaving the
36 existing 156-byte rows and 5888-byte transport unchanged. Header words
24/28 hold enabled/reserved fields; bytes 32..63 hold the last cached PSM
decision. Disabled profiles require zero decision bytes. Encoding, reserved,
disabled-data and torn-publication checks are covered by native tests.

#### Fixed RF comparison and a genuine exhausted rearm

Fresh complete originals in
`build/clean-rx/rearm-decision-investigation/original-{A,B}.bin` are independently
byte/hash-matched to the preceding verified final readbacks before flashing.
Four original-rearm 256-byte trials (two per role) run first, followed by eight
corrected strict trials (two per role/256-byte and role/512-byte cell).
There are no retry-until-good trials or manual reconnects in this matrix.
The original-rearm controller byte-matches its previous window image.
RF wrappers are `0f33f4ae` (baseline) and `5381cb74` (corrected strict).

All four baseline trials finish radio capture without controller fault or
recorded restart, but none qualifies an early multi-body capture. All see HT.
Seven corrected strict trials also finish without controller fault or recorded
restart. Three qualify fresh 298-byte early bodies:

| Trial | Prefix / RX epoch | Independently checked body |
| --- | --- | --- |
| `rearm256-role0-r0` | 5 / 47 | 298 bytes |
| `rearm256-role1-r0` | 6 / 96 | 298 bytes |
| `rearm256-role1-r1` | 2 / 49 | 298 bytes |

Their complete-body bitwise FCS, nonce-derived transmitter address, HT-SIG
CRC, epoch/prefix linkage and nonterminal geometry are independently checked.
The corrected guard resumes with both absent and present GoodFCS events.
This removes the false pre-burst reason-3 failure; the old native model
and control are not used as evidence that waiting alone fails.

Crucially, `rearm512-role0-r0` now records a **genuine** exhausted rearm:

| Decision field | Retained value |
| --- | --- |
| PSM sequence | 1786, committed/even |
| Exact RXCTL tested by both guard branches | `0x804` |
| Wait counter remaining | 0 |
| Later RXCTL | `0x804` |
| Decision outcome | 2, busy-bit fault |
| RXE count / status1 / status2 | 444 / `0x5800` / `0x40` |
| GoodFCS event sample | 0 |
| Bad-FCS count / RX epoch / retained headers | 134 / 758 / 1 |
| Direct controller state / error / SHM reason | 255 / `0x88` / `0x602 = 3` |
| Recorded PSM restart attempts | 0 |

The last active sample at 2,900,831 us has 610 captured frames. At 2,921,820 us
the receiver has faulted after 611 frames and one HT start. This is not a
pre-burst startup fault or an inference from the later stopped RXCTL:
the decision itself retains the busy operand and exhausted wait counter.
The PSM record is committed; whole-mailbox C3 coherence is correctly false
after fault cleanup, not relabelled as a coherent live PHY snapshot.
One otherwise completed 256-byte trial also has a final cache-coherence
miss; its independently verified frozen early body does not depend on that
window snapshot.

This directly establishes that a longer wait can still exhaust under real
traffic, independently of the corrected software fall-through. It does not
prove that every historical zero-HT case has this cause, nor establish why
RXCTL bit 2 remains busy. Faults after hundreds of frames and transient
healthy busy-bit samples argue against diagnosing a simple permanently
broken radio. Preserve this first-fault record when testing one-shot
event-drain/flush recovery; then compare fixed controls and repeat recovery
under load before changing normal RX.

No 554-byte early body qualifies in the four corrected 512-byte trials.
Three finish without controller fault; the fourth is the genuine busy
timeout above. A stronger independently confirmed multi-body stimulus is
still needed, separately from receiver recovery. The four baseline plus
seven completed strict captures retain 68 classified prefixes: 36 terminal
single bodies, six single-body good hints, three retained multi candidates,
one later prior-multi-already-retained, 20 non-A-MPDU, one unsupported early
MCS and one invalid HT-SIG. The faulted trial's one header is visible only
in cached metadata, not miscounted as a successfully exported frozen log.

All 72 raw phase reports independently pass bitwise CRC and length checks:
60 full 5952-byte reports and 12 64-byte arm acknowledgements. All 360
window rows are retained without capacity loss. Eleven successful frozen
captures reconcile controller/post/drop/consume accounting, including
explicit drops and permitted pending frames; the faulted capture remains
an error, with no synthetic successful readout.

All five final reference profiles and the ordinary controller pass builds
and focused native/ASan/UBSan tests. Ordinary, early-only, PHY, advance and
original-rearm window controllers remain byte-identical to their preceding
verified images. The corrected strict controller matches RF `5381cb74`;
final native wrapper identities and measured footprints are in the README.
The strict diagnostic remains default-disabled. Neither recovery reliability,
554-byte repeatability, HT-only misses, normal HT advertisement nor D2
completion is claimed.

Both complete original flashes were restored after this fixed matrix.
Independent 16777216-byte readbacks at
`build/clean-rx/rearm-decision-investigation/restored-{A,B}-full.bin`
byte-compare with the fresh originals and match their verified SHA256 hashes.
Final serial-targeted checks confirm both badges remain in BOOTSEL.

### Parked one-shot PHY receive reset (2026-10-19)

The failed natural event-drain/reflush motivates a stronger, separately
gated control, not a change to normal RX. The public CYW43439 datasheet
does not specify the internal RXE command bits sufficiently to justify a
new guessed reset mask. The original PSM abort at PC266..269 resets
crypto controls (`0x7c0/0x7d0`), not the PHY. Reusing it is therefore not
an independent stronger PHY-reset experiment.

The selected recipe already appears in `radio_phy.c` during RX-path
configuration and AGC finalization, and in the separate source-backed
`r3_rx_path_configure`/AGC implementation:

1. Write PHY `0xa00 = 0`.
2. Clear PHY `0x440` bit 0 (deaf).
3. Set PHY `0x44a` bits `0x44`.
4. Write PHY `0x44a = 0x80`.
5. Write PHY `0xa00 = 0xff`.
6. Set PHY `0x440` bit 0 (undeaf).

This is repository-grounded startup prior art, **not prior qualification
as a live RXE timeout remedy**. The earlier authentication investigation
also tried a PHY reset without per-execution instrumentation and did not
establish a reliable fix. The current test instruments actual execution.

`CLEAN_RX_HT_REARM_RESET` defaults off and requires one-shot strict passive
recovery. The first busy decision remains immutable at SHM `0x7c0..0x7cf`.
After the existing selective event drain, the PSM writes a request at
`0x7d0` and enters a bounded 65,535-iteration reply loop. C3 services the
request on every RX poll, pauses the PSM without restarting it, rechecks
the request, and verifies TX is disabled and the four existing PHY TX
override controls remain inhibited. It permits at most one reset execution.
It applies the six operations and validates final PHY `0x440` bit 0,
`0x44a = 0x80`, `0xa00 = 0xff`, and unchanged inhibition before acknowledging.
All register operations are fixed-length, without unbounded I/O polling.

The PSM then retries its original bounded RXE flush. A busy retry still
faults with reason 3; C3 rejection/readback failure faults with reason 8;
a missing reply faults with reason 7. The reply check precedes the final
zero-count check, so a reply at the last modeled opportunity is accepted.
There is no core or PSM restart, counter clearing, TX or normal RX change.
The normal diagnostic restart-attempt counter must remain zero.

ABI19/WND8 retains 35 160-byte samples with a 128-byte header. Modes 5/7
identify natural/injected PHY reset; 0/1/3 retain previous meanings.
SHM `0x7d0..0x7df` is copied to header bytes 96..127, below the RX-header
and C3 capture-storage boundaries:

| Word | Reset record |
| --- | --- |
| 0 | State: 0 unused, 1 request, 2 complete, 3 rejection/readback failure, 4 reply timeout, `0xffff` invalid cached copy |
| 1 | Actual C3 execution count, at most one |
| 2..3 | Original MACCONTROL low/high |
| 4..6 | PHY `0x440/0x44a/0xa00` before |
| 7..9 | The same registers after |
| 10..11 | Before/after four-control TX-inhibit masks (15 means all four inhibited) |
| 12..13 | Elapsed C3 service TSF low/high |
| 14..15 | RXCTL before/after PHY reset, before retried flush |

The final sample word now holds the 16-bit first-fault commit and 16-bit
reset state separately. A completed PHY reset is not itself proof that
RXE flush recovers or that later frames are received; both are checked
independently. Injected mode still modifies only a latched decision, not
physical hardware RXCTL.

Native execution tests cover the emitted handshake instructions, successful
and persistently busy retry, rejected/missing replies and the exact final
reply opportunity/timeout boundary. The six-operation recipe is checked
against the startup sequence and modeled over varied initial values,
preserving unrelated PHY bits and TX overrides. Protocol tests cover mode
and reset-state encodings, disabled payloads, cached invalidation markers
and capacity. Both experimental targets retain the framebuffer-free screen.

#### Instrumented injected execution

Fresh whole-flash originals in
`build/clean-rx/phy-reset-investigation/original-{A,B}.bin` are exactly
16777216 bytes each and byte/hash-match the preceding independently
verified readbacks before any experimental load.

Four fixed injected trials (`aad318de`, mode 7), at 256/512 bytes in both
roles, execute exactly one C3 reset each. All four report state 2, execution
count 1, before/after TX-inhibit masks 15, and valid final PHY readbacks.
The before and after values are `0x440 = 0x3e01`, `0x44a = 0x80` and
`0xa00 = 0xff`; intermediate reset writes are covered by the native recipe
test, while the execution count distinguishes running it from merely
observing unchanged final values. Service takes 16 TSF units in each trial.
The later PSM decision succeeds at RXCTL `0x800`.

| Trial | First to last sampled captures | Final HT starts |
| --- | --- | --- |
| `injected256-role0-r0` | 0 to 274 | 5 |
| `injected256-role1-r0` | 2 to 214 | 2 |
| `injected512-role0-r0` | 1 to 597 | 3 |
| `injected512-role1-r0` | 0 to 345 | 1 |

All four preserve the first forced fault, have no controller error or PSM
restart, and reconcile frozen delivery accounting. None qualifies an early
body. This verifies actual live reset execution and subsequent receive
progress from an injected decision, not recovery from physical busy RXE.

#### Fixed natural controls and a real recovered timeout

The fixed matrix adds four 512-byte event-drain/reflush controls
(`9fd0f901`, mode 1) followed by ten natural PHY-reset trials
(`0157638f`, mode 5): one 256-byte trial per role, then four 512-byte
trials per role. No RF trial is retried until success.

Both role-0 reflush controls finish without controller fault but retain
zero HT starts (346 and 590 captures). Both role-1 controls activate on a
real timeout, exhaust the retry and stop after one capture, preserving
RXCTL `0x804`, zero wait remaining and SHM reason 3. Their first RXE
counts are 980/924, status1 `0x4000` and status2 `0x4140`.

Three of the ten natural PHY-reset trials activate on a real exhausted
wait. Each executes exactly one reset with valid readbacks and inhibited
TX, preserving both original and subsequent PSM decisions:

| Trial | First fault RXE count / status1 / status2 | RXCTL before/after PHY reset | Result |
| --- | --- | --- | --- |
| `reset512-role1-r0` | 980 / `0x4000` / `0x4140` | `0x804` / `0x804` | Retry exhausts; explicit reason-3 fault after one capture |
| `reset512-role1-r1` | 1301 / `0x5800` / `0x0040` | `0x804` / `0x800` | Strict retry succeeds; reception continues |
| `reset512-role1-r2` | 980 / `0x4000` / `0x4140` | `0x804` / `0x804` | Retry exhausts; explicit reason-3 fault after one capture |

The middle trial is a **real, instrumented recovered busy timeout**, not
an injected decision or an automatic restart. Its first-fault record has
zero wait remaining, outcome 2, RX epoch 330 and bad-FCS count 4.
C3 executes the reset in 17 TSF units and directly observes RXCTL change
from `0x804` to `0x800`, before reissuing the original flush. The final
main record is committed sequence 962, outcome 1, RXCTL `0x800`, RX epoch
355 and unchanged bad-FCS count 4.

The receive timeline independently brackets the activation:

| Sample | Calibrated elapsed us | Captured | First-fault commit | PHY reset state |
| --- | ---: | ---: | ---: | ---: |
| 24 | 2401564 | 323 | 0 | 0 |
| 25 | 2501582 | 327 | 2 | 2 |
| 26 | 2601589 | 330 | 2 | 2 |
| 30 | 2987803 | 350 | 2 | 2 |

These four samples have coherent cached publication, direct controller
state 9/error 0, and zero recorded PSM restarts. Frozen accounting retains
350 captures = 343 posted/consumed + 7 explicit drops, with no pending
frame. The increasing post-activation counts prove receive progress;
they do not prove HT reception: this trial has zero HT starts throughout.
This single episode tests the combined pause/reset/reflush sequence.
It does not isolate an individual reset write from PSM pause or the
additional delay; no time-matched parked/no-reset control is claimed.

The two failures have the earlier startup-like `0x4000/0x4140` status
pair and are already faulted at the first timeline sample. Their PHY
reset operations actually run and pass readback checks, but RXCTL remains
busy and the second wait really exhausts. The reset is therefore **not a
general or reliable remedy**. The differing status/count/timing groups
are evidence for separate failure states, not sufficient documentation
to assign undocumented RXE bit meanings or a unique root cause.
The next hardware investigation should distinguish this persistent
startup-like RXE state from the recoverable midstream state, retaining
pre/post reset evidence instead of increasing identical retry counts.

#### Why 554-byte repeatability remains open

No new early body qualifies in this fixed matrix. Fourteen completed
frozen exports retain 28 valid HT headers, including seven A-MPDU-format
headers. Their exclusions are explicit:

- 21 are not A-MPDU format.
- Six end as terminal single bodies.
- One has an early single-body good hint.

The seven A-MPDU-format PSDUs are 302 or 558 bytes: they describe only one
body, not the earlier qualifying 1118-byte multi-body PSDU. All fourteen
completed vendor trials retain Block ACK activity, but that alone is not
proof of multiple bodies in any captured PPDU. Thus this campaign has no
observed qualifying multi-body opportunity to turn into a repeated
554-byte early proof. It neither retracts the preceding two independently
verified 554-byte bodies nor counts these single-body frames as repeats.
A reliably confirmed multi-body stimulus remains necessary.

Three otherwise completed trials see no HT starts despite continuing
ordinary capture and successful rearm decisions (the two mode-1 role-0
controls and the recovered mode-5 role-1 trial). HT-only loss remains
separate from persistent RXE timeout.

#### Validation and preservation

All nine reference profiles and the ordinary controller pass builds and
native/ASan/UBSan tests. All seven pre-existing controller profiles and
the ordinary controller remain byte-identical to the preceding verified
images; only the newly gated reset profiles change C3/PSM firmware.
Final wrapper identities and footprints are in the reference README.

All 108 raw phase reports from 18 fixed RF trials independently pass
length and bitwise CRC checks. All 554 WND8 samples are retained with
zero capacity loss (309 coherent cached publications, 245 explicitly
incoherent). Four genuine faulted captures remain errors; the fourteen
successful frozen exports reconcile 5822 captured frames, including
163 explicit drops. No qualification failure is hidden or replaced by
a successful-looking empty export.

Final restoration initially fails its bounded readiness check before
any flash write because Google Chrome again exclusively owns badge A's
BOOTSEL USB interface. IORegistry identifies the owner; this is not a
radio failure. The user closes the browser windows, then restoration
resumes using the unchanged fresh original images.

Both entire original flashes are now restored. Independent readbacks
`build/clean-rx/phy-reset-investigation/restored-{A,B}-full.bin` each contain
exactly 16777216 bytes and byte-compare with the fresh campaign originals.
Their SHA256 values match the verified originals:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

Both complete load/verify operations and independent full readbacks
succeed. Serial-targeted final checks confirm both badges in BOOTSEL.
The reset remains default-disabled; one recovered episode does not
qualify general RXE recovery, HT reliability, repeated 554-byte capture,
normal HT advertisement or D2 completion.

### Fixed-stimulus early-capture qualification (2026-10-19)

**Result: complete 554-byte nonterminal early bodies now repeat in both
roles with independently checked bytes and multi-body geometry. General
receive reliability and physical busy recovery remain unqualified.**

Evidence, matching UF2s/controllers/collectors, failed trials and the
independent bitwise-CRC/fixture audit are retained under
`build/clean-rx/early-reliability/`. `audit.js` regenerates the fixed source
without calling firmware helpers; `audit-final.jsonl` contains each result.
Role 0 observes on A; role 1 observes on B.

#### Deterministic source and evidence

The vendor's focused submissions still take roughly 1.8..2.7 ms per
512-byte Ethernet frame. An aggregation request and received BA count
cannot establish a multi-body PPDU. The new separate clean source sends
eight cycles of:

1. One 554-byte legacy OFDM control at 6 Mbps.
2. One 554-byte nonaggregated HT20/MCS0 control.
3. One HT20/MCS0 aggregate containing two complete 554-byte MPDUs.

Forty calibrated milliseconds separate transmissions. The aggregate PSDU
is 1118 bytes: delimiter + 554-byte body + two padding bytes + delimiter +
554-byte terminal body. Source addresses, sequence numbers, payload nonce,
deterministic bytes, delimiter CRCs, HT-SIG CRC and both MPDU FCS values
are checked. Frames are broadcast-addressed, unprotected raw PHY fixtures,
not a negotiated unicast A-MPDU exchange; this does not qualify protected
AP traffic or either body's general delivery.

`CLEAN_RX_HT_STIMULUS` defaults off and requires HT data/extended RX.
Command-12 version 6 admits only this exact fixture with one attempt, no
extra flags and no station mode. Native tests cover MCS0..7; RF uses MCS0.
The existing finite aggregate and new source share HT-SIG construction.
The source wrapper runs a separate clean controller, not vendor firmware,
and does not read or provision credentials. The observer stays TX-inhibited
and passive. Neither default station behavior nor HT advertisement changes.

Every source attempt exports its command, raw host ticks, TSF interval,
logical length, descriptor CRC, PLCP, sampled TXE controls, three actual
completion conditions and zero PHY error. Staging/template/control checks
precede transmission; the final complete aggregate descriptor is retained.
These records establish checked source execution, **not independent OTA
receipt for every attempt**. Actual multi-body reception is established by
the separate badge's matching HT-SIG/1118-byte geometry and a complete,
nonce-bound, FCS-valid, byte-for-byte expected nonterminal body.
There is no third receiver or independently measured per-attempt RF loss
denominator. A second complete body is not claimed.

The observer validates every delivered control's complete bytes and
passive metadata, retains a cumulative index bitmap and duplicate/invalid
counts, and separately saves a complete legacy wrapper/body. The collector
checks that saved body independently and correlates single-HT prefix
bytes with scheduled indices. Prefix-log overflow disqualifies absence
inferences. The stronger diagnostic pass requires all sixteen controls
and one exact early body; this conservative pass gate is not a general
zero-loss RF specification.

#### Fixed matrix and retained failures

| Corrected fixed-source profile | Trials per role | Exact early body, both roles | All-control pass |
| --- | ---: | ---: | ---: |
| Strict rearm, first matrix | 4 | 8/8 | 4/8 |
| Natural PHY-reset gate | 4 | 8/8 | 4/8 |
| Injected PHY-reset branch | 2 | 4/4 | 3/4 |
| Injected park-only branch | 1 | 2/2 | 1/2 |
| Final strict/reporting check | 1 | 2/2 | 1/2 |
| **Total** | **12** | **24/24** | **13/24** |

Both badges therefore retain an exact early body in 12/12 corrected
fixed-source boots. One body is retained per boot; this is not 192/192
aggregate-body retention or a per-frame capture probability.
The stricter all-control passes are 3/12 observing on A and 10/12 observing
on B. That finite asymmetry does not establish a defective badge.

Ten earlier fixed trials remain explicit instrumentation failures:
two pilot collectors wrongly required sampled TXE `0x56a` to equal the
logical template length; eight subsequent trials wrongly required the
ordinary negotiated HT metadata in the deliberately passive observer.
The final collector retains `0x56a` raw without assigning undocumented
length semantics, and validates the actual passive metadata explicitly.
All ten nevertheless contain independently valid 554-byte early bodies.
They are excluded from the corrected reliability matrix, not relabelled
as passing runs or silently discarded.

#### Loss isolation

Across the 24 corrected trials, complete control delivery is 180/192
legacy and 181/192 single HT, with no corrupt or duplicate control
accepted. These are device-validated full-frame bitmaps, not independent
host captures of all 384 frames; the saved complete bodies and native
validators provide separate checks.

Ten missing single-HT controls also lack their expected HT prefix in a
nonoverflowed log. One has a matching prefix but no complete F2 delivery:

- `strict-v3-role1-r0`: control 19 has no HT prefix; all eight legacy
  controls arrive, including neighboring indices 18 and 21. There is no
  exhausted-rearm fault. This isolates a selective pre-prefix miss from
  the old globally stuck `0x804` condition and downstream USB delivery.
  It does not identify RF interference, PHY detection or pre-log receive
  ownership as the unique physical cause.
- `reset-role0-r0`: control 16 reaches prefix 10/epoch 38 with complete
  560-byte RXE length and terminal good outcome 1, but not the control
  delivery bitmap. Frozen accounting is captures 47 = posted 45 + dropped
  2, consumed 45, pending zero. This places that miss after prefix/good
  capture and before host delivery; it does not uniquely assign either
  aggregate drop to that specific frame. Controls 13 and 22 separately
  lack prefixes. No PHY recovery activates in this trial.

The live RP2350 fixture comparator adds host-side CPU work. These diagnostic
loss counts must not be extrapolated to ordinary station reception;
instrumentation overhead remains a candidate for post-capture losses.

Thus not every HT-only miss is an RXE flush timeout, and not every missing
delivered control is a failed HT PHY decode. Extending the flush wait or
repeating the PHY reset cannot, by itself, explain both classes.

#### Bounded reset and park-only controls

All four injected PHY controls execute exactly one checked reset, retain
TX-inhibit masks 15/15 and subsequently capture the exact HT early body.
The early epochs are later than the preserved injected first fault;
PSM restart attempts remain zero. This proves reception after actual
reset execution, not recovery from a natural physical busy state.

`CLEAN_RX_HT_REARM_PARK` adds a separate default-disabled no-reset
control using the same handoff, TX guards and strict retry. It performs
no PHY-reset writes, waits at least 16 TSF units with a 65,535-poll bound,
and requires unchanged PHY readbacks. The two injected park trials record
completion state 2, actual reset count zero, guards 15/15, and a later
exact early body in both roles. Full service takes 29 TSF units, compared
with 16 for injected reset: **not an exactly time-matched control**.
Observer ABI20/WND9 adds modes 13/15; matching archived ABI19/WND8
collectors remain necessary for earlier trials.

A separately fixed vendor/AP matrix has eight natural reset trials and
four natural park-only trials:

- Three real reset activations (`vendor-reset-role1-r0/r2/r3`) exhaust
  their first waits at RXCTL `0x804`, RXE 924/982/986 and raw status
  `0x4000/0x4140`. A reset executes once, readbacks/guards pass, but RXCTL
  stays `0x804` and the bounded retry fails with reason 3.
- One real park activation (`vendor-park-role1-r0`) has RXE 978 and the
  same raw status group. Actual reset count stays zero, complete service
  takes 30 TSF units, and the retry likewise faults. These raw groups are
  not assigned undocumented hardware meanings.
- The remaining eight do not activate recovery. One,
  `vendor-park-role0-r1`, independently captures another protected
  554-byte vendor early body: prefix 3, epoch 266, PSDU 1118, valid FCS,
  fresh nonce MAC, matching HT-SIG/CRC and nonterminal geometry.
  It is not evidence of park recovery, which never ran.

No new naturally busy state recovers. The earlier single midstream
recovery remains valid evidence for that episode only. Persistent
startup-like busy handling still needs a hardware-grounded RXE-specific
solution; enabling repeated resets or PSM restarts would hide the failure.

#### Audit, validation and preservation

All 46 actual trials, including instrumentation failures, are retained.
The independent audit checks 276 nonce/build-bound report CRCs and 881
window rows with no window-capacity loss. Forty-two successful frozen
exports reconcile 5643 captures, including 130 explicit drops; the four
genuine faulted captures remain errors.

Native ASan/UBSan tests cover all source rates/indices, exact delimiters,
padding, HT-SIG, FCS, disabled/station gates, every command bit and every
byte mutation of a representative aggregate, passive control metadata,
corrupt control/prefix rejection, and sender evidence thresholds.
PSM reply success/rejection/absence/boundary and strict retry tests remain
enabled. Builds, unresolved-symbol checks and native tests pass for the
ordinary, window, reset, park and fixed-source profiles. The ordinary
controller is byte-identical to the preceding verified default image.

The final strict observer and fixed-transmitter controller binaries also
byte-compare with their archived RF-tested images; the final natural-reset
controller matches its RF-tested image.

The original full flashes were saved fresh before testing. Both complete
originals are now restored and independently reread:
`restored-A-full.bin` and `restored-B-full.bin` each have 16777216 bytes,
byte-compare with the fresh originals, and have SHA256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

Both complete load/verify operations succeed, and final serial-targeted
checks confirm both badges in BOOTSEL.

The first D2 work package stays open for persistent busy recovery and
remaining pre-prefix/post-capture losses. Repeating the 554-byte body and
providing confirmed multi-body stimulus are now completed foundations.

### Passive observer FIFO qualification (2026-10-19)

Evidence: `build/clean-rx/observer-fifo/`. The independent `audit.js`
checks all 28 RF trials / 168 raw reports, source fixture CRCs and saved
descriptor bytes, exact early MPDU/SIG/epoch ownership, frozen transport
counts and final full-queue stop retention. `audit.jsonl` retains per-trial
failures and cohort totals. Seven additional BOOTSEL-readiness failures
never reached RF and remain in their readiness logs; macOS USB ownership
was released by the user, not by terminating applications.

#### Ownership fix

The passive observer previously released a completed capture even if F2
posting returned zero for unavailable credit or DMA descriptors. Its
saturating drop counter recorded that loss, but no controller FIFO held
the frame. The station path already had an eight-slot SRAM FIFO.

`CLEAN_RX_HT_OBSERVER_FIFO=1` now reuses that FIFO. It defaults off and
requires the passive observer. When full, the PSM's completed SHM capture
remains held instead of being overwritten. A gated 16-byte side array
latches each slot's prefix correlation before releasing SHM; delayed F2
publication no longer reads the live next-frame correlation. Ordinary
station accounting is not applied to these passive frames.

Stop freezes MAC/PSM reception, copies a completed held-SHM frame when
space becomes available, and drains all queued captures. It does not
restart the PSM. The RP continues receiving while waiting for the stopped
controller and consumes the last posted packet before saving diagnostics.
FIFO reports require exact `captured == posted == consumed`, with no
dropped frames or outstanding packets. Controller draining has both
500000-microsecond and 1000000-poll limits; the host has independent raw
timer/poll limits. Invalid occupancy and exhausted bounds fault explicitly.
An unfinished PPDU can still be aborted at stop; this is not a promise to
finish an arbitrary in-progress radio reception.

#### RF matrix and retained failures

All source runs attempted the same 24 one-shot channel-6 transmissions:
eight each of legacy OFDM, single HT20/MCS0 and exact two-by-554-byte
aggregates. No vendor image, association or credential provisioning was
used. Both badge roles were exercised.

| Cohort | RF trials | Controller captures | Exact 554-byte early body | All 16 controls |
| --- | ---: | ---: | ---: | ---: |
| Initial FIFO, four trials per role | 8 | 484 | 8/8 | 4/8 |
| Timed host pause, two per role | 4 | 176 | 4/4 | 0/4 |
| Initial stop while full, two per role | 4 | 58 | 4/4 | 0/4 |
| Stale-profile trials, excluded from normal qualification | 4 | 105 | 4/4 | 0/4 |
| Final flag-bound normal FIFO, two per role | 4 | 264 | 4/4 | 0/4 |
| Final stop including held SHM, two per role | 4 | 68 | 3/4 | 0/4 |

Every one of the **1155 completed controller captures** was posted and
consumed, with zero transport drops, duplicate controls, corrupt accepted
controls or final pending packets. No strict first fault or PSM restart
was recorded. These totals include failed trials; they do not establish
that every transmitted RF frame reached controller capture.

Normal FIFO trials repeat exact nonterminal early bodies in **12/12
boots, six per role**, while the stricter all-controls gate passes only
4/12. They receive 88/96 legacy and 87/96 single-HT controls. Seven missing
single-HT controls have no logged matching prefix. Two more match a logged
prefix but exit with bad FCS and short RXE lengths:

- `verified-fifo-role0-r0`, control 13: prefix 7, epoch 41,
  outcome 2, RXE length 51 instead of 560.
- `verified-fifo-role1-r0`, control 13: prefix 7, epoch 56,
  outcome 2, RXE length 83 instead of 560.

Those two are **pre-capture radio exits**, not losses of completed
controller frames on F2. Matching HT-SIG and saved body-prefix bytes bind
them to the controlled source. The evidence does not distinguish
interference, PHY truncation or an internal receive-abort cause, and no
undocumented status-bit meaning or reset mask is inferred.

The timed-pause profile withholds host frame polling for 600000
SOF-calibrated microseconds after control 1. All four trials observe all
eight SRAM slots occupied, one F2 credit outstanding, and cached PSM state
14 holding the next frame. After host polling resumes, every completed
controller capture drains. The intentional RF misses remain strict-gate
failures rather than being relabelled as a full RF success.

The full-stop profile withholds polling until stop. In the four final
trials, the last pre-stop timeline counts are 16, 14, 20 and 14; frozen
counts become 17, 15, 21 and 15. Each conserves the additional completed
held-SHM frame, not just the eight SRAM slots. All four pass the explicit
full-queue ownership proof. The last trial misses every complete early
aggregate body, which is retained as a failure rather than omitted.

#### Build-profile correction and validation

Four trials labelled `final-fifo-*` unexpectedly reused the preceding
stop-test image. Their source flags changed only host code, leaving the
controller bytes and old build-ID inputs unchanged. Build-ID-dependent
RP objects and collector were consequently stale. Three actually ran
full-stop pauses; the fourth never triggered its pause. These trials
remain available but are excluded from normal-FIFO qualification.

Build IDs now include `REF_FLAGS` and `REF_STIMULUS`. A regression switches
normal FIFO to full-stop FIFO and verifies different build IDs and RP
observer objects while the C3 controller remains byte-identical to the
final RF-tested ownership code. Both profiles run native ASan/UBSan
tests. Existing non-FIFO frozen-report rules remain tested, while FIFO
tests additionally reject dropped or unconsumed captures. Queue tests
cover immutable IDs across 16/32-bit counter wrap, full occupancy,
one-credit draining, invalid occupancy, exact deadline boundaries and a
nonadvancing clock bounded by polls.

An initial pause build exposed an unwanted `__aeabi_uldivmod` dependency.
The calibrated pause threshold now uses bounded 32-bit arithmetic; final
RP ELFs pass the existing unresolved-symbol checks and remain nolibc.
The ordinary controller passes native tests and remains byte-identical
to `phy-reset-investigation/verified-default/cyw43_rx_fw.bin`.

Fresh full originals were saved before experimental flashing. The final
restoration/readback evidence is `final-restore-{A,B}.log`,
`final-readback-{A,B}.log`, `final-restored-{A,B}-full.bin` and
`final-bootsel-{A,B}.log`. Both complete 16-MiB readbacks byte-compare
with the fresh originals and retain the original SHA256 values recorded
above. Both final serial-targeted checks confirm BOOTSEL.

This completes a bounded passive-transport foundation, not D2 work
package 1 or 3. Persistent RXE busy recovery, pre-prefix and truncated-body
losses, repeated variable-length aggregate reception, aggregate handoff
negative cases and mandatory station MAC integration remain unqualified.
HT association advertisement stays disabled.

### Controlled handoff stimulus and reproducible continuation failure

This campaign implements a deterministic source for the existing bounded
170/298-byte next-body diagnostic, not a generalized receive engine or HT
station. The previous historical vendor pair captures remain valid;
repeatability, malformed-input recovery and negotiated operation are still
open. The current campaign does not reproduce a complete valid pair.

#### Implemented source and evidence guards

`CLEAN_RX_HT_HANDOFF_STIMULUS`, default zero, enables exact command-12
version 7. It requires HT data and extended RX, rejects station mode and
extra flags, and regenerates and compares every one of the 1132 staged
bytes. Construction reuses the existing fixed-source scratch buffer.
The 478-byte physical PPDU contains delimiter + padded first body +
delimiter + second body, in 170/298 or 298/170 order. The logical
descriptor is 492 bytes; the source stages/readback-checks the zero tail
as well. HT-SIG always describes the actual finite PPDU.

The source interleaves eight legacy/single-HT/aggregate cycles, with
calibrated 40-ms gaps. Cases are good, bad first FCS, bad last FCS,
bad second delimiter CRC and a second delimiter claiming eight bytes
beyond the physical remainder. Only index 2 is faulted; subsequent
aggregates remain good. The malformed case does not transmit an
incorrect HT-SIG or deliberately underrun the TX engine.

The new `reference-handoff` profile wires source selection through RP
staging, controller admission, sender records, passive observer byte
validation, frozen sample retention, collector and runner. Use the
[profile instructions](../vendor-burst/README.md#controlled-170298-byte-handoff)
and [API contract](../doc/HT20.md#finite-aggregate-diagnostic).
The observer reuses the FIFO and existing one-shot advance; no receive
register/reset experiment or per-frame TFT operation was added.

An initial collector weakness could count a generic second bad-FCS
outcome plus recovery controls as a bad-last success, even though good
controls also failed there. The corrected guard requires complete exact
bad-second bytes, invalid FCS and matching early/second owner and epoch.
The native tests exercise acceptance and ownership, length, terminal,
body and FCS mutations in both orders. Old apparent bad-last passes are
retained as insufficient instrumentation, not negative qualification.
Bad-first qualification still requires a later complete valid pair.
Bad-delimiter/truncated-body cases intentionally cannot pass negative
qualification until their complete expected behavior is established.

Native tests cover both orders, five cases, eight MCS and all 24 indices,
including delimiter/FCS geometry, zero tails, source/receive validation,
disabled/station/command-bit rejection and every staging-byte mutation
of representative aggregate descriptors. Controller and protocol native
tests run with ASan/UBSan. The normal controller remains byte-identical
to the preceding verified default. The old fixed-source FIFO profile
also builds/tests and its controller remains byte-identical to the
previous verified FIFO image. Matching final handoff images/collector
were rebuilt and byte-compared to their RF archives.

#### Retained hardware campaign

Evidence is under `build/clean-rx/controlled-handoff/`: raw reports,
JSONL, stderr, loader logs, archived source/observer images and collectors,
fresh full originals, restore readbacks, and independent `audit.js` /
`audit.jsonl`. Role 0 is B source/A observer; role 1 reverses them.
All controlled-source RF trials use MCS0; eight-rate native coverage is
not an eight-rate RF qualification.

| Cohort | Trials | Completed captures | Exact early bodies | Advances | Bad/abort continuations | All controls received |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial broadcast fixtures | 4 | 239 | 4 | 4 | 4 | 3 |
| Unicast pilots | 12 | 817 | 12 | 10 | 10 | 4 |
| Strict five-case/two-order/two-role matrix | 20 | 1405 | 20 | 16 | 16 | 7 |
| Distinct-prefix unicast follow-up | 4 | 296 | 4 | 4 | 4 | 2 |
| Vendor/AP controls, mixed sizes | 4 | 1304 | 1 | 1 | 1 | Not comparable |
| Total | 44 | 4061 | 41 | 35 | 35 | |

The independent audit verifies all 176 raw report headers/build IDs/
session nonces, all 960 controlled-source transmission records against
an independently reconstructed fixture, the final staged descriptors,
complete early bytes/FCS and fresh owner/epoch correlation. All 4061
completed controller captures were posted and consumed, with zero F2
drops. Every populated retained sample has matching stored/computed FCS,
and none is tagged as advanced. All 35 continuation snapshots match their early owner/epoch
and have outcome 2 (bad/abort); no exact valid second body or exact
intentionally bad last body is established.

This conservation result is not zero RF loss: only 7/20 strict-matrix
trials received every interleaved control, and only 12/20 received the
selected recovery controls. All 40 controlled-source trials retain an
exact first body, but the diagnostic retains one early body per boot;
that is not evidence of receiving every aggregate or every first body.
The four vendor controls use the existing independent vendor/AP path
with a byte-identical C3 observer; one reproduces the bad continuation.
They weaken a clean-transmitter-only explanation but do not independently
prove that either source's complete second body was correct over the air.

#### Fresh prefix discriminator and narrowed boundary

In all 30 original controlled-source advances, exactly the first eight
snapshot bytes matched the expected second body, then diverged. Those
eight bytes were also identical in the first body, so they could not
prove that a new second prefix had arrived. To remove that ambiguity,
the final source sets the legal More Data bit in the first FromDS frame
and clears it in the second. Addresses and TID remain unchanged; this
does not invent fragments, protected frames or a new receive policy.

All four follow-ups, covering both roles/orders, retain first FC
`88 22`, then a fresh second FC `88 02` and the correct remaining
eight-byte prefix. Thus the second snapshot is not just the unchanged
first header. In every case the first mismatch is byte 8; second RXE
counts are 304/306 or 176/178, versus expected 304 or 176 including
the six-byte receive header. The outcome remains bad/abort and the
remaining bytes do not establish the expected MPDU.

This narrows the observed boundary to reception/copy/terminal handling
after a fresh next-body prefix, rather than missing the entire aggregate,
failure to take the advance, host credit loss or stale early ownership.
It does **not** prove that the radio payload physically corrupts at byte 8:
an invalid/aborted snapshot need not contain a completed hardware copy.
It also rules out treating an isolated final-FCS overwrite as sufficient
explanation for the observed non-FCS byte mismatches.

The actual PSM image was decoded through copy setup, body loop and
GoodFCS completion. Event-drain instructions at PCs 163/164 are
conditional; their fall-through is already represented by existing
event-aware native tests. No new event-semantics defect was demonstrated,
so no speculative receive-loop patch, delay, undocumented TX control
or stronger reset was introduced. The next discriminator is the
next-body copy-engine progress/terminal transition, or an independent
complete on-air second-body capture, not more identical blind repeats.
Correct live multi-body handling remains the blocker for work package 2.
HT association, BA lifecycle/Immediate BA and protection gates remain off.

The final marked-prefix build is `8cdf0e3a`. Its 60004-byte observer
controller has SHA256
`c070b8ad26798a9996be960fe0d1ee0e9d21d388cd939cd1a274ce0958d7daec`;
the identical complete binary is embedded in the archived vendor/AP
observer UF2. Fresh 16-MiB originals were saved before experiments.
After the last marked-prefix trial, both entire originals were restored,
independently saved as `restored-final-{A,B}-full.bin`, byte-compared
and SHA256-verified. Both were left in BOOTSEL; final info logs are
`bootsel-final-{A,B}.log`.

| Badge | Original and final full-readback SHA256 |
| --- | --- |
| A | `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628` |
| B | `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2` |

### Second-body overwrite probe and rejected fresh-count wait

This follow-up distinguishes a stalled live-SHM copy from a changing
diagnostic snapshot, then tests the specific stale-count rearm hypothesis.
It does not repair or qualify the second-body receive path.

#### Bounded implementation and fault evidence

The default-disabled `CLEAN_RX_HT_COPY_PROBE` gate requires passive early
retention and one-shot advance. After the complete first body is saved,
it writes and reads back four `0xa55a` sentinel words in the live receive
buffer at MPDU offsets 8, 32, 64 and 128. The original EOI/base/end/copy
sequence then runs. At the continuation terminal entry, a separate
snapshot records 12 existing RXE/TSF registers, the sentinel locations and
24 prefix bytes, before the existing complete continuation snapshot.
Owner/epoch, seed readback and commit phase are checked; no new register
mask, radio reset or source payload is introduced. The probe adds
pre-EOI instructions and changes live first-body bytes after retention,
so it is a controlled intervention, not passive timing qualification.

The dependent `CLEAN_RX_HT_COPY_WAIT` experiment tests whether arming
against the previous body's count causes the failure. It acknowledges
EOI, then waits for a latched count in `[6, first_count)`, checking
EOF/abort and a 65,535-decrement budget before original rearm. Failure
records phase 5/reason 9 and takes the existing PSM fault path; C3 reports
error `0x88` instead of automatically restarting. This gate defaults off
and is deliberately **not** promoted as a fix.

The first four wait trials exposed a diagnostic gap: the RP powered off
after a controller error without exporting stopped SHM. They remain
retained but cannot establish the exact fault state. The copy-probe
error path now pauses PSM and uses an isolated diagnostic loader to read
SHM/mailbox evidence before power-off. It never clears the original
loader/receive failure. Separate collection success/error and reason
fields remain available even though the requested phase still fails.
Two further trials verify the collector's structured `handoff-copy-fault`
output against the raw retained snapshot. Error snapshots are not
success-shaped reports or normal stop/drain evidence.

The probe fits unused SHM words `0x5ca..0x5f3` already included in the
existing 512-word export. The RP-only fault metadata occupies trace bytes
5792..5823. No normal capture/F2 layout is expanded.
See [profiles and field layout](../vendor-burst/README.md#copy-progress-probe-and-rejected-wait-control).

#### Hardware comparisons and independent audit

All evidence is under `build/clean-rx/copy-progress/`, with fresh full
originals, archived matching images/collectors, source records, raw
reports/JSONL, failed trials, build logs and `audit.js` / `audit.jsonl`.
Source B/observer A is role 0; role 1 reverses them. All RF uses MCS0
and both 170/298-byte orders.

| Cohort | Trials | Completed captures | Result |
| --- | ---: | ---: | --- |
| Ordinary handoff baseline | 4 | 167 | Fresh prefix; bad continuation |
| Initial sentinel probe | 4 | 390 | All sentinel locations overwritten; bad continuation |
| Repeated good/bad-last probe | 8 | 361 | Same overwrite result; no exact good or injected-bad second body |
| Initial wait faults, before SHM export fix | 4 | Not qualified | Original controller failure retained; detailed fault unavailable |
| Wait faults with stopped SHM export | 4 | 9, not drain-qualified | EOF with unchanged first count, reason 9 |
| Structured fault-output verification | 2 | 3, not drain-qualified | Same failure; raw and JSON fields agree |

The independent audit validates 104 raw report headers/build/session
bindings and 624 source records against independently constructed
fixtures, including the final full staged descriptor per run. Every
source-controller binary is byte-identical across these experiments.
The baseline observer controller is byte-identical to the previous
marked-prefix campaign. Normal and final probe controller binaries
are compared against their RF archives after rebuilding.

All 918 completed captures in the 16 normally stopped trials were
posted and consumed, with zero F2 drops. Each of those trials has an
exact first body and a fresh second eight-byte prefix, but no valid
pair. Only 6/16 received every interleaved control: RF losses remain.
The 12 completed captures reported in stopped-fault snapshots are kept
separate: a controller fault prevents normal drain qualification.
The four original unexported fault trials are not assigned capture or
mechanism results merely from their sender completion.

Across all 12 sentinel trials, all four seeds first read back correctly
and all four terminal locations differed from the seeds. Every terminal
location agrees with the later full continuation snapshot, and all
24 independently sampled prefix bytes are stable between snapshots.
The expected body nevertheless fails after the fresh header, and
bad-last trials do not retain the exact intentionally corrupted body.
This excludes a wholly untouched receive buffer and weakens an
unstable diagnostic-copy explanation. It does not establish that every
byte was correctly copied or identify where physical corruption begins.

All six exported wait faults retain the first count (176 or 304), with
EOF status and nonzero remaining budget. EOF arrives after roughly
175..340 TSF units; this is not a countdown timeout. The hypothesized
fresh-count wait therefore prevents the original continuation and
cannot be used as a repair. A pre-arm count is not an independently
advancing freshness signal in this experiment. No undocumented
reset, delay or event-clear sequence was substituted.

#### Validation and remaining boundary

Native ASan/UBSan tests cover both size orders, exact sentinel placement
and readback, first-body retention, terminal register/data snapshots,
unchanged owner/epoch, immutable records across new RX, bounds and no TX.
Wait tests model a fresh count, stale-count exhaustion, EOF and abort;
faults must take the explicit bounded failure path without arming.
Protocol tests reject missing owners, wrong epochs/versions/phases,
seed corruption and invalid terminal count geometry.
The ordinary handoff and production-default controller binaries remain
byte-identical to their verified predecessors. All builds remain
freestanding/nolibc; a compiler-generated struct-copy helper was replaced
with the existing byte-copy helper rather than adding a runtime dependency.

The current boundary is an active overwrite path yielding stable but
incorrect continuation bytes, not missing host credit, wholly stalled
copy or merely a moving terminal snapshot. Next work needs a
discriminator for receive/FIFO boundary/length handling versus incorrect
on-air second bytes; independent complete RF evidence remains valuable.
The wait experiment is a rejected hypothesis, not general recovery.
Work package 2, negotiated aggregation, Immediate BA and HT association
remain unqualified.

The final non-wait probe build is `337b6c20`; structured fault-output
verification used `1b01c4b9`. The shared build is left on the non-wait
probe, not the rejected wait control. After the last RF trial, both
original 16-MiB flashes from this campaign were restored and independently
saved as `restored-{A,B}-full.bin`, byte-compared and SHA256-verified
against the fresh backups (the A/B hashes listed above). Final
`bootsel-{A,B}.log` reads confirm both badges were left in BOOTSEL.
No AP credentials were read or provisioned in this campaign.

### Nonterminal second-body and copy-end boundary controls (2026-10-20)

This campaign tests whether the failing second MPDU depends specifically
on PPDU EOF, and whether the six-byte PLCP/count convention explains the
copy-end error. A follow-up tests the first-body streaming transition.
None of these controls repairs reception. They do not uniquely
distinguish RXE/FIFO corruption from incorrect transmitted second-body
bytes, and it does not complete D2 work package 2.

#### Implemented controls and evidence repair

- `CLEAN_RX_HT_HANDOFF_GUARD`, default zero, selects exact command-12
  version 8 and a 564-byte physical PSDU. It appends two alignment bytes,
  a valid delimiter and an 80-byte FCS-valid guard to the original pair.
  For a fixed nonce/index, the first and second MPDU bytes are unchanged.
  The guard clones the second header/payload prefix, uses sequence
  `2*index+2`, body marker 2 and a recomputed FCS. HT-SIG describes the
  actual longer PPDU. Staging still verifies every one of 1132 bytes.
  Source, observer length admission, protocol correlation and build
  identity are wired together; version 7 stays unchanged with the gate off.
- `reference-handoff-guard` retains the existing one-shot advance and
  copy probe. It tests only the first two bodies, not delivery of the
  guard. The host rejects non-good case arguments before USB access:
  a second body followed by a guard is no longer the last body, and
  the old overlong-second claim is no longer a physical PPDU truncation.
  Exact raw fault fixtures remain native-tested, not RF-qualified cases.
  The guarded pair predicate requires the 564-byte source and a good,
  non-EOF/non-aborted second-body status; it cannot substitute a terminal
  snapshot or ordinary two-body geometry.
- `CLEAN_RX_HT_COPY_END_PLUS6`, default zero, requires the non-wait copy
  probe. `reference-handoff-end6` changes the copy-end instruction from
  saved next length to saved next length plus six. No source bytes,
  reset bits, delay or stronger event clearing are changed. It is a
  bounded perturbation of the PLCP-inclusive count hypothesis, not a
  proposed general length rule.
- `CLEAN_RX_HT_COPY_QUIET`, default zero, requires the unmodified-end
  non-wait copy probe. `reference-handoff-quiet` clears only the existing
  low two-bit streaming field in RX control after first-body retention
  and before EOI. It records the immediate before/after values in probe
  words 42/43, expanding the probe to 44 words inside the unchanged
  early SHM export. The ordinary 128-byte streaming threshold remains;
  no flush, reset or source change is added. This follows a code-path
  difference from the working fixed 80-byte aggregate diagnostic, which
  stays below that threshold. Native execution verifies the field clear,
  preserved surrounding control bits/registers, unchanged copy end and
  count, and no TX.
- Initial guarded trials exposed a missing evidence path: explicit
  aborts were logged but skipped the full continuation snapshot. With
  the copy probe enabled, outcome 5 now passes through the existing
  bounded snapshot, owner/epoch/length stability checks, and then the
  original abort cleanup. Aborted data is never delivered or recast as
  a good/bad-FCS completion. Wrong/stale owners, already committed
  snapshots and counts outside 50..306 still cannot commit.
  The collector exposes `terminal_sample_committed` and
  `copy_end_plus6_enabled`; an armed-only mask zero is not overwrite proof.

#### Retained RF campaign and interpretation

Evidence is under `build/clean-rx/terminal-boundary/`, including matching
image/controller/collector archives, all failed raw reports, logs and an
independent `audit.js` / `audit.jsonl`. Role 0 remains B source/A observer;
role 1 reverses them. Every cohort covers both 170/298 orders and both
badge roles. RF stimulus is MCS0; native tests cover MCS0..7.

| Cohort | Trials | Completed captures | All controls received | Abort exits | Bad-FCS exits | Committed second snapshots |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial guard, before abort export | 4 | 106 | 2 | 4 | 0 | 0 |
| Guard with abort export | 4 | 107 | 2 | 4 | 0 | 4 |
| Two-body copy-end plus six | 4 | 72 | 1 | 2 | 2 | 3 |
| Matched two-body baseline | 4 | 74 | 2 | 3 | 1 | 4 |
| Two-body stream field clear | 4 | 132 | 4 | 2 | 2 | 4 |

All 20 trials retain an exact first body from source index 2 and perform
the one-shot advance. None establishes a complete valid pair. The eight
guarded exits have RX abort set and EOF clear, at counts
223/304/178/178 before abort export and 306/196/62/178 afterward. A
failure that requires reaching PPDU EOF is therefore insufficient to
explain this behavior; a nonterminal second body fails too.

All 15 committed continuation samples have the correct fresh first
eight bytes, with the first mismatch at byte 8. Independent terminal
prefix samples agree with the later full snapshots. Twelve samples
overwrite all four sentinels. Three short aborts, at counts 59/62/62,
retain the two higher sentinels: their mask is 12, not an all-buffer
overwrite. Bytes beyond the reported short count are stale diagnostics,
not received-body evidence. No complete exact second body, good or
deliberately bad, is inferred from these snapshots.

The copy-end control reads back the expected six-byte bias in every
trial. Its exits are counts 190/310/59/182, compared with matched
baseline counts 190/196/62/178. The 310-byte exit is preserved in the
ordinary terminal record but exceeds the unchanged continuation-copy
ceiling, so no full sample is committed. In the reverse-role short-body
cell, count changes from 178 to 182 while the body remains incorrect.
This rejects the specific plus-six fix; it does not establish a general
RXE count interpretation or rule out other FIFO/boundary mistakes.
The source controller is byte-identical across baseline/end6 and to
the preceding copy-progress campaign. Guard-source controllers are
byte-identical before and after the abort-export change.

All four stream-field trials independently record `0x802 -> 0x800`
before EOI, with the ordinary copy end unchanged. Counts
306/196/140/178 still end bad/abort; all four retained second prefixes
diverge at byte 8. The source controller remains byte-identical to
baseline/end6. Receiving every interleaved control in these four boots
is encouraging but is neither statistical recovery qualification nor
a handoff repair. This rejects the specific clear-before-EOI strategy,
not FIFO streaming/ownership as a class of causes.

Across 20 trials, all 491 completed controller captures are posted and
consumed, with zero F2 drops. Only 11/20 receive every interleaved control;
some aborts are followed by substantial RF loss. Conservation of
completed captures does not qualify receive recovery or early reliability.
The independent audit reconstructs all 480 transmitted fixtures and
checks 80 report headers/CRCs/builds/nonces, source record/staging/SIG
bytes, exact early bodies, owner/epoch, programmed copy ends/stream
readbacks, bounded
second snapshots, terminal outcomes and normal drain accounting.

Two attempted cells failed BOOTSEL readiness before flashing or RF and
have readiness logs only; successful repeats use new prefixes. Badge B
intermittently exposed its mass-storage interface while picotool reported
exclusive USB ownership. Serial-to-disk mapping was verified before
copying the existing auto-BOOTSEL observer UF2 to that badge's volume.
The existing collector's missing-peer cleanup then requested BOOTSEL
without starting an RF session. Both recovery-only failures are retained
separately and excluded from the 20 RF trials. No unrelated process was
terminated or USB device reset.

#### Validation, restoration and next discriminator

Guard, end6, quiet, ordinary probe and rejected wait profiles run existing
native ASan/UBSan tests and freestanding C3/RP links. Tests exercise guard
delimiter/padding/FCS geometry, all supported fixture indices/rates,
exact staging admission and mutation rejection, nonterminal pair
status/geometry rejection, end-register values, abort snapshot
ownership/bounds/immutability, preserved abort cleanup and no TX.
Final guard, end6 and ordinary probe controller bytes are compared with
their RF archives; the quiet archive retains the exact native-tested
RF image. The ordinary production controller remains byte-identical
to the preceding verified default.

Fresh complete originals were saved before flashing. Both full 16-MiB
flashes were restored and independently verified after the first 16
trials; those fresh full readbacks also preserve the originals before
the quiet follow-up. After the final trial both entire originals were
restored again, independently reread into
`final-restored-{A,B}-full.bin`, byte-compared and SHA256-verified against the
fresh backups. They retain the A/B hashes recorded above.
`final-restore-{A,B}.log`, `final-readback-{A,B}.log` and `final-bootsel-{A,B}.log`
record the final restoration and BOOTSEL checks. No AP credentials
were read or provisioned.

Remaining discrimination needs either an independent complete on-air
second-body decode or a causal receiver-only change that restores exact
bytes/FCS with the same source. The evidence now excludes EOF-only
failure and rejects a simple plus-six copy-end repair; it does not
repair reception by clearing the first-body stream field either. It does not
justify guessed RXE reset masks, a larger successful-looking snapshot
bound, or HT association. D2 handoff, recovery, variable-length buffering,
secure BA lifecycle, Immediate BA/SIFS, mandatory protection and negotiated
interoperability remain open.

### Independent vendor raw receiver (2026-10-20)

The preceding boundary campaign could not distinguish wrong on-air bytes
from a clean receive/configuration defect. This campaign replaces only the
listener with the existing production vendor firmware, while retaining the
exact version-7 MCS0/channel-6 source controller and staged fixtures.
The source controller byte-compares with
`terminal-boundary/baseline-images/source-controller.bin`.

#### Capability, implementation and qualification gate

The existing vendor peer had only queried GET_MONITOR. A bounded new
`--monitor-probe` mode records GET_MONITOR, radio up, channel 6, enable,
enabled readback, disable and disabled readback. Badge A passes all seven
operations, with monitor state 0/1/0, build `50ff7cae`. This result alone
does not qualify raw receive.

`reference-handoff-monitor` now pairs the unchanged clean source with
`vendor-burst/monitor.c`. The listener uses no clean controller/receive
program, credentials, association, scan, Ethernet TX or requested data
transmission. It disables MPC/PM, enables/readback-checks monitor mode and
verifies actual/target channel 6. Capture has a calibrated twelve-second
bound; normal freeze performs a 300-ms drain, disables/readback-checks
monitor mode and powers off. The existing TFT identifies passive RX.

An opt-in callback in the shared vendor transport exports complete
validated non-control SDPCM packets, including packets received while
waiting for CDC responses. This is separate from Ethernet decoding and
cannot be enabled together with the Ethernet receive queue. Raw mode
permits uninterpreted non-control channels; ordinary parsing retains its
original channel limit. Flow and credit handling remain active.

The raw trace is a distinct `MON1` format, not clean observer ABI 21.
It retains nine setup/cleanup CDC records, channel and selection counts,
and eight immutable complete challenge-bearing SDPCM packets, each up
to 640 bytes. Unrelated packet contents are not saved. Oversize and
selection-full exclusions are explicit and do not masquerade as successful
captures or clean F2 drops. Monitor mode or generic Ethernet delivery
alone cannot qualify a trial.

The collector checks every source descriptor/staging record, final staging,
raw length/complement/header/CRC and selection conservation. Its success
gate requires complete exact legacy and single-HT controls and both
complete aggregate bodies at a common transmitted index, **including
the exact FCS bytes**. Payload-only matches remain diagnostic-only.
There are no retries of individual PPDUs in this stimulus.

#### Eight-trial result

Evidence: `build/clean-rx/independent-rx/`, RF build **`dc77ae04`**.
Matching UF2s, collector, build ID and source controller are archived in
`monitor-images/`; the earlier capability-only images are in `probe-images/`.

| Vendor listener | Body order | Trials | Complete exact saved MPDUs | Complete aggregate pairs |
| --- | --- | ---: | ---: | ---: |
| A | 170 to 298 | 2 | 16 | 4 |
| A | 298 to 170 | 2 | 16 | 4 |
| B | 170 to 298 | 2 | 16 | 4 |
| B | 298 to 170 | 2 | 16 | 4 |
| **Total** | Both | **8** | **64** | **16** |

Every trial retains the first eight challenge-bearing packets: exact
legacy controls at source indices 0/3, single-HT controls at 1/4, and
both aggregate bodies at indices 2/5. Their complete MAC bytes and FCS
match the independently reconstructed fixtures. All retained RF data
arrives on SDPCM channel 2; an ordinary Ethernet decoder is not used.

The 32 raw reports include 192 source records. Across the eight trials,
240 non-control SDPCM packets reach the callback. Of 166 packets carrying
the full challenge, 64 are saved and 102 intentionally exceed the
eight-slot selection; none exceed 640 bytes. Another 74 packets lack
the challenge and are counted without retaining content. These counts
are not a complete on-air receive denominator. Vendor-internal filtering
or drops are unknown, and the campaign does **not** qualify receive
reliability or claim lossless transport.

The independent ignored `audit.js` reconstructs every source fixture,
uses a separate bitwise IEEE CRC32, checks all 32 report bindings/CRCs,
192 source staging CRCs, final staging, all 64 complete raw bodies/FCS,
selection/channel accounting, four repeated cells and original restoration.
`node build/clean-rx/independent-rx/audit.js` produces retained `audit.jsonl`:
eight trials, 64 exact bodies and 16 exact pairs.

One attempted second B/170 trial fails BOOTSEL readiness before flashing
or RF. The successful repeat has a new `-retry` prefix. Badge A exposes
mass storage but not the vendor USB interface, reproducing the prior macOS
ownership quirk. After explicitly mapping A to its disk/mount, the archived
monitor UF2 is copied only to A's volume. The existing collector's
missing-live-peer cleanup returns it to BOOTSEL without starting RF.
Readiness-only and recovery-only failures remain separate from the eight
RF trials; no unrelated USB device or process is reset/terminated.

#### Interpretation, validation and restoration

The same two physical radios independently decode the controlled second
body in both roles and both size orders when running vendor receive.
Together with unchanged source controller bytes and exact staging checks,
this excludes systematically incorrect second-body transmission for the
tested fixture. The observed clean second-body divergence at byte 8 is now
localized to the clean receive/configuration path, rather than an inherent
radio inability to receive the source. It is **not** yet a proof of one
particular FIFO/end/EOI rule: receiver PHY/MAC setup, RXE context, streaming,
copy ownership and early-retention timing still need causal isolation.
No new guessed register masks or purported receive repair are promoted.

ASan/UBSan tests cover raw versus ordinary channel admission, malformed
envelopes, raw callbacks during CDC waits, RX-mode exclusion, session reset,
no Ethernet TX, exact 640-byte storage, overflow/oversize accounting,
immutable slots, CRC rejection, payload truncation, byte-8 corruption,
bad/missing FCS and the exact common-index/FCS qualification gate.
The original vendor peer and ordinary clean handoff/copy profiles retain
native tests and freestanding links with no undefined runtime symbols.
Final shared harness outputs are left on ordinary `reference-handoff-copy`.
Its source and probe-observer controllers remain byte-identical to the
preceding RF archives; the default production controller also remains
byte-identical to the prior verified default.

Fresh complete originals are saved before any flashing. Both full
16-MiB flashes are restored afterward and independently saved as
`restored-{A,B}-full.bin`, byte-compared and SHA256-verified against those
backups. Hashes retain A `8cda7a00...23628` and B `bc3f972d...21e2`.
`restore-{A,B}.log`, `readback-{A,B}.log` and `bootsel-{A,B}.log` record
restoration and final serial-targeted BOOTSEL checks. No private AP
credentials are read or provisioned.

This supplies the missing independent multi-body stimulus proof. It does
not mark D2 complete: clean handoff/abort recovery, repeated variable-size
buffering, secure BA lifecycle, Immediate BA/SIFS, mandatory protection and
negotiated HT interoperability remain open.

### Deferred pre-handoff streaming repair (2026-10-20)

The independent vendor receiver established correct transmitted second-body
bytes. The next causal control keeps the exact same source and changes
only the clean receiver's streaming threshold. The working 80-byte fixed
diagnostic is below the ordinary 128-byte threshold; the failing 170/298
bodies cross it. The earlier clear-before-EOI test happened **after**
streaming had already started and did not test this distinction.

`CLEAN_RX_HT_COPY_DEFER_STREAM=1` is a default-disabled control in
`frame_limits.h`/`ht_observer.h`. The `reference-handoff-defer` profile uses
384 bytes instead of 128, keeping both bounded bodies below the threshold
while remaining below the existing 506-byte staging-overflow bound.
It requires the ordinary non-wait copy probe and excludes end bias,
stream clear and guard-body geometry. No source, copy-end, EOI, reset,
cleanup, payload/FCS predicate or receive-size bound changes.

#### Matched causal result

Evidence: `build/clean-rx/stream-threshold/`, with matching RF
`defer-images/` and `baseline-images/` archives. The 60,372-byte receiver
controllers have exactly one differing byte, at offset 34,806:
baseline `0x10`, deferred `0x30`. That changes one bit encoding the
128-to-384 comparison immediate in the existing streaming instruction.
Their source controllers byte-compare with each other and the independently
decoded vendor-monitor source. All 1,132 staging bytes remain regenerated,
written and verified; every source record passes independent reconstruction.

The campaign runs deferred, matched baseline, then deferred again,
covering both badge roles and both body orders:

| Good fixture | Trials | Exact complete valid pair | Full qualification |
| --- | ---: | ---: | ---: |
| Baseline, threshold 128 | 4 | 0 | 0 |
| Deferred, A receives 170 to 298 | 2 | 2 | 0 |
| Deferred, A receives 298 to 170 | 2 | 2 | 0 |
| Deferred, B receives 170 to 298 | 2 | 2 | 2 |
| Deferred, B receives 298 to 170 | 2 | 2 | 2 |

All twelve good trials retain the exact first body at source index 2.
All four baselines reproduce second-body divergence at byte 8: three
bad-FCS exits and one explicit abort. All eight deferred trials instead
retain the exact second body/FCS at its correct logical RXE count:
304 for a 298-byte body and 176 for a 170-byte body, with good outcome.
Two pre-handoff hardware samples show RX control `0x800` rather than
baseline `0x802`. Owner, epoch and handoff phases also match.

The collector requires that readback evidence for deferred-profile success;
a missing or differently owned trace cannot qualify. It reports the
threshold, enabled control and readback verification explicitly. The
archived images were tested before the final host-only helper/test
extraction; final source and receiver controller bytes still compare
identical to those RF archives.

The four good A-listener trials remain **failed full trials** because
interleaved controls are missing. One also lacks a required late recovery
control. These losses are not relabelled as successful receive reliability
or hidden by the complete pair. The full good-trial gate is unchanged.

#### Bad-frame and malformed-input controls

The same deferred receiver is exercised once in every role/order for each
of bad-first, bad-last, bad second delimiter and an eight-byte-overlong
second delimiter claim. Only source aggregate index 2 carries the fault.

| Case | Trials | Later/exact good pairs | Strict negative qualification |
| --- | ---: | ---: | ---: |
| Bad first FCS | 4 | 4, all at source index 5 | 4 |
| Bad last FCS | 4 | 0, intentionally invalid second | 4 |
| Bad second delimiter | 4 | 4, at source index 5 or 8 | 0, diagnostic-only |
| Overlong second-body claim | 4 | 1, at source index 5 | 0, diagnostic-only |

All four bad-first trials retain the rejecting bad-FCS terminal record,
then capture an exact later pair and required early/late recovery controls.
All four bad-last trials retain the complete deliberately corrupted
second bytes at the exact length, with matching owner/epoch and bad-FCS
outcome, and receive the required recovery controls. A generic bad-FCS
exit is still insufficient. Other missed controls remain reported; the
existing negative gate requires its defined early/late recovery set,
not every ordinary good-fixture control.

Three overlong-claim trials retain source index 2's first body but correctly
do not advance, because the decoded next length differs from the admitted
170/298-byte geometry. Their absent advance/readback is not interpreted as
measured active streaming. The fourth misses the initial candidate and
captures a later good pair. Malformed-delimiter and overlong-claim
negative qualification remain unsupported and always return nonzero;
one trial of each also misses required recovery controls. These do not
prove general abort/truncation recovery.

#### Accounting, validation and restoration

There are **28 RF trials, 112 reports and 672 source records**. All
**953 completed captures** reconcile through posted/consumed/F2 counters
without delivery drops. This does not count RF/pre-prefix losses as
delivered. The independent ignored `audit.js` reconstructs all good/fault
fixtures and staging CRCs, validates every report binding/CRC, all retained
first/second bytes and independent bitwise FCS, hardware threshold
readbacks, one-byte controller difference and full restoration.
It finds 17 exact valid pairs (including later recovery pairs), four exact
deliberately invalid second bodies and 25 committed continuation snapshots.
Twelve trials pass existing strict gates: four good and eight bad-FCS
negative controls. All failures remain retained.

One B/298 attempt fails BOOTSEL readiness before flashing/RF; its repeat
uses a new `-retry` prefix. Badge A's mass-storage-only/vendor-interface
ownership quirk is recovered only after verifying its current serial to
disk/mount mapping. The archived observer is copied to that badge's
volume; missing-peer collector cleanup returns it to BOOTSEL without RF.
Readiness/recovery-only failures are excluded from the 28 RF trials.

Native ASan/UBSan tests exercise every RX count and 256 control-state
patterns around the exact streaming threshold, preserved unrelated bits,
idempotence, snapshot ownership/bounds and no TX. Added host tests reject
active-stream readbacks, wrong/missing owner or epoch, incomplete phases
and malformed headers. The default production controller and ordinary
128-byte baseline remain byte-identical to prior verified images.

Fresh complete originals precede all writes. After the campaign both
16-MiB originals are restored, independently saved as
`restored-{A,B}-full.bin`, byte-compared and SHA256-verified against those
backups. A retains `8cda7a00...23628`; B retains `bc3f972d...21e2`.
Final serial-targeted `bootsel-{A,B}.log` checks confirm both in BOOTSEL.
No AP credentials are read or provisioned.

This demonstrates that **pre-handoff streaming is causal** for the tested
clean SHM-only continuation failure. It strongly implicates FIFO/copy
ownership interaction rather than wrong transmitted bytes; it does not
establish the undocumented internal DMA/FIFO mechanism. Merely clearing
the stream field after first-body retention was too late in the rejected
earlier control. The 384-byte control remains gated: it is not a general
solution for 554-byte bodies, variable-length/long aggregates or the
ordinary production DMA receive path. D2's loss accounting, malformed/abort
recovery, general buffering, BA/protection and negotiated HT gates remain.

### Streaming byte destination and DMA discriminator (2026-10-20)

The threshold control above proves a streaming-dependent failure but does
not locate the missing bytes. The ordinary Cortex receive path retained
only a completed descriptor's 38-byte header before clearing its entire
buffer. Inspecting only those old headers could not distinguish a broken
SHM copy from bytes already transferred elsewhere.

Added default-off `CLEAN_RX_HT_COPY_DMA_PROBE=1` and the matched
`reference-handoff-dma` / `reference-handoff-defer-dma` profiles. This is a
read-only data-path diagnostic, not a receive repair. It changes no PSM
instruction, RXE command, DMA ring programming, on-air fixture or cleanup
sequence. Existing DMA shutdown gains a diagnostic readback check; an
unstopped engine produces a controller error instead of a purported scan.

Each completed descriptor is examined before its existing clear/repost.
The first experiments showed why this was insufficient: an A-listener
baseline abort had **no completion after the handoff**. The final diagnostic
also examines all 32 whole buffers after verified radio/DMA shutdown, so
unfinished descriptors cannot escape simply because no header completed.
Shutdown status/control read back zero in every full-ring RF trial.

The core scans all 3,968 bytes of each descriptor for the fresh 14-byte
challenge copied from the retained first MPDU. It counts matching first
and second QoS prefixes and checks their actual FCS before selection limits.
It saves two 768-byte windows, preferring an aggregate over ordinary
traffic and a second body over a first in the second slot. Matched-window
metadata includes descriptor index, completion count, DMA status, current
handoff owner/epoch, held state, original header length and window offset.
Frozen matches are not labeled completed packets. Current handoff context
does not assign the DMA packet to that handoff; independently reconstructed
source bytes and FCS establish actual identity.

The 1,728-byte version-5 `DMO1` trace occupies the former MPDU proof-sample
area at 2560; observer ABI becomes 22 only for this profile. FIFO delivery
and accounting still run, but the ordinary proof-sample count stays zero.
**Every trial remains diagnostic-only with exit 1.** In particular,
`exact_pair:false` is intentional for this profile even when separate
early/continuation SHM snapshots contain an exact pair. This does not
silently relax or replace the existing strict F2 pair/control gate.

Evidence: `build/clean-rx/stream-dma/`. Twenty-eight RF trials include the
initial completed-only, counter-qualified, stopped-ring, priority-retention
and final first/second-FCS variants. No failed trial is removed:

- 112 complete reports and 672 source records;
- 757 completed captures conserved through F2 with zero delivery drops;
- 1,404 counted buffer examinations, including repeated completed/frozen
  inspections, **not a wireless receive denominator**;
- 14 saved challenge-bearing DMA windows, each containing independently
  reconstructed complete source bytes and valid FCS;
- 14 exact retained-SHM pairs across the deferred diagnostic trials;
- final baseline/deferred builds `c84deea2` / `74910108`.

Final version-5 result, repeated across both roles and both body orders:

| Listener / first body | Baseline captures | Baseline terminal | Baseline valid first / second QoS bodies in DMA | Deferred captures | Deferred SHM second |
| --- | ---: | --- | ---: | ---: | --- |
| A / 170 | 3 | abort, RXE 190 | 0 / 0 | 38 | exact 298 + valid FCS |
| A / 298 | 3 | abort, RXE 66 | 0 / 0 | 37 | exact 170 + valid FCS |
| B / 170 | 41 | bad FCS, RXE 304 | 7 / 0 | 36 | exact 298 + valid FCS |
| B / 298 | 34 | bad FCS, RXE 178 | 7 / 0 | 35 | exact 170 + valid FCS |

All eight retain the exact first body at source index 2. Every baseline
continuation first diverges at byte 8. On B, the first retained DMA example
is the exact later legacy control; the prioritized example is the exact
first MPDU of aggregate 5, at physical DMA offset 44 (38-byte offset plus
six-byte PLCP). The per-buffer counters independently count all seven
later aggregate first bodies as valid and **zero second-body prefixes**,
rather than inferring absence from the two saved windows.

On A, baseline DMA buffer contents are all zero after the abort and there
are no later completions to inspect. On B, later legacy/HT controls and
aggregate first bodies really do stream through DMA. This excludes a
generally broken or permanently stalled DMA engine as the explanation
for B's reproducible continuation corruption. It does not explain A's
stronger abort/recovery failure.

Every deferred case has an exact valid SHM second body. No challenge reaches
DMA there: these 170/298-byte bodies never reach the 384-byte streaming
threshold. That is expected for this SHM-only control, not evidence DMA
independently received the pair. Both A-listener final deferred trials
still miss required interleaved controls; the two B-listener controls
receive them all. None is promoted to production/D2 qualification.

**Causal interpretation:** the first-body stream path itself can deliver
correct aggregate bytes. The broken second prefix/body does not merely
reappear as a complete second body in the inspected DMA stream. Together
with independent vendor reception and the one-bit threshold reversal,
this narrows the remaining defect to RXE/copy/streaming ownership or byte
position at the aggregate handoff, upstream of DMA completion. It does
not prove the undocumented internal RX-buffer pointer, first-body FIFO
publication or latch semantics. Intermediate bytes flushed before a
completion or before shutdown cannot be recovered by this scan.

The next discriminator must separate streaming read-position/context
changes from first-body publication timing without reviving the rejected
80-byte stream-after-arm/header/EOI controls as purported fixes. Any
per-MPDU finalization experiment needs its own bounded progress, exact
byte/FCS and error/recovery proof. The 384-byte gate still cannot be
generalized blindly to 554-byte bodies or long aggregates.

Native ASan/UBSan tests cover full-buffer challenge boundaries, no-match/
partial-match exclusion, immutable slots, aggregate/second-body priority,
both FCS lengths, corrupted FCS, malformed metadata/counters and shutdown
readbacks. Both C3/RP images link freestanding. Three invalid gate
configurations fail compilation. The instrumented baseline PSM object
equals the uninstrumented one. The two final receiver images differ only
at zero-based byte 35762, `0x10` versus `0x30`; the source controller is
byte-identical to the independently vendor-proven image. Production
default firmware remains byte-identical. The independent bitwise-CRC
`audit.js` checks all reports/source records, retained bodies, accounting,
one-byte control and restoration; `audit.jsonl` passes.

Fresh complete 16-MiB originals precede all writes. Both are restored,
independently reread, byte-compared and SHA256-verified: A
`8cda7a00...23628`, B `bc3f972d...21e2`. Final serial-targeted checks
confirm both badges in BOOTSEL. No credentials are read or provisioned.
D2, malformed/abort recovery and HT association remain open.

### Continuation streaming and moving copy boundary (2026-10-20)

The DMA experiment did not separate first-body streaming from continuation
streaming. Added default-off `CLEAN_RX_HT_COPY_STREAM_PHASE`, requiring the
existing deferred, non-wait copy probe. The first body always uses threshold
384. An exact one-shot advance state of 1 selects:

- phase 1, `reference-handoff-stream-hold`: remain at 384;
- phase 2, `reference-handoff-stream-resume`: resume at 128;
- phase 3, `reference-handoff-stream-late`: resume at 256.

Advance remains 1 after the retained handoff: the selected continuation
threshold also applies to subsequent traffic for the rest of the trial.
These are matched diagnostic controls, not a per-MPDU streaming repair.
Invalid advance states retain the initial threshold. All three execute
the same native PSM branches; only the comparison immediate changes.
No source-controller bytes, EOI, copy endpoint or RXE command change.

The initial eight trials retain exact pairs in all four hold cells and
both 170-byte resumed-continuation cells. Both 298-byte resumed
continuations instead watchdog at RXE count 134. The original terminal
copy probe does not snapshot watchdogs, so those two failures retain
their terminal log but no continuation-body snapshot. Their after-arm
readbacks still show the first-body count, making stale-count activation
a plausible hypothesis at that point, not an established cause.

Added phase-only version-3 copy diagnostics without replacing normal
F2 proof samples or changing observer ABI 21. Copy words 36..43 retain
the first streaming activation after advance:

| Word | Meaning |
| --- | --- |
| 36, 37 | RXE count immediately before and after the streaming write |
| 38, 39 | RX-control readback and status1 after the write |
| 40, 41 | TSF low/high before the write |
| 42, 43 | Commit marker 1 and receive epoch |

The marker is cleared at copy arm; subsequent stream events cannot
overwrite a committed event. A matching epoch is mandatory before
attributing activation to the retained continuation. A hold or short
256-threshold trial can legitimately record streaming of a later packet;
the collector explicitly labels that event as another epoch, not proof
that this continuation streamed.

Only phase profiles additionally retain a bounded continuation snapshot
on native watchdog outcome 6. Existing owner/epoch, 50..306-byte count
bounds and immutable terminal-copy commit guards still apply. The
watchdog returns to its original cleanup with outcome 6; it is never
converted to good completion or delivered. Explicit abort remains
outcome 5. Activation proof is separate from complete-body/FCS/F2 and
control-receipt qualification.

Evidence: `build/clean-rx/stream-resume/`. Twenty-six actual RF trials
include the initial eight, eight activation-instrumented hold/resume
cells, four 256-threshold cells and six reverse-order repeats. Five
BOOTSEL-readiness failures and one USB-only recovery attempt are also
retained, but are not counted as RF trials. All RF source controllers
are byte-identical to the independently vendor-verified source.

| Continuation threshold / second body | RF trials | Exact SHM/F2 pairs | Strict passes | Retained failure boundary |
| --- | ---: | ---: | ---: | --- |
| 384 / 170 or 298 | 10 | 10 | 7 | none in retained pairs; three control-receipt failures |
| 128 / 170 | 4 | 4 | 3 | one control-receipt failure |
| 128 / 298 | 6 | 0 | 0 | RXE 134; all four instrumented copies first differ at byte 128 |
| 256 / 170 | 2 | 2 | 2 | threshold not reached in the retained continuation |
| 256 / 298 | 4 | 0 | 0 | RXE 258; all four copies first differ at byte 252 |

Both badge roles reproduce every row. The reverse repeats change both
role order and threshold order. Every retained first MPDU is byte-exact
with valid FCS. One initial 170-byte resumed continuation belongs to
source index 5 rather than index 2; this is a valid later aggregate,
not proof that the initial aggregate was retained. All other retained
first bodies are source index 2. Twelve of the sixteen exact pairs also
receive every required interleaved control; the four control-loss
trials stay failed despite their valid pairs.

The instrumented 128-threshold trials consistently commit activation at
RXE **130 before and after** the write, with matching continuation epoch
and control `0x802`. Their 298-byte continuations stop at 134 with exactly
128 canonical bytes. Thus streaming did not activate from the stale
176/304 first-body count in these observations. The superficially
suggestive arithmetic `134 = 304 - 170` is not evidence of first-body
length subtraction.

Moving only the threshold to 256 commits matching-epoch activation at
**258 before and after**, and moves the canonical-byte boundary to
**252**, with terminal count 258. Three trials watchdog; one explicitly
aborts. Bytes beyond that boundary are not a valid continuation or FCS
and cannot be interpreted as newly received body data. In contrast,
the 170-byte continuation can stream at 128 and still complete exactly,
and hold384 retains every tested full pair.

**Causal conclusion:** loss of continuation-copy progress follows the
streaming transition itself, even after an unstreamed first body, and
the loss position moves when the threshold moves. A stale first-body
count is not needed to reproduce this failure. First-body streaming
cannot be its sole cause. This is stronger than a fixed-length,
bad-second-body or generally broken-DMA explanation, but does not
identify the internal RXE/FIFO read pointer, copy ownership or latch
semantics. In particular, the two thresholds do not establish one
universal count-to-copy offset. Do not invent a subtract-first-length
rule or treat streaming as categorically incompatible with all
continuations.

The independent bitwise-CRC audit verifies 104 raw reports, 624 source
records and final staged fixtures, exact first/second bodies and FCS,
normal retained F2 samples, activation epochs/readbacks, copy boundaries
and complete capture/post/consume conservation. All 896 raw captures
are conserved with no delivery drops; this is not a count of distinct
OTA MPDUs or an RF reliability denominator. Failed byte/FCS/control
trials remain separate from the twelve strict successes.

Native ASan/UBSan tests cover every RX count and 256 control states,
advance 0/1/2/0xffff, unrelated control-bit preservation, idempotence,
phase-specific watchdog snapshot/return behavior and invalid owner,
epoch, version, commit, count and streaming readbacks. All three C3/RP
profiles link freestanding; invalid phase 4 and missing parent gates
fail compilation. Default native tests pass and the production image
remains byte-identical.

Initial hold/resume receiver images differ only at zero-based byte
60310 (`0x30` versus `0x10`). Instrumented hold/resume/late images
differ only at byte 60334 (`0x30`, `0x10`, `0x20`); both hold comparisons
are single-bit reversals. Final host-gate validation builds reproduce
the corresponding RF C3 images exactly. The audit also checks these
identities, rather than trusting profile names.

Fresh complete 16-MiB originals were saved before all experimental writes.
Both full flashes are restored and independently reread, byte-compared
and SHA256-matched: A `8cda7a00...23628`, B `bc3f972d...21e2`.
Final serial-targeted checks confirm both in BOOTSEL. The macOS USB
ownership hiccup was recovered through the serial-mapped mass-storage
volume and bounded collector cleanup, without killing unrelated
processes. No credentials were read or provisioned.

The next repair must preserve both body copying and RX-buffer ownership
when streaming becomes necessary, rather than just moving this cutoff.
Hold384 still cannot cover a 554-byte body within the known staging
overflow limit. Loss-accounted longer-body delivery, repeated PPDUs,
malformed/abort recovery and mandatory HT MAC integration remain open.
D2 is not complete and HT station advertisement stays disabled.

### Streaming copy-state and immediate retrigger discriminator (2026-10-20)

The moving boundary above leaves two distinct questions: does the
streaming transition rewrite the programmed continuation-copy context,
and can an immediate replay of the existing copy command restore progress?
Added default-off `CLEAN_RX_HT_COPY_STREAM_REARM`, requiring phase 2
(resume128), with two instruction-count-matched profiles:

- mode 1, `reference-handoff-stream-rearm-control`: capture state and
  execute a jump to the next instruction;
- mode 2, `reference-handoff-stream-rearm`: capture the identical state
  and execute the existing continuation instruction
  `0x376, 0x1883, 0x1004, 0x1004` once after streaming starts.

No guessed register bits, copy length adjustment, reset or new on-air
fixture is introduced. The command uses the existing copy base/end.
The first activation's existing commit marker guards both diagnostics
and command replay; later frames cannot retrigger it even if their
streaming bit clears. Instruction-count matching does not claim identical
hardware latency for a jump and a copy command.

Only these profiles expand the copy probe from 44 to 54 words and
version 3 to version 4. Words 44..48 record control, base, endpoint,
raw next-length and raw context before the streaming write. Words
49..53 record RXE count, control, base, endpoint and context after the
no-op or command. Existing words 36..43 still retain count/control
immediately after streaming and before this action, TSF, epoch and
final commit. The SHM record now ends exactly at 0x600; its report
export ends at trace 5792, immediately before existing controller-fault
evidence. Compile-time checks prevent overlap. Observer ABI 21,
normal F2 samples, terminal ownership and all good/bad/watchdog gates
are unchanged.

The collector emits `handoff-stream-rearm` and requires matching owner,
epoch, version and activation commit, stream-clear-before/active-after
readbacks, bounded count, and the unchanged programmed base/endpoint
before accepting the diagnostic proof. Activation JSON now also
requires a committed marker before labeling an epoch match. An exact
pair and all required controls remain separate strict-pass conditions.

Evidence: `build/clean-rx/stream-rearm/`. Twelve RF trials comprise both
roles/orders with both profiles, then all four long-continuation cells
repeated in reverse role/profile order. The receiver binaries differ
in exactly one 8-byte PSM instruction (seven differing bytes at
zero-based 60476..60482). Source controllers are byte-identical to the
independently vendor-verified stimulus.

All twelve commit activation at count 130 and retain the exact first
body. Sampled copy state is consistent across roles and size orders:

| Readback | Matched no-op | Existing-command retrigger |
| --- | --- | --- |
| Control before streaming | 0x0800 | 0x0800 |
| Control immediately after streaming | 0x0802 | 0x0802 |
| Control after action | 0x0802 | 0x08c2 |
| Count before streaming / after streaming / after action | 130 / 130 / 130 | 130 / 130 / 130 |
| Base before / after action | 0x0800 / 0x0800 | 0x0800 / 0x0800 |
| Endpoint before / after action | actual second length / same | actual second length / same |
| Raw next / context before streaming | 0x8000 / 0xf020 | 0x8000 / 0xf020 |
| Context after action | 0xf020 | 0xf020 |
| Terminal control | 0x0802 | 0x0802 |

The request's 0x00c0 bits are visible after the retrigger and gone at
the terminal snapshot. This demonstrates a real control-register
effect, not a missing software branch; it does **not** prove the
internal copy engine accepted or completed another transfer.

| Profile / second body | Trials | Exact SHM/F2 pairs | Strict passes | Terminal |
| --- | ---: | ---: | ---: | --- |
| no-op / 298 | 4 | 0 | 0 | all watchdog, count 134, first byte mismatch 128 |
| retrigger / 298 | 4 | 0 | 0 | all watchdog, count 134, first byte mismatch 128 |
| no-op / 170 | 2 | 2 | 0 | good, count 176; required controls missing |
| retrigger / 170 | 2 | 2 | 1 | good, count 176; one control-loss trial |

Thus immediate command replay does not extend the canonical 128-byte
prefix or repair the long continuation. It also does not corrupt the
complete short continuation in these trials. The sampled base,
endpoint and context are not accidentally rewritten by streaming.
This narrows the search beyond a simple missing immediate copy-command
replay or visibly wrong programmed base/end, without proving
undocumented pointer/ownership semantics or excluding fast transient
state between samples.

The experiment acts immediately at count 130, before the later stall
at 134. A command ignored while an engine is already active, or a
different recovery after independently confirmed stalled progress,
is not excluded. Do not present this failed immediate retrigger as
testing every possible rearm timing. A future control must distinguish
those cases with a bounded progress/timeout decision and preserved
failure bytes, not by blindly repeating the command.

The independent audit reuses the previous campaign's pure independent
fixture/bitwise-CRC functions, not production validators. It verifies
48 raw reports, 288 source records and final fixtures, all before/after
readbacks, exact first bodies and short continuations, all eight
128-byte copy cutoffs, retained F2 samples and command-only image
differences. All 410 raw captures are conserved through post/consume
with no delivery drops; they are not a distinct-OTA receive denominator.
All eleven strict failures remain retained. No control-loss result is
upgraded because its pair is valid.

Native ASan/UBSan tests cover every RX count and 256 control states,
advance 0/1/2/0xffff, preservation outside the intentional command field,
idempotence and committed/invalid nonzero markers after streaming is
cleared. Host tests mutate fifteen ownership/version/marker/readback
fields and reject unsupported modes. Invalid mode 3 and absent phase
parent fail compilation. Both C3/RP profiles build freestanding; final
validation reproduces the RF controller images. Production default
and the earlier resume128 C3 image remain byte-identical.

Fresh complete originals were independently saved and matched to the
previous restored originals before any write. Both full 16-MiB flashes
are restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Final serial-targeted checks
confirm both badges in BOOTSEL. No USB recovery was required and no
credentials were read or provisioned. D2, larger-body streaming,
loss-accounted recovery and HT association remain open.

### Measured post-stall replay and suffix overwrite (2026-10-20)

Immediate replay did not establish whether the copy request was ignored
while reception was still progressing. Extended the default-off
`CLEAN_RX_HT_COPY_STREAM_REARM` gate with modes 3/4, requiring the same
resume128 phase and existing copy/observer gates:

- `reference-handoff-stall-control` (mode 3): await a measured plateau,
  then execute the matched no-op;
- `reference-handoff-stall-rearm` (mode 4): await the identical plateau,
  then issue the existing continuation-copy command once.

The new path awaits count 134, requires it to remain unchanged for
32 TSF microseconds, and checks count/abort/EOF again immediately before
the action. One shared 1,023-poll fallback bounds both waiting stages,
including a stopped TSF. Count progress, explicit abort, EOF or exhausted
budget skip the command. The wait does not use or alter native GPRs,
the outer receive watchdog or any RXE register; only the selected final
action writes the existing copy command. The native EOF/FCS/abort and
ownership paths resume afterward without outcome conversion.

Both profiles use copy-probe version 5 with the same 54-word storage,
ending at SHM 0x600 / trace 5792 before controller-fault evidence.
Words 36..43 retain the prior independent activation proof. Version-5
words 44..53 instead encode:

| Word | Meaning |
| --- | --- |
| 44 | Count sampled when beginning the stability test, or initial count if skipped earlier |
| 45 | Remaining shared poll budget |
| 46, 47 | TSF low at test start and modular elapsed microseconds |
| 48 | Decision: 1 stable, 2 progress/change, 3 EOF, 4 abort, 5 exhausted |
| 49, 50 | Count and control after the command or skip |
| 51..53 | Base, endpoint and raw context after the action |

`handoff-post-stall` distinguishes valid diagnostics, a verified stable
interval, and actual command issue. A progressing short body is not
mistaken for a stall: its command is skipped. Poll exhaustion cannot
qualify as a success. Normal proof samples and exact-byte/FCS/F2/control
gates remain mandatory; observer ABI 21 is unchanged.

Evidence: `build/clean-rx/post-stall/`. Twelve RF trials cover both
roles/orders and four reversed role/profile repeats of the long body.
Two failed BOOTSEL readiness checks and two USB-only serial-mapped
recovery attempts remain archived but are not RF trials. Source
controllers remain byte-identical to the independently vendor-verified
source. The two receiver controllers differ only in one 8-byte action
instruction (seven bytes at zero-based 60716..60722).

All eight long-continuation trials verify count 134 held for exactly
32 us, with 587..597 polls remaining. All retain the correct copy base
0x0800, endpoint 298 and sampled raw context 0xf020 after the action.

| Profile / second body | Trials | Action | Terminal and bytes | Exact pairs / strict passes |
| --- | ---: | --- | --- | --- |
| delayed no-op / 298 | 4 | no command after verified plateau | watchdog, count 134; first 128 bytes canonical | 0 / 0 |
| delayed replay / 298 | 4 | command after verified plateau | explicit abort; A count 62, B count 68; suffix overwrites SHM start | 0 / 0 |
| delayed no-op / 170 | 2 | skipped on progress | good, count 176, complete valid FCS/F2 | 2 / 2 |
| delayed replay / 170 | 2 | skipped on progress | good, count 176, complete valid FCS/F2 | 2 / 2 |

The command readback again becomes 0x08c2 rather than the no-op's 0x0802,
with count still 134 immediately afterward. Unlike immediate replay,
the subsequent failure now changes: **SHM MPDU bytes 0..31 are exactly
transmitted second-body bytes 128..159**, reproduced twice in each badge
role. A search across every offset of all source bodies for the trial
finds this fresh 32-byte sequence only in the retained continuation at
offset 128. It is not a first-body, stale-trial or arbitrary corrupt
prefix match.

Only that 32-byte shifted span is demonstrated. The next byte diverges
from the expected suffix. Later SHM bytes include unchanged parts of
the old prefix and other noncanonical contents; they are not labeled
newly received data. Neither the terminal count nor a 32-byte suffix
match is complete-MPDU or valid-FCS evidence.

The no-op preserves the original 128-byte prefix instead. One B no-op
repeat retained source aggregate 5 rather than the initial aggregate 2;
its complete first body, failed continuation and ownership are verified
against that actual source index. Its absent initial-case terminal log
is not turned into evidence that aggregate 2 completed.

The A delayed-abort trials capture only two/three raw frames and miss
required later controls; B captures 35 and receives the required
controls. This role-dependent recovery failure remains explicit.
Four short-body successes do not qualify recovery from a stalled
long body: their commands were never issued.

**Causal conclusion:** after a measured stall, replay does access fresh
continuation data beyond the preserved prefix, but places it at the
unchanged SHM start and destroys that prefix. The request is not merely
ineffective in the same way as immediate replay. Source/read progress
and SHM write placement are therefore distinct pieces of state that a
safe continuation must preserve. This does not identify the undocumented
internal pointer/bank or prove which earlier operation advanced it.
It does not justify blindly adding 128 to a destination register,
changing endpoint units, or treating counts 62/68 as usable body lengths.

The new count retreat exposed an old host-proof assumption: terminal
count below the streaming threshold had been treated as an unstreamed
short body. Four original delayed-abort JSON reports consequently say
`stream_phase_readback_verified:false` despite their independently
committed activation and plateau. They remain unchanged. The final
validator permits this bounded version-5 count retreat only with
matching committed activation/epoch and active stream readbacks; it
does not weaken byte, FCS, owner or terminal-good checks. A sanitizer
`recheck.c` tool validates all twelve archived raw reports using the
corrected helpers; `activation-recheck.jsonl` records the result.
All four aborted bodies remain failures, not newly qualified pairs.

The independent bitwise-CRC/fixture audit verifies 48 raw reports,
288 source records and final fixtures, twelve exact first bodies,
all eight timing decisions, four uniquely mapped suffix overwrites,
14 retained F2 samples and complete accounting for 356 raw captures.
The capture count is not a distinct-OTA denominator. It also checks
the original rejected phase flags, corrected activation rechecks,
command-only image delta, final validation-image identity and full
restoration.

Native ASan/UBSan tests cover all RX counts and 256 control states
through the stall entry, then execute the actual bounded wait for
13 normal/fault cases: progressing/decreasing count, EOF/abort,
nonadvancing count, frozen TSF, TSF wrap and last-moment changes or
budget exhaustion before action. All four native GPRs are preserved.
Host tests reject 16 mutated fields, invalid reasons/modes, command
bits on a skipped action, premature/excessive elapsed time and
invalid post-replay count/activation combinations. A redundant nested
test loop introduced in the prior work is moved outside the count
loop without reducing its cases. Both new C3/RP profiles link
freestanding; at that checkpoint, out-of-range mode 5 and missing parents
fail compilation.
The production image and previous immediate-replay C3 image remain
byte-identical.

Fresh complete originals preceded all writes. Both full 16-MiB flashes
are restored and independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Final serial-targeted checks
confirm BOOTSEL. No credentials were read or provisioned.

The next discriminator should separate write-destination placement
from source progress, preserving both old prefix and newly read bytes
under explicit bounded geometry. Destination/end/count units must be
verified before attempting a segmented repair. D2, larger-body
streaming, repeated aggregates and reliable abort recovery remain open;
HT station advertisement remains disabled.

### Bounded copy-destination placement (2026-10-20)

The preceding delayed replay exposes source bytes 128..159 at the wrong
SHM destination. This experiment tests write placement independently:
after the same verified count-134/32-us plateau, both profiles write the
copy-base register and replay the same existing copy command. Only the
added destination offset differs. There is no endpoint change, guessed
reset, repeated command or new production receive path.

Default-off `CLEAN_RX_HT_COPY_STREAM_REARM` now accepts:

- **5:** add zero to SPR `0x001`, then replay.
- **6:** add 64 to SPR `0x001`, then replay.

Both require the existing phase-2 resume128 profile. The shared
1,023-poll budget, progress/EOF/abort checks and final pre-action checks
remain unchanged. Skipped actions neither write the destination nor
issue the command. No native GPR is borrowed. The bounded 300-byte
snapshot plus candidate displacement stays within RX scratch storage.
Adding zero is still a register write, **not** a hardware no-op; the
matched comparison isolates its offset, not possible side effects of
writing the base itself.

Copy-probe **version 6** retains the 54-word version-5 layout and
decisions. Validators require the exact mode/version combination and
post-action base: `0x840` only for mode 6 with a confirmed stable
decision, otherwise `0x800`. Count-retreat activation validation also
recognizes version 6 without relaxing full-body/FCS checks.
`handoff-post-stall` adds `destination_probe` and `base_shift_words`;
the controlled result explicitly reports `diagnostic_only:true`.
Both destination profiles are deliberately excluded from qualification,
including short-body trials where the action is skipped.

Evidence: `build/clean-rx/copy-destination/`. Twelve RF trials cover both
roles/orders and reverse-order long-body repeats. The receiver C3 images
are each 60,884 bytes and differ in **one byte**, zero-based offset
60,718, inside the same eight-byte add instruction at 60,716. Independent
instruction decoding checks SPR `0x001` as source/destination and
immediates zero/64, followed by the identical existing copy command.
Both source-controller binaries exactly match the independently
monitored fixture image.

| Profile / second body | Trials | Retained canonical prefix | New source-128 span | Terminal result | Exact SHM/F2 pairs / with all controls |
|---|---:|---:|---|---|---:|
| base + 0 / 298 | 4 | 0 bytes | bytes 0..31 | 2 aborts, 2 bad-FCS completions | 0 / 0 |
| base + 64 / 298 | 4 | 160 bytes | bytes 128..159 | 3 aborts, 1 bad-FCS completion | 0 / 0 |
| base + 0 / 170 | 2 | complete 170 bytes | action skipped | good, count 176 | 2 / 1 |
| base + 64 / 170 | 2 | complete 170 bytes | action skipped | good, count 176 | 2 / 1 |

All eight long-body trials activate streaming at fresh count 130, then
verify count 134 unchanged for exactly 32 TSF microseconds. Immediate
post-action count is still 134; base readback is exactly `0x800` or
`0x840`, endpoint remains 298, and raw context remains `0xf020`.
Post-action control reads `0x0802` in this campaign, rather than the
earlier command-only campaign's `0x08c2`; command bits are not required
to remain latched. The executed instruction path and newly placed
source bytes supply the action evidence, not an assumption about
hardware bit-clear timing.

In **all four shifted trials**, original bytes 0..127 remain correct,
and exactly 32 newly placed bytes extend the canonical prefix through
159. The first mismatch is offset 160. In all four zero-offset controls,
the same source span replaces bytes 0..31; the first mismatch is offset
zero. Searching every 32-byte window of every transmitted source body
uniquely identifies each new span as the retained continuation's bytes
128..159. Searching the complete 300-byte snapshot finds it only at
the corresponding destination offset, zero or 128.

This establishes that **adding 64 to this copy-base register moves this
write by 128 SHM bytes**, under the tested geometry. It preserves the
old prefix but does not recover the rest of the body. Do not classify
bytes after the first mismatch as fresh data: stale/noncanonical bytes
remain in the snapshots.

Abort terminal counts are 62 on A, 68 or 78 on B. Three repeats instead
reach bad-FCS terminals: both zero-offset repeats at count 304 and the
shifted A repeat at 306. A terminal count near the intended length is
therefore **not** evidence of a correct body. Every long-body fixture
FCS fails independently, and no long pair reaches the normal exact
SHM/F2 gate. The first shifted B trial retains source index 5 rather
than initial index 2; its initial-case terminal fields remain zero.
Later control losses are retained, not converted to successful recovery.

All four short-body trials detect progress, leave base `0x800`, skip
the command and retain exact FCS-valid SHM/F2 pairs. Only the two B
trials receive every required control. These are compatibility controls,
not evidence that a stalled transfer recovered; all twelve runner exits
remain nonqualifying because the profiles are explicitly diagnostic-only.

Native ASan/UBSan tests exercise the exhaustive streaming/once gates and
all 13 bounded stall scenarios for each profile. Tests require the
destination shift only on a confirmed mode-6 action; every skipped
path retains the old base, endpoint/context and four native GPRs.
Host tests reject wrong offsets, shifts on skipped actions and wrong
mode/version combinations, while keeping existing mutation gates.
At that checkpoint, mode 7 and a mode-6 profile without its parents
reject compilation.
Both C3/RP profiles link freestanding. Production default and prior
mode-4 C3 firmware remain byte-identical.
Final validation rebuilds reproduce both RF C3 images exactly; RP
binaries differ only in their four-byte archive-path-dependent build ID.

The independent bitwise-CRC/fixture audit checks 48 raw reports,
288 source records, twelve exact first bodies, eight uniquely mapped
suffix spans, 14 retained F2 samples and conserved accounting for
342 raw captures. Raw captures are not a distinct-OTA denominator.
Both fresh complete 16-MiB originals are restored and independently
reread, byte-compared and SHA256-verified: A `8cda7a00...23628`,
B `bc3f972d...21e2`. Both are confirmed BOOTSEL. No credentials were
read or provisioned.

The destination-placement hypothesis is now directly supported, but
placement alone is insufficient. The next discriminator must explain
why only 32 new canonical bytes survive, including bad-FCS cases whose
terminal count advances to 304/306: verify endpoint/count semantics
versus copy-bank/stream ownership before attempting further segmented
transfers. D2, variable-length aggregate reception and reliable abort
recovery remain open; HT station advertisement remains disabled.

### Replay endpoint versus remaining bytes (2026-10-20)

The bounded destination test preserves 160 canonical bytes but leaves
the original endpoint 298 in place after source offset 128. This
experiment tests a specific remaining-length interpretation: with
destination displacement fixed at the verified +64 units, compare
endpoint 298 with `298 - 128 = 170` immediately before the same replay.
It does not assume that endpoint/count units or copy-bank ownership
are already understood.

Default-off `CLEAN_RX_HT_COPY_STREAM_REARM` modes **7/8** add matched
endpoint writes: subtract zero or 128 from SPR `0x02a`. Both retain the
same measured count-134/32-us stall, +64 destination write and existing
copy command. Both endpoint writes are real register writes, not no-ops.
They run only after all shared progress/EOF/abort/budget checks. A new
bounded geometry guard permits endpoints 129..300; otherwise decision
**6** skips both destination/endpoint writes and replay. Old modes
0..6 have no new guard or instruction.

Copy-probe **version 7** keeps the 54-word layout. Host validation
requires exact mode/version, action-dependent base and endpoint, and
verified stable timing for decisions 1 and 6. A skipped action cannot
change either register; an in-range endpoint cannot claim a geometry
rejection. `handoff-post-stall` adds `endpoint_probe` and
`endpoint_subtract`. All destination/endpoint profiles remain explicitly
diagnostic-only and cannot qualify station HT operation.

Evidence: `build/clean-rx/copy-remaining/`. Twelve RF trials cover both
roles/orders and reverse-order repeats of long-body cells. Receiver
C3 images are 60,924 bytes and differ in exactly one byte at zero-based
offset 60,758: the subtract immediate in the instruction at 60,756.
Independent decoding confirms the same SPR `0x02a` operands and the
identical copy command that follows. Source C3 images remain identical
to the independently monitored fixture image.

| Endpoint / second body | Trials | Canonical prefix | Terminal results | Exact SHM/F2 pairs / with all controls |
|---|---:|---:|---|---:|
| unchanged 298 / 298 | 4 | 160 bytes | bad FCS; counts 304, 304, 304, 306 | 0 / 0 |
| remaining 170 / 298 | 4 | 160 bytes | A: bad FCS/count 178 twice; B: abort/count 68 or 69 | 0 / 0 |
| unchanged / 170 | 2 | complete 170 bytes | progress, action skipped, good/count 176 | 2 / 2 |
| remaining / 170 | 2 | complete 170 bytes | progress, action skipped, good/count 176 | 2 / 2 |

All eight long-body cases activate streaming at count 130 and confirm
count 134 unchanged for exactly 32 TSF microseconds. Post-action
readback is base `0x840`, endpoint 298 or 170, context `0xf020`,
count 134 and control `0x08c2`. In every case the original 128-byte
prefix remains intact and exactly source bytes 128..159 extend it to
160 canonical bytes. Byte 160 still mismatches. Independent searches
across every transmitted source body and all destination windows
uniquely map those 32 bytes to source continuation offset 128 and
snapshot offset 128.

To distinguish missing bytes from a merely displaced later suffix, the
audit searches every 16-byte window of source offsets 160 onward across
the entire retained 300-byte snapshot. None appears in any long-body
trial. This is a bounded SHM observation, not a claim that later bytes
are absent from every other receive/DMA buffer.

Shrinking the endpoint changes termination but **does not extend the
correct bytes or yield a valid body**. The A count changes from
304-class to 178; B instead aborts early. This does not establish a
global endpoint/count unit rule or exclude every alternative length
interpretation. It excludes this specific remaining-length adjustment
as a sufficient repair under the tested geometry. Matching terminal
counts or latched command bits remain insufficient evidence of valid
receive data.

All retained early bodies are source index 2, including both B repeats;
there is no later-aggregate substitution in this campaign. Four short
controls skip the action without changing base `0x800` or endpoint 170,
and deliver complete FCS-valid pairs with every required control.
Eight long-body cases remain failures: six bad-FCS completions and
two explicit aborts. Lost interleaved controls remain recorded.
All runner exits remain nonqualifying by design.

Both profiles pass native ASan/UBSan tests, including the original 13
stall scenarios plus five exact geometry boundary cases: reject 128,
301 and 65535; accept 129 and 300. Rejected geometry preserves both
registers, command state and all four native GPRs. Host mutations cover
mode/version, wrong/subtracted endpoints, changed skipped registers,
in-range geometry rejection and premature decision-6 timing.
Mode 9 and missing mode-8 parents fail compilation. Final C3 images
reproduce both RF images; production default and preceding mode-6 C3
images remain byte-identical.

The final actual C validators, run under ASan/UBSan, recheck all twelve
archived captures without changing original RF JSON. The independent
bitwise-CRC/fixture audit verifies 48 raw reports, 288 source records,
eight uniquely mapped 32-byte spans, 16 retained F2 samples and conserved
accounting for 412 raw captures. These are not 412 distinct OTA MPDUs.
Fresh complete originals preceded all writes. Both full 16-MiB flashes
are restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`; both are confirmed BOOTSEL.
No credentials were read or provisioned.

The unresolved discriminator is now the **32-byte fresh-copy limit and
its producer/consumer ownership**, rather than treating a changed
termination count as repaired data. Inspect bounded copy progress and
bank availability before further segmented commands. D2, repeatable
variable-length delivery and HT station advertisement remain open.

### Focused copy microscope and current-epoch stream ownership (2026-10-20)

The preceding 32-byte observation was a **canonical-prefix limit with
streaming active**, not a proven maximum transfer size. A small on-chip
PSM microscope now distinguishes live copy progress from terminal SHM
corruption. Its dedicated native executable also decodes saved reports;
the unchanged reference source and USB harness supply only stimulus and
transport. No new TX operation, library or radio reset is introduced.

`copy_scope.h` contains the probe program; `copy_scope_test.c` tests the
actual emitted PSM instructions and can inspect a raw capture directly.
Default-off `CLEAN_RX_HT_COPY_SCOPE` requires the existing mode-6
destination-shift replay, with the original endpoint unchanged:

| Scope mode | During the bounded observation | Later streaming in the same receive epoch |
|---|---|---|
| 1 / on | keep stream field 2 | original behavior |
| 2 / off | write stream field 0 before replay | original behavior can re-enable it |
| 3 / hold | write stream field 0 before replay | keep it 0 for the committed matching epoch |
| 4 / resume | write stream field 0 before replay | matched guard explicitly writes it back to 2 |

Modes 1/2 have identical instruction footprints and differ in one byte
of one stream-field write. Modes 3/4 share the same guarded program and
differ in one byte of its later stream-field write. The latter pair
therefore tests **re-enablement**, not a different replay or endpoint.

#### Probe layout and boundedness

The probe takes three 26-word snapshots: before the existing +64 base
write/replay, just after replay, and after a bounded 32-TSF-us wait.
Each stores TSF low, count, status1/2, copy control, base, endpoint, raw
next/FIFO/context and two 16-byte SHM windows at MPDU offsets 128 and
160. EOF or abort ends the observation early; a 1,023-poll budget also
bounds a frozen TSF. Snapshot 2 temporarily supplies the elapsed/budget
slots, then overwrites them with final readbacks.

The 80-word scope occupies SHM `0x4b0..0x4ff`, entirely inside unused
first-body snapshot storage. Only scope profiles reduce the early-body
cap from 480 to 320 bytes; static assertions prevent overlap with the
retained first body and continuation header at `0x500`. The 170/298-byte
fixture remains unchanged. Header word 1 carries the epoch. Header word
0 commits only after all three snapshots, as `0x10 + reason`: 17 timer,
18 EOF, 19 abort, 20 exhausted. Copy arm clears both header words.

The ordinary copy-probe layout/version 6 stays unchanged. With the scope
enabled its post-action readbacks occur **after** the observation window;
they must not be interpreted as immediate-command timing. Separate
`copy-scope`/`copy-scope-record` JSON and validation establish the live
stream-on/off state rather than forcing a stream-off experiment through
the older stream-on validator. Commit 20 is explicitly nonqualifying.
All four profiles remain diagnostic-only regardless of their byte result.

The epoch guard requires advance 1, a committed scope header 17..20 and
the matching current epoch. Stale, pending or invalid headers and other
advances retain normal streaming. R3 is borrowed only with the existing
save/restore slot; every exit restores it, preserving all four native
GPRs. The epoch comparison uses a register and one SHM input, not an
unsupported two-SHM-input instruction.

#### Physical results

Evidence: `build/clean-rx/copy-scope/`. **Twenty-four RF trials** cover all
four profiles in both roles/orders, with reverse-order long-body repeats.
On/off C3 images are 61,676 bytes with one differing byte at zero-based
60,959, in the field-write instruction at 60,956. Hold/resume images
are 61,756 bytes with one differing byte at 61,647, in the instruction
at 61,644. Independent decoding checks opcode `0x310`, the intended
0/2 immediate and the same copy-control register. Every source image
matches the independently monitored source fixture.

| Scope / 298-byte continuation | Trials | Canonical SHM bytes | Expected 298-byte FCS | Native outcomes | Normal exact SHM/F2 pair |
|---|---:|---:|---|---|---:|
| on | 4 | 160 | invalid | 3 aborts, 1 bad FCS | 0 |
| off, original re-enable | 4 | 256 | invalid | 2 aborts, 2 bad FCS | 0 |
| off, epoch-held quiet | 4 | **298, complete** | **valid** | 4 bad FCS, count 176 | 0 |
| off, matched re-enable | 4 | 256 | invalid | 2 aborts, 2 bad FCS | 0 |

All sixteen long-body trials first verify the same count-134/32-us stall
and fresh activation at count 130. Before replay, neither sampled window
matches the continuation. Immediately afterward, bytes 128..143 match
but 160..175 do not. At exactly 32 TSF microseconds after the second
snapshot, bytes 160..175 become correct in **every stream-off profile**,
but never in the stream-on control. This is live evidence of additional
copy progress, not a final-buffer inference.

Allowing the native path to turn streaming back on produces a new
256-byte canonical boundary. The matched current-epoch hold instead
retains **every byte of the 298-byte continuation, including the exact
transmitted FCS**, in both roles and reverse-order repeats. Independent
fixture generation checks the complete body/sequence/nonce, not just
the CRC or selected windows.

This rules out a fixed 32-byte copy-engine capacity under this geometry:
the truncation follows streaming ownership and re-enablement. It does
**not** identify an internal bank-pointer implementation or establish a
general segmented receive algorithm.

#### Why this is still not valid delivery

All four complete held SHM bodies retain native **outcome 2**, count
**176**, and base `0x840`; none reaches the advanced normal F2 sample.
The 176 count corresponds numerically to the 170-byte remainder plus
six, not the complete 304-byte logical receive length. This is evidence
of restarted/local accounting, not yet a universally qualified rule.
The normal snapshot's FCS check uses its reported 170-byte MPDU length
and therefore remains false. The focused decoder separately checks the
known expected 298-byte body and reports its valid FCS, explicitly
alongside the native rejection and `delivery_qualified:false`.

Do not replace the native outcome, normalize its count blindly, waive
hardware GoodFCS, or advertise HT. The remaining investigation must
separate copy/count restart from hardware FCS/terminal ownership, then
publish a correctly bounded FIFO/header without duplicate or partial
delivery. Correct retained bytes alone do not satisfy those gates.

The first held A capture uses aggregate source index 5, not initial
index 2; its repeat retains index 2. The resumed B repeat also retains
index 5. Original initial-case misses/zero terminal fields are preserved.
Every other retained case is index 2. Eight short-body controls progress,
skip the microscope and deliver exact SHM/F2 pairs; six receive every
required interleaved control. All sixteen long-body native failures and
other control losses remain nonqualifying.

#### Focused verification and restoration

`make -C clean-rx/src ... copy-scope-test` builds a small dedicated
executable. It executes only the emitted probe instructions for normal,
EOF, abort, frozen-timer and TSF-wrap cases, checks bounded writes and
waits, snapshot offsets, commit values and GPR preservation, and rejects
12 readback mutations per case. The hold/resume variants additionally
test seven fresh/stale/pending/invalid epoch/advance guard cases.
This is software-control verification, **not RF emulation**.

Running that executable with a capture filename validates the real
probe ownership/readbacks and separately reports expected-length FCS
versus native count/outcome. All 24 archived captures pass this final
ASan/UBSan recheck. Existing native receive, host fixture/mutation and
freestanding C3/RP builds also pass. Scope 5 and missing replay parents
reject compilation. Scope-disabled production and preceding mode-6 C3
firmware remain byte-identical. Final builds reproduce all four RF C3
images; RP binaries differ only in their archive-dependent build ID.

The independent bitwise-CRC/fixture audit verifies **96 raw reports,
576 source records, 16 three-snapshot probe runs, four complete
298-byte SHM bodies, 29 F2 samples and conserved accounting for
951 raw captures**. Raw captures are not a distinct-OTA denominator.
It also verifies both one-byte image pairs and all final C rechecks.

Fresh complete originals were saved before experimental flashing. Both full 16-MiB flashes
are restored and independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Both badges are confirmed
BOOTSEL. No credentials were read or provisioned. D2 and HT station
qualification remain open.

### Repeated early-MPDU FIFO delivery and complete packet archives (2026-10-20)

This implements a missing delivery foundation, **not D2 completion or
early-capture reliability qualification**. The new default-disabled
`CLEAN_RX_HT_EARLY_FIFO` profile delivers retained first nonterminal MPDUs
through the existing SRAM FIFO and F2 transport. It does not advance to
the next aggregate body, admit a station, enable TX or advertise HT.
Evidence is retained under `build/clean-rx/early-fifo/`.

#### Committed retention, publication and acknowledgement

The new profile requires early save and the passive observer FIFO, and
rejects combination with next-body advance. Its retained header is
14 words, with a maximum 996-byte body; a compile-time bound keeps both
within the existing 512-word SHM export. The original twelve header
words retain their meanings. Word 12 preserves native PHY status, and
word 13 is a consumer acknowledgement.

PSM commits the owner last, after header/body copying. C3 verifies the
owner and epoch before and after reading into an unpublished FIFO slot.
Admission requires a nonterminal multi-body candidate, native GoodFCS
without EOF/abort, matching prefix epoch and HT-SIG, canonical aggregate
MCS0/HT20 geometry, the configured transmitter, a supported data frame
and complete software FCS. It does not waive native rejection or infer
successful reception from a length alone.

C3 acknowledges the retained owner only after publishing the complete
SRAM slot with barriers. PSM may then recycle it for a different receive
epoch. Missing/stale acknowledgements, the same epoch, absent pending
ownership and exhausted FIFO credit preserve retained bytes. Existing
singleton-to-first-multi replacement remains available. The emitted PSM
instructions are tested for all five recycle/ownership cases.

F2 correlation bit `0x4000` identifies this early path separately from
the existing `0x8000` next-body diagnostic. The host keeps exact source
receipt bits and duplicate counts. These counters occupy trace
5824..5835, beyond the complete early and controller-fault exports.
Initial placements at hardware snapshot word 84 and FIFO-stress word 16
were rejected by testing and corrected; their failed RF evidence remains
archived, not relabelled as passing.

#### Pre-terminal PHY status was not a valid F2 classification source

The archive campaign exposed a second real integration defect:
`export-role-B-2` retained eight exact 554-byte bodies, but two F2 packets
were labelled CCK and rejected by the strict host parser. Native early
PHY format bits were observed as 0, 1 and 2 despite canonical HT-SIG and
correct complete HT body bytes. The terminal PHY-status header cannot
be assumed valid at this earlier event.

For this separately tagged and already HT-SIG-validated path only,
F2 now uses unsupported/raw HT metadata (`0x40 | channel`, `0x46` on
channel 6). Native PHY status remains unchanged in diagnostic storage.
The host rejects legacy/CCK metadata for the new packet type. This does
not set negotiated HT station metadata or alter normal packet
classification. Tests cover all four raw PHY format values.

#### Full packet export, not only a final sample

The RP retains up to eight complete early F2 packets in a separate static
buffer. After receive stop, observer phase 5 exports that archive, leaving
the saved phase-2 capture and window evidence intact. There is no added
RP/controller communication during the radio copy.

At trace 128, the 16-byte `ERF1` header records stored/total counts and
overflow; eight 584-byte slots contain packet size, logical F2 ordinal,
parsed source index and complete packet bytes. A 554-byte body uses a
572-byte SDPCM/BDC packet. Strict validation checks envelopes, complement,
sequence/ordinal, tags/owner, metadata, nonce, full fixture/FCS, unique
source identity, receipt mask and zero unused slots. A valid partial
archive is not an all-bodies-received success.

The old metadata failure is preserved explicitly: source indices 11/23
have metadata `0x06`, parsed-index sentinel 24, and exact body bytes.
Only six packets were accepted; its original export validation failed.
The independent audit verifies those negatives as well as the complete
bytes, rather than retroactively accepting the malformed metadata.

#### Both-role RF results, including final failures

The unchanged version-6 clean source sends 24 transmissions per trial:
eight cycles of legacy, single HT and a two-554-byte-body aggregate, at
MCS0/channel 6 with calibrated 40-ms gaps. Source descriptors, HT-SIG,
fresh nonce, complete received bytes and independent FCS establish the
observed multi-body stimulus. Source completion alone is not an
independent per-attempt RF denominator; there is no third receiver.

Earlier repeated profiles delivered 48/48 first bodies, with one ordinary
HT control miss. Earlier injected-reset tests delivered 16/16 with all
controls. Five old archive trials delivered all eight bodies and all
controls; the sixth exposed the metadata defect above. These are not
substitutes for the final corrected matrix:

| Final profile / receiver | First bodies delivered | Required ordinary controls | Strict trial result |
|---|---:|---|---|
| normal A0 / B0 | 8/8 each | all | pass / pass |
| normal B1 | 7/8 | all | fail |
| normal A1 | 7/8 | missing index 9 | fail |
| normal A2 | 8/8 | missing index 13 | fail |
| normal B2 | 8/8 | all | pass |
| injected reset A | 7/8 | missing indices 4/12 | fail |
| injected reset B | 7/8 | all | fail |
| pause-until-stop A / B | 2/8 each | later traffic intentionally lost | stress only |

Final normal delivery is **46/48**, with 94/96 ordinary controls; final
reset delivery is **14/16**, with 30/32 controls. No duplicate early
delivery occurs. All sixteen old/final archive exports preserve 112
complete exact bodies; all ten final archives validate strictly.

The four final non-stress missing early source indices are B1:23, A1:17,
reset-A:20 and reset-B:8. These trials contain only seven aggregate-prefix
records and seven count-560 good hints each, with no prefix-log overflow;
each recorded aggregate exits as outcome 5. Every observed eligible
aggregate prefix is matched by an exact early receipt. Thus the measured
shortfall is already present at the prefix-observation boundary, rather
than an extra observed eligible candidate disappearing in the new FIFO
handoff. Identical aggregate HT-SIGs and the lack of a third receiver
prevent assigning a more specific RF/PHY cause or a per-missing-attempt
on-air verdict.

Both final injected resets execute once, retain TX-inhibit value 15
before/after, and measure 16 TSF microseconds. That proves this bounded
injected integration, not recovery from every natural persistent-busy
state. Both final stop-stress trials reach eight queued slots, retain
2,328/2,479 full-held samples and drain exactly after stop. Their later
RF losses are expected backpressure consequences, not reliability passes.
Final reports show no controller error, pending/dropped F2 packet, host
resync/stall/retry or prefix mismatch.

#### Verification, preservation and remaining gates

`reference-early-fifo`, `reference-early-fifo-reset-test` and
`reference-early-fifo-stop-test` build the three gated profiles.
`early-rx-test` exhausts every supported length 44..996, including odd
tails and output canaries, and mutates header/prefix/SIG/FCS, sample,
packet and archive evidence under ASan/UBSan. Existing PSM, MAC, Block ACK
and host protocol tests pass for the profiles. Invalid parents and
combination with advance reject compilation. Final freestanding C3/RP
builds pass; separate rebuilds reproduce all three final RF C3 images.
The default controller is byte-identical to the preceding production
image, and all ten source-profile C3 images are identical.

The independent audit verifies **33 trials, 181 full reports, 33 armed
reports, 792 source records, 33 exact retained SHM bodies, 31 exact early
F2 proof samples and 112 complete archived packets**, with conserved
accounting for **1,628 raw captures**. Raw captures include ambient traffic
and are not distinct transmitted opportunities. Bad-FCS discards remain
recorded, not misclassified as transport drops. Historical metadata,
counter-layout, RF and stress failures remain in the audit.

Fresh complete originals were saved before both flashing campaigns.
Both final full 16-MiB flashes were independently reread, byte-compared
and SHA256-verified after restoration: A `8cda7a00...23628`,
B `bc3f972d...21e2`. Both badges are confirmed BOOTSEL. No credentials were
used.

The finite sixteen-prefix log, fixed MCS0/554-byte stimulus and bounded
eight-packet export are not arbitrary sustained aggregate reception.
Persistent-busy/pre-prefix loss qualification, variable-length
continuation accounting and native FCS state, negotiated Block ACK/MAC,
protection and HT activation remain open. Do not advertise HT or mark
D2/early-capture reliability complete.

### Fixed-source vendor baseline and pre-prefix discriminator (2026-10-20)

The next bounded investigation bypasses the clean PSM/C3 receive path
while keeping the version-6 two-554-byte-body source unchanged.
**The vendor raw monitor is not yet a qualified loss baseline.**
Evidence, RF images, complete archives, failures and independent audit
are retained under `build/clean-rx/preprefix-baseline/`.

#### Implemented control and complete archives

`reference-stimulus-monitor` uses the existing vendor raw-monitor setup
on channel 6 with the fixed source. Unlike the older 170/298 handoff
monitor's eight-packet selection, this profile retains up to **32 complete
nonce-bound SDPCM packets** in a separate bounded RP buffer. That covers
the sixteen ordinary controls and both bodies of eight aggregates.
It records capture/full/oversize/unmatched counts and keeps the first
eight packets in the ordinary stopped report for continuity.

After stop, phases 5..8 export four pages. At trace 128, a 16-byte `MRA1`
header contains magic `0x3141524d`, zero-based page, total retained count
and this page's count. Up to eight existing 656-byte raw packet slots
follow. Validation checks identity/counts, packet envelopes, complete
packet CRC and zero padding/unused slots. The collector then regenerates
every source body and compares all 554 bytes, including FCS, across all
pages. The new fixed-source pass requires all 32 expected bodies, no
duplicate/corrupt body and no archive overflow/oversize.

The old handoff monitor retains its original eight-slot behavior and
pair/control qualification. No shared driver or vendor firmware is
modified; no station credentials or additional on-air TX are introduced.
All source C3 images remain byte-identical to the preceding RF source.
The clean receiver C3 image also remains byte-identical.

An initial setup rejection incorrectly treated fixed-source nonce byte
10 as a handoff case selector. That byte is random for version 6.
The guard now distinguishes the profiles; the rejected setup remains
excluded from RF evidence. The native lifecycle test explicitly uses a
nonzero random byte there.

#### Measured cutoff, not an RF-loss conclusion

Eight unpolled vendor trials across both roles/orders each report
**exactly 30 raw SDPCM packets**, including ambient packets. Their
nonce-bound selections vary from 8 to 17, for 108 complete exact packets
in total. They receive complete correct first/second bodies from the
same source aggregates in both roles, but stop reporting before the
source's finite sequence is fully represented. All eight fail the new
all-32 gate, without selection overflow, oversize or corrupt/duplicate
accepted bodies.

The separately gated `reference-stimulus-monitor-poll` performs read-only
get-monitor command 107 checks at calibrated 100-ms intervals. Two A
trials extend raw reception to 41/42 packets and preserve 19/11 exact
nonce packets. Both still fail all-32 qualification. This demonstrates
that host/control servicing affects the measured baseline; it does not
identify a unique internal vendor buffer, interrupt, credit or RF cause.

The 10-ms `reference-stimulus-monitor-poll-fast` discriminator instead
hits a control timeout after 60 successful checks and a failed 61st,
with 46 raw packets and 25 counted nonce packets. Its original code
could not export the partial archive after that error. Those 25 counted
packets are **not** included in the independently byte-verified packet
total. The failed capture and failed page request remain failures.

The final implementation freezes archive/counters and original firmware
error details on either normal stop or error. Post-fault diagnostic
pages can be retrieved without clearing or disguising the original
error; the trial's failed stop still prevents successful qualification.
ASan/UBSan tests execute this actual monitor lifecycle for every retained
count 0..32, including timeout preservation and all post-fault pages.
This post-fault path is native-tested, **not subsequently RF-qualified**.

#### Clean control, exclusions and outcome

Four additional clean FIFO trials deliver **29/32** exact first bodies:
7, 7, 7 and 8. The three missing bodies again coincide with only seven
aggregate-prefix/count-560-hint records; every observed eligible
aggregate is delivered. No prefix overflow/mismatch, controller error,
pending/dropped F2 packet or host resync/stall/retry is reported.
Captured/posted/consumed accounting reconciles for 197 raw captures.
Three trials receive every ordinary control; the fourth misses controls
3/9/13. Thus ordinary control delivery is 61/64 and **none of the four
trials passes the complete strict fixture gate**.

An external process twice claimed B's BOOTSEL interface exclusively.
Testing paused, the user released/reconnected it, and restoration was
completed. Readiness-only attempts that never submitted a source burst
are not RF trials. A resumed batch also stopped at a transient readiness
check; its unstarted cells are not fabricated. Further flashing stopped
after the recurring exclusive-access failure; the planned B slow-poll
and final fast-poll RF cells remain unexecuted.

The independent bitwise-CRC/fixture audit verifies **16 submitted
sessions, 15 source RF trials, 111 full reports, four armed reports and
360 source records**. It independently checks **138 complete vendor
packets and 29 complete clean early packets**, preserving the setup and
timeout exclusions. All four final software profiles build freestanding;
the actual monitor lifecycle, page/count/padding mutation tests and
existing clean/handoff/native suites pass under ASan/UBSan. Invalid
polling modes/parents reject compilation.

Both original full 16-MiB flashes were saved before writes and finally
restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. Both badges are BOOTSEL.
No further RF writes followed final restoration.

This campaign excludes using this vendor-monitor configuration as an
unqualified per-attempt RF denominator. It does not prove that the
clean pre-prefix misses are interference, reset failures or a specific
PHY defect. Early-capture reliability and D2 remain open; a sustained
independent receive baseline or simultaneous third-receiver evidence is
still needed before assigning that cause.

### Quiet-only versus copy replay at the original destination (2026-10-20)

Evidence: `build/clean-rx/replay-count/`. This bounded discriminator
separates stream quieting from replay without the previous 64-word
destination displacement. Both default-off profiles use scope 3,
stream-rearm mode 5, threshold 128, the same verified count-134/32-TSF-us
plateau, three scope records and matching-epoch stream hold:

- `reference-handoff-scope-replay` reissues the existing copy command.
- `reference-handoff-scope-no-replay` sets
  `CLEAN_RX_HT_COPY_SCOPE_NO_REPLAY=1` and jumps directly to the next
  snapshot instead.

Replay suppression is boolean and requires exactly scope 3/rearm 5.
Existing displaced scope profiles and the default controller are unchanged.
The dedicated native scope interpreter counts zero or one actual replay
instruction, checks the original destination, guards, timer wrap,
EOF/abort/frozen-timer paths and register preservation. This is emitted
program/guard testing, not emulation of receive-engine count or FCS.

The two archived 61,756-byte receiver images differ in just seven bytes
of one eight-byte PSM instruction. PSM starts at image offset `0x7e44`;
action PC 3580 is at file offset 60964. The replay is
`op=0x376,a=0x1883,b=0x1004,d=0x1004`; its control is
`op=0x77f,a=0x1780,b=0x1780,d=3581`, jumping to the following instruction.
The comparison uses alignment relative to PSM, not file-offset alignment.
This is instruction matching, not a claim of equal hardware latency.
Source controllers byte-match the independently vendor-verified version-7
170/298-byte source.

Twelve RF trials cover both receiver roles, both size orders, and
reverse-order repeats of the long continuation:

| Profile / continuation | Trials | Live count after 32 TSF us | Retained bytes | Native terminal result | Exact F2 pairs | All controls |
| --- | ---: | --- | --- | --- | ---: | ---: |
| Quiet only / 298 | 4 | 134, unchanged | 128-byte canonical prefix | count 134, watchdog outcome 6 | 0/4 | 3/4 |
| Quiet + replay / 298 | 4 | 58, after 134 | all 170 remaining source bytes overwrite body offset 0 | count 176, bad-FCS outcome 2 | 0/4 | 0/4 |
| Quiet only / 170 | 2 | scope correctly skipped | complete FCS-valid 170-byte body | count 176, good outcome 1 | 2/2 | 1/2 |
| Quiet + replay / 170 | 2 | scope correctly skipped | complete FCS-valid 170-byte body | count 176, good outcome 1 | 2/2 | 0/2 |

Every executed scope retains original base `0x800`, end 298 and context
`0xf020`. Controls change from `0x802` to `0x800` in both profiles and stay
quiet. Every live post-action observation is 32 TSF units. Quiet alone
never progresses beyond the original 128 canonical bytes. Replay obtains
the entire expected source range `[128,298)` at destination `[0,170)`,
including the original full-body FCS bytes, but that is not a complete
298-byte body and cannot validate as one. The native count falls from
134 to 58 in the live scope and eventually reaches 176. These are raw
observations, not an adjusted length rule.

This isolates a replay-dependent restart/overwrite from quieting alone.
It reinforces why the previous displaced replay could reconstruct the
whole SHM body while native reception still rejected it. It does **not**
identify a particular undocumented control bit as the FCS reset, prove
that count compensation is safe, or provide a normal admission repair.
The next discriminator must separate the replay field's receive-state
effects or find a continuation operation that preserves native count/FCS.
Native GoodFCS and FIFO/F2 admission remain mandatory.

One old collector summary had a separate selection defect: normal-mode
`replay-B-128` retains the first exact body at source index 5, but
`case_terminal_*` only followed retained early records at index 2. Its
summary printed zero while the complete second snapshot, matching copy
probe and prefix record actually contain count 176/outcome 2. Normal
mode now follows any already validated exact early body; negative cases
remain pinned to their deliberately corrupted source index 2. The original
failed JSON is preserved. A small offline harness includes the actual
collector and reprocesses all twelve original report pairs under
ASan/UBSan: only those three fields change in that one summary. All
qualification booleans and failures remain unchanged.

The independent bitwise-CRC/fixture audit verifies **48 full reports,
288 source records, 16 full F2 samples and 621 conserved captures**.
It independently verifies all twelve first bodies, all four replay
tails, four short exact F2 pairs, all scope/native outcomes and all twelve
native capture-decoder rechecks. Ordinary controls total **168/192**;
only one short-body trial has a pair plus every control. These are
diagnostics, not strict qualification passes. No failure is discarded,
and bad-FCS discard counters are not treated as transport loss.

Both final profiles, the existing displaced held-scope profile and the
default production controller build and pass their native ASan/UBSan
suites. Five invalid replay/value/parent combinations reject compilation.
Final RF receiver/source controllers byte-match their archives; existing
held-scope and production images byte-match their previous versions.
Both complete original 16-MiB flashes were saved before writes and
restored, independently reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`. A needed a bounded BOOTSEL
reenumeration retry before restoration; both final serial-specific checks
report BOOTSEL. No RF writes followed final restoration.

### Compound replay command, FCS state and copy-local length (2026-10-20)

Evidence: `build/clean-rx/copy-field/`. The next investigation traces the
actual emitted instructions before changing timing or lengths. Production
PC 135 starts the six-byte prefix copy with control `0x40`; PC 161,
the first continuation after PLCP, and the aggregate's initial body-start
instruction use `op=0x376,a=0x1883,b=d=0x1004`. The post-stall replay had
reused that same compound command as if it were a resume operation.
Its eight-bit field update covers register bits 6..13, asserting
`0x20c0`: bits 6, 7 and 13 together, not a single resume bit.

Public [b43 register names](https://github.com/mbuesch/b43-tools/blob/master/debug/include/spr.inc)
identify the legacy FIFO control/count registers but do not document these
three bits for this Arch15 device. The
[Linux register structure](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h)
also leaves relevant offsets unnamed. Neither is treated as a definitive
CYW43439 bit specification. No public code is incorporated into the firmware.

#### Matched field omissions

Three default-off profiles omit one already asserted bit:
`reference-handoff-scope-no-bit6` (`field=0x82`),
`reference-handoff-scope-no-bit7` (`0x81`) and
`reference-handoff-scope-no-bit13` (`0x03`).
They retain scope 3/rearm 5, original destination, the count-134 plateau,
32-TSF-us observations and matching-epoch stream hold. Each archived
61,756-byte receiver differs from the full-field reference by exactly
one byte of the same PSM instruction at PC 3580/file offset 60964.
The full-field controller byte-matches the previous replay campaign;
source controllers remain independently verified version 7.

The first eight RF trials cover all four profiles in both roles:

| Replay field | Omitted register bit | Count after 32 TSF us | Native terminal result | SHM result |
| --- | --- | --- | --- | --- |
| `0x83` | none | 134 -> 58 | 176, bad FCS/outcome 2 | entire remaining 170-byte tail overwrites prefix |
| `0x82` | 6 | remains 134 | 134, watchdog/outcome 6 | only original 128-byte prefix |
| `0x81` | 7 | 134 -> 58 | **176, GoodFCS/outcome 1** | entire remaining tail overwrites prefix |
| `0x03` | 13 | 134 -> 58 | 176, bad FCS/outcome 2 | entire remaining tail overwrites prefix |

This separates count restart from FCS state. Bit 6 is necessary for
progress in these replay combinations; bit 13 is not necessary.
Omitting bit 7 preserves native FCS success without preserving the old
count. The initial PLCP/body/next-MPDU starts remain unchanged: only the
mid-body replay field changes. This is not a general recommendation to
remove bit 7 at actual frame boundaries.

Four next RF controls deliberately corrupt only the final FCS byte of
source index 2's second body, comparing full-field and no-bit7 replay in
both roles. All four retain that first aggregate and native bad FCS.
The no-bit7 success is therefore not explained by simply leaving a stale
first-body GoodFCS flag or disabling final error detection.

#### Complete bytes and native GoodFCS together

`reference-handoff-scope-no-bit7-shift` combines the same `0x81` replay
with the already verified 64-word destination shift, scope 3/rearm 6.
Before the admission guard below, this controller differs from the old
held-shift reference in just one byte of the replay instruction.
Four good trials across both roles/repeats retain **all 298 expected
bytes, valid full-body software FCS, native status `0x4100` and outcome
1**. Two bad-last controls retain the deliberately wrong FCS and native
status `0x4000`/outcome 2. The final guarded tests reproduce both outcomes.
Across the whole campaign, nine retained complete 298-byte bodies have
both native GoodFCS and independently valid software FCS.

The count still resets to 58 in the live scope and ends at 176: it
describes the restarted copy, not the complete reconstructed MPDU.
The fundamental error was treating a compound start/initialization as a
transparent mid-body resume. The matched bit-7 effect is consistent with
FCS reinitialization; an undocumented universal bit name is not claimed.
The byte count and FCS accumulator need separate ownership/accounting.
No raw count is changed to 304 or otherwise normalized.

#### Unsafe partial export and explicit quarantine

Native GoodFCS exposed a second assumption: ordinary FIFO publication
uses native count minus six as the complete MPDU length. With count 176,
the shifted diagnostic consequently exported a 170-byte prefix of the
298-byte body. The full software FCS of that prefix fails. All four
complete shifted-good snapshots above therefore have failed delivery
integrity, not successful pairs. These four complete malformed exports
are independently checked and retained; software CRC fields calculated
over them are not mistaken for valid on-air FCS. Original-destination
tails lack the expected MAC header and do not provide equivalent archived
export evidence; absence from the selected samples is not proof of absence
from the wire.

Modified-field profiles now add a PSM admission guard **after** preserving
the immutable second/copy snapshot and before ordinary FIFO capture.
For the matching nonzero retained owner, current epoch and one-shot
advance, native success must also have `count == expected_body_bytes+6`.
A mismatch retains native count/status/outcome in the snapshot, but
records explicit length-reject outcome 4 and increments the existing
length-discard counter before rearm. It cannot enter SRAM/F2 publication.
Matching native geometry follows the original path. Bad FCS, EOF/abort/
watchdog handling, non-advanced frames and unrelated owners/epochs are
unchanged.

An initial guard lacked the owner/epoch restriction: eight failed RF
trials over-rejected later ordinary traffic, with 32..105 length drops.
They are preserved, not used as reliability successes. The final guard
has emitted-instruction tests for eleven cases, including complete/short
geometry, mismatch, bad/abort/watchdog, no advance, missing/wrong owner
and both stale-epoch directions. Tests check R2 restoration, unchanged
native count/status and unchanged snapshot bytes. Existing native
missing-owner/epoch/length/immutable-snapshot regressions also pass.

Eight final RF trials cover original/shifted good long bodies, shifted
bad-last in both roles, and both short-continuation controls:

- All four retained good long bodies remain native GoodFCS/count 176,
  but produce exactly one explicit length reject each and no partial
  advanced F2 sample. Both shifted bodies still have full valid FCS bytes.
- Both deliberately corrupted retained second bodies remain native
  bad FCS/outcome 2, with zero length rejects and no advanced delivery.
- Both short controls skip replay and deliver exact 170-byte F2 pairs,
  with zero length rejects.
- All eight have valid delivery integrity and zero invalid-control
  packets. Ordinary controls are **118/128**, with all controls in four
  trials. Early/control misses still prevent strict qualification.

The guard is compiled only for the modified-field discriminators.
Default, prior full-field, quiet-only, stream-on and displaced held-scope
controller binaries remain byte-identical to their previous versions.
Seven invalid field/value/parent combinations reject compilation; both
original-destination and shifted new profiles build freestanding and pass
native ASan/UBSan suites.

#### Campaign accounting and boundary

The independent bitwise-CRC/fixture audit passes **34 RF trials,
136 full reports, 816 source records, 1,336 conserved captures and
34 actual capture-decoder rechecks**. It checks all complete first bodies,
all replay tails/full continuations, nine observed corrupted-last native
rejections, four malformed archived exports and 26 exact archived F2
samples. One intermediate bad-last trial retained source index 5 instead
of the intentionally corrupted index 2; it is explicitly not a bad-last
reception proof. The original failed collector summary is preserved.
Total ordinary controls are **399/544**, including the over-rejecting
guard trials; neither those losses nor the final residual misses are
hidden. Four short pairs exist across both guard revisions, but only one
has every ordinary control.

One argument-only setup failure and one BOOTSEL readiness failure submit
no source burst and are excluded from RF totals. A's USB interface was
claimed exclusively by an external process; testing stopped, B was
restored first, and the user released A before its restoration. Full
readback-verified originals were retained before each subsequent RF phase.
After final RF, both entire 16-MiB originals were restored, independently
reread, byte-compared and SHA256-verified: A `8cda7a00...23628`,
B `bc3f972d...21e2`. Both final serial-specific checks report BOOTSEL.
No RF writes followed final restoration.

This establishes the controlled mid-body FCS-state cause and safely
quarantines the newly reachable partial-export path. It does **not**
implement ordinary full-body FIFO publication from copy-local counters,
variable-length segmented admission, general receive reliability or HT
station activation. Next work must model the complete MPDU and its native
copy segments explicitly, or avoid the disruptive stream/replay handoff;
relabeling count 176 as a complete 298-byte receive is not a repair.

### Next-MPDU command and bounded RX recovery experiments (2026-10-20)

Follow-up to the vendor-reference hints distinguishes the command at a
**real MPDU boundary** from the mid-body replay studied above. Selected
instructions were independently decoded from the local packed vendor
image: the vendor changes its body-start field from `0x83` to `0xc3`
for subsequent subframes. This is evidence for a controlled experiment,
not an authoritative universal definition of register bit 12.

#### One-byte next-MPDU discriminator

`CLEAN_RX_HT_NEXT_FIELD` defaults to `0x83`; `0xc3` requires the bounded
advance/copy diagnostic. Only `OBSERVER_ADVANCE_COPY` uses this flag.
Initial prefix/body commands and all mid-body replay fields are unchanged.
New `reference-handoff-next-c3` and
`reference-handoff-defer-next-c3` targets match the existing ordinary
128-byte and deferred 384-byte copy profiles respectively.

Evidence is retained in `build/clean-rx/next-command/`. Each matched
receiver pair has the same 60,372-byte size and differs at exactly one
byte, file offset 57152 in PSM PC 3103. Decoding that instruction confirms
the sole change is field `0x83` versus `0xc3`. All four version-7 source
controllers byte-match the independently vendor-verified source.

| Good-source cohort | Trials | Exact retained/F2 pairs | Every ordinary control |
| --- | ---: | ---: | ---: |
| Ordinary threshold, `0x83` | 4 | 0 | 0 |
| Ordinary threshold, `0xc3` | 6 | 6 | 2 |
| Deferred threshold, `0x83` | 4 | 4 | 4 |
| Deferred threshold, `0xc3` | 4 | 3 | 2 |

The ordinary-threshold cohorts cover both badge roles and both body
orders; the `0xc3` cohort adds long-continuation repeats in both roles.
The `0x83` failures retain bad-FCS/abort continuations. Every retained
good `0xc3` continuation has the exact complete 170- or 298-byte body,
native GoodFCS, valid full software FCS and native count **176 or 304**
respectively. Exact matching advanced F2 bytes are archived. After-arm
control changes from `0x802` to `0x1802`; the streaming low bits remain
enabled. No replay, SHM destination splice, count normalization or
relaxed FCS/length gate is needed for these pairs.

This identifies a missing next-MPDU command-field distinction as a
causal repair for the bounded streaming handoff. It supersedes segmented
reconstruction as the necessary repair for these particular fixtures.
It does not establish larger-body, repeated variable-length aggregate
or receive-reliability qualification. One deferred `0xc3` trial never
retains an eligible first body; it is a failed trial, not an exact pair.
Ordinary control misses remain visible in the successful-pair trials.

Eight additional `0xc3` trials cover bad-first, bad-last, bad delimiter
and truncated body in both roles. Both retained corrupted second bodies
have exact corrupted bytes, native bad FCS/count 304 and no advanced
export. Both bad-first source frames have a linked native bad-FCS exit;
the retained good pair comes from a later source aggregate. Only the
B-role bad-first/bad-last trials satisfy the existing full negative-case
recovery predicate. The A-role recovery controls are incomplete.
Malformed-delimiter trials retain later good source indices 11 and 8,
not the deliberately damaged aggregate's second body. Truncated-length
trials do not advance because the next length fails the fixed fixture
gate. **Neither malformed nor truncated case qualifies a generalized
delimiter/EOF recovery path.**

The independent fixture/bitwise-FCS audit checks all **26 RF trials,
104 full reports, 624 source descriptors, 5,132 conserved captures and
41 exact archived F2 samples**. There are 17 exact pairs across all
cohorts, including later good PPDUs in negative trials. Ordinary controls
are **347/416**. No invalid-control packet is hidden or admitted.

#### Ordered flush and instrumented stuck-prefix recovery

New default-off diagnostics are implemented in `rx_recovery.h`:

- `CLEAN_RX_HT_ORDERED_FLUSH=1` runs an adapted vendor-ordered sequence
  at each rearm; value `2` restricts it to the existing one-shot rearm
  fault-recovery branch. Both require the passive timeline/recovery
  parent and exclude the separate host PHY-reset experiment.
- The sequence preserves FIFO-control high bits, selects low byte 2,
  clears the explicit copy length, issues `0x14` with a read barrier,
  clears SPR `0x048` bit 6, and waits for event 10 to deassert. This wait
  has a 65,535-poll bound. If it clears, the sequence writes crypto
  control `0x8300`, pulses `0x0309 -> 0x0300`, delays ten countdown
  iterations, issues a second barriered `0x14`, pulses SPR `0x1ec`
  `9 -> 0`, and returns to the existing RXCTL verification.
  Timeout records phase 4 and explicit PSM fault reason 9, then stops;
  it never bypasses the wait to claim recovery.
- `CLEAN_RX_HT_PREFIX_RECOVERY=1` instruments prefix entries and
  count-2/RX-active polls. Only that state persisting to a wrap-safe
  TSF elapsed time of at least 250 us triggers a recorded PHY-control
  `old | 0x000e` pulse, ten preservation instructions, restoration of
  the exact old value, and rearm. Trigger count, native count/status,
  saved PHY control and elapsed time are exported.
  The original prefix fallback is **1,023 polls**, not the 65,535
  assumed in the hints. This diagnostic deliberately uses 65,535
  polls so its TSF deadline has a larger fallback budget; this change
  is gated and is not a production timeout adjustment.

Diagnostic words occupy the existing rearm export's last 16 words,
mutually exclusive with the host-reset record. WND9 flags 16/32
identify ordered/prefix diagnostics; frame wrappers and normal RX
metadata are unchanged. The source-side fixture remains the unchanged
version-6 fixed 554-byte/two-body source, requiring argument `512`.

Evidence is retained in `build/clean-rx/rx-recovery/`. The initial
proactive profiles omitted the SPR `0x048` bit-clear; their failures
are preserved separately from the corrected `final-*` profiles.
The complete proactive sequence still stops at event 10 on five of
eight final trials, before reaching the crypto pulses. Across both
proactive revisions there are **ten bounded event-10 timeouts**.
This disproves treating the supplied sequence as an unconditional
drop-in rearm repair in the current clean RX environment.

The fault-only profiles preserve ordinary rearms:

| Final fault-only cohort | Trials | Natural recovery attempts | Early bodies / expected | Ordinary controls |
| --- | ---: | ---: | ---: | ---: |
| Ordered sequence | 4 | 0 | 32/32 | 61/64 |
| Ordered + prefix instrumentation | 4 | 0 | 31/32 | 60/64 |
| Software-fault injection + ordered sequence | 2 | Not a physical wedge | 16/16 | 32/32 |

The injected trials retain the deliberate software-latched busy fault,
show native RXCTL is actually clear at ordered-sequence entry, execute
one sequence to phase 3, then deliver all early/control frames in both
roles. They prove real-hardware execution and branch recovery, **not
repair of a physical `0x804` wedge**. Neither uninjected fault-only
cohort encounters that wedge. Two legacy-control trials retain native
rearm failures. Prefix count-2 observations occur, but **no natural
250-us prefix recovery pulse is triggered** anywhere in this campaign.
It therefore does not establish the cause of pre-prefix/zero-HT misses.

The recovery audit checks **31 RF trials, 186 full reports, 31 arm
reports and 744 complete source descriptors**. Twelve trials end in
controller errors and remain failed, including the ten event-10
timeouts and two legacy rearm failures. Among the 19 completed trials,
all **1,128 captures** are conserved, **142/152** expected early
packets have complete independently byte/FCS-checked archives, and
ordinary controls are **275/304**. These completed-only denominators
do not exclude the twelve failed trials from the campaign.
One unsupported-size collector invocation submits no source burst;
its empty report/stderr are retained, and the running fixtures are
recovered through a separately named explicit-512 capture.

Emitted-program ASan/UBSan tests cover flush write ordering/barriers,
ten-iteration drain, busy clear/stuck bounds, integration with the
one-shot first-fault path, prefix 249/250/251-us boundaries, wrong
count/inactive exclusion, TSF wrap, exact ten-instruction PHY pulse
and register restoration. Seven invalid gate combinations fail
compilation. Current default, `0xc3`, full-prefix and fault-only-prefix
controller validation builds byte-match their corresponding archived
images; the default also matches the pre-experiment controller.

Fresh complete originals were saved before the campaign. After the last
injected RF tests, both entire 16-MiB originals are restored, independently
reread, byte-compared and SHA256-matched: A `8cda7a00...23628`,
B `bc3f972d...21e2`. The authoritative final evidence is
`rx-recovery/last-restored-{A,B}-full.bin` and matching info/log files;
both devices are in BOOTSEL. No credentials were read or provisioned.

### Variable-length boundaries and initial stream state (2026-10-20)

The next small fixture generalizes body geometry before introducing an
N-body loop. `CLEAN_RX_HT_VARIABLE=1` selects source descriptor version 9;
all production gates and the earlier version-7/8 fixtures remain separate.
The shared source/controller validator reconstructs and compares the
entire 1,132-byte command, including unused zero tail. It explicitly builds
every body, payload, sequence, delimiter, padding and FCS rather than
moving a fixed 554-byte body and merely changing its advertised size.

Eight length pairs are rotated across the eight actual aggregates in
each 24-transmission burst:

`80/554`, `127/381`, `170/298`, `298/170`, `257/255`,
`479/554`, `80/80`, `321/173`.

Both orders and badge roles are exercised. A selector rotates which pair
is transmitted in source aggregate 2. Sixteen ordinary controls retain
their original per-order 170/298-byte sizes. Negative mutations affect
only aggregate 2, preserving later independent recovery controls.
Nonce profiles 4..19 encode the selector/order; other profiles remain
rejected by the version-9 builder. Source admission, controller command
validation, observer F2 classification, collector exact-pair correlation
and independent raw-source reconstruction use the same wire geometry.

The new observer path is still bounded to **one advance and one immutable
early slot**, not a descriptor ring. It retains the existing native
count/owner/epoch/GoodFCS checks and EOF/abort exclusions, rejects a busy
next-length register, bounds the first body to 44..480 bytes and the
continuation to 44..554 bytes, and requires:

`PSDU = 8 + align4(native_first_count - 6) + next_body_length`.

There is no count normalization, replay or destination splice. Odd first
bodies exercise padding and odd last bodies exercise complete software
FCS/F2 verification. The terminal snapshot still holds only the first
300 bytes of a continuation; larger-body integrity comes from the full
archived F2 sample, not reading beyond that snapshot. Bad-second exact
correlation explicitly refuses lengths exceeding its snapshot capacity.

#### RF results and the newly exposed dependency

Evidence is under `build/clean-rx/variable-handoff/`, with immutable RF
profiles `dev`, `stream-before`, `stream-after` and `stream64`.

- The full original-threshold geometry matrix has **32 trials**. Thirty
  retain an exact pair, but only **25/32** retain an exact pair from the
  deliberately selected source aggregate 2. A 554-byte first body exceeds
  early-slot capacity; later eligible pairs must not be counted as success
  for that source. One other cell also retains a later source.
- Native GoodFCS, native count and full F2 bytes are exact for retained
  odd-length pairs in both roles, including 127/381, 257/255, 255/257,
  479/554 and 321/173. For the 554-byte continuation the native count is
  **560**, with no normalization.
- **80/554 fails in both roles** at native threshold 128. The 80-byte
  first body has native count 86 and exact software FCS; the continuation
  times out at native count **474**, not 560. Before the boundary,
  `SPR0x004` is `0x0800` rather than the `0x0802` seen for streamed first
  bodies; after `0xc3` it is `0x1800`, rather than `0x1802`. A short
  80/80 pair succeeds in both roles. Length alone and `0xc3` alone are
  therefore insufficient to establish general continuation behavior.
- A matched one-instruction bit-1 OR immediately **before** `0xc3` still
  fails in both roles, now timing out at count **86**. Moving the same OR
  immediately **after** `0xc3` fails in both roles at count **474**.
  The late control value is not equivalent to the prior copy/stream state.
- Lowering only the normal native stream threshold from **128 to 64**
  produces an exact **80/554** pair at source aggregate 2 in both roles,
  with native count **560** and `0x0802` before the boundary. Four targeted
  trials retain four exact pairs, but only **three** select aggregate 2;
  the fourth selects a later 127/381 pair. Canonical 170/298 pairs also
  remain exact in two threshold-64 regression trials. Five of the six
  threshold trials fail the existing collector policy because of missed
  controls or the invalid-control evidence below. Its one policy pass
  selects the later 127/381 pair, not the targeted 80/554 source aggregate.

These observations isolate a dependency on **initial native streaming
state/history**. They do not establish the internal hardware mechanism,
prove a safe universal replacement threshold, or justify replaying a
small first body or adding its count to a continuation.

#### Negatives, losses and validation

Eight odd-length negative trials use selected 257/255 bodies in both
roles: bad first, bad last, malformed delimiter and truncated advertised
second length. Both bad-last trials preserve the exact corrupted 255-byte
body, native count **261**, native bad-FCS outcome 2, and no second-body
publication. Both bad-first trials preserve native rejection; only one
also has the required later pair/recovery controls. Only one bad-last
trial has its required later controls. Those **two** trials pass the
existing negative policy. Neither malformed-delimiter nor truncated-body
cell is promoted to qualified recovery; later good pairs do not repair
the deliberately damaged PPDU's qualification gap.

Across **all 50 RF trials** (not just successful cells):

- **200** full reports and **1,200** source descriptors independently
  match report/header CRC, source wire bytes/CRC, exact lengths, commands,
  HT-SIG, delimiter mutations, sequence and nonce.
- **2,530** total captures, including unrelated traffic, are conserved
  across controller/DMA/F2/host accounting.
- **39** retained pairs and **99** archived F2 bodies have independently
  checked complete expected bytes and software FCS; pairs also match
  native count/GoodFCS, owner and source index.
- Ordinary controls are **726/800**. One threshold-64 trial records
  **one invalid control**, zero duplicate controls, and fails delivery
  integrity despite having an exact target pair and all control-mask bits.
  No failure is hidden behind the exact-pair count.
- Only **13** trials pass the existing collector policy: eleven good
  and two negative. There is no all-rate/repeated-aggregate qualification.

`audit.js` and its passing `audit.jsonl` independently reconstruct every
source packet, compare complete F2 bytes/FCS and native terminal counts,
check every denominator and failure, and verify final flash restoration.
The 480-byte early slot and 300-byte terminal snapshot remain explicit
bounds, not inferred upstream staging capacities.

ASan/UBSan source tests cover **1,920** variable fixtures, both orders,
all pairs/indices/negative cases, payload/FCS/padding and exact command/F2
admission. Emitted-PSM tests cover **400** variable geometry cases:
44..480-byte first bodies, 44..554-byte continuations, low/high PSDU
mutations, busy delimiter, native GoodFCS, odd tails, register preservation,
and exact stream-probe instruction position/bit preservation.
Eight invalid gates/combinations fail compilation.
Fresh validation builds byte-match all four archived RF receiver images;
the source controller also byte-matches its RF archive. Default and
canonical `0xc3` builds byte-match their pre-extension controller images.

The matrix paused after a BOOTSEL readiness failure, without flashing
the next trial. The failed readiness log is retained separately. Badge B
was restored immediately; badge A required a user USB replug, then was
also fully restored/verified before further testing. After the final RF
trial both complete original 16-MiB flashes were restored, independently
reread, byte-compared and SHA256-verified:
A `8cda7a00...23628`, B `bc3f972d...21e2`.
The authoritative final proof is
`variable-handoff/final-restored-{A,B}-full.bin` with matching info/log
files; both badges are in BOOTSEL. No credentials were accessed.

**Next implementation boundary:** qualify short-first initial/continuation
copy-state handling and loss accounting, then replace one-shot early
retention with bounded per-body ownership, delimiter/EOF advancement and
transport backpressure. No N-body ring or generalized malformed/abort
recovery was implemented or qualified by this fixture extension.
D2 remains incomplete and HT advertisement remains off.

### Verified hint follow-up: SHM limits, short-first streaming and admission (2026-10-20)

The remaining useful hints were tested against the new size matrix rather
than assuming register meanings or extrapolating from 170/298. Evidence is
under `build/clean-rx/hints-shm/`.
`vendor-instructions.jsonl` records a direct seven-byte big-endian decode
of selected instructions in the local vendor image, including
`0x08aa: op 0x504, a 0x100e`, the stream/split writes, and delimiter-ready/
EOF polling. This verifies the cited predicates, not every semantic label
or claimed timing/capacity in the hints.

#### SHM-only proposal: tested and rejected for general reception

`CLEAN_RX_HT_SHM_ONLY=1` replaces only the PC-311 mid-body stream write
with a jump to the existing completion path. Initial commands, `0xc3`,
copy base/end, native count/FCS checks and final header publication remain
unchanged. The matched variable fixture was run in both badge roles:

| Selected first / second | Native first count | Terminal continuation count | Result in each role |
| --- | --- | --- | --- |
| 80 / 554 | 86 | 426 | Abort |
| 127 / 381 | 133 | 382 | Abort |
| 170 / 298 | 176 | 304 | Exact pair |
| 257 / 255 | 263 | 250 | Abort |
| 479 / 554 | 485 | 26 | Abort |
| 321 / 173 | 327 | 179 | Exact pair |

Thus all eight long-case failures reproduce with matching native counts
across both roles. The failing cumulative counts are 512, 515, 513 and
511; exact smaller pairs total 480 and 506. This is evidence of a bounded
upstream resource near that region, **not** proof of an exact universal
512-byte FIFO model: alignment/prefix bookkeeping and the internal
mechanism remain unresolved. The 4-KiB SHM address window does not remove
this limit. The hints' claim that disabling streaming permits full bodies
through 3,896 bytes is therefore not adopted.

#### Implemented: short-first-only streaming and delimiter predicate

`CLEAN_RX_HT_SHORT_STREAM=1` uses a bounded, register-preserving PSM
threshold dispatcher. Only a pending **first aggregate body** with current
copy length 64..121 selects native count 64. It explicitly checks pending
ownership, no prior advance and the aggregate HT-SIG flag. All other
cases retain 128: ordinary/legacy frames, single HT frames, larger first
bodies and continuations. The 64-byte minimum remains beyond the required
MAC/CCMP prefix. Bodies shorter than 64 are not generalized by this change.
The initial command, continuation `0xc3`, copy geometry, replay and FCS
policy are unchanged; there is no counter reconstruction.

This makes the earlier global-64 diagnostic more surgical. Four targeted
80/554 trials of the isolated short-first path select aggregate 2 and
deliver both bodies exactly in both roles/repeats, with native count 560.
Its six canonical/80-byte regression trials also select exact pairs.
Ordinary losses remain visible rather than being inferred away from those
ten pair successes.

`CLEAN_RX_HT_DELIMITER_GUARD=1` adds the vendor's `SPR0x00e` bit-4 exclusion
at the actual next-body admission point, in addition to busy/length/
padding/EOF/abort/GoodFCS gates. It is verified by direct vendor decoding
and emitted-program fault tests with valid next length/native GoodFCS.
Real malformed-delimiter tests still show no bit 4 in their **terminal**
status-2 snapshots. Do not claim that a real latched CRC-error trigger or
general malformed recovery was demonstrated. Invalid/null delimiter
handling beyond this bounded two-body fixture remains open.

#### Implemented: reject short native counts even when GoodFCS is set

A pre-fix `short-guard-A-128-p1` trial retains first body 127 and expected
continuation 381, but native terminal count is **243 instead of 387**,
with native status 1 `0x4100` (GoodFCS/EOF). The old copy admission accepts
this and publishes a **237-byte** advanced F2 prefix with invalid software
FCS. Its original source, snapshot, raw native status/count and invalid
F2 bytes are retained. This is a failed trial, not an exact-pair result.
It exposes a safety gap even without modified mid-body replay.

The existing owner/epoch-aware native-length quarantine now applies to
**all variable handoff profiles**, not just altered-replay profiles.
An advanced, owned GoodFCS completion must have native count
`saved_next_length + 6` before FIFO admission. Otherwise outcome 4 and
the existing length-rejection counter are recorded, without changing
native count/status or rewriting the earlier immutable snapshot's native
outcome. There is no software-FCS waiver or success-shaped fallback.
This reuses the established quarantine rather than adding another
publication path.

Emitted-program tests exercise exact counts and one-byte short/long,
stuck count 6 and the real 243-count failure across seven lengths:
**35** admission cases. All mismatches reject before FIFO; native
registers remain unchanged and snapshots preserve native data when their
bounded geometry permits a snapshot. Good exact-length bodies still pass.
No natural copy-count fault happened in the final RF matrix, so rejection
of that fault is proven by these emitted-program cases, not presented as
a physical-wedge recovery result.

#### Final build, complete campaign and hardware restoration

The public `reference-handoff-safe-variable` target selects short-first
streaming plus the delimiter-status predicate; every variable build gets
the native-length quarantine. These remain opt-in diagnostics. RF images
are frozen under `safe-short/`.

The final matrix is **24 trials**:

- **16/16 selected good pairs**, all eight length pairs in both roles,
  with exact source index 2, complete bytes/software FCS, native GoodFCS
  and native continuation count. This includes 80/554 at count 560 and
  127/381 at count 387.
- Eight negative trials retain bad-first/bad-last rejection. Four pass
  the existing negative/recovery policy; malformed/truncated cases remain
  unqualified even where later source pairs arrive.
- **364/384 controls**, **837** conserved captures, zero invalid or
  duplicate controls. Only **10/24** pass the existing collector policy
  (six good, four negative); the exact-pair count is not reliability.
- No malformed archived F2 sample in the final profile. The malformed
  pre-fix prefix remains separately auditable.

Across **all 64** exploratory/final RF trials: **256** full reports,
**1,536** verified source records, **4,513** conserved captures,
**944/1,024** controls, **47** retained exact pairs and **126** exact
archived F2 bodies. The **one malformed archived F2 sample**, two
delivery-integrity failures and all control misses remain failures.
There are **20** existing collector-policy passes and seven qualified
negative-policy results, not 64 reliable receptions.

`audit.js` adapts the earlier independent fixture reconstruction with
explicit allowances only to **record** pre-fix malformed output and
non-exact native snapshots. It prohibits malformed archived output in
the final profile and preserves all denominators. Its passing
`audit.jsonl` also verifies raw report CRC/build/nonce/source geometry,
sample FCS/native count/owner, conservation and original-flash restoration.

ASan/UBSan builds test 1,920 source fixtures, **480** geometry/delimiter/
GoodFCS cases, the 35 native-admission cases and **44,880** exhaustive
short-first threshold/context cases. Ordinary and continuation thresholds
are tested unchanged. Default and the canonical non-variable `0xc3`
receiver images remain byte-identical to the earlier archives; both final
receiver/source controller binaries byte-match the final RF archive.
The final default comparison uses
`variable-handoff/validation-default/cyw43_rx_fw.bin` as its archive.
All 21 invalid-value, missing-parent and conflicting-gate compile cases
fail through the intended static assertions; `git diff --check` passes.

After 40 exploratory trials both full originals were restored. The final
matrix then paused on badge B's BOOTSEL readiness failure, before flashing
the next trial. Badge A was fully restored immediately; after the user's
USB replug, badge B was fully restored/verified before resuming.
Following the last trial both original 16-MiB flashes were again restored,
independently reread and byte/hash checked, with BOOTSEL confirmed.
Authoritative **last** evidence:
`hints-shm/last-restored-{A,B}-full.bin` and matching info/log files,
A `8cda7a00...23628`, B `bc3f972d...21e2`.
Earlier `final-restored-*` files precede the last RF matrix.
No credentials were read.

The hints' already-tested `0xc3` lead remains useful; ordered-flush and
250-us prefix recovery keep their earlier physical-recovery limitations.
Count normalization, presumed unlimited SHM-only capture, nonexistent
descriptor-ring symbols and the suggested incorrect MAC/BA offsets are
not adopted. N-body ownership/backpressure, all-rate receive reliability,
malformed/abort recovery and HT MAC/BA obligations remain open. HT
advertisement stays disabled and D2 is not complete.

### Remaining D2 work

Use the [consolidated D2 work packages](../../todo.md#remaining-work-packages)
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
The variable-length extension above proves selected odd/long-body pairs
and exposes a short-first initial-stream-state dependency; it does not
remove the one-shot/480-byte early-slot boundary or qualify repeated
N-body buffering and backpressure.

The isolated retained-SHM/SRAM/F2 diagnostic now delivers all three actual
MPDUs at every MCS0..7 in both directions, without duplicates in its finite
matrix. Controlled bad first/middle/last frames are rejected while other
MPDUs reach the badge. See the
[full delivery evidence and limitations](AMPDU_INTERFACE_INVESTIGATION.md#retained-shm-to-f2-delivery-2026-10-19).
This does not resolve the separate DMA/EOF experiment, generalize the
three fixed slots, or implement Immediate BA or mandatory protection.
The subsequent
[explicit terminal-ownership campaign](AMPDU_INTERFACE_INVESTIGATION.md#explicit-terminal-ownership-2026-10-19)
replaces ordinary held-receive polling with a PSM state-16 commit/claim/
release, including bad-last/all-bad controls. Copying no longer depends on
a later good ordinary frame. Failed/partial RF receptions remain visible;
the measured local TSF lease intervals are not qualified EOF-to-air SIFS.

The subsequent default-disabled
[compressed BA return campaign](AMPDU_INTERFACE_INVESTIGATION.md#diagnostic-compressed-ba-return-2026-10-19)
adds a real PSM BA response and sender turnaround for the fixed fixture.
All normal MCS/direction cells now have a complete delivery plus actual
matching on-air return after repeats; 28 returned BA frames are
independently validated across 48 trials, including one partial normal
response. Single-bad controls exclude the invalid body's receipt bit.
Bad-final reverse MCS7 remains unqualified after four no-capture trials.
Command 17 arms this diagnostic, not a production agreement. The hardware
SIFS control selection and observed IFS timestamps do not independently
qualify RF EOF-to-air SIFS. HT advertisement remains disabled.

- Integrate the [native-tested Block ACK/reordering software](BLOCK_ACK_IMPLEMENTATION.md)
  with actual PSM aggregate boundaries, retained multi-MPDU capture and
  hardware-timed Immediate BA; wire station agreement/key lifecycle before
  enabling negotiation.
- Qualify [AP-driven non-HT WMM/EDCA integration](EDCA_IMPLEMENTATION.md#ap-driven-non-ht-wmm-integration)
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
