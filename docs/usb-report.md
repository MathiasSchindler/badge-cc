# USB bootloader report round trip

> **Physical result: the SRAM-retention approach failed on the tested
> RP2350 A4 badge.** The application ran and returned to BOOTSEL, but the
> request and report areas read as zero. The host correctly rejected the
> result. This is a retained feasibility test, not a working diagnostic
> channel. See the physical result below before using these instructions.
> The replacement [live USB interface](usb-live.md) has now returned fresh
> verified reports from the attached badge without retaining RAM across reset.

This test establishes a host-readable diagnostic path without adding USB
device code, Pico SDK, or libc to the RP2350 application. It uses the
RP2350's built-in ROM BOOTSEL/picoboot interface **between test runs**.
It is not a live serial console or debugger.

The first firmware deliberately performs **no radio test**. It returns a
known result and echoes a fresh host challenge. Radio-probe integration is
gated on physically verifying this simpler round trip.

## Build

```sh
make usb-report-test
make test-usb-report
make picotool-usb
```

Artifacts:

- `build/universe26/uf2/usb_report_test.uf2`
- `build/host-tools/usb_report_host`
- `build/picotool-usb/picotool`

The firmware is opt-in and uses only project sources plus the chip's ROM
API. The host decoder uses the host C library. USB-enabled picotool uses
host libusb and existing picotool/SDK source as build inputs; those are
not linked into the badge firmware.

`make picotool-usb` defaults to the already-present picotool checkout at
`build/wifi-handover/_deps/picotool-src`. Set `PICOTOOL_SRC` to another
existing picotool checkout if necessary. Host libusb must be discoverable
through pkg-config. The SDK-generated picotool in an experiment's `_deps`
directory may be file-only; this dedicated build verifies that the `save`
command is available.

## First physical test

**The collector replaces the selected badge's installed application with
the report-test firmware.** It never selects an arbitrary device or uses
`--force`. Use a 2026 RP2350 badge initially.

1. Connect the badge with a data-capable USB cable while holding Boot.
2. Confirm the BOOTSEL drive appears.
3. Find its serial number:

   ```sh
   build/picotool-usb/picotool info -a
   ```

4. Run the collector with that explicit serial:

   ```sh
   bash src/host/tools/usb_report_run.sh SERIAL_FROM_PICOTOOL
   ```

The collector:

1. Builds the matching target and host decoder.
2. Checks that the explicitly selected device is accessible and identified
   as RP2350.
3. Generates a fresh 128-bit nonce from the host's random source.
4. Loads/verifies the firmware, clears the old response, and writes/verifies
   the request into reserved SRAM.
5. Reboots the badge into its application.
6. Waits for it to return to BOOTSEL and reads the response via picoboot.
7. Accepts only the expected build/test, nonce, CRC, status and known payload.

Successful collection prints one JSON record with `"verified":true`,
the run nonce, source-build fingerprint, mailbox address, known value
`feedc0de`, and checksum. Raw request/response and command logs remain in a
unique directory under `build/usb-report-runs`. The badge stays in BOOTSEL,
ready for another run without unplugging.

`PICOTOOL` can select another USB-enabled host executable.
`USB_REPORT_OUTPUT_DIR` can select the evidence directory.
`USB_REPORT_TIMEOUT_SECONDS` accepts 1-120 seconds (default 30); a USB
command's own timeout may extend the deadline slightly.

If no badge is visible, no flashing or RAM write is attempted. Multiple
badges require explicit serial selection; never replace this with a
loop that flashes every connected device.

## Target behavior

The application copies the request from the reserved mailbox before
initializing the display. It invalidates the old response and consumes
the request marker, then validates the request and writes a response.
An invalid request produces a failure record, not a fabricated successful
sample. This also lets a manually flashed image return to BOOTSEL so a
proper host challenge can subsequently be supplied.

The screen shows report generation or a request failure. The target calls
the Arm Secure ROM lookup at `0x16`, requests `RB` with lookup mask `0x4`,
then calls the returned reboot function with flags `0x102`, a 10 ms delay,
and zero BOOTSEL options. Both ROM pointers must be plausible Thumb
addresses. Success does not return; both mass-storage and picoboot USB
interfaces remain enabled. No activity-LED GPIO is passed to ROM.

If lookup or reboot fails, the target writes a failure record and leaves
an explicit error on the TFT. Use the physical Boot procedure to recover.
Do not infer success merely because the application disappears from USB
or the screen says it wrote a report.

## RAM layout and freshness

The dedicated linker script reserves the last 4 KiB of main SRAM:

| Region | Address / size |
| --- | --- |
| Application main SRAM | `0x20000000`, 508 KiB |
| Reserved mailbox page | `0x2007f000`, 4 KiB |
| Request | `0x2007f000`, 64 bytes |
| Response | `0x2007f100`, 64 bytes |
| Existing stack top | `0x20082000` |

The mailbox section is `NOLOAD`, outside `.data` and `.bss`, and is not
included in the UF2's flash payload. The normal linker script used by
other firmware is unchanged.

**RAM retention failed on the tested RP2350 A4.** The reserved addresses
protect the mailbox from the application's linker/startup, but cannot
protect it from ROM initialization. Software reboot, ROM revision, and
security settings can affect retention or access. The host does not
disable security controls, ignore permissions, or silently fall back to
flash logging if readback fails. No test results are written to flash;
installing the application itself does write flash.

Records are fixed 64-byte, little-endian values:

| Offset | Request | Response |
| ---: | --- | --- |
| 0 | Magic `U26Q` | Magic `U26R` |
| 4 | Version u16 = 1 | Version u16 = 1 |
| 6 | Length u16 = 64 | Length u16 = 64 |
| 8 | Source-build fingerprint u32 | Same expected build fingerprint |
| 12 | Test ID u32 = 1 | Test ID u32 = 1 |
| 16 | Fresh nonce, 16 bytes | Echoed nonce |
| 32 | Reserved zeros through offset 59 | Status u32: 1 success, 2 invalid request, 3 ROM error |
| 36 | Reserved | Known value `0xfeedc0de`, or failure detail |
| 40 | Reserved | `0x01234567` |
| 44 | Reserved | `0x89abcdef` |
| 48 | Reserved | Response address `0x2007f100` |
| 52 | Reserved | Response length 64 |
| 56 | Reserved | Completion tag `0xc001cafe` |
| 60 | CRC32 of bytes 0-59 | CRC32 of bytes 0-59 |

The generated build fingerprint is the first 32 bits of the SHA-256 hash
of the source-manifest hash listing and Makefile. It is an experiment
identifier, not firmware authentication or an ELF attestation. Target
and host decoder include the same generated definition.

The target writes the response magic last with memory barriers. Exact
length, version, build/test ID, reserved fields, nonce, CRC, status, and
known payload are checked independently by the host. A report from an
earlier run is rejected even if its checksum is valid.

## Host validation

Host tests cover short/oversized records, every single-byte corruption,
wrong magic/version/build/test, stale nonce, nonzero reserved fields,
failure status and wrong known values. Sanitizers are enabled.

The collector is also tested against an explicitly selected mock picotool:
successful command sequencing, nonce echo, stale-report rejection, absent
device, and refusal to run without a serial. Mock outputs are kept under
the host test directory, not mixed with physical report evidence.

The USB-enabled picotool was built successfully using installed libusb.
The target builds freestanding with no undefined symbols, and linker
symbols confirm that startup clearing cannot overlap either mailbox.
These facts are not a physical retention test.

Initially no badge was connected, and picotool reported:

```text
No accessible RP-series devices in BOOTSEL mode were found.
```

## Physical result on 2026-09-14

The owner connected one 2026 badge in BOOTSEL. USB enumeration and picotool
identified serial `08DA26F86E57F5EA`, RP2350 revision A4, QFN80 package,
Arm mode, secure boot disabled, ROM revision `0xa8bfe860`.

The collector ran against that explicit serial:

```sh
bash src/host/tools/usb_report_run.sh 08DA26F86E57F5EA
```

Evidence is in `build/usb-report-runs/run.YYXRbptV`:

- Flash loading and verification succeeded.
- Request/cleared-response SRAM writes and verification succeeded.
- Picotool reported reboot into application mode.
- The badge returned to BOOTSEL with the new report-test image installed;
  boot diagnostics reported slot 0 as the last booted partition.
- The entire 64-byte response at `0x2007f100` read as zero. The collector
  correctly rejected it with `INVALID MAGIC`, not a successful JSON result.
- Follow-up reads also found zeros at the request address `0x2007f000`
  and the application's response buffer at `0x20000000`.

This demonstrates functioning USB flash/write/read access and return to
BOOTSEL, **not retention of the application report**. No radio data was
collected and no networking test ran.

The [published RP2350 BOOTSEL implementation](https://github.com/raspberrypi/pico-bootrom-rp2350/blob/master/src/main/arm/varm_nsboot.c)
describes erasing memory that becomes non-secure-accessible, including main
SRAM, XIP SRAM, and USB RAM. The observed zeroed data is consistent with
that design. The exact abbreviated on-device ROM commit was not available
at the corresponding public raw-source URL; the source reference is not
claimed to be a byte-for-byte dump of this chip's ROM.

Changing the linker reservation alone does not solve this. Do not disable
memory clearing or security protections, accept zero/stale data, or silently
fall back to flash logging.

### Next transport

For cable-only, no-SDK/no-libc diagnostics, implement a minimal
project-owned USB device interface that accepts a host request and returns
the checksummed report **while the application is running**. A vendor
control endpoint would suffice; a full CDC serial implementation is not
required for machine-readable results. The same nonce/build/CRC validation
can be reused. This replacement is now implemented and physically tested;
see [the live USB guide](usb-live.md).

SWD remains the alternative for live RAM/register inspection with an
external debug probe. The badge is left in BOOTSEL, ready for another
explicitly selected firmware load.
