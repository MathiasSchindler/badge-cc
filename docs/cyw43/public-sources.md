# Public-source catalogue

[Guide](README.md) | [Project evidence](references.md) | [Clocks](calibration.md)

Updated through **2026-09-22**; earlier source pins are retained.
This is a reusable index of public information for
our freestanding C host and source-owned CYW43439 research. A source URL is
not a dependency, an exact-target qualification, or a permission to activate
hardware. Keep repository commits distinct from chip/firmware revisions.

## Highest-priority sources for PMU/PLL startup

### P01 - Infineon Wi-Fi Host Driver (WHD)

- Repository: [Infineon/wifi-host-driver](https://github.com/Infineon/wifi-host-driver).
- Pin: `f0d7589d565f95e6c0576797463a492fb260e265` (release-v4.3.1).
- [Chip-specific mapping](https://github.com/Infineon/wifi-host-driver/blob/f0d7589d565f95e6c0576797463a492fb260e265/WHD/COMPONENT_WIFI5/src/whd_chip_constants.c#L298):
  explicitly selects ChipCommon `0x18000000` as the **43439** PMU base.
- [Chip initialization and SR/KSO handling](https://github.com/Infineon/wifi-host-driver/blob/f0d7589d565f95e6c0576797463a492fb260e265/WHD/COMPONENT_WIFI5/src/whd_chip.c#L1103):
  on 43439, zero SR control1 means firmware SR has not been initialized;
  ordinary HT-request and initialized-SR KSO paths are distinct.
- [Register definitions](https://github.com/Infineon/wifi-host-driver/blob/f0d7589d565f95e6c0576797463a492fb260e265/WHD/COMPONENT_WIFI5/src/bus_protocols/whd_chip_reg.h).

**Use:** authoritative target-aware host mapping, widths, wake and reset
contracts. **Limit:** WHD interacts with on-chip vendor firmware; it is not
the complete source-owned radio initializer. Its unfortunately named
SR-related `+0x64c` constant must not be confused with AOB capability at
ChipCommon `+0xac`.

### P02 - Broadcom device-side PMU and attach code

- Public mirror: [edwardkc/xiaomi_r2d_linux-4.9.73](https://github.com/edwardkc/xiaomi_r2d_linux-4.9.73).
- Pin: `33fd6c6d49e303d75f7aded450dc0fb4704591cb`.
- [hndpmu.c](https://github.com/edwardkc/xiaomi_r2d_linux-4.9.73/blob/33fd6c6d49e303d75f7aded450dc0fb4704591cb/brcm/arm/shared/hndpmu.c):
  PLL programming, oscillator tables, dependency/up-down tables, mask
  selection, transition waits and save/restore of resource policy.
- [siutils.c](https://github.com/edwardkc/xiaomi_r2d_linux-4.9.73/blob/33fd6c6d49e303d75f7aded450dc0fb4704591cb/brcm/arm/shared/siutils.c):
  device attach ordering. The relevant related-chip path performs PMU and
  chip-specific setup, PLL initialization, SR initialization, resource
  initialization, then regulator initialization. SR precedes resource-mask
  selection because that selection consults SR state.

**Use:** the strongest source-form guide to the missing internal sequence,
not merely its host-side trigger. **Limit:** relevant branches are mainly
**43430-labelled**. We have found differences from the exact 43439 image's
PLL tables; copying older tables is not a valid port.

The subsequent [effective-configuration qualification](../../experimental/cyw43/r3/pmu-startup/timing/configuration.md)
binds this analysis to actual initialization-time lookups. Global provider
results, rather than static NVRAM text or late reclaimed queries, determine
the effective `pllN` overrides. The captured absence is not proof of blank OTP.

The inspected `hndpmu.c` header expressly grants use/copy/modify/distribute
with preservation of its copyright and permission notice (ISC-style
terms). That file-specific finding does not establish the terms of every
file or firmware asset in the mirror. Our implementation remains original
C against verified behavior; copied code would need its notices retained.

The same pinned mirror supplies the PLL8 interpretation used on2026-09-21:

- [sbchipc.h, line1573](https://github.com/edwardkc/xiaomi_r2d_linux-4.9.73/blob/33fd6c6d49e303d75f7aded450dc0fb4704591cb/brcm/arm/include/sbchipc.h#L1573)
  names PLL8 bit1 `PMU1_PLLCTL8_OPENLOOP_MASK`.
  File SHA256:`c3afdd137a86481c7e12467db5b2978b691be78bfa65080dadd2b303a6ff6d4e`.
- [siutils.c, line8894](https://github.com/edwardkc/xiaomi_r2d_linux-4.9.73/blob/33fd6c6d49e303d75f7aded450dc0fb4704591cb/brcm/arm/shared/siutils.c#L8894)
  defines `si_pll_closeloop`. The43430 branch at line8931 reads PLL8,
  conditionally clears bit1 and commits under enabled SR. The100-unit delay
  in a different chip branch is **not** copied into the43439 recipe.
  File SHA256:`00b7fa9af61f201d6eef05983af5886c46f48cc81e0f44cef80406a195e7b7a5`.

This is related-chip naming corroborated by the pinned43439 ROM operation.
It is not an exact-silicon analog measurement. The
[hardware contrast](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#pll-mode-and-live-pmu-tables---2026-09-21)
shows RC can still succeed with the bit set in the previously initialized
vendor path, so the source/vendor PLL8 difference is not by itself a fix.

For the read-only control-bank comparison, the same header's
`PCAP5_VC_MASK=07c00000`, shift22, and `PCAP5_CC_MASK=f8000000`, shift27,
decode PMU capability`39d25f18` as seven regulator and seven chip-control
words. The same mirror's `hndpmu.c` uses those masks for PMU revisions>=5;
the observed target is revision24. These bounds identify a candidate
observation scope, not a qualified write policy or a measured state mismatch.

The control-bank investigation also uses the same pinned `hndpmu.c`
(SHA256`8753a1f9c8ea4fb10391f4ef9084736cfc9c4f5998477e5cee5561d5fccaebac`).
Its related43430 switcher setup supplies names for frequency, ramp and
feedback fields; the exact43439 RAM code independently supplies the
matching masks/values. Exact image keys are `lpldo1`, `lnldo1`, `cldo_pwm`
and `cldo_burst`, which differ from some names in the related source.
Header labels for regulator field codes are not physical voltage measurements.
`CC_EXT_LPO_PU` identifies chip-control0 bit21; neither that field nor any
regulator field was changed in the comparison.
[Hardware differences and controlled follow-up](../../experimental/cyw43/r3/live-queue/README.md#control-bank-comparison-and-contrasts---2026-09-21).

### P03 - Published Broadcom/Infineon register definitions

- Repository: [radxa/rkwifibt](https://github.com/radxa/rkwifibt).
- Pin: `b61a1e8499a4f1956ef417cbcfaa6c2cf805ce08`.
- [sbchipc.h](https://github.com/radxa/rkwifibt/blob/b61a1e8499a4f1956ef417cbcfaa6c2cf805ce08/drivers/infineon/include/sbchipc.h):
  ChipCommon/PMU register layout, capability and control masks, timer
  synchronization caveats, and 43430-labelled resource names.
- [hndsoc.h](https://github.com/radxa/rkwifibt/blob/b61a1e8499a4f1956ef417cbcfaa6c2cf805ce08/drivers/infineon/include/hndsoc.h):
  core clock-control/status definitions.

**Use:** names, offsets and access semantics. **Limit:** a register
definition does not prove the order, delay, dependency or board setting.
PMU-status HT bit `0x4`, ChipCommon HT bit `0x20000`, and F1 HT bit `0x80`
are different indications and must remain separate.

### P04 - Linux brcmfmac core discovery/reset

- Pin: `torvalds/linux@238650ef6c7c7cca08e032527329424c9fbd70e5`.
- [chip.c](https://github.com/torvalds/linux/blob/238650ef6c7c7cca08e032527329424c9fbd70e5/drivers/net/wireless/broadcom/brcm80211/brcmfmac/chip.c):
  core enumeration, AOB/PMU discovery and CM3 passive/reset handling.

**Use:** independent corroboration of mapping and core-ordering choices.
**Limit:** brcmfmac is a firmware-dependent FullMAC host driver. Its D11
handling is not proof that releasing D11 is necessary for our HT request.

## Exact-firmware analysis and observation

### P05 - Nexmon's 43439 target

- Repository: [seemoo-lab/nexmon](https://github.com/seemoo-lab/nexmon).
- Pin: `549e7db5bbed07d3a9c99a2dd6a8d6bd7d862a51`.
- [7.95.49 target definitions](https://github.com/seemoo-lab/nexmon/blob/549e7db5bbed07d3a9c99a2dd6a8d6bd7d862a51/firmwares/bcm43439a0/7_95_49_2271bb6/definitions.mk):
  `armv7-m`, RAM `0..0x80000`, CPU ROM `0x800000..0x8a0000`,
  microcode hook, reclaim and template boundaries.
- [Target structures](https://github.com/seemoo-lab/nexmon/blob/549e7db5bbed07d3a9c99a2dd6a8d6bd7d862a51/firmwares/bcm43439a0/structs.common.h):
  candidate structures and offsets; previously used and physically checked
  in our vendor queue-reference experiment.

**Use:** identify routines/objects and design bounded instrumentation in
our exact approved image. The supported version string is
`7.95.49 (2271bb6 CY)`. **Limit:** matching a version string alone is not an
image-hash/layout check. Nexmon patches vendor firmware; it is not a
complete replacement firmware or a proof of source-only cold RX.

### P06 - pico-nexmon ROM reader

- Pin: `seemoo-lab/pico-nexmon@36bc8cc6194e321dfa27bcf10920501e92c36b01`.
- [picow_dump_rom.c](https://github.com/seemoo-lab/pico-nexmon/blob/36bc8cc6194e321dfa27bcf10920501e92c36b01/dump_rom/picow_dump_rom.c).

**Use:** corroborates CPU versus backplane ROM addressing. Our bounded ROM
captures use the existing project-owned loader, identity checks, repeated
readback and D11 reset guard, not a dependency on this program.

## Independent host implementation

### P07 - soypat/cyw43439 (Go/TinyGo)

- Pin: `4a2e02e6d6db317536765aaff07ac46edc438179`.
- [firmware_embed.go](https://github.com/soypat/cyw43439/blob/4a2e02e6d6db317536765aaff07ac46edc438179/firmware_embed.go):
  embedded Wi-Fi/Bluetooth binaries, CLM and NVRAM.
- [device.go](https://github.com/soypat/cyw43439/blob/4a2e02e6d6db317536765aaff07ac46edc438179/device.go):
  host initialization uploads firmware/NVRAM, releases the CYW ARM core,
  then waits for HT availability.
- [bus.go](https://github.com/soypat/cyw43439/blob/4a2e02e6d6db317536765aaff07ac46edc438179/bus.go):
  alternative host transport implementation.

**Use:** independent gSPI/reset/host-protocol comparison, including
allocation-conscious design. **Limit:** heapless does not mean
vendor-firmware-free. This is a host driver, not Go source for CYW PMU/PHY
firmware. Its NVRAM includes `xtalfreq=37400` and `spurconfig=0x3`, but its
board/calibration values are not automatically approved for our badge.
Reading this project does not add Go or TinyGo to our implementation.

## Other relevant public material

### P08 - Related LCN20 PHY and radio20692 definitions

- Pin: `blackfuel/asuswrt-rt-ax88u@481642dfe5fa62cc2ccf2b9e656396395181ac58`.
- [LCN20 implementation](https://github.com/blackfuel/asuswrt-rt-ax88u/blob/481642dfe5fa62cc2ccf2b9e656396395181ac58/release/src-rt-5.02axhnd/bcmdrivers/broadcom/net/wl/impl51/main/components/phy/old/wlc_phy_lcn20.c),
  SHA256 `1d27488793fdfbcbbf0affd9fabe2760c7b3f9253c22aa944cc619b77f925438`.
- [PHY register definitions](https://github.com/blackfuel/asuswrt-rt-ax88u/blob/481642dfe5fa62cc2ccf2b9e656396395181ac58/release/src-rt-5.02axhnd/bcmdrivers/broadcom/net/wl/impl51/main/components/phy/old/wlc_phyreg_lcn20.h),
  SHA256 `01baa5c5a4de1a6c81b033bab27d71b1cbf64282b8ed47f79dbc19f89d85ecd9`.
- [Radio20692 definitions](https://github.com/blackfuel/asuswrt-rt-ax88u/blob/481642dfe5fa62cc2ccf2b9e656396395181ac58/release/src-rt-5.02axhnd/bcmdrivers/broadcom/net/wl/impl51/main/components/phy/old/wlc_radioreg_20692.h),
  SHA256 `6887c0730b505721b202fb1fbb269687b4e9347026deb0ae05d30d0f1475b5ad`.

**Use:** interpret the exact-target startup/calibration captures and distinguish
RX controls, shared supplies and TX settings. The radio definitions identify
TX-LDO power/override at `0x19:0x08` / `0x9b:0x10`, and PA TX-power
control/override at `0x79:0x01` / `0xa4:0x04`. These are **related-source
field meanings, not qualified shutdown writes** for our target.

**Limit:** demonstrated differences include the ADC RC multiplier, RFSEQ
values, TIA entry count and DC delay argument. The exact target delay is32,
not this source's50. Public availability is not a blanket code/data reuse
license or electrical qualification. No implementation is imported.
The [current startup record](../../experimental/cyw43/r3/phy-tables/radio-cold-start.md)
binds these names to fixed ROM captures and records the unresolved mixed
TX/RX power/reset boundary.

The 2026-09-18 follow-up keeps the observed supply sequence and models a
separate PA-output override instead of speculatively removing TX-labelled
supplies. The exact initializer preserves radio `0x79` bit0 and does not write
`0xa4`. This establishes register-policy compatibility, not early-register
access, reset retention or electrical suppression. The related PHY definitions
also distinguish radio/LDO, radio-control, RX-frontend and packet-FSM resets;
none alone establishes the D11 wrapper-reset sequence.

Subsequent [exact-target captures](../../experimental/cyw43/r3/phy-tables/radio-cold-start.md#common-tables-channel-tuning-and-reset-stages)
now supply the missing core/PHY register order: they identify the low-level
core-reset body, 32-bit core-flag accessor and 20-MHz PHY clock sequence.
The initial analog-core callback is a captured no-op, not the generic
analog-control fallback. These target observations supersede reliance on
related-chip reset guesses; board binding and electrical qualification remain
separate requirements.

The later physical source digital/reset/guard tests now close selected
access and post-reset readback questions. Vendor early-inhibit RC tests
also demonstrate compatibility of all six fields through radio power setup
and RC. Neither result establishes continuous electrical suppression or
equivalent earlier initialization history.
[Current calibration evidence](calibration.md#current-hardware-status-and-causal-rc-dependency).

The 2026-09-22 [selected cold/retained comparison](phy-radio.md#selected-cold-versus-vendor-retained-state---2026-09-22)
adds physical readback evidence without changing these public-source pins.
Of 21 selected getters, the three candidate override-enables and small
radio92/93 calibration-count differences separate the historical cold and
working retained snapshots. Three checked six-byte CCK prefixes then coexist
with all six cold candidate-inhibit readbacks in the vendor-retained path.
Upstream-FIFO retention is not excluded: related-source field names plus
those readbacks still do not establish fresh reception under inhibition,
TX silence, SafeRX or the isolated cold cause.

### P09 - gSPI subword-access comparison

- Pin: `georgerobotics/cyw43-driver@bed438f119cfba03f75957fceb6a5623ef95e6d9`.
- [cyw43_ll.c](https://github.com/georgerobotics/cyw43-driver/blob/bed438f119cfba03f75957fceb6a5623ef95e6d9/src/cyw43_ll.c),
  SHA256 `654d83d4c92070d1e71f2805bf0014c3c542ac2c4d98a3a857f4f3d43658e605`.
- [cyw43_spi.c](https://github.com/georgerobotics/cyw43-driver/blob/bed438f119cfba03f75957fceb6a5623ef95e6d9/src/cyw43_spi.c),
  SHA256 `f24f55d39bcadfd1e6170c1ae7e1e83c94e416512e847d57ff2889c3104319da`.

The higher-level backplane helpers accept byte or32-bit accesses, not16-bit
accesses. The SPI module does provide generic16-bit functions using length2,
rounded transfer storage and low-order returned bytes. This is useful host
framing evidence, **not proof of halfword/byte-lane semantics at D11's PHY
data port**. A failed PHY readback under reset must not be attributed solely
to reset without distinguishing transport width/alignment behavior. No
reference-driver code is imported and the protected badge transport remains
unchanged.

The manufacturer implementation provides a more specific contrast:
[whd_bus_spi_protocol.c](https://github.com/Infineon/wifi-host-driver/blob/f0d7589d565f95e6c0576797463a492fb260e265/WHD/COMPONENT_WIFI5/src/bus_protocols/whd_bus_spi_protocol.c),
SHA256 `a620d0de0c4629d2bf7280d4ae5846dee4abbc5841762c8c7379dacbcf5cbf0f`.
Its definition describes address bit15 as selecting **32-bit backplane
access**, and its backplane read/write wrappers set that bit only for
length4, leaving it clear for length1/2. This motivated the subsequent flagged/unflagged lane and write-footprint
tests. Those tests now support unflagged native halfword requests and
corrected physical PHY/radio readbacks; the earlier failures are retained.
The qualification applies to the tested transport/profile, not arbitrary
accesses inferred from framing agreement alone.
[Measured transport correction](../../experimental/cyw43/r3/pmu-startup/pll-probe/README.md#corrected-transport-phy-readbacks).

### P10 - Clock request and D11 control definitions

- Version pin: `torvalds/linux` tag **v6.12**.
- [bcma_regs.h](https://github.com/torvalds/linux/blob/v6.12/include/linux/bcma/bcma_regs.h):
  shared core-relative clock/status offset `0x1e0`, FORCEHT `0x2`,
  external-request mask `0x700` and status mask `0x07000000`.
- [brcmsmac/d11.h](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h):
  independent MAC_ENABLE/PSM_RUN/PSM_JMP_0 fields, wrapper PHY-clock flags
  and D11 PLL request/availability names.
- [brcmsmac/main.c](https://github.com/torvalds/linux/blob/v6.12/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c):
  related-core PHY-PLL request handling; explicitly warns that another core
  can keep a shared PLL available after one requester releases it.

**Use:** distinguish request from status, MAC from PSM and ChipCommon from
D11. These fields were checked during the 2026-09-20 experiments.
**Limit:** common offsets do not prove exact 43439 resource routing or a
source startup recipe. The target's measured request-release behavior and
resource removal/restoration, not names alone, establish the current
[causal RC result](calibration.md#current-hardware-status-and-causal-rc-dependency).

The existing [pinned reference table](references.md#pinned-public-sources)
retains the manufacturer datasheet pin/hash, approved firmware and NVRAM
provenance, related LCN20 source, Linux DMA references, OpenFWWF, b43 tools,
`nap2` identification and RXE register documentation. Those sources remain
useful for the PHY/PSM/RX work beyond the PMU stage. The datasheet is an
architecture/electrical reference, not a complete exact-silicon programming
manual.

## Evidence and implementation rules

1. Record the source ID, immutable commit/path and what it actually says.
   Record file hashes for downloaded evidence where needed; do not silently
   advance a pin when a repository changes.
2. Label **43439-specific**, **related chip**, **exact image**, **model**,
   and **physical observation** separately. A name match is not a port.
3. Bind candidate operations to our approved image/ROM and NVRAM identity.
   Distinguish static code from the branch observed at runtime.
4. Implement verified behavior in project-owned freestanding C with
   bounded waits, explicit errors and recovery. Do not call an unidentified
   ROM service or turn an unqualified block into a success-shaped stub.
5. Keep vendor observation distinct from source-only execution. A vendor
   post-boot register snapshot is an endpoint, not the full write order.
6. Promote an operation to an active source path only after its guards,
   order, timing and failure/recovery behavior are qualified. Public
   availability does not establish RF safety or asset reuse rights.

The historical [cold-clock record](../../experimental/cyw43/r3/clock-meter/cold-start.md)
binds P01-P03 to the reset-held maximum mask`0x0fcefd77`, which excluded
HT-related resources. The later source resource stage obtained HT with
MAX=`7fffffff`, but at that checkpoint still lacked the active 20/21/30 group
needed by the tested RC path. The subsequent original-C D11 PHYPLL request
activates that group; implemented RX-focused calibration and bounded
quiescent handoff now pass. Cold source MAC/PSM activation still has no-event
failures. These are different states, not conflicting measurements or
permission to substitute a public driver's whole mask or PLL table.
