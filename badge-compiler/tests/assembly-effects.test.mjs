import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
const triple = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function fixture(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/assembly-effects-test-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  return name => path.join(directory, name);
}
function run(command, args, success = true) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8',
    timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(result.error);
  if (success) assert.equal(result.status, 0, `${command}\n${result.stdout}\n${result.stderr}`);
  else assert.notEqual(result.status, 0, 'expected explicit failure');
  return result;
}
function compile(file, source, selection = 'thumb-effects', residency = 'global-compact',
  executable = 'badge-codegen-sanitize') {
  fs.writeFileSync(file('input.ll'), triple + source);
  run(tool(executable), [`--scalar-selection=${selection}`, `--register-residency=${residency}`,
    '--frame-addressing=direct', '--memory-selection=word', file('input.ll'), '-o', file(`${selection}.s`)]);
  return fs.readFileSync(file(`${selection}.s`), 'utf8');
}
const keep = (body, constraints = '') => `define i32 @keep(i32 %x) {
entry:
 call void asm sideeffect "${body}", "${constraints}"()
 %r = add i32 %x, 7
 ret i32 %r
}\n`;
const branch = ` %c = icmp ult i32 %x, 7
 br i1 %c, label %yes, label %no
yes:
 ret i32 1
no:
 ret i32 0`;
const count = (output, field) => [...output.matchAll(new RegExp(`${field} (\\d+)`, 'g'))]
  .reduce((sum, match) => sum + Number(match[1]), 0);

test('only complete operand-free NOP lines prove registers preserved; memory and flags are independent', t => {
  const file = fixture(t);
  for (const body of ['nop', 'nop\\0Anop\\0Anop', '\\09nop \\0D\\0A\\0Anop\\09']) {
    for (const clobber of ['', '~{memory}', '~{cc}', '~{memory},~{cc}']) {
      const after = compile(file, keep(body, clobber));
      assert(after.includes('@ assembly-effects: known 1 reads 0 writes 0'));
      assert(after.includes(`flags 0/${Number(clobber.includes('cc'))} memory 0/0 barrier ${Number(clobber.includes('memory'))} control 0`));
      assert.equal(count(after, 'asm-spills'), 0);
      const before = compile(file, keep(body, clobber), 'thumb-calls');
      assert.equal(count(before, 'asm-spills'), 1);
      run(tool('badge-asm'), [file('thumb-effects.s'), '-o', file('owned.o')]);
      run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
        '-mthumb', '-c', file('thumb-effects.s'), '-o', file('llvm.o')]);
    }
  }
  for (const body of ['', ' ', 'nop.w', 'nop @ comment', 'nop; movw r8, #0', 'nop\\0Amovw r8, #0',
    'movw r8, #0', 'adds r0, #1', 'wfi', 'dmb sy', 'dsb sy', 'isb sy', 'label:\\0Anop']) {
    const output = compile(file, keep(body));
    assert(output.includes('@ assembly-effects: known 0 reads 65535 writes 65535'));
    assert(output.includes('flags 1/1 memory 1/1 barrier 0 control 1'));
    assert.equal(count(output, 'asm-spills'), 1, body);
  }
});

test('compare/branch selection is instruction-local; flag-changing assembly between them remains a barrier', t => {
  const file = fixture(t);
  for (const body of ['nop', 'movw r8, #0', 'adds r0, #1', 'dmb sy']) {
    const source = keep(body).replace(' %r = add i32 %x, 7\n ret i32 %r', branch);
    assert.equal(count(compile(file, source), 'fused'), 1);
    assert.equal(count(compile(file, source, 'thumb-calls'), 'fused'), 0);
  }
  const separated = `define i32 @separated(i32 %x) {
entry:
 %c = icmp ult i32 %x, 7
 call void asm sideeffect "adds r0, #1", "~{cc}"()
 br i1 %c, label %yes, label %no
yes:
 ret i32 1
no:
 ret i32 0
}\n`;
  const output = compile(file, separated);
  assert.equal(count(output, 'fused'), 0);
  assert.equal(count(output, 'asm-spills'), 1);
  assert(output.includes('it lo'), 'nonadjacent boolean still materialized');
});

test('local and global residency preserve loop-carried values, calls, volatile ordering and unknown clobbers on Cortex-M33', t => {
  const file = fixture(t);
  const source = `
@sink = global i32 0
define i32 @callee(i32 %x) {
entry:
 %r = add i32 %x, 1
 ret i32 %r
}
define i32 @loop(i32 %limit) {
entry:
 br label %body
body:
 %n = phi i32 [0, %entry], [%next, %body]
 %sum = phi i32 [0, %entry], [%nextsum, %body]
 store volatile i32 %n, ptr @sink
 call void asm sideeffect "nop\\0Anop", "~{memory}"()
 %seen = load volatile i32, ptr @sink
 %step = call i32 @callee(i32 %seen)
 call void asm sideeffect "movw r8, #4660\\0Amovw r10, #22136\\0Amovw r11, #39612", ""()
 %nextsum = add i32 %sum, %step
 %next = add i32 %n, 1
 %c = icmp ult i32 %next, %limit
 br i1 %c, label %body, label %done
done:
 ret i32 %nextsum
}
${keep('nop', '~{memory}')}
define i32 @local_keep(i32 %x) {
entry:
 %v = add i32 %x, 5
 call void asm sideeffect "nop", ""()
 %r = add i32 %v, 7
 ret i32 %r
}
define i32 @flags(i32 %x) {
entry:
 %c = icmp ult i32 %x, 7
 call void asm sideeffect "adds r0, #1", "~{cc},~{memory}"()
 br i1 %c, label %yes, label %no
yes:
 ret i32 1
no:
 ret i32 0
}
define i32 @main() {
entry:
 %v = call i32 @loop(i32 31)
 %ok1 = icmp eq i32 %v, 496
 %k = call i32 @keep(i32 35)
 %ok2 = icmp eq i32 %k, 42
 %f = call i32 @flags(i32 3)
 %ok3 = icmp eq i32 %f, 1
 %g = call i32 @flags(i32 9)
 %ok4 = icmp eq i32 %g, 0
 %local = call i32 @local_keep(i32 30)
 %ok5 = icmp eq i32 %local, 42
 %a = and i1 %ok1, %ok2
 %b = and i1 %ok3, %ok4
 %ab = and i1 %a, %b
 %all = and i1 %ab, %ok5
 br i1 %all, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}\n`;
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const residency of ['stack', 'local', 'global', 'global-flex', 'global-compact']) {
    const output = compile(file, source, 'thumb-effects', residency);
    if (residency.startsWith('global')) assert(count(output, 'asm-spills') > 0);
    run(tool('badge-asm'), [file('thumb-effects.s'), '-o', file('body.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
      file('start.o'), file('body.o'), '-o', file('test.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('test.elf')]);
  }
  const local = `define i32 @main() {\nentry:\n%v = add i32 30, 5\ncall void asm sideeffect "nop", ""()\n%r = add i32 %v, 7\nret i32 %r\n}\n`;
  const after = compile(file, local, 'thumb-effects', 'local');
  const before = compile(file, local, 'thumb-calls', 'local');
  assert(count(after, 'avoided-loads') > count(before, 'avoided-loads'));
  assert(!after.includes('str.w r0, [r7, #0]'));
});

test('optimizer keeps ordinary memory accesses and side effects across proven NOP memory barriers', t => {
  const file = fixture(t);
  const source = triple + 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' + `@mem = global i32 7
define i32 @memory() {
entry:
 %before = load i32, ptr @mem
 call void asm sideeffect "nop\\0Anop", "~{memory}"()
 %after = load i32, ptr @mem
 %sum = add i32 %before, %after
 ret i32 %sum
}\n`;
  fs.writeFileSync(file('memory.ll'), source);
  run(tool('badge-opt'), [file('memory.ll'), '-o', file('memory.opt.ll')]);
  const ir = fs.readFileSync(file('memory.opt.ll'), 'utf8');
  assert.equal((ir.match(/load i32, ptr @mem/g) ?? []).length, 2);
  assert(ir.includes('asm sideeffect "nop\\0Anop", "~{memory}"'));
  const output = compile(file, ir.replace(triple, ''));
  assert.equal(count(output, 'asm-spills'), 0);
  const accesses = [...output.matchAll(/^ldr(?:\.w)? r0, \[r4/gm)].map(m => m.index);
  assert.equal(accesses.length, 2);
  assert(accesses[0] < output.indexOf('\nnop\nnop\n'));
  assert(accesses[1] > output.indexOf('\nnop\nnop\n'));
});

test('both C frontend policies carry memory/cc clobbers into the owned optimizer and backend', t => {
  const file = fixture(t);
  fs.writeFileSync(file('source.c'), `int source(int x) {
    __asm__ volatile("nop\\nnop" ::: "c" "c", "m" "emory");
    __asm__ volatile("adds r0, #1" ::: "cc", "memory");
    return x + 7;
  }
  int main(void) { return source(35) != 42; }\n`);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const mode of ['legacy', 'ssa']) {
    run(tool('badge-ir'), [`--expression-lowering=${mode}`, file('source.c'), '-o', file('source.ll')]);
    const initial = fs.readFileSync(file('source.ll'), 'utf8');
    assert(initial.includes('"~{memory},~{cc}"()'));
    run(tool('badge-opt'), [file('source.ll'), '-o', file('source.opt.ll')]);
    run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('source.opt.ll')]);
    run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-effects',
      file('source.opt.ll'), '-o', file('source.s')]);
    run(tool('badge-asm'), [file('source.s'), '-o', file('source.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
      file('start.o'), file('source.o'), '-o', file('source.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('source.elf')]);
  }
  fs.writeFileSync(file('source.c'), 'void bad(void) { __asm__ volatile("nop" ::: "r8"); }\n');
  fs.writeFileSync(file('preserved.ll'), 'preserved');
  const error = run(tool('badge-ir'), [file('source.c'), '-o', file('preserved.ll')], false);
  assert.match(error.stderr, /only memory and cc assembly clobbers/);
  assert.equal(fs.readFileSync(file('preserved.ll'), 'utf8'), 'preserved');
});

test('invalid assembly and analysis-budget failures preserve output; existing default remains timing-qualified', t => {
  const file = fixture(t);
  assert.equal(run(tool('badge-codegen'), ['--default-scalar-selection']).stdout.trim(), 'thumb-widths');
  const invalid = [keep('nop', '~{r8}'), keep('nop', '~{memory},'), keep('nop', '~{memory}x'),
    keep('nop\\00'), keep('nop\\0X'), keep('nop').replace('""()', '""(i32 %x)'),
    keep('nop').replace('call void asm', '%a = call i32 asm'),
    keep('nop').replace('asm sideeffect', 'asm alignstack')];
  for (const source of invalid) {
    fs.writeFileSync(file('input.ll'), triple + source);
    fs.writeFileSync(file('preserved.s'), 'preserved');
    const result = run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-effects',
      file('input.ll'), '-o', file('preserved.s')], false);
    assert.match(result.stderr, /inline assembly/);
    assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
  }
  fs.writeFileSync(file('input.ll'), triple + keep('nop\\0A'.repeat(500)));
  const result = run(tool('badge-codegen-branch-budget'), ['--scalar-selection=thumb-effects',
    file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(result.stderr, /work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
});
