# Source-radio handoff for the next engineer or LLM

## Resume here - cold implementation checkpoint 2026-10-09

**Latest investigation:** the owner-selected source RXE diagnostic is now
implemented, strictly qualified and physically sampled. Source `b7c96b54` /
RP `fd5cfb1d`, immutable PSM `73fd0ced`, uses fresh MODE0 `p3nxef8y`,
MODE2 `gq1i8r7j` and dedicated active-only replay `4pxkftu1`.
Unarmed no-AP `zw6yfadm` and same-image AP `vlsclqe1` capture bound HOF8
negatives with two complete 20-I/O, zero-primary-fault-bracketed samples:
pre-PSM `0800/f000/0000/0000`, post-MAC `0800/f020/0000/0000`.
Both reach MAC then controller time error 6, mailbox `[7,3,0]`; no frame.
AP counters `1 -> 167` bracket 166 submissions, not receive-window airtime.
Both badges independently recovered and were queried in BOOTSEL; frozen
recovery assets are unchanged.

Read [implementation, commands and exact scope](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#opt-in-pre-fault-rxe-diagnostic---2026-10-09).
Current MODE14 outputs are opt-in HOF8; default selection is still off/HOF7.
No pause, wait extension, inhibit relaxation, register replacement or PSM
change is included. Normal/private stack bounds remain 864/2,128.
The direct control pair matches two retained observations at a different
sampling phase; this is not listening or whole-state proof.
Next: establish stimulus overlap before inferring a lower RX/event defect.
General lifecycle, live DMA, persistent TX inhibition and broader
qualification remain deferred.

**Previous read-only comparison:** strict replay of three retained full-frame positives
and three cold pause-candidate negatives confirms the positives have the same
six actual candidate-inhibit readbacks before MAC and at terminal capture.
Their selected 21-register comparison differs only in radio `92/93` counts;
one pair matches all selected values, but the retained comparison combines
pre-guard getters with six later guard reads and is not atomic or whole-state
equivalence. Retained enabled MAC `c0020403` versus cold `84020403` differs
in WAKE/DISCARD_PMQ; do not change those bits on this evidence.
The [comparison and diagnostic boundary](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#retained-full-frame-comparison-and-missing-live-rxe-sample---2026-10-09)
identified the Cortex-M3-only, opt-in RXCTL/CORECTL/length/status snapshot
before PSM and immediately after MAC that is now implemented above.
Bracket post-MAC reads with sticky primary-fault checks, abort on observed
fault, mark incomplete brackets invalid, and qualify a new source image.
Do not repin the immutable PSM, widen waits, relax inhibits or bypass the
retained stimulus gate. No hardware or production source changed in this
read-only comparison.

**Previous pause rollback:** the original-PSM pause retry is complete with no cold frame and
no promoted candidate. Baseline source `ab121779` and both downloaded
controller binaries are restored byte-identically. Rebuilt RP `316c8db6`
passes strict receiver preflight; it is not a new physical receiver trial.
The controller manifest's decoded contents match the physically tested
`0c214ad4`, but stack-proof map key ordering changed its byte hash/RP identity.

Candidate `ef634564` / RP `4d5d8688` had exact fresh MODE0 `auxlkgxi`,
MODE2 `jgancq25` and active-only `s_p0xo0k` prerequisites. Unarmed/no-AP
`9q1ulo9r` reached MAC and controller timeout 6. Paused controls `enxgo1x2`
(no AP) and `pjkfmroa` (AP) passed both READY checks, reached MAC, and ended
with primary PSM fault 6 `[7,3,6]`. All three committed CRC/request-bound
negative reports and independently recovered. The AP was separately
stopped/recovered; its 181 submissions are not receive-window airtime proof.
No direct PSM-PC preservation or continuous listening was observed.

Read the [pause retry and event-wait boundary](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#original-psm-pause-retry-and-event-wait-boundary---2026-10-09).
Fault 6 is bounded condition-14 wait exhaustion. The PSM writes fault before
its raw fault snapshot/state 15; the controller samples raw words before
state/fault and aborts immediately at fault. Do not infer final zero hardware
registers from raw zeros, rewrite state 3 to 15, or reread after primary fault.
At that rollback checkpoint, next was bounded pre-fault evidence distinguishing
stimulus overlap from cold RX/event configuration, not an identical trial,
widened wait or silently replaced PSM. A new diagnostic PSM/profile needs separate explicit selection
and qualification. Candidate/restored native tests passed 81,161/80,571 fresh
and 30,574 legacy cases respectively. Both serials are in BOOTSEL; frozen
recovery hashes are unchanged. The four broader workstreams stay deferred.

**Earlier baseline capture checkpoint:**
Fresh MODE0 `28s09qt9` and MODE2 `cufji_e2` pass both reloads and recovery.
Active qualification-only `tcrgaak2` ends deliberately at `ED000001`/908;
the dedicated replay accepted it during receiver construction despite generic
runner exit 1. The earlier zero-SOF failures are not explained by these passes.

The fixed RP captures committed CRC/request-bound reports in two same-image,
no-AP controls: unarmed `j2bue2dm` reaches MAC and controller timeout 6 with
`[7,3,0]`; released `c5elo3j5` sees primary fault 5 `[7,15,5]` before MAC.
This physically verifies report capture for these controls, not that a race
was exercised or that every old report loss has the same cause.

The host checker now source-binds a checkpoint-held timing class instead of
applying the unarmed five-I/O limit to 393-I/O paused/revalidated spans.
Twenty host tests pass, including bounds and changed-generation rejection.
Archived negative outcomes remain negative; no frame/RF acceptance is relaxed.
Unarmed preparation consumed about 64% of its shared 20M controller budget;
held operation has a separate post-release bound. Do not compare these as equal
RF listening windows or change the unarmed deadline without an explicit design.

The [capture and held-timing follow-up](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rp-capture-controls-and-held-timing---2026-10-09)
led to the subsequently completed pause retry above.
No pause or diagnostic startup was selected during that earlier phase. All hardware trials
independently recovered; both serials were queried in BOOTSEL, and no AP was
started during that earlier capture phase. No cold frame.

## Previous implementation checkpoint 2026-10-08

**Latest finding:** a fixed-low-layout diagnostic saved software cold-stage
error `4e41000a`, not a captured CPU exception. The RP supervisor can lose a
valid final report by reading zero HOF commit before source publishes HOF/FAULT,
then examining the later FAULT. A deterministic native regression failed before
the fix and passes after collecting mailbox RAM before the completion marker.
Fresh/legacy PSM and ordinary handoff supervisor/USB tests, restored controller
tests, four exception tests and real Cortex-M3 execution pass.

The fix is retained in RP source, but **physical requalification is blocked**.
The first receiver comparison was rejected before flash by guard freshness.
A refreshed diagnostic MODE0 passed; two identical MODE2 attempts observed no
USB SOF advance and failed the unchanged timing gate. Their source and RP
Thumb instructions match the previous passing MODE2 image; only RP build-ID
literals differ. Do not infer the SOF failure's cause or bypass qualification.

Temporary controller/pause wiring was removed, and source `147dc17f` is again
byte-identical. RP `0cd9e65e` contains the reporting fix but old prerequisites
fail strict preflight with `Fresh matching-code MODE0 required`; it is not a
new qualified flash image. Both badges were queried in BOOTSEL, and all actual
hardware runs independently recovered. No AP or frame in this follow-up.

Resume with the [RP publication race and qualification blocker](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rp-final-publication-race---2026-10-08).
Obtain fresh baseline MODE0/MODE2 USB-SOF/active evidence, then verify the RP
fix physically and correct held-versus-unarmed timing before a pause candidate.
The exact linked diagnostic ELF/map are now retained beside `rhi835vn`.

**Previous investigation:** a separately qualified local exception-PC diagnostic
and original-PSM pause candidate committed negative reports in unarmed/no-AP,
released/no-AP and released/AP controls. Both released controls observed MAC
and primary PSM fault word 6, not the earlier pre-MAC fault 5. No frame or
failing Cortex-M3 PC was obtained. The extra Thumb instructions and stack
bounds match the rejected candidate; only 16 literals/data words differ.
This is not a proved publication fix. The held controls expose a host gate
that assumes at most five unarmed READY-to-MAC I/O attempts instead of the
393-attempt paused/revalidated path; no checkpoint qualification is claimed.
All recoveries passed and both serials were queried in BOOTSEL.

The diagnostic and pause candidate were removed from the active builder.
Source `147dc17f` is again byte-identical and strict preflight passes with the
original prerequisites. The standalone exception capture tests remain available
but their startup is not selected by production builds.
Resume with the [publication diagnostic and remaining gates](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#publication-diagnostic-and-pause-controls---2026-10-08):
isolate the low-bootstrap/data or timing sensitivity, retain the next linked
diagnostic ELF, and source-bind held-path timing evidence before promotion.

**Previous follow-up:** the AP build blocker is resolved by a strictly bounded
eight-byte report-storage audit. The owner-selected original-PSM pause/resume
candidate failed before source handoff publication, even in an unarmed control;
an inline variant also failed. Neither is retained as the active controller.
Source `147dc17f` was restored byte-for-byte and RP `e5930e3a` passes strict
preflight. The restored unarmed AP trial `c3s6zjpa` again commits the bounded
MAC/event-wait/error-6 negative report and independently recovers both badges.
The stronger active native tests pass 80,571 fresh and 30,574 legacy cases.
No cold frame or verified PSM-PC resume was obtained.

Resume with the [latest candidate and rollback evidence](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#ap-storage-audit-and-rejected-pause-experiment---2026-10-08):
diagnose the source-publication regression before retrying synchronization.
The implementation notes below describe the earlier revalidation checkpoint;
their AP blocker and RP build ID are superseded by this follow-up.

The owner explicitly authorized first-frame implementation and experiments on
both attached badges, including vendor and newly built firmware. The
[guide](README.md#latest-cold-checkpoint-2026-10-09) owns the updated conclusion:
**no cold frame yet**. Do not expand this milestone to general rearm or DMA.

- **Implemented:** post-host-hold stable PSM READY revalidation before MAC.
  A native regression reproduced the stale-READY bug before the fix. Fault,
  changed generation/state and unstable snapshots now fail closed; fault-first
  handling, original READY timing and all budgets remain unchanged.
- **Validated:** 41,685 fresh and 30,574 legacy native transport cases under
  ASan/UBSan; fresh cold-stage integration and Cortex-M3 private-stack execution;
  RP supervisor and 1,065 identity-bound USB checkpoint pages. The linked receive
  image retains normal/private bounds 864/2128 bytes, ceilings 864/4096.
- **Current prerequisites:** MODE0 `yh9l2khs`, MODE2 `1qwyx9_9`, active-PHY
  qualification-only `nfx3dqpg`. The latter deliberately stops at
  `ED000001`/908; its generic runner exits nonzero, but the dedicated strict
  qualification replay accepted it during receive-image construction.
- **Current receive image:** source `147dc17f` / RP `fc548b94`, strict preflight
  passed. Original immutable PSM ABI7 `73fd0ced` and frozen source5840 recovery
  assets were not changed or repinned.
- **Physical controls:** released no-AP `ui_ih__v` observes fault 5 before any
  MAC request; unarmed no-AP `yr2n4xju` observes MAC then bounded error 6 in
  event-14 wait. Neither is a frame or RF proof. Both independently recover.
- **AP blocker:** `aae4639x` failed AP source verification before flashing
  either badge. Its isolated current-source rebuild failed frozen non-opt-in
  `main.o` comparison (0 TU); the failed rebuild has no accepted manifest.
  Do not flash its partial outputs, change frozen pins or bypass the check.
- **Resume with:** investigate a bounded PSM/host synchronization design and
  audit the AP's non-opt-in build difference. Preserve the retained burst's
  receiver-baseline/pause prerequisites. No claim of missing airtime or a PHY
  defect follows from the no-AP controls.
- **Last queried hardware:** research `9BBF88DD17D66E7D` and reference/AP
  `08DA26F86E57F5EA`, both independently queried in BOOTSEL after these tests.

Exact records and reproducible checks are in the
[implementation checkpoint](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#cold-ready-revalidation---2026-10-08).

## Documentation checkpoint 2026-10-08 (before implementation authorization)

The [guide's current status](README.md#current-status) is the consolidated
status owner; its [active milestone](README.md#active-milestone-cold-source-acquisition-of-a-fresh-frame)
defines first-frame acceptance and its [next steps](README.md#next-steps-after-first-acquisition)
defer general lifecycle, live DMA, persistent RX-compatible TX inhibition and
broader qualification. That earlier task stopped after documentation verification:
no code/build/image change, new hardware run or permission to resume testing.

- **Next implementation objective:** a fresh complete frame after source-owned
  cold initialization. Vendor-retained PSM successes are a comparison, not a
  cold proof. Do not add DMA or general rearm to this milestone.
- **Latest physical checkpoint:** 2026-09-24, source `f5b89561` / RP
  `0ca84492`; released condition-36 fault-5 and unarmed event-14-wait controls
  remain negative. Timing correlation does not establish the cause.
- **Last recorded hardware state:** research `9BBF88DD17D66E7D` independently
  recovered through source5840; reference/AP `08DA26F86E57F5EA` stopped;
  both queried in BOOTSEL. This is not live discovery.
- **Before any future run:** explicitly select scope/serials, follow
  [workflow](workflow.md), verify exact current-code prerequisites and
  [frozen recovery assets](firmware.md#frozen-recovery-assets), and retain
  [experimental controls](safety.md). First-frame work does not waive them.
- **Evidence reviewed:** local documentation links/anchors, the retained
  three-positive/two-control index and its 15 report/proof/outcome hashes,
  selected September 24 raw reports/outcomes, source component contracts and
  all three frozen recovery hashes. Software/hardware suites were not rerun.
  See [verification scope](references.md#documentation-verification---2026-10-08).

## Checkpoint history

The dated entries below preserve original results, failures, build identities
and next-gate decisions. "Current", "latest", "next" and NO-GO in older entries
refer to that checkpoint, not today's task list. The 2026-09-21 experimental
policy supersedes earlier blanket activation prohibitions, not qualification
requirements. Never combine prerequisites or fault-code meanings across images.

**2026-09-24 two-badge unarmed AP control:** new host-only opt-in
`--ap-unarmed-control` reuses the unchanged qualified source
`f5b89561` / RP `0ca84492` and normal bounded 100-TU vendor AP. One
[physical trial](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-xkxw1xh0/outcome.json)
recorded active AP submissions rising 0→186 across receiver
flash/capture; source first-cycle mailbox `[7,3,0,...]` and
READY-to-MAC sample gap **64,650 ticks**, followed by event-14
phase 10/error 6. This matches the earlier unarmed no-AP result,
contrasting with fault 5 at event 36 and a 4,365,418-tick sampled
gap in the released AP trial below. The strict frame verifier found
**no source frame** (runner exit 1). Source5840 restoration, AP stop
and both badges' independently queried BOOTSEL passed. AP counters
are not RF airtime, and the source's event-14 progress is not proof
of RXE listening or the cause of the release-only event-36 fault.
See the [two-badge diagnostic
contract](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#two-badge-receive-experiment-stimulus-versus-receiver-blocker).
Do not use the tagged GPIO monitor as an RF witness; it changes pad
configuration and has millisecond-scale, display-filtered edges.

**2026-09-24 AP/receiver timing contrast:** one bounded, unchanged-image
[AP trial](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-vuzj6vqu/outcome.json)
passed strict preflight on source `f5b89561` / RP `0ca84492`. Host-monotonic
brackets put the AP start request 32.84 s before READY and an active
preflash AP status 27.30 s before READY (zero submissions); the active
postcapture status counted 187 submissions. A5 ACK was 16.25 ms after
READY, the MAC-bearing A6 read was bracketed 33.52–45.56 ms after READY,
and the last no-STOP A6 read 45.63–47.41 ms after READY. The RP measured
3,555 us between MAC readback and its last no-STOP observation, with
only one early MAC-bearing A6 sample. AP lab traffic is nominally 200 ms
apart, beacons 100 TU; neither status counter timestamps airtime in that
short receiver interval. The strict verifier rejected the no-frame trial
(exit 1), event-36 fault 5 persisted, and source5840, AP stop and **both
independent BOOTSEL checks passed**. The
[timing interpretation](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#apreceiver-timing-contrast-2026-09-24-no-rf-witness)
does not establish on-air alignment or Safe RX. Do not bypass the
guarded burst's baseline-frame/pause gate to force alignment; an
independent RF observer and a separately reviewed READY-timed stimulus
are still needed.

**2026-09-24 host/PSM timing follow-up:** unchanged qualified source/RP
images were retested once without an AP, retaining
[raw checkpoint and PSM evidence](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-6jwtxspj/report.json).
There were 2,754 A4 attempts over 23.37 s; the final READY A4 took
9.90 ms and A5 ACK took 16.19 ms after a 77-us host
READY-to-A5-request gap. The separate PSM counter reported 4,391,486
ticks READY-to-MAC, then event-36 fault 5 with mailbox
`0x0800/0xf020/0`. No source frame was bound; the runner correctly
exited nonzero on its strict verifier. The
[outcome](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-6jwtxspj/outcome.json)
shows source5840 and final BOOTSEL recovery; both badges were separately
observed in BOOTSEL. A4/A5 host time is not aligned to the PSM clock or
source release-consumption time. No event-36 pending address has been
validated, the fault-time mailbox is not an atomic RXE snapshot, and
there remains no independent RF observer. Do not claim an event-36
cause, a frame or Safe RX from this trial.

**2026-09-24 additional physical no-AP diagnostic:** strict preflight
accepted unchanged source `f5b89561` / RP `0ca84492`; a single bounded
[release trial](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-x3j16yyr/report.json)
reproduced fault 5 after MAC readback with raw source mailbox SPR samples
`0x004=0x0800`, `0x078=0xf020`, `0x00c=0`, and a 3,732,354-tick
READY-to-MAC sample gap. The strict verifier **rejected** the no-frame
trial (runner exit 1). Its [outcome](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-x3j16yyr/outcome.json)
confirms source5840 restoration and BOOTSEL. The AP badge also remained
in BOOTSEL. These are source-written mailbox samples, not a coherent
event-36 pending bit, a received frame, or electrical RF evidence. With
no independent RF witness, Safe RX is still **unqualified**. The tagged
[GPIO monitor](../../src/tools/gpio_monitor.c) was not attached; it
changes pad controls and cannot witness RF or internal PSM conditions.

**Event-36 next-gate replay, 2026-09-24:** identical source/RP hashes
give [unarmed](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-d9gmw3t3/report.json)
READY-to-MAC samples of 64,650 ticks (four I/O) and a later state-3
event-14 wait. Three [released](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#event-36-timing-contrast-existing-hardware-no-new-firmware)
checkpoint trials show 3.72–4.39 million ticks (five I/O) and the
event-36 fault-5 mailbox state. A new fail-closed host replay checks those
bound markers and image identity without modifying any target code or
reusing the SOF diagnostic as qualification. READY-to-A5 host timestamps
do not bound source consumption. This is a **correlation**, not proof of
why event 36 is absent. Correction: the current RP fault-first reader
**does** fetch source-written mailbox 4–6 before the fault, retaining raw
SPR `0x004=0x0800`, `0x078=0xf020`, `0x00c=0` on three release trials.
It aborts before fetching separate diagnostic SHM, so those reported
zeros are not sampled SPR `0x00d`/`0x00e`. The mailbox samples are not
atomic or proof of event-36 status/RXE listening. Do not bypass the
fault-first policy or assume event 36 has a known MMIO pending bit. Before any
receive-image target change or flash,
establish a reviewed, read-only register mapping and bounded
READY/MAC/terminal telemetry; changed inputs require fresh MODE0, MODE2
and active-PHY qualification. No GPIO monitor or on-air observer was
attached; Safe RX remains unproved.

**2026-09-24 isolated SOF follow-up:** the [opt-in MODE0 link
diagnostic](../../experimental/cyw43/r3/clock-meter/device/sof-link/README.md)
ran once on the research badge with distinct RP `ab784bfd`/test ID
`0x43a6`. Its [strict outcome](../../experimental/cyw43/r3/clock-meter/device/sof-link/runs/sof-link-9a0_hx7b/outcome.json)
has six verified meter intervals and recovered source5840/BOOTSEL. The
[SIE/SOF snapshots](../../experimental/cyw43/r3/clock-meter/device/sof-link/runs/sof-link-9a0_hx7b/proof.json)
show connected/not suspended/line-state 1 at request start and probe
return, with SOF 309 and 783 modulo 2048. This healthy trial **did not
reproduce** the intermittent zero-SOF failure and does not establish its
root cause or continuous link state. It is separate from the receive
image's MODE0 prerequisite, which must not be replaced by this diagnostic.
There is still no on-air witness, cold frame or Safe RX proof.

**2026-09-24 no-observer checkpoint follow-up:** the separate fresh
source `f5b89561` / RP `0ca84492` image passed strict preflight against
recovered [MODE0](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-0-xcj54q5p/outcome.json)
and [MODE2](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-2-q73l0m3h/outcome.json);
its active-PHY prerequisite passed its dedicated strict diagnostic verifier.
[No-AP release](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-9stkmwgz/report.json)
and [withhold](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-7f13ddgr/report.json)
exercise A3/A4/A5/A6 independently; only the release reaches MAC readback.
The [100-TU AP contrast](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-3mygdekm/report.json)
reports 368 AP submissions, but neither it nor an
[immediate three-A6 no-AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-at8vnq6e/report.json)
has a source frame or enough early A6 samples for the conditional
three-poll overlap test. Release trials have a 3.5-ms MAC-readback-to-
last-no-STOP observation interval and controller evidence error 9, with
source mailbox fault 5: the pinned PSM exhausted its event-36 MAC wait.
No independent RF observer is available. All research trials restored
source5840 and BOOTSEL; the AP contrast stopped/recovered both badges.
This is **not** Safe RX, RF-silence qualification, or a replacement for
the distinct older HOF7 trials below. Use the
[checkpoint contract and current limitations](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#opt-in-fresh-ready-checkpoint-handshake-tested-rx-not-qualified)
before any further run; do not reuse an older physical proof for a changed
source/RP image.

**Checkpoint: 2026-09-22. Read this before testing.**
This is the current starting point, not permission to flash. It supersedes
older "next step", pause and failed-startup summaries, not their raw evidence.
No conversation history or previous agent session is needed to use it.

**Corrected HOF7 follow-up, 2026-09-22:** the shared-version RP fix was
rebuilt and physically requalified with recovered fresh
[MODE0](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-0-o7j90a3q/outcome.json),
[MODE2](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-2-1_3oyc44/outcome.json)
and [active PHY](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-3xjnddi0/outcome.json).
The active diagnostic exit passed the dedicated strict replay. The new source
`ef4981a3` / RP `7a318800` passed preflight. Its
[supervised AP100TU trial](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-wvp9f997/outcome.json)
reached a committed [PSM report](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-omem2ud8/report.json):
phase 10/error 6 after the unchanged 20-million-tick budget, with MAC-enable
readback observed, mailbox state 3, no first full snapshot and no frame.
The five HOF7 boundary samples show 12,545,912 ticks and 11,780 I/O
attempts through MAC readback; the remaining sampled interval was 7,481,795
ticks. These measurements include guarded MMIO and do not isolate on-air
receive time. The AP stopped; research source5840 recovery and both BOOTSEL
checks passed. **This fixes the RP version rejection, not cold RX.** DMA
activation remains gated on strict cold frame proof, and RF safety still
requires independent measurement.
A second [20-TU supervised trial](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-7ud5lvpn/outcome.json)
with the **same** qualified receiver also ended at committed PSM phase 10 /
error 6 with MAC-enable readback and no frame; its sampled interval after
the MAC-enable attempt was 7,481,798 ticks. Its AP shut down and both badges
recovered. Shortening the beacon cadence alone did not produce a cold frame.
The source PSM's sampled state 3 is **EVENT_WAIT**, not MAC_WAIT: it passed
the MAC-enable wait and post-MAC EOI sweep. However, neither trial completed
its first post-MAC snapshot (`polls=2`, `snapshot_stable=0`): the second
poll consumed the remaining ~7.48 million ticks while reading the
mailbox, diagnostic and frame scratch twice. The state-3 value is from
that incomplete poll, **not** continuous observation through the deadline.
It establishes no observed event 14 by that sample, but cannot exclude
an event, fault or frame after the mailbox read. Inspect RX PHY/clock/PLL
state and the event-14 path within the existing bounded fault policy.
Neither AP-only nonce nor a controller PSM timeout
proves a newly generated frame, and no independent RF instrument capture
has been recorded.
Do not treat the clear PHY `0x44d` RF-PLL override bit as an established
cause: the [selected working vendor-retained comparison](phy-radio.md#selected-cold-versus-vendor-retained-state---2026-09-22)
also read `0x44d=0`. The guarded RF-PLL-power contrast exists for the
older prefix profile, but the fresh HOF7 builder explicitly rejects it;
its native model passing is not authority to bypass that profile gate or
flash a changed image. No RF-PLL-power contrast was run on hardware.

**Event-14 investigation after the corrected trial:** both HOF7 reports
contain ABI7 mailbox `[7,3,0,...]`. In the exact
[source PSM program](../../experimental/cyw43/r3/psm-rx/fresh/program.json),
state13 is published before the MAC-enable condition36, whereas state3
is published only after that condition and the post-MAC event-EOI sweep.
State3 then waits on condition14 (the PLCP event) before requesting any
prefix copy. The sampled state is neither a MAC handshake timeout nor
rejection of a captured PLCP/FCS, but the partial snapshot does not
locate the source's final state. The
[100-TU](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-wvp9f997/ap-stop.json)
and [20-TU](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-7ud5lvpn/ap-stop.json)
AP stop reports contain 397 and 399 submitted lab frames over their
~88-second runs (within 180-second leases), but
provide no timestamp/air confirmation for the short source receive window.
Both AP profiles send these lab frames at about200-ms intervals; 20 TU
changes beacon cadence, **not** that lab-frame interval. Do not infer
post-MAC challenge delivery from total submissions.

The new cold reports each contain a 21-register live post-calibration
fingerprint at consumer release. Comparing it with the checked
[working retained snapshot](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json)
shows only the known PHY`44c`/`938` and radio`a4` candidate-inhibit
differences plus small radio`92`/`93` calibration-count differences.
The other16 selected values, including PHY`44d=0`, match. This is a
**selected pre-PSM** comparison, not proof of identical live RX state
after MAC enable; inhibited retained prefix captures cannot exclude
queued vendor-origin frames. The earlier
[cold no-event investigation](#no-event-investigation---latest-2026-09-21-checkpoint)
already tried the RF-PLL bit, SPR049, SPR024, staged MAC enable and
20-TU beacons without an event. Do not repeat those as a purported fix.
The unresolved discriminator is whether the PHY/RXE is producing a
fresh PLCP/event14 during the source-owned window. A next bounded
experiment needs independently witnessed on-air stimulus aligned with
that window and/or a guarded source-owned RX status/event snapshot;
changing either firmware or its read plan invalidates the current
qualification identity and requires fresh prerequisites.

**Read-plan experiment, 2026-09-22:** a nonterminal mailbox-only polling
variant retained the original 20M-tick/I/O/service/inhibit bounds and
full twice-checked terminal capture. A targeted native timing regression
failed with the old full-every-poll plan and passed with the selective plan;
native fault-injection, stage and Cortex-M3 tests also passed. It was built
as source`de263be0`/RP`c4d9a6ee` after recovered
[MODE0](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-0-vn6ectd4/outcome.json),
[MODE2](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-2-enyu_jfe/outcome.json)
and [strict active PHY](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-m2aqb295/outcome.json)
qualification. Its [first](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-opk1kpzs/outcome.json)
and [repeat](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-t2ni99w6/outcome.json)
AP trials, plus a [no-AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-0bcxemi8/outcome.json),
all recovered but **did not commit a PSM report**. RP error5
(`PROBE_SOURCE_FAILED`) recorded source mailbox `R2_FAULT` at `0x8040`;
its prefetched HOF7 was still phase3/commit0 despite the RP stop-token
record. The source fault reason and whether a later HOF/PSM report was
published are not captured; these trials cannot validate the polling
change on silicon or establish an event-14 result. APs shut down and
independent source5840/BOOTSEL recovery passed. The experimental
selective-polling edit was **reverted**; the previously qualified
source`ef4981a3`/RP`7a318800` artifacts were restored from their
archived run and passed strict current-code preflight. Do not flash the
experimental image as a qualified receiver or infer that it received a
frame. A future fault-discriminator must retain the pre-fault forensic
order, the independent cleanup and the full terminal acceptance contract
while making the final source fault/report observable.

**Fault-observation follow-up:** the RP MODE14 observer now samples the
128-byte source mailbox in RAM when STOP9 has been sent but the final HOF
commit is absent, *before* checking its fault state. The MODE14 native
footer's previously unused word31 carries the sampled mailbox error;
words32-35 still carry sampled state/exception data. This allows a
published `R2_FAULT` to be reported with its source error even when no
final HOF exists. A fault racing the two-block copy can still leave a
stale error; a missing or partial HOF remains **unproved** and is never
promoted to a final PSM result. The read does not extend any deadline or
perform post-fault peripheral access; independent power-off remains
mandatory. Native fault-injection covers STOP without acknowledgement,
uncommitted final HOF, a fault during prefetch, and the existing final
publication path. Focused MODE14/legacy handoff/PSM-fresh native and USB
regressions pass, and both changed RP units compile for Cortex-M33.
The broad non-fresh integration suite remains red on historical artifact
identity checks and an unreviewed outlined dispatch in a non-fresh source
rebuild; neither guard was bypassed. In the isolated **fresh** family,
source identities, builds, strict preflight, and new physical prerequisites
were instead verified before either cold run below.

**Fresh fault-visible cold follow-up, 2026-09-22:** current-source
[MODE0](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-0-6w6mur13/outcome.json),
[MODE2](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-2-xokjgodc/outcome.json)
and strictly replayed [active PHY](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-if8v2yc4/outcome.json)
preceded a strict-preflight source`e7438437`/RP`2d156d45`
[no-AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-hsofkagy/outcome.json).
It captured a **committed** HOF7/PSM failure: post-MAC mailbox state3,
PSM phase10/error6, `polls=2`, `snapshot_stable=0`, and no frame. Its first
full terminal-poll snapshot consumed the remaining source deadline. The
missing final report of the older alternative image has not been reproduced
or retrospectively explained by this control.

The fresh nonterminal-poll change then retained the twice-checked mailbox
and full-terminal snapshot contract. Fresh [MODE0](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-0-unlsvcvu/outcome.json),
[MODE2](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-2-id6usad1/outcome.json),
and strictly replayed [active PHY](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-lqhl2h9u/outcome.json)
qualified source`8614bfbf`/RP`5f1f2479`; strict preflight passed.
Its [no-AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-1w54q1qr/outcome.json)
and [AP-supervised trial](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-r3nwfxfl/outcome.json)
both independently recovered and committed HOF7/PSM failures. Each reached
MAC-enable readback and mailbox state3, then sampled the PSM's own
**fault6** (event-wait countdown exhausted) on poll6, error9; no event14
or complete frame was captured. The AP trial recorded five baseline
submissions, not an on-air packet during the short receive window. The
code change makes nonterminal reads cheaper and the PSM fault observable;
it does **not** establish synchronized stimulus, a cold frame, DMA
lifecycle, or independent RF safety. Those gates remain closed.

The existing [fresh AP burst](../../experimental/cyw43/r3/rxe-observe/fresh-stimulus/README.md#coordinator-api)
is intentionally gated on five **receiver-validated** baseline frames
before pause; this cold receiver has zero. Reusing its burst before that
gate would weaken the stimulus contract, while baseline AP submissions
alone do not establish an on-air frame in EVENT_WAIT. The cold USB runner
does not yet expose a guarded takeover-ready/release checkpoint. A future
bounded two-sided trial needs that synchronization and independent
air-time evidence, preserving the source deadline and recovery, before
assigning the absent event14 to a PHY/RXE defect.

**Cold-transfer checkpoint,2026-09-22:** corrected retained one-shot capture
passes three consecutive trials; the same PSM is now integrated and tested
on the cold path, which still has no proved frame. Older entries retain their recorded
build IDs, test counts and decisions; their use of “current” describes that
historical checkpoint, not the firmware to flash now. The cold image, old
prefix image and immutable recovery pins are unchanged.

**Later HOF7 physical checkpoint,2026-09-22:** fresh-family MODE0
[`oao2jmek`](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-0-oao2jmek/outcome.json)
and MODE2 [`8c6xgo56`](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-2-8c6xgo56/outcome.json)
passed with independent source5840 and BOOTSEL recovery. The recovered
active-PHY [`ppp2tqji`](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-ppp2tqji/outcome.json)
deliberately exited at its diagnostic terminal; its separate strict
verifier accepted all eight delay arguments, tables, powered state and
recovery. The qualified HOF7 source`b0671c00`/RP`458775fd` image passed
current-code preflight. Two supervised AP100TU attempts
[`bcyk3uh4`](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-bcyk3uh4/outcome.json)
and [`9nzr8jb_`](../../experimental/cyw43/r3/rxe-observe/runs/cold-session-v7-9nzr8jb_/outcome.json)
were **negative**: both RP records report `PROBE_NATIVE` (18) at the
handoff header address `0x42740`, with handoff phase 3. The RP expected
HOF6 while the source offered HOF7. The `undeaf` phase15, completed
mask`0x7ffe`, 6,396 I/O-attempt snapshot was prefetched before this
rejection and is **not** proof that calibration stopped there. No PSM report,
HOF7 telemetry, cold frame, or controller `PLL_PSM_TIME` was committed.
The RP header check and native fixture now use the shared HOF7 source
constant; the corrected image was subsequently qualified as noted above.
Both APs stopped,
both badges returned to BOOTSEL, and each research trial independently
executed frozen source5840. Do not reuse the older HOF6 frame-less timeout
as if it came from this image. The new qualification and trial above
supersede this particular prerequisite, but not the missing cold frame;
do not enlarge bounds or publish DMA to bypass it.
A subsequent bounded [no-AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-i8u8m7e6/outcome.json)
with the same old image also rejected HOF7 at the RP handoff header and
independently recovered; its prefetched phase15 snapshot had 6,397 I/O
attempts. This shows the RP mismatch does not depend on AP activity.

## 1. Objective and present decision

This historical objective summary is superseded for work ordering by the
[active milestone and deferred next steps](README.md#active-milestone-cold-source-acquisition-of-a-fresh-frame).

Build a source-owned CYW43439 receive path for the RP2350 badge in original
freestanding C: no target SDK, libc, allocator or vendor runtime/service
calls. Pinned ROM configuration/table data are still read; "source-owned
execution" does not mean independent calibration data or a clean-room design.

**Bounded experimental source-receive runs are now explicitly authorized;
transmit-inhibition/RF measurement is deferred to a separate track.**
No unintended emission is an unverified working assumption, not a proved
fact. Keep the existing inhibits, bounds and recovery; record the explicit
experimental opt-in. No source-only frame has yet been demonstrated.

| Track | Verified status | Do not confuse it with |
| --- | --- | --- |
| RP2350 host + vendor radio firmware | Own gSPI/ARP/IPv4/UDP; 600/600 echoes across three baseline sessions | Source-owned radio firmware |
| Source CYW Cortex-M3 | Own startup, RAM, fault handling, PMU/PHYPLL activation and complete implemented RX-focused calibration | A complete Wi-Fi MAC/PSM receiver |
| Calibrated-state handoff | Two physical passes: real quiescent consumer after private-stack return, bounded retention, stop/revoke/cleanup | Indefinite operation, analog preservation or RF silence |
| PSM/RXE and DMA | Three consecutive fresh retained-state MPDUs and clean controls; cold transfer verifies code/SHM/READY/MAC but reaches its controller deadline with last PSM state3 | Cold source-only reception, general reliability/continuation/rearm, DMA publication or RF qualification |

The SDK-based BLE/Pong/handover apps are a separate dependency-bearing track.
Host test libraries, Python and existing Ruby tools are not target dependencies.

## 2. Hardware and last known state

| Item | Established fact |
| --- | --- |
| Research badge | RP2350 serial **`9BBF88DD17D66E7D`**; only this badge used for the latest source tests |
| Reference/stimulus badge | **`08DA26F86E57F5EA`**; used as the controlled channel-1 vendor AP in the later receive experiments, then returned to BOOTSEL |
| Last recorded research state | Frozen source5840 executed successfully, then final **BOOTSEL** verified; no experiment left running |
| Radio | RM2/CYW43439, chip revision 5, D11 revision 39, package 4; chip word `0x1545a9af`, CYW CPUID `0x412fc230` |
| Radio pins | GPIO23 shared WLON/BTON; GPIO24 shared gSPI data/IRQ; CS25; clock29 |
| Not a radio control | GPIO41 powers the switched TFT/IMU/touch rail |
| Power/RF boundary | VDDBAT remains supplied when GPIO23 is low; no independently qualified PA-disable pin or RF-containment setup |
| Time reference | Source counter `0x18003090`; RP timer `0x400b000c` is raw ticks; USB SOF qualification is conditional, not absolute microsecond accuracy |

These are last recorded roles/state, **not a claim that devices are currently
attached**. Rediscover by serial, never port/order. Local USB is required
for the source experiment; internet or venue Wi-Fi is not. USB loss/frozen
SOF must fail boundedly, not justify longer limits.
[Schematic and electrical limits](hardware.md) |
[Exact identity, ROM and firmware hashes](firmware.md#identity-contract) |
[All badge peripherals](../spec.md).

### Attached-hardware baseline check - 2026-09-21, after precopy implementation

The owner authorized testing with both connected badges. USB enumeration and
serial-bound picotool queries identified both expected RP2350 A4 devices in
BOOTSEL. Only research serial `9BBF88DD17D66E7D` was flashed, using the
unchanged, hash-verified source5840 recovery assets and the existing
`host_support.restore_baseline` path, not the unintegrated PSM probe.

[Baseline report](../../experimental/cyw43/r3/psm-rx/source/precopy-baseline-y9tcr5bk/source5840/report.json):
source `3ddd0935`, RP `1bcf2b0e`, two reload cycles, six valid challenge
records including the expected negative request, independent 2,448-byte
staging readback, pinned ROM identity and D11 reset=1 all pass.
[Outcome](../../experimental/cyw43/r3/psm-rx/source/precopy-baseline-y9tcr5bk/outcome.json)
confirms independent source5840 execution and final BOOTSEL. Reference badge
`08DA26F86E57F5EA` was left unchanged. No PSM or receiver was activated;
no received-byte claim follows from this reset-held result.

Permission and USB availability are no longer blockers. At that baseline
checkpoint the probe still lacked calibrated-owner/load/stop integration;
the later integration and hardware work are described below. Independent RF
observation/containment remains unresolved for qualification; the owner's
later deferral permits the bounded experimental activation described above.
Do not substitute this reset-held result for receive or RF evidence. Exact hashes are indexed under
`follow_up.precopy_hardware_baseline_2026_09_21` in the central ledger.

## 3. Authoritative verified checkpoint

This section anchors the **historical quiescent source-owner proof**. It is
not the latest receive-experiment image. The current retained catalogue and
its separate evidence are at the start of section4.

- Source **`2755f5b0`**, RP **`6d2058f2`**; cold v22 / handoff v2 / wire v10.
- Identical UF2 SHA256:
  `fde47176cf3268c28f9e86432815432e9bf92de1cf45b53929a469fbb6dab4f8`.
- Original guard/board/rate objects remain in owned BSS. Normal consumer
  SP `0xfe50` is outside returned private stack `[0x42ec0,0x43ec0)`.
  Native/M3 tests poison that stack; do not copy board self-pointers or
  reinitialize an armed guard to manufacture ownership.

| Run | Nominal SOF frames | Hold checks | Terminal state |
| --- | ---: | ---: | --- |
| [prnwzxb3](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-prnwzxb3/report.json) | 267 | 8 | `quiescent_lease_stopped` |
| [_na2u3rk](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-_na2u3rk/report.json) | 268 | 8 | Same |

Both retain 21 live post-calibration registers at acceptance, eight checks
and stop. Register words here are hexadecimal: resources=`7efa3df7`,
D11 clock=`070b0240`, IOCTRL=`2055`,
stopped MAC=`84000404`, MIN=`0ec23df7`, SR0/SR1=0, DMA=`40/0/0`.
All six inhibits remain checked. Stop sequence 9/ACK 9 revokes the lease before
success-only request/IO release. Independent source5840 and BOOTSEL recovery
pass. Neither run enables PSM, MAC reception or DMA publication.

The [central ledger](../../experimental/cyw43/r3/references.json), key
`follow_up.calibrated_state_handoff_2026_09_21`, owns exact report hashes,
prerequisite identities, state vectors, object addresses and validation.
[Calibration guide](calibration.md#bounded-calibrated-state-handoff-verified---2026-09-21)
explains the result. Last validation: source native/M3 and RP failure tests,
82 host tests, 24 decoder/stack tests and strict replay of both raw runs.
These are checkpoint results, not tests performed by whoever next reads this.

Two earlier attempts remain **unqualified**: `xlqhs6ww` exceeded the SOF-step
limit because of a 1536-byte in-window prefetch; `y9yfx68u` captured the prefix
before asynchronous cleanup committed. The RP now meters only a 64-byte
command/ACK slab and awaits final commit before bulk capture. Shared
regressions cover both; limits and binding predicates were not relaxed.

### Interpretation traps

- Generic runner exit 1 / `measurement_verified=false` is expected at the
  deliberate pre-reset terminal, but **is never success by itself**.
  Require strict raw handoff/calibration bindings and marker `0xf0000001`/8.
- Capture-time `recovery_verified=false` precedes recovery. Check the later
  outcome's `source_baseline_restored` and `final_bootsel_verified`, plus
  the independent source5840 proof. BOOTSEL alone is insufficient.
- Fingerprint equality is within a run. Measured RC counts legitimately
  differ between runs. Use the live post-calibration baseline, not pre-RC values.
- Calibration, bounded sampled retention, whole-hardware/analog retention,
  continuous RF silence and RX are different claims. Only the first two pass.

## 4. Next work and unresolved gates

This section is a checkpoint ledger, not the active backlog. The
[consolidated guide](README.md#next-steps-after-first-acquisition) owns priorities.

Entries run newest first. Cold, retained full-frame and retained prefix
images are separate; their current entries name their own selections.
Subsequent “current image” and prerequisite lists are historical records.
Use the selected workflow's manifest and current-code checks,
not an older build number copied from this chronology.

### Cold full-frame transfer tested - historical HOF6 2026-09-22 checkpoint

The corrected PSM`73fd0ced` is now wired into the **cold source-owned
calibrated owner**, using the original persistent objects after private-stack
return. It retains actual full code readback,172-word SHM clear/readback,
generation-last publication, held-jump startup and READY-before-MAC gating.
The physically tested `FRESH_PSM=1`/`--psm-fresh-probe` profile used HOF6
and PSM reportv5: the160-byte snapshot occupies release-only handoff
bytes172..331;332..907 were zero. The **new, offline-only HOF7** retains
the same report and snapshot, adds 64 bytes of phase telemetry at332..395,
and keeps396..907 zero. Historical HOF6 captures still decode. USB extents
and the PSM report ABI are unchanged.
No DMA, rearm, new post-ready USB gate or vendor runtime is added.

**The transfer ran physically, but did not prove a cold frame.**
The [cold-transfer index](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/cold-fresh-transfer-2026-09-22.json)
binds new, independently recovered current-code prerequisites:

- MODE0`vmeezwzc`: source`07b9f788`, RP`29974097`.
- MODE2`njxjzhlg`: source`1c7a8f99`, RP`b0f04dd2`.
- Active PHY qualification`sklt6vxo`: source`7a0da333`, RP`4df1925d`;
  all eight delay arguments and908 table samples pass the dedicated strict
  verifier. Its deliberate diagnostic terminal still has generic
  `measurement_verified=false`; it is not relabeled complete cold startup.

These records produced the qualified experimental image:
source **`dda2ca27`**, RP **`1049d9ff`**, PSM **`73fd0ced`**.
UF2 SHA256:
`ed09b3f3ad4137f3852c31f55a0c77281dd8b24548ea10f1ef3d4d6e53a12602`.
State uses29,136/32,768 bytes and extended code92,808/98,304;
normal/private analyzed stack bounds remain864/2,128, without raising
the existing acceptance caps. No blocked offline UF2 was flashed.

Two identical-image trials used AP100TU (`48l2ljnx`) and the immutable
AP20TU contrast (`iitg_9b4`). Both verified3,144 actual instruction bytes,
172 cleared words, generation and READY, then MAC readback`84020403`.
Both reached **controller `PLL_PSM_TIME` error6** at the unchanged20M
source-tick budget. The last mailbox was source state3 (receive-event wait),
fault0, commit0; no frame snapshot was committed. There were only two ARM
mailbox polls. This is **not** a PSM event-timeout terminal and does not
prove uninterrupted observation between samples.

The AP session nonce was distinct from research control and supplied only
to AP/host parsing; no post-ready attribution is claimed. APs reported
five baseline submissions and correct beacon readbacks, not an on-air
transmission count. All five research runs recovered frozen source5840;
both AP experiments stopped and returned both badges to BOOTSEL.

The existing counter fields now have a bounded offline timing interpretation
in `controller/cold_fresh.py` (no firmware or report-ABI change). In the
100-TU run, first sample to pre-MAC-write sample is 12,601,875 raw ticks,
followed by 7,413,601 ticks to the last sample; the 20-TU run gives
12,601,880 and 7,413,566. Thus about 63% of each sampled controller budget
preceded the MAC-enable callback. The latter interval includes the MAC
write/readback and guarded polling; it is **not** measured on-air receiver
time. These three existing samples cannot separate code loading from
per-access guard cost or establish continuous PSM execution. The offline
checker rejects a timestamp outside the sampled counter interval and handles
wrap and a legitimate zero timestamp. Frozen run reports are not rewritten.
The changed host verifier input requires fresh matching qualification before
any further physical cold run; rebuilding the blocked offline image does not
grant flash authority or requalify the earlier physical images.

The HOF7 telemetry records the last **existing** source counter sample and
I/O-attempt count at each completed boundary: code written, code read back,
SHM generation committed, READY, MAC readback and first complete snapshot.
Its 16 little-endian words contain magic `0x314d5450`, version1, checkpoint
count, six ticks, six I/O counts and a zero reserved word. Count permits
partial failure records and disambiguates legitimate zero ticks; unused
slots must be zero. The decoder checks wrap-safe ordering, bounds against
the report and the completed-stage count. These are sampled controller
boundaries, **not** exact post-callback timestamps, additional target MMIO,
isolated guard costs, continuous PSM execution or RF receive duration.
The unchanged20M-tick budget and strict frame/fault checks still apply.

The newly rebuilt source `55113968` (BSS29,200/32,768 bytes, extended
code90,540/98,304, normal/private stack864/2,128) and RP `16ff9194`
are **blocked offline diagnostics only**. The offline verifier returns
`flash_authorized=false`; neither badge was flashed with HOF7.
Fresh current-code MODE0, MODE2 and active-PHY physical prerequisites,
independent recovery and bounded supervised experimental opt-in are needed
before a HOF7 hardware trial. Its checkpoints may locate the cold timeout;
they do not supply a missing cold frame or RF qualification.

Native transport41,679 cases, all12,769 I/O fault/cancellation positions,
real-owner/poisoned-stack native and Cortex-M3 tests, RP/1065-page USB,
eight host/integration/physical-replay tests and five decoder tests pass.
Full current-code preflight passes. Source-only frame production remains
**blocked**, rather than being inferred from those software results.

**Next:** investigate how load and active per-access guard observations
consume the bounded window, and the cold D11/PHY receiver state not covered
by the selected getter comparison. Keep deadlines and inhibition intact;
the two failed runs are not a reason to mask errors or extend limits.
[Cold profile commands](workflow.md#cold-full-frame-profile).

### Corrected bounded recapture - current retained 2026-09-22 checkpoint

**Three consecutive independently recovered trials now pass**, followed by
passing no-burst and clean old-challenge controls on the same frozen image.
This is reproducible fixed-frame one-shot reception under vendor-retained
initialization, not a statistical reliability claim or cold source-only RX.
The [nine-attempt follow-up index](../../experimental/cyw43/r3/rxe-observe/runs/reliable-one-shot-2026-09-22.json)
preserves all four preceding failures as well as the five final trials.

The earlier status discrepancy was an instruction-encoding defect:
55 comparisons had two SHM input operands, which the pinned assembler
explicitly rejects. The old model incorrectly treated them as independent
reads. The corrected encoder lowers each comparison through a GPR;
encoder, verifier and model now enforce the one-memory/one-SPR-input limits.
The [physical diagnostic](../../experimental/cyw43/r3/rxe-observe/runs/status-discrepancy-investigation-2026-09-22.json)
then reproduces `0182 -> 4182` as **PSM fault12/commit0**, not merely a
later C rejection. That run's matching fresh payload/FCS remains diagnostic.
The old positive's independent C/host checks remain valid; its defective
PSM image is archived, not recommended for reuse.

Two other producer assumptions were corrected:

- A measured pre-READY drain failure still had condition3, the previous
  FCS result, asserted after64 polls. That result is not a busy condition
  and need not become false before MAC enable. Only its false-required
  drain predicate was removed; conditions5/6/11/13/14, both existing EOI3
  operations, all control/core checks and candidate inhibits remain.
- A changing snapshot is recopied and compared again using the **same
  existing1023-count frame budget**. Every retry decrements it; no reset,
  new RXE command, flush, rearm or additional deadline is introduced.
  Persistent instability ends at fault8 with commit0. Acceptance still
  requires an exact stable96-byte snapshot, completion/FCS, full payload
  and CRC checks. No status bit was masked to manufacture success.

The AP is now`aecf68c1`, with100-TU beacons. After five receiver-validated
baseline frames, an AP-only ACK-queued command pauses periodic lab traffic
after the current send returns. A50-ms host settling period precedes
takeover. The pause persists after the challenge burst, but does not prove
an empty vendor queue or RF silence; firmware-managed traffic remains possible.
Fresh challenge creation still occurs only after the guarded source-ready
gate, and challenge bytes still go only to the AP.

Current RP/controller/PSM: **`f49aea13` /`24da697f` /`73fd0ced`**.
PSM is3,144 bytes, ABI7. UF2 SHA256:
`153c85de0b3b5785a8735ff247f44ac2519fd20933df25ed208e0e59dd7776cd`.
Full code readback remains actual, exact, byte-for-byte comparison.
The controller no longer stores a duplicate code image: an independent
completion state plus both exact byte counts gate metadata validation.
The live workspace ends at1316; fixed gate4028/release4092 and4096-byte
history are unchanged. The linked reservation is14,377/14,412 bytes
(35 free), with all138 declared input hashes verified.

All three positives have status4182 before/after, sequence80000001,
88-byte MPDU/FCS, zero source/controller/C errors and both candidate-guard
checkpoints. The old-challenge control now also has a valid stable full
frame and zero C errors, but rejects the newly expected challenge: unlike
the earlier control, it is not confounded by snapshot instability.
The no-burst control accepts no expected challenge and is not a silence test.
All nine attempts independently restored source5840 and both badges to
BOOTSEL. The source58-test suite, native sanitizers, seven integrated build
checks and host/physical replays pass.

**Next:** the separate cold calibrated-owner integration must preserve
source-only initialization and obtain its own physical evidence. Its
first release-only profile need not add a post-ready USB challenge gate;
any session-attributed result must be labeled accordingly. No cold
receive success or RF qualification is inferred from these retained trials.

<a id="fresh-source-psm-frame-proved---2026-09-22-checkpoint"></a>
### Fresh source-PSM frame proved - earlier 2026-09-22 checkpoint

**The original source PSM received one newly transmitted, complete frame.**
This closes fresh-frame attribution in the **vendor-retained** experiment,
not cold source-only reception or qualified Safe RX.

The [eleven-attempt index](../../experimental/cyw43/r3/rxe-observe/runs/fresh-frame-proof-2026-09-22.json)
pins every report, manifest, outcome and recovery. The accepted
[raw capture](../../experimental/cyw43/r3/rxe-observe/runs/fresh-transmit-f3a033f877d5340f/report.json)
and [independent proof](../../experimental/cyw43/r3/rxe-observe/runs/fresh-transmit-f3a033f877d5340f/report.proof.json)
establish:

1. Vendor initialization and five ordinary received frames precede takeover.
   The controller masks interrupts, stops MAC/PSM and candidate DMA, loads
   and reads back the original PSM, clears/read-verifies172 owned SHM words,
   and checks source READY plus the six candidate inhibits with MAC clear.
2. The RP verifies the committed ready gate. Only then does the host generate
   challenge`7a111d0ce0a6b2ee3b4f6a805024b92c`. Neither receiver control
   packet contains it: release carries the old experiment nonce and token.
   The new challenge goes only to the AP, after receiver release.
3. The PSM commits96 snapshot bytes: two duplicate PLCP bytes, six PLCP bytes
   and an88-byte MPDU including FCS. Addresses, LLC/EtherType, every lab
   payload byte, challenge, sequence`80000001`, payload CRC and MPDU FCS
   all match. PLCP is`6e0440007fee`; FCS bytes are`a95612cb`.
4. ABI7 state14/commit1, marker15, exact length94, stable status`4182`
   and both guard checkpoints pass the on-chip C checker and host verifier.
   All C evidence error bits are zero. Retained old FIFO bytes cannot explain
   a frame containing this post-ready unpredictable challenge.

Frozen research identities: RP`879f0729`, controller`7264f9c3`,
PSM`25cc3489` (2,712 bytes, ABI7). Research UF2 SHA256:
`e8b9f6c5761dd9fb53250b4ed232730ec4577b36b79867682c6d80d21ed3569f`.
The isolated challenge AP is`25d4a903`. Use the
[fresh-frame workflow](workflow.md#guarded-fresh-frame-experiment), not the
prefix collector or its three-case UF2. The controller uses13,969/14,412
reserved bytes; the pre-gate workspace gap is only four bytes. Source,
images and manifests are frozen, not repinned to later result notes.

Hardware established three important distinctions: the five selected
post-MAC EOIs are issued once; on the sampled plain PHY path, live count
`8000` derives count0 and skips the vendor bit15 rejection (zero is not a
forced byte count); and completed status`4182` need not retain the prefix's
bit12. ABI7 still requires initial-prefix bit12, then completion6/FCS3,
stable final bit15-clear status and exact full-frame bytes/FCS. General
count semantics and continuation atomicity are not qualified.

**Failures remain failures.** The no-burst and old-challenge controls
reached released guarded windows without accepting the new expected nonce.
The old-challenge capture also fails C status stability, so it is not a
clean nonce-only control. A subsequent same-image run contains matching new
challenge/MAC/payload/FCS but status changes`0182 -> 4182`; the C checker
correctly rejects it with`FRESH_E_SNAPSHOT=0x100`. Why the PSM committed
despite that change is unresolved. Beacon-first captures fail fixed-frame
geometry. There is one fully accepted positive, not repeatable general RX.

All eleven attempts independently recovered frozen source5840 and recorded
both badges in BOOTSEL. These recorded outcomes are not a claim that both
devices remain attached later. Final offline validation passes114 host tests
(including three hardware-replay tests),36 PSM tests, native sanitizer
suites and the six fresh-build checks.

Ledger key`follow_up.fresh_frame_attribution_2026_09_22` owns the hashes.
The runner checks the historical isolation snapshot without repinning it;
only the four exact owner-requested documentation revisions listed at the
end of this handoff are excepted. It records
`historical_full_audit_passed=false`, not a full historical audit pass.

**Next:** resolve the PSM/status-stability discrepancy and make one-shot
capture reliable before adding discard/rearm or DMA. Then transfer the
working RXE preparation into the cold calibrated owner and isolate the
remaining cold no-event state. Independent RF/TX-inhibition qualification
remains on its separate track; no source-only or RF-safety claim follows.

### Candidate-inhibit comparison - earlier 2026-09-22 checkpoint

**Three checked prefix captures now coexist with the same six candidate
inhibit readbacks used by the cold source firmware.** This is a useful
configuration discriminator, not Safe RX qualification: fresh on-air frame
attribution and exclusion of retained upstream FIFO contents are still
missing, and transmit-inhibition measurement remains deferred.

First, a bounded selector-only/read-only-data snapshot captured the same21
PHY/radio getters as the pinned cold no-event run`8az0im44`. It runs after
checked MAC/PSM pause and candidate DMA stopping, before source PSM loading,
with interrupts masked and no return to vendor code. Two reference trials
both produced checked prefixes. Their selected configuration values matched
the historical cold values except:

| Register | Cold | Working vendor-retained | Difference |
| --- | --- | --- | --- |
| PHY44c | 4200 | 0200 | Candidate TX-chain override-enable4000 |
| PHY938 | 0800 | 0000 | Candidate internal-PA override-enable0800 |
| Radioa4 | 0004 | 0000 | Candidate radio-PA override-enable0004 |
| Radio92/93 | Calibration counts | Slightly different counts | Not automatically a cause or a proved benign difference |

The corresponding value fields already matched: PHY44d=0, PHY939=0,
radio79=7f80. The other selected getters, including the RFPLL override
bit/value, matched. This does **not** establish equality of all MAC/PHY state.
The cold comparison is historical and hash/CRC-bound, not new cold
qualification.

The new explicit catalogue case`source-prefix-inhibited` performs the
same six tighten-only masked updates as the cold candidate policy, preserving
all unrelated bits. It never releases those settings within a trial.
Both baseline and guarded cases sample the six fields before MAC enable
and at terminal capture; only the guarded case performs the data writes
and enforces the selected-bit policy. Prefix acceptance and all deadlines
remain unchanged; source PSM`e6947a2c` is unchanged.

| Batch | Actual trials | Result |
| --- | --- | --- |
| `a664022fe8be10a3` | Two read-only-state baseline trials | Both prefixes passed |
| `65c87690cc086160` | Baseline, then inhibited | Both prefixes passed; guarded values exactly match cold at both checkpoints |
| `dc76cc8b4c4e0689` | First inhibited trial only | Stale-event drain fault3 before READY/MAC enable; queue aborted, second trial not run |
| `683a6188fa83540e` | Two inhibited trials | Both checked prefixes passed with all six cold readbacks at both checkpoints |

The failed trial is retained, not retried within its invalidated session.
All four batches independently recovered frozen source5840 and both badges
to BOOTSEL. Readback equality and a checked prefix are not continuous
inhibition, RF silence, fresh reception under inhibition, or a complete frame.
In particular, the remaining upstream-FIFO ambiguity means this result
**does not rule out every inhibitor-related explanation for cold no-event**.

Current image: RP`e17ad806`, controller`1223f20d`; UF2 SHA256:
`5c602fcc07e044c0c442c0f4a5968763f5bf6b1a615839d1606ab2ffeabac870`.
RRT v3 carries the receiver snapshot and guard checkpoints. Catalogue v2
has three fixed cases on two64-byte pages. Historical v1/v2 reports and
v1 catalogues require their archived manifests and do not inherit new
observations or guard claims.

The [comparison evidence](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json)
and ledger key`follow_up.safe_rx_inhibit_comparison_2026_09_22` pin all raw
reports.102 host tests, seven build tests and native sanitizer suites pass,
including all new-access failures/cancellations/timeouts and bit-preservation
checks. The controller fits13,789/14,412 bytes; no reservation, deadline or
source inhibit policy was weakened. No cold source image was modified,
rebuilt or freshly qualified in this comparison.

**Next work:** establish fresh-frame attribution and investigate the cold
D11/PHY state not represented by these21 getters, including possible
guard-polling/timing interactions. Initial flush/event drain remains a
separate intermittent problem. Keep independent RF qualification explicit.

### Faster resident catalogue workflow - current 2026-09-22 checkpoint

**Historical two-case catalogue checkpoint.** The later candidate-inhibit
entry above adds the third case, catalogue v2 and newer build identities.

**Batching and the single-UF2 fixed catalogue are implemented and physically
tested.** This changes test orchestration, not the receive algorithm.
The same image contains the vendor-observation path (`vendor-rxe`, mode0)
and original source-PSM prefix path (`source-prefix`, mode4). USB IN`a7`
enumerates the two exact descriptors and build identities before activation.
The canonical registry generates the firmware table and manifest.

The research image is flashed once per batch; each subsequent trial verifies
its flash bytes and reboots that installed image through BOOTSEL. Every
trial still fully reinitializes the radio, validates at least five vendor
frames and uses a fresh experiment challenge. The retired flag is not
cleared in a live session. A real source-prefix trial followed by a fresh
vendor-rxe trial passed, demonstrating recovery of the vendor environment
through reboot/reload rather than resuming its overwritten state.

Current catalogue image: RP`3964f145`, controller`9671eca4`.
UF2 SHA256:
`8fe099e3dac07cc13e20462087ea4fbfaeb4a2a46168daacca671f47130ef6f0`.
Source PSM`e6947a2c`, format4 and all radio acceptance predicates are
unchanged. This does not consolidate or bypass cold-source qualification.

| Measured workflow | Wall seconds including frozen recovery | Outcome |
| --- | ---: | --- |
| [Standalone1](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-51c075b37ed697ee/outcome.json) | 57.742 | Checked prefix passed |
| [Standalone2](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-00f6b635ca29a333/outcome.json) | 57.839 | Existing initial-flush fault2; recovered |
| [Two source-prefix trials](../../experimental/cyw43/r3/rxe-observe/runs/batch-ch1-14a38a6030399a81/outcome.json) | 67.160 total | Both passed |
| [Source-prefix then vendor-rxe](../../experimental/cyw43/r3/rxe-observe/runs/batch-ch1-c751cd9ec0ea5b68/outcome.json) | 67.094 total | Both passed |

The two separate attempts total115.581s, versus67.160s for the two-trial
source batch: about42% less wall time in this small sample. The standalone
failure remains part of the record; this is not a statistical reliability
benchmark. The repeated batch's independent baseline recovery alone takes
37.4s, compared with about4.7-4.8s per radio initialization,1.1s per positive
control and0.24s per source capture/verification.

Batch limits are four total trials and120 active host seconds, further
capped by the AP's calibrated180-second lease minus10 seconds. The first
failure aborts remaining cases. Stop the AP, then execute the unchanged
independent source5840 recovery once at the batch boundary or failure.
That recovery proof belongs to the **batch**, not each trial.
It overwrites the candidate, so another batch needs one new candidate flash.
No dual-slot boot design or rebuilt/repinned recovery was introduced.
Single-trial operation retains its original per-trial recovery policy.

Use the [workflow commands](workflow.md#bounded-resident-research-batches)
and [measured evidence](../../experimental/cyw43/r3/rxe-observe/runs/resident-batch-validation-2026-09-22.json).
Both complete hardware batches passed, all measured runs recovered the
independent baseline, and both badges are back in BOOTSEL.85 host tests,
six build tests and native sanitizer/USB/retirement suites pass. A final
explicit batch-report RP/controller/serial cross-check was added after
measurement and replayed against every captured report; it changes neither
target code nor scheduling/timing behavior.

### Checked post-request prefix snapshot - current 2026-09-22 checkpoint

**First format4 checkpoint.** Its receive-check contract remains relevant;
the image IDs below precede the later receiver-state/guarded catalogue.

**The bounded vendor-retained prefix experiment now passes on hardware.**
Two channel1 runs reached PSM state12 with a committed format4 snapshot,
zero PSM/C evidence errors, and independent host validation of all six
CCK PLCP bytes. This is the first checked-prefix success, not source-only
reception or a full-frame result.

The command is still`SPR004=0040`. The new contract expects the observed
post-request **RXCTL readback exactly0800**, rather than assuming the command
value remains readable. It retains the preceding PLCP event, exact
instruction readback, owned-SHM clearing and generation binding, status/core/
length checks, repeated samples, CCK/service/length/CRC checks, and bounded
stop. Format4 explicitly versions this rule; historical format2 keeps
its original0040 rule. Fault-only format3 remains uncommitted.
The verifier proves every commit passes the exact0800 comparison and
cannot bypass its constant materialization. No arbitrary RXCTL values or
clear-low-bits approximation are accepted.

| Run | Channel | Result |
| --- | --- | --- |
| [f9aa54b9ec17846e](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-f9aa54b9ec17846e/retained.proof.json) | 1 | Checked format4 prefix; C and host checks pass; stopped-state readbacks pass |
| [4c7a515237cc24ef](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-4c7a515237cc24ef/retained.proof.json) | 1 | Same checked result with a new generation/nonce |
| [d5008831e238dc6a](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch6-d5008831e238dc6a/retained.proof.json) | 6 | Raw snapshot`ca7502000000` committed by PSM, **rejected by CCK validation**, evidence error2048 |
| [1b51a5a18f966e86](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch6-1b51a5a18f966e86/retained.proof.json) | 6 | Precopy timeout15; status0080, length0, zero copy; rejected |

Both positive runs have length6 before/after, status`3a00`, core`f020`,
RXCTL`0800`, prefix`0a043806455e`, tag`da15`, commit1, and all source
identity/generation checks passing. This is a1-Mbps CCK candidate with
matching CRC16`5e45`. MAC/PSM final readback is`c0020400`; all seven DMA
controls read zero with candidate stopped-state/error checks. These
readbacks do not prove analog silence or FIFO quiescence.

The channel6 negatives are important: **RXCTL0800 and a PSM commit alone
do not produce a passing result**. The first sample is outside the
supported CCK contract; its encoding/cause is not identified, and it must
not be labeled a corrupt on-air packet or automatically accepted as OFDM.
The second never met status/length requirements. Neither negative was
converted to success or omitted.

The independent host checker now repeats the complete prefix/CCK checks,
not merely report flags and CRC. Tests based on a real passing report
reject a substituted old format/readback, absent status bit12, corrupted
CCK CRC, reset bit in RXCTL and absent commit even after recomputing the
outer report CRC. Historical fault19 reports still mean rejection against
0040 and still fail. Bit11's general hardware meaning, transaction
completion, raw-copy atomicity, fresh-on-air attribution, retained upstream
FIFO exclusion, full MPDU/FCS, source-only RX and RF safety remain unproved.

Image used for this first format4 checkpoint: RP`723e4803`, controller`4fa3e039`, PSM`e6947a2c`/2,416
bytes. UF2 SHA256:
`92fcaed5b47c4d1d814166f8702822f961b1342346e556df75cb16916c17d687`.
The1,023-poll precopy bound, other wait bounds, controller/RP deadlines,
held-jump startup and DMA policy are unchanged. Target code remains
freestanding C with no new dependency. The cold image was not rebuilt,
flashed or requalified; new cold activation still needs fresh
identity-bound prerequisites.

All four runs first validated at least five vendor lab frames, then
automatically restored/executed frozen source5840, stopped the AP and
verified both badges in BOOTSEL. The [four-run evidence index](../../experimental/cyw43/r3/rxe-observe/runs/checked-prefix-format4-2026-09-22.json)
and ledger key`follow_up.checked_prefix_format4_2026_09_22` own exact hashes.
Validation includes44 source/model tests, all65,536 RXCTL halfwords in the
C checker,59 observer host tests, five build tests, native fault suites,
30,574 cold-transport cases,12 cold decoder tests, seven build-gate tests
and native/Cortex-M3 composition tests.

**Next meaningful work:** isolate the cold-versus-retained receive-state
difference using the now-checked prefix path, with fresh cold qualification;
retain the remaining rate, flush/takeover and fresh-frame boundaries. This
checkpoint does not authorize an unsupported full-frame continuation.

### Startup and precopy timeout follow-up - current 2026-09-22 checkpoint

**Historical fault-attribution checkpoint.** The later format4 contract
above changes the checked readback under a new image/format identity;
these original rejected reports remain failures.

**The immediate retained-state rejection is now identified, not guessed:
the source probe requires RXCTL readback`0040`, but captures`0800` after
copying the complete six-byte PLCP candidate.** A new fault19 is reachable
only from that exact guard. Two hardware runs report19 with bound source
identity/generation, length6, and CRC-valid`0a043806455e`. The original cold
no-event cause is still a separate unresolved issue.

Ten additional serial-bound trials are preserved in the
[timeout investigation index](../../experimental/cyw43/r3/rxe-observe/runs/retained-timeout-investigation-2026-09-22.json).
Each first validated at least five normal vendor lab frames. All ten
automatically executed frozen source5840 recovery, stopped the AP, and
verified both badges in BOOTSEL.

| Contrast | Trials | Observation |
| --- | --- | --- |
| Fault-only status instrumentation; existing startup | `7f7e48a823f79c69`, `f2f8cce7e1b7abab`, `83fb8319a2712392` | Transport report intact, but PSM ABI/build/generation/blockers zero. **Rejected as unbound**, not qualified source faults |
| Fresh postload jump0 pulse while PSM stopped: owned bits`0 -> 4 -> 2` | `c1c019602cb969b7` | Still unbound; merely pulsing reset while stopped did not establish entry |
| Hold jump0 across PSM run enable, then release: `0 -> 4 -> 6 -> 2` | `ff554487563bc03b`, `c2d766bfb351ecd4` | Both source identities and READY valid; MAC enabled; precopy timeout15 at length2 |
| Same held-jump startup; **only precopy**64 ->1,023 polls | `b1397ffd44a9bc64`, `10b2ddd2af0575d9` | First initial-flush fault2; second reaches length6 and full candidate, then shared precondition fault7 |
| Distinguish exact RXCTL guard as fault19; no further predicate or timing change | [bd012ba7275ef4c7](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-bd012ba7275ef4c7/retained.proof.json), [5e1f3a6875b1f485](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-5e1f3a6875b1f485/retained.proof.json) | Both report **fault19**, length6, RXCTL`0800`, complete CRC-valid PSM snapshot |

All six held-jump trials have bound PSM mailbox identity, including the
initial-flush failure. This supports retaining the checked sequence, not
claiming universally qualified reset/clock semantics. The MAC-enable bit
stays clear until valid READY. Source/RP deadlines were not increased.

The missing status evidence is now available: the64-poll failures sampled
SPR00d`3a80` (bit12 **set**, bit15 clear), length2, and only`0a04` plus zeros
in the PSM snapshot. The later ARM copy contained all six bytes. Thus those
failures were not merely “status never became ready”: the local window
ended before the remaining bytes were captured. The1,023-poll contrast
keeps all success and stability checks; the final PSM snapshots have
length6 and status`3a80` or`3a00`. This is a bounded count contrast, not a
calibrated microsecond timeout.

The flush-failure snapshot records RXCTL`0804`, SPR00d`7a00`, length205,
before READY or MAC enable. Its64-poll flush contract remains unresolved.
The pinned vendor graph also has an alternative completion-event06 route
when SPR00d bit12 is clear; only its syntax is verified, not a source
implementation or physical execution.

Format3 is **fault-only and uncommitted**. The original format2 success
checks remain intact, including the now-localized RXCTL guard. Source
generation/build binding, CRC-valid raw bytes, a committed prefix, a fresh
on-air frame, complete MPDU/FCS, and source-only reception are different
claims. No prefix commit, fresh-frame binding, DMA publication or RF
qualification is claimed. Retained upstream FIFO data is still not excluded.

Retained image used for this attribution: RP`0ab35631`, controller`8f0c6ff8`,
PSM`41aaba30`/2,392 bytes. UF2 SHA256:
`12296c8040c2fbb443ab2f7fd9e9fe82dc99993a247b800339d3c167cda3df20`.
Earlier images are archived under the retained build's`history-*`
directories; historical report replay uses the corresponding archived
manifest and does not inherit new fault codes or wait budgets.
The **cold UF2 was not rebuilt or flashed**. Shared-source changes were
wired into cold transport/build/decoder tests, but a new cold image needs
fresh identity-bound MODE0/MODE2/active qualification before activation.

Final validation:43 source/model tests,52 observer host tests, five build
tests, native sanitizer suites including20,492 retained callback failures
and10,246 cancellations;30,334 cold-transport cases,11 decoder tests and
seven build-gate tests pass. The shared source composition also passes its
native and Cortex-M3/QEMU private-stack tests without rebuilding a cold UF2.
The14,412-byte retained reservation still fits
(13,053 bytes used); no target dependencies were added.

**Next work:** define and validate the precopy completion/readback contract
using this observed command-versus-status behavior, without simply accepting
arbitrary RXCTL values. Then qualify the remaining flush/takeover timing and
compare cold versus retained receive configuration. Do not reinterpret
fault19 as success or replay bulk vendor tables.

### Vendor-retained comparison - latest 2026-09-22 checkpoint

**Initial retained comparison, before format3/4 and guarded profiles.**

**There is now a useful discriminator: the unchanged source PSM can pass
the PLCP event branch and issue precopy against vendor-initialized state.**
It still does not complete its stable-prefix contract. This is not source-only
reception or proof of a fresh on-air frame, and the exact cold no-event cause
remains unresolved.

The opt-in [retained experiment](../../experimental/cyw43/r3/rxe-observe/README.md)
was implemented and physically run three times. Each run first associated
normally and validated five nonce/sequence/payload/CRC-bound vendor lab
frames against the verified20-TU channel1 AP. All used RP`41a8f765`,
controller`21ea1dc9`, and the **identical** source PSM`9aca9e37`/2,264 bytes
used in the cold comparison. Code readback and zero/readback of all120
owned SHM words passed before source execution.

| Run | Source result | Raw copy / diagnostic |
| --- | --- | --- |
| [cffa170aae7e9d0c](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-cffa170aae7e9d0c/retained.proof.json) | READY and MAC enable; precopy timeout15 | `0a04 0a04 3806 455e`, then zeros |
| [367faa41740b5d77](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-367faa41740b5d77/retained.proof.json) | Initial flush timeout2, before READY/MAC enable | RXCTL`0804`, length205; no copy |
| [b318af04ca00c328](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-b318af04ca00c328/retained.proof.json) | READY and MAC enable; precopy timeout15 | Same raw bytes as first run |

Fault15 is reachable only after PLCP condition`0x0e`, entry guards and the
`0x40` precopy request. SHM621..623 in the two progressing runs contain
`0a043806455e`: a1-Mbps CCK PLCP candidate with service4, length1592us,
and matching computed/stored CRC16`5e45`. This is post-clear scratch data,
not merely uncleared vendor SHM. However, upstream retained FIFO data is
not excluded, raw-copy capture is not proven atomic, and no AP nonce/FCS
binds these bytes to a complete received frame.

Both progressing runs sampled length2 in the PSM fault mailbox; the later
ARM sample read length6. The precopy wait has only64 PSM polling iterations
and requires **both** SPR00d bit12 and length>=6. SPR00d was not captured at
the fault, so the blocking predicate and elapsed hardware timing cannot be
reconstructed. Do not simply raise a bound or treat RXCTL readback`0800`
as the requested`0040`. All diagnostic-prefix words remain zero.

The retained MAC transition preserves vendor non-control bits:
`c0020403 -> c0020400 -> c0020402 -> c0020403`, rather than substituting
cold-source`84020403`. DMA-stop readbacks are explicitly candidate-only.
Pre-trigger vendor history was captured, but not a qualified same-IRQ/
same-DMA-stop vendor-PSM control. The intermittent flush failure also makes
the takeover transition a remaining variable.

**Next work:** compare the cold and retained receive configuration without
bulk table replay; capture the missing RX status at precopy failure and
measure the bounded flush/precopy timing/readback contract. The evidence
weakens a universal broken-PSM/event-branch explanation, but does not yet
identify a particular missing PHY/RF/MAC setting.

The controller never resumes vendor code after replacement. RP captures
the CRC/commit-last report independently, cancels the waiting control call
without calling that a successful CDC reply, powers off and retires the
session. The first host cleanup mistakenly skipped RP reboot as well as
vendor shutdown; its original recovery failure is retained. A separate
RP-only reboot and [independent frozen source5840 retry](../../experimental/cyw43/r3/rxe-observe/runs/retained-ch1-cffa170aae7e9d0c/independent-recovery-retry-e4jv2azn/outcome.json)
passed. The host-only fix then automatically recovered trials2 and3.
Both badges are back in BOOTSEL; no experiment remains active.

The [comparison evidence](../../experimental/cyw43/r3/rxe-observe/runs/retained-comparison-2026-09-22.json)
and central ledger key`follow_up.vendor_retained_psm_comparison_2026_09_22`
pin all reports and recovery outcomes. Validation includes38 host tests,
five build tests, native ASan/UBSan controller/dispatch/RP suites, exhaustive
19,700 callback failures and9,850 cancellations, and271 RP capture bus
faults. Target code remains freestanding C with no new dependency. The
linked retained call chain is conservatively868 bytes including dispatcher,
excluding pre-existing vendor caller/exception frames; physical stack
headroom is not independently qualified. RF measurement remains deferred,
not proved.

### No-event investigation - latest 2026-09-21 checkpoint

**The exact root cause is still unresolved.** The failure is before the
precopy request and CCK/header validation, not a rejected captured packet.
The source PSM used in these cold trials reaches initialization-ready state13, the ARM
validates its build/generation, then enables MAC. The PSM advances through
the MAC-enable condition into event wait, but never observes PLCP condition
`0x0e` in the bounded window. It reports fault4, not MAC-wait fault18.

Controlled hardware comparisons, all with independent source5840 recovery:

| Change | Run | Result |
| --- | --- | --- |
| Read-only RF PLL override baseline | `ibuww15g` | PHY44c/44d=`4200/0000`; no event |
| Set only PHY44d mask`0200`, retaining TX inhibits | `8lgj978h` | Verified`4200/0200`; same no-event result |
| Initialize PHY_HDR_Parameter SPR049=2 | `en_d8zs9` | Same no-event result |
| Also initialize SPR024=`0c02`, preserving exact init-table high bits | `ec86mzhm` | Same no-event result |
| Start PSM with MAC clear, wait for READY, then enable MAC | `cqzevix4` | READY/MAC enable succeed; same event timeout |
| Same staged image, verified20-TU AP beacon-period configuration | `8az0im44` | Same event timeout, RXCTL`0800`, core`f020`, RX length0 |

The fast AP uses normal vendor control commands and verifies beacon-period
readback before and after BSS activation. This reduces the configured
beacon-spacing/window concern; it does not measure continuous RF output or
prove that every beacon was on air. None of these results establishes a
received header/frame, PLL lock or RF safety.

Image used in these cold trials: source`25ef3078` /RP`e43c5430`, PSM`9aca9e37` (2,264 bytes).
Report v4 separates PSM start from MAC enable; HOF v5 records the optional
RF PLL before/after contrast without extending the wire layout. RF PLL power
override was default0. The exact recorded prerequisites were
MODE0`ioqlmue9`, MODE2`tyw0ppyq`, active qualification`h3fjez75`.
The AP20-TU variant is `rx-lab/vendor/build/beacon20/`, build`24a0f6ba`.
The central ledger key `follow_up.no_event_investigation_2026_09_21`
contains complete paths, report hashes, artifact identity and recovery scope.

The proposed vendor-retained discriminator was subsequently implemented
and tested in the2026-09-22 checkpoint above. Do not turn these earlier
failed contrasts into a proven cause or replay an entire vendor init table
as an undocumented fix.

### Experimental MAC/PSM execution observed - 2026-09-21

With the owner's RF-measurement deferral, the receive-enabled program has
now been tested with the reference badge providing normal channel-1 AP
traffic. Both attempts below independently executed frozen source5840
recovery and returned the research badge to BOOTSEL; the AP was explicitly
stopped and returned to BOOTSEL too.

- `source-pll-14-bk2hdsob`: expired the original40M-tick owner lease during
  microcode preparation (638 completed I/O operations, zero instruction
  readback). No MAC start occurred. The last lease sample was39,998,148 ticks.
  Exact sealed-word comparison replaced repeated binding CRCs; full inhibit
  sampling is batched only while MAC/PSM are stopped, with per-access
  ownership/time/cancellation checks and full active/control checks retained.
  No deadline was increased.
- `source-pll-14-30mt4ory`, source`21ab00de` /RP`f0126037`: verified all
  2,136 PSM bytes, cleared/read-checked120 SHM words and committed generation.
  **MACCONTROL `84020403` was read back and the original PSM wrote its
  expected build/generation.** It reached event wait, then reported
  `EVENT_TIMEOUT`4 after65,535 iterations. No prefix or frame was captured.
  Pre-start work took7,382,143 sampled counter ticks; the post-start sample
  span was497,795 ticks, not a calibrated PSM clock or RF duration.

The second result closes the earlier "PSM has never executed" boundary for
this bounded attempt, not complete cold RX or RF qualification. The next
iteration gives the receive probe the existing lease instead of repeating
the quiescent256-SOF retention experiment, and extends the finite PSM event
window. It must not fabricate eight hold checks or SOF-retention evidence.
Raw runs, hashes and separate stimulus/recovery records are indexed under
`follow_up.experimental_receive_hardware_2026_09_21`.

The extended-window image has now also run twice:

- `source-pll-14-94qxpjd6`: source`ca3b9887` /RP`f4d01a35`,
  PSM`d5341a0f` (2,160 bytes). Handoff v4 performs two real owner checks
  and direct release, with no duplicated quiescent hold or SOF-retention
  claim. MAC enable and PSM identity again pass; all16x65,535 event
  iterations expire. Recorded RXE control=`0800`, core control=`f020`,
  RX length=0; no prefix is captured.
- `source-pll-14-4gov__2j`: identical source image, preceded by a current
  vendor station positive control that validated **five lab frames** on
  channel1. The source phase still reports the same no-event result.
  That positive control confirms the vendor traffic rig, not source RX.

The last run's first frozen-baseline collector returned **Invalid standalone
report**. Its original failed outcome is retained. A separate
`independent-recovery-retry-dcqhok7n` then passed the pinned source5840
execution proof and final BOOTSEL. The AP was explicitly stopped/rebooted.
Both serials were finally rechecked in BOOTSEL; no experiment remains running.

**Current remaining issue:** no source PLCP receive event or header has been
observed, despite source PSM execution and MAC-enable readback. PHY-to-RXE
receive activation, inhibit compatibility with RX, event semantics and
stimulus-window coverage still need investigation; do not identify one as
the proven cause. The source field `PHY44a=80` alone is not evidence of
held reset: the already-catalogued related P08 header names bit7
`rxfrontendresetStretchEn`, not `rxfrontendSoftReset`.

Final experimental UF2 SHA256:
`6429b23e426ef2112cef620a8a031196a6a475db235b05bb81a074b1f5464294`.
Its recorded prerequisites were MODE0`ipv2uubn`, MODE2`c0u7brbj` and recovered,
strictly replayed active qualification`xtz2p7zm`. Continue through the existing
runner with `--psm-precopy-probe --experimental-rf-unverified`; the assumption
remains unverified and no source frame, RF safety or continuous retention is
claimed. Keep the frozen recovery assets unchanged.

### Receive-enabled hardware request - external qualification blocker

**Superseded for bounded experimentation:** the owner subsequently
explicitly deferred the transmit-inhibition test to a separate track and
authorized continuing under an unverified no-unintended-emission working
assumption. No instrument is required to proceed with that experimental
scope. The following records the earlier blocker, not a current prohibition.

On 2026-09-21 the owner requested a receive-enabled experiment and hardware
testing after cold-source MAC enable and physical PSM/RF qualification.
Read-only USB discovery found both expected serials as `RP2350 Boot`:
`9BBF88DD17D66E7D` and `08DA26F86E57F5EA`. No identifiable RF instrument
appeared in that enumeration. This does not rule out network-connected or
standalone instruments; no usable measurement/containment setup could be
established, and the owner was unavailable to identify one.

The confirmed available setup therefore remains the two badges, not an
independent RF observer. No image was flashed, no MAC/PSM was started and
no receive or RF qualification result was generated for this request.
The source and blocked artifact remain unchanged.

Before the requested physical qualification, identify the actual instrument
and its control/capture interface, characterized containment/coupling,
measurement sensitivity/uncertainty, stimulus and acceptance limits from
the [existing RF campaign requirements](../../experimental/cyw43/r3/rx-preparation/README.md#required-rf-setup-and-execution-campaign).
Do not replace this external prerequisite with another model, a caller
Boolean, a fabricated attestation or an assertion that two badges prove
TX inhibition. Cold-source MAC enable/PSM timing also remains unqualified;
the known enable-bit arithmetic alone is not a completed cold receive path.

### Historical offline checkpoints (superseded by the hardware results above)

**Offline PSM follow-up, 2026-09-21:** the existing source generator/model/
verifier now includes an independent initial-precopy diagnostic. It issues
SPR004=`0x40` after bounded cold/event checks, samples six prefix bytes with
length/status/stability checks, and stops without stage, publication or
rearm. The freestanding C checker binds code/build/generation and validates
the diagnostic plus the observed 11-Mbps CCK PLCP format/CRC16. This is
software preparation, not source reception or hardware qualification.
The later `0x20c0` continuation remains unimplemented and physically
unqualified; it depends on live FIFO count and intervening frame processing.
The unchanged conservative cold probe still stops at `PRECOPY_UNQUALIFIED`.

Image: 2,120 bytes / 265 words, SHA256
`285996c2f2e7ce470dd5fc9e6b8f0f1e7e00ee8dd379e06dd26735b77b920194`.
Tests: 26 source/model and 40 pinned-evidence tests; native ASan/UBSan
preflight/prefix, DMA and recovery tests; freestanding M3/M33 component links
with no unresolved symbols; 133-file isolation audit. Exact commands and
limits are in the [source PSM guide](../../experimental/cyw43/r3/psm-rx/source/README.md).
Ledger key: `follow_up.initial_precopy_probe_2026_09_21`.
No hardware was accessed by that implementation task; no PSM loader or
activation path was added at that checkpoint.

**Subsequent supervised integration:** `PSM_PRECOPY_PROBE=1` now connects
the diagnostic to the existing calibrated owner, not another harness.
After normal-stack acceptance, eight checks and the final quiescent sample,
the source loads and verifies the exact PSM image, clears/checks owned SHM,
binds a generation to recorded handoff fields, and requests bounded PSM
execution and capture. Original owner/guard objects and independent RP
deadline/poll bounds are preserved. Handoff v3 reserves 208 existing tail
bytes; terminal `f0000002`/9 is distinct from ordinary handoff cleanup.
Faults stop source-side peripheral access, and independent recovery owns
reset. The original no-probe profile remains the default.

**Important remaining boundary:** the supported `04020402` start request
keeps MAC_ENABLE clear. This is PSM-start/handshake integration, **not a MAC
receive attempt**. Cold-source MAC enable and physical execution/PSM/RF
qualification remain unresolved; preflight refuses the new profile before
hardware access. A blocked image is not permission to flash. See the
[controller integration contract](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/README.md#supervised-psm-probe-integration-offline-not-receiver-activation).
No hardware was accessed by this integration task; the baseline observation
in section 2 remains the last device-state evidence.
Native transport fault tests (27,006 cases), both default/probe owner paths
on native and Cortex-M3, RP timeout/capture variants and unchanged PSM
model/pinned-evidence suites pass. Exact commands and source hashes are
recorded in the controller guide and ledger key
`follow_up.supervised_psm_probe_integration_2026_09_21`.
The complete offline RP/controller UF2 is separately marked BLOCKED:
source `f005e241`, RP `41d5737d`, UF2 SHA256
`f6d7a87ae97e5cb2ab8c81d9f3d6aee043a2d593d195058bd0d65ee3d0058476`.
`build.py verify-blocked --mode 14` validates bytes/identities but explicitly
grants no flash authority. Source stack bounds remain 864 normal /2,128
private within the unchanged 864/2,240 acceptance limits. Final host
evidence tests (75), probe decoder/stack tests (12) and opt-in gate tests
(5) pass. The offline artifact and its non-production manifest are under
`experimental/cyw43/r3/pmu-startup/pll-probe/build/offline-14/`.

**Historical recommendation, superseded by the later integration and
physical results above:** offline receive-preparation integration under the
existing quiescent owner, not another parallel test harness or RX enable.
Reuse the local buffer-plan API; prove lifetime, RAM non-overlap, rejected
stale ownership and stop/error behavior while preserving the current
no-publication/no-PSM/no-MAC boundary. Label this as software preparation,
not a received frame or a new physical qualification.

The following open questions are current; the superseded offline
recommendation above is not the next task.

| Open question | Evidence needed before advancing |
| --- | --- |
| Can the owner support the full receiver lifecycle? | Qualified transitions beyond the bounded hold; cancellation, deadlines, retirement and independent recovery with the original guard/board/rate objects |
| Who produces fresh RXE input? | The integrated source PSM executes and produces checked format4 prefixes after vendor initialization, including guarded readbacks. Cold MAC enable/READY are observed but cold event timeout remains. Exclude retained upstream data and establish fresh on-air attribution; full-frame continuation, event/EOI and flush/rearm remain unresolved |
| Can rev39 DMA safely own this memory? | Review the integrated RAM map; qualify status/error/coherency/completion and quarantine semantics. Candidate 32-descriptor/31-posted ring, 1,716-byte buffers, arena `[0x20000,0x30000)` and RX offset 38 are not physical frame-layout proof |
| What is a fresh source frame? | Bound input-to-RXE-to-DMA evidence, exact MPDU boundary, known stimulus/nonce and FCS. Vendor scratch captures and AP submission counters are not source reception |
| Can reception remain TX-inhibited through every path? | Persistent RX-compatible coverage of ACK/CTS/BA, templates, offloads, calibration and reset/fault transitions, plus independent RF-energy/IQ observation and suitable containment |

The owner has explicitly deferred the TX/RF measurement gate for bounded
receive experiments. Two badges still cannot certify RF silence, and no
calibrated RF instrumentation/containment is established by this workspace.
Use the explicit experimental opt-in, retain the existing software inhibits
and independent recovery, and keep RF qualification claims false.
[Detailed receive contracts](rx-path.md) |
[Safety decision](safety.md) |
[Full readiness review](../../experimental/cyw43/r3/safe-rx-readiness.md).

Do not reopen solved blockers without contrary evidence: actual D11
PHYPLL request `0x240` at `0x180011e0` activates the group; MIN30/FORCEHT alone
did not. ChipCommon `0x180001e0` is a different instance. PLL8 `0x106a` alone did
not explain RC failure. Do not copy vendor regulator settings, coefficients
or increase calibration deadlines to force success.

## 5. Working files and resources

Paths below are repository-relative; use these owners rather than new runners.

| Work | Entry points |
| --- | --- |
| Quiescent source owner | [receiver_lease.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/receiver_lease.c), [native_stage.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/native_stage.c), [controller guide](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/README.md) |
| Guarded calibration and timing | [cold_stage.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/cold_stage.c), [analog_time.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/analog_time.c), [PHY tables/composition](../../experimental/cyw43/r3/phy-tables/README.md), [TX guard](../../experimental/cyw43/r3/tx-inhibit/README.md) |
| Supervisor, capture and proof | [PLL-probe guide](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md), [supervisor.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/supervisor.c), [evidence.py](../../experimental/cyw43/r3/pmu-startup/pll-probe/evidence.py), [trace_decode.py](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/trace_decode.py) |
| Shared regression tests | [cold_stage_test.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/controller/tests/cold_stage_test.c), [supervisor_test.c](../../experimental/cyw43/r3/pmu-startup/pll-probe/tests/supervisor_test.c), [test_evidence.py](../../experimental/cyw43/r3/pmu-startup/pll-probe/test_evidence.py) |
| Next receive components | [Local preparation](../../experimental/cyw43/r3/rx-preparation/README.md), [source PSM](../../experimental/cyw43/r3/psm-rx/source/README.md), [DMA/recovery](../../experimental/cyw43/r3/rx-dma/README.md), [frame validator](../../experimental/cyw43/r3/rx-validation/README.md) |
| Primary observations | [Evidence index](references.md), [vendor RXE observations](vendor-observations.md), [28 full vendor scratch-frame bindings](scratch-frame-binding.md) |
| Retained prefix and register comparison | [Retained controller](../../experimental/cyw43/r3/rxe-observe/device/retained.c), [source PSM](../../experimental/cyw43/r3/psm-rx/source/README.md), [receiver comparison](../../experimental/cyw43/r3/rxe-observe/compare_receiver_state.py) |
| Fixed catalogue and batches | [Catalogue](../../experimental/cyw43/r3/rxe-observe/catalogue.py), [batch orchestration](../../experimental/cyw43/r3/rxe-observe/batch.py), [runner](../../experimental/cyw43/r3/rxe-observe/run.py), [workflow](workflow.md#bounded-resident-research-batches) |
| External references | [Pinned public catalogue](public-sources.md): WHD/register definitions P01/P03, device PMU P02, exact-firmware tools P05/P06, related PHY P08, clock definitions P10; [DMA/reclaim sources](../../experimental/cyw43/r3/init/rx-status-contract.md) |

External code is corroboration, not an exact-revision recipe or permission
to import dependencies. Preserve pins/licenses and distinguish vendor-runtime
observations from source execution. No new web research is needed to recover
the checkpoint; missing local evidence is not permission to fabricate it.

## 6. Start with offline verification

Known host: macOS; workspace was `/Users/mathiasschindler/universe26`, not a
Git repository. Use relative paths on another host. Existing tools include
host C compiler, Clang/LLD/LLVM, GNU Arm tools, QEMU, libusb/USB utilities,
Python and legacy Ruby. Do not install or replace tooling merely to read
the docs; consult the owning Makefile if a selected command needs a tool.

Select the smallest owning suite. For the current retained catalogue,
from the project root:

```sh
make -C experimental/cyw43/r3/rxe-observe/device RETAINED_PSM=1 test verify
cd experimental/cyw43/r3/rxe-observe
PYTHONDONTWRITEBYTECODE=1 /opt/homebrew/bin/python3 -m unittest discover -v -p 'test_*.py'
```

These are offline tests/artifact checks, not physical runs. The docs-only
refresh did not rerun them. For the separate quiescent source-owner work,
run sequentially:

```sh
cd experimental/cyw43/r3/pmu-startup/pll-probe
# Existing local interpreter; select an available equivalent on another host.
PYTHON=/opt/homebrew/bin/python3
make -C controller cold-native-test cpu-test \
  COLD_PROFILES=calibrated_handoff PYTHON="$PYTHON"
make calibrated-handoff-test active-qualification-test
make host-test PYTHON="$PYTHON"
```

These are software tests, not flashing. `calibrated_handoff` belongs to
`COLD_PROFILES`, **not** `SUPERVISOR_CASES` (only 0..14 and `macphy_clock`).
Extend these harnesses; avoid new atomic runners, Ruby or Pylance
infrastructure. Broader tests and artifact gates are in [workflow.md](workflow.md).
Never build conflicting profiles concurrently in shared output directories.

## 7. Before any later physical test

This documentation update runs no hardware and grants no new experiment
scope. Use [serial selection/recovery](workflow.md#physical-research-selection-and-cleanup)
and the [guarded handoff selection sequence](workflow.md#guarded-handoff-selection-sequence).

1. Confirm the requested experiment and attached research serial; leave the
   reference badge unchanged unless a separately selected paired test needs it.
2. Verify the [frozen recovery assets](firmware.md#frozen-recovery-assets).
   Preserve working root networking and the 133-file outside-file audit.
   Never rebuild/repin recovery files or reset the audit baseline to hide a failure.
3. Check required raw runs, manifests, pinned inputs and recovery proofs are
   present. They may not accompany a source-only copy. Preserve them during
   transfer; stop at missing-input or hash failures.
4. Select the correct lineage. Cold-source activation follows current-code
   MODE0 -> MODE2 -> active v3 qualification -> handoff gates. The retained
   catalogue uses its pinned manifest, vendor positive control and separate
   one-shot/batch recovery contract; it does not satisfy cold prerequisites.
   Historical successful builds are evidence, not flash authority.
5. Keep all inhibits, independent RP bounds and failure handling. After the
   first source fault, do not attempt source-side peripheral cleanup;
   the independent RP owns radio-off/reset recovery.
6. Require strict record replay, explicit retirement and independent recovery.
   In batches the source5840 proof is at the boundary or first failure, not
   per trial. Do not enable MAC/PSM or publish DMA merely because an earlier
   handoff passed.

### Authorized documentation changes and historical isolation hashes

The 2026-10-08 consolidation also intentionally changes
[docs/cyw43-driver.md](../cyw43-driver.md), which is already covered by the
historical snapshot, as well as the CYW43 guides and experiment entry points.
No baseline or evidence pin is updated and no isolation pass is claimed.

The user's2026-09-22 documentation refresh updates four files covered by
the historical outside-file snapshot:
[docs/cyw43-driver.md](../cyw43-driver.md),
[docs/usb-live.md](../usb-live.md),
[docs/wifi-assets.md](../wifi-assets.md) and
[docs/wifi.md](../wifi.md).
Consequently the existing isolation check reports those four documentation
mismatches. The recorded hashes have **not** been repinned; no source,
vendor asset or frozen recovery image was changed.

Do not describe that current audit as passing or ignore additional
mismatches. This task does not authorize changing isolation pins or
bypassing the check. Resolve the documentation-only drift explicitly before
a later physical run that requires a clean current audit. Historical
hardware reports and their pins remain evidence, not rewritten successes.

After each task, update this checkpoint and the owning guide/central ledger:
changes, exact test commands/results, image/run identities, interpretation,
remaining gate and last device state. Preserve raw failures and qualification
inputs. Consolidate observations instead of accumulating compiler logs;
remove only inspected, unreferenced temporary outputs.
