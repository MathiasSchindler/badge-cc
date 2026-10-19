# Vendor/AP HT reference experiment

Two dedicated, freestanding C/nolibc RP2350 applications:

- **Vendor station:** loads the unchanged supplied vendor Wi-Fi firmware,
  joins the approved WPA2 AP, requests HT20 MCS0, queries actual supported
  interfaces and submits a bounded, known unicast burst.
- **Clean observer:** runs a default-disabled passive PSM HT lifecycle logger
  on the station's actual channel. TX is disabled; it does not associate,
  install a key or transmit competing ACK/BA responses.

Neither application links the display, clean station/WPA2 stack or previous
aggregate/BA badge experiments. Shared board, USB, transport and loader code
is reused. The host collector uses the repository's existing libusb transport.
Vendor assets are not dependencies of the clean controller.

## Build and run

From the repository root:

```sh
make -f clean-rx/vendor-burst/Makefile reference reference-test
bash clean-rx/vendor-burst/run.sh VENDOR_SERIAL OBSERVER_SERIAL \
    build/clean-rx/vendor-burst/NEW_TRIAL_PREFIX
```

Both serial-matched badges must be in BOOTSEL. The optional fourth argument
selects a private credential file; the default is `ssid-data/ssid.txt`, with
`SSID:` and `Pass:` fields. Credentials are provisioned over USB into RAM,
wiped after use and never included in image assets, reports or command lines.
An ASCII WPA2 passphrase of 8..63 bytes is required.

**Back up both entire flashes first. The runner replaces firmware and does
not back up or restore originals.** Use a fresh prefix; partial/failed trials
are retained. Raw evidence files are opened exclusively, not overwritten.
The collector asks both devices to return to BOOTSEL, including after an
ordinary failure. An absolute device watchdog bounds abandoned sessions;
the host also has a USB deadline.

The two retained phases are deliberate:

1. Associate and authorize the vendor station; discover the actual channel.
2. Load, arm and explicitly start the clean receiver; require `RX_ACTIVE`.
3. Snapshot vendor counters, submit 128 frames and drain for 1.2 seconds;
   snapshot counters again and stop the vendor radio.
4. Issue the clean controller's stop command and require `RX_DONE`.
   Then read the immutable PSM log, mailbox and early-HT diagnostics.

Stopping the PSM alone is insufficient for readout: the running Cortex-M3
also uses the D11 object selector. Controller stop acknowledgement avoids
cross-core selector races before shared-memory reads.

## Fixture and evidence

The burst contains 32 frames of each Ethernet length: 128, 256, 512 and
1024 bytes. The length index is `(sequence + sequence / 4) & 3`: each
four-frame group contains every size, and its first size rotates. Each size
starts eight groups. This avoids always placing the smallest frame at the
same submission phase or first position of a possible four-MPDU aggregate.
It does not assume the vendor actually groups frames that way.
It uses experimental EtherType `0x88b5`, unicast to the AP,
with a nonce-derived local MAC, `HTREF001`, sequence number, challenge,
deterministic payload and trailing CRC32. Submission is bounded to six
seconds. A rolling digest of pre-trailer CRCs detects fixture changes;
CRC32 over a message plus its own CRC would instead have a constant residue.
This is not a broadcast/IP flood or continuous RF transmission.

Each raw phase file contains a 64-byte build/nonce-bound, CRC-checked report
and 5888-byte trace. Trace pages are independently bound to that report.
The collector writes JSONL, stderr and loading/readiness logs alongside them.
Device-reported submission is not treated as OTA payload verification.

### Vendor queries

The measured firmware supports:

- `nmode`, `nrate`, `2g_rate`, `mimo_bw_cap`;
- `ampdu`, `ampdu_tx`, `ampdu_rx`, `ampdu_mpdu`, `ampdu_ba_wsize`;
- `amsdu` GET, `wme`, `chanspec`, `sta_info`, and `counters`.

`amsdu` SET, `ampdu_counters` GET and `ampdu_dump` GET return controller
status `-23` on this firmware. The unsupported SET was removed; optional
unsupported GETs remain explicit records, not zero-valued successes.
The final command status may therefore be `-23` while the experiment itself
completed successfully.

The observed `counters` structure is version 10, length 848. The decoder
rejects other layouts and retains raw replies. Offsets were checked against
the complete public `wl_cnt_ver_ten_t` layout, including six `rxuflo` entries:

| Counter | Byte offset | Interpretation |
| --- | ---: | --- |
| `txframe` | 4 | Driver data-frame count, not aggregation proof |
| `txerror` | 16 | Driver TX errors |
| `txallfrm` | 196 | MAC transmitted frames, including control traffic |
| `rxback` | 416 | Received Block ACKs |
| `txfail` | 432 | MIB TX failures |
| `txretry` | 436 | MIB TX retries |
| `txfrmsnt` | 472 | MIB transmitted-frame count |

Reference: [public Infineon WHD ioctl header](https://github.com/Infineon/wifi-host-driver/blob/master/WHD/COMPONENT_WIFI6/src/include/whd_wlioctl.h).
No public vendor implementation was copied into the clean firmware.

### Passive lifecycle logger and correlation ABI

`CLEAN_RX_HT_OBSERVER=1` requires extended HT RX, excludes the aggregate
fixture/interface/delivery profiles and defaults off. It retains at most 16 prefixes
in SHM `0x100`; overflow is counted and retained entries never overwritten.
The observer trace has ABI 5 at byte 64. The 16-word SHM header holds session
identity, permit/count/overflow, pending/held IDs, RX epoch, progress and the
first good-only hint's RX count. Each 47-word record has an append-only
prefix and a one-shot terminal/exit snapshot:

| Record words | Contents |
| --- | --- |
| 0..9 | Prefix TSF, raw PHY/RXE status, HT-SIG and working-register value |
| 10 | Prefix RX epoch |
| 11 | Outcome: 0 open, 1 good, 2 bad FCS, 3 mismatch/short, 4 length rejection, 5 abort/fault, 6 watchdog, 7 new RX, 8 new prefix |
| 12..17 | Terminal TSF, RXE length, PHY and status |
| 18..37 | First 40 MAC/CCMP/ciphertext bytes, only after terminal checks |
| 38..39 | Terminal epoch and record ID |
| 40 | Observed progress: 0 no body wait, 1 body wait, 2 event 6, 3 event 6 with good-only status1 bit 8, 4 observed status1 bit 14 |
| 41..45 | Raw SPR `0x1004`, `0x1002`, `0x102a`, `0x102b`, `0x10af` |
| 46 | RX count at the first event-6/good-only-bit-8 observation; zero if absent |

Every RX start logs any pending exit before clearing ownership and advancing
the epoch. Every new prefix, including legacy, similarly records the boundary
before invalidating old ownership and classifying the new prefix.
An HT prefix gets a bounded record ID. At the later good/bad terminal hook,
the logger requires the same epoch, all six signature bytes and sufficient
length before copying any MAC bytes. Mismatches/short frames are explicit
rejections. Bad frames cannot acquire a delivery ID. Completed snapshots
are not rewritten by duplicate terminal events.

Length rejection, short completion, the existing abort/fault branch and
watchdog expiry snapshot the pending record before returning to their
unchanged original continuation. They never grant a delivery ID. The
completion logger observes event 6 and status bits but does not acknowledge
early events, advance the receiver or transmit. Bit 8 is a good-only hint,
not a general subframe-ready indication. Progress describes observations,
not proof of physical EOF or its absence: a normal good completion can
follow a recorded phase 3 without an intervening logged phase 4.

The logger runs after the established HT-prefix copy. The earlier PC153
handoff was experimentally unsuitable: incoming PHY status could indicate HT
while shared-memory prefix and MAC bytes still belonged to an earlier RTS.
The final logger does not copy or attribute those stale MAC bytes. Likewise,
PHY format bits need not yet indicate HT at the actual prefix-copy point.
The host independently checks HT20/MCS0..7, reserved/tail bits and HT-SIG CRC8,
then inspects the HT-SIG aggregation bit.

The good terminal's held ID is propagated in reserved SDPCM bytes 10..11
of the existing F2 publication, only in this diagnostic build. Ordinary
profiles still write zero there. The PSM remains held until publication,
and fragmented packets retain the same ID. No extra live backplane polling
or unsafe concurrent SHM readout is needed.

The RP2350 retains full ciphertext/FCS samples for at most two distinct
vendor-TA MPDU lengths, replacing a same-size candidate with the latest
tagged candidate. Each 1104-byte slot holds a 16-byte descriptor and up to
1088 MPDU bytes. The sample pool starts at trace byte 2560; the SHM log ends
at byte 2560, exactly before the sample pool; the two ordinary samples end
at byte 4768. A separate 1024-byte frozen early snapshot occupies bytes
4768..5791. Compile-time checks enforce these boundaries. Oversize candidates
are counted explicitly. Full-F2 counts are retained even when a sample is
not saved.

The host independently requires:

- A bounded ID referring to an actual retained prefix;
- Valid HT-SIG CRC/profile and a PSDU length consistent with the MPDU;
- Good terminal outcome, matching prefix/terminal epochs and record ID;
- Exact agreement between the terminal's 40-byte snapshot and the F2 bytes;
- The fresh nonce-derived vendor TA and a complete valid MPDU FCS;
- Exact terminal RXE length equal to delivered MPDU length plus six.

Only a fully checked ordinary or early sample makes its prefix
`transmitter_attributed:true`.
Other records remain unattributed, even when their MAC snapshot looks plausible.
The collector passes only with vendor received-BA activity, two different
fully correlated MPDU lengths and at least one correlated A-MPDU-format
reception. This is not timestamp-only or stale-header correlation.
The current early-snapshot experiment additionally requires a fully
correlated early sample; ordinary-reference success alone is insufficient.
Valid prefixes without an exit must still be the pending prefix at host
stop; unexplained lost ownership fails qualification. JSON includes named
exit reasons, terminal status/TSF, raw registers and first-hint RX counts.

An A-MPDU-format PPDU can contain a single MPDU. Correlation of one complete
MPDU does not prove reception of multiple bodies from that PPDU.

## Initial uncorrelated reference result

Final RF build `fd5025e1`, actual AP channel 6, both physical role assignments:

| Vendor / observer | Submitted | Delta `rxback` | Valid HT prefixes | A-MPDU-format prefixes | Ordinary F2 frames |
| --- | ---: | ---: | ---: | ---: | ---: |
| A / B | 128 | 103 | 7 | 6 | 348 |
| B / A | 128 | 97 | 5 | 5 | 571 |

A is `08DA26F86E57F5EA`; B is `9BBF88DD17D66E7D`.
Both runs were authorized and HT-capable, with zero `txerror`, `txfail`
and `txretry` deltas, no prefix overflow and clean observer TX disabled.
`txfrmsnt` increased by 130 in each run; `txallfrm` increased by 238/219.
The fixture submitted 32 of each requested length in both runs.
Rate readback was `0xc1010000`, and captured valid headers were MCS0.
Final evidence prefixes:

- `build/clean-rx/vendor-burst/final-ab`
- `build/clean-rx/vendor-burst/final-ba`

Thus the reference demonstrates actual vendor/AP Block ACK activity and
independent on-channel A-MPDU-format HT headers, not just enabled flags or
submitted bursts. The original collector required both positive observations to pass.
It does **not** claim RF transmitter attribution, decrypted fixture payloads,
complete multi-MPDU reception, production clean BA or EOF-to-air SIFS timing.
One reverse prefix advertises a 478-byte PSDU, consistent with aggregation
of small fixtures, but its MPDU bodies were not captured/validated.

Earlier failed evidence is retained as `trial-ab-r0` through `trial-ab-r5`:
unsupported `amsdu` SET; receiver left at `PSM_READY`; shared object-selector
readout race; stale controller build from a missing header dependency; and
the stale PC153 prefix/MAC assumption. Those issues were corrected rather
than attributed to unexplained RF loss. Successful precursor runs are
`trial-ab-r6` and `trial-ba-r0`. The PSM make rules now track the observer
header for both target objects and native tests.

## Fresh-prefix/MAC/F2 result

Final correlation RF build **`4e209642`**, MCS0, actual AP channel 6:

| Vendor / observer | Fully checked MPDU lengths | Correlated A-MPDU-format samples | Received-BA delta | Matching vendor F2 frames |
| --- | --- | ---: | ---: | ---: |
| A / B | 170, 298, 554 | 1 | 110 | 4 |
| B / A | 170, 298, 1066 | 3 | 112 | 10 |

Each run submitted 128 fixtures, 32 of each Ethernet length. Across the two
roles the four observed MPDU lengths are consistent with those Ethernet
lengths plus 42 bytes of encapsulation/encryption/FCS. This length relation
is not a claim to have decrypted the payload. Every listed sample has its
full captured bytes, valid FCS, matching terminal header and independently
validated prefix/epoch/ID association. Both collectors exit zero.

Evidence:

- `build/clean-rx/vendor-correlate/rotated-ab-r0`
- `build/clean-rx/vendor-correlate/rotated-ba-r0`

The A/B run has six CRC-valid prefixes; four reach good terminal snapshots.
Two larger A-MPDU-format PSDUs, **2110 and 862 bytes**, have fresh valid
prefixes but no observed good/bad terminal commit before subsequent RX.
They remain unattributed and have no invented body delivery. This narrows
the next live-interface investigation to these missing terminal/advance paths;
it does not prove absence of a physical EOF.

The B/A run has eleven prefix candidates, ten with valid CRC. A bad-terminal
candidate with invalid HT-SIG is retained and rejected, not counted as a
correlated reception. Neither run has prefix-log overflow, signature/epoch
mismatches or oversize samples. Vendor TX retry deltas are 3/0; TX error and
failure deltas are zero. Observer TX remains disabled.

Earlier correlation controls are preserved in the same directory:
`trial-{ab,ba}-r0` passed on build `c5b1c57d`; `final-ab`, `qualified-ab-r0`
and `qualified-ab-r1` failed the distinct-length criterion after retaining
only 170-byte MPDUs, despite successful identity/FCS correlation. The
corresponding B/A controls passed. Rotating the stimulus removes its
fixed-first-size bias, but these few controls do not establish causality,
loss-free reception, a receive-corruption fix or a performance qualification.
In particular, 4/10 matching vendor F2 frames from 128 submissions is not
a successful sustained-reception result.

Native ASan/UBSan tests cover new RX without a new prefix, intervening legacy
traffic, each signature word, epoch mismatch, short/bad frames, duplicate
terminal events, sample ID/length/header/FCS corruption and balanced rotated
fixture counts. Default PSM reconstruction identity and non-observer
controller tests still pass.

## Live aggregate exit investigation

The passive logger now accounts for exits instead of treating every absent
good/bad commit as unexplained loss. Initial exit-trace build **`95e7676c`**
(ABI 3, 46-word records) passed both role assignments:

| Evidence suffix under `build/clean-rx/vendor-exits/` | Checked MPDU lengths | Received-BA delta | Valid / A-MPDU prefixes |
| --- | --- | ---: | ---: |
| `trace-ab-r0` | 170, 298 | 112 | 8 / 7 |
| `trace-ba-r0` | 170, 1066, 298 | 96 | 11 / 10 |

In `trace-ba-r0`, two larger CRC-valid A-MPDU-format prefixes have explicit
abort/fault exits, not a normal good/bad terminal commit:

| Prefix ID | PSDU bytes | Prefix-to-exit us | Exit RX count | Observed phase | Status1 / status2 | Raw `0x102a` / `0x102b` |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 2 | 1598 | 1347 | 560 | 3 | `0x9922` / `0x0a43` | 554 / 170 |
| 4 | 1342 | 1016 | 304 | 3 | `0x9922` / `0x0a41` | 298 / 554 |

Both retain raw `0x1004=0x0802`, `0x1002=0x0f40` and `0x10af=0x1000`.
They reached event 6 and the good-only bit-8 hint before the existing
abort/fault path. Exit RX counts are consistent with candidate first
MPDUs plus six bytes, but no complete first-body/FCS snapshot was retained.
These prefixes remain **unattributed**. Raw registers do not independently
establish next-frame ownership, a FIFO/DMA root cause or physical EOF.
Waiting only for terminal status is now a concrete suspect, not a proven
cause of the aborts.

Build **`7f1e3e90`** adds the first-hint RX count and ABI 4. Five subsequent
bounded controls pass:

| Evidence suffix | Checked MPDU lengths | Received-BA delta | Valid / A-MPDU prefixes |
| --- | --- | ---: | ---: |
| `hint-ab-r0` | 170, 298 | 112 | 6 / 6 |
| `hint-ba-r0` | 298, 554, 170 | 113 | 8 / 7 |
| `hint-ab-r1` | 170, 298 | 112 | 5 / 5 |
| `hint-ba-r1` | 170, 298 | 108 | 5 / 4 |
| `hint-ba-r2` | 170, 554 | 124 | 3 / 2 |

All have zero unaccounted exits, overflow and pending-at-stop IDs. A
fully correlated 298-byte MPDU in `hint-ab-r0` records its first good-only
hint at RX count 304. However, these five controls did **not** reproduce
the larger abort cases. Their new first-hint field therefore cannot be
retroactively assigned to the ABI-3 abort observations.

Keep `trace-ab-r1` as a failed control: received-BA delta 116 but **zero**
captured prefixes and no correlated MPDUs. It correctly exits nonzero.
A transient host `picotool info` segmentation fault during readiness
polling was visible before a later successful retry; both subsequent
collectors passed. These results do not establish reception reliability.

The exit-only follow-up build **`d7519182`** additionally routes the
existing sub-14-byte completion rejection through the explicit short-exit
logger, returning to the original PC263. It has not been radio-tested.
Native tests cover this branch, length/abort/watchdog and replacement
exits, immutable completed records, first-hint retention when later counts
change, and rejection of every non-good outcome by full-sample correlation.

No early handoff is enabled. The next controlled step is to preserve a
complete variable-length candidate at the early event, independently check
its FCS and boundaries, and only then test the existing advance primitive
under a bounded default-off diagnostic. Success still requires at least
two distinct complete FCS-valid MPDUs bound to **one fresh PPDU**, not two
prefixes, good-only status hints or received-BA counters. Bad subframes,
whose bit 8 may be clear, must remain part of the handoff design.

## Complete early-event MPDU preservation

Build **`e9008554`** adds `CLEAN_RX_HT_EARLY_SAVE=1`, default off outside
this focused experiment. It copies complete MCS0 A-MPDU-format candidates
without acknowledging the early event, advancing the receiver or enabling
TX. The minimal USB main is unchanged.

The snapshot occupies SHM words `0x400..0x5ff`, immediately after the
prefix records and strictly before the live control/status words at
`0x600`. Its maximum body is **1000 bytes**, not the ordinary receiver's
larger limit. This covers the 170/298/554-byte fixtures but deliberately
excludes 1066-byte MPDUs; excluded candidates are counted with saturation
instead of truncated. Direct, length-guarded PSM word moves avoid an
unverified indirect-memory interface. Native tests cover every length
44..1000, including odd final words and both exact memory boundaries.

| Snapshot words | Contents |
| --- | --- |
| 0 | Commit-last prefix ID; zero means absent or incomplete |
| 1..2 | RX epoch and RX count |
| 3..4 | Hint-time status1 and snapshot status2 |
| 5..6 | Snapshot TSF |
| 7..9 | Retained HT-SIG |
| 10 | Candidate kind: 1 single-MPDU-sized, 2 advertised PSDU exceeds this candidate's padded MPDU plus delimiter |
| 11 | Oversize-candidate counter |
| 12..511 | Up to 1000 complete MPDU bytes, including FCS |

The first retained candidate is immutable unless a later multi-candidate
replaces a single-sized candidate. The first retained multi-candidate is
then immutable. RX count, epoch and all HT-SIG words must remain unchanged
across the copy before its ID is committed. Scratch registers are restored.
Native negatives cover missing ownership/event/hint, legacy/other MCS,
short/oversize frames, stale copies and repeated retention.

The hint's raw status is read at the hint hook, before the longer copy
setup. It temporarily reuses the global outcome word; every terminal or
exit entry assigns its own outcome before consuming that word. This avoids
mixing a late terminal status read with an earlier event observation.
A native race test changes live status to terminal after the hint and
verifies that the retained hint status is unchanged.

The host independently requires bounded identity/length, matching fresh
prefix epoch and all HT-SIG bytes, valid HT-SIG CRC/profile, MCS0 and
aggregation, a consistent candidate-size classification, nonterminal/
nonabort good-only hint status, the nonce-derived vendor TA, data-frame
type, and a valid FCS over the complete saved bytes. A later abort need not
invalidate a previously complete early body. Invalid/incomplete snapshots
never qualify, and the ordinary F2 correlation remains a separate predicate.

### Positive radio result and limits

`build/clean-rx/vendor-early/hint-ba-r0` passes the full collector:

- B runs unchanged vendor firmware; A is strictly passive.
- Prefix **ID 4, epoch 74**, CRC-valid HT-SIG `00de010fb003`, advertises a
  **478-byte** A-MPDU-format PSDU at MCS0.
- The independent early snapshot retains a complete **170-byte MPDU**,
  RX count 176, with valid FCS and the fresh vendor TA.
- Hint status1 is **`0x1922`**: good-only bit 8 set, terminal bit 14 and
  abort bit 15 clear. Snapshot status2 is `0x0a41`.
- Snapshot TSF is 407517 versus prefix TSF 407296. These are diagnostic
  PSM observations, not externally measured RF/SIFS timing.
- The later good terminal still has RX count 176; raw `0x102b` is 298.
  **No second complete MPDU was captured or independently validated.**
- Ordinary saved MPDUs are 170/298 bytes, received-BA delta is 101, and
  there is no prefix overflow, unaccounted exit or pending ID at stop.
- One oversize early candidate is explicitly counted.

This closes one concrete early-byte preservation test on a larger live
PPDU. It does not qualify repeated early capture, both physical roles,
other early lengths/MCS, next-frame advance, or two-body reception.
“Nonterminal” refers to the captured hint status; it does not establish
copy completion before physical EOF or an EOF-to-air timing bound.

All six final-build controls are retained, including failures:

| Evidence suffix under `build/clean-rx/vendor-early/` | Ordinary checked lengths | Received-BA delta | Valid / A-MPDU prefixes | Early qualification |
| --- | --- | ---: | ---: | --- |
| `hint-ab-r0` | 554, 170 | 108 | 5 / 5 | No retained early sample; fail |
| `hint-ba-r0` | 170, 298 | 101 | 15 / 14 | Complete 170-byte multi-candidate; pass |
| `hint-ab-r1` | 170, 298 | 111 | 6 / 5 | No retained early sample; fail |
| `hint-ba-r1` | 170 | 112 | 5 / 4 | No early sample and only one ordinary length; fail |
| `hint-ab-r2` | 170, 298 | 125 | 3 / 2 | No retained early sample; fail |
| `hint-ba-r2` | 170 | 111 | 3 / 3 | No early sample and only one ordinary length; fail |

Earlier build `d4ec91aa`, `trial-{ab,ba}-r{0,1}`, also remains as failed
evidence. B/A retained complete FCS-valid 170-byte snapshots, but the
status was sampled too late and already indicated terminal completion
(`0x4122`). They correctly failed early qualification; moving the status
read, not weakening the predicate, fixed that instrumentation problem.
The initial A/B trial captured no prefixes at all. These trials do not
support loss-free reception or a general receive-corruption fix.

The justified next control is bounded preservation plus a single
next-body advance, starting with the observed 478-byte case whose sizes
are consistent with 170/298-byte MPDUs. The second body's actual bytes,
FCS, boundaries and nonduplicate identity must be demonstrated under the
same fresh prefix before claiming two-MPDU delivery. Existing fixed-fixture
advance code is prior art, not yet a live variable-length implementation.
Production admission, bad-subframe handling, BA/reorder/key lifecycle and
physical SIFS qualification remain later gates.

## Footprint and validation

Current native-validated build sizes (`llvm-size`):

| Image | Text | Data | BSS |
| --- | ---: | ---: | ---: |
| Vendor RP2350 | 244272 | 28 | 40004 |
| Observer RP2350 | 75520 | 28 | 23216 |
| Included clean controller | 61364 | 0 | 6408 |

The observer already includes the clean controller blob; do not add its
size a second time. The vendor image includes the unchanged vendor assets.
All three ELFs have no undefined symbols. Neither RP2350 ELF contains TFT,
clean station, clean WPA2 or previous BA application symbols.

Validation includes ASan/UBSan fixture/digest/counter/HT-SIG tests, captured
RF HT-SIG vectors, PSM observer tests, default-controller regression and
original-image reconstruction identity, plus runner syntax and whitespace.
The previous uncorrelated reference ended with host-only rebuild `f8916d0c`.
Correlation build `4e209642`, exit/first-hint build `7f1e3e90` and early
snapshot build `e9008554` were radio-tested in both roles, with their
different success criteria and qualification limits recorded above.
Final tests also cover the default profile and the exit-only observer
with early preservation disabled. The final radio build ID remains
`e9008554` after the native race test.

## Preservation and next investigation

Fresh full backups are `build/clean-rx/vendor-burst/original-{A,B}.bin`,
each 16777216 bytes, independently matched to the previously restored images.
Restore each serial separately using:

```sh
build/picotool-usb/picotool load --ignore-partitions -v ORIGINAL.bin \
    -t bin -o 0x10000000 --ser SERIAL
build/picotool-usb/picotool save -a RESTORED.bin -t bin --ser SERIAL
cmp ORIGINAL.bin RESTORED.bin
```

Leave BOOTSEL after the independent full reread/compare. Original SHA-256:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

After this campaign both complete originals were restored, independently
reread as `restored-{A,B}-full.bin`, byte-compared and hash-checked against
the backups. Both devices were left in BOOTSEL.

Fresh backups for the correlation follow-up are
`build/clean-rx/vendor-correlate/original-{A,B}.bin`, each independently
matched to the prior restored originals. They have the same hashes above.
After the follow-up both complete flashes were restored, independently
reread as `build/clean-rx/vendor-correlate/restored-{A,B}-full.bin`,
byte-compared and SHA-256 matched to those backups. Each reread contains
16777216 bytes. Both devices were left in BOOTSEL.

Fresh backups for the exit investigation are
`build/clean-rx/vendor-exits/original-{A,B}.bin`, independently matched to
the prior restored originals and using the same hashes above.
Both complete originals were restored after the trials, independently
reread as `build/clean-rx/vendor-exits/restored-{A,B}-full.bin`, byte-compared
and SHA-256 matched. Every backup and reread is 16777216 bytes. Both badges
were left in BOOTSEL.

Fresh prefix-to-MAC-to-complete-variable-length-MPDU correlation and
explicit larger-prefix abort observations are now demonstrated, together
with one complete early-event MPDU from a larger live PPDU. Repeating
that capture and qualifying a bounded next-frame handoff remain the next
D2 investigations. Admission-derived BA and agreement/key/reorder lifecycle
integration remain later gates. Do not enable clean negotiated HT from
these results.

For the early-preservation campaign, fresh full backups are
`build/clean-rx/vendor-early/original-{A,B}.bin`, independently compared
with the prior restored originals before experimental writes. Both full
originals were restored afterward and independently reread as
`build/clean-rx/vendor-early/restored-{A,B}-full.bin`. All four files are
16777216 bytes; byte comparisons and SHA-256 match the original hashes
above. Both badges are left in BOOTSEL.
