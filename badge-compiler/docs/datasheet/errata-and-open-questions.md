# Errata and unresolved target questions

Evidence: [fact ledger](fact-ledger.json), `RP2350-ERRATA-E27`,
`RP2350-ERRATA-E11`, `RP2350-ERRATA-E26`, `RP2350-BOARD-STATE-001`;
[source catalog](reference-sources.json). Errata are from the pinned RP2350
Appendix D, not an assertion that every entry applies to the attached badges.

| Entry | Exact reference: PDF / printed pages | Impact and gate |
| --- | --- | --- |
| E27, D.3.1 | 1357-1358 / 1356-1357 | A2/A3/A4 APB/FASTPERI priority controls miswired; ROM/SRAM/XIP controls documented as correct. Check full mapping before contention tests. |
| E11, D.10.1 | 1366-1367 / 1365-1366 | A2/A3/A4 clean-by-set/way can retag dirty XIP lines. Maintenance experiments need the documented workaround. |
| E26, D.8.1 | 1365 / 1364 | RCP delay caveat; non-delay guidance and A3 bootrom change. Not a performance-optimization primitive. |

These entries were text-reviewed against the exact source, not all promoted
to visual-table verification. Their confidence is high **conditional** on
revision and operation. No workaround/default is enabled by D01.

## Explicit unresolved questions and handoff

| Question | Why it matters | Next evidence |
| --- | --- | --- |
| Actual silicon revision | Errata applicability | Non-destructive revision read on each badge |
| Application-entry security/CPACR/NSACR/FPSCR state | Coprocessor legality and FP semantics | Runtime observation with permitted accesses |
| Active clocks/QMI/cache/IRQ/DMA/other core | Timing environment | D02 campaign metadata and controlled variants |
| MAC forwarding exact forms | Consumer-position rules, destination wording/examples | Exact dependent/independent MLA and long-MAC sequences |
| Fold history and target/pool addresses | Branch/fetch profitability | Final-byte layouts plus hardware alignment sweep |
| Code/data placement tradeoffs | SRAM banks, XIP misses, budgets | Paired placement experiments, not opcode counts |
| Standalone M33 TRM cross-check | Independent implementation reference | Acquire exact edition/hash; current portal/static links failed |
| Full ISA effects beyond starter forms | Scheduler/narrowing correctness | Add form-specific architecture references before new rules |

The RP2350 datasheet explicitly reproduces licensed Cortex-M33 TRM material.
It is the current implementation reference; the separately acquired architecture
manual establishes semantics, not device timing. The missing independent TRM
must not be counted as an acquired/verified source. No more download guesses
are needed to use the pinned, applicable RP2350 material.

## Promotion gate

Documentary confidence is not hardware validation. QEMU/oracles can establish
correctness, not cycle costs. A later candidate needs broad compiler/corpus
correctness, paired hardware measurements and explicit uncertainty before
changing a target default. Keep size, speed and host compilation improvements
separate; a smaller opcode sequence is not automatically a faster maze.
