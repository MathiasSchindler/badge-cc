import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { emittedMemory } from '../tools/memory-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
const oracle = process.env.BADGE_TEST_MODE !== 'owned';
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stderr}`);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/local-memory-'));
  t.after(() => { for (const n of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, n)); fs.rmdirSync(dir); });
  return name => path.join(dir, name);
}
const header = 'target triple = "thumbv8m.main-unknown-none-eabi"\ntarget datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n';
function lower(file, body) {
  fs.writeFileSync(file('input.ll'), header + body);
  for (const name of ['badge-opt', 'badge-opt-sanitize']) {
    run(tool(name), ['--passes=local-memory', file('input.ll'), '-o', file(`${name}.ll`)]);
    if (oracle)
      run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file(`${name}.ll`)]);
  }
  assert.deepEqual(fs.readFileSync(file('badge-opt.ll')), fs.readFileSync(file('badge-opt-sanitize.ll')));
  return fs.readFileSync(file('badge-opt.ll'), 'utf8');
}
test('exact scalar load reuse never forwards stores or inserts new values and stays bounded', t => {
  assert.deepEqual(emittedMemory('@ note\n.section .text\nf:\nldr.w r0, [r7]\nstrb r1, [r0]\nbl f\nmov r1, r0\n'),
    { instructions: 4, loads: 1, stores: 1, calls: 1 });
  const f = files(t), text = lower(f, `define i32 @load_pair(ptr %p) {
entry:
  %a = load i32, ptr %p, align 4
  %b = load i32, ptr %p, align 4
  %c = add i32 %a, %b
  ret i32 %c
}
define i32 @store_pair(ptr %p, i32 %x) {
entry:
  store i32 %x, ptr %p, align 4
  %b = load i32, ptr %p, align 4
  %c = load i32, ptr %p, align 4
  %d = xor i32 %b, %c
  ret i32 %d
}
define ptr @pointer(ptr %p, ptr %x) {
entry:
  store ptr %x, ptr %p, align 4
  %a = load ptr, ptr %p, align 4
  ret ptr %a
}`);
  assert.equal((text.match(/ = load /g) ?? []).length, 3);
  assert.doesNotMatch(text, / = freeze /);
  assert.match(text, /store i32 %x/); assert.match(text, /store ptr %x/);
  fs.writeFileSync(f('kept.ll'), 'kept');
  run(tool('badge-opt-memory-budget'), ['--passes=local-memory', f('input.ll'), '-o', f('kept.ll')], false);
  assert.equal(fs.readFileSync(f('kept.ll'), 'utf8'), 'kept');
  const undef = lower(f, `define i32 @undef_store(ptr %p) {
entry:
  store i32 undef, ptr %p, align 4
  %a = load i32, ptr %p, align 4
  %b = load i32, ptr %p, align 4
  %c = xor i32 %a, %b
  ret i32 %c
}`);
  assert.doesNotMatch(undef, / = freeze /);
  assert.equal((undef.match(/ = load /g) ?? []).length, 1);
  assert.match(undef, /store i32 undef/);
  const pointer = lower(f, `define i32 @alias_pointer(ptr %slot, ptr %q) {
entry:
  store ptr %q, ptr %slot, align 4
  %p = load ptr, ptr %slot, align 4
  store i32 17, ptr %p, align 4
  %x = load i32, ptr %p, align 4
  ret i32 %x
}`);
  assert.equal((pointer.match(/ = load /g) ?? []).length, 2);
  fs.writeFileSync(f('kept.ll'), 'kept');
  fs.writeFileSync(f('bad.ll'), header + 'define i32 @f(ptr %p) {\nentry:\n  %a = load atomic i32, ptr %p monotonic, align 4\n  ret i32 %a\n}\n');
  run(tool('badge-opt'), ['--passes=local-memory', f('bad.ll'), '-o', f('kept.ll')], false);
  assert.equal(fs.readFileSync(f('kept.ll'), 'utf8'), 'kept');
  const before = fs.readFileSync(f('input.ll'));
  run(tool('badge-opt'), ['--passes=local-memory', f('input.ll'), '-o', f('input.ll')], false);
  assert.deepEqual(fs.readFileSync(f('input.ll')), before);
});
test('aliasing writes, calls, volatile, metadata, wide accesses and long/cross-block uses stay conservative', t => {
  const f = files(t);
  for (const barrier of ['store i8 7, ptr %q, align 1', 'call void @touch(ptr %p)',
    '%v = load volatile i32, ptr %p, align 4']) {
    const text = lower(f, `declare void @touch(ptr)
define i32 @f(ptr %p, ptr %q) {
entry:
  %a = load i32, ptr %p, align 4
  ${barrier}
  %b = load i32, ptr %p, align 4
  %c = add i32 %a, %b
  ret i32 %c
}`);
    assert.match(text, /%b = load i32/);
  }
  for (const [type, suffix] of [['i64', ''], ['i32', ', !nontemporal !0']]) {
    const text = lower(f, `define ${type} @f(ptr %p) {
entry:
  %a = load ${type}, ptr %p, align 4${suffix}
  %b = load ${type}, ptr %p, align 4${suffix}
  %c = add ${type} %a, %b
  ret ${type} %c
}
!0 = !{i32 1}`);
    assert.match(text, /%b = load/);
  }
  const text = lower(f, `define i32 @far(ptr %p, i32 %x) {
entry:
  %a = load i32, ptr %p, align 4
  %b = load i32, ptr %p, align 4
${Array.from({ length: 10 }, (_, i) => `  %n${i} = add i32 %x, ${i}`).join('\n')}
  %c = add i32 %a, %b
  ret i32 %c
}
define i32 @edge(ptr %p) {
entry:
  %a = load i32, ptr %p, align 4
  %b = load i32, ptr %p, align 4
  br label %end
end:
  %c = add i32 %a, %b
  ret i32 %c
}`);
  assert.equal((text.match(/%b = load/g) ?? []).length, 2);
  for (const distance of [2, 3]) {
    const at = lower(f, `define i32 @window(ptr %p, i32 %x) {
entry:
  %a = load i32, ptr %p, align 4
${Array.from({ length: distance - 1 }, (_, i) => `  %n${i} = add i32 %x, ${i}`).join('\n')}
  %b = load i32, ptr %p, align 4
  %c = add i32 %a, %b
  ret i32 %c
}`);
    assert.equal(at.includes('%b = load'), distance === 3);
    const uses = lower(f, `define i32 @uses(ptr %p, i32 %x) {
entry:
  %a = load i32, ptr %p, align 4
  %b = load i32, ptr %p, align 4
${Array.from({ length: distance - 1 }, (_, i) => `  %n${i} = add i32 %x, ${i}`).join('\n')}
  %c = add i32 %a, %b
  ret i32 %c
}`);
    assert.equal(uses.includes('%b = load'), distance === 3);
  }
});
test('Cortex-M33 executes general scalar memory semantics through both optimization/backends', { skip: !oracle }, t => {
  const f = files(t), clang = process.env.LLVM_CLANG ?? 'clang', opt = process.env.LLVM_OPT ?? 'opt';
  const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-ffreestanding', '-fno-builtin'];
  const source = `typedef unsigned U;
static U cell; static volatile U observed; static unsigned char byte; static short half;
static U narrow(U x){byte=(unsigned char)x;half=(short)(x&32767u);U a=byte,b=byte,c=half,d=half;return a+b+c+d;}
static U update(U x){cell=x; U a=cell,b=cell; cell=a*1664525u+1013904223u; U c=cell,d=cell; return (a^b)+(c^d)+cell;}
static U alias(U *p,unsigned char *q){*p=0x12345678u; U a=*p; *q=9; return a^*p;}
static U effect(U x){observed=x; cell=x+7; return cell;}
static U calls(U x){cell=x; U a=cell; U b=effect(x); return a+cell+b;}
int main(void){U seed=1;for(U i=0;i<512;i++){seed=seed*1664525u+1013904223u;
if(update(seed)!=seed*1664525u+1013904223u)return 1;
if(narrow(seed)!=2u*(seed&255u)+2u*(seed&32767u))return 4;
U v=0; if(alias(&v,(unsigned char*)&v)!=(0x12345678u^0x12345609u))return 2;
if(calls(seed)!=seed+2u*(seed+7u)||observed!=seed)return 3;}return 0;}`;
  fs.writeFileSync(f('source.c'), source);
  run(tool('badge-ir'), ['--expression-lowering=ssa', f('source.c'), '-o', f('frontend.ll')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', f('start.o')]);
  const execute = object => {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld', f('start.o'), object, '-o', f('run.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', f('run.elf')]);
  };
  run(clang, [...target, '-Oz', '-c', f('source.c'), '-o', f('reference.o')]); execute(f('reference.o'));
  for (const optimizer of ['owned', 'llvm']) {
    run(tool('badge-opt'), ['--passes=mem2reg,scalar-cleanup,constant-fold,local-memory', f('frontend.ll'), '-o', f('memory.ll')]);
    if (optimizer === 'llvm') run(opt, ['-S', '-passes=default<Oz>', f('memory.ll'), '-o', f('optimized.ll')]);
    else run(tool('badge-opt'), [f('memory.ll'), '-o', f('optimized.ll')]);
    run(opt, ['-passes=verify', '-disable-output', f('optimized.ll')]);
    run(clang, [...target, '-Oz', '-c', '-x', 'ir', f('optimized.ll'), '-o', f('llvm.o')]); execute(f('llvm.o'));
    run(tool('badge-codegen'), ['--scalar-selection=thumb-homes', f('optimized.ll'), '-o', f('owned.s')]);
    run(tool('badge-asm'), [f('owned.s'), '-o', f('owned.o')]); execute(f('owned.o'));
  }
});
