import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { traceFunctions } from './backend-attribution.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const relative = file => path.relative(root, file);
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
function run(name, args) {
  const result = spawnSync(path.join(tools, name), args, {
    cwd: root, encoding: 'utf8', timeout: 60000, maxBuffer: 16 * 1024 * 1024,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stderr);
}
const isCall = operation => /^(?:%\S+ = )?(?:tail |notail )?call /.test(operation.ir);
function metrics(fn) {
  const assembly = [...fn.entry, ...fn.operations.flatMap(o => o.assembly)];
  const callOperations = fn.operations.filter(isCall);
  const callSites = callOperations.map(operation => {
    const index = operation.assembly.findIndex(line => /^bl(?:x)? /.test(line));
    return { ir: operation.ir, target: index < 0 ? null : operation.assembly[index],
      argument_instructions: index < 0 ? 0 : index,
      result_and_stack_restore_instructions: index < 0 ? 0 : operation.assembly.length - index - 1,
      intrinsic_or_assembly_instructions: index < 0 ? operation.assembly.length : 0 };
  });
  const count = expression => assembly.filter(line => expression.test(line)).length;
  return { frame_bytes: fn.frame_bytes, saved_register_bytes: fn.saved_bytes,
    entry_instructions: fn.entry.length,
    call_transport_instructions: callOperations.reduce((n, operation) => n + operation.assembly.length, 0),
    noncall_instructions: fn.operations.filter(operation => !isCall(operation))
      .reduce((n, operation) => n + operation.assembly.length, 0),
    instructions: assembly.length, loads: count(/^ldr/), stores: count(/^str/),
    call_sites: callSites };
}
export function callTransportChanges(before, after) {
  assert.deepEqual(after.map(fn => fn.name), before.map(fn => fn.name));
  return before.map((a, index) => {
    const b = after[index], label = a.name;
    assert.deepEqual(b.registers, a.registers, `${label}: register assignments`);
    assert.equal(b.saved_bytes, a.saved_bytes, `${label}: callee saves`);
    assert.equal(b.frame_bytes, a.frame_bytes, `${label}: frames`);
    assert.deepEqual(b.slots, a.slots, `${label}: value/object homes`);
    assert.deepEqual(b.phi, a.phi, `${label}: PHI transport`);
    assert.deepEqual(b.operations.map(o => o.ir), a.operations.map(o => o.ir), `${label}: IR`);
    for (let i = 0; i < a.operations.length; i++) {
      const x = a.operations[i], y = b.operations[i];
      if (!isCall(x))
        assert.deepEqual(y.assembly, x.assembly, `${label}: noncall computation`);
      else {
        const split = operation => {
          const call = operation.assembly.findIndex(line => /^bl(?:x)? /.test(line));
          return call < 0 ? operation.assembly : operation.assembly.slice(call);
        };
        assert.deepEqual(split(y), split(x), `${label}: call/result/intrinsic effects`);
      }
    }
    const control = metrics(a), candidate = metrics(b);
    assert(candidate.instructions <= control.instructions, `${label}: instruction growth`);
    assert.equal(candidate.loads, control.loads, `${label}: loads`);
    assert.equal(candidate.stores, control.stores, `${label}: stores`);
    return { function: label, control, candidate };
  });
}
export function verifyCallEvidence(file) {
  const report = read(file);
  assert.equal(report.milestone, 'PO07');
  assert.equal(report.status, 'qualified');
  assert.equal(report.modules.length, 37);
  for (const artifact of report.artifacts)
    assert.equal(hash(path.join(root, artifact.file)), artifact.sha256, artifact.file);
  for (const module of report.modules) {
    const ir = fs.readFileSync(path.join(root, module.ir), 'utf8');
    const functions = ['control', 'candidate'].map(role =>
      traceFunctions(fs.readFileSync(path.join(root, module[role].assembly), 'utf8'), ir));
    assert.deepEqual(callTransportChanges(...functions), module.functions);
  }
  return report;
}
function capture(directory, inputFile) {
  const offline = read(inputFile);
  assert.equal(offline.milestone, 'PO07');
  assert.equal(offline.status, 'offline-qualified');
  const candidate = offline.candidates.find(item => item.id === 'po07');
  assert.equal(candidate.modules.length, 37);
  fs.mkdirSync(directory);
  const report = { schema: 1, milestone: 'PO07', status: 'in-progress',
    input: { file: relative(inputFile), sha256: hash(inputFile) },
    modules: [], artifacts: [], limitations: [
      'Emitted instruction counts separate entry, arguments, result restoration and callee computation; not retired cycles.',
      'No bounded inlining: public/callback edges, repeated callees and size/pressure uncertainty remain.',
      'Allocation, homes, saves, calls, noncall effects and result handling must be identical.',
      'Physical application timings use ordinary whole-source Clang C as reference, not a backend-only attribution.',
    ] };
  const artifact = file => report.artifacts.push({ file: relative(file), sha256: hash(file) });
  artifact(inputFile);
  for (const module of candidate.modules) {
    const input = path.join(root, module.file);
    assert.equal(hash(input), module.sha256);
    artifact(input);
    const record = { module: module.module, ir: relative(input), functions: [] }, functions = [];
    for (const role of ['control', 'candidate']) {
      const stem = path.join(directory, `${module.module}.${role}`);
      const assembly = `${stem}.s`, object = `${stem}.o`;
      const normal = `${stem}.normal.s`, replay = `${stem}.normal.o`;
      const flags = [`--scalar-selection=${role === 'control' ? 'thumb-homes' : 'thumb-calls'}`];
      run('badge-codegen-attribution', [...flags, input, '-o', assembly]);
      run('badge-asm', [assembly, '-o', object]);
      run('badge-codegen', [...flags, input, '-o', normal]);
      run('badge-asm', [normal, '-o', replay]);
      assert.equal(hash(object), hash(replay), 'trace changes executable object');
      if (role === 'candidate')
        assert.equal(hash(normal), module.assembly_sha256, `${module.module}: corpus replay differs`);
      functions.push(traceFunctions(fs.readFileSync(assembly, 'utf8'), fs.readFileSync(input, 'utf8')));
      record[role] = { assembly: relative(assembly), object: relative(object), sha256: hash(object) };
      for (const file of [assembly, object, normal, replay]) artifact(file);
    }
    record.functions = callTransportChanges(...functions);
    report.modules.push(record);
  }
  for (const name of ['badge-codegen', 'badge-codegen-attribution', 'badge-asm'])
    artifact(path.join(tools, name));
  artifact(fileURLToPath(import.meta.url));
  artifact(path.join(root, 'badge-compiler/tools/backend-attribution.mjs'));
  report.status = 'qualified';
  const file = path.join(directory, 'results.json');
  fs.writeFileSync(file, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  verifyCallEvidence(file);
  const functions = report.modules.flatMap(module => module.functions);
  const sum = (role, key) => functions.reduce((n, fn) => n + fn[role][key], 0);
  console.log(JSON.stringify({ file: relative(file), modules: report.modules.length, functions: functions.length,
    removed_entry_instructions: sum('control', 'entry_instructions') - sum('candidate', 'entry_instructions'),
    removed_call_instructions: sum('control', 'call_transport_instructions') - sum('candidate', 'call_transport_instructions'),
    removed_instructions: sum('control', 'instructions') - sum('candidate', 'instructions'),
    unchanged_frame_bytes: sum('control', 'frame_bytes'),
    unchanged_saved_bytes: sum('control', 'saved_register_bytes') }));
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, output, input, ...extra] = process.argv.slice(2);
    assert(extra.length === 0 && (mode === '--capture' && output && input ||
      mode === '--verify' && output && !input),
    'Usage: call-evidence.mjs --capture NEW_DIRECTORY OFFLINE.json | --verify REPORT.json');
    if (mode === '--capture') capture(path.resolve(output), path.resolve(input));
    else {
      verifyCallEvidence(path.resolve(output));
      console.log('PASS: exact-input call/entry transport, unchanged computation/ABI and trace/object equality.');
    }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
