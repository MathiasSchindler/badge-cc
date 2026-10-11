import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { invocation } from './pipeline-stages.mjs';
import { verifyApplicationReport } from './application-benchmark.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const relative = file => path.relative(root, file);
const llvm = name => process.env.LLVM_BIN ? path.join(process.env.LLVM_BIN, name) : name;
const save = (file, data) => fs.writeFileSync(file, JSON.stringify(data, null, 2) + '\n');
const walk = directory => fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
  const file = path.join(directory, entry.name);
  return entry.isDirectory() ? walk(file) : entry.isFile() ? [file] : [];
});
function run(command, args, log) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000,
    maxBuffer: 64 * 1024 * 1024 });
  if (log) fs.writeFileSync(log, `${result.stdout ?? ''}${result.stderr ?? ''}${result.error ?? ''}`);
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${command}: ${result.stderr?.slice(-4000)}`);
  return result.stdout;
}

export const canonicalIr = text => text.replace(/^source_filename = .*\n/m, '');
export const backendFlags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
  '-Oz', '-ffunction-sections', '-fdata-sections', '-Xclang', '-disable-llvm-passes', '-x', 'ir'];

function compile(role, args) {
  assert(['llvm', 'owned', 'llvm-frontend'].includes(role));
  const parsed = invocation(args);
  run('bash', ['badge-compiler/scripts/owned-cc.sh', ...args]);
  if (!parsed.source?.endsWith('.c')) return;
  const directory = path.resolve(root, `${parsed.output}.owned`);
  const frontend = role === 'llvm-frontend';
  const input = path.join(directory, frontend ? 'initial.ll' : 'fixed.ll');
  fs.writeFileSync(input, canonicalIr(fs.readFileSync(path.join(directory, frontend ? 'frontend.ll' : 'optimized.ll'), 'utf8')));
  const metadata = { role, source: parsed.source, original_args: args,
    input: relative(input), sha256: sha(input), normal_llvm_ir_optimizer: frontend,
    compiler: role.startsWith('llvm') ? llvm('clang') : path.join(tools, 'badge-codegen') };
  if (role.startsWith('llvm')) {
    const assembly = path.join(directory, 'llvm.s');
    metadata.args = [...(frontend ? backendFlags.filter((flag, i) =>
      flag !== '-Xclang' && backendFlags[i - 1] !== '-Xclang') : backendFlags),
      '-S', input, '-o', assembly];
    run(llvm('clang'), metadata.args, path.join(directory, 'backend.log'));
    run(llvm('clang'), ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
      '-c', assembly, '-o', parsed.output], path.join(directory, 'assembler.log'));
  } else {
    metadata.args = [input, '-o', path.join(directory, 'assembly.s')];
    run(path.join(tools, 'badge-codegen'), metadata.args);
    run(path.join(tools, 'badge-asm'), [path.join(directory, 'assembly.s'), '-o', parsed.output]);
  }
  metadata.object = relative(path.resolve(root, parsed.output));
  metadata.object_sha256 = sha(path.resolve(root, parsed.output));
  save(path.join(directory, 'crossover.json'), metadata);
}

export function disassemblyFunctions(text, frameBases = ['r7', 'sp']) {
  const functions = [];
  let current;
  for (const line of text.split('\n')) {
    const label = line.match(/^([0-9a-f]+) <([^>]+)>:$/);
    if (label) {
      if (/^[.$]/.test(label[2])) continue;
      current = { name: label[2], address: parseInt(label[1], 16), instructions: [], data: [],
        executable_bytes: 0, literal_bytes: 0, loads: 0, stores: 0,
        frame_loads: 0, frame_stores: 0, copies: 0, calls: 0, branches: 0,
        pushes: 0, pops: 0, extensions: 0 };
      functions.push(current);
      continue;
    }
    const instruction = line.match(/^\s*([0-9a-f]+):\s+([0-9a-f]{2}(?:[0-9a-f]{2}){0,3}(?: [0-9a-f]{2}(?:[0-9a-f]{2}){0,3})*)[ \t]{2,}([a-z.][a-z0-9.]*)\s*(.*)$/);
    if (!instruction || !current) continue;
    const [, address, raw, mnemonic, operands] = instruction;
    const bytes = raw.trim().replaceAll(/\s/g, '').length / 2;
    if (mnemonic.startsWith('.')) {
      current.literal_bytes += bytes;
      current.data.push({ address: parseInt(address, 16), bytes, mnemonic, operands });
      continue;
    }
    current.instructions.push({ address: parseInt(address, 16), bytes, mnemonic, operands });
    current.executable_bytes += bytes;
    const op = mnemonic.split('.')[0];
    const load = /^ldr/.test(op), store = /^str/.test(op);
    current.loads += Number(load); current.stores += Number(store);
    if (frameBases.some(base => new RegExp(`\\[${base}(?:,|\\])`).test(operands))) {
      current.frame_loads += Number(load); current.frame_stores += Number(store);
    }
    current.copies += Number(op === 'mov' || op === 'movs');
    current.calls += Number(op === 'bl' || op === 'blx');
    current.branches += Number(/^b(?:eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?$/.test(op) ||
      ['bx', 'cbz', 'cbnz', 'tbb', 'tbh'].includes(op));
    current.pushes += Number(op === 'push'); current.pops += Number(op === 'pop');
    current.extensions += Number(['ubfx', 'sbfx', 'uxtb', 'uxth', 'sxtb', 'sxth'].includes(op));
  }
  return functions;
}

export function traceFunctions(assembly, ir) {
  const lines = ir.split('\n'), functions = [];
  let current, operation, allocation;
  for (const line of assembly.split('\n')) {
    if (/^@ global-allocation:/.test(line)) { allocation = line; continue; }
    const start = line.match(/^@ attribution-function (.+)$/);
    if (start) {
      current = { name: start[1], slots: [], registers: {}, operations: [], entry: [],
        stack_categories: {}, metadata: allocation ? [allocation] : [] };
      allocation = undefined;
      functions.push(current); operation = undefined;
      continue;
    }
    if (!current) continue;
    const slot = line.match(/^@ attribution-slot (\S+) (\d+) (\d+) (\d+) (\d+) (\d+)$/);
    if (slot) {
      current.slots.push({ name: slot[1], offset: +slot[2], size: +slot[3],
        object: +slot[4], object_size: +slot[5], parameter: slot[6] === '1' });
      continue;
    }
    const register = line.match(/^@ attribution-register (\S+) (\d+)$/);
    if (register) { current.registers[register[1]] = +register[2]; continue; }
    const phi = line.match(/^@ attribution-phi (\d+) (\d+)$/);
    if (phi) { current.phi = { offset: +phi[1], size: +phi[2] }; continue; }
    const frame = line.match(/^@ stack-frame: (\d+) \+ (\d+) saved-register bytes$/);
    if (frame) { current.frame_bytes = +frame[1]; current.saved_bytes = +frame[2]; continue; }
    const marker = line.match(/^@ attribution-ir (\d+)$/);
    if (marker) {
      const text = lines[+marker[1] - 1];
      assert(text?.trim(), `trace references missing IR line ${marker[1]}`);
      operation = { line: +marker[1], ir: text.trim(), assembly: [] };
      current.operations.push(operation); continue;
    }
    if (/^@ (global-|compact-|allocation-|memory-|branch-)/.test(line))
      current.metadata.push(line);
    if (!/^[a-z][a-z0-9.]*(?: |$)/.test(line)) continue;
    (operation ? operation.assembly : current.entry).push(line);
    const memory = line.match(/^(ldr|str)(?:\.w|\.n)? \w+, \[r7, #(\d+)\]/);
    if (!memory) continue;
    const offset = +memory[2];
    const op = operation?.ir.match(/^(?:%\S+ = )?(\w+)/)?.[1];
    let category;
    if (!operation) category = 'parameter-entry-homes-or-incoming-arguments';
    else if (offset >= current.frame_bytes) category = 'incoming-arguments';
    else if (current.phi?.size && offset >= current.phi.offset &&
        offset < current.phi.offset + current.phi.size) category = 'phi-transfer-buffer';
    else if (current.slots.some(slot => slot.object_size && offset >= slot.object &&
        offset < slot.object + slot.object_size)) category = 'private-frame-object';
    else if (op === 'br' || op === 'switch') category = 'phi-edge-home-transfer';
    else {
      const slots = current.slots.filter(slot => offset >= slot.offset && offset < slot.offset + slot.size);
      category = slots.length ? 'ssa-or-parameter-homes-not-eviction-spills' : 'unclassified-frame-access';
    }
    const key = `${category}:${memory[1] === 'ldr' ? 'load' : 'store'}`;
    current.stack_categories[key] = (current.stack_categories[key] ?? 0) + 1;
  }
  return functions;
}

export function llvmSpillAnnotations(text) {
  const rows = {};
  let name;
  for (const line of text.split('\n')) {
    const start = line.match(/^\s*\.type\s+([^,\s]+),\s*[%@]function/);
    if (start) {
      name = start[1]; rows[name] = { spill_stores: 0, reload_loads: 0, frame_adjustment_bytes: 0 };
    }
    if (!name) continue;
    if (/\b(?:Folded )?Spill\b/.test(line)) rows[name].spill_stores++;
    if (/\b(?:Folded )?Reload\b/.test(line)) rows[name].reload_loads++;
    const adjustment = line.match(/^\s*\.pad\s+#(\d+)/);
    if (adjustment) rows[name].frame_adjustment_bytes += +adjustment[1];
  }
  return rows;
}

export function emissionInventory(fn) {
  const operations = {};
  for (const row of fn.operations) {
    const op = row.ir.match(/^(?:%\S+ = )?(?:tail |notail )?([a-z]\w*)/)?.[1];
    if (!op) continue;
    operations[op] ??= { ir_operations: 0, emitted_instruction_lines: 0 };
    operations[op].ir_operations++;
    operations[op].emitted_instruction_lines += row.assembly.length;
  }
  let adjacent_compare_branches = 0;
  for (let index = 0; index + 1 < fn.operations.length; ++index) {
    const compare = fn.operations[index].ir.match(/^(%\S+) = icmp /);
    if (compare && fn.operations[index + 1].ir.startsWith(`br i1 ${compare[1]},`))
      adjacent_compare_branches++;
  }
  return { operations, adjacent_compare_branches,
    volatile_ir_operations: fn.operations.filter(row => /\bvolatile\b/.test(row.ir)).length,
    literal_pool_skip_branches: fn.operations.reduce((sum, row) => sum +
      row.assembly.filter(line => /^b\.w \.Lcg_after/.test(line)).length, 0),
    entry_instruction_lines: fn.entry.length };
}

function symbolSizes(file) {
  return new Map(run(llvm('llvm-nm'), ['--print-size', '--defined-only', file]).split('\n')
    .map(line => line.match(/^([0-9a-f]+) ([0-9a-f]+) [tT] (.+)$/)).filter(Boolean)
    .map(match => [match[3], { address: parseInt(match[1], 16) & ~1, bytes: parseInt(match[2], 16) }]));
}

function capture(directory, hardwareFile) {
  fs.mkdirSync(directory);
  const report = { schema: 1, root, status: 'in-progress', hardware: relative(hardwareFile),
    hardware_sha256: sha(hardwareFile), modules: [], artifacts: [], sources: [], errors: [] };
  const file = path.join(directory, 'results.json');
  const artifacts = new Set([hardwareFile]);
  try {
    const hardware = verifyApplicationReport(hardwareFile);
    report.analysis_source_drift = [];
    for (const [source, digest] of Object.entries(hardware.sources)) {
      const current = sha(path.join(root, source));
      if (current !== digest) {
        assert.equal(source, 'badge-compiler/tools/backend-attribution.mjs',
          `non-analysis source changed since hardware capture: ${source}`);
        report.analysis_source_drift.push({ file: source, captured_sha256: digest, analysis_sha256: current });
      }
    }
    assert.equal(hardware.fixed_ir, true, 'not a fixed-IR hardware experiment');
    const campaign = JSON.parse(fs.readFileSync(path.join(root, hardware.campaigns[0].file), 'utf8'));
    const sources = ['Makefile', 'badge-compiler/Makefile',
      ...fs.readdirSync(path.join(root, 'badge-compiler/src')).filter(name => /\.(c|h)$/.test(name))
        .map(name => `badge-compiler/src/${name}`),
      'badge-compiler/tools/backend-attribution.mjs'];
    report.sources = sources.map(source => ({ file: source, sha256: sha(path.join(root, source)) }));
    run('make', ['-C', 'badge-compiler', 'build/badge-codegen-attribution'], path.join(directory, 'trace-build.log'));
    const own = campaign.rounds[0].configurations.find(config => config.name === 'owned');
    const reference = campaign.rounds[0].configurations.find(config => config.name === 'llvm');
    const firmware = config => path.join(root, config.dir, 'universe26/firmware');
    for (const config of [own, reference])
      for (const stage of walk(firmware(config))) artifacts.add(stage);
    for (const config of [own, reference]) {
      const currentUf2 = path.join(root, config.dir, 'universe26/uf2/compiler_test.uf2');
      assert.equal(sha(currentUf2), config.sha256, 'attribution image differs from executed archive');
      artifacts.add(currentUf2);
      const replay = path.join(directory, `${config.name}-linked-replay.bin`);
      run(path.join(tools, 'elf2bin'), ['-O', 'binary',
        path.join(firmware(config), 'compiler_test.elf'), replay]);
      assert.deepEqual(fs.readFileSync(replay), fs.readFileSync(path.join(firmware(config), 'compiler_test.bin')),
        'linked ELF differs from executed binary');
      artifacts.add(replay);
    }
    for (const source of Object.keys(hardware.sources))
      report.sources.push({ file: source, sha256: sha(path.join(root, source)) });
    for (const tool of ['badge-codegen', 'badge-codegen-attribution', 'badge-asm'])
      artifacts.add(path.join(tools, tool));
    const symbols = Object.fromEntries([own, reference].map(config => {
      const image = path.join(firmware(config), 'compiler_test.elf'); artifacts.add(image);
      return [config.name, symbolSizes(image)];
    }));
    for (const entry of fs.readdirSync(firmware(own)).filter(name => name.endsWith('.o.owned'))) {
      const original = path.join(firmware(own), entry);
      if (!fs.existsSync(path.join(original, 'crossover.json'))) continue;
      const other = path.join(firmware(reference), entry);
      const a = JSON.parse(fs.readFileSync(path.join(original, 'crossover.json'), 'utf8'));
      const b = JSON.parse(fs.readFileSync(path.join(other, 'crossover.json'), 'utf8'));
      assert.equal(a.sha256, b.sha256, `${entry}: optimized IR differs`);
      for (const metadata of [a, b]) {
        assert.equal(sha(path.join(root, metadata.input)), metadata.sha256);
        assert.equal(sha(path.join(root, metadata.object)), metadata.object_sha256);
        artifacts.add(path.join(root, metadata.input)); artifacts.add(path.join(root, metadata.object));
      }
      const input = path.join(root, a.input), stem = entry.slice(0, -8);
      const trace = path.join(directory, `${stem}.trace.s`);
      run(path.join(tools, 'badge-codegen-attribution'), [input, '-o', trace]);
      const replay = path.join(directory, `${stem}.trace.o`);
      run(path.join(tools, 'badge-asm'), [trace, '-o', replay]);
      assert.deepEqual(fs.readFileSync(replay), fs.readFileSync(path.join(root, a.object)),
        `${entry}: trace changes generated object`);
      const traced = traceFunctions(fs.readFileSync(trace, 'utf8'), fs.readFileSync(input, 'utf8'));
      const disassemblies = {};
      for (const [role, metadata] of [['owned', a], ['llvm', b]]) {
        const output = path.join(directory, `${stem}.${role}.dis`);
        const text = run(llvm('llvm-objdump'), ['-d', path.join(root, metadata.object)], output);
        const llvmAssembly = role === 'llvm' ? fs.readFileSync(path.join(other, 'llvm.s'), 'utf8') : '';
        assert(!/\.setfp/.test(llvmAssembly), 'LLVM frame-pointer attribution requires explicit base mapping');
        disassemblies[role] = disassemblyFunctions(text, role === 'owned' ? ['r7'] : ['sp']);
      }
      const functions = traced.map(fn => {
        const rows = Object.fromEntries(['owned', 'llvm'].map(role => {
          const dis = disassemblies[role].find(row => row.name === fn.name);
          assert(dis, `${entry}/${fn.name}/${role}: missing disassembly`);
          const annotations = role === 'llvm' ?
            llvmSpillAnnotations(fs.readFileSync(path.join(other, 'llvm.s'), 'utf8'))[fn.name] : undefined;
          return [role, { ...dis, ...(annotations ? { allocator_annotations: annotations } : {}),
            linked: symbols[role].has(fn.name),
            linked_symbol: symbols[role].get(fn.name) ?? null }];
        }));
        return { ...fn, inventory: emissionInventory(fn), ...rows };
      });
      artifacts.add(trace); artifacts.add(replay);
      artifacts.add(path.join(original, 'crossover.json')); artifacts.add(path.join(other, 'crossover.json'));
      artifacts.add(path.join(other, 'llvm.s'));
      report.modules.push({ module: entry, source: a.source, optimized_ir_sha256: a.sha256,
        commands: [a, b], functions });
    }
    assert(report.modules.length >= 8, 'incomplete application crossover');
    report.opportunities = [
      { rank: 1, mechanism: 'Single-use adjacent i32 icmp/conditional-br fusion',
        reached_work: ['maze-ram-render', 'maze-generation', 'maze-physics-input', 'maze-reached-frame'],
        selection: 'Bounded first PO03 change; preserve existing PHI-edge transfer implementation, compare signedness and APSR boundaries.',
        experiment: 'Count eliminated boolean materialization, normalization and second compare; then run the same paired application campaigns and full semantic/ABI/corpus gates. No default promotion from static counts.' },
      { rank: 2, mechanism: 'SSA home and PHI transfer traffic',
        reached_work: ['maze-generation', 'maze-physics-input', 'maze-ram-render'],
        selection: 'PO04 after liveness/edge investigation; no blanket claim that scalar home traffic is eviction spilling.' },
      { rank: 3, mechanism: 'Leaf/call prologues and operand register copies',
        reached_work: ['maze-display-transfer', 'maze-reached-frame'],
        selection: 'PO07/general ABI economics; volatile GPIO access count/order must remain unchanged. No helper-name special casing.' },
      { rank: 4, mechanism: 'Global address literal pools, narrowing and small aggregate-memory expansion',
        reached_work: ['maze-generation', 'maze-ram-render', 'production-diagnostics-graphics'],
        selection: 'Investigate independently; rejected as first blanket fix because D06 regressed and access alignment/overlap/volatile legality differs.' },
    ];
    report.hardware_summary = hardware.summary;
    report.limitations = ['Static instructions are disassembled machine instructions, not dynamically retired counts.',
      'Literal pools/alignment are reported separately where disassembler marks data; linked symbol extent is not executable-byte count.',
      'Frame categories classify direct r7 accesses; indirect frame/object accesses remain in total memory counts, not guessed spills.',
      'SSA homes combine parameters and nonresident values sharing colors; allocator spilled count is not a dynamic eviction count.',
      'Phi buffers/edge homes, entry homes, private objects, inline-asm preservation metadata and required volatile IR remain separate.',
      'Hardware timing is per reached workload, not sampled per-function CPU attribution; no cycle attribution to individual IR operations.',
      'LLVM machine legalization/combine/allocation passes remain enabled; only the normal LLVM IR optimization pipeline is disabled.',
      'Physical IMU, visual/electrical panel and controlled cold-XIP remain unqualified.'];
    report.status = 'qualified-fixed-ir-attribution';
  } catch (error) {
    report.status = 'failed'; report.errors.push(error.message); process.exitCode = 1;
  }
  for (const entry of fs.readdirSync(directory)) {
    const artifact = path.join(directory, entry);
    if (fs.statSync(artifact).isFile() && entry !== 'results.json') artifacts.add(artifact);
  }
  report.artifacts = [...artifacts].map(artifact => ({ file: relative(artifact), sha256: sha(artifact) }));
  save(file, report);
  console.log(`${report.status}: ${file}; ${report.modules.length} modules`);
  if (report.errors.length) console.error(report.errors.join('\n'));
}

export function verifyAttribution(file) {
  const report = JSON.parse(fs.readFileSync(file, 'utf8'));
  assert.equal(report.status, 'qualified-fixed-ir-attribution');
  for (const artifact of [...report.sources, ...report.artifacts])
    assert.equal(sha(path.join(root, artifact.file)), artifact.sha256, artifact.file);
  assert.equal(sha(path.join(root, report.hardware)), report.hardware_sha256);
  const hardware = verifyApplicationReport(path.join(root, report.hardware));
  assert.equal(hardware.fixed_ir, true);
  assert.deepEqual(report.hardware_summary, hardware.summary);
  for (const module of report.modules) {
    const stem = module.module.slice(0, -8);
    const directory = path.dirname(file);
    const trace = traceFunctions(fs.readFileSync(path.join(directory, `${stem}.trace.s`), 'utf8'),
      fs.readFileSync(path.join(root, module.commands[0].input), 'utf8'));
    const dis = Object.fromEntries(['owned', 'llvm'].map(role => [role, disassemblyFunctions(
      fs.readFileSync(path.join(directory, `${stem}.${role}.dis`), 'utf8'), role === 'owned' ? ['r7'] : ['sp'])]));
    for (const fn of module.functions) {
      const traced = trace.find(row => row.name === fn.name);
      assert(traced, 'missing traced function');
      assert.deepEqual(fn.inventory, emissionInventory(traced));
      for (const key of Object.keys(traced)) assert.deepEqual(fn[key], traced[key], `${fn.name}: trace ${key}`);
      for (const role of ['owned', 'llvm']) {
        const machine = dis[role].find(row => row.name === fn.name);
        for (const key of Object.keys(machine)) assert.deepEqual(fn[role][key], machine[key],
          `${fn.name}/${role}: disassembly ${key}`);
      }
    }
  }
  return report;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2);
  try {
    if (args[0] === '--cc') compile(args[1], args.slice(2));
    else if (args[0] === '--capture' && args.length === 3) capture(path.resolve(args[1]), path.resolve(args[2]));
    else if (args[0] === '--verify' && args.length === 2) {
      verifyAttribution(path.resolve(args[1])); console.log('PASS: fixed-IR attribution and hardware artifacts.');
    } else throw new Error('Usage: backend-attribution.mjs --capture NEW_DIRECTORY FIXED_IR_HARDWARE.json | --verify REPORT.json');
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
