# AP authentication and bounded RX staging investigation

## Scope and conclusion

This D2 increment investigates authentication replies missing from an active
station, then reduces receive-service coupling to host backpressure. It does
**not** establish the exact PHY/capture cause or fix the missing replies.
HT/WMM advertisement remains disabled, explicit legacy fallback remains, and
D2 is incomplete.

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
