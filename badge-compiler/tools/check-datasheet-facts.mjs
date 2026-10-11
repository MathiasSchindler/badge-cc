import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { createHash } from "node:crypto";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = fileURLToPath(new URL("../../", import.meta.url));
const topics = new Set([
  "instruction-semantics", "instruction-timing", "registers-and-abi",
  "branches-and-layout", "memory-system", "coprocessors",
  "errata-and-open-questions",
]);
const baseline = "M33-TIMING-BASE-001";
const digest = bytes => createHash("sha256").update(bytes).digest("hex");
const text = value => typeof value === "string" && value.trim().length > 0;
const list = (value, name) => assert(Array.isArray(value) && value.length > 0, name);
const relativeFile = value => text(value) && !path.isAbsolute(value)
  && value.split("/").every(part => part && part !== "." && part !== "..")
  && !value.includes("\\");

export function validate(catalog, ledger) {
  assert.equal(catalog.schema, 1, "source schema");
  assert.equal(ledger.schema, 1, "ledger schema");
  assert(text(ledger.scope), "ledger scope");
  list(catalog.sources, "sources");
  list(ledger.facts, "facts");
  const sources = new Map();
  for (const s of catalog.sources) {
    assert(text(s.id) && /^[a-z][a-z0-9-]*$/.test(s.id) && !sources.has(s.id), "duplicate/invalid source ID");
    sources.set(s.id, s);
    for (const key of ["edition", "rights", "applicability"]) assert(text(s[key]), `source ${key}`);
    for (const key of ["file", "raw"]) assert(relativeFile(s[key]), `source ${key} path`);
    for (const key of ["sha256", "raw_sha256"]) assert(/^[a-f0-9]{64}$/.test(s[key]), `source ${key}`);
    assert(Number.isSafeInteger(s.pages) && s.pages > 0, "source page count");
    assert(["rp2350-footer", "last-text-line", "before-copyright"].includes(s.label_rule), "label rule");
    assert(Array.isArray(s.reviewed_pages), "reviewed pages");
    assert.equal(new Set(s.reviewed_pages).size, s.reviewed_pages.length, "duplicate reviewed page");
    for (const p of s.reviewed_pages) assert(Number.isSafeInteger(p) && p > 0 && p <= s.pages, "reviewed page bounds");
    assert(s.inherited_images && typeof s.inherited_images === "object", "inherited images");
    for (const [p, image] of Object.entries(s.inherited_images)) {
      assert(s.reviewed_pages.includes(Number(p)) && relativeFile(image), "inherited image mapping");
    }
  }
  const facts = new Map();
  for (const f of ledger.facts) {
    assert(/^[A-Z][A-Z0-9-]+$/.test(f.id) && !facts.has(f.id), "duplicate/invalid fact ID");
    facts.set(f.id, f);
    assert(topics.has(f.topic), "unknown topic");
    assert(["documented", "visually_verified", "unresolved"].includes(f.status), "unqualified status");
    assert(["high", "high-conditional", "unresolved"].includes(f.confidence), "confidence");
    assert.equal(f.status === "unresolved", f.confidence === "unresolved", "unresolved confidence");
    assert(text(f.claim), "claim");
    list(f.conditions, "conditions");
    assert(f.conditions.every(text), "empty condition");
    list(f.compiler_consumers, "compiler consumers");
    assert(f.compiler_consumers.every(relativeFile), "consumer path");
    assert(Array.isArray(f.experiment_refs) && f.experiment_refs.length === 0, "D01 has no measured experiments");
    assert(f.hardware_measured === undefined || f.hardware_measured === false, "D01 has no hardware measurements");
    assert(f.core_timing === undefined || typeof f.core_timing === "boolean", "core timing marker");
    list(f.references, "references");
    for (const ref of f.references) {
      const s = sources.get(ref.source);
      assert(s, `unknown source ${ref.source}`);
      assert(text(ref.section), "section");
      list(ref.pages, "reference pages");
      assert.equal(new Set(ref.pages.map(p => p.pdf)).size, ref.pages.length, "duplicate reference page");
      for (const p of ref.pages) {
        assert(Number.isSafeInteger(p.pdf) && p.pdf > 0 && p.pdf <= s.pages, "PDF page bounds");
        assert(text(p.printed), "printed label");
        if (f.status === "visually_verified") assert(s.reviewed_pages.includes(p.pdf), "unreviewed visual claim");
      }
      if (ref.tables !== undefined) {
        list(ref.tables, "tables");
        assert(ref.tables.every(t => typeof t === "string" && /^\d+$/.test(t)), "table number");
      }
    }
    if ((f.topic === "instruction-timing" || f.core_timing) && f.id !== baseline) {
      assert.equal(f.timing_context, baseline, "missing timing context");
    }
  }
  for (const f of facts.values()) {
    if (f.timing_context !== undefined) {
      assert.equal(f.timing_context, baseline, "unexpected timing context");
      assert.equal(facts.get(baseline)?.topic, "instruction-timing", "unknown timing context");
    }
  }
  assert(Array.isArray(catalog.unacquired), "unacquired sources");
  for (const s of catalog.unacquired) {
    assert(text(s.id) && !sources.has(s.id) && text(s.locator) && text(s.status), "unacquired source");
    assert.equal(s.used_as_evidence, false, "unacquired source used as evidence");
  }
  return { sources: sources.size, facts: facts.size };
}

export function printedLabel(source, page) {
  switch (source.label_rule) {
    case "rp2350-footer":
      return page.footer_text.trim().match(/(?:^|\s)(\d+)$/)?.[1] ?? null;
    case "last-text-line":
      return page.text.trim().split("\n").at(-1).trim();
    case "before-copyright": {
      const lines = page.text.trim().split("\n");
      const copyright = lines.findLastIndex(line => line.startsWith("Copyright "));
      return copyright > 0 ? lines[copyright - 1].trim() : null;
    }
    default:
      throw new Error(`Unsupported label rule: ${source.label_rule}`);
  }
}

function sectionRange(raw, section) {
  const index = raw.outline.findIndex(item => item.title.split(/\s/u)[0].replace(/\.$/, "") === section);
  assert(index >= 0, `unknown section ${section}`);
  const start = raw.outline[index];
  assert(Number.isSafeInteger(start.pdf_page), `section destination ${section}`);
  const end = raw.outline.slice(index + 1).find(item => item.depth <= start.depth);
  return [start.pdf_page, end ? end.pdf_page : raw.pages.length];
}

export function verifyFiles(catalog, ledger, workspace = root) {
  const result = validate(catalog, ledger);
  const read = file => fs.readFileSync(path.join(workspace, file));
  const rawById = new Map();
  result.source_files = catalog.sources.map(s => {
    assert.equal(digest(read(s.file)), s.sha256, `source hash drift: ${s.id}`);
    const bytes = read(s.raw);
    assert.equal(digest(bytes), s.raw_sha256, `extract hash drift: ${s.id}`);
    const raw = JSON.parse(bytes);
    assert.equal(raw.pages.length, s.pages, `extract page count: ${s.id}`);
    raw.pages.forEach((p, i) => assert.equal(p.pdf_page, i + 1, `extract page order: ${s.id}`));
    rawById.set(s.id, raw);
    return { id: s.id, edition: s.edition, file: s.file, sha256: s.sha256, raw: s.raw, raw_sha256: s.raw_sha256 };
  });
  const cited = new Map();
  const consumers = new Set();
  for (const f of ledger.facts) {
    for (const file of f.compiler_consumers) consumers.add(file);
    for (const ref of f.references) {
      const s = catalog.sources.find(source => source.id === ref.source);
      const raw = rawById.get(s.id);
      const [start, end] = sectionRange(raw, ref.section);
      for (const p of ref.pages) {
        assert(p.pdf >= start && p.pdf <= end, `section/page mismatch: ${f.id}/${ref.section}`);
        assert.equal(printedLabel(s, raw.pages[p.pdf - 1]), p.printed, `printed label mismatch: ${f.id}/${p.pdf}`);
        cited.set(`${s.id}:${p.pdf}`, { source: s.id, ...p });
      }
      for (const table of ref.tables ?? []) {
        const caption = new RegExp(`^Table\\s+${table}\\.`, "m");
        assert(ref.pages.some(p => caption.test(raw.pages[p.pdf - 1].text)), `missing table caption: ${f.id}/${table}`);
      }
    }
  }
  result.cited_pages = [...cited.values()];
  result.reviewed_images = catalog.sources.flatMap(s => s.reviewed_pages.map(pdf => {
    const file = s.inherited_images[pdf] ?? `badge-compiler/build/datasheet/d01-review/${s.id}-${pdf}.png`;
    const bytes = read(file);
    assert(bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])), `not a PNG: ${file}`);
    return { source: s.id, pdf, file, sha256: digest(bytes), inherited_from_D00: Object.hasOwn(s.inherited_images, pdf) };
  }));
  result.compiler_consumers = [...consumers].sort().map(file => ({ file, sha256: digest(read(file)) }));
  return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    const args = process.argv.slice(2);
    assert(args.length === 0 || (args.length === 1 && args[0] === "--files"), "usage: node tools/check-datasheet-facts.mjs [--files]");
    const directory = path.join(root, "badge-compiler/docs/datasheet");
    const catalog = JSON.parse(fs.readFileSync(path.join(directory, "reference-sources.json")));
    const ledger = JSON.parse(fs.readFileSync(path.join(directory, "fact-ledger.json")));
    const result = args.length ? verifyFiles(catalog, ledger) : validate(catalog, ledger);
    console.log(JSON.stringify(result, null, 2));
  } catch (error) {
    console.error(`datasheet facts: ${error.message}`);
    process.exitCode = 1;
  }
}
