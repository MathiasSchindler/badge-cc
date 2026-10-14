import assert from 'node:assert/strict';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const names = ['sensor', 'input', 'physics_step', 'render', 'present',
  'writer_region', 'frame_work', 'completed_frame_interval'];

export function boatProfileReport(record) {
  assert.equal(record.verified, true, 'unverified USB report');
  assert.equal(record.test, 'boat-profile');
  assert.match(record.serial, /^[0-9A-F]{16}$/);
  assert.match(record.build_id, /^[0-9a-f]{8}$/);
  assert.match(record.nonce, /^[0-9a-f]{32}$/);
  assert.notEqual(record.nonce, '0'.repeat(32), 'empty nonce');
  const w = record.words;
  assert(Array.isArray(w) && w.length === 512 &&
    w.every(v => Number.isInteger(v) && v >= 0 && v <= 0xffffffff), 'invalid payload');
  assert.equal(w[0], 0x34544142, 'wrong boat schema');
  assert.equal(w[1], 1);
  assert.equal(w[2], 0, `device error ${w[2] | 0}`);
  assert.equal(w[3], 3, 'unfinished diagnostic');
  assert.equal(record.diagnostic_ok, true, 'collector rejected diagnostic');
  assert.equal(w[5], 60000, 'missing physics steps');
  assert.equal(w[7], 0, 'dropped physics ticks');
  assert.equal(w[16], 0, 'water volume violations');
  assert.equal(w[15], 80 * 80 * 256, 'wrong final water volume');
  assert.equal(w[31], w[15], 'wrong initial water volume');
  assert(w[4] >= 599e6 && w[4] <= 601e6, 'invalid wall-clock duration');
  assert(w[13] > 0 && Math.abs(w[4] / (w[13] * 1000) - 1) <= .01, 'timer/SOF disagreement');
  assert(w[14] > 0 && w[14] <= 100, 'missing/stalled USB SOF sampling');
  assert(w[6] > 1 && w[6] <= 18100, 'invalid completed frames');
  assert(w[12] > 0 && w.slice(32, 37).reduce((a, b) => a + b, 0) === w[12], 'IMU status counts');
  assert(w[9] > 0 && w[9] <= w[6] * 76800 && w[10] > 0, 'invalid panel traffic');
  assert(w[11] <= w[6] && w[11] * 76800 <= w[9], 'invalid full refresh count');
  assert(w[17] <= w[18] && w[18] <= 196 * 256, 'water bounds');
  assert(w[19] >= 20 * 256 && w[20] <= 300 * 256 && w[19] <= w[20], 'hull x bounds');
  assert(w[21] >= 16 * 256 && w[22] <= 228 * 256 && w[21] <= w[22], 'hull y bounds');
  assert(w[23] <= 128, 'hull slope bound');
  const metrics = Object.fromEntries(names.map((name, i) => {
    const [count, sum, min, max] = w.slice(64 + 4 * i, 68 + 4 * i);
    assert(count > 0 && min <= max && sum >= count * min && sum <= count * max, `${name} statistics`);
    const expected = i <= 1 ? w[12] : i === 2 ? w[5] :
      i === 7 ? w[6] - 1 : i === 5 ? null : w[6];
    if (expected !== null) assert.equal(count, expected, `${name} count`);
    return [name, {count, mean_us: sum / count, min_us: min, max_us: max}];
  }));
  const histogram = w.slice(112, 176);
  assert.equal(histogram.reduce((a, b) => a + b, 0), w[6], 'frame histogram count');
  const firstBin = histogram.findIndex(v => v !== 0);
  const lastBin = histogram.findLastIndex(v => v !== 0);
  assert.equal(firstBin, Math.min(63, Math.floor(metrics.frame_work.min_us / 1000)), 'histogram minimum');
  assert.equal(lastBin, Math.min(63, Math.floor(metrics.frame_work.max_us / 1000)), 'histogram maximum');
  assert.equal(w[65 + 4 * 6], w[65 + 4 * 3] + w[65 + 4 * 4], 'render/present/work sum');
  assert(w[65 + 4 * 5] <= w[65 + 4 * 4], 'writer time exceeds presentation');
  let cumulative = 0, p99bin = -1;
  const rank = Math.ceil(w[6] * .99);
  for (let i = 0; i < histogram.length; ++i) {
    cumulative += histogram[i];
    if (cumulative >= rank) { p99bin = i; break; }
  }
  assert(p99bin >= 0);
  assert(w[37] <= w[6], '25ms threshold count');
  assert(w[29] <= w[6] - 1, 'frame gap count');
  const fps = w[6] * 1e6 / w[4];
  return {
    serial: record.serial, build_id: record.build_id, nonce: record.nonce,
    workload: '60000 scripted 10ms steps; 50 tilt/settle cycles; IMU monitored, not driving water',
    duration_s: w[4] / 1e6, timer_sof_ratio: w[4] / (w[13] * 1000),
    steps: w[5], physics_skips: w[7], completed_frames: w[6], completed_fps: fps,
    render_skips: w[8], completed_intervals_over_40ms: w[29],
    frame_work_p99_us: {lower: p99bin * 1000,
      upper: p99bin === 63 ? metrics.frame_work.max_us : (p99bin + 1) * 1000,
      resolution_us: 1000},
    frame_work_over_25ms: w[37],
    targets: {average_30fps: fps >= 30, frame_work_p99_at_most_25ms: w[37] <= w[6] - rank},
    metrics,
    panel: {pixels: w[9], regions: w[10], full_frames: w[11], mean_pixels: w[9] / w[6]},
    water: {volume_q8: w[15], violations: w[16], min_depth_px: w[17] / 256, max_depth_px: w[18] / 256},
    hull: {x_px: [w[19] / 256, w[20] / 256], y_px: [w[21] / 256, w[22] / 256], max_slope_q8: w[23]},
    sensor: {samples: w[12], calibrated: w[24] === 1,
      gyro_bias_raw: w.slice(25, 28).map(v => v | 0), max_sample_gap_us: w[28],
      status_samples: {ready: w[32], calibrating: w[33], pose: w[34], unreliable: w[35], argument: w[36]}},
    scope: 'Instrumented diagnostic includes USB service overhead; no tear-free, physical-fluid or fused-input long-run qualification.'
  };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv.length !== 3) {
    console.error('Usage: node boat_profile_report.mjs measurement.json');
    process.exitCode = 2;
  } else {
    try {
      console.log(JSON.stringify(boatProfileReport(JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))), null, 2));
    } catch (error) {
      console.error(`Boat report rejected: ${error.message}`);
      process.exitCode = 1;
    }
  }
}
