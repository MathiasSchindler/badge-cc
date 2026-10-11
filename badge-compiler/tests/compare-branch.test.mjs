import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
const triple = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
const options = ['--frame-addressing=direct', '--register-residency=global-compact'];
function files(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/compare-branch-test-'));
  t.after(() => {
    for (const entry of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, entry));
    fs.rmdirSync(directory);
  });
  return name => path.join(directory, name);
}
function run(command, args, success = true) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000,
    maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(result.error);
  if (success) assert.equal(result.status, 0, `${command}\n${result.stdout}\n${result.stderr}`);
  else assert.notEqual(result.status, 0, 'expected explicit failure');
  return result;
}
function compile(file, source, selection = 'thumb-branch', extra = options, executable = 'badge-codegen-sanitize') {
  fs.writeFileSync(file('input.ll'), triple + source);
  run(tool(executable), [...extra, `--scalar-selection=${selection}`, file('input.ll'), '-o', file(`${selection}.s`)]);
  return fs.readFileSync(file(`${selection}.s`), 'utf8');
}
const predicates = ['eq', 'ne', 'slt', 'sle', 'sgt', 'sge', 'ult', 'ule', 'ugt', 'uge'];
const predicateFunction = (predicate, type = 'i32') => `define i32 @compare_${predicate}(${type} %x, ${type} %y) {
entry:
 %c = icmp ${predicate} ${type} %x, %y
 br i1 %c, label %yes, label %no
yes:
 ret i32 1
no:
 ret i32 0
}\n`;

test('all predicates fuse, canonicalization disappears and local veneers preserve distant branches', t => {
  const file = files(t);
  const source = predicates.map(predicate => predicateFunction(predicate)).join('');
  const before = compile(file, source, 'thumb-layout');
  const after = compile(file, source);
  assert.equal((after.match(/@ compare-branch: fused 1/g) ?? []).length, 10);
  assert(!after.includes('ubfx'));
  assert.equal((after.match(/cmp r0, r1/g) ?? []).length, 10);
  assert(!after.includes('cmp r0, #0'));
  assert(after.length < before.length);
  for (const selection of ['thumb-layout', 'thumb-branch'])
    run(tool('badge-asm'), [file(`${selection}.s`), '-o', file(`${selection}.o`)]);
  assert(fs.statSync(file('thumb-branch.o')).size < fs.statSync(file('thumb-layout.o')).size);
  const long = predicateFunction('eq').replace('yes:\n ret i32 1',
    `yes:\n${Array.from({ length: 800 }, (_, i) => `store volatile i32 ${i}, ptr @sink`).join('\n')}\n ret i32 1`);
  compile(file, '@sink = global i32 0\n' + long);
  run(tool('badge-asm'), [file('thumb-branch.s'), '-o', file('long.o')]);
});

test('negative cases retain booleans: extra uses including PHIs, nonadjacent, narrow/wide, assembly and local residency', t => {
  const file = files(t);
  const cases = [
    predicateFunction('eq').replace('yes:\n ret i32 1', 'yes:\n %result = zext i1 %c to i32\n ret i32 %result'),
    predicateFunction('eq').replace('yes:\n ret i32 1',
      'yes:\n store i1 %c, ptr @sink\n ret i32 1'),
    'declare void @consume(i1)\n' + predicateFunction('eq').replace('yes:\n ret i32 1',
      'yes:\n call void @consume(i1 %c)\n ret i32 1'),
    `define i1 @phi_use(i32 %x) {
entry:
 %c = icmp eq i32 %x, 0
 br i1 %c, label %yes, label %no
yes:
 br label %join
no:
 br label %join
join:
 %p = phi i1 [%c, %yes], [false, %no]
 ret i1 %p
}\n`,
    predicateFunction('eq').replace(' br i1', ' store volatile i32 7, ptr @sink\n br i1'),
    predicateFunction('eq', 'i8'),
    predicateFunction('eq', 'i16'),
    predicateFunction('eq', 'i64'),
    predicateFunction('eq').replace(' %c =', ' call void asm sideeffect "", ""()\n %c ='),
    predicateFunction('eq').replace('icmp eq', 'icmp samesign eq'),
  ];
  for (const source of cases) {
    const output = compile(file, '@sink = global i32 0\n' + source);
    assert(output.includes('@ compare-branch: fused 0'));
    assert(output.includes('it eq'), 'required boolean must be materialized');
  }
  for (const residency of ['stack', 'local', 'global', 'global-flex'])
    assert(compile(file, predicateFunction('eq'), 'thumb-branch',
      ['--frame-addressing=direct', `--register-residency=${residency}`]).includes('@ compare-branch: fused 0'));
  for (const residency of ['global-compact']) {
    const output = compile(file, predicateFunction('eq'), 'thumb-branch',
      ['--frame-addressing=direct', `--register-residency=${residency}`]);
    assert(output.includes('@ compare-branch: fused 1'), `${residency}\n${output}`);
  }
});

function expected(predicate, x, y) {
  if (predicate[0] === 's') { x |= 0; y |= 0; }
  if (predicate === 'eq') return Number(x === y);
  if (predicate === 'ne') return Number(x !== y);
  if (predicate.endsWith('lt')) return Number(x < y);
  if (predicate.endsWith('le')) return Number(x <= y);
  if (predicate.endsWith('gt')) return Number(x > y);
  return Number(x >= y);
}

test('Cortex-M33 executes all boundary predicates, volatile operands and cyclic PHI edges', t => {
  const file = files(t);
  const values = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 65535, 65536];
  let checks = '', cases = 0;
  for (const predicate of predicates) for (const x of values) for (const y of values) {
    const id = cases++;
    checks += `%r${id} = call i32 @compare_${predicate}(i32 ${x}, i32 ${y})\n`;
    checks += `%e${id} = icmp eq i32 %r${id}, ${expected(predicate, x, y)}\n`;
    checks += `%ok${id} = and i1 ${id ? `%ok${id - 1}` : 'true'}, %e${id}\n`;
  }
  const source = predicates.map(predicate => predicateFunction(predicate)).join('') + `
@sensor = global i32 4294967295
define i32 @volatile_work() {
entry:
 %x = load volatile i32, ptr @sensor
 %c = icmp slt i32 %x, 0
 br i1 %c, label %yes, label %no
yes:
 ret i32 1
no:
 ret i32 0
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
 %sum = add i32 %v, %b
 ret i32 %sum
}
define i32 @main() {
entry:
 ${checks}
 %v = call i32 @volatile_work()
 %valid_v = icmp eq i32 %v, 1
 %p = call i32 @cycle()
 %valid_p = icmp eq i32 %p, 21
 %both = and i1 %valid_p, %valid_v
 %all = and i1 %ok${cases - 1}, %both
 br i1 %all, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}`;
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const selection of ['thumb-layout', 'thumb-branch', 'thumb-homes']) {
    compile(file, source, selection);
    run(tool('badge-asm'), [file(`${selection}.s`), '-o', file(`${selection}.o`)]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld', file('start.o'),
      file(`${selection}.o`), '-o', file(`${selection}.elf`)]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file(`${selection}.elf`)]);
  }
  assert.equal(cases, 640);
});

test('selection and budget failures preserve existing output; promoted selector is default', t => {
  const file = files(t);
  assert.equal(run(tool('badge-codegen'), ['--default-scalar-selection']).stdout.trim(), 'thumb-widths');
  const source = predicates.map(predicate => predicateFunction(predicate)).join('');
  fs.writeFileSync(file('input.ll'), triple + source);
  fs.writeFileSync(file('preserved.s'), 'preserved');
  const result = run(tool('badge-codegen-branch-budget'),
    [...options, '--scalar-selection=thumb-branch', file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(result.stderr, /work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
  const invalid = run(tool('badge-codegen'), ['--scalar-selection=thumb-branch',
    '--scalar-selection=thumb-branch', file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(invalid.stderr, /duplicate/);
  const homes = run(tool('badge-codegen-branch-budget'),
    [...options, '--scalar-selection=thumb-homes', file('input.ll'), '-o', file('preserved.s')], false);
  assert.match(homes.stderr, /work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'preserved');
});
