import test from 'node:test';
import assert from 'node:assert/strict';
import { profiles, assessTiming, promotionDecision } from '../tools/qualify-datasheet.mjs';
import { modules, kernels, timingStatistics, timingRepeats } from '../tools/compiler-test-protocol.mjs';

const serials = ['9BBF88DD17D66E7D', '08DA26F86E57F5EA'];
function campaign(tag, owned = 1000, spread = 0) {
  const timing = (value, repetitions) => {
    const samples = [value - spread, value, value + spread, value, value, value, value];
    return { samples, repetitions, ...timingStatistics(samples) };
  };
  const report = { schema: 3, matched: true, toolchain: { llvm: 'fixture',
    owned: { 'badge-codegen': 'a'.repeat(64) } }, rounds: [] };
  for (let role = 0; role < 2; role++) {
    const nonce = Buffer.alloc(16);
    nonce.writeUInt32LE(123); nonce[15] = tag * 2 + role;
    const configuration = name => ({ name, serial: serials[name === 'llvm' ? role : 1 - role],
      sha256: (name === 'llvm' ? 'b' : 'c').repeat(64), build: 42 });
    const results = (definitions, isKernel) => definitions.map((d, i) => {
      const repetitions = !isKernel && i === 5 ? 1 : timingRepeats;
      const values = isKernel ? [0, 0, 0, 1, 1, d.cases] : [0, 0, 0, 0, 0, 0];
      return { module: d.name, cases: d.cases, matched: true, llvm: values, owned: values,
        timing: { repetitions, ...(isKernel ? { iterationsPerCall: d.cases } : {}),
          llvm: { ...timing(1000, repetitions), ...(isKernel ? { iterationsPerCall: d.cases } : {}) },
          owned: { ...timing(owned, repetitions), ...(isKernel ? { iterationsPerCall: d.cases } : {}) } } };
    });
    report.rounds.push({ matched: true, timingVersion: 3, seed: 123, nonce: nonce.toString('hex'),
      configurations: [configuration('llvm'), configuration('owned')],
      clocks: Array.from({ length: 2 }, () => [0, 65536, 0, 65536, 3, 24, 0, 0, 0, 0, 0, 0, 0, 1]),
      overhead: [Array(7).fill(1), Array(7).fill(1)],
      modules: results(modules, false), kernels: results(kernels, true) });
  }
  return report;
}
const offline = { correctness: true, memory_and_frames: true };
const restoration = serials.map(serial => ({ serial, verified: true, left_in_bootsel: true }));

test('independent profiles preserve defaults and do not silently compose candidates', () => {
  assert.deepEqual(profiles.map(p => p.id), ['control', 'd04', 'd05', 'd06', 'd07']);
  assert.equal(profiles[0].scalar, 'thumb-layout');
  assert(profiles.slice(0, 4).every(p => p.passes === profiles[0].passes));
  assert.equal(profiles[4].scalar, profiles[0].scalar);
  assert(profiles[4].passes.includes('constant-fold,target-strength-reduce,aggregate-memory'));
});

test('strict per-board/repeat noise gate distinguishes improvement, regression and equality', () => {
  const improve = assessTiming(campaign(1), campaign(2, 900), campaign(3), campaign(4, 900));
  assert.equal(improve.regressions.length, 0);
  assert.equal(improve.repeatable_improvements.length, modules.length + kernels.length);
  assert.equal(promotionDecision(improve, offline, restoration).eligible_for_speed_review, true);
  assert.equal(promotionDecision(improve, offline, restoration).promoted, false);
  const equality = assessTiming(campaign(1), campaign(2), campaign(3), campaign(4));
  assert.equal(equality.repeatable_improvements.length, 0);
  assert.equal(promotionDecision(equality, offline, restoration).eligible_for_speed_review, false);
  const noisy = assessTiming(campaign(1, 1000, 40), campaign(2, 990, 40),
    campaign(3, 1000, 40), campaign(4, 990, 40));
  assert.equal(noisy.repeatable_improvements.length, 0);
  const slower = assessTiming(campaign(1), campaign(2, 1100), campaign(3), campaign(4, 1100));
  assert.equal(slower.regressions.length, modules.length + kernels.length);
  assert.match(promotionDecision(slower, offline, restoration).decision, /reject-speed/);
});

test('one bad role or repeat cannot hide behind a geometric mean', () => {
  const candidate = campaign(4, 900);
  const sample = candidate.rounds[1].modules[0].timing.owned;
  sample.samples.fill(1100); Object.assign(sample, timingStatistics(sample.samples));
  const assessed = assessTiming(campaign(1), campaign(2, 900), campaign(3), candidate);
  assert(assessed.regressions.includes(modules[0].name));
  assert(!assessed.repeatable_improvements.includes(modules[0].name));
  assert.equal(promotionDecision(assessed, offline, restoration).eligible_for_speed_review, false);
});

test('exact one-tick noise ties stay unresolved in both directions', () => {
  for (const value of [999, 1001]) {
    const controls = [campaign(1), campaign(3)];
    for (const report of controls)
      for (const round of report.rounds)
        for (const field of ['modules', 'kernels'])
          for (const row of round[field]) {
            row.timing.owned.samples[6] = 1001;
            Object.assign(row.timing.owned, timingStatistics(row.timing.owned.samples));
          }
    const assessed = assessTiming(controls[0], campaign(2, value), controls[1], campaign(4, value));
    assert(assessed.rows.every(row => row.observations.every(x => x.classification === 'within-noise')));
    assert.equal(assessed.repeatable_improvements.length, 0);
    assert.equal(assessed.regressions.length, 0);
    assert.equal(promotionDecision(assessed, offline, restoration).eligible_for_speed_review, false);
  }
});

test('control drift/LLVM drift enlarge uncertainty and invalid campaigns fail explicitly', () => {
  const drift = assessTiming(campaign(1), campaign(2, 950), campaign(3, 1100), campaign(4, 950));
  assert.equal(drift.repeatable_improvements.length, 0);
  assert.throws(() => assessTiming(campaign(1), campaign(1), campaign(3), campaign(4)), /nonces/);
  const invalid = campaign(2);
  invalid.rounds[1].configurations.reverse();
  assert.throws(() => assessTiming(campaign(1), invalid, campaign(3), campaign(4)), /roles|roles.*swapped/);
  assert.throws(() => promotionDecision(drift, { correctness: false, memory_and_frames: true }, restoration), /correctness/);
  assert.throws(() => promotionDecision(drift, offline, restoration.slice(0, 1)), /two restorations/);
  assert.throws(() => promotionDecision(drift, offline, [{ verified: true, left_in_bootsel: true }, { verified: false }]), /unverified/);
  assert.throws(() => promotionDecision(drift, offline, [restoration[0], { ...restoration[0] }]), /distinct/);
});
