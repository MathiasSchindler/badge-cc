import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { verifyAssemblyEffects } from './assembly-effects-evidence.mjs';
import { parseAssembly } from './benchmark-c06.mjs';
import { decodeElf, auditBudgets } from './boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const bytes = file => decodeElf(fs.readFileSync(file)).sections.filter(s => s.flags & 4)
  .reduce((n, s) => n + s.size, 0);
function loadedImage(elfFile, binaryFile) {
  const file = fs.readFileSync(elfFile), elf = decodeElf(file), binary = fs.readFileSync(binaryFile);
  let end = 0;
  for (const s of elf.segments.filter(s => s.fileSize && s.load >= 0x10000000 && s.load < 0x10400000)) {
    const offset = s.load - 0x10000000;
    assert.deepEqual(binary.subarray(offset, offset + s.fileSize), file.subarray(s.offset, s.offset + s.fileSize));
    end = Math.max(end, offset + s.fileSize);
  }
  assert.equal(binary.length, end);
  return elf;
}
function compare(before, after, module) {
  const a = parseAssembly(fs.readFileSync(before, 'utf8'), module);
  const b = parseAssembly(fs.readFileSync(after, 'utf8'), module);
  assert.deepEqual([...a.keys()], [...b.keys()]);
  return [...a].map(([name, x]) => {
    const y = b.get(name);
    assert(y.frame_bytes <= x.frame_bytes && y.saves_bytes <= x.saves_bytes, `${name}: allocation growth`);
    assert.deepEqual(y.calls, x.calls);
    assert.equal(y.max_outgoing_bytes, x.max_outgoing_bytes);
    assert(!y.unknown.includes('unverified_frame_balance'));
    return { name, before_frame: x.frame_bytes, after_frame: y.frame_bytes,
      before_saves: x.saves_bytes, after_saves: y.saves_bytes };
  });
}
function summary(report) {
  return {
    modules: report.modules.length,
    changed_ir_modules: report.modules.filter(m => m.changed).length,
    compiled_modules: report.modules.filter(m => m.changed).length,
    functions: report.modules.reduce((n, m) => n + m.functions.length, 0),
    module_code_bytes_saved: report.modules.reduce((n, m) => n + m.before_bytes - m.after_bytes, 0),
    relinked_images: report.images.filter(i => i.relinked).length,
    image_bytes_saved: Object.fromEntries(report.images.map(i => [i.image, i.before_bytes - i.after_bytes])),
    events: report.modules.flatMap(m => read(absolute(m.events)).events)
      .reduce((rows, e) => {
        const key = `${e.event}:${e.reason}`;
        rows[key] = (rows[key] ?? 0) + e.count;
        return rows;
      }, {}),
  };
}
export function verifyLoopScalar(file, current = false) {
  const report = read(file);
  assert.equal(report.milestone, 'PO15');
  assert.equal(report.status, 'qualified-host-candidate');
  assert.equal(report.default_changed, false);
  assert.deepEqual(report.policy, { pass: 'loop-scalar', backend: 'direct/global-compact/thumb-operands/word' });
  const baseline = verifyAssemblyEffects(absolute(report.baseline));
  assert.equal(baseline.milestone, 'PO14');
  assert.equal(hash(absolute(report.baseline)), report.baseline_sha256);
  const tracked = new Set(report.artifacts.map(a => a.file));
  assert.equal(tracked.size, report.artifacts.length);
  for (const a of report.artifacts) assert.equal(hash(absolute(a.file)), a.sha256, a.file);
  assert(report.commands.every(c => c.status === 0));
  assert.equal(report.modules.length, baseline.modules.length);
  for (const m of report.modules) {
    const old = baseline.modules.find(b => b.module === m.module);
    assert(old);
    for (const key of ['ir', 'events']) assert(tracked.has(m[key]));
    assert.equal(m.changed, hash(absolute(m.ir)) !== hash(absolute(old.ir)));
    assert.equal(m.before_bytes, bytes(absolute(old.candidate.object)));
    assert.equal(m.after_bytes, bytes(absolute(m.object)));
    assert(m.after_bytes <= m.before_bytes);
    if (m.changed) {
      assert(tracked.has(m.assembly) && tracked.has(m.object));
    } else {
      assert.equal(m.assembly, old.candidate.assembly);
      assert.equal(m.object, old.candidate.object);
    }
    assert.deepEqual(m.functions, compare(absolute(old.candidate.assembly), absolute(m.assembly), m.module));
  }
  assert.equal(report.images.length, baseline.images.length);
  for (const i of report.images) {
    const old = baseline.images.find(b => b.image === i.image);
    assert(old);
    assert.equal(i.before_bytes, fs.statSync(absolute(old.candidate.binary)).size);
    assert.equal(i.after_bytes, fs.statSync(absolute(i.binary)).size);
    assert(i.after_bytes <= i.before_bytes);
    auditBudgets(loadedImage(absolute(i.elf), absolute(i.binary)), fs.readFileSync(absolute(old.script), 'utf8'));
    const metrics = read(absolute(i.metrics));
    assert.equal(metrics.data, old.candidate.data);
    assert.equal(metrics.bss, old.candidate.bss);
    if (i.relinked) for (const key of ['elf', 'binary', 'metrics']) assert(tracked.has(i[key]));
    else assert.equal(i.elf, old.candidate.image);
  }
  assert(tracked.has(report.runtime_log));
  const runtime = fs.readFileSync(absolute(report.runtime_log), 'utf8');
  assert.match(runtime, /(?:#|\u2139) fail 0/);
  assert.match(runtime, /(?:#|\u2139) tests 8/);
  assert.match(runtime, /PO15 production fill oracle: \d+ -> \d+ reached instructions/);
  assert.deepEqual(report.summary, summary(report));
  assert(report.summary.module_code_bytes_saved > 0, 'no actual production win');
  if (current) for (const s of report.sources) assert.equal(hash(absolute(s.file)), s.sha256, s.file);
  return report;
}
export function captureLoopScalar(directory, baselineFile) {
  assert(!fs.existsSync(directory), 'capture requires a fresh directory');
  const baseline = verifyAssemblyEffects(baselineFile);
  assert.equal(baseline.milestone, 'PO14');
  fs.mkdirSync(directory, { recursive: true });
  const tracked = new Set();
  const out = name => {
    const file = path.join(directory, name);
    tracked.add(file);
    return file;
  };
  const report = { schema: 1, milestone: 'PO15', status: 'capturing', default_changed: false,
    policy: { pass: 'loop-scalar', backend: 'direct/global-compact/thumb-operands/word' },
    baseline: relative(baselineFile), baseline_sha256: hash(baselineFile),
    modules: [], images: [], commands: [], sources: [], artifacts: [] };
  const run = (command, args) => {
    const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
    const log = out(`${report.commands.length}.log`);
    fs.writeFileSync(log, r.stdout + r.stderr);
    report.commands.push({ executable: relative(command), args, log: relative(log), status: r.status });
    assert.ifError(r.error);
    assert.equal(r.status, 0, r.stderr);
    return r.stdout;
  };
  const tools = {};
  fs.mkdirSync(path.join(directory, 'tools'));
  for (const name of ['badge-ir-metrics', 'badge-codegen', 'badge-asm', 'badge-link', 'elf2bin', 'badge-elf-info']) {
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
  const save = () => {
    report.artifacts = [...tracked].filter(fs.existsSync).map(file => ({ file: relative(file), sha256: hash(file) }));
    fs.writeFileSync(path.join(directory, 'results.json'), JSON.stringify(report, null, 2) + '\n');
  };
  try {
    run(process.execPath, ['--test', 'badge-compiler/tests/loop-scalar.test.mjs']);
    report.runtime_log = report.commands.at(-1).log;
    const replacements = new Map();
    for (const old of baseline.modules) {
      const ir = out(`${old.module}.ll`), events = out(`${old.module}.events.json`);
      run(tools['badge-ir-metrics'], ['--passes=loop-scalar', `--emit-ir=${ir}`, absolute(old.ir), '-o', events]);
      const changed = hash(ir) !== hash(absolute(old.ir));
      let assembly = absolute(old.candidate.assembly), object = absolute(old.candidate.object);
      if (changed) {
        assembly = out(`${old.module}.s`);
        object = out(`${old.module}.o`);
        run(tools['badge-codegen'], ['--frame-addressing=direct', '--register-residency=global-compact',
          '--scalar-selection=thumb-operands', '--memory-selection=word', ir, '-o', assembly]);
        run(tools['badge-asm'], [assembly, '-o', object]);
        replacements.set(absolute(old.candidate.object), object);
      }
      report.modules.push({ module: old.module, changed, ir: relative(ir), events: relative(events),
        assembly: relative(assembly), object: relative(object), before_bytes: old.candidate.code_bytes,
        after_bytes: bytes(object), functions: compare(absolute(old.candidate.assembly), assembly, old.module) });
    }
    for (const old of baseline.images) {
      const command = baseline.commands.find(c => c.executable.endsWith('/badge-link') &&
        c.args.at(-1) === absolute(old.candidate.image));
      assert(command, `missing archived link command: ${old.image}`);
      const relinked = command.args.some(a => replacements.has(a));
      let elf = absolute(old.candidate.image), binary = absolute(old.candidate.binary), metrics = absolute(old.candidate.metrics);
      if (relinked) {
        elf = out(`${old.image}.elf`); binary = out(`${old.image}.bin`); metrics = out(`${old.image}.metrics.json`);
        const args = command.args.map(a => replacements.get(a) ?? a);
        args[args.length - 1] = elf;
        run(tools['badge-link'], args);
        run(tools.elf2bin, ['-O', 'binary', elf, binary]);
        fs.writeFileSync(metrics, run(tools['badge-elf-info'], ['--metrics', elf]));
      }
      report.images.push({ image: old.image, relinked, elf: relative(elf), binary: relative(binary),
        metrics: relative(metrics), before_bytes: fs.statSync(absolute(old.candidate.binary)).size,
        after_bytes: fs.statSync(binary).size });
    }
    report.summary = summary(report);
    report.status = 'qualified-host-candidate';
    save();
    return verifyLoopScalar(path.join(directory, 'results.json'), true);
  } catch (error) {
    report.status = 'failed'; report.error = error.message; save();
    throw error;
  }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, baseline, ...extra] = process.argv.slice(2);
    assert(!extra.length && file && (mode === '--capture' && baseline ||
      ['--verify', '--verify-current'].includes(mode) && !baseline),
    'Usage: loop-scalar-evidence.mjs --capture NEW_DIRECTORY PO14.json | --verify[-current] PO15.json');
    const report = mode === '--capture' ? captureLoopScalar(absolute(file), absolute(baseline)) :
      verifyLoopScalar(absolute(file), mode === '--verify-current');
    console.log(`PASS: PO15 ${JSON.stringify(report.summary)}`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
