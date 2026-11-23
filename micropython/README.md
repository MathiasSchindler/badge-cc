# MicroPython on RP2350

This experiment builds upstream MicroPython v1.29.0 without patching its
source tree. It produces four Arm Cortex-M33 images:

| Image | Target | Compiler |
| --- | --- | --- |
| `pico2-stock` | Upstream `RPI_PICO2` board | GCC throughout |
| `badge-stock` | Universe badge RP2350B, 16 MiB flash | GCC throughout |
| `badge-mixed` | Same badge configuration | GCC except for LittleFS `lfs2_util.c` |
| `badge-core` | Same badge configuration | Also replaces `py/smallint.c` with an owned target object |

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
[a CMake hook](mixed.cmake), and runs the [CRC oracle](crc-oracle.c) and
[signed arithmetic oracle](smallint-oracle.c).
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
| Badge core | 332,788 | 72 |

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
removes only GCC diagnostic pragmas, then records the owned frontend's
first result for each. GNU member alignment and type-form `__alignof__`
now handle GCC's `max_align_t` layout. All six probes now reach GCC's
C23 `__typeof__(nullptr)` typedef in `stddef.h`, which the owned frontend
does not support.
An exploratory `-std=gnu11` preprocessing run bypassed that typedef for
`smallint.c` and `parsenumbase.c`; both then stopped at Pico SDK's GNU
`__inline__` declaration for a memory barrier. Switching dialects alone
does not make the SDK headers compatible.

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
bytes versus GCC's 28, and floor division is 104 versus 38. The complete
core image uses 96 more flash bytes than `badge-mixed`.
The stock/core/stock REPL timing medians were 54,158 / 55,691 / 54,158 us
per batch (core took 2.8% longer). These are end-to-end VM workload timings,
not isolated helper timings.

The alignment change passed the target-layout oracle and left the loaded
size of all 51 compiler-corpus images unchanged.

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
