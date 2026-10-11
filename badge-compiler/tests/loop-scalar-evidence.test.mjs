import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyLoopScalar } from '../tools/loop-scalar-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const reportFile = process.env.PO15_REPORT ?? path.join(root, 'build/po15-loop-scalar/results.json');
test('persistent PO15 evidence verifies without repeating compiler/runtime campaigns', {
  skip: !fs.existsSync(reportFile),
}, () => {
  const report = verifyLoopScalar(reportFile);
  assert(report.summary.changed_ir_modules <= report.modules.length);
  assert(report.summary.events['skipped:final_code_growth'] > 0);
  assert(report.summary.module_code_bytes_saved > 0);
});
test('persistent evidence rejects fabricated savings, policy and per-function allocation facts', {
  skip: !fs.existsSync(reportFile),
}, t => {
  const dir = fs.mkdtempSync(path.join(root, 'build/loop-scalar-report-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const original = JSON.parse(fs.readFileSync(reportFile));
  for (const mutate of [
    r => { r.summary.module_code_bytes_saved++; },
    r => { r.policy.backend = 'direct/global-compact/thumb-calls/word'; },
    r => { r.modules[0].functions[0].after_frame++; },
    r => { r.artifacts[0].sha256 = '0'.repeat(64); },
    r => { r.images[0].after_bytes--; },
  ]) {
    const copy = structuredClone(original);
    mutate(copy);
    const file = path.join(dir, 'tampered.json');
    fs.writeFileSync(file, JSON.stringify(copy));
    assert.throws(() => verifyLoopScalar(file));
  }
});
