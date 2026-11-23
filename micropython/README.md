# MicroPython on RP2350

This experiment builds upstream MicroPython v1.29.0 without patching its
source tree. It produces five Arm Cortex-M33 images:

| Image | Target | Compiler |
| --- | --- | --- |
| `pico2-stock` | Upstream `RPI_PICO2` board | GCC throughout |
| `badge-stock` | Universe badge RP2350B, 16 MiB flash | GCC throughout |
| `badge-mixed` | Same badge configuration | GCC except for LittleFS `lfs2_util.c` |
| `badge-core` | Same badge configuration | Also replaces `py/smallint.c` with an owned target object |
| `badge-vm` | Same badge configuration | Also replaces `py/parsenumbase.c`, `py/unicode.c`, and `shared/timeutils/timeutils.c` |

The [badge board definition](board/BADGE_RP2350/) is for USB REPL and flash
testing, not a full badge port. It does not configure the display, Wi-Fi,
buttons, or PSRAM. Do not flash the Pico 2 control on the badge.

## Build and check

From the repository root, with Node.js, Git, Make, CMake, Python 3, Arm
GNU Embedded tools, LLVM `clang`/`llvm-nm`/`ld.lld`, and `qemu-system-arm`
on `PATH`:

```sh
node micropython/build.mjs
```

The script pins the release and its SDK submodules, builds host `mpy-cross`,
builds the two GCC controls, swaps in the owned objects using
[a CMake hook](mixed.cmake), and runs the [CRC](crc-oracle.c),
[signed arithmetic](smallint-oracle.c), [radix](radix-oracle.c), and
[calendar](calendar-oracle.c) oracles.
It checks that the mixed build has no GCC compile command for the CRC module
and that its link map names the owned object. All clones, logs, images and
comparison JSON are under the ignored `micropython/build/`. GCC 15.3.1
reports an mbedTLS `-Warray-bounds` warning in this release; the build keeps
it in the log but does not make it fatal.

The CRC translation unit uses the original LittleFS source and the stock
LittleFS options that affect `lfs2_crc`. The owned preprocessor, frontend,
optimizer, backend, and assembler build the object, using
[minimal headers](include/) for that translation unit. The remaining
LittleFS compilation and the final link are GCC/SDK-managed. The oracle
checks the exact GCC and owned CRC objects against an independent bitwise
reference on 1,028 seeded alignment/length pairs, including split updates.

| Output | Loaded bytes | `lfs2_crc` symbol bytes |
| --- | ---: | ---: |
| Pico 2 stock | 329,756 | 56 |
| Badge stock | 332,676 | 56 |
| Badge mixed | 332,692 | 72 |
| Badge core | 332,748 | 72 |
| Badge VM | 333,828 | 72 |

These are flash sizes, not UF2 container sizes. The mixed image is now
16 bytes larger than the badge control, down from a 40-byte difference.

On the same preprocessed source and a 1,024-byte input, folding single-use
right shifts into the XORs after indexed table loads reduced the CRC's
size and removed its local frame. Both GCC and LLVM still avoid one byte
reload per iteration, and use a post-indexed pointer loop.

| Compiler | CRC symbol bytes | Executed CRC instructions in QEMU |
| --- | ---: | ---: |
| Previous owned | 100 | 22,541 |
| Current owned | 72 | 14,347 |
| GCC `-Oz` | 56 | 12,294 |
| LLVM `-Oz` | 52 | 12,292 |

These are emulator instruction counts, not on-badge timings. Across the
51-image badge compiler corpus, 11 images shrank by 132 loaded bytes total;
none grew. No measured function's local-plus-save stack grew in the
147 matched objects.

## Survey more upstream modules

After building the control, run:

```sh
node micropython/survey.mjs
```

The survey replays six CMake compile commands through GCC preprocessing,
removes only GCC diagnostic pragmas, and records the first failure in GCC
preprocessing or the owned frontend, optimizer, code generator, or
assembler. An object-ready result is not a link or execution result.
GNU member alignment, type-form `__alignof__`, C23
`__typeof__(nullptr)`, Boolean spellings, and GNU inline aliases now let
all six inputs get further. Scoped GNU statement expressions, the ARM
`__builtin_va_list` object type, and iterative parsing of generated long
`else if` chains then moved the first blockers to forced-inline attributes
and declaration assembly labels. This does not implement `va_start`,
`va_arg`, or register-operand assembly.

The separate Clang IR compatibility probe now produces owned target objects
for five of six modules. The bridge keeps `memcpy` as a normal freestanding
library call and the owned backend supports named IR records. Sequence
handling still stops at an `inttoptr` constant expression. The ring-buffer
object is not promoted into the firmware because the five public routines
are discarded by the stock link.

With the small header surface it actually needs, the unmodified upstream
`py/ringbuf.c` builds through the entire owned pipeline. Its five public
functions agree with the exact stock GCC object on 2,048 operations covering
wraparound, empty reads, and oversized requests. The five public symbols
total 470 bytes versus GCC's 320, not counting their callees. This is an isolated
Cortex-M33 test, not a second mixed firmware module: the stock badge link
discards those five functions.

## Core Python arithmetic

The `badge-core` image builds the unmodified `py/smallint.c` using the
stock headers and CMake definitions. This is a compatibility route:
GCC preprocesses; Clang emits unoptimized IR; the owned optimizer,
backend and assembler make the target object. The CMake compile database
and link map verify that no GCC `smallint.c` object is compiled or linked
in this image.

The exact objects agree with GCC and an independent arithmetic reference
on 182 signed pairs in Cortex-M33 QEMU. The owned modulo function is 56
bytes versus GCC's 28. Folding negation plus or minus one reduces floor
division from 104 to 66 bytes, versus GCC's 38. The complete core image
uses 56 more flash bytes than `badge-mixed`. This saves 40 loaded bytes
compared with the previous core image.

The VM image also replaces the prefix parser (170 B versus GCC's 86 B),
checked against GCC and an independent table of expectations on 12,288
input/base cases. Its Unicode replacement is exercised in the REPL by
character classification and UTF-8 length, indexing, slicing, and encoding.
The previous four-module image is 528 loaded bytes larger than the stock
badge control. Its historical timing results are below.
The linked `.data` and `.bss` section sizes are identical. In the 182-pair
QEMU workload, floor division went from 3,278 to 2,648 reached instructions
(GCC: 1,716). The owned smallint helpers now reserve no local stack and save
8 bytes of registers per call; this is not a VM-wide stack high-water mark.

The stock / VM / stock medians on the badge were 54.158 / 55.676 /
54.158 ms for signed arithmetic and 54.992 / 71.822 / 54.989 ms for `int()`
with radix prefixes. The VM builds took 2.8% and 30.6% longer respectively
on these end-to-end workloads. This compares linked images, not isolated
hardware timings for the C helpers.

The alignment change passed the target-layout oracle and left the loaded
size of all 51 compiler-corpus images unchanged.

## Calendar conversion

The fifth replacement passes ARM named-record layout across GCC/owned
object boundaries. The exact objects agree with GCC on all output fields
and round-trip 2,048 timestamps in Cortex-M33 QEMU. The running interpreter
also passed `gmtime()` and `mktime()` checks for the epoch, leap days,
normalization, and the 2038 signed timestamp boundary.

The five-module image is 333,828 loaded bytes: 624 more than the four-module
image and 1,152 more than stock. Its owned conversion helpers are 416 and
426 B versus GCC's 180 and 184 B. The calendar benchmark medians were
35.431 / 35.413 / 35.406 ms across stock / mixed / stock boots. Sample ranges
overlap; this establishes compatibility, not a calendar speed win.
In the same campaign, the radix workload medians were 54.990 / 58.267 /
54.991 ms and the signed-arithmetic medians were 54.159 / 55.673 /
54.158 ms. Both still trail stock GCC. The radix workload improved versus
the previous mixed-image run, but those were separate linked images and
campaigns, not an isolated comparison of the radix helper.
All 51 existing corpus UF2s verified at unchanged sizes.

## Optional physical smoke test

This test **temporarily reflashes the selected badge and erases its top
3 MiB experiment filesystem**. Use only a badge you are authorized to
reflash and can manually recover into BOOTSEL if USB fails.
Place that badge in BOOTSEL, install the official `mpremote` host tool,
and set `PICOTOOL` if the USB-enabled tool is not at
`build/picotool-usb/picotool`:

```sh
node micropython/smoke.mjs YOUR_16_HEX_DIGIT_BOOTSEL_SERIAL
```

Append `--core` to boot `badge-core` instead of `badge-mixed`. That variant
also checks `//`, `%`, `divmod()` and divide-by-zero through the running
interpreter, and records seven elapsed-time samples per boot for a loop
that performs 10,000 divisions and 10,000 remainders per sample.
Use `--vm` to also test the radix, Unicode, and timeutils replacements,
invalid `int()` input, and live-object retention during garbage collection.

The runner verifies RP2350B/Arm/16 MiB identity, snapshots all flash before
writing, and boots the badge GCC control, mixed image, and GCC control again.
It checks USB REPL, VM arithmetic, and LittleFS data written by each
compiler and read by the other. It restores the snapshot, independently
reads back all 16 MiB, and leaves the badge in BOOTSEL. `MPREMOTE_PYTHON`
can select `python -m mpremote` instead of the `mpremote` executable.
The images are experimental and should not be used as a daily badge firmware.

## Scope

This does not replace GCC for the interpreter or compile Python scripts with
the owned compiler. Stock RP2 still needs GNU computed `goto`, floating-point
lowering, and more SDK/toolchain interoperability. The owned preprocessor
now handles LittleFS's stringification and standard variadic macros; token
pasting and broader SDK/system-header compatibility remain separate work.

For a fully owned stock build, finish forced-inline and declaration symbol
label support, then qualify ARM varargs and SDK register-operand assembly
against GCC. Keep
the current operand-free barrier and clobber tests as a control. Then
remove the compatibility frontend for a live module. Test switch dispatch
with a separately configured VM before adding computed `goto`; neither is
a substitute for qualifying stock computed goto. Add floating point and
mixed soft-float calling convention tests independently before attempting
to replace all stock target objects.
