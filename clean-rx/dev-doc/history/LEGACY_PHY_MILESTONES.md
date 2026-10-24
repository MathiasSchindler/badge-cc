# Legacy PHY milestone and boundary history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

## C. Complete 802.11b operation

### C1. All legacy DSSS/CCK rates

- Recorded open criterion: Implement/repair TX and RX configuration for 1 and 2 Mbps DSSS and
  5.5 and 11 Mbps CCK. Retain existing 11 Mbps qualification as a regression.
- Recorded completed criterion: Implement correct PLCP construction/validation, airtime calculations,
  and long/short preamble behavior where permitted by the standard.
- Recorded completed criterion: Test rate/preamble boundary cases and verify actual OTA modulation and
  rate independently, not just requested rate fields or successful TXE status.

**C1 progress: implemented and substantially tested, but not fully closed.**
The first checkbox remains open because full-sized 1-Mbps reception still
fails; small and 1,500-byte exchanges at that rate work. Do not interpret the
rate/preamble results below as full 802.11b qualification.

Implementation is entirely under `clean-rx/`:

- Shared [DSSS/CCK PHY helpers](../../src/phy_rates.h) select all four
  rates, generate/validate complete six-byte PLCP headers, check CRC/SERVICE/
  length extension, and calculate long/short airtime. Short 1 Mbps is rejected.
  TX PHYCTL1 now follows the validated SIGNAL rate, rather than a fixed
  11-Mbps value. Command 10 requests short CCK without repurposing any
  existing command-5 flags.
- Slow CCK ACK waits anchor to the first observed physical TX-active-to-idle
  transition without enlarging the existing 398-us ACK allowance. Completion
  polling accounts for slow full-sized frames; OFDM scheduling is unchanged.
- Receiving rate/preamble evidence is taken from the separate receiving
  chip's PLCP and held PSM PHY header. An intermediate test exposed a stale
  DMA-header association; the final implementation snapshots the status
  belonging to the held capture instead. Both transport fragments preserve
  metadata, and the fixture rejects modulation/rate/preamble mismatches.
- [Native tests](../../badge/loader_test.c) exhaust MPDU lengths 14..2400
  at all four rates, independent fixed CRC vectors, every header-bit
  corruption, all seven legal preambles, illegal preambles, fragmented RX
  metadata and every explicit fixture PHY/payload selection. Controller,
  loader/strict-loader, WPA2/crypto and secret tests pass with sanitizers;
  PSM image identity, single-SHM inputs and PHY-header latch checks pass.
  All ten existing Micro Signal modes build and their native regression
  tests pass. Both freestanding ELFs have no unresolved symbols.

**Final paired OTA evidence, build `9c3deabc`, channel 1:** each passing cell
below exchanged 100 fresh, ACK-confirmed frames **per direction**, with
independent payload/FCS and receiving-PHY checks.

| PHY/preamble | 32-byte MSDU | 1,500-byte MSDU | 2,304-byte MSDU |
| --- | --- | --- | --- |
| 1 Mbps long | Pass | Pass | **Not passed** |
| 2 Mbps long | Pass | Pass | Pass |
| 5.5 Mbps long | Pass | Pass | Pass |
| 11 Mbps long | Pass | Pass | Pass |
| 2 Mbps short | Pass | Pass | Pass |
| 5.5 Mbps short | Pass | Pass | Pass |
| 11 Mbps short | Pass | Pass | Pass |
| 6 Mbps OFDM regression | Pass | Pass | Pass |

The 23 passing cells total 4,600 application deliveries/ACK-confirmed TXs,
4,600 receiver PHY checks, 4,639 TX attempts and 39 ACK timeouts. They have
zero corruption, duplicate application deliveries, ordering errors,
PHY mismatches or controller faults. Retransmissions are not counted as new
deliveries. Evidence is retained under
`build/clean-rx/badge/c1-final-PHY-SIZE-*`, including configuration,
endpoint reports and logs.

**Remaining blocker:** full 2,304-byte MSDUs at 1 Mbps long. Inspection of
`c1-final-1-long-2304-run.log` corrected the earlier characterization of the
final fixed-size cell: picotool aborted during reboot/BOOTSEL launch
(`cancelled_exception`); this was not an RF trial or a measured loss rate.
Earlier retained cyclic-size trials (`c1-tail-1-long`, build `b678cb3e`)
delivered 32/1,500-byte frames but exhausted eight attempts on the full-sized
frame. A hardware-clock RX-deadline experiment did not repair reception and
was removed. No speculative PHY-register write or increased ACK allowance
is retained.

**Follow-up receive-truncation investigation, build `956a3e85`:**
paired channel-1 tests retained as `build/clean-rx/badge/c1-cause-retained-*`
reproduce the failure independently of the aborted launch:

- `full`: a 2,304-byte MSDU has a 2,332-byte MPDU. The receiver decodes the
  correct PLCP `0a04e048c464` (18,656-us LENGTH), but its RXE count reaches
  only 1,589 bytes at the PSM no-GoodFCS expiry. The transmitter consumes
  the complete staged template (end pointer 585 words, remaining count 0),
  reports no PHY TX error, and shows about 19,219 us of TX-active time.
  These counters do not independently prove the complete OTA waveform.
- `reverse`: swapping badge roles passes 1,500 and 1,550-byte MSDUs in
  both directions, then fails at 1,600 bytes. That failing receiver decodes
  the correct PLCP `0a04e03219b8` (13,024-us LENGTH), but again stops at
  RXE count 1,589. Thus the problem starts below the maximum MSDU and below
  the F2 fragmentation boundary; it is not solely a 2,304-byte buffer issue.
- Other trials observe 1,590 bytes. Same-attempt PSM tracing measures
  approximately 16,429 us from decoded PLCP to the normal discard.
  A removed, SHM-backed two-pass wait experiment (`e24cdff6`,
  `c1-cause-count-shm`) extends this to about 32,814 us, yet RXE still
  reports only 1,590 bytes. Waiting longer alone does not repair reception.
  Earlier traces that mixed a first-prefix timestamp with a later retry's
  failure timestamp are not valid elapsed-time evidence.
- Removing PLCP SERVICE Locked Clocks, and separate receiver-only changes
  to PHY `0x42c` (2590 to 5120) and `0x465` (10 to 20), did not help.
  The latter register names come from related LP-family public definitions,
  not verified LCN20 semantics. All those experiments were removed.
- On this same retained build, `2-long`, `11-long` and `ofdm6` each pass
  30 fresh cyclic-size frames per direction (32/1,500/2,304 bytes):
  180 total verified deliveries, with no fixture failure.

The verified failure is incomplete PHY/RXE reception, followed by no valid
FCS, no published MPDU and consequently no ACK. The initiating PHY/RXE
condition or exact control remains **unresolved**; a receive-duration limit
is a hypothesis, not an identified hardware watchdog. C1 stays open.
Retained diagnostics include a boundary fixture, retry-aligned PSM trace
and stopped-failure snapshots; see the
[diagnostic layout](HARDWARE_BRINGUP.md#1-mbps-receive-truncation-diagnostics).
Native controller/loader/PSM tests, Micro Signal regressions, freestanding
ELF closure and whitespace checks pass.

Intermediate `c1-matrix-*` evidence belongs to build `786ad5ee`, including
one stale-PHY-metadata rejection and an interrupted OFDM full-size trial;
it is not pooled with final-build results. This work does not complete C2,
C3's at-least-1,000-per-cell matrix, milestone A timing/soak gates, calibrated
RF waveform/EVM checks or AP interoperability.

Both badges were restored and independently compared after testing:
`08DA26F86E57F5EA` matches its original 33,604-byte application backup
(not a full-flash backup); `9BBF88DD17D66E7D` matches its entire original
16-MiB flash. Both were left in BOOTSEL. The Mac's networking was unchanged.
See [fixture usage and detailed evidence](../../badge/README.md#dssscck-rate-and-preamble-fixture).

#### C1 independent boundary investigation

The later `build/clean-rx/c1-finish/hardware/` campaign reproduces the failure
on the current clean build (`3b5748a1`): the paired
`baseline-boundary` trial ACKs and delivers indices 0/1 (1,500/1,550 bytes)
in both directions, then fails index 2 (1,600 bytes). The responder observes
the correct 13,024-us PLCP LENGTH but only 1,590 RXE bytes. No PHY timeout,
PLCP, ACK ownership or receive-validation change is made.

A fixed `1600` MSDU profile now permits directly testing the failing size
without first relying on successful ACKs to advance through a boundary
cycle. It preserves the original profile IDs, uses ID 7, and emits one
1,628-byte MPDU with PLCP `0a04e03219b8` at 1 Mbps. Native tests exercise all
eight profiles, rate/preamble combinations, frame sizes and invalid bounds.

The vendor peer gains an explicit passive raw-capture mode, separate from
its monitor-capability probe and protected AP profiles. Its native ASan/UBSan
tests reject altered payload/header/FCS, wrong nonce and truncation, and
accept complete boundary/fixed-1,600 frames in both source roles. It makes
no PHY-rate claim from opaque vendor envelopes and does not enlarge the
vendor transport's 2,040-byte limit.

| Evidence prefix | Vendor listener | Clean stimulus | Complete independent captures |
| --- | --- | --- | --- |
| `baseline-boundary` | Pico, `075cf3e5` | Paired 1-Mbps boundary cycle | 0; initial accounting incorrectly treated CDC requests as data TX |
| `monitor-small` | Pico, `6bb0fcf7` | Paired 1-Mbps / 32 | 0, despite successful clean deliveries |
| `monitor-promisc-boundary` | Pico, `567e8725` | Paired boundary cycle, explicit promiscuous readbacks | 0 |
| `badge-monitor-small` | Badge B, `dcc070df` | 1-Mbps / 32 | 8 complete attempts, payload and FCS verified |
| `badge-monitor-1600` | Badge B, `3ce2b1f5` | 1-Mbps / 1,600 | 0; capture operation itself succeeds |
| `badge-monitor-1500` | Badge B, `4495cafa` | 1-Mbps / 1,500 | 8 complete attempts, payload and FCS verified |

The two positive independent controls each repeat fixture index zero because
the vendor listener does not ACK. They are not sixteen fresh deliveries or
a loss-rate qualification. Source collector failure without an ACK is
expected in those passive two-device trials. `sdpcm_tx` explicitly includes
CDC setup/cleanup requests and cannot be interpreted as data/RF TX.

The Pico's missing positive controls prevent using its missing larger frames
as evidence of source silence. Even with the working badge reference,
absence of a complete 1,600-byte frame does not distinguish an incomplete
transmit waveform from a limitation of the reference receiver. No complete
full-sized OTA waveform is established. The initiating cause and safe repair
remain unknown; **C1 remains open**. Do not repeat the removed speculative
LCN20 register writes or relax FCS/length/acceptance gates.

All three devices were freshly backed up in full. After every campaign,
all borrowed flash was restored and independently compared in full:
16 MiB for each badge and 4 MiB for the Pico. Original SHA-256 values are
`02ff81bb2e453010f8517eb2df0391d569d285ed4ac97bfa275d4493c044b7ac`
(A), `2db102857596e0e680415be45b21ffb703a149c24ba9ee0b4950d008bf7ab3b3`
(B), and `f26bc587c863623426a4d0eeb0624c8f01e8fb150de7fe4c2ac28fe43c00e486`
(Pico). All were left in BOOTSEL; the laptop networking was not changed.

#### C1 transmit/reference localization follow-up

The `build/clean-rx/c1-localize/hardware/` archives retain a further bounded
investigation. The raw-source experiment used the exact soak frame with
three unacknowledged raw attempts, outside live-station DCF/ACK ownership.
Both `raw-controls` and `timed-raw-controls` failed their positive controls:
no fixture captures at 1 Mbps / 1,500, 1 Mbps / 1,600 or 2 Mbps / 1,600.
Successful source command completion and zero PHY errors therefore do not
localize the fault. The temporary raw-source code was removed.

Unpolled timed reference build `49f21eee` ends at 32 raw packets. Its
last-arrival timestamp includes cleanup-drained packets and is not evidence
of ten seconds of continuous reception. This matches the earlier vendor
forwarding/servicing limitations documented in
[the HT20 investigation](../experiments/ht20/README.md). New trace counters separate
pre-cleanup arrival/count from the final totals.

The `polled-station-controls` trials use known live-station TX and reference
build `8386a305`, issuing GET_MONITOR every 100 ms:

| Stimulus | Complete payload/FCS-valid MPDUs | Raw packets | Control polls | Last raw arrival before cleanup |
| --- | --- | --- | --- | --- |
| 1 Mbps / 1,500-byte MSDU | 3 | 46 | 21 | 2,065,299 us |
| 1 Mbps / 1,600-byte MSDU | 0 | 46 | 30 | 2,977,410 us |
| 2 Mbps / 1,600-byte MSDU | 8 | 46 | 24 | 2,936,788 us |

**All three capture operations fail with firmware timeout 6.** These partial
frame observations are not successful qualification. The 2-Mbps captures
prove this reference can forward a complete 1,628-byte MPDU, excluding a
simple size-only cap at that boundary. They do not establish a complete
long 1-Mbps waveform or exclude a duration-sensitive reference limitation.
Source ACK exhaustion is expected with a passive reference. Control polling
is now explicit `--cck-monitor-poll`; it is not silently enabled by default
and is not presented as a transport repair.

The `vendor-phy-readback` trial on badge B (`cf0cf940`) attempts read-only
GET_PHYREG command 94 at seven addresses after vendor radio-up/channel-1
setup. Every query is explicitly rejected with controller status -23,
error 8 and reply length zero. No register values or LCN20 semantics can be
inferred. The proposed eight-byte register/band request layout remains
unverified. The final collector reports `phy_readback_supported=false` and
fails its exit status when any readback is missing, rejected or malformed;
native tests cover complete/error/incomplete and invalid-value traces.

The final `vendor-phy-final` repetition (`63f667fc`) reproduces all seven
rejections and verifies collector exit 1 with an explicit unsupported-readback
diagnostic. Badge B is again fully restored and compared. Final peer firmware
build/native ASan/UBSan tests, badge native regressions and `git diff --check`
pass; those checks do not qualify the failing RF cells.

No speculative PHY writes, longer PSM waits, shared vendor transport changes
or relaxed frame checks are retained. The core cause and safe repair remain
unresolved, and **C1 stays open**. Both badges were freshly backed up before
borrowing, then restored and compared across the complete 16-MiB flash after
every campaign. The PHY-only trial borrowed only badge B; the Pico was not
modified. All remain in BOOTSEL, and laptop networking was unchanged.

#### C1 Pico vendor reference campaign

The user requested using the attached Pico 2 W as an independent official-
firmware source or receiver. This campaign uses the unchanged vendor radio
firmware asset (CRC `e627c344`) behind a diagnostic RP2350 host application.
It does not claim the host application itself is the official Pico SDK stack.
The source configures a normal channel-1 open AP, verifies fixed multicast
rate and long-preamble readback, then submits eight nonce-bound broadcast
Ethernet frames. Production packet-engine stats/stop requests again return
unsupported status -23, and monitor enable/readback alone is not RX proof.

Archives are under `build/clean-rx/pico-reference/hardware/`. Initial
`controls`/`timed-controls` HT trials are not passing controls: the first
hits the generic six-second host loader deadline; the second retains seven
valid terminal fixtures but has no gated HT packet metadata. The raw CCK
fixture requires no HT gates. Its dedicated ten-second loader budget covers
cold initialization, the existing 4.2-second collection window and shutdown,
without altering PHY timeout or non-fixture behavior. The initial
`cck-boundary` records fail the old MCS-only input guard before RX; these are
retained as preflight failures, not RF evidence. Native input-guard tests now
cover legal HT/CCK and malformed CCK configurations.

`cck-small-controls` passes all eight fresh 84-byte MPDUs at both 1/2 Mbps.
Complete on-device address/payload/sequence/FCS and decoded rate/channel/
long-preamble checks pass. `cck-long-controls` repeats 8/8 at 84/1536 bytes
at both rates, 0/8 at 1636 bytes at 1 Mbps and 8/8 at 2 Mbps.

The separate `vendor-to-vendor` trial uses Pico source `24397e6e` and badge B
vendor monitor `c96649f9`, not a clean receiver. It retains four complete
frames each at 84/1536 bytes at both rates and at 1636 bytes at 2 Mbps,
with payload/FCS verification, but none at 1636 bytes at 1 Mbps. All eight
monitor operations finish their setup/collection/cleanup successfully, but
forwarding again stops at 30 pre-cleanup raw packets (32 final). These
partial captures are not a reliable loss-rate baseline, and opaque vendor
metadata does not independently decode rate.

Source counters in `cck-counter-controls` (`b5cc8393`) use the public
version-10/848-byte ABI already verified by the vendor-burst fixture. At
Ethernet sizes 62/1514/1614, both rates advance `txframe` and `txfrmsnt` by
eight, with no `txerror`, `txnobuf`, `txphyerr` or `txfail` increases.
At size 1714 both data-TX counters remain zero, although host submissions
return successfully. That size is **not a valid RF stimulus**. The final
source treats missing data-TX accounting as protocol failure rather than
reporting success-shaped submission-only evidence. Counter ABI rejection
also fails explicitly; unrelated AP beacons/control frames are not counted
as fixture data.

The final `fine-boundary` source build `8f3e01dc` and clean receiver
`8bf5afb9` test exact MPDU lengths:

| Ethernet bytes | Plain MPDU bytes, including FCS | 1-Mbps fresh captures | 2-Mbps fresh captures | Vendor data-TX accounting |
| --- | --- | --- | --- | --- |
| 1558 | 1580 | 8/8 | 8/8 | 8 frames, zero TX errors at both rates |
| 1562 | 1584 | 4/8 | 8/8 | 8 frames, zero TX errors at both rates |
| 1566 | 1588 | 0/8 | 8/8 | 8 frames, zero TX errors at both rates |
| 1606 | 1628 | 0/8 | 8/8 | 8 frames, zero TX errors at both rates |
| 1714 | 1736 expected, not captured | 0, invalid stimulus | 0, invalid stimulus | Zero data-TX frames; explicit source error 7 and exit 1 |

Successful clean captures require complete validation, not a matching
prefix, fragment or PLCP length alone. The 1584-byte 1-Mbps mask is `0x8e`;
there are no duplicate fixture deliveries. Global discard counters include
ambient traffic and cannot be attributed to missing fixture frames.
The 1628-byte test matches the failing clean-soak MPDU size, despite the
AP fixture's additional LLC/SNAP overhead changing its Ethernet payload
length. No accepted full-size frame is silently replaced by fragmentation.

This removes dependence on the clean transmitter for reproducing the
receive boundary and establishes a useful independent vendor source for
passing cells. It does **not** establish a complete 1628-byte waveform at
1 Mbps: the vendor receiver also misses it and vendor TX counters cannot
prove physical EOF. It therefore does not yet identify a unique clean-RX
bug, a safe LCN20 register repair or full C1 qualification. C1 stays open.

Every borrowed Pico/badge was freshly backed up in full and restored with
complete-flash SHA-256 comparison after each campaign (4 MiB Pico, 16 MiB
per badge). Original hashes remain those recorded above; all devices are
left in BOOTSEL. No laptop networking, credentials, vendor radio binary,
speculative PHY register or production Ethernet MTU was changed.

Final Pico/clean-badge/vendor-badge builds pass. Peer and badge native
ASan/UBSan regressions, shared vendor transport/default-MTU tests, new CLI
invalid-rate/alignment/upper-bound checks and `git diff --check` pass.
The two final zero-data-TX controls are checked to exit 1. These are
bounded fixture results, not C3's 1,000-frame-per-cell qualification.

#### C1 vendor PHY window and coexistence controls

The next investigation adds opt-in `--phy-window-probe`, independently of the
still-unsupported GET_PHYREG command. The live transport now exposes aligned
16-bit backplane reads/writes without the force-32-bit address flag, matching
the access distinction in [Linux SSB SDIO](https://github.com/torvalds/linux/blob/master/drivers/ssb/sdio.c).
Existing 32-bit and block operations are unchanged. Native fake-bus tests
check halfword-but-not-word-aligned access, omitted flag, neighboring memory
preservation, and rejection of misaligned/null inputs.

The peer saves PHYADDR at `0x180013fc`, selects one register, verifies the
selector, requires two equal PHYDATA reads at `0x180013fe`, and restores the
saved selector even after an intermediate error. No PHYDATA writes occur.
Restoration errors remain fatal; successful restoration preserves a preceding
failure. The collector requires all seven successful, complete 16-bit values
and a plausible nonzero/non-`0xffff` raw version in new trace mode 4.
These checks cannot exclude concurrent selector accesses by the running
vendor firmware and do not establish LCN20 register semantics.

Three initial cold trials (`072f1d7c`, vendor asset CRC `e627c344`) give:

| PHY register | Vendor readback |
| --- | --- |
| 0 | `0xdc00` (raw version, not decoded) |
| `0x42c` | 2590 |
| `0x465` | 10 |
| `0x464` | 0 |
| `0x466` | 2000 |
| `0x430` | 0 |
| `0x431` | 0 |
| `0x432` | 46336 (`0xb500`) |

The suspected `0x42c`/`0x465` values match clean defaults, so there is no
observed vendor/clean mismatch to repair there. In particular, the number
2000 alone does not identify a receive-duration limit. Evidence and the
full-flash restoration comparison are retained under
`build/clean-rx/cck-window/hardware/initial-probe/`.

A separate isolated source build `02ee8919` tests vendor Bluetooth
coexistence as a duration-arbitration hypothesis. The public `wlioctl.h`
definition `WL_BTC_DISABLE=0` establishes the requested disabled mode, rather
than relying on the unrelated Linux DHCP coexistence enum. Each cold AP trial
queries `btc_mode`, sets 0, and confirms readback `1 -> 0` before submitting
eight frames. Clean receiver `8bf5afb9` reports:

| Rate | Ethernet bytes | MPDU bytes including FCS | Complete fresh frames |
| --- | --- | --- | --- |
| 1 Mbps | 62 | 84 | 8/8 |
| 1 Mbps | 1514 | 1536 | 8/8 |
| 1 Mbps | 1606 | 1628 | 0/8 |
| 2 Mbps | 1606 | 1628 | 8/8 |

All four sources account eight data transmissions with zero new TX errors.
Successful receives validate full payload/FCS and decoded rate/long preamble.
Global bad-FCS counters include ambient traffic and cannot identify the lost
frames. Disabling source coexistence does not recover the boundary; this
does not test a clean-receiver-side coexistence change or prove physical EOF.
The unsuccessful compile-time switch and temporary collector fields were
removed, leaving production coexistence behavior unchanged.
Evidence is under `build/clean-rx/cck-window/hardware/btc-off/`.

The Pico and badge A were freshly backed up, restored, and compared over
their entire 4-MiB/16-MiB flash, with original hashes unchanged. Badge B was
not modified. There is still no safe demonstrated PHY repair, no independent
complete long 1-Mbps waveform capture, and no C1 qualification.

The final diagnostic build `42827d38` repeats all seven readings unchanged
across three further cold Pico trials, with full-flash restoration verified;
records are under `build/clean-rx/cck-window/hardware/final-probe/`.
Pico and default badge vendor peers build successfully. Shared transport,
peer ASan/UBSan regressions, and clean badge build/native regressions pass.

#### C1 associated vendor reference investigation

With only the two badges and Pico available, the next diagnostic replaces
passive vendor monitoring with an ordinary associated open-network station.
This removes dependence on monitor forwarding and its previous 30/32/46-
packet limitations. Both endpoints retain the unchanged official radio asset
behind our diagnostic RP2350 application, not the official Pico SDK host
stack. No speculative PHY write or receive-wait extension is introduced.

Opt-in peer mode 13 (`--vendor-cck-station`) joins the nonce-derived AP and
requires its exact BSSID. Mode 14 (`--tx-cck-associated`) waits up to eight
seconds for the exact nonce-derived station MAC before sending the existing
eight multicast-rate/long-preamble fixtures. The source rechecks those
settings after association and after TX, in addition to the existing
version-10 data-TX counters. Legacy source/monitor modes are unchanged.

The receiver uses the existing raw SDPCM callback without enabling monitor
mode, then an explicitly diagnostic BDC decoder and complete Ethernet
destination/source/EtherType/nonce/sequence/pattern validation. Normal RX
queues and the normal decoder retain the 1514-byte limit. The diagnostic
decoder is bounded by the existing 2040-byte SDPCM envelope, including the
12-byte minimum SDPCM and 4-byte minimum BDC headers. It does not enlarge a
buffer or copy an oversized frame into the normal queue.

The station requires eight distinct complete frames with no duplicates,
malformed data or fixture corruption to pass. Missing frames retain evidence
but exit 1, not a success-shaped result. Its four-second collection window
starts after association. Ethernet decapsulation strips MPDU/FCS and PHY
metadata: `mpdu_fcs_visible=false` and `phy_rate_verified=false` remain explicit.

Archives are under `build/clean-rx/cck-managed/hardware/`:

- `associated-controls`: Pico source `76e543fb`, badge B managed station
  `bef8b820`. All eight frames arrive at 2 Mbps / 62 and 1606 Ethernet bytes,
  and 1 Mbps / 62 and 1514 bytes. At 1 Mbps / 1606, one complete payload
  arrives (sequence 5, mask `0x20`) and seven do not; the receiver exits 1.
  This is useful partial payload evidence, not a passing capture operation
  or independently rate-decoded complete MPDU.
- `reverse-controls`: badge B source and Pico managed station. At 2 Mbps /
  62 bytes the Pico receives 8/8, but at 1606 bytes only 3/8 (`0x2c`).
  The next 1-Mbps / 62-byte positive control receives 7/8 (`0xef`), so
  the campaign stops and restores both devices rather than interpreting
  further missing long frames from an unqualified reference direction.
- `simultaneous-controls`: Pico source, badge B managed station and badge A
  clean receiver `8bf5afb9` listen to the same broadcasts. Both receivers
  receive 8/8 at 2 Mbps / 62 and 1606 Ethernet bytes and 1 Mbps / 1514.
  Both receive 0/8 at 1 Mbps / 1606 in two separate nonce-bound trials.

The final `final-boundary` campaign uses source `8a59ca5c` and vendor
station `b6d25163`, adding the post-association/post-TX settings checks.
The clean receiver deliberately stays on the matched archived build
`8bf5afb9` to avoid changing a second experimental variable:

| Requested rate | Ethernet bytes | Expected plain MPDU bytes including FCS | Vendor station complete payloads | Clean complete, FCS/rate/preamble-verified MPDUs |
| --- | --- | --- | --- | --- |
| 2 Mbps | 62 | 84 | 8/8 | 8/8 |
| 2 Mbps | 1606 | 1628 | 8/8 | 8/8 |
| 1 Mbps | 1514 | 1536 | 8/8 | 8/8 |
| 1 Mbps | 1562 | 1584 | 6/8 (`0xd7`) | 4/8 (`0x74`) |
| 1 Mbps | 1566 | 1588 | 0/8 | 0/8 |
| 1 Mbps | 1606 | 1628 | 0/8 | 0/8 |

All six sources account eight data transmissions with zero new TX errors
and successful fixed-rate/long-preamble readbacks after association and TX.
At 1584 bytes, the masks differ: some sequences arrive only at the vendor
station and one only at the clean receiver. Thus a universal hard transmitter
cutoff is not an adequate explanation for that cell. Global clean discard
counters include ambient traffic and are not per-fixture failure counts.

The sharp long-1-Mbps loss also reproduces without any clean firmware in
either vendor endpoint. Therefore a timeout solely in our clean PSM cannot
explain the entire observation. The single initial 1606-byte Ethernet
delivery establishes that the managed vendor path is not absolutely
incapable of delivering that size, but it does not establish stable
1-Mbps transmission or isolate TX versus RX at the failing boundary.
RF/clock/board configuration or a shared chip/firmware duration-sensitive
limitation remain hypotheses, not demonstrated causes. C1 stays open.

Every campaign freshly backs up and restores complete flash. Final SHA-256
comparisons match the original 4-MiB Pico and both original 16-MiB badges;
all devices are left in BOOTSEL. Source/peer ASan/UBSan tests, shared
transport/default-MTU regressions and clean badge build/native regressions
pass. New tests cover every-byte fixture corruption/truncation, identity and
nonce mismatch, duplicates, malformed BDC, all normal/diagnostic TX lengths,
and exact maximum diagnostic RX bounds. Fourteen invalid new CLI cases
reject before USB access. No laptop network, credential, radio asset,
production MTU or qualification criterion was changed.

### C2. Basic-rate and mixed-network compatibility

- Recorded open criterion: Negotiate appropriate supported/basic rates, including a usable
  802.11b-only association path instead of requiring an AP to advertise 6 Mbps.
- Recorded open criterion: Integrate ACK/control-response selection and applicable b/g protection.
- Recorded open criterion: Verify connection and bidirectional traffic against a b-only AP and a
  mixed b/g AP, including peers that require long preambles.

### C3. Rate/size qualification matrix

- Recorded open criterion: Verify each rate in both directions using small frames, Ethernet-sized
  frames and full 2,304-byte MSDUs, with fresh payloads and independent FCS/
  payload checks. Run at least 1,000 frames per matrix cell.
- Recorded open criterion: Record loss/retries and require zero corruption, zero duplicate delivery
  and no controller/transport faults. Re-run the existing OFDM regression.


