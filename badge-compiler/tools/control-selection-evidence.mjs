import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { captureAssemblyEffects, verifyAssemblyEffects } from './assembly-effects-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
try {
  const [mode, output, attribution, baseline, ...extra] = process.argv.slice(2);
  assert(!extra.length && output && (mode === '--capture' && attribution && baseline ||
    ['--verify', '--verify-current'].includes(mode) && !attribution),
  'Usage: control-selection-evidence.mjs --capture NEW_DIRECTORY PO10.json PO12.json | --verify[-current] PO13.json');
  if (mode === '--capture') assert.equal(verifyAssemblyEffects(path.resolve(root, baseline)).milestone, 'PO12');
  const report = mode === '--capture' ? captureAssemblyEffects(path.resolve(root, output),
    path.resolve(root, attribution), { control: true, effectsReport: baseline }) :
    verifyAssemblyEffects(path.resolve(root, output), mode === '--verify-current');
  assert.equal(report.milestone, 'PO13');
  console.log(`PASS: PO13 ${JSON.stringify(report.summary)}`);
} catch (error) { console.error(error.message); process.exitCode = 1; }
