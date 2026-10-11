import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { byteDifference, executable, verifyReport } from './pipeline-stages.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const relative = file => path.relative(root, file);
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const hash = file => digest(fs.readFileSync(file));
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const signed = (value, bits) => value >= 2 ** (bits - 1) ? value - 2 ** bits : value;
const u32 = value => value >>> 0;

export function decodeElf(bytes) {
  assert(Buffer.isBuffer(bytes) && bytes.length >= 52 && bytes.length <= 64 * 1024 * 1024, 'ELF size');
  assert(bytes.subarray(0, 7).equals(Buffer.from([127, 69, 76, 70, 1, 1, 1])), 'ELF32 little-endian header');
  assert.equal(bytes.readUInt16LE(18), 40, 'ELF ARM machine');
  assert.equal(bytes.readUInt32LE(20), 1, 'ELF version');
  assert.equal(bytes.readUInt16LE(40), 52, 'ELF header size');
  const type = bytes.readUInt16LE(16);
  assert([1, 2].includes(type), 'ELF type');
  const range = (offset, size) => {
    assert(Number.isSafeInteger(offset) && Number.isSafeInteger(size) &&
      offset >= 0 && size >= 0 && offset + size <= bytes.length, 'ELF range');
    return bytes.subarray(offset, offset + size);
  };
  const string = (data, offset) => {
    assert(offset < data.length, 'ELF string offset');
    const end = data.indexOf(0, offset);
    assert(end >= offset, 'ELF unterminated string');
    return data.toString('utf8', offset, end);
  };
  const count = bytes.readUInt16LE(48), start = bytes.readUInt32LE(32);
  assert(count > 0 && count <= 16384 && bytes.readUInt16LE(46) === 40, 'ELF section table');
  range(start, count * 40);
  const sections = Array.from({ length: count }, (_, index) => {
    const at = start + index * 40;
    const section = { index, nameOffset: bytes.readUInt32LE(at), type: bytes.readUInt32LE(at + 4),
      flags: bytes.readUInt32LE(at + 8), address: bytes.readUInt32LE(at + 12),
      offset: bytes.readUInt32LE(at + 16), size: bytes.readUInt32LE(at + 20),
      link: bytes.readUInt32LE(at + 24), info: bytes.readUInt32LE(at + 28),
      align: bytes.readUInt32LE(at + 32), entrySize: bytes.readUInt32LE(at + 36) };
    assert(section.align === 0 || (section.align & (section.align - 1)) === 0, 'ELF alignment');
    section.data = section.type === 8 ? Buffer.alloc(0) : range(section.offset, section.size);
    return section;
  });
  const nameIndex = bytes.readUInt16LE(50);
  assert(nameIndex > 0 && nameIndex < count && sections[nameIndex].type === 3, 'ELF section names');
  for (const section of sections) section.name = string(sections[nameIndex].data, section.nameOffset);
  const symbolTables = new Map(), symbols = [];
  for (const section of sections.filter(section => section.type === 2)) {
    assert(section.entrySize === 16 && section.size % 16 === 0 &&
      section.link < count && sections[section.link].type === 3, 'ELF symbol table');
    assert(section.size / 16 <= 262144, 'ELF symbol limit');
    const table = Array.from({ length: section.size / 16 }, (_, index) => {
      const at = index * 16, info = section.data[at + 12];
      const symbol = { index, name: string(sections[section.link].data, section.data.readUInt32LE(at)),
        value: section.data.readUInt32LE(at + 4), size: section.data.readUInt32LE(at + 8),
        binding: info >> 4, type: info & 15, visibility: section.data[at + 13] & 3,
        section: section.data.readUInt16LE(at + 14) };
      assert(symbol.section < count || [0xfff1, 0xfff2].includes(symbol.section), 'ELF symbol section');
      if (symbol.type === 2 && symbol.section > 0 && symbol.section < count) {
        const code = sections[symbol.section], address = symbol.value & ~1;
        assert((code.flags & 6) === 6 && address >= code.address &&
          address + symbol.size <= code.address + code.size, 'ELF function range');
      }
      return symbol;
    });
    symbolTables.set(section.index, table); symbols.push(...table);
  }
  assert(symbolTables.size === 1, 'ELF needs exactly one symbol table');
  const relocations = [];
  for (const section of sections.filter(section => section.type === 9)) {
    assert.equal(type, 1, 'unexpected linked relocations');
    const table = symbolTables.get(section.link);
    assert(table && section.entrySize === 8 && section.size % 8 === 0 &&
      section.info > 0 && section.info < count, 'ELF REL table');
    for (let at = 0; at < section.size; at += 8) {
      const offset = section.data.readUInt32LE(at), info = section.data.readUInt32LE(at + 4);
      assert(info >>> 8 < table.length && offset + 4 <= sections[section.info].size, 'ELF relocation range');
      relocations.push({ section: section.info, offset, type: info & 255, symbol: table[info >>> 8] });
      assert(relocations.length <= 262144, 'ELF relocation limit');
    }
  }
  assert(!sections.some(section => section.type === 4), 'ELF RELA unsupported');
  const programCount = bytes.readUInt16LE(44), programStart = bytes.readUInt32LE(28);
  const segments = [];
  if (programCount) {
    assert(programCount <= 128 && bytes.readUInt16LE(42) === 32, 'ELF program table');
    range(programStart, programCount * 32);
    for (let index = 0; index < programCount; index++) {
      const at = programStart + index * 32;
      if (bytes.readUInt32LE(at) !== 1) continue;
      const segment = { offset: bytes.readUInt32LE(at + 4), address: bytes.readUInt32LE(at + 8),
        load: bytes.readUInt32LE(at + 12), fileSize: bytes.readUInt32LE(at + 16),
        memorySize: bytes.readUInt32LE(at + 20) };
      assert(segment.fileSize <= segment.memorySize, 'ELF segment size');
      range(segment.offset, segment.fileSize); segments.push(segment);
    }
  }
  return { type, entry: bytes.readUInt32LE(24), sections, symbols, relocations, segments };
}

function immediate(data, offset) {
  const high = data.readUInt16LE(offset), low = data.readUInt16LE(offset + 2);
  return ((high & 15) << 12) | (((high >> 10) & 1) << 11) |
    (((low >> 12) & 7) << 8) | (low & 255);
}
function branch(data, offset) {
  const high = data.readUInt16LE(offset), low = data.readUInt16LE(offset + 2), sign = (high >> 10) & 1;
  return signed((sign << 24) | (((~((low >> 13) ^ sign)) & 1) << 23) |
    (((~((low >> 11) ^ sign)) & 1) << 22) | ((high & 1023) << 12) | ((low & 2047) << 1), 25);
}
export function relocationAddend(object, relocation) {
  const data = object.sections[relocation.section].data, offset = relocation.offset;
  if ([2, 3].includes(relocation.type)) return signed(data.readUInt32LE(offset), 32);
  if (relocation.type === 42) return signed(data.readUInt32LE(offset) & 0x7fffffff, 31);
  if ([10, 30].includes(relocation.type)) return branch(data, offset);
  if ([47, 48].includes(relocation.type)) return signed(immediate(data, offset), 16);
  throw new Error(`unsupported relocation audit: ${relocation.type}`);
}
function canonicalObject(object) {
  const sections = object.sections.filter(section => (section.flags & 2) && section.size).map(section => {
    const bytes = Buffer.from(section.data);
    const relocations = object.relocations.filter(relocation => relocation.section === section.index).map(relocation => {
      const symbol = relocation.symbol;
      let target = symbol.binding ? `global:${symbol.name}` :
        symbol.section === 0xfff1 ? 'absolute' : `section:${object.sections[symbol.section].name}`;
      let addend = relocationAddend(object, relocation) + (symbol.binding ? 0 : symbol.value);
      if ([10, 30].includes(relocation.type)) {
        addend += 4;
        if (!symbol.binding) addend -= symbol.value & 1;
      }
      bytes.fill(0, relocation.offset, relocation.offset + 4);
      return { offset: relocation.offset, type: relocation.type, target, addend, binding: symbol.binding };
    }).sort((a, b) => a.offset - b.offset);
    return { name: section.name, type: section.type, flags: section.flags, align: section.align,
      size: section.size, entrySize: section.entrySize, bytes: digest(bytes), relocations };
  }).sort((a, b) => a.name.localeCompare(b.name));
  const symbols = object.symbols.filter(symbol => symbol.binding && symbol.section).map(symbol => ({
    name: symbol.name, binding: symbol.binding, type: symbol.type, visibility: symbol.visibility,
    section: symbol.section < object.sections.length ? object.sections[symbol.section].name : symbol.section,
    value: symbol.value, size: symbol.size,
  })).sort((a, b) => a.name.localeCompare(b.name));
  return { sections, symbols };
}
export function compareObjects(a, b) {
  const before = canonicalObject(a), after = canonicalObject(b);
  assert.deepEqual(after, before, 'same assembly allocated bytes/definitions/relocations differ');
  return { allocated_sections: before.sections.length,
    relocations: before.sections.reduce((n, section) => n + section.relocations.length, 0),
    public_definitions: before.symbols.length,
    owned_sections: a.sections.length, llvm_sections: b.sections.length,
    owned_symbols: a.symbols.length, llvm_symbols: b.symbols.length };
}

export function inputPlacements(text) {
  const result = new Map();
  for (const line of text.split('\n')) {
    const match = line.match(/^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(.+):\(([^)]+)\)\s*$/i);
    if (!match || match[5] === '<internal>') continue;
    const key = `${path.resolve(root, match[5])}:${match[6]}`;
    assert(!result.has(key), `duplicate input placement: ${key}`);
    result.set(key, { address: parseInt(match[1], 16), load: parseInt(match[2], 16),
      size: parseInt(match[3], 16), align: Number(match[4]) });
  }
  return result;
}
function internalPlacements(text) {
  return [...text.matchAll(/^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+<internal>:\(([^)]+)\)\s*$/gmi)]
    .map(match => ({ address: parseInt(match[1], 16), size: parseInt(match[3], 16),
      align: Number(match[4]), name: match[5] }));
}
function bytesAt(elf, address, size) {
  const sections = elf.sections.filter(section => (section.flags & 2) && section.type !== 8 &&
    address >= section.address && address + size <= section.address + section.size);
  assert.equal(sections.length, 1, `unmapped linked bytes at 0x${address.toString(16)}`);
  return sections[0].data.subarray(address - sections[0].address, address - sections[0].address + size);
}
function mergeValue(section, offset) {
  assert(offset >= 0 && offset < section.size && section.entrySize > 0, 'merge target offset');
  if (section.flags & 32) {
    assert.equal(section.entrySize, 1, 'only byte-string merge entries are audited');
    const end = section.data.indexOf(0, offset);
    assert(end >= offset, 'unterminated merge string');
    return section.data.subarray(offset, end + 1);
  }
  const end = Math.floor(offset / section.entrySize + 1) * section.entrySize;
  assert(end <= section.size, 'partial merge constant');
  return section.data.subarray(offset, end);
}
export function auditLink(objects, elf, placements) {
  assert.equal(elf.type, 2);
  const globals = new Map();
  for (const symbol of elf.symbols.filter(symbol => symbol.binding && symbol.section && symbol.name)) {
    assert(!globals.has(symbol.name), `duplicate linked global: ${symbol.name}`);
    globals.set(symbol.name, symbol);
  }
  const placement = (file, section) => placements.get(`${path.resolve(root, file)}:${section.name}`);
  const report = { retained_sections: 0, discarded_sections: 0, relocations: {},
    retained_nonmerge: [], discarded_nonmerge: [],
    noload_sections: 0, merge_references: [], branches: 0, max_branch_displacement: 0 };
  for (const { file, object } of objects) {
    const checkedPairs = new Set();
    for (const section of object.sections.filter(section => (section.flags & 2) && section.size)) {
      const location = placement(file, section);
      const identity = `${path.basename(file).replace(/\.llvm\.o$/, '')}:${section.name}`;
      if (!location) {
        if (!(section.flags & 16)) report.discarded_nonmerge.push(identity);
        report.discarded_sections++;
        continue;
      }
      if (!(section.flags & 16)) report.retained_nonmerge.push(identity);
      report.retained_sections++;
      assert.equal(location.address % Math.max(1, section.align), 0, 'input section alignment');
      assert.equal(location.size, section.size, 'nonmerge input section size');
      const noLoad = elf.sections.some(output => output.type === 8 && (output.flags & 2) &&
        location.address >= output.address && location.address + section.size <= output.address + output.size);
      if (noLoad) {
        assert(!object.relocations.some(relocation => relocation.section === section.index), 'relocation in NOLOAD');
        report.noload_sections++;
        continue;
      }
      assert.notEqual(section.type, 8, 'NOBITS input became loaded');
      const input = Buffer.from(section.data), output = Buffer.from(bytesAt(elf, location.address, section.size));
      const relocations = object.relocations.filter(relocation => relocation.section === section.index);
      for (const relocation of relocations) {
        const { symbol, type, offset } = relocation, place = location.address + offset;
        const addend = relocationAddend(object, relocation);
        const targetSection = symbol.section > 0 && symbol.section < object.sections.length ?
          object.sections[symbol.section] : null;
        let value, weak = false;
        if (symbol.binding) {
          const resolved = globals.get(symbol.name);
          weak = !resolved && symbol.binding === 2;
          assert(resolved || weak, `unresolved global: ${symbol.name}`);
          value = resolved?.value ?? 0;
        } else if (symbol.section === 0xfff1) value = symbol.value;
        else if (targetSection && !(targetSection.flags & 16)) {
          const target = placement(file, targetSection);
          assert(target, `unmapped local target: ${file}/${targetSection.name}`);
          value = target.address + symbol.value;
        }
        report.relocations[type] = (report.relocations[type] ?? 0) + 1;
        if (targetSection?.flags & 16) {
          assert(!symbol.binding, 'global merge target needs a separate symbol audit');
          const payload = mergeValue(targetSection, symbol.value + addend);
          let address;
          if (type === 2) address = output.readUInt32LE(offset);
          else if ([47, 48].includes(type)) {
            const register = (input.readUInt16LE(offset + 2) >> 8) & 15;
            const matches = relocations.filter(other => other.type === (type === 47 ? 48 : 47) &&
              other.symbol.index === symbol.index && relocationAddend(object, other) === addend &&
              Math.abs(other.offset - offset) <= 12 &&
              ((input.readUInt16LE(other.offset + 2) >> 8) & 15) === register);
            assert.equal(matches.length, 1, 'merge MOVW/MOVT pair is not uniquely adjacent');
            const other = matches[0], low = type === 47 ? offset : other.offset, high = type === 48 ? offset : other.offset;
            address = u32(immediate(output, low) + immediate(output, high) * 65536);
            if (!checkedPairs.has(low)) {
              checkedPairs.add(low);
              report.merge_references.push({ object: relative(path.resolve(root, file)), section: section.name,
                offset: low, target_section: targetSection.name, target_offset: symbol.value + addend,
                address, bytes: payload.toString('hex'), sha256: digest(payload) });
            }
          } else throw new Error(`unsupported merge relocation: ${type}`);
          assert(bytesAt(elf, address, payload.length).equals(payload), `merge reference value mismatch: ${file}/${section.name}+${offset}`);
          if (type === 2) report.merge_references.push({ object: relative(path.resolve(root, file)),
            section: section.name, offset, target_section: targetSection.name,
            target_offset: symbol.value + addend, address, bytes: payload.toString('hex'), sha256: digest(payload) });
        } else if (type === 2) assert.equal(output.readUInt32LE(offset), u32(value + addend), 'ABS32 target');
        else if (type === 3) assert.equal(output.readUInt32LE(offset), u32((weak ? place : value) + addend - place), 'REL32 target');
        else if (type === 42) {
          assert.equal(output.readUInt32LE(offset) & 0x80000000, input.readUInt32LE(offset) & 0x80000000, 'PREL31 retained high bit');
          assert.equal(signed(output.readUInt32LE(offset) & 0x7fffffff, 31),
            (weak ? place + 4 : value) + addend - place, 'PREL31 target');
        } else if ([10, 30].includes(type)) {
          const displacement = branch(output, offset);
          assert.equal(displacement, weak ? addend + 4 : (value & ~1) + addend - place, 'Thumb branch target');
          report.branches++;
          report.max_branch_displacement = Math.max(report.max_branch_displacement, Math.abs(displacement));
        } else if ([47, 48].includes(type)) {
          const expected = u32(value + addend);
          assert.equal(immediate(output, offset), type === 47 ? expected & 65535 : expected >>> 16, 'MOVW/MOVT target');
        } else throw new Error(`unsupported retained relocation: ${type}`);
        if ([10, 30].includes(type)) {
          assert.equal(output.readUInt16LE(offset) & 0xf800, 0xf000, 'branch opcode');
          assert.equal(output.readUInt16LE(offset + 2) & 0xd000,
            input.readUInt16LE(offset + 2) & 0xd000, 'branch opcode preservation');
        }
        if ([47, 48].includes(type)) {
          assert.equal(output.readUInt16LE(offset) & 0xfbf0, input.readUInt16LE(offset) & 0xfbf0, 'MOV opcode preservation');
          assert.equal(output.readUInt16LE(offset + 2) & 0x8f00, input.readUInt16LE(offset + 2) & 0x8f00, 'MOV register preservation');
        }
      }
      for (const { offset } of relocations) {
        input.fill(0, offset, offset + 4); output.fill(0, offset, offset + 4);
      }
      assert(output.equals(input), `nonrelocation bytes changed: ${file}/${section.name}`);
    }
  }
  return report;
}
export function auditLoadedLayout(objects, elf, mapText, script) {
  const placements = inputPlacements(mapText), internal = internalPlacements(mapText);
  const merged = [], ranges = [];
  for (const { file, object } of objects)
    for (const section of object.sections.filter(section => (section.flags & 2) && section.type !== 8 && section.size)) {
      const location = placements.get(`${path.resolve(root, file)}:${section.name}`);
      if (location && elf.sections.some(output => output.type !== 8 && (output.flags & 2) &&
          location.address >= output.address && location.address + location.size <= output.address + output.size))
        ranges.push({ address: location.address, size: location.size, align: Math.max(1, section.align) });
    }
  for (const group of internal.filter(group => group.size && elf.sections.some(section =>
    (section.flags & 2) && section.type !== 8 && group.address >= section.address &&
    group.address + group.size <= section.address + section.size))) {
    const inputs = objects.flatMap(({ object }) => object.sections.filter(section =>
      (section.flags & 18) === 18 && section.name === group.name));
    assert(inputs.length, `unexplained synthetic output: ${group.name}`);
    const width = inputs[0].entrySize, strings = Boolean(inputs[0].flags & 32);
    assert(width > 0 && inputs.every(section => section.entrySize === width && Boolean(section.flags & 32) === strings),
      'inconsistent merge group');
    assert(!strings || width === 1, 'only byte-string merge output is audited');
    const values = new Set();
    for (const input of inputs) {
      for (let offset = 0; offset < input.size;) {
        const value = mergeValue(input, offset);
        values.add(value.toString('hex')); offset += value.length;
      }
    }
    const output = bytesAt(elf, group.address, group.size);
    let entries = 0;
    for (let offset = 0; offset < output.length;) {
      const end = strings ? output.indexOf(0, offset) + 1 : offset + width;
      assert(end > offset && end <= output.length, 'malformed merged output entry');
      assert(values.has(output.subarray(offset, end).toString('hex')), `invented merged value: ${group.name}`);
      offset = end; entries++;
    }
    merged.push({ name: group.name, address: group.address, size: group.size, entries, sha256: digest(output) });
    ranges.push(group);
  }
  const padding = [];
  for (const section of elf.sections.filter(section => (section.flags & 2) && section.type !== 8 && section.size)) {
    const contributions = ranges.filter(range => range.address >= section.address &&
      range.address + range.size <= section.address + section.size).sort((a, b) => a.address - b.address);
    assert(contributions.length, `unexplained loaded section: ${section.name}`);
    let cursor = section.address;
    for (const contribution of [...contributions, { address: section.address + section.size, size: 0, align: 4 }]) {
      assert(contribution.address >= cursor, 'overlapping input/synthetic contributions');
      const size = contribution.address - cursor;
      if (size) {
        assert(size < Math.max(1, contribution.align), `unexplained layout gap: ${section.name}`);
        const bytes = bytesAt(elf, cursor, size);
        const zero = bytes.every(byte => byte === 0);
        const trap = Boolean(section.flags & 4) && bytes.every(byte => byte === 0xd4);
        const nop = Boolean(section.flags & 4) && bytes.every((byte, index) => byte === (index & 1 ? 0xbf : 0));
        const fill = section.name === '.flash_end' && />\s*FLASH\s*=\s*0xaa/i.test(script) && bytes.every(byte => byte === 0xaa);
        assert(zero || trap || nop || fill, `unknown alignment fill: ${section.name}`);
        padding.push({ section: section.name, address: cursor, size, bytes: bytes.toString('hex') });
      }
      cursor = contribution.address + contribution.size;
    }
  }
  return { merged, padding };
}
export function auditBudgets(elf, script) {
  const number = text => {
    const match = text.match(/^(0x[0-9a-f]+|\d+)([kKmM]?)$/);
    assert(match, 'unsupported region bound');
    return Number(match[1]) * (match[2].toLowerCase() === 'k' ? 1024 : match[2].toLowerCase() === 'm' ? 1048576 : 1);
  };
  const regions = [...script.matchAll(/(\w+)\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-f]+|\d+)\s*,\s*LENGTH\s*=\s*(\w+)/gi)]
    .map(match => ({ name: match[1], address: number(match[2]), size: number(match[3]) }));
  assert(regions.length > 0, 'missing physical regions');
  for (const section of elf.sections.filter(section => (section.flags & 2) && section.size))
    assert(regions.some(region => section.address >= region.address &&
      section.address + section.size <= region.address + region.size), `section exceeds physical region: ${section.name}`);
  for (const segment of elf.segments.filter(segment => segment.fileSize && segment.load >= 0x10000000)) {
    assert(regions.some(region => segment.load >= region.address &&
      segment.load + segment.fileSize <= region.address + region.size), 'loaded segment exceeds physical region');
  }
  const entryName = script.match(/ENTRY\s*\(\s*(\w+)\s*\)/)?.[1];
  assert(entryName, 'missing explicit entry');
  assert.equal(elf.symbols.find(symbol => symbol.name === entryName)?.value, elf.entry, 'entry point differs from script');
  return { entry: elf.entry, entry_symbol: entryName, regions,
    sections: elf.sections.filter(section => (section.flags & 2) && section.size)
      .map(section => ({ name: section.name, address: section.address, size: section.size, type: section.type })) };
}

function functions(elf) {
  return elf.symbols.filter(symbol => symbol.type === 2 && symbol.section && symbol.name)
    .map(symbol => [symbol.name, symbol.value, symbol.size, symbol.binding])
    .sort((a, b) => a[0].localeCompare(b[0]) || a[1] - b[1]);
}
function qualification(report) {
  assert.equal(report.modules.length, 39, 'actual assembly inventory changed');
  assert.equal(report.images.length, 12, 'actual image inventory changed');
  assert.equal(new Set(report.modules.map(module => module.module)).size, 39, 'duplicate assembly evidence');
  assert.equal(new Set(report.images.map(image => image.image)).size, 12, 'duplicate image evidence');
  for (const module of report.modules)
    assert.equal(module.comparison.allocated_sections > 0, true, 'empty module evidence');
  for (const image of report.images) {
    assert(image.owned_objects.comparison.equal && image.same_assembly.comparison.equal, 'owned boundary loaded bytes differ');
    assert(image.assembler_replay.equal, 'reassembly changes original loaded bytes');
    for (const pool of [image.owned_objects, image.llvm_objects, image.same_assembly]) {
      assert.deepEqual(pool.llvm.functions, pool.owned.functions, 'function layout changed');
      const a = pool.owned.audit, b = pool.llvm.audit;
      assert.deepEqual(b.relocations, a.relocations, 'live relocation inventory changed');
      assert.deepEqual(b.retained_nonmerge, a.retained_nonmerge, 'retained section set changed');
      assert.deepEqual(b.discarded_nonmerge, a.discarded_nonmerge, 'GC/discard section set changed');
      const references = audit => audit.merge_references.map(reference => [
        path.basename(reference.object), reference.section, reference.offset,
        reference.target_section, reference.target_offset, reference.sha256]);
      assert.deepEqual(references(b), references(a), 'merge reference semantics changed');
    }
    assert.deepEqual(image.owned_objects.owned.audit, image.same_assembly.owned.audit, 'assembler changes linked semantics');
  }
}
export function verifyBoundaryReport(file) {
  const report = read(file);
  assert.equal(report.milestone, 'PO08');
  assert.equal(report.status, 'qualified');
  for (const artifact of report.artifacts) assert.equal(hash(path.join(root, artifact.file)), artifact.sha256, artifact.file);
  const source = verifyReport(path.join(root, report.input));
  assert.deepEqual(report.policy, source.policy, 'source policy changed');
  assert(report.commands.every(command => command.status === 0), 'failed capture command');
  for (const module of report.modules)
    assert.deepEqual(compareObjects(decodeElf(fs.readFileSync(path.join(root, module.owned_object))),
      decodeElf(fs.readFileSync(path.join(root, module.llvm_object)))), module.comparison);
  for (const image of report.images) {
    const original = source.images.find(item => item.image === image.image);
    assert(original, 'unrecognized production image');
    assert.equal(image.script, original.pools.owned.invocation.script);
    assert.deepEqual(image.linker_flags, original.pools.owned.invocation.linkerFlags);
    assert.deepEqual(image.owned_objects.objects, original.pools.owned.invocation.objects);
    assert.deepEqual(image.llvm_objects.objects, original.pools.llvm.invocation.objects);
    assert.deepEqual(image.same_assembly.objects, image.owned_objects.objects.map(object =>
      report.modules.find(module => module.owned_object === object)?.llvm_object), 'object order changed');
    for (const pool of [image.owned_objects, image.llvm_objects, image.same_assembly]) {
      const objects = pool.objects.map(file => ({ file, object: decodeElf(fs.readFileSync(path.join(root, file))) }));
      for (const role of ['owned', 'llvm']) {
        const result = pool[role], elf = decodeElf(fs.readFileSync(path.join(root, result.elf)));
        assert.deepEqual(functions(elf), result.functions);
        const command = report.commands.find(command => command.args.includes(path.join(root, result.elf)));
        assert(command, 'missing exact link command');
        assert.deepEqual(command.args, ['-T', path.join(root, image.script), ...image.linker_flags,
          '-Map', path.join(root, result.map), ...pool.objects.map(object => path.join(root, object)),
          '-o', path.join(root, result.elf)], 'link flags/object order changed');
        assert.deepEqual(auditLink(objects, elf, inputPlacements(fs.readFileSync(path.join(root, result.map), 'utf8'))), result.audit);
        assert.deepEqual(auditBudgets(elf, fs.readFileSync(path.join(root, image.script), 'utf8')), result.budgets);
        assert.deepEqual(auditLoadedLayout(objects, elf, fs.readFileSync(path.join(root, result.map), 'utf8'),
          fs.readFileSync(path.join(root, image.script), 'utf8')), result.layout);
      }
      assert.deepEqual(byteDifference(fs.readFileSync(path.join(root, pool.owned.binary)),
        fs.readFileSync(path.join(root, pool.llvm.binary))), pool.comparison);
    }
    assert.deepEqual(byteDifference(fs.readFileSync(path.join(root, image.owned_objects.owned.binary)),
      fs.readFileSync(path.join(root, image.same_assembly.owned.binary))), image.assembler_replay);
  }
  qualification(report); return report;
}
function capture(directory, inputFile) {
  const old = verifyReport(inputFile);
  assert.equal(old.scope, 'corpus');
  assert.equal(old.policy.BADGE_CODEGEN_SCALAR_SELECTION, 'thumb-calls', 'expected accepted PO07 selector');
  assert.equal(old.policy.BADGE_IR_EXPRESSION_LOWERING, 'ssa', 'expected accepted SSA frontend');
  fs.mkdirSync(directory);
  const file = path.join(directory, 'results.json');
  const report = { schema: 1, milestone: 'PO08', status: 'in-progress', started_at: new Date().toISOString(), input: relative(inputFile),
    policy: old.policy, modules: [], images: [], commands: [], artifacts: [],
    limitations: ['Host-only equivalence; no badge programming or new application timings.',
      'Merged references are checked by resolved payload, not placement/order equality.',
      'No pooling, relaxation, veneers or default changes. D06 shared-pool/alignment deferrals remain.',
      'Input sections omitted by both linkers are GC/discard evidence, not proof of whole-program reachability.'] };
  const save = () => fs.writeFileSync(file, JSON.stringify(report, null, 2) + '\n');
  const artifacts = new Set([inputFile, fileURLToPath(import.meta.url),
    path.join(root, 'badge-compiler/tools/pipeline-stages.mjs')]);
  const run = (command, args, label) => {
    const log = path.join(directory, `command-${report.commands.length}.log`);
    const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000, maxBuffer: 32 * 1024 * 1024 });
    fs.writeFileSync(log, `${result.stdout ?? ''}${result.stderr ?? ''}${result.error ?? ''}`);
    artifacts.add(log); report.commands.push({ command, args, label, log: relative(log), status: result.status });
    assert.ifError(result.error); assert.equal(result.status, 0, `${label}: ${result.stderr}`);
  };
  const clang = executable(process.env.LLVM_CLANG ?? 'clang'), lld = executable(process.env.LLVM_LLD ?? 'ld.lld');
  for (const command of [clang, lld, ...['badge-asm', 'badge-link', 'elf2bin'].map(name => path.join(tools, name))])
    artifacts.add(command);
  const swapped = new Map(), nativeObjects = new Map();
  save();
  try {
    for (const module of [...old.modules, ...old.assembly_sources]) {
      const invocation = module.owned_invocation, original = path.resolve(root, invocation.output);
      const assembly = path.join(`${original}.owned`, invocation.source.endsWith('.S') ? 'preprocessed.s' : 'assembly.s');
      const name = path.basename(original), object = path.join(directory, `${name}.llvm.o`);
      run(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-x', 'assembler',
        '-c', assembly, '-o', object], `assemble-${name}`);
      artifacts.add(assembly); artifacts.add(original); artifacts.add(object);
      const a = decodeElf(fs.readFileSync(original)), b = decodeElf(fs.readFileSync(object));
      report.modules.push({ module: name, assembly: relative(assembly), owned_object: relative(original),
        llvm_object: relative(object), owned_file_bytes: fs.statSync(original).size,
        llvm_file_bytes: fs.statSync(object).size, comparison: compareObjects(a, b) });
      swapped.set(original, object); nativeObjects.set(original, a);
    }
    for (const image of old.images) {
      const owned = image.pools.owned.invocation, llvm = image.pools.llvm.invocation;
      assert.equal(owned.script, llvm.script); assert.deepEqual(owned.linkerFlags, llvm.linkerFlags);
      assert.deepEqual(owned.objects.map(object => path.basename(object)), llvm.objects.map(object => path.basename(object)));
      const script = path.resolve(root, owned.script); artifacts.add(script);
      const record = { image: image.image, script: relative(script), linker_flags: owned.linkerFlags };
      for (const [id, objects] of [
        ['owned_objects', owned.objects.map(object => path.resolve(root, object))],
        ['llvm_objects', llvm.objects.map(object => path.resolve(root, object))],
        ['same_assembly', owned.objects.map(object => swapped.get(path.resolve(root, object)))],
      ]) {
        assert(objects.every(Boolean), 'unpaired assembly input');
        const pool = { objects: objects.map(relative) };
        const models = objects.map(object => {
          artifacts.add(object); return { file: relative(object), object: nativeObjects.get(object) ?? decodeElf(fs.readFileSync(object)) };
        });
        for (const role of ['owned', 'llvm']) {
          const stem = path.join(directory, `${image.image}.${id}.${role}`);
          const elfFile = `${stem}.elf`, map = `${stem}.map`, binary = `${stem}.bin`;
          run(role === 'owned' ? path.join(tools, 'badge-link') : lld,
            ['-T', script, ...owned.linkerFlags, '-Map', map, ...objects, '-o', elfFile], `link-${id}-${role}`);
          run(path.join(tools, 'elf2bin'), ['-O', 'binary', elfFile, binary], 'extract-load-image');
          const elf = decodeElf(fs.readFileSync(elfFile));
          pool[role] = { elf: relative(elfFile), map: relative(map), binary: relative(binary),
            functions: functions(elf),
            elf_file_bytes: fs.statSync(elfFile).size, load_file_bytes: fs.statSync(binary).size,
            audit: auditLink(models, elf, inputPlacements(fs.readFileSync(map, 'utf8'))),
            budgets: auditBudgets(elf, fs.readFileSync(script, 'utf8')),
            layout: auditLoadedLayout(models, elf, fs.readFileSync(map, 'utf8'), fs.readFileSync(script, 'utf8')) };
          for (const artifact of [elfFile, map, binary]) artifacts.add(artifact);
        }
        pool.comparison = byteDifference(fs.readFileSync(path.join(root, pool.owned.binary)),
          fs.readFileSync(path.join(root, pool.llvm.binary)));
        record[id] = pool;
      }
      const primary = path.resolve(root, owned.output).replace(/\.elf$/, '.bin');
      assert(fs.readFileSync(primary).equals(fs.readFileSync(path.join(root, record.owned_objects.owned.binary))), 'primary owned link replay');
      record.assembler_replay = byteDifference(fs.readFileSync(path.join(root, record.owned_objects.owned.binary)),
        fs.readFileSync(path.join(root, record.same_assembly.owned.binary)));
      artifacts.add(primary); report.images.push(record); save();
      console.log(`PO08: ${record.image} objects/assembly, relocated values and physical budgets verified.`);
    }
    qualification(report); report.status = 'qualified';
  } catch (error) { report.status = 'failed'; report.error = error.message; throw error; }
  finally {
    report.completed_at = new Date().toISOString();
    report.artifacts = [...artifacts].map(artifact => ({ file: relative(artifact), sha256: hash(artifact) }));
    save();
  }
  verifyBoundaryReport(file);
  console.log(`PO08: ${report.modules.length} actual assembly inputs, ${report.images.length} images qualified. ${file}`);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, output, input, ...extra] = process.argv.slice(2);
    assert(extra.length === 0 && (mode === '--capture' && output && input || mode === '--verify' && output && !input),
      'Usage: boundary-evidence.mjs --capture NEW_DIRECTORY PO00_CORPUS.json | --verify REPORT.json');
    if (mode === '--capture') capture(path.resolve(output), path.resolve(input));
    else {
      verifyBoundaryReport(path.resolve(output));
      console.log('PASS: allocated assembly semantics, linker relocation/value audits, loaded images and physical budgets.');
    }
  } catch (error) { console.error(`boundary-evidence: ${error.message}`); process.exitCode = 1; }
}
