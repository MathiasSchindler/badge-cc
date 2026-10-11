# Safety decision and validation boundaries

[Guide](README.md) · [Workflow](workflow.md) · [Receive implementation](rx-path.md)

## Scope and work ordering - 2026-10-08

The [active milestone](README.md#active-milestone-cold-source-acquisition-of-a-fresh-frame)
is a fresh cold-source frame through the bounded scratch path. General
receiver lifecycle, live DMA, persistent RX-compatible TX inhibition and
broader qualification are [next steps](README.md#next-steps-after-first-acquisition),
not prerequisites for a narrowly labelled first-frame result. Their deferral
does not waive candidate inhibits, identity gates, independent bounds,
fault handling or recovery. This documentation task authorizes no new run.

## Experimental receive policy - owner direction, 2026-09-21

The owner explicitly deferred transmit-inhibition/RF measurement to a
separate track and authorized continued receive experiments on the two
badges. For these bounded experiments, **no unintended emission is an
unverified working assumption, not a measurement or qualification result**.
The absence of an independent RF instrument is no longer an activation
blocker for this explicitly selected experimental scope.

Keep the candidate inhibits, source/RP deadlines, fault containment,
serial selection, immutable recovery assets and independent recovery.
Do not add TX instructions, publish DMA, remove guards, or label RF silence
proved. Require the explicit `--experimental-rf-unverified` host opt-in
for the source PSM experiment and record the deferred measurement in its
outcome. The reference badge may provide normal vendor-firmware AP traffic.
This does not authorize arbitrary RF modes or regulatory overrides.

The requirements below remain the standard for **qualified RX-only/RF
claims**, not a demand to finish the separate RF track before the owner's
bounded receive experiment. This supersedes the earlier blanket NO-GO
policy, not its evidence or the remaining technical unknowns.

## Qualification status: source-only reception and RF safety remain unproved

The immutable source5840 recovery image deliberately holds D11 in reset.
Separate guarded source images now activate PHYPLL/resources and complete
the RX-focused calibration composition on hardware. A bounded quiescent
handoff also passes twice, retaining21 selected registers across normal-stack
acceptance, eight hold checks and stop over267/268 nominal SOF frames.
Original source PSM receive diagnostics now execute physically, and the
vendor-retained profiles produce checked prefixes and three consecutive
fresh challenge-bound frames that pass C and host checks. A complete active
receiver lifecycle, reliable/general full-frame producer, DMA publication
and a fresh cold source-only frame remain unproved. A passing unit test, decoded
routine, nonzero pointer or `ready` flag must not replace those prerequisites.

[The end-to-end readiness review](../../experimental/cyw43/r3/safe-rx-readiness.md)
is the detailed decision record.

| Required closure | Current evidence | Still missing |
| --- | --- | --- |
| Own controller execution | Repeated bootstrap/mailbox/full-RAM ownership proofs | Preserve those checks when integrating larger state |
| Cold PHY/radio/channel path | Active rate/delay/table/power checks and full guarded original-C calibration composition pass twice; success-only request/IO cleanup verified | Complete inline lifecycle/reset qualification and later receiver integration |
| Calibrated-state ownership | Original guard/board/rate objects survive private-stack return; bounded source lease,21-register sampled retention, explicit stop/revoke and cleanup pass twice | Long-lived active receiver lifecycle, whole-hardware/analog retention and transition qualification |
| Time and readiness | Reset-held and active-PHYPLL/no-SR counter domains measured; all8 active minimum-delay trials pass | Other-state timing, analog maximum tolerances and full calibration accuracy |
| PSM/RXE producer | Illegal two-SHM comparisons corrected; bounded same-input recapture passes three consecutive fresh full-frame trials and two controls | Cold event acquisition, general reliability/continuation, output/FIFO/DMA layout and flush/rearm |
| Active DMA | Concrete source transport and ownership model | Reviewed new RAM layout, physical state/error/coherency/completion semantics |
| Frame evidence | 28 complete known vendor MPDUs; three consecutive original-PSM88-byte MPDUs with post-ready challenges, payload and FCS verified; clean old-challenge control | Cold source-only frame, statistical reliability and general modes remain unqualified |
| TX inhibition | Six-field arming/readbacks, reference RC compatibility, three guarded prefixes and repeated fresh frames with checked pre-MAC/terminal values | Continuous all-path interlock, startup/fault coverage and independent emission measurement |
| Recovery | Reset-held proofs; independent source5840 recovery; one-shot retained controller parks, RP captures/offlines it and a fresh boot starts another trial | General orderly drain of an active full receiver/DMA arena; per-trial recovery must not be inferred from batch-level proof |
| RF validation | Two-badge traffic rig passed | Defined independent energy/IQ observation and containment |

The [historical corrected cold full-frame transfer](handoff.md#cold-full-frame-transfer-tested---historical-hof6-2026-09-22-checkpoint)
was tested with its matching recovered MODE0/MODE2/active-PHY
prerequisites. Both AP100TU and AP20TU attempts verify code, cleared SHM,
READY and MAC enable, but expire the unchanged controller budget with the
last source mailbox at receive-event wait and no snapshot commit. No
deadline, guard policy or RF qualification was relaxed to convert them
into successes. Cold frame production remains unproved.

## TX inhibition is an end-to-end contract

Candidate masked pairs from related LCN20 source:

| Candidate | Clear value first | Set override-enable next |
| --- | --- | --- |
| Internal TX chain | PHY `0x44d & ~0x4000` | PHY `0x44c`, mask `0x4000` |
| Internal PA | PHY `0x939 & ~0x0800` | PHY `0x938`, mask `0x0800` |
| Radio PA-output candidate | Radio `0x79 & ~0x0001` | Radio `0xa4`, mask `0x0004` |

These three pairs form **six participating-writer fields**, not a measured
all-path interlock. Source arming/readbacks pass, and controlled vendor RC
completes with all six set before radio power setup and retained after RC.
Removing resources 20/21/30 reproduces the RC failure without changing those
fields. This is compatibility evidence for that calibration context, not
permission to remove the guards or assume electrical silence. Other paths
release or reverse them:
idle-TSSI cleanup, `rx_pu(FALSE)`, `tx_pu(FALSE,...)`, TXIQLO, ordinary
register initialization and whole-word calibration restoration. A function
argument named `false` is not necessarily force-off.

The2026-09-22 [guarded-prefix comparison](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json)
adds a narrower physical fact: three checked prefixes coexist with all six
full readback words matching the cold fingerprint before MAC enable and
at terminal capture. The explicit retained profile performs only the six
masked updates and preserves unrelated bits; the cold policy is unchanged.
Baseline and guarded profiles both observe the two checkpoints, but the
baseline performs no guard data writes or selected-policy enforcement.

That prefix-only result is **not** proof that fresh RF frames arrived after
arming. The later [corrected fresh-frame experiment](handoff.md#corrected-bounded-recapture---current-retained-2026-09-22-checkpoint)
does exclude old FIFO contents for three consecutive accepted post-ready
challenge-bound frames. Neither experiment's readback checkpoints prove continuous
enforcement or TX silence between them. One earlier guarded prefix attempt
failed initial event drain before MAC enable; its batch aborted and
recovered. Do not label the new catalogue case `safe_rx` or remove a cold
inhibit on the strength of this result.

Do not substitute:

- MAC off/suspend: may stop RX too; nesting/timeout returns do not prove completion.
- Software mute or role overrides: later shadow writers can undo effective state.
- TX queue suspension: does not cover direct template/dummy-TX registers.
- Zero BB multiplier, power index, TSSI detector settings or eTR switch:
  not established PA/ePA shutdown.
- “No outgoing host packets”: does not cover ACK/CTS/BA, beacons/templates,
  offloads, calibration or reset/error transients.
- D11 stopped after vendor startup: does not remove retained PHY/SHM/RXE state.

The exact [transmit contract](../../experimental/cyw43/r3/init/transmit-contract.md)
and [calibration analysis](calibration.md) identify these separate paths.
No independent analogue-off acknowledgement, external-PA shutdown or
reset-retention guarantee is established.

The new [source-writer guard](../../experimental/cyw43/r3/tx-inhibit/README.md)
implements candidate-pair arming/readback and rejects later participating
PHY/radio writes that would release or reverse them. The RC/ADC/TIA/VCO
code passes through this interface; the source calibration composition
and later bounded quiescent handoff now complete on hardware. This is software
enforcement, **not a qualified physical TX interlock**: raw-bus bypass,
PSM, hardware defaults/reset and other analog controls remain outside it.

The [RXE recovery protocol](../../experimental/cyw43/r3/rx-dma/README.md#source-owned-rxe-recovery-protocol)
also now issues a single `0x14` request and checks both bits clear under
bounded, explicitly quiesced-source prerequisites. It is not connected to
the current badge payload and is not an active-receiver recovery proof.

## Recovery and irreversible-state boundary

R2/R3 use RAM-only controller changes, fixed allowlists, bounded operations,
fresh challenge/CRC/build identity, independent RP supervision, radio-off
cleanup and BOOTSEL recovery. No OTP, permanent controller storage or
execution-protection setting is changed.

R3a's deliberate undefined instruction records an owned HardFault result;
the stall scenario produces a bounded RP timeout. A discovered request-arrival
race was fixed in the R3 hook and tested with the precise interleaving.
The failed run remains evidence. Those R3a tests are real **reset-held**
recovery proofs. Later guarded startup/reference attempts also pass
independent power-off, source5840 execution and BOOTSEL recovery. Neither
establishes a general orderly drain/shutdown of an active source receiver
with PSM/DMA-owned buffers.

A callback may fail after its write reached hardware. For source DMA,
uncertain publication quarantines the object **and its arena** until
independent cold recovery. Reinitializing a C structure cannot revoke
hardware ownership. Raw-tick and sample bounds do not make a non-returning
bus callback safe without the independent supervisor.

The fixed resident catalogue uses the same independent supervision rather
than resuming damaged vendor state. Each trial gets a fresh RP boot,
radio initialization, challenge and positive control. After source
replacement, only capture/retirement and RP-only reboot are permitted;
the live-session retirement flag is not cleared.

Standalone runs retain per-run frozen recovery. In explicit batches,
independent source5840 execution occurs **once at the batch boundary or
first failure**; a trial's BOOTSEL record is not a separate baseline proof.
Batches stop at the first failed case, keep the AP within its existing
lease, and require final independent recovery before reporting batch
success. The120-second/four-trial cap and10-second AP margin do not extend
the radio's180-second lease. Recovery replaces the candidate UF2; there is
no new dual-slot boot path or modified baseline.
[Exact workflow and measured limits](workflow.md#bounded-resident-research-batches).

### Keep evidence levels distinct

1. Nonzero scratch bytes or a matching CRC are data observations.
2. A PSM commit marker records that program's local snapshot checks.
3. A checked format4 prefix additionally passes identity, status, length,
   exact RXCTL0800 and CCK validation in both C and the host.
4. Fresh-frame attribution needs stimulus/nonce and complete-frame evidence,
   including exclusion of retained upstream data where relevant.
5. Qualified Safe RX additionally needs the persistent TX/RF contract above.

Levels1-3 do not imply4 or5. In particular, a PSM-committed channel6 sample
was rejected by the CCK checker, and RXCTL0800 also occurred with length0.
No general completion meaning for bit11, transaction completion, FIFO
quiescence or raw-copy atomicity is inferred from the accepted readback.

## What each validation layer can prove

| Layer | Useful evidence | Not established |
| --- | --- | --- |
| Native C + sanitizers | Bounds, overflow policy, descriptor ledger, failure injection, packet validation | Bus coherency, analog operation, hardware ownership |
| Captured-code QEMU | Unchanged selected instructions, arithmetic/port-call order versus source C | A CYW simulator, RF effects, timing, successful omitted callees |
| Python/Ruby evidence tests | Exact input hashes, typed records, report consistency, bounded static tracing | Authenticity of arbitrary edited JSON or physical execution of every path |
| Reset-held ROM capture | Bytes from the observed identity and safe read profile | CPU-visible runtime patch equivalence or permission to invoke a routine |
| No-UP vendor reference | Allocated object roots/configuration after stock startup | Source-only operation, active-to-DOWN drain or startup RF silence |
| Bounded pre-RC vendor contrast | Defined resource/clock changes, RC completion/failure and inhibit retention at the measured boundary | Complete vendor UP, source-only calibration, continuous RF silence or equivalent earlier initialization |
| Bounded source calibrated-state handoff | Real consumer after private-stack return; live selected-register checks, explicit stop/revoke and independent recovery | Indefinite or whole-hardware/analog retention, active reception, configured state after reset or continuous RF silence |
| Vendor-retained source PSM | Original code/readback/READY, checked prefix, candidate guard checkpoints and independent capture/retirement | Source-only initialization, fresh on-air attribution, full frame, transaction/FIFO completion or RF silence |
| Fixed catalogue batch | Verified resident image, fresh boot/radio/nonce/positive control per trial, bounded queue and independent recovery at the boundary | Warm-session reuse, per-trial baseline proof, arbitrary payload execution or additional receiver qualification |
| Two-badge lab | Fresh identifiable vendor reception and recovery | Source reception in a later phase, RF-energy silence or arbitrary on-air frame format |

Examples of substantial but bounded software coverage include all 32,768
declared ring geometries and u16 length inputs in the validator, 100
model DMA copy/refill cycles across wraps, and failure injection at
9,108 start / 425 receive / 63 stop callback positions. PSM analysis has
39 tests, including complete 16-bit control-word arithmetic pools.
These counts do not convert into physical qualification.

## RF measurement must have an explicit scope

“Absolutely no RF energy” is not a measurable universal claim. An agreed
test must state channel/bandwidth, observation duration, sensitivity,
transient coverage, device attribution and acceptance limits. Independent
calibrated RF-energy/IQ observation and suitable controlled coupling are
needed; missing decoded packets on another badge is not enough.

The [completed vendor observer](vendor-observations.md) can transmit normally.
Its 76 bursts include four after our AP radio is off, **not RF silence**.
On-chip ARM sampling preserves selector ownership but can miss short
busy/flush pulses. The original campaign missed the flush assertion; a later
read-only fast trigger caught **`0x814 → 0x800`**. That is a real set/clear
observation, not proof of hardware self-clear, universal latency or RF silence. Continued vendor
RX does not prove zero perturbation, nonce-specific scratch-frame binding
or complete ownership transfer. Raw PSM debug does not settle `nap2` stack
semantics. These observations do not establish qualified source-only
reception or Safe RX. They do not reinstate the superseded blanket ban on
the explicitly authorized, bounded, RF-unverified experiments.
