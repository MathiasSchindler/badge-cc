# PO12: lightweight frames and caller-saved residency

**Implemented and host-qualified as `thumb-frames`.** This explicit candidate
inherits PO11's `thumb-effects`; the qualified default remains `thumb-calls`.
Select it through `--scalar-selection=thumb-frames` or
`BADGE_CODEGEN_SCALAR_SELECTION=thumb-frames`.

## Frame contract

The backend finalizes each function's save group from its emitted body,
including scratch operands, PHI transfers, byval copies, and inline assembly,
not just the register allocator's assignments. Assembly with unknown effects
keeps conservative saves, LR protection, and a stable frame base.

- Zero local bytes never produce zero SP subtraction/addition.
- Stable-SP leaves use SP-relative stack references and addresses, without
  `r7` setup. Functions needing stable addressing across calls retain `r7`.
- LR is saved only for emitted calls or opaque assembly that may affect it.
  Leaf returns use `bx lr`; call-bearing returns restore PC from saved LR.
- Save/restore lists contain used callee-saved registers rather than fixed
  groups. A caller-saved alignment word is included only when needed to keep
  an allocated frame/call site eight-byte aligned or encode a lone high save.
- Incoming stack arguments are resolved **after** the actual save size is
  known, including offsets above 64 KiB. Outgoing argument handling, AAPCS
  alignment, return carriers and hidden results retain their existing rules.
- Published local-frame/save metadata describes the finalized emission.
  The shared C06 inspector validates masks, balance and call alignment.

The frame finalizer uses the compiler's bounded, owned emitted instruction
stream. It counts register mentions conservatively rather than pretending
arbitrary inline assembly has fully decoded effects. No external disassembler
or dependency is introduced.

## Caller-saved allocation

The candidate adds `r3` as a preferred home for eligible scalar functions:
at most three arguments, no byval, return/value types at most one word,
no wide integer operations, and no unknown assembly. `r0-r2/r12` remain
selector scratch, so broad allocation into those registers is not claimed.

Intervals live at ordinary calls forbid `r3`, including tail/notail calls,
indirect calls and intrinsics. Values needing preservation choose the existing
callee-saved homes or spill. Short non-call-crossing intervals can use `r3`
inside functions that otherwise contain calls. Narrow values retain their
normalization. PHI interference/cycle handling remains unchanged.

This bounded extension avoids a speculative full scratch-register allocator.
General interval splitting and rematerialization are not implemented; they
are not necessary for the qualified one-register extension.

## Qualification

[The persisted production report](../build/po12-lightweight-frames/results.json)
compares **identical archived optimized IR**, PO11 control versus PO12 candidate,
across **37 modules, 554 functions and twelve ordinary images**. The capture
reuses the PO11 evidence tool with PO12 policies and the existing ELF/frame
inspectors; it does not add a parallel compiler pipeline.

- All control object and loaded binary identities replay the PO11 candidate.
- Nine production functions have zero local frame and zero saves.
- 32 local frames shrink; no local frame or save group grows.
- Unallocated values decrease from **2,174 to 1,930**, with no per-module growth.
- Assembly spills and fused compare counts are unchanged from PO11.
- All twelve images shrink, without data/BSS growth or physical-region overflow.

| Image | PO11 bytes | PO12 bytes | Saved |
| --- | ---: | ---: | ---: |
| button_mapper | 12,916 | 12,388 | 528 |
| diagnostics | 57,496 | 56,036 | 1,460 |
| gpio_events | 10,748 | 10,272 | 476 |
| gpio_monitor | 8,636 | 8,128 | 508 |
| hello_idle | 568 | 536 | 32 |
| imu_demo | 29,120 | 28,276 | 844 |
| lower_mapper | 13,148 | 12,624 | 524 |
| peripheral_probe | 16,172 | 15,496 | 676 |
| screen_test | 5,796 | 5,492 | 304 |
| tilt_breakout | 22,696 | 21,832 | 864 |
| tilt_maze | 34,780 | 33,660 | 1,120 |
| usb_wifi_udp | 366,584 | 363,436 | 3,148 |

Targeted sanitizer tests assert exact zero-save/zero-frame leaf output,
both assemblers accepting it, and mask metadata rejecting tampering.
Cortex-M33 execution covers cyclic PHIs, actual unknown register clobbers,
call-crossing values, and nine bidirectional/all-owned AAPCS combinations
using the existing frame-addressing, register-residency and call-transport
fixtures. These exercise high scratch registers, large stack argument offsets,
wide/narrow/composite carriers, byval/sret, recursion and indirect calls.
Resource-limit failures preserve existing output.

PO11 mechanism and build-driver tests remain passing. No broad historical
hardware/profile matrix is rerun.

## Limits and promotion

These are static size and host/QEMU correctness results, **not speed or
physical-peripheral qualification**. PO11's display-delay/LCD/USB settling
timing gate still applies. Faster frames and busy loops are not silently
promoted; no badge is flashed and no application timing is rewritten.

Older selectors retain their qualified behavior. The explicit candidate
lets PO13 and later work build on these improvements while affected-path
hardware timing is qualified independently.

## Reproduction

From the project root with LLVM and QEMU on `PATH`:

```sh
make -C badge-compiler test-lightweight-frames
node badge-compiler/tools/lightweight-frame-evidence.mjs \
  --capture badge-compiler/build/po12-new-capture \
  badge-compiler/build/po10-qualified-small-functions-v2/results.json \
  badge-compiler/build/po11-qualified-effects/results.json
node badge-compiler/tools/lightweight-frame-evidence.mjs \
  --verify-current badge-compiler/build/po12-new-capture/results.json
PO12_REPORT=badge-compiler/build/po12-new-capture/results.json \
  node --test --test-name-pattern='persistent PO12' \
  badge-compiler/tests/lightweight-frames.test.mjs
```

Captures refuse existing directories and preserve failed attempts. Historical
verification hashes persisted artifacts and recomputes frame/object accounting;
current verification additionally checks source/tool identities.
