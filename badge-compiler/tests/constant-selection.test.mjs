import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { decodeElf } from '../tools/boundary-evidence.mjs';
import { parseAssembly } from '../tools/benchmark-c06.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  return result.stdout;
}
const tool = name => path.join(root, 'build', name);
function files(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/constant-selection-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  return name => path.join(directory, name);
}
test('RP2350 DSP extensions match LLVM and unknown packed operands do not select sticky-Q multiply', t => {
  const file = files(t);
  fs.writeFileSync(file('dsp.s'), `.syntax unified
.cpu cortex-m33
.thumb
smuad r2, r1, r0
smlatt r0, r1, r0, r2
sbfx r2, r1, #8, #16
ssat r3, #32, r9, lsl #31
ssat r12, #8, r1, asr #16
uxtah r2, r3, r4, ror #8
uxtah r2, r3, r4, ror #16
uxtah r2, r3, r4, ror #24
adc r1, r2, #0
`);
  run(tool('badge-asm'), [file('dsp.s'), '-o', file('owned.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('dsp.s'), '-o', file('llvm.o')]);
  const text = name => decodeElf(fs.readFileSync(file(name))).sections.find(s => s.name === '.text').data;
  assert.deepEqual(text('owned.o'), text('llvm.o'));

  const dot = fs.readFileSync(path.join(root, '../src/tools/compiler_micro/packed-dot.c'), 'utf8');
  const unknown = dot.replace('uint32_t word)', 'uint32_t word, uint32_t other)')
    .replace('    uint32_t other = word ^ 0x73962dd1u;\n', '');
  for (const [name, source] of [['proved', dot], ['unknown', unknown]]) {
    fs.writeFileSync(file(`${name}.c`), source);
    run(tool('badge-cpp'), ['-I../src/shared', file(`${name}.c`), '-o', file(`${name}.i`)]);
    run(tool('badge-ir'), [file(`${name}.i`), '-o', file('initial.ll')]);
    run(tool('badge-opt'), [file('initial.ll'), '-o', file('input.ll')]);
    run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
      file('input.ll'), '-o', file(`${name}.s`)]);
  }
  assert.match(fs.readFileSync(file('proved.s'), 'utf8'), /^smuad /m);
  assert.doesNotMatch(fs.readFileSync(file('unknown.s'), 'utf8'), /^smuad /m);
});
test('RP2350 byte, halfword, table and carry selectors reject near misses', t => {
  const file = files(t);
  const cases = [
    ['byte-swap', 'pairs >> 16u', 'pairs >> 15u', /^rev\.n /m],
    ['extend-add', 'word >> 8u', 'word >> 7u', /^uxtah /m],
    ['nibble-map', 'return 13u', 'return 16u', /@ nibble-lookup:/],
    ['wide-sum', '+ 33u', '+ 256u', /@ wide-sum:/],
  ];
  for (const [name, from, to, marker] of cases) {
    const source = fs.readFileSync(path.join(root, '../src/tools/compiler_micro', `${name}.c`), 'utf8');
    for (const [accepted, c] of [[true, source], [false, source.replace(from, to)]]) {
      fs.writeFileSync(file('kernel.c'), c);
      run(tool('badge-cpp'), ['-I../src/shared', file('kernel.c'), '-o', file('kernel.i')]);
      run(tool('badge-ir'), [file('kernel.i'), '-o', file('initial.ll')]);
      run(tool('badge-opt'), [file('initial.ll'), '-o', file('input.ll')]);
      run(tool('badge-codegen-sanitize'), [file('input.ll'), '-o', file('kernel.s')]);
      assert.equal(marker.test(fs.readFileSync(file('kernel.s'), 'utf8')), accepted, name);
    }
  }
});
test('unsigned distance diamond preserves extremes and signs without branching', t => {
  const file = files(t);
  fs.writeFileSync(file('input.c'), `
unsigned distance(unsigned a, unsigned b) {
  return a > b ? a - b : b - a;
}
unsigned different(unsigned a, unsigned b) {
  return a > b ? a + b : b - a;
}
`);
  run(tool('badge-ir'), [file('input.c'), '-o', file('initial.ll')]);
  run(tool('badge-opt'), [file('initial.ll'), '-o', file('input.ll')]);
  run(tool('badge-codegen-sanitize'), [file('input.ll'), '-o', file('input.s')]);
  const asm = fs.readFileSync(file('input.s'), 'utf8');
  assert.match(asm.slice(0, asm.indexOf('.size distance,')), /@ unsigned-distance:/);
  assert.doesNotMatch(asm.slice(asm.indexOf('\ndifferent:')), /@ unsigned-distance:/);
  fs.writeFileSync(file('main.c'), `
extern unsigned distance(unsigned, unsigned);
int main(void) {
  const unsigned inputs[] = {0, 1, 2, 128, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
  for (unsigned i = 0; i < 8; ++i)
    for (unsigned j = 0; j < 8; ++j) {
      unsigned a = inputs[i], b = inputs[j];
      if (distance(a, b) != (a > b ? a - b : b - a)) return 1;
    }
  return 0;
}
`);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const arm = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(clang, [...arm, '-Oz', '-ffreestanding', '-c', file('main.c'), '-o', file('main.o')]);
  run(tool('badge-asm'), [file('input.s'), '-o', file('input.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('input.o'), file('main.o'), '-o', file('oracle.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('oracle.elf')]);
});
test('small symmetric clamp preserves both values of sticky Q and retains volatile tails', t => {
  const file = files(t);
  fs.writeFileSync(file('clamp.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@sink = global i32 0, align 4
define i32 @clamp(i32 %x) {
entry:
  %hi = icmp sgt i32 %x, 127
  br i1 %hi, label %upper, label %low
upper:
  ret i32 127
low:
  %lo = icmp slt i32 %x, -128
  br i1 %lo, label %lower, label %keep
lower:
  ret i32 -128
keep:
  ret i32 %x
}
define i32 @with_effect(i32 %x) {
entry:
  %hi = icmp sgt i32 %x, 127
  br i1 %hi, label %upper, label %low
upper:
  store volatile i32 %x, ptr @sink, align 4
  ret i32 127
low:
  %lo = icmp slt i32 %x, -128
  br i1 %lo, label %lower, label %keep
lower:
  ret i32 -128
keep:
  ret i32 %x
}
`);
  run(tool('badge-codegen-sanitize'), [file('clamp.ll'), '-o', file('clamp.s')]);
  const asm = fs.readFileSync(file('clamp.s'), 'utf8');
  assert.match(asm.slice(0, asm.indexOf('.size clamp,')), /@ clamp-leaf: Q-neutral/);
  const effect = asm.slice(asm.indexOf('\nwith_effect:'), asm.indexOf('.size with_effect,'));
  assert.doesNotMatch(effect, /@ clamp-leaf:/);
  assert.match(effect, /^str/m);
  fs.writeFileSync(file('caller.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
.global main
.thumb_func
main:
  push {r4, lr}
  movs r0, #0
  msr apsr_nzcvq, r0
  movw r0, #256
  bl clamp
  mrs r1, apsr
  tst.w r1, #134217728
  bne fail
  cmp r0, #127
  bne fail
  mov.w r1, #134217728
  msr apsr_nzcvq, r1
  mov.w r0, #2147483648
  bl clamp
  mrs r1, apsr
  tst.w r1, #134217728
  beq fail
  cmn.w r0, #128
  bne fail
  movs r0, #0
  pop {r4, pc}
fail:
  movs r0, #1
  pop {r4, pc}
`);
  const objects = [['tests/fixtures/codegen_start.S', 'start.o'],
    [file('caller.s'), 'caller.o'], [file('clamp.s'), 'clamp.o']];
  for (const [s, o] of objects) run(tool('badge-asm'), [s, '-o', file(o)]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    ...objects.map(([, o]) => file(o)), '-o', file('clamp.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('clamp.elf')]);
});
test('numeric synthesis and copy folding retain flags and match LLVM encodings', t => {
  const file = files(t);
  fs.writeFileSync(file('matrix.s'), run(tool('constant_selection_test'), []));
  run(tool('badge-asm'), [file('matrix.s'), '-o', file('owned.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('matrix.s'), '-o', file('llvm.o')]);
  const text = name => decodeElf(fs.readFileSync(file(name))).sections.find(s => s.name === '.text').data;
  assert.deepEqual(text('owned.o'), text('llvm.o'));
  fs.writeFileSync(file('runtime.s'), run(tool('constant_selection_test'), ['--runtime']));
  run(tool('badge-asm'), [file('runtime.s'), '-o', file('runtime.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('runtime.s'), '-o', file('runtime-llvm.o')]);
  assert.deepEqual(text('runtime.o'), text('runtime-llvm.o'));
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('runtime.o'), '-o', file('runtime.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('runtime.elf')]);
});
test('unrelated volatile RAM constants, joins, loops and opaque barriers preserve reached results', t => {
  const file = files(t);
  fs.writeFileSync(file('source.c'), `
static volatile unsigned values[7];
static unsigned work(unsigned x) {
  unsigned i, sum = 0;
  values[0] = 0x01010101u; values[1] = 0xffffff00u;
  values[2] = 0x80000000u; values[3] = 0x00ff00ffu;
  __asm__ volatile("\\nnop\\n\\ndsb sy" ::: "memory", "cc");
  values[4] = x ? 0xff00ff00u : 0x0c000000u;
  for (i = 0; i < 7; i++) sum ^= values[i];
  return sum;
}
int main(void) {
  unsigned expected = 0x01010101u ^ 0xffffff00u ^ 0x80000000u ^ 0x00ff00ffu;
  return work(0) != (expected ^ 0x0c000000u) ||
         work(1) != (expected ^ 0xff00ff00u);
}`);
  run(tool('badge-ir'), [file('source.c'), '-o', file('initial.ll')]);
  run(tool('badge-opt'), [file('initial.ll'), '-o', file('input.ll')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-Oz', '-ffreestanding', '-fno-builtin', '-c', file('source.c'), '-o', file('llvm.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const residency of ['global-compact', 'local', 'stack']) {
    let control;
    for (const policy of ['thumb-widths', 'thumb-constants']) {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`,
        `--register-residency=${residency}`, file('input.ll'), '-o', file(`${policy}.s`)]);
      const assembly = fs.readFileSync(file(`${policy}.s`), 'utf8');
      assert.match(assembly, /nop\n\ndsb sy/);
      const frames = parseAssembly(assembly, policy);
      run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
      run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
        file('start.o'), file(`${policy}.o`), '-o', file(`${policy}.elf`)]);
      run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
        '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
        '-kernel', file(`${policy}.elf`)]);
      const bytes = decodeElf(fs.readFileSync(file(`${policy}.o`))).sections
        .filter(s => s.flags & 4).reduce((sum, s) => sum + s.size, 0);
      if (!control) control = { frames, bytes };
      else {
        assert(bytes < control.bytes);
        for (const [name, f] of frames)
          for (const field of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
            assert(f[field] <= control.frames.get(name)[field]);
      }
    }
  }
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('llvm.o'), '-o', file('llvm.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('llvm.elf')]);
});

test('short integer forms preserve wraparound, bit fields, and masked conditions', t => {
  const file = files(t);
  const fn = (name, parameters, body) =>
    `define i32 @${name}(${parameters}) {\nentry:\n${body}\n}\n`;
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
    fn('reverse', 'i32 %x', '%r = sub i32 228, %x\nret i32 %r') +
    fn('clear', 'i32 %x', '%r = and i32 4294967291, %x\nret i32 %r') +
    fn('extract', 'i32 %x', '%s = lshr i32 %x, 23\n%r = and i32 511, %s\nret i32 %r') +
    fn('middle', 'i32 %x', '%s = lshr i32 %x, 9\n%r = and i32 %s, 2047\nret i32 %r') +
    fn('msub', 'i32 %x, i32 %y, i32 %z',
      '%p = mul i32 %x, %y\n%r = sub i32 %z, %p\nret i32 %r') +
    fn('opposite', 'i32 %x, i32 %y, i32 %z',
      '%p = mul i32 %x, %y\n%r = sub i32 %p, %z\nret i32 %r') +
    fn('keep_shift', 'i32 %x',
      '%s = lshr i32 %x, 11\n%m = and i32 %s, 7\n%r = xor i32 %s, %m\nret i32 %r') +
    fn('masked_eq', 'i32 %x',
      '%m = and i32 %x, 2147483648\n%c = icmp eq i32 0, %m\n%r = zext i1 %c to i32\nret i32 %r') +
    fn('masked_branch', 'i32 %x, i32 %y',
      '%m = and i32 %x, %y\n%c = icmp ne i32 %m, 0\nbr i1 %c, label %yes, label %no\n' +
      'yes:\nret i32 77\nno:\nret i32 91') +
    fn('opaque', 'i32 %x',
      'call void asm sideeffect "nop", "~{cc}"()\n' +
      '%m = and i32 %x, 16\n%c = icmp eq i32 %m, 0\n%r = zext i1 %c to i32\nret i32 %r'));
  fs.writeFileSync(file('caller.c'), `
extern unsigned reverse(unsigned), clear(unsigned), extract(unsigned), middle(unsigned);
extern unsigned msub(unsigned, unsigned, unsigned), opposite(unsigned, unsigned, unsigned);
extern unsigned keep_shift(unsigned), masked_eq(unsigned), masked_branch(unsigned, unsigned), opaque(unsigned);
int main(void) {
  unsigned rng = 17;
  for (unsigned i = 0; i < 258; i++) {
    unsigned x = i == 0 ? 0u : i == 1 ? ~0u : rng;
    unsigned y = i % 7 ? rng ^ 0xba2fe3b9u : 0u;
    unsigned z = rng * 17u;
    if (reverse(x) != 228u - x || clear(x) != (x & ~4u) ||
        extract(x) != x >> 23 || middle(x) != ((x >> 9) & 2047u) ||
        msub(x, y, z) != z - x * y ||
        opposite(x, y, z) != x * y - z ||
        keep_shift(x) != ((x >> 11) ^ ((x >> 11) & 7u)) ||
        masked_eq(x) != (x >> 31 == 0) ||
        masked_branch(x, y) != ((x & y) ? 77u : 91u) ||
        opaque(x) != ((x & 16u) == 0)) return 1;
    rng = rng * 1664525u + 1013904223u;
  }
  return 0;
}
`);
  run(tool('badge-codegen-sanitize'), [file('input.ll'), '-o', file('input.s')]);
  const asm = fs.readFileSync(file('input.s'), 'utf8');
  const body = name => asm.slice(asm.indexOf(`\n${name}:`), asm.indexOf(`.size ${name},`));
  assert.match(body('reverse'), /^rsb\.w .*#228$/m);
  assert.match(body('clear'), /^bic\.w .*#4$/m);
  assert.match(body('extract'), /^ubfx .*#23, #9$/m);
  assert.match(body('middle'), /^ubfx .*#9, #11$/m);
  assert.match(body('msub'), /^mls /m);
  assert.match(body('masked_eq'), /^tst\.w .*#2147483648$/m);
  assert.match(body('masked_branch'), /^tst\.[nw] r\d+, r\d+$/m);
  assert.doesNotMatch(body('opposite'), /^mls /m);
  assert.doesNotMatch(body('keep_shift'), /^ubfx .*#11, #3$/m);
  assert.doesNotMatch(body('opaque'), /^tst\./m);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-asm'), [file('input.s'), '-o', file('input.o')]);
  run(clang, [...target, '-c', file('input.s'), '-o', file('llvm.o')]);
  const code = name => decodeElf(fs.readFileSync(file(name))).sections
    .filter(s => s.flags & 4).map(s => [s.name, s.data]);
  assert.deepEqual(code('input.o'), code('llvm.o'));
  run(clang, [...target, '-O2', '-ffreestanding', '-c', file('caller.c'), '-o', file('caller.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('caller.o'), file('input.o'), '-o', file('input.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('input.elf')]);
});

test('short constant arithmetic and inverted operands preserve Thumb results', t => {
  const file = files(t);
  const fn = (name, args, body) => `define i32 @${name}(${args}) {\nentry:\n${body}\n}\n`;
  const unary = 'i32 %x', binary = 'i32 %x, i32 %y';
  fs.writeFileSync(file('arith.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
    fn('low', unary, '%r = and i32 %x, 2047\nret i32 %r') +
    fn('plus', unary, '%r = mul i32 5, %x\nret i32 %r') +
    fn('minus', unary, '%r = mul i32 %x, 2147483647\nret i32 %r') +
    fn('scaled', binary, '%p = mul i32 %x, 64\n%r = sub i32 %y, %p\nret i32 %r') +
    fn('reverse', binary, '%p = mul i32 8, %x\n%r = sub i32 %p, %y\nret i32 %r') +
    fn('literal', unary, '%p = mul i32 8, %x\n%r = sub i32 %p, 3\nret i32 %r') +
    fn('clear_reg', binary, '%n = xor i32 %x, -1\n%r = and i32 %n, %y\nret i32 %r') +
    fn('set_reg', binary, '%n = xor i32 -1, %x\n%r = or i32 %y, %n\nret i32 %r') +
    fn('keep_reg', binary, '%n = xor i32 %x, -1\n%r = and i32 %n, %y\n%a = xor i32 %n, %r\nret i32 %a') +
    fn('equal', unary, '%c = icmp eq i32 -17, %x\nbr i1 %c, label %yes, label %no\n' +
      'yes:\nret i32 8\nno:\nret i32 3') +
    fn('ordered', unary, '%c = icmp ult i32 %x, -17\n%r = zext i1 %c to i32\nret i32 %r') +
    fn('signed_ordered', unary, '%c = icmp slt i32 %x, -128\n%r = zext i1 %c to i32\nret i32 %r') +
    fn('signed_minimum', unary, '%c = icmp sgt i32 %x, -2147483648\n%r = zext i1 %c to i32\nret i32 %r'));
  fs.writeFileSync(file('caller.c'), `
extern unsigned low(unsigned), plus(unsigned), minus(unsigned), literal(unsigned),
                equal(unsigned), ordered(unsigned), signed_ordered(unsigned),
                signed_minimum(unsigned);
extern unsigned scaled(unsigned, unsigned), clear_reg(unsigned, unsigned),
                set_reg(unsigned, unsigned), keep_reg(unsigned, unsigned),
                reverse(unsigned, unsigned);
int main(void) {
  unsigned seed = 9;
  for (unsigned i = 0; i < 264; i++) {
    unsigned x = i == 0 ? 0 : i == 1 ? ~0u : i == 2 ? 0x80000000u :
                 i < 6 ? ~0u - (i + 12u) : i < 9 ? (unsigned)(-136 + (int)i) : seed;
    unsigned y = seed ^ 0xba2fe3b9u;
    if (low(x) != (x & 2047u) || plus(x) != 5u * x ||
        minus(x) != 2147483647u * x || scaled(x, y) != y - 64u * x ||
        reverse(x, y) != 8u * x - y || literal(x) != 8u * x - 3u ||
        clear_reg(x, y) != (~x & y) || set_reg(x, y) != (~x | y) ||
        keep_reg(x, y) != (~x ^ (~x & y)) ||
        equal(x) != (x == -17u ? 8u : 3u) || ordered(x) != (x < -17u) ||
        signed_ordered(x) != ((int)x < -128) || signed_minimum(x) != (x != 0x80000000u)) return 1;
    seed = seed * 1664525u + 1013904223u;
  }
  return 0;
}
`);
  run(tool('badge-codegen-sanitize'), [file('arith.ll'), '-o', file('arith.s')]);
  const asm = fs.readFileSync(file('arith.s'), 'utf8');
  const body = name => asm.slice(asm.indexOf(`\n${name}:`), asm.indexOf(`.size ${name},`));
  for (const [name, expected] of Object.entries({
    low: /^ubfx .*#0, #11$/m, plus: /^add\.w .*lsl #2$/m,
    minus: /^rsb\.w .*lsl #31$/m, scaled: /^sub\.w .*lsl #6$/m,
    reverse: /^rsb\.w .*lsl #3$/m,
    clear_reg: /^bic\.w r\d+, r\d+, r\d+$/m, set_reg: /^orn\.w r\d+, r\d+, r\d+$/m,
    equal: /^cmn\.w .*#17$/m,
    ordered: /^cmn\.w .*#17$/m, signed_ordered: /^cmn\.w .*#128$/m,
  })) assert.match(body(name), expected);
  assert.doesNotMatch(body('keep_reg'), /^bic\.w /m);
  assert.doesNotMatch(body('literal'), /^rsb\.w .*lsl #3$/m);
  assert.doesNotMatch(body('signed_minimum'), /^cmn\.w /m);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-asm'), [file('arith.s'), '-o', file('arith.o')]);
  run(clang, [...target, '-c', file('arith.s'), '-o', file('llvm.o')]);
  const text = name => decodeElf(fs.readFileSync(file(name))).sections
    .filter(s => s.flags & 4).map(s => [s.name, s.data]);
  assert.deepEqual(text('arith.o'), text('llvm.o'));
  run(clang, [...target, '-O2', '-ffreestanding', '-c', file('caller.c'), '-o', file('caller.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('caller.o'), file('arith.o'), '-o', file('arith.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('arith.elf')]);
});

test('direct casts and compact wide compares preserve signed and unsigned boundaries', t => {
  const file = files(t);
  const compare = ['eq', 'ne', 'ult', 'ule', 'ugt', 'uge', 'slt', 'sle', 'sgt', 'sge'];
  const branches = ['eq', 'ult', 'sgt', 'sle'];
  const fn = (name, args, type, body) =>
    `define ${type} @${name}(${args}) {\nentry:\n${body}\n}\n`;
  const wide = 'i64 %x, i64 %y';
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
    fn('word_low', 'i64 %x', 'i32', '%r = trunc i64 %x to i32\nret i32 %r') +
    fn('byte_low', 'i32 %x', 'i8', '%r = trunc i32 %x to i8\nret i8 %r') +
    fn('half_low', 'i32 %x', 'i16', '%r = trunc i32 %x to i16\nret i16 %r') +
    fn('zero_half', 'i8 %x', 'i16', '%r = zext i8 %x to i16\nret i16 %r') +
    fn('zero_wide', 'i32 %x', 'i64', '%r = zext i32 %x to i64\nret i64 %r') +
    fn('sign_wide', 'i16 %x', 'i64', '%r = sext i16 %x to i64\nret i64 %r') +
    fn('zero_word', 'i32 %x', 'i32', '%c = icmp eq i32 0, %x\n%r = zext i1 %c to i32\nret i32 %r') +
    fn('branch_negative', 'i64 %x', 'i32',
      '%c = icmp slt i64 %x, 0\nbr i1 %c, label %yes, label %no\n' +
      'yes:\nret i32 7\nno:\nret i32 3') +
    compare.map(op => fn(`is_${op}`, wide, 'i32',
      `%c = icmp ${op} i64 %x, %y\n%r = zext i1 %c to i32\nret i32 %r`)).join('') +
    branches.map(op => fn(`branch_${op}`, wide, 'i32',
      `%c = icmp ${op} i64 %x, %y\nbr i1 %c, label %yes, label %no\n` +
      'yes:\nret i32 7\nno:\nret i32 3')).join(''));
  fs.writeFileSync(file('caller.c'), `
typedef unsigned long long u64;
typedef long long s64;
extern unsigned word_low(u64);
extern unsigned char byte_low(unsigned);
extern unsigned short half_low(unsigned), zero_half(unsigned char);
extern u64 zero_wide(unsigned);
extern s64 sign_wide(short);
extern unsigned zero_word(unsigned), branch_negative(u64);
${compare.map(op => `extern unsigned is_${op}(u64, u64);`).join('\n')}
${branches.map(op => `extern unsigned branch_${op}(u64, u64);`).join('\n')}
int main(void) {
  static const u64 values[] = {
    0, 1, 0xfffffffeu, 0xffffffffu, 0x100000000ull, 0x7fffffffffffffffull,
    0x8000000000000000ull, 0x8000000000000001ull, 0xffffffff00000000ull, ~0ull,
    0x123456789abcdef0ull, 0x12345678fedcba98ull,
    0x80000000ffffffffull, 0x7fffffff00000000ull, 0x1234567800000000ull
  };
  for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
    u64 x = values[i];
    if (word_low(x) != (unsigned)x || byte_low((unsigned)x) != (unsigned char)x ||
        half_low((unsigned)x) != (unsigned short)x ||
        zero_half((unsigned char)x) != (unsigned char)x ||
        zero_wide((unsigned)x) != (unsigned)x || sign_wide((short)x) != (short)x ||
        zero_word((unsigned)x) != ((unsigned)x == 0) ||
        branch_negative(x) != ((s64)x < 0 ? 7u : 3u))
      return 1;
    for (unsigned j = 0; j < sizeof(values) / sizeof(*values); j++) {
      u64 y = values[j];
      if (is_eq(x,y) != (x == y) || is_ne(x,y) != (x != y) ||
          is_ult(x,y) != (x < y) || is_ule(x,y) != (x <= y) ||
          is_ugt(x,y) != (x > y) || is_uge(x,y) != (x >= y) ||
          is_slt(x,y) != ((s64)x < (s64)y) || is_sle(x,y) != ((s64)x <= (s64)y) ||
          is_sgt(x,y) != ((s64)x > (s64)y) || is_sge(x,y) != ((s64)x >= (s64)y) ||
          branch_eq(x,y) != (x == y ? 7u : 3u) ||
          branch_ult(x,y) != (x < y ? 7u : 3u) ||
          branch_sgt(x,y) != ((s64)x > (s64)y ? 7u : 3u) ||
          branch_sle(x,y) != ((s64)x <= (s64)y ? 7u : 3u))
        return 2;
    }
  }
  return 0;
}
`);
  run(tool('badge-codegen-sanitize'), [file('input.ll'), '-o', file('input.s')]);
  const asm = fs.readFileSync(file('input.s'), 'utf8');
  const body = name => asm.slice(asm.indexOf(`\n${name}:`), asm.indexOf(`.size ${name},`));
  for (const op of compare.slice(0, 6))
    assert.match(body(`is_${op}`), /^cmp\.n r2, r3\nit eq\ncmpeq\.n r0, r1$/m);
  assert.doesNotMatch(body('is_slt'), /^cmpeq\.n /m);
  assert.match(body('is_slt'), /^subs\.n r0, r0, r1\nsbcs\.w r2, r2, r3$/m);
  assert.match(body('is_sgt'), /^subs\.n r0, r1, r0\nsbcs\.w r2, r3, r2$/m);
  assert.match(body('is_eq'), /^ite eq\nmoveq\.n r0, #1\nmovne\.n r0, #0$/m);
  assert.match(body('zero_word'), /^clz r\d+, r\d+$/m);
  assert.match(body('branch_negative'), /^cmp\.n r0, #0$/m);
  assert.doesNotMatch(body('branch_negative'), /^sbcs\.w/m);
  for (const op of branches) assert.match(body(`branch_${op}`), /compare-branch: fused 1/);
  assert.doesNotMatch(body('zero_half'), /^uxth[. ]/m);
  assert.match(body('sign_wide'), /^sxth\.[nw] r0, r[3-9]\d*$/m);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-asm'), [file('input.s'), '-o', file('input.o')]);
  run(clang, [...target, '-c', file('input.s'), '-o', file('llvm.o')]);
  const text = name => decodeElf(fs.readFileSync(file(name))).sections
    .filter(s => s.flags & 4).map(s => [s.name, s.data]);
  assert.deepEqual(text('input.o'), text('llvm.o'));
  run(clang, [...target, '-O2', '-ffreestanding', '-c', file('caller.c'), '-o', file('caller.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('caller.o'), file('input.o'), '-o', file('input.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('input.elf')]);
});

test('wide arithmetic lowers carry, borrow and word boundaries exactly', t => {
  const file = files(t);
  const fn = (name, args, body) => `define i64 @${name}(${args}) {\nentry:\n${body}\n}\n`;
  const unary = 'i64 %x', binary = 'i64 %x, i64 %y';
  const truncatedDivisors = [2, 4096, 2147483648];
  const shifts = ['shl', 'lshr', 'ashr'].flatMap(op =>
    [0, 1, 31, 32, 33, 63].map(n => [op, n]));
  const masks = [
    ['keep_low', 'and', '4294967295', 'x & 0xffffffffull'],
    ['keep_high', 'and', '-4294967296', 'x & 0xffffffff00000000ull'],
    ['or_low', 'or', '4275878552', 'x | 4275878552ull'],
    ['xor_high', 'xor', '1095216660480', 'x ^ 1095216660480ull'],
  ];
  fs.writeFileSync(file('wide.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
    shifts.map(([op, n]) => fn(`${op}${n}`, unary, `%v = ${op} i64 %x, ${n}\nret i64 %v`)).join('') +
    masks.map(([name, op, value]) => fn(name, unary, `%v = ${op} i64 ${value}, %x\nret i64 %v`)).join('') +
    fn('neg', unary, '%v = sub i64 0, %x\nret i64 %v') +
    fn('product', binary, '%v = mul i64 %x, %y\nret i64 %v') +
    fn('wide_mac', 'i32 %a, i32 %b, i64 %bias', `%x = sext i32 %a to i64
%y = sext i32 %b to i64
%p = mul i64 %x, %y
%v = add i64 %bias, %p
ret i64 %v`) +
    `define i32 @wide_reduction(i32 %a, i32 %b) {
entry:
  %x = sext i32 %a to i64
  %y = sext i32 %b to i64
  %p = mul i64 %x, %y
  %neg = icmp slt i64 %p, 0
  br i1 %neg, label %negative, label %positive
negative:
  br label %join
positive:
  br label %join
join:
  %bias = phi i32 [-2048, %negative], [2048, %positive]
  %wide = sext i32 %bias to i64
  %v = add i64 %p, %wide
  %d = sdiv i64 %v, 4096
  %low = trunc i64 %d to i32
  ret i32 %low
}
` +
    [1, 1024, 2147483648].map(n => fn(`div${n}`, unary,
      `%v = sdiv i64 %x, ${n}\nret i64 %v`)).join('') +
    truncatedDivisors.map(n => `define i32 @lowdiv${n}(i64 %x) {
entry:
  %v = sdiv i64 %x, ${n}
  %low = trunc i64 %v to i32
  ret i32 %low
}
`).join(''));
  fs.writeFileSync(file('caller.c'), `
typedef unsigned long long u64;
typedef long long s64;
${[...shifts.map(([op, n]) => `${op}${n}`), ...masks.map(([name]) => name),
    'neg', 'div1', 'div1024', 'div2147483648']
    .map(name => `extern u64 ${name}(u64);`).join('\n')}
extern u64 product(u64, u64);
extern u64 wide_mac(int, int, u64);
extern unsigned wide_reduction(int, int);
${truncatedDivisors.map(n => `extern unsigned lowdiv${n}(u64);`).join('\n')}
int main(void) {
  static const u64 values[] = {
    0, 1, 1023, 1024, 0xffffffffu, 0x100000000ull, 0x100000001ull,
    0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull,
    0xffffffff00000000ull, ~1022ull, ~0ull, 0x123456789abcdef0ull
  };
  for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
    u64 x = values[i];
    ${shifts.map(([op, n]) => `if (${op}${n}(x) != (u64)(${op === 'ashr' ? '(s64)x' : 'x'} ${op === 'shl' ? '<<' : '>>'} ${n})) return 1;`).join('\n    ')}
    ${masks.map(([name, , , expr]) => `if (${name}(x) != (${expr})) return 2;`).join('\n    ')}
    if (neg(x) != 0ull - x || div1(x) != x || (s64)div1024(x) != (s64)x / 1024ll ||
        (s64)div2147483648(x) != (s64)x / 2147483648ll) return 3;
    ${truncatedDivisors.map(n => `if (lowdiv${n}(x) != (unsigned)((s64)x / ${n}ll)) return 5;`).join('\n    ')}
    for (unsigned j = 0; j < sizeof(values) / sizeof(*values); j++) {
      u64 y = values[j];
      if (product(x, y) != x * y) return 4;
      int a = (int)x, b = (int)y;
      s64 p = (s64)a * b;
      if (wide_mac(a, b, x) != (u64)p + x) return 6;
      if (wide_reduction(a, b) != (unsigned)((p + (p < 0 ? -2048 : 2048)) / 4096)) return 7;
    }
  }
  return 0;
}
`);
  run(tool('badge-codegen-sanitize'), [file('wide.ll'), '-o', file('wide.s')]);
  const assembly = fs.readFileSync(file('wide.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('shl1'), /^orr\.w r1, r1, r0, lsr #31$/m);
  assert.match(body('ashr31'), /^orr\.w r0, r0, r1, lsl #1$/m);
  assert.doesNotMatch(body('lshr32'), /^movw r[23],/m);
  assert.doesNotMatch(body('keep_low'), /^and\./m);
  assert.match(body('neg'), /^rsbs\.n r0, r0, #0\nmvn\.w r1, r1\nadc r1, r1, #0$/m);
  assert.match(body('product'), /^umull r0, r1, r0, r2$/m);
  assert.match(body('wide_mac'), /^smlal /m);
  assert.doesNotMatch(body('wide_mac'), /^smull /m);
  assert.match(body('wide_reduction'), /^smull r(?:4, r6|10, r11),/m);
  assert.doesNotMatch(body('wide_reduction'), /^smlal /m);
  assert.match(body('div1024'), /^lsr(?:\.w|s\.n) r4, r4, #22$/m);
  for (const n of truncatedDivisors) {
    assert.match(body(`lowdiv${n}`), /@ truncated-division: low result only/);
    assert.doesNotMatch(body(`lowdiv${n}`), /^asr\.w r1, r1,/m);
  }
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-asm'), [file('wide.s'), '-o', file('wide.o')]);
  run(clang, [...target, '-c', file('wide.s'), '-o', file('llvm.o')]);
  const text = name => decodeElf(fs.readFileSync(file(name))).sections
    .filter(s => s.flags & 4).map(s => [s.name, s.data]);
  assert.deepEqual(text('wide.o'), text('llvm.o'));
  run(clang, [...target, '-O2', '-ffreestanding', '-c', file('caller.c'), '-o', file('caller.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('caller.o'), file('wide.o'), '-o', file('wide.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('wide.elf')]);
});

test('direct result forwarding and stable SP frames preserve the mixed-call ABI', t => {
  const file = files(t);
  const local = (name, assembly) => `
define i32 @${name}(i32 %x) {
entry:
  %home = alloca i32, align 4
  store i32 %x, ptr %home, align 4
  ${assembly}
  %value = call i32 @mutate(ptr %home)
  %updated = load i32, ptr %home, align 4
  %out = xor i32 %value, %updated
  ret i32 %out
}`;
  fs.writeFileSync(file('calls.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
declare i32 @mutate(ptr)
declare i32 @callee5(i32, i32, i32, i32, i32)
declare i32 @callee2(i32, i32)
declare i64 @callee_wide(i64, i64, i32)
${local('stable', '')}
${local('fenced', 'call void asm sideeffect "nop", "~{memory}"()')}
define i32 @sibling(i32 %x) {
entry:
  %r = tail call i32 @callee2(i32 %x, i32 5)
  ret i32 %r
}
define i32 @nosibling(i32 %x) {
entry:
  %r = notail call i32 @callee2(i32 %x, i32 5)
  ret i32 %r
}
define i32 @return_word(i32 %x) {
entry:
  %r = call i32 @callee5(i32 1, i32 2, i32 3, i32 4, i32 %x)
  ret i32 %r
}
define i64 @return_wide(i64 %x, i64 %y) {
entry:
  %r = call i64 @callee_wide(i64 %x, i64 %y, i32 9)
  ret i64 %r
}
define i32 @continued(i32 %x) {
entry:
  %r = call i32 @callee5(i32 1, i32 2, i32 3, i32 4, i32 %x)
  %adjusted = xor i32 %r, 1
  ret i32 %adjusted
}`);
  fs.writeFileSync(file('caller.c'), `
typedef unsigned long long u64;
extern unsigned stable(unsigned), fenced(unsigned), return_word(unsigned), continued(unsigned);
extern u64 return_wide(u64, u64);
extern unsigned sibling(unsigned), nosibling(unsigned);
unsigned mutate(unsigned *p) { unsigned x = *p; *p ^= 0xdeadbeefu; return x + 1; }
unsigned callee5(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e) {
  return a + b + c + d + e;
}
unsigned callee2(unsigned a, unsigned b) { return a + b; }
u64 callee_wide(u64 a, u64 b, unsigned salt) {
  return a ^ (b + salt);
}
int main(void) {
  unsigned x = 0xffffffffu;
  u64 a = 0x0123456789abcdefull, b = 0xfffffffffffffffaull;
  return stable(x) != (x ^ 0xdeadbeefu) || fenced(x) != stable(x) ||
         return_word(x) != 9 || continued(x) != 8 || return_wide(a,b) != (a ^ 3ull) ||
         sibling(x) != 4 || nosibling(x) != 4;
}
`);
  run(tool('badge-codegen-sanitize'), [file('calls.ll'), '-o', file('calls.s')]);
  const assembly = fs.readFileSync(file('calls.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  const frames = parseAssembly(assembly, 'call transport');
  assert.equal(frames.get('stable').frame_base, 'sp');
  assert.equal(frames.get('fenced').frame_base, 'r7');
  assert.match(body('fenced'), /^mov sp, r7$/m);
  assert.match(body('sibling'), /^b\.w callee2$/m);
  assert.match(body('nosibling'), /^bl callee2$/m);
  assert.deepEqual(frames.get('sibling').calls, [{ target: 'callee2', outgoing_bytes: 0, sibling: true }]);
  assert.match(body('return_word'), /^str\.w r(?:8|9|10|11|12), \[sp, #0\]$/m);
  assert.match(body('return_wide'), /^str\.n r[0-7], \[sp, #0\]$/m);
  for (const name of ['return_word', 'return_wide']) {
    assert.match(body(name), /@ call-result: ABI registers forwarded to return/);
    assert.doesNotMatch(body(name), /^mov sp, r7$/m);
    assert.equal(frames.get(name).max_outgoing_bytes, 8);
  }
  assert.doesNotMatch(body('continued'), /@ call-result:/);
  assert([...frames.values()].every(f => !f.unknown.includes('unverified_frame_balance')));
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-asm'), [file('calls.s'), '-o', file('calls.o')]);
  run(clang, [...target, '-c', file('calls.s'), '-o', file('llvm.o')]);
  const text = name => decodeElf(fs.readFileSync(file(name))).sections
    .filter(s => s.flags & 4).map(s => [s.name, s.data]);
  assert.deepEqual(text('calls.o'), text('llvm.o'));
  run(clang, [...target, '-O2', '-ffreestanding', '-c', file('caller.c'), '-o', file('caller.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('caller.o'), file('calls.o'), '-o', file('calls.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('calls.elf')]);
});

test('direct scaled and frame addresses preserve aligned, unaligned, and volatile accesses', t => {
  const file = files(t);
  fs.writeFileSync(file('address.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@words = global [4 x i32] [i32 11, i32 20, i32 31, i32 47], align 4
@halves = global [4 x i16] zeroinitializer, align 2
define internal zeroext i16 @narrow(i32 %x) {
entry:
  %v = trunc i32 %x to i16
  ret i16 %v
}
define internal void @indexed_store(ptr %base, i32 %index, i32 %value) {
entry:
  %off = mul i32 %index, 2
  %dest = getelementptr i8, ptr %base, i32 %off
  %v = call zeroext i16 @narrow(i32 %value)
  store i16 %v, ptr %dest, align 2
  ret void
}
define internal void @relay(ptr %base, ptr %table, i32 %index) {
entry:
  %next = add i32 %index, 1
  %out = mul i32 %next, 2
  %dest = getelementptr i8, ptr %base, i32 %out
  %off = mul i32 %index, 4
  %p = getelementptr i8, ptr %table, i32 %off
  %value = load i32, ptr %p, align 4
  %v = call zeroext i16 @narrow(i32 %value)
  store i16 %v, ptr %dest, align 2
  ret void
}
define internal void @unaligned_store(ptr %base, i32 %index, i16 %value) {
entry:
  %off = mul i32 %index, 2
  %dest = getelementptr i8, ptr %base, i32 %off
  store i16 %value, ptr %dest, align 1
  ret void
}
define internal i32 @indexed(ptr %base, i32 %index) {
entry:
  %p = getelementptr i32, ptr %base, i32 %index
  %value = load volatile i32, ptr %p, align 4
  ret i32 %value
}
define internal i32 @frame(i32 %index) {
entry:
  %prefix = alloca [1024 x i8], align 4
  %buffer = alloca [16 x i32], align 4
  store volatile i32 7, ptr %prefix, align 4
  %p = getelementptr i32, ptr %buffer, i32 %index
  store volatile i32 1234, ptr %p, align 4
  %a = load volatile i32, ptr %p, align 4
  %b = load volatile i32, ptr %prefix, align 4
  %sum = add i32 %a, %b
  ret i32 %sum
}
define i32 @main() {
entry:
  %base = getelementptr i32, ptr @words, i32 2
  %far = getelementptr i32, ptr %base, i32 65537
  %back = getelementptr i32, ptr %far, i32 -65537
  %a = call i32 @indexed(ptr %back, i32 -1)
  %b = call i32 @frame(i32 3)
  %bases = getelementptr i16, ptr @halves, i32 2
  call void @indexed_store(ptr %bases, i32 -1, i32 70000)
  %h = getelementptr i16, ptr @halves, i32 1
  %half = load volatile i16, ptr %h, align 2
  %check = xor i16 %half, 4464
  %z = zext i16 %check to i32
  call void @relay(ptr @halves, ptr @words, i32 1)
  %relayed = load volatile i16, ptr %bases, align 2
  %r = zext i16 %relayed to i32
  %relaycheck = xor i32 %r, 20
  %x = xor i32 %a, 20
  %y = xor i32 %b, 1241
  %sum = or i32 %x, %y
  %partial = or i32 %sum, %z
  %status = or i32 %partial, %relaycheck
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('address.ll'), '-o', file('address.s')]);
  const assembly = fs.readFileSync(file('address.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('indexed'), /add\.w r\d+, r(?:[3-6]|8|10|11), r\d+, lsl #2/);
  assert.match(body('indexed_store'), /bl narrow[\s\S]*strh\.w r0, \[r\d+, r\d+, lsl #1\]/);
  assert.match(body('relay'), /ldr\.w r0, \[r\d+, r\d+, lsl #2\]\nbl narrow\nadd\.w [^\n]+\nstrh\.n r0, \[r\d+, #2\]/);
  assert.doesNotMatch(body('unaligned_store'), /^strh[. ]/m);
  assert.match(assembly, /addw r\d+, (?:r7|sp), #1024\b/);
  run(tool('badge-asm'), [file('address.s'), '-o', file('owned.o')]);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(clang, [...target, '-c', file('address.s'), '-o', file('reassembled.o')]);
  run(clang, [...target, '-Oz', '-c', file('address.ll'), '-o', file('llvm.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const name of ['owned', 'reassembled', 'llvm']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file(`${name}.o`), '-o', file('address.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('address.elf')]);
  }
});

test('known upper-zero bits remove narrow-return truncation only when proved', t => {
  const file = files(t);
  const f = (name, mask) => `define zeroext i16 @${name}(i32 %a, i32 %b) {
entry:
  %l = and i32 %a, ${mask}
  %r = and i32 %b, 2016
  %merged = or i32 %l, %r
  %narrow = trunc i32 %merged to i16
  ret i16 %narrow
}`;
  fs.writeFileSync(file('bits.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
${f('clear', 63519)}
${f('unknown', 129055)}
define i32 @main() {
entry:
  %a = call zeroext i16 @clear(i32 -1, i32 -1)
  %b = call zeroext i16 @unknown(i32 -1, i32 -1)
  %ab = and i16 %a, %b
  %status = xor i16 %ab, -1
  %v = zext i16 %status to i32
  ret i32 %v
}`);
  run(tool('badge-codegen-sanitize'), [file('bits.ll'), '-o', file('bits.s')]);
  const assembly = fs.readFileSync(file('bits.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.doesNotMatch(body('clear'), /^uxth[. ]/m);
  assert.match(body('unknown'), /^uxth[. ]/m);
  run(tool('badge-asm'), [file('bits.s'), '-o', file('bits.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('bits.o'), '-o', file('bits.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('bits.elf')]);
});

test('signed offset folds and stack-free wrapping absolute-value leaves preserve sign boundaries', t => {
  const file = files(t);
  const abs = (name, extra) => `
define i32 @${name}(i32 %x) {
entry:
  %negative = icmp slt i32 %x, 0
  br i1 %negative, label %neg, label %pos
neg:
  ${extra}
  %flip = sub i32 0, %x
  br label %join
pos:
  br label %join
join:
  %answer = phi i32 [%flip, %neg], [%x, %pos]
  ret i32 %answer
}`;
  fs.writeFileSync(file('shape.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@points = global [2 x i32] [i32 0, i32 65534], align 4
@sink = global i32 0, align 4
${abs('magnitude', '')}
${abs('observable', 'store volatile i32 %x, ptr @sink, align 4')}
define i32 @read_signed(ptr %p, i32 %a, i32 %b) {
entry:
  %first = load i16, ptr %p, align 2
  %offset = getelementptr i8, ptr %p, i32 4
  %second = load i16, ptr %offset, align 2
  %wide = sext i16 %second to i32
  %head = zext i16 %first to i32
  %x = add i32 %wide, %head
  %y = add i32 %a, %b
  %answer = add i32 %x, %y
  ret i32 %answer
}
define i32 @main() {
entry:
  %a = call i32 @read_signed(ptr @points, i32 0, i32 0)
  %b = call i32 @magnitude(i32 %a)
  %c = call i32 @magnitude(i32 -2147483648)
  %d = call i32 @magnitude(i32 32767)
  %e = call i32 @observable(i32 -7)
  %written = load volatile i32, ptr @sink, align 4
  %f = xor i32 %b, 2
  %g = xor i32 %c, -2147483648
  %h = xor i32 %d, 32767
  %i = xor i32 %e, 7
  %j = xor i32 %written, -7
  %k = or i32 %f, %g
  %m = or i32 %h, %i
  %l = or i32 %k, %m
  %status = or i32 %l, %j
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants', file('shape.ll'), '-o', file('shape.s')]);
  const text = fs.readFileSync(file('shape.s'), 'utf8');
  const body = name => text.slice(text.indexOf(`\n${name}:`), text.indexOf(`.size ${name},`));
  assert.match(body('magnitude'), /scalar-abs-leaf/);
  assert.doesNotMatch(body('magnitude'), /\bpush\b/);
  assert.doesNotMatch(body('observable'), /scalar-abs-leaf/);
  assert.match(body('read_signed'), /ldrsh\.w r\d+, \[r\d+, #4\]/);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('shape.s'), '-o', file('shape.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('shape.o'), '-o', file('shape.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('shape.elf')]);
});

test('loop allocation weights PHI edges but ignores irreducible and unreachable cycles', t => {
  const file = files(t);
  fs.writeFileSync(file('loops.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @nested(i32 %limit) {
entry:
  br label %outer
outer:
  %i = phi i32 [0, %entry], [%next, %latch]
  %done = icmp uge i32 %i, %limit
  br i1 %done, label %exit, label %inner
inner:
  %j = phi i32 [0, %outer], [%step, %inner]
  %step = add i32 %j, 1
  %again = icmp ult i32 %step, 4
  br i1 %again, label %inner, label %latch
latch:
  %next = add i32 %i, %step
  br label %outer
dead:
  br label %dead
exit:
  ret i32 %i
}
define i32 @irreducible(i1 %choose, i1 %leave) {
entry:
  br i1 %choose, label %left, label %right
left:
  br i1 %leave, label %exit, label %right
right:
  br i1 %leave, label %exit, label %left
exit:
  ret i32 0
}
define i32 @main() {
entry:
  %a = call i32 @nested(i32 9)
  %b = call i32 @nested(i32 0)
  %c = call i32 @irreducible(i1 false, i1 true)
  %x = xor i32 %a, 12
  %y = or i32 %b, %c
  %status = or i32 %x, %y
  ret i32 %status
}`);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const policy of ['thumb-widths', 'thumb-constants']) {
    run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`,
      file('loops.ll'), '-o', file('loops.s')]);
    const assembly = fs.readFileSync(file('loops.s'), 'utf8');
    assert.deepEqual(assembly.match(/@ loop-allocation:.*$/gm) ?? [],
      policy === 'thumb-constants' ? ['@ loop-allocation: weighted headers 2'] : []);
    run(tool('badge-asm'), [file('loops.s'), '-o', file('loops.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file('loops.o'), '-o', file('loops.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('loops.elf')]);
  }
});

test('adjacent store forwarding keeps different widths, addresses and volatile accesses', t => {
  const file = files(t);
  fs.writeFileSync(file('forward.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@a = global i32 0, align 4
@b = global i32 23, align 4
define i32 @work(ptr %p, ptr %q, i32 %value) {
entry:
  store i32 %value, ptr %p, align 4
  %a = load i32, ptr %p, align 4
  store volatile i32 7, ptr %p, align 4
  %b = load i32, ptr %p, align 4
  store i32 8, ptr %p, align 4
  %c = load volatile i32, ptr %p, align 4
  store i32 9, ptr %p, align 4
  %d = load i32, ptr %q, align 4
  store i32 255, ptr %p, align 4
  %byte = load i8, ptr %p, align 1
  %e = zext i8 %byte to i32
  %f = add i32 %a, %b
  %g = add i32 %c, %d
  %h = add i32 %f, %g
  %sum = add i32 %h, %e
  ret i32 %sum
}
define i32 @main() {
entry:
  %r = call i32 @work(ptr @a, ptr @b, i32 1024)
  %status = xor i32 %r, 1317
  ret i32 %status
}`);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const policy of ['thumb-widths', 'thumb-constants']) {
    run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`,
      file('forward.ll'), '-o', file('forward.s')]);
    const assembly = fs.readFileSync(file('forward.s'), 'utf8');
    assert.equal((assembly.match(/@ store-load-forward:/g) ?? []).length,
      policy === 'thumb-constants' ? 1 : 0);
    run(tool('badge-asm'), [file('forward.s'), '-o', file('forward.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file('forward.o'), '-o', file('forward.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('forward.elf')]);
  }
});

test('rematerialized frame addresses survive outgoing call stack adjustments', t => {
  const file = files(t);
  const cells = Array.from({ length: 9 }, (_, i) => `%p${i}`);
  const params = cells.map(p => `ptr ${p}`);
  const lines = cells.map((p, i) => `  %v${i} = load volatile i32, ptr ${p}, align 4`);
  for (let i = 1; i < cells.length; i++)
    lines.push(`  %s${i} = add i32 %${i === 1 ? 'v0' : `s${i - 1}`}, %v${i}`);
  fs.writeFileSync(file('frame.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @read_cells(${params.join(', ')}) {
entry:
${lines.join('\n')}
  ret i32 %s8
}
define i32 @main() {
entry:
${cells.map(p => `  ${p} = alloca i32, align 4`).join('\n')}
${cells.map((p, i) => `  store volatile i32 ${i + 1}, ptr ${p}, align 4`).join('\n')}
  %sum = call i32 @read_cells(${params.join(', ')})
  %status = xor i32 %sum, 45
  ret i32 %status
}`);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('frame.ll'), '-o', file('frame.s')]);
  assert.match(fs.readFileSync(file('frame.s'), 'utf8'), /@ frame-rematerialization: addresses [1-9]/);
  run(tool('badge-asm'), [file('frame.s'), '-o', file('owned.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-Oz', '-c', file('frame.ll'), '-o', file('llvm.o')]);
  for (const name of ['owned', 'llvm']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file(`${name}.o`), '-o', file('frame.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('frame.elf')]);
  }
});

test('fixed positive loops use a countdown only when the counter is unobserved', t => {
  const file = files(t);
  const loop = (name, limit, value) => `define i32 @${name}(i32 %seed) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %acc = phi i32 [%seed, %entry], [%sum, %body]
  %go = icmp ult i32 %i, ${limit}
  br i1 %go, label %body, label %done
body:
  %sum = add i32 %acc, ${value}
  %next = add i32 %i, 1
  br label %header
done:
  ret i32 %acc
}`;
  fs.writeFileSync(file('count.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
${loop('fixed', 3, '7')}
${loop('zero', 0, '7')}
${loop('observed', 3, '%i')}
define i32 @main() {
entry:
  %a = call i32 @fixed(i32 9)
  %b = call i32 @zero(i32 9)
  %c = call i32 @observed(i32 9)
  %x = xor i32 %a, 30
  %y = xor i32 %b, 9
  %z = xor i32 %c, 12
  %xy = or i32 %x, %y
  %status = or i32 %xy, %z
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('count.ll'), '-o', file('count.s')]);
  const assembly = fs.readFileSync(file('count.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('fixed'), /@ loop-countdown:/);
  assert.doesNotMatch(body('zero') + body('observed'), /@ loop-countdown:/);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('count.s'), '-o', file('count.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('count.o'), '-o', file('count.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('count.elf')]);
});

test('fixed halfword fills keep invariants in registers and use bounded writeback', t => {
  const file = files(t);
  const loop = (name, trips, qualifier) => `define void @${name}(i16 %color) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %latch]
  %go = icmp ult i32 %i, ${trips}
  br i1 %go, label %body, label %done
body:
  %offset = mul i32 %i, 2
  %dst = getelementptr i8, ptr @pixels, i32 %offset
  store ${qualifier}i16 %color, ptr %dst, align 2
  br label %latch
latch:
  %next = add i32 %i, 1
  br label %header
done:
  ret void
}`;
  fs.writeFileSync(file('fill.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@pixels = internal global [10 x i16] zeroinitializer, align 2
${loop('fill', 9, '')}
${loop('zero_fill', 0, '')}
${loop('volatile_fill', 9, 'volatile ')}
define i32 @main() {
entry:
  call void @fill(i16 4660)
  call void @zero_fill(i16 0)
  %low = load i16, ptr @pixels, align 2
  %tail = getelementptr i8, ptr @pixels, i32 16
  %high = load i16, ptr %tail, align 2
  %end = getelementptr i8, ptr @pixels, i32 18
  %guard = load i16, ptr %end, align 2
  %a = xor i16 %low, 4660
  %b = xor i16 %high, 4660
  %c = or i16 %a, %b
  %d = or i16 %c, %guard
  %status = zext i16 %d to i32
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('fill.ll'), '-o', file('fill.s')]);
  const assembly = fs.readFileSync(file('fill.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('fill'), /strh\.w r2, \[r1\], #2\nsubs\.n r0, #1\nbne\.n/);
  assert.doesNotMatch(body('zero_fill') + body('volatile_fill'), /@ fixed-halfword-fill:/);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('fill.s'), '-o', file('fill.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('fill.o'), '-o', file('fill.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('fill.elf')]);
});

test('paired transfers require suitable stack RAM and preserve word order and guard bytes', t => {
  const file = files(t);
  const source = `target triple = "thumbv8m.main-unknown-none-eabi"
declare void @llvm.memcpy.inline.p0.p0.i32(ptr, ptr, i32, i1)
declare void @llvm.memset.inline.p0.i32(ptr, i8, i32, i1)
define i64 @unknown(ptr %p) {
entry:
  %v = load i64, ptr %p, align 4
  ret i64 %v
}
define i64 @ordered() {
entry:
  %s = alloca i64, align 8
  store volatile i64 1234605616436508552, ptr %s, align 8
  %v = load volatile i64, ptr %s, align 8
  ret i64 %v
}
define i32 @main() {
entry:
  %a = alloca [32 x i8], align 4
  %b = alloca [32 x i8], align 4
  call void @llvm.memset.inline.p0.i32(ptr %a, i8 0, i32 32, i1 false)
  store i64 1234605616436508552, ptr %a, align 4
  call void @llvm.memset.inline.p0.i32(ptr %b, i8 0, i32 32, i1 false)
  %guard = getelementptr i8, ptr %b, i32 24
  store i32 79, ptr %guard, align 4
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %b, ptr %a, i32 24, i1 false)
  %q = load i64, ptr %b, align 4
  %g = load i32, ptr %guard, align 4
  %good0 = icmp eq i64 %q, 1234605616436508552
  %good1 = icmp eq i32 %g, 79
  %ok = and i1 %good0, %good1
  %bad = xor i1 %ok, true
  %status = zext i1 %bad to i32
  ret i32 %status
}`;
  fs.writeFileSync(file('pair.ll'), source);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    '--memory-selection=word', file('pair.ll'), '-o', file('pair.s')]);
  const assembly = fs.readFileSync(file('pair.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('main'), /strd r0, r1, \[(?:r7|sp), #/);
  assert.match(body('main'), /ldrd r0, r1, \[(?:r7|sp), #/);
  assert.match(body('main'), /ldm r5!, \{r0, r1\}\nstm r4!, \{r0, r1\}/);
  assert.doesNotMatch(body('unknown') + body('ordered'), /\b(?:ldrd|strd|ldm|stm)\b/);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('pair.s'), '-o', file('pair.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('pair.s'), '-o', file('pair-clang.o')]);
  const section = name => decodeElf(fs.readFileSync(file(name))).sections.find(s => s.name === '.text.main').data;
  assert.deepEqual(section('pair.o'), section('pair-clang.o'));
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('pair.o'), '-o', file('pair.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('pair.elf')]);
});

test('tiny byte fills preserve byte accesses and in-block div/rem shares one divide', t => {
  const file = files(t);
  const fill = (name, trips, qualifier) => `define void @${name}(ptr %p, i8 %color) {
entry:
  br label %head
head:
  %i = phi i32 [0, %entry], [%next, %latch]
  %go = icmp ult i32 %i, ${trips}
  br i1 %go, label %body, label %done
body:
  %dst = getelementptr i8, ptr %p, i32 %i
  store ${qualifier}i8 %color, ptr %dst, align 1
  br label %latch
latch:
  %next = add i32 %i, 1
  br label %head
done:
  ret void
}`;
  fs.writeFileSync(file('shape.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
@data = internal global [8 x i8] zeroinitializer, align 1
@sink = internal global i32 0, align 4
${fill('fill', 6, '')}
${fill('ordered', 6, 'volatile ')}
${fill('too_many', 9, '')}
define i32 @digits(i32 %n) {
entry:
  %r = urem i32 %n, 10
  %ascii = add i32 %r, 48
  %q = udiv i32 %n, 10
  %sum = add i32 %ascii, %q
  ret i32 %sum
}
define i32 @separated(i32 %n) {
entry:
  %r = urem i32 %n, 10
  store volatile i32 %r, ptr @sink, align 4
  %q = udiv i32 %n, 10
  ret i32 %q
}
define i32 @main() {
entry:
  call void @fill(ptr @data, i8 32)
  %a = load i8, ptr @data, align 1
  %last = getelementptr i8, ptr @data, i32 5
  %b = load i8, ptr %last, align 1
  %after = getelementptr i8, ptr @data, i32 6
  %guard = load i8, ptr %after, align 1
  %quot = call i32 @digits(i32 12345)
  %ac = icmp eq i8 %a, 32
  %bc = icmp eq i8 %b, 32
  %gc = icmp eq i8 %guard, 0
  %qc = icmp eq i32 %quot, 1287
  %ab = and i1 %ac, %bc
  %gq = and i1 %gc, %qc
  %good = and i1 %ab, %gq
  %bad = xor i1 %good, true
  %status = zext i1 %bad to i32
  ret i32 %status
}`);
  run(tool('badge-opt-sanitize'), ['--passes=divrem-pairs', file('shape.ll'), '-o', file('paired.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('paired.ll')]);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('paired.ll'), '-o', file('shape.s')]);
  const asm = fs.readFileSync(file('shape.s'), 'utf8');
  const body = name => asm.slice(asm.indexOf(`\n${name}:`), asm.indexOf(`.size ${name},`));
  assert.match(body('fill'), /@ fixed-byte-fill:/);
  assert.equal([...body('fill').matchAll(/^strb\.n /gm)].length, 6);
  assert.doesNotMatch(body('ordered') + body('too_many'), /@ fixed-byte-fill:/);
  assert.equal([...body('digits').matchAll(/^udiv /gm)].length, 1);
  assert.equal([...body('separated').matchAll(/^udiv /gm)].length, 2);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('shape.s'), '-o', file('shape.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('shape.o'), '-o', file('shape.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('shape.elf')]);
});

test('bounded loops hoist immutable literals but retain loads across calls or clobbers', t => {
  const file = files(t);
  const loop = (name, update, result) => `define i32 @${name}(i32 %n) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %sum = phi i32 [7, %entry], [${result}, %body]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %body, label %done
body:
  %a = add i32 %sum, 305419896
${update}
  %next = add i32 %i, 1
  br label %header
done:
  ret i32 %sum
}`;
  fs.writeFileSync(file('pool.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define void @effect() {
entry:
  ret void
}
${loop('hoist', '', '%a')}
${loop('clobber', '  %b = sub i32 %a, 3180503887', '%b')}
${loop('call_barrier', '  call void @effect()', '%a')}
define i32 @main() {
entry:
  %zero = call i32 @hoist(i32 0)
  %three = call i32 @hoist(i32 3)
  %changed = call i32 @clobber(i32 3)
  %called = call i32 @call_barrier(i32 3)
  %a = xor i32 %zero, 7
  %b = xor i32 %three, 916259695
  %c = xor i32 %changed, ${(7 + 3 * (305419896 - 3180503887)) >>> 0}
  %d = xor i32 %called, 916259695
  %ab = or i32 %a, %b
  %cd = or i32 %c, %d
  %status = or i32 %ab, %cd
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('pool.ll'), '-o', file('pool.s')]);
  const assembly = fs.readFileSync(file('pool.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('hoist'), /ldr\.n r\d+, \.Lcg_shared_\d+_\d+\n@ loop-pool:.*\n\.Lcg_\d+_header:/);
  for (const name of ['clobber', 'call_barrier']) {
    assert.match(body(name), /ldr\.[nw].*\.Lcg_shared_/);
    assert.doesNotMatch(body(name), /@ loop-pool:/);
  }
  run(tool('badge-asm'), [file('pool.s'), '-o', file('pool.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('pool.o'), '-o', file('pool.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('pool.elf')]);
});

test('repeated symbol addresses share one unreachable literal with both assemblers', t => {
  const file = files(t);
  fs.writeFileSync(file('pool.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@cell = global [2 x i32] zeroinitializer, align 4
define internal void @set(ptr %p) {
entry:
  store volatile i32 47, ptr %p, align 4
  ret void
}
define i32 @main() {
entry:
  %p = getelementptr i8, ptr @cell, i32 4
  store volatile i32 13, ptr %p, align 4
  call void @set(ptr %p)
  %q = getelementptr i8, ptr @cell, i32 4
  %v = load volatile i32, ptr %q, align 4
  %status = xor i32 %v, 47
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('pool.ll'), '-o', file('pool.s')]);
  const assembly = fs.readFileSync(file('pool.s'), 'utf8');
  assert.equal((assembly.match(/ldr\.[nw] r\d+, \.Lcg_shared_/g) ?? []).length, 2);
  assert.match(assembly, /pop\.[wn] \{[^\n]*pc\}\n(?:@ [^\n]*\n)*\.p2align 2\n\.Lcg_shared_/);
  assert.equal((assembly.match(/\.word cell\+0/g) ?? []).length, 1);
  run(tool('badge-asm'), [file('pool.s'), '-o', file('owned.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('pool.s'), '-o', file('llvm.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const name of ['owned', 'llvm']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file(`${name}.o`), '-o', file('pool.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('pool.elf')]);
  }
});

test('signed halfword and widening products agree with LLVM at signed boundaries', t => {
  const file = files(t);
  const pairs = [[-32768, -32768], [-32768, 32767], [-1, 32767], [0, -1], [1, 1]];
  const tests = pairs.flatMap(([a, b]) => [[16, 32, a, b], [32, 64, a * 65536, b * 65536]]);
  fs.writeFileSync(file('product.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @half(i16 signext %a, i16 signext %b) {
entry:
  %x = sext i16 %a to i32
  %y = sext i16 %b to i32
  %p = mul i32 %x, %y
  ret i32 %p
}
define i64 @wide(i32 %a, i32 %b) {
entry:
  %x = sext i32 %a to i64
  %y = sext i32 %b to i64
  %p = mul i64 %x, %y
  ret i64 %p
}
define i64 @kept(i32 %a, i32 %b, ptr %out) {
entry:
  %x = sext i32 %a to i64
  %y = sext i32 %b to i64
  %p = mul i64 %x, %y
  store i64 %x, ptr %out, align 8
  ret i64 %p
}
@wide_value = global i64 0, align 8
define i32 @main() {
entry:
  %initial = call i64 @kept(i32 -1, i32 5, ptr @wide_value)
  %sign = load volatile i64, ptr @wide_value, align 8
  %failure0 = icmp ne i64 %sign, -1
${tests.map(([input, output, a, b], i) => `  %result${i} = call i${output} @${input === 16 ? 'half' : 'wide'}(i${input} ${a}, i${input} ${b})
  %bad${i} = icmp ne i${output} %result${i}, ${BigInt(a) * BigInt(b)}
  %failure${i + 1} = or i1 %failure${i}, %bad${i}`).join('\n')}
  %status = zext i1 %failure${tests.length} to i32
  ret i32 %status
}`);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('product.ll'), '-o', file('product.s')]);
  const assembly = fs.readFileSync(file('product.s'), 'utf8');
  assert.match(assembly, /^smulbb /m);
  assert.equal((assembly.match(/^smull /gm) ?? []).length, 2);
  run(tool('badge-asm'), [file('product.s'), '-o', file('owned.o')]);
  run(clang, [...flags, '-c', file('product.s'), '-o', file('encoding.o')]);
  run(clang, [...flags, '-Oz', '-c', file('product.ll'), '-o', file('llvm.o')]);
  const halfText = name => decodeElf(fs.readFileSync(file(`${name}.o`))).sections
    .find(s => s.name === '.text.half').data;
  assert.deepEqual(halfText('owned'), halfText('encoding'));
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const name of ['owned', 'llvm']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file(`${name}.o`), '-o', file('product.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('product.elf')]);
  }
});

test('direct comparisons preserve narrow sign boundaries and materialized boolean results', t => {
  const file = files(t);
  fs.writeFileSync(file('compare.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i1 @byte_eq(i8 %x) {
entry:
  %result = icmp eq i8 -1, %x
  ret i1 %result
}
define i32 @half_unsigned(i16 %a, i16 %b) {
entry:
  %result = icmp ult i16 %a, %b
  br i1 %result, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}
define i1 @half_signed(i16 %a, i16 %b) {
entry:
  %result = icmp slt i16 %a, %b
  ret i1 %result
}
define i32 @main() {
entry:
  %a = call i1 @byte_eq(i8 255)
  %b = call i1 @byte_eq(i8 127)
  %c = call i32 @half_unsigned(i16 -1, i16 1)
  %d = call i1 @half_signed(i16 -1, i16 1)
  %good = and i1 %a, %d
  %bad0 = xor i1 %good, true
  %bad1 = or i1 %bad0, %b
  %bad2 = zext i1 %bad1 to i32
  %status = or i32 %bad2, %c
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('compare.ll'), '-o', file('compare.s')]);
  assert.match(fs.readFileSync(file('compare.s'), 'utf8'), /cmp\.[nw] r\d+, #255\b/);
  run(tool('badge-asm'), [file('compare.s'), '-o', file('owned.o')]);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(clang, [...flags, '-c', file('compare.s'), '-o', file('independent.o')]);
  run(clang, [...flags, '-Oz', '-c', file('compare.ll'), '-o', file('llvm.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const name of ['owned', 'independent', 'llvm']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file(`${name}.o`), '-o', file('compare.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('compare.elf')]);
  }
});

test('signed loads keep one volatile access and fall back for reused or unaligned loads', t => {
  const file = files(t);
  fs.writeFileSync(file('load.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@byte = global i8 -1
@half = global i16 -32768, align 2
define i32 @main() {
entry:
  %a = load volatile i8, ptr @byte, align 1
  %x = sext i8 %a to i32
  %b = load i16, ptr @half, align 2
  %y = sext i16 %b to i32
  %z = zext i16 %b to i32
  %c = load i16, ptr @half, align 1
  %w = sext i16 %c to i32
  %xcheck = xor i32 %x, -1
  %ycheck = xor i32 %y, -32768
  %zcheck = xor i32 %z, 32768
  %wcheck = xor i32 %w, -32768
  %first = or i32 %xcheck, %ycheck
  %second = or i32 %zcheck, %wcheck
  %status = or i32 %first, %second
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('load.ll'), '-o', file('load.s')]);
  const assembly = fs.readFileSync(file('load.s'), 'utf8');
  assert.equal((assembly.match(/^ldrsb\.w /gm) ?? []).length, 1);
  assert.doesNotMatch(assembly, /^ldrsh\.w /m);
  run(tool('badge-asm'), [file('load.s'), '-o', file('load.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('load.o'), '-o', file('load.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('load.elf')]);
});

test('shifted operands fold without changing multi-use or reversed subtraction', t => {
  const file = files(t);
  fs.writeFileSync(file('shift.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @case(i32 %a, i32 %b) {
entry:
  %ls = shl i32 %a, 13
  %x = xor i32 %ls, %a
  %rs = lshr i32 %b, 31
  %y = add i32 %x, %rs
  %as = ashr i32 %b, 24
  %z = sub i32 %y, %as
  %keep = shl i32 %a, 1
  %first = xor i32 %z, %keep
  %second = sub i32 %keep, %first
  %reverse = lshr i32 %b, 17
  %result = sub i32 %reverse, %second
  ret i32 %result
}
define i32 @masked_shift(i32 %x) {
entry:
  %hi = lshr i32 %x, 7
  %not = xor i32 %x, -1
  %bits = and i32 %hi, 63519
  %r = xor i32 %bits, %not
  ret i32 %r
}
define i32 @main() {
entry:
  %actual = call i32 @case(i32 3, i32 -1)
  %a = xor i32 %actual, 57340
  %shifted = call i32 @masked_shift(i32 305419896)
  %b = xor i32 %shifted, ${(((305419896 >>> 7) & 63519) ^ ~305419896) >>> 0}
  %status = or i32 %a, %b
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('shift.ll'), '-o', file('shift.s')]);
  const assembly = fs.readFileSync(file('shift.s'), 'utf8');
  assert.match(assembly, /^eor\.w .* lsl #13$/m);
  assert.match(assembly, /^add\.w .* lsr #31$/m);
  assert.match(assembly, /^sub\.w .* asr #24$/m);
  assert.match(assembly, /^lsls?\.([wn]) .*#1$/m);
  assert.match(assembly, /^lsrs?\.([wn]) .*#17$/m);
  assert.match(assembly, /^and\.w .* lsr #7$/m);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('shift.s'), '-o', file('shift.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('shift.o'), '-o', file('shift.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('shift.elf')]);
});

test('dense Thumb switch tables preserve holes, defaults, and PHI edge copies', t => {
  const file = files(t);
  const cases = [0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12];
  const group = n => n === 8 ? 8 : n % 2;
  const target = n => `case${group(n)}`;
  const blocks = [...new Set(cases.map(group))];
  fs.writeFileSync(file('switch.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @table(i32 %x, i32 %y) {
entry:
  switch i32 %x, label %missing [
${cases.map(n => `    i32 ${n}, label %${target(n)}`).join('\n')}
  ]
${blocks.map(n => `case${n}:\n  %v${n} = phi i32 ${cases.filter(k => group(k) === n).map(() => '[ %y, %entry ]').join(', ')}\n  %r${n} = add i32 %v${n}, ${n + 3}\n  ret i32 %r${n}`).join('\n')}
missing:
  %fallback = phi i32 [ %y, %entry ]
  %result = sub i32 %fallback, 9
  ret i32 %result
}`);
  fs.writeFileSync(file('caller.c'), `
extern unsigned table(unsigned, unsigned);
int main(void) {
  for (unsigned i = 0; i < 20; i++) {
    unsigned n = i < 16 ? i : ~i, x = 0xfffffffau + i;
    unsigned expected = n <= 12 && n != 7 ? x + (n == 8 ? 8 : n % 2) + 3 : x - 9;
    if (table(n, x) != expected) return 1;
  }
  return 0;
}`);
  run(tool('badge-codegen-sanitize'), [file('switch.ll'), '-o', file('table.s')]);
  assert.match(fs.readFileSync(file('table.s'), 'utf8'), /^tbb \[pc, r\d+\]$/m);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const targetFlags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
  run(tool('badge-asm'), [file('table.s'), '-o', file('table.o')]);
  run(clang, [...targetFlags, '-c', file('table.s'), '-o', file('llvm.o')]);
  run(clang, [...targetFlags, '-O2', '-ffreestanding', '-c', file('caller.c'), '-o', file('caller.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const role of ['table', 'llvm']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file('caller.o'), file(`${role}.o`), '-o', file(`${role}.elf`)]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file(`${role}.elf`)]);
  }
});

test('compact switches preserve PHI edges and fold indexed narrow loads only when aligned', t => {
  const file = files(t);
  fs.writeFileSync(file('dispatch.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@table = constant [4 x i16] [i16 7, i16 257, i16 1024, i16 0], align 2
define i32 @dispatch(i32 %x, i32 %index) {
entry:
  switch i32 %x, label %fallback [
    i32 7, label %hit
    i32 305419896, label %join
  ]
hit:
  %addr = getelementptr i8, ptr @table, i32 %index
  %byte = load volatile i16, ptr %addr, align 2
  %value = zext i16 %byte to i32
  br label %join
fallback:
  br label %join
join:
  %result = phi i32 [19, %entry], [%value, %hit], [5, %fallback]
  ret i32 %result
}
define i32 @unaligned(i32 %index) {
entry:
  %addr = getelementptr i8, ptr @table, i32 %index
  %half = load i16, ptr %addr, align 1
  %value = zext i16 %half to i32
  ret i32 %value
}
define i32 @main() {
entry:
  %a = call i32 @dispatch(i32 7, i32 2)
  %b = call i32 @dispatch(i32 305419896, i32 0)
  %c = call i32 @dispatch(i32 0, i32 0)
  %d = call i32 @unaligned(i32 1)
  %x = xor i32 %a, 257
  %y = xor i32 %b, 19
  %z = xor i32 %c, 5
  %w = xor i32 %d, 256
  %t = or i32 %x, %y
  %u = or i32 %z, %w
  %code = or i32 %t, %u
  ret i32 %code
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('dispatch.ll'), '-o', file('dispatch.s')]);
  const assembly = fs.readFileSync(file('dispatch.s'), 'utf8');
  assert.match(assembly, /^cmp\.[nw] r\d+, #7$/m);
  assert.match(assembly, /^ldrh\.[nw] r\d+, \[r\d+, r\d+\]$/m);
  assert.match(assembly, /^\.word 305419896$/m);
  assert.doesNotMatch(assembly.slice(assembly.indexOf('\nunaligned:'), assembly.indexOf('.size unaligned,')), /^ldrh/m);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('dispatch.s'), '-o', file('dispatch.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('dispatch.o'), '-o', file('dispatch.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('dispatch.elf')]);
});

test('straight-line leaf reuse removes saves and narrows instructions without crossing assembly effects', t => {
  const file = files(t);
  const seed = 0x12345678, low = seed ^ 41;
  const expected = ((low ^ (low << 13)) + (seed >>> 3)) >>> 0;
  fs.writeFileSync(file('leaf.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @leaf(i32 %x) {
entry:
  br label %body
body:
  %low = xor i32 %x, 41
  %upper = lshr i32 %x, 3
  %shift = shl i32 %low, 13
  %mixed = xor i32 %low, %shift
  %sum = add i32 %mixed, %upper
  ret i32 %sum
}
define i32 @leaf_with_asm(i32 %x) {
entry:
  %low = xor i32 %x, 41
  %upper = lshr i32 %x, 3
  call void asm sideeffect "nop", "~{cc}"()
  %shift = shl i32 %low, 13
  %mixed = xor i32 %low, %shift
  %sum = add i32 %mixed, %upper
  ret i32 %sum
}
define i32 @main() {
entry:
  %a = call i32 @leaf(i32 ${seed})
  %b = call i32 @leaf_with_asm(i32 ${seed})
  %x = xor i32 %a, ${expected}
  %y = xor i32 %b, ${expected}
  %status = or i32 %x, %y
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('leaf.ll'), '-o', file('leaf.s')]);
  const assembly = fs.readFileSync(file('leaf.s'), 'utf8');
  const body = name => assembly.slice(assembly.indexOf(`\n${name}:`), assembly.indexOf(`.size ${name},`));
  assert.match(body('leaf'), /leaf-registers: entry\/return copies removed 2/);
  assert.doesNotMatch(body('leaf'), /^p(?:ush|op)|^mov r\d+, r0|^mov r0, r\d+/m);
  assert.match(body('leaf'), /^lsrs\.n r\d+, r\d+, #3$/m);
  assert.match(body('leaf'), /^eor\.w .* lsl #13$/m);
  assert.doesNotMatch(body('leaf_with_asm'), /leaf-(?:registers|widths):/);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('leaf.s'), '-o', file('leaf.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('leaf.o'), '-o', file('leaf.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('leaf.elf')]);
});

test('shifted multiply-adds and direct comparisons preserve arithmetic and all signed/unsigned predicates', t => {
  const file = files(t);
  let ir = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
    'target triple = "thumbv8m.main-unknown-none-eabi"\n', oracle = '';
  for (let shift = 0; shift < 32; shift++) {
    const scale = 2 ** shift;
    for (const reversed of [false, true]) {
      const name = `scale_${shift}_${Number(reversed)}`;
      ir += `define i32 @${name}(i32 %x, i32 %base) {
entry:
  %product = mul i32 ${reversed ? `${scale}, %x` : `%x, ${scale}`}
  %sum = add i32 ${reversed ? '%product, %base' : '%base, %product'}
  ret i32 %sum
}\n`;
      oracle += `extern unsigned ${name}(unsigned, unsigned);\n`;
    }
  }
  let checks = '';
  const predicates = {
    eq: (a, b) => a === b, ne: (a, b) => a !== b,
    ult: (a, b) => a < b, ule: (a, b) => a <= b,
    ugt: (a, b) => a > b, uge: (a, b) => a >= b,
    slt: (a, b) => (a | 0) < (b | 0), sle: (a, b) => (a | 0) <= (b | 0),
    sgt: (a, b) => (a | 0) > (b | 0), sge: (a, b) => (a | 0) >= (b | 0),
  };
  for (const [pred, evaluate] of Object.entries(predicates))
    for (const n of [0, 1, 255, 256, 32768, 0x80000000, 0xffffffff, 0x12345678]) {
      const name = `compare_${pred}_${n}`;
      ir += `define i32 @${name}(i32 %x) {
entry:
  %condition = icmp ${pred} i32 %x, ${n}
  br i1 %condition, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}\n`;
      oracle += `extern unsigned ${name}(unsigned);\n`;
      for (const x of [0, n, (n - 1) >>> 0, (n + 1) >>> 0, 0xffffffff, 0x80000000])
        checks += `if (${name}(${x}u) != ${Number(evaluate(x, n))}u) return 1;\n`;
    }
  oracle += 'int main(void) {\n' + checks;
  for (let shift = 0; shift < 32; shift++)
    for (const reversed of [false, true])
      for (const [x, base] of [[0, 0], [1, 7], [0xffffffff, 0x80000001], [0x12345678, 0xfedcba98]]) {
        const expected = Number((BigInt(x) * (1n << BigInt(shift)) + BigInt(base)) & 0xffffffffn);
        oracle += `if (scale_${shift}_${Number(reversed)}(${x}u, ${base}u) != ${expected}u) return 1;\n`;
      }
  oracle += 'return 0;\n}\n';
  fs.writeFileSync(file('input.ll'), ir);
  fs.writeFileSync(file('oracle.c'), oracle);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-Oz', '-ffreestanding', '-fno-builtin', '-c', file('oracle.c'), '-o', file('oracle.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const residency of ['global-compact', 'local', 'stack']) {
    run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
      `--register-residency=${residency}`, file('input.ll'), '-o', file('candidate.s')]);
    if (residency === 'global-compact')
      assert.match(fs.readFileSync(file('candidate.s'), 'utf8'), /add\.w .*lsl #31/);
    run(tool('badge-asm'), [file('candidate.s'), '-o', file('candidate.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file('oracle.o'), file('candidate.o'), '-o', file('candidate.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('candidate.elf')]);
  }
});

test('boolean PHI edge forwarding removes only branch-only single-use joins', t => {
  const file = files(t);
  const header = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
    'target triple = "thumbv8m.main-unknown-none-eabi"\n';
  const make = (name, extra = '', use = '', dest = '') => `define i32 @${name}(i32 %x) {
entry:
  %first = icmp eq i32 %x, 29
  br i1 %first, label %constant, label %other
constant:
  br label %join
other:
  %second = icmp eq i32 %x, 40
  br label %join
join:
  %choice = phi i1 [true, %constant], [%second, %other]
  ${extra}
  br i1 %choice, label %yes, label %no
yes:
  ${dest}
  ret i32 ${use || '1'}
no:
  ret i32 0
}\n`;
  fs.writeFileSync(file('input.ll'), header +
    make('forward') +
    make('multiuse', '%extended = zext i1 %choice to i32', '%extended') +
    make('sideeffect', 'call void @effect()') +
    make('destination_phi', '', '', '%keep = phi i32 [7, %join]') + `
define i32 @wrapper(i32 %x) {
entry:
  %condition = icmp ne i32 %x, 0
  br i1 %condition, label %bridge, label %no
bridge:
  br label %yes
yes:
  ret i32 1
no:
  ret i32 0
}
define i32 @loop(i32 %x) {
entry:
  br label %head
head:
  %n = phi i32 [%x, %entry], [%next, %back]
  %condition = icmp ne i32 %n, 0
  br i1 %condition, label %body, label %exit
body:
  %next = sub i32 %n, 1
  br label %back
back:
  br label %head
exit:
  ret i32 %n
}
define i32 @constant_false() {
entry:
  br i1 false, label %yes, label %no
yes:
  ret i32 1
no:
  ret i32 0
}
`);
  fs.writeFileSync(file('oracle.c'), `
static volatile unsigned calls;
void effect(void) { calls++; }
extern unsigned forward(unsigned), multiuse(unsigned), sideeffect(unsigned), destination_phi(unsigned);
extern unsigned wrapper(unsigned), loop(unsigned), constant_false(void);
int main(void) {
  unsigned i;
  for (i = 0; i < 100; i++) {
    unsigned expected = i == 29 || i == 40;
    if (forward(i) != expected || multiuse(i) != expected ||
        sideeffect(i) != expected || destination_phi(i) != expected ||
        wrapper(i) != (i != 0) || loop(i) != 0 || constant_false() != 0) return 1;
  }
  return calls != 100;
}`);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-Oz', '-ffreestanding', '-fno-builtin', '-c', file('oracle.c'), '-o', file('oracle.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const residency of ['global-compact', 'local', 'stack'])
    for (const policy of ['thumb-widths', 'thumb-constants']) {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`,
        `--register-residency=${residency}`, file('input.ll'), '-o', file('output.s')]);
      const assembly = fs.readFileSync(file('output.s'), 'utf8');
      assert.equal(assembly.includes('.Lcg_1_join:'), policy === 'thumb-widths');
      for (const n of [2, 3, 4]) assert(assembly.includes(`.Lcg_${n}_join:`));
      assert.equal(assembly.includes('.Lcg_5_bridge:'), policy === 'thumb-widths');
      assert(assembly.includes('.Lcg_6_back:'));
      run(tool('badge-asm'), [file('output.s'), '-o', file('output.o')]);
      run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
        file('start.o'), file('oracle.o'), file('output.o'), '-o', file('output.elf')]);
      run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
        '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
        '-kernel', file('output.elf')]);
    }
});
