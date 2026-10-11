import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { captureAssemblyEffects, verifyAssemblyEffects } from './assembly-effects-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
try {
  const [mode, output, attribution, baseline, ...extra] = process.argv.slice(2);
  assert(!extra.length && output && (['--capture', '--resume'].includes(mode) && attribution && baseline ||
    ['--verify', '--verify-current'].includes(mode) && !attribution && !baseline),
  'Usage: operand-selection-evidence.mjs --capture NEW_DIRECTORY | --resume FAILED_DIRECTORY PO10.json PO13.json | --verify[-current] PO14.json');
  const capture = ['--capture', '--resume'].includes(mode);
  if (capture) assert.equal(verifyAssemblyEffects(path.resolve(root, baseline)).milestone, 'PO13');
  const report = capture ? captureAssemblyEffects(path.resolve(root, output),
    path.resolve(root, attribution), { operands: true, effectsReport: baseline, resume: mode === '--resume' }) :
    verifyAssemblyEffects(path.resolve(root, output), mode === '--verify-current');
  assert.equal(report.milestone, 'PO14');
  console.log(`PASS: PO14 ${JSON.stringify(report.summary)}`);
} catch (error) { console.error(error.message); process.exitCode = 1; }
