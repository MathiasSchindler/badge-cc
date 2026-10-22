# Pico 2 W: freestanding RP2350 firmware

A standalone, headless Pico 2 W target extracted from the badge project's
RP2350 startup, boot metadata, ROM recovery and USB-report implementation.
This directory can be copied out of the repository and built independently.
The default LLVM build takes no input from the badge directories. An optional
owned-toolchain mode accepts an explicit directory of prebuilt compiler tools;
the firmware sources remain entirely local.

An additional [clean-rx radio lab](radio/README.md) builds a headless Pico UF2
and separate badge A/B UF2s. It has physically exchanged fresh binary payloads
between a Pico 2 W and both 2026 badges. This optional target reuses sources
from the parent repository, requires LLVM/Make/libusb on the host, and is not
part of the standalone USB-only build described below.

## Dependencies

**Default probe firmware:** none. The target uses project-owned C and a small assembly boot
metadata file, without libc, libgcc/compiler-rt, the Pico SDK, TinyUSB, an OS,
an allocator, or vendor radio binaries. Its integer types come from compiler
built-ins, not standard-library headers. LLVM compilation uses `-nostdinc`
and links with `ld.lld` directly. The owned compiler uses its own preprocessor
and linker. Neither pipeline links startup files or libraries.

**Optional HTTP monitor:** the RP2350 code has the same no-SDK/no-libc
contract, including its project-owned TCP/HTTP stack. The separate CYW43439
Wi-Fi processor needs an explicitly authorized vendor firmware/configuration
data exception. Credentials are supplied at build time, not in source.
The resulting private firmware embeds those credentials in plaintext and
must not be published. See the [monitor guide](docs/monitor.md).

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

The default `--app probe` retains the short-lived, opt-in radio diagnostic.
The separate `--app monitor` builds a persistent Wi-Fi HTTP dashboard and
receive-only LoRa monitor (Meshtastic by default, LoRaWAN selectable); it requires explicit private inputs and
board confirmation. Follow the [monitor build instructions](docs/monitor.md).
Its opt-in [868 MHz activity survey](docs/monitor.md#opt-in-868-mhz-activity-survey)
records noise-relative RSSI bursts, observed start gaps and matching LoRa
packet counts across 65 settings, with browser history and JSON export.
Scanning interrupts normal single-profile reception; stopping restores it.
Tests never connect to an AP or open USB/SPI hardware.

### Native Windows control and deployment

The project-owned [picoctl C host tool](docs/picoctl.md) discovers USB devices,
validates monitor status, requests BOOTSEL and coordinates verified flashing
through an explicitly selected picotool executable. It uses Windows system
libraries and the host C runtime, not Python at runtime. Build it with
`.\pico2w\tools\build-host.ps1 -Test` from the repository root, or use the
direct C compiler command in its guide. This does not change target firmware
dependencies. Firmware now advertises Windows' built-in WinUSB binding.
The [hardware qualification record](docs/picoctl.md#physical-qualification-2026-10-20)
documents verified deployment/recovery, Wi-Fi/DHCP/HTTP and SX1262 RX-mode
startup. Over-the-air LoRa reception still needs a known matching signal.

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
executable; those hashes also participate in the build ID. Native USB and LoRa tests
still run as host C tests, not as execution of generated Arm code.

Both pipelines were built natively on Windows ARM64. Current flash sizes
and UF2 block counts are recorded in each build's manifest rather than
being a fixed property of this evolving application.
LLVM independently accepts the owned ELF and extracts the
same binary bytes as `elf2bin`. This establishes image structure and
packaging, **not execution equivalence**. Later LLVM USB hardware qualification is
recorded separately in the [control-tool guide](docs/picoctl.md#physical-qualification-2026-10-20).
Independently linking all owned objects with LLVM's linker also produces
the same complete binary. The original probe baseline was also tested in
an isolated copy of this directory, outside the
repository and under a path containing spaces, produces an identical owned
UF2 with all LLVM target-tool options deliberately set to unavailable names.

Outputs stay in ignored `build/`:

- `pico2w.elf`, `pico2w.bin`, `pico2w.uf2`, and `pico2w.map`.
- `build_id.h` and `manifest.json`, recording the source fingerprint, compiler,
  image hashes, image size, UF2 family, and unresolved-symbol check.
- Native test executable and intermediate objects.
- Monitor builds use `build/monitor/` (LLVM) or `build/monitor/owned/`,
  separate from probe outputs. Their generated credential headers and
  images are private, not release artifacts.

The build checks the stack/reset vectors, linked boot-block chain, flash
capacity, UF2 packaging, and absence of unresolved symbols. The native tests
cover the USB state machine, report protocol, SX1262 command framing and
fault handling, and Windows collector protocol logic. They do not exercise
the physical USB/SPI controllers, ROM calls, clocks, or boot on a Pico.

## Current application

The default probe application:

1. Schedules a bounded-lifetime ROM reboot to BOOTSEL before touching USB.
2. Reads the chip ID and uses it as the USB serial.
3. Configures clk_sys at 120 MHz to satisfy RP2350-E12, then native USB at
   48 MHz from the board's 12 MHz crystal.
4. Serves nonce/build/CRC-checked reports containing chip ID and request count.
5. Accepts an explicit BOOTSEL request after its USB status stage completes.
6. Accepts an explicit non-transmitting SX1262 probe, with raw results and
   errors returned through separate USB requests. No radio pins are touched
   until the probe submission is acknowledged.

This is an initial diagnostic image, not a persistent production application.
The requested recovery delay is 8000 ms, but inherited clocks/timer settings
mean it is **not a qualified eight-second wall-clock guarantee**. Recovery is
not refreshed by USB traffic. USB initialization changes the core clock to
120 MHz; inherited TIMER0 configuration remains unchanged. A stopped timer causes bounded initialization
failure rather than an infinite polling loop. An explicitly requested LoRa
probe configures crystal-derived reference/peripheral clocks and TIMER1.
Inherited clocks following the reference (possibly the core and recovery
timing) are affected; see the [probe timing details](docs/lora-probe.md).

There is no display, generic GPIO probing, battery management, button/sensor
driver, RF reception/transmission, LoRaWAN, Wi-Fi, Bluetooth, or onboard LED
support. The opt-in probe targets the Waveshare Pico-LoRa-SX1262-868M only.
The Pico 2 W
LED belongs to its wireless chip; this image deliberately leaves that chip
alone. No flash writes are made by the running application.

The USB interface is vendor-specific, endpoint-zero only, **not a COM port**.
See the [protocol](docs/usb-protocol.md). The Windows host needs an appropriate
USB driver binding before a collector can issue vendor control transfers.
Microsoft OS descriptors now request the built-in WinUSB driver automatically,
without a custom driver installer. A standard-library-only
Windows [LoRa collector](tools/lora_probe.py) checks the explicit device's
identity before requesting a radio probe; see its
[usage and protocol](docs/lora-probe.md). The badge's collector is not
compatible with the Pico-specific identity and report payload. Device
communication is a separate hardware-qualification step.

The [persistent monitor](docs/monitor.md) instead joins a WPA2 network,
obtains an IPv4 address by DHCP and serves a local HTTP dashboard. It
starts receive-only SX1262 operation on the Meshtastic EU_868 LongFast
profile (869.525 MHz, SF11/BW250, sync word `0x2b`) and can switch to an
EU868 LoRaWAN uplink profile at runtime (RAM-only). Received frames are
streamed over USB to `picoctl listen`, which decrypts Meshtastic channel
traffic on the host with the project's own AES-CTR and protobuf decoder;
see [listening to Meshtastic](docs/picoctl.md#listen-to-meshtastic-traffic).
The browser also provides a continuous live list (latest 1,000 packets),
all-hash activity, expandable raw/header/radio details, local Meshtastic
decoding, browser-only optional keys and JSON export; see the
[live viewer](docs/monitor.md#live-packet-viewer). Keys stay on the host
or in the browser tab. No LoRaWAN join, MIC verification, on-device
decryption, LoRa transmission or Internet service is included. It is not a
gateway or a Meshtastic node. The firmware also reports the channel noise
floor, and `picoctl survey` measures received power from 150 to 960 MHz to
check the antenna path. On-air trials, nearby TTN gateways and open RF questions are kept in
the [RF field notes](docs/rf-field-notes.md).

## Hardware status and safety

LLVM monitor images have been flashed and exercised on Pico
`7C4918BAB9492C77` (RP2350 A2): USB recovery/status, WPA2 authorization,
DHCP and HTTP serving work, and the SX1262 enters continuous RX without
errors. Meshtastic LongFast packets have been received and decoded on the
host; antenna comparisons are recorded in the field notes. The owned pipeline
is build/layout-qualified, not hardware execution-qualified.

Separately, an authorized 2026-10-22 trial of the earlier inherited-clock
USB-only image on A2 Pico `6572BE3E4636AF51` did not complete USB enumeration;
automatic BOOTSEL recovery was observed. That image is **not hardware-qualified**.
The separate [radio target](radio/README.md) explicitly configures a 120 MHz
system clock and passed live USB and three-device radio exchange on that Pico.
That result does not qualify the earlier inherited-clock USB-only image,
nor does monitor qualification on the first Pico establish it on the second.

Building or testing either target never opens a USB device, changes drivers,
resets hardware, or flashes firmware.

Installation would replace the selected device's current application.
Save any needed firmware/data first, select the actual device explicitly, and
obtain permission before a hardware trial. For manual recovery, hold BOOTSEL
while connecting USB and load a suitable RP2350 Arm Secure UF2. This project
provides explicit selected-device flashing through the native
[picoctl tool](docs/picoctl.md); building alone never flashes.

Errors are retained in debugger-visible `pico2w_error`; `pico2w_stage` is
1 for ROM setup, 2 for USB initialization, 3 while serving, and 4 for an
application-requested disconnect/recovery. USB hardware counters are available
through `usb_hw_stats()`. On initialization failure, the application stops
serving and relies on the already scheduled recovery. If scheduling itself
fails, physical BOOTSEL recovery is necessary.
Radio-probe failures are separate: they remain available in the USB LoRa
report and `pico2w_lora_error` without stopping normal USB service.

## Layout and origin

| Directory | Contents |
| --- | --- |
| [src/rp2350/](src/rp2350/) | Startup, linker layout, Arm Secure image metadata, ROM chip ID/recovery. |
| [src/usb/](src/usb/) | Minimal endpoint-zero state machine, RP2350 USB driver, report codec. |
| [src/lora/](src/lora/) | Opt-in, non-transmitting SX1262 probe and RP2350 SPI1/timing backend. |
| [src/wifi/](src/wifi/) | Project-owned Pico 2 W gSPI/SDPCM/CDC/BDC Wi-Fi station backend. |
| [src/net/](src/net/) | Bounded DHCP/ARP/IPv4/ICMP/TCP/HTTP implementation. |
| [src/app/](src/app/) | Embedded dashboard and bounded JSON/settings handlers. |
| [src/include/](src/include/) | Freestanding integer types. |
| [src/main.c](src/main.c) | Headless application and error/recovery lifecycle. |
| [src/main_monitor.c](src/main_monitor.c) | Persistent monitor, Wi-Fi/receiver/network integration and USB status. |
| [tools/](tools/) | Host-only, standard-library Python build, UF2 generation and explicit Windows probe collector. |
| [tests/](tests/) | Native USB/radio tests and Python packaging/collector tests. |
| [docs/](docs/) | Pico-specific protocol documentation. |
| [radio/](radio/) | Optional repository-integrated clean-rx radio lab, separate headless Pico and display-capable badge images, collector and native tests. |

The starting implementations were the badge project's `src/arch/rp2350/`,
`src/usb/`, and `src/shared/usb_report.c`. They are local adaptations, not
symlinks or wrappers around those directories. Badge radio/application code
and badge GPIO assumptions have been removed. The SX1262 probe is a separate
Pico-specific implementation. The CRC implementation uses a
small bitwise loop instead of carrying the badge's diagnostics/table code.
Report publication is deferred until the submission's status ACK.

The target is the Pico 2 W's RP2350A, Arm Cortex-M33 Secure mode, with 4 MiB
flash and 520 KiB physical SRAM. The stack occupies scratch Y; the main data
region is limited to 512 KiB. Only core 0 is used. RISC-V, multicore startup,
PSRAM, and a production clock/timing contract are outside this initial subset.
