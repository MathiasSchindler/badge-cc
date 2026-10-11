import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { offline, verifyCampaign, assessTiming, frontendHelpers } from './qualify-datasheet.mjs';
import { preserveFlash, verifyApplicationCampaign } from './application-benchmark.mjs';
import { parseAssembly } from './benchmark-c06.mjs';
import { decodeElf, auditBudgets } from './boundary-evidence.mjs';
import { validateTimingCampaign } from './compiler-timing-compare.mjs';
import { request, suiteId } from './compiler-test-protocol.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const defaults = 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division';
export const promotionProfiles = [
  { id: 'control', scalar: 'thumb-layout', expression: 'legacy', passes: defaults },
  { id: 'po03', scalar: 'thumb-branch', expression: 'legacy', passes: defaults },
  { id: 'po04', scalar: 'thumb-homes', expression: 'legacy', passes: defaults },
  { id: 'po05', scalar: 'thumb-homes', expression: 'ssa', passes: defaults },
  { id: 'po06', scalar: 'thumb-homes', expression: 'ssa', passes: defaults + ',local-memory' },
  { id: 'po07', scalar: 'thumb-calls', expression: 'ssa', passes: defaults },
  { id: 'po06-po07', scalar: 'thumb-calls', expression: 'ssa', passes: defaults + ',local-memory' },
];
export const seeds = [3276377847, 123456789];
const suites = ['application', 'timing'];
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const artifact = file => ({ file: relative(file), sha256: hash(file) });
const save = (file, report) => {
  fs.writeFileSync(`${file}.tmp`, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  fs.renameSync(`${file}.tmp`, file);
};
function verifyArtifacts(artifacts) {
  for (const input of artifacts) assert.equal(hash(path.join(root, input.file)), input.sha256, input.file);
}
function command(executable, args, log, env = process.env, timeout = 600000) {
  const result = spawnSync(executable, args, { cwd: root, env, timeout,
    encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  fs.writeFileSync(log, `${result.stdout ?? ''}${result.stderr ?? ''}${result.error ?? ''}`);
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${log}: ${result.stderr?.slice(-4000)}`);
  return result.stdout;
}
const environment = profile => ({ ...process.env,
  BADGE_OPT_PASSES: profile.passes, BADGE_IR_EXPRESSION_LOWERING: profile.expression,
  BADGE_CODEGEN_FRAME_ADDRESSING: 'direct', BADGE_CODEGEN_REGISTER_RESIDENCY: 'global-compact',
  BADGE_CODEGEN_SCALAR_SELECTION: profile.scalar, BADGE_CODEGEN_MEMORY_SELECTION: 'word' });
function frameRecords(candidate, control) {
  const parsed = new Map(candidate.modules.map(module => [module.module,
    parseAssembly(fs.readFileSync(path.join(root, module.assembly), 'utf8'), module.module)]));
  const baseline = new Map(control.modules.map(module => [module.module,
    parseAssembly(fs.readFileSync(path.join(root, module.assembly), 'utf8'), module.module)]));
  const aliases = new Map(candidate.modules.map(module => {
    const baseline = control.modules.find(before => before.module === module.module);
    return [module.module, frontendHelpers(fs.readFileSync(path.join(root, baseline.file), 'utf8'),
      fs.readFileSync(path.join(root, module.file), 'utf8'))];
  }));
  assert.equal(candidate.frames.length, [...parsed.values()].reduce((count, functions) => count + functions.size, 0));
  assert.equal(new Set(candidate.frames.map(frame => `${frame.module}/${frame.function}`)).size, candidate.frames.length);
  return candidate.frames.map(frame => {
    const fn = parsed.get(frame.module).get(frame.candidate_symbol);
    const before = baseline.get(frame.module).get(frame.control_symbol);
    assert(fn && before, 'missing frame function');
    const total = fn => fn.frame_bytes === null || fn.saves_bytes === null ? null : fn.frame_bytes + fn.saves_bytes;
    assert.equal(total(fn), frame.candidate);
    assert.equal(total(before), frame.control);
    assert.deepEqual(fn.unknown, frame.unknown);
    assert.deepEqual(before.unknown, frame.unknown);
    const [controlAliases, candidateAliases] = aliases.get(frame.module);
    const calls = (fn, map) => fn.calls.map(call => ({ ...call, target: map.get(call.target) ?? call.target }));
    assert.deepEqual(calls(fn, candidateAliases), frame.calls);
    assert.deepEqual(calls(before, controlAliases), frame.calls);
    assert.equal(fn.max_outgoing_bytes, before.max_outgoing_bytes);
    return frame;
  });
}
export function verifyPromotionOffline(file, current = false) {
  const report = read(file);
  assert.equal(report.milestone, 'PO09');
  assert.equal(report.status, 'offline-qualified');
  assert.deepEqual(report.profiles, promotionProfiles);
  assert.deepEqual(report.default_profile, promotionProfiles[0]);
  assert.deepEqual(report.candidates.map(candidate => candidate.id), promotionProfiles.map(profile => profile.id));
  verifyArtifacts(report.logs);
  if (current) verifyArtifacts(Object.entries(report.hashes).map(([file, sha256]) => ({ file, sha256 })));
  for (const candidate of report.candidates) {
    assert.equal(candidate.correctness, true);
    assert.equal(candidate.modules.length, 37);
    assert.equal(new Set(candidate.modules.map(module => module.module)).size, 37);
    assert.equal(candidate.images.length, 12);
    assert.equal(new Set(candidate.images.map(image => image.image)).size, 12);
    for (const module of candidate.modules)
      verifyArtifacts([{ file: module.file, sha256: module.sha256 },
        { file: module.frontend, sha256: module.frontend_sha256 },
        { file: module.assembly, sha256: module.assembly_sha256 }]);
    frameRecords(candidate, report.candidates[0]);
    assert.deepEqual(candidate.tradeoffs.growing_frames,
      candidate.frames.filter(frame => frame.control !== null && frame.candidate > frame.control));
    assert.deepEqual(candidate.tradeoffs.growing_spill_counts,
      candidate.modules.filter(module => module.selection.unallocated_values > module.control_selection.unallocated_values));
    assert.deepEqual(candidate.tradeoffs.growing_images,
      candidate.images.filter(image => image.text > image.control_text));
    assert.equal(candidate.memory_and_frames, candidate.tradeoffs.growing_frames.length === 0);
    for (const image of [...candidate.images, candidate.instrumented_maze]) {
      verifyArtifacts([image]);
      const result = spawnSync(path.join(tools, 'badge-elf-info'), ['--metrics', path.join(root, image.file)],
        { encoding: 'utf8', timeout: 30000 });
      assert.ifError(result.error); assert.equal(result.status, 0, result.stderr);
      const measured = JSON.parse(result.stdout);
      for (const field of ['text', 'data', 'bss']) assert.equal(image[field], measured[field], `${image.file}/${field}`);
      const executable = decodeElf(fs.readFileSync(path.join(root, image.file)));
      const script = path.join(root, 'src/arch/rp2350',
        image.image === 'usb_wifi_udp.elf' ? 'memmap_report.ld' : 'memmap_flash.ld');
      auditBudgets(executable, fs.readFileSync(script, 'utf8'));
    }
  }
  return report;
}
function captureOffline(directory) {
  offline(directory, promotionProfiles, 'PO09');
  const file = path.join(directory, 'offline.json'), report = read(file);
  const log = path.join(directory, 'promotion-validation.log');
  try {
    command('make', ['-C', 'badge-compiler', 'test-pipeline-promotion', 'test-datasheet-qualification',
      'test-compare-branch', 'test-scalar-homes', 'test-ssa-expressions', 'test-local-memory',
      'test-call-transport', 'test-boundary-evidence'], log, process.env, 1800000);
    report.logs.push(artifact(log));
    save(file, report);
    verifyPromotionOffline(file, true);
  } catch (error) {
    report.status = 'failed'; report.error = error.message; save(file, report); throw error;
  }
}

export function decidePromotion(candidate, assessments, restoration) {
  assert.equal(candidate.correctness, true, 'correctness missing');
  assert.equal(restoration.length, 2, 'two restorations required');
  assert(restoration.every(item => item.verified && item.left_in_bootsel), 'unverified restoration');
  assert.equal(new Set(restoration.map(item => item.serial)).size, 2, 'duplicate restoration device');
  assert.deepEqual(assessments.map(item => [item.suite, item.seed]),
    suites.flatMap(suite => seeds.map(seed => [suite, seed])), 'incomplete independent suite/seed evidence');
  const regressions = assessments.flatMap(item => item.timing.regressions.map(workload =>
    ({ suite: item.suite, seed: item.seed, workload })));
  const improvements = suites.map(suite => {
    const observations = assessments.filter(item => item.suite === suite);
    return { suite, workloads: observations[0].timing.repeatable_improvements.filter(workload =>
      observations.every(item => item.timing.repeatable_improvements.includes(workload))) };
  });
  const tradeoffs = candidate.tradeoffs;
  const staticTradeoff = tradeoffs.growing_frames.length > 0 || tradeoffs.growing_spill_counts.length > 0 ||
    tradeoffs.growing_images.length > 0 || tradeoffs.maze_text_delta > 0;
  const eligible = !regressions.length && !staticTradeoff &&
    improvements.find(item => item.suite === 'application').workloads.length > 0;
  return { id: candidate.id, promoted: false, eligible_for_speed_review: eligible,
    decision: regressions.length ? 'reject-speed-promotion-retain-explicit' :
      staticTradeoff ? 'retain-explicit-static-tradeoff' :
        eligible ? 'retain-opt-in-pending-production-runtime-coverage' : 'retain-opt-in-no-resolved-speed-benefit',
    regressions, improvements, static_tradeoff: staticTradeoff, assessments,
    remaining_gates: ['Physical panel and live network/IMU behavior are not certified by scripted reached work.',
      'Recursive/indirect/interrupt whole-program stack bounds remain unresolved; local frame sizes are not a proof.',
      'Warm-working-state timer0 measurements do not certify cold-XIP or external-clock calibration.'] };
}
function verifyCapturedCampaign(file, profile, suite) {
  const verified = suite === 'application'
    ? verifyApplicationCampaign(file, profile.scalar, profile.expression, profile.passes)
    : verifyCampaign(file);
  const result = verified.result;
  validateTimingCampaign(result);
  assert.equal(result.toolchain.reference, undefined, 'ordinary whole-source LLVM reference required');
  for (const [key, value] of Object.entries({
    ownedScalarSelection: profile.scalar, ownedPasses: profile.passes, ownedExpressionLowering: profile.expression,
    ownedFrameAddressing: 'direct', ownedRegisterResidency: 'global-compact', ownedMemorySelection: 'word',
  })) assert.equal(result.toolchain[key], value, `unexpected ${key}`);
  if (suite === 'timing') {
    for (const [index, round] of result.rounds.entries())
      assert.deepEqual(fs.readFileSync(path.join(path.dirname(file), `round${index + 1}-request.bin`)),
        request(round.configurations[0].build, Buffer.from(round.nonce, 'hex'), suiteId));
  }
  return verified;
}
function decisions(report, qualified) {
  const get = (suite, seed, id, repeat) => {
    const campaign = report.campaigns.find(item => item.suite === suite && item.seed === seed &&
      item.id === id && item.repeat === repeat);
    assert(campaign, 'missing qualification campaign');
    return read(path.join(root, campaign.file));
  };
  return qualified.candidates.slice(1).map(candidate => decidePromotion(candidate,
    suites.flatMap(suite => seeds.map(seed => ({ suite, seed,
      timing: assessTiming(get(suite, seed, 'control', 0), get(suite, seed, candidate.id, 0),
        get(suite, seed, 'control', 1), get(suite, seed, candidate.id, 1)) }))), report.restoration));
}
export function verifyPromotionReport(file, current = false) {
  const report = read(file);
  assert.equal(report.schema, 1);
  assert.equal(report.milestone, 'PO09');
  assert.equal(report.status, 'qualified-retained-explicit');
  assert.deepEqual(report.errors, []);
  verifyArtifacts([report.offline, ...report.artifacts]);
  const qualified = verifyPromotionOffline(path.join(root, report.offline.file), current);
  assert.equal(report.backups.length, 2);
  assert.equal(report.restoration.length, 2);
  for (const backup of report.backups) {
    const restored = report.restoration.find(item => item.serial === backup.serial);
    assert(restored?.verified && restored.left_in_bootsel, 'missing verified restoration');
    assert.equal(restored.sha256, backup.sha256);
    for (const file of [backup.file, restored.reread]) {
      assert.equal(fs.statSync(path.join(root, file)).size, 16777216, 'incomplete flash image');
      verifyArtifacts([{ file, sha256: backup.sha256 }]);
    }
  }
  assert.equal(report.campaigns.length, suites.length * seeds.length * 2 * promotionProfiles.length);
  verifyCampaignInventory(report);
  if (report.continuation) {
    const prior = verifyContinuation(report.continuation, qualified);
    for (const campaign of prior.campaigns)
      assert.deepEqual(report.campaigns.find(item => item.file === campaign.file), campaign, 'continued campaign changed');
    assert.deepEqual(report.backups.map(item => [item.serial, item.sha256]),
      prior.backups.map(item => [item.serial, item.sha256]), 'original firmware changed before continuation');
  }
  for (const attempt of report.failed_attempts ?? []) {
    verifyArtifacts([attempt.log]);
    assert.equal(retryableTransport(fs.readFileSync(path.join(root, attempt.log.file), 'utf8')), true);
  }
  assert.deepEqual(report.decisions, decisions(report, qualified));
  return report;
}
function verifyCampaignInventory(report) {
  const identities = new Set(), nonces = new Set();
  let clock, toolchain;
  for (const captured of report.campaigns) {
    const identity = [captured.suite, captured.seed, captured.repeat, captured.id].join('/');
    assert(!identities.has(identity), 'duplicate campaign'); identities.add(identity);
    assert(suites.includes(captured.suite) && seeds.includes(captured.seed) && [0, 1].includes(captured.repeat));
    const profile = promotionProfiles.find(item => item.id === captured.id);
    assert(profile, 'unknown qualification profile');
    verifyArtifacts([captured, ...captured.artifacts]);
    const verified = verifyCapturedCampaign(path.join(root, captured.file), profile, captured.suite);
    assert.deepEqual(verified.artifacts, captured.artifacts);
    if (toolchain) assert.deepEqual(verified.result.toolchain.owned, toolchain, 'owned tools changed');
    else toolchain = verified.result.toolchain.owned;
    for (const round of verified.result.rounds) {
      assert.equal(round.seed, captured.seed);
      assert(!nonces.has(round.nonce), 'reused nonce'); nonces.add(round.nonce);
      assert.deepEqual(round.configurations.map(item => item.serial).sort(),
        report.backups.map(item => item.serial).sort());
      if (clock) assert.deepEqual(round.clocks, clock, 'clock drift');
      else clock = round.clocks;
    }
  }
}
export function retryableTransport(text) {
  const collector = /round[12]-collector failed: exit 1/.test(text) &&
    (/LIBUSB_ERROR_/.test(text) || /Suite summary read: -1/.test(text));
  const flash = /round[12]-(?:owned|llvm)-flash failed: spawnSync [^\n]+ ETIMEDOUT/.test(text);
  return (collector || flash) &&
    !/fingerprint|CRC|mismatch|guard failure/.test(text);
}
function verifyContinuation(continuation, qualified) {
  verifyArtifacts([continuation.report, continuation.capture_runner]);
  const prior = read(path.join(root, continuation.report.file));
  assert.equal(prior.milestone, 'PO09');
  assert.equal(prior.status, 'failed', 'continuation requires retained failed capture');
  verifyArtifacts([prior.offline]);
  const captured = verifyPromotionOffline(path.join(root, prior.offline.file));
  const allowed = new Set(['badge-compiler/tools/pipeline-promotion.mjs', 'badge-compiler/tests/pipeline-promotion.test.mjs']);
  assert.deepEqual(Object.keys(captured.hashes), Object.keys(qualified.hashes), 'source inventory changed');
  for (const [file, digest] of Object.entries(captured.hashes))
    if (!allowed.has(file)) assert.equal(qualified.hashes[file], digest, `continued source/tool drift: ${file}`);
  assert.equal(continuation.capture_runner.sha256, captured.hashes['badge-compiler/tools/pipeline-promotion.mjs']);
  verifyArtifacts(prior.artifacts.filter(item => item.file !== 'badge-compiler/tools/pipeline-promotion.mjs'));
  assert.equal(prior.backups.length, 2);
  assert.equal(prior.restoration.length, 2);
  for (const backup of prior.backups) {
    const restored = prior.restoration.find(item => item.serial === backup.serial);
    assert(restored?.verified && restored.left_in_bootsel, 'prior restoration incomplete');
    for (const file of [backup.file, restored.reread]) {
      assert.equal(fs.statSync(path.join(root, file)).size, 16777216);
      verifyArtifacts([{ file, sha256: backup.sha256 }]);
    }
  }
  verifyCampaignInventory(prior);
  return prior;
}
function measure(directory, offlineFile, serials, continuation) {
  assert.equal(serials.length, 2);
  assert(serials.every(serial => /^[0-9A-F]{16}$/.test(serial)) && new Set(serials).size === 2);
  const qualified = verifyPromotionOffline(offlineFile, true);
  const prior = continuation ? verifyContinuation(continuation, qualified) : null;
  fs.mkdirSync(directory);
  const file = path.join(directory, 'results.json');
  const pico = process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool');
  const report = { schema: 1, milestone: 'PO09', status: 'in-progress', started_at: new Date().toISOString(),
    offline: artifact(offlineFile), artifacts: [artifact(pico), artifact(fileURLToPath(import.meta.url))],
    backups: [], restoration: [], campaigns: prior?.campaigns ?? [], failed_attempts: [], errors: [],
    ...(continuation ? { continuation } : {}) };
  const persist = () => save(file, report);
  persist();
  try {
    preserveFlash(serials, directory, (args, label) =>
      command(pico, args, path.join(directory, `${label}.log`)), report, persist, () => {
      if (prior) assert.deepEqual(report.backups.map(item => [item.serial, item.sha256]),
        prior.backups.map(item => [item.serial, item.sha256]), 'original firmware changed before continuation');
      for (const suite of suites) for (const seed of seeds) for (const repeat of [0, 1])
        for (const profile of promotionProfiles) {
          if (report.campaigns.some(item => item.suite === suite && item.seed === seed &&
            item.repeat === repeat && item.id === profile.id)) continue;
          verifyArtifacts(Object.entries(qualified.hashes).map(([file, sha256]) => ({ file, sha256 })));
          const label = `${suite}-${seed}-${repeat}-${profile.id}`;
          let stdout, log;
          for (let attempt = 0; attempt < 3; attempt++) {
            log = path.join(directory, `${label}-attempt${attempt + 1}.log`);
            try {
              stdout = command(process.execPath, ['badge-compiler/tools/compiler-compare.mjs',
                `--${suite}`, `--seed=${seed}`, ...serials], log, environment(profile));
              break;
            } catch (error) {
              const text = fs.readFileSync(log, 'utf8');
              if (!retryableTransport(text)) throw error;
              report.failed_attempts.push({ suite, seed, repeat, id: profile.id, attempt: attempt + 1,
                log: artifact(log), error: error.message });
              persist();
              console.error(`PO09 ${label}: transport attempt ${attempt + 1} failed; evidence retained.`);
              if (attempt === 2) throw error;
              for (const serial of serials) {
                const recovery = command(pico, ['info', '-d', '--ser', serial],
                  path.join(directory, `${label}-attempt${attempt + 1}-${serial}-recovery.log`));
                assert(/RP2350/.test(recovery), 'badge not recovered to BOOTSEL');
              }
            }
          }
          const capturedDirectory = stdout.match(/Compiler comparison: ([^\n]+)/)?.[1];
          assert(capturedDirectory, 'missing campaign output');
          const campaignFile = path.join(capturedDirectory, 'results.json');
          const { result, artifacts } = verifyCapturedCampaign(campaignFile, profile, suite);
          assert.equal(result.matched, true);
          report.campaigns.push({ suite, seed, repeat, id: profile.id, ...artifact(campaignFile), artifacts });
          report.artifacts.push(artifact(log));
          persist();
          console.log(`PO09 ${label}: both swapped roles captured.`);
        }
    });
    report.decisions = decisions(report, qualified);
    report.status = 'qualified-retained-explicit';
    persist();
    verifyPromotionReport(file, true);
  } catch (error) {
    report.status = 'failed';
    if (!report.errors.includes(error.message)) report.errors.push(error.message);
    persist(); throw error;
  }
  console.log(`PO09 qualified and originals restored: ${file}`);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, output, input, ...serials] = process.argv.slice(2);
    if (mode === '--offline' && output && !input) captureOffline(path.resolve(output));
    else if (mode === '--measure' && output && input && serials.length === 2)
      measure(path.resolve(output), path.resolve(input), serials.map(serial => serial.toUpperCase()));
    else if (mode === '--continue' && output && input && serials.length === 4)
      measure(path.resolve(output), path.resolve(input), serials.slice(2).map(serial => serial.toUpperCase()),
        { report: artifact(path.resolve(serials[0])), capture_runner: artifact(path.resolve(serials[1])) });
    else if (['--verify', '--verify-current'].includes(mode) && output && !input) {
      verifyPromotionReport(path.resolve(output), mode === '--verify-current');
      console.log('PASS: PO09 exact profile/suite/seed decisions, artifacts and full-flash restoration.');
    } else throw new Error('Usage: pipeline-promotion.mjs --offline NEW_DIRECTORY | --measure NEW_DIRECTORY OFFLINE.json SERIAL SERIAL | --continue NEW_DIRECTORY OFFLINE.json FAILED_REPORT.json CAPTURE_RUNNER.mjs SERIAL SERIAL | --verify[-current] REPORT.json');
  } catch (error) { console.error(`pipeline-promotion: ${error.message}`); process.exitCode = 1; }
}
