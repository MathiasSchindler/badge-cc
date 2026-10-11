import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyConstantSelection } from '../tools/constant-selection-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const report = process.env.RS01_REPORT ?? [
  'rs01-address-selection', 'rs01-edge-selection', 'rs01-flow-selection',
  'rs01-constant-selection-final', 'rs01-constant-selection',
].map(directory => path.join(root, 'badge-compiler/build', directory, 'results.json'))
  .find(file => fs.existsSync(file));
test('RS01 evidence recomputes resource/size facts and rejects false target completion', {
  skip: !report || !fs.existsSync(report),
}, t => {
  verifyConstantSelection(report);
  const directory = fs.mkdtempSync(path.join(root, 'badge-compiler/build/constant-evidence-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = path.join(directory, 'results.json');
  for (const tamper of [
    r => r.red_square.target_met = true,
    r => r.red_square.main_bytes = 184,
    r => r.policy.default_changed = true,
    r => r.hardware_touched = true,
    r => r.summary.code_bytes_saved++,
    r => r.modules[0].functions[0].frame++,
    r => r.images[0].loaded_bytes--,
    r => r.artifacts[0].sha256 = '0'.repeat(64),
    r => r.final_validation = { ...r.final_validation, tests: 5 },
  ]) {
    const input = JSON.parse(fs.readFileSync(report));
    tamper(input);
    fs.writeFileSync(file, JSON.stringify(input));
    assert.throws(() => verifyConstantSelection(file));
  }
});
