# Pico 2 W: freestanding RP2350 firmware

A standalone, headless Pico 2 W target extracted from the badge project's
RP2350 startup, boot metadata, ROM recovery and USB-report implementation.
This directory can be copied out of the repository and built independently.
The default LLVM build takes no input from the badge directories. An optional
owned-toolchain mode accepts an explicit directory of prebuilt compiler tools;
the firmware sources remain entirely local.

## Dependencies

**Firmware:** none. The target uses project-owned C and a small assembly boot
metadata file, without libc, libgcc/compiler-rt, the Pico SDK, TinyUSB, an OS,
an allocator, or vendor radio binaries. Its integer types come from compiler
built-ins, not standard-library headers. LLVM compilation uses `-nostdinc`
and links with `ld.lld` directly. The owned compiler uses its own preprocessor
and linker. Neither pipeline links startup files or libraries.

**Development host:** Python 3.9+ (standard library only), LLVM `clang`,
`ld.lld`, `llvm-objcopy`, and `llvm-nm`. Native C tests additionally need
`cc` or another host C compiler and its normal host C library. That library
is used only by the test harness, never linked into firmware.
No package installation, CMake, Make, Bash, or network access is needed.
Alternatively, prebuilt owned compiler tools can replace all LLVM target
tools; building those tools requires a native Clang/GCC-compatible host
compiler. Its host C library does not become a firmware dependency.

## Build and test

From this directory:

```powershell
python tools\build.py all
```

Or from the repository root:

```powershell
python pico2w\tools\build.py all
```

Use `build` or `test` instead of `all` for either step separately.
Tool paths can be supplied with `--cc`, `--ld`, `--objcopy`, `--nm`, and
`--host-cc`. For example, `--host-cc clang` uses Clang for native tests.
The build script resolves its own directory, independently of the working
directory. It also works on POSIX hosts with the corresponding path syntax.

### Owned compiler

From the repository root, build the host tools and then the same firmware
through the owned pipeline:

```powershell
python badge-compiler\scripts\build.py
python -m unittest discover -s badge-compiler\tests -p test_native_tools.py -v
python pico2w\tools\build.py all --toolchain owned --owned-tools badge-compiler\build
```

The explicit `--owned-tools` path is resolved relative to the invocation
directory. A standalone copy can point to tools anywhere; it does not need
badge sources. Missing tools or failed stages are errors: there is no fallback
to LLVM. The stages are `badge-cpp`, `badge-ir`, `badge-opt`, `badge-codegen`,
`badge-asm`, `badge-link`, `badge-elf-info`, and `elf2bin`. The final UF2
packaging uses this directory's standard-library Python encoder.

Owned outputs go to `build/owned/`, without overwriting the LLVM outputs.
Preprocessed sources, IR and assembly are retained there for diagnosis.
The manifest identifies the selected pipeline and hashes each owned
executable; those hashes also participate in the build ID. Native USB tests
still run as host C tests, not as execution of generated Arm code.

Both pipelines were built natively on Windows ARM64: the owned image occupies
11,720 flash bytes and 212 BSS bytes, compared with LLVM's 3,040 flash bytes
and 212 BSS bytes. LLVM independently accepts the owned ELF and extracts the
same binary bytes as `elf2bin`. This establishes image structure and
packaging, **not execution equivalence or working USB on hardware**.
Independently linking all owned objects with LLVM's linker also produces
the same complete binary. An isolated copy of this directory, outside the
repository and under a path containing spaces, produces an identical owned
UF2 with all LLVM target-tool options deliberately set to unavailable names.

Outputs stay in ignored `build/`:

- `pico2w.elf`, `pico2w.bin`, `pico2w.uf2`, and `pico2w.map`.
- `build_id.h` and `manifest.json`, recording the source fingerprint, compiler,
  image hashes, image size, UF2 family, and unresolved-symbol check.
- Native test executable and intermediate objects.

The build checks the stack/reset vectors, linked boot-block chain, flash
capacity, UF2 packaging, and absence of unresolved symbols. The native tests
cover the USB control state machine and report protocol. They do not exercise
the physical USB controller, ROM calls, clocks, or boot on a Pico.

## Current application

The application:

1. Schedules a bounded-lifetime ROM reboot to BOOTSEL before touching USB.
2. Reads the chip ID and uses it as the USB serial.
3. Initializes native USB at 48 MHz from the board's 12 MHz crystal.
4. Serves nonce/build/CRC-checked reports containing chip ID and request count.
5. Accepts an explicit BOOTSEL request after its USB status stage completes.

This is an initial diagnostic image, not a persistent production application.
The requested recovery delay is 8000 ms, but inherited clocks/timer settings
mean it is **not a qualified eight-second wall-clock guarantee**. Recovery is
not refreshed by USB traffic. The code leaves the core clock and inherited
timer configuration unchanged; a stopped timer causes bounded initialization
failure rather than an infinite polling loop.

There is no display, GPIO probing, battery management, button/sensor driver,
radio initialization, Wi-Fi, Bluetooth, or onboard LED support. The Pico 2 W
LED belongs to its wireless chip; this image deliberately leaves that chip
alone. No flash writes are made by the running application.

The USB interface is vendor-specific, endpoint-zero only, **not a COM port**.
See the [protocol](docs/usb-protocol.md). The Windows host needs an appropriate
USB driver binding, such as WinUSB, before a collector can issue vendor control
transfers. This initial subset does not install drivers or include a collector;
the badge's collector is not compatible with the Pico-specific identity and
report payload. Device communication is a separate hardware-qualification step.

## Hardware status and safety

Builds and native tests are verified; **this target has not yet been flashed
or physically qualified on a Pico 2 W**. Building or testing never opens a USB
device, changes drivers, resets hardware, or flashes firmware.

Installation would replace the selected device's current application.
Save any needed firmware/data first, select the actual device explicitly, and
obtain permission before a hardware trial. For manual recovery, hold BOOTSEL
while connecting USB and load a suitable RP2350 Arm Secure UF2. This project
does not provide an automatic flashing command.

Errors are retained in debugger-visible `pico2w_error`; `pico2w_stage` is
1 for ROM setup, 2 for USB initialization, 3 while serving, and 4 for an
application-requested disconnect/recovery. USB hardware counters are available
through `usb_hw_stats()`. On initialization failure, the application stops
serving and relies on the already scheduled recovery. If scheduling itself
fails, physical BOOTSEL recovery is necessary.

## Layout and origin

| Directory | Contents |
| --- | --- |
| [src/rp2350/](src/rp2350/) | Startup, linker layout, Arm Secure image metadata, ROM chip ID/recovery. |
| [src/usb/](src/usb/) | Minimal endpoint-zero state machine, RP2350 USB driver, report codec. |
| [src/include/](src/include/) | Freestanding integer types. |
| [src/main.c](src/main.c) | Headless application and error/recovery lifecycle. |
| [tools/](tools/) | Host-only, standard-library Python build and UF2 generation. |
| [tests/](tests/) | Native protocol/control tests and Python image-packaging tests. |
| [docs/](docs/) | Pico-specific protocol documentation. |

The starting implementations were the badge project's `src/arch/rp2350/`,
`src/usb/`, and `src/shared/usb_report.c`. They are local adaptations, not
symlinks or wrappers around those directories. Radio/application extensions
and badge GPIO assumptions have been removed. The CRC implementation uses a
small bitwise loop instead of carrying the badge's diagnostics/table code.
Report publication is deferred until the submission's status ACK.

The target is the Pico 2 W's RP2350A, Arm Cortex-M33 Secure mode, with 4 MiB
flash and 520 KiB physical SRAM. The stack occupies scratch Y; the main data
region is limited to 512 KiB. Only core 0 is used. RISC-V, multicore startup,
PSRAM, and a production clock/timing contract are outside this initial subset.
