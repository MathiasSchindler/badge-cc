import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyWidthSelection } from '../tools/width-selection-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const file = process.env.PO17_REPORT ?? path.join(root, 'build/po17-qualified-widths/results.json');
test('persistent PO17 evidence verifies allocation, module and loaded-image savings without rebuilding', {
  skip: !fs.existsSync(file),
}, () => {
  const report = verifyWidthSelection(file);
  assert.equal(report.summary.modules, 37);
  assert(report.summary.module_code_bytes_saved > 0);
});
test('persistent width evidence rejects policy, ABI, source identity and byte-accounting tampering', {
  skip: !fs.existsSync(file),
}, t => {
  const dir = fs.mkdtempSync(path.join(root, 'build/width-evidence-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const original = JSON.parse(fs.readFileSync(file));
  for (const mutate of [
    r => { r.policy.candidate = 'thumb-compact'; },
    r => { r.modules[0].functions[0].frame++; },
    r => { r.modules[0].ir_sha256 = '0'.repeat(64); },
    r => { r.modules[0].after_bytes--; },
    r => { r.images[0].after_bytes--; },
    r => { r.artifacts[0].sha256 = '0'.repeat(64); },
    r => { r.summary.module_code_bytes_saved++; },
  ]) {
    const report = structuredClone(original);
    mutate(report);
    const tampered = path.join(dir, 'tampered.json');
    fs.writeFileSync(tampered, JSON.stringify(report));
    assert.throws(() => verifyWidthSelection(tampered));
  }
});
