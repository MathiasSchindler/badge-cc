import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { decodeElf } from './boundary-evidence.mjs';
import { parseAssembly } from './benchmark-c06.mjs';
import { verifyLoadedImage } from './cross-function-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const read = file => JSON.parse(fs.readFileSync(absolute(file)));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(absolute(file))).digest('hex');
const code = file => decodeElf(fs.readFileSync(absolute(file))).sections
  .filter(s => s.flags & 4).reduce((sum, s) => sum + s.size, 0);
const policy = { control: 'thumb-widths', candidate: 'thumb-constants',
  ir: 'identical qualified PO18 optimized modules', default_changed: false };
function summary(report) {
  return { modules: report.modules.length,
    functions: report.modules.reduce((n, m) => n + m.functions.length, 0),
    code_bytes_saved: report.modules.reduce((n, m) => n + m.before_bytes - m.after_bytes, 0),
    images_smaller: report.images.filter(i => i.loaded_bytes < i.before_bytes).length,
    command_milliseconds: report.commands.reduce((n, c) => n + c.milliseconds, 0) };
}
function frames(beforeFile, afterFile) {
  const before = parseAssembly(fs.readFileSync(absolute(beforeFile), 'utf8'), 'control');
  const after = parseAssembly(fs.readFileSync(absolute(afterFile), 'utf8'), 'candidate');
  assert.deepEqual([...after.keys()], [...before.keys()]);
  return [...after].map(([name, frame]) => {
    const original = before.get(name);
    for (const field of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
      assert(Number.isInteger(frame[field]) && frame[field] <= original[field], `${name}: ${field} grew`);
    assert.deepEqual(frame.calls, original.calls);
    assert(!frame.unknown.includes('unverified_frame_balance'));
    return { name, frame: frame.frame_bytes, saves: frame.saves_bytes,
      outgoing: frame.max_outgoing_bytes, calls: frame.calls };
  });
}
export function verifyConstantSelection(file) {
  const report = read(file);
  assert(['RS01', 'RS02', 'RS03', 'RS04', 'RS05', 'RS06'].includes(report.milestone));
  const delay = report.milestone === 'RS06';
  const pixel = report.milestone === 'RS05' || delay;
  const interpreter = report.milestone === 'RS04' || pixel;
  const transport = report.milestone !== 'RS01';
  const wrapper = report.milestone === 'RS03';
  const preservation = report.call_preservation !== undefined;
  assert(!preservation || wrapper);
  const targetedTests = delay ? 13 : pixel ? 12 : interpreter ? 11 : preservation ? 9 : wrapper ? 8 : transport ? 7 : 6;
  assert.equal(report.status, transport ? 'host-qualified-hardware-pending' : 'host-qualified-target-not-met');
  assert.deepEqual(report.policy, policy);
  assert.equal(report.hardware_touched, false);
  const tracked = new Set(report.artifacts.map(a => a.file));
  assert.equal(tracked.size, report.artifacts.length);
  assert(tracked.has(report.baseline));
  for (const a of report.artifacts) assert.equal(hash(a.file), a.sha256, a.file);
  assert(report.commands.every(c => c.status === 0));
  for (const command of report.commands) assert(tracked.has(command.log));
  if (report.schema === undefined) {
    for (const [file, tests] of [['rs01-targeted-tests.log', 2], ['rs01-driver-tests.log', 15]]) {
      const log = `badge-compiler/build/${file}`;
      assert(tracked.has(log));
      const text = fs.readFileSync(absolute(log), 'utf8');
      assert.match(text, new RegExp(`# tests ${tests}\\b`));
      assert.match(text, /# fail 0\b/);
    }
  } else {
    assert([2, 3, 4].includes(report.schema));
    const prefix = report.schema === 3 ? 'rs01-control' : 'rs01-final';
    const logPath = suffix => report.schema === 4 ?
      `${path.dirname(report.red_square.elf)}/${suffix}.log` :
      `badge-compiler/build/${prefix}-${suffix}.log`;
    assert.deepEqual(report.validation, [
      { file: logPath('targeted-tests'), tests: transport ? targetedTests : report.schema >= 3 ? 6 : 5 },
      { file: logPath('driver-tests'), tests: 15 },
    ]);
    for (const { file, tests } of report.validation) {
      assert(tracked.has(file));
      const text = fs.readFileSync(absolute(file), 'utf8');
      assert.match(text, new RegExp(`# tests ${tests}\\b`));
      assert.match(text, /# fail 0\b/);
      assert.match(text, /# skipped 0\b/);
    }
    const pipeline = logPath('pipeline-tests');
    assert(tracked.has(pipeline));
    assert.match(fs.readFileSync(absolute(pipeline), 'utf8'),
      /owned-pipeline: stage order, quoted paths\/flags and immediate failure propagation PASS/);
  }
  const baseline = read(report.baseline);
  assert.equal(report.modules.length, baseline.modules.length);
  for (const module of report.modules) {
    const old = baseline.modules.find(m => m.module === module.module);
    assert(old);
    for (const key of ['ir', 'control', 'assembly', 'object'])
      assert(tracked.has(module[key]));
    assert.equal(module.ir, old.ir);
    assert.equal(module.before_bytes, code(old.object));
    assert.equal(module.after_bytes, code(module.object));
    assert(module.after_bytes <= module.before_bytes);
    assert.equal(module.control_identical, hash(module.control) === hash(old.assembly));
    assert.equal(module.control_identical, true);
    assert.deepEqual(module.functions, frames(module.control, module.assembly));
  }
  for (const image of report.images) {
    const old = baseline.images.find(i => i.image === image.image);
    assert(old);
    for (const key of ['elf', 'binary', 'metrics', 'script']) assert(tracked.has(image[key]));
    assert.equal(image.loaded_bytes, fs.statSync(absolute(image.binary)).size);
    assert(image.loaded_bytes <= old.loaded_bytes);
    const metrics = read(image.metrics), before = read(old.metrics);
    assert.equal(metrics.data, before.data);
    assert.equal(metrics.bss, before.bss);
    verifyLoadedImage(absolute(image.elf), absolute(image.binary), absolute(image.script));
  }
  assert.equal(report.images.length, baseline.images.length);
  const red = report.red_square;
  for (const key of ['elf', 'binary', 'metrics', 'script']) assert(tracked.has(red[key]));
  verifyLoadedImage(absolute(red.elf), absolute(red.binary), absolute(red.script));
  assert.equal(red.loaded_bytes, fs.statSync(absolute(red.binary)).size);
  assert.equal(red.main_bytes, read(red.metrics).symbols.find(s => s.name === 'bare_main').size);
  assert(red.main_bytes < 396);
  assert.equal(red.target_bytes, 184);
  assert.equal(red.target_met, red.main_bytes <= red.target_bytes);
  assert.equal(red.target_met, false);
  if (transport) {
    assert(tracked.has(report.predecessor));
    const prior = verifyConstantSelection(report.predecessor);
    assert.equal(prior.milestone, delay ? 'RS05' : pixel ? 'RS04' : interpreter || preservation ? 'RS03' : wrapper ? 'RS02' : 'RS01');
    for (const module of report.modules) {
      const old = prior.modules.find(m => m.module === module.module);
      assert(old && module.after_bytes <= old.after_bytes);
      for (const frame of module.functions) {
        const before = old.functions.find(f => f.name === frame.name);
        for (const field of ['frame', 'saves', 'outgoing'])
          assert(before && frame[field] <= before[field]);
      }
    }
    for (const image of report.images) {
      const old = prior.images.find(i => i.image === image.image);
      assert(old && image.loaded_bytes <= old.loaded_bytes);
    }
    const symbol = delay ? 'delay_cycles' : pixel ? 'fill' : interpreter ? 'commands' : wrapper ? 'command' : 'write_byte';
    const target = delay ? 20 : pixel ? 36 : interpreter ? 40 : wrapper ? 24 : 32;
    const before = read(prior.red_square.metrics).symbols.find(s => s.name === symbol).size;
    const after = read(red.metrics).symbols.find(s => s.name === symbol).size;
    const result = delay ? report.initialization_delay : pixel ? report.pixel_fill : interpreter ? report.table_interpreter : wrapper ? report.command_wrapper : report.byte_transport;
    assert.deepEqual(result, {
      before_bytes: before, bytes: after, llvm_target: target, target_met: after <= target,
      hardware_qualified: false, default_changed: false,
    });
    assert.equal(result.target_met, true);
    if (interpreter) {
      const old = read(prior.red_square.metrics);
      const current = read(red.metrics);
      for (const fn of old.symbols.filter(s => s.type === 'FUNC' || s.type === 2))
        assert(current.symbols.find(s => s.name === fn.name)?.size <= fn.size, `${fn.name}: bytes grew`);
      assert(red.loaded_bytes <= prior.red_square.loaded_bytes);
      const beforeAssembly = `${path.dirname(prior.red_square.elf)}/red_square.s`;
      const afterAssembly = `${path.dirname(red.elf)}/red_square.s`;
      assert(tracked.has(beforeAssembly) && tracked.has(afterAssembly));
      assert.deepEqual(report.red_square_functions, frames(beforeAssembly, afterAssembly));
      const table = report.red_square_functions.find(f => f.name === symbol);
      assert.equal(table.frame, 0);
      assert.equal(table.saves, delay ? 0 : 16);
      assert.equal(current.data, old.data);
      assert.equal(current.bss, old.bss);
      if (delay) {
        const assembly = fs.readFileSync(absolute(afterAssembly), 'utf8')
          .split(/(?=\.section \.text\.)/).find(s => s.startsWith('.section .text.delay_cycles,'));
        assert(assembly);
        assert.match(assembly, /@ counted-leaf: unsigned i32 iterations, 6 explicit nops/);
        assert.equal((assembly.match(/^nop$/gm) ?? []).length, 6);
        assert.match(assembly, /cbz r0, \.Lcg_return\d+/);
        assert.match(assembly, /subs\.n r0, r0, #1\nbne\.n \.Lcg_counted\d+/);
        assert.deepEqual(report.minimum_delay, {
          iterations: 4000000, explicit_nops_per_iteration: 6,
          minimum_cycles: 24000000, maximum_clock_hz: 150000000, minimum_milliseconds: 160,
          electrical_timing_measured: false,
        });
      }
    }
    if (preservation) {
      const assembly = fs.readFileSync(absolute(`${path.dirname(red.elf)}/red_square.s`), 'utf8');
      const functions = assembly.split(/(?=\.section \.text\.)/).slice(1);
      const callee = functions.find(text => text.startsWith('.section .text.write_byte,'));
      const caller = functions.find(text => text.startsWith('.section .text.command,'));
      const proof = callee?.match(/finalized-register-mentions: known 1 mask (\d+)/);
      assert(proof && !(Number(proof[1]) & 4));
      assert.match(caller, /r2 retained through proven internal call/);
      assert.equal((caller.match(/^mov\.w r2, #268435456$/gm) ?? []).length, 1);
      assert.equal((caller.match(/^str\.n r2, \[r4, #(?:24|32)\]$/gm) ?? []).length, 2);
      assert.deepEqual(report.call_preservation, {
        target_bytes: 20, bytes: after, target_met: after <= 20, register: 2,
        callee: 'write_byte', callee_register_mentions: Number(proof[1]),
        proof: 'finalized-internal-register-mentions',
      });
      assert.equal(report.call_preservation.target_met, true);
    }
    assert(red.loaded_bytes <= prior.red_square.loaded_bytes);
    if (wrapper) {
      const previousMetrics = read(prior.red_square.metrics);
      const currentMetrics = read(red.metrics);
      for (const symbol of currentMetrics.symbols.filter(s => s.type === 2)) {
        const old = previousMetrics.symbols.find(s => s.name === symbol.name && s.type === 2);
        assert(old && symbol.size <= old.size, `${symbol.name}: compact code grew`);
      }
      assert.equal(currentMetrics.data, previousMetrics.data);
      assert.equal(currentMetrics.bss, previousMetrics.bss);
      const preceding = parseAssembly(fs.readFileSync(absolute(
        `${path.dirname(prior.red_square.elf)}/red_square.s`), 'utf8'), 'predecessor');
      const current = parseAssembly(fs.readFileSync(absolute(
        `${path.dirname(red.elf)}/red_square.s`), 'utf8'), 'candidate');
      for (const [name, frame] of current) {
        const old = preceding.get(name);
        for (const field of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
          assert(old && frame[field] <= old[field]);
        assert.deepEqual(frame.calls, old.calls);
      }
    }
    const targeted = fs.readFileSync(absolute(report.validation[0].file), 'utf8');
    const reached = targeted.match(new RegExp(`${report.milestone} reached oracle instructions (\\d+) -> (\\d+)`));
    assert(reached && Number(reached[2]) < Number(reached[1]));
    assert.deepEqual(report.reached_instructions, {
      control: Number(reached[1]), candidate: Number(reached[2]),
    });
  }
  assert.deepEqual(report.summary, summary(report));
  if (report.final_validation) {
    const final = report.final_validation;
    const directory = path.dirname(report.red_square.elf);
    assert.equal(final.tests, targetedTests);
    assert.equal(final.log, `${directory}/final-validation/targeted-tests.log`);
    assert.deepEqual(final.sources, [
      `${directory}/final-validation/constant_selection_test.c`,
      `${directory}/final-validation/constant-selection.test.mjs`,
      ...(transport ? [`${directory}/final-validation/byte-transport.test.mjs`] : []),
      ...(wrapper || interpreter ? [`${directory}/final-validation/command-wrapper.test.mjs`] : []),
      ...(preservation || interpreter ? [`${directory}/final-validation/call-preservation.test.mjs`] : []),
      ...(interpreter ? [`${directory}/final-validation/table-interpreter.test.mjs`] : []),
      ...(pixel ? [`${directory}/final-validation/pixel-fill.test.mjs`] : []),
      ...(delay ? [`${directory}/final-validation/counted-delay.test.mjs`] : []),
    ]);
    assert.equal(final.native, `${directory}/final-validation/constant_selection_test`);
    for (const file of [final.log, final.native, ...final.sources]) assert(tracked.has(file));
    const log = fs.readFileSync(absolute(final.log), 'utf8');
    assert.match(log, new RegExp(`# tests ${targetedTests}\\b`));
    assert.match(log, /# fail 0\b/);
    assert.match(log, /# skipped 0\b/);
    if (interpreter) {
      assert.deepEqual(final.driver, { file: `${directory}/final-validation/driver-tests.log`, tests: 15 });
      assert.equal(final.pipeline, `${directory}/final-validation/pipeline-tests.log`);
      for (const file of [final.driver.file, final.pipeline]) assert(tracked.has(file));
      const driver = fs.readFileSync(absolute(final.driver.file), 'utf8');
      assert.match(driver, /# tests 15\b/);
      assert.match(driver, /# fail 0\b/);
      assert.match(driver, /# skipped 0\b/);
      assert.match(fs.readFileSync(absolute(final.pipeline), 'utf8'),
        /owned-pipeline: stage order, quoted paths\/flags and immediate failure propagation PASS/);
    }
  }
  return report;
}
function finalize(file) {
  const report = verifyConstantSelection(file);
  assert(!report.final_validation, 'final validation already recorded');
  const directory = path.dirname(report.red_square.elf);
  const transport = report.milestone !== 'RS01';
  const wrapper = report.milestone === 'RS03';
  const delay = report.milestone === 'RS06';
  const pixel = report.milestone === 'RS05' || delay;
  const interpreter = report.milestone === 'RS04' || pixel;
  const preservation = report.call_preservation !== undefined;
  const tests = delay ? 13 : pixel ? 12 : interpreter ? 11 : preservation ? 9 : wrapper ? 8 : transport ? 7 : 6;
  assert.equal(hash('badge-compiler/build/badge-codegen'), hash(`${directory}/tools/badge-codegen`),
    'backend changed: a new corpus capture is required');
  const logPrefix = delay ? 'rs06' : pixel ? 'rs05' : interpreter ? 'rs04' : preservation ? 'rs03-reuse' : wrapper ? 'rs03' : transport ? 'rs02' : 'rs01-branch';
  const logFile = `badge-compiler/build/${logPrefix}-targeted-tests.log`;
  const log = fs.readFileSync(absolute(logFile), 'utf8');
  assert.match(log, new RegExp(`# tests ${tests}\\b`));
  assert.match(log, /# fail 0\b/);
  assert.match(log, /# skipped 0\b/);
  const out = `${directory}/final-validation`;
  fs.mkdirSync(absolute(out));
  const entries = [
    [logFile, 'targeted-tests.log'],
    ['badge-compiler/tests/constant_selection_test.c', 'constant_selection_test.c'],
    ['badge-compiler/tests/constant-selection.test.mjs', 'constant-selection.test.mjs'],
    ['badge-compiler/build/constant_selection_test', 'constant_selection_test'],
    ...(transport ? [['badge-compiler/tests/byte-transport.test.mjs', 'byte-transport.test.mjs']] : []),
    ...(wrapper || interpreter ? [['badge-compiler/tests/command-wrapper.test.mjs', 'command-wrapper.test.mjs']] : []),
    ...(preservation || interpreter ? [['badge-compiler/tests/call-preservation.test.mjs', 'call-preservation.test.mjs']] : []),
    ...(interpreter ? [['badge-compiler/tests/table-interpreter.test.mjs', 'table-interpreter.test.mjs']] : []),
    ...(pixel ? [['badge-compiler/tests/pixel-fill.test.mjs', 'pixel-fill.test.mjs']] : []),
    ...(delay ? [['badge-compiler/tests/counted-delay.test.mjs', 'counted-delay.test.mjs']] : []),
    ...(interpreter ? [
      [`badge-compiler/build/${logPrefix}-driver-tests.log`, 'driver-tests.log'],
      [`badge-compiler/build/${logPrefix}-pipeline-tests.log`, 'pipeline-tests.log'],
    ] : []),
  ];
  for (const [source, name] of entries) {
    const target = `${out}/${name}`;
    fs.copyFileSync(absolute(source), absolute(target), fs.constants.COPYFILE_EXCL);
    report.artifacts.push({ file: target, sha256: hash(target) });
  }
  report.final_validation = { tests, log: `${out}/targeted-tests.log`,
    sources: [`${out}/constant_selection_test.c`, `${out}/constant-selection.test.mjs`,
      ...(transport ? [`${out}/byte-transport.test.mjs`] : []),
      ...(wrapper || interpreter ? [`${out}/command-wrapper.test.mjs`] : []),
      ...(preservation || interpreter ? [`${out}/call-preservation.test.mjs`] : []),
      ...(interpreter ? [`${out}/table-interpreter.test.mjs`] : []),
      ...(pixel ? [`${out}/pixel-fill.test.mjs`] : []),
      ...(delay ? [`${out}/counted-delay.test.mjs`] : [])],
    native: `${out}/constant_selection_test` };
  if (interpreter) {
    report.final_validation.driver = { file: `${out}/driver-tests.log`, tests: 15 };
    report.final_validation.pipeline = `${out}/pipeline-tests.log`;
  }
  fs.writeFileSync(absolute(file), JSON.stringify(report, null, 2) + '\n');
  return verifyConstantSelection(file);
}
export function captureConstantSelection(directory, milestone = 'RS01', preservation = false) {
  assert(['RS01', 'RS02', 'RS03', 'RS04', 'RS05', 'RS06'].includes(milestone));
  const delay = milestone === 'RS06';
  const pixel = milestone === 'RS05' || delay;
  const interpreter = milestone === 'RS04' || pixel;
  const wrapper = milestone === 'RS03';
  const transport = milestone !== 'RS01';
  assert(!preservation || wrapper);
  assert(!fs.existsSync(absolute(directory)), 'fresh evidence directory required');
  const baselineFile = 'badge-compiler/build/po18-qualified-defaults/results.json';
  const baseline = read(baselineFile);
  const prior = read('badge-compiler/build/po17-final-widths/results.json');
  fs.mkdirSync(absolute(directory), { recursive: true });
  const tracked = new Set();
  const track = file => { tracked.add(file); return file; };
  const out = name => track(`${directory}/${name}`);
  const report = { schema: 4, milestone, status: 'capturing', policy, baseline: track(baselineFile),
    modules: [], images: [], commands: [], artifacts: [], hardware_touched: false };
  const tools = {};
  for (const name of ['badge-codegen', 'badge-asm', 'badge-link', 'elf2bin', 'badge-elf-info']) {
    tools[name] = out(`tools/${name}`);
    fs.mkdirSync(path.dirname(absolute(tools[name])), { recursive: true });
    fs.copyFileSync(absolute(`badge-compiler/build/${name}`), absolute(tools[name]));
  }
  function run(name, args) {
    const started = performance.now();
    const r = spawnSync(absolute(tools[name]), args,
      { cwd: root, encoding: 'utf8', timeout: 120000 });
    const log = out(`${report.commands.length}.log`);
    fs.writeFileSync(absolute(log), `${r.stdout ?? ''}${r.stderr ?? ''}${r.error ?? ''}`);
    report.commands.push({ tool: name, args, log, status: r.status, milliseconds: performance.now() - started });
    assert.ifError(r.error); assert.equal(r.status, 0, r.stderr);
    return r.stdout;
  }
  const replacements = new Map();
  for (const old of baseline.modules) {
    track(old.ir); track(old.object); track(old.assembly);
    const control = out(`${old.module}.control.s`), assembly = out(`${old.module}.s`);
    run('badge-codegen', ['--scalar-selection=thumb-widths', old.ir, '-o', control]);
    assert.equal(hash(control), hash(old.assembly), 'default control emission changed');
    run('badge-codegen', ['--scalar-selection=thumb-constants', old.ir, '-o', assembly]);
    const object = out(`${old.module}.o`);
    run('badge-asm', [assembly, '-o', object]);
    replacements.set(hash(old.object), absolute(object));
    report.modules.push({ module: old.module, ir: old.ir, control, assembly, object,
      control_identical: true, before_bytes: code(old.object), after_bytes: code(object),
      functions: frames(control, assembly) });
  }
  for (const old of baseline.images) {
    const original = prior.images.find(i => i.image === old.image);
    const link = baseline.commands.find(c => c.executable.endsWith('/badge-link') &&
      c.args.at(-1) === absolute(old.elf)) ??
      prior.commands.find(c => c.executable.endsWith('/badge-link') &&
        c.args.at(-1) === absolute(original.elf));
    assert(link);
    const args = link.args.map(a => a.endsWith('.o') && fs.existsSync(a) ?
      replacements.get(hash(a)) ?? a : a);
    for (const a of args) if (a.endsWith('.o')) track(a);
    const elf = out(`${old.image}.elf`), binary = out(`${old.image}.bin`);
    const metrics = out(`${old.image}.metrics.json`);
    args[args.length - 1] = absolute(elf);
    run('badge-link', args);
    run('elf2bin', ['-O', 'binary', elf, binary]);
    fs.writeFileSync(absolute(metrics), run('badge-elf-info', ['--metrics', elf]));
    report.images.push({ image: old.image, elf, binary, metrics, script: track(old.script),
      before_bytes: old.loaded_bytes, loaded_bytes: fs.statSync(absolute(binary)).size });
  }
  const source = 'build/red-square-owned-current/universe26/firmware';
  const assembly = out('red_square.s'), object = out('red_square.o');
  run('badge-codegen', ['--scalar-selection=thumb-constants', track(`${source}/red_square.o.owned/optimized.ll`),
    '-o', assembly]);
  run('badge-asm', [assembly, '-o', object]);
  const elf = out('red_square.elf'), binary = out('red_square.bin'), metrics = out('red_square.metrics.json');
  const script = track('src/arch/rp2350/memmap_red_square.ld');
  run('badge-link', ['-T', script, track(`${source}/start_red_square.o`),
    track(`${source}/picobin.o`), object, '-o', elf]);
  run('elf2bin', ['-O', 'binary', elf, binary]);
  fs.writeFileSync(absolute(metrics), run('badge-elf-info', ['--metrics', elf]));
  const main = read(metrics).symbols.find(s => s.name === 'bare_main').size;
  report.red_square = { elf, binary, metrics, script, before_bytes: 992,
    loaded_bytes: fs.statSync(absolute(binary)).size, main_before: 396, main_bytes: main,
    target_bytes: 184, target_met: main <= 184 };
  for (const file of ['src/codegen_operands.c', 'src/codegen_select.c', 'src/codegen_frame.c', 'src/codegen_global.c', 'src/codegen_cfg.c',
    'src/codegen.c', 'src/codegen_internal.h', 'src/codegen.h',
    'src/badge_codegen.c', 'tests/constant_selection_test.c', 'tests/constant-selection.test.mjs',
    'tests/red-square.test.mjs', 'tests/constant-selection-evidence.test.mjs',
    ...(transport ? ['tests/byte-transport.test.mjs', 'tools/byte-transport-evidence.mjs'] : []),
    ...(wrapper ? ['tests/command-wrapper.test.mjs', 'tools/command-wrapper-evidence.mjs'] : []),
    ...(preservation ? ['tests/call-preservation.test.mjs'] : []),
    ...(interpreter ? ['tests/table-interpreter.test.mjs'] : []),
    ...(pixel ? ['tests/pixel-fill.test.mjs'] : []),
    ...(delay ? ['tests/counted-delay.test.mjs', 'tests/counted-delay-evidence.test.mjs',
      'tools/byte-transport-hardware.mjs', 'tests/byte-transport-hardware.test.mjs'] : []),
    'tools/constant-selection-evidence.mjs']) {
    const snapshot = out(`sources/${file}`);
    fs.mkdirSync(path.dirname(absolute(snapshot)), { recursive: true });
    fs.copyFileSync(absolute(`badge-compiler/${file}`), absolute(snapshot));
  }
  const logs = {};
  for (const suffix of ['targeted-tests', 'driver-tests', 'pipeline-tests', 'build']) {
    logs[suffix] = out(`${suffix}.log`);
    fs.copyFileSync(absolute(`badge-compiler/build/${delay ? 'rs06' : pixel ? 'rs05' : interpreter ? 'rs04' : preservation ? 'rs03-reuse' : wrapper ? 'rs03' : transport ? 'rs02' : 'rs01-branch'}-${suffix}.log`),
      absolute(logs[suffix]));
  }
  report.validation = [
    { file: logs['targeted-tests'], tests: delay ? 13 : pixel ? 12 : interpreter ? 11 : preservation ? 9 : wrapper ? 8 : transport ? 7 : 6 },
    { file: logs['driver-tests'], tests: 15 },
  ];
  report.summary = summary(report);
  report.status = transport ? 'host-qualified-hardware-pending' : 'host-qualified-target-not-met';
  if (transport) {
    report.predecessor = track(delay ? 'badge-compiler/build/rs05-pixel-fill/results.json' :
      pixel ? 'badge-compiler/build/rs04-byte-streams/results.json' :
      interpreter ? 'badge-compiler/build/rs03-call-preservation/results.json' :
      preservation ? 'badge-compiler/build/rs03-command-wrapper/results.json' :
      wrapper ? 'badge-compiler/build/rs02-byte-transport/results.json' :
      'badge-compiler/build/rs01-address-selection/results.json');
    const prior = verifyConstantSelection(report.predecessor);
    const symbol = delay ? 'delay_cycles' : pixel ? 'fill' : interpreter ? 'commands' : wrapper ? 'command' : 'write_byte';
    const target = delay ? 20 : pixel ? 36 : interpreter ? 40 : wrapper ? 24 : 32;
    report[delay ? 'initialization_delay' : pixel ? 'pixel_fill' : interpreter ? 'table_interpreter' : wrapper ? 'command_wrapper' : 'byte_transport'] = {
      before_bytes: read(prior.red_square.metrics).symbols.find(s => s.name === symbol).size,
      bytes: read(metrics).symbols.find(s => s.name === symbol).size,
      llvm_target: target, target_met: read(metrics).symbols.find(s => s.name === symbol).size <= target,
      hardware_qualified: false, default_changed: false,
    };
    if (delay) report.minimum_delay = {
      iterations: 4000000, explicit_nops_per_iteration: 6,
      minimum_cycles: 24000000, maximum_clock_hz: 150000000, minimum_milliseconds: 160,
      electrical_timing_measured: false,
    };
    if (interpreter) {
      const before = track(`${path.dirname(prior.red_square.elf)}/red_square.s`);
      report.red_square_functions = frames(before, assembly);
    }
    const reached = fs.readFileSync(absolute(logs['targeted-tests']), 'utf8')
      .match(new RegExp(`${milestone} reached oracle instructions (\\d+) -> (\\d+)`));
    assert(reached);
    report.reached_instructions = { control: Number(reached[1]), candidate: Number(reached[2]) };
    if (preservation) {
      const text = fs.readFileSync(absolute(assembly), 'utf8').split(/(?=\.section \.text\.)/)
        .find(text => text.startsWith('.section .text.write_byte,'));
      const proof = text.match(/finalized-register-mentions: known 1 mask (\d+)/);
      assert(proof);
      const bytes = read(metrics).symbols.find(s => s.name === 'command').size;
      report.call_preservation = { target_bytes: 20, bytes, target_met: bytes <= 20,
        register: 2, callee: 'write_byte', callee_register_mentions: Number(proof[1]),
        proof: 'finalized-internal-register-mentions' };
    }
  }
  report.artifacts = [...tracked].map(file => ({ file, sha256: hash(file) }));
  const result = `${directory}/results.json`;
  fs.writeFileSync(absolute(result), JSON.stringify(report, null, 2) + '\n');
  verifyConstantSelection(result);
  return report;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, ...extra] = process.argv.slice(2);
    assert(file && !extra.length && ['--capture', '--verify', '--finalize'].includes(mode),
      'Usage: constant-selection-evidence.mjs --capture NEW_DIRECTORY | --verify RS01.json | --finalize RS01.json');
    const report = mode === '--capture' ? captureConstantSelection(file) :
      mode === '--finalize' ? finalize(file) : verifyConstantSelection(file);
    console.log(JSON.stringify({ status: report.status, summary: report.summary, red_square: report.red_square }, null, 2));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
