# CYW43439 on the Universe 2026 badge

An engineering guide to the badge's wireless hardware, the working
project-owned Wi-Fi host, and the separate controller-firmware research.
Documentation consolidated **2026-10-08**, followed by explicitly authorized
cold-source implementation and hardware diagnostics through 2026-10-09.
The documentation-only review itself performed no firmware execution or RF
measurement. This page owns the current status and priorities;
the linked experiment records retain exact captures, manifests and failures.

The reusable on-chip C modules are now owned by
[`src/cyw43/`](../../src/cyw43/README.md), separate from the RP2350-side
[`src/radio/`](../../src/radio/). The original R3 paths remain compatibility
links for test and evidence workflows. The promoted component link is not a
new runnable firmware image: source-built controller experiments remain
explicit opt-in builds with their existing qualification gates. Default badge
networking continues to use the pinned vendor radio image.

## Start here

Read the current status and milestone below, then the
[handoff](handoff.md) for hardware roles, recovery and checkpoint history.
[Workflow](workflow.md) owns commands; [references](references.md) owns the
evidence index. A historical build or successful test is not flash authority.

## Current status

**Own code executes on the radio, but a fresh cold-source frame has not been
proved.** Keep these results separate:

| Track | Strongest recorded result | Not established |
| --- | --- | --- |
| RP2350 host with vendor radio firmware | Own gSPI/control/ARP/IPv4/UDP; 600/600 baseline echo transactions | Source-owned radio firmware |
| Cold-source Cortex-M3 execution | Own vectors/startup/mailbox, full-RAM ownership and reset-held fault/stall recovery | Wi-Fi reception from execution proof alone |
| Source initialization/calibration | Source PHYPLL activates resources 20/21/30; guarded RX-focused calibration passes twice, with 908 table entries and timing/readback checks | Analog accuracy, whole-state equivalence or a complete receiver |
| Calibrated-state handoff | Two quiescent-owner passes; 21 selected registers retained over 267/268 nominal SOF frames, followed by stop/revoke/cleanup | Indefinite active receiver operation |
| Vendor-retained original PSM | Three consecutive fresh 88-byte MPDUs, full payload/challenge/FCS checks and passing no-burst/clean old-challenge controls | Cold-source reception, general reliability or RF safety |
| Cold-source original PSM | Bound pre-fault RXE samples before PSM and after MAC, with/without AP; earlier READY revalidation rejects a condition-36 fault before MAC | A received frame, receiver readiness or the cause of the no-event result |
| RX DMA and recovery components | Concrete C transport, descriptors, ownership validation and bounded recovery protocol | Live source descriptor publication, physical completion or FIFO/frame format |
| Candidate TX inhibition | Six participating-writer fields, checked before MAC and at retained-frame capture | Continuous RX-compatible interlock or measured electrical silence |

The cold-source path downloads no vendor executable firmware or microcode,
NVRAM or CLM and calls no vendor ROM services; explicitly pinned ROM
configuration/table data remain a dependency. The frozen **source5840**
recovery image holds D11 in reset and is not the larger startup/receive image.

### Latest cold checkpoint: 2026-10-09

The owner-selected [opt-in pre-fault RXE diagnostic](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#opt-in-pre-fault-rxe-diagnostic---2026-10-09)
is implemented and tested on both unarmed no-AP/AP controls, source
`b7c96b54` / RP `fd5cfb1d`. Both zero-fault-bracketed samples show
RXCTL/CORECTL/length/status `0800/f000/0000/0000` before PSM and
`0800/f020/0000/0000` after MAC. Both commit CRC/request-bound negatives:
controller time error 6, primary fault zero, no frame. The post-MAC control
pair matches two earlier retained observations, not whole-state equivalence
or listening proof. AP submissions `1 -> 167` do not establish airtime overlap.
The original PSM, limits and inhibits are unchanged; HOF8 is explicit opt-in,
default fresh remains HOF7. Both badges independently recovered to BOOTSEL.
Next is verified stimulus overlap, then specific RX/event-path evidence if
needed; the fresh-frame milestone and four broader workstreams remain open.

**Earlier comparison and pause rollback:**
The [retained full-frame comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#retained-full-frame-comparison-and-missing-live-rxe-sample---2026-10-09)
replayed three full fresh-frame/FCS positives with the same six sampled
candidate-inhibit settings as cold operation. Only radio `92/93` count samples
differ in the selected 21-register comparison; one pair matches all selected
values across the recorded sampling points. This is not an atomic snapshot,
whole-state equivalence or cold qualification. A WAKE/DISCARD_PMQ MACCONTROL
difference is recorded without claiming a cause or changing bits.
At that comparison checkpoint, direct pre-fault RXE state and stimulus overlap
were missing. The diagnostic above now provides the former; overlap remains
unproved. No additional hardware trial was run for the comparison itself.

The [original-PSM pause retry](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#original-psm-pause-retry-and-event-wait-boundary---2026-10-09)
qualified source `ef634564` / RP `4d5d8688` and captured three same-image
negative controls. Both paused controls passed READY revalidation and reached
MAC, then primary PSM fault 6, with and without the AP. The AP's 181 submissions
do not prove airtime overlap. Fault 6 establishes bounded condition-14 wait
exhaustion, not a PHY root cause; `[7,3,6]` and raw zeros are potentially
transitional samples because the controller aborts at the first primary fault.
The candidate was removed. Baseline controller bytes are restored; rebuilt
RP `316c8db6` passes strict preflight but is not a new physical receiver trial.
Its identity differs from `0c214ad4` because identical decoded controller
metadata was serialized with a different stack-proof map key order.
Both badges independently recovered and were queried in BOOTSEL.
That rollback motivated the bounded pre-fault diagnostic above, rather than
another identical pause/AP trial or live DMA.
No cold frame or pause candidate is promoted.

### Earlier capture checkpoint: 2026-10-09

The [capture follow-up](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rp-capture-controls-and-held-timing---2026-10-09)
obtained fresh baseline MODE0/MODE2/active qualification without changing limits.
Source `ab121779` / RP `0c214ad4` passes strict preflight. Both no-AP receiver
controls capture CRC/request-bound negative reports: unarmed MAC/event-14 wait
then controller timeout 6, and released checkpoint fault 5 before MAC.
The RP fix therefore captures reports on hardware in these controls, but the
earlier zero-SOF failures and attribution of every historical report loss remain
unproved.

The host timing checker now distinguishes verified checkpoint-held spans from
unarmed fast paths; the archived 393-I/O paused controls replay within existing
source budgets, without becoming frame or RF proofs. The unarmed budget still
includes preparation, unlike the held path's separate receive budget.
Both badges independently recovered and were queried in BOOTSEL; no AP was
started in that phase and no frame was captured. Its proposed original-PSM
pause/resume experiment was subsequently tested as described above.

### Previous cold checkpoint: 2026-10-08

The [latest investigation](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rp-final-publication-race---2026-10-08)
found and reproduced an RP reporting race: a stale zero HOF commit can be paired
with a later expected software FAULT, losing a committed negative report.
The source fix and regression tests pass, but physical requalification stopped
at two zero-USB-SOF MODE2 timing failures. No receiver test with the fixed RP
passed its prerequisites. Source `147dc17f` is restored byte-identically; rebuilt
RP `0cd9e65e` is **not flash-qualified** by the old records. Both badges recovered
and were queried in BOOTSEL. No cold frame.

Previous diagnostic:

The [latest publication diagnostic](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#publication-diagnostic-and-pause-controls---2026-10-08)
committed source reports in all three controls. The original-PSM pause path
reached MAC and primary fault word 6 both with and without AP stimulus, but
obtained no frame. Its extra Thumb instructions match the earlier rejected
candidate; 16 literal/data words differ. This does not establish why that
earlier candidate failed publication. Held controls also expose an unarmed-only
host timing gate, so they are not passing checkpoint qualifications.
The diagnostic/pause candidate was removed again; the byte-identical known-good
source and strict original prerequisites remain active. Both badges recovered.

The AP qualification blocker is now resolved: the frozen comparison rejected
an already-added eight-byte diagnostic-report storage change, not changed AP
instructions. The exact metadata-only exception remains strictly checked.
An owner-approved original-PSM pause/resume candidate then passed native and
exact-image gates but failed source publication physically, including in an
unarmed control. It was removed, not promoted to a working receiver.

The [restored AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-c3s6zjpa/outcome.json)
uses source `147dc17f` / RP `e5930e3a` and again commits MAC/event-wait evidence
followed by bounded error 6, with no frame and independent two-badge recovery.
The source binaries exactly match the previous verified source image.
See the [previous implementation checkpoint](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#ap-storage-audit-and-rejected-pause-experiment---2026-10-08)
for rejected candidates, tests and remaining synchronization blocker.

Earlier in this implementation phase:

Source `147dc17f` / RP `fc548b94` was rebuilt against current shared sources
and passed strict preflight after new MODE0, MODE2 and active-PHY qualification.
The [released no-AP trial](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-ui_ih__v/outcome.json)
accepted USB release but observed PSM condition-36 fault 5 before requesting
MAC; both MAC flags and STOP remained zero. PSM continues running during the
host hold, so USB release cannot make the original READY observation durable.
The controller now rechecks a stable, identity-bound READY after the hold,
using unchanged budgets and fault-first handling.

The [unarmed no-AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-yr2n4xju/outcome.json)
still observes MAC readback and event-14 wait, then the bounded controller
timeout (error 6). Neither trial captured a frame; both independently restored
source5840 and BOOTSEL. Both badge serials were also queried in BOOTSEL.

The [AP contrast attempt](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-aae4639x/outcome.json)
stopped before either flash: the existing AP no longer matches current shared
sources. An isolated rebuild then failed its frozen non-opt-in `main.o`
comparison (0 TU). No guard was weakened and no new AP image was authorized
by that failed build. See the [implementation checkpoint](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#cold-ready-revalidation---2026-10-08)
for tests, exact prerequisites and remaining blockers.

### Previous cold checkpoint: 2026-09-24

Source `f5b89561` / RP `0ca84492` was used for the recorded controls.
The [unarmed AP control](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-xkxw1xh0/outcome.json)
reports no frame, a 64,650-tick READY-to-MAC sample gap and event-14 wait.
The [released AP contrast](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-vuzj6vqu/outcome.json)
reports no frame, a 4,365,418-tick gap and source fault 5 at condition 36.
Both record independent source5840 recovery, AP shutdown and both BOOTSEL
checks. These are last recorded states, not current attachment discovery.

The timing difference is a correlation, not a cause. Source-written mailbox
samples are non-atomic; AP submissions do not establish airtime or overlap
with the short receiver window. There is no independent on-air witness.
Earlier HOF6 timeouts and HOF7 header/polling failures retain their own
identities in the [checkpoint history](handoff.md#checkpoint-history).

## Active milestone: cold-source acquisition of a fresh frame

The next implementation objective is **one complete, identifiable, fresh
frame after source-owned cold initialization**, using the existing bounded
PSM/scratch evidence path. It does not require live DMA or a general
receiver. Implementation and bounded tests are now authorized and underway;
the first-frame milestone remains **unproved**.

The pause retry passed READY revalidation and reached MAC, but still exhausted
the original PSM's bounded condition-14 wait. The immediate investigation is
stimulus overlap versus cold RX/event configuration using pre-fault evidence.
The active controller remains the baseline; no pause candidate was promoted.
Fresh qualification,
physical RP capture controls and held-path host timing analysis now pass,
without proving the historical candidate's publication failures fully explained.
The AP's current-source qualification blocker is resolved. Revalidation alone
contains the stale-READY bug; it neither extends the PSM wait nor proves RXE
listening.

Acceptance requires:

1. Exact device/image/input identities and source-owned cold initialization;
   no retained vendor initialization substituted for the cold path.
2. Actual code/SHM readback and bounded READY/MAC/terminal evidence, with
   matching qualification prerequisites for the selected image.
3. A complete MPDU with checked header, full identifiable payload and FCS,
   attributable to fresh stimulus in that attempt. A prefix, stale bytes,
   AP counter, READY flag or changing/uncommitted snapshot is not a pass.
4. Independent on-chip and host acceptance, explicit failure records and
   independent recovery. One accepted frame proves first acquisition only;
   repeatability and negative controls must support any stronger claim.

Preserve candidate inhibits, source/RP deadlines, fault-first observation,
serial-bound selection and immutable recovery. Do not bypass the retained
burst's baseline-frame gate, enlarge bounds or publish DMA to manufacture
a pass. Bounded RF-unverified experiments remain distinct from qualified
RX-only operation under the [experimental policy](safety.md#experimental-receive-policy---owner-direction-2026-09-21).

## Next steps after first acquisition

These are deferred workstreams, not additional acceptance criteria for the
first-frame milestone and not claims of readiness:

| Next step | Existing starting point | Closure still needed |
| --- | --- | --- |
| General receiver lifecycle | Bounded one-shot PSM capture and quiescent ownership | Varied frames, discard/rearm, sustained operation, errors, stop and ownership retirement |
| Live DMA | Source descriptor/planning/transport/validation components | Integrated RAM ownership, physical completion/coherency, FIFO/header format and safe refill/drain |
| Persistent RX-compatible TX inhibition | Participating-writer guard and six candidate-field checks | All-path continuous enforcement, startup/fault coverage and independent emission measurements |
| Broader qualification | Exact-image, selected-board/channel/timing evidence | Reproducibility, analog accuracy, other clock states/channels/boards and applicable RF/regulatory validation |

Deferral does not relax existing experimental safety controls. A first frame
will not establish RF silence, production readiness or general reliability.

## Documentation map

| Read | For |
| --- | --- |
| [Engineer/LLM handoff](handoff.md) | Resume checklist, last recorded hardware state and preserved checkpoint history |
| [Hardware](hardware.md) | Final pins, RM2 supplies, antenna/oscillator evidence and old discovery pitfalls |
| [Host driver and networking](host-network.md) | gSPI, firmware loading, SDPCM/CDC/BDC, packet subset, Wi-Fi/BLE measurements |
| [Controller execution and ownership](firmware.md) | Identity, RAM/ROM maps, R0–R3 evolution, exact image sizes/hashes, runtime dependencies |
| [PHY/radio interfaces](phy-radio.md) | Address spaces, access widths, init scripts, hardware tables, channel/RX configuration |
| [Calibration and clocks](calibration.md) | Completed source calibration and bounded handoff, causal resource experiments, clock domains and failure contracts |
| [Receive engine, PSM and DMA](rx-path.md) | Header producer, RXE scratch configuration, `nap2`, descriptors and ownership |
| [Measured vendor observations](vendor-observations.md) | Pre-RC power/clock contrasts, plus 76 RXE bursts / 908 records, progression and limits |
| [Complete scratch/frame binding](scratch-frame-binding.md) | 28 complete known MAC/nonce/payload/FCS matches across three packet lengths |
| [Safety and evidence limits](safety.md) | Current gates, recovery, RF observation requirements, model versus hardware proof |
| [Build and hardware workflow](workflow.md) | Existing commands, serial-bound selection, reports and isolation rules |
| [References and provenance](references.md) | Primary evidence index, pinned public sources and unresolved interpretations |
| [Public-source catalogue](public-sources.md) | Reusable source pins, target scope, PMU/PLL leads and firmware-dependency distinctions |

## Keep these four configurations separate

| Configuration | RP2350 side | CYW43439 side | Established scope |
| --- | --- | --- | --- |
| Optional SDK demos | SDK/newlib and CYW43/lwIP/BTstack integrations | Vendor radio runtime | Reference applications, not the freestanding stack's dependency closure |
| Working freestanding Wi-Fi | Own startup, gSPI, control and ARP/IPv4/UDP | Vendor firmware and vendor PSM, with NVRAM/CLM | Working networking and the600/600 echo baseline |
| Cold source-only research | Own loader, USB and bounded supervisor | Original Cortex-M3 code and original PSM diagnostics; pinned ROM configuration/table data still used | Execution, initialization/calibration, ownership and bounded receive attempts; no fresh source frame |
| Vendor-retained source-PSM experiments | Own supervisor and fixed prefix/fresh-frame profiles | Vendor initializes the radio; an original controller then replaces PSM, captures and parks | Checked prefixes and three consecutive fresh complete frames, not source-only initialization or RF qualification |

An ARM program running in the radio is not necessarily a Wi-Fi implementation:
the D11 PSM is a separate programmable engine. The RP UF2 can carry radio
payloads that are uploaded into CYW RAM; that is not programming persistent
flash inside the Wi-Fi chip. Original C/no target libc does not make a
vendor-initialized experiment source-only, nor remove the working stack's
vendor firmware/data dependencies.

## How to interpret a statement

| Evidence class | What it establishes |
| --- | --- |
| Schematic | Manufacturer design connection; not assembled-board behavior |
| Exact image/capture | Bytes, local instructions and data for a pinned input; not physical execution of every path |
| Related public source | Names and corroborating mechanisms; not automatically a rev39 recipe |
| Host/CPU test | Software arithmetic, ordering and error handling under declared models |
| Hardware observation | The stated device, image, reset state, experiment and measurement conditions |
| Candidate / pending | A design input or experiment to qualify, not permission to activate |

Historical “planned,” “unresolved” and “not yet tested” statements remain in
earlier milestone reports. For example, the original
[controller plan](../cyw43-driver.md) predates the successful R1/R2 work.
Use the corresponding final result and later follow-up rather than treating
every old status paragraph as current. Conversely, later software progress
must not be used to rewrite an earlier failed run as a pass.

### Vendor-runtime observations and later retained experiments

The [vendor RXE campaign](vendor-observations.md) is complete on channels
**1 and 6: 76 bursts / 908 retained records**. All records retain destination
`0x620`, copy length 180 and the request/status bits. On-chip samples capture
receive progression and **`0x802 → 0x801`** stage/publication control; selected
scratch words form a CRC-valid 11 Mbps CCK PLCP candidate. Complete raw
PSMDEBUG values were retained without an assumed PC mask.

These are **vendor-runtime measurements**, not source-only RX. A subsequent
fast-trigger campaign caught **`0x814 → 0x800`**: bits 2 and 4 set, then clear,
without an observer control write. The clearing cause, full request latency
and `nap2` wake/stack semantics remain unresolved. The original metadata-only
records are non-atomic and capped. A later [full scratch capture](scratch-frame-binding.md)
binds **28 complete known frames** at three packet lengths, with PLCP at
scratch byte 2 and MAC frame at byte 8. This is vendor input-scratch evidence,
not a source-only receive or DMA-prefix proof. The runs recovered to the
unchanged source baseline and BOOTSEL. The
[source RX DMA transport](../../experimental/cyw43/r3/rx-dma/README.md)
remains unactivated. Later original-PSM prefix experiments do not replace
those full vendor-frame bindings. The separate fresh-frame experiment adds
source-PSM bindings under vendor-retained initialization, not fresh
cold source-only reception.
Qualified Safe RX remains unproved; bounded experiments follow the current
explicit policy rather than the older blanket NO-GO.

This directory is documentation-only. It does not change working code,
research images, vendor assets or the outside-file isolation baseline.
