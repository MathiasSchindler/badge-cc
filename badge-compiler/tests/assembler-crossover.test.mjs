import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { compareObjects, normalizeObject, readObject, verifyCrossover } from '../tools/assembler-crossover.mjs';

function object(symbol, addend = 0, type = 2) {
  const data = Buffer.alloc(4); data.writeUInt32LE(addend >>> 0);
  return { bytes: 100, sections: [{ index: 0, name: '', type: 0, flags: 0, data: Buffer.alloc(0) },
    { index: 1, name: '.text', type: 1, flags: 6, alignment: 4, entry_size: 0, size: 4, data },
    { index: 2, name: '.rodata', type: 1, flags: 2, alignment: 1, entry_size: 0, size: 0, data: Buffer.alloc(0) }],
  symbols: [symbol], relocations: [{ section: 1, offset: 0, type, symbol }] };
}
const symbol = (name, value, type = 0) => ({ name, value, type, size: 0,
  binding: 0, visibility: 0, section: 2 });

test('section-symbol addends and local-symbol offsets normalize without hiding target differences', () => {
  const a = object(symbol('.Lstr', 15)), b = object(symbol('', 0, 3), 15);
  const comparison = compareObjects(a, b);
  assert(comparison.allocated_equal && comparison.relocations_equal);
  assert.equal(comparison.raw_allocated_equal, false);
  b.relocations[0].symbol.value = 1;
  assert.equal(compareObjects(a, b).relocations_equal, false);
  b.relocations[0].symbol.value = 0;
  b.sections[2].name = '.other';
  assert.equal(compareObjects(a, b).relocations_equal, false);
});

test('nonrelocated opcode bits, alignment and public symbol attributes remain exact', () => {
  const a = object(symbol('public', 0)), b = object(symbol('public', 0));
  a.symbols[0].binding = b.symbols[0].binding = 1;
  b.sections[1].alignment = 8;
  assert.equal(compareObjects(a, b).allocated_equal, false);
  b.sections[1].alignment = 4;
  b.symbols[0].visibility = 2;
  assert.equal(compareObjects(a, b).public_symbols_equal, false);
  a.relocations = []; b.relocations = [];
  b.sections[1].data[0] = 1;
  assert.equal(compareObjects(a, b).allocated_equal, false);
});

test('Thumb symbol bit, unresolved targets, PREL31 and unsupported relocations are explicit', () => {
  const a = object(symbol('callee', 1, 2), -4, 10);
  a.sections[1].data.writeUInt16LE(0xf7ff, 0);
  a.sections[1].data.writeUInt16LE(0xfffe, 2);
  const r = normalizeObject(a).relocations[0];
  assert.equal(r.thumb, 1); assert.equal(r.effective_addend, -4);
  a.relocations[0].symbol = { ...symbol('external', 0), section: 0 };
  assert.equal(normalizeObject(a).relocations[0].target, 'undefined:external');
  a.relocations[0].type = 42; a.sections[1].data.writeUInt32LE(0x7fffffff);
  assert.equal(normalizeObject(a).relocations[0].effective_addend, -1);
  a.relocations[0].type = 255;
  assert.throws(() => normalizeObject(a), /unsupported relocation normalization/);
});

test('MOVW/MOVT masks preserve opcodes and signed REL addends', () => {
  for (const type of [47, 48]) {
    const a = object(symbol('data', 0), 0, type);
    a.sections[1].data.writeUInt16LE(type === 47 ? 0xf64f : 0xf6cf, 0);
    a.sections[1].data.writeUInt16LE(0x70ff, 2);
    assert.equal(normalizeObject(a).relocations[0].effective_addend, -1);
    const masked = normalizeObject(a).allocated.find(s => s.name === '.text').content;
    assert.equal(masked, type === 47 ? '40f20000' : 'c0f20000');
  }
});

test('malformed ELF cannot become a successful empty comparison', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ba01-malformed-'));
  try {
    const file = path.join(dir, 'bad.o'); fs.writeFileSync(file, 'not ELF');
    assert.throws(() => readObject(file), /not ELF/);
  } finally { fs.rmSync(dir, { recursive: true }); }
});

const directory = process.env.COMPILE_GRAPH_REPORT ? path.resolve(process.env.COMPILE_GRAPH_REPORT) : null;
test('real BA01 covers both producers and linkers and detects changed normalized evidence',
  { skip: !directory && 'Set COMPILE_GRAPH_REPORT to a BA01 report.' }, () => {
    const report = JSON.parse(fs.readFileSync(path.join(directory, 'report.json')));
    assert.equal(report.assembler.modules.length, 18);
    assert.equal(report.assembler.images.length, 4);
    verifyCrossover(directory, report.assembler);
    assert.deepEqual(report.assembler.modules.filter(m => !m.comparison.raw_allocated_equal)
      .map(m => [m.module, m.source_role]), [['diagnostic_hw.o', 'llvm'], ['tilt_maze.o', 'llvm']]);
    assert(report.assembler.modules.some(m => !m.comparison.symbols_equal));
    for (const row of report.assembler.images) for (const linker of ['lld', 'owned']) {
      assert(row.linkers[linker].equal);
      assert.equal(row.linkers[linker].routes[0].bytes, row.linkers[linker].routes[1].bytes);
    }
    report.assembler.modules[0].comparison.relocations_equal = false;
    assert.throws(() => verifyCrossover(directory, report.assembler), /comparison tampered/);
  });
