import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { compareObjects, readObject } from '../tools/assembler-crossover.mjs';
import { verifyListing } from '../tools/assembler-listing.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const tool = name => path.join(root, 'build', name);
const clang = process.env.LLVM_CLANG ?? 'clang';
const lld = process.env.LLVM_LLD ?? 'ld.lld';
const qemu = process.env.QEMU_ARM ?? 'qemu-system-arm';
const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
const header = '.syntax unified\n.arch armv8-m.main\n.thumb\n';
const entry = '.global entry\n.type entry,%function\n.thumb_func\nentry:\n';
function invoke(command, args, success = true) {
  const result = spawnSync(command, args, { encoding: 'utf8', timeout: 30000 });
  assert.ifError(result.error);
  if (success) assert.equal(result.status, 0, result.stderr);
  else assert.notEqual(result.status, 0, 'expected explicit failure');
  return result;
}
function fixture(run) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'ba04-'));
  try { run(directory); } finally { fs.rmSync(directory, { recursive: true }); }
}
function assemble(dir, source, oracle = false) {
  const input = path.join(dir, 'input.s'), output = path.join(dir, 'owned.o');
  const listing = path.join(dir, 'listing.json');
  fs.writeFileSync(input, header + source);
  invoke(tool('badge-asm'), ['--listing', listing, input, '-o', output]);
  const object = readObject(output), listed = JSON.parse(fs.readFileSync(listing));
  verifyListing(listed, output, input);
  if (oracle) {
    const reference = path.join(dir, 'llvm.o');
    invoke(clang, [...target, '-c', input, '-o', reference]);
    const comparison = compareObjects(object, readObject(reference));
    assert(comparison.allocated_equal, JSON.stringify(comparison.normalized));
    assert(comparison.relocations_equal, JSON.stringify(comparison.normalized));
    assert(comparison.public_symbols_equal, JSON.stringify(comparison.normalized));
  }
  return { object, listing: listed, output, input };
}
const text = object => object.sections.find(section => section.name === '.text').data;
function rejected(dir, source, message) {
  const input = path.join(dir, 'bad.s'), output = path.join(dir, 'preserved.o'),
    listing = path.join(dir, 'preserved.json');
  fs.writeFileSync(input, header + source);
  fs.writeFileSync(output, 'preserved'); fs.writeFileSync(listing, 'preserved');
  const result = invoke(tool('badge-asm'), ['--listing', listing, input, '-o', output], false);
  assert.match(result.stderr, /badge-asm:/);
  if (message) assert.match(result.stderr, message);
  assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
  assert.equal(fs.readFileSync(listing, 'utf8'), 'preserved');
}
function decodeLiteral(bytes, offset) {
  const a = bytes.readUInt16LE(offset);
  if ((a & 0xf800) === 0x4800) return { width: 2, register: a >> 8 & 7,
    address: ((offset + 4) & ~3) + (a & 255) * 4 };
  const b = bytes.readUInt16LE(offset + 2);
  assert([0xf8df, 0xf85f].includes(a));
  return { width: 4, register: b >> 12, address: ((offset + 4) & ~3) +
    (a === 0xf8df ? 1 : -1) * (b & 4095) };
}

test('numeric labels bind nearest forward/backward definitions across repeats, expressions and cached aliases', () => fixture(dir => {
  const source = `1: nop
.equ previous,1b
.equ next,1f
b 1f
1: nop
b 1b
b 1f
.word 1b-previous
1: nop
b 1b
.word 1b-next,1b-previous
8: 4294967295: nop
b 8b
b 4294967295b
`;
  const { object } = assemble(dir, source, true);
  const bytes = text(object);
  assert.equal(bytes.readUInt16LE(2), 0xe7ff);
  assert.equal(bytes.readUInt16LE(6), 0xe7fd);
  assert.equal(bytes.readUInt16LE(8), 0xe001);
  assert.equal(bytes.readUInt32LE(10), 4);
  assert.equal(bytes.readUInt16LE(16), 0xe7fd);
  assert.equal(bytes.readUInt32LE(18), 10);
  assert.equal(bytes.readUInt32LE(22), 14);
}));

test('numeric references in cross-section data and MOVW/MOVT keep ordinary relocations', () => fixture(dir => {
  assemble(dir, `1: nop
movw r0,#:lower16:1b
movt r0,#:upper16:1f
b 1f
1: bx lr
.section .data,"aw",%progbits
.word 1b,2f+4
2: .word 1b
`, true);
}));

test('numeric and named labels can prefix symbol assignments without confusing literal equals operands', () => fixture(dir => {
  assemble(dir, '1: named: count = 7\n.word count\nldr r0,=0x12345678\nbx lr\n.pool\n', true);
}));

test('numeric missing directions, malformed labels and overflow preserve object and listing', () => fixture(dir => {
  for (const source of ['b 1f\n', 'b 1b\n1:nop\n', '1:nop\nb 1f\n',
    'b 4294967296f\n', '4294967296:nop\n', '1x:nop\n', '1=2\n', '.global 1\n',
    '.type 1,%function\n', '.equ 1,2\n'])
    rejected(dir, source, /numeric/);
}));

test('arch accepts only the RP2350 Mainline spelling without changing CPU/DSP attributes', () => fixture(dir => {
  const a = assemble(dir, '.cpu cortex-m33\n.arch armv8-m.main\nnop\n', true);
  const attribute = a.object.sections.find(section => section.name === '.ARM.attributes');
  assert(attribute.data.includes(Buffer.from('cortex-m33')));
  for (const spelling of ['armv6-m', 'armv7-m', 'armv8-m.base', 'armv8.1-m.main',
    'armv8-m.main+dsp', 'armv8-a', 'cortex-m33', '', 'armv8-m.main,armv8-m.main'])
    rejected(dir, `.arch ${spelling}\n`, /only .arch armv8-m.main/);
}));

test('explicit pools deduplicate 32-bit constants and affine aliases in first-use order', () => fixture(dir => {
  const { object, listing } = assemble(dir, `ldr r0, =0x12345678
ldr r1, =(0x12340000+0x5678)
ldr r2, =-1
ldr r3, =0xffffffff
.equ alias,external+4
ldr.w r8, =alias
ldr lr, =external+4
bx lr
.ltorg
.section .data,"aw",%progbits
.global external
external:.word 0
`);
  const bytes = text(object), loads = listing.ranges.filter(r => r.kind === 'instruction' && r.operation.startsWith('ldr'));
  const addresses = loads.map(r => decodeLiteral(bytes, r.offset).address);
  assert.equal(addresses[0], addresses[1]);
  assert.equal(addresses[2], addresses[3]);
  assert.equal(addresses[4], addresses[5]);
  assert.equal(bytes.readUInt32LE(addresses[0]), 0x12345678);
  assert.equal(bytes.readUInt32LE(addresses[2]), 0xffffffff);
  assert.equal(bytes.readUInt32LE(addresses[4]), 4);
  assert.equal(object.relocations.length, 1);
  assert.equal(object.relocations[0].type, 2);
  assert.equal(object.relocations[0].offset, addresses[4]);
  assert.equal(object.relocations[0].symbol.name, 'external');
  const pool = listing.ranges.filter(r => r.operation === '.ltorg' && r.kind === 'data');
  assert.equal(pool.reduce((n, r) => n + r.size, 0), 12);
}));

test('pool relocations and repeated numeric branches match LLVM and both linker paths', () => fixture(dir => {
  const source = fs.readFileSync(path.join(root, 'tests/fixtures/ergonomics_oracle_fixture.S'), 'utf8');
  assemble(dir, source, true);
  const script = path.join(root, 'tests/fixtures/link_oracle_fixture.ld');
  const binaries = [];
  for (const producer of ['owned', 'llvm']) for (const linker of ['owned', 'llvm']) {
    const elf = path.join(dir, `${producer}-${linker}.elf`), binary = path.join(dir, `${producer}-${linker}.bin`);
    invoke(linker === 'owned' ? tool('badge-link') : lld,
      ['-T', script, '--no-gc-sections', '-o', elf, path.join(dir, `${producer}.o`)]);
    invoke(tool('elf2bin'), ['-O', 'binary', elf, binary]);
    binaries.push(fs.readFileSync(binary));
  }
  assert(binaries.every(binary => binary.equals(binaries[0])));
}));

test('relative pool words retain the load-site subtraction rather than the pool address', () => fixture(dir => {
  const { object, input } = assemble(dir, entry + `1: ldr r0,=external-1b
ldr r1,=external-1b
bx lr
.pool
.section .data,"aw",%progbits
.global external
external:.word 0
`);
  invoke(clang, [...target, '-c', input, '-o', path.join(dir, 'llvm.o')]);
  assert.equal(object.relocations.length, 1);
  assert.equal(object.relocations[0].type, 3);
  const script = path.join(root, 'tests/fixtures/link_oracle_fixture.ld');
  for (const producer of ['owned', 'llvm']) for (const linker of ['owned', 'llvm']) {
    const elf = path.join(dir, `${producer}-${linker}.elf`), binary = path.join(dir, `${producer}-${linker}.bin`);
    invoke(linker === 'owned' ? tool('badge-link') : lld,
      ['-T', script, '--no-gc-sections', '-o', elf, path.join(dir, `${producer}.o`)]);
    invoke(tool('elf2bin'), ['-O', 'binary', elf, binary]);
    const bytes = fs.readFileSync(binary), address = decodeLiteral(bytes, 0).address;
    if (producer === 'owned') assert.equal(address, decodeLiteral(bytes, 2).address);
    assert.equal(bytes.readUInt32LE(address), 0x10000000);
    assert.equal(bytes.readUInt32LE(decodeLiteral(bytes, 2).address), 0x10000000);
  }
}));

test('pools stay per-section, reset after each explicit flush, and retain use-site dot and numeric expressions', () => fixture(dir => {
  const { object, listing } = assemble(dir, `1: ldr r0,=.
ldr r1,=.
ldr r2,=1b
b 2f
.pool
2: ldr r3,=1b
bx lr
.ltorg
.section .text.other,"ax",%progbits
1: ldr r4,=.
bx lr
.pool
`);
  const loads = listing.ranges.filter(r => r.operation.startsWith('ldr'));
  const bytes = text(object), address = r => decodeLiteral(bytes, r.offset).address;
  assert.notEqual(address(loads[0]), address(loads[2]));
  assert.notEqual(address(loads[0]), address(loads[1]));
  assert.notEqual(address(loads[0]), address(loads[3]));
  const textRelocations = object.relocations.filter(r => object.sections[r.section].name === '.text');
  assert.deepEqual(textRelocations.map(r => (r.symbol.value + bytes.readUInt32LE(r.offset)) >>> 0), [0, 2, 0, 0]);
  assert.equal(object.relocations.filter(r => object.sections[r.section].name === '.text.other').length, 1);
}));

test('pending pools survive section switches until explicitly flushed in the originating section', () => fixture(dir => {
  const { object } = assemble(dir, `ldr r0,=0x12345678
bx lr
.section .text.other,"ax",%progbits
ldr r1,=0x89abcdef
bx lr
.pool
.text
.ltorg
`, true);
  for (const name of ['.text', '.text.other']) {
    const bytes = object.sections.find(section => section.name === name).data;
    assert.equal(bytes.length, 8);
    assert.equal(decodeLiteral(bytes, 0).address, 4);
  }
}));

test('no pool is silently inserted, branched over, placed across IT or emitted into executable fallthrough', () => fixture(dir => {
  for (const source of [
    'ldr r0,=42\nbx lr\n',
    'ldr r0,=42\n.ltorg\n',
    'ldr r0,=42\nbeq done\n.pool\ndone:bx lr\n',
    'ldr r0,=42\nbl callee\n.pool\ncallee:bx lr\n',
    'ldr r0,=42\nbx lr\nlabel:\n.pool\n',
    'ldr r0,=42\npop {r0,lr}\n.pool\n',
    'ldr r0,=42\nit eq\nbxeq lr\n.pool\n',
    'it eq\n.pool\nmoveq r0,r1\n',
    'ldr r0,=42\nbx lr\n.pool 1\n',
    'ldr r0,=42\nbx lr\n.section .data,"aw",%progbits\n.pool\n',
  ]) rejected(dir, source, /literal pool|pending literal/);
  for (const instruction of ['b done', 'bx r0', 'pop {r0,pc}', 'ldm r0!,{r1,pc}'])
    assemble(dir, `ldr r2,=42\n${instruction}\n.pool\ndone:bx lr\n`);
  assemble(dir, '.ltorg\n.pool\nnop\n');
}));

test('bad literal operands, destinations, values and incompatible width preserve output', () => fixture(dir => {
  for (const source of ['ldr sp,=42\nbx lr\n.pool\n', 'ldr pc,=42\nbx lr\n.pool\n',
    'ldr r0,=\nbx lr\n.pool\n', 'ldr r0,=0x100000000\nbx lr\n.pool\n',
    'ldr r0,=-2147483649\nbx lr\n.pool\n', 'ldr.n r8,=42\nbx lr\n.pool\n',
    'ldr r0,=external-other\nbx lr\n.pool\n', 'ldrb r0,=42\n',
    'str r0,=42\n', 'ldr r0,=42,r1\n'])
    rejected(dir, source);
}));

test('narrow pool reach, aligned PC and explicit width are checked at exactly 1020 and 1024 bytes', () => fixture(dir => {
  for (const prefix of ['', 'nop\n']) {
    const { object, listing } = assemble(dir, `${prefix}ldr.n r0,=0x12345678\n.space ${prefix ? 1018 : 1020}\nbx lr\n.pool\n`, true);
    const offset = prefix ? 2 : 0, bytes = text(object), load = decodeLiteral(bytes, offset);
    assert.equal(load.width, 2);
    assert.equal(load.address - ((offset + 4) & ~3), 1020);
    assert(listing.decisions.some(d => d.line && d.displacement === 1020 && d.reach_max === 1020));
  }
  rejected(dir, 'ldr.n r0,=0x12345678\n.space 1022\nbx lr\n.pool\n', /narrow/);
  const { object } = assemble(dir, 'ldr r0,=0x12345678\n.space 1022\nbx lr\n.pool\n', true);
  assert.equal(decodeLiteral(text(object), 0).width, 4);
}));

test('wide pool reach honors aligned 4092/4096 boundary and direct literals honor signed +/-4095', () => fixture(dir => {
  const positive = assemble(dir, 'ldr.w r8,=0x12345678\n.space 4090\nbx lr\n.pool\n', true);
  assert.equal(decodeLiteral(text(positive.object), 0).address, 4096);
  rejected(dir, 'ldr.w r8,=0x12345678\n.space 4092\nbx lr\n.pool\n', /4095/);
  for (const sign of [1, -1]) {
    const source = sign === 1 ? 'ldr.w r8,1f\n.space 4095\n1:.word 0x12345678\n' :
      '.byte 0\n1:.word 0x12345678\n.space 4087\nldr.w r8,1b\n';
    const { object, listing } = assemble(dir, source, true);
    const load = listing.ranges.find(r => r.operation === 'ldr.w');
    assert.equal(listing.decisions.find(d => d.line === load.line).displacement, sign * 4095);
    assert.equal(decodeLiteral(text(object), load.offset).address, sign === 1 ? 4099 : 1);
  }
  rejected(dir, 'ldr.w r8,1f\n.space 4096\n1:.word 42\n', /4095/);
  rejected(dir, '1:.word 42\n.space 4092\nldr.w r8,1b\n', /4095/);
}));

test('pool alignment and branch relaxation converge without changing explicit control flow', () => fixture(dir => {
  const { object, listing } = assemble(dir, `b 1f
ldr r0,=0x12345678
ldr r8,=0x89abcdef
.space 2040
b 2f
.pool
2: nop
1: bx lr
`, true);
  const bytes = text(object), branch = listing.ranges.find(r => r.operation === 'b');
  assert.equal(branch.width, 4);
  assert.equal(bytes.readUInt16LE(0) & 0xf800, 0xf000);
  const loads = listing.ranges.filter(r => r.operation.startsWith('ldr'));
  assert(loads.every(load => load.width === 4));
  const padding = listing.ranges.filter(r => r.kind === 'alignment' && r.operation === '.pool');
  assert.equal(padding.reduce((n, r) => n + r.size, 0), 2);
  assert(padding.every(r => /^00+$/.test(r.hex)));
}));

test('literal pseudo-loads cover every supported destination, widths and conditional IT slots', () => fixture(dir => {
  let source = '';
  for (const register of [...Array(13).keys(), 14]) {
    const name = register === 14 ? 'lr' : `r${register}`;
    source += `ldr ${name},=0x12345678\nldr.w ${name},=0x12345678\n`;
    if (register < 8) source += `ldr.n ${name},=0x12345678\n`;
  }
  source += 'it eq\nldreq r0,=0x12345678\nbx lr\n.pool\n';
  const { object, listing } = assemble(dir, source, true), bytes = text(object);
  const loads = listing.ranges.filter(r => r.operation.startsWith('ldr'));
  assert.equal(loads.length, 37);
  const addresses = loads.map(r => decodeLiteral(bytes, r.offset).address);
  assert(addresses.every(address => address === addresses[0]));
  assert.equal(bytes.readUInt32LE(addresses[0]), 0x12345678);
}));

test('Cortex-M33 executes both assemblers numeric loops, two pools, constants and symbol loads', () => fixture(dir => {
  const fixture = fs.readFileSync(path.join(root, 'tests/fixtures/ergonomics_runtime.S'), 'utf8');
  for (const small of [false, true]) {
    const source = small ? fixture.replace('ba04_reset:\n',
      'ba04_reset:\n    ldr r5,=42\n    cmp r5,#42\n    bne 9f\n') : fixture;
    const { input } = assemble(dir, source, !small);
    if (small) invoke(clang, [...target, '-c', input, '-o', path.join(dir, 'llvm.o')]);
    const binaries = [];
    for (const role of ['owned', 'llvm']) {
      const elf = path.join(dir, `${role}.elf`), binary = path.join(dir, `${role}.bin`);
      invoke(lld, ['-T', path.join(root, 'tests/fixtures/system_runtime.ld'), '-e', 'ba04_reset',
        '--fatal-warnings', path.join(dir, `${role}.o`), '-o', elf]);
      invoke(tool('elf2bin'), ['-O', 'binary', elf, binary]);
      binaries.push(fs.readFileSync(binary));
      invoke(qemu, ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
        '-semihosting-config', 'enable=on,target=native', '-kernel', elf]);
    }
    assert.equal(binaries[0].equals(binaries[1]), !small);
  }
}));

test('external conditional branches remain rejected until R_ARM_THM_JUMP19 is supported by the owned linker', () => fixture(dir => {
  const source = entry + 'beq external\nbx lr\n';
  rejected(dir, source, /external conditional branch is unsupported/);
  const input = path.join(dir, 'input.s'), object = path.join(dir, 'llvm.o');
  fs.writeFileSync(input, header + source);
  invoke(clang, [...target, '-c', input, '-o', object]);
  assert.equal(readObject(object).relocations[0].type, 51);
  const output = path.join(dir, 'preserved.elf');
  fs.writeFileSync(output, 'preserved');
  const result = invoke(tool('badge-link'), ['-T', path.join(root, 'tests/fixtures/link_oracle_fixture.ld'),
    '-o', output, object], false);
  assert.match(result.stderr, /unsupported relocation/);
  assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
}));
