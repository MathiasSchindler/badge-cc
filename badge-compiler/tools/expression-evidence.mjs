import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { irCounts, normalizeInitialIr } from './pipeline-stages.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file);
function run(command, args) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 60000,
    maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status, 0, `${command}: ${r.stderr}`);
}
const signatures = text => [...text.matchAll(/^define [^\n]+$/gm)].map(m => m[0]
  .replace(/(?:dso_local |local_unnamed_addr |unnamed_addr )/g, '')
  .replace(/%[\w.]+/g, '%arg').replace(/@"([^"]+)"/g, '@$1')
  .replace(/ #[0-9]+.*$/, ' {').trim());
export function expressionCounts(text) {
  return { ...irCounts(text),
    boolean_extensions: [...text.matchAll(/ = zext i1 /g)].length,
    volatile_loads: [...text.matchAll(/ = load volatile /g)].length,
    volatile_stores: [...text.matchAll(/^  store volatile /gm)].length };
}
export function verifyExpressionEvidence(file) {
  const report = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert.equal(report.status, 'qualified');
  assert.equal(report.modules.length, 37);
  for (const a of report.artifacts) assert.equal(hash(path.join(root, a.file)), a.sha256, a.file);
  for (const m of report.modules) for (const role of ['legacy', 'ssa', 'clang'])
    assert.deepEqual(expressionCounts(fs.readFileSync(path.join(root, m[role].file), 'utf8')), m[role].counts);
  assert(report.modules.some(m => m.ssa.counts.operations.alloca < m.legacy.counts.operations.alloca));
  assert(report.modules.some(m => m.ssa.counts.boolean_extensions < m.legacy.counts.boolean_extensions));
  return report;
}
function capture(input, directory) {
  const corpus = JSON.parse(fs.readFileSync(input, 'utf8'));
  assert.equal(corpus.status, 'offline-qualified'); assert.equal(corpus.milestone, 'PO05');
  fs.mkdirSync(directory);
  const report = { schema: 1, milestone: 'PO05', status: 'in-progress', modules: [], artifacts: [],
    limitations: ['Clang ASTs and aggregate ABI representations are references, not matching-schema requirements.',
      'Counts are initial/static IR, not a hardware performance claim. Both modes use the same owned semantic analysis.',
      'Float IR verification does not qualify soft-float owned runtime support.'] };
  const artifact = file => report.artifacts.push({ file: relative(file), sha256: hash(file) });
  artifact(input);
  for (const name of ['badge-ir', 'badge-sema']) artifact(path.join(tools, name));
  for (const module of corpus.candidates[0].modules) {
    const dir = path.dirname(path.join(root, module.frontend));
    const source = path.join(dir, 'preprocessed.i'), record = { module: module.module, source: relative(source) };
    artifact(source);
    const defaultFile = path.join(directory, `${module.module}.default.ll`);
    run(path.join(tools, 'badge-ir'), [source, '-o', defaultFile]);
    for (const mode of ['legacy', 'ssa']) {
      const file = path.join(directory, `${module.module}.${mode}.ll`);
      run(path.join(tools, 'badge-ir'), [`--expression-lowering=${mode}`, source, '-o', file]);
      run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file]);
      const text = fs.readFileSync(file, 'utf8');
      record[mode] = { file: relative(file), counts: expressionCounts(text), signatures: signatures(text) };
      artifact(file);
    }
    assert.deepEqual(record.legacy.signatures, record.ssa.signatures, module.module);
    assert.equal(normalizeInitialIr(fs.readFileSync(defaultFile, 'utf8')),
      normalizeInitialIr(fs.readFileSync(path.join(root, record.legacy.file), 'utf8')));
    assert.equal(normalizeInitialIr(fs.readFileSync(defaultFile, 'utf8')),
      normalizeInitialIr(fs.readFileSync(path.join(root, module.frontend), 'utf8')));
    artifact(defaultFile);
    const semantic = path.join(directory, `${module.module}.sema.json`);
    run(path.join(tools, 'badge-sema'), [source, '-o', semantic]); artifact(semantic);
    const clangFile = path.join(directory, `${module.module}.clang.ll`);
    run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
      '-ffreestanding', '-fno-builtin', '-std=c11', '-x', 'c', '-S', '-emit-llvm', '-O0',
      source, '-o', clangFile]);
    run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', clangFile]);
    record.clang = { file: relative(clangFile), counts: expressionCounts(fs.readFileSync(clangFile, 'utf8')),
      signatures: signatures(fs.readFileSync(clangFile, 'utf8')) }; artifact(clangFile);
    record.optimized = Object.fromEntries(corpus.candidates.map(c => {
      const m = c.modules.find(m => m.module === module.module); artifact(path.join(root, m.file));
      return [c.id, expressionCounts(fs.readFileSync(path.join(root, m.file), 'utf8'))];
    }));
    report.modules.push(record);
  }
  report.status = 'qualified';
  const file = path.join(directory, 'results.json');
  fs.writeFileSync(file, JSON.stringify(report, null, 2) + '\n');
  verifyExpressionEvidence(file);
  console.log(`PO05: 37 actual preprocessed modules; same owned signatures, default replay, initial/optimized counts and LLVM reference captured: ${file}`);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, input, directory] = process.argv.slice(2);
    if (mode === '--verify' && input && !directory) verifyExpressionEvidence(path.resolve(input));
    else {
      assert(mode === '--capture' && input && directory,
        'Usage: expression-evidence.mjs --capture OFFLINE.json DIRECTORY | --verify REPORT.json');
      capture(path.resolve(input), path.resolve(directory));
    }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
