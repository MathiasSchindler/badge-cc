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
  return result.stdout;
}
export function comparisonChanges(before, after) {
  assert.deepEqual(before.map(f => f.name), after.map(f => f.name));
  return before.flatMap((a, n) => {
    const b = after[n];
    assert.equal(b.frame_bytes, a.frame_bytes);
    assert.equal(b.saved_bytes, a.saved_bytes);
    assert.deepEqual(b.registers, a.registers);
    assert.deepEqual(b.operations.map(o => o.ir), a.operations.map(o => o.ir));
    const instructions = fn => fn.entry.length +
      fn.operations.reduce((sum, op) => sum + op.assembly.length, 0);
    const pairs = a.operations.flatMap((op, i) => {
      if (!/ = icmp /.test(op.ir) || !op.assembly.length ||
          b.operations[i].assembly.length) return [];
      const branch = a.operations[i + 1], selected = b.operations[i + 1];
      assert(branch && /^br i1 /.test(branch.ir));
      assert(selected.assembly.includes('cmp r0, r1'));
      assert(!selected.assembly.includes('cmp r0, #0'));
      return [{ ir_line: op.line, comparison: op.ir, branch: branch.ir,
        before: [...op.assembly, ...branch.assembly],
        after: selected.assembly,
        removed: op.assembly.length + branch.assembly.length - selected.assembly.length }];
    });
    return [{ function: a.name, before_instructions: instructions(a),
      after_instructions: instructions(b), pairs }];
  });
}
export function verifyBranchEvidence(file) {
  const report = read(file);
  assert.equal(report.status, 'qualified');
  assert.equal(report.modules.length, 11);
  for (const artifact of report.artifacts)
    assert.equal(hash(path.join(root, artifact.file)), artifact.sha256, artifact.file);
  for (const module of report.modules) {
    const functions = ['control', 'candidate'].map(role => traceFunctions(
      fs.readFileSync(path.join(root, module[role].assembly), 'utf8'),
      fs.readFileSync(path.join(root, module.ir), 'utf8')));
    assert.deepEqual(comparisonChanges(...functions), module.functions);
    assert.equal(module.control.sha256, module.po02_object_sha256);
  }
  return report;
}
function capture(directory, attribution) {
  const old = read(attribution);
  assert.equal(old.status, 'qualified-fixed-ir-attribution');
  fs.mkdirSync(directory);
  const report = { schema: 1, milestone: 'PO03', status: 'in-progress',
    input: { file: relative(attribution), sha256: hash(attribution) },
    modules: [], artifacts: [], limitations: [
      'Same historical owned optimized IR, not another IR optimization campaign.',
      'Static assembly traffic counts are not retired instructions or timing predictions.',
      'Unchanged allocation deliberately retains unused compare-result assignments and homes.',
      'Exact real-hardware timing is independently captured by the application benchmark.',
    ] };
  const artifact = file => report.artifacts.push({ file: relative(file), sha256: hash(file) });
  artifact(attribution);
  for (const module of old.modules) {
    const command = module.commands.find(c => c.role === 'owned');
    const input = path.join(root, command.input);
    assert.equal(hash(input), module.optimized_ir_sha256, 'historical IR changed');
    artifact(input);
    const record = { module: module.module, ir: relative(input),
      ir_sha256: hash(input), po02_object_sha256: command.object_sha256 };
    const traced = [];
    for (const role of ['control', 'candidate']) {
      const stem = path.join(directory, `${module.module}.${role}`);
      const assembly = `${stem}.s`, object = `${stem}.o`;
      const plain = `${stem}.plain.s`, replay = `${stem}.plain.o`;
      const flags = [`--scalar-selection=${role === 'control' ? 'thumb-layout' : 'thumb-branch'}`];
      run('badge-codegen-attribution', [...flags, input, '-o', assembly]);
      run('badge-asm', [assembly, '-o', object]);
      run('badge-codegen', [...flags, input, '-o', plain]);
      run('badge-asm', [plain, '-o', replay]);
      assert.equal(hash(replay), hash(object), 'trace changes object');
      if (role === 'control') assert.equal(hash(object), command.object_sha256,
        `${module.module}: baseline no longer matches PO02`);
      traced.push(traceFunctions(fs.readFileSync(assembly, 'utf8'), fs.readFileSync(input, 'utf8')));
      record[role] = { assembly: relative(assembly), object: relative(object),
        sha256: hash(object), object_bytes: fs.statSync(object).size };
      for (const file of [assembly, object, plain, replay]) artifact(file);
    }
    record.functions = comparisonChanges(...traced);
    report.modules.push(record);
  }
  for (const name of ['badge-codegen', 'badge-codegen-attribution', 'badge-asm']) {
    artifact(path.join(tools, name));
  }
  artifact(fileURLToPath(import.meta.url));
  artifact(path.join(root, 'badge-compiler/tools/backend-attribution.mjs'));
  report.status = 'qualified';
  const file = path.join(directory, 'results.json');
  fs.writeFileSync(file, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
  verifyBranchEvidence(file);
  const pairs = report.modules.flatMap(m => m.functions.flatMap(f => f.pairs));
  console.log(JSON.stringify({ file: relative(file), baseline_objects_identical: report.modules.length,
    fused_comparisons: pairs.length, removed_static_instructions: pairs.reduce((n, p) => n + p.removed, 0) }));
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, output, input, ...extra] = process.argv.slice(2);
    assert(extra.length === 0 && (mode === '--capture' && output && input ||
      mode === '--verify' && output && !input),
    'Usage: compare-branch-evidence.mjs --capture NEW_DIRECTORY PO02.json | --verify REPORT.json');
    if (mode === '--capture') capture(path.resolve(output), path.resolve(input));
    else {
      verifyBranchEvidence(path.resolve(output));
      console.log('PASS: historical baseline objects, trace equality and exact comparison traffic.');
    }
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
