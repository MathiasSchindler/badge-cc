# Datasheet-driven RP2350 compiler optimization

Proposed on 2026-10-09. D00 and D01 are implemented and qualified for the local
source editions; D02 is implemented and hardware-qualified in its calibration
report. D03 is implemented and qualified as an opt-in analytical model;
D04 is implemented and selection-only qualified as an opt-in size candidate;
its repeated runtime regression prevents speed promotion. D05 is implemented
and offline-qualified; D09 completes repeated suite timing after fixing
collector teardown. D06 is implemented as opt-in `thumb-relax`;
alignment/shared-pool decisions remain deferred and D09 rejects speed promotion.
D07 is implemented as opt-in
`target-strength-reduce`, with no resolved suite speed benefit in D09. D08 is implemented
and evidence-qualified as six independent decisions; no features or defaults
change. D09 is implemented and qualified for the independent D04-D07 candidates;
defaults remain unchanged and production-runtime coverage is not certified.
This document plans source extraction, target
research, experiments and compiler work. Extraction does not establish new
compiler optimizations or hardware timing claims.

## Objective and boundaries

Turn the RP2350 and Arm documentation into a small, traceable, experimentally
checked target model that improves generated code across the freestanding
corpus. Optimize execution time, code size and stack demand without confusing
those objectives or promising a globally optimal binary.

This follows the qualified Z01-Z06 work in the
[maze optimization plan](maze-optimization.md). Preserve the current
`direct/global-compact/thumb-layout/word` defaults and
`mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division` pipeline
until a separately qualified candidate earns promotion.

Scope is the badge's RP2350 Arm Cortex-M33 configuration, not Hazard3/RISC-V,
a generic multi-target framework, or a requirement to reproduce LLVM.
Firmware remains freestanding C without external dependencies or a standard
C library. Host-only extraction and reporting tools must not become
dependencies of ordinary firmware builds.

Research should identify missing features as well as optimization
opportunities. FPU, double-precision and peripheral-coprocessor support are
separate feature decisions, not prerequisites for speeding up integer code.
Do not change the maze's fixed-point arithmetic to manufacture a hardware win.

## Source material and priority

The local [RP2350 datasheet](../../pimoroni/RP-008373-DS-3-rp2350-datasheet.pdf)
currently contains 1,371 PDF pages. Start with the relevant sections, not a
chapter-by-chapter rewrite of the entire document:

| Source | Initial focus | Purpose |
| --- | --- | --- |
| RP2350 datasheet | Chapter 2 system bus; SRAM and flash/XIP sections | Memory routing, contention, placement and access costs |
| RP2350 datasheet | 3.7.2 configuration and Raspberry Pi modifications | Actual implemented features, including `UNCROSS_I_D` |
| RP2350 datasheet | 3.7.4.9 instruction execution timing | Dependencies, forwarding, division, branches, folding and instruction widths |
| RP2350 datasheet | 3.6 coprocessors | GPIOC, DCP, RCP and single-precision FPU boundaries |
| Cortex-M33 Technical Reference Manual | Pipeline, interfaces and execution behavior | Core implementation details and timing assumptions |
| Armv8-M Architecture Reference Manual | Instruction semantics and restrictions | Legality, flags, exceptions, alignment and architectural behavior |
| AAPCS32 | Calls, register classes and stack rules | ABI-safe allocation, prologues and future floating-point support |
| Applicable RP2350/Arm errata | Relevant silicon/core revisions | Correctness restrictions and required workarounds |

Record exact editions and applicability before relying on external sources.
Generic Cortex-M33 documentation does not override RP2350-specific
configuration or bus modifications. Resolve apparent conflicts explicitly;
do not silently choose the more favorable timing.

## Documentation and extraction layout

Use three distinct layers:

1. **Original sources:** retain the PDFs as received, with hashes and edition
   metadata. Do not modify or replace the existing local datasheet.
2. **Generated extraction:** page-addressable plain text and table evidence
   for search and verification, kept separate from authored explanations.
3. **Authored Markdown:** concise topic notes, verified facts, uncertainties
   and compiler implications, with references back to exact source locations.

Proposed authored files, created as their research milestones are completed:

```text
badge-compiler/docs/datasheet/
  README.md                    # Navigation, scope and reading priorities
  sources.md                   # Editions, hashes, rights and extraction method
  rp2350-chapter-map.md         # Relevant source chapters and page mapping
  instruction-semantics.md     # Encodings, flags, restrictions and DSP features
  instruction-timing.md        # Latency, issue/occupancy and forwarding
  registers-and-abi.md         # Register constraints, preservation and calls
  branches-and-layout.md       # Folding, alignment, widths and literal pools
  memory-system.md             # SRAM, flash/XIP, buses and contention
  coprocessors.md              # FPU, DCP, GPIOC and RCP; separate feature scope
  errata-and-open-questions.md  # Applicability, conflicts and unresolved facts
  experiments.md               # Methods and links to measured evidence
```

Keep generated bulk extraction under a dedicated ignored build directory,
initially `badge-compiler/build/datasheet/`. Do not put thousands of mechanically
generated pages into the authored documentation or create one Markdown file
per PDF page. Topic notes may cross-reference several source chapters.

### Extraction and reference contract

- Use an available local extractor first. macOS PDFKit is available in the
  current environment; a layout-preserving extractor such as `pdftotext` can
  be evaluated as an optional alternative. Do not upload source documents
  to third-party conversion services.
- Record source SHA-256, title, revision/date, page count, extraction tool and
  version, command/options, and generated-output hashes in a manifest.
- Preserve page boundaries, preferably as `pages/0001.txt` and so on, with
  an index mapping **1-based PDF page index** to **printed page label**,
  chapter, section and table identifiers.
- Distinguish these page identities in every reference. For example,
  the current PDF's Cortex-M33 configuration appears on PDF page 117 with
  printed page label 116. Do not infer all page labels from a fixed offset.
- Use stable fact identifiers such as `M33-TIMING-MUL-001`. A fact record
  includes source edition/hash, section/table, both page identities,
  conditions, confidence, experiment links and compiler consumers.
- Separate `documented`, `visually verified`, `hardware measured`,
  `hypothesis` and `unresolved` claims. Missing data is unknown, not a
  zero-cycle cost or an assumed supported feature.
- Preserve table row/column associations, units, footnotes and conditions.
  Visually verify every table or formula used by a compiler decision
  against the original PDF. Plain-text extraction alone is insufficient.
- If extraction loses symbols, diagrams, subscripts or layout, mark the
  item unresolved and use the original page for verification. Do not
  silently repair it from guesses or another source.
- Check source licenses and redistribution permissions, including embedded
  third-party material, before committing extracts. The local RP2350 PDF
  declares CC BY-ND and includes third-party notices. Do not assume that
  a converted or edited full-text redistribution is permitted.
  Prefer original analysis, factual records, attribution and source links;
  keep bulk source extracts local unless their distribution is cleared.

## Milestones

Dependencies below are actual prerequisites, not a demand that all research
be performed serially. Each implementation candidate remains opt-in until
D09 qualification.

| Milestone | Deliverable | Dependencies | Status |
| --- | --- | --- | --- |
| D00 | Reproducible source inventory, page extraction and reference map | None | Done; all 1,371 pages, repeatability and source-navigation checks qualified |
| D01 | Verified topic notes and target facts ledger | D00 | Done; 44 sourced facts, seven topic notes and source/state limits qualified |
| D02 | Hardware timing experiments and repeatability baseline | D01 timing/configuration subset | Done; six paired rounds, 16,380 raw samples, 49 tests; one sequence discrepancy remains unresolved |
| D03 | Small shared RP2350 cost model and opportunity ranking | D01, D02 | Done: model, opt-in analysis and 45-module qualification |
| D04 | Compact instruction selection and encoding-aware allocation candidates | D03 | Done: selection-only size candidate; no speed promotion or allocation change |
| D05 | Dependency-aware bounded local scheduling candidate | D03 | Done; D09 suite wins, retained opt-in pending production-runtime coverage |
| D06 | Branch, alignment and literal-pool layout candidates | D03; D04 before combined-width qualification | Done; D09 rejects speed promotion, opt-in `thumb-relax`, alignment/shared pools deferred |
| D07 | Target-informed C06 profitability decisions | D03; measured backend candidates as applicable | Done; D09 finds no resolved suite speed benefit, opt-in unsigned strength reduction |
| D08 | Memory-placement and coprocessor feasibility decisions | Relevant D01/D02 results | Implemented/decision-qualified; six independent records, bounded follow-ups only, production unchanged |
| D09 | Per-candidate corpus/hardware qualification and promotion decision | The candidate milestone being evaluated | Done; five offline profiles, ten restored hardware campaigns, exact noise gates, defaults unchanged |

### D00 - Source inventory and reproducible extraction

Implemented by the optional local PDFKit extractor and indexed page corpus.
See the [reference corpus guide](datasheet/README.md),
[source inventory](datasheet/sources.md), [chapter map](datasheet/rp2350-chapter-map.md)
and [qualification/limitations](datasheet/validation.md).

Create the source manifest, extraction tool and page mapping. Start with the
local RP2350 PDF; add exact Arm manuals, ABI documents and errata as needed.
Create the documentation index and chapter map, not all topic notes at once.

Acceptance:

- Re-running extraction on the same source/tool/options reproduces the
  recorded content hashes, excluding explicitly identified metadata.
- Every source page is accounted for; empty or unextractable pages are
  reported, not dropped.
- A section, printed page label or PDF page index can be resolved back to
  the original document.
- Visually check representative timing tables, multi-column text, formulas
  and footnotes. Record extractor limitations and rights decisions.
- No compiler behavior, firmware dependencies or target output changes.

### D01 - Verified target knowledge

Implemented in the [topic notes and reference guide](datasheet/README.md),
[fact ledger](datasheet/fact-ledger.json) and
[qualification](datasheet/d01-validation.md). RP2350, Armv8-M ARM and AAPCS32
are pinned; the independent M33 TRM cross-check and actual badge state remain
explicitly unresolved. No cost model, compiler behavior or hardware timing
claim is introduced.

Populate the topic notes and facts ledger. Separate instruction legality
from execution cost, and distinguish:

- Encoding size and operand/immediate restrictions.
- Register and APSR flag definitions/uses, including implicit effects.
- Issue cost or resource occupancy versus result availability.
- Consumer-specific forwarding and dependency penalties.
- Operand-dependent timing, especially division.
- Branch taken/not-taken behavior, folding and alignment conditions.
- Memory wait states and instruction-fetch assumptions.
- Actual enabled features versus optional features of a generic M33.
- ABI, Secure/Non-secure access, startup and interrupt-state requirements.

Acceptance: every compiler-actionable claim has a source reference,
conditions and confidence; unknowns and errata remain visible. Explain
`UNCROSS_I_D`, DSP, FPU and the differences between DCP and a conventional
double-precision FPU. Do not infer complete execution timing from opcode
counts or treat RCP as a general performance accelerator.

### D02 - Hardware experiments and calibration

Implementation and reproduction are documented in
[hardware calibration](datasheet/hardware-calibration.md). Qualification
requires the complete six-round hardware campaign, not just the offline tests.

Build explicit instruction-sequence experiments using the existing
freestanding firmware and two-badge capture infrastructure where practical.
Microbenchmarks measure mechanisms; compiler code must not recognize them.

Measure at least:

- Dependent versus independent ALU, multiply and load sequences.
- Multiply-accumulate forwarding and representative division operands.
- Relevant 16-bit/32-bit forms and different branch-target alignments.
- Taken/not-taken branches and documented folding opportunities.
- Literal loads versus immediate construction.
- Execution from SRAM and flash/XIP, with cold/warm conditions distinguished.
- Relevant memory-bank or bus contention, isolated from ordinary tests.

Acceptance:

- Inspect final linked disassembly so encodings, addresses and alignment
  actually match the experiment.
- Document timing-source resolution, units, clock configuration, harness
  overhead, interrupt policy, result consumption and compiler barriers.
- Use enough repetitions for the timer resolution, prevent eliminated work,
  verify outputs, and distinguish startup effects from steady-state timing.
- Repeat control/control measurements, swap badge roles and retain raw
  samples. Establish a noise envelope before candidate comparisons.
- Record whether observations support, contradict or cannot resolve each
  documented prediction. Do not fit an unexplained correction constant.

Use QEMU and LLVM for correctness and independent compilation comparisons,
not as cycle-accurate timing evidence.

### D03 - Shared cost model and ranked opportunities

Implementation, caller obligations, reached-code ranking and qualification
are documented in the [target cost model report](datasheet/target-cost-model.md).
All 45 modules preserve disabled metrics and qualified target assembly;
enabled release/sanitizer reports agree. The model changes no production
defaults and predicts no summed runtime.

Introduce the smallest target-specific representation needed by actual
consumers. Extend existing target/selection helpers where appropriate;
do not first build a generic architecture database or compiler framework.

Keep distinct dimensions for code bytes, baseline execution cost, result
dependencies, resource constraints and register-pressure effects.
Represent operand-dependent or memory-dependent costs as conditional
estimates/ranges, with unknowns explicit. Do not sum one fixed cycle value
per opcode and label the result predicted runtime.

Acceptance:

- Each modeled rule points to a verified fact and, where feasible, a
  hardware experiment.
- Test representative comparisons, boundary conditions and unknown cases.
- Rank opportunities using reached corpus code and existing profiles, not
  only synthetic sequences or static instruction totals.
- Define which consumers need exact machine operands/addresses and which
  may use an earlier conservative estimate.
- Preserve current defaults and target output when the model is disabled.
- Record host time/memory and analysis-work bounds; the model must not undo
  Z06's host improvements through unbounded analysis.

### D04 - Compact encodings and allocation economics

Implementation and qualification are documented in
[compact selection](datasheet/compact-selection.md). `thumb-compact` reduces
linked text in all 12 images and preserves allocation and correctness gates.
Repeated signed-div/rem timing regresses, so the candidate remains opt-in and
size-oriented. No allocation-economics candidate is justified/promoted here.

First choose legal compact forms using already allocated registers, without
changing allocation policy. Test operand ranges, register restrictions,
implicit flag writes, live flags and encoding boundaries. A shorter
instruction is not automatically a semantically equivalent replacement.

Then, as a separate candidate if evidence justifies it, make allocation
aware of low-register encoding benefits. Account for introduced moves,
spills, live ranges, saved-register costs and existing scratch/PHI/frame
reservations. Do not force all values into low registers.

Acceptance: assembler/disassembler boundary tests, semantic oracles,
adversarial flag/register-pressure cases, and measured linked bytes and
runtime. Qualify selection-only and allocator changes independently.

### D05 - Dependency-aware local scheduling

Implementation and evidence are documented in
[local scheduling](datasheet/local-scheduling.md). Opt-in `thumb-schedule`
uses eight-record typed windows, physical dependencies and strict local
readiness/exit gates without changing allocation, widths or defaults.
Correctness, 45-module preservation and 12-image layout gates pass.
Historical D05 single-role data and two collector timeouts are retained.
D09 fixes teardown and completes repeated role-swapped suite timing;
production-runtime coverage remains open. Both badges were restored and
independently verified; no speed promotion is claimed.

Start inside one basic block with a bounded window and explicit register,
flag and memory effects. Prefer placing genuinely independent work between
a producer and delayed consumer when measured benefits justify it.

Integrate at a structured C07 selection/emission boundary; do not rewrite
assembly strings. If the current emitter lacks effect information, add only
the local representation needed for this milestone, not a full machine-IR
rewrite.

Acceptance: preserve dependencies and aliasing; honor calls, volatile/MMIO,
inline assembly and other side-effect barriers; do not speculate across
branches. Test implicit operands, flags, PHIs, scratch clobbers and pressure.
Measure both stalls avoided and any extra moves/spills introduced. Bound
analysis work and surface budget/unsupported-case behavior explicitly.

### D06 - Branches, alignment and literal pools

Implemented and offline-qualified as opt-in `thumb-relax`; see the
[final-layout contract and evidence](datasheet/final-layout.md).
Eligible symbol addresses use relocatable MOVW/MOVT pairs; structured
CFG/return/switch branches use bounded final-width relaxation. PHI,
allocation and guarded Z05 eligibility rules are preserved. Directive
padding/section positions participate in convergence; explicit `.n`
violations and budget exhaustion diagnose without replacing output.

All 12 fresh corpus images have smaller linked text, with unchanged
data/BSS; ordinary and instrumented maze text each decrease by 11.45%.
Generated LLVM/LLD, exhaustive native fixups, branch/literal boundaries,
final-byte audits, owned/LLVM execution and actual maze correctness pass.
Default C07 assembly and 45 default assembler objects remain byte-identical.
No D04/D05 composition or default promotion is made.

Static alignment geometry and optimistic shared-pool bounds receive
explicit no-go/defer decisions in the report. They do not justify blanket
padding, new pool placement or runtime claims. D06 itself was offline-only;
D09's subsequent repeated hardware gate rejects speed promotion after
resolved cross-file ABI, branch-PHI and signed-div/rem regressions.

Use final instruction widths and structured CFG edges to investigate branch
folding, selective hot-target alignment, constant materialization and shared
literal-pool placement. Extend the qualified Z05 behavior rather than
replacing its edge/PHI correctness rules with text peepholes.

Acceptance:

- Test branch/literal reach, relocation, padding and instruction-boundary
  limits, including changes that push another reference out of range.
- Keep embedded data non-executable and preserve PHI transfers.
- Make layout/relaxation converge within explicit bounds.
- Validate final linked addresses and encodings, not estimated source
  assembly widths.
- Measure padding and pool overhead as well as removed instructions.
  Blanket loop alignment or narrower branches are not default policies.

### D07 - Target-informed IR profitability

Implemented and offline-qualified as opt-in `target-strength-reduce`; see
[the contract and qualification](datasheet/ir-profitability.md).
The bounded first opportunity replaces unflagged unsigned i32 power-of-two
div/rem with shifts/masks. Shared-model decisions include actual baseline
constant materialization, adjacent quotient sharing, code-growth and
no-added-live-value constraints. No motion, signed expansion or inlining is
enabled, and Z04's rejected `loop-cost` is not revived.

Fresh qualification changes 38 IR sites across nine of 37 modules,
removing 35 UDIV and 17 MLS instructions. Six of twelve images shrink and
six are unchanged; data/BSS, calls and stack uncertainties are preserved,
with no frame growth across 552 functions. Maze text decreases by just
12 bytes. Native flags/poison/PHI tests, allocated lowering and owned/LLVM
execution, actual maze correctness and exact default-output gates pass.
Defaults are unchanged; D09's repeated hardware gate finds no resolved
suite speed benefit or regression.
Other IR profitability opportunities remain separately scoped future work.

Apply the shared model to selected C06 decisions: strength reduction,
expression reuse, loop motion, induction/address work and bounded inlining.
Choose one measured opportunity at a time.

Acceptance: include register pressure, lowered addressing forms, code
growth and lost fusion in profitability. Compare the resulting machine code,
not just shorter IR. Preserve typed arithmetic, overflow/poison rules,
volatile behavior and canonical IR boundaries. Revisit the rejected Z04
loop-cost experiment only with a new explanation and measured evidence;
documentation alone does not justify promotion.

### D08 - Memory and coprocessor decision records

Implemented as six independent [decision records](datasheet/README.md#d08-independent-decisions)
with [hash-bound evidence](../build/datasheet/d08-results.json):

| Record | Outcome |
| --- | --- |
| [Memory placement](datasheet/memory-placement.md) | GO S08-MEM selected-function experiment; no blanket placement |
| [DSP/scalar long multiply](datasheet/dsp-decision.md) | GO S08-LMUL signed widening selector; packed DSP deferred |
| [FPU](datasheet/fpu-decision.md) | Defer end-to-end support; no maze arithmetic conversion |
| [DCP](datasheet/dcp-decision.md) | No strict automatic double substitution; defer explicit relaxed API |
| [GPIOC](datasheet/gpioc-decision.md) | GO S08-GPIO explicit-helper state/waveform pilot; no generic MMIO rewrite |
| [RCP](datasheet/rcp-decision.md) | No performance feature; separate requested hardening project |

Evidence checks 37 control IR modules and 12 images: no executable main-SRAM
section or floating type is reached; five signed widening multiply patterns
are syntactic candidates, not proved profitable rewrites. SMULL's linked text
matches LLVM. Direct flash-to-SRAM BL fails without veneers; an address-pair
indirect fixture links, without proving ABI/startup execution. Current FPU/
coprocessor forms are rejected with output preservation. All 92 captured
production/tool/fact hashes and the control IR/ELFs remain unchanged.
No badges are programmed and no hardware speed/panel-safety claim is made.

Produce independent go/no-go records rather than one large feature project:

- **Memory placement:** determine whether hot-code/data placement can help
  given actual SRAM/flash execution, bank usage, startup copying, stack,
  BSS, DMA and other-core contention. Preserve memory budgets and existing
  sections; do not move all code to RAM.
- **DSP:** identify existing scalar patterns that match legal instructions;
  packed arithmetic needs a proven semantic match, not guessed vectorization.
- **FPU:** specify frontend/IR coverage, ABI choice, initialization,
  floating-point semantics and interrupt/context handling before lowering.
- **DCP:** assess complete operation sequences, special cases, rounding,
  state and calling costs. Do not model it as one hardware instruction per
  C `double` operation.
- **GPIOC:** investigate explicit fast-I/O helpers with preserved hardware
  semantics, not automatic replacement of arbitrary volatile accesses.
- **RCP:** document security relevance separately from performance work.

Acceptance: each record states applicability, expected beneficiaries,
correctness requirements, dependency-free implementation options, expected
costs and evidence. A go decision opens a separately bounded implementation
milestone; D08 does not silently commit to all features.

### D09 - Qualify each candidate and decide promotion

Implemented: [qualification, method and outcomes](datasheet/qualification.md)
and [machine-readable evidence](../build/datasheet/d09-results.json).
Five fresh profiles each pass 37 modules, 12 images, an instrumented maze
and 552 frame/call checks. Ten paired campaigns (20 role-swapped rounds)
complete after diagnosing/fixing selected-device USB teardown. Both original
full flash images are restored and independently verified.
D04 and D06 have resolved regressions and are not promoted; D05 has
repeatable shared-code suite wins but lacks production-runtime coverage;
D07 has no resolved suite speed benefit. Exact integer-rational boundaries
preserve one-tick equality as unresolved. All defaults remain unchanged.

This gate is repeated for each candidate; it does not wait for every research
milestone. Archive a fresh control before changing production behavior.

Acceptance:

- Run targeted boundary tests, relevant native sanitizer matrices and
  owned/LLVM differential execution, including mixed ABI and raw PHIs where
  affected. Retain negative diagnostics and failure/output-preservation tests.
- Build the actual full corpus and instrumented maze in fresh build
  directories. The current qualification corpus has 37 C modules and
  12 linked images; enumerate and record the actual set rather than reusing
  historical 36-module/11-image collector assumptions.
- Check all workloads' results, code/data/BSS, frame/call metadata and
  conservative stack uncertainties. No invented callback or interrupt bound.
- Repeat hardware comparisons with controlled seeds, clock settings and
  role swaps. Restore and verify the playable applications after campaigns.
- For speed-default promotion, require an improvement beyond the D02 noise
  envelope in a reached real-corpus workload, with no unexplained repeatable
  regression beyond the predeclared envelope in other workloads. Check
  code-size and memory-budget tradeoffs explicitly.
- If a size-oriented candidate trades runtime for bytes, keep that tradeoff
  explicit and separately selected; do not quietly change the speed default.
- Preserve explicit old controls, shared CLI/API policy behavior and stage
  contracts. Research/extraction alone must preserve target bytes; intentional
  code-generation changes require semantic equivalence, not identical assembly.
- Persist raw evidence, source/tool/firmware hashes, measured effects and
  limitations. Update the topic notes and this milestone table with the
  outcome, including rejected candidates.

## Recommended next execution slice

D00-D09 are complete for their bounded scopes, with independent D04-D07
qualification and no default promotion. Collector cleanup is fixed.
Resolve D04's signed-div/rem and D06's ABI/branch/signed-divrem regressions
before promoting or combining those candidates. D05 needs reached physical
maze/peripheral timing and conservative production stack/interrupt coverage
before speed-default review. D07's instruction reduction has no resolved
suite speed benefit; other IR opportunities need their own gates.
Alignment/shared pools remain deferred. D08's six
independent decisions are complete. S08-LMUL is the bounded compiler
opportunity; S08-MEM and S08-GPIO first require placement/state/electrical
gates. Floating features and RCP hardening remain separate deferred projects.
Reuse the D09 gate with fresh controls for any follow-up, combination or
allocation-economics change. Static coverage and smaller encodings are not
speedup predictions.

Do not block integer optimization on comprehensive peripheral documentation,
floating-point support or an exhaustive reading of every PDF page.
