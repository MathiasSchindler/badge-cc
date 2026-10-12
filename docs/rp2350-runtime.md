# RP2350 runtime and hardware qualification

This guide records runtime findings on the 2026 badge as of 2026-10-11.
[The hardware specification](spec.md) owns board wiring and component facts;
this page owns clock, memory, diagnostic and multicore qualification boundaries.
No documentation page authorizes flashing a badge.

## Evidence levels

Keep three categories separate:

- **Documented:** the pinned RP2350 datasheet establishes chip behavior, not
  successful execution of our code.
- **Physically qualified:** an identified image and selected badge returned
  fresh validated evidence.
- **Open:** implementation or host/emulator tests exist, but the physical
  result is missing or failed.

The reference is RP-008373-DS-3, build `80d627281ed5`, 2026-10-01, recorded in
the [source inventory](../badge-compiler/docs/datasheet/rp2350-source.json).
Page references below are physical 1-based PDF pages, not printed labels.

## Tested devices and memory

Both tested badges have chip/USB serials `9BBF88DD17D66E7D` and
`08DA26F86E57F5EA`. Saved picotool reports identify the tested hardware as
RP2350 A4, QFN80, Arm, secure boot disabled; the selected BA03/BA05 badge
reports ROM git revision `0xa8bfe860`.
Do not derive the A3/A4 distinction from the CHIP_ID revision field alone.

Picotool reports 16 MiB flash. Full 16 MiB backup, restoration and independent
readback/hash verification have passed. This is operational evidence for the
tested range, not JEDEC identification, alias testing, endurance testing or
full electrical memory qualification. PSRAM initialization/testing remains
separate and unqualified by these compiler campaigns.

The ordinary linker uses 512 KiB main SRAM and two 4 KiB scratch banks.
Together these are 520 KiB of physical SRAM. A diagnostic linker can reserve
a mailbox inside that space; it cannot protect RAM from bootrom clearing.

## BOOTSEL and report persistence

The relevant Non-secure BOOTSEL entry clears SRAM0-SRAM9, XIP cache and USB
RAM (section 5.2.2, PDF pages 340-341). The failed
[SRAM-report experiment](usb-report.md) agrees with this documented behavior.
Changing the mailbox address or adding `NOLOAD` does not preserve it.
This is not a claim that every reset mode clears all RAM.

Reboot flag `0x100` means `NO_RETURN_ON_SUCCESS`, not warm reboot or RAM
retention (section 5.4.8.24, PDF page 364).
The [live USB report interface](usb-live.md) avoids retention by reading
results while the application runs.

BA03 also physically qualified a different transport: a nonce-bound result
is persisted in the final 4 KiB flash sector before BOOTSEL. The writer
executes from SRAM and uses checked ROM APIs. The complete original flash,
including that sector, is restored and independently verified afterward.

The sector at `0x10fff000` is **not free storage for an arbitrary installed
application**. This technique requires explicit permission, the selected
serial, a fresh evidence directory, a complete hash-checked backup and
verified restoration. Flash snapshots may contain private data and remain
local/ignored. Neither an empty report nor a pending checkpoint is success.

## Clock and timing contract

The board has a nominal 12 MHz crystal, used by the qualified USB clock
configuration. Do not assume XOSC is enabled when a minimal application
starts. The first BA05 probe observed it disabled and stopped with a clock
setup error; that does not establish a missing or defective crystal.

Earlier instruction calibration captured inherited `CLK_SYS_CTRL = 0x41`
and divisor one: the system clock selected the ROSC auxiliary source,
not an assumed production 150 MHz PLL configuration. Other firmware can
configure different clocks. Every hardware result must identify its own
selectors, divisors and enabled/stable sources.

`CLK_SYS_CTRL.SRC` is glitchless; `AUXSRC` is not. Preserve the active
auxiliary selection when requesting a switch to `clk_ref`, wait for
`CLK_SYS_SELECTED`, and only then change an inactive auxiliary source.
Writing the whole control register to zero can select an unconfigured PLL
while the old auxiliary path is still active. The original BA05 sequence
contained this unsafe write; the manual establishes the bug, but the
historical pending report alone does not locate the old hang.
See section 8.2 clock controls, PDF pages 489-491.

Raw timer ticks are not automatically microseconds. Earlier instruction
measurements explicitly selected TIMER0's `clk_sys` source; radio latency
measurements used their own USB-SOF calibration. Clock configuration is not
an external frequency measurement. SRAM placement, XIP working state and
DMA routing also affect measured sequences; there is no universal memory
access cost or blanket transfer of a benchmark result to a production app.

## System instructions and synchronization

BA03 physically qualified the Secure core-0 register/interrupt probe and
SVC entry/return, with expected exception number 11 and restored register
state. It does not qualify Non-secure transitions or core 1.
Interrupt masks are per-core: disabling interrupts on core 0 does not lock
core 1. SVC and faults need valid vectors and appropriately sized stacks.

Sections 2.1.6-2.1.6.2 (PDF pages 26-28) document global exclusive support
for complete accesses within `0x20000000 <= address < 0x20082000`,
including both scratch banks. The reservation granule is 16 bytes.
Both cores and DMA writes participate; isolate unrelated lock/handshake
words where false conflicts matter.

The CPU local monitor and RP2350 global monitor are distinct. CLREX clears
local state, not the global reservation. Exclusive pairs need matching
address, size, Security and privilege state, and naturally aligned accesses.
Retry budgets and explicit failure paths are required.

With the MPU disabled, each participating Cortex-M33 needs
`ACTLR.EXTEXCLALL` (bit 29 at `0xe000e008`) to allow external exclusives.
Its documented reset value is zero (PDF page 165). Single-core exclusive
success alone does not establish participation in the RP2350 global monitor.
The first isolated ladder passed launch/park and the single-core probe, but
failed cross-core invalidation: STREX returned success and overwrote the
competing value. Both snapshots had the MPU disabled.
The diagnostic now checks privileged MPU-disabled startup, enables the bit
on each core with barriers/readback, preserves unrelated bits and restores
core 0's original ACTLR before final publication, including fault paths.
This is diagnostic initialization, not a speculative production-startup change.
The corrected ladder physically recorded ACTLR `0 -> 0x20000000` on both
cores, core-0 restoration to zero, failed STREX status **1**, and the competing
payload still **`0x12345678`**. This qualifies the isolated invalidation
correction, not a full contended lock.

**Never probe exclusive support by using LDREX/STREX on arbitrary MMIO,
ROM, XIP/cache SRAM or unsupported aliases.** Outside supported SRAM,
exclusive writes can report failure yet still write. Peripheral registers
need their own documented protocols.

LDA/LDAEX and STL/STLEX provide acquire/release ordering. Plain LDREX/STREX
do not replace the required barriers. DMB orders memory accesses; DSB
waits for completion where required; ISB synchronizes execution-context
changes. SEV/WFE notify a waiter but do not transfer ownership or replace
publication ordering.

SIO FIFO is an ordered message mechanism, including the bootrom core-1
launch handshake (section 5.3, PDF pages 346-347). SIO spinlocks have
read-to-claim/write-to-release semantics and separate Security banks.
The A2 spinlock erratum RP2350-E2 matters when supporting that revision;
it is not an established explanation for the selected A4 badge's BA05
failure. Hardware spinlocks, FIFO and events still require bounded,
interrupt-aware ownership and recovery protocols.

## BA05 qualification

Assembler encodings, LLVM comparisons, Cortex-M33 emulator execution,
bounded lock timeout/recovery and host restoration checks pass.
The isolated startup ladder and full bounded two-core protocol now also
have passing physical evidence on the selected A4 badge. Host/emulator
results alone would not establish that behavior.

The initial BA05 clock-error report and two later pending reports are
retained as failed evidence. Both later attempts needed manual BOOTSEL
recovery. All original flash was restored and readback-verified.
The only durable checkpoint in those attempts was committed before the
probe, so it cannot distinguish clock switching, core launch, contention
or final publication failure.

The following isolated ladder was qualified before full contention:

1. Known boot/transport control.
2. Core-1 FIFO launch, acknowledgement, SRAM park/reset and flash
   publication without changing inherited clocks.
3. Single-core exclusive success, CLREX invalidation and bounded lock
   failure/recovery in supported SRAM.
4. One deliberate cross-core reservation invalidation, without contention.
5. XOSC enable alone, then a safe glitchless clock switch as separate tests.
6. Scheduled-watchdog-only BOOTSEL recovery, both with inherited clocks and
   after the qualified switch; no explicit immediate-reboot fallback.

### Isolated ladder results

The authorized `ba05-startup-extexclall` campaign passed the boot control
and all seven stages on A4 badge `9BBF88DD17D66E7D`:

| Stage | Physical result |
| --- | --- |
| 1: inherited-clock core launch | FIFO acknowledgement, SRAM park/reset and final publication passed |
| 2: single-core exclusives | All widths, CLREX and bounded lock timeout/recovery passed |
| 3: cross-core invalidation | Competing ordinary write defeated STREX without changing its payload |
| 4: XOSC enable only | Stable XOSC; inherited clock selectors/divisors unchanged |
| 5: safe clock switch | System switched to reference, then reference to XOSC; divisors one |
| 6: inherited-clock watchdog | Scheduled watchdog alone returned to BOOTSEL |
| 7: switched-clock watchdog | Scheduled watchdog alone returned to BOOTSEL after the safe switch |

Inherited system control was `0x41`, divisor `0x10000`, selected `2`;
reference control was `0`, divisor `0x40000` (**four**, not one), selected `1`.
The switched snapshots recorded system control `0x40`, selected `1`;
reference control `2`, selected `4`, and both divisors `0x10000`.
These verify mux/register behavior, not an external frequency calibration.

The watchdog-only stages scheduled 250 ms and then entered an assembly WFE
loop with no immediate reboot call. Host recovery was observed after
1,583/1,576 ms, including polling and USB enumeration; this is **not** a
measurement of the watchdog timebase.
All stages returned fault-free, with restored core-0 ACTLR and masks.
Every image matched the LLVM-assembled image byte-for-byte.

The complete original 16 MiB flash was restored and independently verified
against SHA-256
`91f0dfce98cb2bea2fa0bbb11a27ac04f59dce17a3b4b960517d0fef157a14cd`.
The selected badge was left in BOOTSEL; `08DA26F86E57F5EA` was untouched.
Private backups and source/image/nonce-bound reports remain local in
`badge-compiler/build/ba05-startup-extexclall/`; the failed first ladder
remains separately retained in `badge-compiler/build/ba05-startup-ladder/`.

Bind each stage to its image, source hashes, fresh nonce, state and
restoration evidence. Commit a durable stage checkpoint while core 1 is
stopped. Stop the ladder on the first failed stage; manual recovery is a
fallback, not a passing watchdog test. Physical contention, ordering,
progress and both-core configuration are the BA05 exit criterion.
The ladder satisfies the startup/recovery prerequisite; full contention
was qualified separately below, not inferred from these isolated stages.

### Full bounded two-core protocol results

The separately authorized `ba05-contention-qualified` campaign passed the
boot control and complete synchronization diagnostic on A4 badge
`9BBF88DD17D66E7D`. **BA05's physical exit criterion is satisfied.**

| Check | Validated physical result |
| --- | --- |
| Progress | Each core completed exactly 1,024 protected updates |
| Shared counter | Exactly 2,048 updates, with none lost or duplicated |
| Payload ordering/integrity | Checksum invariant checked inside every critical section; final checksum 2,098,176 |
| Bounded failure paths | Core 0 and core 1 timed out on the deliberately held lock before release and subsequent successful work |
| Cross-core invalidation | STREX status 1; competing `0x12345678` remained intact |
| Configuration | Both cores privileged, interrupt masks clear, MPU disabled, ACTLR external exclusives enabled |
| Clocks | Shared system/reference XOSC selection, divisors one, XOSC stable |
| Completion/recovery | No per-core errors or faults; core 1 parked in SRAM and held in reset before publication; automatic BOOTSEL return |
| Core-0 restoration | Original ACTLR readback checked before final publication; mask/control state unchanged |

The complete LLVM-assembled and owned-assembled diagnostic images matched
SHA-256 `90d00553fefea70182ee157736979a3fa0605c0aab0367cfd2fc7162f4bce812`.
The owned-assembled image was executed; running the byte-identical LLVM
image separately would not provide a distinct firmware test.
The nominal selected clock is 12 MHz, not an externally measured frequency.

The full original 16 MiB flash was restored and independently readback-verified
against the original hash recorded above. The selected badge was left in
BOOTSEL; the other badge was untouched. Source/tool/image hashes, fresh
nonces, raw reports and restoration evidence remain local in
`badge-compiler/build/ba05-contention-qualified/results.json`.
This campaign is functional evidence for this bounded diagnostic, not
fairness, throughput, arbitrary fault recovery or universal concurrency proof.

Production C atomic lowering, language-level memory ordering, interrupt
locking, DMA ownership, cache/MPU placement, fairness and performance are
separate prerequisites or qualification projects, outside the completed
BA05 scope. PSRAM, flash endurance/alias testing, external clock calibration
and Non-secure transitions are likewise not qualified by this result.

## Evidence navigation

- [BA03 contract and successful physical evidence](../badge-compiler/docs/badge-assembler.md#ba03--system-registers-exceptions-and-interrupt-control).
- [BA05 contracts, complete qualification and retained attempts](../badge-compiler/docs/badge-assembler.md#ba05--multicore-synchronization-instruction-coverage).
- [Instruction calibration and memory/timing limits](../badge-compiler/docs/datasheet/hardware-calibration.md).

Historical records describe the source/tool revisions actually executed.
Documentation updates do not refresh their hashes or authorize another test.
