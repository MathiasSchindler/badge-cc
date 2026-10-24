# Pico 2 W + two badges: clean-rx radio lab

**Physically verified on 2026-10-22:** 330/330 fresh payload/echo exchanges,
55 in each of the six directed links between a Pico 2 W and two Universe
2026 badges. These were sequential exchanges with all three radios running,
not a simultaneous-transmitter or throughput qualification.

The Pico runs a headless application. The badges run separate applications
with TFT status, using the same shared packet protocol and source-built
CYW43439 controller. Never install the Pico image on a badge or vice versa.
No Pico SDK, TinyUSB, target libc, vendor radio executable, external NVRAM,
CLM, AP, credentials or IP stack is used. The radio reads its on-chip ROM
calibration tables, as in the existing clean-rx targets.

This is a low-duty-cycle, unencrypted and unauthenticated **raw 6 Mbps legacy
OFDM** lab protocol on **channel 6**, not general-purpose Wi-Fi networking.
It does not qualify association, standard ACK/SIFS timing, complete CSMA/CA,
rate adaptation, HT/aggregation, sustained reliability or RF conformance.
Do not send sensitive data. Use only where this raw lab traffic is appropriate.

## Build

From the repository root, with LLVM tools, Make, a native C compiler,
pkg-config and the repository's existing libusb dependency:

```sh
make -C pico2w/radio test all
```

This builds the clean-rx controller from source, links the RP2350 images
without target libraries, checks for unresolved symbols, and packages the
RP2350 Arm Secure UF2 family `0xe48bff59`. Outputs are ignored:

| Node | Board | Output, relative to repository root | Tested serial |
| --- | --- | --- | --- |
| 1 | Pico 2 W, headless | `pico2w/build/radio/node1/radio.uf2` | `6572BE3E4636AF51` |
| 2 | Universe 2026 badge A, TFT | `pico2w/build/radio/node2/radio.uf2` | `08DA26F86E57F5EA` |
| 3 | Universe 2026 badge B, TFT | `pico2w/build/radio/node3/radio.uf2` | `9BBF88DD17D66E7D` |

The two badge images have distinct node identities. All three images share
a source/controller fingerprint; the collector also checks node identity and
chip ID against the explicitly selected USB serial. This is not a signature
or an on-device board-identification interlock: choose the correct image.

The original standalone [USB-only target](../README.md) is unchanged.
The collector now retains both endpoint traces and source-local round-trip
timing independently of desktop polling latency. Use the offline
[retry/latency analyzer](../../clean-rx/dev-doc/guides/HOST_TOOLS.md#retry-latency-and-host-pause-measurements)
for per-link first-attempt rates, retry distributions and latency percentiles.
This instrumentation does not change the six-attempt retry policy.

This optional target intentionally depends on the parent repository's
loader, transport, startup, USB and badge display sources; it cannot be
copied out independently like the original target. Its firmware build is
LLVM-only; the standalone target's owned-compiler qualification does not
extend to this application.

## Board adaptation

The [Pico board interface](pico_board.c) only initializes its radio GPIOs:
enable=23, shared data/host-wake=24, chip select=25, clock=29. These match
[Raspberry Pi's board definition](https://github.com/raspberrypi/pico-sdk/blob/master/src/boards/include/boards/pico2_w.h)
and the existing badge radio interface. The PIO transfer engine is shared.
No display, button, sensor, battery, PSRAM or wireless-LED driver is linked
into the Pico image. The badge builds retain the existing board/display
drivers and show radio readiness, request/confirmation counts and faults.

Both board types use the existing 12 MHz reference/timer setup. The Pico
additionally uses [explicit PLL_SYS setup](clock.c) at 120 MHz, switching
execution to the reference before resetting the PLL and using bounded waits.
The measured system clock before/after setup is included in USB status.
On the tested A2 Pico, the inherited clock measured about 11.23 MHz and the
new clock measured 120 MHz. The inherited-clock radio trial, a USB-first
trial and the original USB-only image all failed to complete enumeration;
they recovered automatically to BOOTSEL. The explicit-clock image completed
enumeration and radio startup. This observation does not establish a
universal A2 USB erratum or a minimum supported system frequency.

The badges retain their inherited system clocks, measured at approximately
45.34 and 45.77 MHz in the passing campaign. Their radios and USB worked
without the Pico-specific PLL change.

## Installation and recovery

**Installation replaces the selected board's application.** Obtain permission,
save any needed firmware/data, verify the backups and explicitly select each
serial. The tested Pico is A2/QFN60 with 4 MiB flash; both badges are A4/QFN80
with 16 MiB. Do not infer board identity or flash capacity from RP2350 alone.
The build's linker bounds all three images to the Pico's 4 MiB flash limit.

With the actual boards in BOOTSEL, the following commands install only the
matching images (replace serials for other boards):

```sh
build/picotool-usb/picotool load -u -v \
  pico2w/build/radio/node1/radio.uf2 --ser 6572BE3E4636AF51
build/picotool-usb/picotool load -u -v \
  pico2w/build/radio/node2/radio.uf2 --ser 08DA26F86E57F5EA
build/picotool-usb/picotool load -u -v \
  pico2w/build/radio/node3/radio.uf2 --ser 9BBF88DD17D66E7D

build/picotool-usb/picotool reboot -a --ser 6572BE3E4636AF51
build/picotool-usb/picotool reboot -a --ser 08DA26F86E57F5EA
build/picotool-usb/picotool reboot -a --ser 9BBF88DD17D66E7D
```

The application waits for USB configuration before radio startup; it is a
USB-attached diagnostic, not an autonomous battery-powered messenger. Failure
to configure USB within ten seconds requests BOOTSEL. Each run is limited
to 180 timer seconds, measured from USB initialization, and automatically
returns to BOOTSEL. A serviced ROM eight-second watchdog also requests
BOOTSEL if application progress stops. This does not guarantee recovery
from arbitrary faults; manual BOOTSEL remains the fallback.

Status and explicit recovery, from the repository root:

```sh
pico2w/build/radio/collector status 6572BE3E4636AF51
pico2w/build/radio/collector bootsel 6572BE3E4636AF51
```

Repeat `bootsel` with each badge serial to stop the whole trial. Wait for
each board to reappear in picotool before another flash/reboot operation.
While live, the target uses the existing lab VID/PID `cafe:4026` and shared
USB descriptor labels; it is not a serial port. Serial/build/node checks,
not the product label, identify a board. The standalone USB-only target's
`cafe:4027` protocol is different.

## Verify communication and data

After starting all three images, run:

```sh
pico2w/build/radio/collector test \
  6572BE3E4636AF51 08DA26F86E57F5EA 9BBF88DD17D66E7D 5 \
  > pico2w/build/radio/exchanges.jsonl
```

The optional round count is 1..100, default 5; each round exercises all six
directed links. A longer campaign must still fit within the 180-second
firmware lifetime. A nonready radio, mismatched image/node/serial, rejected
command, malformed USB record, exhausted retry, radio restart during an
exchange, or missing receiver evidence fails explicitly with nonzero exit
status. The collector does not reset or flash devices.

Each exchange:

1. Reads 14 fresh random bytes from the host's `/dev/urandom`.
2. Queues those bytes on the source via USB.
3. Requires the destination to receive the exact complete radio request.
4. Requires the destination to transmit an echo over the radio, and the
   source to receive it with the original session, sequence and payload.
5. Independently collects both boards' received frames over USB and checks
   FCS, canonical headers/body/padding, source chip ID, destination node,
   session/sequence, payload and received channel/OFDM rate metadata.

USB does not forward messages between boards. JSON Lines retain each
challenge, both complete received packet records, attempt count and
host-observed completion time, plus initial/final device counters and a
pass/fail summary. USB page records are bound to the command nonce,
build ID and report CRC. Arbitrary binary values, including zero bytes,
are valid payload data; this first protocol fixes their length at 14 bytes.
The host collector is POSIX/libusb-based, unlike the cross-platform
standard-library-only builder of the original target.

## Protocol and limits

[protocol.c](protocol.c) implements three numbered nodes and at most one
outstanding request per node. Each 80-byte MPDU has a broadcast Data header,
LLC/SNAP experimental EtherType `0x88b5`, protocol tag/version, kind, source
and destination nodes, sender's full chip ID, originating boot session and
sequence, 14 payload bytes, canonical zero padding and software FCS.
Full-frame validation excludes unrelated ambient traffic.

The destination generates a delayed application reply, not an IEEE 802.11
ACK. A request has at most six attempts with node-dependent retry spacing.
Duplicates in a peer's current session are suppressed and re-acknowledged;
stale sequences in that session are rejected. A one-slot reply queue applies
explicit backpressure, counted as `reply_drops`. This is not exactly-once
delivery across receiver resets or authenticated replay protection.
`failed` means the sender could not confirm the exchange; the request may
already have arrived when a reply was lost.

Controller/bus faults retain their error/address/actual values and trigger a
counted power-cycle/reload after a one-second pause. Outstanding work fails
explicitly rather than becoming successful after recovery. The collector
retains initial fault counters and rejects restarts during a tested exchange.
These retries/reloads do not fix or qualify clean-rx's underlying sustained
receive limitations.

## Recorded qualification

Source/controller build ID: **`9e83870d`**.

| Image | Binary flash bytes | UF2 bytes |
| --- | ---: | ---: |
| Pico, node 1 | 55,984 | 112,128 |
| Badge A, node 2 | 58,476 | 117,248 |
| Badge B, node 3 | 58,500 | 117,248 |

- ASan/UBSan tests cover all six directed links, timer wrap, byte corruption,
  canonical headers/padding, FCS, metadata, stale/mismatched replies,
  duplicate re-ACK, two simultaneous outstanding requests, reply-queue
  backpressure, peer-session changes, and retry/sequence exhaustion.
- Existing clean-rx default-profile controller and loader/USB regression
  tests pass. The unchanged standalone Pico USB tests and seven image/build
  packaging tests pass; its 3,040-byte LLVM build also passes.
- Hardware campaigns passed **30/30**, then **300/300** fresh exchanges:
  **55 per directed link**, **330/330 total**, **660 received frames**.
  An additional independent Python `binascii.crc32`/payload/identity check
  validated all 660 recorded frames.
- **316** exchanges completed on attempt one; **14** needed attempt two.
  No exhausted requests, radio faults, radio restarts, RX transport resyncs
  or reply-queue drops occurred in these passing campaigns. Duplicate
  requests were observed and re-acknowledged; this was not a loss-free link.
- Host-observed per-exchange completion was 10..388 ms, mean 41.4 ms.
  These include USB collection and polling; they are not RF latency or
  throughput measurements.

Full original flash backups were readback-verified before installation:
4 MiB for the Pico and 16 MiB per badge. Evidence, backups, load/verify logs
and both passing JSONL campaigns are in
`pico2w/build/radio/hardware-20261022/`. The earlier failed enumeration trials
are not included in the passing radio counts. All three installed final
images were verified again after the campaigns and, at the user's request,
**left installed in BOOTSEL**, not running or transmitting.
