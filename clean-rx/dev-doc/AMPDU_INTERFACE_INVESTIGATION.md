# Next-MPDU and aggregate-terminal interface investigation

## Current result

**Complete finite three-MPDU radio capture is now demonstrated at MCS0..7
in both directions.** The 2026-10-18 handoff campaign fixes the leading-byte
loss by continuing body copies within the PPDU, instead of repeating the
PLCP-prefix/body split. Bad per-MPDU FCS no longer stalls until EOF.

The final normal campaign retained all three fresh, full-FCS MPDUs in
17/21 one-PPDU trials. Four trials captured nothing; repeats demonstrate
every MCS/direction cell, not loss-free reception. Controlled first,
middle and last bad-FCS tests reject invalid MPDUs and retain later valid
ones. Additional RF corruption is also rejected.

No Immediate BA was transmitted or timed. There is
no production multi-MPDU credit/DMA/station integration, and negotiated HT
remains disabled. Both endpoints use clean firmware; this is not an
independent-vendor or RF-analyzer qualification.

## Retained SHM-to-F2 delivery (2026-10-19)

**All three complete fixture MPDUs now reach the RP2350 through the real
SRAM/F2 receive path.** This is an alternative to the still-incomplete
per-MPDU DMA publication experiment below, not a claimed DMA/EOF fix.
The default-disabled `CLEAN_RX_AMPDU_SHM=1` gate requires probe/interface-3/
handoff-2 and is mutually exclusive with `CLEAN_RX_AMPDU_DMA`.
It remains a one-PPDU, three-by-80-byte diagnostic, not a station BA
recipient or negotiated HT.

### Ownership and validation

The unchanged PSM first retains the three actual radio bodies through its
already-proven direct-body handoff. When the controller observes a held
receive, it pauses the PSM and verifies the paused control readback before
copying all committed snapshot words and the three sampled good/bad
outcomes into private controller SRAM. It never reads this record through
the object window while the PSM is running.

Each retained MPDU is independently checked for bounds, the complete
aggregate HT-SIG, sampled outcome and full MAC/FCS before entering an
eight-slot receive FIFO. Bad outcomes are not delivered. Invalid metadata
or a good outcome with invalid bytes produces an explicit controller
error. Pending records survive FIFO backpressure; queued frames survive
SDIO backpressure. Ordinary receives in this build use the same FIFO, so
direct publication cannot skip retained aggregate records.

The ordinary held aggregate frame is reclaimed without a second
publication. Acquisition is checked at the held-frame dispatch boundary,
not just before polling: otherwise the last frame can become held between
the early check and ordinary dispatch and be delivered twice. The initial
`6a644ab8` trial demonstrated all three bytesets but counted that extra
delivery. The corrected final build `b1a19678` reports exactly three
deliveries and zero duplicates.

The badge/collector use the existing version-5 diagnostic metadata and
full saved-payload evidence, with actual USB-capacity assertions and
beacon-inventory exclusion shared by both delivery paths.
`shm_delivery_enabled` distinguishes the new path from
`dma_experiment_enabled`; the latter is false here. No payload or FCS is
generated on receive.

### Final normal radio matrix

Normal build `b1a19678`, prefixes
`build/clean-rx/badge/d2-shm-final-{ab,ba}-m{0..7}-r0`:

- **16/16 fresh one-PPDU trials deliver sequences 0, 1 and 2.**
- Every MCS0..7/direction cell passes on its initial trial.
- All have independently collector-validated delivery bitmap 7,
  exactly three delivered MPDUs, zero duplicates and zero endpoint errors.
- No Immediate BA is transmitted; BA, SIFS and HT qualification stay false.

This is a finite matrix, not sustained reception, independent-vendor
interoperability, arbitrary MPDU lengths or receive-reliability
qualification. The SHM record has three fixed slots and is consumed once
per firmware boot. It does not install agreements or connect the station
reorder/security lifecycle.

### Fault controls and independent byte proof

Final fault prefixes are `d2-shm-final-bad{1,2,4}-{ab,ba}-m{0,7}-r0`,
plus `d2-shm-final-bad4-ba-m7-r1`. The mask selects the deliberately bad
first, middle or last FCS:

| Build | Bad FCS mask | Successful controls | Delivered sequences |
| --- | --- | --- | --- |
| `760e2b9e` | 1 | 4/4 initial MCS0/7 direction trials | 1, 2 |
| `3bb1f277` | 2 | 4/4 initial MCS0/7 direction trials | 0, 2 |
| `dfba0bc9` | 4 | 3/4 initial trials; fresh reverse-MCS7 repeat passes | 0, 1 |

The failed reverse-MCS7 `bad4` trial retained one bad/nonmatching raw
observation and delivered nothing; it is not discarded or promoted to
a successful rejection control. All twelve successful controls deliver
exactly two MPDUs, with zero duplicates and endpoint errors.

All-bad build `e91d443e`, mask 7, uses
`d2-shm-final-bad7-{ab,ba}-m0-r0` plus reverse `r1`. Forward `r0` and
reverse `r1` each retain all three intentionally invalid bodies and
deliver none. Reverse `r0` observes none and is retained as another failed
reception, not counted as proof of rejecting received data. These
negative controls return failure from the accepted-data pair runner and
do not generate a success-shaped pair summary.

An independent Node pass checks **32 unique challenges**, including the
two failed initial receptions. It reconstructs every expected byte and
standard reflected CRC32 independently of the firmware/collector helpers.
It verifies **72 complete saved SDIO MPDUs**, exactly one delivery per
accepted sequence, and **90 full retained radio MPDUs** from the completed
normal/fault controls, including the exact 18 intentionally invalid FCS
values. It also checks sampled good/bad outcomes, builds, endpoint errors,
all delivery flags and pair output. The failed trial's partial raw record
is kept separate from those 90 verified bodies.
Results: `build/clean-rx/badge/d2-shm-evidence-assertions.log`.

Acquisition still requires a held receive boundary. A bad final MPDU
or all-bad aggregate can therefore wait for a later ordinary held frame
before the controller copies its committed record. This is an explicit
latency limitation, not bounded EOF-to-delivery qualification. Immediate
BA must use PSM-side receipt/retention state and an EOF/SIFS response path;
it cannot wait for this deferred Cortex/SDIO delivery.

### Native checks

Default, capture-only handoff, DMA, SHM and SHM bad-mask 1/2/4/7
controller/badge suites pass, including ASan/UBSan. The final focused HT
suite passes in all eight profiles, including every MPDU/signature bit
flip, all three indices, invalid counts/indices/length/format/magic,
sampled bad outcomes and unchanged output on rejection. All sixteen
freestanding ELFs have no undefined symbols.
Negative production compilations reject simultaneous DMA/SHM delivery
and an undersized 1,024-byte receipt report. The existing FIFO's native
full-queue, retained ownership, credit and counter-wrap tests also pass.

### Reproduction and rejected DMA changes

Build controller and badge with matching flags:

```sh
flags='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3 -DCLEAN_RX_AMPDU_HANDOFF=2 -DCLEAN_RX_AMPDU_SHM=1'
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-shm EXTRA_CFLAGS="$flags" all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-shm CONTROLLER_BIN=../../build/clean-rx-d2-shm/cyw43_rx_fw.bin EXTRA_CFLAGS="$flags" all test
```

After fresh full-flash preservation and verified installation, use
`CLEAN_RX_COLLECTOR=build/clean-rx/badge/d2-shm/collector` with the existing
`ht_probe.sh TX RX PREFIX MCS --ampdu-probe` command. Restore and independently
read back both complete original flashes afterwards.

The preceding DMA probes are retained as `d2-eof-{stream,early,eoi,arm,progress}-*`.
Keeping streaming across header publication, starting it after the next
copy arm, and starting on apparent in-progress counts did not repair the
DMA path. They produced stale/invalid second completions within roughly
4..9 us of the first, rather than the expected next radio MPDU.
Explicit event-6 clearing did not fix this. All these PSM changes were
removed. The default, handoff and DMA PSM instruction images are unchanged.
The new SHM build's PSM object is byte-identical to the previous
capture-only handoff object.

### Hardware preservation

Fresh complete `d2-eof-start-{A,B}-full.bin` reads were compared with the
previous independently verified originals before either badge was written.
After all normal/fault controls, both complete original flashes were
restored with verification and independently read back as
`d2-shm-restored-{A,B}-full.bin`. Both 16,777,216-byte readbacks compare
byte-for-byte with the fresh originals:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

Logs and all successful/failed RF evidence are retained under
`build/clean-rx/badge/`, including `d2-eof-preservation.log` and
`d2-shm-restoration.log`. Both badges remain in BOOTSEL with their
original contents. Original applications were not booted, and no
credentials or host networking were changed.

## Per-MPDU DMA-to-F2 delivery follow-up

**The requested production aggregate/Immediate-BA integration is not
complete.** A separate, default-disabled `CLEAN_RX_AMPDU_DMA=1` experiment
now publishes individual MPDUs through the real D11 DMA ring, controller
SRAM FIFO, SDIO/F2 transport and RP2350 receive dispatcher. It requires
the existing probe, interface 3 and handoff 2 gates; it is still restricted
to the three-by-80-byte fixture. It does not install a BA agreement,
transmit BA, implement mandatory protection, or enable HT association.

### Implemented path and hardware findings

- At each retained per-MPDU completion, preserve the good/bad decision in
  the DMA header before advancing. Explicitly start RX-buffer streaming
  even for these short MPDUs; the ordinary 128-byte streaming threshold
  otherwise leaves a header-only DMA buffer with zero payload.
- Publish the ordinary receive header without entering the Cortex-held
  capture state between MPDUs. After header publication, restore the
  existing receive context and continue the direct body copy, bounded
  by the fixture's three records and observed terminal condition.
- Header word 1 carries the sampled outcome, word 9 the ordinal. The
  physical first body has six PLCP bytes followed by its 80-byte MPDU.
  Continuation DMA bodies start directly with their MAC header. Their
  physical header length is therefore 80, rather than the RXE logical
  count of 86. The first body's PLCP must match the retained header;
  later normalized transport prefixes copy those actual retained bytes.
  No MAC bytes or FCS are synthesized.
- The controller validates header identity, ordinal, length, complete
  HT-SIG/CRC and the entire MPDU/FCS before publishing an SRAM slot.
  Bad-FCS outcomes are reclaimed without F2 delivery. Invalid metadata,
  mismatched first-body PLCP and invalid FCS produce explicit controller
  errors `0x8d`, `0x8f` and `0x8e`, respectively.
- Validated frames remain in the DMA ring when the eight-slot controller
  FIFO is full, and in SRAM until F2 accepts them. Normal single-frame
  publication and the experiment share consistent published-frame
  accounting. This is not a general PSM admission/credit protocol.
- Early intermediate PHY format can still be 1; terminal format is 2.
  Only this gated, exact aggregate signature admits that early form.
  F2 version 5 explicitly identifies the diagnostic, retains the
  unsupported-legacy-consumer flag, and derives its HT identity from the
  complete validated aggregate HT-SIG. Raw PHY observations are not
  rewritten. Ordinary nonaggregate version-2 validation is unchanged.

The RP2350 validates the received SDIO MPDU's metadata, complete challenge,
sequence, payload and FCS. It separately saves each delivered 80-byte MPDU
at detail offsets `1664 + 80 * sequence` in the actual 2,048-byte USB report.
Only this diagnostic receiver disables its unrelated beacon inventory,
whose storage is reserved for the delivered payloads. Both badge and host
assert the record against the actual USB trace capacity. The host collector independently
reconstructs the delivery bitmap from these bytes, rather than accepting
an observation count or the RP2350 bitmap alone. Duplicate or stale
sequence-zero copies cannot satisfy sequence one or two.

Reports distinguish `multi_mpdu_radio_capture`,
`reported_delivered_subframe_bitmap`, `delivered_subframe_bitmap`,
`delivery_payloads_valid`, `actual_multi_mpdu_delivery` (at least two
distinct complete delivered MPDUs), and `all_subframes_delivered`
(all three). Immediate BA, SIFS qualification and HT qualification remain
explicitly false in both endpoint and pair output.

### Radio evidence and remaining failure

The initial final delivery matrix, build `ea86b5cd`, used fresh challenges
at MCS0, MCS3 and MCS7 in both directions. Prefixes under
`build/clean-rx/badge/` are `d2-live-mac-delivery-{ab,ba}-m{0,3,7}-r0`.
All six delivered sequences 0 and 1, with bitmap 3 and no controller
error. **None delivered sequence 2.** An additional record can occur
later with bad-FCS status and stale sequence-one bytes; it is not a third
valid receipt and is not published.

The bad-middle controls, build `da868164`, are
`d2-live-mac-bad-{ab,ba}-m0-r0`. Both delivered only sequence zero,
reported the middle MPDU bad, and had no controller error. They prove
rejection, not successful continuation to the missing final MPDU.

The final self-contained byte-evidence campaign uses normal build
`c74df883` and bad-middle build `e5cce64e`. Its prefixes are
`d2-live-mac-verified-{ab,ba}-m{0,3,7}-r0`,
`d2-live-mac-verified-ba-m7-r1`, and
`d2-live-mac-verified-bad-{ab,ba}-m0-r0`.

| Final control | Trials | Independently validated delivery |
| --- | --- | --- |
| Normal MCS0, both directions | 2 | Sequences 0/1 in both |
| Normal MCS3, both directions | 2 | Sequences 0/1 in both |
| Normal MCS7, both directions, including repeat | 3 | Sequences 0/1 in two; reverse `r0` captured/delivered none |
| Bad middle, MCS0, both directions | 2 | Sequence 0 only; middle rejected |

Thus **6/7 normal trials deliver two distinct MPDUs, 0/7 deliver all
three**, and both bad-middle controls reject the middle MPDU. The failed
reverse-MCS7 trial is retained, returns failure from the pair runner, and
has no success-shaped pair summary. The fresh repeat demonstrates the
cell, not loss-free operation.

An independent Node assertion pass reconstructs all expected MAC bytes
and IEEE FCS from each fresh challenge, compares the actual saved SDIO
payload records, checks both endpoint build/error status, and verifies
the endpoint/pair flags. It checks **14 full delivered MPDUs across nine
trials**. Results are in
`build/clean-rx/badge/d2-live-mac-evidence-assertions.log`.
The independent check also rejected an initial out-of-report
payload-evidence placement (`d2-live-mac-proof-*`); that placement was
removed. Final storage uses the actual 2,048-byte USB ABI and has
compile-time checks on badge and host. A negative compilation test
rejects a 1,024-byte production collector trace.

The final PSM publication delta in these captures is about 3 us from the
retained observation timestamp. This is **not** an EOF-to-air response
measurement, does not include every preceding operation, and cannot
qualify SIFS.

Earlier failed experiments remain under `d2-live-mac-dma-*`. They include
header-only DMA, the continuation's missing PLCP, an initially incorrect
mixed publication counter, misses, and an unsuccessful attempt to bypass
the receive crypto-reset sequence. That bypass produced invalid payloads
and was removed. Neither changing header length nor restoring the
receive-context bit established final-MPDU delivery; do not attribute
the remaining loss to either change as a demonstrated cause.

The next hardware prerequisite is preserving the last MPDU through the
DMA/header/EOF transition without a stale completion, followed by general
lengths, bounded admission and abort/BAR recovery. A BA bitmap must not
acknowledge this missing frame. Controller agreement installation, PSM
compressed BA transmission and measured SIFS, station ADDBA/DELBA/key
lifecycle wiring, mandatory receive-size and protection qualification
all remain open. The existing policy commit barrier does not supply them.

### Native validation and reproduction

Default, existing handoff, DMA and bad-middle controller suites pass;
default, DMA and bad-middle badge suites pass, including ASan/UBSan.
Tests exercise first/continuation layout, every single-bit corruption in
each 80-byte MPDU, all HT-SIG and magic bits, every truncated DMA body,
bad-FCS suppression, atomic invalid-output preservation, independent
delivery-payload validation, header-length selection, publication bounds
and counter wrap. Existing default and handoff PSM objects remain
byte-identical to the preceding policy campaign.

```sh
FLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3 -DCLEAN_RX_AMPDU_HANDOFF=2 -DCLEAN_RX_AMPDU_DMA=1'
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-live-mac \
  EXTRA_CFLAGS="$FLAGS" all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-live-mac \
  CONTROLLER_BIN=../../build/clean-rx-d2-live-mac/cyw43_rx_fw.bin \
  EXTRA_CFLAGS="$FLAGS" all test
```

After fresh verified full-flash backups and deliberate installation, use
the existing `--ampdu-probe` pair command with this matching collector.
Add `-DCLEAN_RX_AMPDU_BAD_FCS=2` to both builds for the bad-middle control.
Always restore and independently compare both complete original flashes.

### DMA campaign preservation

Fresh `d2-live-mac-start-{A,B}-full.bin` originals were independently
read twice before testing. Full-range verified restoration and separate
complete readbacks passed both after the initial delivery campaign and
after the final byte-evidence campaign. The final readbacks are
`d2-live-mac-proof-restored-{A,B}-full.bin`; both are 16,777,216 bytes and
compare byte-for-byte with the start originals. Their SHA256 hashes are:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

The final restore log is `d2-live-mac-proof-restoration.log` under
`build/clean-rx/badge/`. Both badges remain in BOOTSEL with their original
flash contents; original applications were not booted. Originals,
readbacks, positive evidence and failed RF experiments are retained.

## Next/last-frame handoff campaign (2026-10-18)

### Locating and fixing the loss

`CLEAN_RX_AMPDU_HANDOFF=1`, with interface profile 3, records state after
the original `SPR02b` read and before body-copy arm. It preserves the
original store and copies the already-read r2 value, without rereading a
potentially side-effecting FIFO register. Each one-shot record holds TSF,
RX count, SHM base, copy end/control, FIFO control, actual RDF and status2,
followed by the first 24 SHM bytes.

Build `ac656c81`, prefix `d2-handoff-prefix-ba-m0-r0`, reproduced the old
two-good/short-third result:

| Prefix ordinal | Elapsed us | RX count | Requested copy end | Actual RDF |
| --- | --- | --- | --- | --- |
| 0 | 1 | 6 | 6 | 80 |
| 1 | 105 | 6 | 6 | 80 |
| 2 | 215 | 10 | 6 | 80 |

SHM base stays `0x800`, copy control `0x0800` and FIFO control `0x0c02`.
The last prefix reaches count 10 despite the six-byte request; actual RDF
is still **80**, not 76. The subsequent short body contains expected MAC
bytes 4..79. The failing software sequence is reentering the prefix/body
split inside the aggregate, not transmitting a shorter frame or omitting
trailing FCS. The internal FIFO/copy reason for the prefix overrun remains
undocumented; the fix does not invent a register bit definition.

`CLEAN_RX_AMPDU_HANDOFF=2` requires interface profile 3 and implements:

1. Retain the complete current raw slot before advancing.
2. Keep the PPDU's captured HT-SIG, reset the SHM base, arm an 80-byte body
   copy with the existing body-copy operation, reset the bounded wait
   budget and enter the ordinary body-wait path.
3. Do not issue another prefix copy, Cortex hold or whole-RX flush between
   MPDUs. Stop at three retained snapshots or the terminal condition.

Build `9512d93d` first retained all three MPDUs, including MCS7 in both
directions. A reverse MCS0 trial exposed another failure: a corrupted
middle snapshot waited until EOF and was classified using later GoodFCS
status. Independent byte/FCS validation correctly rejected that snapshot.

The hardened implementation consumes a fresh body-completion event at
count 86 without requiring status1 bit 8. Controlled faults demonstrate
bit 8 set for good MPDUs and clear for bad ones: it is **not a general
completion-ready gate**. GoodFCS is checked immediately after event-6 EOI.
Full-sized good/bad diagnostic snapshots carry outcomes 1/2 and both may
advance while capacity remains. Invalid bytes never enter the collector's
receipt bitmap. Ordinary nonfixture failure handling is unchanged.
The host validator also preserves earlier valid slots when the last
retained observation is early/bad rather than terminal; it still validates
each complete expected MPDU/FCS independently, never the snapshot count.

### Final RF matrix

Prefixes are relative to `build/clean-rx/badge/`. Each trial sends exactly
one PPDU with a fresh challenge. Final normal build: `913cea35`.

| Direction | MCS | `d2-handoff-robust-*` result |
| --- | --- | --- |
| B -> A | 0..6 | `ba-m{0..6}-r0`: all three, bitmap 7 |
| B -> A | 7 | r0 no capture; r1/r2 all three |
| A -> B | 0..4, 6 | `ab-m{0..4,6}-r0`: all three |
| A -> B | 5 | r0 no capture; r1 all three |
| A -> B | 7 | r0/r2 no capture; r1 all three |

At MCS0 the three good completions are around 103/213/283 us after prefix;
at MCS7, around 12/24/31 us. All RX counts are 86, with first/second
status1 `0x1922` and final `0x4122`. Raw PHY progresses from 1 to terminal
2. All 80 bytes of every accepted MPDU are exact, including the formerly
missing four leading bytes. No bytes are synthesized or reconstructed.

`CLEAN_RX_AMPDU_BAD_FCS` is a compile-time three-bit fixture fault mask,
default 0, permitted only with handoff profile 2. Selected MPDUs have their
last FCS byte toggled after correct construction. Payloads, delimiters
and lengths are unchanged. Command validation accepts only that exact
build's fixture, never arbitrary malformed transmissions.

| Build | Fault | Prefixes/results |
| --- | --- | --- |
| `ac3d8e54` | First, mask 1 | `d2-handoff-bad-1-ba-m{0,7}-r0`: bitmap 6, outcomes 2/1/1 |
| `8e6bbefb` | Middle, mask 2 | `d2-handoff-bad-middle-ba-m0-r{1,2}`, `ba-m7-r0`, `ab-m7-r0`: bitmap 5, outcomes 1/2/1; `ba-m0-r0` captured nothing |
| `376ed250` | Last, mask 4 | `d2-handoff-bad-4-ba-m0-r0`, `ba-m7-r1`: bitmap 3, outcomes 1/1/2 |
| `376ed250` | Last plus RF error | `ba-m7-r0`: bitmap 2, outcomes 2/1/2; first MPDU also fails software FCS |

Early bad status1 is `0x1822`; final bad status1 is `0x4022`. Bit 14 still
belongs to the final capture even when its FCS is bad. This strengthens
the terminal-event evidence for this fixture; it does not qualify
truncation, delimiter loss, PHY aborts, variable lengths or SIFS timing.

Independent assertions over **31 final normal/fault trials** check fresh
challenges, every expected byte, CRC-32, outcomes, count 86, copy end 80,
terminal-bit placement and exact bitmaps. Normal: 17/21 complete and four
no-capture. Faults: 9/10 captured, with the extra RF error explicitly
rejected. Failed trials are retained. The assertion output is
`d2-handoff-evidence-assertions.log`.

### Layout, reproduction and validation

`ADP2`/`AIF2` sizes are unchanged. Handoff-profile-1 records use unused
early-interface SHM words 68..127, 20 words each, with commit words
128..130; the collector exports `interface.handoff`. Passive profiles
never share this instrumentation. Current early-interface row word 7
stores MPDU outcome; passive rows retain their event mask. Older early
rows used zero. Reports expose `fixture_bad_fcs_mask`; count 3 includes
bad snapshots and is not equivalent to bitmap 7.

```
FLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3 -DCLEAN_RX_AMPDU_HANDOFF=2'
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-handoff-robust EXTRA_CFLAGS="$FLAGS" all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-handoff-robust \
  CONTROLLER_BIN=../../build/clean-rx-d2-handoff-robust/cyw43_rx_fw.bin \
  EXTRA_CFLAGS="$FLAGS" all test
```

After fresh full-flash backups and installation of matching images:

```
CLEAN_RX_COLLECTOR=build/clean-rx/badge/d2-handoff-robust/collector \
  bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 7 --ampdu-probe
```

Use separate build directories and add `-DCLEAN_RX_AMPDU_BAD_FCS=1`, `=2`
or `=4` for fault positions. Handoff profile 1 reproduces the instrumented
old prefix transition; profile 2 fixes it. All gates default to zero.

Ten controller/badge build and native-test profiles passed: default,
HT/response/EDCA/WMM, passive post/body, original early capture, prefix
instrumentation, hardened continuation and each individual fault
position. Sanitizer-enabled suites are included. Additional HT/fixture
sanitizer tests cover masks 3/5/6/7, including all-invalid MPDUs. Tests cover
original PC153 preservation, one-shot SHM-only snapshots, direct body arm,
wait-budget reset, fresh bad completion without bit 8, bad-slot retention
and nonfixture fallback. Default baseline-image identity still passes.
After the RF matrix, a host-validator-only corner case was added for
early/bad last metadata, plus its native test. Controller image byte
identity is checked against the RF builds; no radio/PSM behavior changed.

Fresh `d2-handoff-start-{A,B}-full.bin` originals and independent
`d2-handoff-restored-{A,B}-full.bin` readbacks are all 16,777,216 bytes.
Full-range verified restores and byte comparisons passed on both badges.
Hashes match the originals listed in the preservation section below.
One image-install attempt encountered USB re-enumeration; waiting for
BOOTSEL readiness and retrying succeeded. Its failed log is retained.

### D2 boundary

This fixes the **finite diagnostic next/last-frame byte-loss problem**,
not D2 as a whole. The pinned 80-byte/three-slot path is not general
station reception. Variable-length ownership and retained credits,
multi-MPDU DMA delivery, atomic agreements, station lifecycle/CCMP/reorder
integration, protection and SIFS-timed compressed BA remain unimplemented
or unqualified. No response is emitted; negotiated HT remains disabled.

## Earlier prefix-continuation campaign

The following sections preserve the earlier two-MPDU results. Their
short-third-frame limit applies to the original prefix continuation,
not the new handoff-profile-2 body continuation.

## Why the old continuation attempts were too late

The existing `RX_COMPLETE` path requires both external event 6 and
status1 bit 14 before consuming the completion and checking GoodFCS.
The terminal HT trace also requires PHY format 2.

Passive PSM timelines now show these are different stages for this
aggregate:

| MCS0 observation, relative to prefix timestamp | RX count | Status1 | Status2 | Nonclearing events |
| --- | --- | --- | --- | --- |
| 1 us | 10 | `0x1822` | `0x0a00` | 0 |
| 97 us | 82 | `0x1822` | `0x0a00` | 0 |
| 129 us | 86 | `0x1922` | `0x0a41` | `0x0048` |
| 225 us | 86 | `0x1922` | `0x0a42` | `0x0048` |
| 289 us | 86 | `0x4122` | `0x0a42` | `0x0048` |

`0x0048` means events 3 and 6. Event 6 plus status1 bit 8 becomes available
well before bit 14. The old path does not process that earlier completion.
Post-completion snapshots begin 283 us after prefix at MCS0 but 30 us at
MCS7; the body timeline brackets the later bit-14 transition at MCS0
between 257 and 289 us. These observations distinguish a per-MPDU
opportunity from the later PPDU-terminal latch for this fixture.

There is a second gating problem: actual early captures report raw PHY
format **1**, even though their retained HT-SIG, complete MPDU bytes,
fresh challenge and FCS match. Requiring the eventual terminal format 2
discards these early captures. The initial early-completion experiment
therefore retained nothing until its diagnostic capture entry stopped
requiring terminal PHY metadata. Raw PHY values are preserved, not
rewritten to pretend terminal HT classification is already available.

The bit-14 transition is a useful aggregate-terminal candidate, **not yet
a generally qualified EOF/SIFS trigger**. Different sizes, loss, delimiter
errors, truncation and PHY aborts still need independent verification.

## Implemented diagnostic profiles

`CLEAN_RX_AMPDU_INTERFACE` defaults to 0 and requires the finite
`CLEAN_RX_AMPDU_PROBE=1`, HT-data and extended-RX gates:

| Value | Behavior |
| --- | --- |
| 0 | Existing finite probe, no interface instrumentation |
| 1 | Passive timeline after the original terminal capture |
| 2 | Passive timeline after body-copy arm, before the legacy completion wait |
| 3 | Early fixed-fixture MPDU completion, bounded retention and next-prefix advance |

Profiles 1/2 take sixteen samples nominally 32 us apart, without changing
RX control or clearing events. They use a bounded clock-wait budget.
A stuck clock marks the mode's high bit and returns to the original
path, rather than looping indefinitely. Profile 2 deliberately pauses
normal PSM receive processing while sampling; it is instrumentation, not
a working live receive path.

Original profile 3 without the newer handoff gate admits the pinned
252-byte/80-byte diagnostic:

1. Observe event 6, good-only bit 8 and RX count 86, or fall back to the
   original bit-14 terminal check.
2. Consume the completion through the existing event-6 EOI.
3. Require GoodFCS event 3. Preserve actual PHY status, bypassing the
   terminal-format-only trace filter for this pinned diagnostic entry.
4. Copy the complete raw frame to one of three fixed SHM slots, then
   commit its count. No positive capture depends solely on hardware flags:
   the collector independently validates all expected bytes and FCS.
5. Before the aggregate-terminal bit, reset the SHM capture pointer and
   enter the ordinary prefix arm without Cortex hold or whole-RX flush.
   Stop at three snapshots, bad FCS or the terminal condition.

This sequence successfully advances to sequence 1 at MCS0. It does not
establish a lossless general next-MPDU interface. The late/short sequence-2
capture still fails the ordinary FCS path and is not admitted to the valid
receipt bitmap. It is retained separately for diagnosis.

### Evidence format

The existing `ADP2` three-slot record is unchanged. New `AIF2`
(`0x32464941`) evidence occupies 182 SHM words starting at `0x200`.
After RX stops, it is copied to SRAM `0x20800`, with its magic committed
last, then to badge detail offset 1104. Total exported size is 368 bytes.
It replaces saved-packet evidence only in the aggregate diagnostic modes.

- Four header words: mode, committed record count, prefix TSF, prefix
  RX count.
- Sixteen eight-word rows: TSF, RX count, status1, status2, FIFO control
  (`SPR024`), copy end (`SPR02a`), copy control (`SPR004`), event mask.
- Six tail words: RX count, status1, status2, PHY, TSF and outcome.
- An 88-byte raw tail snapshot.

Profiles 1/2 fill the timed rows; profile 3 fills up to three completion
rows. Its row event field is unused/zero. Event masks in passive profiles
are sequential nonclearing observations, not an atomic hardware latch.
TSF subtraction wraps at 16 bits. Early `AIF1` captures in this campaign
have only the header/rows and must be read using their named build layout.

Reports distinguish:

- `multi_mpdu_radio_capture:true`: at least two different complete,
  challenge-matching fixture MPDUs, independently including FCS;
- `retained_subframe_bitmap:3`: sequences 0 and 1, **not** sequence 2;
- `all_subframes_retained:false`: the complete three-MPDU criterion failed;
- `actual_multi_mpdu_delivery:false`: no live station/transport integration;
- `terminal_aggregate_sig:false` can coexist with valid early capture:
  raw terminal PHY classification has not yet arrived;
- `tail.matching_sequence2_shift4_bytes:76`: the short tail matches
  expected bytes 4..79 of sequence 2. It is not a complete valid frame.

The ordinary receiver's PHY validation and negotiated-HT fallback remain
unchanged. No BA agreement or response state is installed by any profile.

## RF record

A is `08DA26F86E57F5EA`; B is `9BBF88DD17D66E7D`.
Prefixes below are relative to `build/clean-rx/badge/`.

| Build | Prefix | Observation |
| --- | --- | --- |
| `087e6184` | `d2-interface-ba-m{0,7}-r0` | Passive post-terminal; 283/30-us prefix-to-sample delay |
| `afe81c7b` | `d2-interface-body-ba-m{0,7}-r0` | Passive body timeline; separate early completion and later terminal stages |
| `3c712d06` | `d2-interface-early-ba-m0-r0` | Early event selected, but terminal PHY filter discarded the capture |
| `b8d1bdaf` | `d2-interface-early2-ba-m0-r0` | First two full MPDUs, bitmap 3, early raw PHY 1 |
| `a5d51b4f` | `d2-interface-early3-ba-m{0,7}-r0` | Terminal fallback: two valid at MCS0; one valid at MCS7 |
| `3891a2e1` | `d2-interface-tail-ba-m0-r0` | Four-byte TXE trailer reservation did not improve capture; removed |
| `14031e7e` | `d2-interface-eof-ba-m0-r0` | Short third capture retained; exact 76-byte shifted suffix, including FCS |
| `43ec5b00` | `d2-interface-fifo-ba-m0-r0` | Early aggregate FIFO selection 1 instead of 2 gave the same four-byte loss; removed |
| `8e88761d` | `d2-interface-final-*` | Final six-trial matrix below |

The extra TXE trailer was an explicitly tested hypothesis, not a
retained fix. The PHY-advertised length was unchanged. The tail-byte
comparison subsequently established that the loss was at the **start**
of the MPDU, not its FCS. Switching FIFO selection also did not resolve
it. Do not reintroduce either change as a known-good advance sequence.

Final six one-PPDU trials, each with a fresh challenge:

| Prefix suffix | Direction / MCS | Valid bitmap | Third-frame result |
| --- | --- | --- | --- |
| `ba-m0-r0` | B -> A / 0 | 3 | RX count 82, exact shifted suffix 76 bytes |
| `ba-m0-r1` | B -> A / 0 | 3 | Same |
| `ba-m0-r2` | B -> A / 0 | 3 | Same |
| `ba-m7-r0` | B -> A / 7 | 1 | Second snapshot bad FCS; no valid third |
| `ab-m0-r0` | A -> B / 0 | 0 | No fresh aggregate capture |
| `ab-m7-r0` | A -> B / 7 | 0 | No fresh aggregate capture |

In the three positive MCS0 trials, full MPDU retention occurs at
103/213, 103/213 and 104/214 us after prefix. The short tail is observed
at 282, 282 and 283 us. Its status1 is `0x4000`, status2 `0x0a60`, PHY 2
and outcome 2 (bad FCS). Never pool this tail with the valid receipts.
These small diagnostic counts are not a reliability qualification.

## Reproduction and validation

For profile 3, from the repository root:

```sh
FLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3'
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-interface-verified \
  EXTRA_CFLAGS="$FLAGS" all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-interface-verified \
  CONTROLLER_BIN=../../build/clean-rx-d2-interface-verified/cyw43_rx_fw.bin \
  EXTRA_CFLAGS="$FLAGS" all test
```

Back up/verify both full flashes before deliberately installing matching
images. With both badges in BOOTSEL and an unused evidence prefix:

```sh
CLEAN_RX_COLLECTOR=build/clean-rx/badge/d2-interface-verified/collector \
  bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 --ampdu-probe
```

Use separate build directories and interface values 1/2 for passive
comparisons. Keep failed output and raw binaries. The runner's zero exit
status requires only a fresh capture; inspect the bitmap and
`all_subframes_retained` rather than equating it with a complete aggregate.

Default, HT/response/EDCA/WMM and all three interface profiles passed
controller/badge builds and native suites, including ASan/UBSan.
Tests cover unchanged baseline image identity, nonclearing passive events,
one-shot records, clock wrap/timeout, early completion before bit 14,
raw early PHY preservation, bad-tail copying, complete fixture/FCS checks
and gated early-PHY parser admission. Exact assertions over all six
final RX JSON records passed. Final probe ELFs have no undefined symbols.
Bash syntax and whitespace checks passed.

Final software-validation build is `1c33b7f3`, not reflashed. After the RF
matrix, only the collector's absent-tail elapsed-time display and a
redundant native-test assignment changed; PSM/controller behavior did not.
Final build logs are `d2-interface-{default,gated,post-verified,body-verified,verified}-{controller,badge}.log`.

## Hardware preservation

Fresh, verified full-flash backups were taken before this campaign:
`d2-interface-start-{A,B}-full.bin`. Both originals were restored with
full-range verified updates, followed by separate complete
`d2-interface-restored-{A,B}-full.bin` reads and byte-for-byte comparisons.
All four files are 16,777,216 bytes; readback hashes match the originals:

- A: `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628`
- B: `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2`

Both badges remain in BOOTSEL with their original flash contents. USB
preservation operations were serialized. No credentials, vendor/shared
applications or host networking were changed.

## Next D2 work

1. Generalize the demonstrated one-copy continuation to per-MPDU lengths
   and explicit bounded buffer ownership; do not turn the fixed diagnostic
   into production reception by merely removing its gates.
2. Verify the completion/terminal handshake across variable MPDU sizes,
   bad FCS, delimiter errors, truncation and abort, ideally with an
   independent aggregate source/capture.
3. Replace the fixed diagnostic slots with bounded production retained
   credits and DMA delivery before admitting frames to a hardware BA bitmap.
4. Install atomic agreement state and implement/measure real SIFS-timed
   compressed BA at qualified EOF/BAR events. Then integrate station
   lifecycle, reordering/CCMP and negotiate HT last.
