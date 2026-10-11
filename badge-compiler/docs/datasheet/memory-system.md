# RP2350 memory and fetch assumptions

Evidence: [fact ledger](fact-ledger.json), `RP2350-BUS-001`,
`RP2350-SRAM-001`, `RP2350-XIP-001`, `M33-FETCH-001`, `M33-ALIGN-001`,
`RP2350-ERRATA-E27`, `RP2350-ERRATA-E11`.
RP2350 sections 3.7.2.1.1, 3.7.4.9.12, 4.2 and 4.4.1.
Configuration confidence is high; predicting contention or cache-dependent
production runtime remains conditional. D02 calibrates specific controlled
kernels, not a universal memory-access cost.

## Core routing versus downstream banking

`UNCROSS_I_D` overrides the generic M33 address-based port selection:
instruction fetches use Code, loads/stores/debug use System. It removes the
described **internal** conflict; it does not create a second port in a physical
SRAM bank or eliminate the system crossbar.

SRAM has eight 64 KiB and two 4 KiB banks, 520 KiB total. The two main 256 KiB
regions stripe words across distinct four-bank groups. Different bus managers
can access different banks concurrently. Code and data in one logical address
space can therefore contend or proceed in parallel depending on exact mapping.

The core can fetch 32 bits per cycle. The nearby-literal example describes a
possible conflict between the roughly eight-byte-ahead fetch and a data access
in the same modulo-16 stripe position. Do not hard-code its observed extra
cycle as a universal literal cost.

## XIP is a different regime

The XIP cache is 16 KiB, two-way, with 8-byte lines and a documented one-cycle
hit. This is a subsystem hit characteristic, not one-cycle execution for every
instruction, nor a cache covering all SRAM/MMIO.

Cold/warm cache state, serial-flash/QMI configuration, instruction widths,
line crossings, both cores, DMA and data traffic can alter measured performance.
Cache permissions and maintenance depend on security/control state. E11
qualifies clean-by-set/way; a cold-cache experiment must not introduce unsafe
maintenance to obtain a superficially repeatable number.

## D02/D08 experiment requirements

Record final executable addresses/bytes and placement, silicon revision,
clock/QMI/cache configuration, execution/security state, IRQ/DMA/other-core
activity and counter overhead. Start with warm-XIP and SRAM comparisons, then
controlled contention and cold conditions where safe. Do not change bus
priorities blindly: E27 affects APB/FASTPERI controls on listed revisions.

SRAM placement needs a whole-image decision: data/BSS, stack and interrupt
space, startup copy/zero, relocation, bank mapping and flash footprint all
remain constraints. These notes do not authorize moving the whole program,
pinning cache lines or replacing MMIO access sequences.

## D08 decision

[The independent placement record](memory-placement.md) checks actual
control-image sections, main-RAM headroom, stack uncertainty and startup
copying. It opens only S08-MEM, a selected-function experiment: direct
flash-to-SRAM BL exceeds the owned linker's range and no veneers exist.
Address-pair indirect linking is feasible but is not a complete ABI/startup
implementation. No production function, bank map or bus priority changes.
