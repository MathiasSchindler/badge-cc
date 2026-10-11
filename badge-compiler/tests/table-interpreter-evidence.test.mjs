import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyConstantSelection } from '../tools/constant-selection-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const report = process.env.RS04_REPORT ?? path.join(root, 'build/rs04-byte-streams/results.json');
test('RS04 evidence binds parser bytes, resource bounds and functional-only qualification', {
  skip: !fs.existsSync(report),
}, t => {
  const verified = verifyConstantSelection(report);
  assert.equal(verified.milestone, 'RS04');
  assert.equal(verified.table_interpreter.bytes, 40);
  assert.equal(verified.red_square.loaded_bytes, 644);
  const dir = fs.mkdtempSync(path.join(root, 'build/table-evidence-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const file = path.join(dir, 'results.json');
  for (const tamper of [
    r => r.table_interpreter.bytes--,
    r => r.table_interpreter.before_bytes++,
    r => r.table_interpreter.target_met = false,
    r => r.table_interpreter.hardware_qualified = true,
    r => r.table_interpreter.default_changed = true,
    r => r.red_square_functions.find(f => f.name === 'commands').saves++,
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
