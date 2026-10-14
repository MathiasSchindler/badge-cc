# Integrating `clean-rx` CYW43439 Firmware with an RP2350 Host

This guide explains how an RP2350 host application loads, boots, and communicates with the open-source `cyw43_rx_fw.bin` transceiver firmware over the 4-pin gSPI bus.

The repository now includes a working [freestanding badge host](../badge/README.md)
with build commands and physical results from 2026-10-14. Cold beacon reception
and Function-2 delivery have been verified on a badge; over-the-air TX and WPA2
connectivity have not. `CFG_TX_ENABLE` must be set for explicit TX or automatic
ACKs; a command-5 request with TX disabled reports firmware error `0x81`.

---

## 1. Building and Embedding the Firmware Binary

First, build `build/cyw43_rx_fw.bin` from `clean-rx/src/`:

```bash
make -C clean-rx/src
```

This produces a ~19 KiB raw ARM Cortex-M3 binary (`clean-rx/src/build/cyw43_rx_fw.bin`) linked to execute at CYW43439 SRAM address `0x00000000`.

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
1. Zero the mailbox (`0x00020000`, `448` bytes) and multi-frame capture ring (`0x00020d80`, `4584` bytes).
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
5. Poll `MBOX->fw_state` (`0x00020004`) until it reaches `FW_STATE_PSM_READY` (`8`). At this point, the D11 core, Radio 20691, LCN20 PHY, and 295-instruction PSM microcode are calibrated and ready.
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
| `5 \| (total_len << 8) \| (is_ofdm << 18) \| (cnt << 19)` | **Transmit 802.11 Frame** | Transmits the raw 802.11 frame (`total_len = 6 + mpdu_len` bytes, `16..512`) staged at `FW_TX_BUF_ADDR` (`0x00038400`) `cnt` times (`1..255`), using hardware `AES-128-CCMP` encryption if enabled and `(FC & 0x4000) != 0`. |
| `6 \| (enable << 8)` | **Install AES-128-CCMP Key** | Copies the 16-byte Temporal Key (`TK`) staged at `FW_TX_BUF_ADDR` (`0x00038400`) into `SHM 0x618..0x61f` and sets `SHM 0x615 = enable` (`1` = enable hardware CCMP TX/RX, `0` = disable). |

---

## 5. Receiving 802.11 Frames on the RP2350 (gSPI Function 2)

Every valid 802.11 frame (with good hardware CRC-32 FCS) is automatically pushed by the firmware to the SDIOD Function 2 TX DMA ring. To poll and read incoming frames on the RP2350:

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
   - **802.11 MPDU bytes** start at `&buf[18]` with length `mpdu_len = sdpcm_len - 18u` (including the trailing 4-byte FCS).

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
       /* 16-bit CCITT CRC over plcp[0..3] in plcp[4..5] */
       uint16_t crc = 0xffffu;
       for (uint32_t i = 0u; i < 4u; ++i) {
           uint8_t d = plcp[i];
           for (uint32_t b = 0u; b < 8u; ++b) {
               uint16_t mix = (crc ^ ((uint16_t)d << 8u)) & 0x8000u;
               crc = (uint16_t)((crc << 1u) ^ (mix ? 0x1021u : 0u));
               d >>= 1u;
           }
       }
       crc = (uint16_t)~crc;
       uint16_t rev = 0u;
       for (uint32_t b = 0u; b < 16u; ++b)
           if (crc & (1u << b)) rev |= (uint16_t)(1u << (15u - b));
       plcp[4] = (uint8_t)(rev & 0xffu);
       plcp[5] = (uint8_t)(rev >> 8u);
   }
   ```
   *(Note: For unencrypted frames (`(FC & 0x4000) == 0`), pad `mpdu_no_fcs` to at least 84 bytes before appending the 4-byte FCS so `total_len >= 94` bytes).*
2. Write the frame into `FW_TX_BUF_ADDR` (`0x00038400`) using 32-bit backplane writes:
   ```c
   for (uint32_t off = 0u; off < total_len; off += 4u) {
       bp_write(0x00038400u + off, 4u, load_le32(frame + off));
   }
   ```
3. Trigger transmission via `MBOX->host_cmd` (`0x00020018`) and wait for `host_cmd == 2`:
   ```c
   uint32_t cmd = 5u | (total_len << 8u) | (1u << 19u); /* 1 repetition, CCK */
   bp_write(0x00020018u, 4u, cmd);
   while (bp_read32(0x00020018u) != 2u) { /* wait ~200-500 us */ }
   ```

---

## 7. Hardware WPA2-PSK (`AES-128-CCMP`) Encryption & Decryption

The CYW43439 D11 core includes a dedicated hardware `AES-128-CCMP` engine driven directly by `cyw43_rx_fw.bin`:

1. **4-Way EAPOL Handshake:**
   - Associate with the AP using an RSN Information Element advertising `CCMP-128` (`00-0F-AC:4`) and exchange `EAPOL-Key` Messages `1/4..4/4` as normal unencrypted 802.11 Data / QoS Data frames (`FC = 0x0108` or `0x0188`).
2. **Install the 128-bit Temporal Key (`TK`):**
   - After deriving `PTK = PRF-384(PMK, "Pairwise key expansion", min(AA,SPA) || max(AA,SPA) || min(ANonce,SNonce) || max(ANonce,SNonce))`, write the 16-byte `TK` (`PTK[32..47]`) to `FW_TX_BUF_ADDR` (`0x00038400`) and issue `host_cmd = 6 | (1 << 8)`:
     ```c
     for (uint32_t off = 0u; off < 16u; off += 4u) {
         bp_write(0x00038400u + off, 4u, load_le32(tk + off));
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
