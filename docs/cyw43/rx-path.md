# Receive engine, PSM, headers and DMA ownership

[Guide](README.md) · [PHY contracts](phy-radio.md) · [Safety gates](safety.md)

**No fresh cold source-only frame has been demonstrated.** Original source
PSM code has now captured three consecutive complete, newly transmitted88-byte MPDUs
including checked payload/FCS, after guarded vendor-retained takeover.
That milestone is separate from a cold source-owned receiver, repeatable
general reception and live DMA. This page's detailed records cover
2026-09-22 and earlier; the [current status](README.md#current-status) includes
later September 24 controls. Historical measurements retain their own scope.
First cold acquisition is the [active milestone](README.md#active-milestone-cold-source-acquisition-of-a-fresh-frame);
general lifecycle, live DMA, persistent inhibition and broader qualification
are [deferred next steps](README.md#next-steps-after-first-acquisition).

The guarded [source calibration composition](calibration.md#complete-guarded-source-calibration-observed---2026-09-21)
now completes on hardware with MAC/PSM stopped and no RX descriptors.
That closes a PHY prerequisite, not the receive responsibilities below.
The later [bounded calibrated-state handoff](calibration.md#bounded-calibrated-state-handoff-verified---2026-09-21)
now transfers the original guard/board/rate objects to a real quiescent
source consumer after private-stack return. Two runs retain21 selected
post-calibration registers at acceptance, eight checks and explicit stop,
over267/268 nominal SOF frames. The lease is revoked before clock/IO cleanup
and independent recovery.

That **quiescent** profile keeps MAC/PSM stopped. A later opt-in consumer
uses the same ownership framework to load/read back the original PSM, wait
for READY, then enable MAC. Cold hardware runs reach that sequence but still
show no accepted frame. The historical HOF6 full-frame transfer reaches the controller
deadline with its last source mailbox still at receive-event wait; it is
not a source event-timeout terminal. DMA remains unpublished in both
profiles. The local buffer-plan API is not a live receiver initializer,
and sampled retention is not continuous RF safety.

## What now works, and which path it exercises

| Path | Physical result | Boundary |
| --- | --- | --- |
| Ordinary vendor receiver | 28 complete nonce/payload/FCS-bound MPDUs in input scratch; other RXE campaigns sample progression | Vendor firmware and vendor PSM, not source reception |
| Cold source-owned initialization + corrected full-frame PSM | Historical HOF6 code/SHM/READY/MAC proof; later HOF7 and checkpoint controls remain negative | No cold frame; latest condition-36/event-14 distinction is in the current status, not inferred from the HOF6 timeout |
| Vendor initialization + original source PSM | Checked format4 six-byte CCK prefixes committed; C and independent host checks pass | Vendor-retained state; upstream FIFO freshness and complete-frame attribution unproved |
| Same retained path with six candidate inhibit settings | Three checked prefixes with matching pre-MAC/terminal readbacks | Prefix/readback coexistence, not fresh RF reception under inhibition or TX-silence proof |
| Separate guarded fresh-frame profile | Three consecutive complete post-ready challenge-bound88-byte MPDUs with FCS and stable ABI7 snapshots; clean controls | Vendor-retained, one-shot fixed-frame capture; no cold RX, general reliability, rearm, DMA or RF-safety proof |

The diagnostic path is:

```text
RXE input scratch -> original PSM's checked prefix or full-frame snapshot
                  -> Cortex-M3 C evidence checks
                  -> independent RP capture and USB report
```

It does not yet exercise the full RXE-to-host-FIFO-to-RX-DMA path below.
The earlier prefix diagnostic uses SHM`0x7f4..0x7ff` for its own12-word format;
reusing that address does not make it the vendor's hardware receive header
or prove DMA offset38.

The [historical cold full-frame transfer](handoff.md#cold-full-frame-transfer-tested---historical-hof6-2026-09-22-checkpoint)
uses the corrected PSM and its actual dedicated snapshot, not the legacy
prefix as a substitute. Its HOF6 report is session-attributed in design,
without a post-ready challenge gate; neither of the two physical trials
produced an accepted frame. Preparation and active observations consume
the same existing controller budget. The source was last sampled at state3
before controller timeout, so unseen later PSM behavior is not inferred.

## Fresh-frame attribution under guarded retained initialization

**Current:** the corrected PSM is`73fd0ced`,3,144 bytes. It passes
[three consecutive captures and two controls](../../experimental/cyw43/r3/rxe-observe/runs/reliable-one-shot-2026-09-22.json).
The historical description below records the first positive before that fix.
Fifty-five two-SHM-input comparisons were invalid instructions; they now
load one operand into a GPR. Unstable snapshots recapture the same input
through the existing frame decrement, never resetting the1023 budget or
reissuing RXE commands. The previous FCS result is no longer required to
clear before MAC enable; all other drain/control/inhibit gates remain.
Successful final C/host predicates are unchanged. Persistent instability
fails closed; this is not discard/rearm or a general receiver.

The [accepted capture and all eleven attempts](../../experimental/cyw43/r3/rxe-observe/runs/fresh-frame-proof-2026-09-22.json)
use a separate original2,712-byte PSM`25cc3489`, not the old prefix image.
After code/SHM readback, source READY and guard checks with MAC clear, the
RP exposes a committed gate. Only then is a new unpredictable challenge
generated; the receiver receives only an opaque release token, and the AP
receives the challenge. Complete captured bytes bind that new stimulus.

The selected continuation follows the pinned plain-PHY branch: PHY00e
bits9/4 clear; live SPR02b low12 feeds SPR02a; count1 is rejected, but
count0 is permitted on that branch. Physical raw8000 derives0. No guessed
94/96-byte count is written. Source acceptance bounds the derived value
to0..180 except1; general count semantics remain experimental.

ABI7 separates initial-prefix and final-frame evidence. The prefix still
requires status bit12 and exact RXCTL0800. Final capture requires length94,
completion condition6 and FCS condition3, stable bit15-clear status and
marker15. The positive has status4182 before/after, PLCP`6e0440007fee`
and FCS`a95612cb`. The96-byte PSM snapshot includes a duplicate PLCP
halfword, the six-byte PLCP and the88-byte MPDU. Independent full-address,
payload, challenge, sequence, internal CRC and FCS checks all pass.

Owned SHM word ranges are mailbox600..60f, generation610..611,
RX input620..679, dedicated snapshot680..6af and diagnostics700..70f:
172 words cleared/read back before generation publication. RRTv4 carries
the full snapshot and gate/release evidence. It does not use the prefix
diagnostic as an output header or publish FIFO/DMA data.

One later matching frame fails the unchanged C stability check because
status changes0182 to4182 despite PSM commit. The cause remains unresolved;
the run is not accepted. No-burst and old-challenge controls do not accept
the expected new challenge, but the latter also fails status stability.
Beacon-first attempts fail the fixed geometry. This is one positive proof,
not a reliability result or general receiver. The
[handoff](handoff.md#fresh-source-psm-frame-proved---2026-09-22-checkpoint)
and [workflow](workflow.md#guarded-fresh-frame-experiment) retain exact pins.

## Receive responsibilities

```text
Radio/PHY: demodulation, PLCP/status, received bytes
   -> RXE: scratch copy/staging and host receive FIFO
        ^ PSM: event service, header, stage/commit, discard/rearm
   -> RX DMA: FIFO transfer into posted buffers
   -> ARM: validate completion, copy/interpret, reclaim/refill
```

The PSM need not copy every payload byte in software. Equally, posting DMA
buffers does not create the PSM-built header or issue its per-frame commit.
An ARM halt does not stop PSM; a stopped PSM after vendor initialization
retains hardware/SHM state, and DMA might drain an already committed frame.
Neither demonstrates cold source-only reception.

The design therefore requires a source-owned PSM receive sequencer unless
a genuine autonomous bypass is proved. This is an engineering decision,
not proof that every undocumented no-PSM silicon mode is impossible.
[Exact-image/public-source analysis](../../experimental/cyw43/r3/psm-rx/README.md).

The later [RC dependency tests](calibration.md#current-hardware-status-and-causal-rc-dependency)
show that live PSM execution is not required for RC completion in the
already-initialized reference when the necessary resources remain active.
That is not an autonomous-RX demonstration and does not remove the fresh
PSM/RXE producer requirement above.

## Publication leaf and header evidence

**Every PC in the following PSM sections is an instruction-word address,
not an ARM address or byte offset.** The exact expanded vendor image has
6,283 eight-byte words. Its publication routine is **`0x0e87..0x0e95`**,
expanded byte range `[0x7438,0x74b0)`, called at `0x0b58` and `0x0b82`.

| Word PC | Established decoded operation |
| --- | --- |
| `0x0e87` | Set SPR `0x078` bit 7 |
| `0x0e88` | Update SHM word `0x7f6` low two bits |
| `0x0e89` | SPR `0x007` = **`0x7f4`**, header offset |
| `0x0e8a` | SPR `0x008` = **12**, related-source length in 16-bit words |
| `0x0e8b` | SPR `0x004` low two bits = **1**, publication command |
| `0x0e8c..0x0e8f` | Wait for condition `0x0b` to assert then deassert; overflow condition `0x0d` escapes to `0x0ca1` |
| `0x0e92..0x0e94` | Check full again; conditionally notify via SPR `0x042=0x0100` |
| `0x0e95` | Stacked return |

There is no local deadline in the busy loops. Condition names “busy/full”
come from related sources; exact rev39 handshake timing and complete event
semantics remain unqualified despite the later sampled vendor progression.
Stage at **`0x09ce`** uses `(old & 0xfffc) | 2`, distinct from publish.

There are **35 known direct ALU writes** to SHM `0x7f4..0x7ff`, including
frame length, PHY status and flags. Pointer/length and field positions fit
the target-family **24-byte raw header** declaration. Older OpenFWWF uses
**10 words**, not this candidate 12-word header.

This does not prove all words are fresh on every path, nor establish
PLCP/padding/FCS, aggregate/crypto formats or the observed DMA offset **38**.
Do not equate `12*2` with a physically verified complete DMA prefix.

## ARM supplies RXE's scratch configuration

The latest bounded analysis resolves a previously missing producer.
Exact stock **init Table A** contains:

| Zero-based record | ARM image address | D11 byte offset / SPR | 16-bit value |
| ---: | --- | --- | --- |
| 109 | `0x141fc` | `+0x402` / SPR001 | **`0x0620`** |
| 176 | `0x14414` | `+0x404` / SPR002 | **`0x00b4` = 180** |

Literal `0xd0c4` and call `0xcf42` bind the table to the reviewed writer.
The selected stock order places these after PSM initial self-suspend and
before RX DMA initialization/ordinary reception.

Related register documentation gives **SHM-word units** for destination
and **byte units** for copy length: a candidate **180-byte** scratch region
starting at SHM word `0x620`, byte offset **`0xc40`**. PSM `off1=0x621`
reads **one 16-bit word later**. These are not ARM RAM addresses.

Absence of a direct PSM SPR001 writer therefore does not mean there is
no known initializer. Later [vendor measurements](vendor-observations.md)
confirm destination `0x620` and length 180 in all 908 retained runtime
samples. Selected scratch words also have a CRC-valid CCK PLCP interpretation.
Complete physical layout, exact fresh-byte timing and mode-dependent/later
writes remain unqualified.
[RXE binding checker](../../experimental/cyw43/r3/psm-rx/rxe_contract.py).

The [subsequent full-copy measurements](scratch-frame-binding.md) now bind
28 complete known plain CCK MPDUs at three lengths: two prefix bytes,
six-byte PLCP at offset 2, and MAC at offset 8 through verified FCS.
This resolves the tested **input-scratch** layout, not the separate
PSM-output/FIFO/DMA prefix or timing/aggregation/general-mode behavior.

## Cold entry, frame entry and rearm boundaries

The [source-owned implementation](../../experimental/cyw43/r3/psm-rx/source/README.md)
emits three original call-free arch15 programs: a **1,528-byte cold
diagnostic probe**, a **1,344-byte conditional transfer tail**, and the
**2,416-byte initial-precopy probe**. The conservative cold probe
stops with `PRECOPY_UNQUALIFIED` before staging; the tail requires already
fresh input from that still-missing producer. They are not connected into a
complete receiver.

The current initial-precopy probe, build`e6947a2c`, has separate bounded
MAC-enable and receive-event waits. It publishes
READY after cold initialization; ARM validates that fresh mailbox before
enabling MAC. After the bounded cold/event sequence it issues the evidence-backed
SPR024/SPR02a/SPR004=`0x40` initial request, samples status/length and copies
six prefix bytes twice-checked against the input. Core/flush/drain waits
remain64 iterations; precopy uses1,023; MAC/event waits each use16 windows
of65,535. These are instruction-count bounds, not calibrated microseconds.
A freestanding C checker
validates the diagnostic and 1/2/5.5/11-Mbps CCK PLCP format/CRC.
This is an implemented diagnostic, **not fresh-byte or frame proof**. It
does not emit the later `0x20c0` continuation, stage, publish, or rearm.
Historical cold versions executed with verified build/generation and
MAC-enable readback but ended at event timeout without a prefix. The
vendor-retained path described next supplies checked prefixes; it does
not retroactively qualify the cold runs. The other two programs remain
separate components.

The subsequent [supervised integration](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/README.md#supervised-psm-probe-integration-offline-not-receiver-activation)
adds a checked one-shot load/readback and generation-bound snapshot under
the calibrated owner and existing RP bounds. The experimental start request
starts PSM with MAC clear, then sets MAC_ENABLE after READY; `84020403`
has been read back on hardware. Handoff v5
performs two real owner checks and releases the probe without repeating the
quiescent retention experiment. Live execution requires current-code proofs
and explicit `--experimental-rf-unverified` under the owner's measurement
deferral. No DMA or full-frame producer is connected by this diagnostic.
Historical RF PLL override, missing cold register initialization, staged
enable and verified fast-beacon stimulus comparisons all retained the
no-event result;
see the [historical cold no-event investigation](handoff.md#no-event-investigation---latest-2026-09-21-checkpoint).

A C diagnostic checker verifies exact byte readback, ABI/build/generation
and terminal faults. It never converts matching diagnostics into permission
to start DMA. The new source arrays/checker link with DMA/recovery and the
TX writer guard in freestanding M3/M33 component builds. The PSM diagnostic
is now runtime-integrated, but the DMA transport is not activated. Complete
receive initialization, physical inhibition and cold first-frame proof remain
open. The separate retained fresh-frame profile above does not close them.

### Versioned post-request readback and retained takeover

The retained supervisor first initializes/associates through vendor firmware
and validates ordinary reference frames. It masks interrupts, pauses
MAC/PSM, checks candidate DMA stopping, loads/read-verifies the source
instructions, clears120 owned SHM words and publishes generation last.
The checked postload owned-bit sequence`0 -> 4 -> 6 -> 2` keeps jump-to-zero
asserted across PSM run enable before release. MAC enable follows a
fresh, identity-bound READY mailbox.

Two earlier assumptions were exposed by hardware:

- A64-iteration precopy wait could expire with status bit12 set but only
  length2 and the first PLCP halfword available. The1,023-iteration contrast
  reaches a complete six-byte candidate without increasing outer deadlines.
- The command write`0040` does not remain the observed readback. At the
  checked prefix boundary, RXCTL reads`0800`. A dedicated fault19 established
  the exact old rejection rather than guessing from a shared fault code.

Current **format4** requires **exactly0800**, plus the existing status/core,
length, stability, identity and CCK checks. It does not accept arbitrary
clear low bits or either old/new value. Historical **format2** retains its
0040 expectation. **Format3** is fault-only sampling with commit0; its
changing or incomplete values remain diagnostics, not acceptance.
No general hardware meaning for bit11 or transaction-completion proof is
inferred from this observed readback.

The two initial passing format4 trials contain`0a043806455e`, length6,
status3a00, coref020, RXCTL0800, tagda15 and commit1. Device and independent
host CCK checks pass. Two channel6 trials remain negative: one committed
raw`ca7502000000` but failed CCK validation; another timed out with no data.
The former's encoding/cause is not established; it is not automatically
accepted as OFDM or labeled a corrupt on-air frame.

Subsequent21-getter snapshots found three candidate override-enable
differences between the working retained state and the historical cold
fingerprint, plus small RC-count differences. The explicit
`source-prefix-inhibited` case adds the same six cold candidate settings.
Three checked prefixes coexist with exact cold-matching readbacks before
MAC enable and at capture; another attempt failed event drain before MAC
enable. These results do not exclude retained upstream data, prove fresh
reception under inhibition, or isolate every cold-state/timing cause.
[Current comparison evidence](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json).

After source PSM/SHM replacement, the controller commits a protected report
and parks; it never resumes the vendor runtime. The RP independently
captures that report while cancelling the waiting control call, powers off
and retires the session. A missing CDC reply is not relabeled successful
IOCTL completion. Repeated tests require a fresh RP/radio start, not
clearing the retirement flag. [Batch/recovery workflow](workflow.md#bounded-resident-research-batches).

The bounded tracer follows recognized control flow with stacked-call context;
it stops at unknown semantics. Its routes are **syntactic reachability
witnesses**, not feasible packet executions or proof that selected routes
compose under real predicates.

| Transition | Exact-image fact | Remaining contract |
| --- | --- | --- |
| Cold entry | `0 → 0x0e5c`; SPR047 bit 5 can skip SHM clearing | Reset GPR/SHM state must be established, not inherited |
| Cold core handshake | `0x0e76` sets SPR078 bit 5; `0x0e77` waits for bit 13 | Related PPAR/PPAS names do not supply clocks, acknowledgement timing or a safe timeout |
| Initial suspend | Zero SHM `0x05c` path waits for MAC enable; nonzero branch writes TX-control SPR080=`0x4001` | Zero-filled software fixtures are not proof of the desired physical branch |
| PLCP entry | `0x00c4` tests condition `0x0e` with EOI and branches to `0x0834` | Edge/level/clear/reassertion behavior |
| Entry guard | `0x0839` rejects SPR004 bit 2 **before** local FIFO initialization | Flush completion before the next event |
| Scratch preparation | Clears two SHM words at `off1`; writes SPR004=`0x40`, later sets bits 6–13 from GPR21 | Fresh RXE data and exact copy/layout timing |
| Completion | Event `0x06`, FCS condition `0x03`, overflow and header construction are distinct | Not one generic “packet-ready” flag |
| Normal publish return | `0x0b83` uses `(old & 0xffc0) | 2`; one cleanup branch then clears bit 1 | Low **six** bits, not just low two; aggregation/crypto branches differ |

Maximum-length SHM word `0x010` and IRQ threshold word `0x37f` have no
known direct PSM ALU writer in this inventory. Zeroing everything is not
a complete cold configuration.

### Flush/discard: bit 2 is not bit 4

SPR004 corresponds to D11 `+0x408`. A write of **`0x14`** sets:

- Bit **2**: related disconnect/suspend/air-buffer flush.
- Bit **4**: separately named receiver reset.

Discard paths write/read back `0x14`, wait for crypto, pulse SPR1e8
`0x309→0x300` and SPR1ec `9→0`; bad PLCP instead reaches a path writing
**4**. The next PLCP guard rejects bit 2. Stage/publish arithmetic preserves
it for every old word.

Older OpenFWWF explicitly sets bit 2 and waits for it to clear **without
a local software clear**. That supports a device-completed flush hypothesis,
not permission to add an unconditional clear. Rev39 self-clear, possible
read effects, full completion latency and discard-to-next-event progression
remain unqualified. The first vendor campaign missed the assertion; the
[fast-trigger follow-up](vendor-observations.md#follow-up-fast-trigger-caught-both-bits)
now records **`0x814 → 0x800`** with no observer control write. Its 518-raw-tick
bracket establishes the observed set/clear pair, not the clearing cause or
a complete request-latency bound.

EOI locations are separately inventoried for PLCP, bad PLCP, complete,
full and FCS; no busy EOI is encoded in the selected inventory. An encoded
EOI is not an established ARM-side event-clear API.

### `nap2`: encoding known, stack/wake semantics unknown

The overflow escape from the publication leaf can retain caller return
`0x0b83` or `0x0b59` until shared cleanup reaches **PC `0x0014`**.
Nexmon's arch15 assembler/disassembler names tuple
**`(0x002,0x1780,0x1780,0)`** **`nap2`**. Occurrences are `0x0014`,
`0x037c`, `0x0d35`. This is not the older-core `call`, opcode `0x001`
`nap`, or opcode `0x005` `rets`.

Public encoding support does not establish wake/fallthrough, stack reset,
stack preservation or ordinary return. The tracer names it but stops; it
does not silently pop abandoned calls. A call-free, always-awake original
sequencer avoids relying on `nap2`, but still needs the physical
cold/event/flush contracts.

Raw PSM debug at **D11 `+0x154`** is now observed on hardware: the canonical
channel-1/6 runs retain **92 / 117 distinct complete values**. The public
helper's **`0xfff` PC mask cannot distinguish all 6,283 target instruction
addresses** and was not applied. Samples are not retired-PC traces,
stack-depth observations or proof of `nap2` behavior.

### Measured vendor progression, not source-only RX

The [completed vendor campaign](vendor-observations.md) has **76 bursts /
908 records**. In
[channel-6 active-042, record 6](../../experimental/cyw43/r3/rxe-observe/runs/vendor-ch6-bpshukkj/active-042.proof.json),
control changes **`0x802 → 0x801`** inside one sample at RX length 94,
followed by `0x800` / core `0xf0a0`. Channel-1 active-009 shows growing
lengths 6 through 94 and `0x800 → 0x802 → 0x800`.

Selected SHM `0x621..0x623` bytes `6e 04 40 00 7f ee` form a CRC-valid
11 Mbps CCK PLCP candidate, length 64 us, consistent with an 88-byte PSDU
plus six-byte PLCP. Only scratch words were sampled: this is **not**
nonce-specific binding to a complete frame or the full DMA prefix/FCS map.

The sampler in that historical vendor campaign preserves vendor PSM. Interrupt masking protects selector
ownership but does not make PSM/RXE samples atomic. The 337-tick maximum
record span stays in raw units; omitted records and retention-period
semantics prevent a sampling-rate/overhead qualification. The original campaign did not establish a flush assertion; the separately
recorded fast-trigger follow-up did. Neither establishes full self-clear
latency or `nap2` stack effects.

## DMA configuration and descriptor contract

The separate [no-UP vendor reference](../../experimental/cyw43/r3/live-queue/rx-configuration.md)
observed ring `0x2d5b0`, 32 descriptors/512 bytes, zero indices and all-zero
packet slots. Stored completion mask was `0x1fff`, but stored pointer base
and availability were **zero**. Buffer input 1,716, extra headroom 204,
desired-post input 16 and offset 38 are configuration, not received bytes.
D11 remained reset-held: allocated is not initialized, posted or completed.

Exact descriptor writers establish four little-endian words at 16-byte
stride:

| Byte offset | Content |
| ---: | --- |
| 0 | Caller control; ordinary first chunk `0x80000000`, plus `0x10000000` at ring end |
| 4 | Length `&0x7fff`, applicable address-extension bits and optional parity `0x40000` |
| 8 / 12 | Low/high translated buffer address |

The vendor writer can truncate invalid lengths to zero; its wide-address
addition does not carry low overflow into high. Its stores are not atomic.
Producer publication is **`pointer_base + (producer << 4)`** at RX register
`+4`, after descriptor/packet bookkeeping. No generic per-descriptor
ownership bit was established.
[Serialization/publication](../../experimental/cyw43/r3/init/rx-descriptor-contract.md).

The new source [DMA transport](../../experimental/cyw43/r3/rx-dma/README.md)
implements, but has **not activated**, this fixed candidate profile:

| Field | Value |
| --- | --- |
| RX registers | **`0x18001220..0x18001234`**: control, producer, ring-low/high, status0/1 |
| Control | **`0x0004084d`** = `0x40000` mode + `0x800` parity-disable + `(38<<1)` + enable |
| Ring / posted | 32 × 16 bytes, **31 posted** |
| Buffers | 1,716 bytes each, identity translation, no split/parity mode |
| Completion mask | `0x1fff` |
| Proposed arena | `[0x20000,0x30000)`; populated extent through `0x2d87f` |

Eight-KiB ring alignment avoids relying on the vendor capability probe.
The constructor's mask choice is conditional; `0x1fff` is not universal.
Active state 1/2 and status1 error handling partly use related Linux DMA
source and remain **candidate physical semantics**.

The implementation verifies reset/clock access, idle initialization,
RAM/descriptors and readbacks; orders publication; takes repeated status0
samples; validates fresh completed distance against the entire posted
ledger; copies only completed buffers; rechecks before reclaim/refill.
Ambiguous writes, invalid status or errors **quarantine** queue and arena.
No automatic rollback, force-reclaim, rebind or pool reuse is allowed.

Startup-only source probes physically observe disabled RX control`0x40`.
Their predicate accepts exactly0 or`0x40`, still requires bit0 clear and
disabled/no-error status, and publishes nothing. That correction is not a
relaxation of this active DMA profile or proof that a descriptor completed.

Stop clears RX control and requires disabled/no-error state with both
raw-tick and **1,001-sample** bounds. This is not an RF-silence check.
The concrete backend has CYW M3 MMIO/RAM and DSB, no PHY/PSM/TX authority,
and is not linked into the 5,840-byte image.

## Completion, length and frame proof are separate

Vendor reclaim can use cached completion, force bypasses ownership, and
all-ones status can produce an invalid boundary yet return a buffer.
Peek can loop indefinitely on unreachable boundaries. Source validation
must reject these cases rather than copy their permissive behavior.
[Exact reclaim/length hazards](../../experimental/cyw43/r3/init/rx-reclaim-contract.md).

The ordinary length path adds configured **38** to the first buffer u16
and caps at 1,716. Even zero input produces 38; neither proves a frame.
Alternate mode reads a u16 at byte **42** for this offset; its protocol
meaning remains unresolved.

The new strict matcher accepts an **already delimited 802.11 MPDU including
FCS**, checks exact addresses/BSSID/LLC/SNAP and nonce-labelled lab payload,
and rejects unsupported protected/fragmented/aggregate layouts. It does
not hunt for plausible offsets or infer where the MPDU starts in raw DMA.
Host model completion and matched fixtures are not over-the-air reception.

The associated vendor lab station received five verified frames on each
of channels 1/6/11. The later source phase still had zero published
descriptors and `source_received_frames=null`, not a measured zero.
AP submission counters do not prove on-air data delivery with no associated
station; an identifiable beacon is a useful first source-RX target.
[Stimulus lab and limitations](../../experimental/cyw43/r3/rx-lab/README.md).
