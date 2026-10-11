import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyCommandWrapper } from '../tools/command-wrapper-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const report = process.env.RS03_REPORT ?? path.join(root, 'build/rs03-command-wrapper/results.json');
test('RS03 evidence verifies the wrapper target, unchanged resources and honest qualification status', {
  skip: !fs.existsSync(report),
}, t => {
  const verified = verifyCommandWrapper(report);
  assert(verified.command_wrapper.bytes <= 24);
  const directory = fs.mkdtempSync(path.join(root, 'build/wrapper-evidence-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = path.join(directory, 'results.json');
  for (const tamper of [
    r => r.command_wrapper.bytes--,
    r => r.command_wrapper.before_bytes++,
    r => r.command_wrapper.target_met = false,
    r => r.command_wrapper.hardware_qualified = true,
    r => r.command_wrapper.default_changed = true,
    r => r.reached_instructions.candidate = r.reached_instructions.control,
    r => r.red_square.loaded_bytes--,
    r => r.modules[0].functions[0].saves++,
    r => r.artifacts[0].sha256 = '0'.repeat(64),
    ...(verified.call_preservation ? [
      r => r.call_preservation.target_met = false,
      r => r.call_preservation.register = 1,
      r => r.call_preservation.callee_register_mentions |= 4,
      r => r.call_preservation.target_bytes = 24,
    ] : []),
  ]) {
    const input = JSON.parse(fs.readFileSync(report));
    tamper(input);
    fs.writeFileSync(file, JSON.stringify(input));
    assert.throws(() => verifyCommandWrapper(file));
  }
});
