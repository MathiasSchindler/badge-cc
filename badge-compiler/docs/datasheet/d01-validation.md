# D01 qualification

D01 establishes documentary target knowledge, not compiler profitability or
badge timing results. Its deliverables are the seven [topic notes](README.md),
[44-fact ledger](fact-ledger.json), [three-source catalog](reference-sources.json)
and [validator](../../tools/check-datasheet-facts.mjs).

## Evidence and checks

The full source-backed validation verifies:

- Original PDFs and raw extracts against separate pinned SHA-256 values.
- Physical page counts/order and every cited page's actual printed label.
- Cited section existence and inclusive bookmark-range membership; these
  candidate ranges do not prove that every line belongs to that section.
- Table 122/123 caption presence on their cited pages.
- Human-reviewed page membership for every visually verified claim, local
  PNG evidence and its hash.
- Potential compiler consumer paths and current content hashes.

Metadata checks reject duplicate IDs, missing conditions, unknown sources,
out-of-bounds pages, invalid confidence/state, absent core-timing context,
unreviewed visual claims, invented experiments/hardware measurements and
escaping paths. Tests also require every fact in its topic note and resolve
the notes' fact IDs. These checks enforce provenance and structure; they are
not an automated proof that a prose claim is true.

```sh
node --test badge-compiler/tests/datasheet_facts_test.mjs \
  badge-compiler/tests/datasheet_test.mjs \
  badge-compiler/tests/datasheet_pdfkit_test.mjs
node badge-compiler/tools/check-datasheet-facts.mjs --files
```

Portable ledger tests are in the normal compiler Node test recipe. Native
PDFKit tests are optional/macOS-specific. `test-datasheet-facts` performs the
portable ledger tests plus local source verification without building firmware.
Neither ordinary `all`/`test-owned` acquires PDFs or depends on native PDFKit.

## Actual visual review set

Physical PDF pages checked against rendered originals:

- RP2350: 107, 118, 128, 129, 132, 133, 312, 316; also 130/131 inherited
  transparently from D00 review of the same original source hash.
- Armv8-M ARM: 537, 538, 542, 671, 672, 741, 743, 1463.
- AAPCS32: 18, 20, 21, 24, 27.

That is 23 distinct pages. Merely rendered but uninspected pages do not count.
Timing formulas/tables and the starter ADD/MOV/literal encoding diagrams were
checked visually. APSR continuation, ABI continuations, configuration, interrupt
and errata prose have explicit documentary rather than blanket visual status
where not every cited page was inspected.

The full validation covers 41 distinct cited physical pages. Source-specific
label rules are documented in [sources.md](sources.md); no offset is inferred.
Additional manuals use pinned raw page-addressable JSON rather than pretending
the RP2350 heading/footer indexer handles all formats. D00's extracted corpus,
manifest and qualification remain unchanged.

The focused combined run passed all 31 tests with no failures or skips
(20 D01 tests and 11 existing D00 tests). No full compiler rebuild or hardware
campaign is claimed: this milestone adds reference/validation tooling, not
production compiler transformations.

## Limits and preservation

- No standalone M33 TRM was independently acquired; RP2350's licensed core
  material supplies the implementation reference. The portal is not evidence.
- No badge revision, runtime coprocessor/security state or timing environment
  is inferred. Relevant errata and forwarding questions remain visible.
- No hardware measurements, target cost model, codegen/default changes,
  firmware dependencies or badge reflashes are part of D01.
- Recheck the D00 recorded production source/executable and maze UF2 hashes,
  plus extraction tool/corpus hashes, in the generated D01 result.
- Original PDFs, bulk raw extracts and review images remain local ignored data.
  The notes and ledger are authored factual analyses with source attribution.

Qualification outputs are local
[`d01-results.json`](../../build/datasheet/d01-results.json),
[`d01-validation.json`](../../build/datasheet/d01-validation.json) and
[`d01-tests.log`](../../build/d01-tests.log).
The result records validation counts, test and authored-document hashes,
review-image hashes and preservation comparisons. D02 must calibrate the
documented mechanisms before D03 gives them compiler cost-model meaning.
