import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly } from '../tools/benchmark-c06.mjs';
import { decodeElf } from '../tools/boundary-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stdout}\n${r.stderr}`);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/cross-function-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
const target = 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
  'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n';
function optimize(file, source, pass = 'cross-function') {
  fs.writeFileSync(file('input.ll'), target + source);
  run(tool('badge-opt-sanitize'), [`--passes=${pass}`, file('input.ll'), '-o', file('candidate.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
  run(tool('badge-opt-sanitize'), [`--passes=${pass}`, file('candidate.ll'), '-o', file('twice.ll')]);
  assert.deepEqual(fs.readFileSync(file('twice.ll')), fs.readFileSync(file('candidate.ll')));
  return fs.readFileSync(file('candidate.ll'), 'utf8');
}
function production(file, sources, main) {
  fs.writeFileSync(file('source.c'), sources.map(source =>
    `#include "${path.resolve(root, '..', source)}"`).join('\n') + '\n' + main);
  run(tool('badge-cpp'), ['-I../src/shared', file('source.c'), '-o', file('source.i')]);
  run(tool('badge-ir'), ['--expression-lowering=ssa', file('source.i'), '-o', file('raw.ll')]);
  const passes = run(tool('badge-opt'), ['--default-passes']).stdout.trim().split(',');
  run(tool('badge-opt'), [`--passes=${passes.filter(pass => pass !== 'cross-function').join(',')}`,
    file('raw.ll'), '-o', file('input.ll')]);
  run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('candidate.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
}
function execute(file, role, trace = false, scalar = 'thumb-operands') {
  run(tool('badge-codegen-sanitize'), [`--scalar-selection=${scalar}`, file(`${role === 'control' ? 'input' : 'candidate'}.ll`),
    '-o', file(`${role}.s`)]);
  const assembly = fs.readFileSync(file(`${role}.s`), 'utf8'), frames = parseAssembly(assembly, role);
  run(tool('badge-asm'), [file(`${role}.s`), '-o', file(`${role}.o`)]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file(`${role}.o`), '-o', file(`${role}.elf`)]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${role}.elf`),
    ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', file(`${role}.trace`)] : [])]);
  const count = trace ? (fs.readFileSync(file(`${role}.trace`), 'utf8').match(/^Trace /gm) ?? []).length : null;
  return { frames, count, bytes: decodeElf(fs.readFileSync(file(`${role}.o`))).sections
    .filter(s => s.flags & 4).reduce((n, s) => n + s.size, 0) };
}
const encoder = `define internal i32 @encode(i32 %x, i32 %mode) {
entry:
 %which = icmp eq i32 %mode, 0
 br i1 %which, label %low, label %high
low:
 %a = and i32 %x, 255
 ret i32 %a
high:
 %b = lshr i32 %x, 8
 ret i32 %b
}\n`;
test('constant specialization removes dead paths without duplicating effects and lowers whole-module code', t => {
  const file = files(t);
  const source = encoder + `define i32 @main() {
entry:
 %a = call i32 @encode(i32 305419896, i32 0)
 %b = icmp eq i32 %a, 120
 br i1 %b, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert(!result.includes('%which = icmp'));
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes);
  assert(b.count < a.count);
  console.log(`PO16 encoder oracle: ${a.count} -> ${b.count} reached instructions`);
});
const guardedLoop = `@position = internal global i32 6, align 4
@last = global i32 0, align 4
define void @record(ptr %slot, i32 %value) {
entry:
 %old = load i32, ptr %slot, align 4
 %next = add i32 %old, %value
 store i32 %next, ptr @last, align 4
 ret void
}
define internal void @resolve(i32 %lo, i32 %width) {
entry:
 %hi = add i32 %lo, %width
 %x = load i32, ptr @position, align 4
 %low = icmp sle i32 %x, %lo
 br i1 %low, label %miss, label %upper
miss:
 br label %check
upper:
 %high = icmp sge i32 %x, %hi
 br label %check
check:
 %no = phi i1 [true, %miss], [%high, %upper]
 br i1 %no, label %reject, label %body
reject:
 ret void
body:
 %slot = alloca [32 x i8], align 4
 store i32 %lo, ptr %slot, align 4
 call void @record(ptr %slot, i32 %hi)
 ret void
}
define i32 @main() {
entry:
 br label %loop
loop:
 %i = phi i32 [0, %entry], [%next, %after]
 %width = add i32 %i, 3
 call void @resolve(i32 %i, i32 %width)
 br label %after
after:
 %previous = phi i32 [%i, %loop]
 %saved_width = phi i32 [%width, %loop]
 store volatile i32 %saved_width, ptr @position, align 4
 %next = add i32 %previous, 1
 %more = icmp ult i32 %next, 10
 br i1 %more, label %loop, label %done
done:
 %seen = load i32, ptr @last, align 4
 %status = sub i32 %seen, 30
 ret i32 %status
}
`;
test('loop guard splitting preserves short-circuit edges, live bounds, and successor PHIs', t => {
  const file = files(t);
  const result = optimize(file, guardedLoop, 'guard-split');
  const [helper, caller] = result.split('define i32 @main()');
  assert.doesNotMatch(helper, /icmp s[lg]e/);
  assert.match(caller, /icmp sle/);
  assert.equal([...caller.matchAll(/call void @resolve/g)].length, 1);
  assert.doesNotMatch(caller, /phi i32 \[%i, %loop\]/);
  const a = execute(file, 'control', true, 'thumb-constants');
  const b = execute(file, 'candidate', true, 'thumb-constants');
  assert(b.bytes < a.bytes && b.count < a.count);
});
test('loop guard splitting leaves observable reads and externally callable helpers intact', t => {
  for (const source of [
    guardedLoop.replace('load i32, ptr @position', 'load volatile i32, ptr @position'),
    guardedLoop.replace('define internal void @resolve', 'define void @resolve'),
    guardedLoop.replace('call void @resolve(i32 %i,', 'call void @resolve(i32 noundef %i,'),
  ])
    assert.equal(optimize(files(t), source, 'guard-split'), target + source);
});
test('compact helper folding removes different literal calls but retains dynamic calls', t => {
  const file = files(t);
  const result = optimize(file, `@seed = global i32 4
define internal i32 @blend(i32 %x, i32 %y) {
entry:
 %a = xor i32 %x, %y
 %b = shl i32 %x, 3
 %c = add i32 %a, %b
 ret i32 %c
}
define i32 @main() {
entry:
 %s = load volatile i32, ptr @seed, align 4
 %a = call i32 @blend(i32 2, i32 9)
 %b = call i32 @blend(i32 5, i32 12)
 %c = call i32 @blend(i32 %s, i32 3)
 %pair = add i32 %a, %b
 %all = add i32 %pair, %c
 %status = sub i32 %all, 115
 ret i32 %status
}`, 'compact-inline');
  assert.match(result, /literal_result_calls_folded blend/);
  assert.equal([...result.matchAll(/call i32 @blend/g)].length, 1);
  const a = execute(file, 'control', true, 'thumb-constants');
  const b = execute(file, 'candidate', true, 'thumb-constants');
  assert(b.bytes < a.bytes && b.count < a.count);
});

test('private records promote across a join and fixed carriers load directly into ABI lanes', t => {
  const file = files(t);
  const result = optimize(file, `@seed = global i32 23
declare void @llvm.memset.inline.p0.i32(ptr, i8, i32 immarg, i1 immarg)
define i32 @combine([2 x i32] %pair) {
entry:
 %a = extractvalue [2 x i32] %pair, 0
 %b = extractvalue [2 x i32] %pair, 1
 %sum = add i32 %a, %b
 ret i32 %sum
}
define i32 @main() {
entry:
 %slot = alloca [8 x i8], align 4
 call void @llvm.memset.inline.p0.i32(ptr %slot, i8 0, i32 8, i1 false)
 %s = load volatile i32, ptr @seed, align 4
 store i32 %s, ptr %slot, align 4
 %test = icmp ne i32 %s, 0
 br i1 %test, label %write_field, label %done
write_field:
 %field = getelementptr i8, ptr %slot, i32 4
 store i32 19, ptr %field, align 4
 br label %done
done:
 %record = load [2 x i32], ptr %slot, align 4
 %sum = call i32 @combine([2 x i32] %record)
 %status = sub i32 %sum, 42
 ret i32 %status
}
define i32 @volatile_record() {
entry:
 %kept = alloca [8 x i8], align 4
 store volatile i32 42, ptr %kept, align 4
 %x = load volatile i32, ptr %kept, align 4
 ret i32 %x
}`, 'small-aggregate');
  assert.doesNotMatch(result, /%slot = alloca|call void @llvm\.memset/);
  assert.match(result, /phi i32/);
  assert.match(result, /store volatile i32 42, ptr %kept/);
  execute(file, 'control', false, 'thumb-constants');
  assert.match(fs.readFileSync(file('control.s'), 'utf8'), /@ record-argument: words forwarded 2/);
  execute(file, 'candidate', false, 'thumb-constants');
});

test('straight-line inlining retains volatile order and caller ABI, charging removed helper', t => {
  const file = files(t);
  const result = optimize(file, `@sink = global i32 0
define internal i32 @emit(i32 %x) {
entry:
 store volatile i32 %x, ptr @sink
 %next = add i32 %x, 1
 ret i32 %next
}
define i32 @main() {
entry:
 %a = call i32 @emit(i32 41)
 %value = load volatile i32, ptr @sink
 %ok1 = icmp eq i32 %a, 42
 %ok2 = icmp eq i32 %value, 41
 %ok = and i1 %ok1, %ok2
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`);
  assert(!result.includes('define internal i32 @emit'));
  execute(file, 'control'); execute(file, 'candidate');
});
test('profiled inlining only spends its budget on reached annotated helpers', t => {
  const file = files(t);
  fs.writeFileSync(file('input.ll'), target + `@sink = global i32 0
define internal i32 @cold(i32 %x) !prof !1 {
entry:
 %y = xor i32 %x, 1
 ret i32 %y
}
define internal i32 @hot(i32 %x) !prof !0 {
entry:
 br label %body
body:
 store volatile i32 %x, ptr @sink
 %y = add i32 %x, 1
 ret i32 %y
}
define i32 @cold_path(i32 %x) {
entry:
 %r = call i32 @cold(i32 %x)
 ret i32 %r
}
define i32 @main() {
entry:
 br label %loop
loop:
 %i = phi i32 [0, %entry], [%next, %loop]
 %next = call i32 @hot(i32 %i)
 %again = icmp ult i32 %next, 128
 br i1 %again, label %loop, label %exit
exit:
 %r = load volatile i32, ptr @sink
 %status = xor i32 %r, 127
 ret i32 %status
}
!0 = !{!"function_entry_count", i64 128}
!1 = !{!"function_entry_count", i64 0}
`);
  run(tool('badge-opt-sanitize'), ['--passes=hot-inline', file('input.ll'), '-o', file('candidate.ll')]);
  const ir = fs.readFileSync(file('candidate.ll'), 'utf8');
  assert.match(ir, /profiled_helper_inlined hot/);
  assert.match(ir, /call i32 @cold/);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes && b.count < a.count);
  console.log(`Profiled inlining: ${a.bytes} -> ${b.bytes} bytes; ${a.count} -> ${b.count} reached instructions`);
});

test('compact inlining reduces final bytes for an unprofiled entry-jump helper', t => {
  const file = files(t);
  const source = `define internal i32 @scramble(i32 %a) {
entry:
 br label %body
body:
 %shift = shl i32 %a, 9
 %result = xor i32 %a, %shift
 ret i32 %result
}
define i32 @main() {
entry:
 %a = call i32 @scramble(i32 7)
 %b = call i32 @scramble(i32 %a)
 %status = xor i32 %b, 1835015
 ret i32 %status
}
`;
  const candidate = optimize(file, source, 'compact-inline');
  assert.match(candidate, /compact_helper_inlined scramble/);
  assert.doesNotMatch(candidate, /(?:define|call).*@scramble/);
  const a = execute(file, 'control', true, 'thumb-constants');
  const b = execute(file, 'candidate', true, 'thumb-constants');
  assert(b.bytes < a.bytes && b.count < a.count);
  const before = a.frames.get('main'), after = b.frames.get('main');
  for (const key of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
    assert(after[key] <= before[key]);
  const noinline = source.replace('@scramble(i32 %a) {', '@scramble(i32 %a) noinline {');
  assert.equal(optimize(file, noinline, 'compact-inline'), target + noinline);
});

test('a tiny integer helper can remove repeated calls without growing the caller frame', t => {
  const file = files(t);
  run(tool('badge-cpp'), ['-I../src/shared', '../src/shared/bitmap_font.c',
    '-o', file('font.i')]);
  run(tool('badge-ir'), [file('font.i'), '-o', file('font.ll')]);
  run(tool('badge-opt-sanitize'), [file('font.ll'), '-o', file('compact.ll')]);
  const compact = fs.readFileSync(file('compact.ll'), 'utf8');
  const decision = compact.match(/compact_helper_inlined badger_font_alpha_round - (\d+) (\d+)/);
  assert(decision);
  assert(Number(decision[2]) - Number(decision[1]) <= 8);
  assert.doesNotMatch(compact, /\b(?:define|call)\b[^\n]*@(?:"badger_font_alpha_round"|badger_font_alpha_round\b)/);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('compact.ll')]);
  run(tool('badge-codegen-sanitize'), [file('compact.ll'), '-o', file('compact.s')]);
  const assembly = fs.readFileSync(file('compact.s'), 'utf8');
  assert.doesNotMatch(assembly, /^bl badger_font_alpha_round$/m);
  const frames = parseAssembly(assembly, 'production font');
  for (const name of ['coverage', 'badger_rgb565_blend']) {
    const f = frames.get(name);
    assert(f && f.calls.length === 0);
  }
});

test('equal-size leaf-read inlining keeps the production frame timer out of the call path', t => {
  const file = files(t);
  run(tool('badge-cpp'), ['-I../src/shared', '-I../src/platform/badger',
    '../src/tools/tilt_breakout.c', '-o', file('breakout.i')]);
  run(tool('badge-ir'), [file('breakout.i'), '-o', file('breakout.ll')]);
  run(tool('badge-opt-sanitize'), [file('breakout.ll'), '-o', file('ready.ll')]);
  const ir = fs.readFileSync(file('ready.ll'), 'utf8');
  const decision = ir.split('\n').find(line => line.includes('compact_helper_inlined time_us'));
  assert.match(decision ?? '', /badge-cross-decision: compact_helper_inlined time_us - (\d+) \1$/);
  run(tool('badge-codegen-sanitize'), [file('ready.ll'), '-o', file('ready.s')]);
  const assembly = fs.readFileSync(file('ready.s'), 'utf8');
  const frame = parseAssembly(assembly, 'tilt breakout').get('wait_frame');
  assert(frame && frame.calls.length === 0);
  const wait = assembly.slice(assembly.indexOf('\nwait_frame:\n'), assembly.indexOf('.size wait_frame,'));
  assert.equal([...wait.matchAll(/^ldr\.n .* \[r\d+\]$/gm)].length, 1);
});

test('specialization can prefer a repeated literal shared by distinct call sites', t => {
  const file = files(t);
  const calls = Array.from({ length: 12 }, (_, n) =>
    `  %r${n} = call i32 @group(i32 %x, i32 0, i32 ${n})\n` +
    `  store volatile i32 %r${n}, ptr @sink, align 4`).join('\n');
  const candidate = optimize(file, `@sink = global i32 0, align 4
define internal i32 @group(i32 %x, i32 %mode, i32 %offset) {
entry:
  %selected = icmp eq i32 %mode, 0
  br i1 %selected, label %simple, label %other
simple:
  %result = add i32 %x, %offset
  ret i32 %result
other:
  %mix = xor i32 %x, %mode
  %wide = mul i32 %mix, 16777619
  %last = add i32 %wide, %offset
  ret i32 %last
}
define i32 @main() {
entry:
  %x = load volatile i32, ptr @sink, align 4
${calls}
  br label %end
end:
  %u = load volatile i32, ptr @sink, align 4
  %g = call i32 @group(i32 0, i32 %u, i32 1)
  %status = xor i32 %g, 184553810
  ret i32 %status
}`);
  assert.match(candidate, /constant_specialization group/);
  assert.equal([...candidate.matchAll(/call i32 @"?\.badge\.opt\.\d+"?\s*\(\s*i32 %x\s*,\s*i32 \d+\s*\)/g)].length, 12);
  assert.match(candidate, /call i32 @group\(i32 0, i32 %u, i32 1\)/);
  const control = execute(file, 'control'), selected = execute(file, 'candidate');
  assert(selected.bytes < control.bytes);
});

test('identical non-address-taken helpers share one body without changing call multiplicity', t => {
  const file = files(t);
  const helper = name => `define internal void @${name}(i32 %x) {
entry:
 store volatile i32 %x, ptr @sink
 call void asm sideeffect "nop", "~{memory}"()
 ret void
}\n`;
  const result = optimize(file, '@sink = global i32 0\n' + helper('a') + helper('b') + `define i32 @main() {
entry:
 call void @a(i32 1)
 call void @b(i32 2)
 %value = load volatile i32, ptr @sink
 %ok = icmp eq i32 %value, 2
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`);
  assert(!result.includes('define internal void @b'));
  assert.equal((result.match(/call void @a/g) ?? []).length, 2);
  const a = execute(file, 'control'), b = execute(file, 'candidate');
  assert(b.bytes < a.bytes);
});
test('public, address-taken, recursive and noinline helpers remain unchanged; alwaysinline fails explicitly', t => {
  const file = files(t);
  for (const source of [
    encoder.replace('internal ', ''),
    encoder + '@address = global ptr @encode\n',
    encoder.replace('%a = and i32 %x, 255', '%a = call i32 @encode(i32 %x, i32 %mode)'),
    encoder.replace(') {', ') noinline {'),
    encoder.replace(') {', ') #7 {') + 'attributes #7 = { noinline }\n',
  ]) {
    const full = source + `define i32 @main() {\nentry:\n%r = call i32 @encode(i32 17, i32 0)\nret i32 %r\n}`;
    assert.equal(optimize(file, full), target + full);
  }
  fs.writeFileSync(file('input.ll'), target + encoder.replace(') {', ') alwaysinline {'));
  fs.writeFileSync(file('protected.ll'), 'original-output');
  const r = run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('protected.ll')], false);
  assert.match(r.stderr, /alwaysinline.*supported.*contract/);
  assert.equal(fs.readFileSync(file('protected.ll'), 'utf8'), 'original-output');
});
test('resource exhaustion fails without partial publication', t => {
  const file = files(t);
  fs.writeFileSync(file('input.ll'), target + encoder + 'define i32 @main() {\nentry:\n%r = call i32 @encode(i32 17, i32 0)\nret i32 %r\n}');
  fs.writeFileSync(file('protected.ll'), 'original-output');
  const r = run(tool('badge-opt-cross-budget'), ['--passes=cross-function', file('input.ll'), '-o', file('protected.ll')], false);
  assert.match(r.stderr, /cross-function work limit exceeded/);
  assert.equal(fs.readFileSync(file('protected.ll'), 'utf8'), 'original-output');
});
test('a beneficial reduced-ABI clone pays for its generic helper and preserves dynamic calls', t => {
  const file = files(t);
  let calls = '', sum = '%dynamic';
  for (let i = 0; i < 20; i++) {
    calls += `%v${i} = call i32 @encode(i32 %x, i32 0)\n`;
    calls += `%sum${i} = add i32 ${sum}, %v${i}\n`;
    sum = `%sum${i}`;
  }
  const source = encoder + `define i32 @batch(i32 %x, i32 %mode) {
entry:
 %dynamic = call i32 @encode(i32 %x, i32 %mode)
 ${calls}
 ret i32 ${sum}
}
define i32 @main() {
entry:
 %r = call i32 @batch(i32 305419896, i32 1)
 %ok = icmp eq i32 %r, 1195446
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert.match(result, /define internal i32 @encode/);
  assert.match(result, /define internal i32 @"\.badge\.opt\.\d+" \( i32 %x \)/);
  assert.match(result, /call i32 @encode\(i32 %x, i32 %mode\)/);
  const a = execute(file, 'control'), b = execute(file, 'candidate');
  assert(b.bytes < a.bytes);
});
test('mixed fifth/eighth arguments, signed narrow transport and quoted operands preserve reduced ABI', t => {
  const file = files(t);
  const source = `define internal i32 @"sum"(i8 signext %"a", i32 %b, i32 %c, i32 %d,
i32 %e, i32 %f, i32 %g, i32 %h) {
entry:
 %wide = sext i8 %"a" to i32
 %ab = add i32 %wide, %b
 %cd = add i32 %c, %d
 %ef = add i32 %e, %f
 %gh = add i32 %g, %h
 %first = add i32 %ab, %cd
 %second = add i32 %ef, %gh
 %all = add i32 %first, %second
 ret i32 %all
}
define i32 @main() {
entry:
 %r = call i32 @"sum"(i8 signext 255, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7)
 %ok = icmp eq i32 %r, 27
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source.replace('i32 %d,\ni32', 'i32 %d, i32'));
  const a = execute(file, 'control'), b = execute(file, 'candidate');
  assert(b.bytes < a.bytes);
  assert(!result.includes('call i32 @sum(i8'));
});
test('canonical quoted address references and mutual recursion prohibit specialization', t => {
  const file = files(t);
  const source = encoder + '@address = global ptr @"encode"\n' +
    'define i32 @main() {\nentry:\n%r = call i32 @"encode"(i32 17, i32 0)\nret i32 %r\n}';
  assert.equal(optimize(file, source), target + source);
  const mutual = encoder.replace('%a = and i32 %x, 255', '%a = call i32 @other(i32 %x, i32 %mode)') +
    'define internal i32 @other(i32 %x, i32 %mode) {\nentry:\n%r = call i32 @"encode"(i32 %x, i32 %mode)\nret i32 %r\n}' +
    '\ndefine i32 @main() {\nentry:\nret i32 0\n}';
  assert.equal(optimize(file, mutual), target + mutual);
});
test('rejected clone attempts release names/output before subsequent candidates', t => {
  const file = files(t);
  const source = encoder + `define internal i32 @small(i32 %x) {
entry:
 %r = add i32 %x, 7
 ret i32 %r
}
define i32 @main() {
entry:
 %a = call i32 @encode(i32 1234, i32 0)
 %b = call i32 @encode(i32 %a, i32 1)
 %r = call i32 @small(i32 %b)
 %ok = icmp eq i32 %r, 7
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  optimize(file, source);
  execute(file, 'control'); execute(file, 'candidate');
});
test('musttail and unsupported forced inline fail even with completion markers or later functions', t => {
  const file = files(t);
  for (const source of [
    encoder.replace(') {', ') #8 {') + 'attributes #8 = { alwaysinline }\n; badge-cross-function: bounded selection complete\n',
    encoder + 'define i32 @main() {\nentry:\n%r = musttail call i32 @encode(i32 17, i32 0)\nret i32 %r\n}',
  ]) {
    fs.writeFileSync(file('input.ll'), target + source);
    const r = run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('out.ll')], false);
    assert.match(r.stderr, /(?:musttail|alwaysinline).*supported.*contract/);
  }
});
test('actual USB report/CRC call graph preserves request/response bytes and rejection behavior', t => {
  const file = files(t);
  production(file, ['src/shared/usb_report.c', 'src/tools/diagnostics_logic.c'], `
static uint8_t request[64], response[64];
int main(void) {
  uint8_t nonce[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
  usb_report_request(request, 0x12345678, nonce);
  if (usb_report_check_request(request, 64, 0x12345678) != USB_RECORD_OK) return 1;
  usb_report_response(response, 0x12345678, nonce, USB_REPORT_OK, 0xfeedc0de);
  if (usb_report_check_response(response, 64, request, 64, 0x12345678) != USB_RECORD_OK) return 2;
  request[8] ^= 1;
  if (usb_report_check_request(request, 64, 0x12345678) != USB_RECORD_CRC) return 3;
  if (usb_report_check_request(request, 63, 0x12345678) != USB_RECORD_LENGTH) return 4;
  return 0;
}`);
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes);
  assert(b.count <= a.count);
  console.log(`PO16 production USB oracle: ${a.count} -> ${b.count} reached instructions`);
});
test('actual Wi-Fi link helper call graph preserves peer filtering, clearing and skipped comparisons', t => {
  const file = files(t);
  production(file, ['src/radio/wifi_link.c'], `
static wifi_link_t link;
int main(void) {
  uint8_t a[6] = {2,3,4,5,6,7}, b[6] = {2,3,4,5,6,7}, zero[6] = {0};
  if (!equal_mac(a,b) || !zero_mac(zero) || zero_mac(a)) return 1;
  b[5] = 8;
  if (equal_mac(a,b)) return 2;
  if (!peer_mac(&link,a)) return 3;
  copy(link.report.mac,a,6);
  if (peer_mac(&link,a)) return 4;
  for (unsigned i=0;i<6;i++) link.report.peer[i]=255;
  waiting(&link);
  for (unsigned i=0;i<6;i++) if(link.report.peer[i]) return 5;
  if (link.report.authorized || link.report.peer_count) return 6;
  return 0;
}`);
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes);
  assert(b.count <= a.count);
  console.log(`PO16 production WiFi oracle: ${a.count} -> ${b.count} reached instructions`);
});
test('tail-forwarding helpers eliminate one call boundary without native tail-frame assumptions', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define i32 @external(i32 %x) {
entry:
 store volatile i32 %x, ptr @sink
 ret i32 %x
}
define internal i32 @forward(i32 %x) {
entry:
 %r = call i32 @external(i32 %x)
 ret i32 %r
}
define i32 @main() {
entry:
 %r = call i32 @forward(i32 42)
 %value = load volatile i32, ptr @sink
 %a = icmp eq i32 %r, 42
 %b = icmp eq i32 %value, 42
 %ok = and i1 %a, %b
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert(!result.includes('define internal i32 @forward'));
  assert.match(result, /badge-cross-decision: tail_forwarder_inlined/);
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes && b.count < a.count);
});
test('untracked assembly symbol references retain every helper and call ABI', t => {
  const file = files(t);
  const source = encoder + `define i32 @main() {
entry:
 call void asm sideeffect "bl encode", "~{lr}"()
 %r = call i32 @encode(i32 17, i32 0)
 ret i32 %r
}`;
  assert.equal(optimize(file, source), target + source);
});
