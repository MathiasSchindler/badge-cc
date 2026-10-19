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

[psm_ucode_test.c](../src/psm_ucode_test.c) verifies formats 2/3 do not
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
- [collector.c](../badge/collector.c) exposes the unadvertised target IE
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

### Remaining D2 work

Use the [consolidated D2 work packages](../../todo.md#remaining-work-packages)
for the current checklist. The detailed qualification boundaries follow.

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
