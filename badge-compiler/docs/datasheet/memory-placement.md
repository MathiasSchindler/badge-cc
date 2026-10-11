# D08: memory-placement decision

**GO for a bounded selected-function experiment (S08-MEM), not automatic
placement. NO-GO for moving all code to SRAM, changing bus priorities or
remapping banks as a default.** This decision enables no compiler policy.

## Applicability and evidence

The RP2350 Arm configuration separates Code/System routing with
`UNCROSS_I_D`, but physical SRAM bank ports remain shared. Main SRAM consists
of two 256 KiB word-striped four-bank groups; the remaining two banks are
4 KiB each. XIP hits, cold entry and concurrent traffic are different regimes.
See [memory facts](memory-system.md), [source ledger](fact-ledger.json)
(`RP2350-BUS-001`, `RP2350-SRAM-001`, `RP2350-XIP-001`) and the conditional
[D02 calibration](hardware-calibration.md). E11 cache maintenance and E27
bus-priority restrictions still apply.

[D08 evidence](../../build/datasheet/d08-results.json) inspects the hash-bound
D07 default control corpus: 37 modules and 12 images. None has a nonempty
executable main-SRAM section. Production maze physics/drawing has no
RAM-function annotation; a benchmark drawing to a RAM buffer does not
establish that its instructions execute in SRAM.

| Default control | Main-RAM allocated span | Unallocated region headroom |
| --- | ---: | ---: |
| `tilt_maze.elf` | 160,456 bytes | 363,832 bytes of 512 KiB |
| `usb_wifi_udp.elf` | 330,744 bytes | 189,448 bytes of 508 KiB |

These spans include address gaps, not just data+BSS. Wi-Fi's report linker
reserves a separate 4 KiB mailbox at `0x2007f000`; the remaining scratch banks
are not part of this headroom. The production
[flash linker](../../../src/arch/rp2350/memmap_flash.ld) budgets 4 MiB flash,
512 KiB main RAM and two 4 KiB scratch regions, with stack top `0x20082000`.
The 4 MiB budget is not a measurement of the badges' 16 MiB physical flash.
Test/smoke linkers with a 64 KiB stack are not production stack budgets.

The largest measured local frame plus saves is 1,568 bytes in IMU
`render_visual`. This does **not** bound nested calls, interrupts, indirect
calls or whole-program stack use. Unallocated main RAM does not make a stack
starting in 4 KiB SCRATCH_Y safe.

D02 warm-XIP/SRAM ALU chains agree conditionally. Its first LLVM cold-entry
increment is 1,163 ticks per long invocation; same-bank versus disjoint DMA
changes the literal kernel by 126,978 ticks but the ALU chain by two ticks.
These are whole-kernel contrasts, not universal load/miss prices or a
production-clock speed prediction. Radio RX-DMA and SDK multicore programs
exist elsewhere in the repository; this corpus does not certify their
contention behavior.

## Correctness, freestanding option and costs

[Startup](../../../src/arch/rp2350/start.c) copies `.data`, zeros BSS and sets
VTOR. C07 accepts explicit function sections; `.data.ram_code` could reuse
the existing `.data*` load-image/copy mechanism. This consumes RAM **and**
flash load-image bytes, startup time and potentially extra calls.

The owned linker has no veneers. A direct Thumb BL from flash `0x10000000`
to main SRAM `0x20000000` fails with
`Thumb branch relocation out of range/alignment (no veneers)`. The D08
MOVW/MOVT-address plus BLX probe links successfully. These are placement/link
probes, not executed, ABI-complete wrappers or proof of startup execution.
A bare section annotation is therefore not an end-to-end implementation.
Read-only data placement needs its own copy, pointer/relocation and lifetime
analysis; `.bss`/`.noinit` contracts must remain unchanged.

## S08-MEM boundary and acceptance

Select one reached function from measured profiles, retaining the XIP control.
Before a runnable candidate, resolve every cross-region call/address
relocation with ABI-safe selection or an explicitly scoped veneer design,
Thumb function-pointer bits, literals and interrupt/vector reach. Verify the
load image, copied bytes and executable addresses after reset. Set a
predeclared RAM allowance within actual region headroom and preserve mailbox,
BSS/noinit, stack and flash limits; do not spend unknown stack reserve.

Measure warm/cold XIP versus SRAM with fixed clocks, cache state, traffic,
seeds and role swaps, including copy/startup and call costs. Document physical
bank use and any active DMA/other core; do not assume different logical
addresses imply independent banks. Qualify actual corpus results, stack
uncertainties and recovery under D09. Only a reached improvement beyond the
D02 noise envelope without unexplained regressions can justify promotion.
No SRAM speed benefit or safe whole-program stack bound is claimed by D08.
