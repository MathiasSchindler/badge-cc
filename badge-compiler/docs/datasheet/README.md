# Datasheet reference corpus

D00 provides the local RP2350 extraction and reference map. D01 adds authored
target notes and a source-backed fact ledger, not a redistributed conversion
of the PDFs. D02 adds qualified two-badge hardware calibration. D03 adds a
shared analytical target model and opt-in opportunity reports. D04-D07 add
separately selected compact, scheduling, final-layout and IR-profitability candidates;
speed-default promotion remains gated by the
[milestone plan](../datasheet-plan.md).
D08 adds six independent evidence-backed decisions below, not enabled
features or a hardware speed claim.

- [Sources and extraction contract](sources.md): exact edition, tools, rights
  and reproducibility limits.
- [RP2350 chapter map](rp2350-chapter-map.md): high-priority section destinations.
- [D00 qualification](validation.md): coverage, repeated extraction, visual
  checks and known formatting losses.
- [Machine-readable source inventory](rp2350-source.json): locked source hash.
- [D01 source catalog](reference-sources.json): RP2350, Armv8-M ARM and AAPCS32
  exact editions/hashes, rights, applicability and reviewed-page evidence.
- [D01 fact ledger](fact-ledger.json): 44 bounded claims with stable IDs,
  conditions, confidence, exact PDF/printed pages and potential compiler consumers.
- [D01 qualification](d01-validation.md): checks and unresolved source/state limits.
- [D02 hardware calibration](hardware-calibration.md): explicit kernels,
  repeat/role-swap methodology, persistent transport and recovery guarantees.
- [D03 target cost model](target-cost-model.md): normalized query conditions,
  opt-in analysis, reached-code priorities, preservation and host-work bounds.
- [D04 compact selection](compact-selection.md): selection-only size candidate,
  legality/flag gates, linked bytes and repeated hardware tradeoffs.
- [D05 local scheduling](local-scheduling.md): typed physical effects, bounded
  readiness scheduling, correctness/layout preservation and retained partial
  historical hardware evidence; D09 completes the repeated suite gate.
- [D06 final-width layout](final-layout.md): relocatable address pairs,
  bounded structured branch relaxation, linked-byte qualification and
  explicit alignment/shared-pool deferrals; D09 rejects speed promotion.
- [D07 IR profitability](ir-profitability.md): bounded unsigned strength
  reduction with shared-model materialization/fusion costs, actual pressure
  checks and fresh corpus qualification; defaults are unchanged.
- [D09 qualification and promotion](qualification.md): five independent
  offline profiles, ten repeated role-swapped campaigns, exact noise
  boundaries, collector teardown fix and verified original-image restoration.
  D04/D06 regress, D05 needs production-runtime coverage, and D07 has no
  resolved suite speed benefit. All remain explicit candidates.

## D08 independent decisions

GO opens a bounded follow-up, not default promotion. D08 changes no compiler,
startup, linker, display helper or badge firmware.

| Area | Decision | Follow-up |
| --- | --- | --- |
| [Memory placement](memory-placement.md) | GO for selected-function experiment; no automatic/blanket relocation | S08-MEM: call range, copying, budgets, stack/banks and reached timing |
| [DSP and scalar long multiply](dsp-decision.md) | GO for scalar SMULL pattern; defer packed DSP | S08-LMUL: legal signed widening selection, pressure and differential execution |
| [FPU](fpu-decision.md) | Defer end-to-end support; no fixed-point maze conversion | Reopen with a real floating workload, semantics, base PCS and context contract |
| [DCP](dcp-decision.md) | No strict automatic double substitution | Reopen one explicit relaxed/FTZ operation with state and numerical evidence |
| [GPIOC](gpioc-decision.md) | GO for explicit-helper state/waveform pilot; no generic MMIO rewrite | S08-GPIO: permissions, masks, panel timing and whole-loop comparison |
| [RCP](rcp-decision.md) | Not a performance optimization | Separate requested hardening project and threat model |

[D08 machine-readable evidence](../../build/datasheet/d08-results.json)
binds 92 unchanged production/tool/fact hashes, 37 IR modules and 12 control
ELFs to SRAM/frame/type-pattern inspection and assembler/frontend/linker
probes. Negative probes preserve existing output files. Linked SMULL text
matches LLVM; flash-to-SRAM BL fails without veneers, while an address-pair
indirect call links. No probe establishes runnable RAM startup, floating
execution, GPIO waveform timing or a speedup.

The records cite the pinned D01 source editions and historical D02 state.
Production-entry state remains unresolved. Fresh D09 qualification is still
required for every implemented follow-up; the collector teardown fix and
completed D04-D07 suite campaigns do not qualify unimplemented D08 features.

## D01 topic notes

- [Instruction semantics and target features](instruction-semantics.md)
- [Issue, result availability and forwarding](instruction-timing.md)
- [Registers, APSR and ABI](registers-and-abi.md)
- [Branches, folding and literal layout](branches-and-layout.md)
- [Memory system and fetch](memory-system.md)
- [DSP, FPU, GPIOC, DCP and RCP](coprocessors.md)
- [Errata and unresolved questions](errata-and-open-questions.md)

These are original, bounded analyses. `documented` means reviewed extracted
text; `visually_verified` additionally requires every cited page to be in the
recorded human-reviewed image set; `unresolved` states an open question.
Confidence concerns the conditional claim, not predicted performance.
No fact is hardware-measured in D01, and consumer paths are future integration
points, not a claim that code already uses the facts.

Validate metadata without local manuals, or include source files and images:

```sh
node badge-compiler/tools/check-datasheet-facts.mjs
make -C badge-compiler test-datasheet-facts
```

The optional Make target runs the portable tests and full file verification.
It requires the pinned local PDFs, raw extracts and reviewed images. Ordinary
compiler tests include the portable ledger tests without those artifacts.
There is no automatic download or dependency on local manuals for the target
cost model.

## Generate and use the local corpus

Run from the workspace root on macOS with the installed Apple Swift/PDFKit
toolchain and Node. No package installation, network service, SDK or firmware
dependency is required.

```sh
make -C badge-compiler build/pdfkit-datasheet
node badge-compiler/tools/datasheet.mjs extract \
  badge-compiler/docs/datasheet/rp2350-source.json \
  badge-compiler/build/datasheet/rp2350
```

The qualified corpus already exists at that output path in this workspace.
Extraction requires a **new** directory; to repeat it, choose another name.
Existing directories, files and symlinks are refused rather than overwritten.
Source hash/title/page-count mismatches and worker failures are explicit
errors. Temporary partial results are cleaned up before publication.

The generated directory contains:

```text
manifest.json       # Source/tool provenance, limitations and artifact hashes
index.json          # Physical/printed page, chapter, section and table indexes
pdfkit.json         # Original extractor text, metadata, outline and geometry
pages/0001.txt      # One UTF-8 file for every PDF page, including no-text pages
...
pages/1371.txt
```

Search the page files directly and resolve references through the hash-checked
index:

```sh
rg -n 'result-use|forwarding' badge-compiler/build/datasheet/rp2350/pages
node badge-compiler/tools/datasheet.mjs lookup \
  badge-compiler/build/datasheet/rp2350 section:3.7.4.9
node badge-compiler/tools/datasheet.mjs lookup \
  badge-compiler/build/datasheet/rp2350 table:122
node badge-compiler/tools/datasheet.mjs lookup \
  badge-compiler/build/datasheet/rp2350 printed:128
node badge-compiler/tools/datasheet.mjs lookup \
  badge-compiler/build/datasheet/rp2350 page:129
```

`page:` always means a 1-based physical PDF page. `printed:` matches the
actual footer label. Table queries return caption candidates, not prose
mentions. Section queries use PDF bookmarks, not the unreliable extracted
contents-page ordering. Unknown references fail, rather than returning an
apparently successful empty result.

Render the original PDF page when checking a table, formula or diagram:

```sh
mkdir -p badge-compiler/build/datasheet/review
node badge-compiler/tools/datasheet.mjs render \
  badge-compiler/build/datasheet/rp2350 131 \
  badge-compiler/build/datasheet/review/page-0131.png
make -C badge-compiler test-datasheet
```

Rendering requires an unchanged original PDF and the recorded native
extractor binary, and refuses to overwrite an existing output. It is a
fixed-width 1,800-pixel page preview, not an OCR or table-conversion pass.
The PDF remains authoritative when fine detail needs higher magnification.

Native extraction/rendering tests are optional and macOS-specific. Pure
JavaScript index, provenance, failure and integrity tests also run in the
normal compiler test target. Neither `all` nor ordinary firmware builds
depend on Swift or PDFKit.
