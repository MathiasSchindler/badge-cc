# Next-MPDU and aggregate-terminal interface investigation

## Current result

**Partial multi-MPDU radio capture is now demonstrated.** An isolated,
default-disabled PSM path retained two distinct, fresh, full-FCS MPDUs
from one three-MPDU PPDU. Three final B -> A MCS0 trials reproduced this.
This advances the earlier [first-MPDU investigation](AMPDU_RADIO_INVESTIGATION.md).

The third capture is not a valid complete MPDU: its first four MAC bytes
are missing. Its remaining 76 bytes exactly match sequence 2, including
that MPDU's expected FCS. This is a leading-byte/boundary problem, not
evidence that the last four FCS bytes were missing.

MCS7 retained only sequence 0 in the final trial; the reverse direction
captured neither rate. No Immediate BA was transmitted or timed. There is
no production multi-MPDU credit/DMA/station integration, and negotiated HT
remains disabled. Both endpoints use clean firmware; this is not an
independent-vendor or RF-analyzer qualification.

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

Profile 3 only admits the pinned 252-byte/80-byte diagnostic:

1. Observe event 6, ready bit 8 and RX count 86, or fall back to the
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

1. Trace the last-frame prefix/body handoff and copy ownership around the
   four leading bytes. Distinguish a late capture/copy window from a
   different last-MPDU FIFO/header convention; neither cause is proven.
2. Verify the completion/terminal handshake across variable MPDU sizes,
   bad FCS, delimiter errors, truncation and abort, ideally with an
   independent aggregate source/capture.
3. Replace the fixed diagnostic slots with bounded production retained
   credits and DMA delivery before admitting frames to a hardware BA bitmap.
4. Install atomic agreement state and implement/measure real SIFS-timed
   compressed BA at qualified EOF/BAR events. Then integrate station
   lifecycle, reordering/CCMP and negotiate HT last.
