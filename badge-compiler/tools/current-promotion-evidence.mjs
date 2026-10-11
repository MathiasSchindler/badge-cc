import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { currentProfiles, verifyCurrentPromotion } from './current-promotion.mjs';
import { cacheKey, verifyCachedBuild } from './compiler-image-cache.mjs';
import { verifyLoadedImage } from './cross-function-evidence.mjs';
import { parseAssembly } from './benchmark-c06.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const read = file => JSON.parse(fs.readFileSync(absolute(file)));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(absolute(file))).digest('hex');
export function verifyPromotionCache(config, suite, profile, verifyOutputs = true) {
  assert(['llvm', 'owned'].includes(config.name));
  const cache = read(config.cache.manifest);
  assert.equal(config.cache.key, cacheKey(cache.identity), 'cache key does not identify manifest');
  assert.equal(path.basename(path.dirname(config.cache.manifest)), config.cache.key);
  const policy = cache.identity.configuration;
  assert.equal(policy.compiler, config.name);
  assert.equal(policy.application, suite === 'application');
  assert.equal(policy.timing, true);
  assert.deepEqual(policy.policy, config.name === 'owned' ? {
    passes: profile.passes, frame: 'direct', registers: 'global-compact',
    scalar: profile.scalar, memory: 'word', expression: profile.expression,
  } : null);
  assert.equal(config.sha256, cache.outputs.find(o =>
    o.file === 'universe26/uf2/compiler_test.uf2')?.sha256, 'image is not the declared cached output');
  if (verifyOutputs)
    verifyCachedBuild(path.dirname(absolute(config.cache.manifest)), cache.identity);
  return cache;
}
export function verifyOpticalPromotion(report, hardware) {
  assert.equal(report.status, 'observed-restored');
  assert.deepEqual(report.errors, []);
  assert.equal(report.observations.length, 2);
  assert.equal(report.backups.length, 2);
  assert.equal(report.restoration.length, 2);
  assert.deepEqual(report.images.map(i => i.id), ['control', 'candidate']);
  for (const image of report.images) assert.equal(hash(image.file), image.sha256);
  for (const backup of report.backups) {
    assert.equal(backup.sha256, hardware.backups.find(b => b.serial === backup.serial)?.sha256);
    const restored = report.restoration.find(b => b.serial === backup.serial);
    assert(restored?.verified && restored.left_in_bootsel);
    for (const file of [backup.file, restored.reread]) {
      assert.equal(fs.statSync(absolute(file)).size, 16777216);
      assert.equal(hash(file), backup.sha256);
    }
  }
  for (const [phase, observation] of report.observations.entries()) {
    assert.equal(observation.phase, phase);
    for (const field of ['control_ok', 'candidate_ok', 'cold_boot_ok', 'returned_to_bootsel'])
      assert.equal(observation[field], true, `missing optical evidence: ${field}`);
    assert.equal(observation.assignments.length, 2);
    for (const [index, assignment] of observation.assignments.entries()) {
      assert.equal(assignment.serial, report.backups[index].serial);
      assert.deepEqual(assignment.image, report.images[(index + phase) % 2]);
    }
  }
}
export function verifyPromotionEvidence(file) {
  const report = read(file);
  assert.equal(report.milestone, 'PO18');
  assert.equal(report.status, 'qualified-promoted');
  assert.equal(report.promoted, true);
  assert.deepEqual(report.defaults, { expression: 'ssa', passes: currentProfiles[1].passes,
    frame: 'direct', registers: 'global-compact', scalar: 'thumb-widths', memory: 'word' });
  for (const artifact of report.artifacts) assert.equal(hash(artifact.file), artifact.sha256, artifact.file);
  assert(report.commands.every(c => c.status === 0));
  assert.equal(hash(report.optimizer_default.output), hash(report.optimizer_default.expected));
  const hardware = verifyCurrentPromotion(absolute(report.hardware));
  assert.equal(hardware.decision.eligible, true);
  const seen = new Set();
  let builds = 0, reuses = 0;
  for (const campaign of hardware.campaigns) {
    const result = read(campaign.file);
    for (const field of ['ownedFrameAddressing', 'ownedRegisterResidency', 'ownedMemorySelection'])
      assert.equal(result.toolchain[field], {
        ownedFrameAddressing: 'direct', ownedRegisterResidency: 'global-compact', ownedMemorySelection: 'word',
      }[field]);
    for (const [index, round] of result.rounds.entries()) {
      for (const config of round.configurations) {
        verifyPromotionCache(config, campaign.suite, currentProfiles.find(p => p.id === campaign.id),
          !seen.has(config.cache.key));
        seen.add(config.cache.key);
        if (index === 0) {
          if (config.cache.reused) reuses++; else builds++;
        }
      }
    }
  }
  for (const attempt of hardware.failed_attempts)
    assert.equal(hash(attempt.log.file), attempt.log.sha256);
  assert.deepEqual(report.hardware_cache, { images: seen.size, builds, reuses });
  assert.equal(seen.size, 6);
  assert.equal(builds, 6);
  verifyOpticalPromotion(read(report.optical), hardware);
  const baseline = read(hardware.offline.file);
  assert.equal(report.modules.length, baseline.modules.length);
  const names = new Set();
  for (const module of report.modules) {
    assert(!names.has(module.module)); names.add(module.module);
    const old = baseline.modules.find(m => m.module === module.module);
    assert(old);
    assert.equal(hash(module.assembly), hash(module.expected_assembly), 'default emission drift');
    assert.equal(hash(module.object), hash(module.expected_object), 'reused object drift');
    const before = parseAssembly(fs.readFileSync(absolute(old.assembly), 'utf8'), 'PO18-before');
    const after = parseAssembly(fs.readFileSync(absolute(module.assembly), 'utf8'), 'PO18-after');
    for (const [name, frame] of after) {
      const original = before.get(name);
      assert(original, `unexpected production function: ${name}`);
      for (const field of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
        assert(Number.isInteger(frame[field]) && frame[field] <= original[field], `${name}: ${field} grew`);
      assert(!frame.unknown.includes('unverified_frame_balance'));
    }
    if (!module.source_changed) {
      assert.equal(module.ir, old.ir);
      assert.equal(hash(module.assembly), hash(old.assembly));
      assert.equal(hash(module.object), hash(old.object));
    } else assert.equal(module.module, 'display.o.owned');
  }
  assert.equal(report.images.length, baseline.images.length);
  for (const image of report.images) {
    const old = baseline.images.find(i => i.image === image.image);
    assert(old);
    assert.equal(image.loaded_bytes, fs.statSync(absolute(image.binary)).size);
    assert(image.loaded_bytes <= old.after_bytes, `production image grew: ${image.image}`);
    const metrics = read(image.metrics), before = read(old.metrics);
    assert.equal(metrics.data, before.data);
    assert.equal(metrics.bss, before.bss);
    verifyLoadedImage(absolute(image.elf), absolute(image.binary), absolute(image.script));
  }
  for (const image of report.red_square) {
    assert.equal(image.loaded_bytes, fs.statSync(absolute(image.binary)).size);
    verifyLoadedImage(absolute(image.elf), absolute(image.binary), absolute(image.script));
    assert.equal(read(image.metrics).data, 0);
    assert.equal(read(image.metrics).bss, 0);
    assert.equal(image.loaded_bytes, Object.values(image.categories).reduce((sum, bytes) => sum + bytes, 0));
  }
  return report;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, ...extra] = process.argv.slice(2);
    assert(mode === '--verify' && file && !extra.length,
      'Usage: current-promotion-evidence.mjs --verify PO18_FINAL.json');
    const report = verifyPromotionEvidence(file);
    console.log(`PASS: PO18 promoted defaults, ${report.modules.length} modules, ${report.images.length} images, cached hardware and optical restoration`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
