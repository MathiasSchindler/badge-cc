import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { modules, kernels, applications, timingSamples, timingRepeats, timingStatistics, validateTimingClock } from './compiler-test-protocol.mjs';

export function validateTimingCampaign(report) {
  assert.equal(report.schema, 3, 'requires a P00 schema-3 report');
  assert.equal(report.matched, true, 'unmatched or incomplete campaign');
  assert.equal(report.rounds.length, 2, 'requires two role-swapped rounds');
  assert.match(report.toolchain.owned['badge-codegen'], /^[0-9a-f]{64}$/, 'missing C07 provenance');
  const first = report.rounds[0];
  const application = first.timingVersion === 4;
  for (const [index, round] of report.rounds.entries()) {
    assert.equal(round.matched, true, 'unmatched round');
    assert.equal(round.timingVersion, application ? 4 : 3, 'timing workload versions differ');
    assert.equal(round.seed, first.seed, 'seeds differ within campaign');
    assert.match(round.nonce, /^[0-9a-f]{32}$/, 'invalid nonce');
    assert.equal(Buffer.from(round.nonce, 'hex').readUInt32LE(0), round.seed, 'nonce/seed mismatch');
    assert.deepEqual(round.configurations.map(x => x.name), ['llvm', 'owned'], 'wrong roles');
    assert.equal(new Set(round.configurations.map(x => x.serial)).size, 2, 'duplicate devices');
    assert.deepEqual(round.configurations.map(x => x.serial),
      index === 0 ? first.configurations.map(x => x.serial) : first.configurations.map(x => x.serial).reverse(),
      'roles were not swapped');
    for (const config of round.configurations) {
      assert.match(config.serial, /^[0-9A-F]{16}$/, 'invalid device');
      assert.match(config.sha256, /^[0-9a-f]{64}$/, 'invalid image provenance');
      assert(Number.isInteger(config.build) && config.build >= 0 && config.build <= 0xffffffff,
        'invalid source-suite identity');
      assert.equal(config.build, first.configurations[0].build, 'source-suite identities differ');
      assert.equal(config.sha256, first.configurations.find(x => x.name === config.name).sha256,
        'images changed within campaign');
    }
    assert.equal(round.clocks.length, 2);
    assert.deepEqual(round.clocks[0], round.clocks[1], 'badge clocks differ');
    assert.deepEqual(round.clocks, first.clocks, 'clocks changed within campaign');
    validateTimingClock(round.clocks[0]);
    assert.equal(round.overhead.length, 2);
    for (const samples of round.overhead) {
      assert.equal(samples.length, timingSamples);
      assert(samples.every(x => Number.isInteger(x) && x >= 0 && x <= 1000), 'invalid overhead');
    }
    for (const [field, definitions] of [['modules', application ? applications : modules],
      ['kernels', application ? [] : kernels]]) {
      assert.equal(round[field].length, definitions.length, `incomplete ${field}`);
      for (const [i, result] of round[field].entries()) {
        assert.equal(result.module, definitions[i].name, 'workload order/name differs');
        assert.equal(result.cases, definitions[i].cases, 'workload count differs');
        assert.equal(result.matched, true, 'unmatched workload');
        assert.equal(result.llvm.length, 6);
        assert(result.llvm.every(x => Number.isInteger(x) && x >= 0 && x <= 0xffffffff));
        assert.deepEqual(result.owned, result.llvm, 'result fingerprints differ');
        if (field === 'kernels') {
          assert.deepEqual(result.owned.slice(3), [1, 1, definitions[i].cases], 'kernel guard/work failure');
          assert.equal(result.timing.iterationsPerCall, definitions[i].cases, 'kernel iteration count differs');
        }
        if (application) assert.equal(result.owned[4], 1, 'application memory guard failure');
        const repetitions = application ? definitions[i].cases :
          field === 'modules' && i === 5 ? 1 : timingRepeats;
        assert.equal(result.timing.repetitions, repetitions, 'batch work differs');
        for (const [role, name] of ['llvm', 'owned'].entries()) {
          const timing = result.timing[name];
          assert.equal(timing.repetitions, repetitions, 'role repetition count differs');
          if (field === 'kernels')
            assert.equal(timing.iterationsPerCall, definitions[i].cases, 'role iteration count differs');
          const statistics = timingStatistics(timing.samples);
          for (const key of ['min', 'median', 'max'])
            assert.equal(timing[key], statistics[key], 'reported statistics disagree with raw samples');
          assert(statistics.min >= 100 && statistics.max <= 1000000, 'invalid timing bounds');
          assert(statistics.min >= 100 * Math.max(...round.overhead[role]), 'excessive timer overhead');
        }
      }
    }
  }
}

export function compareTimingReports(baseline, candidate) {
  validateTimingCampaign(baseline);
  validateTimingCampaign(candidate);
  assert.equal(candidate.rounds[0].timingVersion, baseline.rounds[0].timingVersion,
    'timing workload versions differ');
  assert.equal(candidate.toolchain.llvm, baseline.toolchain.llvm, 'LLVM toolchains differ');
  const nonces = [...baseline.rounds, ...candidate.rounds].map(x => x.nonce);
  assert.equal(new Set(nonces).size, 4, 'nonces are not fresh across campaigns');
  for (let i = 0; i < 2; i++) {
    const a = baseline.rounds[i], b = candidate.rounds[i];
    assert.equal(b.seed, a.seed, 'baseline/candidate seeds differ');
    assert.deepEqual(b.clocks, a.clocks, 'baseline/candidate clocks differ');
    assert.deepEqual(b.configurations.map(x => [x.name, x.serial, x.build]),
      a.configurations.map(x => [x.name, x.serial, x.build]), 'roles/devices/source suites differ');
    assert.equal(b.configurations[0].sha256, a.configurations[0].sha256, 'LLVM control images differ');
    for (const field of ['modules', 'kernels'])
      for (let k = 0; k < a[field].length; k++)
        assert.deepEqual(b[field][k].llvm, a[field][k].llvm, 'baseline/candidate fingerprints differ');
  }
  const compare = field => baseline.rounds[0][field].map((result, i) => {
    const runs = baseline.rounds.map((round, r) => {
      const a = round[field][i].timing, b = candidate.rounds[r][field][i].timing;
      return { ownedSerial: round.configurations[1].serial,
        baselineNonce: round.nonce, candidateNonce: candidate.rounds[r].nonce,
        baselineOwned: a.owned.median, candidateOwned: b.owned.median,
        baselineLlvm: a.llvm.median, candidateLlvm: b.llvm.median };
    });
    const ratio = name => Math.sqrt(runs.map(x => x[`candidate${name}`] / x[`baseline${name}`])
      .reduce((a, b) => a * b, 1));
    const owned = ratio('Owned'), llvm = ratio('Llvm');
    return { module: result.module, cases: result.cases, runs, candidateToBaselineOwned: owned,
      percentChange: (owned - 1) * 100, llvmControlChange: (llvm - 1) * 100 };
  });
  return { schema: 1, unit: 'timer0-raw-ticks', seed: baseline.rounds[0].seed,
    build: baseline.rounds[0].configurations[0].build,
    clock: baseline.rounds[0].clocks[0],
    baselineImages: baseline.rounds[0].configurations.map(({ name, sha256 }) => ({ name, sha256 })),
    candidateImages: candidate.rounds[0].configurations.map(({ name, sha256 }) => ({ name, sha256 })),
    baselineToolchain: baseline.toolchain, candidateToolchain: candidate.toolchain,
    method: 'paired physical roles; geometric mean of candidate/baseline owned median ratios; LLVM drift reported separately',
    modules: compare('modules'), kernels: compare('kernels') };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv.length !== 4) {
    console.error('Usage: node badge-compiler/tools/compiler-timing-compare.mjs BASELINE/results.json CANDIDATE/results.json');
    process.exit(2);
  }
  console.log(JSON.stringify(compareTimingReports(
    JSON.parse(fs.readFileSync(process.argv[2], 'utf8')),
    JSON.parse(fs.readFileSync(process.argv[3], 'utf8'))), null, 2));
}
