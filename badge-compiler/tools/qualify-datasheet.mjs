import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly } from './benchmark-c06.mjs';
import { compareTimingReports } from './compiler-timing-compare.mjs';
import { decodeTiming, decodeApplication } from './compiler-test-protocol.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const defaults = 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division';
export const profiles = [
  { id: 'control', scalar: 'thumb-layout', passes: defaults },
  { id: 'd04', scalar: 'thumb-compact', passes: defaults },
  { id: 'd05', scalar: 'thumb-schedule', passes: defaults },
  { id: 'd06', scalar: 'thumb-relax', passes: defaults },
  { id: 'd07', scalar: 'thumb-layout', passes: defaults.replace('aggregate-memory', 'target-strength-reduce,aggregate-memory') },
];
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file);
const json = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const save = (file, value) => {
  const temporary = `${file}.tmp`;
  fs.writeFileSync(temporary, JSON.stringify(value, null, 2) + '\n', { flag: 'wx' });
  fs.renameSync(temporary, file);
};
const env = profile => ({ ...process.env, BADGE_OPT_PASSES: profile.passes,
  BADGE_IR_EXPRESSION_LOWERING: profile.expression ?? 'legacy',
  BADGE_CODEGEN_FRAME_ADDRESSING: 'direct', BADGE_CODEGEN_REGISTER_RESIDENCY: 'global-compact',
  BADGE_CODEGEN_SCALAR_SELECTION: profile.scalar, BADGE_CODEGEN_MEMORY_SELECTION: 'word' });
function command(executable, args, log, environment = process.env, timeout = 300000) {
  const result = spawnSync(executable, args, { cwd: root, env: environment, encoding: 'utf8',
    timeout, maxBuffer: 64 * 1024 * 1024 });
  if (log) fs.writeFileSync(log, `${result.stdout || ''}${result.stderr || ''}${result.error || ''}`);
  if (result.error || result.status !== 0)
    throw new Error(`${path.basename(log || executable)}: ${result.error?.message || `exit ${result.status}`}\n${result.stderr || ''}`);
  return result.stdout;
}
const metrics = file => jsonText(command(path.join(tools, 'badge-elf-info'), ['--metrics', file]));
const jsonText = text => JSON.parse(text);
function normalize(source, name) {
  source = source.replace(/^source_filename = "[^"\n]+"\n/m, '');
  if (name === 'usb_wifi_udp.o.owned') {
    const pattern = /(call void @"(?:wifi_link_report_encode|wifi_udp_report_encode|live_usb_control_init)"\(ptr [^,\n]+, i32 )\d+/g;
    assert.equal([...source.matchAll(pattern)].length, 3);
    source = source.replace(pattern, '$1BUILD_ID');
  }
  return source;
}
export function frontendHelpers(control, candidate) {
  const extract = text => [...text.matchAll(/^define internal [^\n]*@"(\.badge\.opt\.\d+)"[^\n]*\{\n[\s\S]*?^\}/gm)];
  const before = extract(control), after = extract(candidate);
  assert.equal(after.length, before.length, 'generated helper count changed');
  const mappings = [before, after].map(matches => new Map(matches.map((m, i) => [m[1], `.badge.generated.${i}`])));
  const normalized = (text, map) => text.replace(/@"(\.badge\.opt\.\d+)"/g,
    (whole, name) => map.has(name) ? `@"${map.get(name)}"` : whole);
  before.forEach((m, i) => assert.equal(normalized(after[i][0], mappings[1]),
    normalized(m[0], mappings[0]), 'generated helper body changed'));
  return mappings;
}
const walk = directory => fs.readdirSync(directory, { withFileTypes: true }).flatMap(e =>
  e.isDirectory() ? walk(path.join(directory, e.name)) : e.isFile() ? [path.join(directory, e.name)] : []);
function provenance() {
  const files = [...walk(path.join(root, 'badge-compiler/src')),
    ...walk(path.join(root, 'badge-compiler/tests')),
    ...walk(path.join(root, 'src/arch/rp2350')), ...walk(path.join(root, 'src/platform/badger')),
    ...walk(path.join(root, 'src/shared')), ...walk(path.join(root, 'src/tools')),
    ...walk(path.join(root, 'src/usb')),
    path.join(root, 'src/host/tools/compiler_test_host.c'),
    path.join(root, 'src/host/tools/usb_live_transport.h'),
    ...walk(path.join(root, 'badge-compiler/scripts')),
    ...walk(path.join(root, 'badge-compiler/tools')),
    path.join(root, 'Makefile'), path.join(root, 'badge-compiler/Makefile'),
    path.join(root, 'badge-compiler/docs/datasheet/fact-ledger.json'),
    ...['badge-cpp', 'badge-parse', 'badge-sema', 'badge-ir', 'badge-codegen',
      'badge-opt', 'badge-asm', 'badge-object', 'badge-link', 'badge-elf-info', 'elf2bin', 'badge-sha256']
      .map(n => path.join(tools, n))];
  return Object.fromEntries(files.map(f => [relative(f), hash(f)]));
}
function verifyHashes(hashes) {
  for (const [file, digest] of Object.entries(hashes)) assert.equal(hash(path.join(root, file)), digest, file);
}

export function verifyCampaign(file) {
  const result = json(file);
  const directory = path.dirname(file);
  const artifacts = [];
  if (result.toolchain.initialIr) {
    assert.equal(result.toolchain.initialIr.length, 11);
    assert.equal(new Set(result.toolchain.initialIr.map(input => input.module)).size, 11);
    for (const input of result.toolchain.initialIr) {
      assert.equal(path.basename(input.file), `${input.module}.initial.ll`);
      const archive = path.join(directory, path.basename(input.file));
      assert.equal(hash(archive), input.sha256);
      artifacts.push({ file: relative(archive), sha256: input.sha256 });
    }
  }
  for (const [index, round] of result.rounds.entries()) {
    for (const [role, config] of round.configurations.entries()) {
      const raw = path.join(directory, `round${index + 1}-${config.name}.bin`);
      const decode = round.timingVersion === 4 ? decodeApplication : decodeTiming;
      const decoded = decode(fs.readFileSync(raw), config.build, Buffer.from(round.nonce, 'hex'));
      assert.deepEqual(decoded.clock, round.clocks[role]);
      assert.deepEqual(decoded.overhead, round.overhead[role]);
      for (const field of ['modules', 'kernels'])
        decoded[field].forEach((row, i) => {
          assert.deepEqual(row.values, round[field][i][config.name]);
          assert.deepEqual(row.timing.samples, round[field][i].timing[config.name].samples);
        });
      assert.equal(hash(config.image), config.sha256);
      artifacts.push({ file: relative(raw), sha256: hash(raw) },
        { file: relative(config.image), sha256: hash(config.image) });
    }
  }
  return { result, artifacts };
}

// Every physical role and repeat must pass; a geometric mean cannot hide a regression.
export function assessTiming(control0, candidate0, control1, candidate1) {
  const comparisons = [compareTimingReports(control0, candidate0), compareTimingReports(control1, candidate1)];
  compareTimingReports(control0, control1);
  const rows = [];
  for (const field of ['modules', 'kernels']) {
    for (let i = 0; i < control0.rounds[0][field].length; i++) {
      const observations = [0, 1].flatMap(repeat => [0, 1].map(role => {
        const a = [control0, control1][repeat].rounds[role][field][i].timing;
        const b = [candidate0, candidate1][repeat].rounds[role][field][i].timing;
        const other = [control1, control0][repeat].rounds[role][field][i].timing;
        const spread = t => (t.max - t.min) / t.median;
        const envelope = spread(a.owned) + spread(b.owned) +
          Math.abs(other.owned.median / a.owned.median - 1) +
          Math.abs(b.llvm.median / a.llvm.median - 1);
        const change = (b.owned.median - a.owned.median) / a.owned.median;
        const controlMedian = BigInt(a.owned.median), candidateMedian = BigInt(b.owned.median);
        const llvmMedian = BigInt(a.llvm.median);
        const denominator = candidateMedian * llvmMedian;
        const numerator = BigInt(a.owned.max - a.owned.min +
          Math.abs(other.owned.median - a.owned.median)) * denominator +
          BigInt(b.owned.max - b.owned.min) * controlMedian * llvmMedian +
          BigInt(Math.abs(b.llvm.median - a.llvm.median)) * controlMedian * candidateMedian;
        const difference = BigInt(b.owned.median - a.owned.median) * denominator;
        return { repeat, role, serial: [control0, control1][repeat].rounds[role].configurations[1].serial,
          control: a.owned.median, candidate: b.owned.median, change, envelope,
          exact_ticks: { difference: b.owned.median - a.owned.median,
            envelope_numerator: numerator.toString(), envelope_denominator: denominator.toString() },
          classification: difference < -numerator ? 'improvement' : difference > numerator ? 'regression' : 'within-noise' };
      }));
      rows.push({ workload: control0.rounds[0][field][i].module, kind: field,
        observations, repeatable_improvement: observations.every(x => x.classification === 'improvement'),
        regression: observations.some(x => x.classification === 'regression') });
    }
  }
  return { comparisons, rows, repeatable_improvements: rows.filter(x => x.repeatable_improvement).map(x => x.workload),
    regressions: rows.filter(x => x.regression).map(x => x.workload),
    method: 'Per-role/per-repeat raw spreads + same-board repeated control drift + paired LLVM drift; exact integer-rational strict inequalities; percentages are display only; no averaging away regressions. Same conservative envelope method as D02, not its numeric noise from a different experiment.' };
}

export function promotionDecision(timing, offline, restoration) {
  assert.equal(offline.correctness, true, 'offline correctness missing');
  assert.equal(offline.memory_and_frames, true, 'memory/frame qualification missing');
  assert.equal(restoration.length, 2, 'two restorations required');
  assert(restoration.every(x => x.verified && x.left_in_bootsel), 'unverified restoration');
  assert.equal(new Set(restoration.map(x => x.serial)).size, 2, 'distinct restoration devices required');
  const eligible = timing.repeatable_improvements.length > 0 && timing.regressions.length === 0;
  return { eligible_for_speed_review: eligible, promoted: false,
    decision: eligible ? 'retain-opt-in-pending-production-runtime-coverage' :
      timing.regressions.length ? 'reject-speed-promotion-retain-explicit' : 'retain-opt-in-no-resolved-speed-benefit',
    reason: eligible ? 'Benchmarks qualify reached shared code, not every production peripheral/interrupt/stack path. No automatic default change.' :
      timing.regressions.length ? 'A resolved physical-role regression blocks speed promotion.' : 'No consistent improvement beyond the predeclared envelope.' };
}

function analyze(file, output) {
  assert(!fs.existsSync(output), 'analysis output must be new');
  const captured = json(file);
  assert(['hardware-captured', 'qualified-retained-explicit'].includes(captured.status), 'complete capture required');
  assert.equal(captured.errors.length, 0);
  assert.equal(captured.campaigns.length, 10);
  const offlineFile = path.join(root, captured.offline.file);
  assert.equal(hash(offlineFile), captured.offline.sha256);
  const qualified = json(offlineFile);
  assert.equal(qualified.status, 'offline-qualified');
  for (const candidate of qualified.candidates)
    for (const artifact of [...candidate.modules, ...candidate.images, candidate.instrumented_maze]) {
      assert.equal(hash(path.join(root, artifact.file)), artifact.sha256, artifact.file);
      if (artifact.assembly)
        assert.equal(hash(path.join(root, artifact.assembly)), artifact.assembly_sha256);
    }
  for (const backup of captured.backups) {
    assert.equal(hash(path.join(root, backup.file)), backup.sha256);
    assert.equal(hash(path.join(root, backup.file.replace(/-original\.bin$/, '-restored.bin'))), backup.sha256);
  }
  const read = (id, repeat) => {
    const c = captured.campaigns.find(x => x.id === id && x.repeat === repeat);
    assert(c, `${id}/${repeat}: missing campaign`);
    const input = path.join(root, c.file);
    assert.equal(hash(input), c.sha256);
    for (const artifact of c.artifacts) assert.equal(hash(path.join(root, artifact.file)), artifact.sha256);
    return verifyCampaign(input).result;
  };
  const decisions = profiles.slice(1).map(profile => {
    const timing = assessTiming(read('control', 0), read(profile.id, 0), read('control', 1), read(profile.id, 1));
    return { id: profile.id, ...promotionDecision(timing, qualified.candidates.find(c => c.id === profile.id), captured.restoration), timing };
  });
  fs.writeFileSync(output, JSON.stringify({ ...captured, status: 'qualified-retained-explicit', decisions,
    analysis: { input: relative(file), input_sha256: hash(file), tool: relative(fileURLToPath(import.meta.url)),
      tool_sha256: hash(fileURLToPath(import.meta.url)), method: 'exact-integer-noise-v1',
      scope: 'Read-only reanalysis of a completed restored campaign; no claim of recompilation with current sources.' } }, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify(decisions.map(d => ({ id: d.id, decision: d.decision,
    improvements: d.timing.repeatable_improvements, regressions: d.timing.regressions })), null, 2));
}

export function offline(directory, selectedProfiles = profiles, milestone = 'D09') {
  fs.mkdirSync(directory);
  const report = { schema: 1, milestone, status: 'offline-in-progress',
    profiles: selectedProfiles, hashes: provenance(), logs: [], candidates: [],
    default_profile: selectedProfiles[0] };
  const recordLog = file => report.logs.push({ file: relative(file), sha256: hash(file) });
  try {
    for (const profile of selectedProfiles) {
      const build = path.join(directory, `${profile.id}-corpus`);
      const maze = path.join(directory, `${profile.id}-maze`);
      const log = path.join(directory, `${profile.id}-build.log`);
      command('make', ['-j4', `BUILD_DIR=${relative(build)}`,
        'RP2350_CC=bash badge-compiler/scripts/owned-cc.sh',
        `RP2350_OBJCOPY=${tools}/elf2bin`, `RP2350_NM=${tools}/badge-elf-info`,
        `RP2350_SIZE=${tools}/badge-elf-info`, 'all',
        `${relative(build)}/universe26/uf2/usb_wifi_udp.uf2`], log, env(profile));
      recordLog(log);
      const mazeLog = path.join(directory, `${profile.id}-maze-build.log`);
      command('make', ['-j4', `BUILD_DIR=${relative(maze)}`, 'TILT_MAZE_FLAGS=-DTILT_MAZE_COMPARE',
        'RP2350_CC=bash badge-compiler/scripts/owned-cc.sh',
        `RP2350_OBJCOPY=${tools}/elf2bin`, `RP2350_NM=${tools}/badge-elf-info`,
        `RP2350_SIZE=${tools}/badge-elf-info`, 'tilt-maze'], mazeLog, env(profile));
      recordLog(mazeLog);
      const firmware = path.join(build, 'universe26/firmware');
      const control = path.join(directory, 'control-corpus/universe26/firmware');
      const names = fs.readdirSync(firmware).filter(n => n.endsWith('.o.owned') &&
        fs.existsSync(path.join(firmware, n, 'optimized.ll'))).sort();
      assert.equal(names.length, 37, 'enumerated corpus changed: update qualification scope deliberately');
      const modules = [], frames = [];
      for (const name of names) {
        const input = path.join(firmware, name, 'optimized.ll');
        const baseline = path.join(control, name, 'optimized.ll');
        if (milestone !== 'PO09' && !['d07', 'po05', 'po06'].includes(profile.id)) assert.equal(normalize(fs.readFileSync(input, 'utf8'), name),
          normalize(fs.readFileSync(baseline, 'utf8'), name), `${name}: IR drift`);
        command(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', input]);
        const frontend = path.join(firmware, name, 'frontend.ll');
        command(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', frontend]);
        const assembly = path.join(firmware, name, 'assembly.s');
        const before = parseAssembly(fs.readFileSync(path.join(control, name, 'assembly.s'), 'utf8'), name);
        const after = parseAssembly(fs.readFileSync(assembly, 'utf8'), name);
        const helperMaps = ['PO05', 'PO06', 'PO09'].includes(milestone) ? frontendHelpers(fs.readFileSync(baseline,'utf8'),
          fs.readFileSync(input,'utf8')) : [new Map(),new Map()];
        const renamed = (functions,map) => new Map([...functions].map(([name,fn])=>[
          map.get(name)??name,{...fn,calls:fn.calls.map(call=>({...call,
            target:map.get(call.target)??call.target}))}]));
        const beforeNames=renamed(before,helperMaps[0]), afterNames=renamed(after,helperMaps[1]);
        if (milestone === 'PO06')
          assert.equal(normalize(fs.readFileSync(frontend, 'utf8'), name),
            normalize(fs.readFileSync(path.join(control, name, 'frontend.ll'), 'utf8'), name),
            `${name}: frontend drift`);
        assert.deepEqual([...beforeNames.keys()], [...afterNames.keys()]);
        for (const [fn, a] of beforeNames) {
          const b = afterNames.get(fn);
          assert.deepEqual(b.calls, a.calls, `${name}/${fn}: calls`);
          assert.deepEqual(b.unknown, a.unknown, `${name}/${fn}: new uncertainty`);
          assert.equal(b.max_outgoing_bytes, a.max_outgoing_bytes);
          const total = x => x.frame_bytes === null || x.saves_bytes === null ? null : x.frame_bytes + x.saves_bytes;
          if (total(a) !== null) {
            assert(total(b) !== null, `${name}/${fn}: missing frame`);
            if (!['PO05', 'PO09'].includes(milestone)) assert(total(b) <= total(a), `${name}/${fn}: frame growth`);
          }
          frames.push({ module: name, function: fn, control: total(a), candidate: total(b),
            ...(['PO05', 'PO09'].includes(milestone)?{control_symbol:a.name,candidate_symbol:b.name}:{}),
            unknown: b.unknown, calls: b.calls });
        }
        const text = fs.readFileSync(assembly, 'utf8');
        const controlText = fs.readFileSync(path.join(control, name, 'assembly.s'), 'utf8');
        const totals = source => ({
          fused_comparisons: [...source.matchAll(/^@ compare-branch: fused (\d+)$/gm)]
            .reduce((n, m) => n + Number(m[1]), 0),
          unallocated_values: [...source.matchAll(/^@ global-allocation:.*spilled (\d+)/gm)]
            .reduce((n, m) => n + Number(m[1]), 0),
        });
        const selected = totals(text), controlTotals = totals(controlText);
        if (!['PO05', 'PO09'].includes(milestone))
          assert(selected.unallocated_values <= controlTotals.unallocated_values, `${name}: new spills`);
        modules.push({ module: name, file: relative(input), sha256: hash(input),
          frontend: relative(frontend), frontend_sha256: hash(frontend),
          assembly: relative(assembly), assembly_sha256: hash(assembly),
          selection: selected, control_selection: controlTotals });
      }
      const images = fs.readdirSync(firmware).filter(n => n.endsWith('.elf')).sort().map(n => {
        const file = path.join(firmware, n), a = metrics(path.join(control, n)), b = metrics(file);
        assert.equal(b.data, a.data); assert.equal(b.bss, a.bss);
        if (!['PO05', 'PO09'].includes(milestone)) assert(b.text <= a.text);
        const limit = n === 'usb_wifi_udp.elf' ? 0x2007f000 : 0x20080000;
        const ram = b.sections.filter(s => (s.flags & 2) && s.size && s.address >= 0x20000000 && s.address < limit);
        assert(ram.every(s => s.address + s.size <= limit), `${n}: RAM budget`);
        const end = b.symbols.find(s => s.name === '__flash_binary_end')?.value;
        assert(Number.isInteger(end) && end <= 0x10400000, `${n}: flash budget`);
        return { image: n, file: relative(file), sha256: hash(file), control_text: a.text,
          text: b.text, data: b.data, bss: b.bss, ram_headroom: limit - Math.max(0x20000000, ...ram.map(s => s.address + s.size)) };
      });
      assert.equal(images.length, 12);
      const mazeFile = path.join(maze, 'universe26/firmware/tilt_maze.elf');
      const mm = metrics(mazeFile);
      const oldMaze = metrics(path.join(directory, 'control-maze/universe26/firmware/tilt_maze.elf'));
      if (!['PO05', 'PO09'].includes(milestone)) assert(mm.text <= oldMaze.text);
      assert.equal(mm.data, oldMaze.data); assert.equal(mm.bss, oldMaze.bss);
      const oracleLog = path.join(directory, `${profile.id}-execution.log`);
      const oracleEnv = { ...env(profile), LLVM_CLANG: process.env.LLVM_CLANG || 'clang',
        LLVM_OPT: process.env.LLVM_OPT || 'opt', LLVM_LLD: process.env.LLVM_LLD || 'ld.lld',
        QEMU_ARM: process.env.QEMU_ARM || 'qemu-system-arm' };
      // Existing scripts use compiler-relative paths; keep their established working directory.
      const script = 'cd badge-compiler && bash tests/codegen_oracle.sh && IR_OPTIMIZER=owned bash tests/codegen_oracle.sh && bash tests/tilt-maze-oracle.sh';
      command('bash', ['-c', script], oracleLog, oracleEnv, 600000);
      recordLog(oracleLog);
      const tradeoffs = {
        growing_frames: frames.filter(f => f.control !== null && f.candidate > f.control),
        growing_spill_counts: modules.filter(m => m.selection.unallocated_values > m.control_selection.unallocated_values),
        growing_images: images.filter(i => i.text > i.control_text),
        maze_text_delta: mm.text - oldMaze.text,
      };
      report.candidates.push({ id: profile.id, correctness: true,
        memory_and_frames: tradeoffs.growing_frames.length === 0,
        ...(['PO05', 'PO09'].includes(milestone) ? { tradeoffs, promotion_eligible: false } : {}),
        modules, images, frames, instrumented_maze: { file: relative(mazeFile), sha256: hash(mazeFile),
          text: mm.text, data: mm.data, bss: mm.bss },
        limitations: ['Frame/call metadata does not certify recursive, indirect, interrupt or peripheral runtime paths. 4 KiB scratch stack safety is not inferred from main-RAM headroom.'] });
      save(path.join(directory, 'offline.json'), report);
      console.log(`${milestone} ${profile.id}: ${modules.length} modules, ${images.length} images, ${frames.length} frame/call checks and both execution routes passed.`);
    }
    verifyHashes(report.hashes);
    report.status = 'offline-qualified';
  } catch (error) {
    report.status = 'failed'; report.error = error.message;
    throw error;
  } finally {
    save(path.join(directory, 'offline.json'), report);
  }
}

function hardware(directory, offlineFile, serials) {
  assert.equal(serials.length, 2);
  assert(serials.every(s => /^[0-9A-F]{16}$/.test(s)) && new Set(serials).size === 2);
  const qualified = json(offlineFile);
  assert.equal(qualified.status, 'offline-qualified');
  verifyHashes(qualified.hashes);
  for (const candidate of qualified.candidates) {
    for (const artifact of [...candidate.images, ...candidate.modules, candidate.instrumented_maze])
      assert.equal(hash(path.join(root, artifact.file)), artifact.sha256, artifact.file);
  }
  fs.mkdirSync(directory);
  const pico = process.env.PICOTOOL || path.join(root, 'build/picotool-usb/picotool');
  const report = { schema: 1, milestone: 'D09', status: 'hardware-in-progress',
    offline: { file: relative(offlineFile), sha256: hash(offlineFile) },
    collector_source_sha256: hash(path.join(root, 'src/host/tools/compiler_test_host.c')),
    picotool_sha256: hash(pico), seed: 3276377845, campaigns: [], backups: [], restoration: [], errors: [] };
  try {
    for (const serial of serials) {
      const identity = command(pico, ['info', '-d', '--ser', serial], path.join(directory, `${serial}-identity.log`));
      assert(/current cpu:\s+ARM/.test(identity) && /secure boot:\s+0/.test(identity) && /flash size:\s+16384K/.test(identity));
      const file = path.join(directory, `${serial}-original.bin`);
      command(pico, ['save', '-a', '-v', file, '-t', 'bin', '--ser', serial], path.join(directory, `${serial}-snapshot.log`));
      assert.equal(fs.statSync(file).size, 16777216);
      report.backups.push({ serial, file: relative(file), sha256: hash(file) });
    }
    for (let repeat = 0; repeat < 2; repeat++) {
      for (const profile of profiles) {
        verifyHashes(qualified.hashes);
        const log = path.join(directory, `${repeat}-${profile.id}.log`);
        const output = command(process.execPath, ['badge-compiler/tools/compiler-compare.mjs',
          '--timing', `--seed=${report.seed}`, ...serials], log, env(profile), 600000);
        const run = output.match(/Compiler comparison: ([^\n]+)/)?.[1];
        assert(run, 'campaign directory missing');
        const file = path.join(run, 'results.json');
        const { result, artifacts } = verifyCampaign(file);
        assert.equal(result.matched, true);
        assert.equal(result.toolchain.ownedScalarSelection, profile.scalar);
        assert.equal(result.toolchain.ownedPasses, profile.passes);
        report.campaigns.push({ id: profile.id, repeat, file: relative(file), sha256: hash(file), artifacts });
        save(path.join(directory, 'hardware.json'), report);
        console.log(`D09 hardware repeat ${repeat + 1} ${profile.id}: two role-swapped rounds captured.`);
      }
    }
  } catch (error) {
    report.errors.push(error.message);
  } finally {
    for (const backup of report.backups) {
      try {
        const file = path.join(root, backup.file);
        assert.equal(hash(file), backup.sha256);
        command(pico, ['load', '-u', '-v', file, '-t', 'bin', '-o', '0x10000000', '--ser', backup.serial],
          path.join(directory, `${backup.serial}-restore.log`));
        const reread = path.join(directory, `${backup.serial}-restored.bin`);
        command(pico, ['save', '-a', '-v', reread, '-t', 'bin', '--ser', backup.serial],
          path.join(directory, `${backup.serial}-verify.log`));
        assert.equal(hash(reread), backup.sha256);
        report.restoration.push({ serial: backup.serial, verified: true, left_in_bootsel: true, sha256: backup.sha256 });
      } catch (error) {
        report.errors.push(`Restoration ${backup.serial}: ${error.message}`);
        report.restoration.push({ serial: backup.serial, verified: false, error: error.message });
      }
    }
    report.status = report.errors.length ? 'failed' : 'hardware-captured';
    save(path.join(directory, 'hardware.json'), report);
  }
  assert.equal(report.errors.length, 0, report.errors.join('\n'));
  verifyHashes(qualified.hashes);
  const read = (id, repeat) => {
    const c = report.campaigns.find(x => x.id === id && x.repeat === repeat);
    assert(c, `${id}/${repeat}: missing campaign`);
    const file = path.join(root, c.file);
    assert.equal(hash(file), c.sha256);
    return verifyCampaign(file).result;
  };
  report.decisions = profiles.slice(1).map(profile => {
    const timing = assessTiming(read('control', 0), read(profile.id, 0), read('control', 1), read(profile.id, 1));
    return { id: profile.id, ...promotionDecision(timing, qualified.candidates.find(c => c.id === profile.id), report.restoration), timing };
  });
  report.status = 'qualified-retained-explicit';
  save(path.join(directory, 'hardware.json'), report);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, directory, offlineFile, ...serials] = process.argv.slice(2);
    assert(['--offline', '--offline-branch', '--offline-homes', '--offline-expressions', '--offline-memory', '--offline-calls'].includes(mode) && directory && !offlineFile ||
      mode === '--hardware' && directory && offlineFile && serials.length === 2 ||
      mode === '--analyze' && directory && offlineFile && serials.length === 0,
    'Usage: qualify-datasheet.mjs --offline[-branch|-homes|-expressions|-memory|-calls] NEW_DIRECTORY | --hardware NEW_DIRECTORY OFFLINE.json SERIAL SERIAL | --analyze NEW_REPORT.json HARDWARE.json');
    if (mode === '--offline-calls') offline(path.resolve(directory),
      [{ id: 'control', scalar: 'thumb-homes', passes: defaults, expression: 'ssa' },
        { id: 'po07', scalar: 'thumb-calls', passes: defaults, expression: 'ssa' }], 'PO07');
    else if (mode === '--offline-memory') offline(path.resolve(directory),
      [{ id: 'control', scalar: 'thumb-homes', passes: defaults, expression: 'ssa' },
        { id: 'po06', scalar: 'thumb-homes',
          passes: defaults + ',local-memory', expression: 'ssa' }], 'PO06');
    else if (mode === '--offline-expressions') offline(path.resolve(directory),
      [{ id: 'control', scalar: 'thumb-homes', passes: defaults, expression: 'legacy' },
        { id: 'po05', scalar: 'thumb-homes', passes: defaults, expression: 'ssa' }], 'PO05');
    else if (mode === '--offline-homes') offline(path.resolve(directory),
      [{ id: 'control', scalar: 'thumb-branch', passes: defaults },
        { id: 'po04', scalar: 'thumb-homes', passes: defaults }], 'PO04');
    else if (mode === '--offline-branch') offline(path.resolve(directory),
      [profiles[0], { id: 'po03', scalar: 'thumb-branch', passes: defaults }], 'PO03');
    else if (mode === '--offline') offline(path.resolve(directory));
    else if (mode === '--hardware') hardware(path.resolve(directory), path.resolve(offlineFile), serials);
    else analyze(path.resolve(offlineFile), path.resolve(directory));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
