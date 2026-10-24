# Freestanding badge host for clean-rx

This opt-in RP2350 application cold-loads the source-built CYW43439 image,
supports raw 802.11 transmission and Function-2 reception, and reports evidence over the
project-owned USB control endpoint. Neither processor links libc, the Pico
SDK, a downloaded vendor executable, NVRAM, or CLM. The controller still
reads its on-chip ROM calibration tables.

This is a radio diagnostic, not a production WiFi connection manager. It includes an
experimental WPA2-PSK/CCMP supplicant and protected round-trip trial, but does
not replace the default networking targets. Host USB collection uses the
repository's existing libusb dependency.

The isolated `BADGE_PICO=1` option builds a headless, receive-only Pico 2 W
HT observer, not a general-purpose badge application for that board.
It admits only mode 25 and uses the Pico system-clock helper; the default
badge build and its TFT behavior remain unchanged. Follow the
[observer build/evidence procedure](../dev-doc/guides/HOST_TOOLS.md#passive-independent-witness)
and never cross-flash board images.

## B1 persistent open-station lifecycle

The opt-in `--station-lifecycle BSSID SECONDS` mode maintains an open,
legacy station for 1..600 seconds. It scans channels 1..11, authenticates,
associates, exchanges session/epoch/sequence-bound application echoes, and
recovers after beacon loss, validated departure, channel changes or three
consecutive failed data submissions/echoes. Authentication and association
retain four actual attempts and a 1.6-second phase deadline. Scan dwell is
400 ms, reconnect backoff is 250 ms, and beacon loss uses the greater of
1.5 seconds or ten advertised beacon intervals. Each operation is bounded;
an operation already in progress may finish after the session deadline.

Recovery clears association, duplicate/reassembly, QoS and security state
and cold-reloads the controller before scanning again. Existing one-shot
station/WPA2 modes are unchanged. B1 does not negotiate WMM/HT or implement
persistent WPA2; secured/unsupported networks are explicitly rejected.
Unknown controller/PHY faults remain fatal rather than becoming retries.

Build the badge and the independent vendor-radio AP fixture:

```sh
make -C clean-rx/badge all test
make -f clean-rx/peer/Makefile ht-peer ht-peer-test
node clean-rx/tools/badge/lifecycle_verify.cjs --self-test
```

After backing up both full flash images, install the matching badge image
and `build/clean-rx/peer/vendor_ht.uf2` on the station and AP respectively.
Leave both in BOOTSEL, then run from the repository root:

```sh
bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX 0
bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX 5
```

The runner never flashes or restores firmware. It boots the installed images,
waits for AP readiness and retains both raw evidence files and endpoint logs.
`CLEAN_RX_COLLECTOR`, `CLEAN_RX_PEER_COLLECTOR` and `CLEAN_RX_PICOTOOL` select
matching isolated builds/tools. Cycle zero is one cold connection; five cycles
physically power down/reload the AP five times, with three-second outages.
The AP uses unchanged vendor firmware on channel 1 and independently checks
its associated peer before returning the exact application payload.
Its host CLI is
`collector SERIAL EVIDENCE_BIN --lifecycle-ap CHALLENGE_HEX_28 SECONDS:8-120 CYCLES:0-5`.

The station collector reports connections separately from data-verified
connections, actual data TX attempts/timeouts/busy results, phase failures,
disconnect reasons and the last 32 state transitions. Local TX completion
and MAC ACKs never count as application data. The paired oracle additionally
requires matching independent MAC/session/epoch evidence, exactly one
connection per planned AP lifetime and no hidden authentication/association
failures. SIGINT/SIGTERM requests BOOTSEL cleanup; cancellation is not reported
as successful qualification. Always restore and full-readback-verify originals
after a hardware campaign. Implementation and RF qualification are tracked
separately in [the roadmap](../../todo.md#b1-connection-lifecycle).

### B1 qualification record

The `build/clean-rx/b1-20261022/calibrated-*` two-badge campaign passed ten
consecutive fresh cold connections and five actual AP power-loss/reload cycles.
Station build `b886bf0a` and independent AP build `2478041e` verified 115
application echoes across the cold connections, then 68 echoes across six
distinct recovery epochs with five beacon-loss recoveries. Every association
had fresh application evidence; authentication/association phase failures
remained zero.

Earlier failed attempts are retained, not counted as passes. They exposed
the USB framework's intentionally one-shot snapshots and the AP fixture's
uncalibrated ROSC timer. B1 reopens only the same configured USB generation
for progress/final snapshots, and normalizes the AP clock using the unchanged
badge clock helper. A USB-SOF check rejects timer drift beyond 1% before AP
startup. Other peer modes retain their previous clock setup. An archive-oracle
assertion initially read reserved footer bytes rather than the station MAC;
the corrected oracle rechecked all eleven retained raw pairs successfully.

These are not loss-free data tests: the ten cold controls retained seven
MAC timeouts, six echo timeouts and thirteen busy returns. Recovery retained
its outage-related losses too. A final corrected-runner cold smoke passed with
13 echoes, and cancellation after independently observed connected data
stopped both collectors and returned both badges to BOOTSEL. Departure and
channel-announcement handling have native coverage; the five recovery cycles
kept the AP on channel 1. Persistent WPA2 and negotiated HT/WMM remain open;
continuous data has the separate B2 profile below.
Default, WMM/HT-response and HT/SHM badge builds and their native regression
suites pass. Vendor firmware, shared platform code and the Pico are unchanged.

## B2 continuous static-address networking

Mode 33 (`--station-network BSSID SECONDS`) reuses B1's legacy/open station
lifecycle for 10..3900 seconds, but exchanges Ethernet ARP and IPv4/UDP rather
than the B1 echo EtherType. This is a deliberately small diagnostic network
path, not a general-purpose IP stack or production networking API:

- AP `192.0.2.1`, station `192.0.2.2`, UDP port `30526`; addresses are static.
  No DHCP, routing, IP options/fragmentation, VLAN, WPA2 or negotiated HT/WMM.
  The fixture stays on channel 1, with legacy rate selection inherited from
  B1 and the vendor AP; this is not an all-rate MAC qualification.
- Each endpoint independently originates requests at up to five per second,
  alternating 48-byte and 1472-byte application payloads: 90-byte and
  full-sized 1514-byte Ethernet frames (1500-byte IP packets). Both directions
  check IPv4/UDP checksums and every application byte.
- Session, connection epoch, origin and sequence bind requests and replies.
  AP binding happens only after complete payload validation and independent
  vendor-radio association confirmation. Old sessions/epochs cannot acknowledge
  current requests. Valid later epochs retain cumulative loss/abandonment evidence.
- TX and RX each have four 1514-byte slots. A full TX ring retains the pending
  RX operation for backpressure; full RX input is explicitly counted. BUSY
  retains the outbound Ethernet frame. Actual MAC submission removes it even
  on ACK timeout; neither completion nor an ACK means application delivery.
- One application request per direction is outstanding, with a 250-ms retry
  interval and four bounded submissions. Exhaustion records loss; no counters
  are cleared to manufacture a pass. Duplicates are answered without duplicate
  delivery. Payload hashes independently cross-check the delivered streams.
- The final five seconds of every 20-second interval stop new requests.
  Pending requests/replies still drain. Generation stops before the overall
  deadline to permit final acknowledgements. Both USB collectors also stop
  issuing requests for two seconds periodically; firmware must keep servicing
  radio and USB independently.

Build with the same two commands as B1. After full-flash backup and installation
of matching images, leave both badges in BOOTSEL and run:

```sh
bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX 30 --network
bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL AP_SERIAL NEW_PREFIX 3600 --network
node clean-rx/tools/badge/network_verify.cjs --self-test
```

The runner's build/tool overrides, signal cleanup and evidence retention are
the same as B1; it never flashes or restores originals. Low-level collector
commands are `collector SERIAL EVIDENCE_BIN CHANNEL --station-network BSSID SECONDS`
and `collector SERIAL EVIDENCE_BIN --network-ap CHALLENGE_HEX_28 SECONDS`.
Retain the station JSON/raw archive and AP JSONL/raw archive together. The
paired oracle checks every network-report field against its raw archive,
MAC/session/epoch bindings, counts and payload hashes in both directions,
full-sized traffic, bounded queue high-water marks, scheduled idle intervals,
host stalls and requested elapsed duration. Loss, corruption, RX overflow,
abandoned work, incomplete acknowledgement or a service gap above two seconds
fails qualification. The conservative minimum stimulus is 0.75 new requests
per elapsed second after the initial five seconds, in each direction.
Only a real run of at least 3600 seconds can set `soak_qualified:true`;
the native virtual-hour test is explicitly not RF evidence.

Network storage is fixed: each engine uses 13,940 bytes, including two
6080-byte rings and reusable packet construction space, with no heap.
Full-sized packets do not allocate full-sized temporary stack arrays in
the network engine. Compiler stack-usage files accompany both firmware
builds; the station network step uses 136 bytes, with separate 56-byte UDP
and 32-byte payload-builder frames. Reports retain application round-trip
latency maximum/sample count, service gap maximum, retries, duplicates,
loss, abandonment and queue pressure rather than hiding radio failures.

Native ASan/UBSan tests cover a virtual bidirectional hour with full-sized
traffic and idle, timer wrap, duplicate suppression, queue saturation and
resumption, checksum/payload corruption, binding poisoning, reconnect epochs,
stale traffic, retry exhaustion and maximum-sized MPDU conversion.
Real qualification and its exact scope are recorded in
[the milestone history](../dev-doc/history/STATION_MILESTONES.md#b2-continuous-application-data)
and [the roadmap](../../todo.md#b2-continuous-application-data).
The 30-second full-sized control passed; the real 60-minute run failed:
five beacon-loss recoveries coincided with application loss (3 station,
21 AP) and 3 abandoned AP work items. There was no corruption or queue
overflow, but this profile is **not yet B2 soak-qualified**. Both originals
were restored and full-readback-verified after testing.

## B3 reproducible AP-configuration qualification

The opt-in B3 runner repeats fresh legacy/open station connections against
three configurations of the controlled vendor AP:

| Channel | AP nmode | Security | Coverage |
| --- | --- | --- | --- |
| 1 | 0 | Open | Controlled legacy baseline |
| 6 | 0 | Open | Different channel/configuration |
| 11 | 1 | Open | Mixed-HT AP, legacy station association |

These are **one AP implementation**, not three independent firmware stacks.
An HT-capable AP does not mean the station negotiated HT.
Sustained software-WPA2 awaits the E milestones. Separate mesh nodes and
roaming are not covered; the matrix always reports `b3_qualified:false`
until that broader acceptance has independent evidence.

The Pico 2 W can be the independent vendor-firmware AP. Its GSPI pins match
the existing board transport (23/24/25/29); no display driver is linked.
The Pico build additionally establishes the reference clock and the existing
Pico 120-MHz SYS clock **before USB enumeration**. Badge peer builds retain
their original startup behavior. Build matching images/collectors separately:

```sh
make -C clean-rx/badge all test
make -f clean-rx/peer/Makefile HT_PEER_BUILD=build/clean-rx/pico-peer \
  HT_PEER_PICO=1 ht-peer ht-peer-test
```

After backing up full flash, install the clean badge image on the station
and `build/clean-rx/pico-peer/vendor_ht.uf2` on the Pico. Leave both in BOOTSEL:

```sh
CLEAN_RX_PEER_COLLECTOR=build/clean-rx/pico-peer/collector \
  bash clean-rx/tools/badge/qualification_probe.sh STATION_SERIAL PICO_SERIAL NEW_PREFIX 3
```

The final parameter is 1..10 cold rounds per configuration, default 3.
One round is only a control; at least three passing rounds per configuration
are required for `open_configurations_qualified:true`. Each trial runs the
station for ten seconds and AP for sixteen seconds. The runner never flashes,
restores firmware, changes unrelated APs or reads credentials. Always restore
and full-readback-verify both originals after a campaign.

AP mode 5 is exposed as
`collector SERIAL EVIDENCE_BIN --qualification-ap CHALLENGE_HEX_28 SECONDS:8-120 CHANNEL:1,6,11`.
It adapts the existing AP configuration callback: selects nmode while DOWN,
overrides the helper's channel-1 command, then independently reads back hardware
channel, target channel, nmode and wsec before publishing readiness.
Neither vendor firmware nor shared platform/AP setup code is modified.
The low-level paired runner is also available:

```sh
bash clean-rx/tools/badge/lifecycle_probe.sh STATION_SERIAL PICO_SERIAL NEW_PREFIX 0 --qualification 6
node clean-rx/tools/badge/qualification_verify.cjs MATRIX_PREFIX 3 STATION_SERIAL PICO_SERIAL
```

Both endpoint archives, JSON/JSONL, stderr and boot logs are retained.
The oracle binds serials, raw AP build ID, BSSID, channel/PHY/security
readbacks, station/AP MACs, application session/epoch and independent
association/data counts. Reused sessions, missing/mislabeled profiles and
failed or incomplete trials reject the matrix. Failed campaigns are retained,
not repaired by splicing in successful rows from another campaign.
Both collector processes are cancelled on interruption. Bounded BOOTSEL
enumeration retries handle the transition between consecutive cold trials.

The `b3-20261023/hardware/qualified02-*` Pico campaign passed all nine
connections and 162 verified echoes with station build `80e8c0bd` and Pico
AP `eebb2313`. There were no authentication/association failures or disconnects.
Six MAC timeouts, four echo timeouts and seven BUSY returns remain in the
evidence; these are bounded connection/data controls, not loss-free MAC or
B2 soak qualification. An earlier campaign was interrupted by the USB hub
disappearing, including the untouched second badge. After the user reconnected
the hub, both originals were restored and fully verified before retesting.
The final campaign also restored both full originals and returned them to BOOTSEL.

## Existing radio qualification

**Current qualification:** cold RX and single-frame **11 Mbps CCK OTA TX** are
verified between a Universe badge and a Pimoroni Tufty2350, in both directions.
The receiver independently checks the entire MPDU, FCS and fresh challenge.
The separate [Micro Signal OFDM profile](../../docs/micro-signal.md#80211g-legacy-ofdm)
now qualifies raw 6/9/12/18/24/36/48/54 Mbps legacy OFDM in both directions
between two Universe badges, at six MPDU sizes through 2,332 bytes.
Use `make micro-signal-g` for the normal fixed-6-Mbps button application or
`make micro-signal-g-test` for that all-rate sweep. This badge diagnostic
application retains its original CCK challenge protocol. Its separate
[DSSS/CCK fixture](#dssscck-rate-and-preamble-fixture) now verifies all four
rates and seven legal preambles on paired badges; full-size 1-Mbps reception
still fails. Do not generalize these bounded results to automatic ACK timing,
complete 802.11b/g MAC operation or general-purpose station reliability.
The separate controlled WPA2 qualification
below proves a bounded encrypted exchange, not sustained networking.

**802.11n:** D1's [HT20 implementation target](../dev-doc/experiments/ht20/README.md)
defines the conservative mixed-mode, single-stream MCS0-MCS7 profile and
maps current firmware gaps. Ordinary HT operation is not enabled or
advertised. Tested framing helpers, a default-disabled one-frame TX probe,
and raw HT diagnostics are implemented. Correcting the mixed-mode
register interface to separate RATE/LENGTH fields produced fresh valid
captures at every MCS0-MCS7 in both directions. A separate experiment
confirmed hardware HT-SIG CRC regeneration. The 48 all-rate one-frame
trials had 39 exact raw captures and 38 F2 deliveries, so this is a bounded
D1 mapping result, not reliable/general HT operation or independent-peer
qualification.
See the map's build/run instructions and evidence limits.
Default builds preserve legacy-only association even when an AP advertises
HT/WMM. The separate opt-in WMM build described below still omits HT.

For an autonomous two-badge application rather than this USB-driven
diagnostic, see the [direct HT20 button messenger](../messenger/README.md).
It uses short MCS0 broadcasts, application acknowledgements and visible
bounded retry/restart handling; it does not enable HT station association
or qualify D2 reliability.

For an independent vendor-to-clean HT receive control, see
[the controlled receive record](../dev-doc/experiments/rx/CONTROLLED_HT_RECEIVE.md).
The clean collector's `--vendor-ht-rx CHALLENGE_HEX_28 MCS:0-7` validates
fresh broadcast fixtures, their complete FCS and terminal HT metadata;
the vendor companion and paired runner live in [peer/](../peer/).
This diagnostic does not enable negotiated HT.

**D2 in progress:** a separate `CLEAN_RX_HT_DATA=1` build adds a versioned,
one-frame broadcast HT payload API (MCS0-MCS7, 28..2,332-byte MPDUs), validated
HT RX metadata and capture retention across F2 backpressure. The paired
`ht_probe.sh` accepts an optional fifth MPDU-length argument to select this
path. HT/WMM beacon profiling records peer EDCA parameters and explicit
fallback reasons, without enabling HT association. The opt-in
`CLEAN_RX_EDCA=1` gate executes per-category contention, and the additional
`CLEAN_RX_WMM=1` gate negotiates non-HT WMM and installs actual AP parameters.
An additional `CLEAN_RX_HT_RX=1` build expands raw receive storage to
3,896-byte MPDUs, checks upper SHM on chip and validates host/controller
layout compatibility. Passing `3869 --extended-rx` to the paired script
selects its fixed version-2, No-Ack QoS A-MSDU diagnostic: two MSDUs inside
the mandatory-minimum 3,839-byte aggregate, delivered through three F2
fragments and validated/deaggregated on the badge. It is not negotiated HT,
Block ACK or A-MPDU reception. Negotiated non-HT QoS station dispatch is
separately enabled by the WMM gate.
Bounded BA agreements, A-MPDU delimiter-stream parsing and ciphertext
reordering now have native integration with WPA2 delivery. They are not
called by the live station dispatcher: general live aggregate delivery and
SIFS-timed Immediate BA are still missing, and negotiated HT stays disabled.
See [the implementation boundary and tests](../dev-doc/history/BLOCK_ACK_SOFTWARE.md).
The separate `CLEAN_RX_AMPDU_PROBE=1` gate requires HT data and extended RX
and adds a finite, nonstation aggregate diagnostic. Use matching images
and collector with `ht_probe.sh TX RX PREFIX MCS --ampdu-probe`.
It submits one three-MPDU PPDU; reports distinguish a fresh individual
capture from all subframes retained and keep BA/HT qualification false.
With `CLEAN_RX_AMPDU_INTERFACE=3`, `CLEAN_RX_AMPDU_HANDOFF=2` now retains
all three complete MPDUs at MCS0..7 in both directions. It avoids repeating
the prefix/body split and continues after bad per-MPDU FCS. Handoff 1
traces the old lossy prefix transition. Optional three-bit
`CLEAN_RX_AMPDU_BAD_FCS` injects controlled FCS faults only in handoff 2;
invalid snapshots never contribute to the reported sequence bitmap.
Interface values 1/2 provide passive post/body timelines. All default to
disabled; the three-slot fixture is not general station delivery or BA.
The additional default-disabled `CLEAN_RX_AMPDU_DMA=1` gate requires
probe/interface-3/handoff-2 and publishes individual fixture MPDUs through
the actual D11 DMA/SRAM/F2 path. Currently only sequences 0 and 1 arrive;
the final MPDU is still missing. The collector independently checks saved
delivered payloads, not just capture counters. `actual_multi_mpdu_delivery`
can therefore be true while `all_subframes_delivered`, `immediate_ba`,
`sifs_timing_qualified` and `ht_qualified` remain false. This is still a
bounded experiment, not production station/BA integration.
Alternatively, `CLEAN_RX_AMPDU_SHM=1` uses the three-slot PSM
capture with an explicit state-16 terminal lease and a
pause/readback-verified controller snapshot, then validates
and queues each complete MPDU through SRAM/F2. It requires the same
probe/interface-3/handoff-2 gates and cannot be combined with DMA delivery.
The finite matrix delivers all three MPDUs at MCS0..7 in both directions,
with exactly three deliveries and zero duplicates in completed controls;
failed/partial receptions and fresh repeats are retained separately.
Bad-last/all-bad completion no longer waits for a later ordinary frame.
The collector exposes `shm_delivery_enabled` and `shm_terminal` ownership
counters/timestamps, and independently verifies the saved delivered bytes.
The measured snapshot/lease intervals are not EOF-to-air SIFS measurements.
This remains a fixed-size, one-aggregate experiment: general station
reception, Immediate BA, measured SIFS and HT advertisement are not enabled.
An additional default-disabled `CLEAN_RX_AMPDU_BA=1` SHM-only profile now
arms a pinned diagnostic PSM compressed BA through command 17. Mode 31
enables TX only for this profile; mode 30 uses automatic turnaround to
receive the actual response. The final campaign independently verifies
28 complete returned BA frames, including one partial normal response,
and normal complete-delivery/return controls at every MCS/direction after
repeats. Bad-final reverse MCS7 remains unqualified.
The collector exposes `ba_experiment`; armed positive pairs require an
actual matching returned BA and delivery bitmap. These are fixed
three-by-80-byte tests, not production agreement/reorder installation,
measured RF SIFS or negotiated HT. The existing qualification flags stay
false. See [BA build gate, command/evidence ABI and full results](../dev-doc/experiments/ampdu/INTERFACE_AND_DELIVERY.md#diagnostic-compressed-ba-return-2026-10-19).
See [interface profiles and measured limitations](../dev-doc/experiments/ampdu/INTERFACE_AND_DELIVERY.md).
See [build commands, evidence layout and failed continuations](../dev-doc/experiments/ampdu/INITIAL_RADIO.md).
The first D2 PHY/association item is now split into completed software
pieces and pending activation/qualification. A conservative unadvertised
HT20 target IE, constrained AP/runtime MCS candidate and shared per-frame
MCS/control/airtime configuration are native-tested. The collector exposes
the target/candidate without claiming negotiation; default or HT-data-only
builds explicitly suppress it. HT probe/data/ACK reports distinguish
calculated airtime from OTA measurement. Actual HT advertisement and
station activation remain disabled pending the mandatory live MAC.
See [candidate policy and configuration](../doc/HT20.md#local-target-and-phy-configuration).
The additional, default-disabled `CLEAN_RX_HT_RESPONSE=1` gate requires
extended RX and adds pinned-radio QoS/HT normal-ACK response logic.
`ht_probe.sh TX RX PREFIX MCS --ack` uses its matching `ht-ack` build for a
fixed 60-byte normal-ACK QoS diagnostic using live PSM/RX and automatic
PHY turnaround (API version 4). `--ack-raw` retains the raw-TX/version-3
control, which restores RX too late for this ACK exchange. The standalone
clocked TX collector flag is `--ht-ack-clocked-tx`; RX remains `--ht-ack-rx`.
Reports distinguish full fresh HT delivery, response starts and an actual
returned ACK, including its complete FCS and legacy PLCP. Clocked success
requires exactly one TX attempt and independently validated ACK/address/rate
evidence, not just local response counters.
The earlier full-evidence matrix passed 12/16 exchanges: three failures never
transmitted and one transmitted frame timed out. Access-state repeats
exposed invalid backoff operands. The DCF transition now stages its counter
and separates the SHM load from its first comparison; out-of-range values
fail explicitly with controller error `0x8e`. Frozen diagnostics include
the last draw, before/after counter values and a TSF sample. Three bounded
all-MCS/direction sweeps subsequently launched 48/48 frames without access
faults, but only 37/48 completed verified ACK exchanges. The remaining
11 transmitted-frame failures are retained, not counted as successful HT.
Reinspection separates them into four with no early HT candidate, two with
expected HT-SIG but no recorded terminal outcome, and five full-length
terminal receive errors. The opt-in ACK diagnostic now snapshots failed
60-byte fixtures before rearm, including raw bytes, count, status/TSF and
terminal-error/RX-abort cause. JSON reports `failure_raw`,
`failure_fcs_valid`, `failure_candidate_match` and, for complete captures,
`failure_fixture_bit_errors`. Only `failure_length` bytes are valid; the
latest qualifying failure can replace an earlier record. Corrupted addresses
do not become exact peer matches simply because payload bytes correlate.
Build `535e0ffe` reproduced six full-length corrupt MCS7 receptions with
57..194 fixture bit differences, independently confirmed by zlib CRC32.
MCS7 ACK results were 5/8, 7/8 and 4/8 on channels 1, 6 and 11;
MCS0 controls were 2/4, 4/4 and 4/4. This establishes received-data corruption
for the reproduced terminal failures, not its RF/PHY root cause or an
explanation for every historical loss. Early-candidate evidence is a latest
snapshot and can be replaced by unrelated traffic even after a success.
Set `HT_PROBE_CHANNEL=6` (validated range 1..11, default 1) before the paired
script for controlled channel comparisons; it selects both endpoints and
their saved channel evidence. These bounded trials do not qualify any
channel or enable HT association.

`HT_PROBE_CHALLENGE` reuses a saved 28-digit lowercase hexadecimal challenge
instead of generating a new one. MCS, MPDU length and an explicitly selected
`HT_PROBE_TID` must match the saved challenge; mismatches fail before hardware
access. Use a fresh evidence prefix for every replay.
Fresh normal-ACK challenges select a TID in 0..7; replayed ACK challenges
outside that range are rejected before hardware access.
Further investigation added `failure_repeated_raw`,
`failure_raw_repeat_match`, `failure_time_us` and `failure_final_*` fields.
Seventeen new full-length failures had identical independent copies and
invalid FCS, including two RX-event aborts that leave the early trace pending.
Capture now precedes terminal PHY classification, closing a diagnostic gap.
A separate quiet-listening watchdog bug was reproduced and fixed for live
hardware DCF; raw one-shot RX keeps its timeout. Final `72496ec8` passed four
receive-only idle controls without controller faults, but only 3/12 paired
cells completed strict ACKs: five corrupt frames, two no-candidate misses
and two zero-attempt access faults remain. New halted counter evidence
(`dcf_guard_latched`, `dcf_final_slots`) confirms that access faults can
still occur; they are not receive losses or a completed backoff repair.
See the [follow-up evidence and limits](../dev-doc/experiments/ht20/CLOCKED_ACK_AND_RX_FAILURES.md#further-investigation-stable-corruption-rx-aborts-and-idle-watchdog).
RF SIFS/reliability qualification, complete EDCA scheduling, HT protection
and Block ACK remain open. The separate opt-in
[non-HT WMM integration](../dev-doc/experiments/mac/EDCA_AND_WMM.md#ap-driven-non-ht-wmm-integration)
has bounded independent-AP association and protected-exchange evidence;
neither it nor a response counter enables HT association.
See [D2 API, build instructions and remaining gates](../dev-doc/history/HT20_FOUNDATIONS.md#d2-implementation-bounded-packet-api-and-peer-profiling).

## Source layout and maintenance

- [`../src/`](../src/) contains the CYW43439 firmware and portable shared code:
  frame limits, PHY rates, TX diagnostics, unicast/SDIO definitions and WPA2
  cryptography. New shared C/headers belong there, not in the `clean-rx/` root.
  The controller Makefile explicitly selects its sources; placing shared code
  here does not automatically link it into the controller image.
- This [`badge/`](./) directory remains the RP2350 diagnostic application,
  USB collector and native tests. Existing build entry points and generated
  output paths are unchanged.
- Host and badge use the same `wpa_secret_binding` implementation, with a
  known-answer vector and changed-secret/BSSID/channel regression tests.
  Keep public wire formats and crypto operations shared rather than duplicating
  them in endpoint code.
- Native badge tests share header prerequisites and loader source lists so
  changing an included protocol header or transport implementation rebuilds
  the corresponding tests.

Useful next structural refactors are separating controller RX/SDIO and TX
handling from [`main.c`](../src/main.c), extracting station orchestration from
the [badge entry point](main.c), and replacing duplicated numeric diagnostic
offsets with a shared layout/encoder. Keep those changes separate from radio
timing or protocol behavior changes so existing hardware evidence remains
meaningful.

## Experimental WPA2 protected exchange

```sh
build/clean-rx/badge/collector SERIAL evidence.bin CHANNEL \
  --ap-wpa2 XX:XX:XX:XX:XX:XX PRIVATE_CREDENTIAL_FILE
```

The credential file must be owned by the current user, have no group/other
permissions (`chmod 600`), and contain exactly two LF-terminated lines: SSID
(1..32 bytes), then passphrase (8..63 bytes) or a raw PSK (exactly 64
case-insensitive hexadecimal digits). The host derives passphrase PMKs using
PBKDF2-HMAC-SHA1/4096, decodes raw PSKs without a second derivation, and provisions
the PMK plus a fresh OS-random SNonce through clean-image-only USB command
`0x67`. A hash binds the one-use provisioning to the BSSID, channel and request.
The radio/badge programs remain freestanding C with no cryptographic library
or vendor radio executable.

The supplicant derives KCK/KEK/TK, sends EAPOL messages 2/4, authenticates
message 3, checks its ANonce/replay counter/RSN IE, unwraps its GTK using
RFC3394 AES, and installs software CCMP state. Duplicate message 3 resends
message 4 without reinstalling keys or resetting packet numbers. An advanced
M3 replay counter is accepted only with a valid MIC and unchanged ANonce,
RSN, GTK and key ID; stale counters and changed keys are rejected. Retransmitted M1
may advance replay with the same ANonce without deriving a second PTK.
An all-zero ANonce/SNonce, changed ANonce, malformed/unsupported pinned RSN,
or failed MIC is an explicit failure with software keys wiped. GTK KDE reserved
bits/bytes are ignored on reception, while their raw metadata remains visible;
KDE length, uniqueness, authenticated key contents and key ID remain checked.
EAPOL MICs
cover the declared EAPOL length, not trailing MAC-payload padding. Physical
SDIO staging is rounded to four bytes without changing the transmitted frame
length or FCS. Old authentication retries are drained before association,
and a received request ACK no longer suppresses retries for a missing response.

After message 4, the station sends a fresh CCMP-protected EtherType `0x88b5`
challenge. `wpa2_authorized:true` and collector success require an authenticated,
non-replayed protected echo matching this session, in addition to independently
validated authentication/association responses. Merely building message 2,
sending message 4, or observing the AP's authorization does not pass.
`m2_sent`/`m4_sent` count completed controller submissions, not ACK confirmations.
The TFT reports the verified protected exchange or an explicit failure.

E1 uses a wrap-safe eight-second absolute deadline, including the initial M1
wait. M2 and M4 each have at most four locally timed retransmissions at 500 ms
intervals. Incoming duplicates do not reset that budget or the absolute
deadline. `clean_wpa_poll` must run before RX/TX service, including while a
controller transmission is pending. `handshake_retries` records timer-generated
retries, which may be superseded by incoming traffic before submission.
The existing submission counters also include responses to AP retransmissions.
A MAC ACK is not authorization: only the protected echo
completes the connection. In E2, fresh pairwise M1 starts a new bounded key
epoch and SNonce without resetting the old data epoch prematurely. The session
retains its PMK and a secret HMAC-based nonce-generator seed until teardown;
the initial SNonce is still independently OS-random.

Station policy command completion is now an applied/readback-verified commit
barrier, not acknowledgement of deferred queueing.
`wmm_policy_committed_updates` counts completed commits; the older
`wmm_policy_queued_updates` JSON name remains an equal compatibility alias.
Readback, timeout and command-replacement failures are explicit. This does
not enable HT advertisement or qualify the missing live MAC/BA obligations;
see the [policy commit campaign](../dev-doc/experiments/mac/EDCA_AND_WMM.md#readback-verified-policy-commit-campaign-2026-10-18).

For a reproducible two-badge test, first load the vendor `usb-wifi-link` image
on the AP and this image on the station, leaving both in BOOTSEL, then run:

```sh
bash clean-rx/tools/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL build/wpa2-trial
# Negative control, with a fresh temporary network:
bash clean-rx/tools/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL build/wpa2-wrong --wrong-password
```

The runner creates temporary credentials in a private directory, removes them
on exit, and requires matching station MACs and protected RX/TX evidence at
both endpoints. Both USB applications boot before the AP session starts;
station collection starts immediately on AP readiness, within its bounded
join window. Set `CLEAN_RX_COLLECTOR` for an isolated matching build.
The paired HT, MAC-soak and WPA2 runners also accept `CLEAN_RX_PICOTOOL`
as the path to an alternative executable or diagnostic wrapper; the default is
`build/picotool-usb/picotool`. A wrapper must preserve arguments and exit status.
The runner records phase times in `PREFIX-timing.log` and preserves failed
reports. It does not flash either badge. The AP fixture exports a
temporary passphrase only with explicit `--credentials-out`; neither collector
logs passwords, PMKs, PTKs or GTKs.

**Earlier hardware qualification:** retained build `0ebad503` completed all four EAPOL
messages and a protected round trip in three of five fresh positive trials,
independently confirmed by the vendor AP (30, 38 and 37 stable peer observations).
The other two trials failed before the key exchange because no association
response reached the host; neither reported authorization.
A real wrong-password control associated and submitted message 2 but neither
endpoint authorized or exchanged protected data. Association-response loss
still occurs in separate trials; this is not a reliable general station.
An intermediate trial also rejected GTK key data; broader AP compatibility
is unqualified. `gtk_kde` reports only KDE length/flags/reserved-byte metadata,
never key bytes.

### E1 protected Pico AP matrix

Build the station normally and the Pico vendor peer with its explicit target:

```sh
make -C clean-rx/badge BUILD="$PWD/build/clean-rx/e1/badge" all test
make -f clean-rx/peer/Makefile HT_PEER_BUILD=build/clean-rx/e1/peer-pico \
  HT_PEER_PICO=1 ht-peer ht-peer-test
```

Back up each entire original flash before installing either image. Load the
station image on the badge and the peer image on the Pico, leaving both in
BOOTSEL. Restore both originals afterward and compare complete flash readbacks;
the runners below deliberately do not flash or restore devices.

```sh
export CLEAN_RX_COLLECTOR=build/clean-rx/e1/badge/collector
export CLEAN_RX_AP_COLLECTOR=build/clean-rx/e1/peer-pico/collector
bash clean-rx/tools/badge/wpa2_matrix.sh AP_SERIAL STATION_SERIAL NEW_PREFIX
node clean-rx/tools/badge/wpa2_verify.cjs --matrix NEW_PREFIX STATION_SERIAL AP_SERIAL

# One fresh protected connection, optionally using --raw-psk or --wrong-password:
bash clean-rx/tools/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL NEW_PREFIX \
  --peer-ap 6 --raw-psk
```

The matrix requires ten consecutive controlled channel-1 connections, followed
by three each on channels 6 and 11, alternating passphrase/raw-PSK provisioning.
Three further fresh networks check wrong passwords, one per channel. Failed
trials remain failures: there is no retry-until-pass or archive splicing.
Each trial records both raw reports, target serials, build IDs, independent
peer identities, channel/nmode/CCMP/WPA2-auth readbacks, protected RX/TX counts,
and process exit codes. The oracle rejects stale sessions and mismatched raw
evidence. Channel 11 enables mixed-HT on the AP only; the station stays legacy.
This is one AP implementation, not three independent stacks.

The new peer `--protected-ap CHANNEL SECONDS PRIVATE_CREDENTIAL_OUT` mode
generates an independent random temporary passphrase and transfers its PMK
through the one-use USB secret endpoint. Its password is not derived from the
public request nonce. Provisioning is consumed before clock/firmware polling:
a not-ready USB report intentionally stalls and wipes any unconsumed secret.
Mode 6 reports the last operation on failure instead of guessing a setup stage.
Temporary credential files are private and removed on normal completion or
cancellation. `collector --derive-psk INPUT OUTPUT` converts a private credential
file into a new, exclusive mode-600 raw-PSK file without printing secrets.

E1's bounded connections do not qualify GTK traffic, group/pairwise rekey,
mesh nodes, roaming, negotiated HT, or the still-open B2/E3 hour-long soak.

### E2 group traffic and rekey

The supplicant supports RSN group-key messages and pairwise rekey, including
EAPOL carried inside CCMP. Four GTK slots have independent replay windows for
each QoS TID and non-QoS. Authenticated RSC floors never decrease. Reusing the
same installed key does not reset any PN; replacing it with new material does.
Previous GTK slots overlap for two seconds, then are wiped. Group fragments,
group A-MSDUs and group Block Ack are not supported and are rejected.

Pairwise rekey uses fresh SNonces from a private HMAC-based sequence seeded
by the initial OS-random nonce and PMK. Data continues under the old TK while
waiting for M3. M2/M4 use the new KCK inside old-TK CCMP envelopes; M3 activates
the new TK once. Old-key M3 retransmissions can recover a lost M4 without
resetting either epoch's PNs. New-key data confirms completion and wipes the
old PTK. Deadlines/retry budgets remain bounded. Concurrent group/pairwise
exchanges fail explicitly rather than sharing inconsistent pending keys.

The existing E1 one-echo mode is unchanged. The explicit mode-8 fixture owns
the authenticator on the RP2350: the PMK is not provisioned to the vendor
authenticator. The vendor AP still owns association and CCMP packet handling.
The host generates fresh ANonces/GTKs from a private OS-random seed, validates
M2/M4/G2, installs each fresh key once, and authorizes only after valid M4.
It drives three pairwise and three group exchanges, alternating **PTK first**,
four seconds apart while traffic continues. Retries use cached EAPOL content
and the current data-key envelope; they do not reinstall keys or extend the
exchange deadline. Failure and teardown wipe private state.

This fixture is intentionally a single-station PSK-CCMP profile for the bundled
vendor firmware, including its observed RSN capabilities (`0x000c`). It pins
unicast/group rates to 1 Mbps and verifies the firmware readbacks, retaining
their override flags in evidence. The station independently pins the on-air
RSN and exports it for the oracle; a changed profile does not silently pass.

Build and test the matching station/Pico images, then install their test UF2s
only after backing up the full original flash as described for E1:

```sh
make -C clean-rx/badge BUILD="$PWD/build/clean-rx/e2-finish/badge" all test
make -f clean-rx/peer/Makefile HT_PEER_BUILD=build/clean-rx/e2-finish/peer-pico \
  HT_PEER_PICO=1 ht-peer ht-peer-test

CLEAN_RX_COLLECTOR=build/clean-rx/e2-finish/badge/collector \
CLEAN_RX_AP_COLLECTOR=build/clean-rx/e2-finish/peer-pico/collector \
bash clean-rx/tools/badge/wpa2_trial.sh 6572BE3E4636AF51 08DA26F86E57F5EA \
  build/clean-rx/e2-finish/hardware/NEW_PREFIX --peer-ap 1 --rekey
```

This selects station `--ap-wpa2-rekey` (mode **34**, not B1's mode 32) and
peer `--protected-rekey-ap` (mode 8). The AP sends independently counted
broadcast/multicast challenges and unicast echoes. Its evidence includes
validated M2/M4/G2 counts, installed epochs, group challenges submitted after
each GTK rotation, and protected station traffic received after each PTK
rotation. The next exchange is gated on that AP-side traffic evidence.

The separate `--group-traffic` runner option uses AP mode 7. It probes
`wpa_gtk_rekey`/`wpa_ptk_rekey`; supported controls are set to five/nine seconds
and read back, while `BCME_UNSUPPORTED` (-23) is explicitly recorded. Other
control errors abort the fixture. Unsupported controls do not imply that a
rekey happened.

The runner's `--group-traffic` is deliberately **traffic-only**, not an E2
milestone pass. `--rekey` invokes the stronger verifier without that override:

```sh
node clean-rx/tools/badge/wpa2_rekey_verify.cjs PREFIX STATION_SERIAL AP_SERIAL
```

Full acceptance requires three group and three pairwise rekeys, each with
subsequent protected challenge proof, plus matching raw endpoint identities,
security configuration, session tokens, counts, duration and successful exits.
Mode 8 additionally binds the AP's raw rekey/traffic counters, pinned rates,
authenticator status and the station's exact observed RSN. Plaintext
application-frame observations fail full acceptance.
The report distinguishes installed epochs from traffic-verified epochs.
Native tests exercise these protocols but are not substitutes for RF proof.
The runner does not flash or restore; preserve full original device flash
and verify restoration separately, as for E1.

**Earlier 2026-10-23 traffic-only result:** `traffic04` passed the 35-second group-traffic
control: 348 protected unicast echoes, 17 broadcast and 52 multicast receptions.
Station `7904df1d` and Pico AP `b91f9a24` also passed a separate fresh E1
connection. Both full original flash images were restored/readback-verified.
Both AP rekey controls actually returned -23; there were **zero RF rekeys**.
That archive remains traffic-only and still fails the full E2 gate.

**Final 2026-10-23 E2 hardware qualification:** the host-owned authenticator
closed the forced-rekey gate with station `3b5748a1` and Pico AP `b40abc4f`.
Each retained run lasts at least 35 seconds and completes three group plus
three pairwise rotations, with all six new epochs traffic-proven:

| Archive under `build/clean-rx/e2-finish/hardware/` | Channel | Protected echoes | Broadcast RX | Multicast RX |
| --- | ---: | ---: | ---: | ---: |
| `rekey10` | 1 | 338 | 67 | 2 |
| `rekey11` | 6 | 339 | 68 | 10 |
| `rekey12` | 11 | 338 | 68 | 10 |

The AP independently received/echoed the same 1,015 unicast challenges and
submitted 203 broadcasts and 203 multicasts. The multicast losses are real;
this is not a lossless-link claim. There were no terminal WPA/transport
errors or plaintext application observations. `rekey11-e1` also passes a
fresh vendor-authenticator E1 connection with these final builds. Both full
original flash images were restored/readback-compared after every campaign.

Earlier group-first fixture attempts completed a GTK rotation but timed out
before pairwise completion amid receive/FCS losses. They remain failed
archives; the passing RF scope is the PTK-first schedule above, not arbitrary
exchange ordering. Native authenticator/supplicant tests separately exercise
group-first rotation and lost G2/M4 recovery, MIC/backend/timeout failure
wiping, and install-once behavior. Native results are explicitly NOT RF.

**E1 hardware qualification, 2026-10-23:** campaign `matrix03` passed all
16 fresh protected connections (10 on channel 1, three each on 6/11), with
16 independently verified protected echoes. Eight connections used passphrases
and eight raw PSKs. All three fresh wrong-password controls rejected without
authorization, M4, or protected traffic. Station build `5265041c` and Pico AP
build `93bf26c0` were bound to the retained raw archives; the exact qualified
AP image/collector remain in `build/clean-rx/e1-20261023/qualified03-peer/`.
The strict matrix and native tests pass. Full original badge/Pico flash
readbacks matched after the campaign; the other badge was untouched.

After adding a host-only guard against archiving an absent AP report, the
final AP build `e08283ff` passed the additional `control04` protected RF
exchange with station `5265041c`. Both full originals were again restored
and readback-verified.

Native ASan/UBSan tests cover independent SHA1/HMAC/PBKDF2/PTK/AES/CCMP
known-answer vectors, unwrap integrity, padded/truncated EAPOL, wrong MIC/key,
ANonce/replay/RSN/GTK validation, duplicate-message non-reinstallation,
per-TID CCMP replay/MIC failures, PN exhaustion and USB secret lifecycle.
Compiler stack-usage output is retained alongside objects. RX consumers share
a static packet rather than stacking multiple full-sized frames on the 8 KiB
stack; the earlier E1 decrypt-call-chain estimate was approximately 4.3 KiB.

Limitations: this bounded fixture stops and wipes keys after its result.
There is no protected DHCP/IP stack, persistent application link, roaming,
WPA3, enterprise authentication or PMF. E2 adds group decryption and rekey
protocols and the bounded legacy RF qualification above; it does not qualify
HT/BA rekeying, multi-station operation or arbitrary authenticators. Other suites
and malformed key exchanges fail closed. General CSMA/CA, reliable unicast and RF/SIFS ACK
timing remain unqualified. This new crypto is experimental, not independently
audited or production-security-qualified.

## Milestone A MAC implementation: experimental, not complete

The station diagnostic now selects experimental **profile 3** in controller
command 7 (`config[15] = 3`). Profile 2 retains the Cortex polling scheduler;
profile 1 remains available for older diagnostic callers;
raw broadcast/Micro Signal modes do not enable this station MAC. No
profile is a claim that milestone A in the [roadmap](../../todo.md) is complete.
All implementation and supporting tests remain inside `clean-rx/`.

Implemented paths:

- [`mac.c`](../src/mac.c) / [`mac.h`](../src/mac.h) provide deterministic
  ERP contention arithmetic: 9/20-us slots, DIFS, EIFS, Duration/NAV,
  xorshift contention draws, freezing/resumption, CW growth through 1023,
  success/new-frame CW reset and unsigned timer wrap. Long unsampled intervals
  are not credited as idle slots.
- Profile 3 executes contention in the PSM, using IFS physical/virtual carrier
  and active-frame flags and the hardware TSF microsecond clock. It explicitly
  waits DIFS (28/50 us), then counts 9/20-us backoff slots, freezing the
  residual and discarding partial slots on busy/RX. Bad-FCS and bad-length
  paths impose SIFS plus the slow legacy ACK airtime, followed by DIFS.
  GoodFCS clears EIFS. RX wins over an armed template; automatic responses
  cancel prepared TX before they can overwrite its TXE configuration.
  The Cortex still chooses random CW draws and prepares templates, but the
  PSM performs final arbitration and triggers profile-3 TX.
- The profile-2 controller samples D11 PHYDEBUG carrier sense (`PDBG_CRS`, bit 0),
  observes the actual PSM idle/receive states, and imports PSM FCS/length
  discard counters. The bit definitions are documented in the public Linux
  [brcmsmac D11 register header](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h).
  TX template selection is deferred until after contention, not enabled while
  waiting for access. Host credit/capture conflicts return `BUSY`, not a
  fictitious successful transmission. One command's access wait is bounded
  at 50 ms; existing host/service deadlines remain in force.
- Reliable commands support legacy OFDM and long-preamble 11 Mbps CCK.
  In a BSS advertising ERP `Use_Protection`, this host uses the advertised
  11 Mbps CCK rate instead of emitting unprotected OFDM. Such a BSS without
  that rate is explicitly unsupported. Association advertises
  short-preamble support only when advertised by the AP; control-response
  preambles are selected from the received PHY header and selected response
  rate. A 1 Mbps response always retains the long preamble. Short-response RF
  timing is not qualified.
- The PSM answers valid bound-peer RTS frames with CTS when the locally
  observed NAV permits it. CTS Duration subtracts SIFS and the selected response
  airtime. RTS is consumed and RX rearmed inside the PSM, without waiting for
  a host credit or Cortex capture handling. This relies on RTS being shorter
  than the RX streaming prefix, which has a compile-time assertion. Wrong
  lengths/addresses, reserved Duration/ID and insufficient Duration do not
  trigger CTS. Profile 1 retains its previous response policy.
- OFDM response classification reads PHY status 0's frame-type field, retaining
  the legacy G-PHY OFDM indicator as a fallback. The latter alone is insufficient
  on this LP PHY: real 6-Mbps receptions had RX flags `0x0440`, without bit 13.
  HT is rejected by default. The separate HT response gate permits only
  the supported HT20 mixed-mode profile and normal-ACK QoS policy.
  The complete ACK/CTS replacement
  header, masks, byte count and crypto bypass are prepared before TX is armed
  and explicitly triggered. TXE ownership remains reserved through scheduled
  start and the physical TX tail, including periods when PHYDEBUG's byte strobe
  is clear. Cortex template/policy changes defer while that ownership is held.
- PSM ACK response-rate selection includes residual Duration for
  supported fragmented reception. The ACK acceptance window accounts for the
  complete slowest legacy ACK PPDU in the modulation family, rather than
  assuming a GoodFCS indication arrives at PHY-RXSTART. It is bounded against
  local TX, not an independently measured RF end timestamp. Profile 3 anchors
  its clock to observed IFS TX-active and uses the same TSF clock domain on both
  processors; the earlier Cortex timer at `D11+0x1c` is not that clock.
  A bounded 64-us status/pipeline allowance is experimental: the large-frame
  test measured ACK notification 115 us after predicted data completion, beyond
  the old allowance. This is not proof that the response meets SIFS.
- Every RA/TA comparison stages one address word in `r2`: D11 permits only
  one SHM input per instruction. The six former SHM-versus-SHM comparisons
  were invalid, despite passing the flat-memory native interpreter. Response
  telemetry exposed unexpected frame classes; the corrected program and an
  ISA-wide native check prevent that mistake from recurring.
- The new profile resumes RX/PSM immediately after triggering TX. Its CPU
  drain guard does not inhibit the transmitter/receiver while waiting:
  PHYDEBUG TXA is a byte-transfer strobe, **not** a frame-active/tail signal.
  Attempts are counted at the actual template trigger, including submissions
  whose subsequent local completion fails. Retries change only Retry/FCS;
  sequence, fragment number, encrypted payload and PN remain intact.
- In profiles 2 and 3, valid addressed ACK control frames are consumed directly in
  the PSM and counted at SHM `0x639`. They bypass DMA/header publication and
  jump to RX rearm, rather than waiting for Cortex handling or occupying the
  only application RX credit. `ACK_SEEN` is set only during an active ACK wait;
  local ACKs outside that wait are consumed without completing it. Profile 1
  retains the earlier capture path. Other held captures
  survive both host-credit and SDIO DMA backpressure, including their saved
  DMA metadata. A capture is released only after successful F2 publication.
  ACK consumption also clears any prepared transport state. Captures arriving
  after the main loop's first state sample are checked again before publication,
  so their NAV is not skipped and staged ACK bytes cannot replace the next
  application capture. These fixes do not explain all observed response loss.
- [`station_rx.h`](station_rx.h) applies the shared non-QoS duplicate cache
  and bounded unprotected reassembly to station waits, drains, post-association
  reception and the WPA loop. Its negotiated WMM path additionally dispatches
  unprotected QoS through per-TID contexts. The pinned-peer cache tracks subtype, sequence
  and fragment identity across sequence wrap. Delivered identities persist
  until their bounded history slot is replaced or a fresh non-Retry frame
  reuses the identity; they do not expire during a long retransmission exchange.
  Only incomplete reassembly has a 512-ms lifetime. Expiry invalidates its
  incomplete history, not an already delivered replacement.
  One assembly supports up to 16 fragments and 2,304 payload bytes; conflicting,
  out-of-order, expired and oversized fragments have explicit counters/results.
  Reassembled packets carry `packet[17] = 1`: their reconstructed FCS is **not**
  an original over-air FCS or RF timing measurement.
- Protected frames bypass the unauthenticated duplicate cache. After each
  supported data fragment passes CCMP MIC and PN checks, a separate authenticated
  cache/reassembly state accepts it. Subsequent fragments must have consecutive
  PNs, consistent headers/addresses and sequence, and the supported pairwise
  Key ID. Reassembly retains the Protected bit as a cache-domain discriminator;
  it never mixes authenticated and unauthenticated fragments. It has the same
  16-fragment/2,304-byte/lifetime bounds. Reconstructed plaintext/FCS stays
  internal, and plaintext is wiped on delivery, expiry or terminal protocol failure.
  QoS reception has 16 independent TID duplicate/fragment contexts, now used
  by the existing WPA QoS receive path. Idle station polling expires assemblies
  without requiring another packet on the same TID. With extended RX and
  negotiated WMM, unfragmented protected A-MSDUs support a 3,839-byte body
  (3,885-byte encrypted MPDU). After MIC verification, complete aggregate
  structure and every subframe's local destination must validate before PN
  commitment or duplicate processing. LLC/SNAP-as-destination rejection
  also guards against changing the unauthenticated A-MSDU-present bit.
  Delivery increments `protected_amsdu_frames` and
  `protected_amsdu_subframes`; retry duplicates do not increment either.
  Block ACK/No Explicit Ack policy, four-address and HT Control frames remain
  unsupported. The opt-in non-HT WMM station gate negotiates QoS separately;
  no HT capability is advertised. The isolated response prototype described
  above is separately gated, not a four-queue EDCA scheduler.
  Pending WPA transmissions
  survive zero-attempt `BUSY` results without advancing submission counters or
  allocating another protected PN.
- Authentication/association budgets now count actual submissions, not `BUSY`
  commands. They drain RX on a short deadline before retrying a deferred MPDU,
  with a separate 1.6-second phase bound. Null-data frames are accepted rather
  than aborting the station as an unsupported subtype.
- Management request recovery distinguishes a MAC retransmission from a new
  request after a MAC-ACKed request's response timeout. A no-ACK retry preserves
  sequence and body and changes only Retry/FCS; zero-attempt `BUSY` does not
  advance identity. Once ACKed, the next request gets a fresh sequence and clear
  Retry bit, avoiding AP duplicate suppression of an already completed MPDU.
  Authentication and association share the advancing sequence allocator.
  The four-submission budget and 1.6-second phase deadline are unchanged.
- Bound, valid-FCS AP beacons refresh ERP policy. Pending frames can switch
  between long-preamble CCK protection and OFDM without changing sequence,
  ciphertext or PN. Basic-rate/slot changes queue controller command 8 using
  the same 16-byte bound-address configuration as command 7. Response-table
  updates wait for idle RX/PHY; pending updates defer new TX. Slot changes
  preserve CW, remaining slots, NAV and pending EIFS, but restart idle sampling.
  A WMM association instead combines rates, slots, QoS responses and all
  four AP contention records in command 16, including runtime WMM changes.
  Changes of advertised rate set, SSID or RSN are explicitly unsupported rather
  than silently changing the established profile.
- Association accepts either setting of the reserved upper AID bits, while
  retaining the nonzero/1..2007 range, bound addresses, status, FCS and IE checks.
  The local mesh AP actually returned clear reserved bits.

### Diagnostics and reproducible native tests

The original 40-byte ACK status at `0x201e0` remains unchanged. A separate
64-byte `clean_mac_radio_status_t` at `0x20210` reports `MAC3` and its profile,
CW, remaining slots, NAV deadline, TX deferrals, internally consumed ACKs,
ACK timeouts, RX backpressure polls, a pending-RX flag, scheduled CTS/short
response counts, queued policy status, prepared-ACK discards and live SHM
address readback. It does not overlap the mailbox, ACK status or capture ring.
New response counts accumulate sampled 16-bit PSM deltas into 32-bit totals;
the original ACK-status counters retain their existing format.

A separate 112-byte `MCR1` status at `0x20250`, copied to detail offset 1808,
reports scheduled responses by auth/association/non-QoS-data/other receive
classes, corresponding bound frames actually published to transport, and
invalid-FCS published captures. These counters are reset at controller startup
and exposed as `mac_response_*`, `mac_published_*` and `mac_trace_report`.
Class counts distinguish PSM response scheduling from later host delivery;
they do not identify individual RF exchanges or measure SIFS. Its original
40-byte prefix is preserved. Appended words report host/PSM IFS and clock
samples, the last local-response stage/FC/RX flags, global bad-FCS count and
last invalid frame identity/length, ACK notification elapsed/window and response
setup delay. Detail offset 1908 reports deliberately suppressed eligible
ACKs as `mac_ack_dropped`. The final two words, at offsets 1912 and 1916,
report `mac_rx_filtered` and `mac_rx_capture_max_us`: station-filtered
captures and maximum sampled profile-3 capture age, respectively. The latter
is a service observation, not an RF/SIFS measurement. Both station and soak
JSON expose these as `mac_*` diagnostics. Soak fault evidence starts at 1920,
after the trace.
Bad-FCS counts include unrelated channel traffic; they are not bound-peer loss.

For station profiles 2/3, the controller also snapshots rejected, 40-byte
RXE authentication candidates matching either the local RA or pinned-peer
TA. The 60-byte `BAF1` record at `0x204b0` includes a wrapping 16-bit count,
sampled TSF, status and the six PLCP plus 34 MPDU bytes. A candidate is not a
validated AP reply. The badge copies this record to detail offset 600 only
if the saved authentication-response length at 596 is zero, so successful
response evidence remains intact. JSON exposes `auth_bad_fcs_report`,
`auth_bad_fcs_count` and, for a nonempty record, `auth_bad_fcs_raw`,
`auth_bad_fcs_phy`, `auth_bad_fcs_status1`, `auth_bad_fcs_status2`,
`auth_bad_fcs_time_us` and `auth_bad_fcs_fcs_valid`. A zero count does not
exclude failures before a matching header/length is available.

Station evidence at detail offset 1680 contains `MAC1` plus unprotected
delivery, duplicate, fragment, malformed, unsupported, expiry and fragment-error
counters. Offset 1712 contains the radio status; offset 1776 contains a separate
`MAC1` report summing authenticated non-QoS and all QoS TID receive counters. PN replays are
rejected before this second cache, not counted as authenticated-cache duplicates.
The collector reports these
as `mac_*` fields; `mac_rx_waits` counts backpressure polls, **not lost packets**.
`ack_responses_scheduled`, `cts_responses_scheduled` and
`short_responses_scheduled` count submissions, not independently verified RF
responses. Station profiles 2 and 3 stage frames in the existing eight-slot
SRAM capture ring and release PSM SHM before host consumption. FIFO order and
the single logical F2 credit are preserved. Saturation retains the next
capture in SHM instead of overwriting queued frames. After NAV processing,
these profiles select own-address frames and group management/data from the
pinned peer; this selection does not replace frame validation. Passive/raw
capture is unchanged. `mac_pending_rx` includes held SHM and queued SRAM
frames. Target-based completion drains staging, but an explicit stop can
leave pending RX.

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx all test
make -C clean-rx/badge all test
make test-micro-signal
```

The ASan/UBSan MAC test covers idle/busy/NAV overlap and expiry, timer wrap,
EIFS, partial-slot freezing, CW limits/reset, deterministic draws, Retry/FCS
identity, all supported CCK PLCP lengths, ERP fallback, malformed frames,
late-retry duplicate retention across timeout and timer wrap, fresh non-Retry
identity reuse, incomplete-assembly expiry/restart, maximum-size 16-fragment
reassembly and the actual station
adapter. It delivers 100,000 frames per **simulated** direction, alternating
32-, 1,500- and 2,304-byte payloads, and suppresses their retries. This is
**not** the roadmap's 100,000-frame-per-direction OTA qualification.
PSM tests also run under ASan/UBSan and exercise all 12 receive rates, long/short
responses, 1 Mbps fallback, CTS Duration, NAV gating, rejection and immediate
rearm. Bounded ACK suppression is tested at all 12 rates, including budget
exhaustion/resumption, wrong-TA budget preservation, no TX trigger/ownership
while dropping, and unaffected CTS. They reject multiple memory inputs in every instruction, flip each of
the 96 RA/TA bits for ACK/CTS rejection, and run 100,000 internal ACK rearms
without DMA/header publication, including counter wrap and out-of-window ACKs.
Native request tests cover zero-attempt deferrals, no-ACK Retry/FCS identity,
fresh sequences after ACKed requests, sequence wrap, phase transitions and
corrupted retry rejection. Discovery tests ensure a matching-BSSID probe
response, data, authentication or association response cannot overwrite a
valid beacon result; truncated packets are ignored and malformed complete
beacons still report parser errors. None of these simulated events counts as
OTA traffic.
WPA tests exercise authenticated 16-fragment reception at 32, 1,500 and
2,304 payload bytes, both legacy and 16 simultaneously interleaved QoS TIDs,
PN carry/continuity, MIC/header/key-ID/order failures, per-TID duplicates,
idle expiry across timer wrap, overflow and plaintext wiping. The shared
QoS core also covers normal/No Ack policy, QoS Null and plaintext A-MSDUs.
Protected aggregate tests cover all TIDs, the exact 3,839-byte bound,
whole-body/local-destination validation, no partial delivery, sequence
wrap, replay/Retry separation, MIC failures and aggregate-bit injection.
Independent OpenSSL CCM vectors cover AES block and ordinary/aggregate
size boundaries. Extended loader tests pass a 3,885-byte encrypted MPDU
through three F2 fragments, station protected bypass and authenticated
delivery, including bad-MIC and malformed-final-subframe controls.
Unsupported Block ACK policies remain rejected.
These are native tests, not OTA
qualification of those new paths.
Loader/DMA-credit, WPA2/crypto, secret-provisioning and Micro Signal regressions
pass; all ten existing Micro Signal modes build, and both freestanding ELFs
have no unresolved symbols.

### Continuous raw MAC fixture

Flash the same current clean image onto both badges, leave them in BOOTSEL,
and use a fresh evidence prefix:

```sh
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/mac-soak 5000
# Optionally suppress the first two eligible ACKs at each endpoint:
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/mac-loss 100 2
```

This does not flash or change the Mac's networking. It runs the responder
first, uses a fresh challenge and pinned local addresses, and alternates
32/1,500/2,304-byte payloads at 6 Mbps on channel 1 by default. Each endpoint requires
exact application indices, MAC sequence wrap, payload pattern and FCS,
and actual ACK-confirmed TX. MAC retries preserve sequence/body; only Retry
and FCS change. Duplicate application delivery, corruption, ordering errors,
eight exhausted actual attempts, peer inactivity and overall deadline are
explicit failures. Zero-attempt BUSY does not consume the retry budget.
The real station duplicate/reassembly adapter runs before fixture validation.

For controlled old/new comparisons, `MAC_SOAK_CHALLENGE` accepts exactly
28 lowercase hexadecimal digits from a saved `PREFIX-config.json`.
Use the same count, ACK-suppression, PHY and payload arguments as the
saved run. Invalid values fail before hardware access. Leaving the variable
unset generates a fresh random challenge; intentional replays are comparison
evidence, not new independent freshness trials.

The optional fifth argument, `DROP_ACKS`, is 0..16 and defaults to zero.
Both collectors also accept it after `COUNT`. Only the
explicit profile-3 test command enables suppression; ordinary station callers
never issue it. Only otherwise eligible GoodFCS, pinned-peer ACK responses
consume the budget; CTS and incoming ACK consumption are unchanged. A passing
run must report exactly the requested suppression count at both endpoints.
Config JSON retains the requested count and reports retain the actual count.

Suppression is a register-staged transaction with separated SHM loads and
consumers, using the previously RF-qualified DCF update pattern. It checks
`remaining + dropped == limit`, bounds each counter by the configured limit,
and bounds the limit by 16 before dropping an ACK. A violation disables DCF,
increments the response error once and retains the first fault; it does not
clamp/reset counters or credit a received application reply as an ACK.
Command 9 configures the budget only at a rechecked, paused, PHY-idle policy
boundary with phase zero, and verifies every configuration word by readback.

Soak detail offset 688 contains the 32-byte `AST1` suppression report:
ARM-expected count, SHM limit/remaining/dropped, fault/phase, before/after,
prior dropped count, low TSF, frame control, sequence and configuration epoch.
Phases are 0 idle, 1 checking, 2 committing and 3 fault. Faults are 1 remaining
out of range, 2 dropped out of range, 3 sum mismatch and 4 limit out of range.
`ack_test_healthy` additionally requires the ARM and SHM limits to agree.
Live reports are non-atomic; successful acceptance still requires a frozen,
complete snapshot. Reconfiguration is explicit, not automatic fault recovery.
Command 9 rejects a faulted configuration with `0x87`; collect the evidence
and restart the controller rather than clearing the first fault in place.
All invariant operands, including the limit, are register-staged. The
12-byte `SUM1` record at detail 864 retains saved response airtime, computed
sum and sampled limit. Two independent operand stores precede the sum branch.
Successful acceptance checks these operands against the configured budget.

Soak detail offset 800 contains the last 32-byte `AKR2` received-ACK record,
now captured for legacy as well as opt-in HT responses. JSON includes
`ack_rx_valid`, count, TSF, PHY and the complete 14-byte `ack_rx_mpdu` with FCS.
The collector checks FCS, ACK type, zero final-fragment Duration, own RA,
basic response rate/preamble and a timestamp inside the last ACKed submission's
start/finish window. Count wrap alone is not a stale-ACK failure. A TIMEOUT
cannot pass this check. This is endpoint evidence, not independent RF/SIFS
measurement or a capture of every earlier ACK.

The 32-byte `OBS1` record at detail 832 records the last own-address ACK's
waiting/deadline state and the most recent ARM/PSM expiry source. Its JSON
fields use `ack_observe_*`, `ack_clear_*` and `ack_arm_*`. Expiry and ACK
observations may be from different attempts; their deadlines must match
before comparing them. This trace localized a CCK completion resample that
replaced an already-established tail-based ACK clock with PHY start while
retaining the shorter window. The resample now preserves an ended tail's
clock; timeout allowances and late-ACK rejection remain unchanged.

Both roles retain bounded RX/automatic-response service for 2.1 seconds after
their final counts complete, allowing the peer's final-request or final-reply
retries to be ACKed without duplicate application delivery.

The loader retains its six-second default for ordinary diagnostics. Only the
soak selects a longer bounded budget, continues USB/cancellation servicing,
and renews the existing eight-second BOOTSEL lease every four seconds.
Elapsed microseconds accumulate in 64 bits from successive unsigned 32-bit
clock deltas; servicing must occur at least once per hardware-clock wrap.
Native tests cover the unchanged default, two wraps, exact deadline boundaries
and cancellation. Targets are 1..100,000, with a 7,200-second radio-test
deadline, a loader budget five seconds longer, and a 7,230-second host limit.
Soak-only host polling is every 250 ms, including loading/snapshot margin.
Status at detail offset 520 is now 64-byte `MSK3`, with elapsed low/high words
reconstructed as a 64-bit JSON `elapsed_us`; the on-air `MSK1` payload is
unchanged. The final three words count PHY-checked deliveries, mismatches
and last received rate/flags. Use the matching current collector and image. Soak JSON also
exposes the controller snapshot as `fw_state`, `fw_substage`, `fw_error`,
`fw_error_addr` and `fw_error_val`, rather than hiding it behind state 255.
The earlier runtime ceiling is removed, but a full run has not passed.
This is a raw two-clean-endpoint test, not a vendor interoperability/control
soak, persistent AP station link or independent RF timing measurement.

### DSSS/CCK rate and preamble fixture

The optional sixth argument selects `ofdm6` (default), `1-long`, `2-long`,
`5.5-long`, `11-long`, `2-short`, `5.5-short` or `11-short`. The optional
seventh argument selects `cycle` (default), `32`, `1500`, `1600`, `2304`,
`cck-boundary`, `frag2` or `frag16`. The boundary mode repeats MSDU lengths
1500/1550/1600/1650/1700/1750/1800/1850/1900/2304 bytes, stopping at the
first fixture failure. Fixed-size selection is explicit and recorded in configuration and
endpoint reports; the original cyclic-size coverage is not silently reduced.
Collectors accept `[DROP_ACKS [PHY [PAYLOAD]]]` after `COUNT`.

```sh
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/cck-1-1500 100 0 1-long 1500
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/cck-11-short 100 0 11-short
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/cck-1-boundary 10 0 1-long cck-boundary
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/cck-1-1600 5 0 1-long 1600
```

The last challenge byte encodes PHY in bits 0..2 and payload selection in
bits 3..5 (values 0..7: cycle/32/1500/2304/cck-boundary/frag2/frag16/1600);
any higher bits are rejected. The fixed 1,600-byte profile is a single MPDU,
not fragmentation or a replacement for full-sized reception. Payload seed,
exact length, FCS and sequence validation cover every selection. CCK selects
the four b basic rates; default OFDM retains its existing rate policy.
Every accepted delivery also checks the separate receiver's decoded
modulation/rate and long/short-preamble status. Passing requires exactly
`COUNT` PHY checks and zero mismatches at each endpoint, alongside the
existing ACK, payload and ordering criteria.

Shared PLCP helpers cover all four rates, length extension, CRC, airtime and
legal preambles. Short CCK TX uses opcode 10; opcode 5 retains its existing
layout. Receiving packet byte 16 publishes short preamble (`0x20`) and
unsupported PHY (`0x40`) without changing the MPDU offset. Fragmented
transport preserves and checks these flags. Native tests cover all lengths,
header-bit corruption, fixed independent CRC vectors, all fixture PHY/size
selections and deliberate receiving-PHY mismatches.

This is C1 PHY/rate evidence, not C2 b-only association, C3's 1,000-frame
per-cell qualification, calibrated RF/EVM/spectral-mask evidence, or
independent SIFS measurements. Full-sized 1-Mbps reception is still failing
and must not be represented as qualified.

#### Independent CCK boundary monitor

The vendor peer has a separate `--cck-monitor SOAK_NONCE_HEX_28` capture
mode (9). Build the peer/collector with the normal
`make -f clean-rx/peer/Makefile ht-peer ht-peer-test` command for a badge;
`HT_PEER_PICO=1` selects Pico wiring. Preserve matching build outputs.
Save full flash before borrowing any device, install the matching images
explicitly, and restore and compare the complete original flash afterward.

Unlike `--monitor-probe`, this mode actually receives for ten seconds on
channel 1. It disables power saving/MPC, enables and checks monitor and
promiscuous mode and the actual/target channel, then disables/readback-checks
both receive modes before stopping. It requests no application/data TX;
`sdpcm_tx` counts control requests too, not RF transmissions.

The monitor nonce must match the **effective** soak nonce, not just its random
challenge: bytes 8..11 contain little-endian `COUNT`, byte 12 is the requested
ACK-drop count, and byte 13 encodes PHY/payload as above. For example, generate
the monitor nonce for `COUNT=5`, `DROP_ACKS=0`, `1-long`, `1600`:

```sh
# Use the same fresh 28-hex challenge in the clean collector.
soak_nonce=$(node -e 'const n=Buffer.from(process.argv[1],"hex"); if(n.length!==14) process.exit(2); n.writeUInt32LE(5,8); n[12]=0; n[13]=57; console.log(n.toString("hex"))' "$challenge")
build/clean-rx/peer/collector MONITOR_SERIAL build/cck-monitor.bin --cck-monitor "$soak_nonce"
# In another terminal, boot the clean sender and run before the monitor ends:
build/clean-rx/badge/collector TX_SERIAL build/cck-tx.bin 1 --mac-soak-init "$challenge" 5 0 1-long 1600
```

The monitor scans validated non-control vendor SDPCM envelopes for the exact
fixture addresses, nonce-derived payload pattern, length and complete FCS.
It retains attempt counts, largest validated MSDU, per-role index masks
(bit `index % 10`), the latest length/FCS/offset, and the first/last up to
64 bytes of the latest validated MPDU. Retries are attempts, **not** fresh
application deliveries. A successful collector exit means capture setup,
collection and cleanup succeeded; `complete=0` is not reception proof or
a passing C1 qualification. There is no decoded PHY-rate/preamble proof
in this opaque vendor format (`phy_rate_verified=false`).

The trace also records first/last raw-packet arrival, setup-ready and finished
times, largest inter-packet gap, and the packet count/last arrival **before
cleanup**, all times in microseconds since monitor setup began. Cleanup can
drain queued packets: the overall last-arrival time alone does not prove
continuous capture. These counters include ambient non-control packets;
only the fixture-matched complete MPDUs establish payload/FCS evidence.

`--cck-monitor-poll SOAK_NONCE_HEX_28` explicitly opts into a GET_MONITOR
control request every 100 ms. The default remains unpolled. This is a
diagnostic, not a fix: the retained polled station trials all fail with
firmware timeout 6 after 46 raw packets, despite partial valid captures.
Failed collection preserves evidence and exits unsuccessfully.

`--phy-probe` separately attempts seven GET_PHYREG (command 94) reads after
bringing the vendor radio up on channel 1. It uses an eight-byte zeroed
register/band request for addresses 0x42c/0x465/0x464/0x466/0x430/0x431/0x432;
that layout and the registers' meanings are not established by successful
hardware readback. The shipped firmware rejects every read with controller
status -23. `phy_readback_supported=false` and a nonzero collector exit mean
the reported zero placeholders **are not register values**. No PHY-register
writes or application/data TX are requested.

`--phy-window-probe` is a separate, opt-in alternative that successfully reads
the same seven registers on the Pico:

```sh
make -f clean-rx/peer/Makefile HT_PEER_PICO=1 \
  HT_PEER_BUILD=build/clean-rx/cck-window/peer ht-peer
# Back up the Pico and flash the matching vendor_ht.uf2 before collecting.
build/clean-rx/cck-window/peer/collector 6572BE3E4636AF51 phy-window.bin --phy-window-probe
```

Build and flash the matching peer with `HT_PEER_PICO=1` first, using the backup
and restoration procedure above. This mode uses genuine 16-bit backplane
accesses: no force-32-bit address flag, and no write to the adjacent PHYDATA
halfword. Each query saves the PHY selector, selects and reads back the
requested address, requires two equal data reads, and restores the selector.
It does not write PHYDATA. Restoration failures and inconsistent reads fail
explicitly. Trace mode 4 and `phy_window_probe=true` distinguish this path
from the unsupported command-94 probe; `phy_version_raw` is not a decoded
hardware revision. The collector requires all seven successful 16-bit values
and a nonzero/non-`0xffff` version before reporting readback supported.

The vendor firmware remains running and could independently access the PHY
selector. Readback checks and repeat cold trials do not make these operations
atomic; this is diagnostic evidence, not an interface for production PHY
modification. Three initial cold Pico trials agree on version `0xdc00` and
values `2590, 10, 0, 2000, 0, 0, 46336` in the address order above.
The suspected `0x42c` and `0x465` values match clean defaults. These readings
do not establish undocumented register semantics or justify changing them.

The vendor monitor transport's 2,040-byte SDPCM limit remains unchanged: it
cannot receive a complete 2,332-byte MPDU. Never infer its complete waveform
from a matching prefix or a source template counter.

The retained campaign independently validates eight attempts each at 32 and
1,500 MSDU bytes with the second badge as vendor listener, but no complete
1,600-byte attempt. The Pico listener misses even the small positive
controls, so its absence of captures cannot localize the boundary failure.
Later 100-ms-polled station trials retain three complete 1-Mbps/1,500-byte
and eight complete 2-Mbps/1,600-byte attempts, but no complete
1-Mbps/1,600-byte attempt. Every polled capture operation itself times out;
these are partial frame observations, not passing reference qualifications.
They exclude a simple reference packet-size cap at 1,628 MPDU bytes, but do
not distinguish incomplete 1-Mbps TX from receive-duration limitations.
The clean paired boundary failure still reproduces. See
[the current investigation](../dev-doc/history/LEGACY_PHY_MILESTONES.md#c1-independent-boundary-investigation).

#### Pico vendor-managed CCK source

The independent peer can also generate eight fresh ordinary broadcast data
frames at fixed 1 or 2 Mbps, with long preamble. This uses the unchanged
official CYW43 radio firmware and its managed open AP, not packet-engine
or clean TX code. The production packet-engine queries remain unsupported.
No laptop association or local network credentials are needed.

```sh
make -f clean-rx/peer/Makefile HT_PEER_BUILD=build/clean-rx/pico-reference/peer \
  HT_PEER_PICO=1 ht-peer ht-peer-test
make -C clean-rx/badge BUILD="$PWD/build/clean-rx/pico-reference/badge" all test

# Back up complete flash, install matching UF2s and boot the badge first.
# Use the same fresh 28-hex challenge in both commands.
build/clean-rx/pico-reference/badge/collector BADGE_SERIAL build/cck-source-rx.bin \
  1 --vendor-cck-rx "$challenge" 1 1606
build/clean-rx/pico-reference/peer/collector PICO_SERIAL build/cck-source-tx.bin \
  --tx-cck "$challenge" 1 1606
```

Run the two collectors concurrently, allowing for the badge's cold radio
initialization before starting the source. The tested harness launched the
Pico about 1.8 seconds after the clean collector, plus USB boot/enumeration
time. Restore and compare each complete original flash afterward. The
receive-only fixture has a ten-second total loader budget covering cold
initialization, 4.2 seconds of RX and shutdown; it does not extend a PHY wait
or change other modes' deadlines.

Both commands take rate `1` or `2` and Ethernet size `62..2014`, with size
modulo four equal to two so SDPCM alignment cannot append Ethernet payload
bytes. Challenge bytes 11/12 encode size; byte 13 encodes `0x80 | Mbps`.
The Ethernet size includes its 14-byte header. A plain From-DS MPDU is
Ethernet size + 22 bytes including FCS (QoS adds two more). Thus size 1606
tests the same 1628-byte MPDU length as the clean 1600-byte MSDU fixture;
size 1614 contains 1600 Ethernet payload bytes but produces a 1636-byte MPDU.

The source verifies multicast-rate and long-preamble readback and sends at
most eight frames at 100-ms target intervals. A separate explicit oversized
TX diagnostic permits packets within the existing 2040-byte SDPCM buffer;
normal Ethernet TX/RX retains its 1514-byte maximum. The size bound is a host
buffer bound, **not** a claim that vendor firmware transmits every size.
Version-10 vendor counters are sampled before/after: eight accounted data
frames and no new TX errors are required. A counter mismatch is an explicit
failure, even if all eight host submissions succeeded. Counters alone do
not establish a complete OTA waveform (`tx_over_air_confirmed=false`).

The clean receiver checks the complete nonce-bound payload, addresses,
sequence and FCS on-device, plus decoded CCK rate, channel and long preamble.
It reports distinct sequence masks/duplicates; it does not infer long-frame
validity from the retained prefix. `fresh_cck_rx=true` means at least one
complete valid capture, not eight-of-eight reliability; inspect
`all_frames_received`. Neither result completes C1 or C3.

A second vendor peer supports
`--vendor-cck-monitor CHALLENGE_HEX_28 MBPS ETHERNET_BYTES` for these exact
AP fixtures, using the same passive channel-1 setup. Its opaque envelopes
still do not verify PHY rate, and its forwarding cutoff remains unqualified.

The Pico control passes eight-of-eight small frames at both rates. Final
independent tests receive 8/8 1580-byte MPDUs at 1 Mbps, 4/8 at 1584 bytes,
and 0/8 at 1588 and 1628 bytes; the same four sizes pass 8/8 at 2 Mbps.
The Pico accounts all eight TX frames at each of those sizes, with no new
TX errors. Size 1714 Ethernet is a separate **invalid stimulus**: vendor
data-TX counters remain zero at both rates despite host submissions, and
the final source fails explicitly. The vendor badge monitor also receives
the 1636-byte fixture at 2 Mbps but not 1 Mbps. A complete long 1-Mbps
waveform is therefore still unproven, even with official vendor endpoints.
See [the retained reference campaign](../dev-doc/history/LEGACY_PHY_MILESTONES.md#c1-pico-vendor-reference-campaign).

#### Associated vendor CCK receiver

An additional opt-in reference avoids passive-monitor forwarding entirely:
one vendor peer runs the open AP and the other joins as an ordinary managed
station. Neither uses the clean transmitter or receiver. Only the temporary
nonce-derived test network is joined; laptop networking is not changed.

```sh
make -f clean-rx/peer/Makefile HT_PEER_BUILD=build/clean-rx/cck-managed/peer \
  HT_PEER_PICO=1 ht-peer ht-peer-test
make -f clean-rx/peer/Makefile HT_PEER_BUILD=build/clean-rx/cck-managed/badge ht-peer

# Back up full flash and install each device's matching vendor_ht.uf2.
# Run in separate terminals with the same fresh 28-hex challenge.
build/clean-rx/cck-managed/peer/collector PICO_SERIAL build/cck-associated-tx.bin \
  --tx-cck-associated "$challenge" 1 1606
build/clean-rx/cck-managed/badge/collector BADGE_SERIAL build/cck-associated-rx.bin \
  --vendor-cck-station "$challenge" 1 1606
```

Start the AP source first and the station promptly afterward. The source
waits up to eight seconds for the exact nonce-derived station MAC before
starting its existing eight-frame burst. The station also requires the exact
AP BSSID and collects for up to four seconds after association. Both require
the same rate/size arguments as `--tx-cck`. The association-gated source
checks fixed multicast rate and long-preamble settings again after
association and after transmission, as well as the existing data-TX counters.
Legacy `--tx-cck` timing and behavior are unchanged.

Station mode 13 uses the existing raw SDPCM callback **without enabling
monitor mode**. An explicit diagnostic BDC decoder accepts larger Ethernet
payloads within the existing 2040-byte SDPCM envelope; normal RX queues,
Ethernet MTU, and the normal decoder remain limited to 1514 bytes. Complete
destination/source/EtherType, nonce, pattern and all eight sequences are
checked on-device. Missing frames, duplicates, malformed data or fixture
corruption fail the station collector with retained evidence and exit 1.
`valid=true` requires eight complete distinct frames, not just association.

This path observes complete decapsulated Ethernet frames, **not** MPDU FCS
or independently decoded PHY rate. JSON explicitly reports
`mpdu_fcs_visible=false` and `phy_rate_verified=false`. A successful payload
therefore must not be described as an independent raw waveform capture or
used alone to complete C1.

The initial Pico-to-badge controls pass 8/8 at 1 Mbps with 62/1514 Ethernet
bytes and at 2 Mbps with 62/1606 bytes. At 1 Mbps / 1606 bytes, one complete
frame (sequence 5) arrives, but seven do not: the operation correctly fails.
Simultaneous vendor/clean receivers both pass their short-1-Mbps and
long-2-Mbps controls, then both miss all eight long-1-Mbps frames in two
trials. Reverse badge-to-Pico reception also loses positive-control frames,
so it is not a qualified reference direction.
See [the associated-reference evidence](../dev-doc/history/LEGACY_PHY_MILESTONES.md#c1-associated-vendor-reference-investigation).
Always restore and compare each complete original flash after testing.

### Bounded legacy fragmentation fixture

`frag2` and `frag16` send each 2,304-byte MSDU as respectively two 1,152-byte
or sixteen 144-byte payload fragments. Each fragment is a complete MPDU with
its own PLCP, FCS, fragment number and ACK/retry exchange; these are not F2
transport fragments or A-MPDUs. `COUNT` still counts complete MSDUs per
direction, not fragments. For example, after flashing matching images:

```sh
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/mac-frag2 100 2 ofdm6 frag2
bash clean-rx/tools/badge/mac_soak.sh INIT_SERIAL REPLY_SERIAL build/mac-frag16 20 2 ofdm6 frag16
```

The controller now admits bounded legacy To-DS Data fragments through its
reliable-transmit header check. Empty fragment bodies, More Fragments on
fragment 15, other DS layouts, Order and fragmented management/QoS frames
remain rejected. No ordinary station sender is changed to fragment its MSDUs.
Each fragment uses the existing per-MPDU contention/retry path: this is **not**
a SIFS-timed fragment-burst scheduler or RF timing qualification.

Before reassembly, the receiving badge checks each bound-peer fragment's
exact challenge/index-derived bytes, original FCS, fragment identity and
decoded PHY/channel metadata. The real station adapter performs reassembly
and duplicate suppression. Complete MSDUs must carry the reconstructed-FCS
marker; unfragmented controls must not. More Fragments reserves the next
fragment and its ACK in Duration; the final fragment reserves only its ACK.

The original 64-byte `MSK3` status is unchanged. An additional 100-byte `MSG1`
record at detail offset 584 supplies `fragments_per_msdu`, `tx_mpdu_acked`,
`rx_mpdu_checked`, `reassembled`, `fragment_last_index`, `fragment_bitmap`,
`fragment_retry_bitmap`, `fragment_fcs` and `reassembled_fcs` in collector JSON.
The bitmaps and per-fragment FCS describe the final received application index;
the retry bitmap records retries observed there, while the FCS is the last
received original MPDU FCS. Earlier indices are checked on the receiving badge,
not retained as individual host captures. `tx_acked` retains its complete-MSDU
meaning; `tx_attempts` and `tx_timeouts` count per-fragment attempts.
`mac_fragments` counts incomplete accepted reassembly steps, excluding the
final delivery and suppressed duplicates.
The reconstructed FCS is locally recomputed over the completed MPDU, retaining
fragment zero's Duration; it is **not** an original over-the-air full-MSDU FCS.
Collector acceptance requires exact ACKed-MPDU/reassembly counts, all final
fragment FCS values, complete bitmap, reconstructed FCS and clean MAC errors.

Build `60f95c36` passed 100 two-fragment and 20 sixteen-fragment MSDUs per
direction with two deliberately suppressed ACKs at each endpoint. A longer
sixteen-fragment target failed at the existing controller TX-completion fault;
an earlier two-fragment attempt also exhausted retries. Native templates
cover all existing PHY/preamble selections, but this new RF evidence is only
6 Mbps OFDM on channel 1. See the
[complete trial/failure record](../dev-doc/experiments/mac/LEGACY_FRAGMENTATION.md).
A1/A2/A3 remain incomplete.

### Quiescent TX-completion timeout recovery

Profile 3 now preserves fired ownership across an access-cancellation race and
latches each Data submission's MAC/PHY completion conditions separately, before
RX priority handling and automatic response setup. A fresh trigger clears the
previous submission's evidence. The original TX diagnostic/mailbox is unchanged.

A missing TXE done event is **not success**. If PHY start was observed and
the MAC/PHY are quiescent without PHY error, the attempted submission can
return `CLEAN_UNICAST_TIMEOUT` rather than terminating the controller when
there is no response owner, or when a completed response owns raw done status.
Response completion never establishes Data success; active/incomplete response
ownership remains fatal. The timeout counts against the existing retry bound
and grows CW.
PSM cleanup independently rechecks RX/TX/ownership hazards before resetting
template controls, preserving held RX captures. Further TX defers until cleanup.
The caller retries the same sequence/fragment/body; protected retries retain
their existing ciphertext/PN behavior. Other completion faults remain fatal.

A separate 28-byte `MTC1` record at controller address `0x204f0` is copied to
detail offset 492 for station/soak reports. JSON exposes
`mac_tx_completion_report`, `mac_tx_completion_latched`,
`mac_tx_completion_preserved`, `mac_tx_completion_raw`,
`mac_tx_completion_request`, `mac_tx_completion_pipeline` and
`mac_tx_completion_timeouts`. The last counter distinguishes explicit
missing-completion TIMEOUTs from ordinary lost-ACK timeouts; it is included in,
not added to, normal attempt/timeout accounting. Retained bits are 1 = MAC not
busy, 2 = MAC done, 4 = PHY idle and 8 = sampling closed before response
reuse. Only the lower three bits establish complete Data evidence; retired
response status cannot fill missing Data bits. These are local engine observations,
not RF timing evidence.

Build `7f4875b7` passed 1,000 maximum-size MSDUs per direction in each of the
two-/sixteen-fragment profiles, plus 5,000 mixed-size MSDUs per direction
across sequence wrap. All passing runs deliberately suppressed two ACKs per
endpoint and reported zero corruption, duplicate application delivery or
ordering errors. An eight-suppressed-ACK negative trial verified eight
failed attempts, CW 1023 and no false success/controller fault.
That build's 100,000-per-direction mixed-size attempt stopped near 48,900
MSDUs after a completed response blocked missing-Data-completion recovery.
Earlier build `08ff63a8` includes that quiescent-response TIMEOUT case and passed
1,000 sixteen-fragment MSDUs and 5,000 mixed-size MSDUs per direction.
Its exact-challenge 100,000 retry failed immediately without eligible peer
receptions. Its exhaustion negative correctly bounded eight attempts but
suppressed only six eligible ACKs, not eight. These failures are retained;
the sustained gate remains open.
See [all trials, failed approaches and unresolved causes](../dev-doc/experiments/mac/COMPLETION_AND_RECOVERY.md).
The physical cause of the missing TXE event and a separate first-no-reception
failure remain unresolved; this is explicit bounded recovery, not a claim that
those underlying RF/event conditions have been eliminated.

Follow-up localization fixes the badge's uninitialized TIMER0 reference,
retired-response contamination of incomplete Data evidence, and RX-priority
loss of an observable PHY-start pulse. Profile-3 completion now uses a
single `airtime + 2,000 us` elapsed-time bound instead of three
iteration-count waits. While PSM holds an RX capture, a guarded ARM observer
can retain real Data completion only before any response reuse. Capture
bytes and RX credits are not discarded.

Soak JSON adds `progress_*`, `clock_*`, `host_submit_before_us`,
`host_submit_after_us`, `host_report_us`, `snapshot_flags`,
`snapshot_stop_error`, `mac_tx_phy_start_handoffs` and
`mac_tx_completion_handoffs` and `closing_duplicates_suppressed`. The separate
`MSP1` record at detail offset 720 is currently 80 bytes. Frozen complete
snapshots have flags 7; live
non-atomic snapshots have flags 6 and cannot pass acceptance. Per-fragment
results start at `0xffffffff`, not stale success. Use a matching image and
collector. First-send timing is command preparation, not an on-air timestamp.
After its final reply delivery, the initiator services RX for the peer's
existing two-second retry bound plus one 100-ms command bound, allowing a
lost final ACK to be retried before shutdown. This does not increase retries
or accept a duplicate application delivery.

Earlier localization build `d4ae73fb` passes those closing controls in both roles and a
5,000-per-direction mixed-size run. Its fragmented run lost the initiator's
USB device/report and is not a pass. The preceding `3bb01fa7` exact long
replay reached 64,003 deliveries per endpoint before ACK exhaustion, with
9 suppressed ACKs reported on a peer configured for 2. These distinct
remaining failures, and initial no-reception failures, keep A1/A2/A3 open;
the detailed record above retains all trials and verified flash restoration.

The A2 follow-up build `b0aedeee` adds guarded register-only suppression,
returned-ACK validation and the CCK physical-tail ACK-clock fix described above.
It passes the exact earlier 5.5-Mbps sum-fault replay, 5,000 cyclic-size
MSDUs per direction, 100 two-/sixteen-fragment MSDUs per direction, both
one-MSDU loss controls and both fully controlled eight-drop exhaustion
negatives. Its final campaign has 21 positive paired passes and **3 positive
paired failures**, not an overall pass. All eight PHY/preamble profiles have
a successful final control; three role/profile combinations still fail.
The failed CCK responders have missing MAC-done and PHY error `0x0010`,
explicitly excluded by the existing incomplete-completion recovery guard.
No final suppression invariant faults occur. See the
[final A2 qualification and retained failures](../dev-doc/experiments/mac/COMPLETION_AND_RECOVERY.md#final-rf-qualification-b0aedeee).
The known 1-Mbps/full-size truncation, sustained and independent ACK/SIFS/
full-credit gates remain open. Both original full flashes were restored and
independently verified; Pico is unchanged.

### Owned CCK PHY-error retirement

The earlier blanket nonzero-PHY-error exclusion is now narrowed for proven
quiescent CCK incomplete transmissions. Known observed status `0x0010`,
`0x0020` or `0x0030`, long or short preamble, can be retired through the
ownership-checked abort/rearm handshake as a **failed attempt/TIMEOUT**.
No Data completion, ACK or application delivery is credited. Existing retry
limits, CW behavior and sequence/fragment/protected-byte identity remain
unchanged; unknown bits, active hardware and unsafe ownership remain fatal.
Status can be sticky across successful packets, so the guard preserves stable
prepare/armed/fired baselines and checks current ownership and a bounded idle
error sample instead of treating every nonzero bit as a fresh fault.

Soak detail adds the 100-byte `TXP1` phase trace at 876 and the 40-byte `PTA1`
sticky first-retirement/count record at 1960. JSON exposes `tx_phases`,
`tx_select_operand`, `tx_select_readback`, `tx_plcp`, `tx_phy_aborts`,
`tx_phy_abort_valid` and `tx_first_phy_abort`. The first-retirement record
survives later successful attempts; the phase trace describes the last
attempt. Its final phase starts with the first quiescent error sample and is
refreshed when known latched bits accumulate; it is not necessarily the first
assertion of a PHY error bit. Unknown observations are retained, not overwritten
by later known status. Use matching firmware and collector.
Acceptance validates both timeout bounds on the abort count and exact
MPDU-attempt accounting, including fragmented MSDUs.

Additional soak evidence exposes `phy_first_bit8` (first sampled new PHY
`0x0008`, or `null`) and `soak_bad_fcs` (a bounded matching rejected Data
prefix, reusing the existing rejected-authentication capture). Data capture
requires active receive streaming and its prefix threshold (normally 128
RXE bytes), so an unstreamed short frame cannot be labelled from stale SHM.
The former
includes prior/current status, timestamp, TXE/IFS/PHY controls, byte count and
sampled owners; it is not an atomic trace or exact silicon assertion time.
The latter saves only 40 RXE bytes, including six PLCP bytes, as 80 hex
characters with RX length/status/time. A zero capture count does not exclude
corruption or an earlier RX abort, and a prefix cannot prove full-frame FCS.
Snapshots occupy detail offsets 1040 and 1616 respectively, without changing
existing packet/status records. No rejected frame is ACKed or delivered and
the unknown-error guard remains intact. Use matching firmware and collector.
See [PHY/RX provenance and retained failed controls](../dev-doc/experiments/mac/COMPLETION_AND_RECOVERY.md#phy-bit-0x08-and-missing-rx-provenance-follow-up).
Targeted build `77bae4fe` passes four 2,000-MSDU/direction 2-Mbps stress
pairs, four exact exhaustion negatives and a zero-injected-loss control.
Its original diagnostic saved three matching SHM prefixes at RXE length 62,
but their attribution to the rejected frames is withdrawn: short rejected
frames can leave a previous frame's header in SHM. The freshness gate above
fixes that diagnostic false attribution, not the physical loss. Bit `0x0008`
is not reproduced, and earlier startup/negative failures remain retained.

For legacy-only receive-copy investigation, matching controller and badge
builds can opt into `EXTRA_CFLAGS=-DCLEAN_RX_REJECT_PROBE=1`. This poisons
the first 66 receive-scratch bytes before the prefix-copy request and saves
the first rejected frame, without claiming it matches the peer, including
an additional 26 bytes plus the raw next-length (`next_length`, `0x102b`)
and requested-copy (`copy_length`, `0x102a`) registers.
JSON `rx_reject_probe` contains a 66-byte/132-hex-character backing-memory
sample, including any unwritten poison bytes; it is not a claim that 66 bytes
were received. In probe builds `soak_bad_fcs` is `null`, rather than labelling
this generic snapshot as matching Data. The probe adds receive-path instructions,
is disabled by default and is not compatible with HT RX or an RF-timing
qualification.

Do not interpret raw next-length `0x8000` or copy-register zero alone as an
invalid RF frame or an unsafe copy. An experimental minimum-length guard
passed native tests but prevented every reception in five hardware controls;
it was withdrawn. Native tests now preserve the original zero-permitted
branch over all 65,536 operands and retain the upper-bound/bad-length handling,
EIFS, deferred access and no ACK/delivery on bad-length rejection.

The selector is correctly `0x0826` before trigger; later `0x082c` is engine
state. Explicit PLCP refreshing was tested and removed because it did not
fix the failures. The first guarded candidate recovers an actual `0x0010`
event and passes six exact/repeated 100-MSDU controls. This does not establish
the silicon meaning of those bits or eliminate the separate no-first-RX,
1-Mbps truncation, sustained and independent RF-timing gates.
See [provenance, guard and retained trials](../dev-doc/experiments/mac/COMPLETION_AND_RECOVERY.md#cck-phy-error-retirement-and-retained-provenance).

Broad-campaign build `a4eefd5a` retires 12 real PHY errors, including a sticky
`0x0020` baseline followed by `0x0030`. Its 34-case campaign has 29 passing
positive pairs, three failed positive pairs and two controlled exhaustion
negatives. All 16 legacy PHY/role matrix controls pass (1 Mbps uses 1,500
bytes), as do 5,000 mixed-size OFDM MSDUs/direction, 1,000 two-/sixteen-fragment
MSDUs/direction and both closing controls. The three failures exhaust the
initial eight attempts without first RX, with no controller/PHY fault.
The independent raw-record/ACK/fragment oracle retains those failures and
reports **QUALIFICATION: FAILED**. This resolves the observed fatal handling,
not every remaining RF failure or the independent timing/sustained gates.
Later replays expose a stale first-error sample followed by additional known
bits; final source `460b7228` fixes that sampling case without relaxing
completion/ownership checks. A further `0x0028` baseline / `0x0038` failure
contains unqualified bit `0x0008` and remains explicitly fatal. ACK exhaustion
also remains possible after initial deliveries. See the retained follow-up
campaigns above; final-source testing does not replace the earlier failures.
Final-source focused controls pass 1,000 cyclic 2-Mbps and 100 short-preamble
MSDUs/direction. Forward exhaustion is fully controlled; reverse exhaustion
has eight attempts but only five eligible suppressed ACKs and is explicitly
**not a passing controlled negative**. The overall final campaign remains
failed, with no false success or widened retry bound.

### Historical CCK fixture results

Final build `9c3deabc`, channel 1, evidence prefix
`build/clean-rx/badge/c1-final-PHY-SIZE`:

| PHY/preamble | 32-byte MSDU | 1,500-byte MSDU | 2,304-byte MSDU | Received rate/flags |
| --- | --- | --- | --- | --- |
| 1 Mbps long | 100/100 | 100/100 | **Incomplete** | `010a` |
| 2 Mbps long | 100/100 | 100/100 | 100/100 | `0114` |
| 5.5 Mbps long | 100/100 | 100/100 | 100/100 | `0137` |
| 11 Mbps long | 100/100 | 100/100 | 100/100 | `016e` |
| 2 Mbps short | 100/100 | 100/100 | 100/100 | `2114` |
| 5.5 Mbps short | 100/100 | 100/100 | 100/100 | `2137` |
| 11 Mbps short | 100/100 | 100/100 | 100/100 | `216e` |
| 6 Mbps OFDM | 100/100 | 100/100 | 100/100 | `118b` / `110b` |

Counts are ACK-confirmed TX/received application frames **at each endpoint**,
not totals pooled between directions. The final column encodes packet bytes
15 and 16 in a little-endian word; OFDM SIGNAL's upper nibble varies with
length. The 23 passing cells total 4,600 application deliveries with exact
PHY checks, 4,639 actual attempts and 39 ACK timeouts, with zero corruption,
duplicate delivery, ordering errors, PHY mismatch or controller faults.

The full-size 1-Mbps cell did not launch successfully: inspection of
`c1-final-1-long-2304-run.log` shows picotool aborting during reboot/BOOTSEL
(`cancelled_exception`), not an RF timeout. Its incomplete reports are not
RF evidence and no success or packet-loss rate is inferred.
Earlier build `b678cb3e`, prefix `c1-tail-1-long`, reached that size after
successful small/1,500-byte exchanges and exhausted eight attempts without
delivery. A hardware-clock RX-deadline experiment did not fix it and was
removed; its evidence remains separate. The cause is unresolved.

Follow-up build `956a3e85`, prefix `build/clean-rx/badge/c1-cause-retained`,
reproduces the RF failure with complete endpoint reports: `full` decodes
the correct 2,332-byte MPDU PLCP but RXE reports only 1,589 bytes at discard.
The TX template is consumed completely; this is not independent waveform
verification. In `reverse`, swapped roles pass 1,500/1,550-byte MSDUs, then
fail at 1,600 with the correct PLCP and the same RXE count. Other trials
report 1,590 bytes. A removed doubled-wait experiment reaches about 32.8 ms
instead of 16.4 ms after PLCP, without receiving the missing bytes.
No complete GoodFCS MPDU is published, so missing ACKs are downstream of
incomplete reception. The exact initiating PHY/RXE condition remains unknown;
no hardware watchdog or timeout-register fix has been established.
On the retained build, `2-long`, `11-long` and `ofdm6` each pass 30
cyclic-size frames per direction, including full-size MSDUs.
See the [investigation and excluded causes](../dev-doc/history/LEGACY_PHY_MILESTONES.md#c1-all-legacy-dssscck-rates).

Failure collection stops the soak before reading diagnostics and preserves
the original failure code. Reports now include `tx_diagnostic`, `psm_diag`,
`rx_cck_trace`, `cck_tx_tail_us`, `txe_timeout`, `tx_template_end`,
`tx_count_end`, `phy_465`, `phy_42c` and `rx_fifo_fault`. These are raw
diagnostics, not additional success criteria. Trace timing is meaningful
only when the retained PLCP LENGTH matches the failed LENGTH; low TSF
differences wrap modulo 65,536 us. See the
[trace layout and limitations](../dev-doc/history/HARDWARE_BRINGUP.md#1-mbps-receive-truncation-diagnostics).

Intermediate build `786ad5ee`, prefix `c1-matrix-*`, exposed one valid OFDM
frame paired with stale DMA PHY status and had an interrupted OFDM full-size
trial. The final build uses the held PSM header for the same capture, not
the lagging DMA descriptor; native image tests pin that PHY-status latch
before publication. Final OFDM cells pass without metadata mismatch.
Intermediate and final-build counts are not pooled.

Both originals were restored and independently read/compared after this matrix:
the 33,604-byte saved application range for `08DA26F86E57F5EA` and full
16-MiB flash for `9BBF88DD17D66E7D`. Both remain in BOOTSEL. No Mac networking
changes were made. C1 remains partial pending the full-size 1-Mbps failure;
see [milestone status](../../todo.md#c1-all-legacy-dssscck-rates).

Build `2269c6c0`, evidence prefix `build/clean-rx/badge/mac-a-soak-owner`:

| Endpoint | ACK-confirmed / received | TX attempts | ACK timeouts | Deferrals | Elapsed |
| --- | --- | --- | --- | --- | --- |
| Initiator | 5,000 / 5,000 | 5,169 | 169 | 6,231 | 225.05 s |
| Responder | 5,000 / 5,000 | 5,064 | 64 | 3,619 | 228.27 s |

Both reported zero corruption, duplicate application delivery and ordering
errors, and one suppressed MAC duplicate each. The run crossed sequence
wrap and repeatedly renewed the watchdog lease. It is not the 100,000-frame
acceptance gate. Earlier retained failures include `a5c9577d` after 2,615
frames per direction (controller TX-completion fault), and a `54054044`
100,000-target attempt near frame 523 (missing initiator USB report and
responder retry exhaustion). Do not pool different builds or exclude failures.

#### Controlled-loss continuation and retained failures

Build `5d7008c7` passed an initial ten-frame-per-direction run with two
deliberately suppressed ACKs per endpoint (`mac-a-fault-two` under
`build/clean-rx/badge/`). Its subsequent 10,000-target run,
`mac-a-fault-long`, failed: the initiator reported 483 ACK-confirmed TX,
482 received and one duplicate application delivery; the responder reported
482 ACK-confirmed TX, 483 received and retry exhaustion. Inspection exposed
a concrete bug: the duplicate cache expired after 512 ms even though a
single fixture retry exchange can last two seconds. The late-retry regression
failed before the fix. Delivered identities now persist independently of
incomplete-assembly expiry; this is not an RF diagnosis of every lost ACK.

The repeat on intermediate build `3414b640`, `mac-a-duplicate-recovery`,
also failed: initiator 449 ACK-confirmed/448 received, responder 448/449.
Neither reported duplicate delivery, corruption or ordering errors, but the
responder stopped with controller error `0x82`, substage 5, address
`0x1800150e`, value `0x00c1002f`: a TX-completion-condition failure. The
initiator then timed out waiting for its peer. This fault remains unresolved;
neither 10,000-target run passed.

Final build `9c6fb6d6`, prefix `mac-a-final-recovery`, passed a **100-frame
smoke test**, not an extended soak:

| Endpoint | ACK-confirmed / received | TX attempts | ACK timeouts | Deferrals | Suppressed ACKs | Suppressed MAC duplicates |
| --- | --- | --- | --- | --- | --- | --- |
| Initiator | 100 / 100 | 117 | 17 | 67 | 2 | 7 |
| Responder | 100 / 100 | 115 | 15 | 40 | 2 | 4 |

Both reported zero corruption, duplicate application delivery and ordering
errors. On that same final build, `mac-a-final-exhaustion` configured eight
suppressed responses and correctly failed at index zero with exactly eight
actual attempts/eight ACK timeouts, zero ACK confirmations and `failure:3`,
`error:2` at both endpoints. Each delivered the first application frame once.
Actual suppressed ACKs were 7/3, not eight each: the configured budget does
not imply every transmitted frame was received. Exact positive/negative
JSON assertions passed. These are controlled **missing-ACK** tests, not
late-ACK, SIFS or occupied-credit qualification.

The same `2269c6c0` image was subsequently tested against unchanged vendor
AP `0353f1de`, channel 1, with evidence prefix
`build/clean-rx/badge/mac-a-psm-vendor`. The runner confirmed 3/10 protected
trials at both endpoints, both wrong-password controls and all three open
controls. Three additional station reports validated protected echoes but
lacked the required independent AP observation; the other four protected
trials lacked a usable station report or AP readiness. Observed failures
included USB report/transfer errors on the clean station and unchanged vendor
peer. All six complete positive station reports authenticated/associated on
their first requests. The runner correctly exited unsuccessfully: seven
unconfirmed trials are retained, not excluded or counted as passes. These
fixture/report failures remain unresolved, and do not establish whether every
unconfirmed trial's RF exchange succeeded.

After these profile-3 tests, badge A's original 33,604-byte application was
restored and independently compared; badge B's entire 16 MiB flash was read
back and compared to its original full backup after restoring the modified
prefix. Both comparisons passed and both badges were left in BOOTSEL.
Restoration was repeated after the controlled-loss continuation: independent
readbacks `mac-a-final-restored-A.bin` (application only) and
`mac-a-final-restored-B-full.bin` (entire 16 MiB), under
`build/clean-rx/badge/`, both matched their originals. Both badges remain BOOTSEL.

### Hardware evidence and remaining gates

Retained build `727a647a` was tested on badge `08DA26F86E57F5EA`, channel 6,
against the authorized Kerbel4 AP `80:23:95:6d:97:90`. In the bounded
association-only trial, independently received valid-FCS authentication and
association responses both had status 0. The controller recorded two received
ACKs, zero ACK timeouts, one filtered duplicate, four EAPOL M1 messages and
zero pending captures. There were 301 RX backpressure polls, not transport
drops. Evidence is retained in
`build/clean-rx/badge/mac-final-assoc-6-4.bin` from the repository root.

The AP subsequently deauthenticated with reason 15 because this
`--ap-assoc` trial deliberately did not run the WPA2 handshake. The final
collector correctly reported `associated:false`, `wpa2_authorized:false`;
this is not a maintained or authorized connection. An earlier build
`a4d0abd6` remained associated at the bounded end of its trial, while other
trials lost replies or missed ACKs. **Reliable MAC operation is not qualified.**
No credentials were used in these association-only trials.

Further controlled testing on 2026-10-16 used the unchanged vendor
`usb-wifi-link` AP, build `0353f1de`, on badge `9BBF88DD17D66E7D`, channel 1.
Every positive trial used a fresh WPA2 network and station challenge; success
required matching peer MACs, authorization and a protected echo at both ends.
The successive image results were:

| Station build | Successful protected exchanges / fresh trials |
| --- | --- |
| `727a647a` (baseline) | 1 / 1 |
| `c6d48656` | 2 / 3 |
| `e5ca386f` | 2 / 3 |
| `22701056` | 8 / 10 |
| `43ff54e4` | 4 / 6 |
| `f42fa969` | 7 / 10 |
| `65f952ee` (internal ACK consumption) | 8 / 10 |
| `40ad3bf8` (response-class diagnostics) | 9 / 10 |
| `06cdb3cc` (single-SHM address checks) | 29 / 30 |
| `0847d369` (management-request identity) | 29 / 30 |
| `851d14b4` (explicit diagnostic reset) | 7 / 10 |
| `5eeed6e7` (retained; discovery guard) | 9 / 10 |

The `f42fa969` failures included one missing authentication exchange and two
missing association responses, despite received request ACKs and the AP
retaining the station. An open-AP control reproduced the association-response
failure, so it is not solely a WPA2 parser problem. Two `f42fa969`
wrong-password controls associated and sent M2 but neither endpoint authorized
or exchanged protected data; another failed before association and is **not**
a valid password-rejection control. Evidence/log prefixes from the repository
root are `build/clean-rx/badge/mac-a-{baseline,controls,final,capture,binding,budget,open}`.
Later prefixes are `mac-a-{direct,trace,address,request,retained}` under the
same build directory. All attempts, including failures, are retained and
results are not pooled across images.

The address-check fix corrects six instructions that used two SHM input
operands, which the D11 ISA does not permit. All comparisons now stage one
word in a register. The native interpreter previously accepted the invalid
encoding; the new instruction-wide operand-limit regression was observed
failing before the fix. Consequently, older ACK counters cannot establish
correct peer-bound ACK filtering. Earlier independently confirmed protected
echoes remain valid evidence of those exchanges, not of ACK timing/filtering.

The `06cdb3cc` association failure occurred after 18 consecutive successes.
In `0847d369`, trials 13 and 16 completed after a second ACKed association
request: a MAC-ACKed request now retires its identity, so a later management
request is not incorrectly presented as a duplicate retry. No-ACK retries
retain the original sequence/body and only update Retry/FCS. BUSY with zero
attempts does not advance the request. The four-submission/1.6-second phase
budget was not enlarged. Trial 10 still exhausted authentication retries.

For `851d14b4`, trial 1 failed before station configuration with
`CLEAN_INPUT` (error 6), AP profile `-1`, and no management submissions;
trials 3 and 6 exhausted four authentication submissions without request ACKs.
Two wrong-password controls were valid and three open-AP controls all passed.
The final station-profile reports had zero other-class responses and zero
invalid-FCS bound publications; these diagnostics are not RF timing evidence.
The subsequent discovery guard fixes a demonstrated code defect that could
overwrite a valid beacon result with a non-beacon parser error. It does not
prove the cause of the earlier input failure.

In the retained `5eeed6e7` series, all ten protected-attempt station reports
confirmed authentication/association. Nine completed independently confirmed
protected echoes. Trial 3 received no EAPOL M1, while the vendor peer failed
with `RADIO ERROR` / firmware error 7 and produced no final AP observation;
it is retained as an unconfirmed attempt, not attributed to clean-MAC RF loss.
Trials 6 and 10 needed two and three association requests respectively.
Both wrong-password controls were valid; all three open controls passed
(open trial 2 needed two association requests). All 15 reports had zero
other-class responses and zero invalid-FCS bound publications. Evidence prefix
is `build/clean-rx/badge/mac-a-regression-5eeed6e7`. The connection runner
correctly returned failure for the unconfirmed attempt; this series is not
a zero-failure qualification result.

Four valid unchanged-vendor-versus-vendor controls independently confirmed
association/authorization with 20 stable observations each, under prefixes
`mac-a-vendor-control-{1,2,4,6}`. Attempts 3 and 5 failed to launch in BOOTSEL
and are not RF connection trials. These controls did not perform a data soak.
The clean-station protected trials used 32-byte plaintexts, 76-byte station
MPDUs and 6 Mbps OFDM. A bounded single protected echo is not full-sized or
sustained MAC qualification. No RTS, short-preamble response, fragmented
protected exchange or dynamic-policy transition was independently exercised.

With the vendor image already on the AP badge and the current clean image
on the station badge, reproduce ten protected trials, two wrong-password
controls and three open controls without changing the Mac's network:

```sh
bash clean-rx/tools/badge/mac_trials.sh AP_SERIAL CLEAN_STATION_SERIAL \
  build/clean-rx/badge/NEW_EVIDENCE_PREFIX
```

This runner does not flash. It retains per-trial endpoint reports, diagnostics
and station detail blobs, writes a JSONL summary, continues after failures
and exits unsuccessfully if any trial is unconfirmed. An existing summary
prefix is rejected. Wrong-password success requires association and M2 but
no authorization/protected traffic at either endpoint. These 15 connection
regressions are **not** the 100,000-frame OTA soak.

Initiator RTS/TX chaining is not implemented; ERP transmission uses the CCK
fallback instead. Current profile-3 carrier/NAV scheduling is PSM/IFS/TSF-based;
profile 2 retains conservative Cortex polling. Profile 3 anchors NAV to the
GoodFCS PSM timestamp and mirrors remaining reservations into hardware NAV.
Independent qualification of its slot/SIFS/turnaround timing is still necessary.
Controlled missing-ACK recovery/exhaustion now has small two-badge evidence.
Independent contention/SIFS captures, controlled late-ACK and occupied-credit
RF trials, and the required large bidirectional OTA
soak/vendor control have **not** passed. Earlier unexplained response loss
and no-ACK authentication failures have not been established as resolved.
Native arithmetic, local counters, a scheduled CTS or
a successful protected echo cannot establish independent RF/SIFS timing.

After these trials, badge `08DA26F86E57F5EA` had its original 33,604-byte
application restored and independently read back/compared byte-for-byte.
Badge `9BBF88DD17D66E7D` had its entire original 16 MiB flash restored and
independently read back/compared byte-for-byte against its verified backup.
Both were left in BOOTSEL. No additional timing hardware was available, and
the Mac's networking was left unchanged at the user's request; no host
monitor-mode capture was attempted.

## Earlier experimental association and ACK work

```sh
build/clean-rx/badge/collector SERIAL evidence.bin CHANNEL \
  --ap-assoc XX:XX:XX:XX:XX:XX
```

This mode is **not yet a reliable station implementation**. It pins one BSSID,
requires a complete valid-FCS beacon from that AP, parses SSID/capabilities,
supported/basic legacy rates and RSN, and rejects unsupported basic rates,
WEP-only networks, unsupported RSN suites and mandatory protected management
frames. It supports an open BSS or selects WPA2-PSK/CCMP for the association
request; selecting RSN suites does not implement the subsequent key exchange.
Only APs advertising legacy 6 Mbps are candidates. No password is read.

The trial installs its fresh MAC in both SHM and hardware address matching.
It sends exact-length 6 Mbps authentication and association requests, validates
response addresses, FCS, authentication transaction, status, association IEs
and AID (1..2007), and observes a 2.5-million-tick post-association window. A bound
deauthentication/disassociation clears the associated result.
Collector success independently revalidates both saved responses. It always
reports `wpa2_authorized:false`; this association-only mode does not run the
supplicant. There is no DHCP or IP stack.
It now recognizes and saves complete valid-FCS RSN EAPOL-Key message 1
frames, including address, LLC/SNAP, descriptor/key-info, body/key-data length
and zero-MIC validation. `eapol_m1_received` independently revalidates the
saved frame; this is handshake-start evidence, not authorization.
An association rejection, missing response or radio error is a failed trial,
not an authenticated connection.

Controller command 7 explicitly opts into this experimental profile using a
16-byte TX-buffer configuration: local MAC, peer BSSID, little-endian basic
rate mask (four CCK followed by eight OFDM rates), short-slot flag, enable=1.
It is never enabled by ordinary RX or Micro Signal. Command 5 bit 31 requests
ACK-aware management transmission, with an attempt bound of 1..8. Sequence is
preserved across attempts; retransmissions set Retry and recompute FCS. A PSM
ACK indication is accepted only during the active wait, with valid hardware
FCS, exact ACK length and matching receiver address. A management response
may prove delivery even if its preceding ACK was missed; it is not counted as
an ACK. Status is a separate 40-byte record at SRAM `0x201e0`.

The appended PSM scheduler runs only after the existing final GoodFCS gate.
It selects a basic response rate in the received modulation class, submits a
peer-bound ACK with hardware FCS generation, and excludes groups, control responses,
fragments, QoS frames and Action No Ack. `ack_responses_scheduled` counts
template submissions, **not verified over-the-air ACKs**. A bounded scheduler
timeout fails the controller explicitly. Station channel switching, key
installation and destructive RX rearming are rejected instead of silently
losing the profile. A station RX fault stops rather than restarting with a
different MAC/configuration.

Station mode retains a captured frame until the host's single SDIO credit
opens instead of discarding a frame for which the PSM may already have
scheduled an ACK. The host makes at most four management attempts, retaining
the sequence and setting Retry with a new FCS. Between unacknowledged requests
it listens/drains RX for 100,000 ticks; an ACKed request gets a 400,000-tick
response window. The independent BOOTSEL watchdog is renewed once at diagnostic
start for eight seconds, and host report polling allows eight seconds rather
than racing the target's six-second deadline.

Controlled vendor-SoftAP results superseding the earlier mesh failures below:

- All-vendor open control: both peers matched for 20 host observations.
- Clean-rx/open AP: three consecutive trials, valid successful responses,
  AID 1, and the exact fresh station MAC independently associated at the AP
  for 35–37 consecutive observations on the final retained build.
- Clean-rx/WPA2-PSK/CCMP AP: three consecutive successful associations,
  AID 1, matching AP records for 33–36 observations, and valid saved EAPOL
  message 1 frames. Neither endpoint claimed WPA2 authorization.
- Default all-vendor WPA2 remained functional, including authorization on
  both peers. These association-only results precede the protected-exchange
  qualification above.

For setup, use the [explicit open AP collector](../../docs/usb-live.md#paired-link-image).
Correlate `local_mac` in the clean-rx JSON with the AP observer's peer; an
AP-only success is not by itself a passing combined experiment.
Association-mode diagnostic offset 1096 counts own-address RX candidates;
1040..1087 holds the last twelve `(valid_FCS << 31) | (length << 8) | subtype`
records. Offset 796/800 preserves the last association candidate even if it
fails validation, without making it success evidence. Offset 576 counts
validated EAPOL M1 frames; the last complete one is saved in the existing
post-TX packet area at 1104 with its length at 1088.

Hardware results and remaining blockers:

- One local mesh AP trial completed authentication and association with
  status 0, AID 38, and received ACK indications for both requests. Repeated
  trials were intermittent; this is not sustained association qualification.
  The earlier build's three fresh trials all authenticated (one attempt and
  one received ACK each) but timed out on association after four attempts.
- An independent second badge captured the authentication request and AP
  responses, but did not establish valid return ACKs from our scheduler.
- Leaving the existing forced-TX PHY overrides enabled prevents subsequent
  reception. Releasing those overrides restored RX and enabled association
  progress. Explicit station TX waits for PHY TX-active to fall instead of
  imposing the broadcast path's extra full-PPDU delay.
- A legacy PSM-to-PHY write-bridge experiment timed out; an independent
  Cortex readback showed the requested RF override was not written. That
  experiment was removed. The current scheduler is not SIFS/RF-qualified.
- Complete CCA/NAV/backoff, robust retries under incoming traffic, duplicate
  delivery suppression, QoS/fragment handling, automatic response preamble
  selection and sustained associated operation remain unfinished. Do not
  use this profile as a reliable unicast/data MAC.

For independent, TX-disabled capture on a second authorized badge:

```sh
build/clean-rx/badge/collector SECOND_SERIAL monitor.bin CHANNEL \
  --ap-monitor XX:XX:XX:XX:XX:XX
```

Start this before the station trial. The 2.5-million-tick window records
valid-FCS authentication/association requests and AP responses for the first
observed authentication-request station TA; later requests from other
stations do not replace that binding. The collector reports first/last
request and response observation times as `monitor_*_us`, relative to monitor
start, and `monitor_requests_before_first_response`. These are badge-host
observation times, not RF/SIFS timestamps. ACKs addressed to the AP are counted separately; other
clients can also generate those ACKs, so that count alone is not attribution
or SIFS timing proof. Monitoring is lossy under load and absence is not proof
that no RF transmission occurred.

Native tests cover bounded RSN/beacon and EAPOL M1 parsing, rates, ACK PLCP, frame FCS, request
construction, response/AID validation, evidence binding and execution of the
appended PSM branch program. They do not establish physical RF behavior.
Normal/strict loader and PSM tests, Micro Signal tests and the loader's
AddressSanitizer/UndefinedBehaviorSanitizer run passed. Earlier default-path
qualification also passed a 60-second paired OFDM regression: all eight rates at all six
MPDU sizes (88 through 2,332 bytes) were received with valid FCS on both badges.
Both original 6 Mbps messenger applications were restored, with complete
33,604-byte flash readbacks matching their separate backups; both badges
were left in BOOTSEL.

## Build and test

From the repository root:

```sh
make clean-rx-badge
make -C clean-rx/badge test
```

Outputs are under `build/clean-rx/`; the badge image and collector are under
`build/clean-rx/badge/`. The image identity includes the linked controller
binary, host sources, relevant headers, collector and build rules.

For an isolated comparison with another source-built image implementing the
same mailbox ABI, set `CONTROLLER_BIN` to its absolute binary path and `BUILD`
to a separate absolute output directory. The default remains the firmware
built from `clean-rx/src`; a comparison image is not qualified by this option.
Use its matching collector, since the embedded firmware changes the build ID.
Do not use the vendor networking image with this custom mailbox loader.

The host qualifies gSPI, holds the controller CPU in reset, clears all
512 KiB of controller RAM, downloads and reads back the executable, initializes
the mailbox, and performs the firmware's HT-clock handshake. It requests
promiscuous reception with TX disabled by default. Burst accesses split at
64-byte boundaries, including across backplane windows.

Each request is bound to a fresh nonce and the compiled build identity.
Collection validates the report and every trace page's CRC/binding. This
image selects a 2,048-byte trace; existing applications retain their
256-byte default. Loader stages have a six-million-tick aggregate deadline,
and the badge schedules independent BOOTSEL recovery before USB startup.
Reception lasts 700,000 timer ticks; enabled-TX trials also have a
200,000-tick response window. These timer counts are not independently
calibrated wall-clock or RF timing measurements.

The 320x240 TFT shows test status and the received packet count. It uses the
existing RP2350 display driver; no display protocol is added to the WiFi chip.
The framebuffer adds 153,600 bytes of RP2350 RAM. A TX-confirmed screen requires
the same saved-frame validation as the collector, not just TX completion.

## Authorized physical test

Only run these commands against an explicitly authorized badge. Set `SERIAL`
to its 16-character uppercase BOOTSEL serial.

```sh
PICOTOOL=build/picotool-usb/picotool
SERIAL=YOUR_BADGE_SERIAL

# Preserve the installed program before the first trial.
$PICOTOOL save -p build/clean-rx/badge/original-badge.bin --ser "$SERIAL"

$PICOTOOL load -v -x build/clean-rx/badge/clean_rx.uf2 --ser "$SERIAL"
build/clean-rx/badge/collector "$SERIAL" build/clean-rx/badge/rx.bin 1

# Wait for independent recovery and confirm the same badge is in BOOTSEL.
sleep 9
$PICOTOOL info -a --ser "$SERIAL"

# Check full MPDU FCS and match the configured SSID locally.
build/clean-rx/badge/collector --inspect \
  build/clean-rx/badge/rx.bin ssid-data/ssid.txt
```

For sustained-receive failures, the separate
[RF-free transport test](../../docs/micro-signal.md#two-universe-investigation-and-rf-free-reproducer)
uses the same loader and SDIO publisher without starting D11/PHY or sending RF.
It preserves first-fault FIFO/DMA state before recovery and reproduces failures
independently of the radio application. Production now uses 32-byte SDIO DMA
bursts and one explicit host-consumption credit. The matching loader checks
post-read status and acknowledges only structurally valid, fully read packets.
It stops on faults without terminating a frame or guessing a credit.
New arrivals while credit is occupied are counted as drops.

The mailbox is now 456 bytes: offsets 0..447 are unchanged, offset 448 is the
host-written full-width consumed counter, and offset 452 is the controller's
full-width published counter. These counters wrap modulo 2^32 independently
of the older saturating diagnostic counts at offset 444. Controller comparison
images must implement this updated protocol; rebuild the host and controller
together. The diagnostic snapshot still captures the original 448-byte prefix.
See the [transport repair](../../docs/micro-signal.md#production-transport-repair)
for negative controls, limitations and hardware qualification.

The channel argument accepts 1..11. `--inspect` reads only the `SSID:` field;
it does not use or print the password. Binary captures contain received
network identifiers and should remain local.

Additional collector modes require a new load/execute and recovery:

- `--tx-one-probe`: enables TX and submits one 94-byte, unencrypted, 11 Mbps
  CCK probe request for the first FCS-validated beacon's SSID. Its source MAC
  is fresh and locally administered. It records command completion and
  controller status, then looks for a valid-FCS probe response to that MAC.
  The selected beacon is not necessarily the AP in the credential file;
  inspect the RX evidence first. This is not a traffic generator.
- `--tx-probe-1m`: one unicast, SSID-directed probe at 1 Mbps, addressed to the
  first validated beacon's BSSID. It contains no vendor padding and has total
  template length `52 + SSID length` bytes, including PLCP and FCS.
- `--tx-probe-broadcast-1m`: the same compact SSID-directed request with both
  destination and BSSID set to broadcast, as in normal active scanning.
  Confirmation remains bound to the first validated beacon's AP and fresh MAC.
- `--tx-one-auth`: one padded 94-byte, 11 Mbps CCK open-system authentication
  request addressed to the first validated beacon's BSSID.
- `--tx-auth-1m`: the padded authentication request at 1 Mbps CCK.
- `--tx-auth-ofdm`: the padded authentication request at 6 Mbps OFDM.
- `--ap-auth XX:XX:XX:XX:XX:XX`: a **BSSID-pinned, exact-length 6 Mbps
  OFDM authentication test**, with a 40-byte template (6-byte PLCP,
  30-byte open-system authentication request and 4-byte FCS). The selected
  BSSID must appear among the eight retained valid-FCS beacons on the
  requested channel; otherwise the test fails without submitting a TX
  command. It never falls back to another AP. The fresh station MAC is
  independent of the requested BSSID. Example:

  ```sh
  build/clean-rx/badge/collector SERIAL evidence.bin CHANNEL \
    --ap-auth XX:XX:XX:XX:XX:XX
  ```

  Collector success requires a valid-FCS authentication transaction 2 with
  algorithm 0, status 0 and the expected receiver, transmitter and BSSID;
  an ACK alone or a rejection is not success. JSON reports the requested
  BSSID, `authentication_response_received`, and `authentication_status`
  (`null` when absent), plus `associated:false` and
  `wpa2_authorized:false`. The TFT explicitly says that successful AP
  authentication is **not connected**. This mode does not read a password,
  send an association request or implement an EAPOL handshake.
- `--tx-auth-short`: an exact-length 40-byte template at 1 Mbps CCK:
  6-byte PLCP, 30-byte authentication request, and 4-byte MPDU FCS.
- `--tx-auth-rearm`: the short authentication request followed by an explicit
  RXE/PSM restart. It requires PSM READY before resuming MAC reception; timeout
  or fault reports controller error `0x84`. The restart resets PSM discard
  counters, so their final values are not whole-trial totals.
- `--tx-auth-match`: the short authentication request with hardware receive
  address matching programmed to its fresh source MAC and selected BSSID.
  It does not change the SHM automatic-ACK address.
- `--tx-auth-match-rearm`: combines those two receive-path experiments.
  Neither option changes the default TX resume path or RF gains/power.
- `--reject-tx`: leaves TX disabled and submits the same command to verify
  rejection. Collector success here requires controller error `0x81`,
  address `0x20018`, ERROR state, and no completed TX command.

Every enabled-TX AP-response mode sends exactly one request with a fresh locally
administered source MAC. Collector exit status is nonzero unless it obtains
over-the-air confirmation, even if the TX command completes. Evidence is saved
before this decision. Confirmation requires all three TXE/IFS completion
conditions, no controller error, and a saved valid-FCS response received after
TX: either the expected management reply with matching receiver, transmitter,
and BSSID, or a 14-byte ACK addressed to the fresh source MAC. An ACK has no
transmitter/BSSID field; it is bound by its receiver address and this single
fresh request. These checks are not cryptographic sender authentication.
Authentication transaction 2 is evidence of reception even if its status
rejects authentication. A bound deauthentication/disassociation reply also
qualifies as reception evidence. Retry, power-management and More Data flags
do not invalidate an otherwise matching management reply; fragmented,
protected, DS-addressed and ordered/HT-control variants are not accepted by
this legacy-frame parser. None of these observations establishes association
or WPA2 connectivity.

`--inspect` reports separate probe/authentication/ACK FCS checks and
`tx_over_air_confirmed`. Its exit status checks the saved SRAM and Function-2
frames' FCS, not TX confirmation. Read the JSON boolean for the latter.
Except for the explicitly BSSID-pinned `--ap-auth` mode, the AP chosen for TX
is still the first validated beacon, not an SSID selected from the credential
file. Only use these modes where that AP is authorized.

### Local secured mesh-network trial, 2026-10-16

Using only the source-built firmware on one Universe badge, passive scans
identified three BSSIDs for the configured local SSID, on channels 1, 6 and
11. Saved beacons from all three advertise WPA2-PSK with CCMP. No credentials
are embedded in the diagnostic image.

Three fresh-MAC, BSSID-pinned 6 Mbps authentication trials per AP received
successful status-0 replies in **1/3, 1/3 and 3/3 trials**, respectively.
The successful replies independently pass the saved-frame FCS, address,
algorithm and transaction checks. The missing replies are not evidence of
an AP rejection; timing/ACK and receive-path reliability remain unqualified.
These local captures remain under `build/clean-rx/badge/kerbel-*` and should
not be uploaded: they contain local network identifiers.

**No secured connection was established.** Open-system authentication is only
the first management exchange; it does not validate a WPA2 password. This
firmware still needs association/RSN negotiation, an EAPOL four-way
handshake with MIC/replay validation, verified key installation/CCMP and
reliable unicast ACK/retry timing. There is no DHCP lease or working IP link
from this test. Multiple APs sharing an SSID are handled by choosing a
specific infrastructure BSSID; this does not test mesh backhaul or roaming.
The tested badge's original 33,604-byte Micro Signal 6 Mbps application was
restored and its full flash readback matched; it was left in BOOTSEL.
The second attached badge was not modified.

`CFG_TX_ENABLE` now gates both explicit TX commands and automatic ACKs in
the controller. With TX enabled, automatic ACKs remain available for frames
addressed to its configured station MAC. A rejected TX command preserves the
ERROR state through cleanup rather than publishing RX_DONE.

Restore the saved program **without executing it**, then confirm BOOTSEL:

```sh
$PICOTOOL load -v build/clean-rx/badge/original-badge.bin --ser "$SERIAL"
$PICOTOOL info -a --ser "$SERIAL"
```

## Source-only peer TX/RX

The local Tufty2350 board header confirms the same WiFi GPIO wiring used by
this host: enable 23, data 24, CS 25 and clock 29. The test does not use an
accelerometer or buttons. Both tested boards have RP2350-A4/QFN80 processors.
No separate Tufty networking executable, vendor WiFi image or retained radio
initialization is used: both run the same image and cold-clear controller RAM.

After preserving each device's installed program separately, load the image
onto both **without** `-x`, then reboot the staged images. Immediate parallel
flash/execute attempts caused intermittent host USB discovery failures;
staging first and rebooting sequentially worked for the qualified trials.
Always select the serial explicitly and restore the correct backup to each.

```sh
PICOTOOL=build/picotool-usb/picotool
COLLECTOR=build/clean-rx/badge/collector
TX_SERIAL=YOUR_TRANSMITTER_SERIAL
RX_SERIAL=YOUR_DIFFERENT_RECEIVER_SERIAL

$PICOTOOL load -v build/clean-rx/badge/clean_rx.uf2 --ser "$TX_SERIAL"
$PICOTOOL load -v build/clean-rx/badge/clean_rx.uf2 --ser "$RX_SERIAL"

# Use a new 112-bit random challenge for every paired trial.
CHALLENGE=$(openssl rand -hex 14)
$PICOTOOL reboot --ser "$TX_SERIAL"
$PICOTOOL reboot --ser "$RX_SERIAL"
$COLLECTOR "$RX_SERIAL" build/clean-rx/badge/peer-rx.bin 6 \
  --peer-rx "$CHALLENGE" > build/clean-rx/badge/peer-rx.json &
RX_PID=$!
sleep 0.1
$COLLECTOR "$TX_SERIAL" build/clean-rx/badge/peer-tx.bin 6 \
  --peer-tx "$CHALLENGE" > build/clean-rx/badge/peer-tx.json
TX_STATUS=$?
wait "$RX_PID"
RX_STATUS=$?
printf 'TX submission=%s, independent RX proof=%s\n' "$TX_STATUS" "$RX_STATUS"

# Independently recheck the saved receiver frame using the same challenge.
$COLLECTOR --inspect-peer build/clean-rx/badge/peer-rx.bin "$CHALLENGE"
sleep 9
$PICOTOOL info -a --ser "$TX_SERIAL"
$PICOTOOL info -a --ser "$RX_SERIAL"
```

Peer modes accept exactly 28 hexadecimal challenge characters:

- `--peer-tx`: one 94-byte, 11 Mbps CCK broadcast wildcard probe request,
  including PLCP and FCS. A vendor IE contains a local OUI, `CRXP` tag and the
  14-byte challenge. The source MAC and sequence are also challenge-derived.
  It does not require an AP beacon to construct the frame.
- `--peer-tx-1m`: the same MPDU at 1 Mbps CCK; currently **unqualified**.
- `--peer-rx`: RX-only, with explicit TX and automatic ACKs disabled. It
  listens for 2,500,000 host timer ticks and retains any exact challenge frame
  in the saved Function-2 evidence. The ordinary host deadline and independent
  BOOTSEL recovery still apply.

A peer transmitter's exit 0 means **command submission/completion only**,
not OTA success. `peer_rx_confirmed:true` and receiver exit 0 require an
independently received 88-byte MPDU matching the expected request byte-for-byte,
including valid FCS, challenge, addresses, frame control and sequence, plus a
successful RX-only controller run. `peer_frames` counts matches, not all RX.
The USB report and every trace page remain nonce/build-bound; the JSON now
includes the selected device serial and challenge. Verify distinct serials,
matching build/channel/challenge and successful TX completion in the paired
reports. Do not reuse challenges.

The existing `tx_over_air_confirmed` field retains its narrower **AP reply or
fresh-MAC ACK** meaning; peer proof is separate and may succeed when that field
is false. `--inspect-peer` exits nonzero for no match, stale/wrong challenge,
corruption, controller error, or a TX-requested trace. Neither receiver
confirmation nor a successful AP probe response establishes association.

## Physical results, 2026-10-14

One RP2350-A4 badge was tested with serial-bound operations and independent
BOOTSEL checks. These are newly acquired cold-source results, not
vendor-retained initialization.

| Trial | Result |
| --- | --- |
| Initial cold boot | 18,960 executable bytes read back; RX_DONE with no controller error; 10 captures and 10 DMA completions |
| Full-frame/Function-2 test | 9 captures, 9 DMA completions, 9 Function-2 packets; host-validated FCS for the saved SRAM and Function-2 frames; local AP SSID matched on channel 1 |
| Enabled wildcard probe | Command completed; TX marker `0x5555005e`, status `0x04c10022`; no matching AP response in the extended response trial |
| Enabled SSID-directed probe | 18 captures/DMA completions/Function-2 packets; same TX marker/status; no matching AP response |
| Disabled-TX rejection, build `bd682752` | Error `0x81` at `0x20018`, controller ERROR state; no completed TX command |
| Final RX-only repeat, build `bd682752` | 12 captures, 12 DMA completions, 12 Function-2 packets; both saved frames pass host FCS checks; local AP SSID matched; no TX requested |

Evidence binaries and JSON reports are in `build/clean-rx/badge/`, including
`full-ch1.*`, `probe-retry-ch1.*`, `directed-ch1.*`, `reject-ch1.*`, and
`final-rx-ch1.*`. An initial response-window trial hit the shorter collector
deadline; that deadline was corrected to cover the loader's aggregate bound,
and the subsequent trial completed. Failed and successful reports are not
interchangeable.

The original 312,560-byte badge program was backed up, restored with write
verification, and left in BOOTSEL.

**Established:** cold source execution, local-AP beacon reception, independent
FCS verification, DMA/Function-2 transport, explicit TX command execution,
and disabled-TX rejection.

**Not established:** successful over-the-air TX, measured RF power/spectrum
or regulatory qualification, automatic ACK timing/reliability, CCMP,
association, WPA2 connectivity, large-frame transport, or long-run DMA-ring
reliability. A completed TX command is not evidence of AP reception.

## Follow-up TX verification

The original PLCP CRC loop was incorrect: it mixed an MSB-first polynomial
with a right-shifting input byte. It now uses reflected CRC-16 (`0x8408`),
initial value `0xffff`, final complement, and little-endian output. The
88-byte/11 Mbps known-answer PLCP is `6e 04 40 00 7f ee`.
Host tests also cover OFDM SIGNAL length/parity, short authentication framing,
and response address/FCS binding.

Corrected probes and padded/exact-length authentication requests were tested
at 1 Mbps CCK, 11 Mbps CCK, and 6 Mbps OFDM. The TX engine reported all three
completion conditions, but no trial acquired a matching management response
or fresh-MAC ACK. Exhausted TX polling now fails explicitly with firmware
error `0x82`; invalid TX lengths fail with `0x83`.

Final image `e0b3f797` cold-loaded and read back 19,176 controller bytes:

| Trial | Result |
| --- | --- |
| Exact-length authentication | 53 captures, 53 DMA completions, 53 Function-2 packets; TX marker `0x55550028`, status `0x04c10022`, completion bits `7`; no matching reply or ACK; collector exit 1 and `tx_over_air_confirmed: false` |
| RX-only regression | 28 captures/DMA completions/Function-2 packets; both saved frames pass independent host FCS checks; local SSID matched; collector exit 0 |
| Disabled-TX regression | Error `0x81` at `0x20018`, ERROR state, no completed TX command; collector exit 0 for the expected rejection |

Final evidence is local under `build/clean-rx/badge/`: `final-auth.*`,
`tx-final-rx.*`, and `tx-final-reject.*`. Native tests, ASan/UBSan host tests,
and existing USB/WiFi-firmware regressions pass. Both device images remain
freestanding, with no undefined link symbols.

**Over-the-air TX remains unconfirmed.** Controller completion and historical
TX evidence from another image are not substitutes for a reply to this image.
Without an independent RF observer, these results do not distinguish an
invalid/absent transmission from a missed AP response.

## Further diagnosis: PHY timing, mesh nodes, and reference firmware

Before issuing TX, the RP2350 now reads the complete staging buffer back and
compares it byte-for-byte. Firmware captures TXE/IFS status before, during and
after TX, PHY/radio/IQ/TX-register readbacks, PHY error status, CPU ticks, and
MAC hardware microsecond-counter timestamps. These are internal observations,
not measured RF power or an over-the-air capture. Successful trials measured:

- Short 1 Mbps authentication: 466-467 us between trigger and completion.
- Compact 1 Mbps probe with a seven-byte SSID: 621 us.
- Completion to MAC RX enable: 7-8 us.
- TXE busy asserted and then cleared, completion asserted, no PHY TX error.
- Intended manual gains, power-control-off setting, TX enables, Farrow
  parameters and identity IQ/baseband multipliers read back correctly.

The timing is consistent with the requested long-preamble frames, but does not
prove that a valid waveform left the antenna or that RX was ready at SIFS.
`fresh_mac_frames` counts any FCS-valid post-TX frame addressed to the fresh
source MAC, including frames not recognized as the expected reply. All new
trials reported zero, so stricter subtype/flag validation alone did not explain
the lack of confirmation. The Retry-flag rejection was nevertheless a real
parser bug and is covered by native tests.

RX-only scans found two matching-SSID BSSIDs on channel 1 and another on
channel 6; channel 11 yielded no frames in its bounded observation window.
Both authentication and compact unicast probes were tried against the
channel-6 node. In the final compact-probe trial, 75 captures, 75 DMA
completions and 75 Function-2 packets were reported, without a fresh-MAC reply.
These tests do not establish which mesh node was physically nearest.

For a control, the earlier `open-rx` C implementation was rebuilt locally from
source into a separate badge image: 47,028 controller bytes, no undefined
symbols, no vendor image. Its continuous PSM image is byte-identical to the
2,360-byte PSM image in `clean-rx`. Cold-loaded short and padded authentication
trials against the same channel-6 AP also produced no fresh-MAC frame. Its
`fw_substage` value `17` is a legacy boot diagnostic, **not** this firmware's TX
completion bitmask; the collector was not weakened to declare it qualified.

Thus the failure is reproducible with the earlier cold-initialization code,
not just the simplified `clean-rx` implementation. Common template-TX, RF,
PSM/RX and AP-response behavior remain candidates. No analog gains or power
tables were swept or raised. An independent RF observation is still needed
to separate absent/invalid TX from a missed reply.

Evidence remains local under `build/clean-rx/badge/`: `tx-diag.*`,
`tx-reply-flags.*`, `compact-probe.*`, `mesh-rx-6.*`, `mesh-rx-11.*`,
`mesh-auth-6.*`, `mesh-probe-6.*`, `control-auth-6.*`, and
`control-auth-padded-6.*`. The separate comparison badge is under
`build/clean-rx/control-badge/`.

Final default image `4f27c413` read back 19,980 controller bytes. Its RX-only
channel-6 regression delivered 40 frames with valid saved-frame FCS. Its final
compact-probe trial delivered 52 RX frames, measured 621 us TX / 8 us return
to MAC RX enable, and still had no fresh-MAC reply; the collector exited 1.
Disabled TX again reported `0x81` and the rejection check exited 0. Sanitized
host tests and shared USB/WiFi regressions pass. After these trials, the
312,560-byte original badge program was restored, read back and compared
byte-for-byte; the badge was left in BOOTSEL without executing that program.

### Badge-only receive-path investigation

Three additional cold-loaded, single-request trials targeted the channel-6
AP with fresh source MACs. None received a valid-FCS frame to that MAC.

| Trial | Captures / DMA / Function-2 | TX duration | Completion to MAC RX enable |
| --- | --- | --- | --- |
| Explicit RXE/PSM restart, build `0c1a22f6` | 43 / 43 / 43 | 466 us | 28 us |
| Hardware source/BSSID matching, build `2937666e` | 49 / 49 / 49 | 466 us | 8 us |
| Both, build `6bd496ec` | 35 / 35 / 35 | 466 us | 29 us |

All reported TX completion bits `7`, no controller error, and zero fresh-MAC
ACKs, management replies or other valid frames. The combined trial's saved
SRAM and Function-2 frames passed independent FCS validation and its beacon
matched the local SSID. MAC RX enable is not a measurement of analog receiver
readiness or proof that a SIFS ACK could have been received.

The matching-only trial paused PSM in receive-copy state `11`, whereas the
combined trial started and resumed in event-wait state `3`. Restarting from
the known PSM entry sequence did reach READY within the bounded poll; a missed
reply is therefore not explained simply by failure to restart that sequence.
This does not exclude other receive-path problems.

The combined trial sampled PHYDEBUG `0x00000844` just after the TX trigger.
Defined bit 2 confirms that the MAC instructed the PHY to transmit. The
earlier `phy_debug_before_cmd` read was subsequently found to be misplaced
before cold initialization; its zero value is not valid carrier evidence.
That placement is corrected in the phase-tagged trials below. PHYDEBUG does
not measure radiated RF power, frame validity or AP reception.

The diagnostic is now `TXD3`, still 180 bytes. PHYDEBUG replaces the old staged
CRC field: CRC over a frame containing its FCS is not a fresh-frame challenge.
The host still checks the staged template byte-for-byte before issuing TX.
The existing PSM fault-recovery path now shares the bounded restart helper
and explicitly fails instead of silently continuing after a READY timeout.

Evidence is local in `build/clean-rx/badge/tx-rx-rearm.*`,
`tx-rx-match.*`, and `tx-rx-match-rearm.*`. These receive-path experiments did
not confirm OTA TX; they remain opt-in rather than speculative default fixes.

Final image `5eb3e077` read back 20,132 controller bytes and passed native,
AddressSanitizer/UndefinedBehaviorSanitizer and shared USB/WiFi tests. Its
channel-6 regressions delivered 30 RX-only frames, 65 frames in normal short
authentication, and 45 with matching plus rearming. Saved SRAM/Function-2
frames passed FCS checks in all three trials, and each trial recorded the local
AP SSID. Both TX trials measured
467 us and still had zero fresh-MAC frames (collector exit 1). Normal RX resume
took 7 us, versus 28 us with the restart. Disabled TX again failed explicitly
with `0x81`, and its expected-rejection check exited 0. Final evidence uses the
`rx-investigation-final-*` prefix. The original 312,560-byte badge program was
restored and independently read back/compared byte-for-byte; the badge remains
in BOOTSEL.

### Separating post-TX RX from earlier queued traffic

Aggregate RX totals alone did not establish that the receiver recovered after
TX. The controller now tags each Function-2 packet when publishing it, using
the existing two SDPCM padding bytes, without extending the packet or changing
BDC payload offsets. Queued pre-TX packets retain their earlier tag.
The collector separately reports packets delivered after TX and packets
published by the controller after TX, with FCS and selected-AP checks.

Final build `73ea2c43` read back 20,164 controller bytes:

| Trial | Packets before TX | Valid controller-tagged post-TX packets | Selected-AP post-TX beacons | Fresh-MAC reply |
| --- | --- | --- | --- | --- |
| Short authentication, channel 6 | 38 | 11 | 3 | None |
| Authentication with matching/rearm, channel 6 | 41 | 11 | 4 | None |
| Compact broadcast probe, channel 6 | 44 | 13 | 2 | None |
| Compact broadcast probe, channel 1 | 29 | 8 | 4 | None |

Thus reception did not remain dead after transmission: several valid beacons
from the selected AP were received after each request. A resumed in-progress
PSM frame could account for at most one publication on the normal resume path;
the explicit restart also removes that ambiguity. These observations do not
establish receiver readiness within the immediate ACK/reply interval.
Authentication took 466 us; broadcast probes took 621 us. Correctly placed
pre-command PHYDEBUG samples were `0x00000040`, without the defined carrier
sense bit set. These single samples do not rule out subsequent collisions.

The compact broadcast probes remove the earlier unicast-probe addressing
question, but still produced no fresh-MAC frame. Both channel-1 mesh nodes
matched the local SSID. One earlier channel-1 cold start failed explicitly
before TX with `0x71`, PSM fault `3` (initial RXE flush); a new cold attempt and
the final repeat succeeded. This intermittent startup fault is not counted as
a transmitted request and is not hidden by a retry in the firmware.

Saved SRAM/Function-2 frames passed FCS inspection for all final RX/TX trials.
RX-only and disabled-TX regressions also passed. Native and sanitizer tests
cover phase tags, queued pre-TX exclusion, FCS rejection, short ACKs, AP binding
and the broadcast frame; shared USB/WiFi regressions pass. Evidence is local
under `phase-final-*`, `rx-phase-*` and `broadcast-probe-*` in
`build/clean-rx/badge/`. OTA TX remains unconfirmed. The original 312,560-byte
program was again restored, independently read back and byte-compared; the
badge was left in BOOTSEL.

## Two-radio physical results, 2026-10-15

Authorized devices:

- Universe badge `08DA26F86E57F5EA`.
- Pimoroni Tufty2350 `D41777A92225F996`.

Final build `44f23426` uses the unchanged 20,164-byte source-built controller
from the preceding investigation. Only the badge host, evidence matching and
collector were extended for peer tests; no RF power/gain settings were changed.

| Trial | Independent receiver result | TX duration |
| --- | --- | --- |
| Universe -> Tufty, 11 Mbps, initial build `8e9cbac5` | Exactly one fresh-challenge frame, full MPDU/FCS match | 264 us |
| Tufty -> Universe, 11 Mbps, initial build | Exactly one fresh-challenge frame, full MPDU/FCS match | 265 us |
| Universe -> Tufty, 11 Mbps, final build | Exactly one fresh-challenge frame, full MPDU/FCS match | 265 us |
| Tufty -> Universe, 11 Mbps, final build | Exactly one fresh-challenge frame, full MPDU/FCS match | 264 us |
| No transmitter, Tufty RX, final build | 48 ambient captures, zero peer frames; receiver exits 1 | No TX |
| Saved positive capture inspected with a new/wrong challenge | Rejected, exit 1 | Offline |
| Universe -> Tufty, 1 Mbps, final build | 89 ambient captures, zero peer frames; receiver exits 1 | 906 us |

The initial successful 11 Mbps trials also recorded matching fresh-MAC AP
probe responses on the transmitter. The final Tufty TX repeat did too; the
final Universe TX repeat did not, despite independent peer confirmation.
This demonstrates why absence of an AP response alone was not enough to
conclude that TX failed. AP observations and RF conditions changed between
days; these results do not establish the cause of the previous AP-only failures.

1 Mbps did not produce a matching frame in the earlier attempts in either
direction or the final Universe-to-Tufty repeat. One reverse-direction 1 Mbps
receiver run exhausted its host deadline and is not a clean negative RF result.
The clean negatives are still not enough to distinguish a 1 Mbps TX waveform
problem from a rate-specific receive-path issue; leave that rate unqualified.
Initial paired attempts with USB discovery/flash failures are also not counted
as OTA trials.

Native tests, ASan/UBSan and shared USB/WiFi regressions pass. Positive receiver
captures were re-inspected offline, and peer receivers had TX disabled in the
saved mailbox flags. Evidence remains local in `build/clean-rx/badge/`:
`peer-staged-u2t-*`, `peer-t2u-11-*`, `peer-final-*`, `peer-negative-*`, and
their `.challenge` files. The final peer trace mode is stored at byte 2000 and
its match count at 1996; the retained Function-2 frame uses the existing offset.

At the end of these peer trials, the original programs were restored and independently read back/compared:
312,560 bytes for the Universe badge and 419,560 bytes for the Tufty
(`wifi_handover`). Both devices were left in BOOTSEL without executing their
restored programs.

## Subsequent Micro Signal reliability investigation

The standalone [Micro Signal app](../../docs/micro-signal.md) now uses this
loader and source controller on both attached boards. Its complete guide
records successful and failed sustained-traffic trials; the earlier one-shot
OTA results above are not a sustained-operation qualification.

- Baseline: two initial-flush PSM failures in 40 controlled cold radio boots.
  Fault snapshots showed RXCTL `0x0804`; a later probe showed `0x0800`.
  The PSM's 255-poll flush countdown expired before hardware completion.
- Repair: the counter-load instruction now uses the existing signed `-1`
  encoding for 65,535 iterations. Branch targets, timeout/error paths and
  outer ARM deadline are unchanged. Subsequent controlled batches passed
  **80/80 radio boots**, without automatic boot retries.
- Multi-minute tests exposed TX staging readback corruption and a missing
  SPI bulk-backplane word-access flag. The loader now sets `0x8000` on aligned
  reads and writes, matching scalar I/O and the existing firmware loader.
- One **300.023-second** paired run passed: Universe 1,383 TX / 469 unique RX
  events / 5,271 published SDIO frames; Tufty 1,405 TX / 459 unique RX events /
  4,160 SDIO frames. Neither reported errors, drops, resyncs or sequence stalls.
- A repeat failed at **124.895 seconds**, with Tufty application error 6 and
  malformed SDPCM first word `0xfc1c013f`. A slower-SPI experiment also failed
  and was reverted. **Sustained reception is still not qualified.**

The controller retains PSM/RXE fault snapshots and periodically publishes
best-effort live snapshots in the existing mailbox diagnostic fields.
The test-only app/collector captures these plus exact staging mismatch bytes.
Native PSM tests verify the image identity, counter encoding, actual branch
execution, delayed completion and persistent timeout. Loader tests enforce
word-access flags, boundary splitting and interleaved staging/mailbox I/O.

The current production Micro Signal UF2 is 63,488 bytes, with a 31,588-byte
RP2350 flash image embedding the 20,500-byte controller. The normal app still
has no USB or recovery watchdog. Updated images supersede the restored
original programs mentioned in the historical trial above; original backups
remain preserved.
