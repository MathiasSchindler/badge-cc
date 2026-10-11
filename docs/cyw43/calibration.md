# Calibration, power controls and timing

[Guide](README.md) · [PHY/radio](phy-radio.md) · [Safety](safety.md)

**The guarded RX-focused source initialization/calibration composition now
completes on hardware, followed by a verified bounded quiescent handoff.**
This does not prove analog accuracy, RF silence or a working source-only
receiver. The frozen source5840 recovery payload remains reset-held; the
larger calibration/handoff experiment is separate.

**Receive boundary, reviewed 2026-10-08:** no cold-source fresh frame or live
source DMA has been proved. The separate vendor-retained original-PSM path
captured three consecutive fresh complete MPDUs, with payload/challenge/FCS
checks and clean controls. Earlier six-byte prefix and selected-register
comparisons retain their narrower scope; neither establishes whole-state
equivalence or the cold failure's cause. See the
[consolidated status](README.md#current-status) and [handoff history](handoff.md#checkpoint-history)
for the later September 24 condition-36/event-14 contrasts. The dated
calibration results below are evidence, not a current implementation backlog.
First cold acquisition is the active milestone; lifecycle, DMA, persistent
inhibition and broader qualification are [next steps](README.md#next-steps-after-first-acquisition).

## Bounded calibrated-state handoff verified - 2026-09-21

Two runs of the identical source/RP image now transfer the original guard,
board and active-timing objects to a real source quiescent owner **after
return from the private calibration stack**. PHYPLL stays requested while
the consumer accepts, services a bounded hold and explicitly stops.
This is ownership and sampled retention, not receiver activation.

| Result | [First run](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-prnwzxb3/report.json) | [Identical-image repeat](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-_na2u3rk/report.json) |
| --- | --- | --- |
| Nominal USB SOF frames | 267 | 268 |
| Eight distributed check frames | 33,65,97,129,162,194,226,258 | 33,65,97,130,162,194,226,259 |
| Source elapsed / budget, raw ticks | 31,124,007 /40,000,000 | 31,242,060 /40,000,000 |
| Selected post-calibration registers | 21 unchanged at accept, eight checks and stop | Same |
| Stop sequence / ACK / error | 9 /9 /0 | Same |
| Final bound state | `quiescent_lease_stopped`, revoked | Same |

Source build`2755f5b0` and RP build`6d2058f2` use cold format22,
handoff v2 and wire v10. The normal consumer SP is`fe50`, outside the
private stack`[42ec0,43ec0)` used when offering the lease at SP`43bd0`.
The original guard at`44824`, board at`44940` and active timing object
at`46178` remain in owned BSS. Board self-pointers are not copied, the armed
guard is not recreated, and callbacks use stable owned context. Native
and Cortex-M3 tests poison the returned private stack and check rejection
of stale, repeated and wrong-owner capabilities before peripheral access.

At acceptance, every hold check and stop, resources remain`7efa3df7`,
D11 clock`070b0240`, IOCTRL`2055`, stopped MACCONTROL`84000404`,
MIN`0ec23df7`, SR0/SR1 zero and DMA control/status`40/0/0`.
All six candidate inhibits remain checked. A separate **live
post-calibration** fingerprint checks the earlier17 selected radio/PHY
words plus radio65, PHY978, PHY96f and PHY440; it is not the pre-RC
snapshot. All ten comparison CRCs match within each run. CRCs differ
between runs because measured RC counts vary; cross-run equality is not
required. No register data writes or recalibration manufacture retention.

Explicit stop revokes the lease before success-only request/IO cleanup.
No MAC reception, PSM execution or DMA publication is enabled. The source
then deliberately parks at marker`f0000001`, value8, rather than crossing
into unqualified inline-reset timing. Both independent source5840 and
final BOOTSEL recoveries pass; the second badge was untouched.

### Observer failures retained, not reclassified

- [First attempt](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-xlqhs6ww/report.json):
  a1536-byte in-window RP prefetch caused SOF step16, exceeding the unchanged
  limit8. The source had accepted on its normal stack; the cold phase15
  prefetch was stale. Metering now reads only the64-byte command/ACK slab.
- [Second attempt](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-y9yfx68u/report.json):
  source checks/stop/revocation completed, but bulk capture raced final
  cleanup and the archive/captured prefix CRCs disagreed. Strict validation
  correctly rejected the binding. The RP now awaits the existing final
  commit before bulk capture.

Both failures remain unchanged and unqualified. Shared tests reproduce
the races and verify the fixes without relaxing bounds or binding rules.
Missing ACKs, frozen SOF, lost progress, source faults, timer expiry/wrap
and poll cutoff also have bounded failure coverage.

Generic runner exit1 and `measurement_verified=false` still describe
incomplete full cold-start/reset execution, not the independently verified
handoff milestone. Capture-time `recovery_verified=false` precedes the
later independent recovery; use its outcome and source5840 proof separately.
Exact hashes, identities and validation counts are in the
[central ledger](../../experimental/cyw43/r3/references.json).

**Scope:** at least256 nominal SOF frames and sampled retention of selected
registers, not indefinite operation, whole-hardware or analog-state
preservation, continuous RF silence, or configured state surviving reset.
**Next at that checkpoint:** extend this owner with qualified source receive
preparation. Later cold MAC/PSM activation now reaches the no-event failure;
a complete receiver lifecycle, fresh source-only PSM/RXE production, live
DMA and identifiable frames remain open. Independent TX/RF qualification
remains separate.

## Complete guarded source calibration observed - 2026-09-21

Two independent runs of the identical source image complete the original-C
radio/PHY composition through RC, ADC tuning, DAC/TIA, channel/VCO, DC,
AGC and finalization. Both reach configured phase18 with error0 and
completed mask`0003fffe`. No coefficient was borrowed from the vendor.

| Result | [First run](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-4n4zi342/report.json) | [Identical-image repeat](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-1ugdabk5/report.json) |
| --- | --- | --- |
| RC passes completed / samples | 2 /1 each | 2 /1 each |
| LPF N0 /N1 | 5019 /5457 | 5017 /5457 |
| ADC N0 /N1 | 5020 /5401 | 5019 /5401 |
| Calculated LPF /ADC result | 4871 /4258 | 4893 /4269 |
| DAC-buffer observation | 9 | 9 |
| VCO | Complete,1 sample, status`5` | Complete,1 sample, status`5` |
| DC | 2 pulses, IDAC`ff`, DCOE`fff`, completion predicate true | Same |

RC arithmetic and all11 ADC coefficients were independently recomputed
from the captured source counts. The first ADC set is
`16,29,20,19,42,95,8,9,9,10,21`; the repeat changes its fourth element
to18, consistent with its measured ADC input.

### Timing and table gate

The build consumes the whole recovered
[active qualification](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-5i7u5n3o/report.json),
not an unbound frequency number. That v3 record checks all908 logical table
entries, both defaults, the17 powered pre-RC words and fresh active-state
rate, then exercises arguments1/10/20/32/35/50/100/200 through a distinct
epoch-bound delay backend. Calibration runs repeat those active checks and
trials before continuing through the real intercepted RCCAL selector.

Every measured delay meets its conservative nominal-USB-derived minimum.
Instrumentation gives an approximately15783-counter-tick floor for the
smaller arguments in the qualification run; these are **not exact
microsecond pulses**. One-poll source completion is therefore not a latency
comparison with the vendor's6-poll result. Target deadlines/sample caps
were not increased; analog maximum-delay tolerance and absolute timing
accuracy remain unproved.

### Completion and cleanup scope

The independently CRC-checked calibration record reports
`calibration_composition_observed`, original-C completion, matching request
binding and verified success-only request release. Native marker`ee000001`
with value`0003fffe` agrees. All six inhibits remain enforced, SR0/SR1
remain0, MAC/PSM remain stopped and no RX descriptors are published.

After success, the PHYPLL request changes`070b0240 ->070b0040`;
IOCTRL returns to`45`, MACCONTROL to`04000404`. Shared availability is
not required to disappear. Inline core-reset waits would cross timing
domains, so the source parks and independent power-off/source5840 recovery
owns reset. Both recoveries and final BOOTSEL checks pass.

The generic runner still exits1 for this deliberate pre-reset stop.
That is not a calibration failure, nor is the successful calibration
record a complete inline-cold-start/reset proof. Configured state is not
claimed retained after recovery; blanket completion freshness, analog
accuracy, source frames and RF silence remain unproved.

The later [bounded quiescent handoff](#bounded-calibrated-state-handoff-verified---2026-09-21)
now retains this state across the private-stack return. A complete
receiver lifecycle, fresh source-only PSM/RXE production and live DMA, fresh source
frames and the end-to-end TX/RF safety boundary remain open.
The dated qualification/failure milestones below retain their original scope.

## Active-state rate and table checks - 2026-09-21

The next qualification-only source image has now passed twice with PHYPLL
held active. Both independently CRC-committed observations verify:

- Fresh256-SOF counter intervals in the active resource state.
- All8 tables /908 logical entries and both defaults after activation,
  including all256 logical table20 addresses against the bound expected
  last-write-wins image.
- All17 selected powered radio/PHY words immediately after activation
  and after the rate/table recheck, unchanged from the committed
  pre-request snapshot.
- Three held-state snapshots at resources`7efa3df7`, D11`070b0240`,
  MIN`0ec23df7`, SR0/SR1 zero and stopped MAC/PSM, with all six inhibits.

The counter intervals contain20902340 and20877541 ticks respectively.
Their conservative nominal-USB denominators for the upper-rate bound
are255000 microseconds. This is a fresh active-domain rate observation,
not an absolute frequency measurement or a short-delay callback proof.
The logical table image remains valid; universal table capacity is not
established by this read-only check.

Both runs deliberately stop before RCCAL and recover source5840/BOOTSEL.
At that checkpoint, the next gate was implementing and exercising the delay
backend with this active epoch, then consuming a matching recovered
qualification to permit full source calibration. Those later gates passed
as recorded above; **these rate/table-only records alone do not authorize
calibration.**
[First observation](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-8105xlqi/report.json) |
[Identical-image repeat](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-peewvl1v/report.json).

## Source PHYPLL activation observed - 2026-09-21

**The missing source resource/PHY-clock gate is now physically passed.**
The [first run](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-kq4jpy00/report.json)
and [identical-image repeat](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-6hc3qdfw/report.json)
each use original source C to write D11 clock controls`0x240` at`180011e0`:
preserve HQ request`0x40`, add PHYPLL request`0x200`, never store read-only
status bits.

| Observation | Both source runs |
| --- | --- |
| D11 before request | `050b0040` |
| First complete sample | Request reads back`050b0240`; resources still`3eca3df7` |
| Second complete sample | D11`070b0240`; resources`7efa3df7` |
| PMU_MIN | Remains`0ec23df7`; no MIN writes |
| Pending at final complete sample | `0` |
| Inhibits | All six retained; three six-field checks |
| SR / MAC / PSM / DMA | SR0/SR1 stay0; MAC/PSM remain stopped; no RX descriptors published |

Both have independently CRC-valid176-I/O observations and native terminal
marker`eb000200`, value2. Observation spans60458/60460 raw counter ticks
include setup/sampling overhead; they are not measured PLL lock times or
new delay qualifications. The original32-sample/200000-tick diagnostic
limits remain unchanged.

The generic runner deliberately reports its incomplete cold-start stop
(exit1 and legacy overall activation flagfalse). That must not override
the separately validated `phypll_and_resources_observed` record. Conversely,
the successful bounded observation must not become a full-startup proof.
All images recovered through source5840 to BOOTSEL.

This explains why request acceptance was insufficient in the earlier
contrasts: ChipCommon request0, D11 FORCEHT and PMU_MIN are distinct from
the actual D11 PHYPLL request. The narrow`0x240` request is sufficient in
this tested source profile; no regulator, oscillator, PLL-data or SR
configuration copying was needed. It does not establish universal routing.
The paired D11PLL+PHYPLL request used by related drivers remains a separate,
untested combination.

**Boundary at that milestone:** active timing/table qualification and
complete bounded source calibration were still pending. Both subsequently
passed, followed by the bounded handoff above. Table20's earlier alias
qualification must not silently be generalized to another resource state.
Live DMA and fresh source-only PSM/RXE production, as well as SafeRX, remain open.

<a id="current-hardware-status-and-causal-rc-dependency"></a>
## Historical RC failure and causal resource dependency

The failure and comparison history below precedes the source PHYPLL
activation milestone above unless explicitly stated otherwise. For current
status and next work use the [handoff](handoff.md), not these earlier gates.

In those pre-activation source runs, digital reset, guard arming, board-data
binding, GCI notification, 908 table comparisons and two defaults completed
before RC. Their committed terminal result was LPF 100/100 samples, radio
`0x91=0`, zero completed passes. Later ADC tuning, VCO/DC/AGC and final
startup checks were not reached by those source runs.

Immediately before RC, **12 radio and 5 PHY register values match** between
two unchanged source runs and the working early-inhibited vendor reference.
Eight source setup readbacks also match their requested values. This rules
out those specific digital-value mismatches, not a missing internal clock
or every unobserved register/history difference.

The vendor-reference experiments retain all six [candidate inhibits](safety.md#tx-inhibition-is-an-end-to-end-contract):

| Controlled state | Repeated result |
| --- | --- |
| Inhibits applied immediately before RC, or before the radio power envelope | LPF and ADC RC passes each complete in 6 polls on four cold boots |
| PSM stopped; exact source MAC flags installed | Both passes still complete |
| SR enable bit cleared; source minimum-resource mask installed | Both passes still complete |
| ChipCommon request0 and D11 FORCEHT cleared, but shared availability retained | Both passes still complete |
| MAX changed only to exclude resources 20/21/30 | Four boots: both passes 100 polls, DONE=0, N0=N1=0 |
| Original MAX restored after that availability loss | Four boots: both passes complete in 6 polls again |

The removal/restoration contrasts check all six inhibits after the resource
transition and after RC. The vendor algorithm still runs its two bounded
passes; negative results are explicitly rejected before later ADC tuning.
These are **vendor-reference executions**, not source calibration success.
No source guard was removed, coefficient borrowed or RC limit increased.

### Requests, eligibility and actual availability

| Quantity | Controlled reference: unavailable | Controlled reference: available |
| --- | --- | --- |
| Active PMU resources | `3eca3df7` | `7efa3df7` |
| D11 clock/status with FORCEHT clear | `050b0040` | `070b0040` |
| Differing active resources | Resources 20/21/30, mask `40300000`, absent | Present |
| D11 PHY-PLL availability indication | Bit `02000000` clear | Set |

Those source runs already had MAX=`7fffffff`, so the resources were **eligible
but not active**: their pre-RC resource state was `3eca3df7`. The reference
removal uses MAX=`3fcfffff`; it does not change MIN=`0ec23df7`, which remains
a subset. Restoring MAX makes the
reference's existing requests effective again. This does not identify the
requester that a source-only startup must implement.

The exact dependency planner gives closure 29=`3eca3df7` and
closure 30=`7efa3df7`. Hardware establishes a dependency on the tested group,
not a unique individual resource or unverified names for its three bits.
Do not simply copy the vendor MIN=1 into the source no-SR branch.

ChipCommon clock/status is **`180001e0`**; D11 clock/status is
**`180011e0`**. Equal offsets within different cores do not establish
identical routing. Clearing ChipCommon request0 produced`000b0040` without
removing the active group. Clearing D11 FORCEHT produced`070b0040`, also
without removing it. Shared availability can outlive one local request.
[Register/state distinctions](phy-radio.md#clock-and-core-control-are-separate-interfaces).

Those source pre-RC snapshots directly measured D11`050b0040`
after the guarded radio-power envelope. This is stronger than the earlier
core-preparation observation, but still shows no PHY availability.

**Gate at that checkpoint:** qualify a source-owned request for the missing group and
observe D11 availability, while keeping MAC/PSM/DMA stopped and all inhibits
armed. Earlier vendor initialization, remaining SR configuration and timing
history are not excluded: clearing SR enable is not equivalent to zeroing
both SR registers or never initializing SR. Matching snapshots are not
simultaneous whole-hardware observations or analog-accuracy/RF-silence proof.

The first [source request experiments](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#source-resource-requests-tested---2026-09-20-evening)
now confirm MIN`0ec23df7 -> 4ec23df7` readback, but active state remains
`3eca3df7` and D11 remains`050b0040` over nine complete guarded samples.
An independent D11 FORCEHT request reads back`050b0042` without PHY
availability. Both hit the original200000-tick observation deadline before
the32-sample cap. These probes precede the radio power envelope; their
failure does not establish that the request could never complete later or
after the remaining power/PLL prerequisites.

The subsequent **post-power placement contrast** repeats MIN30 after31
guarded power operations, the three existing100/20/200 delay calls and a
committed pre-RC snapshot. Two identical-image runs still yield nine
complete samples at resources`3eca3df7` / D11`050b0040`, with accepted
MIN`4ec23df7`. Both stop before any RCCAL setup/trigger, at the unchanged
200000-tick diagnostic bound. Request placement alone is therefore not
sufficient under these conditions.

| Latest observation | Source before/after power | Working vendor pre-RC |
| --- | --- | --- |
| PLL control8 | `0000106a` | `00001068`, four boots |
| PMU pending | `0` at last complete source sample | Checked `0` before RC |
| Calibration | Still blocked by availability; placement probes do not trigger RC | Both RC passes complete in six polls |

The exact vendor pre-mask helper conditionally clears PLL8 bit1 when SR
is enabled; source no-SR startup deliberately skips it. Pinned related
source names the bit as open-loop control. Pending0 rules out a reported pending transition at the sampled
point, not every readiness dependency or possible later activation.
[Full placement/PLL evidence](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#pending-pll8-and-request-placement---2026-09-20-late-evening).

The2026-09-21 follow-up explicitly sets/readbacks PLL8`106a` and commits
while resources20/21/30 are ineligible, then restores their eligibility.
**The vendor path still regains availability and completes both RC passes
in6 polls on four boots.** Clearing/committing back to`1068` also succeeds
on four boots. All six inhibits remain checked. An initial rescue correctly
stopped on pending resource20; bounded settling resolves that transition
without increasing the existing cumulative deadline.

A further read-only comparison found that all31 vendor dependency words,
all31 final resource timing words and PLL controls0-7 match the data the
source implementation programs. These are live vendor reads versus shared
source constants, **not** a new full source-bank readback.
[Controlled results and retained failure](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#pll-mode-and-live-pmu-tables---2026-09-21).

The subsequent [source-powered readback](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#powered-source-bank-readback---2026-09-21)
now closes that distinction: two identical-image source runs compare all31
dependencies,31 final timings and all9 PLL controls successfully, including
PLL8=`106a`. Resource/PLL selectors27/8 are restored and checked. The
independent readback record is CRC-valid in both runs, and the later native
terminal header also confirms completion of that phase.

MIN30 still reads back`4ec23df7`; nine complete samples remain at
`3eca3df7` / `050b0040`, pending0. The read-only bank phase does not extend
the existing200000-tick request-observation window or start RCCAL.
Consequently, replacing these matching values or blindly clearing PLL8 is
not justified. Isolate remaining chip-control/regulator/request-establishment
or earlier initialization-history differences. The group-only eligibility
cycle does not erase prior vendor PLL/SR history. Source activation remains open.

### Chip-control, regulator and full-SR0 comparison

The next source capture and repeat read seven chip-control and seven
regulator words. Five values differ from the four-boot working reference:

| Word | Source | Vendor |
| --- | --- | --- |
| Chip-control0 | `1030803f` | `1010803f` |
| Chip-control2 | `00011480` | `01111480` |
| Chip-control6 | `00000000` | `00000040` |
| Regulator0 | `00000003` | `0006ab83` |
| Regulator4 | `00000081` | `00028081` |

Both paths restore the borrowed selectors, and all71 previously checked
dependency/timing/PLL values remain consistent with their declared profiles.
These are sequential readbacks, not a whole-device atomic snapshot.

The exact target ROM wrappers and initial dispatch bind the CC2/CC6
operations in the firmware's SR preparation. Removing those three bits
in reverse order, cycling resource eligibility, and checking their source-like
values **still allows both vendor RC passes to finish in6 polls**, on four
boots. Restoring the forward order also passes on four boots.

Likewise, zeroing **all** SR_CONTROL0, rather than just its engine-enable
bit, still allows resource reactivation and6-poll RC on four boots; restoring
`403900f2` with engine enable clear also passes. These experiments retain
earlier vendor history and SR1, and do not combine every difference at once.
They weaken simple current-register explanations, not all cold-entry dependencies.

No regulator settings were changed. The pinned code explains regulator0's
switcher frequency/ramp/feedback fields and the LPLDO field code in
regulator4, but effective override policy and a source-side voltage-change
sequence remain unqualified. The oscillator difference also remains unmodified.
[Exact captures, contrasts and limits](../../experimental/cyw43/r3/live-queue/README.md#control-bank-comparison-and-contrasts---2026-09-21).

Exact builds, raw reports, hashes, failed transition probes and recovery
records are in the [dependency-isolation report](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rc-dependency-isolation---2026-09-20)
and [pre-RC comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#immediate-pre-rc-comparison---2026-09-20).
The historical clock milestones below retain their original measurement domains.

## Three different clock facts

| Clock-related fact | What was established | Limit |
| --- | --- | --- |
| RP2350 timer | Early probe measured approximately 15,280 ticks over 32 USB SOF milliseconds; roughly 2.09 us/tick in that boot configuration | Do not assume microsecond ticks or reuse another image's conversion |
| Vendor CYW delay runtime | ROM `0x89213c` dispatches via RAM `0x124` → `0x1415`; getter uses RAM `0x1444` → `0x18003000`, reads `+0x90`. Scale at `0x3b4` was **82**, versus stock initial 80 | A runtime multiplier, not independent oscillator/CPU-frequency measurement or a standalone ROM service |
| Source-only CYW counter | Direct counter **`0x18003090`**, measured near **37.4 million ticks/s** with D11 reset-held | Not 82 ticks/us; no startup-wide or instantaneous rate guarantee |
| Source counter after no-SR resource activation | Same counter, measured near **82 million ticks/s** in the qualified active clock domain | Not a conversion to apply to reset-held or new clock/resource states |

Two final [clock-meter runs](../../experimental/cyw43/r3/clock-meter/README.md)
accepted **12 intervals across four source reloads**. Their conditional
common average-frequency intervals were 37,252,964–37,536,702 Hz and
37,261,054–37,512,706 Hz. They use nominal 1 ms USB SOF bracketing and
outward rounding, not statistical confidence intervals. USB-reference
accuracy, arbitrary unseen multiple wraps, sleep and clock transitions
remain outside the proof.

The measurement leaves the 5,840-byte CYW image unchanged. RP performs
counter reads and restores/verifies the backplane window before returning
to its loader. This is not direct crystal measurement. The NVRAM
`xtalfreq=37400` convention is **kHz**, corresponding to the channel
constructor's **37,400,000 Hz** branch; neither is the gSPI bus frequency.

Pinned NVRAM contains `pa2ga0=-168,7161,-820`. The earlier reference-driver
revision had `-168,6649,-778`; exact upstream commit `cf924bb0…` resolves
the difference. This is a provenance correction, not evidence that the
working coefficients were wrong or permission to retune them.
[NVRAM provenance](../../experimental/cyw43/r3/init/target-prerequisites.md)
does not establish coefficient rationale or board/front-end suitability.

The source [finite poller](../../experimental/cyw43/r3/rx-clock/README.md)
uses both raw elapsed ticks and an independent sample ceiling, rejects
late-ready observations and handles wrap within its bounded domain.
Frozen timestamps cannot produce infinite polling; a callback that itself
never returns still requires independent RP recovery. An average frequency
observation cannot certify minimum analog delays through clock transitions.

### Source-only HT request: tested prerequisite failure

This records the early reset-held attempts. The later source resource/HT
stage below succeeds; these failures are not the current RC blocker.

The [cold clock stage](../../experimental/cyw43/r3/clock-meter/cold-start.md)
now requests HT directly through F1 `0x1000e=0x10`, without vendor RAM
firmware and while D11 remains reset. Two source-only attempts observed:

```text
before 0x48 -> request 0x10 -> last 0x50 -> restored 0x48
```

HT-available bit `0x80` was not observed within either bound: 1,303 reads /
250,183 raw RP ticks, then 5,213 reads / 1,000,116 raw RP ticks.
Both failures restored the original request and the source baseline.
This narrows the cold-start gap to missing PMU/resource/core-clock setup
or required ordering. It does not prove that vendor firmware is essential,
identify the missing register recipe, or authorize forcing HT before ready.
These are raw RP ticks, not calibrated microseconds.

Three further **different** source-only attempts also failed: selecting
WakeTillHT, reasserting KSO while observing DEVICE_ON, and retaining ALP
request while requesting HT (`0x18`, readback `0x58`). Each restored its
controls and independently passed source-baseline recovery.
Sleep CSR initially read **3** (KSO and DEVICE_ON set); after selecting
WakeTillHT it read **1** before the HT request. DEVICE_ON is therefore
qualifier-dependent, not a generic HT/PLL-ready proof. These observations
weaken the simple host-wake-omission hypothesis but do not identify the
missing PMU/PLL recipe or qualify the save/restore engine.

Two subsequent passive PMU captures found a **concrete resource gate**:
maximum mask **`0x0fcefd77`** excludes the target's identified HT-related
bits **28-30 (`0x70000000`)**. All six complete register groups agreed.
HT is absent in PMU status and ChipCommon, not just F1; PMU HT/ALP request
enables are already set. SR control1 is zero, and oscillator selector is
0 rather than the exact image's 37.4-MHz table selector 15.
These are observations, **not an approved mask/PLL write recipe**.
The [full PMU evidence and source pins](../../experimental/cyw43/r3/clock-meter/cold-start.md)
explain why dependency and PLL sequencing must precede enabling resources.

The [evidence-led PMU implementation](../../experimental/cyw43/r3/pmu-startup/README.md)
now includes original C PLL transactions, exact masked-access semantics,
configured-ALP lookup and resource planning. Its selected control
transformation matches the vendor endpoint, and the exact HT dependency
closure matches vendor resource state. Native tests and M3/M33 links pass.
The [guarded gSPI binding](../../experimental/cyw43/r3/pmu-startup/hardware/README.md)
now verifies actual reads through the C access layer on the unchanged source
payload, with independent timeout/power-off recovery. Source SR control0 and
control1 both read zero. That general binding's write/delay callbacks remain
blocked. The dedicated first-preparation experiment below now qualifies a
fixed source sequence; it does not supply the complete resource/SR policy.

### Measured PLL preparation timing and effective inputs

The [startup timing probe](../../experimental/cyw43/r3/pmu-startup/timing/README.md)
has captured **16 preparations across eight fresh vendor boots**.
Argument **30** used multiplier **80**, targeting **2400 counter ticks**;
the original delay loop returned **2434 elapsed ticks** each time.
Eight independent USB-SOF-bracketed measurements at the held pre-calibration
states put the counter near **37.4 MHz**, making that loop approximately
**65 us**, not 30 us. The instrumented envelope was 2508 ticks; it is not
the unmodified analog pulse width.

Both actual configuration scans covered all nine `pllN` names with no
non-null results and with the reclamation guard active. Effective frequency,
update phases, selected table and held row readback matched the supported
profile. This qualifies **effective override absence**, not blank OTP.
This vendor experiment alone does not qualify source pulse transport or
the other delay paths. The general source mutation gate remains closed.

### Measured source-only first preparation

The [fixed source-pulse experiment](../../experimental/cyw43/r3/pmu-startup/pulse/README.md)
now executes the complete first-preparation register sequence in our own
freestanding CYW Cortex-M3 code, without vendor services or a standard C
library. Two selector-only source reloads qualified access before **four
complete pulses** across two further runs.

Each pulse initialized PMU control bit `0x200`, performed the PLL5 low-byte
update, set regulator6 bit `0x800000`, waited at least 2400 direct-counter
ticks, and cleared the bit. All four traces passed exact ordering, readback,
whole-trace CRC, code/RAM ownership and diagnostic cleanup checks.
Regulator6 read back `0x800000` then zero. PLL5 initially had low byte
`0x3c`, so its programming and restoration stores were **idempotent**.
Selectors and PMU control returned to their saved values; other-bit
difference diagnostics were zero.

The waits were 2609 ticks three times and 2595 once. The complete digital
register-high bounds were `11613..11653` ticks three times and
`11612..11652` once. Per-reload USB-SOF rate estimates give an overall
outward-rounded **308.41..313.18 us** envelope, conditional on that measured
rate persisting. Checked access, guards, readbacks and instrumentation while
the bit is high extend the source pulse beyond the vendor's approximately
65-us **delay loop**. These are different intervals, not timing equivalence.

D11 remained reset-held; CC/PMU status stayed `0x50040` / `0x2a`.
Independent source5840 recovery and final BOOTSEL checks passed after all
three runs. This validates only the fixed first-preparation source sequence
and its digital timing. Analog edges, maximum permitted pulse width, analog
rollback and RF silence remain unproved. Later PLL table/commit, resource/SR
startup, other delay paths and RX are not activated by this result.

### PLL shutdown delay and vendor preparation brackets

The separate [PLL timing observer](../../experimental/cyw43/r3/pmu-startup/pll-timing/README.md)
has now passed **eight fresh vendor boots**, four recording-only and four
with a held counter-rate reference. The hold occurs at the selected shutdown
delay entry, after the first preparation has cleared regulator6, never while
the calibration bit is high.

Argument **100** used multiplier **80** and target **8000 counter ticks**.
The original loop returned **8020 ticks** in every boot; the surrounding
instrumented interval was **8097 ticks**. Dispatch/base remained
`0x1415` / `0x18003000`, and control/CC remained `0x012403bd` / `0x50040`.
The four SOF intervals place the original loop conditionally at
**213.15..215.67 us**, not 100 us. This qualifies a fixed 8000-tick target
for this captured state, not a general microsecond conversion or the later
resource delay argument 2000.

All sixteen preparations had SET/CLEAR **accessor-call** high brackets
`2591..3285` ticks and a whole-preparation wrapper interval of 3762 ticks.
The actual stores remain in ROM; these are not exact STR timestamps or
unmodified vendor pulse widths. Only the second preparation shares the held
control state, giving conditional accessor brackets **68.86..88.34 us**.
The first preparation is deliberately not assigned that rate.

Both runs independently restored source5840 and verified BOOTSEL.
That vendor result alone does not qualify source PLL execution, resource
initialization, analog timing tolerance or RF silence.

### Complete source-owned PLL subsequence

The [source PLL probe](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md)
has now executed the selected full PLL subsequence in **two fresh
source-only reloads**, after two selector-only qualification reloads.
The separate source image is 13,136 bytes, with explicitly checked ownership
below `0x4000` and the unchanged 1024-byte stack; it is not source5840.

Each transaction retained 97 checked events: control initialization, both
preparations, bounded pending settling, MIN/MAX shutdown, 18 raw table
selector/data stores, and commit. Post-control was **`0x012403bd`**.
The waits met their `2400 / 8000 / 2400` targets with observed
`2601 / 8075 / 2601` counter ticks. The fuller checked implementation's
digital preparation intervals were approximately 15,889..15,932 raw ticks;
do not substitute the older probe's roughly 310-us result for these traces.

Trace CRCs, exact operation decoding, source memory/ROM proofs and independent
baseline recovery passed. Release-to-DONE took 4780 and 4764 raw RP ticks,
below the 10000-tick deadline. D11 stayed reset-held, SR stayed disabled and
MIN/MAX stayed unchanged. **HT remained unavailable in that PLL-only experiment:** this PLL sequence
does not restore HT bits that were absent from the initial resource masks.

The [decoded no-SR resource sequence](../../experimental/cyw43/r3/pmu-startup/contracts.md#decoded-resource-transaction-with-sr-disabled)
selects MIN `0x0ec23df7`, not the SR-enabled vendor endpoint of 1. The [resource observer](../../experimental/cyw43/r3/pmu-startup/resource-timing/README.md)
has since passed eight vendor boots. All 512 effective resource lookups were
absent with valid reclamation guards. Delay2000 used multiplier 80 and target
160000 ticks, returning 160020 or 160022 ticks. Held-state measurements put
that loop conditionally near 4.26..4.30 ms.

That vendor branch had SR0 `0x403900f3`, SR1 `0x1f8` and MIN 1.
Record-only CC changed during the interval; the held branch had stable
CC `0xb0040`, rather than source CC `0x50040`. These measurements do not
qualify the no-SR source delay. The subsequent
[source timing/resource/HT experiment](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md)
has now supplied that missing qualification and passed four complete cold
source reloads.

After source resource programming, SR0/SR1 remain zero but CC becomes
`0xb0040`, PMU status `0x3e` and resource state `0x3eca3df7`. The source
counter accelerates to roughly 82 MHz. A 160000-tick trial was about 1.95 ms,
so it was not accepted for the nominal 2-ms settling delay. A new
**200000-tick** trial measured 200207 / 200205 ticks, conditionally
**2.438..2.467 ms**. Preflight checks the target against the measured upper
frequency bound, not just whether an arbitrary tick count was reached.

The complete source resource stage uses the measured domain and a bounded
clock-state wait; it does not force a clock or enable SR. All four runs reached
MIN `0x0ec23df7`, MAX `0x7fffffff`, with D11 reset/IO `1/0`. F1 already
reported HT available (`0xc8`); a normal request produced `0xd0`, and the
original request restored to `0xc8`. Source5840 and BOOTSEL recovery passed.
This is a clock-startup result, not PHY calibration, source reception or RF
silence. The duration bounds remain conditional on nominal USB SOF timing
and the measured rate persisting.

The later native PHY-released/no-SR/HT-held MODE13 experiment qualifies
conditional minimum delays for arguments 1, 10, 20, 32, 35, 50, 100, 200.
Checked callback overhead gives an approximately 60-us floor, not exact
1-us pulses.
Neither analog maximum-delay tolerance nor transfer of that qualification
to a new resource/clock profile is established.
[Physical short-delay evidence](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md).

### Vendor observer timing is still raw

The [completed RXE observations](vendor-observations.md) have a maximum
retained-record span of **337 raw CYW counter ticks** across 908 records.
Do not convert this using the source-only 37.4 MHz result or assume the
vendor multiplier is an independently calibrated conversion.

The observer period spaces **retained records**, not actual full-register
polls; every poll still executes the sampler. The record cap omitted
1,652 / 3,446 selected observations, so neither rate nor overhead is
qualified. The 921-tick spacing between one publication-containing sample
and its successor is not a measured completion latency.

The CCK PLCP candidate's **64-microsecond length field** is independently
decoded from protocol bytes. It is not a counter conversion and must not
be used to claim calibrated observer timing or a source startup delay.

## Channel masks are not an RF-safety policy

The exact channel path `0x874a14` tests **`0x0a06`** for an early parameter
copy and **`0x1a06`** to select calibration argument `0x1f0` or `0x10`.
Neither hold mask includes software mute bit **`0x10`**.

The hold-word mute bit and calibration-mode argument `0x10` are different
inputs that happen to have the same number. The dispatcher `0x86fa64` can
resume MAC before checking a hold guard, and has unconditional setup/cleanup.

| Calibration mode bit | Related LCN20 name | Exact target call |
| --- | --- | --- |
| `0x10` | DC (DCOE + IDACC for mode 3) | `0x86fdd4(pi,3,10)` |
| `0x20` | TXIQLO | `0x874420` |
| `0x40` | RXIQ | `0x872770` |
| `0x80` | PAPD | `0x874bb8` |
| `0x100` | DSSF | `0x8700bc`, additional condition |

The related source's broad mask is **`0x3f0`**, including XTALPN `0x200`;
the exact target uses **`0x1f0`**. Names do not override target bytes.
Selecting only DC does **not** remove unconditional dummy-TX work.
[Channel/guard analysis](../../experimental/cyw43/r3/phy-access/README.md).

## DC and the direct template-transmit path

The DC force routine:

- Sets PHY `0x965` bits `0x000c` and `0x440` bit `0x0004`.
- Configures PHY `0x975` mask `0x004f` using
  `(((~mode)&7)<<1)|1`; observed mode 3 gives `0x0009`.
- Pulses PHY `0x44a` **bit 0**, then delays **32 decimal (`0x20`)**.
  The related source's 50 decimal (`0x32`) is not the target constant.
- Tests up to ten attempts for `(PHY[0x978]&0xff)==0xff` and
  `(PHY[0x96f]&0xfff)==0xfff`.
- Cleans up on success **or exhaustion**, without explicit failure status;
  mode-zero cleanup is not restoration of every previous register.

The complete calibration dispatcher calls `0x870620` outside the mode
conditionals. That path unconditionally reaches
**`0x86d194(pi,1,0)`**, optionally twice. Its third argument is overwritten:
passing zero is not PA shutdown.

The exact routine copies a **20-byte template** via 32-bit D11 ports
`+0x130/+0x134`, then writes 16-bit TX registers. Important established
operations include `+0x50c=0x0041`, `+0x514=0x1a02`,
`+0x56a=20`, `+0x568=0x0826`, and **`+0x502=0x00d0`**.
Related Broadcom source names the last operation TX_NOW/MK_CTLWRD in a
dummy-transmit helper. This bypasses the inspected host TX DMA callbacks.
“Dummy” is not “receive-only,” although register writes alone do not prove
RF escaped the chip.

It polls `+0x50e` mask `0x0080` clear, `0x0400` set, and `+0x690`
mask `0x0100` clear. Exhaustion can still reach subsequent work/normal
return. Counter limits and nominal delay 10 are not verified wall-clock
bounds. [Full calibration transaction](../../experimental/cyw43/r3/phy-access/calibration.md).

## RF setup and cleanup do not establish a persistent inhibit

Setup `0x873b5c` requests a 512-byte buffer. If unavailable, it returns
without the later RF/table setup, yet its caller still reaches the direct
transaction. Captured-code execution verified this software failure branch;
it did not simulate emitted RF or a successful RF setup.

Other established pitfalls:

- Power-control OFF is followed by a table clearing PHY `0x4a4/0x4000`
  and setting `0x4a4/0x8000`; a transient mode request is not an invariant.
- Zero BB multiplier, TSSI loopback attenuation and envelope-detector
  gain are not independently proved PA-off controls.
- Gain restoration is not a whole-state rollback; it can re-enable paired
  overrides at PHY `0x4b0/0x0080` and `0x43a/0x0040`.
- Clearing PHY `0x44c/0x4000` **releases** the internal TX-chain override;
  it does not force its value off.
- Power index 127 is a table index, not a measured RF output level.

[RF setup/restoration evidence](../../experimental/cyw43/r3/phy-access/rf-overrides.md)
and [candidate interlocks](safety.md#tx-inhibition-is-an-end-to-end-contract)
must be read together.

## VCO protocol

Exact trigger/wait bodies are `0x8720a4` (220 bytes) and `0x872180`
(72 bytes). The trigger sets radio `0x8a/0x0100`, programs the reviewed
`0x1b/0x51/0xa0/0x42` fields and uses nominal delays **1,1,1,1,50**.
Wait delays **10** before each sample of radio **`0x65 & 1`**, at most
100 samples; the selected caller then requests **100** settling delay and
clears `0x8a/0x0100`.

The vendor completes settle/release even after exhaustion. The checked C
implementation rejects exhaustion, late completion, cancellation and I/O
failure, sets reset-required and does not run success-shaped release after
failure. A set status bit is not yet proof of **fresh calibration or PLL
lock**. Clock/delay callbacks require matching physical qualification; the
active-domain calibration result above does not qualify other clock profiles.
[Protocol and CPU comparisons](../../experimental/cyw43/r3/safe-rx-readiness.md).

## Radio cold-start: RC, ADC and TIA boundaries resolved

The complete enclosing radio initializer is **832 bytes** at `0x871668`.
Three formerly opaque nested routines now have source C counterparts:

| Entry | Resolved function |
| --- | --- |
| `0x871b28` | Two-pass RC calibration: LPF, then ADC |
| `0x8714c4` | ADC coefficient arithmetic and application |
| `0x871fe0` | TIA radio-register lookup population |

### RC calibration

| Register/operation | LPF | ADC |
| --- | ---: | ---: |
| Radio `0x1e`, mask `0x40` | `0x40` | `0x40` |
| Radio `0x1e`, mask `0x30` | 0 | `0x10` |
| Radio `0x8f`, mask `0xff00` | `0x1c00` | `0x7000` |
| Radio `0x90`, mask `0x1fff` | `0x22d` | `0xf0` |
| Result multiplier | **101541** | **102051** |

Each pass toggles radio `0x1e` bit 0 with nominal delays 1/35, sets
`0x8f` bit 0, and samples `0x91` bit 0 after delay 100 up to 100 times.
Counts N0/N1 come from `0x92/0x93`, each masked to **13 bits**:

```text
coefficient = multiplier * (N1 - N0) / (xtal_hz >> 12)
```

The initial `0x8a` MOD supplies value including `0x4400` but mask
`0x07f0`: it sets `0x0400` and **preserves**, not forces, `0x4000`.
Radio `0x94` bits 5–9 provide a DAC-buffer observation; no third pass runs.

Vendor code continues after poll exhaustion. Source C requires an explicit
supported oscillator, rejects bad/late samples, `N1<=N0`, zero or over-16-bit
results, and publishes outputs only after both passes and final release.
This deliberate fail-closed difference is not analog validation.

### ADC, TIA and remaining boundary

ADC arithmetic produces **eleven tuning halfwords**, preserving target
32-bit multiply wrap, signed shifts, division and clipping. Application
performs **27 masked radio modifications**. TIA writes **52 bytes followed
by 56 halfwords**, **108 registers** at indices **`0x00a9..0x0114`**.
Arrays are explicit caller inputs; captured values are fixtures.

Related source differs materially: ADC multiplier **75000**, second `sc`
setting **2**, and **109** TIA entries, versus target **102051 / 1 / 108**.
Do not port it as the exact initializer.

The enclosing prefix includes radio `0x9b/0x10` and `0x19/0x08`,
corresponding to TX-LDO override/power-up; later DAC/PA settings also remain.
The whole shared-power/common-table/channel sequence is not qualified as
minimal RX-only startup. Finishing these nested implementations did not
close that gate. [Exact cold-start evidence](../../experimental/cyw43/r3/phy-tables/radio-cold-start.md).
