# Evidence, public references and interpretation ledger

[Guide](README.md)

For the current checkpoint and recommended next test task, start with the
[engineer/LLM handoff](handoff.md). This page is the detailed evidence index,
not a chronological task list.
The [consolidated guide](README.md#current-status) owns current conclusions
and work ordering; dated experiment records preserve narrower observations.

## Documentation verification - 2026-10-08

This was a read-only evidence review plus documentation edits, not a new
firmware build, software-suite run, hardware test or RF qualification.

- All 697 local file links and 170 Markdown section references passed across
  19 documentation files, including wrapped link labels. Checks covered this
  guide directory, the controller plan, experiment/R3 entry points, full
  safe-RX readiness checklist and promoted component README. Broken historical
  HOF6/READY section links were corrected; external URLs were not re-fetched.
- The [corrected retained-frame index](../../experimental/cyw43/r3/rxe-observe/runs/reliable-one-shot-2026-09-22.json)
  was compared with all 15 saved report/proof/outcome SHA256s for its three
  consecutive positives and two controls. All matched. Saved proof filenames
  are `report.proof.json`, not `proof.json`; those records are present locally.
  Each positive's 96-byte snapshot contained an 88-byte MPDU with independently
  recomputed FCS and its indexed challenge. The saved outcomes confirm the
  two negative controls and recorded independent recovery.
- All three [frozen recovery assets](firmware.md#frozen-recovery-assets)
  matched their documented SHA256s. They were neither rebuilt nor repinned.
- The September 24 [unarmed AP outcome](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-xkxw1xh0/outcome.json)
  and [released AP outcome](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-vuzj6vqu/outcome.json),
  with their saved reports, support the current negative result, timing
  contrast and independent recovery. They do not establish an event cause
  or on-air overlap.
- Source component contracts were inspected to distinguish implemented
  initialization, PSM checks and DMA APIs from hardware-qualified operation.
  No current all-suite green result or passing isolation audit is claimed.

This check does not replay every historical decoder, prove every image still
rebuilds, authenticate the ROM or vendor data, or qualify analog/RF behavior.
Generated evidence and recovery files must be rechecked before future testing
and may not accompany another checkout.

The [public-source catalogue](public-sources.md) records the current
PMU/PLL, Nexmon and independent-host references, their exact commits,
scope and implementation boundaries. Use it for future source research.

This guide is original synthesis of project evidence and technical facts.
It does not reproduce third-party firmware/source. Public accessibility,
matching bytes and code correspondence do **not** establish modification,
distribution or regulatory permission.

## Primary in-repository evidence

| Subject | Authoritative detailed record |
| --- | --- |
| Opt-in pre-fault cold RXE observations | [2026-10-09 diagnostic](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#opt-in-pre-fault-rxe-diagnostic---2026-10-09) and [validated checks](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-zw6yfadm/rxe-status-checks.json); qualified HOF8, same-image unarmed no-AP/AP zero-fault brackets, post-MAC `0800/f020/0000/0000`, bound negative reports and independent recovery; no frame or airtime proof |
| Retained full-frame versus cold negative controls | [2026-10-09 read-only comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#retained-full-frame-comparison-and-missing-live-rxe-sample---2026-10-09); strict full-frame/FCS replays with the six sampled cold candidate-inhibit settings, temporal-scope-limited 21-register comparison, MACCONTROL difference and missing pre-fault RXE/airtime evidence; no new hardware or cold frame |
| Original-PSM pause retry and event-wait boundary | [2026-10-09 retry](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#original-psm-pause-retry-and-event-wait-boundary---2026-10-09); exact-qualified candidate, three committed negative controls, post-MAC primary fault 6 with/without AP, transitional snapshot interpretation and verified baseline controller rollback; no promoted pause or cold frame |
| Fresh qualification, physical RP capture and held timing | [2026-10-09 follow-up](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rp-capture-controls-and-held-timing---2026-10-09); recovered fresh MODE0/MODE2/active evidence, two committed same-image no-frame controls and source-bound held timing replay; no promoted pause/resume or cold frame |
| RP final-publication race and previous qualification block | [2026-10-08 investigation](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rp-final-publication-race---2026-10-08); software cold error, retained linked diagnostic, failing/passing race regression, baseline rollback and two unexplained zero-SOF MODE2 negatives |
| Publication diagnostic and paused controls | [Latest investigation](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#publication-diagnostic-and-pause-controls---2026-10-08); committed no-frame controls, unchanged extra Thumb instructions, shifted low data, and held-path host timing gate; diagnostic candidate removed |
| AP qualifier repair and rejected PSM pause/resume | [2026-10-08 follow-up](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#ap-storage-audit-and-rejected-pause-experiment---2026-10-08); exact ELF storage audit, candidate publication failures and byte-identical controller rollback; no cold frame |
| Cold READY revalidation and new exact-image prerequisites | [2026-10-08 implementation checkpoint](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#cold-ready-revalidation---2026-10-08); released pre-MAC fault, unarmed no-event control and current-source AP build blocker; no cold frame |
| Board design | [Manufacturer schematic](../../pimoroni/github_badge_2026_schematic.pdf), [confirmed specification](../spec.md) |
| Manufacturer architecture/calibration | [Inspected passages and later Rev. D pin](../../experimental/cyw43/r3/init/activation-evidence.md) |
| Working host and packet measurements | [Wi-Fi milestones/results](../wifi.md), [asset contract](../wifi-assets.md), [USB](../usb-live.md) |
| BLE evidence | [Wireless Lab](../wireless-lab.md), [tilt Pong](../ble-tilt-pong.md) |
| Initial intent, now historical | [Controller-firmware plan](../cyw43-driver.md) |
| Exact inputs, layout and R0 | [Reference contract](../../experimental/cyw43/reference-contract.md), [R0 manifest](../../experimental/cyw43/references.json), [R0 results](../../experimental/cyw43/R0-results.md) |
| Vendor extension / own execution | [R1 results](../../experimental/cyw43/r1/R1-results.md), [R2 results](../../experimental/cyw43/r2/R2-results.md) |
| Current research index | [R3 README](../../experimental/cyw43/r3/README.md), **[central R3 references.json](../../experimental/cyw43/r3/references.json)** |
| D11 init/control/mute | [Init](../../experimental/cyw43/r3/init/README.md), [control](../../experimental/cyw43/r3/init/control-contract.md), [mute](../../experimental/cyw43/r3/init/mute-contract.md), [postconditions](../../experimental/cyw43/r3/init/gates-contract.md) |
| Exact ordering and bindings | [Init path](../../experimental/cyw43/r3/init-path/README.md), [PHY boundaries](../../experimental/cyw43/r3/phy-boundaries/README.md) |
| Registers/calibration | [PHY access](../../experimental/cyw43/r3/phy-access/README.md), [calibration](../../experimental/cyw43/r3/phy-access/calibration.md), [RF overrides](../../experimental/cyw43/r3/phy-access/rf-overrides.md) |
| Tables/channel/radio | [Tables](../../experimental/cyw43/r3/phy-tables/README.md), [RX config](../../experimental/cyw43/r3/phy-tables/rx-config.md), [cold start](../../experimental/cyw43/r3/phy-tables/radio-cold-start.md) |
| DMA code evidence | [Descriptors](../../experimental/cyw43/r3/init/rx-descriptor-contract.md), [reclaim](../../experimental/cyw43/r3/init/rx-reclaim-contract.md), [status](../../experimental/cyw43/r3/init/rx-status-contract.md) |
| Source receive components | [Validation](../../experimental/cyw43/r3/rx-validation/README.md), [preparation](../../experimental/cyw43/r3/rx-preparation/README.md), [memory](../../experimental/cyw43/r3/rx-memory/README.md), [DMA](../../experimental/cyw43/r3/rx-dma/README.md) |
| PSM/RXE/nap2 | [Investigation](../../experimental/cyw43/r3/psm-rx/README.md), [findings](../../experimental/cyw43/r3/psm-rx/findings.json), [public-source manifest](../../experimental/cyw43/r3/psm-rx/sources.json) |
| Measured vendor RXE observer | [Implementation/results](../../experimental/cyw43/r3/rxe-observe/README.md), [guide to canonical datasets](vendor-observations.md); 76 bursts / 908 retained records, configuration/control/scratch/raw-debug evidence |
| Runtime/timing/lab | [Vendor configuration](../../experimental/cyw43/r3/live-queue/rx-configuration.md), [counter dependency](../../experimental/cyw43/r3/rx-clock/README.md), [counter measurements](../../experimental/cyw43/r3/clock-meter/README.md), [stimulus lab](../../experimental/cyw43/r3/rx-lab/README.md) |
| Source startup and precise failure | [Guarded PLL/PHY probe](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md), [immediate pre-RC comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#immediate-pre-rc-comparison---2026-09-20) |
| Causal RC resource dependency | [Removal/restoration experiments](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#rc-dependency-isolation---2026-09-20), [vendor-reference profiles](../../experimental/cyw43/r3/live-queue/README.md) |
| PLL8 and live PMU tables | [Open/close-loop contrasts and source-data comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#pll-mode-and-live-pmu-tables---2026-09-21); vendor history versus source hardware readback remains distinct |
| Powered source PMU readback | [Repeated71-value comparison](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#powered-source-bank-readback---2026-09-21); all values match, but the subsequent MIN request still does not activate the group |
| Source PHYPLL activation | [Two-run activation milestone](calibration.md#source-phypll-activation-observed---2026-09-21); D11 request`0x240` activates the group with MIN unchanged, then stops before RCCAL |
| Source calibration composition | [Two-run hardware completion](calibration.md#complete-guarded-source-calibration-observed---2026-09-21); active delay/table proof consumed, measured RC/ADC/VCO/DC results, AGC/finalization and success-only cleanup |
| Calibrated-state handoff | [Two-run bounded quiescent handoff](calibration.md#bounded-calibrated-state-handoff-verified---2026-09-21); real normal-stack consumer,21-register sampled retention, explicit stop/revoke, observer fixes and independent recovery |
| Cold no-event comparisons | [Six controlled contrasts](../../experimental/cyw43/r3/references.json); key`follow_up.no_event_investigation_2026_09_21`, including staged READY/MAC start and verified20-TU AP configuration |
| Retained PSM takeover and timing | [Initial three-run comparison](../../experimental/cyw43/r3/rxe-observe/runs/retained-comparison-2026-09-22.json), [ten-trial startup/timeout attribution](../../experimental/cyw43/r3/rxe-observe/runs/retained-timeout-investigation-2026-09-22.json); preserve unbound starts, drain failures and old fault meanings |
| Checked source-PSM prefix | [Format4 four-run index](../../experimental/cyw43/r3/rxe-observe/runs/checked-prefix-format4-2026-09-22.json); two initial checked prefixes and two rejected channel6 observations, all vendor-retained |
| Resident catalogue and batching | [Measured workflow comparison](../../experimental/cyw43/r3/rxe-observe/runs/resident-batch-validation-2026-09-22.json); fixed cases, fresh boots and batch-scoped independent recovery |
| Selected receiver state / candidate inhibits | [Comparison index](../../experimental/cyw43/r3/rxe-observe/runs/safe-rx-inhibit-comparison-2026-09-22.json);21 getters, exact cold report pin, three guarded prefixes, one retained pre-MAC drain failure |
| Fresh source-PSM frame attribution | [Eleven-attempt index](../../experimental/cyw43/r3/rxe-observe/runs/fresh-frame-proof-2026-09-22.json); one complete post-ready challenge/payload/FCS-bound MPDU, controls and rejected status-changing repeat; vendor-retained, not cold source-only RX |
| Corrected bounded one-shot repeatability | [Nine-attempt follow-up](../../experimental/cyw43/r3/rxe-observe/runs/reliable-one-shot-2026-09-22.json), [instruction/status investigation](../../experimental/cyw43/r3/rxe-observe/runs/status-discrepancy-investigation-2026-09-22.json); three consecutive fresh frames, clean old-challenge control, same-budget recapture and preserved failures |
| Corrected cold full-frame transfer | [Five-run qualification/transfer index](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/cold-fresh-transfer-2026-09-22.json); new current-code MODE0/MODE2/active proofs, two controller-timeout receive attempts, all independent recovery; no cold frame claim |
| Latest cold checkpoint controls, 2026-09-24 | [Unarmed AP](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-xkxw1xh0/outcome.json), [released AP timing contrast](../../experimental/cyw43/r3/pmu-startup/pll-probe/runs/source-pll-14-vuzj6vqu/outcome.json); both negative and independently recovered, condition-36/event-14 contrast is not a cause or airtime proof |
| Current activation policy | [Owner-authorized experimental policy](safety.md#experimental-receive-policy---owner-direction-2026-09-21), [current handoff](handoff.md); [readiness review](../../experimental/cyw43/r3/safe-rx-readiness.md) and [transmit contract](../../experimental/cyw43/r3/init/transmit-contract.md) retain qualification requirements |

Prefer the module's **final accepted** capture/report references. The central
manifest supplies machine-readable identities, flags, profile numbers and
source bindings. It is not an activation permission file. Historical run
directories and generated binaries may be ignored/unavailable in a clean
checkout; their containing READMEs preserve the conclusions and limitations.

## Pinned public sources

These links identify the sources used by the in-repository investigations;
this documentation review did not re-fetch every source or rerun hardware.
File-level hashes and line ranges are in the manifests above where recorded.

| Source | Pinned reference and purpose |
| --- | --- |
| CYW43 firmware input | [georgerobotics/cyw43-driver `f6d06a70…`](https://github.com/georgerobotics/cyw43-driver/blob/f6d06a70f9710f5854808a8a2803e93700e6038a/firmware/w43439A0_7_95_49_00_combined.h): matching Wi-Fi header |
| Manufacturer datasheet, mirror | [002-30348 Rev. *D](https://datasheet4u.com/pdf/1518674/CYW43439.pdf), 2023-04-17: 87 pages / 1,078,144 bytes; SHA256 `658baa1b64569c55648eb630976e3c872a2a88e02d045279c5276fad44d05d44`; architectural/calibration passages, not an exact-silicon programming manual |
| Board NVRAM | [commit `cf924bb0…`](https://github.com/georgerobotics/cyw43-driver/blob/cf924bb04c8984675ca0fc2178f082e404e048c3/firmware/wifi_nvram_43439.h): exact input provenance, not independent badge calibration |
| Nexmon target | [definitions at `549e7db5…`](https://github.com/seemoo-lab/nexmon/blob/549e7db5bbed07d3a9c99a2dd6a8d6bd7d862a51/firmwares/bcm43439a0/7_95_49_2271bb6/definitions.mk): architecture/layout/patch conventions |
| pico-nexmon | [ROM reader at `36bc8cc6…`](https://github.com/seemoo-lab/pico-nexmon/blob/36bc8cc6194e321dfa27bcf10920501e92c36b01/dump_rom/picow_dump_rom.c): backplane versus CPU ROM mapping |
| Related LCN20 | [Broadcom source in public mirror `481642df…`](https://github.com/blackfuel/asuswrt-rt-ax88u/blob/481642dfe5fa62cc2ccf2b9e656396395181ac58/release/src-rt-5.02axhnd/bcmdrivers/broadcom/net/wl/impl51/main/components/phy/old/wlc_phy_lcn20.c): register/calibration names and differences, not a rev39 replay recipe |
| Related DMA | [Linux v6.12 brcmsmac DMA](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmsmac/dma.c): corroboration for candidate active-state/error handling |
| Core/PLL clock controls | [Linux v6.12 definitions and related control path](public-sources.md#p10---clock-request-and-d11-control-definitions): request/status masks and shared-PLL caveat, not an exact43439 resource map |
| Broadcom initialization | [brcmsmac at `adc21867…`](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c): PSM-before-DMA ordering on other explicitly supported revisions |
| Source-form PSM RX | [OpenFWWF `d4bf46f1…`](https://github.com/fullstory/openfwwf/blob/d4bf46f19046a5511ec964ebcb80e5381c93100a/ucode5.asm): older-core staging/header/flush examples |
| Arch15 encoding | [b43-tools `448c90f7…`](https://github.com/mbuesch/b43-tools/blob/448c90f71a929fd6e144a7bd6faa05308eacaf34/disassembler/main.c): packing/operands; this older tool does not resolve every target opcode |
| `nap2` identification | [Nexmon original `54069b12…`](https://github.com/seemoo-lab/nexmon/blob/54069b12493c3a989869a52ed276e2c39b83e763/buildtools/b43/disassembler/main.c#L535-L565), [unified `243aa116…`](https://github.com/seemoo-lab/nexmon/blob/243aa116f6883582b46fd6fa077502eea78afa12/buildtools/b43-unified/disassembler/main.c#L1047-L1071): encoding/mnemonic, not measured semantics; shared lineage |
| Related RXE register units | [bcm-v4 register documentation](https://bcm-v4.sipsolutions.net/802.11/Registers/): SHM-word destination versus byte copy length, not target timing |
| CCK PLCP interpretation | [Linux v6.12 b43 transmit code](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/b43/xmit.c): rate/service/length correspondence for CRC-checked sampled scratch bytes |

## Ownership and distribution

The stock vendor headers retain their original notices. Generated firmware,
CLM and NVRAM are a **data exception**, not newly project-owned assets.
Driver LICENSE/LICENSE.RP terms do not, by themselves, establish blanket
rights in every firmware/calibration blob. Review the
[full license/provenance boundary](../wifi-assets.md#license-evidence-and-distribution-review)
before distributing generated or modified images.

Similarly, public Broadcom mirrors provide research facts, not automatic
permission to copy source into this project. Target-derived gains, channel
rows and calibration tables require explicit provenance and suitability
review even when executable code is original C.

## Corrections and unresolved interpretations

| Tempting shorthand / old uncertainty | Current accurate statement |
| --- | --- |
| “Radio enable is GPIO41” | Final schematic/working path is GPIO23; GPIO41 is the peripheral rail |
| “A0 name identifies the ROM” | Observed chip revision 5 and ROM-window hash; no semantic mask-ROM label |
| “R2 has not run” in the original plan | Later R2 results prove standalone execution on the reviewed reset path |
| “ROM address means callable standalone service” | Some ROM entries dispatch through erased/reused RAM state |
| “PHY early callback is unknown” | Captured no-op identified; later initializer remains substantial |
| “RC/ADC/TIA calls are opaque” | Nested software bodies now resolved/implemented; minimum cold RX still unqualified |
| “All source-only images hold D11 in reset” | Frozen source5840 does; separate guarded source images activate PHYPLL/resources, complete RX-focused calibration, and execute bounded MAC/PSM receive attempts |
| “HT available means radio calibrated” | HT alone was insufficient. The actual D11 PHYPLL request later activated the required group and enabled source calibration; neither result proves a complete receiver |
| “MAX=7fffffff means all resources are active” | MAX is eligibility; requests/dependencies determine actual active state |
| “Clock offset1e0 identifies one register” | ChipCommon`180001e0` and D11`180011e0` are different instances |
| “A cleared request proves shared PLL unavailable” | Other requesters can retain availability; measure the actual status |
| “The inhibits must be removed to make RC work” | Working reference RC retains all six; resource removal reproduces failure without changing them |
| “RXE destination has no producer” | ARM init Table A sets SHM word `0x620`, copy length 180 bytes |
| “RXE runtime retention is still unobserved” | All 908 canonical vendor samples retain those values; startup acknowledgement timing remains unknown |
| “0x14 is one reset bit” | SPR004 bit 2 flush/disconnect and bit 4 reset are distinct |
| “Opcode 002 is an unknown call/return” | Exact tuple identified as `nap2`; wake/stack effects remain unknown |
| “12 words explain DMA offset 38” | Candidate 24-byte PSM header and observed DMA configuration are not a physical layout proof |
| “82 ticks/us applies to every source state” | Reset-held average is near37.4 MHz; active no-SR source state was separately measured near82 MHz |
| “Implemented DMA means first frame” | Transport is unactivated and not linked into the source image |
| “Original PSM code means source-only reception” | Both the passing prefixes and the fresh full-frame proof inherit vendor initialization; the cold-source path still has no fresh frame |
| “RXCTL command0040 must remain readable” | The tested post-request readback is0800. Format4 binds that exact value plus the other prefix checks; it does not assign a general completion meaning to bit11 |
| “A PSM commit proves a received frame” | Prefixes lack full-frame binding; even a matching full frame is rejected when bracketing status changes. The accepted fresh proof requires all independent C/host checks |
| “A fresh frame with candidate inhibit readbacks proves Safe RX” | Three consecutive fresh frames are proved under guarded retained state, but continuous inhibition, physical TX/RF behavior and cold source-only RX remain unproved |
| “No RF instrument means all testing is prohibited” | The owner permits bounded RF-unverified experiments; qualified RX-only/RF claims still require the separate measurement track |
| “Each batch trial has its own independent recovery proof” | Fresh boot/radio and challenge are per trial; frozen source5840 recovery is at the batch boundary or first failure |

When updating this guide, preserve these distinctions and add a direct
final-evidence link. Do not fill unknowns with plausible names, related-core
defaults or successful mocks. The [vendor measurement record](vendor-observations.md)
distinguishes final halfword-access runs, later hash-qualified interpretation,
the earlier 32-bit discovery run and the retained failed/busy run. Preserve
those classifications instead of overwriting original analyses or counting
failed runs as completed campaigns.
