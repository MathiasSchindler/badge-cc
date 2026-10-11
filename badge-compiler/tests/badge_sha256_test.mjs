import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const dir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = path.join(dir, 'build/badge-sha256-sanitize');
function fixture(t) {
  const folder = fs.mkdtempSync(path.join(dir, 'build/sha256-test-'));
  t.after(() => fs.rmSync(folder, { recursive: true }));
  return folder;
}
function run(args, options = {}) {
  const result = spawnSync(tool, args, { encoding: 'utf8', timeout: 15000, ...options });
  assert.ifError(result.error);
  return result;
}
function manifest(file, bytes) {
  const escaped = /[\\\n]/.test(file);
  return `${escaped ? '\\' : ''}${crypto.createHash('sha256').update(bytes).digest('hex')}  ` +
    file.replace(/\\/g, '\\\\').replace(/\n/g, '\\n') + '\n';
}

test('SHA-256 known vectors, padding boundaries and streaming reads', t => {
  const folder = fixture(t);
  for (const bytes of [
    Buffer.alloc(0), Buffer.from('abc'),
    Buffer.from('abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
    Buffer.alloc(1000000, 'a'),
    ...[1, 55, 56, 63, 64, 65, 127, 128, 65535, 65536, 65537, 131073]
      .map(size => Buffer.from(Array.from({ length: size }, (_, i) => i % 251))),
  ]) {
    const file = path.join(folder, 'input.bin');
    fs.writeFileSync(file, bytes);
    const result = run([file]);
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stderr, '');
    assert.equal(result.stdout, manifest(file, bytes));
  }
});

test('multiple manifests preserve ordering, spaces and shasum filename escaping', t => {
  const folder = fixture(t), bytes = Buffer.from([0, 128, 255, 10]);
  const files = ['space name', 'back\\slash', 'new\nline', 'car\rriage', '-option']
    .map(name => path.join(folder, name));
  for (const file of files) fs.writeFileSync(file, bytes);
  const result = run(files);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout, files.map(file => manifest(file, bytes)).join(''));
});

test('invalid inputs fail explicitly without a digest', t => {
  const folder = fixture(t);
  const fifo = path.join(folder, 'pipe');
  const created = spawnSync('mkfifo', [fifo], { encoding: 'utf8', timeout: 15000 });
  assert.ifError(created.error);
  assert.equal(created.status, 0, created.stderr);
  for (const input of [folder, path.join(folder, 'missing'), fifo]) {
    const result = run([input]);
    assert.equal(result.status, 1);
    assert.match(result.stderr, /badge-sha256: (input must be a regular file|cannot open)/);
    assert.equal(result.stdout, '');
  }
  const result = run([]);
  assert.equal(result.status, 2);
  assert.match(result.stderr, /usage: badge-sha256/);
});

test('stdout write errors are failures', t => {
  const folder = fixture(t), file = path.join(folder, 'input');
  fs.writeFileSync(file, 'abc');
  const fd = fs.openSync(path.join(folder, 'read-only-output'), 'w');
  fs.closeSync(fd);
  const readOnly = fs.openSync(path.join(folder, 'read-only-output'), 'r');
  try {
    const result = run([file], { stdio: ['ignore', readOnly, 'pipe'] });
    assert.equal(result.status, 1);
    assert.match(result.stderr, /badge-sha256: failed to write manifest/);
  } finally { fs.closeSync(readOnly); }
});
