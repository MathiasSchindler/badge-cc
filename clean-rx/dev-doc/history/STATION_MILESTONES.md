# Station lifecycle and application milestone history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## B. Sustained station operation

### B1. Connection lifecycle

- Recorded open criterion: Implement a persistent station state machine: discovery, AP selection,
  authentication, association, connected operation, departure and recovery.
- Recorded open criterion: Manage beacon loss, deauthentication/disassociation, AP restart,
  channel changes and cancellation without stale state or leaked resources.
- Recorded open criterion: Verify 10 consecutive cold connection attempts and five AP-loss/recovery
  cycles. Require fresh valid connection evidence and working data each time.

### B2. Continuous application data

**Current implementation (2026-10-23):** mode 33 now provides the bounded
legacy/open Ethernet/ARP/IPv4/UDP path documented in
[the badge README](../../badge/README.md#b2-continuous-static-address-networking).
It uses four TX/four RX slots, explicit backpressure/overflow, static
`192.0.2.1`/`.2` addresses and port `30526`. Both endpoints originate
fresh session/epoch/sequence-bound requests, alternating 90-byte and
1514-byte Ethernet packets. Four bounded application attempts preserve
loss, duplicate and abandonment evidence. Five-second idle intervals and
two-second host USB stalls do not stop firmware servicing.

Native ASan/UBSan tests pass a simulated bidirectional hour, including
full-sized requests, idle and independent counts/hashes; additional tests
cover timer wrap, full queues and resumed consumers, checksum/payload
corruption, validation-before-binding, reconnect and stale epochs,
retry exhaustion and FCS-valid wrong-address/LLC/flag MPDUs.
This is not real-time RF qualification.

The protected `build/clean-rx/b2-20261023/hardware/trial02-*` control passed
30 actual seconds on channel 1 against the unchanged vendor-radio AP.
Station build `22f389ff` independently delivered/acknowledged 108 requests
(54 full-sized); AP build `8595958f` delivered/acknowledged 100 (50 full-sized).
All paired counts and payload hashes matched. Loss, corruption, RX overflow
and abandonment were zero; retries were 6/8, duplicate deliveries were zero,
and queue high-water marks were TX 2/1 and RX 1/1. Both experienced one
scheduled idle interval and actual host stalls (station 3, AP 5).
Maximum application round trips were 318,242/473,253 microseconds, including
retries; maximum network service gaps were 29,978/98,183 microseconds.
Both original full flash images were restored, read back and compared.

An earlier small-packet control (`trial01-*`, builds `d3b17bf1`/`72bf7e6d`)
passed 109/104 messages but is not full-sized qualification. Its subsequent
long run (`soak01-*`) was deliberately cancelled after noticing the detailed
acceptance also requires full-sized packets. Both collectors stopped,
requested BOOTSEL and both original images were fully restored/readback-verified.
It is not a failed RF soak or a completed soak and must not be counted as either.
**Real 60-minute result: failed, acceptance remains open.** The retained
`b2-20261023/hardware/soak02-*` run used station `22f389ff` and AP `24957f3d`.
Both raw network reports match their JSON archives. Station generated 13,214,
acknowledged 13,211 and lost 3; AP generated 13,259, acknowledged 13,235,
lost 21 and abandoned 3 work items during epoch changes. Station/AP delivered
13,239/13,211 peer requests; full-sized generated counts were 6606/6629,
with 6604/6616 acknowledged. Sender/receiver application counts/hashes therefore
do not meet the strict delivery oracle. Do not count partial delivery as a pass.

There were zero corrupt packets, RX overflows or TX-full events. Both queues
remained bounded (TX high-water 2, RX 1), all 180 idle intervals occurred,
and actual two-second host stalls numbered 287/500. Application retry counts
were 510/542; maximum acknowledged round trips were 823,257/786,114 us,
and maximum service gaps were 41,835/97,911 us. Station lifecycle elapsed was
3,600,000,033 us; AP network elapsed was 3,601,999,957 us. This was a real
full-duration test, not simulated time or a short control.

The lifecycle journal identifies five **beacon-loss** recoveries, not departure,
channel-change, authentication or association failures. Six associations
produced only five data-verified epochs. The retained late-run journal shows
repeated beacon-loss/reset/reassociation around minutes 54-55. Local MAC
diagnostics retain 1088 TX timeouts and 9848 BUSY returns. No fatal loader/radio
error occurred, and the RP2350 kept servicing the network in under 42 ms.
Thus host queue growth or two-second USB collector stalls did not cause a
host-service lockup. The archives do not establish whether the beacon gaps
were physical RF outages or missed controller receptions: do not label them
false beacon losses without additional evidence.

Application continuity across B1 cold reloads also remains unqualified:
reloads take more than 2.5 seconds, while the current four-attempt application
window is about one second, and changing epochs explicitly accounts for
abandoned work rather than silently accepting old replies. Next instrument
beacon gaps against fresh validated application RX and preserve pending
application ownership across recovery before rerunning the strict hour.
Raising thresholds or dropping loss/abandonment checks is not a qualification fix.

After every campaign both original 16-MiB images were restored, fully read
back and compared. Final SHA-256 values remain
`02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`
(station `08DA26F86E57F5EA`) and
`2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3`
(AP `9BBF88DD17D66E7D`). Both returned to BOOTSEL; the Pico was untouched.
Default, WMM/HT-response and HT/SHM builds/native regressions passed.

- Recorded completed criterion: Provide an actual Ethernet/MSDU send/receive interface with bounded
  queues, backpressure, meaningful errors and continuous USB/host servicing.
- Recorded completed criterion: Exercise it with a small ARP/IP/UDP test path on the RP2350, using DHCP
  or explicitly documented static addressing. An IP stack is a host-software
  task, not a prerequisite for raw MAC operation.
- Recorded open criterion: Complete a 60-minute bidirectional traffic soak, including idle periods,
  full-sized frames and host stalls. Require no crash, watchdog reset,
  memory corruption, stuck credit, unrecovered disconnect or unbounded queue.
  Report delivery, loss, retries, latency and memory/stack bounds.

### B3. Reproducible station qualification

**2026-10-23 implementation and open-configuration qualification:** the
Pico 2 W (`6572BE3E4636AF51`) now runs the unchanged vendor radio firmware
as an independent AP. Mode 5 configures channel 1/nmode 0, channel 6/nmode 0
and channel 11/nmode 1, all open. A callback adapter sets nmode while DOWN
and replaces the shared AP helper's channel-1 command without changing shared
or vendor code. Hardware/target channel, nmode and wsec are queried before
readiness. Native tests exercise the actual shared AP configuration through
that adapter, including mode-setting failures and wrong/short readbacks.

The Pico variant (`HT_PEER_PICO=1`) initializes reference and 120-MHz SYS
clocks before USB, reusing the existing Pico clock helper unchanged. The
first generic badge-peer attempt (`control01-*`) did not enumerate on the
Pico; this is not an RF failure. After the clock fix, channel 1 and 11 controls
passed (`control02-*`), but a between-trial BOOTSEL re-enumeration race skipped
channel 6. Bounded, logged reboot retries fixed that host-runner race.

The first three-round campaign (`qualified01-*`) completed five verified
sessions before all three USB boards disappeared, including the untouched
second badge. Restoration initially failed because the hub was unavailable.
After the user reconnected it, both original images were restored, fully
read back and compared before any further flashing. Preserve the interrupted
matrix and the initial restoration failures; it is not a qualified campaign.

The fresh `build/clean-rx/b3-20261023/hardware/qualified02-*` campaign passed
all nine cold connections: channel-1 echoes 14/18/15, channel-6 echoes 20/20/20
and channel-11 echoes 18/18/19, **162 total**. Clean badge build `80e8c0bd`
ran on `08DA26F86E57F5EA`; Pico AP build `eebb2313` independently confirmed
the same station, fresh session/epoch and application data in each trial.
Readback-verified configurations and both raw endpoint archives passed the
paired oracle. Authentication/association failures and disconnects were zero;
six MAC timeouts, four echo timeouts and seven BUSY returns were retained.

The matrix reports `open_configurations_qualified:true`,
`ap_implementations:1`, **`b3_qualified:false`**. These are three configurations
of one AP implementation; do not claim three AP stacks, sustained WPA2,
separate mesh-node coverage, negotiated HT, roaming or B2 soak qualification.
Existing bounded WPA2 controls do not replace E's persistence/rekey acceptance.
See [commands and boundaries](../../badge/README.md#b3-reproducible-ap-configuration-qualification).

Final full readbacks matched the original SHA-256 values:
badge `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`,
Pico `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486`.
Both were restored and returned to BOOTSEL; the second badge was not flashed.

- Recorded completed criterion: Repeat connection/data tests against the controlled vendor AP and at
  least two additional AP implementations/configurations available for testing.
- Recorded open criterion: Verify open operation and, after E, sustained WPA2 operation. Include
  separate nodes of the local mesh without claiming roaming support merely
  because association with individual nodes works.


