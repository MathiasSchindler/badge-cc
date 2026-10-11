import assert from 'node:assert/strict';

export const suiteId = 0x434f4d50;
export const applicationSuiteId = 0x41505030;
export const applications = [
  { name: 'maze-generation', cases: 16 },
  { name: 'maze-physics-input', cases: 64 },
  { name: 'maze-ram-render', cases: 4 },
  { name: 'maze-display-transfer', cases: 4 },
  { name: 'maze-reached-frame', cases: 16 },
  { name: 'production-diagnostics-graphics', cases: 256 },
];
export const modules = [
  { name: 'arithmetic', cases: 256 },
  { name: 'strings', cases: 8 },
  { name: 'memory', cases: 64 },
  { name: 'control-flow', cases: 128 },
  { name: 'cross-file-abi', cases: 64 },
  { name: 'graphics', cases: 76800 },
];
export const reportBytes = (modules.length + 1) * 64;
export const timingSamples = 7;
export const timingRepeats = 64;
export const kernels = [
  'kernel-arithmetic', 'kernel-branch-phi', 'kernel-cross-file-call',
  'kernel-unsigned-divrem', 'kernel-signed-divrem', 'kernel-ordinary-memory',
].map(name => ({ name, cases: 128 }));
export const timingReportBytes = (16 + kernels.length * 2) * 64;

export function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (~crc) >>> 0;
}

export function request(build, nonce, id = suiteId) {
  assert.equal(nonce.length, 16);
  assert(nonce.some(x => x !== 0), 'zero nonce');
  const wire = Buffer.alloc(64);
  wire.writeUInt32LE(0x51363255);
  wire[4] = 1;
  wire[6] = 64;
  wire.writeUInt32LE(build, 8);
  assert(id === suiteId || id === applicationSuiteId, 'unknown request suite');
  wire.writeUInt32LE(id, 12);
  nonce.copy(wire, 16);
  wire.writeUInt32LE(crc32(wire.subarray(0, 60)), 60);
  return wire;
}

function records(bytes, build, nonce, count, id = suiteId) {
  assert.equal(bytes.length, count * 64, 'truncated or excess result data');
  const records = [];
  for (let i = 0; i < count; i++) {
    const wire = bytes.subarray(i * 64, (i + 1) * 64);
    assert.equal(wire.readUInt32LE(0), 0x31544342, `record ${i}: missing commit/magic`);
    assert.deepEqual([...wire.subarray(4, 8)], [1, 0, 64, 0], `record ${i}: version/length`);
    assert.equal(wire.readUInt32LE(8), build, `record ${i}: wrong build`);
    assert.equal(wire.readUInt32LE(12), id + i, `record ${i}: wrong module/order`);
    assert.deepEqual(wire.subarray(16, 32), nonce, `record ${i}: stale nonce`);
    assert.equal(wire.readUInt32LE(60), crc32(wire.subarray(0, 60)), `record ${i}: CRC mismatch`);
    records.push(Array.from({ length: 7 }, (_, k) => wire.readUInt32LE(32 + k * 4)));
  }
  return records;
}

function moduleResults(results, definitions = modules) {
  return results.map((values, i) => {
    assert.equal(values[0], definitions[i].cases, `${definitions[i].name}: incomplete cases`);
    return { module: definitions[i].name, cases: values[0], values: values.slice(1) };
  });
}

export function decode(bytes, build, nonce) {
  const [summary, ...results] = records(bytes, build, nonce, modules.length + 1);
  assert.deepEqual(summary, [1, modules.length, 0, nonce.readUInt32LE(0), 0, 0, 0],
    'device failure or invalid summary');
  return moduleResults(results);
}

export function decodeApplication(bytes, build, nonce) {
  return decodeTiming(bytes, build, nonce, true);
}

export function decodeTiming(bytes, build, nonce, application = false) {
  const definitions = application ? applications : modules;
  const id = application ? applicationSuiteId : suiteId;
  assert(bytes.length >= 16 * 64, 'truncated timing result data');
  const summary = records(bytes.subarray(0, 64), build, nonce, 1, id)[0];
  assert.equal(summary[0], 1, `device timing failure, detail ${summary[2]} ` +
    '(1-6: module correctness/timing bounds; 7: clock change; 8-13: kernel correctness/timing bounds)');
  const version = summary[6];
  assert(application ? version === 4 : version === 1 || version === 2 || version === 3,
    'unknown timing workload version');
  const count = version === 3 ? timingReportBytes / 64 : 16;
  assert.equal(bytes.length, count * 64, 'truncated or excess result data');
  assert.deepEqual(summary, [1, modules.length, 0, nonce.readUInt32LE(0),
    timingSamples, timingRepeats, version], 'device failure or invalid timing summary');
  const data = records(bytes, build, nonce, count, id);
  const clock = [...data[7], ...data[8]];
  validateTimingClock(clock);
  const overhead = data[9];
  assert(overhead.every(x => x <= 1000), 'excessive timer read overhead');
  const results = moduleResults(data.slice(1, 7), definitions);
  for (let i = 0; i < modules.length; i++) {
    const samples = data[10 + i];
    if (application) assert.equal(results[i].values[4], 1, `${definitions[i].name}: damaged memory guards`);
    results[i].timing = checkedTiming(definitions[i].name, samples, overhead,
      application ? definitions[i].cases : i === 5 ? 1 : timingRepeats);
  }
  const kernelResults = version === 3 ?
    moduleResults(kernels.map((_, i) => data[16 + 2 * i]), kernels) : [];
  for (let i = 0; i < kernelResults.length; i++) {
    const result = kernelResults[i];
    assert.equal(result.values[3], 1, `${result.module}: damaged memory guards`);
    assert.equal(result.values[4], 1, `${result.module}: source modified`);
    assert.equal(result.values[5], kernels[i].cases, `${result.module}: unequal kernel work`);
    result.timing = { ...checkedTiming(result.module, data[17 + 2 * i], overhead, timingRepeats),
      iterationsPerCall: kernels[i].cases };
  }
  return { modules: results, kernels: kernelResults, clock, overhead, version };
}

export function validateTimingClock(clock) {
  assert.equal(clock.length, 14, 'incomplete clock snapshot');
  assert(clock.every(x => Number.isInteger(x) && x >= 0 && x <= 0xffffffff), 'invalid clock register');
  assert((clock[7] & 0x40000000) === 0 && (clock[10] & 0x40000000) === 0,
    'PLL lock-loss flag during timing');
  assert(clock[6] === 0 || clock[6] === 1, 'unknown timer source');
  if (clock[6] === 0)
    assert((clock[4] & 1) !== 0 && clock[5] > 0, 'disabled timer tick source');
}

function checkedTiming(name, samples, overhead, repetitions) {
  const statistics = timingStatistics(samples);
  assert(samples.every(x => x >= 100 && x <= 1000000),
    `${name}: under-resolved or overlong timing sample`);
  assert(Math.min(...samples) >= 100 * Math.max(...overhead),
    `${name}: timer overhead exceeds 1%`);
  return { repetitions, samples, ...statistics };
}

export function timingStatistics(samples) {
  assert.equal(samples.length, timingSamples, 'wrong timing sample count');
  assert(samples.every(x => Number.isInteger(x) && x >= 0 && x <= 0xffffffff),
    'invalid timing sample');
  const sorted = [...samples].sort((a, b) => a - b);
  return { min: sorted[0], median: sorted[3], max: sorted[6] };
}

export function compareTiming(llvm, owned) {
  return compareTimings(llvm, owned, llvm.version === 4 ? applications : modules, 'modules');
}

export function compareKernelTiming(llvm, owned) {
  assert.equal(llvm.version, 3, 'kernel timing requires version 3');
  return compareTimings(llvm, owned, kernels, 'kernels');
}

function compareTimings(llvm, owned, definitions, field) {
  assert.equal(owned.version, llvm.version, 'timing workload versions differ');
  assert.deepEqual(owned.clock, llvm.clock, 'badge clock configurations differ');
  return definitions.map((module, i) => {
    const a = llvm[field][i].timing, b = owned[field][i].timing;
    assert.equal(a.repetitions, b.repetitions, `${module.name}: unequal timing work`);
    assert(a.median > 0 && b.median > 0, `${module.name}: zero timing denominator`);
    const ratio = b.median / a.median;
    return { unit: 'timer0-raw-ticks', repetitions: a.repetitions,
      ...(field === 'kernels' ? { iterationsPerCall: module.cases } : {}),
      llvm: a, owned: b, ownedToLlvm: ratio, percentSlower: (ratio - 1) * 100 };
  });
}

export function compare(llvm, owned, definitions = modules) {
  assert.equal(llvm.length, definitions.length);
  assert.equal(owned.length, definitions.length);
  return definitions.map((module, i) => {
    assert.equal(llvm[i].module, module.name);
    assert.equal(owned[i].module, module.name);
    for (const result of [llvm[i], owned[i]]) {
      assert.equal(result.cases, module.cases);
      assert.equal(result.values.length, 6);
      assert(result.values.every(x => Number.isInteger(x) && x >= 0 && x <= 0xffffffff),
        `${module.name}: invalid result lane`);
    }
    const differences = [];
    for (let k = 0; k < 6; k++) {
      if (llvm[i].values[k] !== owned[i].values[k])
        differences.push({ lane: k, llvm: llvm[i].values[k], owned: owned[i].values[k] });
    }
    return { module: module.name, cases: module.cases, matched: differences.length === 0,
      llvm: llvm[i].values, owned: owned[i].values, differences };
  });
}
