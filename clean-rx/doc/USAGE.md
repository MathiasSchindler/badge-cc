# Integrating clean-rx with an RP2350 host

The RP2350 loads `cyw43_rx_fw.bin` into CYW43439 SRAM and exchanges
commands and frames over gSPI. The controller executes radio, PHY, PSM,
DMA and raw-frame operations. The badge host supplies station orchestration
and software WPA2/CCMP.

Shared protocol definitions are in [frame_limits.h](../src/frame_limits.h),
[cyw43_hw.h](../src/cyw43_hw.h), [tx_diag.h](../src/tx_diag.h) and
[unicast.h](../src/unicast.h). Use their constants and helpers rather than
duplicating sizes or command bit fields.

## Building

From the repository root:

```sh
make -C clean-rx/src CC=clang BUILD=../../build/clean-rx
make -C clean-rx/badge
```

The controller output is `build/clean-rx/cyw43_rx_fw.bin`. The badge build
embeds it and produces `build/clean-rx/badge/clean_rx.uf2` and the USB
`collector`. The controller and badge use freestanding C and project-owned
code. Building the collector requires the host's libusb development files.

The controller production build is self-contained: copying `clean-rx/src/`
is sufficient to build its binary with the tools above. It requires no
other repository source, SDK, libc or downloaded firmware executable.
Native tests and RP2350 host applications have separate source/tool
dependencies. See [the isolation checks](../dev-doc/C02_ISOLATION.md) for
standalone builds and byte-for-byte comparison against the pre-cleanup
controller profiles. On-chip ROM calibration tables remain a hardware
dependency.

For an application embedding the controller directly, place the binary in
a read-only assembly section using `.incbin`, and expose start/end symbols
to the host loader. The controller loads at SRAM address `0x00000000`.
The linker reports the image size.

The [badge loader](../badge/loader.c) implements the bus setup, staged
loading, readback, deadlines and packet assembly described below.
Use matching controller/host build flags for the [HT20 profiles](HT20.md).

## gSPI interface

| Signal | Direction | Purpose |
| --- | --- | --- |
| `WL_ON` | RP2350 to CYW43439 | Power/reset enable |
| `WL_CS` | RP2350 to CYW43439 | Active-low chip select |
| `WL_CLK` | RP2350 to CYW43439 | SPI clock |
| `WL_D` | Bidirectional | Command/write data and read data |

The 32-bit command word contains write at bit 31, increment at bit 30,
function at bits 29..28, address at bits 27..11 and length at bits 10..0.

- Function 0 provides bus configuration, interrupts and packet status.
- Function 1 accesses SRAM and registers through a backplane window.
- Function 2 reads the host-facing WLAN packet FIFO.

At power-on, Function 0 uses halfword-swapped framing. Writing
`0x000304b3` to Function 0 address `0x0000` selects 32-bit little-endian
framing. The test register at `0x0014` reads `0xfeedbead`.

Select the backplane window with Function 1 registers `0x1000a..0x1000c`.
Use the low 15 address bits with the `0x8000` word-access flag for aligned
32-bit scalar and burst backplane I/O. Split block transfers at 64-byte
boundaries.

## Loading and starting the controller

1. Hold `WL_ON` low for at least 20 ms, raise it and wait 50 ms.
2. Qualify the bus, configure Function 0, request the ALP clock using
   Function 1 address `0x1000e` and wait for ALP availability.
3. Hold the Cortex-M3 wrapper in reset and initialize SOCRAM.
4. Clear the mailbox and capture storage using the selected build's
   layout. Copy the controller binary to SRAM and verify readback.
5. Set `config_flags`, a nonzero generation token and the capture target.
   `0xffffffff` selects continuous capture.
6. Release the Cortex-M3 reset and wait for `FW_STATE_WAIT_HT` (3).
7. Request the HT PLL clock through Function 1 address `0x1000e`, wait
   for availability and write `host_cmd = 1`.
8. Wait for `FW_STATE_PSM_READY` (8). Read and validate the layout manifest
   at `CLEAN_RX_LAYOUT_ADDR` against the host's compiled layout.
9. Write `host_cmd = 2` and wait for `FW_STATE_RX_ACTIVE` (9).

The clock named HT is a backplane/PHY resource; frame formats are selected
separately by the command and build profile.

The mailbox is 480 bytes at `0x00020000`. Important fields are:

| Byte offset | Field |
| --- | --- |
| 4 | `fw_state` |
| 8 | `fw_substage` |
| 12, 16, 20 | Error code, address and value |
| 24 | `host_cmd` |
| 28 | `config_flags` |
| 32 | Generation token |
| 444 | Saturating publication/drop counters |
| 448, 452 | Consumed and posted logical-frame counters |
| 456 | RX fault snapshot |

Configuration bits select RXE setup, branch masks, antenna grant, IFS/TSF,
radio LPF setup, promiscuous reception, TX permission and station role.
`CFG_CONTINUOUS_RX` and `CFG_WPA2_CCMP` are defined in the header but are
not read by the controller. Continuous operation is selected by
`MULTI->target_frames = 0xffffffff`; command 6 selects hardware crypto.
The initial channel occupies bits 23..16.
Controller channel selection accepts 1..14; the badge collector accepts 1..11.

Bound every wait and check `fw_state` for `FW_STATE_ERROR` (255).
Preserve the first error code/address/value and treat it as a failed
operation. Runtime commands that resume reception return `host_cmd` to 2;
stop command 3 instead terminates in `FW_STATE_RX_DONE` (10).

## Runtime commands

Stage command data at `CLEAN_TX_BUFFER_ADDR` before writing `host_cmd`.
The staging address comes from the selected build's layout.

| Command | Behavior |
| --- | --- |
| `2` | Active reception and frame delivery |
| `3` | Stop MAC/PSM and RX DMA, snapshot state, enter `FW_STATE_RX_DONE` (10) |
| `4 \| (channel << 8)` | Request a channel retune and resume reception; completion acknowledges handling, not successful tuning |
| `5 \| clean_tx_length_bits(total) \| (ofdm << 18) \| (count << 19)` | Send a staged legacy frame with OFDM or long-preamble DSSS/CCK |
| `6 \| (enable << 8)` | Copy the staged 16-byte key to the D11 crypto key area and select the hardware-crypto path |
| `7` | Configure the 16-byte pinned-peer station MAC record |
| `8` | Queue basic-rate/slot policy for active station profile 2 or 3 |
| `10 \| clean_tx_length_bits(total) \| (count << 19)` | Send short-preamble DSSS/CCK at 2, 5.5 or 11 Mbps |

Command 7's record contains local MAC at byte 0, peer MAC at byte 6,
little-endian basic-rate mask at byte 12, short-slot selection at byte 14
and profile at byte 15. Profile 1 selects the capture-based ACK path,
profile 2 Cortex DCF/NAV and profile 3 PSM/IFS/TSF-clocked medium access.
Command 8 requires profile 2 or 3 and keeps the bound addresses and active
profile. Profile 1 is rejected with error `0x85`. Policy is applied at an
idle RX/response boundary. Completion acknowledges queueing; the
policy-pending status records application.

Profiles 2 and 3 process NAV before selecting own-address captures and
group-address management/data from the pinned peer for delivery. They use
an eight-slot SRAM FIFO independently of the single logical F2 credit;
when full, the next capture stays in SHM. Target-based completion drains
staged frames, but an explicit stop can leave pending RX. The radio status
includes either held SHM or queued SRAM frames in `pending_rx`. The MAC
trace includes the filtered-capture count and, for profile 3, maximum
sampled capture age in microseconds. These are service diagnostics, not
RF timing or packet-loss measurements.

When no validated authentication response is saved, station reports can
include `auth_bad_fcs_report` and `auth_bad_fcs_count`. A nonzero count adds
the candidate's raw PLCP/MPDU, PHY/RXE status, sampled TSF value and independent
software FCS result. This diagnostic does not deliver rejected frames or
relax authentication validation.

Command 4 uses the channel field at bits 15..8. Values outside 1..14 are
acknowledged without a retune. For in-range values, the controller updates
its software channel only when the tuning routine succeeds, but returns
`host_cmd` to 2 even if that routine fails. The command does not report
that failure through `fw_error`; neither completion nor the retained
software channel confirms the physical tuning state.

Station mode rejects channel/key changes and raw rearm/address-match
overrides. Reliable legacy unicast uses `CLEAN_TX_ACK_REQUIRED`, at most
eight attempts and a bound-peer management or non-QoS ToDS data frame.
An active EDCA policy additionally accepts the bounded normal-ACK QoS
ToDS Data/Null headers described in the [HT20 reference](HT20.md).
The status at `CLEAN_UNICAST_ADDR` distinguishes idle, ACKed, timeout and
busy. Busy with zero attempts is a deferral.

Outside station mode, `CLEAN_TX_REARM_RX` repeats RXE configuration after
TX. `CLEAN_TX_MATCH_SOURCE` installs template address 2 as the hardware
station match and address 3 as the BSSID match before TX; it accepts only
version-0 non-DS management frames with a unicast source and total template
length of at least 34 bytes. These flags do not change the SHM auto-ACK
address. The hardware matcher persists until reprogrammed or reset.

The [HT20 reference](HT20.md) describes commands 12, 14, 15 and 16,
including the opt-in non-HT WMM station build.
Developer-only fixtures and test commands are documented in
[dev-doc](../dev-doc/README.md).

## Receiving frames

Poll Function 0 status at `0x0008`. Bit 8 indicates an available Function 2
packet; bits 19..9 give its length. Read the packet rounded up to a
four-byte boundary, retaining its declared unrounded length.

The ordinary packet has an 18-byte SDPCM/BDC header followed by the MPDU,
including its four-byte FCS. Validate the length/complement, data offset,
metadata and complete payload before releasing a logical-frame credit.
See the [packet format](HARDWARE_AND_FIRMWARE.md#function-2-packet-format).

Packets larger than the single-packet limit arrive as two physical
fragments, or up to three in the extended receive profile. Check matching
sequence/metadata, total length, offsets and lengths. The continuation
deadline is 100 ms. A partial or invalid frame does not receive a credit.

Legacy metadata provides the channel, PLCP rate, OFDM flag and CCK
preamble flag. Use `clean_ofdm_rate_index` for OFDM and the SIGNAL values
10/20/55/110 for DSSS/CCK. The OFDM RATE nibble shares its byte with
LENGTH bits, so the whole byte is not a rate identifier.

Station profiles 2 and 3 consume local ACKs internally. Accepted RTS/CTS
responses rearm directly. Application delivery and control-frame
processing use separate paths.

## Transmitting frames

1. Build a six-byte PLCP followed by the MPDU, including FCS.
   Use `clean_cck_plcp_rate` or `clean_ofdm_plcp_rate` from
   [phy_rates.h](../src/phy_rates.h).
2. Validate the length against `CLEAN_MPDU_BYTES` and write the complete,
   word-padded template to the staging buffer. `total` is six plus MPDU
   length; pass it through `clean_tx_length_bits`, not a direct shift.
3. Set `CFG_TX_ENABLE` and submit the command with a repetition count
   of 1..255. Command 5 uses long-preamble CCK or OFDM; command 10 selects
   short-preamble CCK.
4. Poll completion under a host deadline, preserving any controller error.
   For reliable unicast, inspect the separate ACK result and attempt count.

Raw command completion reports local TXE/PHY processing. A received ACK or
peer response is a separate result. MAC retries retain sequence/fragment
identity and protected bytes/PN; a new management request uses a new
sequence.

## WPA2 in the badge host

The badge host implements WPA2-PSK authentication and software CCMP,
including handshake validation, replay protection and protected receive
reassembly. Its station association emits legacy capabilities.

The collector accepts:

```sh
build/clean-rx/badge/collector SERIAL evidence.bin CHANNEL \
  --ap-wpa2 BSSID PRIVATE_CREDENTIAL_FILE
```

The credential file belongs to the current user, has mode `0600`, and
contains exactly two LF-terminated lines: SSID (1..32 bytes) and passphrase
(8..63 bytes). The collector derives the PMK and sends it with a fresh
SNonce through the dedicated secret-control path, bound to the BSSID,
channel and request. This is separate from the controller's raw
hardware-key command.

For a paired WPA2 fixture, install the unmodified vendor `usb-wifi-link`
image on one badge and the clean image on the other, leaving both in
BOOTSEL. Run:

```sh
CLEAN_RX_COLLECTOR=build/clean-rx/badge/collector \
  bash clean-rx/badge/wpa2_trial.sh AP_SERIAL STATION_SERIAL build/new-wpa2-trial
```

The runner boots both USB applications before starting the AP, starts
station collection at AP readiness, and requires independent matching-peer
authorization and protected traffic. It writes phase times and both
endpoints' evidence under the new prefix. `--wrong-password` selects the
negative control. Temporary credentials are private and removed on exit;
the runner does not flash either badge.
