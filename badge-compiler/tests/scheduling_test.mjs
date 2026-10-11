import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../', import.meta.url));
function fixture(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/scheduling-test-'));
  t.after(() => {
    for (const name of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, name));
    fs.rmdirSync(dir);
  });
  return name => path.join(dir, name);
}
const source = `target triple = "thumbv8m.main-unknown-none-eabi"
define i32 @work(i32 %a, i32 %b, i32 %c) {
entry:
%m = mul i32 %a, %b
%u = xor i32 %m, %c
%v = add i32 %a, 100
%w = xor i32 %u, %v
ret i32 %w
}
`;
function run(tool, args) {
  const r = spawnSync(path.join(root, 'build', tool), args, { encoding: 'utf8', timeout: 30000 });
  assert.ifError(r.error);
  return r;
}
test('new CLI/API policy is deterministic, assembles and preserves existing annotations', t => {
  const file = fixture(t);
  fs.writeFileSync(file('input.ll'), source);
  let expected;
  for (const residency of ['stack', 'local', 'global', 'global-flex', 'global-compact']) {
    for (const tool of ['badge-codegen', 'badge-codegen-sanitize']) {
      const r = run(tool, ['--scalar-selection=thumb-schedule', `--register-residency=${residency}`,
        file('input.ll'), '-o', file('out.s')]);
      assert.equal(r.status, 0, r.stderr);
      const text = fs.readFileSync(file('out.s'), 'utf8');
      assert.match(text, /@ local-scheduling: windows \d+ hazards \d+ moved \d+ unfilled \d+/);
      if (tool === 'badge-codegen') expected = text;
      else assert.equal(text, expected);
      const assembled = run('badge-asm', [file('out.s'), '-o', file('out.o')]);
      assert.equal(assembled.status, 0, assembled.stderr);
    }
  }
});
test('budget/duplicate failures preserve output; non-scheduling policies ignore its budget', t => {
  const file = fixture(t);
  fs.writeFileSync(file('input.ll'), source);
  fs.writeFileSync(file('out.s'), 'preserved');
  let r = run('badge-codegen-schedule-budget',
    ['--scalar-selection=thumb-schedule', file('input.ll'), '-o', file('out.s')]);
  assert.equal(r.status, 1);
  assert.match(r.stderr, /local scheduling analysis work limit exceeded/);
  assert.equal(fs.readFileSync(file('out.s'), 'utf8'), 'preserved');
  r = run('badge-codegen', ['--scalar-selection=thumb-schedule',
    '--scalar-selection=thumb-schedule', file('input.ll'), '-o', file('out.s')]);
  assert.equal(r.status, 2);
  assert.equal(fs.readFileSync(file('out.s'), 'utf8'), 'preserved');
  r = run('badge-codegen-schedule-budget', [file('input.ll'), '-o', file('out.s')]);
  assert.equal(r.status, 0, r.stderr);
  assert(!fs.readFileSync(file('out.s'), 'utf8').includes('local-scheduling'));
});
