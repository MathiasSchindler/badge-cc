# D00 qualification and limitations

D00 is qualified for source inventory, reproducible extraction and reference
navigation. It does not establish instruction costs, a timing predictor,
new compiler defaults or hardware performance gains.

## Automated evidence

- All 1,371 physical pages have separate UTF-8 files; no page is omitted.
- Every page has extractable text in this edition. The tool nevertheless
  explicitly preserves and reports no-text pages, tested with an original
  synthetic PDF containing one.
- Printed labels are resolved on 1,369 pages; only the cover and back cover
  remain unresolved. Each footer was checked, rather than filled from an
  offset. Synthetic tests use roman PDF labels and non-offset printed labels.
- The index has 1,168 numbered section bookmarks, no unresolved bookmark
  destinations and 1,692 table-caption candidate occurrences. The caption
  count is not an independent claim that every table was semantically parsed.
- Two complete runs with final tooling produced byte-identical manifests and
  all 1,373 listed artifacts: 1,371 text files, extractor JSON and index JSON.
  Every recorded byte length and SHA-256 was verified.
- Eleven focused tests pass, including actual PDFKit extraction/rendering,
  reproducibility, labels, outlines, table/prose distinction, no-text pages,
  source drift, invalid inputs, failed workers, overwrite/symlink protection,
  malformed output, unknown references and changed-index rejection.

The corpus is under `badge-compiler/build/datasheet/rp2350/`; the temporary
`rp2350-repeat/` snapshot was removed after exact comparison. Evidence is recorded in the
[generated result report](../../build/datasheet/d00-results.json).
The reproducible primary manifest SHA-256 is
`ab6ab76992fb882ccc95a0b7a3676d8c8188d5f481f5a9aecb56afd49efe0dab`.
The [focused test target](README.md) does not alter compiler code or firmware.

## Visual source checks

Rendered original PDF pages, inspected against their extracted text:

| PDF / printed page | Sample | Outcome and limitation |
| --- | --- | --- |
| 3 / 2 | Contents: number, title and page columns | PDFKit groups some column values out of reading order. Use bookmarks, not reconstructed contents text, for section navigation. |
| 23 / 22 | Bus diagram and qualifying note box | Diagram text is searchable, but arrows, connections and box geometry are not represented by plain text. Retain the original page for topology. |
| 130 / 129 | Division formula and Tables 122/123 | Sample operands and table cells are present, but column boundaries are flattened. Verified samples do not justify automatic parsing of all tables. |
| 131 / 130 | Branch formula, conditions and code examples | Operators and condition text are retained in this sample. Highlighting, indentation and association with the surrounding explanation still need page inspection. |
| 277 / 276 | Table 386 and numbered footnotes | Superscript markers become adjacent ordinary digits in text, potentially resembling part of a cost. Do not interpret those digit strings as numbers without visual verification. This is a formatting check, not RISC-V research. |

Rendered samples remain local in `build/datasheet/verification/`. The source
hash, rendered-image hashes and page identities are retained in the result
report. Rendering preserves the original page layout rather than attempting
to repair its extracted text.

## Rules for D01 consumers

Extraction is a navigation layer, not authoritative structured instruction
data. Before using a table, equation, footnote or diagram in a fact ledger:

1. Resolve its source edition, physical PDF page and printed label.
2. Inspect the original page, including continuation pages and qualifications.
3. Record the source section/table and verification state.
4. Preserve uncertainties and separate documented behavior from measurement.

Neither the per-page footer heuristic nor candidate section ranges establish
paragraph ownership in every possible PDF. A different edition/tool version
requires renewed checks. No OCR or general table reconstruction was added.
