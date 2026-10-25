# D11 PSM processor

[Manual contents](README.md)

## Execution model

The PSM is a D11-local programmable processor for packet sequencing and
time-sensitive MAC operations. It is not the Cortex-M3 and does not execute
ARM instructions.

Its program controls RXE, TXE, response construction and medium-state
handling through register operands. SHM provides packet storage and
communication with the controller.

The instruction representation described here is the D11 Arch15 form.
This chapter specifies the subset below, not every opcode, operand mode,
pipeline hazard or implementation revision.

## Instruction storage

Instructions occupy eight bytes in little-endian storage.
The fields used here are:

```text
instruction = (opcode << 39) | (A << 26) | (B << 13) | D
```

| Bits | Field |
| --- | --- |
| 12..0 | D: destination or branch target |
| 25..13 | B: second input |
| 38..26 | A: first input |
| 49..39 | Opcode field used by this instruction subset |
| 63..50 | Zero in this representation; other uses unspecified |

Program-counter values count instructions, not bytes or 32-bit object
words. Instruction `n` is stored at byte offset `8*n` in an image and
object-word offsets `2*n`, `2*n+1` in instruction memory.

## Operand classes

| Operand encoding | Meaning |
| --- | --- |
| `0x0000..0x0fff` | Direct SHM word |
| `0x1000..0x13ff` | D11 internal-register operand window |
| `0x1400..0x177f` | Additional memory/indirect operand space; detailed addressing unspecified |
| `0x1780` | General register r0 |
| `0x1781` | General register r1 |
| `0x1782` | General register r2 |
| `0x1783` | General register r3 |
| `0x1800..0x1fff` | Signed 11-bit immediate |

Other general-register encodings are unspecified here.

An immediate is the low 11 bits sign-extended to a 16-bit value:

```text
0x1800 ->      0
0x1801 ->      1
0x1bff ->   1023
0x1c00 ->  -1024, represented as 0xfc00
0x1fff ->     -1, represented as 0xffff
```

An arbitrary 16-bit constant cannot be encoded by simply ORing it with
`0x1800`. Build larger values from byte pieces or load them from SHM.

### Internal-register address conversion

For the mapped IHR window:

```text
D11 byte offset = 0x400 + 2 * (operand - 0x1000)
```

Examples:

| PSM operand | D11 offset | Function |
| --- | --- | --- |
| `0x1001` | `0x402` | Receive-copy base |
| `0x1002` | `0x404` | Receive-copy limit |
| `0x1004` | `0x408` | RXE control |
| `0x1009` | `0x412` | PHY status 0 |
| `0x100c` | `0x418` | Receive count |
| `0x100d` | `0x41a` | RX status 1 |
| `0x100e` | `0x41c` | RX status 2 |
| `0x102a` | `0x454` | Copy end/length |
| `0x102b` | `0x456` | Copy next/decoded length |
| `0x1081` | `0x502` | TX auxiliary control |
| `0x1087` | `0x50e` | TX status |
| `0x10b4` | `0x568` | TX selection |
| `0x1119` | `0x632` | Timer low |
| `0x111a` | `0x634` | Timer next halfword |
| `0x1148` | `0x690` | IFS/medium status |

The mapping does not imply that every register offset in the window
has a defined function.

## Memory-input restriction

An instruction has at most one memory input. Direct SHM operands and the
additional memory/indirect space consume that input.

To compare two SHM words:

```text
move SHM[x] to r2
compare SHM[y] with r2
```

Do not encode a two-memory-input comparison even if an instruction word
can be constructed for it.

## Arithmetic and data instructions

The listed arithmetic operates on 16-bit values:

| Opcode | Operation |
| --- | --- |
| `0x3f0` | Move A to D, with B set to immediate zero |
| `0x1c0` | Add A and B, write low 16 bits to D |
| `0x1d0` | Subtract B from A, write low 16 bits to D |
| `0x140` | Bitwise AND A and B |
| `0x160` | Bitwise OR A and B |
| `0x378` | Byte construction: `(A << 8) | (B & 0xff)` |
| `0x270` | Extract A low byte, with B zero |
| `0x278` | Extract A high byte, with B zero |
| `0x310` | Replace B bits 1..0 with A bits 1..0 |
| `0x307` | Replace B bit 7 with A bit 0 |
| `0x376` | Replace B bits 13..6 with A bits 7..0 |

Example: construct `0x0826` in r2:

```text
(0x3f0, 0x1826, 0x1800, 0x1782)
(0x378, 0x1808, 0x1782, 0x1782)
```

Example: a 65,535-count bounded wait initializes r0 with immediate
`0x1fff`, not `0x18ff`. Subtraction wraps modulo 65,536.

## Branch subset

| Opcode | Condition for branch to D |
| --- | --- |
| `0x0d0` | A equals B |
| `0x0d1` | A differs from B |
| `0x0da` | A is less than B in the unsigned comparisons described here |
| `0x0dc` | A is greater than B in the unsigned comparisons described here |
| `0x0db` | A is greater than or equal to B in the unsigned comparisons described here |
| `0x400 + n` | Bit n of A is clear, with B zero, for n = 0..15 |
| `0x500 + n` | Bit n of A is set, with B zero, for n = 0..15 |
| `0x77f` | Unconditional branch; unused inputs conventionally name r0 |

Other flag-setting, signed-compare, call/return and conditional forms are
unspecified.

External-condition branches provide hardware-event tests. For example,
`0x785` is used for condition 5 and `0x78e` for receive-start condition 14
in the receive configuration described here. Their interpretation depends
on the D11 branch-mask configuration. Do not replace those tests by a
software count or assume an event edge is an abort.

## Loading and starting

1. Stop MAC/PSM as required by the initialization state.
2. Write each instruction as two object-port words.
3. Read back the program through the same interface.
4. Initialize all SHM locations the program may read.
5. Establish receive bounds, branch masks and required timing configuration.
6. Apply the MACCONTROL start sequence.
7. Require a program-defined readiness indication before enabling dependent
   DMA/publication operations.

Readiness flags, state numbers and SHM mailbox offsets are software
conventions. The chip does not require a particular state value at SHM
word `0x601` or any other fixed mailbox location.

## Concurrent controller access

PSM run and MAC enable are separate controls. Pausing the PSM does not
necessarily stop the receive hardware or finish a pending response.

Before replacing a TX template or response policy:

1. Pause the PSM while preserving MAC control.
2. Check live RX and TX/response ownership.
3. Defer the update if the relevant hardware is busy.
4. Publish complete control data.
5. Restore the prior PSM state.

Multi-word SHM records need a defined commit/ownership protocol.
The hardware does not make a sequence of SHM writes atomic.
