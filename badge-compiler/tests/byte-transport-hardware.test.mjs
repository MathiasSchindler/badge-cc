import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyHardware } from '../tools/byte-transport-hardware.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const report = process.env.HARDWARE_REPORT ?? path.join(root, 'build/rs02-hardware/results.json');
test('physical acceptance preserves provenance, role swaps and unmeasured timing', {
  skip: !fs.existsSync(report),
}, t => {
  const verified = verifyHardware(report);
  assert.equal(verified.rounds.reduce((n, r) =>
    n + r.configurations.length * r.observations.cold_boots_per_badge, 0),
  ['RS04', 'RS05', 'RS06'].includes(verified.milestone) ? 2 : 12);
  const directory = fs.mkdtempSync(path.join(root, 'build/transport-hardware-test-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = path.join(directory, 'results.json');
  for (const tamper of [
    r => r.electrical_timing_measured = true,
    r => r.default_changed = true,
    r => r.images[0].bytes--,
    r => r.images[0].elf_sha256 = '0'.repeat(64),
    r => r.images[0].binary_sha256 = '0'.repeat(64),
    r => r.images[0].sha256 = '0'.repeat(64),
    r => r.host_report.sha256 = '0'.repeat(64),
    r => r.backups[0].sha256 = '0'.repeat(64),
    r => r.rounds.at(-1).configurations.reverse(),
    r => r.rounds[0].configurations[0].verified = false,
    r => r.rounds.at(-1).observations.cold_boots_per_badge = 2,
    r => r.acceptance.scope = 'electrical-hardware-qualified',
    r => r.milestone = 'RS01',
  ]) {
    const input = JSON.parse(fs.readFileSync(report));
    tamper(input);
    fs.writeFileSync(file, JSON.stringify(input));
    assert.throws(() => verifyHardware(file));
  }
});
