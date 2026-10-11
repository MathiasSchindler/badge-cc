import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { indexDocument, lookup, extract, readIndex } from '../tools/datasheet.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const page = (pdf_page, text, footer_text = null) => ({
  pdf_page, text, footer_text, pdf_label: String(pdf_page),
  text_status: text.trim() ? 'text' : 'no_text',
  width_points: 600, height_points: 800, rotation: 0,
});

test('page identity comes from the footer, not a constant offset or PDF label', () => {
  const index = indexDocument({ schema: 1, outline: [], pages: [
    page(1, ''), page(2, 'Body 900', 'Example 40'), page(3, 'More', 'Example 41'),
  ] });
  assert.equal(index.pages.length, 3);
  assert.equal(index.pages[0].printed_page_label, null);
  assert.equal(index.pages[0].text_status, 'no_text');
  assert.equal(index.pages[1].printed_page_label, '40');
  assert.equal(index.pages[1].pdf_label, '2');
  assert.equal(lookup(index, 'page:2')[0].file, 'pages/0002.txt');
  assert.equal(lookup(index, 'printed:41')[0].pdf_page, 3);
  assert.deepEqual(lookup(index, 'printed:900'), []);
});

test('bookmark hierarchy includes shared boundary pages and reports unresolved destinations', () => {
  const index = indexDocument({ schema: 1, pages: [1, 2, 3, 4].map(n => page(n, 'Text')),
    outline: [
      { title: '1. First', depth: 1, pdf_page: 1 },
      { title: '1.1. Child', depth: 2, pdf_page: 2 },
      { title: '1.2. Another child', depth: 2, pdf_page: 3 },
      { title: '2. Second', depth: 1, pdf_page: 4 },
      { title: 'Unknown', depth: 1, pdf_page: null },
    ] });
  assert.deepEqual(lookup(index, 'section:1.1')[0].pages.map(p => p.pdf_page), [2, 3]);
  assert.deepEqual(index.pages[3].chapter_candidates, ['1', '2']);
  assert.equal(index.unresolved_bookmarks.length, 1);
  assert.deepEqual(lookup(index, 'section:99'), []);
});

test('table captions are distinguished from prose mentions and dotted contents leaders', () => {
  const index = indexDocument({ schema: 1, outline: [], pages: [
    page(1, '3.7.4.9. Timing\n3.8. Other . . . . . . 200\nTable 12.\nColumn A Column B\nSee Table 13 for details.'),
  ] });
  assert.deepEqual(index.pages[0].heading_candidates.map(h => h.id), ['3.7.4.9']);
  assert.deepEqual(index.pages[0].table_mentions, ['12', '13']);
  assert.equal(lookup(index, 'table:12')[0].line, 3);
  assert.deepEqual(lookup(index, 'table:13'), []);
});

test('invalid or incomplete page identities and bookmarks fail explicitly', () => {
  for (const pages of [[page(2, 'Text')], [page(1, 'Text'), page(1, 'Text')]]) {
    assert.throws(() => indexDocument({ schema: 1, outline: [], pages }), /PDF page/);
  }
  const repeated = { title: '1. Same', depth: 1, pdf_page: 1 };
  assert.throws(() => indexDocument({ schema: 1, pages: [page(1, 'Text')],
    outline: [repeated, repeated] }), /Duplicate/);
  assert.throws(() => indexDocument({ schema: 1, pages: [page(1, 'Text')],
    outline: [{ ...repeated, pdf_page: 2 }] }));
  assert.throws(() => lookup({ pages: [] }, 'page:0'), /Invalid/);
  assert.throws(() => lookup({ pages: [] }, '129'), /Reference/);
});

function fixture(t, mode = 'success') {
  const dir = fs.mkdtempSync(path.join(root, 'build/datasheet-test-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const pdf = path.join(dir, 'source.pdf'), descriptor = path.join(dir, 'source.json');
  const tool = path.join(dir, 'extractor');
  fs.writeFileSync(pdf, 'Synthetic source');
  fs.writeFileSync(descriptor, JSON.stringify({
    schema: 1, id: 'synthetic', title: 'Synthetic', file: 'source.pdf',
    sha256: sha(fs.readFileSync(pdf)), pages: 1, rights: 'Original test content',
  }));
  const raw = { schema: 1, attributes: { Title: 'Synthetic' },
    extractor: { name: 'test' }, outline: [], pages: [page(1, 'Text', 'Example 40')] };
  fs.writeFileSync(tool, `#!/usr/bin/env node
const fs = require('node:fs');
if (${JSON.stringify(mode)} === 'failed') { console.error('worker failure'); process.exit(7); }
if (${JSON.stringify(mode)} === 'drift') fs.appendFileSync(process.argv[3], 'changed');
fs.writeFileSync(process.argv[4], ${JSON.stringify(mode === 'malformed' ? '{}' : JSON.stringify(raw))});
`, { mode: 0o700 });
  return { dir, pdf, descriptor, tool, output: path.join(dir, 'output') };
}

test('repeated extraction has identical manifests and every artifact hash verifies', t => {
  const f = fixture(t);
  const first = extract(f.descriptor, f.output, f.tool);
  const secondOutput = path.join(f.dir, 'repeat');
  const second = extract(f.descriptor, secondOutput, f.tool);
  assert.deepEqual(first, second);
  for (const item of first.artifacts) {
    const bytes = fs.readFileSync(path.join(f.output, item.file));
    assert.equal(bytes.length, item.bytes);
    assert.equal(sha(bytes), item.sha256);
    assert(bytes.equals(fs.readFileSync(path.join(secondOutput, item.file))));
  }
  assert.equal(first.coverage.total_pages, 1);
});

test('source drift, worker failures and malformed results never publish partial extraction', t => {
  for (const mode of ['drift', 'failed', 'malformed']) {
    const f = fixture(t, mode);
    assert.throws(() => extract(f.descriptor, f.output, f.tool));
    assert(!fs.existsSync(f.output));
    assert(!fs.readdirSync(f.dir).some(n => n.startsWith('.datasheet-')));
  }
});

test('existing outputs and dangling symlinks are refused; mismatched inventories fail', t => {
  const f = fixture(t);
  fs.writeFileSync(f.output, 'prior output');
  assert.throws(() => extract(f.descriptor, f.output, f.tool), /new directory/);
  assert.equal(fs.readFileSync(f.output, 'utf8'), 'prior output');
  fs.unlinkSync(f.output);
  fs.symlinkSync(path.join(f.dir, 'missing'), f.output);
  assert.throws(() => extract(f.descriptor, f.output, f.tool), /new directory/);
  fs.unlinkSync(f.output);
  fs.appendFileSync(f.pdf, 'changed');
  assert.throws(() => extract(f.descriptor, f.output, f.tool), /inventory hash/);
});

test('unknown references fail through the CLI without producing a success-shaped result', t => {
  const f = fixture(t);
  extract(f.descriptor, f.output, f.tool);
  const result = spawnSync(process.execPath, [path.join(root, 'tools/datasheet.mjs'),
    'lookup', f.output, 'section:missing'], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Reference not found/);
  assert.equal(result.stdout, '');
});

test('changed indexes are rejected before reference lookup', t => {
  const f = fixture(t);
  extract(f.descriptor, f.output, f.tool);
  assert.equal(readIndex(f.output).pages.length, 1);
  fs.appendFileSync(path.join(f.output, 'index.json'), ' ');
  assert.throws(() => readIndex(f.output), /Index artifact hash changed/);
});
