# D03: shared target costs and investigation priorities

D03 is implemented and qualified. It adds an analytical model, an opt-in
consumer and reached-code opportunity ranking. It does **not** change C06
profitability, C07 selection, allocation, scheduling, layout or defaults.
There is no claim of a target speedup from this milestone.

Implementation:

- [Normalized C interface](../../src/target_cost.h) and
  [source-backed rules](../../src/target_cost.c).
- [IR metrics consumer](../../tools/badge_ir_metrics.c).
- [Reached-code ranking](../../tools/rank-target-costs.mjs).
- [Native boundary tests](../../tests/target_cost_test.c) and
  [portable report/ranking tests](../../tests/target_cost_test.mjs).

## Separate dimensions, explicit unknowns

Queries distinguish instruction bytes, baseline issue cost, result readiness,
consumer-specific dependency bubbles, resource category/occupancy, memory
wait uncertainty and additional temporary-register demand. They do not add
these dimensions into one score. `known = 0` represents an unknown C range;
the report serializes it as `null`, never an invented zero cost.

Baseline timing describes the documented core conditions, not arbitrary
flash, peripheral or contended memory. Selecting core-baseline memory is an
explicit caller assertion. Warm XIP, cold XIP, MMIO, contention and unresolved
memory retain unknown downstream waits and unknown load-result readiness.
Stores do not produce a GPR result. Resource masks classify integer, multiply,
divider, memory and control work; they are not a dual-issue scheduling model.
Additional live registers/pressure remain unknown until a consumer supplies
actual allocation evidence.

The normalized API expects an already legal, correctly classified machine
operation. It is not an ISA decoder or a complete register/flag/alias graph.
In particular:

- Distinguish simple/exempt ALU operands from non-exempt complex forms;
  complex forms require explicit flag effects.
- Select load-to-PC operations as branches. Register counts describe the
  selected memory operation, not an IR aggregate's element count.
- Division magnitudes may give an exact documented baseline; unknown
  nonexceptional operands give a conditional 2..11 issue range. Zero operands
  and signed `INT_MIN / -1` remain unknown. Signed magnitudes use unsigned
  arithmetic, avoiding host signed-overflow undefined behavior.
- Branch queries need exact target width/alignment plus known taken/folding/
  history predicates. Without these, branch cost is unknown.
- Dependency queries distinguish early inputs, multiply-family accumulator
  inputs, late store data and earlier registers of multi-register loads.
  Unsupported accumulator producers and nonexistent earlier-load operands
  remain unknown. A late input is still a **semantic dependency**; unknown
  timing is not permission to reorder it. `intervening` counts elapsed
  independent issue cycles, not arbitrary IR instructions.

## Rule provenance

Stable IDs resolve through the [D01 fact ledger](fact-ledger.json), which
retains exact source editions, pages and conditions. Experiment IDs refer to
the [D02 kernels](../../tools/datasheet-experiments.mjs) and qualified
[hardware calibration](hardware-calibration.md), not workload recognition.

| Query/rule | Verified fact | Representative D02 experiment |
| --- | --- | --- |
| Simple/complex ALU | `M33-TIMING-ALU-001` | `simple-alu-dependency`, `complex-alu-result-delay` |
| Multiply | `M33-TIMING-MUL-001` | `multiply-dependency` |
| MAC/long MAC forwarding | `M33-TIMING-FWD-001` | `mac-operand-position`, `long-mac-accumulator` |
| Divide | `M33-TIMING-DIV-001` | `divide-255` and operand variants |
| Load | `M33-TIMING-LOAD-001` | `last-ldm-register` |
| Store | `M33-TIMING-STORE-001` | `store-address-versus-data` |
| Branch | `M33-BRANCH-001` | `wide-target-halfword` |
| Literal load | `THUMB-LITERAL-001` | `literal` |
| MOVW/MOVT pair | `THUMB-MOV-001` | `mov-pair` |
| ADD byte helper | `THUMB-ADD-001`, `THUMB-ADD-002` | Encoding legality, not a fitted cycle advantage |
| Unsupported category | `RP2350-CONFIG-001` | No timing rule inferred |

D02 evidence does not replace normative encoding legality or resolve all
contexts. In particular, its one-tick narrow/wide ADD sequence discrepancy
remains unresolved; the model does not introduce a correction constant.
FPU, DCP, GPIOC/RCP, exception timing and unmodeled instructions remain outside
the timing model.

## Encoding helpers and consumer stages

The ADD helper takes allocated R0..R12 operands, an immediate, and explicit
dead-flags/outside-IT proofs. Narrow imm3/destructive imm8 candidates are
considered only under those proofs. Wide imm12/modified-immediate legality
reuses the existing bounded immediate helper. Unsupported operands are
unknown, not instructions to synthesize.

The literal helper needs the final instruction/pool addresses and allocated
destination. It uses `Align(instruction + 4, 4)`, low-register/scaled-positive
T1 reach and signed wide reach, with alignment/overflow checks. Its answer is
instruction bytes only: pool data, padding and skip branches are excluded.
Neither helper is wired into selection in D03.

| Consumer stage | Available evidence and safe use |
| --- | --- |
| Early IR / current metrics | Conservative opportunity categories; no exact machine counts, byte totals or runtime estimates |
| C06 profitability, D07 | Definedness, legalization, operand ranges, expansion bytes and additional live values |
| Post-allocation selection, future D04 | Exact registers, flags/IT effects and legal encodings |
| Local scheduling, future D05 | Explicit effects, operand roles, alias/volatile/call barriers and pressure |
| Final layout, future D06 | Actual widths/addresses and branch-history evidence; no assumed universal folding |

[D07 IR profitability](ir-profitability.md) now consumes this model in
opt-in `target-strength-reduce`, with explicit constant materialization,
quotient-sharing and no-added-live-value gates, followed by actual allocated
machine-code qualification. D03's historical metrics remain analytical;
their occurrence counts are not promoted into runtime predictions.

## Opt-in metrics and ranking

From the workspace root:

```sh
make -C badge-compiler build/badge-ir-metrics
badge-compiler/build/badge-ir-metrics --target-costs INPUT.ll -o REPORT.json
make -C badge-compiler test-metrics test-target-cost
node badge-compiler/tools/rank-target-costs.mjs LINKED-INPUT.json
```

Without `--target-costs`, the historical report is preserved byte-for-byte.
With it, functions gain `target_costs` rows and the report gains `target_model`
with `runtime_prediction: false`. Rows include conditions, fact/experiment
links, occurrence counts and separately qualified dimensions.

The consumer classifies scalar memory operations up to 32 bits, i32 arithmetic
and branches. Div/rem rows identify divider opportunities, not guaranteed
single-instruction lowering. Other types/opcodes remain unknown. In particular,
`single_selected_instruction_baseline_issue` describes a normalized candidate,
**not** one selected instruction for every counted IR occurrence.

Ranking input has schema 1, linked `functions` with module/name, global
visibility, direct calls, unresolved indirect-call evidence and `target_costs`;
`profiles` supply a linked root, positive owned/reference measurements, name,
unit and source. Same-module targets resolve before unique globals; traversal
handles cycles and exposes unresolved calls rather than inventing their bodies.

The qualified maze input is
[ranking-input.json](../../../build/d03-cost-analysis-final/ranking-input.json).
It matches linked ELF symbols and map ranges to assembly call evidence and
backend-ready IR. The `before` metrics are used: rerunning C06 is not assumed
idempotent. Measurement roots are the actual RAM `draw_board` and
`physics_step`, not display transfer. Each reaches 13 linked functions, with
no unresolved calls in these scopes.

Using the user-confirmed Z05 measurements, physics has an owned/reference
ratio of 11.25 and RAM drawing 4.06. Priority sorts by measured scope gap,
then static reached coverage:

| Scope | D04 compact | D05 dependency | D06 branch | D07 division |
| --- | ---: | ---: | ---: | ---: |
| Physics | 204 | 110 | 101 | 18 |
| RAM drawing | 171 | 75 | 154 | 19 |

These counts are investigation coverage, not dynamic executions, legal
compact forms, schedulable pairs, CPU share or predicted savings. The ranking
therefore starts with physics compact encodings, dependency work, branches and
division, followed by drawing compact encodings, branches, dependencies and
division. Every `predicted_saving` is `null`; incompatible profile units/rates
are not summed.

## Qualification and analysis bounds

Final local evidence is [d03-results.json](../../build/datasheet/d03-results.json).
It records input/report hashes, model/consumer/ranking/test source hashes,
executable provenance, source-ledger/calibration/profile hashes and host runs.
The initial campaign remains separate historical evidence.

- All 45 qualified modules: 647 functions and 33,169 input IR instructions.
- Disabled reports byte-identical to the archived pre-D03 metrics executable.
- Enabled release/sanitizer reports byte-identical across all 45 modules.
- C07 assembly byte-identical to the archived qualified output for all 45.
- Qualified maze UF2 unchanged:
  `3c5ab4acb65ac119b556bf108b75943583223dbb175d883d8f17fec07bf48da9`.
- Existing metrics tests: 15 passed. New portable tests: 5 passed.
  Native cost-model sanitizer assertions and native ELF-metrics tests passed.

Classifier work counts instruction/token visits across report phases, with a
16,000,000 limit and explicit failure before output publication. Maximum
observed corpus work is 13,916. Counts use fixed per-function categories;
there is no per-instruction immediate search. Exact ADD queries reuse the
at-most-4,096-candidate modified-immediate helper. Ranking allows at most
50,000 linked functions, 200,000 traversed call edges across scopes and 32
profiles; the CLI additionally limits regular JSON input to 64 MiB.

Five alternating 45-input CLI campaigns measured median wall totals of
561 ms disabled and 570 ms enabled (about 1.6% higher), with overlapping
551..586 ms and 553..583 ms ranges. Summed reported CPU was 80 ms in every
campaign, at only 10 ms per-process resolution: a small CPU difference is
unresolved. Maximum individual-process RSS varied roughly 26.4..38.5 MB,
without a stable increase established.

These measurements include startup, parsing, C06 and report I/O, not just the
classifier. They are not comparable to Z06's in-process C07 measurements.
The model is linked only into the metrics consumer; ordinary code generation
does not call it. No extraction/manual acquisition or hardware flashing is
needed for ordinary tests. D04 is the next separately qualified candidate;
this report does not promote any new production policy.
