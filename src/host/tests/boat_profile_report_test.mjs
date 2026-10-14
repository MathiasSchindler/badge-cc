import {test} from 'node:test';
import assert from 'node:assert/strict';
import {boatProfileReport} from '../tools/boat_profile_report.mjs';

function fixture() {
  const w = Array(512).fill(0);
  w[0] = 0x34544142; w[1] = 1; w[3] = 3; w[4] = 600e6; w[5] = 60000;
  w[6] = 18000; w[9] = 18000 * 1000; w[10] = 18000; w[12] = 40000;
  w[13] = 600000; w[14] = 20; w[15] = w[31] = 80 * 80 * 256;
  w[17] = 60 * 256; w[18] = 100 * 256; w[19] = 80 * 256; w[20] = 240 * 256;
  w[21] = 130 * 256; w[22] = 160 * 256; w[23] = 110; w[24] = 1; w[32] = 40000;
  for (let i = 0; i < 8; ++i) {
    const count = i < 2 ? 40000 : i === 2 ? 60000 : i === 7 ? 17999 : 18000;
    const us = i === 3 ? 40 : i === 4 || i === 5 ? 60 : 100;
    w[64 + i * 4] = count; w[65 + i * 4] = count * us;
    w[66 + i * 4] = us; w[67 + i * 4] = us;
  }
  w[112] = 18000;
  return {verified: true, diagnostic_ok: true, test: 'boat-profile', serial: '08DA26F86E57F5EA',
    build_id: '12345678', nonce: '1234567890abcdef1234567890abcdef', words: w};
}

test('boat report validates exact duration/counters and reports measured targets', () => {
  const report = boatProfileReport(fixture());
  assert.equal(report.completed_fps, 30);
  assert.equal(report.steps, 60000);
  assert.equal(report.metrics.sensor.mean_us, 100);
  assert.deepEqual(report.targets, {average_30fps: true, frame_work_p99_at_most_25ms: true});
});

test('nearest-rank p99 and exact 25ms threshold include every frame', () => {
  const record = fixture();
  record.words[67 + 4 * 6] = 25001;
  record.words[65 + 4 * 6] += 180 * (25001 - 100);
  record.words[65 + 4 * 4] += 180 * (25001 - 100);
  record.words[67 + 4 * 4] = 24961;
  record.words[112] = 17820; record.words[112 + 25] = 180; record.words[37] = 180;
  let report = boatProfileReport(record);
  assert.equal(report.frame_work_p99_us.upper, 1000);
  assert.equal(report.targets.frame_work_p99_at_most_25ms, true);
  record.words[112]--; record.words[112 + 25]++; record.words[37]++;
  record.words[65 + 4 * 6] += 25001 - 100;
  record.words[65 + 4 * 4] += 25001 - 100;
  report = boatProfileReport(record);
  assert.equal(report.frame_work_p99_us.lower, 25000);
  assert.equal(report.targets.frame_work_p99_at_most_25ms, false);
});

for (const [name, mutate] of [
  ['transport', r => {r.verified = false;}],
  ['schema', r => {r.words[0]++;}],
  ['device error', r => {r.words[2] = 1;}],
  ['unfinished', r => {r.words[3] = 2;}],
  ['missing steps', r => {r.words[5]--;}],
  ['dropped ticks', r => {r.words[7]++;}],
  ['volume drift', r => {r.words[15]++;}],
  ['SOF mismatch', r => {r.words[13] = 590000;}],
  ['bounds', r => {r.words[18] = 197 * 256;}],
  ['metric count', r => {r.words[64]--;}],
  ['histogram shape', r => {r.words[112]--;}],
  ['status count', r => {r.words[32]--;}],
  ['nonce', r => {r.nonce = '0'.repeat(32);}],
  ['payload length', r => {r.words.pop();}]
]) {
  test(`boat report rejects ${name}`, () => {
    const record = fixture(); mutate(record);
    assert.throws(() => boatProfileReport(record));
  });
}
