# Architecture and memory

[Manual contents](README.md)

## Functional organization

The Wi-Fi subsystem combines a Cortex-M3 controller, D11 MAC core, D11 PSM
processor, LCN20 PHY, Radio 20691, shared system SRAM and a host-interface
core. The host can access the internal backplane through a windowed serial
interface.

```text
External host
    |
    gSPI
    |
    SDIOD ---- host-facing DMA --------+
    |                                  |
    +---------- internal backplane ----+---- SOCRAM
                         |             |
                    Cortex-M3          |
                         |             |
                         D11 --- RX DMA+
                          |
          +---------------+----------------+
          |               |                |
         PSM             RXE              TXE
          |               |                |
         SHM        receive staging    template RAM
                          |                |
                          +------ PHY -----+
                                  |
                              Radio 20691
                                  |
                               RF port
```

ChipCommon, PMU and GCI provide clock/resource control and radio-coexistence
coordination. Core wrappers control reset and local clock state.

The diagram describes functional connections, not physical floorplan,
arbitration latency or a cache-coherency specification.

## Backplane register map

| Block | Register base | Wrapper base | Function |
| --- | --- | --- | --- |
| ChipCommon | `0x18000000` | `0x18100000` | Identification, clocks, PMU, GCI |
| D11 | `0x18001000` | `0x18101000` | MAC, PSM, RXE, TXE, DMA, PHY/radio access |
| SDIOD | `0x18002000` | `0x18102000` | Serial host interface and DMA |
| Cortex-M3 | `0x18003000` | `0x18103000` | Embedded controller |
| SOCRAM | `0x18004000` | `0x18104000` | System SRAM controller |

These bases identify register blocks. They do not imply that every address
between adjacent bases is implemented or that every wrapper has the same
supported controls.

ChipCommon offset `0x000` contains the identification word. Its low 16 bits
identify device number 43439, hexadecimal `0xa9af`. The other fields are not
specified here as a revision-decoding interface.

## Memory spaces

| Space | Addressing | Access |
| --- | --- | --- |
| System SRAM | Backplane byte addresses `0x00000000..0x0007ffff` | Cortex loads/stores, host window, DMA |
| Calibration ROM span | Backplane bytes `0x1e09020c..0x1e090a07` | Read-only data access |
| PSM instruction memory | D11 object-memory word selector | Object address/data ports |
| D11 SHM | 16-bit word indices | PSM operands and object address/data ports |
| D11 template RAM | Template pointer and data ports | CPU writes/reads and TXE consumption |
| PHY registers | 16-bit register indices | PHY selector/data ports |
| PHY tables | Table ID plus entry offset | PHY table interface |
| Radio registers | 16-bit register indices | Radio selector/data ports |
| Cortex system control | Cortex architectural addresses | Controller-local system registers |

The calibration span is a 2,044-byte data region, not the full ROM map.
Other ROM locations, entry points and aliases are unspecified.

### System SRAM

The SRAM aperture contains 512 KiB. There are no hardware-defined
"mailbox", "packet queue", "BSS" or "TX staging" partitions at fixed offsets
inside this aperture. Those are allocations made by the running software.

Code, vectors, constants, stack, buffers and DMA descriptors may occupy
software-selected regions subject to accessibility and alignment.
Firmware must prevent overlap between live allocations.

Low SRAM supports a Cortex-M3 vector table. At reset entry, the first
vector word supplies the initial stack pointer and the second supplies the
reset handler address. A Thumb handler address has bit 0 set.
The vector-table location is controlled by the Cortex VTOR register at
`0xe000ed08`.

SRAM contents must be initialized before use. Resetting a core is not a
substitute for initializing descriptors, ownership state or capture buffers.
Retention guarantees across individual power/resource transitions are
unspecified.

### D11 SHM

SHM is shared storage for PSM data and CPU coordination, not a fixed set of
hardware variables. A word can hold a counter, packet byte pair, control
flag or any other software-defined value.

The word-index range `0x000..0xfff` is addressable through the SHM interface.
The corresponding storage spans 8 KiB. This is not a statement about
additional undocumented SHM banks.

For SHM word index `w`, object access is:

```text
OBJ_ADDR = 0x00010000 | (w >> 1)
read back OBJ_ADDR
16-bit data port = OBJ_DATA + 2 * (w & 1)
```

For example, word `0x601` selects object address `0x10300` and the upper
halfword data port at `0x18001166`. It is not SRAM byte address `0x601`,
nor SRAM byte address `0xc02`.

### PSM instruction memory

Object addresses without the SHM selector bit access PSM instruction
storage in the programming mode described here. A stored instruction is
two successive 32-bit words:

```text
instruction n, low word:  OBJ_ADDR = 2*n
instruction n, high word: OBJ_ADDR = 2*n + 1
```

Instruction-memory capacity is unspecified. Do not infer capacity from
the size of a particular program.

### Template RAM

Template RAM is separate from SHM and system SRAM. Its pointer register is
at D11 offset `0x130`; its 32-bit data port is at offset `0x134`.
Sequential data-port operations advance the template position.

Template capacity and all possible bank modes are unspecified. Select and
initialize a complete template before giving TXE ownership.

## Access ordering and ownership

The backplane allows several agents to access related resources:

- External host through SDIOD.
- Cortex-M3 through MMIO and SRAM.
- PSM through D11-local operands.
- DMA engines through descriptors and buffers.

Indirect selectors are shared mutable state. A selector write followed by
a data access is a single logical transaction and must not be interleaved
with another user of the same selector. This applies even if one user is
the external host and the other is the controller.

A readback of a posted control/selector write establishes ordering for the
following operation. Controller-side data barriers order SRAM descriptor
and buffer stores before DMA pointer publication.

Barriers do not prevent another processor from changing a selector, and
do not make a multi-word data structure atomic. Ownership and publication
protocols are software responsibilities.

See [DMA](DMA.md) for buffer ownership and [PSM](PSM.md) for shared execution.
