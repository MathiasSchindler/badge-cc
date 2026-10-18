# AP authentication and bounded RX staging investigation

## Scope and conclusion

This D2 increment investigates authentication replies missing from an active
station, then reduces receive-service coupling to host backpressure. It does
**not** establish the exact PHY/capture cause or fix the missing replies.
HT/WMM advertisement remains disabled, explicit legacy fallback remains, and
D2 is incomplete.

A subsequent source audit repaired a concrete MAC-control receive-interruption
hazard, described in [the live-RX repair](#live-rx-control-repair). Missing AP
replies and upstream corrupt HT captures are not completely resolved.

Two badges were used only for bounded trials. Badge A is
`08DA26F86E57F5EA`, badge B `9BBF88DD17D66E7D`. Their RP2350 A4 identity
does not identify the Wi-Fi silicon version/revision. Local credentials were
read privately from the ignored credential file; no password was provisioned
to these open-authentication probes or included here.

Evidence prefixes below are relative to `build/clean-rx/badge/`. Reports and
binary captures are retained there, not checked into the source tree.

## Independently observed local-AP replies

| Trial prefix | Observation |
| --- | --- |
| `d2-ap-investigation-station-monitor` | Channel 1: monitor validated four requests; no authentication reply observed by either endpoint |
| `d2-ap-investigation-raw-monitor` | Channel 1 raw request: no observed authentication reply |
| `d2-ap-investigation-ch6` | Monitor validated four successful AP authentication replies; active station published no authentication reply and saw no own-RA candidate |
| `d2-ap-investigation-ch6-raw` | Raw request also missed replies independently captured by the monitor |
| `d2-ap-investigation-ch6-swapped` | Roles swapped: active B failed; passive A observed requests but no replies |

Monitor absence is not proof of AP silence. Role swapping does not exclude
placement/reception differences. An initial channel-11 orchestration failure
was retained separately and is not an RF result.

Passive monitor parsing now lives in the testable
`clean_station_monitor()` helper. It pins the first observed request TA,
ignores other stations' requests, and records first/last request/reply times
and requests observed before the first reply. Times are badge-host
observations relative to monitor start, not RF timestamps.

The timed channel-6 trial first observed a reply 105,157 us after the first
request, with two requests already observed. This cannot assign that reply
to the first request. Additional bounded controls narrowed the explanation:

- `d2-ap-radio-control-{1,2}` configured the monitor with the station radio
  profile but issued no TX. Both controls received four replies, with the
  first observed 2,263/3,235 us after the first request. Station radio setup
  alone does not explain the active station's loss.
- `d2-auth-cck-control-{1,2}` used a 1-Mbps request instead of OFDM. Both
  active trials failed while the monitor saw three replies each, first
  observed after 2,207/4,196 us. Changing request rate did not fix reception.
- These replies were observed well within the existing host response wait.
  Temporary control branches were removed; no speculative PHY-register
  changes or timeout extensions were retained.

Zero own-RA candidates alone does not locate the fault. Together with zero
controller-published authentication responses and independently valid replies,
the evidence places this loss upstream of host authentication parsing.
It does not yet distinguish PHY reception, capture/rearm, or another
controller-side loss.

## Implemented receive-service changes

The active station previously held the sole PSM capture while waiting for
the one logical F2 credit. The implementation now:

- Uses the existing eight SRAM capture slots as a FIFO for station profiles
  2/3, with separate staged and published counters and checked unsigned-wrap
  occupancy. Command 7 starts the published cursor after discovery history.
- Copies complete frame bytes and metadata before releasing PSM SHM.
  Publication preserves FIFO order and retains the head across descriptor,
  credit and fragment backpressure. The host credit window is still one.
- Retains the next capture in SHM on FIFO saturation instead of overwriting
  staged data. Target-based completion drains staging; an explicit stop can
  leave pending RX. `mac_pending_rx` includes both ownership locations.
- Processes NAV before selecting own-RA captures and pinned-peer group
  management/data. Unrelated traffic does not consume transport credit;
  this header selection is not frame validation. Passive/raw RX is unchanged.
- Adds filtered-count and maximum sampled profile-3 capture-age diagnostics.
- Replaces bitwise FCS calculation with a dependency-free, 16-entry nibble
  table. Differential tests check every byte value and supported length
  against the independent bitwise implementation.

The `MCR1` trace is now 112 bytes at `0x20250`, ending at RX trace `0x202c0`.
Its badge copy occupies detail offsets 1808..1919. Soak fault evidence moved
from 1912 to 1920; compile-time bounds and native copy tests prevent overlap.
The role-27 response-delay reader uses the field offset rather than a
trailing-size assumption.

An early queued soak report overlapped the new trace tail with fault
evidence. Its new trace metrics are invalid; payload/count results remain
usable. Do not reinterpret those early metrics. Final builds use the fixed
layout.

## Final default build and legacy controls

Default badge build: `0d3be224`, image/collector in `d2-queued-default/`.
Gated badge build: `eb30aa43`, image/collector in `d2-queued-edca/`.
The gated controller enables `CLEAN_RX_HT_DATA`, `CLEAN_RX_HT_RX`,
`CLEAN_RX_HT_RESPONSE` and `CLEAN_RX_EDCA`; these do not enable station HT
advertisement.

| Evidence prefix | Result |
| --- | --- |
| `d2-queued-fcs-soak` | Default: 16 deliveries per direction across 32/1500/2304-byte payloads, 16 attempts per side, zero corruption/order/controller errors |
| `d2-queued-edca-legacy-soak` | Gated: 16 deliveries per direction, 16/17 attempts, zero corruption/order/controller errors |
| `d2-queued-fcs-vendor` | Default clean station authenticated/associated with an unmodified vendor-firmware AP; AP independently retained the correct station peer |
| `d2-queued-fcs-ch1` | Four attempts; no authentication reply observed |
| `d2-queued-fcs-ch6` | Four attempts; monitor saw four successful replies, active station saw no own-RA candidate |
| `d2-queued-fcs-ch11` | No usable discovery, input error 6, zero TX attempts and no controller error |

Independent decoding of the final channel-6 saved reply validates its FCS,
station destination, open-authentication sequence 2 and successful status.
Its first host-observed reply followed the first request by 3,664 us.
The queued/FCS implementation did not recover this reply at the active
station. Successful vendor association is a control, not proof of
compatibility with the local APs or negotiated HT operation.

Final default sampled maximum capture ages were 2,900/1,864 us in the paired
soak, 765 us in the channel-6 station trial and 376 us in the vendor AP trial.
These bounded observations are not latency guarantees or RF measurements.

## HT receive qualification remains failed

The final gated matrix covers MCS0/7, TIDs 0/1/4/6 and both directions.
Prefixes are `d2-queued-edca-m{0,7}-t{0,1,4,6}-{ab,ba}`.

| Build/control | Passed |
| --- | --- |
| Queued `eb30aa43`, MCS0 | 8/8 |
| Queued `eb30aa43`, MCS7 | 2/8 |
| Previous `e3bb50ff`, MCS7 | 6/8 |
| Identical-payload replay, previous build | 2/4 |
| Identical-payload replay, queued build | 2/4 |

The initial old/new matrices used different random payloads and cannot
isolate a regression. Four original failed-cell challenges were therefore
replayed unchanged on both builds: TID0 A-to-B, TID0 B-to-A, TID4 B-to-A and
TID6 A-to-B. Outcomes varied, including a payload passing after a prior
failure. This excludes a simply deterministic-payload explanation, but the
small sample does not statistically exonerate the changes.

Independent verification in `d2-queued-independent-verification.json`
checks all 16 AC/CW/AIFS/TXOP readbacks. The ten successful cells have exact
challenge-derived MPDUs, independent CRC32 and returned ACK CRC32/address
checks. All six failed cells have invalid MPDU FCS and repeat-identical raw
snapshots. First differing MPDU bytes are 5, 10, 0, 1, 2 and 6; corruption is
not restricted to trailing payload. Previous-matrix and replay failures were
also independently checked for invalid FCS and identical repeated snapshots.

The old/new gated PSM byte arrays compare equal, SHA-256:
`1186133092052798904870ca4f106fc8da1c08f2e9a14793b2b05a6c0381d0f4`.
Corrupted bytes are present before F2 transport and repeat identically
within each failed capture, though different attempts can corrupt differently.
This is not host authentication parsing or a transport-only corruption
explanation. It does not establish the exact PHY/RF/capture cause.

## Validation and hardware preservation

Default and gated controller/badge native suites passed with ASan/UBSan.
Both freestanding builds passed; all four ELFs have no undefined symbols.
Both badge run stacks are 904 bytes. Native additions cover monitor binding
and timestamps, selection boundaries, FIFO credit/wrap arithmetic, FCS
equivalence and report-region overlap.

Before any writes, fresh complete 16-MiB backups were verified as
`d2-ap-investigation-start-{A,B}-full.bin`. Both originals were restored
after testing. Independent complete readbacks
`d2-ap-investigation-restored-{A,B}-full.bin` compare byte-for-byte with the
backups; restore logs are retained. Both badges remain in BOOTSEL.

Remaining work is tracked in the
[D2 implementation gates](HT20_IMPLEMENTATION.md#remaining-d2-work):
resolve receive losses, implement/qualify actual negotiated HT/WMM policy,
protection and required Block ACK/A-MPDU/reordering obligations, and pass
independent negotiated-peer and sustained receive qualification.

## Follow-up: amplitude controls and rejected authentication capture

This follow-up did not complete D2. The final default build is `974503ec`;
the gated build is `8112616b`, under `d2-finish-{default,edca}/`.
HT advertisement remains disabled. The legacy/default baseband multiplier
remains 64; no unproven PHY correction was made the default.

### Matched transmit-amplitude controls

The existing transmit IQ-local table's baseband multiplier was reduced
from 64 to 16 for a bounded RF control, without changing PSM bytes.
Four MCS7/60-byte/normal-ACK challenges, TIDs 0/4 in both directions,
were then replayed unchanged with the original setting and again with
the reduced setting:

| Chronological control | Valid payload and returned ACK |
| --- | --- |
| Multiplier 16 | 4/4 |
| Multiplier 64, identical challenges | 2/4 |
| Multiplier 16, identical challenges repeated | 3/4 |

Prefixes: `d2-finish-{bb16,bb64,bb16-repeat}-t{0,4}-{ab,ba}`.
`d2-finish-amplitude-verification.json` independently checks challenge
identity, delivered payload bytes, MPDU/ACK CRC32 and ACK destination.
All three failed exchanges retain repeat-identical, independently invalid
FCS captures. The remaining reduced-amplitude failure has 45 fixture bit
errors, not a valid reception.

The result suggests transmit distortion or receive overload deserves
investigation, but the small, single-placement sample establishes neither
the mechanism nor a reliable power setting. It does not qualify MCS7 or
negotiated HT. The explicit controller build control
`-DCLEAN_RX_TX_BBMULT=16` preserves reproducibility; only the tested 16/64
settings compile, and 64 remains the default. Values are baseband table
coefficients, not calibrated RF power in dBm.

`HT_PROBE_CHALLENGE` now supplies a saved challenge to the paired script.
It rejects malformed/uppercase values and MCS, length or explicit-TID
mismatches before hardware access. Five negative configurations and real
matched replays were checked. Random generation remains the default.

### Rejected authentication evidence and radio control

The PSM bad-FCS path now records authentication-sized candidates matching
either bound address before rearm. The independently bounded record holds
the complete six-byte PLCP and 34-byte MPDU, status and sampled TSF. Native
tests execute the emitted instructions and cover both address alternatives,
every address word, disabled mode, length/subtype rejection, retry-bit
acceptance, raw/metadata copies, no ACK/delivery and retained EIFS deferral.

The record uses SHM `0x750..0x76b` and SRAM `0x204b0`, with compile-time
overlap checks. It is copied after reception stops. Badge detail offset 600
is reused only when the authentication-response length at 596 is zero.
Successful authentication evidence takes precedence; the collector reports
the record's identity and independent FCS result separately.

In `d2-finish-auth-ch6`, the independent monitor saw one successful AP
reply while the active station saw zero own-RA candidates and no matching
bad-FCS authentication candidate. The diagnostic is not an exhaustive loss
detector: corrupt headers, wrong lengths or earlier PHY/capture failure can
evade its match. This trial does not identify which occurred.

`d2-finish-idle-inhibit` temporarily reapplied known TX-off guards after
explicit TX, releasing them at the next request. Authentication still
failed, but the monitor saw no reply, so this was not a useful positive-reply
control. That branch was removed; it would interfere with autonomous
responses if kept. `d2-finish-auth-bb16` also failed authentication without
an independently observed reply; reduced request amplitude did not establish
recovery. Neither negative control proves AP silence.

### Final verification and restoration

Default/gated controller and badge sanitizer suites and freestanding builds
passed. Final legacy OFDM6/cycle regressions each delivered 16 frames per
direction with 16 attempts per endpoint, zero corruption/order/controller
errors. A first gated-load attempt ran before badge A had re-entered
BOOTSEL; its failure log is retained and is not an RF failure. Retrying
after readiness completed the gated regression.

`d2-finish-vendor` successfully authenticated/associated with an unmodified
vendor AP in one attempt per phase. The AP independently confirmed the
correct station. The collector correctly reported no overlaid rejected-frame
record, preserving successful authentication evidence.

Fresh verified full 16-MiB backups are
`d2-finish-start-{A,B}-full.bin`. Both original flashes were restored after
testing; independent full readbacks `d2-finish-restored-{A,B}-full.bin`
compare byte-for-byte. Both badges remain in BOOTSEL.

Receive reliability, negotiated policy/protection and mandatory
Block ACK/A-MPDU/reordering work remain open; none is satisfied by these
diagnostics or controls.

## Follow-up: receive-path and turnaround controls

This investigation retains the missing-reply failure but has **not identified
a causal firmware repair**. Temporary PLCP-selection, PHY-reset, TX-inhibit,
automatic-control and PA-field experiments were removed. No new radio
register setting is retained as a fix.

Evidence prefixes below remain relative to `build/clean-rx/badge/`.
The unchanged gated baseline is `8112616b`.

### Baseline and rate controls

| Prefix | Observation |
| --- | --- |
| `rx-cause-baseline-ofdm` | 16 exact deliveries per direction with the OFDM6/cycle fixture |
| `rx-cause-baseline-t{0,4}-{ab,ba}` | All four MCS7 exchanges passed with the exact earlier amplitude-test challenges and returned ACKs |
| `rx-cause-baseline-1m` | Short 1-Mbps soak failed: initiator exhausted eight attempts, responder delivered no fixture |
| `rx-cause-baseline-2m` | Eight exact deliveries per direction at 2 Mbps |
| `rx-cause-cck-control` / `rx-cause-cck-repeat` | Subsequent short 1-Mbps trials delivered eight and sixteen frames per direction |

The later 1-Mbps trials used a temporary PLCP-selected rejection snapshot,
not a PHY change or relaxed receive validation. They used different random
soak payloads and boots. They do not establish that the diagnostic repaired
anything, nor that the initial failure was exclusively RX. In particular,
neither rate alone nor a deterministic failure of all 1-Mbps reception
explains these results.

`rx-cause-ap-base` used the raw `--ap-auth` role, not the station state
machine: one request, no observed reply. The actual station trial
`rx-cause-ap-sta` used `--ap-assoc`: four requests, one independently observed
successful AP authentication reply, and no own-address candidate at the
active station. Independent decoding verifies the reply's destination,
open-authentication sequence 2/status 0 and CRC32. Its delivered metadata
reports the 1-Mbps CCK SIGNAL value. This differs from the successful
vendor-AP OFDM control, but is not by itself a causal rate diagnosis.

### Rejected-PLCP and turnaround controls

- `rx-cause-plcp` temporarily selected rejected 1-Mbps, 272-us-PLCP
  candidates without relying on the MAC header or an exact 40-byte count.
  No candidate was saved, but the monitor also observed no AP reply. This
  is not a positive-reply control and does not exclude earlier RX aborts.
- `rx-cause-cck-control` and `rx-cause-cck-repeat` selected the 480-us PLCP
  of the short soak fixture, with a temporary SRAM-report copy at detail
  offset 720. No matching rejected candidate was recorded in the passing
  trials. This diagnostic was removed along with the report copy.
- `rx-cause-recovery` conditionally applied the existing PHY reset sequence
  after explicit TX when sampled PHY/IFS state was idle. The monitor saw
  two AP replies, while the station had no own-address candidate. The saved
  reply independently validates destination, authentication status and FCS.
  The reset was not a reliable fix; individual guard execution was not
  separately instrumented.
- `rx-cause-inhibit-1` reapplied explicit TX-off guards after TX and
  released them before the next request. The station recovered one valid,
  successful authentication reply, then failed all four association
  requests. Its saved authentication frame independently validates the
  station destination, AP transmitter, sequence/status and CRC32.
  The monitor saw no reply. This isolated success is not an A/B proof of
  the cause. In particular, retaining forced inhibition would prevent
  dependable autonomous ACK transmission.
- `rx-cause-inhibit-2` again failed authentication. The monitor collector
  could not find its badge because its reboot raced BOOTSEL readiness.
  Its empty report is an orchestration failure, not evidence of AP silence.
- `rx-cause-auto-t0-ba` started automatic PHY control from the complete
  explicit-off state. Local TX counters advanced, but no fresh HT reception
  or returned ACK was established.
- `rx-cause-phy-auto-t0-ba` cleared only the two PHY TX data bits before
  releasing their overrides, retaining radio power setup. It still
  produced a full-length corrupt HT capture. The corresponding local-AP
  trial saw four requests but no independently observed reply.
- `rx-cause-pa32-t{0,4}-{ab,ba}` changed only the existing TX `0x4fb`
  field masked by `0x7f00` to `0x2000`. All four matched HT exchanges passed,
  just as on the unchanged baseline in this session. The local-AP trial
  observed four requests but no reply. This does not demonstrate an
  improvement or establish that field's RF meaning on the unidentified
  Wi-Fi silicon revision; the setting was removed.

The successful forced-off authentication is a turnaround lead, not a
justified production workaround. Intermittent results across unchanged
baseline boots also leave RF placement, interference and acquisition state
unresolved. A controlled placement comparison is needed before attributing
these differences to firmware or selecting another RF setting.

### Control cleanup before the live-RX repair

After removing every temporary control, controller and badge native suites
and freestanding builds passed. That pre-repair controller binary is byte-identical
to the preserved gated baseline. The badge binary differs only in two build-ID
words: its build hash includes the controller binary filename, which differs
for the isolated verification directory. Replacing only those two build-ID
words yields byte-identical badge binaries.

Fresh, verified full-flash backups for this investigation are
`rx-cause-start-{A,B}-full.bin`. Restoration logs and independent full
readbacks use `rx-cause-restore-{A,B}.log` and
`rx-cause-restored-{A,B}-full.bin`.
Both original 16-MiB flashes were restored and independently compared
byte-for-byte; both badges remain in BOOTSEL.

## Live-RX control repair

### Identified source defect and implementation

Live station control handoffs used MACCONTROL `0x84020400`, which clears
MAC receive enable as well as PSM run. In template preparation and policy/
EDCA selection this happened **before** the post-freeze busy check. A
receive racing that check could therefore be disabled before the code
decided to defer. The reliable-TX timeout path also stopped MAC unless a
reply was already completely captured; it did not preserve an in-progress
reply.

The repair distinguishes PSM ownership from receive enable:

- `clean_mac_pause_psm()` clears only `PSM_RUN` (`0x2`), retaining receive
  enable (`0x1`) and every other running control/filter bit.
- Station configuration, policy application, EDCA selection, live template
  preparation and ACK-suppression configuration use that PSM-only pause.
  Existing post-freeze receive/response ownership checks still defer work.
- Station TX no longer takes the redundant outer whole-MAC stop before
  entering template preparation. The template function owns PSM freezing.
- A station ACK timeout leaves receive enabled, including while a reply
  is incomplete. Raw transmission, retuning, raw key setup and explicit
  shutdown keep their existing whole-MAC-stop behavior.

This fixes a concrete receive-control hazard; it does not repair corrupted
PHY bytes or prove that this hazard caused every missing AP reply. PSM-only
freezing is not a guarantee against arbitrary receive-service latency.
No RF register experiment or transmit-amplitude default change is included.

### Physical placement controls

The user reported the original badges side by side about 10 cm apart, moved
them to 60 cm, then returned them to the original placement for a control.
All used unchanged `8112616b`, replaying the original MCS7 challenges:

| Placement / prefix | Observation |
| --- | --- |
| 60 cm, `rx-placement-r{1,2,3}-t{0,4}-{ab,ba}` | Channel 1: 12/12 strict payload plus returned-ACK exchanges |
| 60 cm, `rx-placement-1m` | Short 1-Mbps fixture: 16 deliveries per direction |
| 60 cm, `rx-placement-ch6-t{0,4}-{ab,ba}` | Channel 6: 3/4 strict exchanges; TID0 B-to-A has a full-length, repeat-identical corrupt capture with 11 fixture bit differences |
| 60 cm, `rx-placement-ap-{ab,ba}` | Both active station trials failed authentication; monitors saw no AP reply, and one saw no request either |
| Original 10 cm, `rx-near-r{1,2}-t{0,4}-{ab,ba}` | Channel 1: 8/8 strict exchanges |
| Original 10 cm, `rx-near-ch6-t{0,4}-{ab,ba}` | Channel 6: 4/4 strict exchanges |

These results do not demonstrate spacing alone as the cause, or establish
overload/interference as the mechanism. The channel-6 corruption remains
independent of host transport. Do not turn the passing channel-1 subset
into a general receive-reliability claim.

### Intermediate timeout-only control

`rx-live-timeout` changed only the unconditional stop after ACK timeout.
One of three local-AP trials authenticated and associated successfully, then
received deauthentication reason 15. Both requests had their ACKs recorded,
so the changed timeout branch did **not** explain that successful exchange.
The other two trials still missed replies observed by the monitor. This is
why the final repair addresses the other live-station freeze surfaces too,
and why this intermediate success is not causal proof.

### Final implementation checks and remaining failures

Final default build: `a5352d8d`, directory `rx-live-default/`.
Final gated build: `e255c26c`, directory `rx-live-psm/`.
Both controller and badge native suites and freestanding builds pass.
All four final ELFs have no undefined symbols. New native tests check that
PSM pause preserves receive enable and every non-PSM control bit, including
promiscuous and already-paused/stopped inputs, and is idempotent.

| Prefix | Observation |
| --- | --- |
| `rx-live-default-retry` | Default: 16 exact OFDM6/cycle deliveries per direction with one deliberately suppressed ACK per endpoint |
| `rx-live-psm-retry` | Gated: the same 16-per-direction result, with 17/18 attempts and one suppressed ACK per endpoint |
| `rx-live-final-1m` | Gated: 16 exact short 1-Mbps deliveries per direction |
| `rx-live-final-t{0,4}-{ab,ba}` | Gated: 4/4 matched MCS7 exchanges with independently checked payload bytes, MPDU CRC32, returned ACK CRC32 and ACK destination |
| `rx-live-psm-{1,2,3}` | Local AP: authentication and association status 0 in trials 1 and 3, each in one attempt per phase; trial 2 missed two replies observed by its monitor |
| `rx-live-vendor-retry` | Final default authenticated/associated with the unmodified vendor AP in one attempt per phase; the AP independently confirmed association |

The two local-AP association-only successes later received deauthentication
reason 15 (four-way-handshake timeout). This role does not run WPA2, so its
final `associated:false` does not mean the association reply was missing.
Saved authentication and association frames in trials 1 and 3 independently
validate their destination, successful status and CRC32. They do not prove
WPA2 connectivity. Trial 2 still shows zero own-address candidates and zero
matching rejected-authentication snapshots despite independently monitored
replies.

A long 1-Mbps, 1,500-byte-MSDU retry stress remains failed on **both** old
and repaired firmware. `rx-live-psm-cck-retry` delivered one fixture per
direction, then exhausted retries. Replaying the same challenge on the old
build (`rx-old-cck-retry`) delivered zero at the initiator and one at the
responder. Neither meets its 16-frame target; this is not a long-CCK repair
or reliable A/B attribution.

The first final vendor-AP trial (`rx-live-vendor`) missed the helper's
bounded independent reporting window: the station associated but the AP
helper did not retain confirmation. The retry started both sides within
that window and confirmed both endpoints. The helper exited; none was
left running.

### Final hardware state

After all placement and repaired-firmware tests, the original backups
`rx-cause-start-{A,B}-full.bin` were restored. Independent full 16-MiB
readbacks `rx-live-restored-{A,B}-full.bin` compare byte-for-byte; logs
are `rx-live-restore-{A,B}.log`. Both badges remain in BOOTSEL.

The live-RX control repair is implemented and regression checked. The exact
cause of the remaining upstream HT corruption and intermittent missing AP
replies is still unidentified; D2 receive qualification remains incomplete.

## Follow-up: CCK turnaround repair and synchronized AP trials

### Reproduced CCK defect

The station TX function waited one complete calculated airtime **after**
its MAC-drain polling loops. Drain already overlaps the PPDU. At 1 Mbps
with a 1,500-byte MSDU, the resulting controller TX interval was about
25,082..25,088 us instead of roughly 12,671 us. While the controller was
inside that redundant wait, completed data could remain in the single PSM
capture rather than returning promptly to the existing staging path.
ACK consumption alone was serviced in those loops.

`rf-repair-baseline-1m-*` reproduced failure with one deliberately suppressed
ACK at each endpoint: one received application frame each, retry exhaustion,
and a TX-completion controller fault at the initiator. The corresponding
`rf-repair-baseline-1m-no-drop-*` control delivered 16 frames each way.
The dropped-ACK challenge was then held constant across interleaved old/new
builds, not regenerated to obtain passing payloads.

The retained change is deliberately PHY-specific:

- For hardware-timed CCK, after the PSM has observed hardware TX start,
  use its latched start for the existing full-airtime CPU guard.
- If start remains pending, keep the previous conservative post-drain guard.
  OFDM and HT retain that guard as well.
- RX/PSM stay live, existing ACK deadlines remain, and all capture FCS,
  peer, duplicate, retry and payload checks remain mandatory.
- No PHY/radio register, amplitude, receive-size bound or success predicate
  is changed. This is not independent physical-tail/SIFS qualification.

The first broader experiment shortened all station PHY guards. Its MCS7
controls failed and were subsequently successful on an old-build replay.
That broader change was **not retained**. Later interleaved controls also
reproduced MCS7 failure on both builds, so those first samples cannot
establish a unique HT cause. They do justify keeping the correction
restricted to the causally tested CCK case.

### Matched RF evidence

All evidence prefixes in this section are relative to
`build/clean-rx/badge/`.

| Prefix | Result |
| --- | --- |
| `rf-guard-ab-{1,2,3}-old-*` | Original `326a4152`: all three same-challenge trials fail; initiator receives zero, responder one, both exhaust eight attempts |
| `rf-guard-ab-{1,2,3}-new-*` | Initial guard experiment: all three deliver/ACK 16 frames per direction with one suppressed ACK each, zero corruption/duplicate delivery |
| `rf-scoped-cck-{1,2,3}-*` | Final CCK-only `14ac6112`: all three pass the same 16-per-direction test, including reversed badge roles in trial 3 |
| `rf-default-cck-*` | Final default `6f3c8c23`: same challenge, 16 exact ACKed deliveries per direction, 18/17 attempts, zero corruption/duplicate delivery |
| `rf-default-ofdm-*` | Final default OFDM6/cycle: 16 exact ACKed deliveries per direction, 17 attempts each, one suppressed ACK each, zero corruption/duplicate delivery |

Final scoped CCK TX intervals are 12,671..12,672 us; default intervals
are 12,675 us. The automated comparison requires intervals below 15,000 us
as well as exact delivery/ACK counts and zero corruption/duplicate delivery.
Final scoped sampled maximum capture ages are 1,230..1,231 us, rather than
the initial failed trial's 12,361/10,750 us. These are sampled software
observations, not RF timing guarantees.

`MAC_SOAK_CHALLENGE` now makes controlled fixture replay reproducible.
Unset still means a new random challenge. Empty, uppercase, non-hexadecimal
and wrong-length values were checked to fail before hardware access.

### AP-window investigation and runner repair

The unmodified vendor AP application has an eight-second join deadline after
configuration while it is not connected. Its host helper independently
collects a bounded series of observations. Starting AP and station in separate
interactive tool calls spends part of the AP window before the station's
own cold initialization and discovery even begin.

`rf-repair-baseline-*` prebooted both USB applications and started station
collection immediately on AP readiness, using unchanged `326a4152`.
Authentication, association and WPA2 all passed in one attempt per phase,
with one independently confirmed protected exchange. AP readiness occurred
at 4 s, station completion at 11 s, and AP observation completion at 15 s.

The controlled `rf-repair-late-*` trial inserted eight seconds **after**
AP readiness: station collection began at 12 s, discovery/association failed,
and neither endpoint authorized. This establishes a real orchestration
failure mode; the earlier `d2-amsdu-wpa2-*` trials did not record phase times,
so their individual causes cannot retrospectively be assigned to that delay.

The clean-owned `wpa2_trial.sh` now boots both USB applications before
starting the AP session and invokes the matching collector immediately on
readiness. It records phase times, checks independent peer identity and both
exit statuses, retains failed reports, and removes generated private
credentials. It does not change the vendor application or host networking.

Final `14ac6112` trials `rf-scoped-wpa2-{1,2,3}-*` all pass:
one authentication/association attempt, negotiated WMM, applied EDCA,
one M1/M2/M3/M4 exchange and one protected echo. Each independent vendor AP
reports authorization, one protected RX/TX and 49 stable observations.
The `rf-scoped-wpa2-wrong-*` negative control associates and sends M2, but
both endpoints reject authorization and report zero protected traffic.
Both expected failing helper exit statuses are retained.

### Remaining PHY failures and validation limits

- `rf-final-large-*` retains the separate maximum-size 1-Mbps failure on an
  intermediate build with the same CCK guard: 2,304-byte MSDU, PLCP duration
  18,656 us, receive count stops at 1,590 bytes and no complete fixture is
  delivered. Shortening redundant CPU waiting does not fix this PHY/RXE loss.
- `rf-scoped-ht-m0-ab-*` passes with an exact frame and returned ACK.
  `rf-scoped-ht-m0-ba-*` and both final MCS7 controls fail.
- The follow-up `rf-ht-ab-{1,2}-{old,new}-*` interleaves the original and final
  scoped builds with identical challenges: MCS0 B-to-A passes both rounds
  on both builds; MCS7 A-to-B fails both rounds on both builds.
  Earlier `rf-old-ht-m7-{ab,ba}-*` replays passed. This retains the
  boot-to-boot HT variability rather than selecting only successful runs.

The CCK retry/turnaround defect is repaired and RF regression checked.
General HT receive reliability, maximum-size 1-Mbps reception and protected
aggregate OTA qualification remain unresolved. No all-rate RF qualification
or complete D2 claim follows.

Final default/WMM controller and badge native suites and freestanding builds
pass, including shell syntax for the new runner. All four final ELFs have
no undefined symbols. Build logs are `rf-final-{default,gated}-build.log`.

### Flash preservation

Before hardware writes, fresh complete 16-MiB backups were saved and verified
as `rf-repair-start-{A,B}-full.bin`. After the investigation, both originals
were restored with verification, then independently read back in full as
`rf-repair-restored-{A,B}-full.bin`. Both readbacks compare byte-for-byte
equal to their respective original backups. Restore and readback logs are
`rf-repair-restore-{A,B}.log` and `rf-repair-readback-{A,B}.log`.

Both badges remain in BOOTSEL, with their original flash contents restored.
Generated private AP credentials were removed; no AP helper remains running.
