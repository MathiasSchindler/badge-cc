import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly } from '../tools/benchmark-c06.mjs';
import { verifyAssemblyEffects } from '../tools/assembly-effects-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
function files(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/lightweight-frames-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  return name => path.join(directory, name);
}
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stdout}\n${r.stderr}`);
  return r;
}
function compile(file, ir, selection = 'thumb-frames', executable = 'badge-codegen-sanitize') {
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' + ir);
  run(tool(executable), [`--scalar-selection=${selection}`, file('input.ll'), '-o', file(`${selection}.s`)]);
  return fs.readFileSync(file(`${selection}.s`), 'utf8');
}
function execute(file, objects, name) {
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'),
    ...objects, '-o', file(`${name}.elf`)]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${name}.elf`)]);
}
test('entry guards bypass saves on void returns and restore unchanged arguments on fallthrough', t => {
  const file = files(t);
  const source = `@value = global i32 0, align 4
@result = global i32 0, align 4
define void @record(i32 %a, i32 %b, i32 %c, i32 %d) {
entry:
  %x = mul i32 %a, 3
  %y = mul i32 %b, 5
  %z = mul i32 %c, 7
  %w = mul i32 %d, 11
  %lo = add i32 %x, %y
  %hi = add i32 %z, %w
  %v = add i32 %lo, %hi
  store volatile i32 %v, ptr @result, align 4
  ret void
}
define void @frame_user(ptr %area) {
entry:
  store volatile i32 9, ptr %area, align 4
  ret void
}
define void @check(i32 %a, i32 %b, i32 %c, i32 %d) {
entry:
  %lo = sub i32 %b, 8
  %v = load i32, ptr @value, align 4
  %low = icmp sle i32 %v, %lo
  br i1 %low, label %return, label %upper
upper:
  %end = add i32 %b, %d
  %hi = add i32 %end, 8
  %v2 = load i32, ptr @value, align 4
  %inside = icmp slt i32 %v2, %hi
  br i1 %inside, label %work, label %return
work:
  %area = alloca [32 x i8], align 4
  call void @frame_user(ptr %area)
  call void @record(i32 %a, i32 %b, i32 %c, i32 %d)
  br label %return
return:
  ret void
}
define i32 @main() {
entry:
  store i32 12, ptr @value, align 4
  call void @check(i32 3, i32 20, i32 7, i32 4)
  %low = load volatile i32, ptr @result, align 4
  store i32 32, ptr @value, align 4
  call void @check(i32 3, i32 20, i32 7, i32 4)
  %high = load volatile i32, ptr @result, align 4
  store i32 31, ptr @value, align 4
  call void @check(i32 3, i32 20, i32 7, i32 4)
  %inside = load volatile i32, ptr @result, align 4
  %good = xor i32 %inside, 202
  %both = or i32 %low, %high
  %status = or i32 %both, %good
  ret i32 %status
}`;
  const control = compile(file, source, 'thumb-widths');
  const selected = compile(file, source, 'thumb-constants');
  const prefix = text => text.match(/^check:\n[^]*?^push\./m)[0];
  assert.doesNotMatch(prefix(control), /entry-guard/);
  assert.match(prefix(selected), /entry-guard: stack bypass checks 2[^]*bx lr/);
  const baselineFrame = parseAssembly(control, 'control').get('check');
  const selectedFrame = parseAssembly(selected, 'candidate').get('check');
  assert(selectedFrame.frame_bytes <= baselineFrame.frame_bytes);
  assert(selectedFrame.saves_bytes <= baselineFrame.saves_bytes);
  const width = compile(file, source.replace('load i32, ptr @value', 'load volatile i32, ptr @value'),
    'thumb-constants');
  assert.doesNotMatch(prefix(width), /entry-guard/);
  const effect = compile(file, source.replace('%lo = sub i32 %b, 8',
    'store volatile i32 9, ptr @result, align 4\n  %lo = sub i32 %b, 8'), 'thumb-constants');
  assert.doesNotMatch(prefix(effect), /entry-guard/);
  fs.writeFileSync(file('thumb-constants.s'), selected);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const policy of ['thumb-widths', 'thumb-constants']) {
    run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
    execute(file, [file(`${policy}.o`)], policy);
  }
});

test('stack-free scalar leaves have no save/frame work; finalized metadata matches emission', t => {
  const file = files(t);
  const source = `define i32 @identity(i32 %x) {\nentry:\nret i32 %x\n}
define i32 @add(i32 %x) {\nentry:\n%v = add i32 %x, 7\nret i32 %v\n}
define void @delay(i32 %n) {
entry:
 br label %loop
loop:
 %x = phi i32 [%n, %entry], [%next, %loop]
 call void asm sideeffect "nop", "~{memory}"()
 %next = sub i32 %x, 1
 %c = icmp ne i32 %next, 0
 br i1 %c, label %loop, label %done
done:
 ret void
}`;
  const after = compile(file, source);
  assert(!/\b(?:push|pop|sp|r7)\b/.test(after.replace(/^@.*$/gm, '')), after);
  assert(!after.includes('movw r12, #0'));
  assert.equal((after.match(/^bx lr$/gm) ?? []).length, 3);
  assert(after.includes('mov r3, r0'));
  for (const fn of parseAssembly(after, 'leaves').values()) {
    assert.equal(fn.frame_bytes, 0);
    assert.equal(fn.saves_bytes, 0);
    assert.deepEqual(fn.unknown, []);
  }
  run(tool('badge-asm'), [file('thumb-frames.s'), '-o', file('leaves.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('thumb-frames.s'), '-o', file('llvm.o')]);
  const before = compile(file, source, 'thumb-effects');
  assert(before.includes('push.w'));
  assert.throws(() => parseAssembly(after.replace('mask 0', 'mask 16'), 'tamper'), /metadata differs/);
});
test('compact leaf returns reuse an existing alignment word without increasing saves', t => {
  const file = files(t);
  const source = `define i32 @leaf(i32 %x) {
entry:
 %p = alloca i32, align 4
 store volatile i32 %x, ptr %p, align 4
 %a = load volatile i32, ptr %p, align 4
 ret i32 %a
}
define i32 @main() {
entry:
 %v = call i32 @leaf(i32 305419896)
 %status = xor i32 %v, 305419896
 ret i32 %status
}`;
  const baseline = compile(file, source, 'thumb-widths');
  const candidate = compile(file, source, 'thumb-constants');
  const leaf = candidate.slice(candidate.indexOf('\nleaf:'), candidate.indexOf('.size leaf,'));
  assert.match(leaf, /^push\.[nw] \{[^}]*lr\}$/m);
  assert.match(leaf, /^pop\.[nw] \{[^}]*pc\}$/m);
  assert.doesNotMatch(leaf, /^bx lr$/m);
  const before = parseAssembly(baseline, 'before').get('leaf');
  const after = parseAssembly(candidate, 'after').get('leaf');
  assert.equal(after.saves_bytes, before.saves_bytes);
  assert.equal(after.frame_bytes, before.frame_bytes);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('thumb-constants.s'), '-o', file('candidate.o')]);
  execute(file, [file('candidate.o')], 'fused');
});

test('mixed AAPCS calls, high-register scratch, large incoming offsets, composites, indirect and recursive calls execute', t => {
  const file = files(t);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const fixture of ['frame_addressing_abi_fixture', 'register_residency_abi_fixture', 'call_transport_abi_fixture']) {
    for (const side of ['caller', 'callee']) {
      const flags = side === 'caller' ? ['-DCALLER'] : [];
      const source = `tests/fixtures/${fixture}.c`;
      run(tool('badge-cpp'), [...flags, source, '-o', file(`${side}.i`)]);
      run(tool('badge-ir'), [file(`${side}.i`), '-o', file(`${side}.initial.ll`)]);
      run(tool('badge-opt'), [file(`${side}.initial.ll`), '-o', file(`${side}.ll`)]);
      run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-frames',
        file(`${side}.ll`), '-o', file(`${side}.s`)]);
      const text = fs.readFileSync(file(`${side}.s`), 'utf8');
      const frames = [...parseAssembly(text, fixture).values()];
      assert(frames.every(f => !f.unknown.includes('unverified_frame_balance')));
      assert(!/@ frame-incoming:/.test(text));
      run(tool('badge-asm'), [file(`${side}.s`), '-o', file(`owned-${side}.o`)]);
      run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
        '-ffreestanding', '-fno-builtin', '-Oz', ...flags, '-c', source, '-o', file(`llvm-${side}.o`)]);
    }
    for (const [caller, callee] of [['owned', 'llvm'], ['llvm', 'owned'], ['owned', 'owned']])
      execute(file, [file(`${caller}-caller.o`), file(`${callee}-callee.o`)], `${fixture}-${caller}-${callee}`);
  }
});
test('caller-saved short values do not cross calls; cyclic PHIs and unknown assembly stay protected', t => {
  const file = files(t);
  const source = `define i32 @callee(i32 %v) {\nentry:\n%r = add i32 %v, 1\nret i32 %r\n}
define i32 @work(i32 %x) {
entry:
 %live = add i32 %x, 10
 %v = call i32 @callee(i32 %x)
 %r = add i32 %live, %v
 ret i32 %r
}
define i32 @cycle() {
entry:
 br label %loop
loop:
 %a = phi i32 [1, %entry], [%b, %body]
 %b = phi i32 [2, %entry], [%a, %body]
 %n = phi i32 [0, %entry], [%next, %body]
 %c = icmp ult i32 %n, 31
 br i1 %c, label %body, label %done
body:
 %next = add i32 %n, 1
 br label %loop
done:
 %v = mul i32 %a, 10
 %r = add i32 %v, %b
 ret i32 %r
}
define i32 @unknown(i32 %x) {
entry:
 call void asm sideeffect "movw r8, #0\\0Amovw r10, #0\\0Amovw r11, #0", ""()
 ret i32 %x
}
define i32 @main() {
entry:
 %w = call i32 @work(i32 5)
 %a = icmp eq i32 %w, 21
 %v = call i32 @cycle()
 %b = icmp eq i32 %v, 21
 %u = call i32 @unknown(i32 123)
 %c = icmp eq i32 %u, 123
 %ab = and i1 %a, %b
 %all = and i1 %ab, %c
 br i1 %all, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}`;
  const out = compile(file, source);
  assert([...parseAssembly(out, 'runtime').values()].every(fn => !fn.unknown.includes('unverified_frame_balance')));
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('thumb-frames.s'), '-o', file('body.o')]);
  execute(file, [file('body.o')], 'calls-phi-assembly');
});
test('bounded analysis fails explicitly and preserves output; qualified selector is default', t => {
  const file = files(t);
  assert.equal(run(tool('badge-codegen'), ['--default-scalar-selection']).stdout.trim(), 'thumb-widths');
  fs.writeFileSync(file('input.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"\ndefine i32 @f(i32 %x) {
entry:\n${Array.from({ length: 300 }, (_, i) => `%v${i} = add i32 %x, ${i}`).join('\n')}\nret i32 %x\n}`);
  fs.writeFileSync(file('preserved.s'), 'preserved');
  const result = run(tool('badge-codegen-branch-budget'), ['--scalar-selection=thumb-frames',
    file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(result.stderr, /work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
});
test('persistent PO12 report rejects frame and image accounting tampering',
  { skip: !process.env.PO12_REPORT }, t => {
    const file = files(t);
    const report = verifyAssemblyEffects(path.resolve(root, '..', process.env.PO12_REPORT), true);
    assert.equal(report.milestone, 'PO12');
    const changed = JSON.parse(JSON.stringify(report));
    changed.modules[0].functions[0].saved_bytes++;
    fs.writeFileSync(file('report.json'), JSON.stringify(changed));
    assert.throws(() => verifyAssemblyEffects(file('report.json')));
    changed.modules = report.modules;
    changed.images[0].candidate.text--;
    fs.writeFileSync(file('report.json'), JSON.stringify(changed));
    assert.throws(() => verifyAssemblyEffects(file('report.json')));
  });
