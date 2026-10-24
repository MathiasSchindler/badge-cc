# HT20 packet, receive and response APIs

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## HT data API version 1

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

## HT/WMM peer profile and legacy fallback

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
The [association/runtime extension](HT20_PEER_POLICY.md#association-response-profiles-and-runtime-htwmm-updates)
uses the remaining four bytes before offset 1088 for a change counter.

## Extended receive profile and minimum A-MSDU diagnostic

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
[memory map](../../doc/HARDWARE_AND_FIRMWARE.md#memory-layout).

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

## Per-TID QoS receive and CCMP integration

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
[protected A-MSDU integration](../experiments/mac/EDCA_AND_WMM.md#protected-a-msdu-receive-integration)
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

## Gated QoS/HT normal-ACK response prototype

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


