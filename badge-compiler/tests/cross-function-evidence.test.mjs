import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyCrossFunction } from '../tools/cross-function-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const file = process.env.PO16_REPORT ?? path.join(root, 'build/po16-qualified-sharing/results.json');
test('persistent PO16 evidence verifies complete module/clone and loaded image economics without rebuilding', {
  skip: !fs.existsSync(file),
}, () => {
  const report = verifyCrossFunction(file);
  assert.equal(report.summary.original_functions, 554);
  assert(report.summary.module_code_bytes_saved > 0);
});
test('persistent report rejects policy, clone provenance, accounting, bytes and artifact tampering', {
  skip: !fs.existsSync(file),
}, t => {
  const dir = fs.mkdtempSync(path.join(root, 'build/cross-report-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const original = JSON.parse(fs.readFileSync(file));
  for (const mutate of [
    r => { r.summary.module_code_bytes_saved++; },
    r => { r.policy.backend = 'direct/global-compact/thumb-calls/word'; },
    r => { r.modules[0].accounting.functions[0].after.frame++; },
    r => { r.modules.find(m => m.decisions.length).decisions[0].clone = 'unknown'; },
    r => { r.artifacts[0].sha256 = '0'.repeat(64); },
    r => { r.images[0].after_bytes--; },
  ]) {
    const report = structuredClone(original);
    mutate(report);
    const tampered = path.join(dir, 'tampered.json');
    fs.writeFileSync(tampered, JSON.stringify(report));
    assert.throws(() => verifyCrossFunction(tampered));
  }
});
