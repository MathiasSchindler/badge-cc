# Receive engine

[Manual contents](README.md)

## Receive path

```text
RF -> Radio 20691 -> LCN20 PHY -> D11 RXE
                                      |
                         prefix/body copy to SHM
                                      |
                         PSM checks and ownership
                                      |
                       controller retention in SRAM
```

A separate D11 FIFO/DMA path can carry a PSM-published receive header or
selected receive data into SRAM. A completed FIFO/DMA record and a
complete SHM capture are not the same object.

The radio demodulates one configured channel at a time. Packet reception
is not a general spectrum-energy or arbitrary-IQ measurement interface.

## Initial configuration

Receive setup includes:

- A calibrated radio and configured PHY.
- Appropriate antenna/coexistence grant.
- A SHM capture base and capacity.
- Receive-copy setup words.
- External-condition branch masks.
- A PSM program that handles completion, discard and rearm.
- Correct MAC enable and PSM run state.

Relevant registers are listed in [D11 registers](D11_REGISTERS.md).

One receive setup uses:

| D11 offset | Setting |
| --- | --- |
| `0x406` | Pulse `0x0001`, then `0x0000` |
| `0x40c` | `0x0018` |
| `0x448` | `0x0c02` |
| `0x402` | Software-selected SHM capture base |
| `0x404` | Software-selected capture capacity |
| `0x450` | `0x4e01` |
| `0x452` | `0x015b` |
| `0x4e4` | `0x0090` |
| `0x4fe` | `0x0000` |

The selected capacity must fit the actual allocation. It is not the
maximum physical receive size of the chip.

## Prefix and body phases

Reception is a sequence of hardware phases, not one atomic memory write:

1. PHY detects and decodes a packet prefix.
2. PSM observes the applicable PLCP-valid/start event.
3. RXE copies the initial prefix to the selected SHM area.
4. PSM waits for the required receive count and acknowledges copy progress.
5. Body copying/streaming continues.
6. A fresh terminal event and receive status determine acceptance/discard.
7. The capture is retained until its consumer releases ownership.
8. RXE is rearmed for the next packet.

A six-byte prefix is used by the packet-copy formats described here.
Its interpretation depends on PHY mode: legacy PLCP and HT signaling
must not be decoded as the same header.

Some receive layouts reserve a leading SHM word before the six-byte
prefix. That location and the surrounding metadata are programmed by
software, not a fixed chip packet ABI.

## Live count and copy state

| Quantity | D11 offset | PSM operand |
| --- | --- | --- |
| RXE control | `0x408` | `0x1004` |
| Live receive count | `0x418` | `0x100c` |
| RX status 1 | `0x41a` | `0x100d` |
| RX status 2 | `0x41c` | `0x100e` |
| Copy end/length | `0x454` | `0x102a` |
| Copy next/decoded length | `0x456` | `0x102b` |

The decoded-length operand, live received count and copied byte extent
can differ during reception. Do not export bytes based only on the
announced PLCP length.

For a captured six-byte prefix followed by an MPDU, the logical captured
length is `6 + MPDU length`. The MPDU length includes FCS when the capture
configuration retains it.

## Streaming

RXE control mask `0x0002` enables the streaming transition used here.
The transition must be coordinated with receive progress and copy setup.

A 128-byte prefix threshold is a usable programmed transition point.
It is not a universal hardware minimum frame length, a universal buffer
size or a rule that shorter frames cannot complete.

Changing the threshold, copy command or phase can change which bytes
reach SHM or DMA. A stable SHM snapshot does not establish that all bytes
belong to the current packet.

## Completion and FCS

The receive path provides terminal status and GoodFCS information.
A valid capture decision requires the appropriate fresh completion
condition, valid length, no disqualifying abort/FIFO condition, and good
FCS for the current frame.

Do not treat these as equivalent:

- A PLCP-valid event.
- A receive count reaching a threshold.
- A copy command finishing.
- GoodFCS.
- A DMA descriptor completing.
- The controller publishing a frame.

Completed bad-FCS frames are discardable frames, not necessarily an
incomplete-reception timeout. A bad frame may leave partially updated
storage. Bytes retained after rejection have no general validity
guarantee.

An external condition 5 event alone is not a sufficient abort criterion.
Use the configured terminal, explicit abort, FIFO and length conditions.

## PHY-format interpretation

PHY status word 0 is at D11 offset `0x412`. Its low two bits provide a
format field. Legacy receive interpretation uses:

- Format 1, or RX status 2 mask `0x2000`: OFDM.
- Legacy format 0: CCK/DSSS interpretation, with PHY status mask `0x0080`
  distinguishing the short-preamble indication.

The low format bits alone are not a universal HT classifier.
HT signaling, length and capture context must agree before treating
received bytes as an HT MPDU. Different receive/FIFO paths can present
different format values for otherwise valid aggregate data.

The channel assigned to a capture describes receiver configuration.
It does not independently establish the transmitter's configured
primary channel. Adjacent WLAN channels overlap spectrally.

## Aggregates and continuation

An A-MPDU contains multiple MPDUs inside one PPDU. Per-MPDU validity,
copy advancement, aggregate termination and storage retention are
separate responsibilities.

Receive-body control fields `0x83` and `0xc3` are not interchangeable
in all copy paths. The `0xc3` next-MPDU continuation is used after header
publication in the DMA streaming path described here. This is not a
general rule for every retained-SHM path.

A continuation DMA layout can contain six staging bytes before the
next MPDU rather than another physical six-byte PLCP. For that layout,
the MPDU starts at DMA offset 44 instead of 38. The complete header,
length and mode must identify the layout; do not choose an offset
merely because an FCS happens to pass.

Receiving one MPDU does not establish aggregate EOF, and retaining one
MPDU does not establish ownership of all previous MPDUs.
A Block ACK bitmap must describe complete retained MPDUs, not merely
observed delimiters or announced lengths.

General aggregate limits and every delimiter/truncation behavior are
unspecified in this manual.

## Rearm and flush

RXE flush uses control setting `0x0014`, with bounded status/event
polling before reuse of capture state. Flush completion may require
more than 255 polls; use a timeout appropriate to the selected clock
and operation.

Drain pending FIFO/DMA records consistently with the chosen receive
mode. Do not reuse an old terminal event for a new packet.
Do not turn an incomplete capture into an empty valid packet.

Stopping the PSM, flushing RXE, resetting the PHY and resetting D11
are different operations with different state loss. A full reset
requires reinitialization of the affected programming state.

## Measurement boundaries

The packet path supplies demodulated packet information, not calibrated
RF power. This manual does not specify:

- A calibrated RSSI-to-dBm conversion.
- An RF-noise-floor or energy-detector readout.
- Continuous raw ADC/IQ capture.
- FFT bins or a spectrum-analyzer mode.
- Sensitivity outside the normal WLAN band.

Missing packets or dark packet-activity samples are not proof of RF silence.
