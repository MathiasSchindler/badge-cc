# PO11: instruction-scoped inline-assembly effects

**Status: implemented and host-qualified; explicitly selectable.**
The qualified `thumb-calls` default is unchanged. Select the candidate with
`--scalar-selection=thumb-effects`, or set
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-effects` for the owned build driver.
The candidate inherits the existing call transport, scalar homes and
compare/branch selector; it is not a different frontend or optimizer.

## Effect contract

[The classifier](../src/codegen_assembly.c) attaches facts to each assembly
instruction, rather than inferring preservation from a function-wide flag or
an empty constraint string. It records register read/write masks, flag reads
and writes, memory reads and writes, a compiler memory barrier, and possible
control effects. Classification and emission share the same decoded string.

The initial proof is intentionally narrow:

- A nonempty sequence consisting entirely of operand-free lowercase `nop`
  lines preserves all registers and flags. Horizontal whitespace, blank
  lines, CR/LF and LLVM hexadecimal string escapes are handled.
- Empty bodies, comments, labels, directives, other mnemonics, mixed
  sequences, `wfi`, `dmb`, `dsb` and `isb` remain unknown. Unknown assembly
  conservatively reads/writes all registers, flags and memory and may affect
  control flow. These instructions are still emitted; they are not nops.
- Supported clobbers are empty, `~{memory}`, `~{cc}`, and comma-separated
  combinations of those. A flag clobber remains a flag write even for a NOP.
  Register/output/input constraints and unsupported modifiers still fail
  explicitly. Malformed escapes and embedded NUL bytes also fail explicitly.
  Both owned C frontend policies lower `"memory"` and `"cc"` clobbers,
  including concatenated C strings, into those same backend constraints.
- A `"memory"` clobber is independent of register preservation. It remains
  a compiler memory barrier, not an implied architectural `dmb`. Neither
  volatile operations nor assembly are removed or reordered.

The existing IR effect analysis continues to treat inline assembly as opaque.
PO11 does not relax load reuse, CSE, loop motion or instruction scheduling
across assembly. Backend-specific register masks are separate from those
IR memory/SSA effects: the shared IR analysis does not model machine registers.

## Selection and residency

For global residency, known NOPs need no register spill homes or live-value
spill/reload pairs. Unknown assembly retains the existing precise live-value
protection. Local residency also keeps a preserved cached value, including
a value still held in `r0`; calls and unknown assembly retain their barriers.
Stack-only emission is supported without changing its residency contract.

In the candidate, a function containing assembly is no longer excluded from
otherwise eligible **adjacent** compare/branch fusion. The compare and branch
must still be in the same block, adjacent, with the existing single-use,
type and residency requirements. Assembly between them, including flag-changing
assembly, prevents fusion; the boolean remains materialized and protected.
No flags are reused across an intervening instruction.

Function-wide block-layout exclusions for volatile I/O, calls, assembly and
integer-to-pointer conversions are deliberately unchanged. Broader effect-safe
CFG and layout work belongs to PO13.

All older selectors retain their conservative assembly spill/fusion behavior.
Valid production baseline object bytes and loaded images replay exactly.

## Qualification and production results

[The persistent result](../build/po11-qualified-effects/results.json) covers
the same **37 C modules, 554 functions and twelve ordinary images** as PO10.
[The maintained capture/verifier](../tools/assembly-effects-evidence.mjs)
reuses PO10's frozen optimized IR, C06 frame parsing, and PO08's ELF decoder
and physical-region checks. It regenerates only backend objects and relinks
the recorded image inputs: no preprocessing/frontend/optimizer campaign or
hardware matrix is repeated. Input IR is identical between roles.

- All 37 control object hashes equal their archived production objects.
- All twelve control loaded binaries equal their archived production binaries.
- **Three static assembly spill/reload pairs are removed**: one in display
  `delay_cycles`, two in USB `arm_buffer`.
- **41 additional compare/branch pairs fuse**; nine modules shrink.
- `delay_cycles` and `arm_buffer` each drop from an eight-byte local frame
  to zero. No local frame, saved-register group or outgoing-call area grows.
- Eleven images shrink; one remains identical in size. Data and BSS are
  unchanged. Flash/RAM physical-region checks pass.

| Image | Control flash text/rodata | Candidate | Saved bytes |
| --- | ---: | ---: | ---: |
| button_mapper | 12,992 | 12,916 | 76 |
| diagnostics | 57,520 | 57,496 | 24 |
| gpio_events | 10,772 | 10,748 | 24 |
| gpio_monitor | 8,660 | 8,636 | 24 |
| hello_idle | 568 | 568 | 0 |
| imu_demo | 29,200 | 29,120 | 80 |
| lower_mapper | 13,224 | 13,148 | 76 |
| peripheral_probe | 16,248 | 16,172 | 76 |
| screen_test | 5,856 | 5,796 | 60 |
| tilt_breakout | 22,976 | 22,696 | 280 |
| tilt_maze | 34,804 | 34,780 | 24 |
| usb_wifi_udp | 366,808 | 366,584 | 224 |

These are static image measurements, not measured speed improvements.
The existing fixed callee-save groups and zero-byte SP adjustments remain;
removing those is PO12, not part of this change.

The sanitizer-backed mechanism tests cover multiline known/unknown assembly,
memory/flag clobbers, architectural barriers, unsupported forms, resource limits
and preserved output on errors. Cortex-M33 execution covers all five residency
modes with loop-carried values, ordinary calls, volatile stores/loads, unknown
assembly that actually overwrites allocated registers, and a boolean kept live
across flag-changing assembly. A separate optimizer test verifies ordinary
loads cannot be reused across a NOP memory barrier. Both assemblers accept
the proven forms. Report tests reject policy, artifact, spill, frame, byte
accounting and inventory tampering.

## Timing audit and promotion gate

The affected production instructions are hardware-timing sensitive:

- [Display delays and LCD strobes](../../src/platform/badger/display.c) use
  counted NOP loops and literal NOP sequences.
- [USB buffer ownership settling](../../src/usb/live_usb_hw.c) uses twelve
  NOPs plus a compiler memory barrier between two volatile writes.
- The compact [red-square reproduction](../../src/tools/red_square.c) has
  the same delay/strobe patterns.

NOP counts, ordering and memory barriers are preserved. Nevertheless, removing
stack traffic or shortening loop branches reduces elapsed time; C correctness
and QEMU do not certify reset/sleep-out delays, LCD pulse widths or USB controller
settling on the badge.

The mitigation is an **explicit candidate gate**, not extra hidden padding or
a demo-specific exemption. Before promotion, independently bound the required
delays and strobes at the supported clock, use timer-based waits or explicit
minimum instruction delays where necessary, and qualify only affected hardware
paths with the PO10 baseline/candidate contract. No badges are touched by PO11's
host qualification. There is no new default or physical-timing claim.

## Reproduction

From the project root, with the existing LLVM/QEMU tools on `PATH`:

```sh
make -C badge-compiler test-assembly-effects
node badge-compiler/tools/assembly-effects-evidence.mjs \
  --capture badge-compiler/build/po11-new-capture \
  badge-compiler/build/po10-qualified-small-functions-v2/results.json
node badge-compiler/tools/assembly-effects-evidence.mjs \
  --verify-current badge-compiler/build/po11-new-capture/results.json
PO11_REPORT=badge-compiler/build/po11-new-capture/results.json \
  node --test badge-compiler/tests/assembly-effects-evidence.test.mjs
```

Capture refuses existing directories, archives tools/inputs, and preserves
failed captures. Historical `--verify` recomputes persisted accounting;
`--verify-current` also checks current source/tool identities.
