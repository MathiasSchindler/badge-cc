# MicroPython on RP2350

This experiment builds upstream MicroPython v1.29.0 without patching its
source tree. It produces six Arm Cortex-M33 images:

| Image | Target | Compiler |
| --- | --- | --- |
| `pico2-stock` | Upstream `RPI_PICO2` board | GCC throughout |
| `badge-stock` | Universe badge RP2350B, 16 MiB flash | GCC throughout |
| `badge-mixed` | Same badge configuration | GCC except for LittleFS `lfs2_util.c` |
| `badge-core` | Same badge configuration | Also replaces `py/smallint.c` with an owned target object |
| `badge-vm` | Same badge configuration | Also replaces `py/parsenumbase.c`, `py/unicode.c`, and `shared/timeutils/timeutils.c` |
| `badge-sequence` | Same badge configuration | Also replaces `py/sequence.c` |

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
[calendar](calendar-oracle.c) oracles, plus the [sequence lookup oracle](sequence-oracle.c).
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
| Badge core | 332,756 | 72 |
| Badge VM | 333,692 | 72 |
| Badge sequence | 334,184 | 72 |

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
node micropython/survey.mjs --core
```

The survey replays six CMake compile commands through GCC preprocessing,
removes only GCC diagnostic pragmas, and records the first failure in GCC
preprocessing or the owned frontend, optimizer, code generator, or
assembler. All six default modules now produce ARM objects without the
Clang IR bridge. The frontend preserves assembly symbol names, expands
live `always_inline` calls, and leaves unreferenced inline bodies and
external prototypes out of the IR. SDK bit-count and signed multiply-overflow
builtins, packed layouts, and limited unsigned 32-bit bit-field layout
support get the stock headers through semantic checking. Sequence also
needed a fix for tentative offsets during Thumb branch relaxation.

The exact stock-header ring-buffer object agrees with the exact GCC object on
2,048 operations covering wraparound, empty reads, and oversized requests.
The five public symbols total 470 bytes versus GCC's 320, not counting their
callees. The stock badge link discards those five functions. The default
survey reports the sequence object with the standard freestanding pass
selection; the image build separately enables the libc optimization below.

`--core` adds six larger translation units and writes `survey-core.json`.
All twelve surveyed modules now produce ARM objects. The frontend supports
the surveyed unsigned bit-field reads, writes and initializers, GNU zero-length
trailing arrays, and initialized file-scope flexible tails. The exact stock
`qstr.c` objects match in QEMU for both constant pools: 778 ordinary and
217 static entries, including pool links, metadata, relocations, string
bytes, and computed hashes. This [oracle](qstr-pool-oracle.c) does not
replace `qstr.c` in the badge firmware.

The stock build commands now pass the frontend's ARM base-PCS float mode.
It preserves scalar float and double ABI bits and uses `__aeabi_*` helpers
from the target runtime. The exact `binary.c` objects match on 99
format/byte-order/value combinations, including half, single and double
precision, signed zero, subnormals, infinities and quiet NaNs. The same
[oracle](binary-float-oracle.c) exercises a GCC caller of owned float
functions, including a stack argument after a double-width parameter.
`objint.c` and `objstr.c` also assemble; both opt into literal
`strcmp` lookup specialization before forced-inline expansion. These
three objects are not included in the mixed badge image.

The final two modules are `runtime.c` and `vm.c`. Dynamic stack allocation
keeps AAPCS stack alignment, and the [runtime oracle](runtime-varargs-oracle.c)
checks that the exact owned `mp_raise_msg_varg` passes a variadic argument
list to GCC code across the register/stack boundary. The stock VM source is
unchanged: its GNU computed-goto table becomes a shared switch, including the
default range and subsequent entry overrides.
The existing mixed image and all 51 compiler-corpus images retained
their loaded sizes.

Run `node micropython/build.mjs --interpreter` to link both last modules
into the experimental `badge-interpreter` variant, in addition to the six
modules in `badge-sequence`. It has 344,368 loaded bytes, 10,184 B above
`badge-sequence` and 11,692 B above the all-GCC control. GCC still builds
the other modules, and this variant has not been boot-tested on a badge.

## Core Python arithmetic

The `badge-core` image builds the unmodified `py/smallint.c` using the
stock headers and CMake definitions. GCC preprocesses; the owned frontend,
optimizer, backend and assembler make the target object. The CMake compile database
and link map verify that no GCC `smallint.c` object is compiled or linked
in this image.

The exact objects agree with GCC and an independent arithmetic reference
on 182 signed pairs in Cortex-M33 QEMU. The owned modulo and floor-division
functions are 74 and 54 bytes versus GCC's 28 and 38. The corresponding
reached instruction counts are 3,570 and 2,378 (GCC: 1,332 and 1,716).
The complete core image uses 64 more flash bytes than `badge-mixed`.

The VM image also replaces the prefix parser (220 B versus GCC's 86 B),
checked against GCC and an independent table of expectations on 12,288
input/base cases. Its Unicode replacement is exercised in the REPL by
character classification and UTF-8 length, indexing, slicing, and encoding.
The current linked `.data` and `.bss` section sizes match stock.

## Calendar conversion

The exact calendar objects agree with GCC on all output fields and
round-trip 2,048 timestamps in Cortex-M33 QEMU. The five-module image is
333,692 loaded bytes: 1,016 B above stock GCC and 136 B below the previous
Clang-IR-bridge image. Its two owned conversion helpers are 292 and 332 B
versus GCC's 180 and 184 B.

The previous bridge image passed live `gmtime()` and `mktime()` checks for
the epoch, leap days, normalization, and the 2038 signed timestamp boundary.
Its calendar benchmark medians were
35.431 / 35.413 / 35.406 ms across stock / mixed / stock boots. Sample ranges
overlapped. These hardware figures do not qualify the new image.
In the same campaign, the radix workload medians were 54.990 / 58.267 /
54.991 ms and the signed-arithmetic medians were 54.159 / 55.673 / 54.158 ms.
All 51 existing corpus UF2s verified after the frontend changes: no image
grew, and two shrank by 20 B total.

## Sequence lookup

The generated compressed-error helper expands to hundreds of `strcmp`
calls. After scalar promotion both operands are known strings. For the
sequence object only, the build opts into `libc-strcmp` because the SDK
link binds `strcmp` to newlib. The compiler folds equality tests and
discards the dead branches; no MicroPython source changes are needed.

| `mp_seq_index_obj` | Function bytes | QEMU instructions inside helper |
| --- | ---: | ---: |
| Owned, default pipeline | 14,134 | 2,232 |
| Owned, with `libc-strcmp` | 180 | 251 |
| Stock GCC | 104 | 142 |

The exact objects returned matching indexes for the two-, three-, and
four-argument cases and identical compressed bytes on `ValueError`.
The miss made 247 `strcmp` calls before the change and none after it.
Instruction counts cover the four oracle cases and exclude callees.

Run `node micropython/build.mjs --sequence-before` to also link the same
six-module image without the fold. It loads 361,440 B versus 334,184 B
with the fold, saving 27,256 B. The optimized image is still 492 B
larger than `badge-vm`, whose sequence module is built by GCC. These
are QEMU and link measurements, not on-badge timings.

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

Completing the survey is not the same as replacing all interpreter objects.
Next, boot-test the eight-module variant, expand the survey to more stock
translation units, and qualify any additional varargs forms and live SDK
register-operand assembly needed by those units.
