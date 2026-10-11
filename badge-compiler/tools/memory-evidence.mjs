import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { irCounts, normalizeInitialIr } from './pipeline-stages.mjs';
import { backendFlags } from './backend-attribution.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file);
const defaults = 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division';
function run(command, args, log) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000,
    maxBuffer: 32 * 1024 * 1024 });
  if (log) fs.writeFileSync(log, `${r.stdout ?? ''}${r.stderr ?? ''}${r.error ?? ''}`);
  assert.ifError(r.error); assert.equal(r.status, 0, `${command}: ${r.stderr}`);
}
export function emittedMemory(text) {
  const opcodes = [...text.matchAll(/^([a-z][a-z0-9]*(?:\.[a-z0-9]+)?)\s/gm)].map(m => m[1]);
  return { instructions: opcodes.length, loads: opcodes.filter(x => x.startsWith('ldr')).length,
    stores: opcodes.filter(x => x.startsWith('str')).length,
    calls: opcodes.filter(x => /^blx?(?:\.[a-z]+)?$/.test(x)).length };
}
function corpusIr(text, module) {
  text = normalizeInitialIr(text);
  if (module === 'usb_wifi_udp.o.owned') {
    const pattern = /(call void @"(?:wifi_link_report_encode|wifi_udp_report_encode|live_usb_control_init)"\(ptr [^,\n]+, i32 )\d+/g;
    assert.equal([...text.matchAll(pattern)].length, 3);
    text = text.replace(pattern, '$1BUILD_ID');
  }
  return text;
}
export function verifyMemoryEvidence(file) {
  const r = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert.equal(r.status, 'qualified'); assert.equal(r.modules.length, 37);
  for (const a of r.artifacts) assert.equal(hash(path.join(root, a.file)), a.sha256, a.file);
  for (const m of r.modules) {
    assert.deepEqual(m.steps.map(step => step.pass), r.passes.candidate.split(','));
    for (const step of m.steps)
      assert.deepEqual(irCounts(fs.readFileSync(path.join(root, step.file), 'utf8')), step.counts);
    assert.equal(m.steps.at(-1).sha256, m.candidate.sha256);
    for (const role of ['control', 'candidate', 'llvm']) {
      const p = m[role]; assert.deepEqual(irCounts(fs.readFileSync(path.join(root, p.ir), 'utf8')), p.counts);
      if (role !== 'llvm') assert.deepEqual(emittedMemory(fs.readFileSync(path.join(root, p.assembly), 'utf8')), p.emitted);
    }
  }
  assert(r.modules.some(m => m.candidate.counts.operations.load < m.control.counts.operations.load));
  return r;
}
function capture(input, directory) {
  const corpus = JSON.parse(fs.readFileSync(input, 'utf8'));
  assert.equal(corpus.status, 'offline-qualified'); assert.equal(corpus.milestone, 'PO06');
  fs.mkdirSync(directory);
  const r = { schema: 1, milestone: 'PO06', status: 'in-progress',
    passes: { control: defaults, candidate: defaults + ',local-memory' },
    modules: [], artifacts: [], limitations: [
      'All optimizer routes consume identical owned SSA frontend input; LLVM is not the semantic specification.',
      'Static IR/emitted counts are not retired instructions or hardware cycles.',
      'Memory forwarding has no alias analysis: exact pointer identity only, all writes invalidate the previous entry.',
      'Ordinary LLVM C is a separate reference; hardware optimizer crossover uses the same initial owned IR.',
    ] };
  const artifact = file => r.artifacts.push({ file: relative(file), sha256: hash(file) });
  artifact(input);
  for (const name of ['badge-opt', 'badge-codegen', 'badge-asm']) artifact(path.join(tools, name));
  for (const module of corpus.candidates[0].modules) {
    const original = path.join(root, module.frontend);
    const frontend = path.join(directory, `${module.module}.initial.ll`);
    fs.writeFileSync(frontend, fs.readFileSync(original, 'utf8').replace(/^source_filename = .*\n/m, ''));
    artifact(original); artifact(frontend);
    const m = { module: module.module, frontend: relative(frontend), frontend_sha256: hash(frontend) };
    m.steps = r.passes.candidate.split(',').map((pass, index, names) => {
      const file = path.join(directory, `${module.module}.step${index}.ll`);
      run(path.join(tools, 'badge-opt'), [`--passes=${names.slice(0, index + 1).join(',')}`,
        frontend, '-o', file]);
      run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file]);
      artifact(file);
      return { pass, file: relative(file), sha256: hash(file), counts: irCounts(fs.readFileSync(file, 'utf8')) };
    });
    for (const role of ['control', 'candidate', 'llvm']) {
      const file = path.join(directory, `${module.module}.${role}.ll`);
      if (role === 'llvm')
        run(process.env.LLVM_OPT ?? 'opt', ['-S', '-passes=default<Oz>', frontend, '-o', file]);
      else run(path.join(tools, 'badge-opt'), [`--passes=${r.passes[role]}`, frontend, '-o', file]);
      run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file]);
      const record = { ir: relative(file), sha256: hash(file), counts: irCounts(fs.readFileSync(file, 'utf8')) };
      artifact(file);
      const object = path.join(directory, `${module.module}.${role}.o`);
      if (role === 'llvm') {
        run(process.env.LLVM_CLANG ?? 'clang', [...backendFlags, '-c', file, '-o', object]);
      } else {
        const assembly = path.join(directory, `${module.module}.${role}.s`);
        run(path.join(tools, 'badge-codegen'), ['--scalar-selection=thumb-homes', file, '-o', assembly]);
        run(path.join(tools, 'badge-asm'), [assembly, '-o', object]);
        record.assembly = relative(assembly); record.emitted = emittedMemory(fs.readFileSync(assembly, 'utf8'));
        artifact(assembly);
        const old = corpus.candidates[role === 'control' ? 0 : 1].modules.find(x => x.module === module.module);
        assert.equal(corpusIr(fs.readFileSync(file, 'utf8'), module.module),
          corpusIr(fs.readFileSync(path.join(root, old.file), 'utf8'), module.module), `${module.module}: optimizer replay`);
      }
      record.object = relative(object); record.object_bytes = fs.statSync(object).size; artifact(object);
      m[role] = record;
    }
    r.modules.push(m);
  }
  r.status = 'qualified';
  const file = path.join(directory, 'results.json');
  fs.writeFileSync(file, JSON.stringify(r, null, 2) + '\n');
  verifyMemoryEvidence(file);
  console.log(`PO06: 37 identical-input optimizer crossovers with verified IR and emitted objects: ${file}`);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, input, directory] = process.argv.slice(2);
    if (mode === '--verify' && input && !directory) verifyMemoryEvidence(path.resolve(input));
    else {
      assert(mode === '--capture' && input && directory,
        'Usage: memory-evidence.mjs --capture OFFLINE.json NEW_DIRECTORY | --verify REPORT.json');
      capture(path.resolve(input), path.resolve(directory));
    }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
