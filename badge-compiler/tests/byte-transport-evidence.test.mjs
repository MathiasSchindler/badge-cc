import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyByteTransport } from '../tools/byte-transport-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const report = path.join(root, 'badge-compiler/build/rs02-byte-transport/results.json');
test('RS02 report proves the size target without claiming physical qualification', {
  skip: !fs.existsSync(report),
}, t => {
  verifyByteTransport(report);
  const directory = fs.mkdtempSync(path.join(root, 'badge-compiler/build/transport-evidence-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = path.join(directory, 'results.json');
  for (const tamper of [
    r => r.byte_transport.bytes = 31,
    r => r.byte_transport.before_bytes = 74,
    r => r.byte_transport.hardware_qualified = true,
    r => r.byte_transport.default_changed = true,
    r => r.byte_transport.target_met = false,
    r => r.reached_instructions.candidate = r.reached_instructions.control,
    r => r.red_square.loaded_bytes--,
    r => r.artifacts[0].sha256 = '0'.repeat(64),
  ]) {
    const input = JSON.parse(fs.readFileSync(report));
    tamper(input);
    fs.writeFileSync(file, JSON.stringify(input));
    assert.throws(() => verifyByteTransport(file));
  }
});
