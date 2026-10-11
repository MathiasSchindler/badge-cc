# WiFi M1b assets: packaging and provenance

## Scope and ownership

[wifi_pack_assets.c](../src/host/tools/wifi_pack_assets.c) is a **host-only,
data-only vendor exception** for the no-SDK/no-libc WiFi M1b build. It includes
only the two existing vendor data headers below, plus standard C library
headers. No SDK or CYW43 driver code is compiled into the target.

These are third-party proprietary firmware/regulatory blobs and vendor board
configuration, **not project-owned code or newly licensed project assets**.
Packaging does not establish a license grant. Do not commit generated binaries
or expand their byte arrays into project source. Keep the original vendor
headers, their copyright notices, and accompanying license files intact.

Current controller-side work is covered by the separate
[source-radio handoff](cyw43/handoff.md) and [workflow](cyw43/workflow.md);
[the original research plan](cyw43-driver.md) supplies historical context.
Neither the cold-source components nor checked vendor-retained PSM prefixes
replace the vendor firmware used by the working 600/600-echo host baseline.
Research does not change these pinned assets or their validation/distribution
boundary; experimental images use a separate build and loader path.

The target [wifi_assets.c](../src/radio/wifi_assets.c) and
[wifi_assets.S](../src/radio/wifi_assets.S) consume only generated metadata and
binary data. The public
[wifi_assets.h](../src/radio/wifi_assets.h) declares
`const wifi_fw_assets_t *wifi_firmware_assets(void)`, using the existing type
from [wifi_firmware.h](../src/radio/wifi_firmware.h).

## Pinned inputs and official references

The only packaging inputs are:

| Local vendor header | File bytes | SHA256 of the complete header file |
| --- | ---: | --- |
| [w43439A0_7_95_49_00_combined.h](../vendor/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h) | 1,389,294 | `120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea` |
| [wifi_nvram_43439.h](../vendor/pico-sdk/lib/cyw43-driver/firmware/wifi_nvram_43439.h) | 4,084 | `ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad` |

Official source references checked on 2026-09-15:

- The [Raspberry Pi Pico SDK submodule declaration](https://github.com/raspberrypi/pico-sdk/blob/master/.gitmodules)
  identifies [georgerobotics/cyw43-driver](https://github.com/georgerobotics/cyw43-driver)
  as its CYW43 source.
- The local WiFi-only header matches the upstream Git blob
  `52aea32f3aa6ef72721799a9c0cdf6d3950d7131` at
  [commit f6d06a70f9710f5854808a8a2803e93700e6038a](https://github.com/georgerobotics/cyw43-driver/blob/f6d06a70f9710f5854808a8a2803e93700e6038a/firmware/w43439A0_7_95_49_00_combined.h).
- The local NVRAM header matches the upstream Git blob
  `c721a170af3c63724a87790177c071f61b4b2e0e` at
  [commit cf924bb04c8984675ca0fc2178f082e404e048c3](https://github.com/georgerobotics/cyw43-driver/blob/cf924bb04c8984675ca0fc2178f082e404e048c3/firmware/wifi_nvram_43439.h).
- The [local firmware README](../vendor/pico-sdk/lib/cyw43-driver/firmware/README.md)
  and its [upstream version](https://github.com/georgerobotics/cyw43-driver/blob/f6d06a70f9710f5854808a8a2803e93700e6038a/firmware/README.md)
  describe WiFi padding to 512 bytes followed by CLM, with unpadded lengths in
  `CYW43_WIFI_FW_LEN` and `CYW43_CLM_LEN`.

The Git blob IDs were compared with upstream repository metadata; no remotely
downloaded asset is used for packaging. These references identify matching
files, not the revision of the entire local SDK snapshot. Upstream `main` has
since changed the combined-header layout, so do not substitute current files
or the `wb` WiFi/Bluetooth variant without an explicit new review and new pins.

### License evidence and distribution review

Read and retain these original notices:

- [Driver README](../vendor/pico-sdk/lib/cyw43-driver/README.md): describes
  non-commercial use under the ordinary license and alternative Raspberry Pi
  semiconductor terms.
- [LICENSE](../vendor/pico-sdk/lib/cyw43-driver/LICENSE)
  ([official version](https://github.com/georgerobotics/cyw43-driver/blob/f6d06a70f9710f5854808a8a2803e93700e6038a/LICENSE)):
  copyright (C) 2019-2022 George Robotics Pty Ltd; restricts redistribution,
  use, and modification to personal benefit, not commercial purpose or
  monetary gain; requires copyright, conditions, and disclaimer retention
  in source and reproduction in materials accompanying binary distributions.
- [LICENSE.RP](../vendor/pico-sdk/lib/cyw43-driver/LICENSE.RP)
  ([official version](https://github.com/georgerobotics/cyw43-driver/blob/f6d06a70f9710f5854808a8a2803e93700e6038a/LICENSE.RP)):
  alternative terms expressly limit use/redistribution to conjunction with
  RP2040 or another Raspberry Pi Ltd semiconductor device; also require
  copyright, conditions, and disclaimer retention/reproduction.
- [NVRAM header](../vendor/pico-sdk/lib/cyw43-driver/firmware/wifi_nvram_43439.h):
  bears the original `$ Copyright Broadcom Corporation $` notice and identifies
  its source configuration as `cyw943439wlpth_rev1_0.txt`.

The combined firmware header has no explicit per-file redistribution grant,
and the NVRAM copyright notice is not itself a license. The repository-level
notices above do **not**, by themselves, establish the scope of authorization
for the Broadcom/Cypress/Infineon firmware, CLM, or board configuration.
This document does not assert that either driver license grants rights in all
these blobs, or that their presence in the SDK permits arbitrary redistribution.

Before distributing any generated image, obtain appropriate review of the
blob rights, applicable hardware restriction, intended commercial/non-commercial
use, required notices, and regulatory/board configuration. Include actual
required notices and disclaimers with a distribution, not merely these links.
The upstream README lists `contact@georgerobotics.com.au` for commercial driver
licensing; that is not evidence of a separate firmware/CLM grant. No blanket
redistribution clearance is recorded here.

## Exact extraction contract

The packer checks both vendor length macros, both array sizes, firmware vector
and tail sentinels, CLM `BLOB`/60-byte-header/`CLM DATA` sentinels, NVRAM prefix
and trailing nulls, the entire zero-filled firmware-to-CLM gap, and pinned
CRC32 values over each original payload. Mismatches fail before output creation.
It does not search for another plausible offset or silently accept a revision.

| Output | Original bytes | Padding added | Output bytes / CRC length | CRC32 |
| --- | ---: | ---: | ---: | --- |
| `wifi_firmware.bin` | 224,190 | 2 zero bytes | 224,192 | `e627c344` |
| `wifi_nvram.bin` | 743 | 1 zero byte | 744 | `fa352d20` |
| `wifi_clm.bin` | 984 | 0 | 984 | `29aa78fd` |

- The combined array is exactly 225,240 bytes. Firmware occupies
  `[0, 224190)`. CLM starts at `align_up(224190, 512) = 224256` and occupies
  `[224256, 225240)`. The intervening 66 zero bytes are container padding,
  not part of the original firmware; only two zero bytes are emitted for
  word-aligned firmware download.
- NVRAM uses `sizeof(wifi_nvram_4343)`, **not `strlen`**. All embedded nulls,
  the two final explicitly added nulls, and the C string's implicit terminal
  null are retained. The original array has four trailing null bytes and the
  emitted file has five. No MAC, country, power, or board values are rewritten.
  A bootloader NVRAM length/complement token is not part of this asset.
- CRC is reflected CRC-32/ISO-HDLC: polynomial `0xedb88320`, initial
  `0xffffffff`, final complement, matching zlib and the project's CRC helper.
  Metadata and `wifi_fw_assets_t` sizes/CRCs cover **all output bytes including
  download padding**, not just original lengths. Original firmware/NVRAM CRCs
  used for input validation are `fe8b5e03` / `357a3338`.
- Generated numeric macros are usable by both C and the assembly preprocessor.
  Each assembly section is allocated, read-only, and `.balign 4`, with global
  `wifi_{firmware,nvram,clm}_{start,end}` symbols. Assembly rejects any binary
  whose length differs from generated metadata. Same-size corruption is
  detected by the firmware loader's existing CRC validation.

SHA256 of the complete generated binary files:

```text
204e802a87c97d691b14eb7c42c41dba6fe109c26451529f4f7a0536342a065f  wifi_firmware.bin
4e85b562cdc6b3914b7bf73a2a62faf484c1ad0673a15ad607134b29e4554954  wifi_nvram.bin
e712b3d218e8b1e2747b092e03b8b0afcb8c8c8e355d2a4a0d47b493800f3f89  wifi_clm.bin
```

## Build integration (owned by the parent build)

Run from the project root; no Makefile, application, loader, or shared-header
changes are included in this asset-only implementation:

```sh
set -eu
shasum -a 256 -c <<'HASHES'
120bb63db1c5cf42cd44cf9a057d6f14601038a9413badf5a3b737f6552bc9ea  vendor/pico-sdk/lib/cyw43-driver/firmware/w43439A0_7_95_49_00_combined.h
ddc82b00667643f55d7687bba1b5e4028f24717ec8883f05bd7d435f4c9c01ad  vendor/pico-sdk/lib/cyw43-driver/firmware/wifi_nvram_43439.h
HASHES
ASSETS="$PWD/build/wifi-assets"
mkdir -p "$ASSETS"
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
  src/host/tools/wifi_pack_assets.c -o "$ASSETS/wifi_pack_assets"
"$ASSETS/wifi_pack_assets" "$ASSETS"

clang --target=arm-none-eabi -mcpu=cortex-m33 -mthumb \
  -std=c11 -Os -Wall -Wextra -Werror -ffreestanding -fno-builtin \
  -ffunction-sections -fdata-sections -I"$ASSETS" \
  -c src/radio/wifi_assets.c -o "$ASSETS/wifi_assets.o"
clang --target=arm-none-eabi -mcpu=cortex-m33 -mthumb -I"$ASSETS" \
  "-DWIFI_FIRMWARE_BIN=\"$ASSETS/wifi_firmware.bin\"" \
  "-DWIFI_NVRAM_BIN=\"$ASSETS/wifi_nvram.bin\"" \
  "-DWIFI_CLM_BIN=\"$ASSETS/wifi_clm.bin\"" \
  -c src/radio/wifi_assets.S -o "$ASSETS/wifi_assets_blob.o"
```

Link both objects into the firmware application; keep **distinct object names**
for the C and assembly sources. The target linker must collect `.rodata.*`.
There are no vendor includes or libc references in these target objects.
The application gets the immutable descriptor from `wifi_firmware_assets()`.

Instead of `-I"$ASSETS"`, both target compilations can receive
`"-DWIFI_ASSETS_GENERATED_HEADER=\"$ASSETS/wifi_assets_generated.h\""`.
The three `.incbin` macros are required quoted strings; use absolute paths to
avoid dependence on the assembler's working directory. The assembly source
must run through the C preprocessor (uppercase `.S` or `-x assembler-with-cpp`).
Shell examples above preserve the embedded quotes; Makefile recipes must also
preserve them.

The host's default vendor includes are source-relative. If necessary,
`WIFI_FIRMWARE_DATA_HEADER` and `WIFI_NVRAM_DATA_HEADER` can be overridden with
quoted absolute paths to the **same pinned local headers**, not alternate
assets. SHA256 pins are recorded in the host tool and generated header for
provenance; the tool does not implement SHA256. The build must enforce the
complete source-file hashes **before compiling** the host executable. Payload
CRC/sentinel checks complement, but do not replace, those cryptographic pins.

Make the host tool depend on its source and both vendor headers. Treat the
three binaries and generated header as one output group; rebuild the group
if any member is absent, and rebuild both target objects when it changes.
Serialize generation with compilation/assembly; do not compile against a set
being replaced. Use a dedicated generated directory under ignored build output.

The packer requires one existing output directory and does not create it.
It exclusively creates `.tmp` files (`wbx`, C11), checks writes and closes,
then renames each output, publishing the generated header **last**. The
header's temporary file also excludes a second packer using the same directory.
Validation or staging failures preserve a previous published set and clean
only this invocation's temporary files. Before publishing any binary, the old
header is removed. A rename failure returns nonzero and explicitly reports
an incomplete set with no generated header; rerun rather than use the binaries.
This is per-file atomic publication, not a four-file transaction or a
power-loss durability guarantee. A crashed process may leave `.tmp` files;
remove only those known stale files after ensuring no packer is running.

## Verification

Verification performed without hardware or SDK linkage:

- Strict C11 host compilation with warnings as errors.
- Independent Ruby extraction of the vendor byte array and concatenated
  NVRAM string literals, including the implicit null, compared byte-for-byte
  with all three outputs; zlib CRCs checked against generated metadata.
- Cortex-M33 C and assembly compilation, no-libc minimal ELF linkage,
  and inspection of exact embedded bytes, all nine descriptor fields, section
  alignment, symbol spans, and absence of unresolved symbols.
- Address/undefined-behavior-sanitized fault injection for length, sentinel,
  padding, and payload-CRC mismatches; allocation, write (including padding),
  close, and first/partial/final rename failures. Previous sets survive
  pre-publication failures; publication failures remove the manifest and
  recover deterministically on rerun.
- CLI argument/path failures, stale temporary-file exclusion, real rename
  failure/recovery, and repeat generation. Explicit include/bin path macros
  were tested with spaces, with both Clang and GNU Arm assembly; missing bin
  macros and each truncated binary were rejected with explicit diagnostics.

For a quick independent CRC/size check after generation:

```sh
ruby -rzlib -e '
  expected = {
    "wifi_firmware.bin" => [224192, 0xe627c344],
    "wifi_nvram.bin" => [744, 0xfa352d20],
    "wifi_clm.bin" => [984, 0x29aa78fd]
  }
  expected.each do |name, pair|
    data = File.binread(File.join(ARGV.fetch(0), name))
    actual = [data.bytesize, Zlib.crc32(data)]
    abort "#{name}: #{actual.inspect} != #{pair.inspect}" unless actual == pair
    puts "%s: %d bytes CRC32=%08x" % [name, *actual]
  end
' "$ASSETS"
```
