import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';

const target = 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
  'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n';
const fixture = (name, wide = false) => `
define i32 @${name}(i16 signext %x, i32 %y, i64 %a, i64 %b) {
entry:
  %nx = sext i16 %x to i32
  %r = srem i32 %nx, %y
  %xx = sext i16 %x to i32
  %q = sdiv i32 %xx, %y
  %out = xor i32 %q, %r
${wide ? '  %unsupported = udiv i64 %a, %b\n  %narrow = trunc i64 %unsupported to i32\n  %result = add i32 %out, %narrow\n  ret i32 %result' : '  ret i32 %out'}
}
`;
function files(t) {
  const dir = fs.mkdtempSync(path.resolve('build/parallel-admission-'));
  t.after(() => {
    for (const name of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir,name));
    fs.rmdirSync(dir);
  });
  return name => path.join(dir,name);
}
function run(tool, jobs, input, output) {
  const p = spawnSync(`./build/${tool}`, ['--passes=divrem-pairs', input, '-o', output],
    {env:{...process.env,BADGE_ADMISSION_JOBS:jobs},encoding:'utf8',timeout:20000});
  assert.ifError(p.error);
  return p;
}

test('parallel admission makes the same decisions across repeated jobs', t => {
  const f=files(t);
  fs.writeFileSync(f('input.ll'), target + Array.from({length:12},(_,i)=>fixture(`pair${i}`)).join(''));
  const serial=run('badge-opt','1',f('input.ll'),f('serial.ll'));
  assert.equal(serial.status,0,serial.stderr);
  const expected=fs.readFileSync(f('serial.ll'),'utf8');
  assert.equal(expected.match(/sdiv i32 %nx, %y/g)?.length,12);
  for (const tool of ['badge-opt-parallel','badge-opt-parallel-sanitize']) {
    for (let i=0;i<4;i++) {
      const parallel=run(tool,'2',f('input.ll'),f('parallel.ll'));
      assert.equal(parallel.status,0,parallel.stderr);
      assert.equal(fs.readFileSync(f('parallel.ll'),'utf8'),expected);
    }
  }
});

test('failed parallel cost trials preserve the serial diagnostics and destination', t => {
  const f=files(t);
  fs.writeFileSync(f('input.ll'),target+fixture('unsupported',true));
  fs.writeFileSync(f('output.ll'),'preserved');
  const baseline=run('badge-opt-parallel-sanitize','1',f('input.ll'),f('output.ll'));
  assert.notEqual(baseline.status,0);
  assert.match(baseline.stderr,/loop-scalar backend admission failed/);
  for (let i=0;i<3;i++) {
    const failed=run('badge-opt-parallel-sanitize','2',f('input.ll'),f('output.ll'));
    assert.equal(failed.status,baseline.status);
    assert.equal(failed.stderr,baseline.stderr);
    assert.equal(fs.readFileSync(f('output.ll'),'utf8'),'preserved');
  }
  const invalid=run('badge-opt-parallel','3',f('input.ll'),f('output.ll'));
  assert.match(invalid.stderr,/BADGE_ADMISSION_JOBS must be 1 or 2/);
  const notBuilt=run('badge-opt','2',f('input.ll'),f('output.ll'));
  assert.match(notBuilt.stderr,/requires badge-opt-parallel/);
});
