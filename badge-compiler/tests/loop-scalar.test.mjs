import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly } from '../tools/benchmark-c06.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 60000, maxBuffer: 16 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stdout}\n${r.stderr}`);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/loop-scalar-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
function optimize(file, source) {
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
    'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' + source);
  run(tool('badge-opt-sanitize'), ['--passes=loop-scalar', file('input.ll'), '-o', file('candidate.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
  const result = fs.readFileSync(file('candidate.ll'), 'utf8');
  run(tool('badge-opt-sanitize'), ['--passes=loop-scalar', file('candidate.ll'), '-o', file('twice.ll')]);
  assert.equal(fs.readFileSync(file('twice.ll'), 'utf8'), result, 'candidate must be idempotent');
  return result;
}
function execute(file, ir, role, trace = false) {
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-operands', ir, '-o', file(`${role}.s`)]);
  const assembly = fs.readFileSync(file(`${role}.s`), 'utf8');
  const frames = parseAssembly(assembly, role);
  run(tool('badge-asm'), [file(`${role}.s`), '-o', file(`${role}.o`)]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file(`${role}.o`), '-o', file(`${role}.elf`)]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${role}.elf`),
    ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', file(`${role}.trace`)] : [])]);
  let count = null;
  if (trace) {
    count = 0;
    const fd = fs.openSync(file(`${role}.trace`), 'r'), buffer = Buffer.alloc(65536);
    let size, tail = '';
    try {
      while ((size = fs.readSync(fd, buffer, 0, buffer.length, null))) {
        const text = tail + buffer.toString('utf8', 0, size), end = text.lastIndexOf('\n');
        count += (text.slice(0, end + 1).match(/^Trace /gm) ?? []).length;
        tail = text.slice(end + 1);
      }
      if (tail.startsWith('Trace ')) count++;
    } finally { fs.closeSync(fd); }
  }
  return { frames, count };
}
const loop = expression => `@sink = global i32 0
define i32 @f(i32 %x, i32 %n) {
entry:
 br label %header
header:
 %i = phi i32 [%n, %entry], [%next, %body]
 %go = icmp ne i32 %i, 0
 br i1 %go, label %body, label %exit
body:
 %v = ${expression}
 store volatile i32 %v, ptr @sink
 %next = sub i32 %i, 1
 br label %header
exit:
 ret i32 0
}\n`;
test('bounded pure shift/mask motion preserves zero-trip, calls and exit effects and reduces reached instructions', t => {
  const file = files(t);
  const source = loop('lshr i32 %x, 8') + `define i32 @main() {
entry:
 store volatile i32 123, ptr @sink
 %zero = call i32 @f(i32 4294967295, i32 0)
 %before = load volatile i32, ptr @sink
 %many = call i32 @f(i32 2147483648, i32 1024)
 %after = load volatile i32, ptr @sink
 %ok1 = icmp eq i32 %before, 123
 %ok2 = icmp eq i32 %after, 8388608
 %all = and i1 %ok1, %ok2
 br i1 %all, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert(result.indexOf('%v = lshr') < result.indexOf('header:'));
  const baseline = execute(file, file('input.ll'), 'control', true);
  const candidate = execute(file, file('candidate.ll'), 'candidate', true);
  assert(candidate.count > 0 && candidate.count < baseline.count);
  assert(candidate.frames.get('f').frame_bytes <= baseline.frames.get('f').frame_bytes);
  console.log(`PO15 shift oracle: ${baseline.count} -> ${candidate.count} reached instructions`);
});
test('unflagged modular induction preserves strided accesses, wrapping values and counters after early exits', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define i32 @f(i32 %n) {
entry:
 br label %header
header:
 %i = phi i32 [4294965248, %entry], [%next, %body]
 %counter = phi i32 [%n, %entry], [%decrement, %body]
 %go = icmp ne i32 %counter, 0
 br i1 %go, label %body, label %exit
body:
 %v = mul i32 %i, 7
 store volatile i32 %v, ptr @sink
 %next = add i32 %i, 2
 %decrement = sub i32 %counter, 1
 br label %header
exit:
 ret i32 %counter
}
define i32 @main() {
entry:
 %counter = call i32 @f(i32 1026)
 %r = load volatile i32, ptr @sink
 %okvalue = icmp eq i32 %r, 14
 %okcounter = icmp eq i32 %counter, 0
 %ok = and i1 %okvalue, %okcounter
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert.match(result, /%v = phi i32/);
  const before = execute(file, file('input.ll'), 'control', true);
  const after = execute(file, file('candidate.ll'), 'candidate', true);
  assert(after.frames.get('f').frame_bytes <= before.frames.get('f').frame_bytes);
  assert(after.count <= before.count);
});
test('traps, poison flags, unavailable operands, unknown effects and high pressure retain rematerialization', t => {
  const file = files(t);
  for (const expression of ['add nsw i32 %x, 7', 'udiv i32 %x, 7', 'shl i32 %x, 32',
    'shl i32 %x, %n', 'mul i32 %i, %x', 'and i32 %x, poison']) {
    const result = optimize(file, loop(expression));
    assert(result.indexOf(`%v = ${expression}`) > result.indexOf('header:'), expression);
  }
  const opaque = loop('lshr i32 %x, 8').replace('store volatile', 'call void asm sideeffect "nop", "~{memory}"()\n store volatile');
  const opaqueResult = optimize(file, opaque);
  assert(opaqueResult.indexOf('%v = lshr') > opaqueResult.indexOf('header:'));
  const pressure = loop('lshr i32 %x, 8').replace('%go =', '%extra = phi i32 [0, %entry], [%v, %body]\n %extra2 = phi i32 [0, %entry], [%v, %body]\n %go =');
  const pressureResult = optimize(file, pressure);
  assert(pressureResult.indexOf('%v = lshr') > pressureResult.indexOf('header:'));
});
test('actual graphics clear fills every production pixel without frame growth and reduces reached work', t => {
  const file = files(t);
  fs.writeFileSync(file('source.c'), `#include "${path.resolve(root, '../src/platform/badger/graphics.c')}"
static badger_canvas_t canvas;
int main(void) {
  badger_gfx_clear(&canvas, 0xf800);
  for (unsigned i = 0; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i)
    if (canvas.pixels[i] != 0xf800) return 1;
  badger_gfx_clear(&canvas, 0x001f);
  for (unsigned i = 0; i < BADGER_GFX_WIDTH * BADGER_GFX_HEIGHT; ++i)
    if (canvas.pixels[i] != 0x001f) return 2;
  return 0;
}`);
  run(tool('badge-cpp'), ['-I../src/shared', file('source.c'), '-o', file('source.i')]);
  run(tool('badge-ir'), ['--expression-lowering=ssa', file('source.i'), '-o', file('raw.ll')]);
  run(tool('badge-opt'), [file('raw.ll'), '-o', file('input.ll')]);
  run(tool('badge-opt-sanitize'), ['--passes=loop-scalar', file('input.ll'), '-o', file('candidate.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
  const before = execute(file, file('input.ll'), 'control', true);
  const after = execute(file, file('candidate.ll'), 'candidate', true);
  assert(after.count < before.count);
  assert(after.frames.get('badger_gfx_clear').frame_bytes <= before.frames.get('badger_gfx_clear').frame_bytes);
  assert(after.frames.get('badger_gfx_clear').saves_bytes <= before.frames.get('badger_gfx_clear').saves_bytes);
  console.log(`PO15 production fill oracle: ${before.count} -> ${after.count} reached instructions`);
});
test('zero/one trips, skipped stores and early exits preserve modular values and live exit counters', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define i32 @f(i32 %n) {
entry:
 br label %header
header:
 %i = phi i32 [0, %entry], [%next, %latch]
 %go = icmp ult i32 %i, %n
 br i1 %go, label %body, label %exit
body:
 %v = mul i32 %i, 7
 %odd = and i32 %i, 1
 %skip = icmp eq i32 %odd, 1
 br i1 %skip, label %check, label %store
store:
 store volatile i32 %v, ptr @sink
 br label %check
check:
 %stop = icmp eq i32 %i, 6
 br i1 %stop, label %exit, label %latch
latch:
 %next = add i32 %i, 1
 br label %header
exit:
 ret i32 %i
}
define i32 @main() {
entry:
 store volatile i32 99, ptr @sink
 %zero = call i32 @f(i32 0)
 %unchanged = load volatile i32, ptr @sink
 %one = call i32 @f(i32 1)
 %single = load volatile i32, ptr @sink
 %early = call i32 @f(i32 65536)
 %last = load volatile i32, ptr @sink
 %a = icmp eq i32 %zero, 0
 %b = icmp eq i32 %unchanged, 99
 %c = icmp eq i32 %one, 1
 %d = icmp eq i32 %single, 0
 %e = icmp eq i32 %early, 6
 %f = icmp eq i32 %last, 42
 %ab = and i1 %a, %b
 %cd = and i1 %c, %d
 %ef = and i1 %e, %f
 %abcd = and i1 %ab, %cd
 %ok = and i1 %abcd, %ef
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  optimize(file, source);
  execute(file, file('input.ll'), 'control');
  execute(file, file('candidate.ll'), 'candidate');
});
test('nested conditional loops, ordinary calls and scalar addresses preserve effects', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define void @set(i32 %x) {
entry:\nstore volatile i32 %x, ptr @sink\nret void\n}
define i32 @main() {
entry:
 br label %outer
outer:
 %i = phi i32 [0, %entry], [%next, %outerlatch]
 %more = icmp ult i32 %i, 3
 br i1 %more, label %innerpre, label %exit
innerpre:
 br label %inner
inner:
 %j = phi i32 [0, %innerpre], [%jn, %innerlatch]
 %again = icmp ult i32 %j, 4
 br i1 %again, label %body, label %outerlatch
body:
 %address = getelementptr i8, ptr @sink, i32 0
 %mask = and i32 %i, 255
 store volatile i32 %mask, ptr %address
 call void @set(i32 %mask)
 br label %innerlatch
innerlatch:
 %jn = add i32 %j, 1
 br label %inner
outerlatch:
 %next = add i32 %i, 1
 br label %outer
exit:
 %r = load volatile i32, ptr @sink
 %ok = icmp eq i32 %r, 2
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  optimize(file, source);
  execute(file, file('input.ll'), 'control');
  execute(file, file('candidate.ll'), 'candidate');
});
test('backend and loop work exhaustion fail explicitly without replacing existing output', t => {
  const file = files(t);
  optimize(file, loop('lshr i32 %x, 8'));
  for (const name of ['badge-opt-loop-budget', 'badge-opt-loop-admission-budget']) {
    fs.writeFileSync(file('protected.ll'), 'existing-output');
    const r = run(tool(name), ['--passes=loop-scalar', file('input.ll'), '-o', file('protected.ll')], false);
    assert.match(r.stderr, /loop-scalar.*work limit exceeded/);
    assert.equal(fs.readFileSync(file('protected.ll'), 'utf8'), 'existing-output');
  }
});
test('measured IR emission runs the same pipeline and rejects input/report aliases', t => {
  const file = files(t);
  optimize(file, loop('lshr i32 %x, 8'));
  run(tool('badge-ir-metrics'), ['--passes=loop-scalar', `--emit-ir=${file('measured.ll')}`,
    file('input.ll'), '-o', file('events.json')]);
  assert.equal(fs.readFileSync(file('measured.ll'), 'utf8'), fs.readFileSync(file('candidate.ll'), 'utf8'));
  const events = JSON.parse(fs.readFileSync(file('events.json'))).events;
  assert(events.some(e => e.reason === 'backend_admitted_function'));
  for (const alias of ['input.ll', 'events.json']) {
    const before = fs.readFileSync(file(alias));
    run(tool('badge-ir-metrics'), ['--passes=loop-scalar', `--emit-ir=${file(alias)}`,
      file('input.ll'), '-o', file('events.json')], false);
    assert.deepEqual(fs.readFileSync(file(alias)), before);
  }
});
