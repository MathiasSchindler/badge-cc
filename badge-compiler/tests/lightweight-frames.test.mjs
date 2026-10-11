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
