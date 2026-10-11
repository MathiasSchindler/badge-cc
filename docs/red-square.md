# Smallest measured red-square firmware

## PO18 default and timing update

The measurements below retain the original cold-boot-tested experiment.
The owned default now includes the PO11-PO17 chain. With PO18's separate
compiler-independent initialization-delay mitigation, the compact program
measures **992 owned loaded bytes versus 568 LLVM bytes**. Each wait iteration
now retains six explicit NOPs: the shorter four-million-iteration wait alone
supplies at least 160 ms at the supported maximum 150 MHz.
Both production assemblies retain that sequence and the native/target
drawing oracle passes. This new size pair is host-measured, not a replacement
for the original 560-byte physical minimum record.
See [PO18 qualification and attribution](../badge-compiler/docs/current-promotion.md).

## Result

The smallest **measured and cold-boot-tested** image in this experiment is
**560 loaded bytes with LLVM**. The same specialized C program and startup
produce **1,432 loaded bytes with the owned compiler's promoted defaults**.
Both badges displayed the same centered red square on black; the owner
confirmed both still worked after removing all power and reconnecting.
This is not a proof that no smaller program can exist.

The square is 64 x 64 pixels, at `(128, 88)` on the 320 x 240 display:
RGB565 red `0xf800`, background `0x0000`. There is no target libc, SDK,
framebuffer, font, USB stack, IMU or radio code.

| Build | Compact loaded image | Existing startup/display loaded image |
| --- | ---: | ---: |
| LLVM Clang `-Oz`, no LTO | **560 bytes** | 1,156 bytes |
| LLVM Clang `-Oz -flto` | **560 bytes** | 1,008 bytes |
| Owned promoted default pipeline | **1,432 bytes** | 3,580 bytes |

The existing-driver control calls the shared
[display functions](../src/platform/badger/display.c) and uses ordinary
[startup](../src/arch/rp2350/start.c). The compact route uses the specialized
[C program](../src/tools/red_square.c) and
[startup](../src/arch/rp2350/start_red_square.S). Both retain all shared-driver
LCD analog/gamma, RGB565, inversion and orientation settings, the reset and
sleep-out waits, board power, pads and write-strobe timing.

The compact program holds chip-select low throughout setup and drawing,
keeps the backlight off until the complete frame is ready, and uses
table-driven commands rather than the generic region/callback interface.
It writes a full black frame followed by a red square window. No previously
running display firmware or preinitialized framebuffer is required.

## Where the loaded bytes go

These totals include machine instructions, literal pools, constant data,
boot metadata and alignment. They are not ELF file sizes.

| Category | LLVM bytes | Owned bytes |
| --- | ---: | ---: |
| Core exception vector table | 64 | 64 |
| RP2350 PICOBIN start/end blocks | 40 | 40 |
| Reset, interrupt masking, vector-base setup and fault halt | 24 | 24 |
| GPIO setup and application sequencing | 184 | 554 |
| Byte transport and command-table interpretation | 96 | 440 |
| Repeated-pixel fill loop | 36 | 104 |
| Reset and sleep-out delay routine | 10 | 100 |
| LCD configuration/window tables | 102 | 102 |
| Alignment | 4 | 4 |
| **Total** | **560** | **1,432** |

The identical startup and LCD data account for 230 bytes before alignment.
The **872-byte compiler gap is entirely in generated C code/literal pools**:
326 bytes for LLVM versus 1,198 for owned. The largest contributors are
GPIO/application sequencing and the command/byte helpers. Owned output
still uses more frame/save, branch and operand-transport instructions in
these tiny functions. It does not beat LLVM on this challenge.

The square has **no bitmap stored in flash**. The program generates 76,800
black pixels and then 4,096 red pixels: 161,792 pixel-data bytes travel to
the panel at runtime. The display controller retains the image in its own
GRAM. Static MCU data/BSS is zero; call stacks still use MCU RAM.

### Binary versus packaging

| Format/allocation | LLVM compact | Owned compact |
| --- | ---: | ---: |
| Raw loaded `.bin` | 560 | 1,432 |
| 256-byte UF2 payload pages | 3 (768 bytes) | 6 (1,536 bytes) |
| UF2 download file | 1,536 | 3,072 |
| Minimum 4 KiB flash erase sectors touched | 1 | 1 |

Each standard UF2 block is 512 bytes but carries 256 firmware bytes;
headers/trailers and unused space are transport overhead, not executing code.
The last payload page also pads the image. Both images fit in a single
4 KiB flash erase sector, even though their raw sizes differ.
ELF files are larger again due to host metadata, symbols, section headers
and file-offset alignment; that material is not copied into badge flash.

## Size investigation and safety limits

The first compact LLVM candidate was 612 bytes. Preventing LLVM from
duplicating the MMIO byte-write sequence reduced it to 560 bytes.
`noinline` is guarded by `__clang__`: the owned compiler has no inliner and
does not support that attribute. LTO produced no further compact-image
reduction, although it shrank the shared-driver control.

The small startup disables maskable interrupts, installs the vector base
and retains all sixteen core vector slots with fault handlers. It does not
initialize data or BSS: the specialized
[linker script](../src/arch/rp2350/memmap_red_square.ld) rejects either if added.
The original two-block PICOBIN format is retained. We did not trade away
these guards, remove LCD tuning/waits, depend on retained panel state, or
claim an untested two-vector/single-block image as a working minimum.

Startup uses explicit Thumb instructions rather than `ldr =constant`
pseudo-instructions, and the owned/LLVM startup instruction bytes are
compared in a maintained test. The two routes use the same startup
instructions; an assembler compatibility workaround is not counted as
a compiler size win.

A hand-written assembly drawing/command interpreter, a newly qualified
smaller boot-block layout or better tiny-function compiler lowering might
shrink this further. Such changes need their own checks; 560 is the
smallest qualified result here, not a mathematical lower bound.
One visually confirmed cold power cycle per badge is not long-duration,
temperature/voltage or panel-waveform qualification.

The [general compiler follow-up plan](../badge-compiler/docs/pipeline-optimization.md#general-follow-up-from-the-red-square-experiment)
turns the generated-code differences into PO10-PO18 milestones: scoped
assembly effects, lightweight frames, CFG/countdown selection, address and
operand reuse, pressure-aware loop motion, sharing/specialization and
independent qualification. These are compiler improvements for ordinary
programs, not red-square-specific rewrites or a requirement to imitate LLVM.
PO10 is [implemented and qualified](../badge-compiler/docs/small-function-attribution.md):
the exact same optimized IR reproduces the full 872-byte backend size gap,
with per-helper byte ledgers and an ordinary-production pattern inventory.

## Reproduce without flashing

From the project root, put LLVM and QEMU on `PATH`:

```sh
export PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:$PATH

make -j4 BUILD_DIR=build/red-square-llvm red-square red-square-shared
make -j4 BUILD_DIR=build/red-square-llvm-lto RP2350_CC='clang -flto' \
  red-square red-square-shared
make -C badge-compiler -j4 all
make -j4 BUILD_DIR=build/red-square-owned \
  RP2350_CC='bash badge-compiler/scripts/owned-cc.sh' \
  RP2350_OBJCOPY="$PWD/badge-compiler/build/elf2bin" \
  RP2350_NM="$PWD/badge-compiler/build/badge-elf-info" \
  RP2350_SIZE="$PWD/badge-compiler/build/badge-elf-info" \
  red-square red-square-shared

make test-red-square test-red-square-target
node badge-compiler/tools/red-square-size.mjs build/red-square-new-sizes.json
```

The targets are optional: ordinary `make all` is unchanged. Use fresh build
directories when changing compiler policies or compiler flags; Make does
not infer that flags have changed. Explicit environment policies override
owned defaults, so unset `BADGE_IR_EXPRESSION_LOWERING`, `BADGE_OPT_PASSES`
and `BADGE_CODEGEN_*` before reproducing this default comparison.
The report writer refuses to overwrite an existing report.

The native sanitizer oracle reconstructs the panel's pixel coordinates and
checks every final pixel, GPIO selection, exact write count, RGB565 bytes,
delay arguments and display/backlight ordering. The target test executes
LLVM and owned C against that oracle on QEMU Cortex-M33, including a
mixed LLVM-harness/owned-application call boundary.
This checks digital behavior; real-panel confirmation is separate.

## Evidence

- [Final size and exact byte-accounting report](../build/red-square-results.json):
  loaded ELF sections, symbols, validated UF2 payloads, hashes, compiler
  version and exact owned default queries.
- [Target execution test log](../build/red-square-target-tests.log).
- [Full flash preservation/restoration](../build/red-square-hardware/results.json).
- [Owner's visual/cold-boot observation](../build/red-square-hardware/observation.json).

Hardware testing used LLVM on `9BBF88DD17D66E7D` and owned on
`08DA26F86E57F5EA`, once each; no timing campaigns or role-swap speed claims.
The two complete 16 MiB originals are preserved independently, then restored,
reread and hash-verified. Both badges are left in BOOTSEL.
