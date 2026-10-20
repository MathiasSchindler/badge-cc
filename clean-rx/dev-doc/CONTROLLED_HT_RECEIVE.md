# Controlled independent HT reception

## Scope and result

This campaign separates clean receive behavior from the clean transmitter.
An RP2350 companion application uses the existing, unchanged vendor loader,
firmware assets and AP configuration. It sends a finite sequence of ordinary
multicast data frames. The other badge runs the clean firmware in receive-only
diagnostic mode.

Small, fresh HT20 frames were received at MCS0 and MCS7 on both physical
badges. The final 16 cold-boot trials verified 120 of 128 submitted frames,
with no duplicate fixture delivery. Each accepted frame passed the complete
payload/address/sequence/FCS predicate on the receiving badge; each successful
trial also retained a fresh, FCS-valid terminal MPDU and valid matching HT-SIG.
This is independent small-frame interoperability evidence, not general
receive reliability, a corruption fix, or completion of D2.

The mandatory negotiated HT MAC path remains unimplemented: actual A-MPDU
reception, Immediate Block ACK agreements/responses, bounded reordering and
their protection/security/receive-size integration are still required.
`clean_ht_peer_fallback()` retains its unconditional MAC fallback, and clean
association still omits HT. No negotiated HT gate was relaxed.

## Subsequent vendor-station/AP reference

The separate [focused vendor burst experiment](../vendor-burst/README.md)
now joins the approved AP using unchanged vendor firmware and sends 128
known unicast fixtures of varied lengths while the other badge remains
strictly receive-only. Both role assignments show actual vendor received-BA
counter increases (103/97), together with independent CRC-valid HT-SIG
A-MPDU-format prefixes (6/5). Raw snapshots and unsupported-query statuses
are retained. This establishes a usable live reference, not complete
multi-MPDU body delivery, prefix transmitter attribution or SIFS timing.

The new prefix logger deliberately avoids the stale MAC/prefix bytes seen
at the early PC153 handoff. A subsequent default-disabled lifecycle logger
now correlates fresh RX epochs and HT signatures with terminal MAC snapshots,
the exact F2 publication ID and complete ciphertext/FCS bytes. Final build
`4e209642` independently verifies MPDU lengths 170/298/554 in A/B and
170/298/1066 in B/A, including correlated A-MPDU-format receptions.
The balanced fixture rotates the first size of four-frame groups.

This closes prefix transmitter attribution for those saved samples, not
general reception reliability or multi-MPDU delivery. Larger live PSDUs
(2110/862 bytes) still have no observed terminal commit; one-size controls
are retained as failures of the variable-length criterion. General
receive/admission/lifecycle integration and clean HT advertisement remain
gated; see the focused experiment's full result and limitations.

The subsequent passive exit investigation records explicit length/short,
abort/fault, watchdog and RX/prefix replacement exits before ownership is
cleared. Build `95e7676c` observes 1598/1342-byte A-MPDU-format prefixes
reach event 6 and the good-only status1 bit-8 hint, then take the existing
abort/fault path at RX counts 560/304, with status1 `0x9922`.
These prefixes have no retained complete body/FCS and remain unattributed;
the observations do not prove an advance primitive, FIFO/DMA root cause
or absence of physical EOF.

Build `7f1e3e90` retains first-hint RX counts and passes five bounded
variable-length controls across both roles with zero unaccounted prefix
exits. Those controls do not reproduce the larger aborts. A zero-prefix
control remains a failed qualification. Final build `d7519182` adds native-
tested short-completion exit accounting, without another RF trial.
See the focused experiment for raw evidence, per-trial counts and ABI 4.
No early next-frame advance, same-PPDU multi-MPDU delivery, production BA
or negotiated HT gate is enabled by this investigation.

The next focused build, `e9008554`, preserves up to 1000 complete early
MPDU bytes in separate SHM, with commit-last prefix identity and native
tests for every length 44..1000, memory bounds, scratch preservation,
stale copies and a status transition after the hint. Frozen readout still
occurs only after the controller and PSM stop. Oversize candidates are
counted, not truncated; ordinary receive sizes are unchanged.

In `build/clean-rx/vendor-early/hint-ba-r0`, vendor B/passive A retains a
complete FCS-valid 170-byte MPDU from fresh prefix ID4/epoch74, whose
CRC-valid HT-SIG advertises a 478-byte A-MPDU-format PSDU. Hint status1
is `0x1922`, with good-only bit8 set and terminal/abort bits clear.
Ordinary 170/298-byte correlation and received-BA delta101 also pass.
This is one concrete early-body preservation result, not two delivered
MPDUs: the second body's bytes/FCS were not captured. Copy completion
before physical EOF and RF/SIFS timing are not established.

The other five final-build controls fail early qualification, including
all A/B attempts; two also retain only one ordinary MPDU length. Earlier
complete snapshots with a late terminal status read correctly fail, and
are retained as instrumentation controls. Repeated/both-role early-byte
capture and a justified single next-body handoff remain unqualified.
The implementation is default-off, MCS0-only and does not advance RX,
acknowledge early events, transmit BA or enable negotiated HT.

## Independent peer

The companion lives in [peer/](../peer/). Its build includes the unchanged
root build rules rather than editing shared/vendor applications. Firmware
remains freestanding C with no undefined runtime symbols. Vendor binary
assets are used only by this independent test application, not introduced
as a dependency of the clean radio firmware.

The production vendor firmware was queried before selecting a TX method:

| Operation | Result |
| --- | --- |
| `nmode`, `nrate`, `2g_rate`, `mimo_bw_cap` GET_VAR | Supported |
| `pkteng_stats` GET_VAR | Controller status `-23`, unsupported |
| `pkteng` TX-stop request | Controller status `-23`, unsupported |
| GET_MONITOR | Supported; returned zero |

There was no packet-engine start request, manufacturing firmware, continuous
tone, count-zero transmission, or arbitrary PHY/radio-register experiment.
GET_MONITOR support alone does not establish working monitor reception.
The later [independent raw receive campaign](HT20_IMPLEMENTATION.md#independent-vendor-raw-receiver-2026-10-20)
actually enables/readback-checks/disables monitor mode and retains 16
complete controlled pairs with both FCS values, across both roles/orders.
That independent receive role does not use this peer's AP/TX configuration.

The alternative uses the existing open-AP configuration on channel 1 and
sets interface 1's `2g_mrate` to `0x01010000 | MCS`. This requests HT20,
one spatial stream, BCC and long GI. The vendor readback sets the documented
rate/mode override bits, returning `0xc1010000 | MCS`. Only those two override
bits are ignored for comparison; bandwidth, encoding, MCS, SGI, STBC and
coding bits must otherwise match. A setting/readback is not OTA proof.

The peer sends exactly eight 62-byte Ethernet frames at 100-ms target
intervals, starting after a 300-ms delay, then allows a bounded drain period
and powers off the radio. Default vendor calibration/power policy is retained.
The AP MAC, SSID and payload are bound to a fresh challenge. No local AP
credential is read or used.

The 62-byte Ethernet size makes the existing SDPCM transfer length, including
its 18-byte header, exactly four-byte aligned. An initial 64-byte fixture
produced two trailing zero bytes before the MPDU FCS. Those captures had
valid FCS but failed the initial exact-length diagnostic; they were not
evidence of RF corruption. The final fixture has no alignment tail.

### Primary interface references

Interface constants and rate fields were checked against the public Infineon
WHD header and public DHD definitions, not guessed from successful commands:

- [Infineon WHD wlioctl definitions](https://github.com/Infineon/wifi-host-driver/blob/master/WHD/COMPONENT_WIFI6/src/include/whd_wlioctl.h)
- [Public DHD ioctl/ratespec definitions](https://github.com/StreamUnlimited/broadcom-bcmdhd-4359/blob/master/include/wlioctl_defs.h)
- [Public DHD packet-engine structure](https://github.com/StreamUnlimited/broadcom-bcmdhd-4359/blob/master/include/wlioctl.h)

These references describe interfaces, not identification of the badge's
exact radio version/revision. Actual command availability was checked on
the supplied production firmware.

## Clean receiver and evidence

The clean badge collector's `--vendor-ht-rx CHALLENGE_HEX_28 MCS` selects
receive-only diagnostic mode 29. It does not enable an automatic ACK,
station association or clean TX.

The fixture predicate accepts plain or QoS From-DS broadcast data, requires
the fresh AP transmitter/source addresses, complete SNAP/EtherType and
payload, sequence 0..7 and the full MPDU FCS. A sequence bitmap and duplicate
count are recorded separately. HT metadata must identify the requested
MCS/channel, and a fresh terminal trace must have the matching valid
HT-SIG and complete valid fixture. Normal fixtures are 84-byte MPDUs;
the corresponding QoS form is 86 bytes. Native tests cover both forms,
wrong-session data, malformed lengths/addresses/payloads and bad FCS.

The receiver listens for 4.2 seconds. The runner starts the vendor peer six
seconds after starting the clean collector to accommodate clean cold
initialization. This is a bounded small-frame investigation, not a
synchronized full reliability/throughput test.

Reports deliberately distinguish:

- Vendor command/readback and eight submitted Ethernet frames.
- Fresh, full-FCS HT reception on the independent receiver.
- A pair's confirmed OTA transmission.
- HT interoperability qualification, which remains `false`.

The pair runner returns success for a verified fresh reception, not for
eight-of-eight reliability. Use the frame count and bitmap for loss.
Global bad-FCS/PLCP/RX-event counters also include unrelated channel traffic;
do not attribute all of them to the eight addressed fixtures.

## Final cold-boot matrix

All trials used the same final images, channel, size and rate policy,
with a fresh challenge and cold firmware initialization for each run:

- Vendor companion build `af68969e`.
- Clean gated receiver build `992f849e`.
- Clean controller build directory `build/clean-rx-d2-controlled/`.
- Gated flags: `CLEAN_RX_HT_DATA=1`, `CLEAN_RX_HT_RX=1`.
- A: `08DA26F86E57F5EA`; B: `9BBF88DD17D66E7D`.
- Evidence prefix: `build/clean-rx/peer/verified-{AB,BA}-mcs{0,7}-round{1..4}`.

| Vendor TX -> clean RX | MCS | Valid frames by cold boot | Total |
| --- | --- | --- | --- |
| A -> B | 0 | 7, 8, 7, 7 | 29/32 |
| A -> B | 7 | 7, 8, 8, 8 | 31/32 |
| B -> A | 0 | 8, 8, 8, 8 | 32/32 |
| B -> A | 7 | 8, 4, 8, 8 | 28/32 |

All 16 vendor records completed with eight submissions and no controller
error. All 16 clean records completed without a controller error, with a
fresh terminal trace and zero duplicate fixture deliveries. All pair
collectors completed normally after the shutdown fix below.

Missing sequences were:

- A -> B, MCS0 rounds 1/3: sequence 0; round 4: sequence 1.
- A -> B, MCS7 round 1: sequence 2.
- B -> A, MCS7 round 2: sequences 0, 2, 3 and 5.

Loss is therefore not explained solely by an early first-frame/window
boundary. This campaign does not isolate interference, analog behavior,
RX discard handling and scheduling as causes of the remaining loss.
It also does not determine why earlier clean-to-clean HT exchanges failed.
Successful vendor-to-clean reception must not be used to assert that clean
HT TX is faulty or that clean HT RX is universally correct.

## Harness defects and discarded hypotheses

The first companion requested a 15-second ROM recovery delay. The shared
helper permits at most eight seconds, so that request failed before USB
initialization. The user manually returned A to BOOTSEL; its full backup
was already verified. The corrected application arms an eight-second
recovery timer and services it during a bounded 16-second test/collection
session.

Several early runs appeared to lose vendor reports at the host deadline,
despite good clean RX. Their complete 320-byte report/trace files had
actually already been saved. The failure occurred in BOOTSEL cleanup:
the companion disconnected USB immediately after observing the reboot
request, unlike the shared application's 10-ms status-completion grace.
Buffered JSON was then lost when the host deadline handler used `_exit`.

The companion now polls USB through that same bounded 10-ms shutdown grace.
The host explicitly flushes JSON before requesting BOOTSEL. The final
16-run directional matrix had no collector failures. Earlier timeouts and
their evidence were retained, not relabeled as RF failures or quietly
turned into successful pair records.

Reducing report polling did not eliminate the early failure. A temporary
mailbox-retention probe also yielded only zeroed SRAM after BOOTSEL.
That instrumentation was removed; neither is claimed as an RF fix.

## Native and default regressions

Both default and HT-gated controller/badge native suites and freestanding
builds passed. The peer's ASan/UBSan tests check request/reply budgets,
TX-stop encoding without a start bit, nonce/build/CRC binding, explicit
controller failures, allowed override bits, rejected rate-mode changes,
and exact data/MPDU/FCS matching. All emitted ELFs pass undefined-symbol
checks.

Default badge build `2dde5b56` also passed:

| Test | Verified deliveries/ACKs per direction | Attempts | Integrity/errors |
| --- | --- | --- | --- |
| OFDM6, 32/1,500/2,304-byte MSDU cycle | 100/100 | A 104, B 102 | Zero corruption, duplicate/out-of-order delivery or controller errors |
| Long-preamble CCK1, 1,500-byte MSDU, B initiating | 32/32 | 33/33 | Same |

Each endpoint deliberately suppressed one ACK in each regression. These
tests preserve legacy behavior; they do not repeat or resolve maximum-sized
1-Mbps reception.

## Reproduction

Take and verify complete flash backups before loading either image.
The build and runner do not create those backups or automatically restore
them. Do not use the runner without an independent restoration plan.

From the repository root:

```sh
make -f clean-rx/peer/Makefile ht-peer ht-peer-test
make -C clean-rx/src CC=clang \
  BUILD=../../build/clean-rx-d2-controlled \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1' all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-controlled \
  CONTROLLER_BIN=../../build/clean-rx-d2-controlled/cyw43_rx_fw.bin \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1' all test
```

Install `build/clean-rx/peer/vendor_ht.uf2` on the independent vendor TX
badge and `build/clean-rx/badge/d2-controlled/clean_rx.uf2` on the clean
RX badge, using verified writes. Leave both in BOOTSEL, then:

```sh
bash clean-rx/peer/run.sh TX_SERIAL RX_SERIAL NEW_EVIDENCE_PREFIX 0
bash clean-rx/peer/run.sh TX_SERIAL RX_SERIAL ANOTHER_NEW_PREFIX 7
```

Optional executable overrides are `CLEAN_VENDOR_PEER_COLLECTOR`,
`CLEAN_RX_COLLECTOR` and `CLEAN_RX_PICOTOOL`. The last can wrap only
`info`/`reboot` with bounded timeouts; never interrupt a healthy flash
write, verification, save or restoration.

For the non-transmitting interface probe, start the vendor companion and
run `build/clean-rx/peer/collector SERIAL NEW_EVIDENCE_BIN`. The probe only
reads configuration and issues the explicit packet-engine TX-stop request.
It reports unsupported commands individually, rather than silently
treating them as supported.

## Preservation

Fresh verified originals for this campaign are
`build/clean-rx/badge/d2-controlled-start-{A,B}-full.bin`, each
16,777,216 bytes. Full restoration and independent full-flash readback
are recorded in `build/clean-rx/peer/{restore,readback}-{A,B}.log` and
`restored-{A,B}-readback.bin`.

Both complete originals were restored with write verification. Independent
full-flash readbacks were each 16,777,216 bytes and byte-identical to their
fresh starting backups. SHA-256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`.
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`.

Both badges were left in BOOTSEL with their original full-flash contents.
Temporary downloaded interface references and the external bounded-picotool
helper were removed. No shared/vendor application source, host network
configuration or credential file was changed. No commit was created.
