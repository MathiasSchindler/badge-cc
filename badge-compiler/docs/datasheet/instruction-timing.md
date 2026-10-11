# Instruction timing: issue, availability and consumers

Evidence: [fact ledger](fact-ledger.json), `M33-TIMING-BASE-001`,
`M33-TIMING-ALU-001`, `M33-TIMING-ALU-002`, `M33-TIMING-MUL-001`,
`M33-TIMING-FWD-001`, `M33-TIMING-DIV-001`, `M33-TIMING-LOAD-001`,
`M33-TIMING-STORE-001`. References point to RP2350 section 3.7.4.9,
PDF pages 128-130, printed 127-129, including tables 122/123.

## Scope of confidence

These are high-confidence **conditional documentary facts**, not hardware
results from our badges. The datasheet itself describes a limited measured
sample under one active core, no cache misses and no active DMA. It explicitly
does not cover every encoding, exceptional case or full timing environment.
Do not sum opcode costs to claim exact runtime or WCET.

Keep separate:

1. Encoded size and legality.
2. Instruction issue/resource occupancy.
3. Producer result availability.
4. Consumer operand position and forwarding.
5. Fetch, data-memory and external contention.

## Candidate classifications, not an executable cost table

| Mechanism | Documentary baseline | Important qualification |
| --- | --- | --- |
| Simple ALU | Usually one cycle | Exact operand form and flags matter |
| Complex ALU | Possible one-cycle result-use penalty | Complex flag-setting forms pay even with independent next work |
| Multiply/MAC | One-cycle issue; result at n+2 | Multipliers needed at n, accumulator at n+1 |
| Divide | Operand-dependent | Immediate result use adds a cycle |
| Load | One cycle per register, possible result delay | LDM/POP delay attaches to last loaded register, except PC |
| Store | Early dependency concerns address inputs | Store-data dependence still exists semantically |

The simple shifted ALU operand exception is LSL #0..3. Plain imm12 ADD/SUB,
immediate MOV/MOVS, UBFX/SBFX/BFI and standalone ASR/LSL/LSR/ROR have the
documented exemptions (`M33-TIMING-ALU-002`). A cheaper form is useful only
when semantics, legal operands, encoded size and surrounding dependencies agree.

For ordinary defined division, let `n` be the difference between highest-one
positions in the absolute dividend and divisor. The described execution cost
is 2 cycles for `n < 0`, otherwise `4 + floor(n/4)`, plus the result-use
qualification. Zero-divisor/trap and signed exceptional cases are not supplied
by this performance formula. Numerator zero needs separate treatment because
its highest-one position is undefined. IR legality still governs transforms.

## Forwarding is consumer-specific

The late MAC accumulator input can receive a previous result without the
ordinary early-input bubble. This does not apply indiscriminately to multiplier
operands. The prose's destination wording and alternating-destination examples
must not become a simplistic register-equality test. D02 must compare the exact
documented sequences, alternate destinations, multiplier dependencies and
long-multiply register-pair cases before D05 encodes a rule.

The load/store source tables describe **pipeline timing sources**, not complete
semantic use sets. A store's value register remains a dependency even though
the relevant result-use stage reads its address registers earlier. Removing
that use from an instruction graph would permit incorrect reordering.

## D02 handoff

Measure dependent and independent sequences separately, subtract/control
measurement overhead, and retain final bytes/addresses. Compare consumers in
ALU, address-generation, store-data and accumulator positions. Sample division
operand classes rather than fit one universal latency. Include warm-XIP/SRAM
and controlled competing-bus conditions from [memory-system.md](memory-system.md).
QEMU can establish correctness, not these hardware timing predicates.
