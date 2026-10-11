import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ownFile = fileURLToPath(import.meta.url);
const compiler = path.dirname(path.dirname(ownFile));
const nativeSource = path.join(compiler, 'tools/pdfkit-datasheet.swift');
const nativeTool = path.join(compiler, 'build/pdfkit-datasheet');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJSON = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const writeJSON = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n', { flag: 'wx' });
const heading = /^((?:\d+|[A-Z])(?:\.\d+)*)\.\s+(.+)$/;

function exists(file) {
  try { fs.lstatSync(file); return true; }
  catch (error) { if (error.code !== 'ENOENT') throw error; return false; }
}

export function indexDocument(raw) {
  assert.equal(raw.schema, 1);
  assert(Array.isArray(raw.pages) && raw.pages.length > 0);
  assert(Array.isArray(raw.outline));
  const sections = [];
  const unresolved = [];
  for (const item of raw.outline) {
    assert(Number.isSafeInteger(item.depth) && item.depth > 0);
    if (item.pdf_page === null) { unresolved.push(item); continue; }
    assert(Number.isSafeInteger(item.pdf_page) && item.pdf_page > 0 && item.pdf_page <= raw.pages.length);
    const match = typeof item.title === 'string' && item.title.match(heading);
    if (match) sections.push({
      id: match[1], title: match[2], pdf_page: item.pdf_page, depth: item.depth,
      evidence: 'PDF bookmark; coverage is an inclusive candidate range',
    });
  }
  assert.equal(new Set(sections.map(s => s.id)).size, sections.length, 'Duplicate section bookmarks');
  sections.forEach((section, i) => {
    const next = sections.slice(i + 1).find(s => s.depth <= section.depth);
    section.last_pdf_page_candidate = next ? Math.max(section.pdf_page, next.pdf_page) : raw.pages.length;
  });
  const tables = [];
  const pages = raw.pages.map((page, i) => {
    assert.equal(page.pdf_page, i + 1, 'Missing, duplicate or out-of-order PDF page');
    assert(typeof page.text === 'string');
    assert(page.pdf_label === null || typeof page.pdf_label === 'string');
    assert(Number.isFinite(page.width_points) && page.width_points > 0);
    assert(Number.isFinite(page.height_points) && page.height_points > 0);
    assert(Number.isSafeInteger(page.rotation));
    assert(['text', 'no_text'].includes(page.text_status));
    assert.equal(page.text_status, page.text.trim() ? 'text' : 'no_text');
    assert(page.footer_text === null || typeof page.footer_text === 'string');
    const footer = page.footer_text?.trim().match(/^(.+?)\s+(\d+)$/);
    const lines = page.text.split('\n');
    const candidates = [];
    const mentions = new Set();
    lines.forEach((line, j) => {
      const match = line.trim().match(/^Table\s+(\d+)\.(?:\s+(.+))?$/);
      if (match) {
        candidates.push(match[1]);
        tables.push({ id: match[1], pdf_page: i + 1, line: j + 1,
          title_candidate: match[2] ?? null, evidence: 'caption candidate; visually verify' });
      }
      for (const mention of line.matchAll(/\bTable\s+(\d+)\b/g)) mentions.add(mention[1]);
    });
    const sectionIds = sections.filter(s => i + 1 >= s.pdf_page &&
      i + 1 <= s.last_pdf_page_candidate).map(s => s.id);
    return {
      pdf_page: i + 1, pdf_label: page.pdf_label,
      printed_page_label: footer ? footer[2] : null,
      printed_label_evidence: footer ? 'numeric label in geometric footer; visually verify' : 'unresolved',
      footer_text: page.footer_text,
      text_status: page.text_status,
      file: `pages/${String(i + 1).padStart(4, '0')}.txt`,
      chapter_candidates: sectionIds.filter(id => !id.includes('.')),
      section_candidates: sectionIds,
      heading_candidates: lines.flatMap((line, j) => {
        const match = line.trim().match(heading);
        return match && !/(?:\.\s*){4}/.test(line) ?
          [{ id: match[1], title: match[2], line: j + 1 }] : [];
      }),
      table_caption_candidates: candidates,
      table_mentions: [...mentions].sort((a, b) => Number(a) - Number(b)),
      width_points: page.width_points, height_points: page.height_points, rotation: page.rotation,
    };
  });
  return { schema: 1, pages, sections, tables, unresolved_bookmarks: unresolved };
}

function invoke(tool, args) {
  const result = spawnSync(tool, args, { encoding: 'utf8', timeout: 300000, maxBuffer: 1024 * 1024 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr || `Extractor exited ${result.status}`);
}

export function extract(descriptorFile, output, tool = nativeTool) {
  descriptorFile = path.resolve(descriptorFile);
  output = path.resolve(output);
  tool = path.resolve(tool);
  assert(!exists(output), 'Output must be a new directory; existing files, directories and symlinks are refused');
  const descriptorBytes = fs.readFileSync(descriptorFile);
  const source = JSON.parse(descriptorBytes);
  assert.equal(source.schema, 1);
  assert(typeof source.id === 'string' && /^[a-z0-9-]+$/.test(source.id));
  assert(typeof source.file === 'string' && source.file.length > 0);
  assert(typeof source.title === 'string' && source.title.length > 0);
  assert(typeof source.rights === 'string' && source.rights.length > 0);
  assert.match(source.sha256, /^[a-f0-9]{64}$/);
  assert(Number.isSafeInteger(source.pages) && source.pages > 0);
  const pdf = path.resolve(path.dirname(descriptorFile), source.file);
  assert(fs.statSync(pdf).isFile(), 'Source is not a regular file');
  const sourceHash = hash(fs.readFileSync(pdf));
  assert.equal(sourceHash, source.sha256, 'Source does not match inventory hash');
  const executableHash = hash(fs.readFileSync(tool));
  const nativeSourceHash = hash(fs.readFileSync(nativeSource));
  const indexSourceHash = hash(fs.readFileSync(ownFile));
  fs.mkdirSync(path.dirname(output), { recursive: true });
  const staging = fs.mkdtempSync(path.join(path.dirname(output), '.datasheet-'));
  try {
    invoke(tool, ['extract', pdf, path.join(staging, 'pdfkit.json')]);
    const raw = readJSON(path.join(staging, 'pdfkit.json'));
    assert.equal(raw.pages.length, source.pages, 'Source page count changed');
    assert.equal(raw.attributes.Title, source.title, 'Source title does not match inventory');
    const index = indexDocument(raw);
    fs.mkdirSync(path.join(staging, 'pages'));
    const artifacts = [];
    function record(file, bytes) {
      fs.writeFileSync(path.join(staging, file), bytes, { flag: 'wx' });
      artifacts.push({ file, bytes: Buffer.byteLength(bytes), sha256: hash(bytes) });
    }
    for (const page of index.pages) {
      const text = raw.pages[page.pdf_page - 1].text;
      record(page.file, text.endsWith('\n') || !text ? text : text + '\n');
    }
    record('index.json', JSON.stringify(index, null, 2) + '\n');
    const rawBytes = fs.readFileSync(path.join(staging, 'pdfkit.json'));
    artifacts.push({ file: 'pdfkit.json', bytes: rawBytes.length, sha256: hash(rawBytes) });
    const manifest = {
      schema: 1, source: { ...source, file: pdf, attributes: raw.attributes },
      extractor: { ...raw.extractor, executable: tool, executable_sha256: executableHash,
        native_source_sha256: nativeSourceHash,
        index_source_sha256: indexSourceHash,
        descriptor_sha256: hash(descriptorBytes),
        command_template: ['node', ownFile, 'extract', descriptorFile, '<new-output-directory>'],
        native_command_template: [tool, 'extract', pdf, '<new-output-directory>/pdfkit.json'],
        options: 'PDFKit default text order; bottom 6% footer; append missing terminal newline to nonempty page text',
      },
      coverage: {
        total_pages: index.pages.length,
        no_text_pages: index.pages.filter(p => p.text_status === 'no_text').map(p => p.pdf_page),
        unresolved_printed_labels: index.pages.filter(p => p.printed_page_label === null).map(p => p.pdf_page),
        section_bookmarks: index.sections.length, table_caption_candidates: index.tables.length,
      },
      limitations: [
        'No OCR; no_text does not mean visually blank.',
        'PDFKit reading order is not guaranteed; columns, formulas and tables require original-page verification.',
        'Bookmark coverage includes shared boundary pages; section/table identities are navigation candidates, not verified facts.',
        'Printed labels are parsed from actual footer selections, never inferred from PDF indices.',
        'No timing, ABI or instruction semantic claim is established by extraction.',
      ],
      artifacts,
    };
    assert.equal(hash(fs.readFileSync(pdf)), sourceHash, 'Source changed during extraction');
    assert.equal(hash(fs.readFileSync(descriptorFile)), hash(descriptorBytes), 'Inventory changed during extraction');
    assert.equal(hash(fs.readFileSync(tool)), executableHash, 'Extractor changed during extraction');
    assert.equal(hash(fs.readFileSync(nativeSource)), nativeSourceHash, 'Native source changed during extraction');
    assert.equal(hash(fs.readFileSync(ownFile)), indexSourceHash, 'Index source changed during extraction');
    writeJSON(path.join(staging, 'manifest.json'), manifest);
    assert(!exists(output), 'Output appeared during extraction');
    fs.renameSync(staging, output);
    return manifest;
  } finally {
    if (exists(staging)) fs.rmSync(staging, { recursive: true });
  }
}

export function lookup(index, query) {
  const match = query.match(/^(page|printed|section|table):(.+)$/);
  assert(match, 'Reference must be page:N, printed:LABEL, section:ID or table:N');
  const [, kind, value] = match;
  if (kind === 'page') {
    assert(/^[1-9]\d*$/.test(value), 'Invalid PDF page');
    return index.pages.filter(p => p.pdf_page === Number(value));
  }

  if (kind === 'printed') return index.pages.filter(p => p.printed_page_label === value);
  if (kind === 'table') return index.tables.filter(t => t.id === value);
  const section = index.sections.find(s => s.id === value);
  return section ? [{ ...section, pages: index.pages.filter(p =>
    p.section_candidates.includes(value)).map(p => ({
      pdf_page: p.pdf_page, printed_page_label: p.printed_page_label, file: p.file,
    })) }] : [];
}

export function readIndex(directory) {
  const manifest = readJSON(path.join(directory, 'manifest.json'));
  const bytes = fs.readFileSync(path.join(directory, 'index.json'));
  const artifact = manifest.artifacts.find(item => item.file === 'index.json');
  assert(artifact, 'Index missing from manifest');
  assert.equal(hash(bytes), artifact.sha256, 'Index artifact hash changed');
  return JSON.parse(bytes);
}

function main(args) {
  if (args[0] === 'extract' && args.length === 3) {
    const manifest = extract(args[1], args[2]);
    console.log(JSON.stringify(manifest.coverage, null, 2));
  } else if (args[0] === 'lookup' && args.length === 3) {
    const result = lookup(readIndex(args[1]), args[2]);
    assert(result.length, `Reference not found: ${args[2]}`);
    console.log(JSON.stringify(result, null, 2));
  } else if (args[0] === 'render' && args.length === 4) {
    assert(/^[1-9]\d*$/.test(args[2]), 'Invalid PDF page');
    const manifest = readJSON(path.join(args[1], 'manifest.json'));
    assert.equal(hash(fs.readFileSync(manifest.source.file)), manifest.source.sha256, 'Source hash changed');
    assert.equal(hash(fs.readFileSync(nativeTool)), manifest.extractor.executable_sha256, 'Extractor hash changed');
    assert(!exists(args[3]), 'Render output already exists');
    invoke(nativeTool, ['render', manifest.source.file, args[2], path.resolve(args[3])]);
    console.log(`Rendered original PDF page ${args[2]} to ${args[3]}`);
  } else {
    throw new Error('Usage: node tools/datasheet.mjs extract SOURCE.json NEW_DIR | lookup DIR REFERENCE | render DIR PDF_PAGE NEW.png');
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === ownFile) {
  try { main(process.argv.slice(2)); }
  catch (error) { console.error(`datasheet: ${error.message}`); process.exitCode = 1; }
}
