# Measured vendor calibration, RXE and PSM observations

[Guide](README.md) · [Receive contracts](rx-path.md) · [Safety](safety.md)

## Current evidence map

Keep the datasets separate; later experiments do not change the totals or
interpretation of the historical908-record campaign.

| Dataset | What ran in the radio | Strongest observation |
| --- | --- | --- |
| Pre-RC resource/clock contrasts | Vendor initialization with controlled pauses/changes | Calibration dependencies and matching selected programming data |
| RXE sampler and fast trigger | Unchanged vendor PSM |908 canonical records plus separate trigger follow-ups; sampled receive/flush progression |
| Full scratch binding | Unchanged vendor PSM |28 complete known nonce/payload/FCS-bound vendor MPDUs |
| [Format4 prefix experiment](../../experimental/cyw43/r3/rxe-observe/runs/checked-prefix-format4-2026-09-22.json) | Vendor initialization, then original source PSM | Two initial checked six-byte prefix passes; two channel6 negatives retained |
| [Receiver-state/inhibit comparison](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json) | Same vendor-retained source PSM, baseline and explicit tightened profile |21 selected getters compared; three guarded checked prefixes with cold-matching candidate readbacks |

The last two rows are **vendor-retained**, not source-only. They do not
inherit the earlier full-frame nonce/FCS binding. Upstream FIFO freshness,
full-frame continuation, DMA publication and RF safety remain unproved.

### Source PSM under vendor-retained initialization

The supervised takeover verifies instruction readback and owned-SHM
clearing, binds generation, waits for READY, then enables MAC. Holding
jump-to-zero across run enable resolved the observed unbound-startup
contrast; this is not a complete reset/clock specification. A short
precopy window initially stopped at length2; the bounded1,023-poll
contrast reached length6 without increasing outer deadlines.

The exact old rejection was RXCTL`!=0040`. Format4 instead requires the
observed post-request readback **exactly0800** and preserves all other
status, length, stability and CCK checks. The command remains0040.
Both C and host checks pass for`0a043806455e`; historical format2/0040
reports retain their original rules. A committed non-CCK candidate and an
empty timeout remain failed observations, not normalized successes.

The selected21-register comparison found three override-enable differences
from the historical cold no-event state (PHY44c, PHY938, radioa4), plus
small RC-count differences. Applying all six cold candidate settings to
the working retained context produced three checked prefixes with matching
pre-MAC/terminal readbacks. One attempt failed initial event drain before
MAC enable. This establishes **prefix/readback coexistence**, not fresh RF
reception under inhibition or an isolated cold failure cause.

The controller parks after replacement and never resumes the overwritten
vendor state. The RP captures independently and retires the session.
The [fixed catalogue and batch workflow](workflow.md#bounded-resident-research-batches)
reboots/reinitializes between trials and retains independent frozen
recovery at batch boundaries or first failure. These runs use the explicit
RF-unverified policy; they are not a general active-receiver safety proof.

## Calibration and clock/resource contrasts

The 2026-09-20 [pre-RC campaign](calibration.md#current-hardware-status-and-causal-rc-dependency)
is separate from the receive measurements below. It samples the actual
initialization boundary, not just a post-UP endpoint:

- All 17 sampled source radio/PHY values match the working early-inhibited
  reference; the source at that checkpoint failed first-pass LPF 100/100.
- Vendor RC works with all six candidate inhibits, PSM stopped, exact
  source MAC flags, SR enable clear and source MIN installed.
- With shared availability retained, releasing the tested ChipCommon and
  D11 requests does not prevent RC.
- Excluding resources 20/21/30 reproduces 100-poll exhaustion and zero counts;
  restoring eligibility restores 6-poll completion on four boots per state.
- The four-boot profile12 reads PLL8=`1068` and pending0 before RC;
  both RC passes still complete in six polls. Its selector is explicitly
  restored after observation; no PLL data/commit write is added. Source
  reads `106a` and pending0 both before and after its guarded power envelope.
  The2026-09-21 contrast then programs/readbacks`106a`, commits it and
  cycles resource eligibility: RC still succeeds in6 polls on four boots.
  The close-loop rescue also succeeds. The value difference alone is not
  sufficient to reproduce source failure; prior vendor history remains.
- A read-only pre-RC comparison on four boots confirms all31 dependency
  words,31 final timing words and PLL0-7 match source programming data.
  This does not establish the same full readback from source hardware.

The later [source-powered comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#powered-source-bank-readback---2026-09-21)
now independently confirms those source dependency/timing values and all9
PLL controls on two runs. The vendor observations remain a separate data
set; earlier initialization history is still not equivalent.

Source PHYPLL activation, calibration and bounded handoff subsequently
passed. The [current handoff](handoff.md) records that source-only milestone;
it does not reclassify the earlier failed source runs or these vendor results.

The subsequent [control-bank contrasts](../../experimental/cyw43/r3/live-queue/README.md#control-bank-comparison-and-contrasts---2026-09-21)
measure five source/vendor differences across chip-control0/2/6 and
regulator0/4. Reference removal/restoration of the CC2/CC6 preparation bits,
and separate whole-SR0 zero/restoration, each retain6-poll RC completion on
four boots. Regulator and oscillator values are not changed. These results
do not erase vendor initialization history or qualify source calibration.

That pre-RC reference records both RC outcomes and deliberately parks
before later calibration/MAC activation. It is not a full-UP or receive
proof, and calibration RF may occur. Each physical run independently
recovers source5840 and BOOTSEL. The second badge was idle throughout these
same-chip comparisons; it was not used as a stimulus or RF-silence detector.
Raw hashes and limits are in the
[latest contrast report](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#pll-mode-and-live-pmu-tables---2026-09-21).

## RXE and PSM receive campaign

**All three requested vendor observation types have now been performed:**
runtime RXE configuration, on-chip receive-control/scratch progression and
complete raw PSM debug sampling. This is **vendor-runtime evidence**, not
source-only reception, RF-silence verification or resolved `nap2` semantics.

The vendor-preserving modes of the [observer implementation](../../experimental/cyw43/r3/rxe-observe/README.md)
preserve vendor PSM microcode, template, NVRAM and CLM, with an ARM sampler
in the reviewed R1 reservation. Research serial `9BBF88DD17D66E7D` runs the
observer; `08DA26F86E57F5EA` supplies the existing vendor AP stimulus.
That campaign used the unchanged reset-held source recovery baseline.
Later guarded source startup experiments are distinct and still do not
demonstrate source-only reception.

The newer [full scratch/frame-binding campaign](scratch-frame-binding.md)
extends this four-word sampler: **28 complete nonce-labelled MPDUs** now
match exact MAC/payload/FCS bytes across Ethernet lengths 66/98/130.
The earlier metadata-only observations below remain a distinct dataset.

## Canonical runs

Both final datasets use **four 16-bit low/high SHM reads**, matching the
exact-target helper, rather than the initial two-word read implementation.

| Run | Channel | Bursts / retained records | Validated vendor RX before → after active phase | RP / controller builds |
| --- | ---: | ---: | ---: | --- |
| [vendor-ch1-9scqpxlg](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch1-9scqpxlg/analysis.json) | 1 | **26 / 293** | **5 → 26** | `735e3454` / `b3c43fb0` |
| [vendor-ch6-bpshukkj](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch6-bpshukkj/analysis.json) | 6 | **50 / 615** | **6 → 55** | `658285f1` / `b3c43fb0` |

Total: **76 bursts / 908 records**, comprising **72 active** bursts and
**four stimulus-off** bursts. Stimulus-off means our AP radio was explicitly
turned off; it does **not** mean RF isolation or no ambient traffic.
Vendor RX counts come from the existing nonce/sequence/payload/CRC validator,
not from treating a scratch header as a complete packet.

Original per-run `analysis.json` files are preserved. Later
[channel-1](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch1-9scqpxlg/analysis-followup.json)
and [channel-6](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch6-bpshukkj/analysis-followup.json)
`analysis-followup.json` files add CCK/header checks and input/tool hashes;
they do not overwrite the original interpretation.

## 1. Runtime configuration matches ARM initialization

Every one of the **908** retained records contains:

- RXE destination **`0x620`**, in SHM-word units.
- Copy length **180 bytes**.
- SPR078 request **bit 5** and status **bit 13** both set.

Observed complete core-control/status values are **`0xf020` / `0xf0a0`**,
differing in bit 7. This closes the sampled steady-state configuration
question for these runs. It does not measure the cold request/acknowledgement
edge or prove those values alone suffice for a source-only cold start.

## 2. Receive progression and a stage/publication transition

[Channel 1, active burst 009](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch1-9scqpxlg/active-009.proof.json)
observes receive lengths **6, 18, 34, 50, 66, 82, 94**, with control moving
**`0x800 → 0x802 → 0x800`** and core bit 7 set at the end.

[Channel 6, active burst 042](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch6-bpshukkj/active-042.proof.json)
captures the transient publication value inside one record:

| Zero-based record | Control before / after | RX length | Core status |
| ---: | --- | ---: | --- |
| 3 | `0x802 / 0x802` | 42 | `0xf020` |
| 4 | `0x802 / 0x802` | 58 | `0xf020` |
| 5 | `0x802 / 0x802` | 74 | `0xf020` |
| **6** | **`0x802 / 0x801`** | **94** | `0xf020` |
| 7 | `0x800 / 0x800` | 94 | `0xf0a0` |

These are real on-chip samples consistent with the exact PSM stage/commit
path, not execution tracing at its precise PCs. Record 7 starts 921 raw
counter ticks after record 6 starts; that is **not a measured 921-tick
completion latency**. The transition times within/between samples are unknown.

### Scratch bytes now have a checked PLCP interpretation

In these selected examples, SHM `0x621..0x623` contains:

```text
6e 04 40 00 7f ee
```

This is a CRC-valid **CCK PLCP candidate**: signal `0x6e` means **11 Mbps**,
service `0x04`, length **64 microseconds**, CRC16 **`0xee7f`**. The reflected
CCITT calculation uses polynomial `0x8408`, initial and final XOR `0xffff`.
The [Linux b43 PLCP generator](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/b43/xmit.c)
corroborates the rate/service/length mapping.

An **88-byte PSDU** at 11 Mbps takes 64 microseconds; adding the six-byte
PLCP fits observed RX length **94** and the configured lab traffic. The
64-microsecond field is a decoded protocol quantity, **not a conversion
of the observer's raw counter ticks**.

Only four scratch words were sampled: no complete MPDU, payload nonce or
FCS was captured. Thus this is not nonce-specific binding of the sampled
header to a particular full frame, nor a complete DMA prefix/FCS map.
Word `0x620` duplicates `0x621` in these examples; its physical explanation
remains open. Other scratch samples fail this candidate CRC or are not CCK
candidates; that is not a measured RF frame-error rate.

### Original campaign: flush was unobserved

**No retained before/after control sample asserted bit 2 or bit 4.**
This supplies neither a flush self-clear latency nor proof that flush/reset
never occurred. No direct flush/EOI command was injected to manufacture an
edge. Device self-clear versus another writer/read side effect, cold
acknowledgement and discard-to-next-frame timing remain unresolved.

### Follow-up: fast trigger caught both bits

The [targeted follow-up](../../experimental/cyw43/r3/rxe-observe/README.md#targeted-fast-trigger-follow-up-flushreset-bits-caught)
adds a read-only fast-control mode. Full SHM/debug snapshots occur only at
the first/last observation, a bit-2/bit-4 match, and its following clear.
Flag bit 3 preserves the earlier fast peek's timestamp and control value.

Two additional 50-burst runs on channels 1 and 6 made **1,262,281 quick
control peeks** in 96 active trigger bursts and retained 254 full samples
including four stimulus-off bursts. The channel-6
[active-018 record](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch6-z6glo39e/active-018.json)
caught **`0x814 → 0x800`**, with both bits set and then clear. The record's
timestamp bracket is **518 raw ticks**, not full request latency.

The observer made no RXE-control write. The clearing cause—hardware, vendor
software or read effects—remains unproved, and this is not a universal
timeout bound. Vendor RX advanced **5 → 45** in each run. The unchanged
source baseline and BOOTSEL recovery passed; source RX remains disabled.
That fast-trigger campaign used RP **`13410238`** / controller **`5599f677`**. Its controller
uses **6,084 of 6,592 reserved bytes**, with no widened memory exception.
Its [source recovery](../../experimental/cyw43/r3/rx-memory/runs/memory-1.8fGlee9Q/report.jsonl)
is separate from active receiver recovery.

## 3. Raw PSM debug sampled without invented PC semantics

The channel-1 and channel-6 datasets retain **92 / 117 distinct complete
PSMDEBUG values**, respectively, with active/stimulus-off sets kept separate.
No `0xfff` PC mask, retired-PC decoding or stack-depth interpretation is
applied. These are raw execution/residency observations, not a proof of
`nap2` wake behavior, stack clearing or ordinary return.

## Sampling limits and recovery

- All **908** records report initial PRIMASK **0** and restored object
  selectors; record flags are **2 or 6**. Interrupt masking protects the
  ARM selector transaction, not concurrent PSM/RXE state.
- Maximum retained record span: **337 raw CYW counter ticks**. Do not
  convert it with the source-only 37.4 MHz measurement or the vendor
  runtime multiplier.
- Record caps omitted **1,652 / 3,446 selected observations**. Slots 0–13
  preserve early samples; slot 14 preserves the latest selected sample.
- The period parameter spaces **retained records**, not actual sampler
  execution: every poll still reads the fixed registers. These runs do
  not qualify sampling rate, observer overhead or zero induced loss/latency.
- Continued validated vendor RX proves continued operation during the
  active phase, not causality for every sample or absence of perturbation.

Both canonical runs completed cleanup, verified both badges in BOOTSEL and
restored the unchanged **5,840-byte** source memory/counter baseline, SHA256
`0fbd8c335e45cebd3b8a03728c17bf9f7bfff6b2d53774a14be882a00327461b`.
The [final source recovery](../../experimental/cyw43/r3/rx-memory/runs/memory-1.DfxxWbEL/report.jsonl)
passed, and the 133-file outside baseline remained unchanged. Neither the
source receiver nor the new source DMA arena was activated.

## Retained history and software corrections

[Initial eh2qplsh](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch1-eh2qplsh/outcome.json)
contains five bursts using 32-bit SHM reads. It remains initial evidence,
not part of the qualified halfword-access totals.

[Failed v0n9_yxu](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch6-v0n9_yxu/outcome.json)
contains 35 valid traces followed by USB `0xa4` STALL while station activity
made observation unavailable. Its failed outcome and recovery are retained.
The collector now retries explicit **pre-acceptance busy STALLs only**, for
a bounded **two seconds**, using the same request/nonce; the final channel-6
run records **three** such rejections. Other failures are not automatically
retried.

The RP ACK race was also corrected: an acknowledged request is queued and
then revalidated/cancelled, not silently dropped when activity changes.
Recorded software validation comprises **five native device suites**, **four
device-builder Python tests** and **17 parent protocol/runner/analysis tests**,
including pinned replay of all **76 final traces / 908 records**. These tests
cover the ACK regression and evidence consistency; they remain separate from
physical handshake, sampling-overhead or `nap2` qualification. No failed run
was relabelled pass.
