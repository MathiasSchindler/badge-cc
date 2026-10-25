# Serial host interface

[Manual contents](README.md)

## gSPI organization

The gSPI interface exposes numbered functions:

| Function | Purpose in this interface |
| --- | --- |
| 0 | Bus configuration, interrupts, status and test pattern |
| 1 | Backplane access and backplane/clock control registers |
| 2 | Host-facing packet FIFO |

The serial link uses an active-low select, clock and bidirectional data.
The shared data wire changes ownership between the command/write phase
and read phase. The host must release it before the device drives it.
Electrical levels, maximum clock rate and setup/hold limits are not
specified in this manual.

## Command word

The command is a 32-bit quantity:

| Bits | Meaning |
| --- | --- |
| 31 | Direction: 1 write, 0 read |
| 30 | Address increment control; set in the transactions described here |
| 29..28 | Function selector |
| 27..11 | 17-bit function address |
| 10..0 | Transfer byte count |

For example, a Function 2 FIFO read of `n` bytes uses command
`0x60000000 | n`, with function address zero.

The 11-bit count field is not a guarantee that all sizes, alignments,
functions or transaction types are accepted. For a count constrained to
a nonzero multiple of four, its largest representable value is 2,044.
That arithmetic limit is not a universal hardware packet-size limit.
Register accesses use byte, halfword or word operations.

## Wire representation

Bits within each transmitted byte are shifted most-significant bit first.
Word byte order depends on bus configuration.

For a logical word with bytes `b0` through `b3`, least-significant first:

| Mode | Serial byte sequence |
| --- | --- |
| Reset 16-bit ordering | `b1 b0 b3 b2` |
| Normal 32-bit ordering | `b0 b1 b2 b3` |

Reset ordering and normal ordering are distinct from bit order within a
byte. Using the wrong representation can produce plausible but incorrect
commands as well as incorrect read values.

## Function 0 registers

Addresses below are Function 0 addresses, not backplane addresses.

| Address | Operation/width | Description |
| --- | --- | --- |
| `0x00000` | Read/write, 32-bit | Bus configuration |
| `0x00004` | Write, 16-bit | Interrupt/status clearing operation |
| `0x00006` | Write, 16-bit | Interrupt mask configuration |
| `0x00008` | Read, 32-bit | Bus and packet status |
| `0x00014` | Read, 32-bit | Constant test pattern `0xfeedbead` |
| `0x0001d` | Read/write, 8-bit | Backplane read-response delay |

The bus configuration setting `0x000200a3` selects the normal 32-bit
transaction configuration described here. Its configuration comparison
mask is `0x00ffff7f`; this is not a field-by-field definition of the
unmasked bits.

Configure the response-delay register to 16 for the Function 1 reads
described below. The corresponding response contains 16 delay bytes
before its data word. Delay bytes are not payload.

### Bus-status fields

| Mask | Interpretation |
| --- | --- |
| `0x00000002` | Underflow/error indication used by packet-read handling |
| `0x00000100` | Function 2 packet available |
| `0x000ffe00` | Available Function 2 packet length, shifted right by 9 |

An all-ones read is not a valid ready indication. Do not interpret
`0xffffffff` as a long packet or as all clocks available.

## Function 1 control registers

These control registers are addressed directly in Function 1:

| Function 1 address | Width | Purpose |
| --- | --- | --- |
| `0x1000a` | 8-bit | Backplane window address bits 15..8 |
| `0x1000b` | 8-bit | Backplane window address bits 23..16 |
| `0x1000c` | 8-bit | Backplane window address bits 31..24 |
| `0x1000e` | 8-bit | Clock request/status |
| `0x1000d` | 8-bit | Packet-frame termination control; `0x01` terminates an incomplete read frame |

The backplane aperture covers 32 KiB. For a 32-bit backplane access at
byte address `a`:

```text
base = a & 0xffff8000
F1[0x1000a] = (base >> 8)  & 0xff
F1[0x1000b] = (base >> 16) & 0xff
F1[0x1000c] = (base >> 24) & 0xff
function address = (a & 0x7fff) | 0x8000
```

The `0x8000` aperture bit selects the 32-bit access form. Keep a block
transfer inside the selected window; reselect before crossing its boundary.

To read ChipCommon at `0x18000000`, the window bytes are `0x00`, `0x00`,
`0x18`, and the function address is `0x8000`.

The window belongs to the host-interface access path, not to an individual
software client. Multiple host clients must serialize changes.

### Clock request/status

| Mask | Meaning |
| --- | --- |
| `0x08` | Request ALP clock |
| `0x10` | Request HT clock |
| `0x40` | ALP available |
| `0x80` | HT available |

Write a request, then poll availability with a bounded timeout.
The HT clock in this register is a high-throughput clock/resource name;
it does not indicate reception of an 802.11n HT frame.

## Basic bus-start sequence

1. Establish a valid external power/reset and host-pin state.
2. Read the test pattern using reset ordering.
3. Write `0x000200a3` to Function 0 address zero using reset ordering.
4. Switch the host's command and data representation to normal ordering.
5. Read back bus configuration and the test pattern.
6. Set the Function 1 read-response delay to 16.
7. Request ALP and wait for availability.
8. Select the backplane window and read ChipCommon identification.

Power-enable delays depend on the surrounding hardware and are not
defined here as chip minimum timing requirements.

## Function 2 packet reads

1. Read Function 0 status.
2. Require packet-available and an acceptable nonzero packet length.
3. Issue a Function 2 FIFO read with that length.
4. Clock the data transfer to a 32-bit boundary when using the word-oriented
   transfer form. Padding bytes are not part of the logical packet.
5. Check status for underflow and parse only the logical packet bytes.

At backplane address `0x18002020`, mask `0x00800000` is a packet-pending
indication used alongside serial bus status.

The FIFO transfers bytes; it does not prescribe an Ethernet, SDPCM, BDC,
802.11 or application packet format. Those envelopes are software-defined.
Likewise, logical host credits and consumed counters are software protocols,
not fixed SRAM hardware registers.

FIFO availability, SDIOD DMA descriptor progress and host acceptance are
different states. Reading a packet does not validate its contents, and
DMA completion alone does not prove host consumption.
