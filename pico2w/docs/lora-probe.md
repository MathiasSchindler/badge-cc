# Non-transmitting SX1262 probe

This implementation targets the **Waveshare Pico-LoRa-SX1262-868M** connected
to a Pico 2 W in the standard orientation. Confirm the model, pinout, power
and header alignment before using it. It is not a generic LoRa HAT detector.

Firmware, build tools, tests and the optional Windows collector stay inside
this standalone tree. Target code has no SDK, libc, allocator, vendor stack
or radio-firmware dependency.

## Wiring and scope

| Signal | Pico GPIO |
| --- | --- |
| BUSY | GP2 |
| NSS, active low | GP3 |
| SPI1 SCK | GP10 |
| SPI1 MOSI | GP11 |
| SPI1 MISO | GP12 |
| RESET, active low | GP15 |
| DIO1 | GP20, input only; not used for polling |

The probe does not touch GP22 (the monitor powers the `ANT_SW` RF-switch
supply through it; see monitor.md), battery ADC GP26, the Pico wireless chip,
TCXO/DIO3 configuration, or RF-switch/DIO2 configuration. Do not connect
5 V logic to these signals. No battery is required by the software.

**Nothing initializes the HAT at boot.** A valid, build/nonce/CRC-checked USB
LORA_SUBMIT must finish its status ACK before the application touches radio
pins. Enumeration, INFO, ordinary chip-ID reports and LORA_REPORT reads do
not start a probe. The collector additionally requires an explicit device
interface path, expected chip ID, installed-build manifest and board-confirmation
flag. This is accident prevention, not authentication.

## Probe sequence

1. Configure crystal-derived timing and SPI1, mode 0, MSB first, 8-bit,
   nominally 1 MHz from the Pico's 12 MHz crystal.
2. Keep NSS high, assert RESET low for at least 20 ms, then release RESET
   to the radio's internal pull-up and wait at least 10 ms.
3. Wait for BUSY low before and after every command, with a 100 ms deadline.
4. Select LoRa packet type (`SetPacketType`, `8a 01`) without enabling RF.
5. Read status (`c0`), packet type (`11`), device errors (`17`), IRQ status
   (`12`), and two bytes at sync-word registers `0740`/`0741` (`ReadRegister`,
   `1d`). Dummy/status bytes are accounted for explicitly.
6. Select GFSK packet type (`8a 00`) and repeat the reads. Require the
   packet-type reads to follow the change, standby-RC status without command
   timeout/processing/execution errors, zero device errors, and stable sync
   registers. The sync-word value is reported, not assumed to identify silicon.
7. Leave NSS high, RESET released and SPI1 disabled on success, failure or
   cancellation. Success leaves the radio in standby RC with GFSK selected.

The only radio write command is SetPacketType. There is no arbitrary
command-passthrough interface and no SetTx, SetRx, CAD, continuous-wave,
LoRaWAN join, frequency, transmit-power or TCXO command. The first sample
is LoRa and the second GFSK; this is not a pair of received packets.

The packet-type challenge rejects simple stuck/floating MISO responses;
status checks reject all-zero/all-ones responses. This demonstrates an
SX126x-compatible control interface, not unique identification as an SX1262,
RF-path integrity, working reception, or LoRaWAN connectivity.

### Timing and cancellation

Radio-specific clock setup is **opt-in along with the probe**. It switches `clk_ref` to the
12 MHz XOSC with divide-by-one, and `clk_peri` to XOSC with divide-by-one.
It resets SPI1 and TIMER1, configures TIMER1's tick generator to 12 reference
cycles per microsecond and uses the free-running TIMER1 low word. TIMER0,
the system-clock mux/divider, the USB clock and its PLL are not reconfigured.
However, if the core or another inherited clock follows `clk_ref`, its
frequency changes too. Separately, USB startup now configures the core at
120 MHz to satisfy RP2350-E12 before the probe can run.
Clock configuration is retained after the probe.
The existing automatic BOOTSEL recovery is not cancelled or refreshed, and
its wall-clock deadline remains unqualified.

Every wait has a finite iteration budget in addition to time checks, so a
stopped timer cannot cause an infinite wait. Each SPI byte also has bounded
FIFO/busy polling. The application services USB during reset delays and
radio BUSY waits. Bus reset, committed configuration changes, or an
acknowledged BOOTSEL request cancel the probe and release the radio.
An acknowledged probe cannot be undone by an unrelated SETUP; only an
unacknowledged submission is aborted that way.

USB-controller failures retain their existing disconnect/recovery behavior.
Radio failures instead remain readable in the LoRa report and
debugger-visible `pico2w_lora_error`, without stopping ordinary USB reports.
An invalidated USB session does not publish a late probe result.

## USB extension

The original [USB protocol v1](usb-protocol.md) and test ID 1 are unchanged.
Two vendor requests are added, with zero `wValue` and `wIndex` and an active
configuration:

| Operation | bmRequestType | bRequest | wLength |
| --- | --- | --- | --- |
| LORA_SUBMIT | `40` | `54` | 64 |
| LORA_REPORT | `c0` | `55` | 64 |

SUBMIT has the same `P2WQ` record layout, version 1, length 64, expected build
ID, fresh nonzero 16-byte nonce, zero bytes 32-59, and CRC as the original
protocol, but **test ID is 2**. Sending test ID 2 to the original SUBMIT or
test ID 1 to LORA_SUBMIT stalls. Invalid lengths, fields and CRCs also stall.

REPORT has these little-endian fields:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | `P2WR` |
| 4 | 2 | Version 1 |
| 6 | 2 | Length 64 |
| 8 | 4 | Running build ID |
| 12 | 4 | Test ID 2 |
| 16 | 16 | Echoed nonce |
| 32 | 4 | Status: 0 queued/running, 1 passed, 2 failed |
| 36 | 4 | Signed error code, zero while pending or on success |
| 40 | 4 | Acknowledged LoRa submission sequence, u32, wraps |
| 44 | 8 | LoRa sample |
| 52 | 8 | GFSK sample |
| 60 | 4 | CRC-32/ISO-HDLC over bytes 0-59 |

Each sample contains: status byte, packet-type byte, two device-error bytes,
two IRQ-status bytes, and two sync-word bytes. These raw multi-byte radio
fields remain in **SPI order (MSB first)**, unlike the little-endian report
integers. Samples are zero-initialized; failed samples can be partial and
must not be interpreted as complete. Pending samples are all zero.

| Error | Meaning |
| --- | --- |
| -3001 | RP2350 crystal/clock/reset setup failed |
| -3002 | Timer not advancing or wait iteration budget exhausted |
| -3003 | Radio BUSY high for 100 ms |
| -3004 | SPI FIFO/transfer wait exhausted |
| -3005 | Invalid status, wrong packet-type echo or unstable sync registers |
| -3006 | Nonzero radio device errors |
| -3007 | Probe cancelled because the USB session ended or recovery was requested |

Underlying USB hardware errors can also appear as the signed error before
disconnect. Cancellation usually invalidates the session/report, so its
diagnostic is retained in `pico2w_lora_error` rather than collected over USB.
For example, a TCXO-related device error is evidence of a responding radio,
but is not silently cleared or reported as a passing probe.

The pending report becomes readable only after status ACK. Poll until status
is nonzero; do not resubmit while queued/running (that stalls). Repeated
reads do not increment the sequence. An accepted new submission invalidates
the previous LoRa report. Bus reset or committed configuration changes
invalidate it and cancel active work. Ordinary test-ID-1 reports and their
submission count are independent.

## Windows collector and permission-gated trial

Build and host-test without opening any hardware:

```powershell
python pico2w\tools\build.py all
python pico2w\tools\build.py build --toolchain owned --owned-tools badge-compiler\build
python pico2w\tools\lora_probe.py --help
```

The collector uses Python's standard-library `ctypes` with Windows WinUSB.
It does not discover/select a device automatically, install a driver,
flash firmware, or reboot the Pico. It is **not a COM7 serial-port tool**.

A separate, explicitly approved trial must first install the chosen UF2 and
provide an appropriate WinUSB binding for `cafe:4027`. Obtain the actual
WinUSB device-interface path from that binding's PnP interface enumeration;
a COM name or Device Manager device-instance ID is not the interface path.
This project does not automate the driver-binding step.

After that setup, run from the repository root, replacing the path below
with the explicitly selected device-interface path:

```powershell
python pico2w\tools\lora_probe.py `
  --device-path '<actual WinUSB device-interface path>' `
  --serial 7C4918BAB9492C77 `
  --manifest pico2w\build\manifest.json `
  --confirm-waveshare
```

For an owned image, select `pico2w\build\owned\manifest.json` instead.
Have the command ready before boot: the diagnostic image has a finite
recovery lifetime. The collector checks USB VID/PID, exact INFO/build,
a fresh test-ID-1 response and chip ID **before** submitting test ID 2.
It validates nonce, CRC, identity, sequence consistency and final result,
prints JSON on success, and exits nonzero with explicit diagnostics on
failure. It polls for three seconds; individual WinUSB control transfers
also have the operating system's control-transfer timeout.

**A successful physical radio probe has not been qualified.** Native tests exercise exact radio
command frames, reset timing, timer wrap, stalled timers, stuck BUSY, SPI
failure, invalid/floating responses, device errors, cancellation, cleanup,
USB ACK/abort/reset semantics, and collector identity/failure handling.
The 2026-10-20 LLVM hardware trial qualified USB execution, automatic Windows
binding and recovery on RP2350 A2, but monitor radio initialization returned
`-3005` (invalid command response). GPIO/SPI operation, successful probe
results and RF reception remained unqualified at that stage. Follow-up
monitor trials qualified GPIO/SPI control, TCXO setup and continuous RX-mode
entry after correcting status RFU-bit handling and the expected startup
XOSC error. The readback-only probe intentionally does not enable the TCXO;
successful probe results and actual over-the-air reception remain separate
qualification items. See the
[physical qualification record](picoctl.md#physical-qualification-2026-10-20).

## References

- [Waveshare product](https://www.waveshare.com/pico-lora-sx1262-868m.htm)
- [Waveshare pinout and resources](https://www.waveshare.com/wiki/Pico-LoRa-SX1262)
- [Board schematic](https://files.waveshare.com/upload/d/d8/Pico-LoRa-SX1262_Sch.pdf)
- [SX1261/SX1262 datasheet linked by Waveshare](https://files.waveshare.com/upload/e/e1/DS_SX1261-2_V1.2.pdf)
- [Semtech command definitions](https://github.com/Lora-net/LoRaMac-node/blob/master/src/radio/sx126x/sx126x.h)
- [Manufacturer-linked SX126x board reference](https://github.com/siuwahzhong/lorawan-library-for-pico/blob/ws_dev/src/boards/rp2040/sx126x-board.c)
- [RP2350 register reference](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)

References informed the implementation; no SDK or vendor stack is included.
