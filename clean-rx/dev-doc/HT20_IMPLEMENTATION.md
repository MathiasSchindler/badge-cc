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
| Buffers/aggregation | [frame_limits.h](../src/frame_limits.h) retains the default 2,400-byte MPDU bound; the opt-in extended RX profile supports 3,896 bytes and three F2 fragments | Minimum A-MSDU validation/deaggregation is implemented in the diagnostic path below. D3 still needs bounded A-MPDU parsing, per-MPDU CRC, BA negotiation/BAR/window/reordering and negotiated delivery/replay integration. F2 fragmentation is neither 802.11 fragmentation nor aggregation. |

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
fragmented aggregates are invalid. Protected A-MSDUs remain disabled until
negotiated crypto/aggregate-injection handling is implemented.

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
layout and sums legacy plus QoS counters. State is static: default and
extended badge BSS is 221,256 and 225,744 bytes respectively; no allocator
or new library dependency was added.

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

### Remaining D2 work

- Integrate the [bounded EDCA command](EDCA_IMPLEMENTATION.md) with actual
  AP negotiation and runtime updates; implement HT protection and qualify the gated ACK
  prototype's returned responses, SIFS timing and EOF anchoring.
- Qualify the implemented per-TID receive/CCMP state over the air and
  integrate negotiated protected aggregates; preserve legacy fallback.
- Meet HT receive-size and required MAC/Block ACK obligations jointly with
  D3 before constructing/advertising an HT association profile.
- Use the implemented association-response/runtime profiles for actual
  local capability/operation negotiation and AP-driven policy activation;
  profile capture alone does not enable negotiated HT operation.
- Verify negotiated association and actual HT OTA framing with an independent
  HT20 implementation, including explicit legacy fallback.
- Diagnose remaining RX losses; no all-rate reliability threshold is passed.
- Extend shared-medium access/operand-visibility qualification beyond the
  bounded counter-repair trials; retain the fail-closed guard and evidence.
