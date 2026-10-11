import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const baseline = absolute(process.env.BACKEND_REFACTOR_BASELINE ??
  'badge-compiler/build/rs06-counted-delay/results.json');
const tool = name => absolute(`badge-compiler/build/${name}`);
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const digest = text => crypto.createHash('sha256').update(text).digest('hex');
function invoke(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  assert.equal(result.signal, null);
  return result;
}
function run(command, args) {
  const result = invoke(command, args);
  assert.equal(result.status, 0, `${command}: ${result.stdout}\n${result.stderr}`);
}
test('backend refactoring preserves policy outputs, diagnostics, attribution and complete images', {
  skip: !fs.existsSync(baseline),
}, t => {
  const previous = JSON.parse(fs.readFileSync(baseline));
  assert.equal(previous.milestone, 'RS06');
  const before = path.join(path.dirname(baseline), 'tools/badge-codegen');
  assert.equal(hash(before), previous.artifacts.find(a => absolute(a.file) === before)?.sha256);
  const dir = fs.mkdtempSync(absolute('badge-compiler/build/backend-refactor-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const file = name => path.join(dir, name);
  const red = previous.commands.find(c => c.tool === 'badge-codegen' &&
    c.args.at(-1).endsWith('/red_square.s'));
  assert(red);
  const inputs = [...previous.modules.map(m => m.ir), red.args[1]];
  const maze = absolute('build/maze-rs06-owned/universe26/firmware');
  if (fs.existsSync(maze))
    for (const name of fs.readdirSync(maze).filter(n => n.endsWith('.o.owned'))) {
      const ir = path.join(maze, name, 'optimized.ll');
      if (fs.existsSync(ir)) inputs.push(ir);
    }
  const records = [];
  function compare(input, options, old = before, current = tool('badge-codegen')) {
    const outcomes = [];
    for (const [compiler, output] of [[old, file('before.s')], [current, file('after.s')]]) {
      fs.writeFileSync(output, 'preserved-output\n');
      const result = invoke(compiler, [...options, input, '-o', output]);
      const assembly = fs.readFileSync(output);
      if (result.status !== 0) {
        assert(result.stderr.length, 'failure must have diagnostics');
        assert.equal(assembly.toString(), 'preserved-output\n');
      }
      outcomes.push({ status: result.status, stdout: result.stdout,
        stderr: result.stderr, assembly: digest(assembly) });
    }
    assert.deepEqual(outcomes[1], outcomes[0], `${input}: ${options.join(' ')}`);
    records.push({ input, options, status: outcomes[0].status,
      assembly_sha256: outcomes[0].assembly, diagnostics_sha256: digest(outcomes[0].stderr) });
    return outcomes[0].status;
  }
  const policies = ['legacy', 'thumb', 'thumb-registers', 'thumb-layout',
    'thumb-compact', 'thumb-schedule', 'thumb-relax', 'thumb-branch', 'thumb-homes',
    'thumb-calls', 'thumb-effects', 'thumb-frames', 'thumb-control', 'thumb-operands',
    'thumb-widths', 'thumb-constants'];
  for (const input of inputs) {
    assert.equal(compare(input, []), 0);
    for (const policy of policies) {
      const status = compare(input, [`--scalar-selection=${policy}`]);
      if (['thumb-widths', 'thumb-constants'].includes(policy)) assert.equal(status, 0);
    }
  }
  for (const input of [red.args[1], ...inputs.slice(previous.modules.length + 1)])
    for (const policy of ['thumb-widths', 'thumb-constants']) {
      for (const residency of ['stack', 'local', 'global', 'global-flex'])
        compare(input, [`--scalar-selection=${policy}`, `--register-residency=${residency}`]);
      compare(input, [`--scalar-selection=${policy}`, '--memory-selection=byte']);
      compare(input, [`--scalar-selection=${policy}`, '--frame-addressing=materialized']);
    }
  const header = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
  const invalid = [
    `${header}define void @bad() {\nentry:\nbr label %missing\n}\n`,
    `${header}define void @bad() {\nentry:\ncall void asm sideeffect "nop", "~{r0}"()\nret void\n}\n`,
    `${header}define i32 @bad(i32 %x) {\nentry:\n%y = mystery i32 %x\nret i32 %y\n}\n`,
  ];
  for (const [index, source] of invalid.entries()) {
    const input = file(`invalid-${index}.ll`);
    fs.writeFileSync(input, source);
    for (const policy of ['thumb-widths', 'thumb-constants'])
      assert.notEqual(compare(input, [`--scalar-selection=${policy}`]), 0);
  }
  for (const args of [['--help'], ['--default-frame-addressing'],
    ['--default-register-residency'], ['--default-scalar-selection'],
    ['--default-memory-selection'], ['--scalar-selection=invalid'],
    ['--memory-selection=invalid'], ['--register-residency=invalid']]) {
    const old = invoke(before, args), current = invoke(tool('badge-codegen'), args);
    for (const key of ['status', 'stdout', 'stderr']) assert.equal(current[key], old[key]);
  }
  const oldAttribution = absolute(process.env.BACKEND_REFACTOR_ATTRIBUTION_BASELINE ??
    'badge-compiler/build/rf01-backend/tools/badge-codegen-attribution-before');
  let attribution = 0;
  if (fs.existsSync(oldAttribution))
    for (const input of inputs) {
      assert.equal(compare(input, ['--scalar-selection=thumb-constants'],
        oldAttribution, tool('badge-codegen-attribution')), 0);
      attribution++;
    }
  const oldProfile = absolute(process.env.BACKEND_REFACTOR_PROFILE_BASELINE ??
    'badge-compiler/build/rf01-backend/tools/profile-codegen-before');
  if (fs.existsSync(oldProfile)) {
    const old = invoke(oldProfile, ['2', ...inputs]);
    const current = invoke(tool('profile-codegen'), ['2', ...inputs]);
    assert.equal(old.status, 0, old.stderr);
    assert.equal(current.status, 0, current.stderr);
    const outputs = text => text.trim().split('\n').map(line => {
      const fields = line.split('\t');
      assert.equal(fields.length, 7);
      return [fields[0], fields[5], fields[6]];
    });
    assert.deepEqual(outputs(current.stdout), outputs(old.stdout));
  }
  const replacements = new Map();
  for (const [index, module] of previous.modules.entries()) {
    const assembly = file(`module-${index}.s`), object = file(`module-${index}.o`);
    run(tool('badge-codegen'), ['--scalar-selection=thumb-constants', module.ir, '-o', assembly]);
    assert.equal(hash(assembly), hash(absolute(module.assembly)));
    run(tool('badge-asm'), [assembly, '-o', object]);
    assert.equal(hash(object), hash(absolute(module.object)));
    replacements.set(module.object, object);
  }
  const redAssembly = file('red_square.s'), redObject = file('red_square.o');
  run(tool('badge-codegen'), ['--scalar-selection=thumb-constants', red.args[1], '-o', redAssembly]);
  assert.equal(hash(redAssembly), hash(absolute(red.args.at(-1))));
  run(tool('badge-asm'), [redAssembly, '-o', redObject]);
  replacements.set(red.args.at(-1).replace(/\.s$/, '.o'), redObject);
  const images = [];
  for (const [index, command] of previous.commands.filter(c => c.tool === 'badge-link').entries()) {
    assert.equal(command.args.at(-2), '-o');
    const output = file(`image-${index}.elf`), original = command.args.at(-1);
    const args = command.args.slice(0, -1).map(arg => replacements.get(arg) ?? arg);
    run(tool('badge-link'), [...args, output]);
    assert.equal(hash(output), hash(absolute(original)), `${original}: ELF changed`);
    const binary = file(`image-${index}.bin`);
    run(tool('elf2bin'), ['-O', 'binary', output, binary]);
    const bytes = fs.statSync(binary).size;
    const originalBinary = absolute(original.replace(/\.elf$/, '.bin'));
    assert.equal(hash(binary), hash(originalBinary), `${original}: binary changed`);
    images.push({ original, elf_sha256: hash(output), binary_sha256: hash(binary), bytes });
  }
  assert.equal(images.length, 13);
  assert.equal(images.find(i => i.original === previous.red_square.elf)?.bytes, 624);
  const reportFile = process.env.BACKEND_REFACTOR_REPORT;
  if (reportFile) {
    const output = absolute(reportFile), directory = path.dirname(output);
    assert(!fs.existsSync(output), 'fresh refactoring report required');
    fs.mkdirSync(path.join(directory, 'tools'), { recursive: true });
    const backend = path.join(directory, 'tools/badge-codegen');
    fs.copyFileSync(tool('badge-codegen'), backend, fs.constants.COPYFILE_EXCL);
    fs.writeFileSync(output, JSON.stringify({
      schema: 1, milestone: 'RF01', status: 'byte-identical-host-qualified',
      baseline: { report: baseline, sha256: hash(baseline), backend: before, backend_sha256: hash(before) },
      backend: { file: backend, sha256: hash(backend) },
      sources: ['src/codegen.c', 'src/codegen_cfg.c', 'src/codegen_frame.c', 'src/codegen_internal.h',
        'tests/backend-refactor.test.mjs'].map(source => ({ file: `badge-compiler/${source}`,
          sha256: hash(absolute(`badge-compiler/${source}`)) })),
      inputs: inputs.map(input => ({ file: input, sha256: hash(absolute(input)) })),
      scalar_policies: policies.length, comparisons: records, attribution_comparisons: attribution,
      images, default_changed: false, hardware_touched: false,
    }, null, 2) + '\n');
  }
  console.log(`RF01: ${inputs.length} IR inputs, ${records.length} exact comparisons, ` +
    `${attribution} attribution comparisons, ${images.length} identical images`);
});
