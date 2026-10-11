# D02 hardware calibration

D02 measures explicit instruction sequences, not compiler-recognizable patterns.
It adds no compiler cost model and changes no production compiler defaults.
The [D01 ledger](fact-ledger.json) remains documentary evidence; the separate
calibration results link back to its fact IDs.

## Reproduction

Run from the workspace root with Node, the existing LLVM/owned compiler tools,
QEMU, the local `bin2uf2` converter and USB-enabled `picotool`. No manual/PDF
extraction, network acquisition or new target library is required.

```sh
make -C badge-compiler test-datasheet-experiments
node badge-compiler/tools/datasheet-experiments.mjs build build/d02-new-images
node badge-compiler/tools/capture-datasheet.mjs \
  build/d02-new-images build/d02-new-capture SERIAL_A SERIAL_B
node badge-compiler/tools/analyze-datasheet.mjs \
  build/d02-new-capture/capture.json > build/d02-new-results.json
```

Directories must be new. Hardware capture requires two distinct, explicitly
selected RP2350 ARM badges in BOOTSEL, unsecured boot and **16 MiB flash**.
The test target is portable and does not access badges or require ignored
hardware evidence. Image generation performs independent LLVM and owned
QEMU semantic checks and final-linked byte/operand/width/target/literal audits.
Both harnesses link the same assembly object. QEMU provides no timing evidence.

## Transport and safety

Each badge's complete flash is snapshotted and hashed before any write.
The campaign temporarily replaces firmware and reserves flash addresses
`0x10fe0000` for the request and `0x10ff0000` for results. These are **not**
safe storage addresses for an arbitrary existing application: capture is
permitted only with the full snapshot/restoration procedure.

The initial SRAM-only transport failed: reports read back as all zeroes.
RP2350 boot flow, physical PDF page 340 / printed page 339, explicitly erases
SRAM0–SRAM9, XIP cache and USB RAM before BOOTSEL. Both original flash images
were restored and independently reread/hash-verified after that failure.
Moving an SRAM buffer is therefore not a fix.

Requests now survive in flash. Results are built in reserved SRAM
`0x20078100`, CRC-committed with a fresh request nonce, then persisted to flash
**after all measurements** using the ROM flash APIs. Erase/program/verification
are never timed. The runtime checks ROM function pointers before entering
command mode and verifies the programmed bytes before rebooting. Persistence
failure stops execution rather than claiming success; capture times out and
requires physical Boot recovery if the badge cannot return to BOOTSEL.

Capture retains every raw report, command/recovery log, role assignment,
request nonce and image hash. Its `finally` path restores every touched badge,
rereads complete flash and compares SHA-256. A capture or restoration failure
cannot produce a successful campaign. Restored devices remain in BOOTSEL.
Full flash snapshots may contain private data: keep them local and ignored;
do not publish them with results.

## Experiment matrix

There are 39 kernels, five modes, seven raw samples and 4,096 loop iterations
per invocation. Bodies generally repeat 32 operations; four independent
streams use eight groups and alternate MAC destinations use sixteen pairs.
The generated registry records exact bodies, loop widths/alignment, operands,
expected results and D01 fact IDs.

The five modes are warm SRAM, warm XIP, cold-entry XIP, SRAM with same-bank DMA,
and SRAM with disjoint-bank DMA. Cold-entry invalidates only the target
function/literal cache lines before one long invocation. Its internal loop
quickly becomes warm: this does **not** measure permanently cold execution,
nor isolate an individual miss latency.
Maintenance uses byte stores: address bits 2:0 select the operation (RP2350
physical page 316 / printed 315), as in the local SDK's cache-maintenance
implementation. An earlier complete campaign used an unaligned word store,
which can split into additional bus operations. That campaign is retained
but not qualified for cold-entry evidence; qualification reruns the full
matrix with the byte-store correction.

Coverage includes ALU dependency and complex shifts, flag-setting forms,
multiply dependency, MAC operand positions, long MAC accumulators,
representative unsigned/signed divisions, load consumers, first/last LDM
consumers, store data versus address dependencies, literals versus MOVW/MOVT,
narrow/wide forms, branch-target alignment, conditional direction/folding
and modern versus legacy NOP encodings.

Main C text/data and SRAM kernels reside below `0x2003e000`. Input/guard/sink
data occupy the low striped group at `0x2003e000`. DMA reads a fixed word in
that group, or `0x20040000` in the separate high group, and writes
`0x20040004` in a different physical bank. Same-bank interference can affect
instruction fetch **and** data access; it is not a pure isolated load penalty.
All 16 DMA channels must initially be idle. Busy state, error bits and bounded
abort completion are checked; the original DMA reset state is restored.
An initial persistent-mailbox run correctly reported device failure `0x10752`
at DMA startup. ENDLESS mode still requires a nonzero count (RP2350 physical
page 1014 / printed 1013); count zero stops immediately. The corrected value
is `0xf0000001`, not `0xf0000000`. Both badges were fully restored after that
failed run, and failed samples are not substituted into the final campaign.

## Timing and correctness

TIMER0 SOURCE is explicitly switched to `clk_sys`. TIMERAWL at offset `0x28`
provides one-system-clock ticks, not microseconds. The original source is
restored before reboot; the timer's absolute count is not restored.
Intervals use unsigned subtraction, reject zero/wrap-ambiguous durations,
and must exceed 100 times the largest measured timer-read overhead.

The runtime masks interrupts with PRIMASK for the complete experiment campaign;
it does not launch core 1, service live USB or draw a display inside measurements.
DSB/ISB barriers bracket volatile timer reads. Clock selectors/divisors and
PLL configuration are captured before/after and configuration drift fails the
campaign. This does not externally measure oscillator frequency or temperature.
Raw metadata also captures chip/core ID, CONTROL, CPACR/NSACR, original mask,
timer pause/debug configuration, XIP/QMI configuration, DMA state, selected
clock and DWT control. DWT CYCCNT is not assumed available.
The final metadata audit uses TIMER0 PAUSE at offset `0x30` (not reserved
offset `0x34`), TIMERAWH at `0x24`, and XIP_CTRL at `0x400c8000` (not HSTX_CTRL
at `0x400c0000`). Earlier campaigns with mislabelled metadata are retained as
superseded evidence, not the qualified state record.

Every invocation returns an independently expected value; every stream is
consumed. Memory guards and store sinks are checked outside the interval.
Warmup is untimed; cold invalidation and DMA setup/stop are outside the interval.
The actual seven timer-read overhead samples and empty-loop controls are
retained. No fitted overhead constant is introduced.

## Campaign and interpretation

Six paired rounds provide two LLVM/LLVM control rounds, two LLVM/owned
role-swapped comparisons, then their role-swapped repeat. Physical execution
order reverses each round. Twelve reports retain **16,380 raw timing samples**.

Analysis first checks role swaps, fresh nonces, fixed images, complete rows,
raw min/median/max, source/image/audit hashes and verified restoration.
It re-decodes raw CRC/nonce reports and verifies snapshot hashes and D01
production-preservation hashes rather than trusting stored summaries.

The conservative noise envelope includes raw sample spread, repeated
same-badge control median drift and paired control/control board differences.
Pairwise sequence comparisons use the sum of both envelopes. Predictions are
classified as supporting, contradicting the **sequence prediction**, or
unresolved within noise; mixed runs stay visible. A contradiction is not an
automatic correction to the datasheet or a universal opcode cost.
Narrow branch targets do not incur the documented wide-target halfword
alignment penalty; their prediction is equality, not a hidden positive cost.

Division estimates explicitly subtract the matched empty-loop median and
divide by the measured operation count. Placement/cache/DMA differences and
their noise are retained separately. LLVM/owned endpoints can differ even
though measured body bytes match. D03 must use bounded mechanisms and verified
conditions, not blindly promote every sequence delta into a scalar cost.

## Qualified result

The final [machine-readable results](../../build/datasheet/d02-results.json)
qualify the [state-corrected campaign](../../../build/d02-calibration-state-capture/capture.json)
and [image manifest](../../../build/d02-calibration-state-images/manifest.json).
All six paired rounds completed; both complete original flash images were
restored and reread/hash-verified. Both badges remain in BOOTSEL.

- **49 portable tests passed**, no failures or skips.
- Both LLVM and owned QEMU semantic oracles passed.
- 156 linked kernel placements were audited; measured body bytes are identical
  across both compilers and both SRAM/XIP placements.
- **16,380 raw timing samples** were retained and re-decoded.
- Fifteen D01 preservation hashes passed, including compiler sources,
  production/sanitized tools, extraction tooling and the qualified maze UF2.
- No production compiler code, optimization pipeline or default was promoted.

The observed warm-SRAM control noise envelope is zero ticks in this campaign,
not a promise of zero noise in every configuration. Eleven sequence
predictions support their documented direction and five support equality
within the measured envelope. One remains **mixed/unresolved**: narrow versus
wide immediate ADD agrees exactly under LLVM, while owned endpoints differ
by one tick per complete 4,096-iteration invocation. The measured bodies are
audited, but endpoint/layout effects are included. Do not interpret that tiny
sequence discrepancy as a new per-instruction rule or invent a correction.

Unsigned division by three reproduces the documented incremental estimates:

| Dividend | Measured cycles per divide after matched empty-loop subtraction |
| --- | ---: |
| 1 | 2 |
| 3 | 4 |
| 255 | 5 |
| 65,535 | 7 |
| 4,294,967,295 | 11 |

Multiply/load dependencies, MAC operand position, complex flag-setting shifts,
last-register LDM use, store address dependencies, branch direction/folding,
wide-target alignment and NOP encoding all produce the predicted sequence
directions. Literal materialization and memory placement remain measured
tradeoffs rather than universal scalar costs.

For example, the first LLVM report records a 1,163-tick cold-entry increment
for the long ALU-chain invocation, while warm XIP and SRAM agree for that
kernel. The literal kernel has a 126,978-tick same-bank/disjoint-DMA difference,
whereas ALU-chain differs by only two ticks. This demonstrates why memory
costs need access-pattern and routing conditions; one DMA surcharge is wrong.
These examples are full-invocation observations, not isolated miss latencies.

Both badges report CHIP_ID `0x30004927` and Cortex-M33 CPUID `0x411fd210`.
Revision field three does not distinguish A3 from A4 by itself. The captured
system clock uses the boot configuration (CTRL `0x41`, divisor 1, ROSC source),
not an assumed production 150 MHz configuration. TIMER0 SOURCE is one,
PAUSE zero, XIP_CTRL `0x83`, and LLVM timer-read overhead is 23 ticks in every
sample. Full owned overhead, state tuples and all per-mode differences remain
in the results. Oscillator frequency was not externally measured.

D03 can now rank general scheduling/selection opportunities using this
bounded evidence. Production-clock workload qualification and promotion
remain separate later milestones.
