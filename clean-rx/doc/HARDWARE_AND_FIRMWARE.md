# CYW43439 Hardware Architecture and Transceiver Firmware Reference

## 1. Chip Architecture Overview

The CYW43439 is a 2.4 GHz IEEE 802.11b/g/n wireless LAN system-on-chip built around an internal interconnect backplane. The chip contains five primary functional cores connected to the backplane, each with a register block in the `0x18000000` range and a corresponding core-control wrapper in the `0x18100000` range:

| Core | Register Base | Wrapper Base | Primary Function |
|---|---|---|---|
| **ChipCommon (`CC`)** | `0x18000000` | `0x18100000` | Power Management Unit (PMU), PLLs, clock control, and GCI coexistence registers |
| **802.11 MAC (`D11`)** | `0x18001000` | `0x18101000` | 802.11 MAC, Programmable State Machine (PSM), Receive Engine (RXE), Transmit Engine (TXE), Template RAM, Shared Memory (SHM), D11 DMA, and indirect bridges to the LCN20 PHY and Radio 20691 |
| **SDIOD (`SDIO/gSPI`)** | `0x18002000` | `0x18102000` | Host bus interface (Function 0/1/2) and host-facing DMA engines |
| **ARM Cortex-M3** | `0x18003000` | `0x18103000` | 32-bit Thumb-2 processor executing the system firmware from SRAM |
| **SOCRAM** | `0x18004000` | `0x18104000` | 512 KiB on-chip SRAM controller (`0x00000000..0x0007ffff`) and mask ROM interface (`0x00800000` / `0x1e000000`) |

### Receive and Transmit Data Path Overview

```text
RECEIVE PATH:
2.4 GHz Antenna
  -> RF Switch (granted to Wi-Fi via D11 +0x6b4 / +0x6b8 and GCI 0x18000d64 / 0x18000d74)
  -> Radio 20691 (RF synthesizer, LNA, mixer, TIA, LPF, ADC)
  -> LCN20 PHY (AGC, DC offset cancellation, Farrow resampler, CCK + OFDM demodulator)
  -> D11 Receive Engine (RXE)
       |-- Streams PLCP + MPDU bytes into D11 Shared Memory (SHM 0x6a0..0x79f)
       +-- Publishes 24-byte receive header (SHM 0x7f4..0x7ff) to D11 RX FIFO
  -> D11 PSM Microcode (Arch15 64-bit engine)
       |-- Waits for PLCP valid (ext 14), RX complete (ext 6), and Good FCS (ext 3)
       |-- Populates 24-byte d11rxhdr and triggers RX FIFO publication
       +-- Signals frame capture (SHM[0x601] = 14) and rearms on acknowledgment
  -> ARM Cortex-M3 Firmware
       |-- Inspects RA against station MAC (SHM 0x612..0x614) and transmits 802.11 ACK if unicast
       |-- Reclaims D11 RX DMA descriptor (0x18001220..0x18001234)
       |-- Reads MPDU from SHM (0x6a0..0x79f) and records frame slot in SRAM (0x00020d80)
       +-- Wraps MPDU in an 18-byte SDPCM + BDC header and posts to SDIOD Function 2 TX DMA (0x18002200)

TRANSMIT PATH:
Host TX Staging Buffer (0x00038400) or Unicast Auto-ACK Generator
  -> ARM Cortex-M3 Firmware (d11_transmit_template_frame / d11_transmit_ack_frame)
       |-- Pauses MAC/PSM, configures LCN20 PHY TX Farrow resampler & TX IIR filters
       |-- Enables Radio 20691 2.4 GHz PA and releases LCN20 PHY TX overrides (phy_tx_set_inhibit(0))
       |-- Writes 32-bit words (6-byte PLCP + MPDU + FCS) into D11 Template RAM (0x18001130 / 0x18001134)
       |-- Programs D11 Hardware Crypto Engine (0x180017c0..0x180017d6) for AES-128-CCMP encryption + 8B MIC when Protected bit (FC & 0x4000) is set
       +-- Triggers D11 Transmit Engine (TXE 0x18001500..0x18001572 / 0x180017c0)
  -> LCN20 PHY (RFSEQ TX power-up sequencer, CCK/OFDM modulator, TX IIR filter, TX Farrow resampler, DAC)
  -> Radio 20691 (TX LPF, 2.4 GHz upconverter mixer, internal 2.4 GHz PA, T/R switch)
  -> 2.4 GHz Antenna
```

---

## 2. Memory Map and Firmware Layout

The CYW43439 provides 512 KiB of contiguous SRAM mapped at `0x00000000..0x0007ffff`. The firmware uses a deterministic static layout with no dynamic memory allocation:

| Address Range | Size | Usage |
|---|---|---|
| `0x00000000..0x000000ff` | 256 B | ARM Cortex-M3 interrupt vector table (`.vectors`) |
| `0x00000100..0x00013fff` | ~18 KiB | Firmware executable code, read-only tables, and PSM microcode image (`.text`, `.rodata`) |
| `0x00014000..0x0001bfff` | ~2 KiB | Zero-initialized firmware state (`.bss`) |
| `0x0001f000` | — | Initial Main Stack Pointer (`MSP`, growing downward) |
| `0x00020000..0x000201bf` | 448 B | Host/Firmware Control Mailbox (`fw_mbox_t`) |
| `0x00020d80..0x00021f67` | 4,584 B | Multi-Frame Capture Ring (`fw_multi_t`, 8 frame slots x 568 B) |
| `0x00022000..0x000221ff` | 512 B | D11 RX DMA Descriptor Ring (32 descriptors x 16 B, 8-KiB aligned) |
| `0x00022200..0x0002fa7f` | 54,912 B | D11 RX DMA Packet Buffers (32 buffers x 1,716 B) |
| `0x00030000..0x000301ff` | 512 B | SDIOD Function 2 TX DMA Descriptor Ring (32 descriptors x 16 B, 8-KiB aligned) |
| `0x00030200..0x000381ff` | 32,768 B | SDIOD Function 2 TX DMA Packet Buffers (32 buffers x 1,024 B) |
| `0x00038400..0x000385ff` | 512 B | 802.11 Transmit Frame & Crypto Key Staging Buffer (`FW_TX_BUF_ADDR`, 64-byte aligned) |
| `0x1e09020c..0x1e090a07` | 2,044 B | On-chip ROM calibration dataset (TIA, gain tables, and common PHY tables) |

---

## 3. Core Wrapper Reset and Clock Protocol

Each core on the backplane has an AI wrapper with two primary control registers:
- **`IOCTRL` (`wrapper_base + 0x408`)**: Controls core clock gating and core-specific mode bits.
- **`RESETCTRL` (`wrapper_base + 0x800`)**: Bit 0 asserts (`1`) or releases (`0`) core reset.

### Cortex-M3 Startup (`startup.S`)
When the host releases the ARM Cortex-M3 wrapper (`0x18103800 = 0`), the core fetches its initial `MSP` (`0x0001f000`) from `0x00000000` and its reset vector (`cyw43_reset`) from `0x00000004`. `cyw43_reset` masks interrupts (`cpsid i`), sets `VTOR` (`0xe000ed08 = 0`), zeroes `.bss`, and calls `cyw43_main()`.

If any CPU fault occurs, `cyw43_fault` records `fw_state = 0xff`, the active exception number (`IPSR`), `CFSR` (`0xe000ed28`), and `HFSR` (`0xe000ed2c`) into the mailbox at `0x00020000` and halts in `wfi`.

---

## 4. PMU, Crystal, PLL, and Resource Initialization (`pmu.c`)

At reset, the chip runs on the Low-Power / Active-Low-Power (ALP) crystal clock (37.4 MHz). Before initializing the 802.11 MAC, PHY, or Radio, `pmu_init_pll_and_resources()` configures the ChipCommon Power Management Unit (`0x18000600..0x18000664`):

1. **PMU Control & Crystal Frequency Configuration (`0x18000600`):**
   - Sets bit `0x200` in `PMU_CONTROL`, pulses regulator control bank 6 bit `0x00800000` (`PMU_REG_CONTROL_ADDR/DATA` at `0x18000658/0x1800065c`), and programs the 37.4 MHz crystal divisor (`((37400 + 127) / 128 - 1) << 16 | 0x3c`) into `PMU_CONTROL`, yielding `0x012403bd`.
2. **Backplane PLL Table Programming (`0x18000660/0x18000664`):**
   - Temporarily clears the HT clock bits (`0x70000000`) in `PMU_MIN_RES_MASK` (`0x18000618`) and `PMU_MAX_RES_MASK` (`0x1800061c`), waits for HT clock availability (`0x00020000` in `CC_CLK_CTL_ST` `0x180001e0`) to drop, writes the 9-word PLL configuration table (`0x0, 0x0, 0x431, 0xba2e8c, 0x04040404, 0x00040104, 0x080004e2, 0xa, 0x106a`), and commits the PLL update via `PMU_CONTROL |= 0x400`.
3. **PMU Resource Timers and Dependency Masks (`0x18000620..0x18000628`):**
   - Programs 33 resource up/down timing records into `PMU_RES_TABLE_SEL` (`0x18000620`) and `PMU_RES_UPDN_TIMER` (`0x18000628`).
   - Programs 31 resource dependency masks (`0..30`) into `PMU_RES_DEP_MASK` (`0x18000624`).
   - Installs `PMU_MIN_RES_MASK = 0x0ec23df7` and `PMU_MAX_RES_MASK = 0x7fffffff`, then waits for `PMU_RES_PENDING` (`0x18000610`) to settle to `0`.
4. **High-Throughput (HT) Clock Handshake:**
   - The firmware sets `fw_state = FW_STATE_WAIT_HT` (`3`) and waits for the host to request the HT clock on gSPI Function 1 (`0x1000e = 0x10`) and set `host_cmd = 1`, verifying that `CC_CLK_CTL_ST` (`0x180001e0`) asserts bits `0x00030000` (HT clock available).

---

## 5. D11 Core Release, Radio 20691, and LCN20 PHY Calibration (`radio_phy.c`)

### 5.1 Indirect PHY, Radio, and Table Access Ports
The LCN20 PHY and Radio 20691 registers are accessed indirectly through D11 bridge ports:
- **Radio 20691 Registers (`0x180013d8` selector / `0x180013da` data):**
  - 16-bit write to `0x180013d8` selects the radio register index; 16-bit read or write at `0x180013da` transfers the register value.
- **LCN20 PHY Registers (`0x180013fc` selector / `0x180013fe` data):**
  - Reads write the 16-bit register index to `0x180013fc` and read `0x180013fe`.
  - Writes can issue a single 32-bit packed write `(value << 16) | index` to `0x180013fc`, or write the selector to `0x180013fc` followed by a 16-bit write to `0x180013fe`.
- **LCN20 PHY Hardware Tables (`PHY 0x00d, 0x00e, 0x456, 0x457`):**
  - While transferring table entries, bit 1 (`0x0002`) of `PHY 0x9a5` is set and restored afterward.
  - `PHY 0x00d` selects the Table ID; `PHY 0x00e` selects the starting entry index.
  - For 8-bit and 16-bit tables, each entry is written to `PHY 0x456` (which auto-increments the entry index). For 32-bit tables, the high 16 bits are written to `PHY 0x457` first, followed by the low 16 bits to `PHY 0x456`.

### 5.2 D11 Core & PHY Release and TX-Inhibit Guards
`d11_release_core_and_phy()` releases D11 reset (`0x18101800 = 0`) with `IOCTRL = 0x0d`, sets stopped `MACCONTROL = 0x04000404`, and steps `IOCTRL` (`0x18101408`) through `0x0d -> 0x4d -> 0x43 -> 0x45` to release the PHY reset.

Immediately upon PHY release (and after every calibration stage), the firmware enforces six hardware TX-inhibit overrides so the transmitter and power amplifiers remain disabled:
- `PHY 0x44d` bit `0x4000 = 0`, `PHY 0x44c` bit `0x4000 = 1` (TX chain override)
- `PHY 0x939` bit `0x0800 = 0`, `PHY 0x938` bit `0x0800 = 1` (Internal PA override)
- `Radio 0x79` bit `0x0001 = 0`, `Radio 0xa4` bit `0x0004 = 1` (Radio PA override)

### 5.3 ROM Calibration Tables and Defaults
The firmware reads the 2,044-byte calibration block from on-chip ROM at `0x1e09020c` and programs:
- **Common PHY Tables:** Table 2 (`128 x 16-bit`), Table 1 (`64 x 16-bit`), Table 20 (`256 x 8-bit`), Table 23 (`154 x 16-bit`), and Table 0 (`108 x 16-bit`).
- **RX Gain Tables:** Table 17 (`26 x 8-bit`), Table 18 (`96 x 32-bit`), and Table 13 (`76 x 32-bit`).

### 5.4 Radio 20691 Power-Up, PHYPLL Activation, and RC/ADC/TIA Calibration
1. **MACPHY Clock & GMODE (`0x18101408 = 0x2055`):** Enables MACPHY clock control (`0x0010`) and `GMODE` (`0x2000`, enabling both 802.11g/n OFDM and 802.11b CCK demodulation).
2. **PHYPLL Request (`0x180011e0 = 0x240`):** Requests the RF/PHY PLL resource and polls `PMU_RES_STATE` (`0x1800060c` mask `0x3eca3df7`) and `D11_CLK_CTL_ST` (`0x180011e0` bit `0x02000000`).
3. **Two-Pass RC Calibration:** Runs pass 0 (LPF calibration) and pass 1 (ADC calibration) by triggering `Radio 0x8f` bit 0, waiting for `Radio 0x91` bit 0, and reading the oscillator counts `n0` (`Radio 0x92`) and `n1` (`Radio 0x93`):
   $$\text{rc\_val} = \frac{K \cdot (n_1 - n_0)}{37400000 \gg 12}, \quad K_0 = 101541,\; K_1 = 102051$$
4. **ADC & TIA Programming:** Computes the 11 ADC tuning coefficients from `rc_adc`, writes them to `Radio 0x20..0x2a`, and writes the 108 TIA calibration values from ROM into `Radio 0xa9..0x114`.

### 5.5 Multi-Channel Synthesizer Tuning, VCO, Farrow Timing, DC, and AGC
To tune to any 2.4 GHz channel $c \in [1, 14]$ with center frequency $f = 2407 + 5c\text{ MHz}$ ($2484\text{ MHz}$ for channel 14):
1. **Radio 20691 Synthesizer & VCO Lock:**
   - Writes the 31 channel-specific words from `radio20691_2g_channels[c - 1]` to `Radio 0x62, 0x53, 0x54, ..., 0x86`, clears band bits in `Radio 0x3d/0x71/0x72`, triggers VCO calibration (`Radio 0x8a/0x51/0xa0/0x42`), and polls `Radio 0x65` bit 0 until VCO lock is achieved.
2. **LCN20 Farrow Resampler & Frequency Drift Timing:**
   - Computes integer resampler parameters for frequency $f$ (in MHz) and programs `PHY 0x9ad, 0x9ae, 0x9b1, 0x9ac, 0x657, 0x658`:
     $$\mu = \left\lfloor \frac{f \cdot 65536 + 2}{5} \right\rfloor, \quad P_{\text{drift}} = \frac{1280}{\gcd(1280, f)}, \quad C_{20} = \left\lfloor \frac{\lfloor 4f/5 \rfloor + 1}{2} \right\rfloor, \quad C_{\text{fac}} = \left\lfloor \frac{\lfloor 5242880/f \rfloor + 1}{2} \right\rfloor$$
3. **DC Offset & AGC Calibration:**
   - Pulses `PHY 0x44a` bit 0 up to 10 times until `PHY 0x978` (`IDAC`, low 8 bits `0xff`) and `PHY 0x96f` (`DCOE`, low 12 bits `0xfff`) complete, then applies the AGC/ACI register tables and clears PHY deaf (`PHY 0x440` bit 0 = `1`).

---

## 6. D11 Hardware Prerequisites for Signal Reception (`main.c`)

Even after the Radio and PHY are calibrated, the D11 core requires four hardware register configurations in `d11_configure_mac_and_rxe()` before frames can reach the PSM and RXE:

1. **2.4 GHz RF Switch / Coexistence Antenna Grant (`0x180016b4` & `0x180016b8`):**
   - The 2.4 GHz antenna switch is shared between Wi-Fi and Bluetooth. Setting `0x180016b4 |= 0x0003` and `0x180016b8 |= 0x00c0` (along with `0x1800149e |= 0x00ff`, `0x1800149c = (old & ~0xff) | 4`, `0x180016f0 = 0x00c2`, `0x180016f2 |= 0x00c0`) connects the 2.4 GHz RF switch to the Wi-Fi LNA. Without these writes, the antenna stays disconnected and no PLCP headers are ever detected.
2. **PSM External Condition Masks (`BRED0` `0x180014a0 = 0xf3f1`, `BRPO0` `0x180014b0 = 0xfdef`):**
   - Configures edge-detection (`BRED0..3`) and polarity (`BRPO0..3`) for the D11 PSM external branch conditions (`ext 3` Good FCS, `ext 5`, `ext 6` RX complete, `ext 11` RX FIFO busy, `ext 13` Bad PLCP/overflow, `ext 14` PLCP valid).
3. **RXE Scratch & Header Framing (`0x18001402..0x180014fe`):**
   - Sets RXE SHM scratch base `0x18001402 = 0x06a0` (`spr:0x001`), scratch byte capacity `0x18001404 = 0x0200` (`512` bytes, `spr:0x002`), RXE control `0x18001448 = 0x0c02` (`spr:0x024`), and RXE framing `0x18001450 = 0x4e01`, `0x18001452 = 0x015b`, `0x180014e4 = 0x0090`.
4. **Radio 20691 LPF Bandwidth Scaling (`Radio 0xa3, 0x71..0x77`):**
   - Scales the Radio 20691 baseband filter registers `0x72..0x76` by the RC-calibrated `rc_lpf` multiplier (`(coeff * rc_lpf) >> 15`).

---

## 7. D11 PSM Microcode (`psm_ucode.c`)

The D11 Programmable State Machine executes 295 64-bit Arch15 instructions (`2,360` bytes) loaded via the D11 object port (`0x18001160` word selector `0..589`, `0x18001164` 32-bit data).

### PSM State Machine Lifecycle
1. **Initialization & RXE Flush (`entry` -> `mac_start`):**
   - Zeroes SHM mailbox (`0x600..0x60f`), diagnostics (`0x7a0..0x7af`), and `d11rxhdr` (`0x7f4..0x7ff`).
   - Verifies the 32-bit session generation token in `shm:0x60c..0x60d`.
   - Sets `spr:0x001 = 0x6a0` (SHM base) and `spr:0x002 = 512` (512-byte max frame capture).
   - Requests PHY/MAC core access (`spr:0x078` bit 5 = `1`, waits for bit 13 = `1`).
   - Flushes RXE (`spr:0x02a = 0`, `spr:0x024 = 0x0c02`, `spr:0x004 = 0x14`, waits for `spr:0x004` bits 2 and 4 to clear), drains stale external conditions (`ext 3, 5, 6, 11, 13, 14`), writes `shm:0x601 = 13` (`PSM_STATE_READY`), and waits for `MACCONTROL` `EN_MAC` (`ext 36`).
   - Initial flush uses a bounded 65,535-iteration countdown. The former 255-poll
     bound faulted while RXCTL was still `0x0804`; a later hardware sample
     confirmed completion (`0x0800`). Only the counter-load instruction changed;
     the completion/fault branches and outer ARM deadline remain intact.
2. **PLCP Detection & Prefix Copy (`event_start` -> `prefix_seen`):**
   - Polls `ext 14` (PLCP valid). When asserted, issues a 6-byte initial copy (`spr:0x02a = 6`, `spr:0x004 = 0x40`) and waits for `spr:0x00d` bit 12 and `spr:0x00c >= 6`.
   - The 6-byte PLCP header is written by RXE into `shm:0x6a1..0x6a3` (`shm:0x6a0` holds a duplicate of `shm:0x6a1`).
3. **Variable-Length MPDU Streaming, Hardware AES-128-CCMP Decryption, & FCS Verification (`prefix_seen` -> `frame_wait`):**
   - Reads `r2 = spr:0x02b & 0x0fff`, writes `spr:0x02a = r2`, and triggers `spr:0x004` continuation (`field 8, 6, 131, spr:0x004`) to stream the entire MPDU into `shm:0x6a4..0x79f`.
   - If `shm:0x615 != 0` (Hardware CCMP enabled) and the received Frame Control word (`shm:0x6a4`) has the Protected bit (`0x4000`) set:
     - Computes the CCMP IV byte offset (`30` for non-QoS Data when `FC` bit 7 is `0`, or `32` for QoS Data when `FC` bit 7 is `1`) and writes `spr:0x1e1` (`WEP_IVLOC`).
     - Streams the 8 words (16 bytes) of the Temporal Key (`TK`) from `shm:0x618..0x61f` into `spr:0x1e2` (`WEP_KEY`, with `spr:0x1e3 = 0x0110`) and triggers `spr:0x1e0 = 0x110b` (`WEP_CTL`: AES-128-CCMP hardware decrypt + MIC verify).
     - The D11 hardware crypto engine decrypts the payload in-place in `shm:0x6a0..0x79f` and verifies the 8-byte CCMP MIC (`spr:0x00d` bit 3 = `0` on valid MIC).
   - In `frame_wait`, waits for `ext 6` (RX complete) and `spr:0x00c >= 14`, then polls `ext 3` (Good FCS) inside the loop. If `ext 3` asserts, branches to `capture`; if the loop expires or `ext 13`/`ext 5` fires, increments the corresponding discard counter (`shm:0x7a8..0x7ab`) and jumps to `rearm_flush`.
4. **24-Byte `d11rxhdr` Construction & RX FIFO Publication (`capture` -> `wait_arm_ack`):**
   - Populates the 12-word (24-byte) hardware receive header at `shm:0x7f4..0x7ff`:
     - `shm:0x7f4`: `RxFrameSize` (`spr:0x00c`, PLCP + MPDU length in bytes)
     - `shm:0x7f6..0x7f9`: `PhyRxStatus_0..3` (`spr:0x009, 0x00a, 0x01a, 0x01b`)
     - `shm:0x7fa..0x7fc`: 6-byte PLCP header (`shm:0x6a1..0x6a3`)
     - `shm:0x7fe`: `0xda15` marker; `shm:0x7ff`: active 2.4 GHz channel (`shm:0x609`)
   - Publishes the header to the D11 RX FIFO (`spr:0x078` bit 7 = `1`, `spr:0x007 = 0x7f4`, `spr:0x008 = 12`, `spr:0x004` low bits = `1`), waits for `ext 11` (FIFO busy) to assert and clear, resets `spr:0x004` low bits = `0`, sets `shm:0x601 = 14` (`PSM_STATE_CAPTURED`), and waits for the Cortex-M3 to acknowledge by clearing `shm:0x601 = 0` before flushing and rearming for the next frame.

---

## 8. D11 RX DMA and SDIOD Function 2 SDPCM Streaming (`main.c`)

### 8.1 D11 RX DMA Ring (`0x18001220..0x18001234`)
After PSM reaches `PSM_STATE_READY` (ensuring the initial RXE flush is complete), `rx_dma_init()` configures the 32-descriptor ring at `0x00022000` with 1,716-byte buffers at `0x00022200` and enables `D11_RX_DMA_CTL = 0x0004084d` (parity check disabled `0x800`, 38-byte receive offset `38 << 1`, enable `1`). On each frame, `rx_dma_poll_and_reclaim()` reads the DMA-delivered 24-byte `d11rxhdr` from the buffer, clears the buffer, and advances `D11_RX_DMA_PTR`.

### 8.2 SDIOD Function 2 TX DMA Ring (`0x18002200..0x18002214`)
`sdio_f2_init()` configures the 32-descriptor SDIOD Function 2 TX DMA ring at `0x00030000` with 1,024-byte packet buffers at `0x00030200` and enables `SDIOD_TX_DMA_CTL = 0x000a0801`.

For every captured frame, `sdio_f2_send_frame()` constructs an 18-byte **SDPCM + BDC header** followed by the received 802.11 MPDU:

| Byte Offset | Field | Value / Description |
|---|---|---|
| `0..1` | `sdpcm_len` | Total packet length in bytes (`18 + mpdu_len`, little-endian) |
| `2..3` | `~sdpcm_len` | Bitwise complement checksum (`sdpcm_len ^ 0xffff`) |
| `4` | `sequence` | Incrementing SDPCM packet sequence number |
| `5` | `channel` | `2` (Data channel) |
| `6` | `next_len` | `0` |
| `7` | `data_offset` | `14` (offset to BDC header) |
| `8` | `flow_ctrl` | `0` |
| `9` | `data_credit` | `8` |
| `10..13` | `pad` | `0` (2 bytes SDPCM reserved + 2 bytes alignment pad) |
| `14` | `bdc_flags` | `0x20` (BDC version 2) |
| `15` | `plcp_rate` | `PLCP[0]` rate/SIGNAL byte |
| `16` | `ch_mod_meta` | `(channel & 0x0f) \| (is_ofdm ? 0x10 : 0x00)` |
| `17` | `bdc_offset` | `0` |
| `18..` | `mpdu` | Complete IEEE 802.11 MPDU (including 4-byte FCS) |

Posting the descriptor (`ctrl1 = 0xc0000000` for `SOF | EOF`) and advancing `SDIOD_TX_DMA_PTR` (`0x18002204`) causes the SDIOD hardware to assert `packet available` (bit 8) and report the packet length in bits `9..19` of the gSPI Function 0 status register (`0x0008`), allowing the host microcontroller to clock the packet out directly over gSPI Function 2.

---

## 9. 802.11 Frame Transmission and Hardware Auto-ACK (`radio_phy.c`, `main.c`)

### 9.1 Radio 20691 and LCN20 PHY Transmit Path Configuration (`phy_tx_hw_configure`)
Before entering active reception, `phy_tx_hw_configure()` configures the analog and digital transmit path while MAC and PSM are paused (`0x18001120 = 0x84020400`):

1. **GCI External RF Switch Control (`0x18000d64` / `0x18000d74`):**
   - Sets `GCI_CONTROL_REG1` (`0x18000d74 = 0x00f0ff08`) and sets bit 20 (`0x00100000`) of `GCI_OUTPUT_REG1` (`0x18000d64`) to enable the external T/R and PA switch control lines.
2. **Radio 20691 TX Chain, PA Bias, and Baseband LPF:**
   - Programs the Radio 20691 2.4 GHz TX driver, mixer, and PA bias registers (`Radio 0x03 = 0x0092`, `0x0c = 0x0001`, `0x34 = 0x271c`, `0x38 = 0x00a2`, `0x66 = 0x0a68`, `0x7e = 0x610b`, `0x81 = 0x006e`) and the TX baseband low-pass filter (`Radio 0xa3 = 0x201f`, `0x72 = 0x0963`, `0x73 = 0x292c`, `0x74 = 0x05af`, `0x75 = 0x0073`, `0x76 = 0x0738`, `0x77 = 0x0060`).
3. **LCN20 PHY TX Digital Path, Manual Gain, and Table 0 IQ/BBmult:**
   - Configures manual TX gain overrides (`PHY 0x4b5 = 0x0f06`, `0x4fb = 0xff22`, `0x439` DAC gain = `7`, `0x4b0` bit 7 = `1`, `0x43a` bit 6 = `1`, `0x46e = 0`, `0x4a4` bits `15:13 = 0`).
   - Writes identity IQ compensation and baseband multiplier `64` (`0x0040`) into PHY Table 0 (`LCN20PHY_TBL_ID_IQLOCAL`) at offsets `96..99` (`0x0400, 0, 0, 0x0040` for OFDM) and `112..115` (`0x4000, 0, 0, 0x0040` for CCK/BPHY), and enables `Core1TxControl` (`PHY 0x631 = 0x2115`).
4. **LCN20 PHY TX IIR Filters and Channel Farrow Resampler:**
   - `phy_tx_iir_filter_init()` loads the 17 CCK transmit IIR filter coefficients (`PHY 0x910..0x932`) and 17 OFDM transmit IIR filter coefficients (`PHY 0x900..0x911`) and pulses `txSoftReset` (`PHY 0x6d3` bit 7) with `forceTxfiltClkOn` (`PHY 0x6da` bit 7).
   - `phy_tx_farrow_init(channel)` programs the channel-specific transmit Farrow resampler step `mu_delta` (`PHY 0x980..0x985`), `TxResamplerSampSyncVal = freq / gcd(freq, 1920)` (`PHY 0x986`), and `MuInitialLOAD` (`PHY 0x987` bit 1 = `1`).

### 9.2 TX Inhibit and Enable Gate (`phy_tx_set_inhibit`)
- **RX Mode (`phy_tx_set_inhibit(1)`):** Forces `PHY 0x44d/0x44c` (`0x0000 / 0x4000`), `PHY 0x939/0x938` (`0x0000 / 0x0800`), `Radio 0x79` (`0x0000`), and `Radio 0xa4` (`0x0004`) so the TX chain and 2.4 GHz PA are held off.
- **TX Mode (`phy_tx_set_inhibit(0)`):** Enables the internal RF TX chain and LCN20 2G PA (`PHY 0x44d/0x44c` bit `0x4000 = 1`, `PHY 0x939/0x938` bit `0x0800 = 1`), powers on the Radio 20691 2.4 GHz PA and bias (`Radio 0x79 |= 0x7f87`), and releases the manual PA off-override in `Radio 0xa4` (`mask 0x005f, val 0x0004`) so the LCN20 RFSEQ hardware sequencer drives the T/R switch and PA timing across the packet burst.

### 9.3 D11 Template RAM, Hardware AES-128-CCMP Encryption, and Transmit Engine (`TXE`) Operation (`d11_transmit_template_frame`)
To install a 128-bit AES-CCMP Temporal Key (`TK`) into the D11 Hardware Crypto Engine (`cmd = 6 | (enable << 8)` in `MBOX->host_cmd`):
1. The host writes the 16-byte `TK` into `FW_TX_BUF_ADDR` (`0x00038400`) and issues `host_cmd = 6 | (1 << 8)`.
2. The firmware copies the 8 16-bit key words into `SHM 0x618..0x61f` and sets `SHM 0x615 = 1`.

To transmit a raw or hardware-encrypted 802.11 frame (`cmd = 5 | (total_len << 8) | (is_ofdm << 18) | (tx_cnt << 19)` in `MBOX->host_cmd`):
1. The host writes the 6-byte PLCP header followed by the IEEE 802.11 MPDU (including 8-byte CCMP IV and 8-byte zero MIC placeholder if `FC & 0x4000`, plus 4-byte FCS) into `FW_TX_BUF_ADDR` (`0x00038400`).
2. The firmware pauses MAC and PSM (`0x18001120 = 0x84020400`), calls `phy_tx_hw_configure(channel)` and `phy_tx_set_inhibit(0)`, writes `D11_TPL_WR_PTR = 0` (`0x18001130`), and streams the frame in 32-bit little-endian words to `D11_TPL_WR_DATA` (`0x18001134`).
3. The firmware enables `MACCONTROL` with PSM paused (`mac_en & ~2`) and configures the D11 TXE and Hardware Crypto registers:
   - `0x18001570 = 0x0000`, `0x18001572 = total_len << 4`, `0x18001568 = 0x0000`
   - If `SHM 0x615 != 0` and `(FC & 0x4000) != 0` (Hardware AES-128-CCMP TX Encryption):
     - Sets `0x180017c8 = 0x0000`, `0x180017c2 = ((FC & 0x0080) ? 32 : 30)` (`WEP_IVLOC`: byte offset of CCMP IV from start of PLCP), `0x180017c6 = 0x0110`
     - Streams the 8 16-bit words of `TK` (`SHM 0x618..0x61f`) into `0x180017c4` (`WEP_KEY`)
     - Programs `0x180017c0 = 0x1170` (`WEP_CTL`: AES-128-CCMP TX encrypt + 8-byte MIC generation + hardware FCS regeneration), `0x180017d4 = 0x0000`, `0x180017d6 = total_len`, `0x180017d0 = 0x0305`
   - Otherwise (unencrypted frame): sets `0x180017c0 = 0x0100`, `0x180017d0 = 0x0300`
   - `0x1800150c = is_ofdm ? 0x0041 : 0x0040` (`TXE_PHYCTL`: OFDM vs. CCK modulation)
   - `0x18001514 = 0x1a02` (`TXE_PHYCTL1`)
   - `0x18001508 = 0x0000` (Template RAM start offset = `0`), `0x1800156a = total_len` (byte count)
   - `0x18001568 = 0x0826`, `0x18001500 = 0x0000`, `0x18001502 = 0x00d0` (`TXE_CTL`: immediate template transmit)
4. The firmware polls `TXE_STATUS` (`0x1800150e`, waiting for bit 7 to clear and bit 10 `0x0400` to assert) and `IFS_STAT` (`0x18001690`, waiting for bit 8 `0x0100` TX-active to clear), records `(st_50e << 16) | st_690` (`0x04c10022`) into `MBOX->vco_status`, clears `0x18001502 = 0` and `0x18001568 = 0`, restores `0x180017c0 = 0x8300` and `0x180017d0 = 0x0300`, re-arms `phy_tx_set_inhibit(1)`, and resumes continuous reception (`MACCONTROL = mac_en`).

### 9.4 Unicast MAC Filter and Automatic 802.11 `ACK` Transmission (`d11_transmit_ack_frame`)
At startup, the firmware stores the local 6-byte unicast MAC address in `SHM 0x612..0x614`:
- Default / AP role (`CFG_STA_ROLE == 0`): `02:9b:bf:88:dd:7d` (`0x9b02, 0x88bf, 0x7ddd`)
- Station role (`CFG_STA_ROLE != 0`): `02:08:da:57:f5:ea` (`0x0802, 0x57da, 0xeaf5`)

Whenever `PSM_STATE_CAPTURED` (`14`) is reached on a valid Management (`frame_type == 0`) or Data (`frame_type == 2`) frame whose Receiver Address (`RA`, `SHM 0x6a6..0x6a8`) matches `SHM 0x612..0x614`, the firmware extracts the Transmitter Address (`TA`, `SHM 0x6a9..0x6ab`) and calls `d11_transmit_ack_frame(ta, channel, mac_en & ~2)`:
- Constructs a 20-byte 11 Mbps CCK `ACK` template (`6e 84 0b 00 d0 9b` PLCP + `d4 00 00 00` Frame Control / Duration + 6-byte `RA = ta` + 4-byte IEEE 802.11 CRC-32 FCS).
- Loads the 5 words into D11 Template RAM (`0x18001130 / 0x18001134`), fires D11 TXE, increments the ACK counter in `SHM 0x7af`, and immediately rearms the receiver.

---

## 10. Building the Firmware

The controller has no external library or SDK dependency
(`-ffreestanding -nostdinc -nostdlib -fno-builtin`). It shares the project-owned
`clean-rx/tx_diag.h` diagnostic layout with the badge host. To build the image:

```bash
make -C clean-rx/src
```

This compiles `startup.S`, `main.c`, `pmu.c`, `radio_phy.c`, and `psm_ucode.c`
into a self-contained ARM Cortex-M3 binary of approximately 20 KiB
(`build/cyw43_rx_fw.bin`). The build reports its exact linked size.
