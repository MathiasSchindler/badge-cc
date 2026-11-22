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

test('direct scaled and frame addresses preserve indexed volatile accesses', t => {
  const file = files(t);
  fs.writeFileSync(file('address.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
@words = global [4 x i32] [i32 11, i32 20, i32 31, i32 47], align 4
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
  %x = xor i32 %a, 20
  %y = xor i32 %b, 1241
  %status = or i32 %x, %y
  ret i32 %status
}`);
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-constants',
    file('address.ll'), '-o', file('address.s')]);
  const assembly = fs.readFileSync(file('address.s'), 'utf8');
  const indexed = assembly.slice(assembly.indexOf('\nindexed:'), assembly.indexOf('.size indexed'));
  assert.match(indexed, /add\.w r\d+, r(?:[3-6]|8|10|11), r\d+, lsl #2/);
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

test('adjacent shifted operands fold without changing multi-use or reversed subtraction', t => {
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
define i32 @main() {
entry:
  %actual = call i32 @case(i32 3, i32 -1)
  %status = xor i32 %actual, 57340
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
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('shift.s'), '-o', file('shift.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file('shift.o'), '-o', file('shift.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('shift.elf')]);
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
