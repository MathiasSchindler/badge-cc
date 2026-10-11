import assert from 'node:assert/strict';
import test from 'node:test';
import { currentPlan, decideCurrentPromotion } from '../tools/current-promotion.mjs';

const restored = ['a', 'b'].map(serial => ({ serial, verified: true, left_in_bootsel: true }));
test('PO18 declares only the current baseline and full candidate, not the historical matrix', () => {
  assert.equal(currentPlan.profiles.length, 2);
  assert.equal(currentPlan.profiles[0].scalar, 'thumb-calls');
  assert.equal(currentPlan.profiles[1].scalar, 'thumb-widths');
  assert.equal(currentPlan.seeds.length, 2);
  assert.equal(currentPlan.repeats, 2);
});
test('a resolved regression cannot be averaged away or automatically promoted', () => {
  const rows = [{ suite: 'timing', seed: 1, timing: {
    regressions: ['signed-divrem'], repeatable_improvements: ['graphics', 'arithmetic'],
  } }];
  const result = decideCurrentPromotion(rows, restored);
  assert.equal(result.eligible, false);
  assert.equal(result.promoted, false);
  assert.equal(result.regressions[0].workload, 'signed-divrem');
  assert.throws(() => decideCurrentPromotion(rows, [{ verified: false }]));
});
test('no-regression size candidates are eligible but still require the production timing audit', () => {
  const result = decideCurrentPromotion([{ suite: 'application', seed: 1,
    timing: { regressions: [], repeatable_improvements: [] } }], restored);
  assert.equal(result.eligible, true);
  assert.equal(result.promoted, false);
  assert.match(result.decision, /production-timing-audit/);
});
