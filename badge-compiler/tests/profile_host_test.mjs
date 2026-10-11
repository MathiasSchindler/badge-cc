import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
function fixture(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/profile-host-test-'));
  const module = path.join(dir, 'one.o.owned');
  fs.mkdirSync(module);
  const input = path.join(module, 'optimized.ll'), output = path.join(dir, 'report.json');
  fs.writeFileSync(input, '; fixture\n');
  const control = path.join(dir, 'control'), candidate = path.join(dir, 'candidate');
  const source = `#!/usr/bin/env node
const fs = require('node:fs');
const fast = process.argv[1].endsWith('candidate');
if (process.env.MOCK_MODE === 'fail') process.exit(1);
if (process.env.MOCK_MODE === 'malformed') { console.log('bad row'); process.exit(0); }
if (process.env.MOCK_MODE === 'drift') fs.appendFileSync(process.argv[3], '; changed');
const different = fast && process.env.MOCK_MODE === 'different';
for (let i=3; i<process.argv.length; i++)
 console.log([i-3, fast?100000:200000, fast?750:1500, fast?250:500,
 fast?2048:4096, 42, different?'ffffffffffffffff':'0123456789abcdef'].join('\\t'));
`;
  for (const file of [control, candidate]) fs.writeFileSync(file, source, { mode: 0o755 });
  t.after(() => {
    for (const file of [output, input, control, candidate])
      if (fs.existsSync(file)) fs.unlinkSync(file);
    fs.rmdirSync(module);
    fs.rmdirSync(dir);
  });
  return { input, output, run: mode => spawnSync(process.execPath,
    [path.join(root, 'tools/profile-host.mjs'), control, candidate, dir, output],
    { encoding: 'utf8', timeout: 15000, env: { ...process.env, MOCK_MODE: mode } }) };
}
test('host profiling aggregates paired batches with output and provenance checks', t => {
  const f = fixture(t), result = f.run('success');
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  const report = JSON.parse(fs.readFileSync(f.output, 'utf8'));
  assert.equal(report.batches, 5);
  assert.equal(report.iterations, 50);
  assert.equal(report.runs.length, 5);
  assert.equal(report.cpu_percent_change, -50);
  assert.equal(report.peak_rss_percent_change, -50);
  assert.equal(report.modules[0].cpu_percent_change, -50);
  assert.match(report.inputs[0].sha256, /^[0-9a-f]{64}$/);
  assert.match(report.rss_scope, /cumulative process peak/);
});
test('failed, malformed, changed-input and changed-output profiles preserve existing reports', t => {
  const f = fixture(t);
  for (const mode of ['fail', 'malformed', 'drift', 'different']) {
    fs.writeFileSync(f.input, '; fixture\n');
    fs.writeFileSync(f.output, 'prior report');
    const result = f.run(mode);
    assert.ifError(result.error);
    assert.notEqual(result.status, 0, mode);
    assert.equal(fs.readFileSync(f.output, 'utf8'), 'prior report');
  }
});
test('host reports cannot overwrite inputs through hard links or symlinks', t => {
  const f = fixture(t);
  fs.linkSync(f.input, f.output);
  assert.notEqual(f.run('success').status, 0);
  assert.equal(fs.readFileSync(f.input, 'utf8'), '; fixture\n');
  fs.unlinkSync(f.output);
  fs.symlinkSync(`${f.input}.missing`, f.output);
  assert.notEqual(f.run('success').status, 0);
  assert.equal(fs.existsSync(`${f.input}.missing`), false);
  fs.unlinkSync(f.output);
  fs.symlinkSync(f.input, f.output);
  assert.notEqual(f.run('success').status, 0);
  assert.equal(fs.readFileSync(f.input, 'utf8'), '; fixture\n');
});
test('native profiler validates repeat bounds, input errors and resource/output row shape', () => {
  const exe = path.join(root, 'build/profile-codegen');
  const input = path.join(root, 'tests/fixtures/global_residency_oracle_fixture.ll');
  const run = args => spawnSync(exe, args, { encoding: 'utf8', timeout: 15000 });
  for (const count of ['0', '-1', '+1', '1x', '10001']) {
    const result = run([count, input]);
    assert.ifError(result.error);
    assert.equal(result.status, 2);
    assert.match(result.stderr, /Usage:/);
  }
  const missing = run(['1', `${input}.missing`]);
  assert.equal(missing.status, 1);
  assert.match(missing.stderr, /cannot open/);
  const result = run(['3', input]);
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  const row = result.stdout.trim().split('\t');
  assert.equal(row.length, 7);
  assert.equal(Number(row[0]), 0);
  assert(Number(row[1]) > 0 && Number(row[4]) > 0 && Number(row[5]) > 0);
  assert.match(row[6], /^[0-9a-f]{16}$/);
});
