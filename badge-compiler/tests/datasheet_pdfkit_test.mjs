import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { extract, lookup } from '../tools/datasheet.mjs';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const tool = path.join(root, 'build/pdfkit-datasheet');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function syntheticPDF() {
  const stream = text => `<< /Length ${Buffer.byteLength(text)} >>\nstream\n${text}\nendstream`;
  const objects = [
    '<< /Type /Catalog /Pages 2 0 R /Outlines 10 0 R /PageLabels << /Nums [0 << /S /r >> 1 << /S /D /St 40 >>] >> >>',
    '<< /Type /Pages /Kids [3 0 R 5 0 R 7 0 R] /Count 3 >>',
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 4 0 R >>',
    stream('q Q'),
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 9 0 R >> >> /Contents 6 0 R >>',
    stream('BT /F1 12 Tf 40 740 Td (1. Example) Tj 0 -30 Td (1.1. Dependency timing) Tj 0 -30 Td (Original test content) Tj ET\nBT /F1 10 Tf 40 20 Td (Example 40) Tj ET'),
    '<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 9 0 R >> >> /Contents 8 0 R >>',
    stream('BT /F1 12 Tf 40 740 Td (Table 7.) Tj 0 -30 Td (Operand   Latency) Tj 0 -30 Td (A         2 cycles) Tj ET\nBT /F1 10 Tf 40 20 Td (Example 41) Tj ET'),
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
    '<< /Type /Outlines /First 11 0 R /Last 11 0 R /Count 2 >>',
    '<< /Title (1. Example) /Parent 10 0 R /Dest [5 0 R /Fit] /First 12 0 R /Last 12 0 R /Count 1 >>',
    '<< /Title (1.1. Dependency timing) /Parent 11 0 R /Dest [5 0 R /Fit] >>',
    '<< /Title (Synthetic PDF) /Creator (Original repository test fixture) >>',
  ];
  let output = '%PDF-1.4\n', offsets = [0];
  objects.forEach((object, i) => {
    offsets.push(Buffer.byteLength(output));
    output += `${i + 1} 0 obj\n${object}\nendobj\n`;
  });
  const xref = Buffer.byteLength(output);
  output += `xref\n0 ${objects.length + 1}\n0000000000 65535 f \n`;
  output += offsets.slice(1).map(n => `${String(n).padStart(10, '0')} 00000 n \n`).join('');
  output += `trailer\n<< /Size ${objects.length + 1} /Root 1 0 R /Info 13 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(output);
}

function fixture(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/datasheet-pdfkit-test-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const pdf = path.join(dir, 'input.pdf'), descriptor = path.join(dir, 'source.json');
  const data = syntheticPDF();
  fs.writeFileSync(pdf, data);
  fs.writeFileSync(descriptor, JSON.stringify({ schema: 1, id: 'synthetic',
    title: 'Synthetic PDF', file: 'input.pdf', sha256: sha(data),
    pages: 3, rights: 'Original repository test content' }));
  return { dir, pdf, descriptor, output: path.join(dir, 'output') };
}

test('real PDFKit preserves empty pages, physical/printed identities, outlines and deterministic outputs', t => {
  const f = fixture(t);
  const a = extract(f.descriptor, f.output);
  const b = extract(f.descriptor, path.join(f.dir, 'repeat'));
  assert.deepEqual(a, b);
  assert.deepEqual(a.coverage.no_text_pages, [1]);
  assert.deepEqual(a.coverage.unresolved_printed_labels, [1]);
  const index = JSON.parse(fs.readFileSync(path.join(f.output, 'index.json')));
  assert.equal(index.pages.length, 3);
  assert.equal(index.pages[0].pdf_label, 'i');
  assert.equal(index.pages[1].printed_page_label, '40');
  assert.equal(lookup(index, 'section:1.1')[0].pdf_page, 2);
  assert.equal(lookup(index, 'table:7')[0].pdf_page, 3);
  for (const item of a.artifacts) {
    const bytes = fs.readFileSync(path.join(f.output, item.file));
    assert.equal(sha(bytes), item.sha256);
    assert(bytes.equals(fs.readFileSync(path.join(f.dir, 'repeat', item.file))));
  }
});

test('real PDFKit renders the original page and refuses invalid pages, PDFs and overwrite', t => {
  const f = fixture(t);
  const png = path.join(f.dir, 'page.png');
  const run = args => spawnSync(tool, args, { encoding: 'utf8', timeout: 30000 });
  const rendered = run(['render', f.pdf, '2', png]);
  assert.ifError(rendered.error);
  assert.equal(rendered.status, 0, rendered.stderr);
  const before = fs.readFileSync(png);
  assert.equal(before.subarray(0, 8).toString('hex'), '89504e470d0a1a0a');
  for (const args of [
    ['render', f.pdf, '2', png], ['render', f.pdf, '0', png],
    ['render', f.pdf, '4', png], ['extract', f.descriptor, png], [],
  ]) {
    const result = run(args);
    assert.ifError(result.error);
    assert.equal(result.status, 1);
    assert(result.stderr.includes('pdfkit-datasheet:'));
    assert(before.equals(fs.readFileSync(png)));
  }
});
