import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { traceFunctions } from './backend-attribution.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
function run(name, args) {
  const result = spawnSync(path.join(tools, name), args,
    { cwd: root, encoding: 'utf8', timeout: 60000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
}
export function scalarHomeChanges(before, after) {
  assert.deepEqual(before.map(f => f.name), after.map(f => f.name));
  const instructions = f => [...f.entry, ...f.operations.flatMap(o => o.assembly)];
  const count = (f, expression) => instructions(f).filter(s => expression.test(s)).length;
  return before.map((a, n) => {
    const b = after[n];
    assert.deepEqual(b.registers, a.registers, `${a.name}: register assignments`);
    assert.equal(b.saved_bytes, a.saved_bytes, `${a.name}: callee saves`);
    assert(b.frame_bytes <= a.frame_bytes, `${a.name}: frame growth`);
    assert(b.phi.size <= a.phi.size, `${a.name}: PHI buffer growth`);
    assert.deepEqual(b.slots.map(s => [s.name, s.size, s.object_size, s.parameter]),
      a.slots.map(s => [s.name, s.size, s.object_size, s.parameter]));
    assert.deepEqual(b.operations.map(o => o.ir), a.operations.map(o => o.ir));
    const calls = f => instructions(f).filter(s => /^bl(?:x)? /.test(s));
    assert.deepEqual(calls(b), calls(a));
    for (let i = 0; i < a.operations.length; i++) {
      const x = a.operations[i], y = b.operations[i];
      if (/^(?:%\S+ = )?(?:load|store) volatile /.test(x.ir)) {
        const memory = o => o.assembly.filter(s => /^(?:ldr|str)[a-z.]*/.test(s));
        // Address/home offsets can move; the actual volatile access width/count must not.
        assert.deepEqual(memory(y).map(s => s.split(' ')[0]),
          memory(x).map(s => s.split(' ')[0]), `${a.name}: volatile accesses`);
      }
    }
    const metrics = f => ({
      frame_bytes: f.frame_bytes, phi_buffer_bytes: f.phi.size,
      saved_register_bytes: f.saved_bytes,
      instructions: instructions(f).length,
      loads: count(f, /^ldr/), stores: count(f, /^str/),
      omitted_scalar_homes: f.slots.filter(s => s.offset === 0xffffffff).length,
      phi_buffer_loads: f.stack_categories['phi-transfer-buffer:load'] ?? 0,
      phi_buffer_stores: f.stack_categories['phi-transfer-buffer:store'] ?? 0,
    });
    const x = metrics(a), y = metrics(b);
    assert(y.loads <= x.loads && y.stores <= x.stores, `${a.name}: memory traffic growth`);
    return { function: a.name, control: x, candidate: y };
  });
}
export function verifyScalarHomeEvidence(file) {
  const report = read(file);
  assert.equal(report.status, 'qualified');
  assert.equal(report.modules.length, 11);
  for (const artifact of report.artifacts)
    assert.equal(hash(path.join(root, artifact.file)), artifact.sha256, artifact.file);
  for (const module of report.modules) {
    const functions = ['control', 'candidate'].map(role => traceFunctions(
      fs.readFileSync(path.join(root, module[role].assembly), 'utf8'),
      fs.readFileSync(path.join(root, module.ir), 'utf8')));
    assert.deepEqual(scalarHomeChanges(...functions), module.functions);
    assert.equal(module.control.sha256, module.po03_object_sha256);
  }
  return report;
}
function capture(directory, inputReport) {
  const old = read(inputReport);
  assert.equal(old.status, 'qualified');
  assert.equal(old.milestone, 'PO03');
  fs.mkdirSync(directory);
  const report = { schema: 1, milestone: 'PO04', status: 'in-progress',
    input: { file: relative(inputReport), sha256: hash(inputReport) },
    modules: [], artifacts: [], limitations: [
      'Same historical owned optimized IR and PO03 baseline, not another IR optimization campaign.',
      'Static emitted traffic and frame reservations are not retired instructions or stack high-water marks.',
      'Unallocated-value metadata is not dynamic eviction traffic.',
      'Real hardware timing is independently captured with ordinary whole-source Clang references.',
    ] };
  const artifact = file => report.artifacts.push({ file: relative(file), sha256: hash(file) });
  artifact(inputReport);
  for (const module of old.modules) {
    const input = path.join(root, module.ir);
    assert.equal(hash(input), module.ir_sha256, 'historical IR changed');
    artifact(input);
    const record = { module: module.module, ir: relative(input), ir_sha256: hash(input),
      po03_object_sha256: module.candidate.sha256 };
    const functions = [];
    for (const role of ['control', 'candidate']) {
      const stem = path.join(directory, `${module.module}.${role}`);
      const assembly = `${stem}.s`, object = `${stem}.o`;
      const normal = `${stem}.normal.s`, replay = `${stem}.normal.o`;
      const flags = [`--scalar-selection=${role === 'control' ? 'thumb-branch' : 'thumb-homes'}`];
      run('badge-codegen-attribution', [...flags, input, '-o', assembly]);
      run('badge-asm', [assembly, '-o', object]);
      run('badge-codegen', [...flags, input, '-o', normal]);
      run('badge-asm', [normal, '-o', replay]);
      assert.equal(hash(replay), hash(object), 'trace changes object');
      if (role === 'control') assert.equal(hash(object), module.candidate.sha256,
        `${module.module}: PO03 baseline changed`);
      functions.push(traceFunctions(fs.readFileSync(assembly, 'utf8'), fs.readFileSync(input, 'utf8')));
      record[role] = { assembly: relative(assembly), object: relative(object), sha256: hash(object) };
      for (const file of [assembly, object, normal, replay]) artifact(file);
    }
    record.functions = scalarHomeChanges(...functions);
    report.modules.push(record);
  }
  for (const name of ['badge-codegen', 'badge-codegen-attribution', 'badge-asm'])
    artifact(path.join(tools, name));
  artifact(fileURLToPath(import.meta.url));
  artifact(path.join(root, 'badge-compiler/tools/backend-attribution.mjs'));
  report.status = 'qualified';
  const file = path.join(directory, 'results.json');
  fs.writeFileSync(file, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  verifyScalarHomeEvidence(file);
  const functions = report.modules.flatMap(m => m.functions);
  const sum = (role, key) => functions.reduce((n, f) => n + f[role][key], 0);
  console.log(JSON.stringify({ file: relative(file), baseline_objects_identical: report.modules.length,
    functions: functions.length, omitted_homes: sum('candidate', 'omitted_scalar_homes'),
    removed_loads: sum('control', 'loads') - sum('candidate', 'loads'),
    removed_stores: sum('control', 'stores') - sum('candidate', 'stores'),
    removed_frame_bytes: sum('control', 'frame_bytes') - sum('candidate', 'frame_bytes') }));
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, output, input, ...extra] = process.argv.slice(2);
    assert(extra.length === 0 && (mode === '--capture' && output && input ||
      mode === '--verify' && output && !input),
    'Usage: scalar-home-evidence.mjs --capture NEW_DIRECTORY PO03.json | --verify REPORT.json');
    if (mode === '--capture') capture(path.resolve(output), path.resolve(input));
    else {
      verifyScalarHomeEvidence(path.resolve(output));
      console.log('PASS: PO03 baseline objects, trace equality and exact scalar/PHI/frame traffic.');
    }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
