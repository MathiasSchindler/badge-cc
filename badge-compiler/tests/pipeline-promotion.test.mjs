import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { promotionProfiles, seeds, decidePromotion, verifyPromotionReport,
  verifyPromotionOffline, retryableTransport } from '../tools/pipeline-promotion.mjs';

const restoration = ['9BBF88DD17D66E7D', '08DA26F86E57F5EA']
  .map(serial => ({ serial, verified: true, left_in_bootsel: true }));
const candidate = () => ({ id: 'po07', correctness: true,
  tradeoffs: { growing_frames: [], growing_spill_counts: [], growing_images: [], maze_text_delta: -20 } });
const assessments = () => ['application', 'timing'].flatMap(suite => seeds.map(seed => ({ suite, seed,
  timing: { repeatable_improvements: ['work'], regressions: [] } })));

test('profiles retain the current default and declare the combination separately', () => {
  assert.deepEqual(promotionProfiles.map(profile => profile.id),
    ['control', 'po03', 'po04', 'po05', 'po06', 'po07', 'po06-po07']);
  const [control] = promotionProfiles, combined = promotionProfiles.at(-1);
  assert.equal(control.expression, 'legacy');
  assert.equal(control.scalar, 'thumb-layout');
  assert(!control.passes.includes('local-memory'));
  assert.equal(combined.expression, 'ssa');
  assert.equal(combined.scalar, 'thumb-calls');
  assert.equal(combined.passes, control.passes + ',local-memory');
  assert.deepEqual(seeds, [3276377847, 123456789]);
});
test('cross-seed improvement is eligible only for review, never automatic promotion', () => {
  const result = decidePromotion(candidate(), assessments(), restoration);
  assert.equal(result.eligible_for_speed_review, true);
  assert.equal(result.promoted, false);
  assert.match(result.decision, /pending-production-runtime/);
  const mixed = assessments();
  mixed[1].timing.repeatable_improvements = [];
  assert.equal(decidePromotion(candidate(), mixed, restoration).eligible_for_speed_review, false);
});
test('any unrelated-suite or seed regression blocks a favorable application mean', () => {
  const evidence = assessments();
  evidence[3].timing.regressions = ['unrelated'];
  const result = decidePromotion(candidate(), evidence, restoration);
  assert.equal(result.eligible_for_speed_review, false);
  assert.match(result.decision, /reject-speed/);
  assert.deepEqual(result.regressions, [{ suite: 'timing', seed: seeds[1], workload: 'unrelated' }]);
});
test('frame, spill and size tradeoffs remain explicit despite faster benchmarks', () => {
  for (const field of ['growing_frames', 'growing_spill_counts', 'growing_images', 'maze_text_delta']) {
    const profile = candidate();
    profile.tradeoffs[field] = field === 'maze_text_delta' ? 4 : [{}];
    const result = decidePromotion(profile, assessments(), restoration);
    assert.equal(result.eligible_for_speed_review, false);
    assert.equal(result.decision, 'retain-explicit-static-tradeoff');
  }
});
test('missing suites, seed order, correctness and restoration cannot qualify', () => {
  assert.throws(() => decidePromotion(candidate(), assessments().slice(1), restoration), /incomplete/);
  assert.throws(() => decidePromotion(candidate(), assessments().reverse(), restoration), /incomplete/);
  assert.throws(() => decidePromotion({ ...candidate(), correctness: false }, assessments(), restoration), /correctness/);
  assert.throws(() => decidePromotion(candidate(), assessments(), restoration.slice(1)), /two restorations/);
  assert.throws(() => decidePromotion(candidate(), assessments(), [restoration[0], restoration[0]]), /duplicate/);
  assert.throws(() => decidePromotion(candidate(), assessments(),
    [restoration[0], { ...restoration[1], verified: false }]), /unverified/);
});
test('persistent verifiers reject failed or wrong-milestone evidence', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'po09-verifier-'));
  const file = path.join(directory, 'report.json');
  try {
    fs.writeFileSync(file, JSON.stringify({ schema: 1, milestone: 'PO09', status: 'failed' }));
    assert.throws(() => verifyPromotionReport(file), /qualified-retained-explicit/);
    assert.throws(() => verifyPromotionOffline(file), /offline-qualified/);
    fs.writeFileSync(file, JSON.stringify({ schema: 1, milestone: 'PO08', status: 'offline-qualified' }));
    assert.throws(() => verifyPromotionOffline(file), /PO09/);
  } finally { fs.unlinkSync(file); fs.rmdirSync(directory); }
});
test('only explicit collector transport failures may trigger bounded fresh retries', () => {
  assert.equal(retryableTransport('round2-collector failed: exit 1\nSuite summary read: -1'), true);
  assert.equal(retryableTransport('round1-collector failed: exit 1\nLIBUSB_ERROR_NO_DEVICE'), true);
  assert.equal(retryableTransport('round2-owned-flash failed: spawnSync /path/picotool ETIMEDOUT'), true);
  assert.equal(retryableTransport('round1-llvm-flash failed: spawnSync /path/picotool ETIMEDOUT'), true);
  for (const text of ['build failed', 'collector timed out', 'round1-collector failed: exit 1',
    'round2-owned-flash failed: exit 1\nInvalid UF2',
    'round2-collector failed: exit 1\nLIBUSB_ERROR_OTHER\nfingerprint mismatch',
    'round1-collector failed: exit 1\nSuite summary read: -1\nCRC mismatch'])
    assert.equal(retryableTransport(text), false);
});
