# Finite A-MPDU radio investigation

> Historical experiment record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../../README.md)
> for current guidance and work.

The follow-up [interface investigation](INTERFACE_AND_DELIVERY.md)
now demonstrates all three complete diagnostic MPDUs with the fixed
direct-body handoff. Its additional gated DMA/F2 experiment actually
delivers the first two to the RP2350 but still misses the last. A newer,
alternative retained-SHM/SRAM/F2 path delivers all three actual fixture
MPDUs at MCS0..7 in both directions, without duplicates in its finite
matrix; it does not fix that DMA transition. General station reception
and Immediate BA remain incomplete. The record below describes the
earlier, first-MPDU-only campaign and its evidence layouts.

## Initial campaign result and limits

**D2 remains incomplete.** The finite source below sends one HT20 PPDU
containing three independently checkable MPDUs. The receiver retained the
fresh first MPDU at MCS0 and MCS7. It did not retain the other two.
No Immediate Block ACK was transmitted or timed. Negotiated HT remains
disabled.

The important observed interface fact is that the leading delimiter is
already stripped before the MPDU reaches clean SHM. RXE reports 86 bytes:
six PLCP bytes plus the first 80-byte MPDU. Its retained HT-SIG advertises
the entire 252-byte aggregate. Consequently, feeding this capture to the
software delimiter parser cannot recover the missing subframes. A live
implementation needs the radio's deaggregated-MPDU interface, retained
credits, a verified next-MPDU transition and an end-of-aggregate event.

The existing single-capture hold/flush protocol is incompatible with that
requirement. However, removing the hold, including before header DMA,
did **not** recover the other MPDUs. The tested alternatives do not
establish the exact aggregate-advance or EOF semantics, or prove that the
hold is the sole cause. Those unsuccessful rearm paths were removed from
the source rather than promoted to a production feature.

This campaign used two independently running clean images, not an
independent vendor aggregate source or RF analyzer. It does not establish
the emitted waveform's complete standards compliance, interoperability,
BA timing, reliability, or reception of all three subframes.

## Retained diagnostic

The default-disabled `CLEAN_RX_AMPDU_PROBE=1` gate requires both
`CLEAN_RX_HT_DATA=1` and `CLEAN_RX_HT_RX=1`. It adds:

- API version 5, restricted to one submission of the exact fixed fixture,
  outside the live station mode. Ordinary HT validators still reject
  aggregation; this exception is not a general raw-aggregate TX API.
- Modes 30/31 and collector options `--ht-ampdu-probe-tx` /
  `--ht-ampdu-probe-rx`, taking a 28-hex-digit challenge and MCS0..7.
- Three 80-byte QoS MPDUs with sequences 0..2, TID0, different local
  unicast RA/TA, a shared fresh challenge, full payload identity and
  individual FCS. Each has a four-byte delimiter with its own CRC.
  HT-SIG sets the aggregation flag and advertises 252 bytes.
- A terminal-only, bounded PSM evidence record. It does not change the
  normal capture hold/flush protocol, install an agreement, or respond.

This is deliberately a diagnostic without ADDBA negotiation, not a
negotiated BA exchange. There is no continuous-transmit mode.

### Evidence layout

SHM word indices `0x100..0x18f` hold 144 words. After RX stops, the
controller copies these to SRAM `0x20600`, following a four-byte magic
committed last. The badge copies the 292-byte record to detail offset 768.

The current `ADP2` magic, `0x32504441`, distinguishes this layout from the
earlier single-snapshot `ADP1` layout:

| SHM offset, words | Contents |
| --- | --- |
| 0 | Committed snapshot count, capped at three |
| 1..7 | Latest outcome, RXE length, status1, status2, PHY, TSF low/high |
| 8..51 | Raw snapshot 0, 88 bytes |
| 52..95 | Raw snapshot 1, 88 bytes |
| 96..139 | Raw snapshot 2, 88 bytes |
| 140..142 | Individual status2 values |
| 143 | Reserved |

Each raw snapshot has two staging bytes, six HT-SIG bytes and an 80-byte
MPDU. Capture filtering requires terminal HT format, the aggregation flag,
advertised length 252 and RXE length 86. Slots are committed only after
their bytes and status are copied. Good- and bad-FCS terminal paths are
observable; the collector independently checks complete expected bytes,
including FCS, rather than trusting the observation count.

The collector checks magic/version, count bounds, RXE length, PHY,
per-slot HT-SIG, MCS, sequence, challenge, payload and FCS.
`retained_subframe_bitmap` describes validated fixture sequence numbers,
not the number of snapshots. Repeated copies of sequence zero remain
bitmap 1, never bitmap 7. Old-format, stale or malformed snapshots cannot
satisfy fresh capture. Native tests cover those distinctions.

`fresh_radio_capture` means at least one fresh MPDU, **not** a complete
aggregate. Pair output separately reports `all_subframes_retained`.
`actual_multi_mpdu_delivery`, `immediate_ba` and `ht_qualified` remain
explicitly false. Local TX submission alone cannot satisfy RX success.

## Reproduction

Back up and verify the complete flash of each borrowed badge before
installing an image. Restore it afterward and independently read and
byte-compare the entire flash. Do not reuse another campaign's backup.
These commands build diagnostic images; they do not install them:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx-d2-final-probe \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_AMPDU_PROBE=1' all test
make -C clean-rx/badge BUILD=../../build/clean-rx/badge/d2-final-probe \
  CONTROLLER_BIN=../../build/clean-rx-d2-final-probe/cyw43_rx_fw.bin \
  EXTRA_CFLAGS='-DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_HT_RX=1 -DCLEAN_RX_AMPDU_PROBE=1' all test
```

After deliberately installing matching images on both badges and leaving
both in BOOTSEL:

```sh
CLEAN_RX_COLLECTOR=build/clean-rx/badge/d2-final-probe/collector \
  bash clean-rx/badge/ht_probe.sh TX_SERIAL RX_SERIAL NEW_PREFIX 0 --ampdu-probe
```

The existing runner defaults to channel 1, generates a fresh challenge,
starts the receive-only badge first, and requests exactly one TX PPDU.
Its collector override must match the installed images. Evidence prefixes
must be unused; preserve negative results as well as successes.

## Measurements and unsuccessful alternatives

A is `08DA26F86E57F5EA`; B is `9BBF88DD17D66E7D`.
All prefixes below are under `build/clean-rx/badge/`.
These are separate one-PPDU trials, not a statistical qualification matrix.

| Build | Prefix | Direction / MCS | Fresh retained result |
| --- | --- | --- | --- |
| `c78d1b4a` | `d2-live-ab-m0-r0` | A -> B / 0 | None |
| `c78d1b4a` | `d2-live-ba-m0-r0` | B -> A / 0 | Sequence 0 only |
| `c78d1b4a` | `d2-live-ba-m7-r0` | B -> A / 7 | Sequence 0 only |
| `ec7512a2` | `d2-stream-ba-m0-r0` | B -> A / 0 | Sequence 0 only |
| `a0e7ac3e` | `d2-stream2-ba-m0-r0` | B -> A / 0 | Sequence 0 only |
| `3a608a57` | `d2-stream3-ba-m0-r0` | B -> A / 0 | Sequence 0 only |
| `b463ad2a` | `d2-stream4-ba-m0-r0` | B -> A / 0 | Sequence 0 only |

The four continuation variants tested, in order:

1. Skip the Cortex hold after header publication, reset the SHM capture
   pointer, and enter the existing prefix arm without a whole-RX flush.
2. Add an aggregate-specific FIFO-control/advance hypothesis and a bounded
   DMA busy-clear wait.
3. Continue immediately after terminal capture, before the normal header
   publication/DMA path, using the ordinary prefix arm.
4. Use the pre-DMA continuation with aggregate-specific FIFO selection
   instead of the ordinary initial-prefix selection.

Every continuation variant passed its native instruction/control-flow
tests but failed the actual multi-MPDU radio criterion. These paths and
their temporary `CLEAN_RX_AMPDU_REARM` gate are **not retained**.
They are evidence against these specific transitions, not proof that all
possible radio continuation mechanisms fail.

Successful first captures reported status1 `0x4122`, status2 `0x0a42`,
PHY format 2, RXE length 86 and the fresh complete sequence-zero MPDU.
The pre-DMA variants also observed status2 `0x0a42` at continuation entry.
The status2 bit differing from earlier nonaggregate observations is not,
by itself, a verified next-subframe or EOF control.

Both radios read ChipCommon ID `0x1545a9af` at `0x18000000`, whose low
16 bits identify 43439. This is a radio register read, not the RP2350's
unrelated A4 revision. No exact radio marketing stepping is inferred.

The final retained probe build is `38af3794`. Its changed evidence
layout/parser and removal of failed continuations were build/native-tested,
not reflashed after restoring the campaign originals. Earlier RF results
must be interpreted using their named build/layout, not the final parser.

## Validation and preservation

Default, extended HT/response/EDCA/WMM, and finite-probe controller/badge
builds and their native suites passed. The sanitizer-enabled suites
include the bounded BA/reordering/CCMP tests and probe validation.
The final probe controller/badge ELFs have no undefined symbols.
`git diff --check` and the paired runner's Bash syntax check passed.
CLI negative controls reject the probe in a default build and reject MCS8
in a probe build before opening USB.
Final logs use `d2-final-{default,gated,probe}-{controller,badge}.log`
under `build/clean-rx/badge/`.

Fresh verified 16,777,216-byte originals:

| Badge | Backup | SHA-256 |
| --- | --- | --- |
| A | `d2-live-start-A-full.bin` | `8cda7a00468339ca723305f3ee7cd5755875ce3f4ffe747f5d0a23d2b0a23628` |
| B | `d2-live-start-B-full.bin` | `bc3f972dcff6724638956cd5b8fd59a4f061b52d7a22eb87470a1f27067721e2` |

Full-flash restoration uses verified writes and separate
`d2-live-restored-{A,B}-full.bin` readbacks compared byte-for-byte to these
originals. **Both restores and independent 16-MiB byte comparisons passed**;
the readback SHA-256 values match the table above. Both badges remain in
BOOTSEL with their original contents.
USB restoration must be serialized: the initial concurrent B
attempt could not open its device while A was being restored and was
retried after A finished. No host networking or credential files changed.


