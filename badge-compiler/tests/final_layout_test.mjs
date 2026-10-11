import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const oracle = process.env.BADGE_TEST_MODE !== 'owned';
const clang = process.env.LLVM_CLANG || 'clang';
const lld = process.env.LLVM_LLD || 'ld.lld';
function fixture(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/final-layout-test-'));
  t.after(() => {
    for (const name of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, name));
    fs.rmdirSync(dir);
  });
  return name => path.join(dir, name);
}
function invoke(command, args, success = true) {
  const r = spawnSync(command, args, { encoding: 'utf8', timeout: 30000 });
  assert.ifError(r.error);
  if (success) assert.equal(r.status, 0, r.stderr);
  else assert.notEqual(r.status, 0, 'expected an explicit diagnostic');
  return r;
}
const tool = name => path.join(root, 'build', name);
const script = path.join(root, 'tests/fixtures/link_oracle_fixture.ld');
function assemble(file, source) {
  fs.writeFileSync(file('input.s'), `.syntax unified\n.cpu cortex-m33\n.thumb\n${source}`);
  invoke(tool('badge-asm'), [file('input.s'), '-o', file('owned.o')]);
}
function link(file, object = 'owned.o', prefix = 'owned') {
  invoke(tool('badge-link'), ['-T', script, '--no-gc-sections', '-o', file(`${prefix}.elf`), file(object)]);
  invoke(tool('elf2bin'), ['-O', 'binary', file(`${prefix}.elf`), file(`${prefix}.bin`)]);
  return fs.readFileSync(file(`${prefix}.bin`));
}
function llvm(file) {
  invoke(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
    '-c', file('input.s'), '-o', file('llvm.o')]);
  invoke(lld, ['-T', script, '--no-gc-sections', '-o', file('llvm.elf'), file('llvm.o')]);
  invoke(tool('elf2bin'), ['-O', 'binary', file('llvm.elf'), file('llvm.bin')]);
  const owned = fs.readFileSync(file('owned.bin')), reference = fs.readFileSync(file('llvm.bin'));
  const first = owned.findIndex((byte, i) => byte !== reference[i]);
  assert.equal(first, -1, `first LLVM byte mismatch at ${first} (10-byte matrix row ${Math.floor(first / 10)}): owned ${owned[first]}, LLVM ${reference[first]}`);
  assert.deepEqual(link(file, 'llvm.o', 'mixed'), fs.readFileSync(file('llvm.bin')));
}
function immediate(bytes, offset) {
  const h = bytes.readUInt16LE(offset), low = bytes.readUInt16LE(offset + 2);
  return ((h & 15) << 12) | (((h >> 10) & 1) << 11) |
    (((low >> 12) & 7) << 8) | (low & 255);
}
const entry = '.text\n.global entry\n.type entry,%function\n.thumb_func\nentry:\n';

test('MOVW/MOVT REL matrix preserves signed addends, odd data and Thumb/weak/absolute symbols', t => {
  const file = fixture(t);
  const cases = [];
  let source = entry;
  for (let reg = 0; reg <= 12; reg++)
    for (const symbol of ['data', 'callee', 'missing', 'absolute'])
      for (const addend of [-32768, -32767, -4096, -2, -1, 0, 1, 2, 4095, 32766, 32767]) {
        const expression = `${symbol}${addend >= 0 ? '+' : ''}${addend}`;
        source += `nop\nmovw r${reg}, #:lower16:(${expression})\nmovt r${reg}, #:upper16:(${expression})\n`;
        cases.push({ reg, symbol, addend });
      }
  source += 'bx lr\n.size entry,.-entry\n.global callee\n.type callee,%function\n.thumb_func\ncallee:bx lr\n';
  source += '.weak missing\n.equ absolute,0xfedc1235\n.section .rodata,"a",%progbits\n.byte 0\n.global data\ndata:.byte 1\n';
  assemble(file, source);
  const bytes = link(file);
  const info = JSON.parse(invoke(tool('badge-elf-info'), ['--metrics', file('owned.elf')]).stdout);
  const symbols = new Map(info.symbols.map(s => [s.name, s.value]));
  let offset = 0;
  for (const { reg, symbol, addend } of cases) {
    assert.equal(bytes.readUInt16LE(offset), 0xbf00);
    const base = symbol === 'missing' ? 0 : symbols.get(symbol);
    assert.notEqual(base, undefined, symbol);
    const full = (base + addend) >>> 0;
    assert.equal(immediate(bytes, offset + 2), full & 65535);
    assert.equal(immediate(bytes, offset + 6), full >>> 16);
    assert.equal((bytes.readUInt16LE(offset + 4) >> 8) & 15, reg);
    offset += 10;
  }
  assert.equal(cases.length, 572);
  if (oracle) llvm(file);
});

test('final branch widths honor signed reach and widening cascades, including padding', t => {
  const file = fixture(t);
  const cases = [
    ['b target\n.space 2048\ntarget:nop\n', 2],
    ['b target\n.space 2050\ntarget:nop\n', 4],
    ['beq target\n.space 256\ntarget:nop\n', 2],
    ['beq target\n.space 258\ntarget:nop\n', 4],
    ['target:.space 2044\nb target\n', 2, 2044],
    ['target:.space 2046\nb target\n', 4, 2046],
    ['target:.space 252\nbeq target\n', 2, 252],
    ['target:.space 254\nbeq target\n', 4, 254],
    ['b far\n.space 2042\nb entry\n.space 6\nfar:nop\n', 4],
    ['b far\n.p2align 3\n.space 2042\nb entry\n.space 6\nfar:nop\n', 4],
  ];
  for (const [body, width, at = 0] of cases) {
    assemble(file, entry + body);
    const bytes = link(file);
    assert.equal((bytes.readUInt16LE(at) & 0xf800) === 0xf000 ? 4 : 2, width);
    if (oracle) llvm(file);
  }
  assemble(file, entry + cases[8][0]);
  const bytes = link(file);
  assert.equal(bytes.readUInt16LE(2046) & 0xf800, 0xf000,
    'first widening must also widen the formerly reachable backward branch');
  fs.writeFileSync(file('preserved.o'), 'preserved');
  const budget = invoke(tool('badge-asm-layout-budget'),
    [file('input.s'), '-o', file('preserved.o')], false);
  assert.match(budget.stderr, /layout relaxation did not converge in 2 passes/);
  assert.equal(fs.readFileSync(file('preserved.o'), 'utf8'), 'preserved');
});

test('literal PC alignment/reach and explicit narrow constraints use final bytes', t => {
  const file = fixture(t);
  for (const [width, space, prefix] of [['n', 1022, ''], ['w', 4095, ''],
    ['w', 4093, 'nop\n'], ['', 1026, '']]) {
    assemble(file, entry + `${prefix}ldr${width ? '.' + width : ''} r0,target\n.space ${space}\ntarget:.word 0x12345678\n`);
    link(file);
    if (oracle) llvm(file);
  }
  for (const body of [
    'ldr.n r0,target\n.space 1026\ntarget:.word 1\n',
    'ldr.w r0,target\n.space 4096\ntarget:.word 1\n',
    'b.n target\n.space 2050\ntarget:nop\n',
    'beq.n target\n.space 258\ntarget:nop\n',
    'movw r0, #:upper16:target\ntarget:nop\n',
    'movt r0, #:upper16:(target+32768)\ntarget:nop\n',
    'movw r0, #:lower16:(target-32769)\ntarget:nop\n',
  ]) {
    fs.writeFileSync(file('bad.s'), entry + body);
    fs.writeFileSync(file('preserved.o'), 'preserved');
    const r = invoke(tool('badge-asm'), [file('bad.s'), '-o', file('preserved.o')], false);
    assert.match(r.stderr, /badge-asm:/);
    assert.equal(fs.readFileSync(file('preserved.o'), 'utf8'), 'preserved');
  }
});

test('C07 policy is deterministic, accepts odd offsets and preserves large-offset guarded literal fallback', t => {
  const file = fixture(t);
  const source = `target triple = "thumbv8m.main-unknown-none-eabi"
@data = global [65536 x i8] zeroinitializer
define ptr @work(i1 %test) {
entry:
br i1 %test, label %yes, label %no
yes:
ret ptr @data
no:
ret ptr getelementptr ([65536 x i8], ptr @data, i32 0, i32 32768)
}
define ptr @odd() {
entry:
ret ptr getelementptr ([65536 x i8], ptr @data, i32 0, i32 1)
}
`;
  fs.writeFileSync(file('input.ll'), source);
  for (const residency of ['stack', 'local', 'global', 'global-flex', 'global-compact']) {
    const args = ['--scalar-selection=thumb-relax', `--register-residency=${residency}`,
      file('input.ll'), '-o', file('out.s')];
    invoke(tool('badge-codegen'), args);
    const expected = fs.readFileSync(file('out.s'), 'utf8');
    assert.match(expected, /movw r\d+, #:lower16:\(data\+0\)/);
    assert.match(expected, /@ final-layout: addresses 1 literal-fallbacks 1 relax-branches/);
    assert.match(expected, /movw r\d+, #:lower16:\(data\+1\)/);
    assert.match(expected, /ldr\.w r\d+, \.Lcg_lit\d+\nb\.w \.Lcg_after\d+\n\.p2align 2/);
    assert.match(expected, /b(?:eq|ne)? \.Lcg_/);
    invoke(tool('badge-codegen-sanitize'), args);
    assert.equal(fs.readFileSync(file('out.s'), 'utf8'), expected);
    invoke(tool('badge-asm'), [file('out.s'), '-o', file('out.o')]);
  }
  fs.writeFileSync(file('out.s'), 'preserved');
  invoke(tool('badge-codegen'), ['--scalar-selection=thumb-relax', '--scalar-selection=thumb-relax',
    file('input.ll'), '-o', file('out.s')], false);
  assert.equal(fs.readFileSync(file('out.s'), 'utf8'), 'preserved');
});
