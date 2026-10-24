# R08 retry and pause measurements - 2026-10-24

## Outcome and limits

**Measurement implementation complete; hardware acceptance remains partial.**
The authorized campaign completed 120/120 fresh raw exchanges, then one
passing 30-second network control. The second network pair failed during
source-controller startup, before station networking initialized. The harness
stopped, did not repeat it or start either badge-B network pair, and restored
all three complete original flashes with independent readbacks.

The original `host_stalls` were deliberate two-second **desktop collector
pauses**, not detected firmware stalls. They cannot be counted as device
starvation or physical RF losses.

Pre-agreed scope was channel 6 only: 20 exchanges on each of six directed
links, then four 30-second badge/Pico-AP network pairs, one pause-off and one
pause-on per badge. Existing bounds were retained: six raw exchange attempts,
four network application enqueue attempts and a two-second maximum network
service interval. No new RF success-rate target, retry policy or repeat
allowance was introduced. Infrastructure/evidence failure required stopping.

This record follows the [original regression](HARDWARE_REGRESSION_2026-10-24.md)
and [R08 work item](todo.md). Measurements and offline commands are maintained
in the [host-tool guide](../guides/HOST_TOOLS.md#retry-latency-and-host-pause-measurements).

Devices: badge A `08DA26F86E57F5EA`, badge B `9BBF88DD17D66E7D`,
Pico 2 W `6572BE3E4636AF51`.

## Implementation

- Raw exchange collectors retain both complete 512-byte traces plus the
  source's first-transmit, completion and round-trip timing. The independent
  analyzer reconstructs complete nonce/identity/sequence-bound request and
  reply frames, including FCS, and checks raw timing against printed JSON.
- The fixed network report preserves its original 152-byte prefix and adds
  72 bytes of attempt counts, 64-bit latency sum, four latency bins, service
  gap/MAC timeout counts and a bounded last-four-event ring. Engine storage
  is 13,940 bytes, without a heap. No controller firmware dependency is added.
- Both desktop collectors measure actual pause intervals; the shared
  `CLEAN_RX_NETWORK_HOST_PAUSES=0|1` control retains the original default
  schedule while allowing a pause-disabled control. Invalid settings fail
  before network submission.
- Event timestamps use local device elapsed time. AP binding establishes its
  time origin, so unbound service gaps are not inserted into the bound
  timeline; the existing whole-run maximum service interval is preserved.
- Historical missing extensions and failed initialization remain explicitly
  unavailable. Truncated rings do not produce fabricated whole-run
  correlations. MAC ACK timeouts, application retries and physical RF loss
  remain distinct observations.

Network latency starts at initial application enqueue and includes queue and
service time. Raw device timing is request/echo round-trip timing, not PHY
turnaround or SIFS. Desktop polling latency is reported separately.

## Reanalysis of the original 330 raw exchanges

Each link completed 55 exchanges. Attempt columns count completion on attempt
1/2/3; attempts 4-6 were zero.

| Link | First attempt | Attempts 1/2/3 | Host latency median/p95/max, ms |
| --- | --- | --- | --- |
| Pico -> A | 49/55 (89.1%) | 49/6/0 | 55/302/336 |
| Pico -> B | 48/55 (87.3%) | 48/7/0 | 54/315/355 |
| A -> Pico | 49/55 (89.1%) | 49/5/1 | 13/363/632 |
| A -> B | 52/55 (94.5%) | 52/3/0 | 16/330/387 |
| B -> Pico | 52/55 (94.5%) | 52/2/1 | 13/323/682 |
| B -> A | 49/55 (89.1%) | 49/6/0 | 15/368/384 |

Thus 299/330 completed first-attempt, 29 on the second and two on the third:
31 retried exchanges and 363 total attempts. All complete frame identities,
fresh challenges and FCS validated independently. Historical device timing
was not retained and is unavailable.

The two original ten-second network pairs retained these observations:

| Pair | Station/AP generated | Station/AP application retries | Station/AP service maximum, us | Station MAC ACK timeouts |
| --- | --- | --- | --- | --- |
| A | 35/29 | 0/0 | 32228/78172 | 0 |
| B | 35/29 | 0/1 | 29469/78174 | 1 |

Both had zero application loss and deliberate desktop pause counts 1/2
(station/AP). Actual historical pause durations and the new attempt
histograms/event timelines were not retained.

## Fresh raw exchanges

All 120 completed with fresh challenges distinct from the original campaign.
The full raw traces independently validated, including all device timing
records. Attempts 3-6 were zero.

| Link | First attempt | Attempts 1/2 | Host median/p95/max, ms | Device RTT median/p95/max, us |
| --- | --- | --- | --- | --- |
| Pico -> A | 20/20 (100%) | 20/0 | 50/78/100 | 6359/6528/7182 |
| Pico -> B | 19/20 (95%) | 19/1 | 50/90/335 | 6346/23137/273280 |
| A -> Pico | 20/20 (100%) | 20/0 | 12/26/48 | 6455/6497/6499 |
| A -> B | 20/20 (100%) | 20/0 | 14/51/83 | 7459/7514/7619 |
| B -> Pico | 20/20 (100%) | 20/0 | 12/13/34 | 6444/6513/6617 |
| B -> A | 18/20 (90%) | 18/2 | 15/331/387 | 7484/308372/309219 |

Overall: 117/120 first-attempt (97.5%), three second-attempt completions,
123 total attempts and no terminal loss. These finite samples satisfy the
existing attempt bound; different sample sizes and uncontrolled RF conditions
do not establish a reliability improvement over the earlier campaign.

## Networking: one passing control and one retained failure

### Pair 1: badge A, desktop pauses disabled

Both endpoint collectors exited 0, and independent raw/JSON/session/hash/
counter verification passed for the complete 30-second run.

| Metric | Source-built station | Vendor Pico AP |
| --- | --- | --- |
| Acknowledged/generated | 85/85 | 93/93 |
| First attempt | 67/85 (78.8%) | 72/93 (77.4%) |
| Acknowledged attempts 1/2/3/4 | 67/15/3/0 | 72/19/2/0 |
| Application retry enqueues / loss | 21 / 0 | 23 / 0 |
| Mean / maximum application RTT, us | 90464.741 / 561170 | 97043.473 / 559856 |
| Latency bins <=10 ms / <=250 ms / <=1 s / >1 s | 22/45/18/0 | 24/48/21/0 |
| Maximum network service interval, us | 31906 | 78922 |
| Bound service gaps >=250 ms | 0 | 0 |
| Actual desktop pauses | 0 | 0 |

All acknowledged requests completed within three application attempts, below
the four-attempt ceiling. Both service maxima were below two seconds. Exact
network percentiles cannot be reconstructed from four bins and are not
invented.

The station separately retained 223 MAC transmit attempts, 46 MAC ACK
timeouts and 127 busy/deferral observations. Vendor AP MAC timeout/retry
telemetry is unavailable. Both last-four-event rings truncated (station 67
events, AP 23), so whole-run event alignment is unavailable.

Application retries and station MAC ACK timeouts occurred without desktop
pauses or any counted >=250 ms bound service gap. This rules out those
measured long service gaps as a necessary explanation for this run's retries;
it does not distinguish lost data from lost ACKs, shorter service delays,
interference or other radio behavior.

### Pair 2: badge A, desktop pauses enabled - failed

The source station retained `CLEAN_CONTROLLER` error 5 at loader stage 5,
address `0x20004`, actual mailbox state `0xff`, after 2,484,966 us. This is the
startup command's wait for state 8 failing with controller fault state 255,
not application retry exhaustion or a measured networking stall.

There were no connections, no initialized network report, no application
traffic and **zero actual station desktop pauses**. Station network metrics
are unavailable, not a successful zero-retry/zero-gap result.

The AP never bound a session and exited failed with error `0x100` after its
existing 45-second outer bound. It injected six measured desktop pauses:
2,003,245; 2,003,810; 2,001,359; 2,005,024; 2,005,027; 2,000,103 us.
Its whole-run service maximum was 78,794 us, but it had no active peer traffic.
This is not a valid pause-on networking comparison.

Both collectors exited 1; the strict analyzer rejected the raw station
controller error. The source's deeper controller failure detail is not
established by this report. No repair, automatic retry or replacement passing
trial was attempted. Both badge-B network pairs were left **not run**.

## Conclusions and remaining acceptance

1. The 31 historical retried exchanges are now accounted for by link, attempt
   count and host latency; fresh raw exchanges additionally retain device RTT.
2. Desktop pauses and device service intervals are different measurements.
   The passing pause-off pair still retried without >=250 ms service gaps.
3. Startup failure prevented a valid loaded pause-on comparison and completion
   of the four-pair matrix. R08's physical RF-loss correlation remains open:
   there is no independent RF-loss timeline or synchronized host/device clock.
4. Further hardware work needs fresh authorization: investigate the retained
   controller startup failure, complete loaded pause-off/on controls on both
   badges, and obtain calibrated independent loss evidence. Do not tune retry
   policy or infer production reliability from these finite observations.

## Evidence, validation and restoration

Evidence root: `build/r08-20261024/`. It retains the authorization and trial
ledger, immutable `provenance/` images/collectors/source snapshot, raw and JSON
archives, collection exit codes, independent analyses, `audit.json`,
`harness/campaign.cjs` and `harness/audit.cjs`.
Original reanalyses are `original-cross-analysis.json` and
`original-network-{a,b}.json`; the original source evidence remains in
`build/radio-regression-20261024T1346/`.

USB build IDs: raw lab `bef1f9d7`, station `d9ce67a7`, AP `3c084ef2`.
The manifest binds complete SHA-256 hashes; selected flashed provenance:

| Image | SHA-256 |
| --- | --- |
| Pico raw UF2 | `b0135a64dbc258ce686e34fe81bb37cd302db4b2680acb524b73c84b33f046d3` |
| Badge A raw UF2 | `6a90c64733e56d2cf4f1b2625f795f4c651bd3ad5924697c5befab049112ba8a` |
| Badge B raw UF2 | `384952481d5d52aa23f35eaf6b747af7819d975ae588aa1ce171b2e0cd5c64e0` |
| Station UF2 | `1c829acb3c81657964ff7d94b40e9c75ddf6b2c35646aa9e9549d60cdd10f7e9` |
| AP UF2 | `8c6caa7441cd96e9e700b01a642d0d987a1804c8b68bd9a850325bcec2463b1e` |
| Raw controller | `eaa953e27167550a156758f67f16351c96ba0896e39246c6dc21c82c74dbda66` |
| Station controller | `163087d9d9f7ef66ef991a976a50450b7d76471669eff2770f40f438620f5fdc` |

Native radio protocol, badge and peer builds/tests passed. Nine focused R08
tests cover canonical frame evidence, timing tampering, failed denominators,
missing/truncated metrics, real C engine/raw/JSON agreement and desktop pause
controls. C04/C07/C08 boundary tests passed. Compatible HT native/oracle
coverage remains separate from default-profile tests; response-only fixtures
are not submitted to a default-profile native collector.

The campaign has bounded child supervision and exact-serial/protocol/build
BOOTSEL cleanup, including a native-tested emergency stop helper for failed
collections. No further RF work was used for the independent audit.

Before flashing, each full original was saved twice and compared with its
known hash. After stopping, all were restored without execution, read back in
full, byte/hash compared, and independently rechecked in BOOTSEL:

| Device | Bytes | Original/restored SHA-256 |
| --- | --- | --- |
| A | 16777216 | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| B | 16777216 | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |
| Pico | 4194304 | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |

Full backup/readback files remain private. No commit was created.
