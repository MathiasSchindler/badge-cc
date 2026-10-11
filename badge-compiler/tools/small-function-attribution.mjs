import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { backendFlags, disassemblyFunctions, traceFunctions, emissionInventory } from './backend-attribution.mjs';
import { executable } from './pipeline-stages.mjs';
import { decodeName, parseAssembly } from './benchmark-c06.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const json = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const names = ['delay_cycles', 'write_byte', 'command', 'commands', 'fill', 'bare_main'];
const ownedNames = ['badge-ir', 'badge-opt', 'badge-codegen', 'badge-asm', 'badge-link', 'badge-elf-info', 'elf2bin'];
const relative = file => path.relative(root, file);
const policy = { expression_lowering: 'ssa',
  optimizer: 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory',
  scalar_selection: 'thumb-calls', frame_addressing: 'direct',
  register_residency: 'global-compact', memory_selection: 'word' };
export const qualificationContract = {
  baseline: policy,
  mechanism_targets: { unnecessary_leaf_frame_work: 0, proved_nop_live_value_spills: 0,
    supported_branch_only_boolean_round_trips: 0 },
  static_gate: { production_modules: 37, ordinary_images: 12, mixed_abi: true,
    frame_call_and_spill_accounting: true, changes_require_general_production_benefit: true },
  hardware_gate: { profiles: ['baseline', 'candidate'], seeds: [3276377847, 123456789],
    repeats: 2, swap_board_roles: true, exact_noise_and_control_drift: true,
    full_flash_restoration: true, delay_and_strobe_audit_when_affected: true },
  reuse: { build_once_per_source_tool_policy_image_identity: true,
    runtime_seed_repeat_and_board_role_are_not_build_keys: true,
    verify_hashes_before_reuse: true, unchanged_qualified_images_need_no_reflash: true },
  limitations: ['PO10 is static attribution, not runtime qualification or promotion.',
    'Future semantic/emission changes need relevant execution/corpus gates.',
    'No historical seven-profile rerun solely for a host-analysis change.',
    'No benchmark-name, MMIO-address or known-answer special casing.'],
};

export function inventoryFunctions(ir) {
  assert(!ir.includes('\0'), 'NUL in IR inventory');
  const rows = [];
  const pattern = /^define ([^\n]+)\{\n([\s\S]*?)^}/gm;
  for (const match of ir.matchAll(pattern)) {
    const signature = match[1], body = match[2];
    const name = signature.match(/(@(?:"(?:[^"\\]|\\[0-9a-f]{2})*"|[-\w.$]+))\(/i)?.[1];
    assert(name, 'unrecognized function signature');
    const instructions = body.split('\n').filter(line => /^\s+\S/.test(line)).map(line => line.trim());
    const blocks = [...body.matchAll(/^([-\w.$]+):/gm)].map(m => m[1]);
    const definitions = new Set([...signature.matchAll(/(%(?:"[^"]+"|[-\w.$]+))(?=\s*[,)]|\s*$)/g)].map(m => m[1]));
    for (const line of instructions) {
      const defined = line.match(/^(%\S+) = /)?.[1];
      if (defined) definitions.add(defined);
    }
    const lastUse = new Map(), firstDefinition = new Map();
    instructions.forEach((line, index) => {
      const defined = line.match(/^(%\S+) = /)?.[1];
      if (defined) firstDefinition.set(defined, index);
      for (const operand of line.replace(/^(%\S+) = /, '').matchAll(/%(?:"[^"]+"|[-\w.$]+)/g))
        if (definitions.has(operand[0])) lastUse.set(operand[0], index);
    });
    const ordinaryCalls = instructions.filter(line => /\bcall\b/.test(line) && !/\basm\b/.test(line));
    const assembly = instructions.filter(line => /\bcall\b.*\basm\b/.test(line));
    const callLive = new Set();
    instructions.forEach((line, index) => {
      if (!/\bcall\b/.test(line) || /\basm\b/.test(line)) return;
      for (const [value, use] of lastUse)
        if ((firstDefinition.get(value) ?? -1) < index && use > index) callLive.add(value);
    });
    let block = -1, backwardEdges = 0;
    for (const line of body.split('\n')) {
      const label = line.match(/^([-\w.$]+):/)?.[1];
      if (label) block = blocks.indexOf(label);
      for (const destination of line.matchAll(/\blabel %([-\w.$]+)/g))
        if (blocks.indexOf(destination[1]) >= 0 && blocks.indexOf(destination[1]) <= block) backwardEdges++;
    }
    let branchOnlyComparisons = 0, pointerPredicates = 0;
    for (let index = 0; index + 1 < instructions.length; ++index) {
      const compare = instructions[index].match(/^(%\S+) = icmp \w+ (\w+) /);
      if (compare && instructions[index + 1].startsWith(`br i1 ${compare[1]},`)) {
        branchOnlyComparisons++;
        pointerPredicates += Number(compare[2] === 'ptr');
      }
    }
    const nop = assembly.filter(line =>
      /asm sideeffect "nop(?:\\0Anop)*", "(?:|~\{memory\})"\(\)/.test(line));
    rows.push({ function: decodeName(name), ir_instructions: instructions.length, blocks: blocks.length,
      no_ordinary_calls: ordinaryCalls.length === 0, ordinary_calls: ordinaryCalls.length,
      assembly_operations: assembly.length, proved_nop_text_candidates: nop.length,
      unknown_assembly_operations: assembly.length - nop.length,
      volatile_operations: instructions.filter(line => /\b(?:load|store) volatile\b/.test(line)).length,
      constant_pointer_conversions: instructions.filter(line => /\binttoptr i32 \d+ to ptr/.test(line)).length,
      backward_textual_edges: backwardEdges, adjacent_branch_predicates: branchOnlyComparisons,
      adjacent_pointer_predicates: pointerPredicates,
      byte_loads: instructions.filter(line => /\bload(?: volatile)? i8\b/.test(line)).length,
      scalar_arithmetic_in_loop_bearing_functions: backwardEdges ? instructions.filter(line =>
        / = (?:add|sub|mul|and|or|xor|shl|lshr|ashr) i\d+ /.test(line)).length : 0,
      lexically_call_live_values: [...callLive].sort(),
      fact_limits: ['Textual backedges and call-live candidates are an inventory, not CFG/dominance or alias proofs.',
        'NOP text candidates are not permission to weaken unknown assembly or memory barriers.'] });
  }
  assert.equal(rows.length, [...ir.matchAll(/^define /gm)].length, 'unparsed function inventory');
  assert.equal(new Set(rows.map(row => row.function)).size, rows.length, 'duplicate function inventory');
  return rows;
}

export function instructionLedger(disassembly, symbols, frameBases = ['sp']) {
  return symbols.filter(symbol => symbol.type === 2 && symbol.size && symbol.section < 0xff00).map(symbol => {
    const start = symbol.value & ~1, end = start + symbol.size;
    const fn = disassembly.find(fn => fn.name === symbol.name);
    assert(fn, `missing disassembly: ${symbol.name}`);
    const instructions = fn.instructions.filter(row => row.address >= start && row.address < end);
    const data = fn.data.filter(row => row.address >= start && row.address < end);
    const categories = {};
    for (const row of instructions) {
      assert(row.address + row.bytes <= end, 'instruction crosses function boundary');
      const op = row.mnemonic.split('.')[0], operands = row.operands;
      let category = 'other_computation';
      if (['push', 'pop', 'stmdb', 'ldmia'].includes(op)) category = 'register_save_restore';
      else if (/^(?:mov|add|sub)/.test(op) && /\bsp\b/.test(operands)) category = 'stack_or_frame_base_arithmetic';
      else if (/^(?:ldr|str)/.test(op)) {
        const stack = frameBases.some(base => new RegExp(`\\[${base}(?:,|\\])`).test(operands));
        category = stack ? 'frame_memory' : op.startsWith('ldr') && /\bpc\b/.test(operands) ?
          'pc_relative_literal_load' : 'non_frame_memory';
      } else if (['bl', 'blx'].includes(op)) category = 'calls';
      else if (/^b(?:eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?$/.test(op) ||
        ['bx', 'cbz', 'cbnz', 'tbb', 'tbh'].includes(op)) category = 'branches_or_returns';
      else if (['ubfx', 'sbfx', 'uxtb', 'uxth', 'sxtb', 'sxth'].includes(op)) category = 'extensions';
      else if (['movw', 'movt', 'mvn', 'mvns'].includes(op) ||
        ['mov', 'movs'].includes(op) && /#/.test(operands)) category = 'constant_materialization';
      else if (['mov', 'movs'].includes(op)) category = 'register_copies';
      else if (['cmp', 'cmn', 'tst', 'it'].includes(op)) category = 'predicate_or_flag_control';
      else if (['nop', 'wfi', 'dmb', 'dsb', 'isb'].includes(op)) category = 'explicit_assembly_or_barrier';
      categories[category] = (categories[category] ?? 0) + row.bytes;
    }
    const covered = [...instructions, ...data].sort((a, b) => a.address - b.address);
    for (let index = 1; index < covered.length; ++index)
      assert(covered[index - 1].address + covered[index - 1].bytes <= covered[index].address, 'overlapping disassembly');
    const literalBytes = data.reduce((sum, row) => sum + row.bytes, 0);
    const instructionBytes = Object.values(categories).reduce((a, b) => a + b, 0);
    const unresolved = symbol.size - instructionBytes - literalBytes;
    assert(unresolved >= 0, 'instruction accounting exceeds ELF symbol');
    return { function: symbol.name, symbol_bytes: symbol.size, instruction_bytes: instructionBytes,
      literal_bytes: literalBytes, unresolved_bytes: unresolved, categories,
      static_instruction_count: instructions.length,
      unknowns: unresolved ? ['Bytes not decoded inside the function; not assumed padding or execution.'] : [] };
  });
}

export function reconcileImage(metrics, rawBytes, functions) {
  assert.equal(metrics.data, 0, 'attribution startup cannot initialize data');
  assert.equal(metrics.bss, 0, 'attribution startup cannot initialize BSS');
  assert.equal(metrics.text, rawBytes, 'ELF/raw byte mismatch');
  const cBytes = functions.reduce((sum, fn) => sum + fn.symbol_bytes, 0);
  const constantBytes = metrics.symbols.filter(symbol => symbol.type === 1 && symbol.size &&
    metrics.sections[symbol.section]?.address >= 0x10000000 &&
    metrics.sections[symbol.section]?.address < 0x11000000).reduce((sum, symbol) => sum + symbol.size, 0);
  const other = rawBytes - cBytes - constantBytes;
  assert(other >= 0, 'function/data symbols exceed loaded bytes');
  return { loaded_bytes: rawBytes, c_function_bytes: cBytes, constant_object_bytes: constantBytes,
    startup_metadata_and_inter_symbol_bytes: other,
    unresolved_function_bytes: functions.reduce((sum, fn) => sum + fn.unresolved_bytes, 0) };
}

function analyze(report) {
  const read = file => fs.readFileSync(path.join(root, file), 'utf8');
  const production = report.production.map(module => {
    const assembly = read(module.assembly);
    const frames = parseAssembly(assembly, module.module);
    const rows = inventoryFunctions(read(module.ir)).map(row => {
      const frame = frames.get(row.function);
      assert(frame, `${module.module}/${row.function}: missing frame inventory`);
      return { ...row, module: module.module, source: module.source,
        frame_bytes: frame.frame_bytes, saved_bytes: frame.saves_bytes,
        max_outgoing_bytes: frame.max_outgoing_bytes, frame_unknowns: frame.unknown };
    });
    assert.equal(rows.length, frames.size, 'IR/frame function inventory differs');
    return { module: module.module, functions: rows };
  });
  const stages = report.stages.map(stage => {
    const metrics = json(path.join(root, stage.metrics));
    const disassembly = disassemblyFunctions(read(stage.disassembly));
    const llvmAssembly = stage.role === 'owned' ? '' : read(stage.assembly);
    const setfp = llvmAssembly.match(/\.setfp\s+(\w+),\s*sp/);
    const bases = stage.role === 'owned' ? ['r7'] : ['sp', ...(setfp ? [setfp[1]] : [])];
    const functions = instructionLedger(disassembly, metrics.symbols, bases);
    const rawBytes = fs.statSync(path.join(root, stage.binary)).size;
    return { id: stage.id, ...reconcileImage(metrics, rawBytes, functions), functions };
  });
  const fixed = stages.find(stage => stage.id === 'owned-fixed');
  const llvmFixed = stages.find(stage => stage.id === 'llvm-fixed');
  const recovered = stages.find(stage => stage.id === 'llvm-recovered');
  const frontend = stages.find(stage => stage.id === 'llvm-frontend');
  const reference = stages.find(stage => stage.id === 'llvm-reference');
  assert(fixed && llvmFixed && recovered && frontend && reference, 'missing crossover stage');
  const helpers = names.map(name => {
    const rows = Object.fromEntries(stages.map(stage => [stage.id, stage.functions.find(fn => fn.function === name) ?? null]));
    assert(rows['owned-fixed'] && rows['llvm-fixed'] && rows['llvm-reference'], `missing fixed helper: ${name}`);
    return { function: name, original_gap_bytes: rows['owned-fixed'].symbol_bytes - rows['llvm-reference'].symbol_bytes,
      fixed_ir_backend_gap_bytes: rows['owned-fixed'].symbol_bytes - rows['llvm-fixed'].symbol_bytes,
      stages: rows,
      unknowns: ['Helper-local sizes after whole-module optimization are not additive marginal optimization savings.',
        ...(['llvm-recovered', 'llvm-frontend'].filter(id => !rows[id]).length ?
          ['Some optimized helpers were eliminated/inlined; use full-image accounting rather than invented zero-byte bodies.'] : [])] };
  });
  const traces = traceFunctions(read(report.trace.assembly), read(report.trace.ir));
  assert.equal(traces.length, names.length);
  const traced = traces.map(fn => ({ ...fn, inventory: emissionInventory(fn) }));
  const rows = production.flatMap(module => module.functions);
  const patterns = {
    leaf_candidates: row => row.no_ordinary_calls,
    assembly_bearing: row => row.assembly_operations > 0,
    nop_text_candidates: row => row.proved_nop_text_candidates > 0,
    volatile_io: row => row.volatile_operations > 0,
    constant_address_materialization: row => row.constant_pointer_conversions > 0,
    pointer_branch_predicates: row => row.adjacent_pointer_predicates > 0,
    loop_bearing_candidates: row => row.backward_textual_edges > 0,
    lexical_call_live_candidates: row => row.lexically_call_live_values.length > 0,
    zero_local_frames_with_saves: row => row.frame_bytes === 0 && row.saved_bytes > 0,
  };
  const inventory = Object.fromEntries(Object.entries(patterns).map(([name, select]) => {
    const matches = rows.filter(select);
    assert(matches.length, `no production examples: ${name}`);
    return [name, { functions: matches.length, modules: new Set(matches.map(row => row.module)).size,
      examples: matches.slice(0, 12).map(row => ({ module: row.module, function: row.function, source: row.source })) }];
  }));
  const contributions = {
    fixed_ir_backend_bytes: fixed.loaded_bytes - llvmFixed.loaded_bytes,
    optimizer_recovery_on_owned_initial_ir_bytes: llvmFixed.loaded_bytes - recovered.loaded_bytes,
    frontend_and_attribute_difference_bytes: recovered.loaded_bytes - frontend.loaded_bytes,
    explicit_pipeline_vs_archived_clang_reference_bytes: frontend.loaded_bytes - reference.loaded_bytes,
  };
  assert.equal(Object.values(contributions).reduce((a, b) => a + b, 0), fixed.loaded_bytes - reference.loaded_bytes);
  assert.equal(helpers.reduce((sum, fn) => sum + fn.original_gap_bytes, 0),
    fixed.c_function_bytes - reference.c_function_bytes);
  return { stages, helpers, traced, production, inventory, contributions,
    production_modules: production.length, production_functions: rows.length,
    original_loaded_gap_bytes: fixed.loaded_bytes - reference.loaded_bytes,
    limits: ['Signed stage deltas telescope but are not unique independent causal shares.',
      'Frontend delta includes different lowering and semantic/optimization attributes.',
      'The explicit opt pipeline is not claimed byte-identical to Clang internal -Oz.',
      'Instruction categories are static byte observations, not dynamic cycles or alias proofs.',
      'NOP/loop/call-live inventory candidates require PO11-PO15 legality proofs.',
      'No badge flashing, new runtime measurement or default promotion.'] };
}

export function verifySmallFunctionReport(file, current = false) {
  const report = json(file);
  assert.equal(report.schema, 1);
  assert.equal(report.milestone, 'PO10');
  assert.equal(report.status, 'qualified-static-attribution', report.error ?? report.status);
  assert.deepEqual(report.contract, qualificationContract, 'qualification contract drift');
  assert.equal(report.production.length, report.contract.static_gate.production_modules);
  assert.equal(report.analysis.production_functions, 554, 'incomplete production function inventory');
  assert.deepEqual(report.stages.map(stage => stage.id),
    ['owned-fixed', 'llvm-fixed', 'llvm-recovered', 'llvm-frontend', 'llvm-reference'], 'stage inventory drift');
  assert.equal(new Set(report.artifacts.map(row => row.file)).size, report.artifacts.length, 'duplicate artifacts');
  const artifacts = new Map(report.artifacts.map(row => [row.file, row.sha256]));
  for (const [file, digest] of artifacts) assert.equal(sha(path.join(root, file)), digest, file);
  for (const stage of report.stages) {
    for (const key of ['ir', 'assembly', 'object', 'image', 'binary', 'metrics', 'disassembly'])
      assert(artifacts.has(stage[key]), `untracked stage ${key}`);
    assert.equal(sha(path.join(root, stage.ir)), stage.input_sha256);
  }
  assert.equal(report.stages.find(stage => stage.id === 'owned-fixed').input_sha256,
    report.stages.find(stage => stage.id === 'llvm-fixed').input_sha256, 'fixed IR differs');
  for (const module of report.production)
    for (const key of ['ir', 'assembly', 'source_snapshot']) assert(artifacts.has(module[key]), 'untracked inventory input');
  assert.equal(report.trace.object_equal_to_archive, true, 'trace changed object');
  for (const key of ['assembly', 'ir', 'object', 'reference_object'])
    assert(artifacts.has(report.trace[key]), `untracked trace ${key}`);
  assert.deepEqual(fs.readFileSync(path.join(root, report.trace.object)),
    fs.readFileSync(path.join(root, report.trace.reference_object)), 'trace object differs');
  for (const key of ['object', 'image', 'binary'])
    assert(artifacts.has(report.assembler_control[key]), `untracked assembler control ${key}`);
  const llvmFixed = report.stages.find(stage => stage.id === 'llvm-fixed');
  assert.equal(report.assembler_control.loaded_equal, true, 'assembler control not equal');
  assert.equal(report.assembler_control.input_assembly, llvmFixed.assembly, 'assembler control input differs');
  assert.equal(report.assembler_control.reference_binary, llvmFixed.binary, 'assembler control reference differs');
  assert.deepEqual(fs.readFileSync(path.join(root, report.assembler_control.binary)),
    fs.readFileSync(path.join(root, llvmFixed.binary)),
    'same LLVM assembly changed loaded bytes across assemblers');
  assert.deepEqual(report.analysis, analyze(report), 'attribution/inventory does not recompute');
  if (current) for (const source of report.sources)
    assert.equal(sha(path.resolve(root, source.file)), source.sha256, `current source/tool drift: ${source.file}`);
  return report;
}

export function captureSmallFunctions(directory, options = {}) {
  fs.mkdirSync(directory);
  const report = { schema: 1, milestone: 'PO10', status: 'in-progress', root,
    contract: qualificationContract, started_at: new Date().toISOString(),
    artifacts: [], sources: [], commands: [], stages: [], production: [] };
  const reportFile = path.join(directory, 'results.json');
  const save = () => fs.writeFileSync(reportFile, JSON.stringify(report, null, 2) + '\n');
  save();
  const files = new Set();
  const track = file => { files.add(path.resolve(file)); return file; };
  const out = name => track(path.join(directory, name));
  const frozen = (source, destination, expected) => {
    const digest = sha(source);
    if (expected) assert.equal(digest, expected, `input drift: ${source}`);
    fs.copyFileSync(source, destination);
    assert.equal(sha(destination), digest);
    track(destination);
    return destination;
  };
  const run = (command, args, label, cwd = root) => {
    const started = Date.now();
    const result = spawnSync(command, args, { cwd, encoding: 'utf8', timeout: 180000, maxBuffer: 64 * 1024 * 1024 });
    const log = out(`${String(report.commands.length).padStart(3, '0')}-${label}.log`);
    fs.writeFileSync(log, `${result.stdout ?? ''}${result.stderr ?? ''}${result.error ?? ''}`);
    report.commands.push({ executable: command, args, cwd, log: relative(log), elapsed_ms: Date.now() - started,
      status: result.status, error: result.error?.message ?? null });
    save();
    assert.ifError(result.error);
    assert.equal(result.status, 0, `${label}: ${result.stderr}`);
    return result.stdout;
  };
  try {
    const squarePath = path.resolve(root, options.squareReport ?? 'build/red-square-results.json');
    const corpusPath = path.resolve(root, options.corpusReport ?? 'badge-compiler/build/po09-finished-corpus/offline.json');
    const replayPath = path.resolve(root, options.replayReport ?? 'badge-compiler/build/po09-promoted-replay/results.json');
    const square = json(squarePath), corpus = json(corpusPath), replay = json(replayPath);
    assert.deepEqual(square.owned_defaults, policy, 'red-square baseline policy');
    assert.equal(corpus.status, 'offline-qualified');
    assert.equal(replay.status, 'default-replay-equal');
    const candidate = corpus.candidates.find(candidate => candidate.id === 'po06-po07');
    assert(candidate && candidate.modules.length === 37 && candidate.images.length === 12, 'incomplete production corpus');
    fs.mkdirSync(path.join(directory, 'tools'));
    const tools = {};
    for (const name of ownedNames) {
      const source = path.join(root, 'badge-compiler/build', name);
      tools[name] = frozen(source, path.join(directory, 'tools', name), square.owned_tools[name]);
      if (replay.compiler_hashes[name]) assert.equal(sha(source), replay.compiler_hashes[name], 'promoted tool differs');
      report.sources.push({ file: relative(source), sha256: sha(source) });
    }
    report.llvm = {};
    for (const [name, variable] of [['clang', 'LLVM_CLANG'], ['opt', 'LLVM_OPT'],
      ['llvm-objdump', 'LLVM_OBJDUMP'], ['ld.lld', 'LLVM_LLD']]) {
      const source = executable(process.env[variable] ??
        (process.env.LLVM_BIN ? path.join(process.env.LLVM_BIN, name) : name));
      const archive = frozen(source, path.join(directory, 'tools', name));
      report.llvm[name] = { executable: source, resolved_file: fs.realpathSync(source),
        archive: relative(archive), sha256: sha(source),
        version: run(source, ['--version'], `${name}-version`) };
      report.sources.push({ file: source, sha256: sha(source) });
    }
    report.tool_limits = ['Owned tools are executed from immutable capture copies.',
      'LLVM executable bytes/versions are archived; commands use original installation paths for dynamic libraries.',
      'OS/LLVM dynamic libraries are not a self-contained archived runtime; historical verification rechecks persisted bytes, not execution.'];
    for (const [file, digest] of Object.entries(square.sources))
      assert.equal(sha(path.join(root, file)), digest, `red-square source drift: ${file}`);
    for (const [name, source] of [['square-report.json', squarePath], ['corpus-report.json', corpusPath],
      ['replay-report.json', replayPath]]) frozen(source, out(name));
    const ownImage = square.images.find(image => image.compiler === 'owned' && image.variant === 'red_square');
    const llvmImage = square.images.find(image => image.compiler === 'llvm' && image.variant === 'red_square');
    assert(ownImage && llvmImage);
    fs.mkdirSync(path.join(directory, 'reference'));
    for (const [role, image] of [['owned', ownImage], ['llvm', llvmImage]]) {
      for (const [file, digest] of Object.entries(image.files))
        frozen(path.join(root, file), path.join(directory, 'reference', `${role}-${path.basename(file)}`), digest);
    }
    const original = path.join(root, 'build/red-square-owned/universe26/firmware/red_square.o.owned');
    const initial = frozen(path.join(original, 'frontend.ll'), out('initial.ll'));
    const fixed = frozen(path.join(original, 'optimized.ll'), out('fixed.ll'));
    const referenceObject = frozen(original.slice(0, -6), out('reference.o'));
    const reproduced = out('reproduced.ll');
    run(tools['badge-opt'], [initial, '-o', reproduced], 'optimizer-replay');
    assert.deepEqual(fs.readFileSync(fixed), fs.readFileSync(reproduced), 'default optimizer changed archived IR');
    const traceTool = path.join(directory, 'tools/badge-codegen-attribution');
    run('make', ['-C', 'badge-compiler', `ATTRIBUTION_BINARY=${traceTool}`, traceTool], 'trace-build');
    track(traceTool);
    const trace = out('trace.s'), traceObject = out('trace.o');
    run(traceTool, [fixed, '-o', trace], 'trace');
    run(tools['badge-asm'], [trace, '-o', traceObject], 'trace-assemble');
    assert.deepEqual(fs.readFileSync(traceObject), fs.readFileSync(referenceObject), 'trace changed archived object');
    report.trace = { assembly: relative(trace), ir: relative(fixed), object: relative(traceObject),
      reference_object: relative(referenceObject), object_equal_to_archive: true };
    fs.mkdirSync(path.join(directory, 'production'));
    for (const module of candidate.modules) {
      const folder = path.join(directory, 'production', module.module);
      fs.mkdirSync(folder);
      const replayed = replay.modules.find(row => row.module === module.module);
      assert(replayed, 'missing promoted inventory module');
      assert.equal(module.sha256, replayed.optimized_sha256);
      assert.equal(module.assembly_sha256, replayed.assembly_sha256);
      const ir = frozen(path.join(root, module.file), path.join(folder, 'optimized.ll'), module.sha256);
      const assembly = frozen(path.join(root, module.assembly), path.join(folder, 'assembly.s'), module.assembly_sha256);
      const sourceManifest = fs.readFileSync(path.join(path.dirname(path.join(root, module.file)), 'source.sha256'), 'utf8');
      const match = sourceManifest.trim().match(/^([0-9a-f]{64})\s+(.+)$/);
      assert(match, 'missing source identity');
      const source = match[2], snapshot = frozen(path.join(root, source), path.join(folder, 'source.c'), match[1]);
      report.sources.push({ file: source, sha256: match[1] });
      report.production.push({ module: module.module, source, source_snapshot: relative(snapshot),
        ir: relative(ir), assembly: relative(assembly) });
    }
    const clang = report.llvm.clang.executable, opt = report.llvm.opt.executable;
    const recovered = out('recovered.ll'), frontend = out('clang-initial.ll'), frontendOptimized = out('clang-optimized.ll');
    run(opt, ['-S', '-passes=default<Oz>', initial, '-o', recovered], 'llvm-recovery');
    run(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz', '-std=c11',
      '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-unwind-tables',
      '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections', '-Isrc/shared',
      '-S', '-emit-llvm', '-Xclang', '-disable-llvm-passes', 'src/tools/red_square.c', '-o', frontend], 'clang-frontend');
    run(opt, ['-S', '-passes=default<Oz>', frontend, '-o', frontendOptimized], 'clang-optimizer');
    const referenceSource = frozen(path.join(root, 'src/tools/red_square.c'), out('reference.c'));
    const start = out('start.o'), picobin = out('picobin.o');
    frozen(path.join(root, 'build/red-square-owned/universe26/firmware/start_red_square.o'), start);
    frozen(path.join(root, 'build/red-square-owned/universe26/firmware/picobin.o'), picobin);
    for (const [id, role, ir] of [['owned-fixed', 'owned', fixed], ['llvm-fixed', 'llvm', fixed],
      ['llvm-recovered', 'llvm', recovered], ['llvm-frontend', 'llvm', frontendOptimized],
      ['llvm-reference', 'llvm', referenceSource]]) {
      const assembly = out(`${id}.s`), object = out(`${id}.o`), image = out(`${id}.elf`), binary = out(`${id}.bin`);
      if (id === 'llvm-reference') {
        frozen(path.join(directory, 'reference/llvm-red_square.elf'), image);
        frozen(path.join(directory, 'reference/llvm-red_square.bin'), binary);
        frozen(path.join(root, 'build/red-square-llvm/universe26/firmware/red_square.o'), object);
        run(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz', '-std=c11',
          '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-unwind-tables',
          '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections',
          '-Isrc/shared', '-S', 'src/tools/red_square.c', '-o', assembly], 'reference-assembly');
      } else {
        run(opt, ['-passes=verify', '-disable-output', ir], `${id}-verify`);
        if (role === 'owned') {
          run(tools['badge-codegen'], [ir, '-o', assembly], `${id}-backend`);
          run(tools['badge-asm'], [assembly, '-o', object], `${id}-assemble`);
          assert.deepEqual(fs.readFileSync(object), fs.readFileSync(referenceObject), 'owned object replay changed');
        } else {
          run(clang, [...backendFlags, '-S', ir, '-o', assembly], `${id}-backend`);
          run(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-c', assembly, '-o', object], `${id}-assemble`);
        }
        run(tools['badge-link'], ['-T', 'src/arch/rp2350/memmap_red_square.ld', start, picobin, object, '-o', image], `${id}-link`);
        run(tools.elf2bin, ['-O', 'binary', image, binary], `${id}-binary`);
        if (role === 'owned') assert.deepEqual(fs.readFileSync(binary),
          fs.readFileSync(path.join(directory, 'reference/owned-red_square.bin')), 'owned loaded replay changed');
      }
      const metrics = out(`${id}.metrics.json`), disassembly = out(`${id}.dis`);
      fs.writeFileSync(metrics, run(tools['badge-elf-info'], ['--metrics', image], `${id}-metrics`));
      fs.writeFileSync(disassembly, run(report.llvm['llvm-objdump'].executable, ['-d', image], `${id}-disassembly`));
      report.stages.push({ id, role, ir: relative(ir), input_sha256: sha(ir), assembly: relative(assembly),
        object: relative(object), image: relative(image), binary: relative(binary), metrics: relative(metrics),
        disassembly: relative(disassembly), normal_llvm_ir_optimizer: ['llvm-recovered', 'llvm-frontend', 'llvm-reference'].includes(id),
        input_note: id === 'llvm-reference' ? 'Archived normal Clang image and its original C source; not a fixed-IR stage.' : 'Exact hashed crossover input.' });
    }
    const sourceFiles = ['Makefile', 'badge-compiler/Makefile', 'src/tools/red_square.c',
      'src/arch/rp2350/memmap_red_square.ld', 'src/arch/rp2350/start_red_square.S', 'src/arch/rp2350/picobin.S',
      'badge-compiler/tools/small-function-attribution.mjs', 'badge-compiler/tools/backend-attribution.mjs',
      'badge-compiler/tools/pipeline-stages.mjs', 'badge-compiler/tools/benchmark-c06.mjs',
      ...fs.readdirSync(path.join(root, 'badge-compiler/src')).filter(name => /\.(c|h)$/.test(name))
        .map(name => `badge-compiler/src/${name}`)];
    report.sources.push(...sourceFiles.map(file => ({ file, sha256: sha(path.join(root, file)) })));
    report.sources = [...new Map(report.sources.map(row => [row.file, row])).values()];
    for (const source of report.sources) assert.equal(sha(path.resolve(root, source.file)), source.sha256, 'source/tool changed during capture');
    const llvmFixed = report.stages.find(stage => stage.id === 'llvm-fixed');
    const controlObject = out('assembler-control.o'), controlImage = out('assembler-control.elf'),
      controlBinary = out('assembler-control.bin');
    run(tools['badge-asm'], [path.join(root, llvmFixed.assembly), '-o', controlObject], 'assembler-control');
    run(tools['badge-link'], ['-T', 'src/arch/rp2350/memmap_red_square.ld', start, picobin,
      controlObject, '-o', controlImage], 'assembler-control-link');
    run(tools.elf2bin, ['-O', 'binary', controlImage, controlBinary], 'assembler-control-binary');
    assert.deepEqual(fs.readFileSync(controlBinary), fs.readFileSync(path.join(root, llvmFixed.binary)),
      'LLVM fixed assembly differs under owned assembler');
    report.assembler_control = { object: relative(controlObject), image: relative(controlImage),
      binary: relative(controlBinary), loaded_equal: true,
      input_assembly: llvmFixed.assembly, reference_binary: llvmFixed.binary };
    report.analysis = analyze(report);
    assert.equal(report.analysis.production_functions, 554, 'incomplete function corpus');
    report.artifacts = [...files].sort().map(file => ({ file: relative(file), sha256: sha(file) }));
    report.status = 'qualified-static-attribution';
    report.finished_at = new Date().toISOString();
    save();
    verifySmallFunctionReport(reportFile, true);
    return report;
  } catch (error) {
    report.status = 'failed';
    report.error = error.message;
    report.artifacts = [...files].filter(file => fs.existsSync(file)).sort().map(file => ({ file: relative(file), sha256: sha(file) }));
    save();
    throw error;
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, ...extra] = process.argv.slice(2);
    assert(file && extra.length === 0 && ['--capture', '--verify', '--verify-current'].includes(mode),
      'Usage: node badge-compiler/tools/small-function-attribution.mjs --capture DIRECTORY | --verify[-current] REPORT.json');
    const report = mode === '--capture' ? captureSmallFunctions(path.resolve(file)) :
      verifySmallFunctionReport(path.resolve(file), mode === '--verify-current');
    console.log(`PASS: PO10 ${report.analysis.helpers.length} helpers, ${report.analysis.production_modules} modules, ` +
      `${report.analysis.production_functions} production functions; ${report.analysis.original_loaded_gap_bytes}-byte archived gap.`);
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
