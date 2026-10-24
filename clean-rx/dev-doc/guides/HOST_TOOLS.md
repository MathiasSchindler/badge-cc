# Host tools and report interpretation

Desktop tooling and paired hardware procedures for the badge host. These
commands are separate from firmware builds and offline tests. Hardware
execution requires [the trial protocol](HARDWARE_TRIALS.md), its own authorized
scope and matched installed images.

## Bounded USB orchestration

Build the host-only readiness probe with the existing compiler/libusb tools:

```sh
make -f clean-rx/tools/Makefile
```

Badge `all`/`tools`, peer `ht-peer`/`ht-peer-tools` and reference
`reference`/`reference-tools` also build it. Firmware-only targets do not.
The runners use existing Node builtins, not npm, packages or a framework.
`CLEAN_RX_USB_READY` overrides the default
`build/clean-rx/tools/usb-ready` executable for isolated tests.

The probe binds the exact serial, configures USB only if necessary and reads
the live-report protocol header and build ID. It submits no radio command.
The observed build ID is logged; this does not replace R09's diagnostic-gate
and matched-image selection preflight.

| Phase | External subprocess bound |
| --- | --- |
| BOOTSEL discovery and reboot | 30 seconds total; each info attempt at most 2 seconds; reboot at most 15 seconds within the remaining total |
| Application readiness | 15 seconds total per endpoint, with 2-second attempt caps |
| Final pair readiness recheck | One 2-second probe per endpoint; failure stops the pair |
| Flash load | 300 seconds per operation |
| Ordinary badge / independent peer collection | 25 / 35 seconds |
| Lifecycle/network station | Requested station duration + 25 seconds |
| Independent AP | Requested AP duration + 50 seconds |
| General vendor AP / WPA2 station | 100 / 75 seconds |
| MAC soak | 7240 seconds, retaining the native 7230-second ceiling |
| Reference aggregate collector | 70 seconds |

The supervisor adds at most a 0.5-second TERM-to-KILL grace period after
expiry/cancellation and reaps the child before returning. It signals only its
owned process group, including descendants. Timeout exits 124; INT/TERM exits
130/143. Missing executables and unexpected child exits are explicit failures.
Info/readiness retries share fixed total deadlines; hung subprocesses are
not retried indefinitely. Reboot is not repeated after an ambiguous timeout.

Paired runners boot and verify **both** applications before submitting RF
commands, then immediately recheck both. The HT 200 ms RX lead, MAC one-second
responder lead and independent-peer six-second radio staging are intentional
protocol delays, not enumeration waits. An endpoint failure stops its partner;
matrix runners stop subsequent trials. Expected wrong-password exit 1 is
allowed only in that control, never as a replacement for a timeout.

Boot/readiness logs append every attempt; raw reports, stdout and stderr
remain under the chosen new prefix. Failed or cancelled runs do not generate
a passing pair. Retry using a new prefix after recovering devices manually;
the runners do not automatically restore or execute a timed-out image.
The historical WPA2 script delegates to the same canonical runner.

Offline failure-path coverage:

```sh
node --test clean-rx/tools/usb-orchestration.test.cjs
```

These tests use fake processes/USB stubs and do not enumerate or flash hardware.

## WPA2 collection

The badge application's independent-peer receive diagnostic is RX-only.
It checks fresh broadcast data addresses, SNAP/payload, fixture sequence,
complete MPDU FCS, HT20 metadata and matching terminal HT-SIG, recording
the received sequence bitmap and duplicate count separately.
The collector uses `auth_bad_fcs_report` to distinguish the rejected
authentication snapshot from a validated response.

The collector accepts:

```sh
build/clean-rx/badge/collector SERIAL evidence.bin CHANNEL \
  --ap-wpa2 BSSID PRIVATE_CREDENTIAL_FILE
```

The credential file belongs to the current user, has mode `0600`, and
contains exactly two LF-terminated lines: SSID (1..32 bytes) and passphrase
(8..63 bytes). The collector derives the PMK and sends it with a fresh
SNonce through the dedicated secret-control path, bound to the BSSID,
channel and request. This is separate from the controller's raw
hardware-key command.

## Paired WPA2 trials

For a paired WPA2 fixture, install the unmodified vendor `usb-wifi-link`
image on one badge and the clean image on the other, leaving both in
BOOTSEL. Run:

```sh
CLEAN_RX_COLLECTOR=build/clean-rx/badge/collector \
  bash clean-rx/tools/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL build/new-wpa2-trial
```

The runner boots both USB applications before starting the AP, starts
station collection at AP readiness, and requires independent matching-peer
authorization and protected traffic. It writes phase times and both
endpoints' evidence under the new prefix. `--wrong-password` selects the
negative control. Temporary credentials are private and removed on exit;
the runner does not flash either badge.


## Desktop report fields

HT probe/data/ACK collector records expose `ht20_format`,
`ht20_width_mhz`, `ht20_gi_ns`, `ht20_streams`, `ht20_rate_kbps`,
`ht20_symbols`, `ht20_expected_airtime_us` and `ht20_lsig_length`.
These describe the requested configuration and calculated airtime, not
independently measured on-air duration. Passive monitoring does not invent
a requested configuration.

Peer reports expose `ht_peer_profile_source` and `ht_peer_runtime_updates`.
The selectable candidate is reported in `ht20_candidate_tx_mcs_mask` and
`ht20_candidate_max_mcs`; unavailable candidates use mask 0 and MCS -1.
The local width, GI, stream count, RX mask and serialized target IE do not
indicate negotiated HT operation. `ht20_active` and `ht_advertised` remain
false.

WMM collector fields are `wmm_report_valid`, `wmm_offered`,
`wmm_negotiated` and `wmm_policy_committed_updates`. The last counts applied,
controller-readback-verified policies, not queued requests.
`wmm_policy_queued_updates` is an equal compatibility alias.
`wmm_edca_enabled` uses controller EDCA status at USB detail offset 752.
`protected_amsdu_frames` and `protected_amsdu_subframes` count newly delivered
validated protected aggregates, not retries, F2 fragments or A-MPDUs.

## Finite aggregate reports

Use [diagnostic profile navigation](DIAGNOSTIC_PROFILES.md) for selection and
campaign evidence rather than treating report booleans as general capability.

- `multi_mpdu_radio_capture` describes retained PSM snapshots.
  `delivered_subframe_bitmap`, `delivery_payloads_valid`,
  `actual_multi_mpdu_delivery` and `all_subframes_delivered` describe actual
  delivered payloads, not the snapshot alone.
- `shm_delivery_enabled` and `dma_experiment_enabled` identify distinct
  paths. SHM delivery reports `shm_delivery_enabled:true` and
  `dma_experiment_enabled:false`. Saved bytes occupy detail offsets `1664 + 80 * sequence` in this
  mode's original 2,048-byte report body, replacing its beacon inventory.
- `shm_terminal` reports publication/claim/release counters, claimed state,
  PSM TSF timestamps and controller snapshot hold time. These use the TSF
  domain, not an independent physical EOF/SIFS clock.
- `ba_experiment` contains the bounded response bitmap/errors and sampled
  EOF/IFS times. Pair `compressed_ba_return` contains actual sender receipt.
  An armed positive pair requires a valid returned BA bitmap matching
  delivered bodies; local TX counters do not substitute for receipt.
- `immediate_ba`, `sifs_timing_qualified` and `ht_qualified` are distinct
  qualification claims, not synonyms for a successful finite diagnostic.

The finite three-MPDU collector options are `--ht-ampdu-probe-tx` and
`--ht-ampdu-probe-rx`, each followed by a fresh 28-hex-digit challenge and
MCS0..7. The paired runner is `clean-rx/tools/badge/ht_probe.sh` with
`TX_SERIAL RX_SERIAL NEW_PREFIX MCS --ampdu-probe`. These are hardware
operations subject to the trial protocol, not offline checks.

### Aggregate receive deadlines

The badge gives startup and BA setup the original six-second loader budget.
After successful setup, aggregate RX uses an 11.4-second total limit measured
from loader initialization: six seconds startup, the 4.2-second receive
window, the 0.2-second stop allowance and one second reserved for the final
report readback. The report reserve is shared headroom within that total,
not a new deadline granted after a failure. Other modes retain their budgets.

Aggregate RX refreshes the existing eight-second BOOTSEL watchdog every four
seconds while USB servicing remains healthy. Cancellation and the operation
limit still stop work; a stalled application cannot keep refreshing recovery.
The desktop collector's existing 15-second alarm is unchanged.

These bounds have native boundary tests and bounded hardware evidence in the
[R03 rerun](../experiments/HARDWARE_REGRESSION_R03_2026-10-24.md);
they are not general reliability qualification. A returned BA alone does not
qualify an incomplete receiver report.

### Fault snapshot recovery

After a failed operation, fault readback has a separate one-second total
allowance, including any stop attempt. The operation's error, address and
actual value remain authoritative. Recovery uses its own loader context
and a fixed deadline; it cannot restart or extend the operation. Healthy
readback keeps the existing operation deadline. Cancellation is not
cleared: already cancelled operations perform no recovery I/O, and a
cancellation during readback stops further access. Servicing and watchdog
refresh stop when this recovery allowance expires as well.

The aggregate operation limit plus recovery allowance is 12.4 seconds,
leaving host/report-transfer headroom under the unchanged 15-second
collector alarm. Native tests check that an additional 2.5 seconds of
headroom fits. These are loader deadlines, not a replacement for bounded
USB orchestration or a hardware watchdog test.

Current badge reports contain 2,092 bytes. The original 2,048-byte body
retains its field offsets; the appended fields are:

| Offset | Meaning |
| --- | --- |
| 2048 | Snapshot result magic `0x31504e53` (`SNP1`) |
| 2052 | Status: 1 = readback complete, 2 = failed, 3 = deliberately skipped |
| 2056 | Snapshot error using the loader error enum |
| 2060 | Snapshot address |
| 2064 | Snapshot actual value |
| 2068 | Complete-read section bits: frozen = 1, mailbox = 2, multi-capture = 4, HT = 8, aggregate = 16, interface/lease = 32 |
| 2072 | Exact 16-byte request nonce, including mode and channel |
| 2088 | Expected controller generation; must match the mailbox generation |

Lifecycle/network modes retain their own reports and mark generic readback
skipped. A completed readback means the requested reads finished; it does
not assert freshness, frozen state, valid payload delivery or lease release.
The original operation error remains at offset 8, address at 12 and actual
value at 16. A snapshot failure is also printed to collector stderr with
its separate error/address/actual and the original operation error.

The collector's offline inspectors and the station pair verifiers also
accept historical 2,048/2,068-byte files; those lack the R03 section/binding fields.
New live collection requires a matched rebuilt badge image and collector.
Aggregate reports expose `snapshot.valid`, `capture_available` and
`lease_available`. Unavailable capture, delivery and lease values are JSON
`null`; they do not mean no reception. `observed_fixture_bitmap` separately
checks the exact configured good/bad-FCS bodies against the request.
`delivery_payloads_valid` also checks the total/unique/duplicate relationship.
Controller generation is the first little-endian nonce word OR 1, not the
USB application's generation counter.

Aggregate modes exclude generic packet/inventory writes from aggregate-owned
report regions. A BA transmitter initializes the 40-byte receipt record at
offset 1480: magic `0x31524241`, count at 1484 and 32-byte frame at 1488.
Count zero is explicit no returned BA; missing receipt magic produces
`returned_ba: null` and `returned_bitmap: null`, never a known-zero claim.
Positive pair verification requires count one and the exact fresh BA.
An absent delivery record also leaves the reported delivery bitmap `null`.

The aggregate paired runner uses `clean-rx/tools/ampdu_verify.cjs` to
cross-check the JSON against complete, fresh raw reports, independently
generated payloads/FCS and terminal lease/BA evidence. It rejects historical,
partial and stale reports for aggregate acceptance. For a bad-FCS mask of
7, actual observed bad bodies and, for SHM delivery, terminal release are required alongside
zero delivery and no BA; absence is not a passing negative control.

### Aggregate DMA state

DMA builds freeze the first fault state before reclaim/reset, or save the
next-consumer state immediately before shutdown when there is no fault.
The generation-bound record lives at controller SRAM `0x20b00`; a complete
snapshot copies its 48-byte context to detail `720..767`, its 128-byte
physical buffer prefix to `1480..1607` and its controller generation to
`1608..1611`. These regions are DMA-owned, not BA receipt storage.
The report remains 2,092 bytes and delivered payload offsets do not move.

`dma_state` is `null` without valid magic and a matching nonzero generation.
Otherwise it contains `generation`, the physical `prefix` hex string and
these twelve little-endian `words`:

| Word | Meaning |
| --- | --- |
| 0 | Consumer index in low 16 bits; producer index in high 16 bits |
| 1/2 | D11 DMA status 0/1, sampled before reclaim/shutdown |
| 3 | Descriptor flags |
| 4 | Commit magic `0x314d4441` |
| 5/6/7 | Descriptor byte count and low/high buffer address |
| 8 | PSM state / pending outcome, low/high 16 bits |
| 9 | Submitted count / live RXE length, low/high 16 bits |
| 10 | RXE status 1/2, low/high 16 bits |
| 11 | PHY status / RX copy control, low/high 16 bits |

The values are sampled in order, not a claim that every live register changes
atomically. The first fault record is not replaced by later stopped state.
`dma_fault_prefix` remains available for faults `0x8d..0x8f`; historical
prefix-only reports remain inspectable without being promoted to complete
DMA state. A mismatched nonzero generation suppresses both fields.

Offline aggregate inspection uses a matched collector:

```sh
build/clean-rx/badge/collector --inspect-ampdu REPORT.bin CHALLENGE_HEX_28
```

An explicitly invalid/incomplete aggregate report still produces diagnostic
JSON and a nonzero exit status. The
[R05 hardware matrix](../experiments/HARDWARE_ACCEPTANCE_R05_2026-10-24.md)
completed with strict passing evidence for all 44 cells after two fresh
repeats; initial acceptance was 42/44. This does not qualify loss-free
reception, DMA or general HT/BA operation.
The later [R06 campaign](../experiments/DMA_REPAIR_R06_2026-10-24.md)
validates the repaired finite DMA fixture and bad-FCS controls; retained
empty captures still prevent a loss-free reception claim.
