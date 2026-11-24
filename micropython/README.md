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
the other modules. See the [badge results](#eight-module-interpreter-on-the-badge)
before using this experimental image.

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

### Eight-module interpreter on the badge

Append `--interpreter` to run a stock/mixed/stock comparison using the
`badge-interpreter` image. The [VM workload](interpreter-smoke.py) checks
imports, caught exceptions, `finally`, and object retention through garbage
collection. The REPL also verifies filesystem data written by the preceding
boot.

All three boots passed on one RP2350 badge. Seven `ticks_us()` samples per
boot gave VM workload medians of **52.983 / 592.556 / 52.984 ms**:
the eight-module build took about **11.2×** as long as stock GCC on this
workload. Sample ranges did not overlap. The original 16 MiB flash was
restored and independently verified. These measurements do not separate the
VM and runtime replacements, so they do not assign the slowdown to a
specific translation unit.

### Isolating the VM and runtime replacements

```sh
node micropython/build.mjs --interpreter-split
node micropython/smoke.mjs YOUR_16_HEX_DIGIT_BOOTSEL_SERIAL --interpreter-split
```

The four images share the `badge-sequence` configuration and six owned
objects. Only `py/runtime.c` and `py/vm.c` vary. The build checks that each
replacement is absent from the GCC compile database and present in the
live link map. The timing runner boots the control, runtime-only, VM-only,
and combined images in that order and then in reverse. Each boot checks
the reported clock, verifies filesystem data from the preceding boot,
and runs the same interpreter workload before finally restoring the
original flash. Use only a badge you are authorized to reflash.

On one RP2350 at 150 MHz, all eight boots passed. Each value below is
the mean of the two per-boot medians (seven `ticks_us()` samples per boot).
The sample ranges did not overlap between images.

| Additional owned code | Loaded flash | VM workload | vs. six-module control |
| --- | ---: | ---: | ---: |
| Neither (six-module control) | 334,104 B | 53.954 ms | 1.00× |
| Runtime only | 340,072 B | 78.953 ms | 1.46× |
| VM only | 338,304 B | 467.863 ms | 8.67× |
| VM and runtime | 344,272 B | 592.652 ms | 10.98× |

Replacing the VM accounts for most of the regression. The runtime
replacement also costs time, and combining them adds about 100 ms beyond
the sum of their isolated increases. The owned build lowers the computed
goto to a 256-case switch. The Thumb backend's table selector stops at
32 cases, so the VM disassembles to a compare chain: the whole dispatcher
contains 309 static compare sites versus 23 in GCC's. This points to a
larger jump table as the first optimization to test; the timings do not
isolate dispatch cost from the rest of the VM.

## Incremental `libm` and LittleFS batch

Run `node micropython/build.mjs --libraries` to build `badge-libm` (owned
CRC and `sqrtf`) and `badge-libraries` (also replaces the LittleFS v2
core). The compile database must omit each replaced GCC translation
unit, and the live link map must name the owned object. Both variants are
experimental; this does not change the default build.

The [sqrt oracle](sqrt-oracle.c) checks 4,097 exact squares plus IEEE
boundary cases, including negative zero, a subnormal, and NaN. The
[LittleFS oracle](littlefs-oracle.c) uses a GCC caller with the stock header
to exercise format, mount, directory creation, file writes across blocks,
rename, remount, readback, and deletion. It simulates aligned flash
transactions and checks that programming never sets programmed bits back to one.
Both GCC and owned objects pass on Cortex-M33 QEMU. The complete sqrt
replay reaches about **15% fewer instructions** with the owned object; the
LittleFS replay reaches about **80% more**. These are replay instruction
counts, not physical flash or panel timings.

| Same-board firmware | Loaded flash |
| --- | ---: |
| Stock GCC | 332,676 B |
| Owned CRC only | 332,692 B |
| Owned CRC and `sqrtf` | 333,252 B |
| Owned CRC, `sqrtf`, and LittleFS core | 357,772 B |

## Survey every remaining C file

After `node micropython/build.mjs --interpreter`, run:

```sh
node micropython/survey-remaining.mjs
make -C badge-compiler test-c-compat
```

For a memory-constrained host, set `JOBS=1` or `JOBS=2` before the survey.
This limits concurrent compiler processes without changing code generation.

The survey replays every C file in the mixed interpreter's GCC compile
database, preserving the board defines and SDK headers. GCC preprocesses;
the owned frontend, optimizer, backend, and assembler build each object.
`build/remaining-c-audit.csv` lists the result and first blocking diagnostic
for every source. This survey does not link substitutions or certify their
runtime behavior. The compatibility oracle separately runs mixed GCC/owned
objects in Cortex-M33 QEMU and checks aggregate layout, bit-fields, builtins,
static relocations, weak symbol overrides, and basic-asm naked functions.

The first compatibility round raised object coverage from 165 to 332 of
423 C files. The register-operand assembly and scaling changes raise it to
381 of 423, with no previously successful file lost. All 25 files
formerly blocked by register operands now assemble. Fifteen of the 16
former resource-limit cases now assemble without increasing the limits:
the IR parser and analysis use less scratch, and the backend indexes
branches and stores instruction uses sparsely. The remaining large
`py/compile.c` unit reaches backend costing but still hits its 128 MiB
allocation limit at that stage. A 256 MiB trial got past that allocation
gate but hit the backend work limit; the optimizer process peaked at
approximately 468 MiB RSS.

The next frontend pass raises coverage to **414 of 423**, resolving 33 of
the 41 frontend failures. It adds scalar `va_arg`, signed bit-fields,
additional initializers, and real ELF function aliases and constructors,
along with GNU declarations and builtins used by the SDK. Eight frontend
failures remain: three files need more GCC ACLE intrinsics, two use
variable-length array types, two need C/GNU atomic support, and one needs
immediate operands in a naked assembly definition. Object coverage does not
prove mixed ABI compatibility; in particular, the owned compiler uses
32-bit enums rather than GCC's short-enum ABI.

Backend scratch reuse and cheaper register summaries now let `py/compile.c`
produce an object without increasing either cap, bringing coverage to
415 of 423. Immediate symbol operands in naked assembly subsequently
enabled the SDK TLS translation unit, bringing coverage to **416 of 423**.
The seven remaining failures involve GCC Arm intrinsics, variable-length
arrays, and C/GNU atomics. Object coverage does not certify the SDK TLS
implementation on hardware.

In three alternating macOS runs, the median peak RSS of
the default optimizer fell from 240 to 170 MiB for `emitinlinethumb.c`,
209 to 123 MiB for `parse.c`, and 238 to 131 MiB for `objtype.c`; optimized
IR was byte-identical. The now-successful full `compile.c` optimization
is a different workload from the old failed run: it peaked at 651 MiB
and took 22.3 seconds on that host.

For a one-off low-memory build, pass
`--passes=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division`
to `badge-opt` for that file. This retains legalization but skips the
expensive target-costed passes. The optimizer peaked at 199 MiB and the
object had about 0.14% more executable bytes than the default. This is
not the default pipeline.

The owned preprocessor supports standard token pasting and 128 include
directories. The stock RP2 build still uses GCC preprocessing: GCC system
header directives, compiler feature detection, and some GNU macros are
not yet supported. Memory/immediate inline-asm constraints, operand modifiers,
`asm goto`, and broader SDK and C runtime interoperability remain open.
The eight-module firmware remains experimental; none of this compiles
Python scripts with the owned C compiler.
