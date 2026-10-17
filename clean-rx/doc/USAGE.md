# Integrating `clean-rx` CYW43439 Firmware with an RP2350 Host

This guide explains how an RP2350 host application loads, boots, and communicates with the open-source `cyw43_rx_fw.bin` transceiver firmware over the 4-pin gSPI bus.

**Full 2,304-byte MSDUs now fit the raw-frame path at 11 Mbps CCK.** A basic
24-byte Data header plus that MSDU and four-byte FCS is a 2,332-byte MPDU.
The qualification profile sweeps 2,268..2,332 MPDU bytes, with exact payload
and FCS checks. Shared storage supports MPDUs through 2,400 bytes, but buffer
capacity does not qualify every header, encryption mode or PHY rate.
See [full-MSDU implementation and qualification](../../docs/micro-signal.md#full-2304-byte-msdus).
This does not add association or an Ethernet/IP stack.
**Legacy 802.11g OFDM is now OTA-qualified at 6, 9, 12, 18, 24, 36, 48 and
54 Mbps** for six MPDU sizes through 2,332 bytes on channel 6. The paired
five-minute test requires exact payload/FCS and received modulation/rate
metadata for all 48 cases on both badges. See
[OFDM implementation, build targets and limitations](../../docs/micro-signal.md#80211g-legacy-ofdm).
Use the shared limits and TX length helpers rather than copying old sizes or
shifting a large length directly into command flags.

MPDUs through 2,026 bytes retain one original F2 packet. Larger ones use two
independently framed F2 packets, each fitting the gSPI length field; the loader
reassembles one complete SDPCM packet before releasing one logical-frame credit.
The `0x21` fragment marker, total length and offset are project-specific and
require a matching host and controller. This is not over-air fragmentation.
See the [physical packet format](HARDWARE_AND_FIRMWARE.md#82-sdiod-function-2-tx-dma-ring-0x180022000x18002214).

The repository now includes a working [freestanding badge host](../badge/README.md)
with build commands and physical results from 2026-10-14 and 2026-10-15.
Cold beacon reception, Function-2 delivery and single-frame 11 Mbps CCK OTA TX
have been verified between a Universe badge and a Tufty2350, in both
directions. Peer RX matches a fresh challenge, full MPDU and valid FCS.
Micro Signal additionally qualifies the eight legacy OFDM rates above.
The paired raw MAC fixture now supports all four DSSS/CCK rates and all seven
legal long/short-preamble combinations, with receiving-chip rate/preamble
checks. See [C1 evidence and limitations](../badge/README.md#dssscck-rate-and-preamble-fixture);
full-size 1-Mbps reception remains an explicit failure. Automatic ACK timing
and general WPA2 connectivity remain unqualified.
For a persistent, no-USB application using this loader and radio interface, see
[Micro Signal](../../docs/micro-signal.md): the same small UF2 on both boards
sends debounced button presses and displays received peer events.
Its startup timeout repair passed 80 controlled cold radio boots. Later
32-byte DMA bursts and explicit host credits repaired the reproduced sustained
transport failures; see current qualification results before relying on
indefinite operation.
`CFG_TX_ENABLE` must be set for explicit TX or automatic
ACKs; a command-5 request with TX disabled reports firmware error `0x81`.
Incomplete TXE/IFS completion reports `0x82`; an invalid TX length reports
`0x83`. RXE/PSM restart failure reports `0x84`; an invalid template for the
optional management-frame address matcher reports `0x85`.
An invalid SDIO TX consumer pointer reports `0x86`;
failure to disable SDIO TX DMA during initialization reports `0x87`. Both
these and the preceding errors preserve ERROR through cleanup.
TX completion predicates are
reported in `fw_substage`: bit 0 means TXE busy cleared, bit 1 means TXE
completion asserted, and bit 2 means IFS TX cleared. All three bits must be set,
but this does not establish reception over the air.

The badge loader requests continuous reception (`CFG_CONTINUOUS_RX`) explicitly,
including when using a compatible reference image for comparison.
During explicit TX, the controller uses the mailbox's 180-byte `rx_input`
diagnostic area for `clean_tx_diag_t` (`TXD3`, defined in `../src/tx_diag.h`),
instead of the final RX SHM snapshot. The host trace offset is 296; the mailbox
previous field offsets are unchanged; the current mailbox is 480 bytes.
RX-only and disabled-TX-rejection
trials retain the original snapshot. Diagnostic readbacks happen before the
trigger, and timing records distinguish TX completion from MAC RX re-enable.
PHYDEBUG is additionally sampled immediately after triggering TX. Its bit 2
means the MAC instructs the PHY to transmit, not that an AP received the frame.
The former staged CRC field is no longer present; the badge host verifies the
staged bytes directly.

Sustained Function-2 delivery uses a 32-descriptor circular SDIO TX ring.
The final descriptor carries end-of-table, and a slot is reused only after
the hardware consumer advances. A full queue drops new frames instead of
resetting live DMA or overwriting pending buffers. The old mailbox's final
four padding bytes now expose `f2_counts` at offset 444: low 16 bits count
published frames, high 16 bits count full-queue drops, both saturating.
Existing mailbox offsets are unchanged; credits and the fault latch extend the
current mailbox to 480 bytes. Initialization
clears the producer pointer before enabling DMA and publishes RX_ACTIVE only
after the RX path is ready.

In uncredited legacy profiles, the badge loader counts SPI read-frame
resynchronizations in `rx_resyncs`.
It terminates a read frame on invalid advertised lengths or underflow, or when
SDIO reports FIFO data but SPI exposes no complete packet across two separated
checks. It permits at most three consecutive resynchronizations without a
valid SDPCM packet; persistent faults and malformed headers still fail.
[Micro Signal](../../docs/micro-signal.md#sustained-rx-repair) displays drop and
recovery counters and includes a minute-long paired regression.
The collector accepts bounded 30..600-second runs. Before the credit/DMA repair,
a five-minute pass was not repeatable. Credited production and fragment faults
fail explicitly without terminating/retrying a partial frame.

There is also an RF-free transport reproducer:
`make micro-signal-transport-test` builds a separate SDIO-only controller and
RP2350 host. It reproduces stalled FIFO delivery and captured a genuine DMA
data-FIFO underrun with payload corruption without initializing D11/PHY.
The strict test loader stops before recovery writes, as does credited
production. Historical unrestricted/64-byte controls retain their old behavior;
later credit/DMA and full-MSDU qualification results supersede these failures.
See [the transport test and evidence](../../docs/micro-signal.md#two-universe-investigation-and-rf-free-reproducer)
for commands, serial bindings and the distinction between the pristine stall
and later underrun. Test command 7 is not part of the production controller ABI.
The default reproducer now requests just four packets and stalls after three
deliveries, before any ring wrap or descriptor reuse.
[The assumption audit](../../docs/micro-signal.md#auditing-transport-assumptions)
also records independent GPIO packet reads, intact descriptor readbacks and
the tested SPI length contract. Those historical assumption-audit results alone did not establish a repair.

SPI bulk backplane reads and writes must set the `0x8000` word-access address
flag, as scalar backplane I/O does. The badge loader now enforces this mode
for its aligned bursts, including TX staging and source-firmware loading.

Command 5 has two opt-in receive-path diagnostic bits, defined in
`../src/tx_diag.h`. Bit 27 (`CLEAN_TX_REARM_RX`) explicitly restarts PSM/RXE after
TX, waits for READY with a deadline, and preserves the channel and CCMP key
configuration before enabling MAC RX. PSM discard counters reset on restart.
Bit 28 (`CLEAN_TX_MATCH_SOURCE`) programs hardware RX address matching before
TX: template address 2 becomes the station match and address 3 the BSSID match.
This option requires a version-0, non-DS management template at least 34 bytes
long with a unicast source address. It does not change the SHM auto-ACK MAC.
The matcher remains configured during subsequent reception until reprogrammed
or reset. Both bits leave the TX count field (bits 19..26) unchanged; omitting
them preserves normal TX resume behavior. The badge collector exposes these
through `--tx-auth-rearm`, `--tx-auth-match` and `--tx-auth-match-rearm`.
Programming uses D11 `rcm_ctl` at `+0x420` and `rcm_mat_data` at `+0x422`,
with `RCM_INC_DATA = 0x20`, station offset 0 and BSSID offset 3, following
the public Linux brcmsmac address-match sequence
([implementation](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmsmac/main.c),
[register definitions](https://github.com/torvalds/linux/blob/master/drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h)).

The badge host also supports a two-radio test using this unchanged controller
ABI: `--peer-tx`, `--peer-tx-1m` and `--peer-rx`, with a shared fresh 14-byte
challenge. Peer RX runs with TX disabled and validates an exact challenge
frame received over Function 2. Transmitter command completion alone still
does not qualify OTA TX. See the badge guide for staged loading, serial
binding, negative controls and `--inspect-peer` offline verification.

---

## 1. Building and Embedding the Firmware Binary

First, build `build/cyw43_rx_fw.bin` from `clean-rx/src/`:

```bash
make -C clean-rx/src
```

This produces a ~20 KiB raw ARM Cortex-M3 binary (`clean-rx/src/build/cyw43_rx_fw.bin`) linked to execute at CYW43439 SRAM address `0x00000000`.

Embed `cyw43_rx_fw.bin` into your RP2350 image using `.incbin` in an assembly file (e.g. `assets.S`):

```armasm
.section .rodata.cyw43_rx_fw, "a"
.balign 4
.global cyw43_rx_fw_start
.global cyw43_rx_fw_end
cyw43_rx_fw_start:
    .incbin "clean-rx/src/build/cyw43_rx_fw.bin"
cyw43_rx_fw_end:
```

Declare the symbols in your RP2350 C code:

```c
extern const uint8_t cyw43_rx_fw_start[];
extern const uint8_t cyw43_rx_fw_end[];
```

---

## 2. RP2350 <-> CYW43439 Hardware & gSPI Bus Protocol

On RP2350 boards (such as the GitHub Universe badge / Pico 2 W), the RP2350 talks to the CYW43439 over a 3-wire/4-pin gSPI interface driven by a PIO state machine:

| Signal | Direction | Description |
|---|---|---|
| `WL_ON` (`REG_ON`) | RP2350 -> CYW43439 | Power/reset enable (active high) |
| `WL_CS` | RP2350 -> CYW43439 | gSPI chip select (active low) |
| `WL_CLK` | RP2350 -> CYW43439 | gSPI serial clock |
| `WL_D` (`DIO`) | Bidirectional | Half-duplex serial data (`MOSI` during command/write, `MISO` during read) |

### 2.1 32-Bit gSPI Command Word
Every gSPI transfer begins with a 32-bit command word:

```c
static inline uint32_t gspi_cmd_word(int write, int incr, uint32_t fn,
                                     uint32_t addr, uint32_t len) {
    return ((write ? 1u : 0u) << 31) |
           ((incr  ? 1u : 0u) << 30) |
           ((fn & 0x3u)       << 28) |
           ((addr & 0x1ffffu) << 11) |
           (len & 0x7ffu);
}
```

- **Function 0 (`fn = 0`)**: gSPI bus configuration, chip ID, interrupt/status (`0x0000..0x001f`).
  - Note: At power-on before `0x0000` is configured to 32-bit little-endian mode, Function 0 uses **16-bit halfword byte-swapped** framing (`(w >> 16) | (w << 16)`). After writing `0x000304b3` to Function 0 register `0x0000`, all transfers use standard 32-bit little-endian words.
- **Function 1 (`fn = 1`)**: Backplane window access to CYW43439 SRAM (`0x00000000..0x0007ffff`) and core MMIO registers (`0x18000000..0x1810ffff`).
  - Set the upper backplane address window (`addr & ~0x7fffu`) by writing bytes `1..3` to Function 1 registers `0x1000a` (`(win >> 8) & 0xff`), `0x1000b` (`(win >> 16) & 0xff`), and `0x1000c` (`(win >> 24) & 0xff`).
  - Access the lower 15-bit offset (`addr & 0x7fffu`) on Function 1 (`or` with `0x08000u` for 32-bit access).
  - **Important Alignment Rule:** Backplane burst transfers (`bp_block`) must not cross a 64-byte address boundary (`(addr & 0x3f) + len <= 64`). For unaligned buffers or general register/staging writes, use 32-bit `bp_write(addr, 4, val)` transfers.
- **Function 2 (`fn = 2`)**: High-speed WLAN packet FIFO (`SDIOD` TX DMA -> RP2350).

---

## 3. Step-by-Step Boot & Initialization Sequence from RP2350

To boot `cyw43_rx_fw.bin` from cold power-on on the RP2350:

### Step 1: Power Cycle & Bus Qualification
1. Drive `WL_ON = 0` for at least `20 ms`, then drive `WL_ON = 1` and wait `50 ms`.
2. Read Function 0 test register `0x0014` (expect `0xfeedbead`) and switch Function 0 register `0x0000` to 32-bit little-endian mode (`0x000304b3`).
3. Write `0x04` to Function 0 register `0x0004` and `0x00f7` to Function 0 interrupt mask `0x0006`.
4. Request the ALP (crystal) clock on Function 1 by writing `0x08` to `0x1000e` and polling `0x1000e` until bit `0x40` (`ALP_AVAIL`) is set, then clear `0x1000e = 0x00`.

### Step 2: Hold ARM Cortex-M3 in Reset & Initialize SOCRAM
1. Reset the ARM Cortex-M3 wrapper (`0x18103000`):
   - Write `1` to `RESETCTRL` (`0x18103800 = 1`), write `3` to `IOCTRL` (`0x18103408 = 3`), wait briefly, and leave `RESETCTRL = 1` (held in reset while loading SRAM).
2. Reset and release the SOCRAM wrapper (`0x18104000`):
   - Write `1` to `0x18104800`, `3` to `0x18104408`, `0` to `0x18104800` (release reset), and `1` to `0x18104408`.
   - Disable SRAM bank standby by writing `0` to `SOCRAM + 0x10` (`0x18004010 = 0`) and `0` to `SOCRAM + 0x44` (`0x18004044 = 0`).

### Step 3: Load `cyw43_rx_fw.bin` and Configure the Mailbox
1. Zero the mailbox (`0x00020000`, `480` bytes) and multi-frame capture ring (`0x00020d80`, `19752` bytes).
2. Copy `cyw43_rx_fw.bin` (`cyw43_rx_fw_start..cyw43_rx_fw_end`) into CYW43439 SRAM starting at `0x00000000` using 64-byte aligned backplane writes.
3. Configure the initial mailbox fields at `0x00020000` (`FW_MBOX_ADDR`):
   ```c
   uint32_t channel = 11u; /* 2.4 GHz channel 1..14 */
   uint32_t flags = (0x1fu)               /* CFG_D11_RXE | BRANCH_MASKS | BTCX_RFSW | IFS_TSF_CLK | RADIO_LPF */
                  | (1u << 5)             /* CFG_PROMISCUOUS */
                  | (1u << 7)             /* CFG_CONTINUOUS_RX */
                  | (1u << 8)             /* CFG_TX_ENABLE */
                  | ((channel & 0xffu) << 16);
   bp_write(0x00020000u + 28u, 4u, flags);       /* MBOX->config_flags */
   bp_write(0x00020000u + 32u, 4u, 0x13572468u); /* MBOX->generation */
   bp_write(0x00020d80u +  4u, 4u, 0xffffffffu); /* MULTI->target_frames (0xffffffff = continuous mode) */
   ```

### Step 4: Release ARM Cortex-M3 Reset & Complete the HT Clock Handshake
1. Release the ARM Cortex-M3 from reset:
   - Write `3` to `0x18103408`, `0` to `0x18103800`, and `1` to `0x18103408`.
2. Poll `MBOX->fw_state` (`0x00020004`) until it reaches `FW_STATE_WAIT_HT` (`3`).
3. On the RP2350, request the High-Throughput (HT) PLL clock on Function 1:
   - Write `0x10` to Function 1 register `0x1000e`.
   - Poll `0x1000e` until `(val & 0x90) == 0x90` (`HT_AVAIL`), and verify `CC_CLK_CTL_ST` (`0x180001e0`) has bits `0x00030000` set.
4. Notify the firmware that the HT clock is locked by writing `host_cmd = 1`:
   ```c
   bp_write(0x00020000u + 24u, 4u, 1u); /* MBOX->host_cmd = 1 */
   ```
5. Poll `MBOX->fw_state` (`0x00020004`) until it reaches `FW_STATE_PSM_READY` (`8`). At this point, the D11 core, Radio 20691, LCN20 PHY, and RX/optional-response PSM program have completed initialization.
6. Start active reception by writing `host_cmd = 2`:
   ```c
   bp_write(0x00020000u + 24u, 4u, 2u); /* MBOX->host_cmd = 2 (FW_STATE_RX_ACTIVE) */
   ```

---

## 4. Runtime Operations (`MBOX->host_cmd` at `0x00020018`)

Once `MBOX->fw_state == FW_STATE_RX_ACTIVE` (`9`), the RP2350 controls the transceiver by writing commands to `MBOX->host_cmd` (`0x00020018`) and polling `0x00020018` until the firmware resets `host_cmd` back to `2` (ready for the next command).

| Command Value | Operation | Description |
|---|---|---|
| `2` | **Idle / Active RX** | Firmware is actively receiving frames and streaming them over gSPI Function 2. Look for `host_cmd == 2` to confirm completion of commands `4`, `5`, or `6`. |
| `3` | **Stop Transceiver** | Cleanly stops MAC/PSM and D11 RX DMA, populates diagnostic snapshots in `MBOX`, and transitions `fw_state` to `FW_STATE_RX_DONE` (`10`). |
| `4 \| (ch << 8)` | **Fast Channel Switch** | Tunes Radio 20691 and LCN20 PHY to 2.4 GHz channel `ch` (`1..14`) in ~3.5 ms without re-running cold calibration, then resumes reception. |
| `5 \| clean_tx_length_bits(total_len) \| (is_ofdm << 18) \| (cnt << 19)` | **Transmit 802.11 Frame** | Accepts `total_len = 6 + mpdu_len`, staged at `FW_TX_BUF_ADDR` (`0x0004a400`), repeated `1..255` times. CCK accepts MPDUs `14..2400` and uses long preamble at 1/2/5.5/11 Mbps; OFDM accepts `10..2400`. Use `clean_cck_plcp_rate` or `clean_ofdm_plcp_rate` in `phy_rates.h` for matching length/rate headers. Hardware CCMP remains unqualified. |
| `6 \| (enable << 8)` | **Install AES-128-CCMP Key** | Copies the 16-byte Temporal Key (`TK`) staged at `FW_TX_BUF_ADDR` (`0x0004a400`) into `SHM 0x618..0x61f` and sets `SHM 0x615 = enable` (`1` = enable hardware CCMP TX/RX, `0` = disable). |
| `7` | **Enable experimental station MAC** | Stage 16 bytes: local MAC at 0, pinned peer at 6, little-endian basic-rate mask at 12, short-slot flag at 14 and profile at 15. Profile 3 selects PSM/IFS/TSF-clocked DIFS/backoff/EIFS and final TX trigger; profile 2 retains Cortex DCF/NAV. Both provide experimental reliable unicast and PSM ACK/CTS handling. Profile 1 retains the older ACK experiment. All require TX enabled and unicast, nonzero addresses. Reconfiguration with command 7 is rejected. |
| `8` | **Queue station rate/slot policy** | Stage the same 16-byte configuration with unchanged bound addresses and active profile (2 or 3). Updates response rates and slot policy at idle RX/PHY without resetting pending TX identity, NAV or receive transport. Completion acknowledges queueing; `mac_policy_pending` reports whether application is still pending. |
| `9 \| (count << 8)` | **Test-only bounded missing ACKs** | Requires enabled profile 3 and `count` 0..16 with no reserved high bits. Atomically resets the actual-drop counter and arms suppression of the next `count` otherwise eligible, valid-FCS local-RA/pinned-TA ACK responses. Zero cancels suppression. Invalid input/profile reports firmware error `0x85`. No ordinary station caller enables it. |
| `10 \| clean_tx_length_bits(total_len) \| (cnt << 19)` | **Transmit short-preamble DSSS/CCK** | Same length/count/reliability flags and staging format as command 5, but requests short preamble at 2/5.5/11 Mbps. Short 1 Mbps or short OFDM requests report error `0x83`. No command-5 flag bit is repurposed. |

With station MAC enabled, commands 4 and 6 and TX rearm/source-match overrides
are rejected rather than discarding held captures. Profile 2 requires reliable
unicast TX: set `CLEAN_TX_ACK_REQUIRED`, use at most eight attempts, and provide
a valid-FCS, bound-peer management or non-QoS ToDS data MPDU. Supported templates
are legacy OFDM or validated DSSS/CCK at all four rates; short preamble requires
command 10 and is forbidden at 1 Mbps. This does not change station association
rate negotiation or establish b-only AP compatibility. Inspect the separate
ACK status at `0x201e0`: a `BUSY` result with zero attempts is a deferral, not
a radio transmission or retry.

Command 9 does not suppress CTS, incoming ACK consumption or application
delivery, and invalid/unbound receptions do not spend its budget. The report
field `mac_ack_dropped` counts actual suppressed responses, not a promise that
all configured responses were received. See the
[continuous fixture and controlled-loss evidence](../badge/README.md#continuous-raw-mac-fixture).

MAC retransmissions retain sequence/fragment identity and protected bytes/PN.
After a request is MAC-ACKed, a management response timeout instead starts a
new request with a fresh sequence and clear Retry bit; resending the completed
MPDU as a duplicate can prevent AP recovery. This does not enlarge the bounded
authentication/association retry budget or phase deadline.

---

## 5. Receiving 802.11 Frames on the RP2350 (gSPI Function 2)

Capture-eligible 802.11 frames with good hardware CRC-32 FCS are published to
the SDIOD Function 2 TX DMA ring. Station profiles 2 and 3 consume local ACK control
frames internally and rearms directly after an accepted RTS/CTS response;
those control frames are not application data. A completed station capture
is held while host credit is occupied, rather than silently discarded.
To poll and read incoming frames on the RP2350:

1. Read the 32-bit gSPI Function 0 status register at `0x0008`:
   ```c
   uint32_t st = f0_read32(0x0008u);
   if ((st & 0x100u) == 0u) return; /* Bit 8 = F2 packet available */
   uint32_t pkt_len = (st >> 9u) & 0x7ffu;
   ```
2. Issue a gSPI Function 2 burst read (`fn = 2`, `addr = 0`, `len = (pkt_len + 3) & ~3`) into an RP2350 buffer `buf[]`.
3. Parse the 18-byte **SDPCM + BDC header**:
   - `uint16_t sdpcm_len = buf[0] | (buf[1] << 8);`
   - `uint16_t sdpcm_chk = buf[2] | (buf[3] << 8);` (verify `(sdpcm_len ^ sdpcm_chk) == 0xffff`)
   - `uint8_t data_offset = buf[7];` (`14`)
   - `uint8_t plcp_rate = buf[15];` (CCK or OFDM rate byte from `PLCP[0]`)
   - `uint8_t channel = buf[16] & 0x0fu;`
   - `int is_ofdm = (buf[16] & 0x10u) != 0u;`
   - `int short_preamble = (buf[16] & CLEAN_RX_SHORT_PREAMBLE) != 0u;` (`0x20`, CCK).
   - `int unsupported_phy = (buf[16] & CLEAN_RX_UNSUPPORTED_PHY) != 0u;` (`0x40`).
   - **802.11 MPDU bytes** start at `&buf[18]` with length `mpdu_len = sdpcm_len - 18u` (including the trailing 4-byte FCS).

For OFDM, use `clean_ofdm_rate_index(plcp_rate)` from `phy_rates.h`:
only the low RATE nibble identifies Mbps; the upper nibble also contains
LENGTH bits. The controller derives the OFDM flag from DMA PHY status or
RXE encoding bit 13, not the unrelated RXE bits 9..10.
For DSSS/CCK, SIGNAL values `10/20/55/110` identify 1/2/5.5/11 Mbps.
The preamble flag comes from actual receiving PHY status bit 7, not
the transmitter command or expected fixture setting. Both physical transport
fragments must agree on rate, channel and preamble metadata.
The controller snapshots the held PSM header at SHM `0x7f6`, not a potentially
lagging DMA descriptor's header, so status belongs to the published capture.

The source-built controller additionally uses the two SDPCM padding bytes
`buf[12..13]` for optional RX publication-phase metadata. The little-endian word
masked with `0xff7f` equals `CLEAN_RX_PHASE_MAGIC` (`0x5258`); bit 7 of byte 12
is excluded from that match and marks publication after a successful explicit
TX command. Thus the complete words are `0x5258` before and `0x52d8` after TX.
The tag remains set until controller reset. BDC offset byte 17 remains zero;
the MPDU offset and packet lengths are unchanged. Older compatible images
without this tag remain readable, but cannot provide phase-qualified counts.

The badge trace stores pre-command packet count at 2016, controller-tagged
post-TX count at 2020, its valid-FCS subset at 2024, first tagged delivery delay
in RP2350 timer ticks at 2028, matching AP management frames at 2032, matching
AP beacons at 2036, all packets delivered during the response window at 2040,
and observed phase-tag support at 2044. AP counts require matching transmitter
and BSSID. The delay measures host delivery, not RF arrival or SIFS readiness.
A normal TX resume may finish one previously started PSM frame; multiple
post-TX AP beacons, or the explicit rearm experiment, avoid treating that one
frame as proof of continued reception. These counters never establish OTA TX
without the separate fresh-MAC ACK or bound management-reply evidence.

### Hardware SIFS Auto-ACK on Unicast Reception
When the firmware receives a Management (`type == 0`) or Data (`type == 2`) frame whose Receiver Address (`RA`, `mpdu[4..9]`) matches the station MAC stored in `SHM 0x612..0x614`:
- Default MAC (`CFG_STA_ROLE == 0`): `02:9b:bf:88:dd:7d`
- Alternate STA MAC (`CFG_STA_ROLE != 0`): `02:08:da:57:f5:ea`

The firmware automatically transmits a 20-byte 11 Mbps CCK `802.11 ACK` (`FC = 0x00d4`) back to the transmitter (`TA = mpdu[10..15]`) before delivering the frame over Function 2. You can also program any custom 6-byte MAC address into `SHM 0x612..0x614` via D11 object writes.

---

## 6. Transmitting 802.11 Frames from the RP2350

To transmit an 802.11 frame from the RP2350:

1. Construct a 6-byte PLCP header followed by the IEEE 802.11 MPDU (`mpdu_no_fcs`) and its 4-byte IEEE 802.11 CRC-32 FCS (`total_len = 6 + mpdu_len`):
   ```c
   /* Example: 11 Mbps CCK PLCP header (rate = 0x6e = 110 * 100 kbps) */
   static void build_cck_plcp(uint8_t plcp[6], uint32_t mpdu_len_with_fcs) {
       plcp[0] = 0x6eu; /* 11 Mbps */
       plcp[1] = 0x04u; /* Locked clocks */
       uint32_t bits = mpdu_len_with_fcs * 8u;
       uint32_t us = (bits + 10u) / 11u;
       if (us * 11u - bits >= 8u) plcp[1] |= 0x80u; /* Length extension bit */
       plcp[2] = (uint8_t)(us & 0xffu);
       plcp[3] = (uint8_t)((us >> 8u) & 0xffu);
       /* LSB-first CRC-16 over plcp[0..3], complemented, little-endian. */
       uint16_t crc = 0xffffu;
       for (uint32_t i = 0u; i < 4u; ++i) {
           crc ^= plcp[i];
           for (uint32_t b = 0u; b < 8u; ++b) {
               crc = (uint16_t)((crc >> 1u) ^ ((crc & 1u) ? 0x8408u : 0u));
           }
       }
       crc = (uint16_t)~crc;
       plcp[4] = (uint8_t)crc;
       plcp[5] = (uint8_t)(crc >> 8u);
   }
   ```
   For an 88-byte MPDU including FCS, this produces `6e 04 40 00 7f ee`.
   Badge diagnostics test both padded 94-byte templates and an exact-length
   40-byte authentication template (including PLCP). Neither length has
   established AP reception in the current cold-source test.
2. Write the frame into `FW_TX_BUF_ADDR` (`0x0004a400`) using 32-bit backplane writes:
   ```c
   for (uint32_t off = 0u; off < total_len; off += 4u) {
       bp_write(0x0004a400u + off, 4u, load_le32(frame + off));
   }
   ```
3. Trigger transmission via `MBOX->host_cmd` (`0x00020018`):
   ```c
   uint32_t cmd = 5u | clean_tx_length_bits(total_len) | (1u << 19u); /* 1 repetition, CCK; tx_diag.h */
   bp_write(0x00020018u, 4u, cmd);
   ```
   Poll for `host_cmd == 2` under a host deadline, checking `fw_state` for
   ERROR (`255`) during the wait. Do not retry or report success after an error
   or timeout. Then require an independent valid-FCS response to the fresh
   source MAC; command completion alone is not over-the-air confirmation.

---

## 7. Hardware WPA2-PSK (`AES-128-CCMP`) Encryption & Decryption

The CYW43439 D11 core includes a dedicated hardware `AES-128-CCMP` engine driven directly by `cyw43_rx_fw.bin`:

1. **4-Way EAPOL Handshake:**
   - Associate with the AP using an RSN Information Element advertising `CCMP-128` (`00-0F-AC:4`) and exchange `EAPOL-Key` Messages `1/4..4/4` as normal unencrypted 802.11 Data / QoS Data frames (`FC = 0x0108` or `0x0188`).
2. **Install the 128-bit Temporal Key (`TK`):**
   - After deriving `PTK = PRF-384(PMK, "Pairwise key expansion", min(AA,SPA) || max(AA,SPA) || min(ANonce,SNonce) || max(ANonce,SNonce))`, write the 16-byte `TK` (`PTK[32..47]`) to `FW_TX_BUF_ADDR` (`0x0004a400`) and issue `host_cmd = 6 | (1 << 8)`:
     ```c
     for (uint32_t off = 0u; off < 16u; off += 4u) {
         bp_write(0x0004a400u + off, 4u, load_le32(tk + off));
     }
     bp_write(0x00020018u, 4u, 6u | (1u << 8u));
     while (bp_read32(0x00020018u) != 2u) { /* wait for key install */ }
     ```
3. **Hardware CCMP TX (`FC | 0x4000`):**
   - Set the Protected Frame bit (`0x4000`) in Frame Control (`0x4108` for Data or `0x4188` for QoS Data).
   - Insert the 8-byte CCMP IV (`PN0, PN1, 0x00, 0x20, PN2, PN3, PN4, PN5`) immediately after the 24-byte (or 26-byte QoS) 802.11 MAC header, followed by the plaintext LLC/SNAP payload, an 8-byte zero MIC placeholder, and a 4-byte FCS placeholder.
   - Transmit via `host_cmd = 5`: the D11 Hardware Crypto Engine encrypts the payload in-place, computes and writes the 8-byte `AES-CCMP` MIC, and regenerates the 4-byte FCS on the fly.
4. **Hardware CCMP RX (`FC & 0x4000`):**
   - Incoming Protected Data (`0x4208`) and QoS Data (`0x4288`) frames are decrypted in-place in D11 SHM by the PSM microcode (`WEP_CTL = 0x110b`) before being delivered over gSPI Function 2.
   - In the received MPDU on the RP2350, the plaintext LLC/SNAP header (`aa aa 03 00 00 00 ...`) immediately follows the 8-byte CCMP IV (at byte offset `32` for non-QoS Data or `34` for QoS Data), followed by the decrypted payload, 8-byte verified MIC, and 4-byte FCS.
