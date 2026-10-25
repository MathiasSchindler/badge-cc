# Glossary

[Manual contents](README.md)

| Term | Meaning |
| --- | --- |
| ADC | Analog-to-digital converter |
| AGC | Automatic gain control |
| ALP | Low-power clock/resource name used by the host interface |
| A-MPDU | Multiple individually framed MPDUs inside one PHY transmission |
| A-MSDU | Multiple service-data units carried inside an MPDU |
| Backplane | Internal addressable interconnect between chip cores and memory |
| BPHY | DSSS/CCK PHY path |
| CC | ChipCommon |
| CCK | Complementary code keying |
| CCMP | Counter mode with CBC-MAC protocol using AES |
| CTS | Clear to send control frame |
| D11 | Wi-Fi MAC hardware core, including PSM, RXE and TXE |
| DAC | Digital-to-analog converter |
| DCF | Distributed coordination function for medium access |
| DCOE/IDAC | DC-calibration control/result terminology |
| Descriptor | Memory record directing a DMA buffer transfer |
| DIFS | Distributed interframe space |
| DMA | Direct memory access |
| DSSS | Direct-sequence spread spectrum |
| EDCA | Enhanced distributed channel access |
| EIFS | Extended interframe space |
| EOF | End of frame; physical, aggregate and copy boundaries must be distinguished |
| ERP | Extended-rate PHY operation in the 2.4-GHz band |
| FCS | Frame check sequence |
| FIFO | First-in, first-out storage/port |
| GCI | General chip interface, including coexistence control |
| GI | Guard interval |
| gSPI | Serial host-interface protocol |
| HT | High throughput; may name a clock resource or an 802.11 PHY mode |
| HT20 | 20-MHz high-throughput WLAN mode |
| IHR | D11 internal hardware register |
| IFS | Interframe spacing/medium-control hardware |
| IQ | In-phase and quadrature components |
| LCN20 | PHY block designation |
| LPF | Low-pass filter |
| MAC | Medium access control |
| MCS | Modulation and coding scheme |
| MMIO | Memory-mapped input/output |
| MPDU | MAC protocol data unit; length here includes FCS when stated |
| MSDU | MAC service data unit |
| NAV | Network allocation vector |
| OFDM | Orthogonal frequency-division multiplexing |
| PLCP | Physical-layer convergence procedure/header |
| PLL | Phase-locked loop |
| PMU | Power management unit |
| PPDU | Complete PHY protocol data unit transmitted over the air |
| PSM | D11 programmable sequencing processor |
| RA / TA | Receiver address / transmitter address |
| RC calibration | Resistor/capacitor-related analog calibration |
| RF | Radio frequency |
| RXE | D11 receive engine |
| SDIOD | Host-interface device core |
| SHM | D11 shared memory; addressed here in 16-bit words |
| SIFS | Short interframe space |
| SOCRAM | Shared on-chip system SRAM and its controller |
| Template RAM | D11 transmit-template storage, separate from SHM and SRAM |
| TIA | Transimpedance amplifier |
| TSF | Timing synchronization function/timer |
| TXE | D11 transmit engine |
| VCO | Voltage-controlled oscillator |
| VTOR | Cortex vector-table offset register |

## Units

- A byte is eight bits.
- A SHM word is 16 bits.
- A DMA descriptor word is 32 bits.
- A stored PSM instruction occupies 64 bits.
- KiB means 1,024 bytes.
- MHz means 1,000,000 hertz.
- Mbit/s means 1,000,000 bits per second.
- Microseconds are written `us` in equations and register descriptions.
- A raw controller tick is not a fixed time unit across clock configurations.
