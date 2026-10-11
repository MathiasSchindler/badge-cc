import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { executable } from './pipeline-stages.mjs';
import { verifyListing, placements, linkListing, verifiedEmissionSections, verifyLinkedRanges } from './assembler-listing.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const sign = (value, bits) => (value << (32 - bits)) >> (32 - bits);
const sum = array => array.reduce((a, b) => a + b, 0);

export function readObject(file) {
  const data = fs.readFileSync(file);
  assert(data.length >= 52 && data.readUInt32LE(0) === 0x464c457f, 'not ELF');
  assert(data[4] === 1 && data[5] === 1 && data.readUInt16LE(16) === 1 &&
    data.readUInt16LE(18) === 40, 'expected little-endian Arm ELF32 relocatable object');
  const at = data.readUInt32LE(32), stride = data.readUInt16LE(46), count = data.readUInt16LE(48);
  assert(stride === 40 && at + stride * count <= data.length, 'invalid section table');
  const sections = Array.from({ length: count }, (_, index) => {
    const p = at + index * stride;
    const item = { index, name_offset: data.readUInt32LE(p), type: data.readUInt32LE(p + 4),
      flags: data.readUInt32LE(p + 8), offset: data.readUInt32LE(p + 16),
      size: data.readUInt32LE(p + 20), link: data.readUInt32LE(p + 24),
      info: data.readUInt32LE(p + 28), alignment: data.readUInt32LE(p + 32),
      entry_size: data.readUInt32LE(p + 36) };
    assert(item.type === 8 || item.offset + item.size <= data.length, 'section outside ELF');
    item.data = item.type === 8 ? Buffer.alloc(0) : data.subarray(item.offset, item.offset + item.size);
    return item;
  });
  const string = (table, offset) => {
    assert(table && offset < table.length, 'string outside table');
    const end = table.indexOf(0, offset);
    assert(end >= offset, 'unterminated ELF string');
    return table.toString('utf8', offset, end);
  };
  const names = sections[data.readUInt16LE(50)]?.data;
  for (const section of sections) section.name = string(names, section.name_offset);
  const tables = new Map(), symbols = [];
  for (const section of sections.filter(s => s.type === 2)) {
    assert(section.entry_size === 16 && section.size % 16 === 0, 'invalid symbol table');
    const table = [];
    for (let p = 0; p < section.size; p += 16) {
      const s = section.data;
      const item = { name: string(sections[section.link]?.data, s.readUInt32LE(p)),
        value: s.readUInt32LE(p + 4), size: s.readUInt32LE(p + 8),
        binding: s[p + 12] >> 4, type: s[p + 12] & 15, visibility: s[p + 13] & 3,
        section: s.readUInt16LE(p + 14) };
      table.push(item);
      if (p) symbols.push(item);
    }
    tables.set(section.index, table);
  }
  const relocations = [];
  for (const section of sections.filter(s => s.type === 9)) {
    assert(section.entry_size === 8 && section.size % 8 === 0, 'invalid REL table');
    const table = tables.get(section.link);
    assert(table && sections[section.info], 'invalid relocation references');
    for (let p = 0; p < section.size; p += 8) {
      const info = section.data.readUInt32LE(p + 4);
      const symbol = table[info >>> 8];
      assert(symbol, 'invalid relocation symbol');
      relocations.push({ section: section.info, offset: section.data.readUInt32LE(p),
        type: info & 255, symbol });
    }
  }
  assert(!sections.some(s => s.type === 4), 'RELA is outside the supported crossover contract');
  return { sections, symbols, relocations, bytes: data.length };
}

export function relocationField(section, relocation) {
  const { offset, type } = relocation;
  assert(offset + 4 <= section.data.length, 'relocation outside section');
  const word = section.data.readUInt32LE(offset), a = word & 65535, b = word >>> 16;
  if ([2, 3].includes(type)) return { addend: word | 0, mask: 0xffffffff };
  if (type === 42) return { addend: sign(word & 0x7fffffff, 31), mask: 0x7fffffff };
  if (type === 10 || type === 30) {
    const s = (a >>> 10) & 1, j1 = (b >>> 13) & 1, j2 = (b >>> 11) & 1;
    const value = (s << 24) | (((j1 ^ s) ^ 1) << 23) | (((j2 ^ s) ^ 1) << 22) |
      ((a & 1023) << 12) | ((b & 2047) << 1);
    return { addend: sign(value, 25), mask: (0x2fff << 16 | 0x7ff) >>> 0 };
  }
  if (type === 47 || type === 48) {
    const immediate = ((a & 15) << 12) | (((a >>> 10) & 1) << 11) |
      (((b >>> 12) & 7) << 8) | (b & 255);
    return { addend: sign(immediate, 16), mask: (0x70ff << 16 | 0x040f) >>> 0 };
  }
  throw new Error(`unsupported relocation normalization: ${type}`);
}

export function normalizeObject(object) {
  const { sections, relocations } = object;
  const normalizedRelocations = [], masked = new Map();
  for (const section of sections.filter(s => s.flags & 2)) masked.set(section.index, Buffer.from(section.data));
  for (const relocation of relocations) {
    const section = sections[relocation.section];
    if (!(section.flags & 2)) continue;
    const { addend, mask } = relocationField(section, relocation), symbol = relocation.symbol;
    let target, effective = addend, thumb = 0;
    if (symbol.section === 0) target = `undefined:${symbol.name}`;
    else if (symbol.section === 0xfff1) { target = 'absolute'; effective += symbol.value; }
    else if (symbol.section === 0xfff2) target = `common:${symbol.name}:${symbol.size}:${symbol.value}`;
    else {
      assert(sections[symbol.section], 'symbol section outside table');
      target = `section:${sections[symbol.section].name}`;
      thumb = symbol.type === 2 ? symbol.value & 1 : 0;
      effective += symbol.value - thumb;
    }
    normalizedRelocations.push({ section: section.name, offset: relocation.offset, type: relocation.type,
      target, effective_addend: effective, thumb });
    const buffer = masked.get(section.index), word = buffer.readUInt32LE(relocation.offset);
    buffer.writeUInt32LE((word & ~mask) >>> 0, relocation.offset);
  }
  const allocated = sections.filter(s => s.flags & 2).map(section => ({
    name: section.name, type: section.type, flags: section.flags, alignment: section.alignment,
    entry_size: section.entry_size, size: section.size, content: masked.get(section.index).toString('hex'),
  })).sort((a, b) => a.name.localeCompare(b.name));
  const symbolInventory = object.symbols.filter(s => s.name && s.type !== 4 &&
    !(s.binding === 0 && (/^\$[adt](?:\.|$)/.test(s.name) || s.name.startsWith('.L') ||
      s.name.startsWith('.asm.dot.'))) && s.type !== 3)
    .map(s => ({ name: s.name, value: s.value, size: s.size, binding: s.binding, type: s.type,
      visibility: s.visibility, section: sections[s.section]?.name ?? s.section }))
    .sort((a, b) => a.name.localeCompare(b.name));
  return { allocated, relocations: normalizedRelocations.sort((a, b) =>
    a.section.localeCompare(b.section) || a.offset - b.offset), symbols: symbolInventory };
}

export function compareObjects(owned, llvm) {
  const a = normalizeObject(owned), b = normalizeObject(llvm);
  const equal = (x, y) => JSON.stringify(x) === JSON.stringify(y);
  const raw = object => object.sections.filter(s => s.flags & 2).map(s =>
    ({ name: s.name, data: s.data.toString('hex') })).sort((a, b) => a.name.localeCompare(b.name));
  return { allocated_equal: equal(a.allocated, b.allocated),
    relocations_equal: equal(a.relocations, b.relocations), symbols_equal: equal(a.symbols, b.symbols),
    public_symbols_equal: equal(a.symbols.filter(s => s.binding !== 0), b.symbols.filter(s => s.binding !== 0)),
    raw_allocated_equal: equal(raw(owned), raw(llvm)), normalized: { owned: a, llvm: b },
    symbol_differences: a.symbols.filter(s => !b.symbols.some(t => equal(s, t))).map(s =>
      ({ owned: s, llvm: b.symbols.find(t => t.name === s.name) ?? null }))
      .concat(b.symbols.filter(s => !a.symbols.some(t => t.name === s.name)).map(s => ({ owned: null, llvm: s }))),
    metadata: Object.fromEntries([['owned', owned], ['llvm', llvm]].map(([role, object]) => [role, {
      elf_bytes: object.bytes, symbol_count: object.symbols.length,
      allocated_file_bytes: sum(object.sections.filter(s => s.flags & 2 && s.type !== 8).map(s => s.size)),
      nobits_bytes: sum(object.sections.filter(s => s.flags & 2 && s.type === 8).map(s => s.size)),
      nonallocated_sections: object.sections.filter(s => !(s.flags & 2) && s.index)
        .map(s => ({ name: s.name, bytes: s.size, type: s.type })),
      mapping: object.sections.filter(s => s.flags & 2 && s.type !== 8).map(section => {
        const markers = object.symbols.filter(s => s.section === section.index &&
          /^\$[adt](?:\.|$)/.test(s.name)).sort((a, b) => a.value - b.value);
        const ranges = markers.map((symbol, index) => ({
          kind: symbol.name[1] === 't' ? 'thumb-code-including-code-padding' : 'data',
          start: symbol.value, bytes: (markers[index + 1]?.value ?? section.size) - symbol.value,
        }));
        return { section: section.name, ranges,
          unmapped_bytes: section.size - sum(ranges.map(range => range.bytes)) };
      }),
    }])) };
}

export function captureCrossover(directory, report, config) {
  const destination = path.join(directory, 'assembler');
  fs.mkdirSync(destination);
  const tool = name => path.join(root, 'badge-compiler/build', name);
  const lld = executable(process.env.LLVM_LLD ??
    (process.env.LLVM_BIN ? path.join(process.env.LLVM_BIN, 'ld.lld') : 'ld.lld'));
  const result = { schema: 1, status: 'complete', target: 'RP2350 Cortex-M33 Thumb little-endian',
    options: ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-x', 'assembler'],
    commands: [], artifacts: [], modules: [], images: [], tools: [] };
  const artifact = file => {
    if (!file.startsWith(directory + path.sep)) {
      const archived = path.join(destination, `${hash(file)}-${path.basename(file)}`);
      fs.copyFileSync(file, archived);
      file = archived;
    }
    const item = { file: path.relative(directory, file).split(path.sep).join('/'),
      bytes: fs.statSync(file).size, sha256: hash(file) };
    result.artifacts.push(item);
    return item.file;
  };
  function run(command, args, label) {
    const response = spawnSync(command, args, { cwd: root, encoding: 'utf8',
      timeout: 180000, maxBuffer: 64000000 });
    const log = path.join(destination, `${label}.log`);
    fs.writeFileSync(log, `${response.stdout ?? ''}${response.stderr ?? ''}`);
    result.commands.push({ command, args, status: response.status, log: artifact(log) });
    assert.ifError(response.error);
    assert.equal(response.status, 0, `${label}: ${response.stderr}`);
    return response.stdout;
  }
  for (const command of [config.llvm.clang, config.llvm.objdump, config.llvm.objcopy, lld,
    tool('badge-asm'), tool('badge-codegen-attribution'), tool('badge-link'), tool('badge-elf-info'), tool('elf2bin')])
    result.tools.push({ file: command, sha256: hash(command) });
  result.llvm_version = run(config.llvm.clang, ['--version'], 'clang-version').trim();
  result.lld_version = run(lld, ['--version'], 'lld-version').trim();
  const outputs = new Map();
  for (const module of report.modules) for (const sourceRole of ['owned', 'llvm']) {
    const assembly = path.join(directory, module.roles[sourceRole].stages.assembly.file);
    const row = { module: module.id, source_role: sourceRole, assembly: artifact(assembly), objects: {} };
    for (const assembler of ['owned', 'llvm']) {
      const label = `${module.id}-${sourceRole}-${assembler}`, output = path.join(destination, `${label}.o`);
      const listing = path.join(destination, `${label}.listing.json`);
      run(assembler === 'owned' ? tool('badge-asm') : config.llvm.clang,
        assembler === 'owned' ? ['--listing', listing, assembly, '-o', output] :
          [...result.options, '-c', assembly, '-o', output], label);
      row.objects[assembler] = artifact(output);
      if (assembler === 'owned') {
        row.listing = artifact(listing);
        row.listing_bytes = verifyListing(JSON.parse(fs.readFileSync(listing)), output, assembly);
        const plain = path.join(destination, `${label}.plain.o`);
        run(tool('badge-asm'), [assembly, '-o', plain], `${label}-plain`);
        row.plain_object = artifact(plain);
        assert.equal(hash(plain), hash(output), 'listing changed object bytes');
      }
      const disassembly = path.join(destination, `${label}.txt`);
      fs.writeFileSync(disassembly, run(config.llvm.objdump,
        ['-d', '--triple=thumbv8m.main-none-eabi', output], `${label}-decode`));
      artifact(disassembly);
      outputs.set(`${sourceRole}/${assembler}/${module.roles[sourceRole].invocation.output}`, output);
    }
    row.comparison = compareObjects(...['owned', 'llvm'].map(role =>
      readObject(path.join(directory, row.objects[role]))));
    assert(row.comparison.allocated_equal && row.comparison.relocations_equal && row.comparison.public_symbols_equal,
      `unexplained object difference: ${module.id}/${sourceRole}`);
    if (sourceRole === 'owned' && config.policy.BADGE_CODEGEN_SCALAR_SELECTION !== 'thumb-schedule') {
      const label = `${module.id}-emission`, assembly = path.join(destination, `${label}.s`);
      const ir = path.join(directory, module.roles.owned.stages.optimized.file);
      const options = ['frame-addressing', 'register-residency', 'scalar-selection', 'memory-selection']
        .map(option => `--${option}=${config.policy[`BADGE_CODEGEN_${option.toUpperCase().replaceAll('-', '_')}`]}`);
      run(tool('badge-codegen-attribution'), [...options, ir, '-o', assembly], label);
      const output = path.join(destination, `${label}.o`), listing = path.join(destination, `${label}.listing.json`);
      run(tool('badge-asm'), ['--listing', listing, '--ir-source', ir, assembly, '-o', output], `${label}-assemble`);
      const comparison = compareObjects(readObject(output), readObject(path.join(directory, row.objects.owned)));
      row.emission = { assembly: artifact(assembly), object: artifact(output), listing: artifact(listing),
        ir: artifact(ir), comparison,
        verified_sections: verifiedEmissionSections(readObject(output), readObject(path.join(directory, row.objects.owned))),
        listing_bytes: verifyListing(JSON.parse(fs.readFileSync(listing)), output, assembly, ir) };
    } else row.emission_unavailable = sourceRole === 'llvm' ?
      'Production LLVM assembly has no source-location markers; C/IR lines remain unmapped.' :
      'Attribution emission does not support instruction scheduling.';
    result.modules.push(row);
  }
  for (const image of report.images) {
    const row = { name: image.name, source_role: image.role, linkers: {},
      production_binary: artifact(path.join(directory, image.binary.file)),
      production_linker: image.role === 'llvm' ? 'lld' : 'owned' };
    for (const linker of ['lld', 'owned']) {
      const binaries = [];
      const routes = [];
      for (const assembler of ['owned', 'llvm']) {
        const label = `${image.name}-${image.role}-${assembler}-${linker}`;
        const elf = path.join(destination, `${label}.elf`);
        const objects = image.invocation.objects.map(object =>
          outputs.get(`${image.role}/${assembler}/${object}`) ?? object);
        for (const object of image.invocation.objects)
          if (!outputs.has(`${image.role}/${assembler}/${object}`)) artifact(object);
        const script = path.resolve(root, image.invocation.script);
        artifact(script);
        const map = `${elf}.map`;
        if (linker === 'owned') run(tool('badge-link'), [...image.invocation.linkerFlags,
          '-T', script, '-Map', map, ...objects, '-o', elf], label);
        else run(lld, ['-m', 'armelf', ...image.invocation.linkerFlags, '-T', script,
          `-Map=${map}`, ...objects, '-o', elf], label);
        const binary = elf.replace(/\.elf$/, '.bin');
        run(linker === 'owned' ? tool('elf2bin') : config.llvm.objcopy,
          ['-O', 'binary', elf, binary], `${label}-binary`);
        artifact(elf); artifact(map);
        const listings = [];
        if (assembler === 'owned') for (const module of result.modules.filter(m => m.source_role === image.role)) {
          const object = path.join(directory, module.objects.owned);
          if (!objects.includes(object)) continue;
          const listing = JSON.parse(fs.readFileSync(path.join(directory, module.listing)));
          const rows = placements(fs.readFileSync(map, 'utf8'), object);
          // SHF_MERGE contributions may have non-linear input-offset placement.
          const merged = new Set(readObject(object).sections.filter(s => s.flags & 16).map(s => s.name));
          const safeRows = rows.filter(row => !merged.has(row.section));
          const linkedFile = path.join(destination, `${label}-${module.module}.listing.json`);
          const linked = { module: module.module, listing: module.listing,
            ranges: linkListing(listing, safeRows) };
          if (module.emission) {
            linked.emission_listing = module.emission.listing;
            linked.emission_ranges = linkListing(JSON.parse(fs.readFileSync(
              path.join(directory, module.emission.listing))), safeRows, module.emission.verified_sections);
          }
          verifyLinkedRanges(linked.ranges, readObject(object), elf, binary);
          fs.writeFileSync(linkedFile, JSON.stringify(linked) + '\n');
          listings.push(artifact(linkedFile));
        }
        const metricFile = `${elf}.metrics.json`;
        const metrics = JSON.parse(run(tool('badge-elf-info'), ['--metrics', elf], `${label}-metrics`));
        fs.writeFileSync(metricFile, JSON.stringify(metrics, null, 2) + '\n');
        const allocated = metrics.sections.filter(s => s.flags & 2);
        const sectionBytes = sum(allocated.filter(s => s.type !== 8).map(s => s.size));
        const loadedBytes = fs.statSync(binary).size;
        assert(loadedBytes >= sectionBytes, 'linked section bytes exceed loaded extent');
        routes.push({ assembler, elf: path.relative(directory, elf), binary: artifact(binary),
          bytes: loadedBytes, map: path.relative(directory, map), metrics: artifact(metricFile),
          listings,
          section_bytes: sectionBytes, extent_gaps: loadedBytes - sectionBytes,
          static_ram_bytes: sum(allocated.filter(s => s.address >= 0x20000000 &&
            s.address < 0x20082000).map(s => s.size)) });
        binaries.push(fs.readFileSync(binary));
      }
      assert(binaries[0].equals(binaries[1]), `linked bytes differ: ${image.name}/${image.role}/${linker}`);
      row.linkers[linker] = { equal: true, routes };
    }
    // Linker differences are a separate observation, never an assembler verdict.
    row.linkers_equal = fs.readFileSync(path.join(directory, row.linkers.lld.routes[0].binary))
      .equals(fs.readFileSync(path.join(directory, row.linkers.owned.routes[0].binary)));
    assert(fs.readFileSync(path.join(directory, row.production_binary)).equals(
      fs.readFileSync(path.join(directory, row.linkers[row.production_linker].routes[0].binary))),
    'assembly replay differs from production firmware');
    result.images.push(row);
  }
  result.artifacts = [...new Map(result.artifacts.map(a => [a.file, a])).values()];
  for (const item of result.tools) assert.equal(hash(item.file), item.sha256, 'assembler tool drift');
  return result;
}

export function verifyCrossover(directory, result) {
  assert.equal(result.schema, 1);
  assert.equal(result.status, 'complete');
  assert(result.modules.length && result.images.length, 'empty crossover');
  assert.equal(new Set(result.modules.map(row => `${row.module}/${row.source_role}`)).size,
    result.modules.length, 'duplicate crossover module');
  for (const item of result.artifacts) {
    const file = path.resolve(directory, item.file);
    assert(file.startsWith(directory + path.sep), `external artifact: ${item.file}`);
    assert.equal(fs.statSync(file).size, item.bytes, item.file);
    assert.equal(hash(file), item.sha256, item.file);
  }
  for (const row of result.modules) {
    const comparison = compareObjects(...['owned', 'llvm'].map(role =>
      readObject(path.join(directory, row.objects[role]))));
    assert.deepEqual(comparison, row.comparison, 'assembler object comparison tampered');
    assert(comparison.allocated_equal && comparison.relocations_equal && comparison.public_symbols_equal);
    if (row.listing) {
      assert.deepEqual(verifyListing(JSON.parse(fs.readFileSync(path.join(directory, row.listing))),
        path.join(directory, row.objects.owned), path.join(directory, row.assembly)), row.listing_bytes);
      assert.equal(hash(path.join(directory, row.plain_object)), hash(path.join(directory, row.objects.owned)));
    }
    if (row.emission) {
      const emission = row.emission;
      assert.deepEqual(verifyListing(JSON.parse(fs.readFileSync(path.join(directory, emission.listing))),
        path.join(directory, emission.object), path.join(directory, emission.assembly),
        path.join(directory, emission.ir)), emission.listing_bytes);
      assert.deepEqual(compareObjects(readObject(path.join(directory, emission.object)),
        readObject(path.join(directory, row.objects.owned))), emission.comparison);
      assert.deepEqual(verifiedEmissionSections(readObject(path.join(directory, emission.object)),
        readObject(path.join(directory, row.objects.owned))), emission.verified_sections);
    }
  }
  for (const image of result.images) {
    for (const control of Object.values(image.linkers)) {
      assert.deepEqual(control.routes.map(route => route.assembler), ['owned', 'llvm']);
      const data = control.routes.map(route => {
        const bytes = fs.readFileSync(path.join(directory, route.binary));
        assert.equal(bytes.length, route.bytes);
        const metrics = JSON.parse(fs.readFileSync(path.join(directory, route.metrics), 'utf8'));
        const allocated = metrics.sections.filter(s => s.flags & 2);
        assert.equal(sum(allocated.filter(s => s.type !== 8).map(s => s.size)), route.section_bytes);
        assert.equal(route.bytes - route.section_bytes, route.extent_gaps);
        assert.equal(sum(allocated.filter(s => s.address >= 0x20000000 &&
          s.address < 0x20082000).map(s => s.size)), route.static_ram_bytes);
        for (const file of route.listings ?? []) {
          const linked = JSON.parse(fs.readFileSync(path.join(directory, file)));
          const module = result.modules.find(m => m.module === linked.module && m.source_role === image.source_role);
          assert(module && linked.listing === module.listing);
          const object = path.join(directory, module.objects.owned);
          // Map paths retain capture-time absolute names; use the archived object suffix offline.
          const map = fs.readFileSync(path.join(directory, route.map), 'utf8');
          const merged = new Set(readObject(object).sections.filter(s => s.flags & 16).map(s => s.name));
          const rows = placements(map, object, true).filter(row => !merged.has(row.section));
          assert.deepEqual(linked.ranges, linkListing(JSON.parse(fs.readFileSync(
            path.join(directory, module.listing))), rows), 'linked listing placement mismatch');
          verifyLinkedRanges(linked.ranges, readObject(object), path.join(directory, route.elf),
            path.join(directory, route.binary));
          if (module.emission) {
            assert.equal(linked.emission_listing, module.emission.listing);
            assert.deepEqual(linked.emission_ranges, linkListing(JSON.parse(fs.readFileSync(
              path.join(directory, module.emission.listing))), rows, module.emission.verified_sections),
            'linked IR provenance mismatch');
          }
        }
        return bytes;
      });
      assert(control.equal && data[0].equals(data[1]), 'assembler linked comparison mismatch');
    }
    assert.equal(fs.readFileSync(path.join(directory, image.linkers.lld.routes[0].binary))
      .equals(fs.readFileSync(path.join(directory, image.linkers.owned.routes[0].binary))),
    image.linkers_equal, 'linker comparison mismatch');
    assert(fs.readFileSync(path.join(directory, image.production_binary)).equals(
      fs.readFileSync(path.join(directory, image.linkers[image.production_linker].routes[0].binary))),
    'production binary comparison mismatch');
  }
}
