import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { decodeElf } from '../tools/boundary-evidence.mjs';
import { verifyAssemblyEffects } from '../tools/assembly-effects-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 60000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stdout}\n${r.stderr}`);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/operand-selection-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
function compile(file, source, selection = 'thumb-operands', residency = 'global-compact') {
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' + source);
  run(tool('badge-codegen-sanitize'), [`--scalar-selection=${selection}`, `--register-residency=${residency}`,
    file('input.ll'), '-o', file(`${selection}.s`)]);
  return fs.readFileSync(file(`${selection}.s`), 'utf8');
}
function execute(file, name, objects = [file('body.o')]) {
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), ...objects, '-o', file(`${name}.elf`)]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${name}.elf`)]);
}
function assemble(file, selection) {
  run(tool('badge-asm'), [file(`${selection}.s`), '-o', file('body.o')]);
}
const bytes = file => decodeElf(fs.readFileSync(file)).sections
  .filter(s => s.flags & 4).reduce((n, s) => n + s.size, 0);
test('related arbitrary addresses retain every volatile width/value/order and shrink final encodings', t => {
  const file = files(t), base = 0x30001000;
  const accesses = [[0, 32, 0x12345678], [4, 8, 255], [6, 16, 65535],
    [4092, 32, 0x76543210], [4096, 32, 0x12345678], [8192, 8, 127], [0, 32, 0xffffffff]];
  let work = '', checks = '', n = 0;
  for (const [offset, bits, value] of accesses) {
    const address = `inttoptr (i32 ${base + offset} to ptr)`;
    work += `store volatile i${bits} ${value}, ptr ${address}, align ${bits / 8}\n`;
    work += `%v${n} = load volatile i${bits}, ptr ${address}, align ${bits / 8}\n`;
    checks += `%ok${n} = icmp eq i${bits} %v${n}, ${value}\n`;
    checks += `%all${n} = and i1 ${n ? `%all${n - 1}` : 'true'}, %ok${n}\n`;
    n++;
  }
  const source = `define i32 @main() {\nentry:\n${work}${checks}
br i1 %all${n - 1}, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  let before;
  for (const selection of ['thumb-control', 'thumb-operands']) {
    const out = compile(file, source, selection);
    const memory = [...out.matchAll(/^(str|ldr)(b|h)?(?:\.w)? r\d+, \[r4(?:, #0)?\]$/gm)]
      .map(m => `${m[1]}${m[2] ?? ''}`);
    assert.deepEqual(memory, accesses.flatMap(([, bits]) => {
      const suffix = bits === 8 ? 'b' : bits === 16 ? 'h' : '';
      return [`str${suffix}`, `ldr${suffix}`];
    }));
    if (selection === 'thumb-operands') assert(out.includes('@ constant-reuse: saved'));
    assemble(file, selection);
    execute(file, selection);
    const size = bytes(file('body.o'));
    if (selection === 'thumb-control') before = size;
    else assert(size < before, `${size} must be smaller than ${before}`);
  }
});
test('allocated byte/halfword loads, stores, unsigned extensions and arithmetic avoid scratch copies', t => {
  const file = files(t);
  const source = `@buffer = global i32 0
define i32 @work(ptr %p, i8 %x, i16 %y) {
entry:
 store volatile i8 %x, ptr %p
 %a = load volatile i8, ptr %p
 %b = add i8 %a, 2
 %wide = zext i8 %b to i32
 %p2 = getelementptr i8, ptr %p, i32 2
 store volatile i16 %y, ptr %p2, align 2
 %half = load volatile i16, ptr %p2, align 2
 %halfwide = zext i16 %half to i32
 %result = add i32 %wide, %halfwide
 ret i32 %result
}
define i32 @main() {
entry:
 %r = call i32 @work(ptr @buffer, i8 255, i16 65535)
 %ok = icmp eq i32 %r, 65536
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const before = compile(file, source, 'thumb-control');
  const after = compile(file, source);
  const body = after.match(/work:\n[^]*?\.size work/)[0];
  assert.equal((body.match(/operand-memory: direct 1/g) ?? []).length, 4);
  assert.equal((body.match(/\bubfx\b/g) ?? []).length, 3, 'normalize incoming narrow values and arithmetic result only');
  assert((body.match(/\bmov r4,/g) ?? []).length < (before.match(/work:\n[^]*?\.size work/)[0].match(/\bmov r4,/g) ?? []).length);
  for (const selection of ['thumb-control', 'thumb-operands']) {
    assemble(file, selection);
    execute(file, selection);
  }
});
test('signed indices, RAM/flash arrays, unaligned and wide fallbacks preserve bytes', t => {
  const file = files(t);
  fs.writeFileSync(file('source.c'), `static const unsigned char table[8] = {9,8,7,6,5,4,3,2};
  static volatile unsigned char ram[8196];
  static unsigned lookup(int index) { return table[index + 3]; }
  int main(void) {
    unsigned i, sum = 0;
    for (i = 0; i < 8; i += 2) { ram[i + 4096] = table[i]; sum += ram[i + 4096]; }
    return sum != 24u || lookup(-3) != 9u || lookup(4) != 2u || ram[4097] != 0u;
  }`);
  run(tool('badge-ir'), [file('source.c'), '-o', file('initial.ll')]);
  run(tool('badge-opt'), [file('initial.ll'), '-o', file('optimized.ll')]);
  const source = fs.readFileSync(file('optimized.ll'), 'utf8');
  for (const residency of ['global-compact', 'local', 'stack']) {
    compile(file, source, 'thumb-operands', residency);
    assemble(file, 'thumb-operands');
    execute(file, residency);
  }
  const fallback = `@buf = global [16 x i8] zeroinitializer
define i32 @main() {
entry:
 %p = getelementptr i8, ptr @buf, i32 1
 store i32 305419896, ptr %p, align 1
 %a = load i32, ptr %p, align 1
 %q = getelementptr i8, ptr @buf, i32 8
 store i64 1234605616436508552, ptr %q, align 4
 %b = load i64, ptr %q, align 4
 %ok1 = icmp eq i32 %a, 305419896
 %ok2 = icmp eq i64 %b, 1234605616436508552
 %all = and i1 %ok1, %ok2
 br i1 %all, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const out = compile(file, fallback);
  assert(!out.includes('operand-memory: direct'));
  assemble(file, 'thumb-operands');
  execute(file, 'fallback');
});
test('calls, CFG joins, IT and opaque assembly end constant availability; NOP memory barriers do not alter pure constants', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define void @callee() {
entry:
 call void asm sideeffect "movw r0, #0\\0Amovw r1, #0\\0Amovw r4, #0", ""()
 ret void
}
define i32 @main() {
entry:
 store volatile i32 305419896, ptr @sink
 call void @callee()
 store volatile i32 305419896, ptr @sink
 call void asm sideeffect "nop", "~{memory}"()
 %r = load volatile i32, ptr @sink
 %ok = icmp eq i32 %r, 305419896
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const out = compile(file, source);
  assert(!out.match(/callee:\n[^]*?\.size callee/)[0].includes('constant-reuse'));
  assemble(file, 'thumb-operands');
  execute(file, 'barriers');
});
test('symbol offsets and high-bit constants assemble identically under owned and LLVM assemblers', t => {
  const file = files(t);
  const source = `@buf = global [8196 x i8] zeroinitializer
define i32 @main() {
entry:
 store volatile i32 2147483648, ptr getelementptr (i8, ptr @buf, i32 4092), align 4
 store volatile i32 4294967295, ptr getelementptr (i8, ptr @buf, i32 4096), align 4
 store volatile i32 2147483648, ptr getelementptr (i8, ptr @buf, i32 0), align 4
 %a = load volatile i32, ptr getelementptr (i8, ptr @buf, i32 4092), align 4
 %b = load volatile i32, ptr getelementptr (i8, ptr @buf, i32 4096), align 4
 %c = load volatile i32, ptr @buf, align 4
 %ab = xor i32 %a, %b
 %r = xor i32 %ab, %c
 %ok = icmp eq i32 %r, 4294967295
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const out = compile(file, source);
  assert.match(out, /(?:addw|subw) r4, r4, #(?:4|4092|4096)/);
  assemble(file, 'thumb-operands');
  execute(file, 'owned');
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('thumb-operands.s'), '-o', file('llvm.o')]);
  execute(file, 'llvm', [file('llvm.o')]);
  const loaded = name => decodeElf(fs.readFileSync(file(`${name}.elf`))).sections
    .filter(s => (s.flags & 2) && s.type !== 8).map(s => {
      assert(Number.isInteger(s.address));
      return { address: s.address, bytes: s.data };
    });
  assert.deepEqual(loaded('owned'), loaded('llvm'));
});
test('narrow modular arithmetic and live-source zero extensions execute at both width boundaries', t => {
  const file = files(t);
  let functions = '', checks = '', n = 0;
  for (const bits of [8, 16]) for (const op of ['add', 'sub', 'mul', 'and', 'or', 'xor', 'shl', 'lshr']) {
    const name = `f${bits}_${op}`, mask = 2 ** bits - 1;
    functions += `define i32 @${name}(i${bits} %x, i${bits} %y) {
entry:
 %n = ${op} i${bits} %x, %y
 %wide = zext i${bits} %n to i32
 %old = zext i${bits} %x to i32
 %r = xor i32 %wide, %old
 ret i32 %r
}\n`;
    for (const x of [0, 1, 2 ** (bits - 1), mask]) for (const y of [0, 1, 7]) {
      const r = op === 'add' ? x + y : op === 'sub' ? x - y : op === 'mul' ? x * y :
        op === 'and' ? x & y : op === 'or' ? x | y : op === 'xor' ? x ^ y :
        op === 'shl' ? x << y : x >>> y;
      checks += `%v${n} = call i32 @${name}(i${bits} ${x === mask ? -1 : x}, i${bits} ${y})
%ok${n} = icmp eq i32 %v${n}, ${(r & mask) ^ x}
%all${n} = and i1 ${n ? `%all${n - 1}` : 'true'}, %ok${n}\n`;
      n++;
    }
  }
  compile(file, functions + `define i32 @main() {\nentry:\n${checks}
br i1 %all${n - 1}, label %yes, label %no\nyes:\nret i32 0\nno:\nret i32 1\n}`);
  assemble(file, 'thumb-operands');
  execute(file, 'narrow');
});
test('direct outgoing scalar arguments and conditional constant availability preserve reached values', t => {
  const file = files(t);
  const source = `define i32 @f(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e) {
entry:\nret i32 %e\n}
define i32 @forward(i32 %x) {
entry:
 %r = call i32 @f(i32 0, i32 1, i32 2, i32 3, i32 %x)
 ret i32 %r
}
define i32 @choose(i1 %flag) {
entry:
 %x = select i1 %flag, i32 305419896, i32 2147483648
 %r = xor i32 %x, 305419896
 ret i32 %r
}
define i32 @main() {
entry:
 %f = call i32 @forward(i32 4294967295)
 %a = call i32 @choose(i1 true)
 %b = call i32 @choose(i1 false)
 %fok = icmp eq i32 %f, 4294967295
 %aok = icmp eq i32 %a, 0
 %bok = icmp eq i32 %b, 2452903544
 %ab = and i1 %aok, %bok
 %all = and i1 %ab, %fok
 br i1 %all, label %yes, label %no\nyes:\nret i32 0\nno:\nret i32 1\n}`;
  const out = compile(file, source);
  assert.match(out.match(/forward:\n[^]*?\.size forward/)[0], /str\.w r(?:8|10|11), \[sp, #0\]/);
  assert.match(out.match(/choose:\n[^]*?\.size choose/)[0], /^it eq$/m);
  assemble(file, 'thumb-operands');
  execute(file, 'outgoing-select');
});
test('mixed LLVM/owned calls preserve narrow normalization, composites, recursion and indirect transport', t => {
  const file = files(t);
  for (const side of ['caller', 'callee']) {
    const flags = side === 'caller' ? ['-DCALLER'] : [];
    const source = 'tests/fixtures/call_transport_abi_fixture.c';
    run(tool('badge-cpp'), [...flags, source, '-o', file(`${side}.i`)]);
    run(tool('badge-ir'), [file(`${side}.i`), '-o', file(`${side}.initial.ll`)]);
    run(tool('badge-opt'), [file(`${side}.initial.ll`), '-o', file(`${side}.ll`)]);
    run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-operands', file(`${side}.ll`), '-o', file(`${side}.s`)]);
    run(tool('badge-asm'), [file(`${side}.s`), '-o', file(`owned-${side}.o`)]);
    run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
      '-ffreestanding', '-fno-builtin', '-Oz', ...flags, '-c', source, '-o', file(`llvm-${side}.o`)]);
  }
  for (const [caller, callee] of [['owned', 'llvm'], ['llvm', 'owned'], ['owned', 'owned']])
    execute(file, `${caller}-${callee}`, [file(`${caller}-caller.o`), file(`${callee}-callee.o`)]);
});
test('bounded analysis and invalid policies preserve output; qualified selector is default', t => {
  const file = files(t);
  assert.equal(run(tool('badge-codegen'), ['--default-scalar-selection']).stdout.trim(), 'thumb-widths');
  fs.writeFileSync(file('input.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"\ndefine i32 @f(i32 %x) {\nentry:\n` +
    Array.from({ length: 300 }, (_, n) => `%v${n} = add i32 %x, ${n}\n`).join('') + 'ret i32 %x\n}');
  fs.writeFileSync(file('preserved.s'), 'preserved');
  const result = run(tool('badge-codegen-branch-budget'), ['--scalar-selection=thumb-operands',
    file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(result.stderr, /work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
});
test('persistent PO14 report recomputes and rejects operand, policy and image tampering',
  { skip: !process.env.PO14_REPORT }, t => {
    const file = files(t);
    const original = verifyAssemblyEffects(path.resolve(root, '..', process.env.PO14_REPORT), true);
    assert.equal(original.milestone, 'PO14');
    assert(original.summary.operands.reused_constants > 0);
    for (const mutate of [r => r.policies.candidate = 'thumb-control',
      r => r.images[0].candidate.text--, r => r.modules[0].candidate.operands.direct_memory++]) {
      const changed = JSON.parse(JSON.stringify(original));
      mutate(changed);
      fs.writeFileSync(file('report.json'), JSON.stringify(changed));
      assert.throws(() => verifyAssemblyEffects(file('report.json')));
    }
  });
