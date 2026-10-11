# D08: GPIO coprocessor decision

**GO for a bounded explicit display-helper state/waveform pilot
(S08-GPIO). NO-GO for automatic rewriting of arbitrary volatile MMIO.**
The existing helper remains the default/control; D08 enables no CP0 code.

## Applicability and actual beneficiary

GPIOC provides specific SIO GPIO operations through port 0, not generic
peripheral acceleration. RP2350 section 3.6.1, tables 109-111,
PDF pages 97-98 / printed 96-97, and
[the ledger](fact-ledger.json) (`RP2350-GPIOC-001`) specify low/high masks.
For example, MCR port 0 opcode 2 sets and opcode 3 clears the selected mask;
the low group selects c0 and the high group c1. Access requires CP0
permissions, plus ACCESSCTRL GPIO_NSMASK0/1 for Non-secure GPIO use.

The concrete beneficiary is the existing freestanding
[display pixel helper](../../../src/arch/rp2350/display_pixels.S), used by
[display code](../../../src/platform/badger/display.c). LCD data pins 32-39
use a high-group `0xff` mask; WR is pin 30. CS/DC/RD include other low-group
pins that must not be clobbered.

Each pixel is read as 16 bits, high byte first then low byte. Each byte
clears/sets high data bits, drives WR low/high, and retains three NOPs before
WR high and two afterward: eight SIO stores per pixel. The assembly already
materializes the SIO address once and uses narrow STR instructions. There is
no evidence that replacing each store with a 32-bit MCR shrinks code or
improves complete frame delivery.

At the eight store sites, a literal one-for-one replacement costs 32
instruction bytes for eight MCRs versus 16 bytes for eight narrow STRs:
16 bytes of body growth before any setup/control changes. Saving an address
register or avoiding SIO bus accesses may offset some costs, but permission
setup, register saves and preserved panel delays must be included. This is
an encoding bound for that replacement, not a measured whole-helper result.

[D08 evidence](../../build/datasheet/d08-results.json) confirms that the owned
assembler rejects MCR while preserving an existing output file. This does
not establish a working GPIOC helper. D02's historical CPACR/NSACR values
(`0x0000c000`/`0x80`) enable port 7 permissions, not CP0. GPIO_NSMASK0/1 and
CPACR_NS were not measured. Current firmware's CP0 usability remains open.

## Correctness and freestanding option

Implement, if qualified, one explicit original assembly helper with a build
selection and retained old implementation. Extend only the required legal
encodings, with validation and output-preservation tests. No SDK, standard
library or generic volatile optimization is needed. Unsupported access
must be diagnosed or deliberately select the known old helper before use;
do not catch a coprocessor fault and pretend the operation succeeded.

Preserve exact low/high masks, high-byte/low-byte order, clear-then-set
transient data behavior, WR strobes, zero-count behavior, pointer advancement,
ABI saves and access ordering. Whole-register PUT can overwrite unrelated
pins and is out of scope. Inspect interrupt/shared pin writers and ownership.
MMIO volatility alone does not justify merging writes or removing delays.

Faster data/WR transitions can violate panel setup/hold or pulse-width
requirements. Instruction counts and unchanged NOP counts do not establish
unchanged **electrical** timing: bus stalls and coprocessor paths differ.
Scope or logic-analyzer measurements at the actual panel pins are a gate.

## S08-GPIO boundary and acceptance

First capture live security state, CP0 permissions and GPIO masks without
changing unrelated access policy. Define panel timing minima from the actual
panel specification and predeclare clock/traffic conditions. If those cannot
be established, stop before implementing/promoting a runnable helper.

Then compare old/new traces and displayed output for zero/one/multiple pixel
counts, all byte patterns, windows, interrupt activity and shared-pin state.
Retain old default/fallback and test assembler legality/LLVM bytes, ABI,
build selection and whole-loop code size. Measure complete transfers/frame
time, not one MCR. Include relevant display/graphics corpus builds and D09
hardware recovery/noise gates. No waveform validation, timing gain or panel
safety claim is made by this decision.
