import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly } from './benchmark-c06.mjs';
import { verifySmallFunctionReport } from './small-function-attribution.mjs';
import { decodeElf, auditBudgets } from './boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const relative = file => path.relative(root, file);
const absolute = file => path.resolve(root, file);
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const codeBytes = file => decodeElf(fs.readFileSync(file)).sections
  .filter(s => s.flags & 4).reduce((n, s) => n + s.size, 0);
const totals = text => Object.fromEntries(['asm-spills', 'fused'].map(field => [field,
  [...text.matchAll(new RegExp(`${field} (\\d+)`, 'g'))].reduce((sum, m) => sum + Number(m[1]), 0)]));
const spills = text => [...text.matchAll(/global-allocation: allocated \d+ spilled (\d+)/g)]
  .reduce((sum, m) => sum + Number(m[1]), 0);
const operandTotals = text => ({
  direct_memory: [...text.matchAll(/@ operand-memory: direct 1/g)].length,
  reused_constants: [...text.matchAll(/@ constant-reuse: saved \d+/g)].length,
  constant_bytes_saved: [...text.matchAll(/@ constant-reuse: saved (\d+)/g)]
    .reduce((n, m) => n + Number(m[1]), 0),
});
function functions(before, after, name) {
  const a = parseAssembly(before, name), b = parseAssembly(after, name);
  assert.deepEqual([...a.keys()], [...b.keys()], 'function inventory drift');
  return [...a].map(([name, x]) => {
    const y = b.get(name);
    assert.deepEqual(y.calls, x.calls, `${name}: calls changed`);
    assert(y.saves_bytes <= x.saves_bytes, `${name}: saves grew`);
    assert.equal(y.max_outgoing_bytes, x.max_outgoing_bytes, `${name}: outgoing area changed`);
    assert(Number.isInteger(x.frame_bytes) && Number.isInteger(y.frame_bytes), 'missing frame facts');
    assert(y.frame_bytes <= x.frame_bytes, `${name}: frame grew`);
    return { name, before_frame: x.frame_bytes, after_frame: y.frame_bytes, saved_bytes: y.saves_bytes };
  });
}
export function verifyAssemblyEffects(file, current = false) {
  const report = read(file);
  assert.equal(report.status, 'qualified-host-candidate');
  assert(['PO11', 'PO12', 'PO13', 'PO14'].includes(report.milestone));
  assert.equal(report.default_changed, false);
  assert.equal(report.modules.length, 37);
  assert.equal(report.images.length, 12);
  const operands = report.milestone === 'PO14';
  const control = report.milestone === 'PO13' || operands;
  const frames = report.milestone !== 'PO11';
  if (operands && report.control_origin !== undefined) {
    assert.equal(report.control_origin, 'verified qualified baseline artifacts');
    assert(!report.commands.some(row => row.args.includes('--scalar-selection=thumb-control')),
      'archived control marked as recompiled');
  }
  assert.deepEqual(report.policies, { control: operands ? 'thumb-control' : control ? 'thumb-frames' : frames ? 'thumb-effects' : 'thumb-calls', candidate: operands ? 'thumb-operands' : control ? 'thumb-control' : frames ? 'thumb-frames' : 'thumb-effects',
    registers: 'global-compact', frame: 'direct', memory: 'word',
    ir: 'identical archived promoted optimized IR' });
  const tracked = new Set(report.artifacts.map(a => a.file));
  assert.equal(tracked.size, report.artifacts.length, 'duplicate artifacts');
  for (const artifact of report.artifacts)
    assert.equal(hash(absolute(artifact.file)), artifact.sha256, artifact.file);
  for (const module of report.modules) {
    const before = fs.readFileSync(absolute(module.control.assembly), 'utf8');
    const after = fs.readFileSync(absolute(module.candidate.assembly), 'utf8');
    assert.deepEqual(module.functions, functions(before, after, module.module));
    assert.deepEqual(module.control.totals, totals(before));
    assert.deepEqual(module.candidate.totals, totals(after));
    if (operands) {
      assert.deepEqual(module.candidate.operands, operandTotals(after));
      assert.deepEqual(module.control.operands, operandTotals(before));
    }
    assert(spills(after) <= spills(before), 'allocation spills grew');
    if (frames) {
      assert(!/movw r12, #0\n(?:sub|add)\.w sp, sp, r12/.test(after), 'zero-byte SP adjustment');
      assert(!after.includes('@ frame-incoming:'), 'unresolved incoming argument offset');
      for (const fn of parseAssembly(after, module.module).values())
        assert(!fn.unknown.includes('unverified_frame_balance'), 'unverified lightweight frame balance');
    }
    for (const role of ['control', 'candidate']) {
      for (const key of ['assembly', 'object']) assert(tracked.has(module[role][key]), `untracked ${key}`);
      assert.equal(module[role].code_bytes, codeBytes(absolute(module[role].object)), 'object byte accounting differs');
    }
    for (const key of ['ir', 'source_snapshot']) assert(tracked.has(module[key]), `untracked ${key}`);
    assert(module.candidate.totals['asm-spills'] <= module.control.totals['asm-spills']);
    assert(module.candidate.code_bytes <= module.control.code_bytes);
    assert.equal(hash(absolute(module.control.object)), module.archived_object_sha256, 'baseline emission drift');
  }
  for (const image of report.images) {
    assert(tracked.has(image.script) && tracked.has(image.archived_binary), 'untracked image input');
    for (const role of ['control', 'candidate']) {
      const record = image[role];
      for (const key of ['image', 'binary', 'metrics']) assert(tracked.has(record[key]), `untracked image ${key}`);
      const metrics = read(absolute(record.metrics));
      for (const field of ['text', 'data', 'bss']) assert.equal(record[field], metrics[field], 'image accounting differs');
      auditBudgets(decodeElf(fs.readFileSync(absolute(record.image))), fs.readFileSync(absolute(image.script), 'utf8'));
    }
    assert.equal(image.control.data, image.candidate.data);
    assert.equal(image.control.bss, image.candidate.bss);
    assert(image.candidate.text <= image.control.text);
    assert.deepEqual(fs.readFileSync(absolute(image.control.binary)),
      fs.readFileSync(absolute(image.archived_binary)), 'baseline loaded bytes drift');
  }
  const expected = {
    functions: report.modules.reduce((n, m) => n + m.functions.length, 0),
    removed_asm_spills: report.modules.reduce((n, m) => n + m.control.totals['asm-spills'] - m.candidate.totals['asm-spills'], 0),
    additional_fused_comparisons: report.modules.reduce((n, m) => n + m.candidate.totals.fused - m.control.totals.fused, 0),
    changed_modules: report.modules.filter(m => m.candidate.code_bytes !== m.control.code_bytes).map(m => m.module),
  };
  if (operands) expected.operands = Object.fromEntries(Object.keys(operandTotals(''))
    .map(key => [key, report.modules.reduce((n, m) => n + m.candidate.operands[key], 0)]));
  assert.deepEqual(report.summary, expected);
  assert.equal(expected.functions, 554);
  assert(frames ? expected.changed_modules.length > 0 : expected.removed_asm_spills > 0, 'no production benefit');
  if (current) for (const source of report.sources)
    assert.equal(hash(absolute(source.file)), source.sha256, `current source/tool drift: ${source.file}`);
  return report;
}
export function captureAssemblyEffects(directory, attribution, options = {}) {
  const reportFile = path.join(directory, 'results.json');
  const previous = options.resume ? read(reportFile) : null;
  if (!previous) fs.mkdirSync(directory);
  const operands = options.operands === true;
  const control = options.control === true || operands;
  const frames = options.frames === true || control;
  const report = { schema: 1, milestone: operands ? 'PO14' : control ? 'PO13' : frames ? 'PO12' : 'PO11', status: 'in-progress', default_changed: false,
    policies: { control: operands ? 'thumb-control' : control ? 'thumb-frames' : frames ? 'thumb-effects' : 'thumb-calls', candidate: operands ? 'thumb-operands' : control ? 'thumb-control' : frames ? 'thumb-frames' : 'thumb-effects', registers: 'global-compact',
      frame: 'direct', memory: 'word', ir: 'identical archived promoted optimized IR' },
    modules: [], images: [], artifacts: [], sources: [], commands: [],
    limitations: ['Host correctness and static qualification, not hardware timing or default promotion.',
      'NOP counts and memory/volatile ordering remain intact; reduced spill/branch overhead can shorten hardware waits.',
      'Display delay/reset/sleep-out, LCD strobe and USB ownership-settling timing require physical qualification before promotion.'] };
  if (operands && (!previous || previous.control_origin))
    report.control_origin = 'verified qualified baseline artifacts';
  const save = () => fs.writeFileSync(reportFile, JSON.stringify(report, null, 2) + '\n');
  const tracked = new Set();
  if (previous) {
    assert.equal(previous.status, 'failed', 'only a failed capture can resume');
    assert.deepEqual(previous.policies, report.policies, 'resume policy drift');
    assert.equal(previous.milestone, report.milestone, 'resume milestone drift');
    for (const artifact of previous.artifacts) {
      assert.equal(hash(absolute(artifact.file)), artifact.sha256, `resume artifact drift: ${artifact.file}`);
      tracked.add(absolute(artifact.file));
    }
    const updates = [];
    const allowed = new Set(['badge-compiler/tools/assembly-effects-evidence.mjs',
      'badge-compiler/tools/benchmark-c06.mjs', 'badge-compiler/tools/operand-selection-evidence.mjs',
      'badge-compiler/tests/operand-selection.test.mjs']);
    for (const source of previous.sources) {
      const now = hash(absolute(source.file));
      if (now === source.sha256) continue;
      assert(allowed.has(source.file), `resume emission/source drift: ${source.file}`);
      updates.push({ file: source.file, previous_sha256: source.sha256, current_sha256: now });
    }
    const archive = path.join(directory, 'results-before-resume.json');
    fs.copyFileSync(reportFile, archive, fs.constants.COPYFILE_EXCL);
    tracked.add(archive);
    report.resume = { original_report: relative(archive), source_updates: updates, reused_commands: 0,
      reason: 'Recompute inspector/accounting with unchanged hashed emission inputs and artifacts; reuse completed successful commands.' };
    report.commands = [...previous.commands];
  }
  const track = file => { tracked.add(path.resolve(file)); return file; };
  const out = name => track(path.join(directory, name));
  const copy = (input, destination, expected) => {
    if (expected) assert.equal(hash(input), expected, `input drift: ${input}`);
    if (previous && fs.existsSync(destination)) {
      assert(tracked.has(path.resolve(destination)), `untracked resume input: ${destination}`);
      assert.equal(hash(input), hash(destination), `resume input drift: ${input}`);
      return destination;
    }
    fs.copyFileSync(input, destination);
    track(destination);
    return destination;
  };
  const run = (command, args) => {
    const completed = previous?.commands.find(row => row.status === 0 &&
      row.executable === relative(command) && JSON.stringify(row.args) === JSON.stringify(args));
    if (completed) {
      const output = args[args.indexOf('-o') + 1];
      assert(args.includes('-o') && tracked.has(path.resolve(output)), 'untracked resumed command output');
      report.resume.reused_commands++;
      return fs.readFileSync(absolute(completed.log), 'utf8');
    }
    const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000,
      maxBuffer: 32 * 1024 * 1024 });
    const log = out(`${report.commands.length}.log`);
    fs.writeFileSync(log, `${r.stdout ?? ''}${r.stderr ?? ''}${r.error ?? ''}`);
    report.commands.push({ executable: relative(command), args, log: relative(log), status: r.status });
    save();
    assert.ifError(r.error);
    assert.equal(r.status, 0, `${command}: ${r.stderr}`);
    return r.stdout;
  };
  save();
  try {
    const prior = verifySmallFunctionReport(attribution);
    copy(attribution, out('po10.json'));
    if (frames) copy(absolute(options.effectsReport), out('po11.json'));
    if (!previous) fs.mkdirSync(path.join(directory, 'tools'));
    const tools = {};
    for (const name of ['badge-codegen', 'badge-asm', 'badge-link', 'badge-elf-info', 'elf2bin']) {
      const source = absolute(`badge-compiler/build/${name}`);
      tools[name] = copy(source, path.join(directory, 'tools', name));
      report.sources.push({ file: relative(source), sha256: hash(source) });
    }
    for (const file of ['badge-compiler/Makefile', 'badge-compiler/tools/assembly-effects-evidence.mjs',
      ...(frames ? ['badge-compiler/tools/lightweight-frame-evidence.mjs', 'badge-compiler/tests/lightweight-frames.test.mjs'] : []),
      ...(control ? ['badge-compiler/tools/control-selection-evidence.mjs', 'badge-compiler/tests/control-selection.test.mjs'] : []),
      ...(operands ? ['badge-compiler/tools/operand-selection-evidence.mjs', 'badge-compiler/tests/operand-selection.test.mjs',
        'badge-compiler/tests/benchmark_c06_test.mjs', 'badge-compiler/tests/build_owned_test.mjs',
        'badge-compiler/tests/owned_pipeline_test.sh', 'badge-compiler/tools/compiler-compare.mjs',
        'badge-compiler/tools/oracle.sh'] : []),
      'badge-compiler/tools/benchmark-c06.mjs', 'badge-compiler/tests/assembly-effects.test.mjs',
      'badge-compiler/scripts/owned-pipeline.sh', 'badge-compiler/scripts/build-owned.sh',
      ...fs.readdirSync(absolute('badge-compiler/src')).filter(n => /\.(c|h)$/.test(n))
        .map(n => `badge-compiler/src/${n}`)])
      report.sources.push({ file, sha256: hash(absolute(file)) });
    const metric = file => JSON.parse(run(tools['badge-elf-info'], ['--metrics', file]));
    const corpus = read(absolute('badge-compiler/build/po09-finished-corpus/offline.json'))
      .candidates.find(c => c.id === 'po06-po07');
    assert(corpus && corpus.modules.length === 37 && corpus.images.length === 12);
    const objects = { control: new Map(), candidate: new Map() };
    for (const module of prior.production) {
      const old = corpus.modules.find(m => m.module === module.module);
      assert(old);
      const input = copy(absolute(module.ir), out(`${module.module}.ll`), old.sha256);
      const original = absolute(old.file).replace(/\.o\.owned\/optimized\.ll$/, '.o');
      const qualified = frames ? read(absolute(options.effectsReport)).modules.find(m => m.module === module.module) : null;
      assert(!frames || qualified, 'missing PO11 baseline module');
      const reference = copy(frames ? absolute(qualified.candidate.object) : original,
        out(`${module.module}.archived.o`), frames ? hash(absolute(qualified.candidate.object)) : undefined);
      const snapshot = copy(absolute(module.source_snapshot), out(`${module.module}.source.c`));
      assert.equal(hash(absolute(module.source)), hash(snapshot), 'production source drift');
      report.sources.push({ file: module.source, sha256: hash(snapshot) });
      const record = { module: module.module, ir: relative(input), source_snapshot: relative(snapshot),
        archived_object_sha256: hash(reference) };
      const texts = [];
      for (const role of ['control', 'candidate']) {
        const assembly = out(`${module.module}.${role}.s`), object = out(`${module.module}.${role}.o`);
        if (operands && role === 'control') {
          copy(absolute(qualified.candidate.assembly), assembly);
          copy(absolute(qualified.candidate.object), object);
        } else {
          run(tools['badge-codegen'], [`--scalar-selection=${report.policies[role]}`,
            '--register-residency=global-compact', '--frame-addressing=direct', '--memory-selection=word',
            input, '-o', assembly]);
          run(tools['badge-asm'], [assembly, '-o', object]);
        }
        const text = fs.readFileSync(assembly, 'utf8');
        texts.push(text);
        record[role] = { assembly: relative(assembly), object: relative(object),
          totals: totals(text), code_bytes: codeBytes(object) };
        if (operands) record[role].operands = operandTotals(text);
        objects[role].set(original, object);
      }
      record.functions = functions(...texts, module.module);
      report.modules.push(record);
    }
    const logFile = absolute('badge-compiler/build/po09-finished-corpus/po06-po07-build.log');
    copy(logFile, out('archived-build.log'));
    const commands = fs.readFileSync(logFile, 'utf8').replace(/\\\n/g, ' ').split('\n');
    for (const old of corpus.images) {
      const line = commands.find(line => line.startsWith('bash badge-compiler/scripts/owned-cc.sh ') &&
        line.trim().endsWith(`-o ${old.file}`));
      assert(line, `missing archived link inputs: ${old.image}`);
      const tokens = line.trim().split(/\s+/);
      const inputs = tokens.filter(t => t.endsWith('.o')).map(absolute);
      assert(inputs.length && inputs.every(f => f.startsWith(path.dirname(absolute(old.file)) + path.sep)),
        'unexpected link input scope');
      const scriptToken = tokens.find(t => t.startsWith('-T'));
      assert(scriptToken && scriptToken.length > 2, 'missing linker script');
      const script = absolute(scriptToken.slice(2));
      const archivedScript = path.join(directory, path.basename(script));
      if (!tracked.has(path.resolve(archivedScript))) {
        copy(script, archivedScript);
        report.sources.push({ file: relative(script), sha256: hash(script) });
      }
      const priorImage = frames ? read(absolute(options.effectsReport)).images.find(i => i.image === old.image) : null;
      assert(!frames || priorImage, 'missing PO11 baseline image');
      const archivedBinary = copy(frames ? absolute(priorImage.candidate.binary) : absolute(old.file.replace(/\.elf$/, '.bin')),
        out(`${old.image}.archived.bin`));
      const record = { image: old.image, archived_binary: relative(archivedBinary), script: relative(archivedScript) };
      for (const role of ['control', 'candidate']) {
        const linked = inputs.map(input => {
          if (objects[role].has(input)) return objects[role].get(input);
          const destination = path.join(directory, `shared-${path.basename(input)}`);
          if (!tracked.has(path.resolve(destination))) copy(input, destination);
          return destination;
        });
        const image = out(`${old.image}.${role}.elf`), binary = out(`${old.image}.${role}.bin`);
        const metricsFile = out(`${old.image}.${role}.metrics.json`);
        let metrics;
        if (operands && role === 'control') {
          copy(absolute(priorImage.candidate.image), image);
          copy(absolute(priorImage.candidate.binary), binary);
          copy(absolute(priorImage.candidate.metrics), metricsFile);
          metrics = read(metricsFile);
        } else {
          run(tools['badge-link'], ['-T', archivedScript, ...linked, '-o', image]);
          run(tools.elf2bin, ['-O', 'binary', image, binary]);
          metrics = metric(image);
          fs.writeFileSync(metricsFile, JSON.stringify(metrics, null, 2) + '\n');
        }
        assert.equal(metrics.data, old.data);
        assert.equal(metrics.bss, old.bss);
        const end = metrics.symbols.find(s => s.name === '__flash_binary_end')?.value;
        assert(Number.isInteger(end) && end <= 0x10400000, 'flash budget exceeded');
        record[role] = { image: relative(image), binary: relative(binary),
          text: metrics.text, data: metrics.data, bss: metrics.bss, metrics: relative(metricsFile) };
      }
      report.images.push(record);
    }
    report.summary = {
      functions: report.modules.reduce((n, m) => n + m.functions.length, 0),
      removed_asm_spills: report.modules.reduce((n, m) => n + m.control.totals['asm-spills'] - m.candidate.totals['asm-spills'], 0),
      additional_fused_comparisons: report.modules.reduce((n, m) => n + m.candidate.totals.fused - m.control.totals.fused, 0),
      changed_modules: report.modules.filter(m => m.candidate.code_bytes !== m.control.code_bytes).map(m => m.module),
    };
    if (operands) report.summary.operands = Object.fromEntries(Object.keys(operandTotals(''))
      .map(key => [key, report.modules.reduce((n, m) => n + m.candidate.operands[key], 0)]));
    report.status = 'qualified-host-candidate';
    report.artifacts = [...tracked].map(file => ({ file: relative(file), sha256: hash(file) }));
    save();
    verifyAssemblyEffects(reportFile, true);
    return report;
  } catch (error) {
    report.status = 'failed';
    report.error = error.message;
    report.artifacts = [...tracked].filter(fs.existsSync).map(file => ({ file: relative(file), sha256: hash(file) }));
    save();
    throw error;
  }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, input, ...extra] = process.argv.slice(2);
    assert(!extra.length && file && (mode === '--capture' && input ||
      ['--verify', '--verify-current'].includes(mode) && !input),
    'Usage: assembly-effects-evidence.mjs --capture NEW_DIRECTORY PO10.json | --verify[-current] REPORT.json');
    const report = mode === '--capture' ? captureAssemblyEffects(path.resolve(file), absolute(input)) :
      verifyAssemblyEffects(absolute(file), mode === '--verify-current');
    console.log(`PASS: PO11 ${JSON.stringify(report.summary)}`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
