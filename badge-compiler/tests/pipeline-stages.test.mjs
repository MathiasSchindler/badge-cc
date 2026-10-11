import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { invocation, normalizeInitialIr, preprocessedFlags, irCounts, byteDifference, verifyReport, executable } from '../tools/pipeline-stages.mjs';

const cli = fileURLToPath(new URL('../tools/pipeline-stages.mjs', import.meta.url));

test('actual compile/link argument shapes preserve configuration and object order', () => {
  assert.deepEqual(invocation(['-Oz', '-Isrc/shared', '-DTILT_MAZE_COMPARE',
    '-c', 'src/tools/tilt_maze.c', '-o', 'build/a/tilt_maze.o']),
  { source: 'src/tools/tilt_maze.c', output: 'build/a/tilt_maze.o',
    flags: ['-Oz', '-Isrc/shared', '-DTILT_MAZE_COMPARE'] });
  assert.deepEqual(invocation(['-Oz', '-nostdlib', '-Tscript.ld', '-Wl,--gc-sections',
    'start.o', 'maze.o', '-Wl,-Map=maze.map', '-o', 'maze.elf']),
  { output: 'maze.elf', script: 'script.ld', linkerFlags: ['--gc-sections'],
    objects: ['start.o', 'maze.o'] });
  assert.equal(invocation(['-Wl,-T,script.ld', 'a.o', '-o', 'out.elf']).script, 'script.ld');
  assert.equal(invocation(['-D', 'NAME=file.c', '-DOTHER=file.c',
    '-c', 'real.c', '-o', 'real.o']).source, 'real.c');
  assert.throws(() => invocation(['-c', 'a.c']), /output/);
  assert.throws(() => invocation(['-c', 'a.c', 'b.c', '-o', 'out.o']), /one compilation/);
  assert.throws(() => invocation(['a.o', '-o', 'out.elf']), /script/);
  assert.throws(() => invocation(['-Tscript.ld', '-Wl,--relax', 'a.o', '-o', 'out.elf']), /unsupported/);
});

test('normalization removes only approved source bookkeeping', () => {
  const prefix = name => `; ModuleID = '${name}'\nsource_filename = "${name}"\n`;
  const body = `target triple = "thumbv8m.main-unknown-none-eabi"\n` +
    `  call void asm sideeffect "wfi", ""(), !srcloc !4\n` +
    `!4 = !{i64 100}\n!5 = !{i64 200}\n`;
  assert.equal(normalizeInitialIr(prefix('a.i') + body),
    normalizeInitialIr(prefix('b.i') + body.replace('i64 100', 'i64 999')));
  assert(normalizeInitialIr(body).includes('!5 = !{i64 200}'));
  for (const semantic of ['load volatile i32', 'add nsw i32', 'align 4', 'noundef',
    'target-cpu', '!tbaa !5']) {
    const text = body + semantic + '\n';
    assert(normalizeInitialIr(text).includes(semantic));
  }
  assert.throws(() => normalizeInitialIr(body.replace('!{i64 100}', '!{!"not a location"}')),
    /unrecognized/);
  const multiline = body.replace('!{i64 100}', '!{i64 100, i64 101, i64 102}');
  assert.equal(normalizeInitialIr(multiline), normalizeInitialIr(multiline.replaceAll('i64 10', 'i64 99')));
  assert.notEqual(normalizeInitialIr(multiline), normalizeInitialIr(body));
});

test('preprocessed-input replay drops already applied CPP flags, not target/semantic/warning flags', () => {
  assert.deepEqual(preprocessedFlags(['-Oz', '-I', 'src/shared', '-DNAME=7', '-UOLD',
    '-D', 'OTHER=2', '-mcpu=cortex-m33', '-fno-builtin', '-Werror']),
  ['-Oz', '-mcpu=cortex-m33', '-fno-builtin', '-Werror']);
  assert.throws(() => preprocessedFlags(['-I']), /missing/);
});

test('IR counts distinguish real instructions from globals, labels and metadata', () => {
  const text = '@g = global i32 0\n' +
    'define i32 @f() {\nentry:\n  %a = alloca i32, align 4\n' +
    '  store i32 1, ptr %a\n  %b = load i32, ptr %a\n' +
    '  %c = phi i32 [ %b, %entry ], [ 0, %next ]\n' +
    '  %d = tail call i32 @h()\n  ret i32 %c\n}\n!1 = !{i32 3}\n';
  assert.deepEqual(irCounts(text), { functions: 1,
    operations: { alloca: 1, store: 1, load: 1, phi: 1, call: 1, ret: 1 } });
});

test('byte comparison reports changed and missing bytes without hiding size differences', () => {
  assert.deepEqual(byteDifference(Buffer.from([1, 2]), Buffer.from([1, 2])),
    { equal: true, lengths: [2, 2], differing_bytes: 0, first: [] });
  assert.deepEqual(byteDifference(Buffer.from([1, 2, 3]), Buffer.from([1, 4])),
    { equal: false, lengths: [3, 2], differing_bytes: 2,
      first: [{ offset: 1, a: 2, b: 4 }, { offset: 2, a: 3, b: null }] });
  assert.equal(byteDifference(Buffer.alloc(100), Buffer.alloc(100, 1)).first.length, 16);
});

test('verification rejects altered evidence, missing files and failed/partial reports', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'po00-evidence-'));
  const artifact = path.join(dir, 'artifact.txt'), reportFile = path.join(dir, 'results.json');
  t.after(() => { fs.rmSync(artifact, { force: true }); fs.rmSync(reportFile, { force: true }); fs.rmdirSync(dir); });
  fs.writeFileSync(artifact, 'immutable');
  const digest = crypto.createHash('sha256').update('immutable').digest('hex');
  const report = { schema: 1, milestone: 'PO00', status: 'qualified-stage-capture', root: dir,
    sources: [{ file: 'artifact.txt', sha256: digest }], executables: [],
    artifacts: [{ file: 'artifact.txt', sha256: digest }], modules: [{}], images: [{}] };
  const save = () => fs.writeFileSync(reportFile, JSON.stringify(report));
  save(); assert.equal(verifyReport(reportFile).status, report.status);
  report.status = 'failed'; save(); assert.throws(() => verifyReport(reportFile), /failed/);
  report.status = 'in-progress'; save(); assert.throws(() => verifyReport(reportFile), /in-progress/);
  report.status = 'qualified-stage-capture'; report.images = []; save();
  assert.throws(() => verifyReport(reportFile), /empty corpus/);
  report.images = [{}]; save(); fs.writeFileSync(artifact, 'changed');
  assert.throws(() => verifyReport(reportFile), /artifact.txt/);
  fs.unlinkSync(artifact); assert.throws(() => verifyReport(reportFile), /ENOENT/);
});

test('CLI errors and existing-directory refusal cannot overwrite prior evidence', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'po00-existing-'));
  const marker = path.join(dir, 'marker');
  fs.writeFileSync(marker, 'preserve');
  t.after(() => { fs.unlinkSync(marker); fs.rmdirSync(dir); });
  const run = args => spawnSync(process.execPath, [cli, ...args], { encoding: 'utf8' });
  assert.equal(run(['--help']).status, 0);
  assert.notEqual(run(['--capture', dir, 'unknown']).status, 0);
  const result = run(['--capture', dir, 'maze']);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /EEXIST/);
  assert.deepEqual(fs.readdirSync(dir), ['marker']);
  assert.equal(fs.readFileSync(marker, 'utf8'), 'preserve');
});

test('tool lookup preserves symlink invocation names used by LLVM multicall drivers', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'po00-tool-name-'));
  const link = path.join(dir, 'ld.lld');
  fs.symlinkSync(process.execPath, link);
  t.after(() => { fs.unlinkSync(link); fs.rmdirSync(dir); });
  assert.equal(executable(link), link);
  assert.throws(() => executable(path.join(dir, 'missing')), /not found/);
});

test('missing required tools persist failed status, never an unsupported or successful capture', t => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'po00-failed-'));
  const capture = path.join(dir, 'capture');
  t.after(() => {
    fs.unlinkSync(path.join(capture, 'results.json'));
    fs.rmdirSync(path.join(capture, 'logs'));
    fs.rmdirSync(capture);
    fs.rmdirSync(dir);
  });
  const result = spawnSync(process.execPath, [cli, '--capture', capture, 'maze'], {
    encoding: 'utf8', env: { ...process.env, LLVM_CLANG: path.join(dir, 'missing-clang') } });
  assert.notEqual(result.status, 0);
  const report = JSON.parse(fs.readFileSync(path.join(capture, 'results.json')));
  assert.equal(report.status, 'failed');
  assert.match(report.error, /executable not found/);
  assert.throws(() => verifyReport(path.join(capture, 'results.json')), /executable not found/);
  assert.deepEqual(report.commands, []);
});
