import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { imageIdentity, cacheKey, sealCachedBuild, verifyCachedBuild } from '../tools/compiler-image-cache.mjs';

test('image identity includes source, headers, linker scripts, tools and policy but not runtime seeds or roles', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'badge-image-identity-'));
  t.after(() => fs.rmSync(root, { recursive: true }));
  for (const dir of ['src', 'badge-compiler/scripts']) fs.mkdirSync(path.join(root, dir), { recursive: true });
  for (const file of ['Makefile', 'src/a.c', 'src/a.h', 'badge-compiler/scripts/map.ld', 'tool'])
    fs.writeFileSync(path.join(root, file), file);
  const identity = () => imageIdentity(root, { policy: 'thumb-widths', flags: '-Oz', suite: 'timing' },
    [path.join(root, 'tool')]);
  const before = cacheKey(identity());
  assert.equal(cacheKey(identity()), before);
  for (const file of ['src/a.c', 'src/a.h', 'badge-compiler/scripts/map.ld', 'tool']) {
    fs.appendFileSync(path.join(root, file), 'changed');
    assert.notEqual(cacheKey(identity()), before);
    fs.writeFileSync(path.join(root, file), file);
  }
  assert.notEqual(cacheKey({ ...identity(), configuration: { policy: 'thumb-operands' } }), before);
});
test('sealed images reject missing, extra and corrupted artifacts rather than silently rebuilding', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'badge-image-cache-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  for (const file of ['universe26/firmware/compiler_test.elf', 'universe26/firmware/compiler_test.bin',
    'universe26/firmware/compiler_test_build.h', 'universe26/uf2/compiler_test.uf2', 'host-tools/compiler_test_host']) {
    fs.mkdirSync(path.dirname(path.join(dir, file)), { recursive: true });
    fs.writeFileSync(path.join(dir, file), file);
  }
  const identity = { policy: 'candidate', sources: [], tools: [] };
  sealCachedBuild(dir, identity);
  verifyCachedBuild(dir, identity);
  assert.throws(() => verifyCachedBuild(dir, { policy: 'control' }), /identity mismatch/);
  fs.writeFileSync(path.join(dir, 'extra'), 'extra');
  assert.throws(() => verifyCachedBuild(dir, identity), /inventory mismatch/);
  fs.unlinkSync(path.join(dir, 'extra'));
  const file = path.join(dir, 'universe26/firmware/compiler_test.bin');
  fs.writeFileSync(file, 'corrupt');
  assert.throws(() => verifyCachedBuild(dir, identity), /artifact mismatch/);
  fs.unlinkSync(file);
  assert.throws(() => verifyCachedBuild(dir, identity), /inventory mismatch/);
});
