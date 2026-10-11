import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { verifyAssemblyEffects } from '../tools/assembly-effects-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
test('persistent PO11 evidence recomputes and rejects policy, accounting, frame, spill and artifact tampering',
  { skip: !process.env.PO11_REPORT }, t => {
    const original = verifyAssemblyEffects(path.resolve(root, process.env.PO11_REPORT), true);
    const directory = fs.mkdtempSync(path.join(root, 'badge-compiler/build/po11-verifier-test-'));
    t.after(() => fs.rmSync(directory, { recursive: true }));
    const changed = mutate => {
      const report = JSON.parse(JSON.stringify(original));
      mutate(report);
      const file = path.join(directory, 'results.json');
      fs.writeFileSync(file, JSON.stringify(report));
      return file;
    };
    for (const mutate of [
      r => r.default_changed = true,
      r => r.policies.candidate = 'thumb-calls',
      r => r.modules.pop(),
      r => r.images.pop(),
      r => r.artifacts[0].sha256 = '0'.repeat(64),
      r => r.modules[0].candidate.code_bytes++,
      r => r.modules[0].candidate.totals['asm-spills']++,
      r => r.modules[0].functions[0].after_frame++,
      r => r.images[0].candidate.text--,
      r => r.summary.removed_asm_spills++,
      r => r.artifacts = r.artifacts.filter(a => a.file !== r.modules[0].ir),
    ]) assert.throws(() => verifyAssemblyEffects(changed(mutate)));
    assert.equal(original.summary.removed_asm_spills, 3);
    assert.equal(original.summary.additional_fused_comparisons, 41);
  });
