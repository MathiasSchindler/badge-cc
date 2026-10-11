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
