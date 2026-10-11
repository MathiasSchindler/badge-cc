import assert from 'node:assert/strict';
import fs from 'node:fs';
import { readObject, normalizeObject, relocationField } from './assembler-crossover.mjs';

export function verifyLinkedRanges(ranges, object, elfFile, binaryFile) {
  const elf = fs.readFileSync(elfFile), binary = fs.readFileSync(binaryFile);
  assert(elf.length >= 52 && elf.readUInt32LE(0) === 0x464c457f && elf[4] === 1 && elf[5] === 1);
  const at = elf.readUInt32LE(28), stride = elf.readUInt16LE(42), count = elf.readUInt16LE(44);
  assert(stride === 32 && at + stride * count <= elf.length, 'invalid linked program table');
  const segments = [];
  for (let i = 0; i < count; i++) {
    const p = at + i * stride;
    if (elf.readUInt32LE(p) !== 1) continue;
    segments.push({ address: elf.readUInt32LE(p + 8), load: elf.readUInt32LE(p + 12),
      file_size: elf.readUInt32LE(p + 16), memory_size: elf.readUInt32LE(p + 20) });
  }
  const base = Math.min(...segments.filter(s => s.file_size).map(s => s.load));
  assert(Number.isFinite(base), 'linked image has no loaded segment');
  for (const range of ranges.filter(r => r.placement === 'mapped')) {
    const section = object.sections.find(s => s.name === range.section);
    if (!(section.flags & 2)) continue;
    const segment = segments.find(s => range.address >= s.address &&
      range.address + range.size <= s.address + s.memory_size);
    assert(segment, 'mapped range outside linked memory');
    assert.equal(range.load_address, segment.load + range.address - segment.address, 'wrong load address');
    if (section.type === 8) continue;
    const offset = range.load_address - base;
    assert(offset >= 0 && offset + range.size <= binary.length, 'mapped range outside loaded binary');
    const expected = Buffer.from(range.hex, 'hex'), actual = Buffer.from(binary.subarray(offset, offset + range.size));
    for (const relocation of object.relocations.filter(r => object.sections[r.section].name === range.section &&
      r.offset >= range.offset && r.offset < range.offset + range.size)) {
      const { mask } = relocationField(section, relocation), p = relocation.offset - range.offset;
      for (const buffer of [expected, actual]) buffer.writeUInt32LE((buffer.readUInt32LE(p) & ~mask) >>> 0, p);
    }
    assert(actual.equals(expected), 'linked bytes disagree with listing outside relocation fields');
  }
}

export function verifiedEmissionSections(trace, production) {
  const a = normalizeObject(trace), b = normalizeObject(production);
  const equal = (x, y) => JSON.stringify(x) === JSON.stringify(y);
  return production.sections.filter(section => {
    const candidate = trace.sections.find(s => s.name === section.name);
    if (!candidate || candidate.size !== section.size || candidate.type !== section.type ||
      candidate.flags !== section.flags || candidate.alignment !== section.alignment ||
      !candidate.data.equals(section.data)) return false;
    return equal(a.relocations.filter(r => r.section === section.name),
      b.relocations.filter(r => r.section === section.name)) &&
      equal(a.symbols.filter(s => s.section === section.name && s.binding !== 0),
        b.symbols.filter(s => s.section === section.name && s.binding !== 0));
  }).map(s => s.name).filter(Boolean);
}

export function verifyListing(listing, objectFile, assemblyFile, irFile = null) {
  assert.equal(listing.schema, 1);
  assert.equal(listing.source_encoding, 'byte-codepoints');
  assert.equal(typeof listing.input, 'string');
  assert(irFile ? typeof listing.ir_source === 'string' : listing.ir_source === null);
  const source = fs.readFileSync(assemblyFile);
  assert.equal(source.length, listing.input_bytes);
  assert(Buffer.from(listing.source, 'latin1').equals(source), 'listing input provenance mismatch');
  const object = readObject(objectFile);
  const cursors = new Map();
  const inputLines = source.toString('utf8').split('\n');
  const irLines = irFile ? fs.readFileSync(irFile, 'utf8').split('\n') : [];
  const origins = [];
  let marker = 0;
  for (const line of inputLines) {
    const match = line.match(/^\s*@ attribution-ir (\d+)\s*$/);
    if (match) marker = Number(match[1]);
    else if (/@ attribution-function |\.type[ \t]/.test(line)) marker = 0;
    origins.push(marker);
  }
  const relocations = [];
  for (const range of listing.ranges) {
    const section = object.sections.find(s => s.name === range.section);
    assert(section, 'listing section missing from object');
    assert.equal(range.offset, cursors.get(range.section) ?? 0, 'listing gap or overlap');
    assert(Number.isInteger(range.size) && range.size > 0 && range.offset + range.size <= section.size);
    cursors.set(range.section, range.offset + range.size);
    if (section.type === 8) assert.equal(range.hex, null);
    else assert.equal(Buffer.from(range.hex, 'hex').toString('hex'),
      section.data.subarray(range.offset, range.offset + range.size).toString('hex'), 'listing byte mismatch');
    assert(range.line === 0 && range.kind === 'generated-metadata' ||
      Number.isInteger(range.line) && range.line > 0 && range.line <= inputLines.length, 'invalid listing line');
    if (range.kind === 'instruction') {
      assert([2, 4].includes(range.width) && range.size === range.width, 'invalid instruction width');
      assert(inputLines[range.line - 1].includes(range.operation), 'operation/input line mismatch');
    }
    if (range.origin_line) {
      assert(irFile && range.origin_line > 0 && range.origin_line <= irLines.length, 'invalid IR origin');
      // Markers describe emission provenance, not semantic instruction equivalence.
      assert.equal(range.origin_line, origins[range.line - 1], 'IR marker mismatch');
    }
    for (const relocation of range.relocations) {
      assert(relocation.offset >= range.offset && relocation.offset + 4 <= range.offset + range.size);
      relocations.push({ section: range.section, ...relocation });
    }
  }
  for (const section of object.sections.filter(s => s.index && ![2, 3, 9].includes(s.type)))
    assert.equal(cursors.get(section.name) ?? 0, section.size, `unaccounted emitted bytes: ${section.name}`);
  const expected = object.relocations.map(r => ({ section: object.sections[r.section].name,
    offset: r.offset, type: r.type, symbol: r.symbol.name }));
  assert.deepEqual(relocations.sort((a, b) => a.section.localeCompare(b.section) || a.offset - b.offset),
    expected.sort((a, b) => a.section.localeCompare(b.section) || a.offset - b.offset));
  for (const decision of listing.decisions) {
    assert(decision.line > 0 && decision.line <= inputLines.length);
    const section = object.sections.find(s => s.name === decision.section);
    assert(section && decision.offset <= section.size);
    if ('pc_base' in decision) {
      assert(decision.displacement >= decision.reach_min && decision.displacement <= decision.reach_max);
      assert(decision.pc_base === decision.offset + 4 || decision.pc_base === ((decision.offset + 4) & ~3));
    }
    if ('alignment' in decision) {
      assert.equal(decision.required_padding, (-decision.offset) & (decision.alignment - 1));
      const emitted = listing.ranges.filter(r => r.line === decision.line && r.kind === 'alignment')
        .reduce((n, r) => n + r.size, 0);
      assert.equal(emitted, decision.max_skip_exceeded ? 0 : decision.required_padding);
    }
  }
  return listing.ranges.reduce((counts, range) => {
    counts[range.kind] = (counts[range.kind] ?? 0) + range.size;
    return counts;
  }, {});
}

export function placements(mapText, objectName, relocated = false) {
  const rows = [];
  for (const line of mapText.split('\n')) {
    const match = line.match(/^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\d+\s+(.+):\(([^)]+)\)\s*$/i);
    if (!match || !(match[4] === objectName ||
      relocated && match[4].endsWith(`/assembler/${objectName.split('/').at(-1)}`))) continue;
    rows.push({ section: match[5], address: parseInt(match[1], 16),
      load_address: parseInt(match[2], 16), size: parseInt(match[3], 16) });
  }
  return rows;
}

export function linkListing(listing, rows, verifiedSections = null) {
  return listing.ranges.map(range => {
    if (verifiedSections && !verifiedSections.includes(range.section))
      return { ...range, origin_line: 0, placement: 'trace-differs-from-production',
        address: null, load_address: null };
    const candidates = rows.filter(row => row.section === range.section);
    const row = candidates.length === 1 ? candidates[0] : null;
    if (row) assert(range.offset + range.size <= row.size, 'listing outside linked input contribution');
    return { ...range, placement: row ? 'mapped' : candidates.length ? 'ambiguous' : 'discarded-or-unmapped',
      address: row ? row.address + range.offset : null,
      load_address: row ? row.load_address + range.offset : null };
  });
}
