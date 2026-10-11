import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { currentProfiles } from '../tools/current-promotion.mjs';
import { cacheKey, fileHash, sealCachedBuild } from '../tools/compiler-image-cache.mjs';
import { verifyOpticalPromotion, verifyPromotionCache } from '../tools/current-promotion-evidence.mjs';

function fixture(t, policy) {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'po18-policy-'));
  t.after(() => fs.rmSync(base, { recursive: true }));
  const identity = { schema: 1, configuration: {
    compiler: 'owned', timing: true, application: true, policy,
  }, inputs: [], tools: [] };
  const key = cacheKey(identity), directory = path.join(base, key);
  for (const name of ['universe26/firmware/compiler_test.elf', 'universe26/firmware/compiler_test.bin',
    'universe26/firmware/compiler_test_build.h', 'universe26/uf2/compiler_test.uf2',
    'host-tools/compiler_test_host']) {
    fs.mkdirSync(path.dirname(path.join(directory, name)), { recursive: true });
    fs.writeFileSync(path.join(directory, name), name);
  }
  sealCachedBuild(directory, identity);
  return { name: 'owned', sha256: fileHash(path.join(directory, 'universe26/uf2/compiler_test.uf2')),
    cache: { key, manifest: path.join(directory, 'image-cache.json') } };
}
const profile = currentProfiles[1];
const policy = { passes: profile.passes, frame: 'direct', registers: 'global-compact',
  scalar: profile.scalar, memory: 'word', expression: profile.expression };
test('promotion cache requires the exact declared backend, optimizer and suite', t => {
  const config = fixture(t, policy);
  verifyPromotionCache(config, 'application', profile);
  assert.throws(() => verifyPromotionCache(config, 'timing', profile));
  for (const [field, value] of [['frame', 'materialized'], ['registers', 'stack'],
    ['memory', 'byte'], ['scalar', 'thumb-calls'], ['passes', 'constant-fold']])
    assert.throws(() => verifyPromotionCache(fixture(t, { ...policy, [field]: value }), 'application', profile));
});
test('a valid sealed manifest cannot claim a different cache key', t => {
  const config = fixture(t, policy);
  config.cache.key = '0'.repeat(64);
  assert.throws(() => verifyPromotionCache(config, 'application', profile), /cache key/);
});
test('cache provenance rejects a mismatched image even when output hashing is deduplicated', t => {
  const config = fixture(t, policy);
  config.sha256 = '0'.repeat(64);
  assert.throws(() => verifyPromotionCache(config, 'application', profile, false), /declared cached output/);
  assert.throws(() => verifyPromotionCache(fixture(t, policy), 'application',
    currentProfiles[0], false));
});
test('optical promotion rejects missing role swaps and incomplete restoration before accessing images', () => {
  assert.throws(() => verifyOpticalPromotion({ status: 'awaiting-observation-1' }, {}));
  assert.throws(() => verifyOpticalPromotion({
    status: 'observed-restored', errors: [], observations: [], backups: [], restoration: [],
  }, {}));
  assert.throws(() => verifyOpticalPromotion({
    status: 'observed-restored', errors: [], observations: [{}, {}],
    backups: [{}, {}], restoration: [{}],
  }, {}));
});
