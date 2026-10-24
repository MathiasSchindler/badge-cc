# Hardware development notes and investigation history

> Historical implementation record. Results, commands, status statements and
> unclosed gates apply to the builds and conditions recorded here, not
> automatically to current firmware. Use [the development index](../README.md)
> for current guidance and work.

This preserves the former mixed hardware reference and development record,
including historical assumptions, qualification results and investigations.
Use the [current architecture reference](../../doc/HARDWARE_AND_FIRMWARE.md)
for the firmware's present behavior.

## Badge silicon identification

The exact CYW43439 version and revision installed on the badges have not
yet been established. The
[manufacturer's family datasheet](https://www.infineon.com/part/CYW43439)
describes the CYW43439 family in general; it does not establish the
badge-specific part or revision. Names such as LCN20, Radio 20691 and
Arch15 in the implementation and historical notes are not, by themselves,
a complete badge silicon identification.
The local snapshot was named
`Infineon_CYW43439_DataSheet_v03_00_EN-3074791.pdf`; its filename is retained
for provenance, not as a dependency on an ignored local download.

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
       |-- Copies PLCP + MPDU bytes into D11 Shared Memory (SHM 0x100..0x5b3)
       +-- Publishes 24-byte receive header (SHM 0x7f4..0x7ff) to D11 RX FIFO
  -> D11 PSM Microcode (Arch15 64-bit engine)
       |-- Waits for PLCP valid (ext 14), RX complete (ext 6), and Good FCS (ext 3)
       |-- Populates 24-byte d11rxhdr and triggers RX FIFO publication
       +-- Signals frame capture (SHM[0x601] = 14) and rearms on acknowledgment
  -> ARM Cortex-M3 Firmware
       |-- Inspects RA against station MAC (SHM 0x612..0x614) and transmits 802.11 ACK if unicast
       |-- Reclaims D11 RX DMA descriptor (0x18001220..0x18001234)
       |-- Reads MPDU from SHM (0x100..0x5b3) and records frame slot in SRAM (0x00020d80)
       +-- Frames MPDU as one F2 packet or two explicit fragments and posts to SDIOD TX DMA (0x18002200)

TRANSMIT PATH:
Host TX Staging Buffer (0x0004a400) or Unicast Auto-ACK Generator
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
| `0x00020000..0x000201df` | 480 B | Host/Firmware Control Mailbox (`fw_mbox_t`), including credits and RX fault latch |
| `0x000203c0..0x000203df` | 32 B | RX layout manifest, checked by the badge before starting RX |
| `0x00020d80..0x00025aa7` | 19,752 B | Multi-Frame Capture Ring (`fw_multi_t`, 8 frame slots x 2,464 B) |
| `0x00026000..0x000261ff` | 512 B | D11 RX DMA Descriptor Ring (32 descriptors x 16 B, 8-KiB aligned) |
| `0x00026200..0x000395ff` | 78,848 B | D11 RX DMA Packet Buffers (32 buffers x 2,464 B) |
| `0x0003a000..0x0003a1ff` | 512 B | SDIOD Function 2 TX DMA Descriptor Ring (32 descriptors x 16 B, 8-KiB aligned) |
| `0x0003a200..0x0004a1ff` | 65,536 B | SDIOD Function 2 TX DMA Packet Buffers (32 buffers x 2,048 B) |
| `0x0004a400..0x0004ad67` | 2,408 B | 802.11 Transmit Frame & Crypto Key Staging Buffer (`FW_TX_BUF_ADDR`, 64-byte aligned) |
| `0x1e09020c..0x1e090a07` | 2,044 B | On-chip ROM calibration dataset (TIA, gain tables, and common PHY tables) |

The table above describes the **default** layout. The opt-in
`CLEAN_RX_HT_RX=1` profile retains mailbox/diagnostic addresses but expands
raw storage to 3,896-byte MPDUs. Capture SHM moves to word indices
`0x800..0xf9f`; MAC/PSM are halted before testing bank readback/aliasing,
and failure is explicit (`0x8e`). The eight capture slots end at SRAM
`0x28967`; D11 descriptors/buffers move to `0x30000`/`0x30200`, with
32 x 3,968-byte buffers ending at `0x4f1ff`. SDIO descriptors/buffers
move to `0x52000`/`0x52200`, ending at `0x621ff`, and TX staging moves
to `0x64000`. Compile-time bounds cover all regions.

The manifest contains magic `0x3159414c`, MPDU capacity, SHM base, D11
ring, SDIO ring, TX staging, maximum fragment count and D11 buffer size.
Both ordinary and transport-test firmware publish it. A missing or
mismatched manifest fails host boot with `CLEAN_INPUT`, reporting the
field address/value; older controller images must be rebuilt with the
updated loader. The extended host/controller must use matching
`CLEAN_RX_HT_RX` flags. Up to three physical F2 packets make one logical
receive and consume only one host credit after complete validation.

See [HT receive-profile instructions](HT20_PACKET_AND_RESPONSE_API.md#extended-receive-profile-and-minimum-a-msdu-diagnostic)
for the bounded 3,839-byte A-MSDU fixture and its remaining station gates.

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
   - Configures edge-detection (`BRED0..3`) and polarity (`BRPO0..3`) for the D11 PSM external branch conditions (`ext 3` Good FCS, `ext 5` Bad PLCP, `ext 6` RX complete, `ext 11` RX FIFO busy, `ext 13` FIFO full, `ext 14` PLCP valid).
3. **RXE Scratch & Header Framing (`0x18001402..0x180014fe`):**
   - Sets RXE SHM scratch base `0x18001402 = 0x0100` (`spr:0x001`), scratch byte capacity `0x18001404 = 0x0968` (`2,408` bytes, `spr:0x002`), RXE control `0x18001448 = 0x0c02` (`spr:0x024`), and RXE framing `0x18001450 = 0x4e01`, `0x18001452 = 0x015b`, `0x180014e4 = 0x0090`.
4. **Radio 20691 LPF Bandwidth Scaling (`Radio 0xa3, 0x71..0x77`):**
   - Scales the Radio 20691 baseband filter registers `0x72..0x76` by the RC-calibrated `rc_lpf` multiplier (`(coeff * rc_lpf) >> 15`).

---

## 7. D11 PSM Microcode (`psm_ucode.c`)

The D11 Programmable State Machine executes 314 64-bit Arch15 instructions
(`2,512` bytes) loaded via the D11 object port (`0x18001160` word selector
`0..627`, `0x18001164` 32-bit data). Fourteen appended instructions preserve RX
rejection evidence and five enable correctly timed RX-buffer streaming.
Native identity tests restore the intended
patches in the original 295-instruction prefix and verify its original CRC.

**All 65 MPDU lengths 2,268..2,332 bytes are qualified bidirectionally at 11 Mbps
CCK**, including a full 2,304-byte MSDU in a basic 2,332-byte Data MPDU.
A five-minute run delivered 462 / 461 peer events, with every length received
on each badge and zero device errors or stalls. Earlier 1,536..1,600-byte
Ethernet-MTU qualification is recorded separately.
The former count-506 overflow was a missing RX-buffer streaming operation,
not the advertised RX-FIFO size or SHM/SRAM capacity. Hardware EROM identifies
D11 core revision 39; rev-40+ BMC register assumptions do not apply. See the
[cause, causal control and qualification evidence](../../../docs/micro-signal.md#ethernet-mtu-frames-rx-streaming-repair).
The [full-MSDU transport and qualification](../../../docs/micro-signal.md#full-2304-byte-msdus)
distinguish the qualified frame types from the 2,400-byte raw buffer capacity.
The CCMP paths below describe existing experimental hooks, not qualified
encrypted reception or WPA2 support.

### PSM State Machine Lifecycle
1. **Initialization & RXE Flush (`entry` -> `mac_start`):**
   - Zeroes SHM mailbox (`0x600..0x60f`), diagnostics (`0x7a0..0x7af`), and `d11rxhdr` (`0x7f4..0x7ff`).
   - Verifies the 32-bit session generation token in `shm:0x60c..0x60d`.
   - Sets `spr:0x001 = 0x100` (SHM base) and loads `spr:0x002` from ARM-written `shm:0x616` (2,408-byte capture capacity). Length checks use `shm:0x617` (2,406-byte RXE ceiling), avoiding signed 11-bit PSM immediate overflow.
   - Requests PHY/MAC core access (`spr:0x078` bit 5 = `1`, waits for bit 13 = `1`).
   - Flushes RXE (`spr:0x02a = 0`, `spr:0x024 = 0x0c02`, `spr:0x004 = 0x14`, waits for `spr:0x004` bits 2 and 4 to clear), drains stale external conditions (`ext 3, 5, 6, 11, 13, 14`), writes `shm:0x601 = 13` (`PSM_STATE_READY`), and waits for `MACCONTROL` `EN_MAC` (`ext 36`).
   - Initial flush uses a bounded 65,535-iteration countdown. The former 255-poll
     bound faulted while RXCTL was still `0x0804`; a later hardware sample
     confirmed completion (`0x0800`). That startup repair changes only the
     counter load; completion/fault branches and the ARM deadline remain intact.
   - The separate post-frame rearm still has its original 255-poll bound
     and unchecked fall-through into event draining. The default-disabled
     strict observer control now preserves the exact RXCTL decision operand
     and disables automatic RX-fault restart. Its earlier false reason-3
     faults came from a conditional-drain fall-through bug, since corrected.
     See the [decision-level investigation](../experiments/ht20/RXE_RECOVERY.md#rearm-decision-capture-and-conditional-drain-correction-2026-10-19).
   - The separately gated `CLEAN_RX_HT_REARM_RECOVER` passive experiment
     preserves the first busy fault, drains ext3/5/6/11/13/14 and reissues
     the original flush once per boot. Further busy faults stop explicitly;
     no PSM restart or diagnostic reset is allowed. Its additional
     `CLEAN_RX_HT_REARM_INJECT` control changes only a latched decision,
     never hardware RXCTL. Neither gate changes ordinary RX.
   - `CLEAN_RX_HT_REARM_RESET` separately adds a bounded PSM/C3 handoff
     for the existing six-operation PHY receive-reset recipe. C3 pauses
     the parked PSM, verifies TX inhibition before/after and validates
     final PHY readbacks before allowing the single flush retry. Rejected
     or unanswered requests fault explicitly; no core/PSM restart occurs.
     See the [instrumented reset experiment](../experiments/ht20/RXE_RECOVERY.md#parked-one-shot-phy-receive-reset-2026-10-19).
   - `CLEAN_RX_HT_REARM_PARK` adds a bounded pause/delay-only control
     with unchanged PHY readbacks and actual reset count zero. The
     [fixed-stimulus campaign](../experiments/ht20/EARLY_CAPTURE_AND_FIFO.md#fixed-stimulus-early-capture-qualification-2026-10-19)
     repeats exact 554-byte early bodies in both roles, distinguishes
     pre-prefix from post-capture misses, and retains naturally busy
     failures after both reset and park-only retries.
   - `CLEAN_RX_HT_OBSERVER_FIFO` reuses bounded SRAM staging for passive
     captures, preserves per-frame IDs and drains SRAM/held SHM on stop.
     The [FIFO qualification](../experiments/ht20/EARLY_CAPTURE_AND_FIFO.md#passive-observer-fifo-qualification-2026-10-19)
     separates conserved controller captures from short bad-FCS radio
     exits; this default-disabled gate is not an RXE reset.
2. **PLCP Detection & Prefix Copy (`event_start` -> `prefix_seen`):**
   - The original one-shot idle wait expires after 64 * 65,535 polls with
     reason 6. For live hardware DCF only, an expiry hook restarts the idle
     wait: quiet listening is not a MAC failure. Raw one-shot capture keeps
     its original timeout; flush/control watchdogs and host deadlines remain
     bounded. The native test includes the outer countdown, not just the
     DCF hook, and on-chip no-peer controls verify the live path.
   - Polls `ext 14` (PLCP valid). When asserted, issues a 6-byte initial copy (`spr:0x02a = 6`, `spr:0x004 = 0x40`) and waits for `spr:0x00d` bit 12 and `spr:0x00c >= 6`.
   - The 6-byte PLCP header is written by RXE into `shm:0x101..0x103` (`shm:0x100` holds a duplicate of `shm:0x101`).
3. **Variable-Length MPDU Streaming, Hardware AES-128-CCMP Decryption, & FCS Verification (`prefix_seen` -> `frame_wait`):**
   - Retains the original SHM continuation handshake: reads `r2 = spr:0x02b & 0x0fff`, writes `spr:0x02a = r2`, and triggers `spr:0x004` continuation (`field 8, 6, 131, spr:0x004`). This continuation value is not treated as a validated full MPDU length.
   - If `shm:0x615 != 0` (Hardware CCMP enabled) and the received Frame Control word (`shm:0x204`) has the Protected bit (`0x4000`) set:
     - Computes the CCMP IV byte offset (`30` for non-QoS Data when `FC` bit 7 is `0`, or `32` for QoS Data when `FC` bit 7 is `1`) and writes `spr:0x1e1` (`WEP_IVLOC`).
     - Streams the 8 words (16 bytes) of the Temporal Key (`TK`) from `shm:0x618..0x61f` into `spr:0x1e2` (`WEP_KEY`, with `spr:0x1e3 = 0x0110`) and triggers `spr:0x1e0 = 0x110b` (`WEP_CTL`: AES-128-CCMP hardware decrypt + MIC verify).
     - The experimental path requests in-place payload decryption in the relocated capture area and an 8-byte CCMP MIC check (`spr:0x00d` bit 3).
   - In `frame_wait`, enables `spr:0x004` low bits `2` once the RXE count reaches 128 bytes, unless bit 1 is already set. This drains PHY staging into the RX buffer while the remaining frame arrives; SHM peeking alone does not do so.
   - Retains the wait for `ext 6` (RX complete) and `spr:0x00c >= 14`, then polls `ext 3` (Good FCS). If `ext 3` asserts, branches to `capture`; if the loop expires or `ext 13`/`ext 5` fires, increments the corresponding discard counter (`shm:0x7a8..0x7ab`) and jumps to `rearm_flush`.
   - Before the overflow/bad-PLCP branch rearms RXE, twelve words at `shm:0x620..0x62b` latch count, raw `0x56`, status, capture limit, phase, FIFO control, raw `0x54`, continuation value, PLCP and encoding/status. These are mirrored at mailbox offset 456.
4. **24-Byte `d11rxhdr` Construction & RX FIFO Publication (`capture` -> `wait_arm_ack`):**
   - Populates the 12-word (24-byte) hardware receive header at `shm:0x7f4..0x7ff`:
     - `shm:0x7f4`: `RxFrameSize` (`spr:0x00c`, PLCP + MPDU length in bytes)
     - `shm:0x7f6..0x7f9`: `PhyRxStatus_0..3` (`spr:0x009, 0x00a, 0x01a, 0x01b`)
     - `shm:0x7fa..0x7fc`: 6-byte PLCP header (`shm:0x101..0x103`)
     - `shm:0x7fe`: `0xda15` marker; `shm:0x7ff`: active 2.4 GHz channel (`shm:0x609`)
   - Publishes the header to the D11 RX FIFO (`spr:0x078` bit 7 = `1`, `spr:0x007 = 0x7f4`, `spr:0x008 = 12`, `spr:0x004` low bits = `1`), waits for `ext 11` (FIFO busy) to assert and clear, resets `spr:0x004` low bits = `0`, sets `shm:0x601 = 14` (`PSM_STATE_CAPTURED`), and waits for the Cortex-M3 to acknowledge by clearing `shm:0x601 = 0` before flushing and rearming for the next frame.

---

## 8. D11 RX DMA and SDIOD Function 2 SDPCM Streaming (`main.c`)

### 8.1 D11 RX DMA Ring (`0x18001220..0x18001234`)
After PSM reaches `PSM_STATE_READY` (ensuring the initial RXE flush is complete), `rx_dma_init()` configures the 32-descriptor ring at `0x00026000` with 2,464-byte buffers at `0x00026200` and enables `D11_RX_DMA_CTL = 0x0004084d` (parity check disabled `0x800`, 38-byte receive offset `38 << 1`, enable `1`). On each frame, `rx_dma_poll_and_reclaim()` reads the DMA-delivered 24-byte `d11rxhdr` from the buffer, clears the buffer, and advances `D11_RX_DMA_PTR`.

### 8.2 SDIOD Function 2 TX DMA Ring (`0x18002200..0x18002214`)
`sdio_f2_init()` configures the 32-descriptor SDIOD Function 2 TX DMA ring at
`0x0003a000` with 2,048-byte packet buffers at `0x0003a200` and enables
`SDIOD_TX_DMA_CTL = 0x00060801` (32-byte bursts).

Production publication allows one unconsumed logical frame. Full-width consumed and
published counters are at mailbox byte offsets 448 and 452. The host grants a
credit only after reading and validating the complete frame; neither DMA
descriptor completion nor a guessed FIFO state is consumption. The receive
buffer is 2,420 bytes for a reconstructed SDPCM packet up to 2,418 bytes.
Physical F2 packets never exceed 2,044 bytes. Historical negative-control profiles deliberately retain their
unsafe burst/window choices.

For frames up to 2,026 MPDU bytes, `sdio_f2_send_frame()` constructs an 18-byte
**SDPCM + BDC header** followed by the received 802.11 MPDU:

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

Larger MPDUs use two independently framed physical F2 packets. Each has the
same header metadata and logical sequence, except that `bdc_flags = 0x21`
marks this project's fragment extension. Bytes 18..19 hold the complete MPDU
length, bytes 20..21 the MPDU byte offset, both little-endian; payload begins
at byte 22. The first fragment contains exactly 1,536 MPDU bytes and the
second contains the remainder. Physical length/complement fields describe
each fragment, not the reconstructed frame.

Both descriptors are checked for space and populated before one producer-pointer
update publishes the pair. Posted/consumed counters count logical frames, not
fragments. The host checks both headers, metadata identity, exact offsets and
payload lengths, with a 100-ms continuation deadline. It reconstructs the
original 18-byte wrapper and releases one credit only after complete validation.
Cancellation, bus errors, missing/reordered fragments and malformed lengths
fail without acknowledging a partial frame. These are host-transport fragments,
not over-air 802.11 fragmentation.

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
1. The host writes the 16-byte `TK` into `FW_TX_BUF_ADDR` (`0x0004a400`) and issues `host_cmd = 6 | (1 << 8)`.
2. The firmware copies the 8 16-bit key words into `SHM 0x618..0x61f` and sets `SHM 0x615 = 1`.

To transmit a raw or hardware-encrypted 802.11 frame (`cmd = 5 | clean_tx_length_bits(total_len) | (is_ofdm << 18) | (tx_cnt << 19)` in `MBOX->host_cmd`; helper in `tx_diag.h`):
1. The host writes the 6-byte PLCP header followed by the IEEE 802.11 MPDU (including 8-byte CCMP IV and 8-byte zero MIC placeholder if `FC & 0x4000`, plus 4-byte FCS) into `FW_TX_BUF_ADDR` (`0x0004a400`). The staging limit is 2,406 bytes, physically padded to 2,408; buffer capacity alone is not proof of OTA qualification for each frame type.
2. The firmware pauses MAC and PSM (`0x18001120 = 0x84020400`), calls `phy_tx_hw_configure(channel)` and `phy_tx_set_inhibit(0)`, writes `D11_TPL_WR_PTR = 0` (`0x18001130`), and streams the frame in 32-bit little-endian words to `D11_TPL_WR_DATA` (`0x18001134`).
3. The firmware enables `MACCONTROL` with PSM paused (`mac_en & ~2`) and configures the D11 TXE and Hardware Crypto registers:
   - `0x18001570 = 0x0000`, `0x18001572 = total_len << 4`, `0x18001568 = 0x0000`
   - If `SHM 0x615 != 0` and `(FC & 0x4000) != 0` (Hardware AES-128-CCMP TX Encryption):
     - Sets `0x180017c8 = 0x0000`, `0x180017c2 = ((FC & 0x0080) ? 32 : 30)` (`WEP_IVLOC`: byte offset of CCMP IV from start of PLCP), `0x180017c6 = 0x0110`
     - Streams the 8 16-bit words of `TK` (`SHM 0x618..0x61f`) into `0x180017c4` (`WEP_KEY`)
     - Programs `0x180017c0 = 0x1170` (`WEP_CTL`: AES-128-CCMP TX encrypt + 8-byte MIC generation + hardware FCS regeneration), `0x180017d4 = 0x0000`, `0x180017d6 = total_len`, `0x180017d0 = 0x0305`
   - Otherwise (unencrypted frame): sets `0x180017c0 = 0x0100`, `0x180017d0 = 0x0300`
   - `0x1800150c = 0x0041` for OFDM, `0x0040` for long-preamble CCK,
     or `0x0050` for short-preamble CCK (`TXE_PHYCTL`).
   - `0x18001514 = phyctl1` (`TXE_PHYCTL1`): OFDM rate-specific modulation/coding
     plus 20-MHz SISO bandwidth from [phy_rates.h](../../src/phy_rates.h). For
     6/9/12/18/24/36/48/54 Mbps these words are
     `0x0002/0x0202/0x0802/0x0a02/0x1002/0x1202/0x1902/0x1a02`.
     DSSS/CCK uses `0x0002/0x0802/0x1002/0x1802` for 1/2/5.5/11 Mbps,
     selected from the validated PLCP SIGNAL byte rather than a fixed 11-Mbps word.
   - `0x18001508 = 0x0000` (Template RAM start offset = `0`), `0x1800156a = total_len` (byte count)
   - `0x18001568 = 0x0826`, `0x18001500 = 0x0000`, `0x18001502 = 0x00d0` (`TXE_CTL`: immediate template transmit)
4. The firmware polls `TXE_STATUS` (`0x1800150e`, waiting for bit 7 to clear and
   bit 10 `0x0400` to assert) and `IFS_STAT` (`0x18001690`, waiting for bit 8
   `0x0100` to clear), recording `(st_50e << 16) | st_690` into
   `MBOX->vco_status`. **These checks can finish before the OFDM PHY tail.**
   Before shutting down OFDM TX, it conservatively waits one complete PPDU
   after MAC drain using the D11 `usectimer` (`0x1800101c`):
   `20 + 4 * ceil((16 + 8 * mpdu_len + 6) / (4 * Mbps)) + 6` microseconds.
   This includes OFDM training/SIGNAL, SERVICE, tail, whole DATA symbols and
   the ERP signal extension. The polling loop is bounded; a stopped timer
   or nonzero OFDM PHY TX error fails explicitly with error `0x82`.
   It then clears `0x18001502 = 0` and `0x18001568 = 0`, restores
   `0x180017c0 = 0x8300` and `0x180017d0 = 0x0300`, re-arms
   `phy_tx_set_inhibit(1)`, and resumes continuous reception
   (`MACCONTROL = mac_en`).

OFDM templates are validated before TX hardware is touched: valid RATE,
matching 12-bit LENGTH including FCS, even SIGNAL parity, zero reserved/tail
bits and zero padding. Malformed templates report error `0x83`.
The five-minute paired qualification received all eight rates at each of
88/128/1,600/2,026/2,027/2,332 MPDU bytes, at least nine times per case per
badge. See [results and scope](../../../docs/micro-signal.md#80211g-legacy-ofdm).
This qualifies raw PHY operation, not full 802.11g MAC compliance, ACK timing,
ERP protection or hardware CCMP.

#### DSSS/CCK PLCP and preamble selection

Shared [PHY helpers](../../src/phy_rates.h) construct and validate all four
DSSS/CCK rates. SIGNAL is `10/20/55/110`; SERVICE retains locked-clock bit 2.
LENGTH is `ceil(16 * mpdu_bytes / half_mbps)` microseconds, including FCS.
Only 11 Mbps sets SERVICE length-extension bit 7 when
`11 * LENGTH - 8 * mpdu_bytes >= 8`. Header CRC is complemented reflected
CCITT (`0x8408`, initial `0xffff`), little endian. Validation regenerates the
complete six-byte header, including SERVICE, LENGTH and CRC.

Long preamble/header adds 192 us and short adds 96 us. Short is legal only at
2/5.5/11 Mbps; the six PLCP bytes are identical, while command 10 selects the
short PHY header. Command 5 remains long-preamble CCK or ordinary OFDM.
Native tests exhaust MPDU lengths 14..2400, all header-bit corruptions and
independent fixed CRC vectors. TX completion polling scales with airtime.

For reliable CCK in profile 3, the first observed IFS TX-active-to-idle
transition anchors the existing bounded ACK wait to the actual transmission
tail. The 398-us long-slot CCK ACK allowance is not enlarged; subsequent
activity cannot re-anchor it. OFDM scheduling is unchanged. This fixes
premature ACK expiry at slow rates, not independent SIFS/timing qualification.

Receiving PHY status supplies frame type (bits 0..1) and short header
(bit 7). Published packet byte 16 adds `0x20` for short preamble and `0x40`
for unsupported PHY encoding, alongside channel and the existing `0x10`
OFDM flag. The controller snapshots the PHY status latched in the held
capture's PSM header (`SHM 0x7f6`); a DMA descriptor can lag capture and must
not supply another frame's status. Native image checks pin this latch before
the captured state. The separate receiving chip thus supplies rate/preamble evidence;
requested TX settings and successful TXE completion alone do not qualify it.
See the [paired fixture results and full-size 1-Mbps limitation](../../badge/README.md#dssscck-rate-and-preamble-fixture).

##### 1-Mbps receive-truncation diagnostics

The PSM retains 12 little-endian 16-bit words at SHM `0x6a0..0x6ab`,
snapshotted to controller SRAM `0x202c0..0x202d7`. The trace hooks preserve
the original PLCP/count diagnostic copies and use only scratch `r3`:

| Word | Meaning |
| --- | --- |
| 0..2 | Maximum observed 1-Mbps PLCP SIGNAL/SERVICE, LENGTH, CRC |
| 3 | Raw `spr:0x02b` at prefix |
| 4 | RXE byte count at prefix |
| 5..6 | Raw `spr:0x00d` and `spr:0x00e` at prefix |
| 7 | Low TSF at prefix |
| 8..11 | Failed LENGTH, low TSF, RXE count, remaining wait counter |

Only PLCP word `0x040a` is traced. Equal maximum LENGTH refreshes the
prefix and clears the failure words, keeping retry timestamps aligned;
smaller lengths and other rates do not replace it. Failure words are
written only for a matching LENGTH at the no-GoodFCS wait-expiry path.
This is not a complete RX event history or independent PHY timing capture.
Low TSF differences wrap modulo 65,536 us.

Six 32-bit words at SRAM `0x202d8..0x202ef` expose the first observed
TX-active duration, TXE timeout register `0x506`, template end pointer
`0x54c`, remaining count `0x56a`, and read-only PHY `0x465`/`0x42c`.
The first four are cleared at startup and before each TX. An end count of
zero establishes template consumption, not complete OTA waveform fidelity.
The PHY values are diagnostic snapshots, not verified LCN20 timeout units.
Static assertions protect trace/storage and report bounds.

Badge soak reports copy the RX trace to detail bytes 200..223, these six
words to 224..247, and the original 12-word RX FIFO fault record to
1912..1935. JSON fields are `rx_cck_trace`, `cck_tx_tail_us`, `txe_timeout`,
`tx_template_end`, `tx_count_end`, `phy_465`, `phy_42c` and `rx_fifo_fault`;
`tx_diagnostic` and `psm_diag` also expose existing raw diagnostics.
The failed-soak path issues stop command 3 before collecting the final
snapshot while preserving the first fixture failure. The existing
64-byte `MSK3` status and packet formats are unchanged.

Reproducible 1-Mbps failures decode valid 1,600/2,304-byte-MSDU PLCPs but
report only 1,589/1,590 RXE bytes at discard. Extending the PSM wait from
about 16.4 ms to 32.8 ms leaves the count unchanged. Missing GoodFCS and
ACK are consequences; the exact initiating PHY/RXE fault remains unknown.
No speculative PHY writes, Locked Clocks changes or expanded wait budgets
are retained. See [evidence and controls](LEGACY_PHY_MILESTONES.md#c1-all-legacy-dssscck-rates).

#### HT20 target, not enabled operation

The [D1 HT20 implementation map](../experiments/ht20/README.md) ties the
datasheet's mixed-mode MCS0-MCS7 target to TX format/rate controls, PLCP
framing, RXE/PSM status, host metadata, association and HT MAC requirements.
Bounded fresh captures now verify selector `+0x50c = 0x42` and all eight
HT20 MCS PHYCTL1 words in both directions. Mixed-mode `+0x510/+0x512`
take separate RATE (`0xb`) and protection LENGTH (`3*Nsym+9`), not
serialized L-SIG words. The previous packed-word assumption lost the
length bits on readback and prevented successful HT TX. An isolated
zero-template-CRC experiment also confirmed hardware HT-SIG CRC
regeneration in this path.

Same-capture diagnostics establish six HT-SIG bytes, RXE length six plus
PSDU length, and terminal PHY format bits 2 at all eight rates for the
88-byte probes. Early PHY bits may be stale and are not a format decision.
The one-frame tests had losses and one controller-captured fresh frame
was absent from F2 delivery; no reliability, large-frame or independent
implementation qualification is claimed. Default firmware rejects the
fixed HT TX diagnostic. Normal RX metadata still marks nonlegacy
formats unsupported; PSM responses reject them, and station association
remains legacy-only. Neither the PMU
"HT clock" nor calibrated legacy OFDM implies enabled 802.11n operation.

D2 adds a separate `CLEAN_RX_HT_DATA=1` image with a versioned, bounded
broadcast payload command, validated HT20/MCS RX metadata, and retention of
unposted captures across F2 backpressure. Its non-QoS MPDU limit is 2,332
bytes, independent of the 2,400-byte raw storage limit. HT/WMM beacon
profiling records peer capability/operation and all four EDCA AC parameters,
but real EDCA/QoS, HT protection and negotiated HT association remain gates.
Legacy associations remain unchanged. See the
[D2 API and qualification limits](HT20_FOUNDATIONS.md#d2-implementation-bounded-packet-api-and-peer-profiling).

### 9.4 Unicast MAC Filter and Automatic 802.11 `ACK` Transmission (`d11_transmit_ack_frame`)
At startup, the firmware stores the local 6-byte unicast MAC address in `SHM 0x612..0x614`:
- Default / AP role (`CFG_STA_ROLE == 0`): `02:9b:bf:88:dd:7d` (`0x9b02, 0x88bf, 0x7ddd`)
- Station role (`CFG_STA_ROLE != 0`): `02:08:da:57:f5:ea` (`0x0802, 0x57da, 0xeaf5`)

In the legacy raw profile, without station command 7 enabled, whenever `PSM_STATE_CAPTURED` (`14`) is reached on a valid Management (`frame_type == 0`) or Data (`frame_type == 2`) frame whose Receiver Address (`RA`, `SHM 0x106..0x108`) matches `SHM 0x612..0x614`, the firmware extracts the Transmitter Address (`TA`, `SHM 0x109..0x10b`) and calls `d11_transmit_ack_frame(ta, channel, mac_en & ~2)`:
- Constructs a 20-byte 11 Mbps CCK `ACK` template (`6e 84 0b 00 d0 9b` PLCP + `d4 00 00 00` Frame Control / Duration + 6-byte `RA = ta` + 4-byte IEEE 802.11 CRC-32 FCS).
- Loads the 5 words into D11 Template RAM (`0x18001130 / 0x18001134`), fires D11 TXE, increments the ACK counter in `SHM 0x7af`, and immediately rearms the receiver.

### 9.5 Experimental pinned-peer PSM responses

Station command 7 disables the legacy Cortex ACK path. The appended PSM
program is entered only from GoodFCS. It checks all three local RA words and,
for management/data/RTS responses, all three pinned TA words before scheduling
an ACK or CTS at a selected basic response rate.

D11 has **one SHM input per instruction**, including on Arch15; see the
[instruction-format reference](https://bcm-v4.sipsolutions.net/802.11/Microcode/#operands).
Comparing two SHM addresses directly is invalid. Each address comparison loads
one word into `r2` before comparing against the other SHM word. The native test
checks every instruction for multiple direct/indirect memory inputs and tests
each of the 96 RA/TA address bits. Its former flat-memory interpreter alone
did not catch this hardware constraint.

`SHM 0x637` response policy bits enable CTS (bit 0), short-preamble selection
(bit 1), and internal local-ACK consumption (bit 2). Station profiles 2 and 3 enable
all three. A valid 14-byte local ACK records `ACK_SEEN` only during an active
ACK wait, increments `SHM 0x639`, and jumps directly to RX rearm, bypassing
DMA/header publication and Cortex/host credits. Local ACKs outside an active
wait are consumed without setting `ACK_SEEN`. Profile 1 retains the older
capture path.

`SHM 0x67c..0x67f` count scheduled responses by received auth, association,
non-QoS data and other frame classes. A separate 104-byte `MCR1` report at
`0x20250` accumulates those counts and counts matching frames actually posted
to transport, plus FCS failures detected in those posted captures. The original
40-byte prefix is unchanged; appended words expose IFS/clock samples, response
path, bad-FCS identity, ACK notification timing, response setup delay and
the actual deliberately suppressed ACK count.
These are local diagnostics, **not** independent proof of RF/SIFS timing.
See the [milestone-A evidence and remaining gates](../../badge/README.md#milestone-a-mac-implementation-experimental-not-complete).

### 9.6 Experimental PSM-clocked medium access

Profile 3 uses SHM `0x680..0x69f` for TX arbitration, clocks, diagnostics and
response ownership. The idle hook prioritizes RX-start, samples IFS physical
and NAV carrier/active-frame flags, explicitly times DIFS (SIFS plus two slots),
then counts complete 9/20-us backoff slots against TSF. Busy/RX resets partial
idle time without changing the residual draw. Bad FCS and invalid length start
EIFS; valid RX clears it. Cortex chooses CW/random draws and prepares templates,
but an armed request is fired by the PSM, not a Cortex final register write.
GoodFCS cancels an armed request before response setup can overwrite TXE.

SHM `0x69e` is the remaining deliberate ACK-suppression budget; `0x69f`
counts actual suppressed responses. Explicit test command
`9 | (count << 8)`, profile 3 only, atomically arms 0..16 suppressions while
PSM is halted, clears partial idle timing, then resumes. Only otherwise valid
GoodFCS, local-RA/pinned-TA, supported-rate ACK responses consume a suppression;
no TX is armed/triggered and response ownership is not acquired for that ACK.
CTS, incoming ACK consumption and application delivery are unchanged.
The default is zero and ordinary station paths never enable it.

The PSM reads TSF through IHR `0x1119/0x111a`; Cortex reads the same domain at
`D11+0x180`, not its old timer at `D11+0x1c`. The deadline is refreshed at
observed physical TX-active. Response format uses PHY status 0, since the old
G-PHY-only OFDM RX flag is absent on real LP-PHY OFDM receptions. Complete
response templates are prepared before arming/triggering, and ownership remains
held through scheduled start and physical tail. The Cortex checks ownership and
IFS activity before changing templates or policy, including after halting PSM.

An initial hardware-backoff-register experiment did not count down and emitted
no frames; it was replaced, not treated as working hardware DCF. Profile 2
retains the old Cortex scheduler. Profile 3 and its bounded ACK-notification
allowance remain experimental, not independently SIFS/contention-qualified.

---

## 10. Building the Firmware

The controller has no external library or SDK dependency
(`-ffreestanding -nostdinc -nostdlib -fno-builtin`). It shares the project-owned
`clean-rx/src/tx_diag.h` diagnostic layout with the badge host. To build the image:

```bash
make -C clean-rx/src
```

This compiles `startup.S`, `main.c`, `pmu.c`, `radio_phy.c`, `psm_ucode.c`, and `mac.c`
into a self-contained ARM Cortex-M3 binary of approximately 30 KiB
(`build/cyw43_rx_fw.bin`). The build reports its exact linked size.

