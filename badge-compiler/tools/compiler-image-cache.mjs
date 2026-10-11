import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

export const fileHash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function files(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))
    .flatMap(entry => {
      assert(!entry.isSymbolicLink(), `cache identity cannot follow symlink: ${entry.name}`);
      const file = path.join(directory, entry.name);
      return entry.isDirectory() ? files(file) : entry.isFile() ? [file] : [];
    });
}
export function imageIdentity(root, configuration, tools) {
  const inputs = [path.join(root, 'Makefile'), ...files(path.join(root, 'src')),
    ...files(path.join(root, 'badge-compiler/scripts'))].filter(file =>
      /\.(c|h|S|sh|ld)$/.test(file) || path.basename(file) === 'Makefile');
  return { schema: 1, configuration,
    inputs: inputs.map(file => ({ file: path.relative(root, file), sha256: fileHash(file) })),
    tools: tools.map(file => ({ file, sha256: fileHash(file) })) };
}
export function cacheKey(identity) {
  return crypto.createHash('sha256').update(JSON.stringify(identity)).digest('hex');
}
export function verifyCachedBuild(directory, identity) {
  const manifest = JSON.parse(fs.readFileSync(path.join(directory, 'image-cache.json')));
  assert.deepEqual(manifest.identity, identity, 'cached build identity mismatch; rebuild explicitly');
  const actual = files(directory).filter(file => path.basename(file) !== 'image-cache.json')
    .map(file => path.relative(directory, file));
  assert.deepEqual(manifest.outputs.map(output => output.file), actual, 'cached output inventory mismatch');
  for (const output of manifest.outputs)
    assert.equal(fileHash(path.join(directory, output.file)), output.sha256,
      `cached artifact mismatch: ${output.file}; rebuild explicitly`);
  return manifest;
}
export function sealCachedBuild(directory, identity) {
  assert(!fs.existsSync(path.join(directory, 'image-cache.json')), 'cache already sealed');
  const outputs = files(directory).map(file => ({ file: path.relative(directory, file), sha256: fileHash(file) }));
  for (const required of ['universe26/firmware/compiler_test.elf', 'universe26/firmware/compiler_test.bin',
    'universe26/firmware/compiler_test_build.h', 'universe26/uf2/compiler_test.uf2', 'host-tools/compiler_test_host'])
    assert(outputs.some(output => output.file === required), `missing image artifact: ${required}`);
  fs.writeFileSync(path.join(directory, 'image-cache.json'), JSON.stringify({ identity, outputs }, null, 2) + '\n',
    { flag: 'wx' });
  return verifyCachedBuild(directory, identity);
}
