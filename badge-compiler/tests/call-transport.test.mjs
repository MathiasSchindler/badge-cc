import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { callTransportChanges } from '../tools/call-evidence.mjs';
import { traceFunctions } from '../tools/backend-attribution.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const triple = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function run(name, args, env = process.env) {
  const result = spawnSync(name, args, { cwd: root, env, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  return result;
}
function fixture(t, text, residency = 'global-compact') {
  const directory = fs.mkdtempSync(path.join(root, 'build/call-transport-'));
  t.after(() => {
    for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
    fs.rmdirSync(directory);
  });
  const input = path.join(directory, 'input.ll'), ir = triple + text;
  fs.writeFileSync(input, ir);
  return ['thumb-homes', 'thumb-calls'].map(selection => {
    const output = path.join(directory, `${selection}.s`);
    const args = [`--scalar-selection=${selection}`, `--register-residency=${residency}`, input, '-o', output];
    let result = run('build/badge-codegen-attribution', args);
    assert.equal(result.status, 0, result.stderr);
    const assembly = fs.readFileSync(output, 'utf8');
    const object = path.join(directory, `${selection}.o`);
    result = run('build/badge-asm', [output, '-o', object]);
    assert.equal(result.status, 0, result.stderr);
    const bytes = fs.readFileSync(object);
    result = run('build/badge-codegen-sanitize', args);
    assert.equal(result.status, 0, result.stderr);
    result = run('build/badge-asm', [output, '-o', object]);
    assert.equal(result.status, 0, result.stderr);
    assert.deepEqual(fs.readFileSync(object), bytes, 'sanitized/trace object equality');
    return { assembly, functions: traceFunctions(assembly, ir) };
  });
}
const scalar = `declare i32 @sink(i32, i32, i32, i32, i32, i32)
define i32 @f(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f) {
entry:
 %q = call i32 @sink(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f)
 %sum = add i32 %q, %a
 ret i32 %sum
}
`;
test('register capture and stack stores change only call transport, not allocation or memory traffic', t => {
  const [control, candidate] = fixture(t, scalar);
  const change = callTransportChanges(control.functions, candidate.functions)[0];
  assert.equal(change.control.entry_instructions - change.candidate.entry_instructions, 4);
  assert.equal(change.control.call_transport_instructions - change.candidate.call_transport_instructions, 4);
  assert.match(candidate.assembly, /entry-copies-removed 4 stack-addresses-removed 2/);
  assert.match(candidate.assembly, /str\.w r5, \[sp, #0\]/);
  assert.match(candidate.assembly, /str\.w r5, \[sp, #4\]/);
  const broken = structuredClone(candidate.functions);
  broken[0].saved_bytes += 8;
  assert.throws(() => callTransportChanges(control.functions, broken), /callee saves/);
  for (const residency of ['stack', 'local', 'global', 'global-flex']) {
    const [a, b] = fixture(t, scalar, residency);
    assert.equal(a.assembly, b.assembly, `inactive candidate: ${residency}`);
  }
});
test('SP immediate threshold, wide alignment, split composites and byval snapshots retain fallbacks', t => {
  const [control, candidate] = fixture(t, `declare void @big([1030 x i32])
define void @f([1030 x i32] %p) {
entry:
 call void @big([1030 x i32] %p)
 ret void
}`);
  callTransportChanges(control.functions, candidate.functions);
  assert.match(candidate.assembly, /str\.w r5, \[sp, #4092\]/);
  assert.doesNotMatch(candidate.assembly, /str\.w r5, \[sp, #4096\]/);
  assert.match(candidate.assembly, /movw r12, #4096\nadd\.w r12, sp, r12\nstr\.w r5, \[r12\]/);
  assert.match(candidate.assembly, /entry-copies-removed 4 stack-addresses-removed 1024/);
  const [a, b] = fixture(t, `define i32 @split(i32 %a, i32 %b, i32 %c, ptr byval([7 x i8]) align 4 %p) {
entry:
 %v = load i8, ptr %p, align 1
 %n = zext i8 %v to i32
 %sum = add i32 %a, %n
 ret i32 %sum
}`);
  callTransportChanges(a.functions, b.functions);
  assert.match(b.assembly, /mov r5, r3/);
  assert.match(b.assembly, /entry-copies-removed 3 stack-addresses-removed 0/);
});
test('candidate work exhaustion, invalid selections and duplicates fail without replacing output', t => {
  const directory = fs.mkdtempSync(path.join(root, 'build/call-budget-'));
  t.after(() => {
    for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
    fs.rmdirSync(directory);
  });
  const input = path.join(directory, 'input.ll'), output = path.join(directory, 'output.s');
  fs.writeFileSync(input, triple + scalar.replaceAll('%a', `%${'a'.repeat(2048)}`));
  fs.writeFileSync(output, 'unchanged');
  for (const [tool, flags] of [
    ['badge-codegen', ['--scalar-selection=thumb-calls', '--scalar-selection=thumb-calls']],
    ['badge-codegen', ['--scalar-selection=thumb-call']],
    ['badge-codegen-branch-budget', ['--scalar-selection=thumb-calls']],
  ]) {
    const result = run(`build/${tool}`, [...flags, input, '-o', output]);
    assert.notEqual(result.status, 0, result.stderr);
    assert.match(result.stderr, /selection|work limit/);
    assert.equal(fs.readFileSync(output, 'utf8'), 'unchanged');
  }
});
test('bidirectional small/wide/aggregate ABI, callbacks and large-frame target execution', {
  skip: process.env.BADGE_TEST_MODE === 'owned',
}, () => {
  for (const optimizer of ['owned', 'llvm']) {
    const result = run('bash', ['tests/codegen_oracle.sh'], { ...process.env,
      IR_OPTIMIZER: optimizer, BADGE_IR_EXPRESSION_LOWERING: 'ssa',
      BADGE_CODEGEN_SCALAR_SELECTION: 'thumb-calls' });
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.match(result.stdout, /bidirectional mixed LLVM\/owned AAPCS calls PASS/);
  }
});
