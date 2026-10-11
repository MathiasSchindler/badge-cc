# PO14: bounded address reuse and direct operand transport

**Implemented and host-qualified as the explicit `thumb-operands` candidate.**
It inherits PO13's `thumb-control`. The owned default remains `thumb-calls`;
the firmware build remains LLVM by default. Physical delay/strobe/USB timing
still needs qualification before promoting the inherited candidate chain.

## Selection rules

### Direct scalar memory and arithmetic

Naturally aligned one-, two- and four-byte accesses can use allocated pointer
and value/result registers directly instead of transporting them through
`r4` and `r0`. Every original access is emitted separately, including volatile
accesses: the address, width, value and order are unchanged. There is no memory
value cache, load reuse or MMIO transaction combining.

Byte/halfword loads already zero-extend their results. Their saves omit redundant
normalization; narrower nonstandard integer widths still normalize. An allocated
narrow value is canonical because incoming arguments, arithmetic, PHIs and saves
establish its declared width. A zero extension to a 32-bit result therefore needs
only the necessary register copy/save, not another mask.

Existing direct arithmetic selection now includes unflagged `i8`/`i16`
add/subtract/multiply/bitwise operations and unsigned shifts. Results still
normalize to their declared widths. Signed shifts/division and unsupported
shapes retain the existing lowering.

Eligible single-word outgoing stack arguments use their allocated register
directly in `str [sp, #offset]`. Signed-extension, by-value, multiword and
out-of-range cases retain the existing transport. Register arguments retain
the existing AAPCS path.

Existing allocator scratch exclusions are deliberately unchanged. No additional
register is reserved, no interval is lengthened and no source register is
overwritten merely to save a copy. Unaligned and wide accesses use the existing
byte/word decomposition; unsupported unaligned volatile accesses still fail.

### Constants and related addresses

Symbol addresses with addends in the existing supported relocation range
(-32,768 through 32,767) use the owned assembler's `movw`/`movt` relocations.
This avoids the current inline literal's load, branch, data word and possible
alignment padding. Other symbol expressions retain the literal fallback.
This reuses the prior final-layout selector's relocation implementation without
enabling its unrelated branch policy.

A bounded final-body pass tracks constants available in the **existing physical
registers**. It recognizes complete numeric or matching symbol `movw`/`movt`
materializations, register copies, and a conservative generated-instruction
write/preservation whitelist. For a new materialization it can:

- omit an identical value already in the destination;
- copy an identical value from another register;
- form the same numeric/symbol base with an offset of at most 4,095 using
  nonflagging `addw`/`subw`.

Only strictly smaller encodings are selected. Register copies use two bytes;
offset costs come from the shared target-cost query with flag-changing forms
disabled. High-register operations are counted as wide where required.
Flags are not repurposed or clobbered by the replacements.

Stores and comparisons preserve constant facts; loads and modeled arithmetic
invalidate their destination. Calls, labels/joins, IT, directives and unknown
instructions end availability. Known NOPs, including memory-barrier NOPs, may
preserve pure address/constant facts, but never authorize memory value reuse.
Functions containing opaque assembly skip the entire constant-reuse pass.
The whitelist models compiler-generated code, not arbitrary Thumb assembly.

Calls rematerialize constants as needed afterward. Keeping a fresh reserved
callee-saved address register across calls was rejected for this slice: its
extra save/live-range cost is not established as profitable.

Address induction was considered but is **not** performed here. Rewriting loop
PHIs, handling zero-trip paths and extending lifetimes belong to PO15's
pressure-aware induction qualification. The strided-array test verifies that
this slice preserves skipped elements and access order without changing the loop.

## Evidence and results

[The qualified report](../build/po14-qualified-operands/results.json) uses the
same archived optimized IR across **37 modules, 554 functions and twelve
ordinary images**, with verified PO13 artifacts as control.

- **3,942 direct scalar memory selections.** This is a selection count, not a
  claim that every selection independently saves an instruction.
- **483 reused constant/address materializations**, saving **2,118 bytes**
  in those sequences alone.
- Every module's executable sections shrink. No per-module frame/save/allocation
  spill/assembly spill growth; data/BSS and physical-region budgets remain intact.
- All twelve images shrink by **52-11,144 bytes versus PO13**.

| Image | PO13 text/rodata bytes | PO14 bytes | Saved |
| --- | ---: | ---: | ---: |
| button_mapper | 11,416 | 10,580 | 836 |
| diagnostics | 52,380 | 47,160 | 5,220 |
| gpio_events | 9,516 | 8,708 | 808 |
| gpio_monitor | 7,440 | 6,904 | 536 |
| hello_idle | 488 | 436 | 52 |
| imu_demo | 26,880 | 25,000 | 1,880 |
| lower_mapper | 11,652 | 10,816 | 836 |
| peripheral_probe | 14,348 | 12,864 | 1,484 |
| screen_test | 5,040 | 4,768 | 272 |
| tilt_breakout | 20,292 | 17,916 | 2,376 |
| tilt_maze | 31,604 | 27,468 | 4,136 |
| usb_wifi_udp | 352,860 | 341,716 | 11,144 |

The benefit is not confined to reproductions: display code shrinks 156 executable
bytes, I2C bitbang 638, IMU driver 40, USB report code 182, Wi-Fi GSPI 286,
UDP parsing/control code 936 and UDP report code 688.

Ten targeted PO14 tests pass, using sanitizer-backed code generation,
Cortex-M33 execution and final ELF encodings. Coverage includes:

- arbitrary RAM addresses with interleaved volatile byte/halfword/word accesses,
  exact emitted access-width/order lists and reached value checks;
- symbol offsets and numeric high-bit constants, including offset-limit crossings,
  plus identical loaded addresses/bytes under owned and LLVM assemblers;
- signed indices, RAM/flash arrays, skipped elements, unaligned and wide fallbacks;
- all eight selected narrow arithmetic operations at both width boundaries,
  negative literals and source values live after extension;
- calls, NOP memory barriers, opaque assembly, conditional selection and outgoing
  argument transport; three mixed/all-owned LLVM/owned AAPCS executions;
- analysis limits, unchanged defaults and persistent report tampering.

Retained PO12/PO13 runtime, driver and frame-inspector gates pass. The frame
inspector now proves outgoing sizes from copied numeric constants and nonflagging
offsets, while clobbers, joins and uncertain instructions retain unknown bounds.
Historical PO12/PO13 static reports still verify.

These are static size and reached correctness results, **not speed measurements
or physical timing qualification**. No badge is flashed.

## Delta-focused reproduction

```sh
BADGE_CODEGEN_SCALAR_SELECTION=thumb-operands \
  bash badge-compiler/scripts/build-owned.sh
make -C badge-compiler test-operand-selection
node badge-compiler/tools/operand-selection-evidence.mjs \
  --capture badge-compiler/build/po14-new-capture \
  badge-compiler/build/po10-qualified-small-functions-v2/results.json \
  badge-compiler/build/po13-control-selection/results.json
node badge-compiler/tools/operand-selection-evidence.mjs \
  --verify-current badge-compiler/build/po14-new-capture/results.json
PO14_REPORT=badge-compiler/build/po14-new-capture/results.json \
  node --test --test-name-pattern='persistent PO14' \
  badge-compiler/tests/operand-selection.test.mjs
```

Run from the project root with existing LLVM and QEMU on `PATH`. The final
capture compiles only 37 candidate modules and links twelve candidate images
(110 bounded tool commands). It reuses hash-verified control assembly, objects,
images and metrics instead of rebuilding unchanged frontend or control paths.

Captures refuse existing directories and preserve failures. `--resume` accepts
a failed capture only after artifact, policy and emission-source hash checks;
it archives the failed report and records inspector/test-only source updates
and successful commands reused. An earlier inspector-only failure reused 88
completed commands rather than recompiling them. The final report is a separate
capture after completing outgoing-argument selection; earlier evidence remains
historical and is not silently relabeled.
