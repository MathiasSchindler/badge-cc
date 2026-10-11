import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { traceFunctions } from '../tools/backend-attribution.mjs';
import { scalarHomeChanges } from '../tools/scalar-home-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const triple = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function execute(name, args) {
  const r = spawnSync(path.join(root, 'build', name), args,
    { cwd: root, encoding: 'utf8', timeout: 60000 });
  assert.ifError(r.error);
  assert.equal(r.status, 0, r.stderr);
}
function fixture(t, ir, residency = 'global-compact') {
  const dir = fs.mkdtempSync(path.join(root, 'build/scalar-homes-'));
  t.after(() => { for (const f of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, f)); fs.rmdirSync(dir); });
  const input = path.join(dir, 'input.ll');
  fs.writeFileSync(input, triple + ir);
  return ['thumb-branch', 'thumb-homes'].map(selection => {
    const file = path.join(dir, `${selection}.s`);
    const args = [`--scalar-selection=${selection}`, `--register-residency=${residency}`, input, '-o', file];
    execute('badge-codegen-attribution', args);
    const assembly = fs.readFileSync(file, 'utf8');
    const object = path.join(dir, `${selection}.o`);
    execute('badge-asm', [file, '-o', object]);
    execute('badge-codegen-sanitize', args);
    const normal = path.join(dir, `${selection}.normal.o`);
    execute('badge-asm', [file, '-o', normal]);
    assert.deepEqual(fs.readFileSync(object), fs.readFileSync(normal));
    return { assembly, functions: traceFunctions(assembly, triple + ir) };
  });
}
const loop = `define i32 @loop(i32 %limit) {
entry:
 br label %body
body:
 %a = phi i32 [1, %entry], [%b, %body]
 %b = phi i32 [2, %entry], [%a, %body]
 %n = phi i32 [0, %entry], [%next, %body]
 %next = add i32 %n, 1
 %c = icmp ult i32 %next, %limit
 br i1 %c, label %body, label %done
done:
 %sum = add i32 %a, %b
 ret i32 %sum
}\n`;
test('allocated homes and register/constant PHI buffers disappear without allocation changes', t => {
  const [a, b] = fixture(t, loop);
  assert.match(b.assembly, /@ scalar-homes: omitted [1-9]/);
  const x = a.functions[0], y = b.functions[0];
  assert.deepEqual(x.registers, y.registers);
  assert.equal(scalarHomeChanges([x], [y])[0].candidate.omitted_scalar_homes,
    y.slots.filter(s => s.offset === 0xffffffff).length);
  const wrong = structuredClone(y);
  wrong.frame_bytes = x.frame_bytes + 8;
  assert.throws(() => scalarHomeChanges([x], [wrong]), /frame growth/);
  assert(y.frame_bytes < x.frame_bytes);
  assert.equal(y.phi.size, 0);
  assert(x.phi.size > 0);
  const traffic = f => f.operations.flatMap(o => o.assembly).filter(s => /^(ldr|str)/.test(s)).length;
  assert(traffic(y) < traffic(x));
  for (const slot of y.slots)
    if (y.registers[slot.name]) assert.equal(slot.offset, 0xffffffff);
});
test('inline assembly live values retain spill homes and real private objects remain reserved', t => {
  const [a, b] = fixture(t, `define i32 @f(i32 %x) {
entry:
 %p = alloca [32 x i8], align 8
 store volatile i32 %x, ptr %p
 call void asm sideeffect "mov.w r8, #0", "~{memory}"()
 %v = load volatile i32, ptr %p
 %sum = add i32 %x, %v
 ret i32 %sum
}`);
  const x = a.functions[0], y = b.functions[0];
  assert.deepEqual(x.registers, y.registers);
  assert(y.slots.find(s => s.name === '%x').offset !== 0xffffffff);
  assert.equal(y.slots.find(s => s.name === '%p').object_size, 32);
  assert.match(b.assembly, /asm-spills [1-9]/);
  assert(y.frame_bytes <= x.frame_bytes);
});
test('wide PHIs retain buffers and other residency policies retain ordinary lowering', t => {
  const [a, b] = fixture(t, `define i64 @f(i64 %x) {
entry:
 br label %join
join:
 %p = phi i64 [%x, %entry]
 ret i64 %p
}`);
  assert.equal(a.functions[0].phi.size, b.functions[0].phi.size);
  assert(b.functions[0].phi.size > 0);
  for (const residency of ['stack', 'local', 'global', 'global-flex']) {
    const [x, y] = fixture(t, loop, residency);
    assert.equal(x.assembly, y.assembly);
  }
});
test('narrow/null constants are deferred but symbol and undefined PHI inputs retain buffers', t => {
  for (const [type, value] of [['i1', 'true'], ['i8', '-128'], ['i16', '65535'], ['ptr', 'null']]) {
    const [a, b] = fixture(t, `define ${type} @f() {
entry:
 br label %join
join:
 %p = phi ${type} [${value}, %entry]
 ret ${type} %p
}`);
    assert.equal(b.functions[0].phi.size, 0);
    assert(a.functions[0].phi.size > 0);
    assert(!b.functions[0].operations.flatMap(o => o.assembly).some(s => /^(?:ldr|str)/.test(s)));
  }
  for (const [type, value, global] of [['ptr', '@g', '@g = global i32 1\n'], ['i32', 'undef', '']]) {
    const [, b] = fixture(t, `${global}define ${type} @f() {
entry:
 br label %join
join:
 %p = phi ${type} [${value}, %entry]
 ret ${type} %p
}`);
    assert(b.functions[0].phi.size > 0);
  }
});
