# DMA engines

[Manual contents](README.md)

## Roles and direction

Two DMA paths are relevant to packet movement:

| Engine | Register base | Direction |
| --- | --- | --- |
| D11 RX DMA | `0x18001220` | D11 receive FIFO to system SRAM |
| SDIOD TX DMA | `0x18002200` | System SRAM to external-host Function 2 FIFO |

"SDIOD TX" means transmit toward the host. It is not Wi-Fi RF
transmission. Conversely, D11 RX DMA does not itself decide whether a
frame is valid or should be acknowledged.

## Register layout

Offsets are relative to each engine's register base:

| Offset | Width | Function |
| --- | --- | --- |
| `0x00` | 32 | Control |
| `0x04` | 32 | Posted descriptor pointer |
| `0x08` | 32 | Descriptor-ring base, low |
| `0x0c` | 32 | Descriptor-ring base, high |
| `0x10` | 32 | Status 0 |
| `0x14` | 32 | Status 1 |

The high base register is zero for rings in the low SRAM aperture.

## Descriptor representation

A descriptor contains four little-endian 32-bit words and occupies
16 bytes:

| Byte offset | Field |
| --- | --- |
| 0 | Control 1 / frame flags |
| 4 | Control 2 / byte count |
| 8 | Buffer address, low |
| 12 | Buffer address, high |

### Fields used by the packet paths

| Word | Mask | Meaning |
| --- | --- | --- |
| Control 1 | `0x80000000` | Start of frame for a host-facing TX descriptor |
| Control 1 | `0x40000000` | End of frame for a host-facing TX descriptor |
| Control 1 | `0x10000000` | End of descriptor table; wrap to ring base |
| Control 2 | `0x00007fff` | Buffer/transfer byte count |

Other flags and their interactions are unspecified.

For a single-descriptor host packet, Control 1 is `0xc0000000`, plus
`0x10000000` for the last ring entry.
Receive descriptors use the end-of-table flag where appropriate and a
buffer-capacity byte count.

## Ring placement

The ring configuration described here uses:

- An 8-KiB-aligned ring base.
- 32 descriptors.
- 16 bytes per descriptor.
- A total descriptor-table size of 512 bytes.
- One reserved position to distinguish producer/consumer states.

Thirty-two descriptors are a usable configuration, not a statement of
the chip's maximum ring capacity.

Status 0 low bits `0x1fff` identify the current descriptor position within
the relevant address window. The upper nibble describes DMA engine state;
zero is the disabled/stopped state used during initialization.
Status 1 contains additional status/error information whose full field
definitions are unspecified.

For an 8-KiB-aligned ring:

```text
offset = STATUS0 & 0x1fff
index = offset / 16
```

For a ring whose low address bits are nonzero, subtract the ring-base
low bits before converting to an index. Reject misalignment and indices
outside the configured table instead of applying an arbitrary modulo.

## D11 receive configuration

Control value `0x0004084d` selects the receive-DMA configuration used here:

| Component | Value |
| --- | --- |
| Enable | `0x00000001` |
| Offset contribution | `38 << 1`, or `0x4c` |
| Parity-disable contribution | `0x00000800` |
| Additional receive mode | `0x00040000` |

The resulting DMA layout has a 38-byte leading area before the selected
receive data. That leading area is not universally a MAC header.
The D11 FIFO input may contain a PSM-constructed header rather than the
whole MPDU.

Initialization:

1. Write control zero and wait for the stopped state.
2. Initialize buffers and all descriptor words.
3. Mark the last descriptor end-of-table.
4. Order SRAM stores with a data barrier.
5. Program ring base low/high.
6. Establish the initial pointer.
7. Enable the engine.
8. Publish the available descriptor boundary.

With a 32-entry ring, software can initialize consumer 0 and producer 31,
leaving one position unused.

### Receive completion and reclamation

A changed current-descriptor index means DMA has advanced past a
descriptor. Read the completed buffer only after ordering the completion
observation against memory reads.

The leading receive length is a little-endian halfword at buffer offset
zero in the described configuration. Validate it against buffer capacity
and the expected FIFO record format.

Copy or consume the data before clearing/reusing the buffer. Restore the
descriptor, order its stores, and only then advance the posted pointer.
Clearing SRAM while the engine still owns it destroys receive data.

## SDIOD host-facing transmit configuration

Control setting `0x00060801` selects the host-facing DMA mode with
32-byte bursts used here. The complete bit decomposition is unspecified.
Do not substitute another burst configuration without defining its
alignment and transfer behavior.

For each packet:

1. Acquire a free descriptor and buffer.
2. Write the complete logical packet.
3. Add initialized padding to a 32-bit boundary.
4. Set descriptor byte count to the padded DMA length.
5. Set start/end flags and the correct buffer address.
6. Order SRAM writes.
7. Advance the posted pointer to expose the descriptor to DMA.

The logical packet length need not include DMA padding.
The hardware FIFO does not validate a software packet header.

For producer index `p` and hardware current index `c`, a one-gap ring
must not publish a position for which `(p + 1) % 32 == c`.

## Three different ownership boundaries

1. **Receive storage:** RXE/PSM must finish writing the selected capture
   before it is released or overwritten.
2. **DMA buffer:** the engine must finish using a descriptor and buffer
   before software recycles them.
3. **Host packet:** the host must consume the packet before a software
   protocol returns its logical credit.

A software credit counter is not the DMA current pointer.
A completed DMA transfer into the FIFO is not proof that the host
received or accepted the packet.

## Shutdown

Prevent new publications, disable the engine, and wait for the stopped
state before rebuilding the ring. Bound the wait. Preserve fault status
and a failing descriptor before resetting or recycling storage.

Complete error-bit decoding, all supported burst sizes, scatter/gather
limits and maximum ring sizes are unspecified.
