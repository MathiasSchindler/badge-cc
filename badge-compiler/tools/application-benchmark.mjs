import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { applications, applicationSuiteId, request, decodeApplication, compare,
  compareTiming } from './compiler-test-protocol.mjs';
import { verifyCampaign, assessTiming } from './qualify-datasheet.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const walk = directory => fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
  const file = path.join(directory, entry.name);
  return entry.isDirectory() ? walk(file) : entry.isFile() ? [file] : [];
});
const policy = { ownedPasses: 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division',
  ownedFrameAddressing: 'direct', ownedRegisterResidency: 'global-compact',
  ownedScalarSelection: 'thumb-layout', ownedMemorySelection: 'word' };

export function verifyApplicationCampaign(file, scalarSelection = 'thumb-layout', expressionLowering = 'legacy',
  passes = policy.ownedPasses) {
  const { result, artifacts } = verifyCampaign(file);
  assert.equal(result.matched, true, 'campaign not matched');
  assert.equal(result.rounds.length, 2, 'missing role swap');
  for (const [key, value] of Object.entries(policy))
    assert.equal(result.toolchain[key], key === 'ownedScalarSelection' ? scalarSelection :
      key === 'ownedPasses' ? passes : value,
      `unexpected application policy: ${key}`);
  assert.match(result.toolchain.llvm, /clang version /);
  assert.equal(result.toolchain.ownedExpressionLowering ?? 'legacy', expressionLowering,
    'unexpected frontend expression lowering');
  const directory = path.dirname(file);
  const serials = result.rounds[0].configurations.map(config => config.serial);
  assert.equal(new Set(serials).size, 2, 'distinct physical badges required');
  const seeds = new Set(), nonces = new Set();
  for (const [index, round] of result.rounds.entries()) {
    assert.equal(round.timingVersion, 4, 'not an application campaign');
    assert.equal(round.matched, true);
    assert.match(round.nonce, /^[0-9a-f]{32}$/);
    assert(!nonces.has(round.nonce), 'reused nonce'); nonces.add(round.nonce);
    const nonce = Buffer.from(round.nonce, 'hex');
    assert.equal(round.seed, nonce.readUInt32LE(0), 'seed differs from request');
    seeds.add(round.seed);
    assert.deepEqual(round.configurations.map(config => config.name), ['llvm', 'owned']);
    assert.deepEqual(round.configurations.map(config => config.serial),
      index === 0 ? serials : [...serials].reverse(), 'physical roles not swapped');
    assert.equal(round.configurations[0].build, round.configurations[1].build, 'different sources');
    const requestFile = path.join(directory, `round${index + 1}-request.bin`);
    assert.deepEqual(fs.readFileSync(requestFile),
      request(round.configurations[0].build, nonce, applicationSuiteId));
    artifacts.push({ file: relative(requestFile), sha256: hash(requestFile) });
    const decoded = round.configurations.map(config => decodeApplication(
      fs.readFileSync(path.join(directory, `round${index + 1}-${config.name}.bin`)), config.build, nonce));
    const comparisons = compare(decoded[0].modules, decoded[1].modules, applications);
    const timings = compareTiming(decoded[0], decoded[1]);
    comparisons.forEach((row, i) => { row.timing = timings[i]; });
    assert(comparisons.every(row => row.matched), 'raw application outputs differ');
    assert.deepEqual(round.modules, comparisons, 'summary differs from raw application results');
    assert.deepEqual(round.kernels, []);
  }
  assert.equal(seeds.size, 1, 'role-swapped inputs differ');
  result.modules.forEach((row, i) => {
    const runs = result.rounds.map(round => round.modules[i].timing);
    const ratio = Math.sqrt(runs[0].ownedToLlvm * runs[1].ownedToLlvm);
    assert.deepEqual(row, { ...result.rounds[0].modules[i],
      timing: { unit: 'timer0-raw-ticks', runs, ownedToLlvm: ratio, percentSlower: (ratio - 1) * 100 } });
  });
  assert.equal(result.modules.length, applications.length);
  return { result, artifacts };
}

export function preserveFlash(serials, directory, command, report, save, body) {
  try {
    for (const serial of serials) {
      const identity = command(['info', '-d', '--ser', serial], `${serial}-identity`);
      assert(/current cpu:\s+ARM/.test(identity) && /secure boot:\s+0/.test(identity) &&
        /flash size:\s+16384K/.test(identity) && /RP2350/.test(identity), 'unexpected badge identity');
      const file = path.join(directory, `${serial}-original.bin`);
      command(['save', '-a', '-v', file, '-t', 'bin', '--ser', serial], `${serial}-snapshot`);
      assert.equal(fs.statSync(file).size, 16777216, 'incomplete full-flash snapshot');
      report.backups.push({ serial, file: relative(file), sha256: hash(file) });
      save();
    }
    body();
  } catch (error) {
    report.errors.push(error.message);
  } finally {
    for (const backup of report.backups) {
      try {
        const original = path.join(root, backup.file);
        assert.equal(hash(original), backup.sha256, 'original backup modified');
        command(['load', '-u', '-v', original, '-t', 'bin', '-o', '0x10000000',
          '--ser', backup.serial], `${backup.serial}-restore`);
        const reread = path.join(directory, `${backup.serial}-restored.bin`);
        command(['save', '-a', '-v', reread, '-t', 'bin', '--ser', backup.serial],
          `${backup.serial}-verify`);
        assert.equal(fs.statSync(reread).size, 16777216, 'incomplete restored readback');
        assert.equal(hash(reread), backup.sha256, 'restored flash differs from snapshot');
        command(['info', '-d', '--ser', backup.serial], `${backup.serial}-final-identity`);
        report.restoration.push({ serial: backup.serial, verified: true, left_in_bootsel: true,
          reread: relative(reread), sha256: backup.sha256 });
      } catch (error) {
        report.errors.push(`Restoration ${backup.serial}: ${error.message}`);
        report.restoration.push({ serial: backup.serial, verified: false, error: error.message });
      }
      save();
    }
  }
  if (report.errors.length) throw new Error(report.errors.join('\n'));
}

export function verifyApplicationReport(file, currentSources = false) {
  const report = read(file);
  assert.equal(report.schema, 1);
  const homes = report.compare_homes === true;
  const expressions = report.compare_expressions === true;
  const memory = report.compare_memory === true;
  const calls = report.compare_calls === true;
  assert([report.compare_branch, report.compare_homes, report.compare_expressions, report.compare_memory, report.compare_calls].filter(x => x === true).length <= 1,
    'overlapping comparison modes');
  const branch = report.compare_branch === true || homes || expressions || memory || calls;
  assert(!branch || !report.fixed_ir, 'candidate experiment cannot use fixed-IR reference');
  assert.equal(report.status, 'measured-restored');
  assert.deepEqual(report.errors, []);
  assert.equal(report.campaigns.length, branch ? 8 : 2);
  assert.equal(report.backups.length, 2);
  assert.equal(report.restoration.length, 2);
  const nonces = new Set(), seeds = new Set(), clocks = [];
  const identities = new Set();
  let initialInputs;
  for (const backup of report.backups) {
    const restored = report.restoration.find(item => item.serial === backup.serial);
    assert(restored?.verified && restored.left_in_bootsel, 'unverified restoration');
    for (const artifact of [backup, { file: restored.reread, sha256: restored.sha256 }]) {
      const absolute = path.join(root, artifact.file);
      assert.equal(fs.statSync(absolute).size, 16777216);
      assert.equal(hash(absolute), backup.sha256, 'full-flash verification mismatch');
    }
  }
  const campaigns = report.campaigns.map(campaign => {
    const absolute = path.join(root, campaign.file);
    assert.equal(hash(absolute), campaign.sha256, 'campaign changed');
    if (branch) {
      assert(['control', 'candidate'].includes(campaign.profile));
      assert([0, 1].includes(campaign.repeat));
      assert([3276377847, 123456789].includes(campaign.seed));
      const identity = `${campaign.seed}/${campaign.repeat}/${campaign.profile}`;
      assert(!identities.has(identity), 'duplicate comparison campaign');
      identities.add(identity);
    }
    const verified = verifyApplicationCampaign(absolute,
      calls ? campaign.profile === 'candidate' ? 'thumb-calls' : 'thumb-homes' :
      expressions || memory ? 'thumb-homes' : homes ? campaign.profile === 'candidate' ? 'thumb-homes' : 'thumb-branch' :
        branch && campaign.profile === 'candidate' ? 'thumb-branch' : 'thumb-layout',
      memory || calls || expressions && campaign.profile === 'candidate' ? 'ssa' : 'legacy',
      policy.ownedPasses + (memory && campaign.profile === 'candidate' ? ',local-memory' : ''));
    assert.equal(Boolean(verified.result.toolchain.reference), Boolean(report.fixed_ir || memory),
      'backend reference differs from declared experiment');
    if (memory) {
      assert.match(verified.result.toolchain.reference, /byte-identical canonical owned initial SSA IR/);
      assert.equal(verified.result.toolchain.initialIr.length, 11);
      const inputs = verified.result.toolchain.initialIr.map(input => [input.module, input.sha256]);
      if (initialInputs) assert.deepEqual(inputs, initialInputs, 'frontend input drift between optimizer campaigns');
      else initialInputs = inputs;
    }
    assert.deepEqual(verified.artifacts, campaign.artifacts);
    if (!branch) assert(!seeds.has(campaign.seed), 'duplicate input seed');
    seeds.add(campaign.seed);
    for (const round of verified.result.rounds) {
      assert.equal(round.seed, campaign.seed);
      assert(!nonces.has(round.nonce), 'campaign nonce reused'); nonces.add(round.nonce);
      assert.deepEqual([...round.configurations.map(config => config.serial)].sort(),
        report.backups.map(backup => backup.serial).sort());
      clocks.push(...round.clocks);
    }
    return verified.result;
  });
  assert(clocks.every(clock => JSON.stringify(clock) === JSON.stringify(clocks[0])),
    'campaign clocks differ');
  assert.equal(seeds.size, 2);
  if (branch) {
    assert.deepEqual(report.summary, summarizeProfiles(report.campaigns));
    assert.deepEqual(report.decisions, branchDecisions(report.campaigns));
  } else assert.deepEqual(report.summary, summarize(campaigns));
  if (currentSources) {
    for (const [source, digest] of Object.entries(report.sources))
      assert.equal(hash(path.join(root, source)), digest, `source drift: ${source}`);
  }
  return report;
}

function summarize(campaigns) {
  return applications.map((definition, i) => {
    const observations = campaigns.flatMap(campaign => campaign.rounds.map(round => ({
      seed: round.seed, llvm_serial: round.configurations[0].serial,
      owned_serial: round.configurations[1].serial, ...round.modules[i].timing,
    })));
    const ratios = observations.map(observation => observation.ownedToLlvm);
    return { workload: definition.name, cases: definition.cases, observations,
      owned_to_llvm_geomean: Math.exp(ratios.reduce((sum, ratio) => sum + Math.log(ratio), 0) / ratios.length),
      ratio_min: Math.min(...ratios), ratio_max: Math.max(...ratios) };
  });
}

function summarizeProfiles(campaigns) {
  return Object.fromEntries(['control', 'candidate'].filter(profile =>
    campaigns.some(c => c.profile === profile)).map(profile => [profile,
    summarize(campaigns.filter(c => c.profile === profile).map(c => read(path.join(root, c.file))))]));
}
function branchDecisions(campaigns) {
  return [3276377847, 123456789].map(seed => {
    const get = (profile, repeat) => {
      const campaign = campaigns.find(c => c.seed === seed && c.profile === profile && c.repeat === repeat);
      assert(campaign, `missing ${seed}/${repeat}/${profile}`);
      return read(path.join(root, campaign.file));
    };
    return { seed, timing: assessTiming(get('control', 0), get('candidate', 0),
      get('control', 1), get('candidate', 1)) };
  });
}

function measure(serials, fixedIr = false, branch = false, homes = false, expressions = false, memory = false, calls = false) {
  const compared = branch || homes || expressions || memory || calls;
  const controlSelection = expressions || memory || calls ? 'thumb-homes' : homes ? 'thumb-branch' : 'thumb-layout';
  const candidateSelection = calls ? 'thumb-calls' : homes || expressions || memory ? 'thumb-homes' : 'thumb-branch';
  const output = path.join(root, 'build/application-benchmark-runs');
  fs.mkdirSync(output, { recursive: true });
  const directory = fs.mkdtempSync(path.join(output, 'run.'));
  const report = { schema: 1, status: 'in-progress', started_at: new Date().toISOString(),
    backups: [], restoration: [], campaigns: [], errors: [], sources: {}, summary: [],
    ...(fixedIr ? { fixed_ir: true } : {}), ...(branch ? { compare_branch: true } : {}),
    ...(homes ? { compare_homes: true } : {}), ...(expressions ? { compare_expressions: true } : {}),
    ...(memory ? { compare_memory: true } : {}), ...(calls ? { compare_calls: true } : {}) };
  const reportFile = path.join(directory, 'results.json');
  const save = () => fs.writeFileSync(reportFile, JSON.stringify(report, null, 2) + '\n');
  const env = { ...process.env };
  for (const key of ['BADGE_OPT_PASSES', 'BADGE_CODEGEN_FRAME_ADDRESSING',
    'BADGE_CODEGEN_REGISTER_RESIDENCY', 'BADGE_CODEGEN_SCALAR_SELECTION', 'BADGE_CODEGEN_MEMORY_SELECTION',
    'BADGE_IR_EXPRESSION_LOWERING'])
    delete env[key];
  env.BADGE_OPT_PASSES = policy.ownedPasses;
  env.BADGE_CODEGEN_FRAME_ADDRESSING = policy.ownedFrameAddressing;
  env.BADGE_CODEGEN_REGISTER_RESIDENCY = policy.ownedRegisterResidency;
  env.BADGE_CODEGEN_SCALAR_SELECTION = controlSelection;
  env.BADGE_CODEGEN_MEMORY_SELECTION = policy.ownedMemorySelection;
  env.BADGE_IR_EXPRESSION_LOWERING = 'legacy';
  const run = (executable, args, label, timeout = 180000) => {
    const result = spawnSync(executable, args, { cwd: root, env, timeout,
      encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
    const text = `${result.stdout ?? ''}${result.stderr ?? ''}${result.error ?? ''}`;
    fs.writeFileSync(path.join(directory, `${label}.log`), text);
    assert.ifError(result.error);
    assert.equal(result.status, 0, `${label}: ${text.slice(-4000)}`);
    return result.stdout;
  };
  const picotool = process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool');
  console.log(`Application benchmark evidence: ${directory}`);
  save();
  try {
    const sources = [...walk(path.join(root, 'src')), ...walk(path.join(root, 'badge-compiler/src')),
      ...walk(path.join(root, 'badge-compiler/scripts')), ...walk(path.join(root, 'badge-compiler/tools')),
      path.join(root, 'Makefile'), path.join(root, 'badge-compiler/Makefile')];
    report.sources = Object.fromEntries(sources.map(file => [relative(file), hash(file)]));
    const unchanged = () => {
      for (const [file, digest] of Object.entries(report.sources))
        assert.equal(hash(path.join(root, file)), digest, `source changed during capture: ${file}`);
    };
    preserveFlash(serials, directory, (args, label) => run(picotool, args, label),
      report, save, () => {
        for (const seed of [3276377847, 123456789])
          for (let repeat = 0; repeat < (compared ? 2 : 1); repeat++)
            for (const profile of compared ? ['control', 'candidate'] : ['control']) {
          if (compared) env.BADGE_CODEGEN_SCALAR_SELECTION =
            profile === 'candidate' ? candidateSelection : controlSelection;
          if (expressions) env.BADGE_IR_EXPRESSION_LOWERING = profile === 'candidate' ? 'ssa' : 'legacy';
          if (calls) env.BADGE_IR_EXPRESSION_LOWERING = 'ssa';
          if (memory) {
            env.BADGE_IR_EXPRESSION_LOWERING = 'ssa';
            env.BADGE_OPT_PASSES = policy.ownedPasses + (profile === 'candidate' ? ',local-memory' : '');
          }
          unchanged();
          const stdout = run(process.execPath, ['badge-compiler/tools/compiler-compare.mjs',
            '--application', ...(memory ? ['--frontend-ir'] : fixedIr ? ['--fixed-ir'] : []),
            `--seed=${seed}`, ...serials], `campaign-${seed}-${repeat}-${profile}`, 600000);
          const campaignDirectory = stdout.match(/Compiler comparison: ([^\n]+)/)?.[1];
          assert(campaignDirectory, 'missing campaign evidence path');
          const file = path.join(campaignDirectory, 'results.json');
          const { result, artifacts } = verifyApplicationCampaign(file,
            compared && profile === 'candidate' ? candidateSelection : controlSelection,
            memory || calls || expressions && profile === 'candidate' ? 'ssa' : 'legacy',
            policy.ownedPasses + (memory && profile === 'candidate' ? ',local-memory' : ''));
          report.campaigns.push({ seed, ...(compared ? { repeat, profile } : {}),
            file: relative(file), sha256: hash(file), artifacts });
          report.summary = compared ? summarizeProfiles(report.campaigns) :
            summarize(report.campaigns.map(item => read(path.join(root, item.file))));
          save();
          assert.equal(result.rounds[0].seed, seed);
          console.log(`Captured ${seed}/${repeat}/${profile}: both physical roles, all application results match.`);
        }
        unchanged();
      });
    report.status = 'measured-restored';
    if (compared) report.decisions = branchDecisions(report.campaigns);
    save();
    verifyApplicationReport(reportFile, true);
  } catch (error) {
    if (!report.errors.includes(error.message)) report.errors.push(error.message);
    report.status = 'failed';
    process.exitCode = 1;
  }
  report.completed_at = new Date().toISOString();
  report.scope = { unit: 'timer0-raw-ticks',
    llvm_reference: memory ? 'LLVM -Oz optimizer/backend of byte-identical owned initial SSA IR' :
      fixedIr ? 'LLVM -Oz backend of owned optimized IR, normal IR optimizer disabled' : '-Oz freestanding Cortex-M33',
    method: compared ? 'Two seeds; repeated baseline/candidate campaigns, two physical role swaps each; seven warm samples per workload/badge/round. Exact per-role/per-repeat noise gates; no averaging away regressions.' :
      'Two seeds, two physical role swaps each, seven warm samples per workload/badge/round. Per-workload geometric mean of four median ratios; no aggregate score.',
    limitations: ['Scripted sensor samples; physical IMU acquisition and motion remain a separate unqualified gate.',
      'Actual panel GPIO transfer executes; visual panel correctness/bus waveform capture is not certified.',
      'Warm-working-state only, no controlled cold-XIP or startup measurement.',
      'Frame includes scripted input, physics, elapsed-time update, reached render policy and display transfer; excludes I2C acquisition, real button polling and main-loop pacing.',
      'Setup, full RAM checksums, guard checks and USB/watchdog service are outside timed intervals.',
      'Generation includes loop and level selection; physics includes scripted state placement and sample preparation.',
      'Transfer includes production clipping, marble overlay lookup, region commands and GPIO pixel writes.',
      'Guarded canvas plus logical wall/cell bounds; native ASan/UBSan and target semantic tests supplement, not replace, arbitrary-program coverage.',
      'Benchmark-only large-stack linker, not a production-stack-size certification.'] };
  save();
  console.log(JSON.stringify({ file: relative(reportFile), status: report.status,
    restoration: report.restoration, errors: report.errors,
    timings: report.summary,
    ...(compared ? { decisions: report.decisions?.map(d => ({ seed: d.seed,
      improvements: d.timing.repeatable_improvements, regressions: d.timing.regressions })) } : {})
  }, null, 2));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  if (args.length === 2 && (args[0] === '--verify' || args[0] === '--verify-current')) {
    try {
      verifyApplicationReport(path.resolve(args[1]), args[0] === '--verify-current');
      console.log('PASS: raw application reports, roles, nonces, clocks, images and full flash restoration.');
    } catch (error) { console.error(error.message); process.exitCode = 1; }
  } else if (args.length === 3 && ['--measure', '--measure-fixed-ir', '--measure-branch', '--measure-homes', '--measure-expressions', '--measure-memory', '--measure-calls'].includes(args[0]) &&
      args.slice(1).every(serial => /^[0-9a-f]{16}$/i.test(serial)) &&
      args[1].toUpperCase() !== args[2].toUpperCase()) {
    measure(args.slice(1).map(serial => serial.toUpperCase()), args[0] === '--measure-fixed-ir',
      args[0] === '--measure-branch', args[0] === '--measure-homes', args[0] === '--measure-expressions',
      args[0] === '--measure-memory', args[0] === '--measure-calls');
  } else {
    console.error('Usage: node badge-compiler/tools/application-benchmark.mjs --measure[-fixed-ir|-branch|-homes|-expressions|-memory|-calls] SERIAL SERIAL | --verify[-current] REPORT.json');
    process.exitCode = 2;
  }
}
