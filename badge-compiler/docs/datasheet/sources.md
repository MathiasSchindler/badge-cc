# Source inventory and extraction contract

## RP2350 source: acquired and locked

| Field | Recorded value |
| --- | --- |
| Source ID | `rp2350` |
| Publisher | Raspberry Pi Ltd |
| Title | RP2350 Datasheet |
| Local original | [RP-008373-DS-3-rp2350-datasheet.pdf](../../../pimoroni/RP-008373-DS-3-rp2350-datasheet.pdf) |
| Filename edition identifier | `RP-008373-DS-3` |
| Colophon build date/version | 2026-10-01 / `80d627281ed5` |
| PDF creation/modification metadata | `2026-10-01T10:57:53Z` |
| Page count | 1,371 physical PDF pages |
| SHA-256 | `a638d6780da919162660691bfa02c6dcf7c2a0b6e9f18e2d7519c80b664a2e39` |
| Inventory | [rp2350-source.json](rp2350-source.json) |

The colophon is physical PDF page 2, printed page 1. Creation metadata is not
a substitute for the colophon build identifier; the SHA-256 identifies the
exact local edition. A replacement PDF requires an explicit inventory update,
fresh extraction and renewed page/reference checks.

## Extraction provenance

[pdfkit-datasheet.swift](../../tools/pdfkit-datasheet.swift) uses the installed
macOS PDFKit framework. The qualified framework reports version `1.0`, build
`1537`. [datasheet.mjs](../../tools/datasheet.mjs) creates the page files,
reference index and manifest.

The manifest records:

- Source inventory, hash, title, page count and PDF attributes.
- PDFKit version/build, OS version and locale.
- Native executable path/hash, native and JavaScript source hashes, and
  source-inventory hash.
- Command templates with a deliberately variable new output directory,
  text/footer extraction options and explicit limitations.
- Byte length and SHA-256 of every page file, raw extractor JSON and index.

The source, inventory, executable and tool sources are checked for drift
during extraction. Lookup checks the index against its manifest hash.
Reproducibility is claimed for the same inputs, executable and environment,
not arbitrary PDFKit versions, OS releases or locales. Absolute provenance
paths can differ between workspaces; that is not text-content equivalence.

PDFKit's text is preserved without guessed corrections or Unicode
normalization. Nonempty page files receive a terminal newline if missing;
empty extracted text stays an empty file. Raw text remains in the extractor
JSON. No OCR, inferred equations or reconstructed tables are supplied.

Printed labels are parsed from text selected in the bottom 6% of each
page's media box. The PDF's own label is stored separately. A missing footer
label is `null`, never filled using a presumed page offset.

Section starts come from numbered PDF bookmarks. Inclusive candidate ranges
retain shared boundary pages, where an old section may end and a new one begin.
They are navigation aids, not proof that every line on a page belongs to
every listed section. Tables are indexed by caption candidates and prose
mentions separately; a caption without an inline title remains unresolved
rather than borrowing the next arbitrary text line.

## Rights and distribution decision

The RP2350 colophon declares CC BY-ND 4.0 and includes additional third-party
copyright notices. This inventory does not grant permission to redistribute
a converted or edited full-text derivative.

Keep the original unchanged. Bulk extracted text, raw metadata, duplicate
snapshots and rendered page images stay local under
`badge-compiler/build/datasheet/`, covered by the existing `build/` ignore rule.
Do not commit or upload those extracts. Authored notes use original analysis,
factual records, short source identifiers and attribution instead of copying
chapters. Further distribution needs a source-specific rights review.

## D01 additions: acquired and pinned

The [D01 catalog](reference-sources.json) locks original and raw-extraction
SHA-256 values separately; it does not alter the frozen D00 RP2350 inventory.

| Source | Exact edition | Physical pages | Acquisition |
| --- | --- | --- | --- |
| Armv8-M Architecture Reference Manual | DDI0553B.y / ID09082024, 2024-08-09 | 2,149 | [Arm-hosted attachment](https://community.arm.com/cfs-file/__key/communityserver-discussions-components-files/471/DDI0553B_5F00_y_5F00_armv8m_5F00_arm.pdf) |
| AAPCS32 | 2025Q4, issued 2026-01-23 | 41 | [Official Arm ABI release](https://github.com/ARM-software/abi-aa/releases/tag/2025Q4), [PDF](https://github.com/ARM-software/abi-aa/releases/download/2025Q4/aapcs32.pdf) |

The architecture manual includes v8.1-M and optional-feature material beyond
the RP2350 configuration. Its physical page 5 has an Arm proprietary notice;
Non-confidential does not grant redistribution rights. Use original factual
analysis and references, not copied pseudocode, diagrams or chapters. The
AAPCS32 physical page 3 declares CC BY-SA 4.0 with an additional patent license.
Keep both originals, bulk extracts and previews local under ignored build data.

The additional raw JSON files were generated with the unchanged D00 native
PDFKit extractor. Its command is:

```sh
badge-compiler/build/pdfkit-datasheet extract SOURCE.pdf NEW.json
badge-compiler/build/pdfkit-datasheet render SOURCE.pdf PDF_PAGE NEW.png
```

Existing outputs are refused. Use the catalog's exact PDF hash before accepting
a new extraction; the locked raw hash describes this qualified tool/environment,
not arbitrary PDFKit releases. Raw files retain extractor and PDF metadata.
Do not apply the RP2350 chapter/label indexer blindly to other manuals:

- RP2350 cited labels come from the actual footer region.
- Arm ARM cited printed labels are the final line of body-page text, independently
  checked against original images for the reviewed set.
- AAPCS cited printed labels occur before the final copyright block.

These are source-specific evidence rules, not physical-page offset arithmetic.
Cover/Roman-page indexing and full chapter navigation for the additional
manuals are not claimed here.

## Standalone Cortex-M33 TRM: independent acquisition unresolved

The [official locator](https://developer.arm.com/documentation/100230/0100/)
redirected to a documentation shell in this environment. Attempted historical
static PDF downloads returned 404. No independent edition/hash is qualified.
The RP2350 datasheet's section 3.7 explicitly incorporates licensed Cortex-M33
TRM material; use that pinned implementation reference, and keep an independent
TRM cross-check outstanding rather than treating the portal as retrieved content.

Appendix D supplies RP2350 errata. D01 records relevant E27/E11/E26 restrictions;
actual badge revision and runtime state remain unresolved. See
[errata and questions](errata-and-open-questions.md).
