import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { preserveFlash, verifyApplicationCampaign } from './application-benchmark.mjs';
import { assessTiming, verifyCampaign } from './qualify-datasheet.mjs';
import { verifyWidthSelection } from './width-selection-evidence.mjs';
import { retryableTransport } from './pipeline-promotion.mjs';
import { validateTimingCampaign } from './compiler-timing-compare.mjs';
import { verifyCachedBuild } from './compiler-image-cache.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const artifact = file => ({ file: relative(file), sha256: hash(file) });
const passes = 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory';
export const currentProfiles = [
  { id: 'control', expression: 'ssa', scalar: 'thumb-calls', passes },
  { id: 'candidate', expression: 'ssa', scalar: 'thumb-widths', passes: passes + ',loop-scalar,cross-function' },
];
export const currentPlan = { schema: 1, milestone: 'PO18', profiles: currentProfiles,
  suites: ['application', 'timing'], seeds: [3276377847, 123456789], repeats: 2, physical_role_swaps: true,
  comparison: 'exact integer noise envelopes, same-board repeated-control drift and paired LLVM drift',
  scope: 'PO11-PO17 combined emission versus promoted SSA/local-memory/thumb-calls; no historical profile matrix',
  promotion: 'Promote the combination only with matching raw results, no resolved regression, ordinary size/resource gates, timing audit and verified restoration.',
  timing: 'Live USB cold starts, clock/XIP identity and serviced display initialization; compiler speed measured separately from firmware delay mitigation.',
  reuse: 'Seeds and board roles are runtime inputs. Content-keyed images include sources, headers, scripts, flags, policy and tools; verify every output hash.',
};
function checkArtifacts(items) {
  for (const a of items) assert.equal(hash(absolute(a.file)), a.sha256, a.file);
}
export function decideCurrentPromotion(rows, restoration) {
  assert(restoration.length === 2 && restoration.every(r => r.verified && r.left_in_bootsel));
  const regressions = rows.flatMap(row => row.timing.regressions.map(workload => ({ suite: row.suite, seed: row.seed, workload })));
  return { eligible: regressions.length === 0, regressions,
    repeatable_improvements: rows.flatMap(row => row.timing.repeatable_improvements.map(workload =>
      ({ suite: row.suite, seed: row.seed, workload }))),
    decision: regressions.length ? 'reject-speed-default-retain-explicit' : 'eligible-for-default-after-production-timing-audit',
    promoted: false };
}
export function verifyCurrentPromotion(file) {
  const report = read(file);
  assert.equal(report.status, 'measured-restored');
  assert.deepEqual(report.plan, currentPlan);
  assert.deepEqual(report.errors, []);
  checkArtifacts([report.offline, ...report.artifacts]);
  verifyWidthSelection(absolute(report.offline.file));
  assert.equal(report.backups.length, 2); assert.equal(report.restoration.length, 2);
  for (const b of report.backups) {
    const restored = report.restoration.find(r => r.serial === b.serial);
    assert(restored?.verified && restored.left_in_bootsel);
    for (const name of [b.file, restored.reread]) {
      assert.equal(fs.statSync(absolute(name)).size, 16777216);
      assert.equal(hash(absolute(name)), b.sha256);
    }
  }
  const expected = currentPlan.suites.length * currentPlan.seeds.length * currentPlan.repeats * currentProfiles.length;
  assert.equal(report.campaigns.length, expected);
  const seen = new Set(), nonces = new Set(), cacheKeys = new Set();
  let clocks;
  for (const c of report.campaigns) {
    const key = `${c.suite}/${c.seed}/${c.repeat}/${c.id}`;
    assert(!seen.has(key)); seen.add(key);
    assert(currentPlan.suites.includes(c.suite) && currentPlan.seeds.includes(c.seed) &&
      c.repeat >= 0 && c.repeat < currentPlan.repeats);
    const profile = currentProfiles.find(p => p.id === c.id); assert(profile);
    checkArtifacts([c, ...c.artifacts]);
    const { result } = c.suite === 'application' ?
      verifyApplicationCampaign(absolute(c.file), profile.scalar, profile.expression, profile.passes) :
      verifyCampaign(absolute(c.file));
    validateTimingCampaign(result);
    assert.equal(result.toolchain.ownedScalarSelection, profile.scalar);
    assert.equal(result.toolchain.ownedPasses, profile.passes);
    assert.equal(result.toolchain.ownedExpressionLowering, profile.expression);
    for (const round of result.rounds) {
      assert.equal(round.seed, c.seed); assert(!nonces.has(round.nonce)); nonces.add(round.nonce);
      assert.deepEqual(round.configurations.map(x => x.serial).sort(), report.backups.map(x => x.serial).sort());
      if (!clocks) clocks = round.clocks;
      else assert.deepEqual(round.clocks, clocks, 'clock/XIP drift');
      for (const config of round.configurations) {
        const cache = read(config.cache.manifest);
        verifyCachedBuild(path.dirname(config.cache.manifest), cache.identity);
        assert.equal(config.sha256, hash(config.image));
        cacheKeys.add(config.cache.key);
      }
    }
  }
  assert.equal(cacheKeys.size, 6, 'two suites: one LLVM and two owned identities each');
  const get = (suite, seed, id, repeat) => read(absolute(report.campaigns.find(c =>
    c.suite === suite && c.seed === seed && c.id === id && c.repeat === repeat).file));
  const rows = currentPlan.suites.flatMap(suite => currentPlan.seeds.map(seed => ({ suite, seed,
    timing: assessTiming(get(suite, seed, 'control', 0), get(suite, seed, 'candidate', 0),
      get(suite, seed, 'control', 1), get(suite, seed, 'candidate', 1)) })));
  assert.deepEqual(report.timing, rows);
  assert.deepEqual(report.decision, decideCurrentPromotion(rows, report.restoration));
  return report;
}
function capture(directory, offline, serials) {
  assert(!fs.existsSync(directory), 'new capture directory required');
  assert(serials.length === 2 && serials.every(s => /^[0-9A-F]{16}$/.test(s)) && new Set(serials).size === 2);
  verifyWidthSelection(offline);
  fs.mkdirSync(directory, { recursive: true });
  const file = path.join(directory, 'results.json');
  const pico = absolute('build/picotool-usb/picotool');
  const report = { plan: currentPlan, status: 'capturing', started: new Date().toISOString(),
    offline: artifact(offline), artifacts: [artifact(pico), artifact(fileURLToPath(import.meta.url))],
    backups: [], restoration: [], campaigns: [], failed_attempts: [], errors: [] };
  const save = () => fs.writeFileSync(file, JSON.stringify(report, null, 2) + '\n');
  fs.writeFileSync(path.join(directory, 'plan.json'), JSON.stringify(currentPlan, null, 2) + '\n', { flag: 'wx' });
  const run = (command, args, log, env = process.env, timeout = 600000) => {
    const r = spawnSync(command, args, { cwd: root, env, encoding: 'utf8', timeout, maxBuffer: 64 * 1024 * 1024 });
    fs.writeFileSync(log, `${r.stdout ?? ''}${r.stderr ?? ''}${r.error ?? ''}`);
    assert.ifError(r.error); assert.equal(r.status, 0, `${log}: ${r.stderr?.slice(-3000)}`); return r.stdout;
  };
  save();
  try {
    preserveFlash(serials, directory, (args, label) => run(pico, args, path.join(directory, `${label}.log`)),
      report, save, () => {
        for (const suite of currentPlan.suites) for (const seed of currentPlan.seeds)
          for (let repeat = 0; repeat < currentPlan.repeats; repeat++) for (const profile of currentProfiles) {
            const label = `${suite}-${seed}-${repeat}-${profile.id}`;
            let output;
            for (let attempt = 0; attempt < 2; attempt++) {
              const log = path.join(directory, `${label}-${attempt}.log`);
              try {
                output = run(process.execPath, ['badge-compiler/tools/compiler-compare.mjs',
                  `--${suite === 'application' ? 'application' : 'timing'}`, `--seed=${seed}`, ...serials], log,
                { ...process.env, LLVM_BIN: process.env.LLVM_BIN ?? '/opt/homebrew/opt/llvm/bin',
                  BADGE_OPT_PASSES: profile.passes, BADGE_IR_EXPRESSION_LOWERING: profile.expression,
                  BADGE_CODEGEN_SCALAR_SELECTION: profile.scalar, BADGE_CODEGEN_REGISTER_RESIDENCY: 'global-compact',
                  BADGE_CODEGEN_FRAME_ADDRESSING: 'direct', BADGE_CODEGEN_MEMORY_SELECTION: 'word',
                  COMPILER_COMPARE_IMAGE_CACHE: relative(path.join(directory, 'image-cache')) });
                break;
              } catch (error) {
                report.failed_attempts.push({ label, attempt, log: artifact(log), error: error.message }); save();
                if (attempt || !retryableTransport(fs.readFileSync(log, 'utf8'))) throw error;
              }
            }
            const match = output.match(/Compiler comparison: ([^\n]+)/); assert(match);
            const campaign = path.join(match[1], 'results.json');
            const verified = suite === 'application' ?
              verifyApplicationCampaign(campaign, profile.scalar, profile.expression, profile.passes) : verifyCampaign(campaign);
            validateTimingCampaign(verified.result);
            report.campaigns.push({ suite, seed, repeat, id: profile.id, ...artifact(campaign), artifacts: verified.artifacts });
            save();
            console.log(`PO18 ${label}: raw results and role swaps verified`);
          }
      });
    const get = (suite, seed, id, repeat) => read(absolute(report.campaigns.find(c =>
      c.suite === suite && c.seed === seed && c.id === id && c.repeat === repeat).file));
    report.timing = currentPlan.suites.flatMap(suite => currentPlan.seeds.map(seed => ({ suite, seed,
      timing: assessTiming(get(suite, seed, 'control', 0), get(suite, seed, 'candidate', 0),
        get(suite, seed, 'control', 1), get(suite, seed, 'candidate', 1)) })));
    report.decision = decideCurrentPromotion(report.timing, report.restoration);
    report.status = 'measured-restored'; save();
    return verifyCurrentPromotion(file);
  } catch (error) { report.status = 'failed'; report.error = error.message; save(); throw error; }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, offline, ...serials] = process.argv.slice(2);
    assert(file && (mode === '--verify' && !offline || mode === '--capture' && offline && serials.length === 2),
      'Usage: current-promotion.mjs --capture NEW_DIRECTORY PO17.json SERIAL SERIAL | --verify PO18.json');
    const report = mode === '--capture' ? capture(absolute(file), absolute(offline), serials) :
      verifyCurrentPromotion(absolute(file));
    console.log(JSON.stringify(report.decision, null, 2));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
