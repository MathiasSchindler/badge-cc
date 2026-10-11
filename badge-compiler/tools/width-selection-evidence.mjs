import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { verifyCrossFunction, verifyLoadedImage } from './cross-function-evidence.mjs';
import { parseAssembly } from './benchmark-c06.mjs';
import { decodeElf } from './boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const codeBytes = file => decodeElf(fs.readFileSync(file)).sections.filter(s => s.flags & 4)
  .reduce((n, s) => n + s.size, 0);
const policy = { control: 'thumb-operands', candidate: 'thumb-widths', registers: 'global-compact',
  frame: 'direct', memory: 'word', ir: 'identical verified PO16 output',
  promotion: 'size candidate; speed and XIP timing not physically qualified' };
function allocation(text) {
  return [...text.matchAll(/^@ global-allocation:.*$/gm)].map(m => m[0]);
}
function effects(text) {
  return [...text.matchAll(/^@ assembly-effects:.*$/gm)].map(m => m[0]);
}
function compare(beforeFile, afterFile) {
  const before = fs.readFileSync(beforeFile, 'utf8'), after = fs.readFileSync(afterFile, 'utf8');
  assert.deepEqual(allocation(after), allocation(before), 'allocation changed');
  assert.deepEqual(effects(after), effects(before), 'assembly effects changed');
  assert(!after.includes('@ local-scheduling:'), 'unqualified scheduling');
  const a = parseAssembly(before, 'PO17'), b = parseAssembly(after, 'PO17');
  assert.deepEqual([...b.keys()], [...a.keys()]);
  return [...a].map(([name, x]) => {
    const y = b.get(name);
    for (const field of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
      assert(Number.isInteger(x[field]) && x[field] === y[field], `${name}: ${field} changed`);
    assert.deepEqual(y.calls, x.calls);
    assert(!y.unknown.includes('unverified_frame_balance'));
    return { name, frame: y.frame_bytes, saves: y.saves_bytes, outgoing: y.max_outgoing_bytes, calls: y.calls };
  });
}
function summary(report) {
  return { modules: report.modules.length, functions: report.modules.reduce((n, m) => n + m.functions.length, 0),
    changed_modules: report.modules.filter(m => m.changed).length,
    ...(report.runtime_tests !== undefined ? { compiled_modules: report.modules.filter(m => !m.reused).length } : {}),
    module_code_bytes_saved: report.modules.reduce((n, m) => n + m.before_bytes - m.after_bytes, 0),
    relinked_images: report.images.filter(i => i.relinked && !i.reused).length,
    image_bytes_saved: Object.fromEntries(report.images.map(i => [i.image, i.before_bytes - i.after_bytes])) };
}
export function verifyWidthSelection(file, current = false) {
  const report = read(file);
  assert.equal(report.milestone, 'PO17');
  assert.equal(report.status, 'qualified-host-size-candidate');
  assert.equal(report.default_changed, false);
  assert.deepEqual(report.policy, policy);
  assert.equal(hash(absolute(report.baseline)), report.baseline_sha256);
  const baseline = verifyCrossFunction(absolute(report.baseline));
  let reused;
  if (report.reuse) {
    assert.notEqual(absolute(file), absolute(report.reuse.report));
    assert.equal(hash(absolute(report.reuse.report)), report.reuse.sha256);
    reused = verifyWidthSelection(absolute(report.reuse.report));
    assert.equal(reused.baseline_sha256, report.baseline_sha256);
  }
  const tracked = new Set(report.artifacts.map(a => a.file));
  assert.equal(tracked.size, report.artifacts.length);
  for (const a of report.artifacts) assert.equal(hash(absolute(a.file)), a.sha256, a.file);
  assert(report.commands.every(c => c.status === 0));
  assert.equal(report.modules.length, baseline.modules.length);
  for (const m of report.modules) {
    const old = baseline.modules.find(b => b.module === m.module);
    assert(old);
    assert.equal(m.ir, old.ir);
    assert.equal(m.ir_sha256, hash(absolute(old.ir)));
    assert(tracked.has(m.assembly));
    assert.equal(m.before_bytes, codeBytes(absolute(old.object)));
    assert.equal(m.after_bytes, codeBytes(absolute(m.object)));
    assert(m.after_bytes <= m.before_bytes, m.module);
    assert.equal(m.changed, hash(absolute(m.object)) !== hash(absolute(old.object)));
    assert.deepEqual(m.functions, compare(absolute(old.assembly), absolute(m.assembly)));
    if (m.reused) {
      assert(reused);
      const prior = reused.modules.find(b => b.module === m.module);
      for (const key of ['assembly', 'object']) assert.equal(hash(absolute(m[key])), hash(absolute(prior[key])));
    }
    if (m.changed) assert(tracked.has(m.object));
    else assert.equal(m.object, old.object);
  }
  assert.equal(report.images.length, baseline.images.length);
  for (const i of report.images) {
    const old = baseline.images.find(b => b.image === i.image);
    assert(old);
    assert.equal(i.script, old.script);
    assert.equal(i.before_bytes, fs.statSync(absolute(old.binary)).size);
    assert.equal(i.after_bytes, fs.statSync(absolute(i.binary)).size);
    assert(i.after_bytes <= i.before_bytes, i.image);
    verifyLoadedImage(absolute(i.elf), absolute(i.binary), absolute(i.script));
    const a = read(absolute(old.metrics)), b = read(absolute(i.metrics));
    assert.equal(b.data, a.data); assert.equal(b.bss, a.bss);
    if (i.relinked) for (const key of ['elf', 'binary', 'metrics']) assert(tracked.has(i[key]));
    else for (const key of ['elf', 'binary', 'metrics']) assert.equal(i[key], old[key]);
    if (i.reused) {
      assert(reused);
      const prior = reused.images.find(b => b.image === i.image);
      for (const key of ['elf', 'binary', 'metrics']) assert.equal(hash(absolute(i[key])), hash(absolute(prior[key])));
    }
  }
  assert(tracked.has(report.runtime_log));
  const runtime = fs.readFileSync(absolute(report.runtime_log), 'utf8');
  const count = report.runtime_tests ?? 5;
  assert(Number.isInteger(count) && count >= 5);
  assert.match(runtime, new RegExp(`# tests ${count}\\b`));
  assert.match(runtime, /# fail 0\b/);
  assert(tracked.has(report.boundary_log));
  const boundary = fs.readFileSync(absolute(report.boundary_log), 'utf8');
  assert.match(boundary, /# tests 2\b/);
  assert.match(boundary, /# fail 0\b/);
  for (const workload of ['USB', 'WiFi']) {
    const match = runtime.match(new RegExp(`PO17 ${workload}: bytes (\\d+) -> (\\d+); reached instructions (\\d+) -> (\\d+)`));
    assert(match && Number(match[2]) < Number(match[1]) && Number(match[4]) <= Number(match[3]) + 1);
  }
  assert.deepEqual(report.summary, summary(report));
  assert(report.summary.module_code_bytes_saved > 0);
  assert(Object.values(report.summary.image_bytes_saved).filter(n => n > 0).length >= 2);
  if (current) for (const s of report.sources) assert.equal(hash(absolute(s.file)), s.sha256, s.file);
  return report;
}
export function captureWidthSelection(directory, baselineFile, reuseFile) {
  assert(!fs.existsSync(directory), 'capture requires a fresh directory');
  const baseline = verifyCrossFunction(baselineFile);
  const reused = reuseFile ? verifyWidthSelection(reuseFile) : null;
  const po15 = read(absolute(baseline.baseline)), po14 = read(absolute(po15.baseline));
  fs.mkdirSync(directory, { recursive: true });
  const tracked = new Set();
  const out = name => { const file = path.join(directory, name); tracked.add(file); return file; };
  const report = { schema: 1, milestone: 'PO17', status: 'capturing', default_changed: false, policy, runtime_tests: 6,
    baseline: relative(baselineFile), baseline_sha256: hash(baselineFile),
    modules: [], images: [], commands: [], sources: [], artifacts: [] };
  if (reused) {
    assert.equal(reused.baseline_sha256, report.baseline_sha256);
    report.reuse = { report: relative(reuseFile), sha256: hash(reuseFile),
      contract: 'Regenerate assembly; reuse only hash-verified byte-identical assembly with the same assembler and image tools.' };
    for (const name of ['badge-asm', 'badge-link', 'elf2bin', 'badge-elf-info']) {
      const prior = reused.artifacts.find(a => a.file.endsWith(`/tools/${name}`));
      assert(prior);
      assert.equal(hash(absolute(`badge-compiler/build/${name}`)), prior.sha256);
    }
  }
  const run = (command, args) => {
    const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
    const log = out(`${report.commands.length}.log`);
    fs.writeFileSync(log, r.stdout + r.stderr);
    report.commands.push({ executable: path.isAbsolute(command) ? relative(command) : command, args,
      log: relative(log), status: r.status });
    assert.ifError(r.error); assert.equal(r.status, 0, r.stderr); return r.stdout;
  };
  const tools = {};
  fs.mkdirSync(path.join(directory, 'tools'));
  for (const name of ['badge-codegen', 'badge-asm', 'badge-link', 'elf2bin', 'badge-elf-info']) {
    tools[name] = out(`tools/${name}`);
    fs.copyFileSync(absolute(`badge-compiler/build/${name}`), tools[name]);
  }
  for (const subdir of ['src', 'tools', 'tests']) {
    for (const name of fs.readdirSync(absolute(`badge-compiler/${subdir}`))) {
      if (!/\.(c|h|mjs)$/.test(name)) continue;
      const file = `badge-compiler/${subdir}/${name}`;
      report.sources.push({ file, sha256: hash(absolute(file)) });
    }
  }
  for (const file of ['badge-compiler/Makefile', 'badge-compiler/scripts/build-owned.sh',
    'badge-compiler/scripts/owned-pipeline.sh', 'badge-compiler/tools/oracle.sh',
    'src/shared/usb_report.c', 'src/tools/diagnostics_logic.c', 'src/radio/wifi_link.c', 'src/shared/bytes.h'])
    report.sources.push({ file, sha256: hash(absolute(file)) });
  const save = () => {
    report.artifacts = [...tracked].filter(fs.existsSync).map(file => ({ file: relative(file), sha256: hash(file) }));
    fs.writeFileSync(path.join(directory, 'results.json'), JSON.stringify(report, null, 2) + '\n');
  };
  try {
    run(process.execPath, ['--test', '--test-reporter=tap', 'badge-compiler/tests/width-selection.test.mjs']);
    report.runtime_log = report.commands.at(-1).log;
    run(process.execPath, ['--test', '--test-reporter=tap',
      '--test-name-pattern=final branch widths|literal PC alignment', 'badge-compiler/tests/final_layout_test.mjs']);
    report.boundary_log = report.commands.at(-1).log;
    const replacements = new Map();
    for (const old of baseline.modules) {
      const assembly = out(`${old.module}.s`), candidate = out(`${old.module}.o`);
      run(tools['badge-codegen'], ['--frame-addressing=direct', '--register-residency=global-compact',
        '--scalar-selection=thumb-widths', '--memory-selection=word', absolute(old.ir), '-o', assembly]);
      const prior = reused?.modules.find(m => m.module === old.module &&
        hash(absolute(m.assembly)) === hash(assembly));
      if (prior) fs.copyFileSync(absolute(prior.object), candidate);
      else run(tools['badge-asm'], [assembly, '-o', candidate]);
      const changed = hash(candidate) !== hash(absolute(old.object));
      const object = changed ? candidate : absolute(old.object);
      if (changed) replacements.set(absolute(old.object), object);
      report.modules.push({ module: old.module, ir: old.ir, ir_sha256: hash(absolute(old.ir)),
        changed, reused: Boolean(prior), assembly: relative(assembly), object: relative(object), before_bytes: old.after_bytes,
        after_bytes: codeBytes(object), functions: compare(absolute(old.assembly), assembly) });
      assert(codeBytes(object) <= old.after_bytes, `${old.module}: executable bytes grew`);
    }
    for (const old of baseline.images) {
      const origin = po14.images.find(i => i.image === old.image);
      const command = po14.commands.find(c => c.executable.endsWith('/badge-link') &&
        c.args.at(-1) === absolute(origin.candidate.image));
      assert(command, `missing archived link command: ${old.image}`);
      let relinked = false;
      const args = command.args.map(a => {
        const original = po14.modules.find(m => absolute(m.candidate.object) === a);
        if (!original) return a;
        const input = absolute(baseline.modules.find(m => m.module === original.module).object);
        if (replacements.has(input)) relinked = true;
        return replacements.get(input) ?? input;
      });
      let elf = absolute(old.elf), binary = absolute(old.binary), metrics = absolute(old.metrics);
      const prior = reused?.images.find(i => i.image === old.image && i.relinked);
      const priorCommand = prior && reused.commands.find(c => c.executable.endsWith('/badge-link') &&
        c.args.at(-1) === absolute(prior.elf));
      const reuseImage = Boolean(priorCommand && args.length === priorCommand.args.length &&
        args.slice(0, -2).every((a, k) => a === priorCommand.args[k] ||
          a.endsWith('.o') && priorCommand.args[k].endsWith('.o') &&
          hash(a) === hash(priorCommand.args[k])));
      if (relinked) {
        elf = out(`${old.image}.elf`); binary = out(`${old.image}.bin`); metrics = out(`${old.image}.metrics.json`);
        if (reuseImage) {
          fs.copyFileSync(absolute(prior.elf), elf);
          fs.copyFileSync(absolute(prior.binary), binary);
          fs.copyFileSync(absolute(prior.metrics), metrics);
        } else {
          args[args.length - 1] = elf;
          run(tools['badge-link'], args);
          run(tools.elf2bin, ['-O', 'binary', elf, binary]);
          fs.writeFileSync(metrics, run(tools['badge-elf-info'], ['--metrics', elf]));
        }
      }
      report.images.push({ image: old.image, relinked, reused: reuseImage, elf: relative(elf), binary: relative(binary),
        metrics: relative(metrics), script: old.script, before_bytes: fs.statSync(absolute(old.binary)).size,
        after_bytes: fs.statSync(binary).size });
    }
    report.summary = summary(report); report.status = 'qualified-host-size-candidate'; save();
    return verifyWidthSelection(path.join(directory, 'results.json'), true);
  } catch (error) { report.status = 'failed'; report.error = error.message; save(); throw error; }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, baseline, reuse, ...extra] = process.argv.slice(2);
    assert(!extra.length && file && (mode === '--capture' && baseline ||
      ['--verify', '--verify-current'].includes(mode) && !baseline && !reuse),
    'Usage: width-selection-evidence.mjs --capture NEW_DIRECTORY PO16.json [PRIOR_PO17.json] | --verify[-current] PO17.json');
    const report = mode === '--capture' ? captureWidthSelection(absolute(file), absolute(baseline),
      reuse ? absolute(reuse) : undefined) :
      verifyWidthSelection(absolute(file), mode === '--verify-current');
    console.log(`PASS: PO17 ${JSON.stringify(report.summary)}`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
