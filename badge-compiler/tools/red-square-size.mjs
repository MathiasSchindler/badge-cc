import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function query(command, args) {
  const result = spawnSync(command, args, { encoding: 'utf8', timeout: 30000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  return result.stdout.trim();
}
function elf(file) {
  const data = fs.readFileSync(file);
  assert.equal(data.readUInt32LE(0), 0x464c457f);
  assert.equal(data[4], 1);
  assert.equal(data[5], 1);
  const string = (start, offset) => data.toString('utf8', start + offset, data.indexOf(0, start + offset));
  const start = data.readUInt32LE(32), stride = data.readUInt16LE(46), count = data.readUInt16LE(48);
  const names = data.readUInt32LE(start + data.readUInt16LE(50) * stride + 16);
  const sections = Array.from({ length: count }, (_, index) => {
    const at = start + index * stride;
    return { name: string(names, data.readUInt32LE(at)), type: data.readUInt32LE(at + 4),
      flags: data.readUInt32LE(at + 8), address: data.readUInt32LE(at + 12),
      offset: data.readUInt32LE(at + 16), bytes: data.readUInt32LE(at + 20),
      link: data.readUInt32LE(at + 24), entry_bytes: data.readUInt32LE(at + 36) };
  });
  const symbols = [];
  for (const table of sections.filter(section => section.type === 2)) {
    const strings = sections[table.link].offset;
    assert.equal(table.entry_bytes, 16);
    for (let at = table.offset; at < table.offset + table.bytes; at += 16) {
      const bytes = data.readUInt32LE(at + 8), section = data.readUInt16LE(at + 14);
      if (bytes && section < sections.length && (sections[section].flags & 2))
        symbols.push({ name: string(strings, data.readUInt32LE(at)),
          address: data.readUInt32LE(at + 4) & 0xfffffffe, bytes, section: sections[section].name });
    }
  }
  return { entry: data.readUInt32LE(24), sections: sections.filter(section => section.flags & 2), symbols };
}
const output = process.argv[2];
assert(output, 'Usage: node badge-compiler/tools/red-square-size.mjs OUTPUT.json');
const report = { schema: 1, square: { width: 64, height: 64, x: 128, y: 88, rgb565: '0xf800',
  background: '0x0000' }, images: [] };
for (const compiler of ['llvm', 'llvm-lto', 'owned']) {
  for (const variant of ['red_square', 'red_square_shared']) {
    const base = `build/red-square-${compiler}/universe26`;
    const binary = `${base}/firmware/${variant}.bin`, image = `${base}/firmware/${variant}.elf`;
    const uf2 = `${base}/uf2/${variant}.uf2`;
    const parsed = elf(path.join(root, image));
    const rawBytes = fs.statSync(path.join(root, binary)).size;
    const flash = parsed.sections.filter(section => section.type !== 8 && section.address >= 0x10000000 &&
      section.address < 0x11000000);
    assert.equal(flash.reduce((sum, section) => sum + section.bytes, 0), rawBytes);
    const uf2Data = fs.readFileSync(path.join(root, uf2));
    assert.equal(uf2Data.length, Math.ceil(rawBytes / 256) * 512);
    const reconstructed = Buffer.alloc(Math.ceil(rawBytes / 256) * 256);
    for (let block = 0; block < uf2Data.length / 512; ++block) {
      const at = block * 512;
      assert.equal(uf2Data.readUInt32LE(at), 0x0a324655);
      assert.equal(uf2Data.readUInt32LE(at + 4), 0x9e5d5157);
      assert.equal(uf2Data.readUInt32LE(at + 8), 0x2000);
      assert.equal(uf2Data.readUInt32LE(at + 12), 0x10000000 + block * 256);
      assert.equal(uf2Data.readUInt32LE(at + 16), 256);
      assert.equal(uf2Data.readUInt32LE(at + 20), block);
      assert.equal(uf2Data.readUInt32LE(at + 24), uf2Data.length / 512);
      assert.equal(uf2Data.readUInt32LE(at + 28), 0xe48bff59);
      assert.equal(uf2Data.readUInt32LE(at + 508), 0x0ab16f30);
      uf2Data.copy(reconstructed, block * 256, at + 32, at + 288);
    }
    assert.deepEqual(reconstructed.subarray(0, rawBytes), fs.readFileSync(path.join(root, binary)));
    const bytes = name => {
      const symbol = parsed.symbols.find(symbol => symbol.name === name);
      assert(symbol, `missing ${name}: ${image}`);
      return symbol.bytes;
    };
    let categories;
    if (variant === 'red_square') {
      categories = { vectors: 64, picobin: 40, startup_and_fault_halt: 24,
        gpio_and_application_sequencing: bytes('bare_main'),
        command_and_byte_transport: bytes('command') + bytes('commands') + bytes('write_byte'),
        pixel_fill: bytes('fill'), reset_sleep_delays: bytes('delay_cycles'),
        lcd_configuration: bytes('setup') + bytes('full_window') + bytes('square_window') };
      categories.alignment = rawBytes - Object.values(categories).reduce((sum, value) => sum + value, 0);
      assert(categories.alignment >= 0 && categories.alignment < 16);
    }
    report.images.push({ compiler, variant, raw_bytes: rawBytes, uf2_bytes: uf2Data.length,
      uf2_payload_pages: reconstructed.length / 256, minimum_4k_erase_sectors: Math.ceil(rawBytes / 4096),
      elf_bytes: fs.statSync(path.join(root, image)).size, categories,
      static_ram_bytes: parsed.sections.filter(section => section.address >= 0x20000000 &&
        section.address < 0x20082000).reduce((sum, section) => sum + section.bytes, 0),
      sections: parsed.sections, symbols: parsed.symbols.sort((a, b) => b.bytes - a.bytes),
      files: Object.fromEntries([image, binary, uf2, `${base}/firmware/${variant}.map`]
        .map(file => [file, hash(path.join(root, file))])) });
  }
}
report.sources = Object.fromEntries(['src/tools/red_square.c', 'src/arch/rp2350/start_red_square.S',
  'src/arch/rp2350/memmap_red_square.ld', 'src/arch/rp2350/start.c', 'src/arch/rp2350/picobin.S',
  'src/arch/rp2350/memmap_flash.ld', 'src/platform/badger/display.c', 'Makefile',
  'tests/red_square_test.c', 'badge-compiler/tests/red-square.test.mjs',
  'badge-compiler/tools/red-square-size.mjs'].map(file => [file, hash(path.join(root, file))]));
report.owned_tools = Object.fromEntries(['badge-cpp', 'badge-ir', 'badge-opt', 'badge-codegen',
  'badge-asm', 'badge-link', 'elf2bin'].map(name => [name, hash(path.join(root, 'badge-compiler/build', name))]));
report.llvm_version = query(process.env.LLVM_CLANG ?? 'clang', ['--version']);
report.owned_defaults = {
  expression_lowering: query(path.join(root, 'badge-compiler/build/badge-ir'), ['--default-expression-lowering']),
  optimizer: query(path.join(root, 'badge-compiler/build/badge-opt'), ['--default-passes']),
  scalar_selection: query(path.join(root, 'badge-compiler/build/badge-codegen'), ['--default-scalar-selection']),
  frame_addressing: query(path.join(root, 'badge-compiler/build/badge-codegen'), ['--default-frame-addressing']),
  register_residency: query(path.join(root, 'badge-compiler/build/badge-codegen'), ['--default-register-residency']),
  memory_selection: query(path.join(root, 'badge-compiler/build/badge-codegen'), ['--default-memory-selection']),
};
fs.writeFileSync(output, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
for (const image of report.images)
  console.log(`${image.compiler}/${image.variant}: ${image.raw_bytes} loaded bytes, ${image.uf2_bytes} UF2 bytes`);
