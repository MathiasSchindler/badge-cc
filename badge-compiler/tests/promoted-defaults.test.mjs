import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const passes = 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function';
function run(name, args) {
  const result = spawnSync(path.join(root, 'build', name), args,
    { cwd: root, encoding: 'utf8', timeout: 30000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  return result.stdout.trim();
}
test('default queries and emitted objects equal the explicitly qualified profile', () => {
  assert.equal(run('badge-ir', ['--default-expression-lowering']), 'ssa');
  assert.equal(run('badge-opt', ['--default-passes']), passes);
  assert.equal(run('badge-codegen', ['--default-scalar-selection']), 'thumb-widths');
  const directory = fs.mkdtempSync(path.join(root, 'build/default-promotion-'));
  try {
    const file = name => path.join(directory, name);
    fs.writeFileSync(file('source.c'), `
extern int callback(int,int,int,int,int);
int exercise(int *p, int a, int b) {
  int x = *p;
  int y = *p;
  return (a < b && x) ? callback(a,b,x,y,a+b) : x+y;
}
`);
    run('badge-cpp', [file('source.c'), '-o', file('input.i')]);
    for (const explicit of [false, true]) {
      const name = explicit ? 'explicit' : 'default';
      run('badge-ir', [...(explicit ? ['--expression-lowering=ssa'] : []),
        file('input.i'), '-o', file(`${name}.ll`)]);
      run('badge-opt', [...(explicit ? [`--passes=${passes}`] : []),
        file(`${name}.ll`), '-o', file(`${name}.opt.ll`)]);
      run('badge-codegen', [...(explicit ? ['--frame-addressing=direct',
        '--register-residency=global-compact', '--scalar-selection=thumb-widths',
        '--memory-selection=word'] : []), file(`${name}.opt.ll`), '-o', file(`${name}.s`)]);
      run('badge-asm', [file(`${name}.s`), '-o', file(`${name}.o`)]);
    }
    for (const suffix of ['ll', 'opt.ll', 's', 'o'])
      assert.deepEqual(fs.readFileSync(file(`default.${suffix}`)), fs.readFileSync(file(`explicit.${suffix}`)));
  } finally {
    for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
    fs.rmdirSync(directory);
  }
});

test('target-costed defaults diagnose unsupported forced-inline IR; the previous explicit pipeline remains available', () => {
  const directory = fs.mkdtempSync(path.join(root, 'build/default-contract-'));
  try {
    const input = path.join(directory, 'input.ll'), output = path.join(directory, 'output.ll');
    fs.writeFileSync(input, `target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
define i32 @f(i32 %x) alwaysinline {
entry:
  ret i32 %x
}
`);
    fs.writeFileSync(output, 'preserved');
    const result = spawnSync(path.join(root, 'build/badge-opt'), [input, '-o', output],
      { cwd: root, encoding: 'utf8', timeout: 30000 });
    assert.ifError(result.error);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /alwaysinline is outside the supported/);
    assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
    run('badge-opt', [`--passes=${passes.split(',').slice(0, 6).join(',')}`, input, '-o', output]);
    assert.match(fs.readFileSync(output, 'utf8'), /alwaysinline/);
  } finally {
    for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
    fs.rmdirSync(directory);
  }
});
