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

## Retry, latency and host-pause measurements

```sh
node clean-rx/tools/reliability.cjs cross EXCHANGES.jsonl
node clean-rx/tools/reliability.cjs network PREFIX SECONDS
node --test clean-rx/tools/reliability.test.cjs
```

These are offline commands. The cross-board reader reconstructs both complete
80-byte legacy request/reply frames, addresses, fresh challenges and FCS before
reporting each directed link's first-attempt rate, six-bin completed-attempt
distribution and host-observed latency median/p95/max. New radio-lab collectors
also archive both complete traces and source-local request/reply timing at
trace offset 384, magic `0x314d3852`. Host collection latency and on-device
round-trip time are separate quantities. Older archives lack device timing;
that field is unavailable, not reconstructed from desktop polling.

For networking, `host_stalls` retains its historical name but counts
**deliberately injected desktop polling pauses**, not detected firmware
starvation. Both collectors now retain each pause's actual monotonic begin/end
in `host_observation`. `CLEAN_RX_NETWORK_HOST_PAUSES=0` disables these pauses
for a control; default/`1` retains the original two-second pauses and cadence.
No retry policy or normal polling interval changes. Invalid values fail
explicitly before a network command is submitted.

The original 152-byte network report prefix is unchanged. A bounded 72-byte
extension (magic `0x314d3842` at relative offset 152) fits both the unchanged
2,092-byte station and 320-byte AP archives:

| Relative offset | Evidence |
| --- | --- |
| 156 | Four acknowledged-message attempt counts, one through four |
| 172 | 64-bit accumulated application round-trip latency |
| 180 | Four latency bins: <=10 ms, <=250 ms, <=1 s, >1 s |
| 196 | Bound network service gaps >=250 ms |
| 200 | Source-built station MAC ACK timeouts; unavailable for the vendor AP |
| 204 | Total diagnostic event count |
| 208 | Last four events: high two bits are retry-enqueued=1, service-gap-end=2 or MAC-ACK-timeout=3; low 30 bits are elapsed milliseconds |

Latency begins at initial application enqueue, not PHY transmission. Attempt
histograms count acknowledged application messages, not firmware retries;
first-attempt rates use all generated messages as denominator. The existing
maximum service interval remains separately reported in `service_max_us`.
AP timeline recording starts after session binding, because binding establishes
its elapsed-time origin. Ring overflow is explicit and makes whole-run event
correlation unavailable; it never becomes a zero-event timeline.

The analyzer checks raw/JSON agreement, histogram/counter totals and measured
pause intervals, retaining old metrics as unavailable when their marker is
absent. Limits are the existing six cross-board attempts, four network
application attempts and two-second maximum network service interval. No new
first-attempt reliability target is invented. Shared-millisecond service-gap/
retry/ACK-timeout observations are not a causal RF-loss proof. Desktop and
device clocks are not synchronized, and a MAC ACK timeout alone cannot locate
data-versus-ACK loss; an independent RF loss timeline is still required.

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

## D2 secure Block ACK software lifecycle

The [WPA adapters](../../badge/wpa2.h) now require explicit, exclusive binding
of caller-owned BA storage and optional delimiter-stream storage through
`clean_wpa_ba_bind`, after key installation and **before** preparing agreements.
Binding initializes receive storage; it neither activates an agreement nor
installs a controller response. Repeating the identical binding preserves
admitted ciphertext; replacing either storage pointer returns a conflict.
Storage must remain valid until `clean_wpa_ba_unbind`; do not copy a bound WPA
state. Abort before reinitializing WPA state for another association.

Use `clean_wpa_ampdu_begin` for successive software PPDUs. A partial delimiter,
partial MPDU or complete MPDU awaiting pool credit returns `CLEAN_BA_BUSY`
instead of being overwritten by the next begin. Retry the unconsumed input
after backpressure, including a zero-byte tail when all input was consumed.
A completed or failed stream can begin again without resetting agreements,
receipt bitmaps or PN state. An explicit `clean_wpa_ba_reset` instead wipes
agreements, queued ciphertext and partial stream storage.

Starting a pairwise rekey invalidates the binding's receive contents and
suspends BA admission until the new PTK is installed. Installation resets
receive storage again; a new agreement is required. Duplicate M3 messages
and GTK-only changes preserve existing unicast BA state. WPA abort, timeout
and MIC failure also wipe the bound storage. Failures inside a reorder sink
defer that wipe until the outer receive, poll, BAR or stream call returns,
so the queue is never reset underneath its callback.

The canonical `clean_wpa_receive` entry point now routes protected unicast
QoS frames for explicitly bound, active agreements through ciphertext reorder,
including normal-ACK-policy frames within those agreements. Compressed BARs
use the same ordered release path. `clean_wpa_poll` services gaps and agreement
timeouts even without another received frame. No ADDBA request is accepted or
activated by this dispatcher.

Ordered EAPOL processing retains at most one response. While that response is
queued, the delivery sink stalls **before** authenticating another MPDU or
advancing its PN. This also prevents a deferred PTK reset from releasing more
old-agreement ciphertext. Polling drains the response; pending replies are WPA
output and survive a BA-only reset/unbind, but abort, timeout and authentication
failure wipe them. Direct BA and delimiter-stream adapters share this behavior.

`CLEAN_WPA_RX_RETRY` means the input is still caller-owned, not that a response
was produced. The badge station loop retains that packet and retries after
polling instead of consuming another F2 packet. While a controller submission
is outstanding, it passes a null output to `clean_wpa_poll`: timers still run,
but ordered delivery and output consumption pause so the submitted frame and
its key-response bookkeeping cannot be overwritten. Fragment expiry and its
pending PN cleanup also run during this paused polling.

**Boundary:** canonical software ingress and the badge caller are wired, but
the live station still does not bind or activate BA storage: a production
agreement cannot be installed safely yet. Its owner must revoke/read back controller
response state on these transitions before permitting further reception.
There is still no production BA-install/revoke command or general aggregate
receive path. Do not activate a production agreement or advertise HT from
these software results. The fixed fixture command is not that backend.
Default-runtime and extended-WMM WPA sanitizer tests, shared authenticator
regressions, and linked badge/Pico builds pass. No devices were flashed for
this change; the station loop's revised TX/RX scheduling is not RF-qualified.

Focused offline tests, with no device discovery or radio operations:

```sh
make -C clean-rx/badge SELECTION=wmm-edca ../../build/clean-rx/badge/wpa2_test
build/clean-rx/badge/wpa2_test
```

Coverage includes canonical receive through station preprocessing, normal/BA ACK
policies, BAR/gap/timeout service, one-reply pressure across canonical/direct/stream
ingress, ordered GTK replies and PTK invalidation, paused-TX deadline handling,
binding-identity rejection, the exact 16-slot pool limit, no receipt bit on rejected
admission, zero-byte retry, three successive variable-length software PPDUs,
PTK/GTK/retransmission boundaries, timeout, reassociation and deferred
MIC-failure wiping through all four adapter entry points. These are not RF,
transport-continuation, SIFS or negotiated-interoperability qualification.

## Repeated retained-SHM handoff (offline prototype)

`CLEAN_RX_AMPDU_REPEAT=1` is default-off and diagnostic-only. It requires
`CLEAN_RX_AMPDU_SHM=1` and excludes the diagnostic BA responder. The
[controller handoff](../../src/runtime/aggregate_rx.inc) now keeps the PSM's
terminal lease until every saved good MPDU has acquired a bounded SRAM slot.
The controller checks publication/release counters before claiming and
releasing each lease. The PSM then resets its capture/prefix counters for
another PPDU, without clearing the monotonic lease evidence.

When SRAM is full, good frames remain in the immutable snapshot and the PSM
stays held. Bad outcomes need no SRAM credit and cannot overwrite occupied
slots. Malformed outcomes, count mismatches, stale/skipped leases and failed
MAC pauses produce explicit firmware errors. Publication and release support
16-bit counter wrap; the existing SRAM ring retains its 32-bit counter rules.
The default one-shot path remains available.

This still accepts the **existing, at-most-three, 80-byte fixture bodies and
252-byte HT-SIG contract**. It does not generalize RXE completion, variable
lengths, arbitrary N-body aggregates or live station agreements. Holding a
producer is software backpressure, not an RF flow-control guarantee.

Offline test command, without device discovery or radio operations:

```sh
make -C clean-rx/tests PROFILE=diagnostic \
  BUILD=../../build/d2-repeat-local/tests \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3 -DCLEAN_RX_AMPDU_HANDOFF=2 -DCLEAN_RX_AMPDU_SHM=1 -DCLEAN_RX_AMPDU_REPEAT=1' \
  aggregate-rx-test
```

The [integration test](../../tests/integration/aggregate_rx_test.c) executes
the actual controller fragment against the existing PSM instruction model,
with modeled MMIO/SHM and independently checked complete delivered bytes/FCS.
It covers 100 successive leases, partial terminal batches, mixed/all-bad
outcomes, exact ring exhaustion, delayed credit, malformed claim/release,
pause failures and 16-/32-bit wrap under ASan/UBSan. Repeated and one-shot
PSM tests, matched firmware builds and the finite oracle also pass. The
default runtime controller remains byte-identical to the preceding build.

The producer records marker `0x5250` at interface SHM word `+78`, saved-report
offset `1264`; the marker survives rearm and does not overlap terminal or BA
records. The native collector and finite-fixture verifier explicitly reject
this mode rather than promoting a single retained snapshot into a repeated
reception claim. Live multi-PPDU collection and a freshly authorized, bounded
hardware campaign are still required. No hardware was flashed for this
implementation; HT association remains disabled.

### Independent repeated-execution oracle

The native harness can now export a bounded, persistent execution transcript
from the actual controller fragment and PSM interpreter, rather than just
asserting success internally. Run the same diagnostic flags above with
`aggregate-rx-oracle-test` instead of `aggregate-rx-test`. The target writes
`aggregate-repeat.ndjson` in the selected build directory and runs
[the independent JavaScript oracle](../../tools/ampdu_repeat_verify.cjs),
including negative controls. No device discovery or radio operations occur.

The version-1 `clean-rx-repeat-model` format is newline-delimited JSON:

- The header fixes the eight-slot SRAM capacity and
  `source: "controller-psm-model"`.
- `begin`/`end` delimit the repeated, pressure and counter-wrap scenarios.
- `publish` retains all 288 SHM capture bytes, observed outcomes and lease
  counters. The oracle independently constructs each scheduled fixture's
  complete body, FCS and aggregate HT-SIG; it does not trust the C fixture's
  expected-byte array or a reported good-frame count.
- `poll` records actual post-call ownership, the retained bytes and any new
  complete SRAM frame. `consume` records the actual frame and returned credit.
  The oracle checks every transition, frame order and held-buffer immutability.
- A mandatory final `complete` record prevents interrupted executions from
  passing. Files above 4 MiB or 2,048 events are rejected.

The deterministic scenarios cover 114 modeled PPDUs, 115 delivered bodies,
ten full-ring stalls, three bad outcomes drained with no credit, and both
16-bit lease and 32-bit ring-counter wrap. Mutation tests reject missing,
duplicated or reordered events, stale identities/bytes, altered bytes with a
recomputed FCS, bad-frame delivery, incorrect counters and damaged captures.

Inspect an existing transcript without rerunning the model:

```sh
node clean-rx/tools/ampdu_repeat_verify.cjs \
  build/d2-repeat-local/tests/aggregate-repeat.ndjson
```

**Boundary:** this is an offline fixed-fixture model oracle, not a live USB
collector, transport/RF witness or fresh-campaign binding. All output explicitly
leaves live-collection, variable-length, RF, SIFS and HT qualification false.
The existing finite-fixture collector still rejects repeated-mode reports.
No controller/PSM/badge executable behavior changes in this oracle increment.

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

## Nonaggregate HT evidence

Data/extended A-MSDU and normal-ACK collectors require a complete, frozen,
nonce/generation-bound snapshot of the mailbox, multi-capture and HT sections.
Unavailable evidence produces `snapshot.valid:false`, `capture_available:false`
and null reception/submission fields, not zero captures. The standalone
inspection command is:

```sh
build/clean-rx/badge/collector --inspect-ht REPORT.bin CHALLENGE_HEX_28
node clean-rx/tools/ht_verify.cjs EVIDENCE_PREFIX
```

Use the matching diagnostic selection/image/collector. `--inspect-ht` handles
modes 24-28; aggregate inspection remains separate. The verifier checks raw
request identity, generation, configuration, completion, retained payload bytes,
the full-payload validation summary/FCS, metadata and returned ACK evidence
against JSON. Long-frame reports retain a prefix, not a complete MPDU archive:
their full-byte comparison still runs on the badge. Data/ACK pairing no longer
prints a hard-coded successful summary. A receiver's fresh validated fixture
can confirm transmission; local submission alone cannot.

Modes 24-28 reserve the following 32 bytes of the otherwise unused post-failure
report gap. Aggregate and station layouts are unchanged:

| Offset | Field |
| --- | --- |
| 1800 | Timing magic `0x31545448` |
| 1804 | Controller request generation |
| 1808 | Post-setup receive-service window beginning |
| 1812 | Receive-service window end, before normal stop |
| 1816 | TX command loop beginning; zero if not reached |
| 1820 | Submitted command/result accepted; zero if not reached |
| 1824 | Stopped PHY-start handoff counter |
| 1828 | Stopped completion handoff counter |

Times are local microsecond deltas from probe start. The two handoff counters
are diagnostic progress, not RF witnesses. JSON also records monotonic desktop
USB-submit brackets and report arrival. These clocks are not synchronized
between boards; adding the desktop submit time to a local delta does **not**
prove receive/transmit overlap. The existing 200 ms staging lead is not a
radio-arm handshake, and has not been increased as a presumed reliability fix.

### Locating a nonaggregate receive loss

```sh
node clean-rx/tools/ht_verify.cjs --diagnose EVIDENCE_PREFIX
```

This retains the ordinary strict pair result and adds stage evidence for
TX, target RX and an optional source-built observer. New images reserve a
92-byte `ht_f2` record at offset 1860, ending before generic counters at 1960:
magic `0x31463248`, correlated count, first packet size/time, calculated and
received full-packet FCS, four metadata bytes and the first 64 MPDU bytes.
Correlation requires the fresh transmitter address and sequence. Capture
does not require valid FCS or expected HT metadata, so a rejected host fixture
is not mistaken for missing radio reception. Later correlated arrivals
increment the count without replacing the first record.

Missing records in older images are unavailable, not zero observations.
Long-frame records retain a prefix and full-packet CRC summary, not a
complete archive. A matching early HT-SIG is only a rate/length candidate:
it has no MAC identity and does not prove reception of the requested fixture.
The existing ACK60 rejection record additionally retains the rejected bytes
twice; nonce-correlated bad-FCS bytes, changing reads, controller capture
without correlated F2, and correlated F2 rejected by the host are distinct
stages. Rejection bytes are SHM observations, not qualified RXE FIFO captures:
`shm_copy_freshness_proven` remains false even when the two reads match.
Short rejected frames can bypass the final good-frame copy; the standard
`ht-response` image has a 128-byte stream threshold, above ACK60's 66 RXE
bytes. Do not interpret bad software FCS on that SHM record as proof of
on-air corruption. These diagnostics do not change the controller RX/TX path or relax
pair acceptance. An unexplained absence stays unexplained.

### Passive independent witness

The explicit `BADGE_PICO=1` build is a **receive-only Pico 2 W diagnostic**,
not the normal badge image or a replacement for the existing Pico application.
It qualifies the reference/system clocks using the existing Pico helper,
does not initialize/write the TFT, and rejects every radio mode except
Data RX (25). It cannot enable station responses or submit a TX fixture.
The default `BADGE_PICO=0` retains the badge behavior.

Build isolated controller, Pico observer and matching collector:

```sh
make -C clean-rx/src PROFILE=diagnostic SELECTION=ht-response \
  BUILD=../../build/clean-rx/observer-controller
make -C clean-rx/badge SELECTION=ht-response BADGE_PICO=1 \
  BUILD=../../build/clean-rx/pico-ht-observer \
  CONTROLLER_BIN=../../build/clean-rx/observer-controller/cyw43_rx_fw.bin all test
```

Never cross-flash badge/Pico images. Preserve and restore complete originals
under the hardware-trial protocol. The observer collector uses the ordinary
`SERIAL EVIDENCE_BIN CHANNEL --ht-data-rx CHALLENGE_HEX_28 MCS LENGTH` interface.
It publishes only the final, frozen 2,092-byte report; there is no live-ready
snapshot or RF-arm handshake. Start it alongside the target RX before TX,
calibrate positive reception first, and retain the actual timing evidence.
Use `witness_kind:"source"` in the pair configuration (optionally binding
`witness_build` as well).

For Data28 and ACK60, the oracle verifies the complete captured HT-SIG,
requested MCS, MPDU and FCS. For A-MSDU3869 it additionally requires the
Pico's full-fixture validation summary and retained prefix; the complete
long MPDU is checked on-device, not entirely archived. ACK60 is deliberately
observed in passive mode 25: the C collector correctly reports that it is
**not** the ordinary Data60 fixture, while the separate oracle checks the
actual ACK-requesting wire fixture.

The unused nonaggregate report gap additionally contains:

| Offset | Field |
| --- | --- |
| 1832 | First passive ACK magic `0x31574148`; zero if not recorded |
| 1836 | Matching complete ACK count |
| 1840 | First matching 14-byte ACK, including its FCS |
| 1856 | Receive-only image marker `0x31534f48` |

Legacy beacon-table writes are excluded from these HT modes: the old table
shares bytes 1640-1959 with these diagnostics. The
[R07 report](../experiments/HT_RECEPTION_R07_2026-10-24.md)
records the discovered overlap and its original native-only validation.
The separately authorized [loss-stage follow-up](../experiments/HT_ROOT_CAUSE_2026-10-24.md)
RF-tested the repair, retaining all 92 complete snapshots/timing records.

ACK recording requires a complete valid-FCS control frame addressed to the
fresh transmitter MAC. The oracle checks those bytes and zero TX attempts,
submissions and ACK-response configuration. A zero passive ACK count is
not proof that no ACK was transmitted. Fresh MAC identities must not be reused.
This third physical radio shares the source-built receiver implementation;
it does not establish vendor interoperability or physical SIFS timing.
The R07 campaign positively calibrated HT frame observation, **not**
passive legacy ACK acquisition. Do not treat its zero ACK records as
qualified observations of absent responses.

`ht_verify.cjs` optionally consumes matching `-witness.bin`/`-witness.json`
files. It distinguishes a complete witnessed frame missed by the target,
a correlated corrupt capture, unrelated capture, missing/only-started ACK,
an independently witnessed ACK not returned to TX, and unavailable evidence.
Without a positive receiver/witness observation, missing TX versus missing RX
remains explicitly unresolved. An absent passive witness never proves no RF.
Intentional omitted-TX/omitted-RX controls must be declared in configuration.

### Rejected ACK60 copy-state trace

`CLEAN_RX_HT_REJECT_TRACE=1` is an opt-in diagnostic for D2 reception work,
with one controlled active-rejection calibration, not general copy-freshness
or passive-rejection qualification. It requires `PROFILE=diagnostic` and
`SELECTION=ht-response`; runtime, compile-time HT-observer and aggregate builds
reject it. Match the flag on the controller and badge/Pico host image.
Receive-only mode 25 still uses this ordinary `ht-response` build, not the
compile-time observer selection. Mode 27 remains the active ACK60 receive role.

The [initial RF prototypes](../experiments/HT_REJECT_COPY_2026-10-25.md)
overlapped derived ACK allocations and are not qualified copy-state evidence.
Four corrected-layout hardware controls passed and retained eight entirely
zeroed receiver records. A later
[bad-FCS control](../experiments/HT_REJECT_BAD_FCS_2026-10-25.md) exercised the
active rejection arm with a complete programmed-byte match, but not the
passive arm. A later
[spontaneous miss](../experiments/HT_SPONTANEOUS_REJECTION_2026-10-25.md)
retained a bound active record with 54/60 differing MAC bytes and no complete
fixture identity. Its partial fresh-payload match does not qualify general
copy freshness. Use fresh matched images, not either retained prototype.

Read the frozen rejection's own SIG/metadata separately from the rolling
last-candidate trace, which may have been overwritten by later activity.
`no_correlated_capture` does not exclude an eligible rejection lacking complete
MAC identity. Also, `tx_diagnostic.mac_rx_turnaround_us` includes the reliable
command's ACK wait before its final timestamp; it is not an isolated hardware
RX-ready latency measurement.

For an explicitly authorized negative control, add
`CLEAN_RX_HT_REJECT_BAD_FCS=1` **only to the transmitting badge/controller
build**. It requires the rejected-copy trace and changes only clocked ACK60 TX:
after normal command/MAC validation, the template write and its verified
readback XOR bit 7 of the last FCS byte (PLCP-inclusive byte 65). The input
packet, HT-SIG, MAC body and other TX modes remain unchanged. Receiving badges
and the passive Pico use the ordinary trace build. Native tests verify the
exact one-bit change and default-off identity. Template readback proves
programming, not the on-air FCS: a delivered valid frame means the intended
negative control was not achieved, not that a bad-FCS frame was accepted.

For an explicitly authorized initial-buffer comparison, add
`CLEAN_RX_HT_REJECT_BASELINE=1` to the **badge/Pico host build**, retaining its
matched trace-enabled controller. The option defaults off and requires the
diagnostic rejected-copy trace. It adds no controller or PSM instructions.
Only ACK60 receive modes 25/27 capture it; other modes and lengths do not.

Before enabling RX, the host verifies controller state 8, host command 1,
the request generation and stopped MAC/PSM bits. It reads SHM words
`0x801..0x821` through the object selector/data port, then repeats those
guards before committing the record. It never writes the receive buffer,
poisons SHM, changes thresholds or forces a copy. The extra pre-RX SDIO reads
do change startup timing.

The 92-byte record at report offset 1520 holds magic `0x31425248`, generation,
before/after MACCONTROL, probe-relative begin/end times, 66 baseline bytes and
a zero reserved word. It reuses only unused ACK60 packet-report tail space.
Publication fails explicitly if retained packet bytes would overlap it;
packet, rejection, ACK and counter evidence are not overwritten.
The native collector exposes `ht_reject_baseline`. Set
`require_reject_baseline:true` in an offline verifier configuration to require
it on each receive report. The independent decoder checks raw/JSON equality,
generation, stopped bits, reserved storage, packet bounds and timing before
the receive window.

When a rejection exists, the diagnosis compares its bytes with that initial
baseline, including unchanged MAC offsets that differ from the normal
fixture. Deliberate bad-FCS controls need a separate programmed-byte comparison.
Equal initial/rejected bytes are observations, not a general stale-byte proof;
different bytes exclude unchanged startup content, not stale content from
intervening candidates. `shm_copy_freshness_proven` remains false.
The [eight-attempt hardware comparison](../experiments/HT_REJECT_BASELINE_2026-10-25.md)
retained sixteen baselines, a byte-exact active negative control and a normal
A-to-B miss with bound active/passive rejections whose MAC bytes differ from
their respective initial buffers. It does not qualify general freshness or
an independent receiver implementation.

#### Per-candidate pre-copy trace

`CLEAN_RX_HT_CANDIDATE_TRACE=1` additionally requires the rejected-copy trace
and defaults off. Match it on controller and badge/Pico builds. It excludes
the early FIFO and copy-scope profiles as well as the existing aggregate/
compile-time-observer exclusions. Unlike the host-only baseline, it changes
the receive PSM program and timing; it is **not RF-qualified**.

Before starting the PSM, the controller clears only the 50 diagnostic SHM
words `0x400..0x431` and invalidates publication magic. It does not clear
receive SHM. Before each candidate's prefix-copy setup, the PSM snapshots
66 existing receive bytes, sequence, adjacent TSF halves, raw RXE fields and
five copy operands. The first eligible rejection freezes the preimage and
binds cause, TSF and length. No copy command/threshold or general register
is changed. Sequence zero is valid after wrap.

The stopped controller record is 104 bytes at SRAM `0x20800`; the host
publishes it at report offset 1216 only for ACK60 modes 25/27 with commit
0/1 and retained packet size at most 112 bytes. It aliases inactive aggregate
storage and unused packet-report tail space, never packet/rejection/ACK
evidence. The collector exposes `ht_candidate` by saved magic, including
offline replay without the compile flag.

Set `require_candidate_trace:true` to require it on each valid receive
report. The independent verifier checks every raw/JSON field, packet bounds,
commit/rejection-copy agreement, cause/TSF/length binding and modular
start-to-rejection order. Diagnosis emits `candidate_trace` and compares a
committed preimage with the rejected bytes using the same byte-comparison
logic as the startup baseline. Uncommitted records cannot supply a rejection
comparison. Equality/difference does not establish stale-byte or RF provenance;
`shm_copy_freshness_proven` stays false.

The [first hardware attempt](../experiments/HT_CANDIDATE_TRACE_2026-10-25.md)
stopped on failed diagnostic publication in both receivers. Missing diagnostic
SHM initialization was then repaired and tested offline. In a separately
authorized [repaired-image follow-up](../experiments/HT_CANDIDATE_REJECTION_2026-10-25.md),
the first normal control failed with valid reports and two bound rejections.
No incorrect MAC byte equalled its own pre-command snapshot byte. The
probe has not passed positive/negative RF calibration; probe perturbation,
general copy freshness and TX/RF versus shared RX remain open.
Old images are not qualified or reusable control images.

The later [matched probe comparison](../experiments/HT_PROBE_CROSSOVER_2026-10-25.md)
holds normal TX and the passive Pico probe-off and changes only the target
receiver. It supplies passing normal probe-on controls in both directions,
one byte-exact probe-on active negative on receiver B, and three byte-exact
passive negatives. Receiver A's probe-on negative was not retained. Probe-off
body corruption and a separate delivered-body/returned-ACK failure also
recurred. This narrows calibration gaps without establishing a harmless probe,
general freshness or a product reliability/timing qualification.

Example offline builds (with the documented LLVM toolchain on `PATH`):

```sh
make -C clean-rx/src CC=clang PROFILE=diagnostic SELECTION=ht-response \
  EXTRA_CFLAGS=-DCLEAN_RX_HT_REJECT_TRACE=1 \
  BUILD=../../build/clean-rx/reject-copy all
make -C clean-rx/badge PROFILE=diagnostic SELECTION=ht-response \
  EXTRA_CFLAGS=-DCLEAN_RX_HT_REJECT_TRACE=1 \
  CONTROLLER_BIN="$(pwd)/build/clean-rx/reject-copy/cyw43_rx_fw.bin" \
  BUILD=../../build/clean-rx/reject-copy-badge firmware \
  ../../build/clean-rx/reject-copy-badge/collector
```

For Pico 2 W, add `BADGE_PICO=1` and use a separate host build directory.
These commands build images only; no new hardware campaign is authorized.

The probe bypasses only the old active-response-policy requirement for the
existing rejected-ACK60 hook. Matching current/early SIG and declared 60-byte
length gates remain. It freezes the **first** eligible bad-FCS/abort record,
including the two existing SHM reads, without enabling responses, forcing a
copy, writing copy/control registers or changing retries/deadlines. Additional
diagnostic instructions do perturb the rejection path's timing.

Before/after raw operands are ordered `0x1004`, `0x1001`, `0x1002`, `0x102a`,
`0x102b` (control, base, limit, end and next-length samples). Their hardware
semantics/completion behavior are not independently established. A 66-byte
RXE rejection is below the compiled ordinary 128-byte streaming threshold;
it also bypasses the final GoodFCS copy. Stable SHM reads therefore cannot
establish that the bytes belong to a complete current frame.

The 2,092-byte report size and existing rejection offsets remain unchanged.
Receive modes 25/27 alone reuse station-only peer-policy report storage:

| Offset | Copy-state field |
| --- | --- |
| 1040 | Magic `0x31504348` |
| 1044 | Six 16-bit words: commit count, cause, TSF low/high, RXE length, stream threshold |
| 1056 | Five before operands |
| 1066 | Five after operands |

Controller storage is SHM words `0x790..0x79f`, after ACK arithmetic/select
state, and SRAM `0x20600..0x20623`, after PHY-edge evidence. The SRAM slot is
intentionally shared with the excluded aggregate probe, not with ordinary
TX completion, ACK suppression/deadline or arithmetic records.
The controller publishes the SRAM magic last; the host includes both records
in the frozen snapshot and propagates read failures as unavailable evidence.
A current matched collector decodes optional `ht_reject_copy` by its marker,
not its own probe flag, and decodes passive rejection bytes against a separate
ACK60 template rather than the normal Data60 fixture.

`node clean-rx/tools/ht_verify.cjs --diagnose EVIDENCE_PREFIX` independently
checks every exposed operand against the raw report. Set
`"require_reject_copy": true` in the campaign configuration to require the
marker on intended target/passive receive roles; older archives without this
requirement remain readable. Diagnosis exposes `copy_trace.status` as
`uncommitted`, `record_mismatch` or `bound_to_rejection`, checking count,
cause, TSF and RXE length against the frozen rejection. Uncommitted includes
both no eligible rejection and interrupted capture; neither is a zero-loss
observation. Threshold/stability interpretations are withheld on mismatches.
`shm_copy_freshness_proven` stays **false** even for bound, stable records.
This trace cannot establish RF corruption or distinguish transmitter waveform
failure from a shared source-built receiver failure.

Native tests exercise complete ACK bytes, truncation and stale identity.
Badge tests run the dependency-free Node oracle against the actual C decoder
for every MCS and all three endpoint/observer fixture types, including
long-frame FCS agreement, malformed snapshots, passivity and raw/JSON
disagreements. Read-only decoding of the abandoned experimental 320-byte
vendor witness remains for auditing archived R07 trials; its monitor modes
are not maintained tools.
The [R06 campaign](../experiments/DMA_REPAIR_R06_2026-10-24.md)
validates the repaired finite DMA fixture and bad-FCS controls; retained
empty captures still prevent a loss-free reception claim.
