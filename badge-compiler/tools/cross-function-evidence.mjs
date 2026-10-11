import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { verifyLoopScalar } from './loop-scalar-evidence.mjs';
import { parseAssembly } from './benchmark-c06.mjs';
import { decodeElf, auditBudgets } from './boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const absolute = file => path.resolve(root, file);
const relative = file => path.relative(root, file);
const read = file => JSON.parse(fs.readFileSync(file));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const codeBytes = file => decodeElf(fs.readFileSync(file)).sections.filter(s => s.flags & 4)
  .reduce((n, s) => n + s.size, 0);
function decisions(file) {
  return [...fs.readFileSync(file, 'utf8').matchAll(
    /^; badge-cross-decision: (\S+) (\S+) (\S+) (\d+) (\d+)$/gm)]
    .map(([, kind, helper, clone, before, after]) => ({ kind, helper, clone,
      before_bytes: Number(before), after_bytes: Number(after) }));
}
function facts(file) {
  const text = fs.readFileSync(file, 'utf8'), functions = parseAssembly(text, 'PO16');
  const spills = new Map([...text.matchAll(
    /@ global-allocation: allocated \d+ spilled (\d+)[^]*?\.type ([^,]+), %function/g)]
    .map(([, n, name]) => [name, Number(n)]));
  return new Map([...functions].map(([name, f]) => [name, { frame: f.frame_bytes,
    saves: f.saves_bytes, spills: spills.get(name), outgoing: f.max_outgoing_bytes,
    calls: f.calls, unknown: f.unknown }]));
}
function compare(before, after, audit) {
  const a = facts(before), b = facts(after);
  const clones = new Map(audit.filter(d => d.clone !== '-').map(d => [d.clone, d.helper]));
  const base = name => {
    const seen = new Set();
    while (!a.has(name) && clones.has(name)) {
      assert(!seen.has(name), 'cyclic clone provenance');
      seen.add(name); name = clones.get(name);
    }
    assert(a.has(name), `unknown clone parent ${name}`);
    return name;
  };
  const rows = [...b].map(([name, y]) => {
    const origin = base(name), x = a.get(origin);
    for (const key of ['frame', 'saves', 'spills', 'outgoing']) {
      assert(Number.isInteger(x[key]) && Number.isInteger(y[key]));
      assert(y[key] <= x[key], `${name}: ${key} grew`);
    }
    assert(!y.unknown.includes('unverified_frame_balance'));
    return { name, origin, before: x, after: y };
  });
  const removed = [...a.keys()].filter(name => !b.has(name));
  for (const name of removed)
    assert(audit.some(d => d.helper === name && ['straight_line_inlined', 'tail_forwarder_inlined',
      'identical_helpers_shared'].includes(d.kind)),
      `unexplained removed helper ${name}`);
  return { functions: rows, removed };
}
function loaded(elfFile, binaryFile, script) {
  const file = fs.readFileSync(elfFile), elf = decodeElf(file), binary = fs.readFileSync(binaryFile);
  let end = 0;
  for (const s of elf.segments.filter(s => s.fileSize && s.load >= 0x10000000 && s.load < 0x10400000)) {
    const offset = s.load - 0x10000000;
    assert.deepEqual(binary.subarray(offset, offset + s.fileSize), file.subarray(s.offset, s.offset + s.fileSize));
    end = Math.max(end, offset + s.fileSize);
  }
  assert.equal(binary.length, end);
  auditBudgets(elf, fs.readFileSync(script, 'utf8'));
}
export { loaded as verifyLoadedImage };
function summary(report) {
  return {
    modules: report.modules.length,
    compiled_modules: report.modules.filter(m => m.changed && !m.reused).length,
    original_functions: report.modules.reduce((n, m) => n + m.accounting.functions.filter(f => f.name === f.origin).length +
      m.accounting.removed.length, 0),
    module_code_bytes_saved: report.modules.reduce((n, m) => n + m.before_bytes - m.after_bytes, 0),
    relinked_images: report.images.filter(i => i.relinked && !i.reused).length,
    image_bytes_saved: Object.fromEntries(report.images.map(i => [i.image, i.before_bytes - i.after_bytes])),
    events: report.modules.flatMap(m => read(absolute(m.events)).events)
      .reduce((rows, e) => {
        const key = `${e.event}:${e.reason}`; rows[key] = (rows[key] ?? 0) + e.count; return rows;
      }, {}),
  };
}
export function verifyCrossFunction(file, current = false) {
  const report = read(file);
  assert.equal(report.milestone, 'PO16');
  assert.equal(report.status, 'qualified-host-candidate');
  assert.equal(report.default_changed, false);
  assert.deepEqual(report.policy, { pass: 'cross-function', backend: 'direct/global-compact/thumb-operands/word',
    scope: 'module-local non-address-taken functions; PO15 input' });
  const baseline = verifyLoopScalar(absolute(report.baseline));
  const po14 = read(absolute(baseline.baseline));
  assert.equal(po14.milestone, 'PO14');
  assert.equal(hash(absolute(report.baseline)), report.baseline_sha256);
  const tracked = new Set(report.artifacts.map(a => a.file));
  assert.equal(tracked.size, report.artifacts.length);
  for (const a of report.artifacts) assert.equal(hash(absolute(a.file)), a.sha256, a.file);
  assert(report.commands.every(c => c.status === 0));
  if (report.reuse) {
    assert.notEqual(absolute(file), absolute(report.reuse.report));
    assert.equal(hash(absolute(report.reuse.report)), report.reuse.sha256);
  }
  const reused = report.reuse ? verifyCrossFunction(absolute(report.reuse.report)) : null;
  assert.equal(report.modules.length, baseline.modules.length);
  for (const m of report.modules) {
    const old = baseline.modules.find(b => b.module === m.module);
    assert(old);
    assert(tracked.has(m.ir) && tracked.has(m.events));
    assert.equal(m.changed, hash(absolute(m.ir)) !== hash(absolute(old.ir)));
    assert.equal(m.before_bytes, codeBytes(absolute(old.object)));
    assert.equal(m.after_bytes, codeBytes(absolute(m.object)));
    assert(m.after_bytes <= m.before_bytes);
    const audit = decisions(absolute(m.ir));
    assert.deepEqual(m.decisions, audit);
    assert(audit.length <= 8);
    assert(audit.every(d => d.after_bytes < d.before_bytes));
    assert.equal(audit.reduce((n, d) => n + d.before_bytes - d.after_bytes, 0), m.before_bytes - m.after_bytes);
    assert.deepEqual(m.accounting, compare(absolute(old.assembly), absolute(m.assembly), audit));
    if (m.changed) assert(tracked.has(m.assembly) && tracked.has(m.object));
    else { assert.equal(m.assembly, old.assembly); assert.equal(m.object, old.object); }
    if (m.reused) {
      assert(reused);
      const prior = reused.modules.find(b => b.module === m.module);
      for (const key of ['ir', 'assembly', 'object']) assert.equal(hash(absolute(m[key])), hash(absolute(prior[key])));
    }
  }
  assert.equal(report.images.length, baseline.images.length);
  for (const i of report.images) {
    const old = baseline.images.find(b => b.image === i.image);
    assert(old);
    assert.equal(i.script, po14.images.find(b => b.image === i.image).script);
    assert.equal(i.before_bytes, fs.statSync(absolute(old.binary)).size);
    assert.equal(i.after_bytes, fs.statSync(absolute(i.binary)).size);
    assert(i.after_bytes <= i.before_bytes);
    loaded(absolute(i.elf), absolute(i.binary), absolute(i.script));
    const a = read(absolute(old.metrics)), b = read(absolute(i.metrics));
    assert.equal(b.data, a.data); assert.equal(b.bss, a.bss);
    if (i.relinked) for (const key of ['elf', 'binary', 'metrics']) assert(tracked.has(i[key]));
    else { assert.equal(i.elf, old.elf); assert.equal(i.binary, old.binary); }
    if (i.reused) {
      assert(reused);
      const prior = reused.images.find(b => b.image === i.image);
      for (const key of ['elf', 'binary', 'metrics']) assert.equal(hash(absolute(i[key])), hash(absolute(prior[key])));
    }
  }
  assert(tracked.has(report.runtime_log));
  const runtime = fs.readFileSync(absolute(report.runtime_log), 'utf8');
  assert.match(runtime, /(?:#|\u2139) fail 0/);
  const runtimeTests = report.runtime_tests ?? 12;
  assert(Number.isInteger(runtimeTests) && runtimeTests >= 12);
  assert.match(runtime, new RegExp(`(?:#|\\u2139) tests ${runtimeTests}\\b`));
  for (const workload of ['USB', 'WiFi']) {
    const match = runtime.match(new RegExp(`PO16 production ${workload} oracle: (\\d+) -> (\\d+) reached instructions`));
    assert(match && Number(match[2]) <= Number(match[1]));
  }
  assert.deepEqual(report.summary, summary(report));
  assert(report.summary.module_code_bytes_saved > 0);
  assert(Object.values(report.summary.image_bytes_saved).filter(n => n > 0).length >= 2,
    'requires multiple actual production image wins');
  if (current) for (const s of report.sources) assert.equal(hash(absolute(s.file)), s.sha256, s.file);
  return report;
}
export function captureCrossFunction(directory, baselineFile, reuseFile) {
  assert(!fs.existsSync(directory), 'capture requires a fresh directory');
  const baseline = verifyLoopScalar(baselineFile);
  const po14 = read(absolute(baseline.baseline));
  assert.equal(po14.milestone, 'PO14');
  const reused = reuseFile ? verifyCrossFunction(reuseFile) : null;
  fs.mkdirSync(directory, { recursive: true });
  const tracked = new Set();
  const out = name => { const file = path.join(directory, name); tracked.add(file); return file; };
  const report = { schema: 1, milestone: 'PO16', status: 'capturing', default_changed: false,
    runtime_tests: 14,
    policy: { pass: 'cross-function', backend: 'direct/global-compact/thumb-operands/word',
      scope: 'module-local non-address-taken functions; PO15 input' },
    baseline: relative(baselineFile), baseline_sha256: hash(baselineFile),
    modules: [], images: [], commands: [], sources: [], artifacts: [] };
  if (reused) {
    assert.equal(reused.baseline_sha256, report.baseline_sha256);
    report.reuse = { report: relative(reuseFile), sha256: hash(reuseFile),
      contract: 'Reuse only byte-identical measured IR and hash-verified objects from identical backend/assembler tools.' };
    for (const name of ['badge-codegen', 'badge-asm', 'badge-link', 'elf2bin', 'badge-elf-info']) {
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
  for (const name of ['badge-ir-metrics', 'badge-codegen', 'badge-asm', 'badge-link', 'elf2bin', 'badge-elf-info']) {
    tools[name] = out(`tools/${name}`);
    fs.copyFileSync(absolute(`badge-compiler/build/${name}`), tools[name]);
  }
  for (const subdir of ['src', 'tools', 'tests']) {
    for (const name of fs.readdirSync(absolute(`badge-compiler/${subdir}`))) {
      if (!/\.(c|h|mjs)$/.test(name)) continue;
      const file = `badge-compiler/${subdir}/${name}`; report.sources.push({ file, sha256: hash(absolute(file)) });
    }
  }
  for (const file of ['badge-compiler/Makefile', 'badge-compiler/scripts/build-owned.sh',
    'badge-compiler/tools/oracle.sh', 'src/shared/usb_report.c', 'src/tools/diagnostics_logic.c',
    'src/radio/wifi_link.c', 'src/shared/bytes.h'])
    report.sources.push({ file, sha256: hash(absolute(file)) });
  const save = () => {
    report.artifacts = [...tracked].filter(fs.existsSync).map(file => ({ file: relative(file), sha256: hash(file) }));
    fs.writeFileSync(path.join(directory, 'results.json'), JSON.stringify(report, null, 2) + '\n');
  };
  try {
    run(process.execPath, ['--test', '--test-reporter=tap', 'badge-compiler/tests/cross-function.test.mjs']);
    report.runtime_log = report.commands.at(-1).log;
    const replacements = new Map();
    for (const old of baseline.modules) {
      const ir = out(`${old.module}.ll`), events = out(`${old.module}.events.json`);
      run(tools['badge-ir-metrics'], ['--passes=cross-function', `--emit-ir=${ir}`, absolute(old.ir), '-o', events]);
      const changed = hash(ir) !== hash(absolute(old.ir));
      let assembly = absolute(old.assembly), object = absolute(old.object);
      const prior = reused?.modules.find(m => m.module === old.module && m.changed &&
        hash(absolute(m.ir)) === hash(ir));
      if (changed) {
        assembly = out(`${old.module}.s`); object = out(`${old.module}.o`);
        if (prior) {
          fs.copyFileSync(absolute(prior.assembly), assembly);
          fs.copyFileSync(absolute(prior.object), object);
        } else {
          run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', ir]);
          run(tools['badge-codegen'], ['--frame-addressing=direct', '--register-residency=global-compact',
            '--scalar-selection=thumb-operands', '--memory-selection=word', ir, '-o', assembly]);
          run(tools['badge-asm'], [assembly, '-o', object]);
        }
        replacements.set(absolute(old.object), object);
      }
      const audit = decisions(ir);
      report.modules.push({ module: old.module, changed, reused: Boolean(prior), ir: relative(ir), events: relative(events),
        assembly: relative(assembly), object: relative(object), before_bytes: old.after_bytes,
        after_bytes: codeBytes(object), decisions: audit, accounting: compare(absolute(old.assembly), assembly, audit) });
    }
    for (const old of baseline.images) {
      const origin = po14.images.find(i => i.image === old.image);
      assert(origin);
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
        metrics: relative(metrics), script: origin.script, before_bytes: fs.statSync(absolute(old.binary)).size,
        after_bytes: fs.statSync(binary).size });
    }
    report.summary = summary(report); report.status = 'qualified-host-candidate'; save();
    return verifyCrossFunction(path.join(directory, 'results.json'), true);
  } catch (error) { report.status = 'failed'; report.error = error.message; save(); throw error; }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, baseline, reuse, ...extra] = process.argv.slice(2);
    assert(!extra.length && file && (mode === '--capture' && baseline ||
      ['--verify', '--verify-current'].includes(mode) && !baseline && !reuse),
    'Usage: cross-function-evidence.mjs --capture NEW_DIRECTORY PO15.json [PRIOR_PO16.json] | --verify[-current] PO16.json');
    const report = mode === '--capture' ? captureCrossFunction(absolute(file), absolute(baseline),
      reuse ? absolute(reuse) : undefined) :
      verifyCrossFunction(absolute(file), mode === '--verify-current');
    console.log(`PASS: PO16 ${JSON.stringify(report.summary)}`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
