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
  const dir = fs.mkdtempSync(path.join(root, 'build/control-selection-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
function compile(file, source, selection = 'thumb-control') {
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' + source);
  run(tool('badge-codegen-sanitize'), [`--scalar-selection=${selection}`, file('input.ll'), '-o', file(`${selection}.s`)]);
  return fs.readFileSync(file(`${selection}.s`), 'utf8');
}
const predicate = (name, type, pred) => `define i32 @${name}(${type} %x, ${type} %y) {
entry:
 %c = icmp ${pred} ${type} %x, %y
 br i1 %c, label %yes, label %no
yes:
 ret i32 1
no:
 ret i32 0
}\n`;
test('direct narrow/pointer predicates execute signed, unsigned and high-bit boundaries without booleans', t => {
  const file = files(t);
  const predicates = ['eq', 'ne', 'slt', 'sle', 'sgt', 'sge', 'ult', 'ule', 'ugt', 'uge'];
  let functions = '', checks = '', n = 0;
  const expected = (p, x, y) => p === 'eq' ? +(x === y) : p === 'ne' ? +(x !== y) :
    p.endsWith('lt') ? +(x < y) : p.endsWith('le') ? +(x <= y) : p.endsWith('gt') ? +(x > y) : +(x >= y);
  for (const bits of [8, 16]) for (const pred of predicates) {
    const name = `p${bits}_${pred}`, mask = 2 ** bits - 1;
    functions += predicate(name, `i${bits}`, pred);
    for (const x of [0, 1, 2 ** (bits - 1), mask]) for (const y of [0, 1, 2 ** (bits - 1), mask]) {
      const signed = v => pred[0] === 's' && v >= 2 ** (bits - 1) ? v - 2 ** bits : v;
      checks += `%r${n} = call i32 @${name}(i${bits} ${x}, i${bits} ${y})\n%e${n} = icmp eq i32 %r${n}, ${expected(pred, signed(x), signed(y))}\n%ok${n} = and i1 ${n ? `%ok${n - 1}` : 'true'}, %e${n}\n`;
      n++;
    }
  }
  for (const pred of predicates.filter(p => p[0] !== 's')) {
    functions += predicate(`ptr_${pred}`, 'ptr', pred);
    for (const [x, y] of [[0, 0], [0x80000000, 1], [0xffffffff, 0x80000000]]) {
      checks += `%x${n} = inttoptr i32 ${x} to ptr\n%y${n} = inttoptr i32 ${y} to ptr\n%r${n} = call i32 @ptr_${pred}(ptr %x${n}, ptr %y${n})\n%e${n} = icmp eq i32 %r${n}, ${expected(pred, x, y)}\n%ok${n} = and i1 %ok${n - 1}, %e${n}\n`;
      n++;
    }
  }
  const source = functions + `define i32 @main() {\nentry:\n${checks}\nbr i1 %ok${n - 1}, label %yes, label %no\nyes:\nret i32 0\nno:\nret i32 1\n}`;
  const out = compile(file, source);
  assert.equal((out.match(/@ compare-branch: fused 1/g) ?? []).length, 26);
  assert(!/\bit (?:lo|lt|eq)\b/.test(out.slice(0, out.indexOf('\nmain:'))));
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('thumb-control.s'), '-o', file('body.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'), file('body.o'), '-o', file('test.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
    '-semihosting-config', 'enable=on,target=native', '-kernel', file('test.elf')]);
});
test('countdown preserves zero-trip, postdecrement exit values, live sources and wrap semantics', t => {
  const file = files(t);
  const source = `
@sink = global i32 0
define i32 @post(i32 %n) {
entry:
 br label %test
test:
 %x = phi i32 [%n, %entry], [%next, %body]
 %next = sub i32 %x, 1
 %c = icmp ne i32 %x, 0
 br i1 %c, label %body, label %done
body:
 store volatile i32 %x, ptr @sink
 br label %test
done:
 ret i32 %next
}
define i32 @count(i32 %n) {
entry:
 %zero = icmp eq i32 %n, 0
 br i1 %zero, label %zeroexit, label %loop
loop:
 %x = phi i32 [%n, %entry], [%next, %loop]
 %next = sub i32 %x, 1
 %c = icmp ne i32 %next, 0
 br i1 %c, label %loop, label %done
done:
 ret i32 %next
zeroexit:
 ret i32 0
}
define i32 @once(i32 %n) {
entry:
 %next = sub i32 %n, 1
 %c = icmp eq i32 %next, 0
 br i1 %c, label %yes, label %no
yes:
 ret i32 %next
no:
 ret i32 %next
}
define i32 @live(i32 %n) {
entry:
 %next = sub i32 %n, 1
 %c = icmp eq i32 %next, 0
 br i1 %c, label %yes, label %no
yes:
 %sum = add i32 %next, %n
 ret i32 %sum
no:
 %difference = xor i32 %next, %n
 ret i32 %difference
}
define i32 @main() {
entry:
 %a = call i32 @post(i32 0)
 %aok = icmp eq i32 %a, 4294967295
 %b = call i32 @post(i32 1)
 %bok = icmp eq i32 %b, 4294967295
 %c = call i32 @count(i32 0)
 %cok = icmp eq i32 %c, 0
 %d = call i32 @count(i32 1)
 %dok = icmp eq i32 %d, 0
 %e = call i32 @count(i32 31)
 %eok = icmp eq i32 %e, 0
 %large = call i32 @count(i32 65536)
 %largeok = icmp eq i32 %large, 0
 %livezero = call i32 @live(i32 0)
 %livezerook = icmp eq i32 %livezero, 4294967295
 %liveone = call i32 @live(i32 1)
 %liveoneok = icmp eq i32 %liveone, 1
 %livehigh = call i32 @live(i32 2147483648)
 %livehighok = icmp eq i32 %livehigh, 4294967295
 %f = call i32 @once(i32 0)
 %fok = icmp eq i32 %f, 4294967295
 %g = call i32 @once(i32 2147483648)
 %gok = icmp eq i32 %g, 2147483647
 %h = call i32 @once(i32 4294967295)
 %hok = icmp eq i32 %h, 4294967294
 %ab = and i1 %aok, %bok
 %cd = and i1 %cok, %dok
 %ef = and i1 %eok, %fok
 %gh = and i1 %gok, %hok
 %abcd = and i1 %ab, %cd
 %efgh = and i1 %ef, %gh
 %allsmall = and i1 %abcd, %efgh
 %allcounts = and i1 %allsmall, %largeok
 %liveab = and i1 %livezerook, %liveoneok
 %liveabc = and i1 %liveab, %livehighok
 %all = and i1 %allcounts, %liveabc
 br i1 %all, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}`;
  const after = compile(file, source);
  assert.equal((after.match(/@ countdown-branch: fused 1/g) ?? []).length, 3);
  const countBody = after.match(/count:\n[^]*?\.size count/)[0];
  assert(countBody.includes('subs.w'));
  assert.equal((countBody.match(/\bcmp\b/g) ?? []).length, 1, 'only the original zero-trip guard compares');
  assert(!/mov r6, r/.test(countBody));
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const selection of ['thumb-frames', 'thumb-control']) {
    compile(file, source, selection);
    run(tool('badge-asm'), [file(`${selection}.s`), '-o', file('body.o')]);
    fs.copyFileSync(file('body.o'), file(`${selection}.o`));
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'), file('body.o'), '-o', file('test.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
      '-semihosting-config', 'enable=on,target=native', '-kernel', file('test.elf')]);
  }
  const bytes = selection => decodeElf(fs.readFileSync(file(`${selection}.o`))).sections
    .filter(s => s.flags & 4).reduce((n, s) => n + s.size, 0);
  assert(bytes('thumb-control') < bytes('thumb-frames'), 'final encodings must shrink');
});
test('effectful CFG cleanup keeps calls/volatile order and PHI chains; flag hazards cannot fuse', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define void @touch() {\nentry:\nstore volatile i32 1, ptr @sink\nret void\n}
define i32 @main() {
entry:
 call void @touch()
 br label %chain
chain:
 br label %work
work:
 %before = load volatile i32, ptr @sink
 store volatile i32 2, ptr @sink
 %c = icmp eq i32 %before, 1
 call void asm sideeffect "adds r0, #1", "~{cc}"()
 br i1 %c, label %yes, label %no
yes:
 %p = phi i32 [0, %work]
 ret i32 %p
no:
 ret i32 1
}`;
  const out = compile(file, source);
  assert(!out.includes('b.w .Lcg_2_chain'));
  assert(out.includes('it eq'));
  assert(out.includes('bl touch'));
  assert(out.includes('@ branch-layout: active 1'));
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('thumb-control.s'), '-o', file('body.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'), file('body.o'), '-o', file('test.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
    '-semihosting-config', 'enable=on,target=native', '-kernel', file('test.elf')]);
});
test('source-level nested loops and early exits preserve reached volatile work and observable counters', t => {
  const file = files(t);
  fs.writeFileSync(file('source.c'), `static volatile unsigned observed;
  static unsigned nested(unsigned n) {
    unsigned outer = n, total = 0;
    while (outer != 0u) {
      unsigned inner = outer;
      while (inner-- != 0u) {
        observed = observed + 1u;
        total++;
        if (inner == 2u) break;
      }
      outer--;
    }
    return total;
  }
  int main(void) {
    unsigned zero = nested(0u);
    unsigned one = nested(1u);
    unsigned many = nested(5u);
    return zero != 0u || one != 1u || many != 9u || observed != 10u;
  }`);
  run(tool('badge-ir'), [file('source.c'), '-o', file('source.initial.ll')]);
  run(tool('badge-opt'), [file('source.initial.ll'), '-o', file('source.ll')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const selection of ['thumb-frames', 'thumb-control']) {
    run(tool('badge-codegen-sanitize'), [`--scalar-selection=${selection}`, file('source.ll'), '-o', file(`${selection}.s`)]);
    run(tool('badge-asm'), [file(`${selection}.s`), '-o', file('body.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'), file('body.o'), '-o', file('test.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
      '-semihosting-config', 'enable=on,target=native', '-kernel', file('test.elf')]);
  }
});
test('extra boolean uses, narrow decrements, intervening effects and analysis bounds retain protection', t => {
  const file = files(t);
  for (const [type, effect] of [['i8', ''], ['i32', 'call void asm sideeffect "nop", "~{memory}"()']]) {
    const source = `define ${type} @f(${type} %x) {
entry:
 %next = sub ${type} %x, 1
 ${effect}
 %c = icmp ne ${type} %next, 0
 br i1 %c, label %yes, label %no
yes:
 ret ${type} %next
no:
 ret ${type} %next
}`;
    assert(!compile(file, source).includes('countdown-branch'));
  }
  const boolean = predicate('used', 'i8', 'eq').replace('ret i32 1', '%r = zext i1 %c to i32\nret i32 %r');
  assert(compile(file, boolean).includes('@ compare-branch: fused 0'));
  fs.writeFileSync(file('input.ll'), 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
    Array.from({ length: 50 }, (_, n) => predicate(`p${n}`, 'i16', 'eq')).join(''));
  fs.writeFileSync(file('preserved.s'), 'preserved');
  const error = run(tool('badge-codegen-branch-budget'), ['--scalar-selection=thumb-control',
    file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(error.stderr, /work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
});
test('branch-only cycles terminate analysis and chains preserve PHI predecessor transport', t => {
  const file = files(t);
  const source = `define void @cycle() {
entry:
 br label %a
a:
 br label %b
b:
 br label %a
}
define i32 @main() {
entry:
 br label %chain
spare:
 ret i32 1
chain:
 br label %join
join:
 %p = phi i32 [0, %chain]
 ret i32 %p
}`;
  const out = compile(file, source);
  assert.match(out, /b(?:\.w)? \.Lcg_2_chain/);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(tool('badge-asm'), [file('thumb-control.s'), '-o', file('body.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'), file('body.o'), '-o', file('test.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
    '-semihosting-config', 'enable=on,target=native', '-kernel', file('test.elf')]);
});
test('control layout preserves mixed LLVM/owned calls including recursion, indirect calls and composites', t => {
  const file = files(t);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const side of ['caller', 'callee']) {
    const flags = side === 'caller' ? ['-DCALLER'] : [];
    const source = 'tests/fixtures/call_transport_abi_fixture.c';
    run(tool('badge-cpp'), [...flags, source, '-o', file(`${side}.i`)]);
    run(tool('badge-ir'), [file(`${side}.i`), '-o', file(`${side}.initial.ll`)]);
    run(tool('badge-opt'), [file(`${side}.initial.ll`), '-o', file(`${side}.ll`)]);
    run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-control', file(`${side}.ll`), '-o', file(`${side}.s`)]);
    run(tool('badge-asm'), [file(`${side}.s`), '-o', file(`owned-${side}.o`)]);
    run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
      '-ffreestanding', '-fno-builtin', '-Oz', ...flags, '-c', source, '-o', file(`llvm-${side}.o`)]);
  }
  for (const [caller, callee] of [['owned', 'llvm'], ['llvm', 'owned'], ['owned', 'owned']]) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld', file('start.o'),
      file(`${caller}-caller.o`), file(`${callee}-callee.o`), '-o', file('test.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
      '-semihosting-config', 'enable=on,target=native', '-kernel', file('test.elf')]);
  }
});
test('persistent PO13 report recomputes and rejects policy and byte tampering',
  { skip: !process.env.PO13_REPORT }, t => {
    const file = files(t);
    const original = verifyAssemblyEffects(path.resolve(root, '..', process.env.PO13_REPORT), true);
    assert.equal(original.milestone, 'PO13');
    for (const mutate of [r => r.policies.candidate = 'thumb-frames',
      r => r.images[0].candidate.text--, r => r.modules[0].candidate.totals.fused++]) {
      const changed = JSON.parse(JSON.stringify(original));
      mutate(changed);
      fs.writeFileSync(file('report.json'), JSON.stringify(changed));
      assert.throws(() => verifyAssemblyEffects(file('report.json')));
    }
  });
