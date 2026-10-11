import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyConstantSelection } from '../tools/constant-selection-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const report = process.env.RS06_REPORT ?? path.join(root, 'build/rs06-counted-delay/results.json');
test('RS06 evidence verifies delay size, stack freedom and minimum explicit cycles', {
  skip: !fs.existsSync(report),
}, t => {
  const verified = verifyConstantSelection(report);
  assert.equal(verified.milestone, 'RS06');
  assert.equal(verified.initialization_delay.bytes, 20);
  assert.equal(verified.red_square.loaded_bytes, 624);
  assert.equal(verified.minimum_delay.minimum_cycles, 24000000);
  const dir = fs.mkdtempSync(path.join(root, 'build/delay-evidence-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const file = path.join(dir, 'results.json');
  for (const tamper of [
    r => r.initialization_delay.bytes--,
    r => r.initialization_delay.before_bytes++,
    r => r.initialization_delay.llvm_target++,
    r => r.initialization_delay.target_met = false,
    r => r.initialization_delay.hardware_qualified = true,
    r => r.initialization_delay.default_changed = true,
    r => r.red_square.loaded_bytes--,
    r => r.red_square_functions.find(f => f.name === 'delay_cycles').saves++,
    r => r.minimum_delay.explicit_nops_per_iteration--,
    r => r.minimum_delay.minimum_cycles--,
    r => r.minimum_delay.maximum_clock_hz++,
    r => r.minimum_delay.minimum_milliseconds--,
    r => r.minimum_delay.electrical_timing_measured = true,
    r => r.reached_instructions.candidate = r.reached_instructions.control,
    r => r.artifacts[0].sha256 = '0'.repeat(64),
  ]) {
    const input = JSON.parse(fs.readFileSync(report));
    tamper(input);
    fs.writeFileSync(file, JSON.stringify(input));
    assert.throws(() => verifyConstantSelection(file));
  }
});
