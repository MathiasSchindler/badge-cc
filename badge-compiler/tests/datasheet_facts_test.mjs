import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { createHash } from "node:crypto";
import test from "node:test";
import { printedLabel, validate, verifyFiles } from "../tools/check-datasheet-facts.mjs";

const catalog = JSON.parse(fs.readFileSync(new URL("../docs/datasheet/reference-sources.json", import.meta.url)));
const ledger = JSON.parse(fs.readFileSync(new URL("../docs/datasheet/fact-ledger.json", import.meta.url)));
const hash = bytes => createHash("sha256").update(bytes).digest("hex");

test("authored ledger passes without local PDFs or native tools", () => {
  assert.deepEqual(validate(catalog, ledger), { sources: 3, facts: ledger.facts.length });
});

test("every fact is covered by its topic note and note IDs resolve", () => {
  const ids = new Set(ledger.facts.map(f => f.id));
  const topics = new Set(ledger.facts.map(f => f.topic));
  for (const topic of topics) {
    const note = fs.readFileSync(new URL(`../docs/datasheet/${topic}.md`, import.meta.url), "utf8");
    for (const id of note.match(/(?:M33|RP2350|THUMB|AAPCS)-[A-Z0-9-]+/g) ?? []) {
      assert(ids.has(id), `unknown note fact: ${id}`);
    }
    for (const fact of ledger.facts.filter(f => f.topic === topic)) {
      assert(note.includes(fact.id), `fact absent from topic: ${fact.id}`);
    }
  }
});

const failures = [
  ["duplicate fact", (_, l) => l.facts.push(l.facts[0]), /duplicate\/invalid fact/],
  ["duplicate source", c => c.sources.push(c.sources[0]), /duplicate\/invalid source/],
  ["unknown source", (_, l) => l.facts[0].references[0].source = "absent", /unknown source/],
  ["missing conditions", (_, l) => l.facts[0].conditions = [], /conditions/],
  ["page outside document", (_, l) => l.facts[0].references[0].pages[0].pdf = 99999, /page bounds/],
  ["unknown timing context", (_, l) => l.facts.find(f => f.timing_context).timing_context = "ABSENT", /timing context/],
  ["missing timing context", (_, l) => delete l.facts.find(f => f.timing_context).timing_context, /timing context/],
  ["missing cross-topic core timing context", (_, l) => delete l.facts.find(f => f.core_timing).timing_context, /timing context/],
  ["unreviewed visual claim", (_, l) => l.facts[0].status = "visually_verified", /unreviewed/],
  ["unjustified measured status", (_, l) => l.facts[0].status = "hardware_measured", /unqualified status/],
  ["unjustified measurement flag", (_, l) => l.facts[0].hardware_measured = true, /no hardware/],
  ["unjustified experiment", (_, l) => l.facts[0].experiment_refs.push("invented"), /no measured experiments/],
  ["escaping consumer path", (_, l) => l.facts[0].compiler_consumers = ["../private.c"], /consumer path/],
  ["blank printed label", (_, l) => l.facts[0].references[0].pages[0].printed = "", /printed label/],
  ["unresolved high confidence", (_, l) => l.facts[0].status = "unresolved", /unresolved confidence/],
  ["unacquired source evidence", c => c.unacquired[0].used_as_evidence = true, /used as evidence/],
];
for (const [name, mutate, error] of failures) {
  test(`rejects ${name}`, () => {
    const c = structuredClone(catalog), l = structuredClone(ledger);
    mutate(c, l);
    assert.throws(() => validate(c, l), error);
  });
}

test("printed labels use source-specific evidence, never PDF-page arithmetic", () => {
  assert.equal(printedLabel({ label_rule: "rp2350-footer" }, { text: "", footer_text: "Processor subsystem 127" }), "127");
  assert.equal(printedLabel({ label_rule: "rp2350-footer" }, { text: "128", footer_text: "" }), null);
  assert.equal(printedLabel({ label_rule: "last-text-line" }, { text: "body\nNon-confidential\n537\n" }), "537");
  assert.equal(printedLabel({ label_rule: "before-copyright" }, { text: "body\n18\nCopyright owner\nreserved." }), "18");
  assert.equal(printedLabel({ label_rule: "before-copyright" }, { text: "18" }), null);
  assert.throws(() => printedLabel({ label_rule: "infer-offset" }, {}), /Unsupported/);
});

test("full validation catches source, extract, label, section, table, image and consumer failures", t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "datasheet-facts-"));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  fs.writeFileSync(path.join(directory, "source.pdf"), "test source bytes");
  fs.writeFileSync(path.join(directory, "consumer.c"), "test consumer bytes");
  const raw = { pages: [{ pdf_page: 1, text: "1. Test\nTable 122.\nbody\n7", footer_text: "7" }], outline: [{ title: "1. Test", pdf_page: 1, depth: 1 }] };
  const bytes = JSON.stringify(raw);
  fs.writeFileSync(path.join(directory, "raw.json"), bytes);
  const c = { schema: 1, sources: [{
    id: "sample", edition: "test", file: "source.pdf", sha256: hash("test source bytes"),
    raw: "raw.json", raw_sha256: hash(bytes), pages: 1, label_rule: "last-text-line",
    rights: "test", applicability: "test", reviewed_pages: [1], inherited_images: { 1: "review.png" },
  }], unacquired: [] };
  fs.writeFileSync(path.join(directory, "review.png"), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
  const l = { schema: 1, scope: "test", facts: [{
    id: "TEST-001", topic: "instruction-semantics", status: "visually_verified", confidence: "high",
    claim: "test", conditions: ["test"], experiment_refs: [], compiler_consumers: ["consumer.c"],
    references: [{ source: "sample", section: "1", tables: ["122"], pages: [{ pdf: 1, printed: "7" }] }],
  }] };
  assert.equal(verifyFiles(c, l, directory).reviewed_images.length, 1);
  const bad = structuredClone(l);
  bad.facts[0].references[0].pages[0].printed = "1";
  assert.throws(() => verifyFiles(c, bad, directory), /printed label mismatch/);
  bad.facts[0].references[0].pages[0].printed = "7";
  bad.facts[0].references[0].section = "2";
  assert.throws(() => verifyFiles(c, bad, directory), /unknown section/);
  bad.facts[0].references[0].section = "1";
  bad.facts[0].references[0].tables = ["123"];
  assert.throws(() => verifyFiles(c, bad, directory), /missing table caption/);
  fs.writeFileSync(path.join(directory, "review.png"), "not PNG");
  assert.throws(() => verifyFiles(c, l, directory), /not a PNG/);
  fs.writeFileSync(path.join(directory, "raw.json"), "changed");
  assert.throws(() => verifyFiles(c, l, directory), /extract hash drift/);
  fs.writeFileSync(path.join(directory, "source.pdf"), "changed");
  assert.throws(() => verifyFiles(c, l, directory), /source hash drift/);
  fs.writeFileSync(path.join(directory, "source.pdf"), "test source bytes");
  fs.writeFileSync(path.join(directory, "raw.json"), bytes);
  fs.writeFileSync(path.join(directory, "review.png"), Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
  const missing = structuredClone(l);
  missing.facts[0].compiler_consumers = ["absent.c"];
  assert.throws(() => verifyFiles(c, missing, directory), /ENOENT/);
});
