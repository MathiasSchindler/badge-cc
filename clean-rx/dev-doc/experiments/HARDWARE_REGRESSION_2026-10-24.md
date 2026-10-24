# Badge and Pico 2 W RX/TX regression, 2026-10-24

## Scope and verdict

The user authorized a one-time campaign on the two attached Universe 2026
badges and Pico 2 W, including experimental aggregate/Block ACK diagnostics.
All three started and finished in BOOTSEL. Complete original flashes were
saved, verified, restored without execution and independently read back.

**Ordinary cross-board RX/TX works in the tested scope.** All 330 raw 6 Mbps
OFDM request/echo exchanges passed. Both badges also passed the bounded
station, networking and WPA2/rekey tests below. HT tests required three
fresh repeats after missing receptions.

**Experimental aggregate acceptance does not pass.** Forty-seven of 48
receiver runs exhausted the host's six-second operation budget; the other
reported a controller fault. Some senders independently received valid
compressed BA frames, but those do not repair the failed receiver reports.
This is not an all-capabilities, lossless-link or vendor-parity qualification.
No firmware or host implementation was changed during this campaign.

## Source, devices and images

Source revision: `752540bd9201937872e18f5c1148e3725b22865d`, initially clean.
Compiler: Homebrew Clang 22.1.8; Node v26.7.0. Existing LLVM, Make, libusb
and `build/picotool-usb/picotool` were used without installing dependencies.

| Device | USB serial | RP2350 | Full flash |
| --- | --- | --- | --- |
| Pico 2 W | `6572BE3E4636AF51` | A2/QFN60 | 4,194,304 bytes |
| Universe 2026 badge A | `08DA26F86E57F5EA` | A4/QFN80 | 16,777,216 bytes |
| Universe 2026 badge B | `9BBF88DD17D66E7D` | A4/QFN80 | 16,777,216 bytes |

Board bindings match the [radio lab](../../../pico2w/radio/README.md).
The headless Pico and display-capable badge images were not interchanged.

| Selection / application | Host build ID | Controller selection |
| --- | --- | --- |
| Three-node radio lab | `3cf7f83c` | `PROFILE=runtime`, `SELECTION=default` |
| Badge HT response/data | `a8932651` | `PROFILE=diagnostic`, `SELECTION=ht-response` |
| Fixed HT probe | `3be40f9c` | HT response selection plus `CLEAN_RX_HT_PROBE=1` |
| Badge station/security | `ccd94093` | `PROFILE=diagnostic`, `SELECTION=default` |
| Aggregate SHM | `e434b689` | Diagnostic HT data plus probe/interface-3/handoff-2/SHM |
| Aggregate BA | `6a06e127` | SHM plus `CLEAN_RX_AMPDU_BA=1` |
| BA bad-middle | `6efcc15c` | BA plus `CLEAN_RX_AMPDU_BAD_FCS=2` |
| BA all-bad | `fc12d00b` | BA plus `CLEAN_RX_AMPDU_BAD_FCS=7` |
| Aggregate DMA | `bf17d913` | Diagnostic HT data plus probe/interface-3/handoff-2/DMA |
| Pico independent AP peer | `a8b7510a` | `HT_PEER_PICO=1`, vendor radio executable |

The three-node phase runs the source-built controller on **every** board.
The later station/security phase deliberately uses the Pico's independent
vendor-radio AP fixture. Its results establish badge station behavior,
not source-built Pico WPA2/AP support.

Each controller, badge host and desktop collector was rebuilt with matching
flags in isolated directories. The radio protocol tests, controller native
suites for each diagnostic selection, default/HT-response badge suites, and independent
peer native suites passed. Freestanding builds checked unresolved symbols.

## Method and retained evidence

Local evidence root: `build/radio-regression-20261024T1346/`.
It is ignored build output, not a repository dependency.

- `manifest.json`: source revision, board bindings, tool/build IDs and SHA-256
  hashes of all selected controller/host images and original flash backups.
- `build-logs/`: exact compiler flags and native/build results.
- `cross-board/exchanges-r2.jsonl`: all fresh challenges, complete independently
  received request/reply records, attempt counts and initial/final counters.
- `legacy/`, `ht/`, `ht-resume/`, `aggregate/`, `controls/`, `probe-enabled/`:
  per-cell configuration, raw endpoint reports, JSON, stderr and runner logs.
- `station/`: both endpoint archives, strict pair-verifier results and timing.
- `*-results.tsv`: every completed or explicitly interrupted diagnostic cell.
- `backups/`, `restoration/`: full original images, independent restored
  readbacks and verification/BOOTSEL logs; directories are private.

Cross-board collection used the documented radio collector with 55 rounds.
Badge fixtures used the existing `ht_probe.sh` with matching isolated
`CLEAN_RX_COLLECTOR`, explicit serials and `HT_PROBE_CHANNEL=6`; channel-1
aggregate controls used the same images. Station/security collection used
`lifecycle_probe.sh` and `wpa2_trial.sh` with the matching Pico peer collector.
Only freshly generated private fixture credentials were used; their temporary
directories were removed. No existing network credentials were supplied.

Every radio challenge was fresh. TX completion alone was not a pass:
receivers had to supply the expected bytes/FCS/metadata, and applicable
ACK/BA tests required an actual independently received return frame.
Failed trials remain separate from their fresh repeats.

## Cross-board and legacy results

All six directed links between the three boards passed **55/55** fresh
request/echo exchanges, **330/330** total, on channel 6 at raw 6 Mbps OFDM.
All 330 payload challenges were distinct. Thirty-one exchanges needed more
than one attempt: 363 request attempts total, maximum three per exchange.
This is successful bounded retry, not zero-loss reception.

Each board finished with 110 confirmed source exchanges, one radio boot,
zero terminal failures, zero resynchronizations and zero reported faults.
Duplicate requests were retained in the counters and handled by the protocol.
No throughput or simultaneous-transmitter qualification was attempted.

The separate 11 Mbps CCK fixture passed **3/3 in each badge direction**.
All six receiver archives also passed independent `--inspect-peer` checks.

## Badge HT results

Here `A -> B` and `B -> A` refer to the badge identities above.
Payload descriptor version 1 is distinct from the separately gated fixed
HT probe. These are raw/diagnostic interfaces, not negotiated HT station mode.

| Fixture | Initial result | Fresh repeat |
| --- | --- | --- |
| 88-byte HT Data, MCS0..7, both directions | 16/16 | Not needed |
| 2,332-byte HT Data, MCS0/7, both directions | 4/4 | Not needed |
| 28-byte HT Data, MCS0/7, both directions | 3/4; A -> B MCS0 had no fresh RX | 1/1 passed |
| 3,869-byte QoS A-MSDU, MCS0/7, both directions | 3/4; B -> A MCS0 had no fresh RX | 1/1 passed |
| Clocked normal ACK, MCS0/7, both directions | 3/4; B -> A MCS7 had neither fresh HT RX nor returned ACK | 1/1 passed |
| Explicitly enabled fixed HT probe, MCS0/7, both directions | 4/4 | Not needed |

The three missing-reception trials had local TX submission and no reported
controller fault; they are not retroactively counted as initial successes.
Sampled cells eventually passed, but this does not establish reliability.

Four preliminary fixed-probe attempts used the HT-response selection without
its separate `CLEAN_RX_HT_PROBE=1` flag. All correctly rejected command 11
with firmware error `0x85` and no TX submission. They are retained as disabled
gate controls, not OTA reception failures. The enabled selection was then
built and tested independently.

## Station and security results

Each badge was tested separately against the Pico vendor-radio fixture.
All **12/12** paired runner/verifier results passed:

| Test, once per badge | Badge A | Badge B |
| --- | --- | --- |
| Open lifecycle, one actual AP outage/reload | Two data-verified connections, 44 echoes | Two data-verified connections, 46 echoes |
| Static-address networking, 10 seconds | 35 station / 29 AP messages | 35 station / 29 AP messages |
| WPA2 passphrase, channel 6 | Authorized, protected echo | Authorized, protected echo |
| WPA2 raw PSK, channel 6 | Authorized, protected echo | Authorized, protected echo |
| Wrong-password control, channel 6 | No M3/M4, authorization or protected echo | No M3/M4, authorization or protected echo |
| Protected rekey, channel 6, at least 35 seconds | 337 echoes; three PTK and three GTK rotations, all traffic-proven | 337 echoes; three PTK and three GTK rotations, all traffic-proven |

Networking also validated large messages: 17 station-originated and 14
AP-originated messages per badge. One AP retry occurred with badge B.
Each run retained one station and two AP host-stall events. The verifier's
`soak_qualified` remains false for these ten-second runs.

The rekey runs received 65/6 and 67/9 broadcast/multicast frames respectively.
These finite results do not claim lossless multicast, persistent protected
networking, an all-channel matrix, arbitrary AP interoperability or an audit
of the experimental crypto.

## Experimental aggregate and BA failures

The channel-6 matrix exercised all MCS0..7 in both badge directions for SHM
and normal BA, plus MCS0/7 in both directions for DMA, bad-middle and all-bad.
Fresh channel-1 MCS0 controls repeated SHM and normal BA in both directions.

| Profile | Trials | Receiver result | Independently received BA |
| --- | ---: | --- | --- |
| SHM | 18 | All host timeout `2`, stage 7 | Not armed |
| Normal BA | 18 | All host timeout `2`, stage 7 | 16 valid bitmap-7 returns |
| Bad-middle BA | 4 | All host timeout `2`, stage 7 | Four valid bitmap-5 returns |
| All-bad BA | 4 | All host timeout `2`, stage 7 | None |
| DMA | 4 | Three host timeouts; one controller fault | Not armed |

**No aggregate trial passed full paired acceptance.** SHM reports retained
three delivered-body counts in all 18 runs, normal BA in 16/18, bad-middle
in 4/4 with two bodies, and DMA in 3/4 with two bodies. Counts alone are
not validated full delivery: timeout prevents the final frozen snapshot,
lease and payload-validation conditions from completing.

The channel-6 non-DMA timeout reports elapsed between 6,000,074 and
6,000,308 microseconds; channel-1 controls also ended just beyond six seconds.
The [loader](../../badge/loader.c) initializes `budget_us` to 6,000,000 and
`clean_service()` returns `CLEAN_TIMEOUT` when that cumulative budget is
exhausted. The [badge flow](../../badge/main.c) gives aggregate RX a
4,200,000-us receive interval after startup but does not extend that budget
as it does for peer RX, lifecycle and rekey modes. This identifies a host
deadline/diagnostic-completion problem, not proof that the radio cannot
transmit or return BA.

After budget exhaustion, the attempted fault snapshot uses the same exhausted
loader. Its absent/invalid capture, chip-ID or terminal-lease fields cannot
be interpreted as authoritative firmware state. All-bad tests therefore do
not qualify completed rejection/lease release merely because no BA arrived.

The DMA A -> B MCS0 trial instead reported controller error `0x8d`,
address `0x4d326`, actual value `86`, at about 4,034,274 us. Its failure is
separate from the budget problem and was not repaired or hidden by repeats.
See the [earlier DMA/SHM and BA records](ampdu/INTERFACE_AND_DELIVERY.md)
for historical evidence; these new failures do not rewrite those results.

No general live A-MPDU, secure reorder/agreement integration, Immediate BA,
physical SIFS timing or HT negotiation is qualified by these diagnostics.

## Harness failures and stopping conditions

The first three-node collector invocation ran before application USB discovery
completed: no radio exchange was attempted. A premature BOOTSEL reboot retry
also failed because the applications were live. Explicit status reads then
found all three radios ready without faults; the retained `r2` run completed
330/330 exchanges without reflashing or a firmware change.

During the reverse HT MCS4 88-byte cell, `picotool reboot` stalled before
starting the transmitter. The receiver timed out with no matching frame.
The process tree was stopped, that cell was recorded as interrupted rather
than an RF failure, and all three devices were found back in BOOTSEL.
The resumed cell passed with a fresh challenge.

Subsequent USB operations used a session-scoped wrapper: info/reboot calls
were limited to 15 seconds, flash/readback calls to 300 seconds, and a timeout
would block later operations pending explicit recovery. No further USB
timeout occurred. This was test-harness instrumentation, not a firmware fix.

## Restoration

`save -a -v` saved and verified each complete original flash before any
installation. At the end, each original binary was loaded and verified
without execution, then separately saved in full and byte-compared.
All three comparisons passed; final `info -a` logs confirm BOOTSEL.

| Device | Matching backup/restored-readback SHA-256 |
| --- | --- |
| Pico | `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486` |
| Badge A | `02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac` |
| Badge B | `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3` |

Current follow-up ownership is in [the issue register](../issues/README.md).
This campaign does not authorize further device access.
