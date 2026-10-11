import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyConstantSelection } from '../tools/constant-selection-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const report = process.env.RS05_REPORT ?? path.join(root, 'build/rs05-pixel-fill/results.json');
test('RS05 evidence verifies the LLVM-beating fill target without changing historical reports', {
  skip: !fs.existsSync(report),
}, t => {
  const verified = verifyConstantSelection(report);
  assert.equal(verified.milestone, 'RS05');
  assert.equal(verified.pixel_fill.bytes, 34);
  assert.equal(verified.red_square.loaded_bytes, 636);
  const dir = fs.mkdtempSync(path.join(root, 'build/pixel-evidence-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const file = path.join(dir, 'results.json');
  for (const tamper of [
    r => r.pixel_fill.bytes--,
    r => r.pixel_fill.before_bytes++,
    r => r.pixel_fill.llvm_target = 40,
    r => r.pixel_fill.target_met = false,
    r => r.pixel_fill.hardware_qualified = true,
    r => r.pixel_fill.default_changed = true,
    r => r.red_square.loaded_bytes--,
    r => r.red_square_functions.find(f => f.name === 'fill').saves++,
    r => r.modules[0].functions[0].frame++,
    r => r.reached_instructions.candidate = r.reached_instructions.control,
    r => r.artifacts[0].sha256 = '0'.repeat(64),
  ]) {
    const input = JSON.parse(fs.readFileSync(report));
    tamper(input);
    fs.writeFileSync(file, JSON.stringify(input));
    assert.throws(() => verifyConstantSelection(file));
  }
});
