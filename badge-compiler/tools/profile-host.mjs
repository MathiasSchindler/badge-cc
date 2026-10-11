import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const args = process.argv.slice(2);
if (args.length !== 4) {
  console.error('Usage: node tools/profile-host.mjs CONTROL_PROFILE CANDIDATE_PROFILE FIRMWARE_DIR OUTPUT.json');
  process.exit(2);
}
const [control, candidate, directory, output] = args.map(p => path.resolve(p));
const names = fs.readdirSync(directory).filter(n => n.endsWith('.o.owned') &&
  fs.existsSync(path.join(directory, n, 'optimized.ll'))).sort();
assert(names.length > 0, 'No optimized C modules');
const inputs = names.map(n => path.join(directory, n, 'optimized.ll'));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const inputHashes = inputs.map(hash);
const executableHashes = [control, candidate].map(hash);
let target;
try {
  target = fs.lstatSync(output);
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
if (target) {
  assert(target.isFile(), 'Report output must be a regular file, not a symlink');
  for (const file of [...inputs, control, candidate]) {
    const source = fs.statSync(file);
    assert(target.dev !== source.dev || target.ino !== source.ino,
      'Report output aliases a compiler or input');
  }
}
const iterations = 50, batches = 5;
function capture(exe) {
  const result = spawnSync(exe, [String(iterations), ...inputs], {
    encoding: 'utf8', timeout: 300000, maxBuffer: 4 * 1024 * 1024,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
  const rows = result.stdout.trim().split('\n').map((line, i) => {
    const parts = line.split('\t');
    assert.equal(parts.length, 7);
    const numbers = parts.slice(0, 6).map(Number);
    assert(numbers.every(n => Number.isSafeInteger(n) && n >= 0));
    assert.equal(numbers[0], i);
    assert(numbers[1] > 0 && numbers[4] > 0);
    assert.match(parts[6], /^[0-9a-f]{16}$/);
    return { module: names[i], wall_ns: numbers[1], user_us: numbers[2],
      system_us: numbers[3], process_peak_rss_bytes: numbers[4],
      assembly_bytes: numbers[5], output_fingerprint: parts[6] };
  });
  assert.equal(rows.length, names.length);
  inputs.forEach((file, i) => assert.equal(hash(file), inputHashes[i], 'Input changed during profiling'));
  [control, candidate].forEach((file, i) =>
    assert.equal(hash(file), executableHashes[i], 'Executable changed during profiling'));
  return rows;
}
const runs = [];
for (let i = 0; i < batches; i++) {
  const order = i % 2 ? [candidate, control] : [control, candidate];
  const a = capture(order[0]), b = capture(order[1]);
  const before = i % 2 ? b : a, after = i % 2 ? a : b;
  before.forEach((row, j) => {
    assert.equal(row.assembly_bytes, after[j].assembly_bytes);
    assert.equal(row.output_fingerprint, after[j].output_fingerprint);
    if (runs.length)
      assert.equal(row.output_fingerprint, runs[0].control[j].output_fingerprint);
  });
  runs.push({ control: before, candidate: after });
}
const median = numbers => [...numbers].sort((a, b) => a - b)[Math.floor(numbers.length / 2)];
const sum = rows => rows.reduce((n, r) => n + r.user_us + r.system_us, 0);
const total = side => ({
  cpu_us: median(runs.map(run => sum(run[side]))),
  wall_ns: median(runs.map(run => run[side].reduce((n, r) => n + r.wall_ns, 0))),
  process_peak_rss_bytes: median(runs.map(run => Math.max(...run[side].map(r => r.process_peak_rss_bytes)))),
});
const before = total('control'), after = total('candidate');
const report = {
  schema: 1, platform: process.platform, iterations, batches, warmups_per_module: 1,
  method: 'Alternating control/candidate process order; in-process API intervals; output checks and input/output I/O outside intervals.',
  rss_scope: 'OS cumulative process peak, not individual function allocation or a per-module peak.',
  inputs: inputs.map((file, i) => ({ module: names[i], sha256: inputHashes[i] })),
  executables: { control: { file: control, sha256: executableHashes[0] },
    candidate: { file: candidate, sha256: executableHashes[1] } },
  control: before, candidate: after,
  cpu_percent_change: (after.cpu_us / before.cpu_us - 1) * 100,
  wall_percent_change: (after.wall_ns / before.wall_ns - 1) * 100,
  peak_rss_percent_change: (after.process_peak_rss_bytes / before.process_peak_rss_bytes - 1) * 100,
  modules: names.map((module, i) => {
    const cpu = side => median(runs.map(r => r[side][i].user_us + r[side][i].system_us));
    const baseline = cpu('control'), value = cpu('candidate');
    return { module, control_cpu_us: baseline, candidate_cpu_us: value,
      cpu_percent_change: baseline ? (value / baseline - 1) * 100 : null };
  }),
  runs,
};
const temporary = fs.mkdtempSync(path.join(path.dirname(output), '.profile-host-'));
const temporaryFile = path.join(temporary, 'report.json');
try {
  fs.writeFileSync(temporaryFile, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  fs.renameSync(temporaryFile, output);
} finally {
  if (fs.existsSync(temporaryFile)) fs.unlinkSync(temporaryFile);
  fs.rmdirSync(temporary);
}
console.log(JSON.stringify({ control: before, candidate: after,
  cpu_percent_change: report.cpu_percent_change,
  peak_rss_percent_change: report.peak_rss_percent_change }, null, 2));
