import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { captureAssemblyEffects, verifyAssemblyEffects } from './assembly-effects-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
try {
  const [mode, output, attribution, effects, ...extra] = process.argv.slice(2);
  assert(!extra.length && output && (mode === '--capture' && attribution && effects ||
    ['--verify', '--verify-current'].includes(mode) && !attribution),
  'Usage: lightweight-frame-evidence.mjs --capture NEW_DIRECTORY PO10.json PO11.json | --verify[-current] PO12.json');
  if (mode === '--capture') verifyAssemblyEffects(path.resolve(root, effects));
  const report = mode === '--capture' ? captureAssemblyEffects(path.resolve(root, output),
    path.resolve(root, attribution), { frames: true, effectsReport: effects }) :
    verifyAssemblyEffects(path.resolve(root, output), mode === '--verify-current');
  assert.equal(report.milestone, 'PO12');
  console.log(`PASS: PO12 ${JSON.stringify(report.summary)}`);
} catch (error) { console.error(error.message); process.exitCode = 1; }
