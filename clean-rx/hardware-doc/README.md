# CYW43439 hardware reference

## Purpose

This manual describes the CYW43439 Wi-Fi subsystem as a programmable device:
its processors, address spaces, register interfaces, memory, DMA engines,
radio controls and packet-processing machinery. It is self-contained.
All chapter links stay within this manual.

The descriptions concern hardware behavior and programming interfaces.
Application protocols, firmware commands, software-assigned mailboxes,
build systems and development history are not part of the chip interface.

## Contents

| Chapter | Contents |
| --- | --- |
| [Architecture and memory](ARCHITECTURE.md) | Processing blocks, backplane map, SRAM, ROM, D11 memories and ownership |
| [Host interface](HOST_INTERFACE.md) | gSPI transactions, byte order, backplane windows, clocks and Function 2 |
| [Reset, clocks and power](RESET_CLOCKS.md) | Core wrappers, reset sequencing, PMU registers and 37.4-MHz configuration |
| [D11 register reference](D11_REGISTERS.md) | MAC control, object access, RXE, TXE, timers, response and crypto registers |
| [DMA engines](DMA.md) | Descriptor format, rings, receive DMA, host-facing transmit DMA and completion |
| [PSM processor](PSM.md) | Instruction representation, operands, arithmetic, branches and shared execution |
| [Receive engine](RECEIVE.md) | Prefix/body copying, streaming, status, FCS, aggregation and rearm |
| [Transmit and medium access](TRANSMIT.md) | Templates, PHY controls, rates, responses, contention and completion |
| [PHY and radio](PHY_RADIO.md) | Indirect access, tables, channel programming, calibration and TX overrides |
| [Coverage and unspecified interfaces](COVERAGE.md) | Revision scope, electrical information, Bluetooth and undefined behavior |
| [Glossary](GLOSSARY.md) | Terminology and units |

## Reading conventions

- Hexadecimal quantities have a `0x` prefix. Decimal quantities do not.
- Unless explicitly qualified, an address is a byte address on the internal
  backplane. Register offsets are relative to the named block.
- SHM addresses are **16-bit word indices**, not backplane byte addresses.
- PHY and radio register numbers select separate **16-bit register banks**.
- PSM program addresses count instructions. Each stored instruction occupies
  eight bytes.
- Multi-byte memory quantities are little-endian unless a bus transaction
  explicitly selects another wire representation.
- Register tables describe the listed fields and operations only. Omitted
  bits are **unspecified**, not necessarily zero, unused or safe to modify.
- Access descriptions such as "read status" and "write control" identify
  supported operations; they do not specify behavior for every other access.
- Configuration values are programming settings, not reset-value declarations.
  Reset values are unspecified unless explicitly stated.

**Unspecified** means that this manual does not define a value, field,
operation, range or guarantee. It must not be replaced by an assumed value.
In particular, this manual is not a complete electrical datasheet, complete
instruction-set specification or guarantee of every silicon revision.

## Essential distinctions

1. The Cortex-M3 and the D11 PSM are different processors with different
   instruction sets and address spaces.
2. SOCRAM, SHM, template RAM and PSM instruction memory are distinct memories.
3. RXE copy completion, frame validity, DMA completion and host consumption
   are separate events.
4. A synthesizer programming value is not a specification of usable RF range.
5. MAC transmit completion is not necessarily the end of the PHY waveform.
6. The presence of programmable hardware does not imply an automatically
   implemented 802.11 station, security protocol or aggregation policy.
