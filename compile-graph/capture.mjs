import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { invocation, executable, preprocessedFlags } from '../badge-compiler/tools/pipeline-stages.mjs';
import { stages, irRegions, assemblyRegions, objectRegions, astRegions, sourceRegions,
  sections, symbols, inventory } from './analysis.mjs';
import { projectIndex, preprocessingStats, tokens, tokenDifference, conditionalDirectives } from './web/insights.mjs';
import { captureCrossover, verifyCrossover } from '../badge-compiler/tools/assembler-crossover.mjs';

const ownFile = fileURLToPath(import.meta.url);
const root = path.resolve(path.dirname(ownFile), '..');
const tools = path.join(root, 'badge-compiler/build');
const roles = ['owned', 'llvm'];
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const writeJSON = (file, data) => fs.writeFileSync(file, JSON.stringify(data, null, 2) + '\n');
const readJSON = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const quote = value => `'${value.replaceAll("'", "'\\''")}'`;
const llvm = (name, variable) => executable(process.env[variable] ??
  (process.env.LLVM_BIN ? path.join(process.env.LLVM_BIN, name) : name));
const policyVariables = ['BADGE_OPT_PASSES', 'BADGE_IR_EXPRESSION_LOWERING',
  'BADGE_CODEGEN_FRAME_ADDRESSING', 'BADGE_CODEGEN_REGISTER_RESIDENCY',
  'BADGE_CODEGEN_SCALAR_SELECTION', 'BADGE_CODEGEN_MEMORY_SELECTION'];

function run(command, args, log, options = {}) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8',
    timeout: 240000, maxBuffer: 128 * 1024 * 1024, ...options });
  if (log) fs.writeFileSync(log, `${JSON.stringify([command, ...args])}\n${result.stderr ?? ''}`);
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${command} failed (${result.status}): ${result.stderr?.slice(-4000)}`);
  return result.stdout;
}

function wrapper(role, directory, args) {
  assert(roles.includes(role), 'invalid compiler role');
  const parsed = invocation(args), output = path.resolve(root, parsed.output);
  assert(output.startsWith(directory + path.sep), 'compiler output outside capture');
  const config = readJSON(path.join(directory, 'config.json'));
  const command = role === 'owned' ? executable('bash') : config.llvm.clang;
  const actualArgs = role === 'owned' ? ['badge-compiler/scripts/owned-cc.sh', ...args] : args;
  const log = `${output}.command.json`;
  writeJSON(log, { command, args: actualArgs, role, ...parsed, source_hash: parsed.source ?
    sha(path.resolve(root, parsed.source)) : null });
  run(command, actualArgs, `${output}.log`);
  if (!parsed.source?.endsWith('.c')) return;
  const stageDir = `${output}.stages`;
  fs.mkdirSync(stageDir);
  const target = name => path.join(stageDir, name);
  if (role === 'owned') {
    for (const [from, to] of [['preprocessed.i', 'preprocessed.i'], ['frontend.ll', 'initial.ll'],
      ['optimized.ll', 'optimized.ll'], ['assembly.s', 'assembly.s']])
      fs.copyFileSync(`${output}.owned/${from}`, target(to));
    for (const [tool, file] of [['badge-parse', 'ast.json'], ['badge-sema', 'typed.json']])
      run(path.join(tools, tool), [target('preprocessed.i'), '-o', target(file)], target(`${tool}.log`));
    const passes = config.policy.BADGE_OPT_PASSES.split(',');
    for (let i = 0; i < passes.length; i++)
      run(path.join(tools, 'badge-opt'), [`--passes=${passes.slice(0, i + 1).join(',')}`,
        target('initial.ll'), '-o', target(`pass-${i + 1}.ll`)], target(`pass-${i + 1}.log`));
    assert.equal(sha(target(`pass-${passes.length}.ll`)), sha(target('optimized.ll')),
      'optimizer replay differs from production output');
  } else {
    const compile = (extra, file) => run(command, [...parsed.flags, ...extra, parsed.source,
      '-o', target(file)], target(`${file}.log`));
    compile(['-E'], 'preprocessed.i');
    const ast = run(command, [...parsed.flags, '-fsyntax-only', '-Xclang', '-ast-dump=json',
      parsed.source], target('ast.log'));
    writeJSON(target('ast.json'), JSON.parse(ast));
    compile(['-S', '-emit-llvm', '-Xclang', '-disable-llvm-passes'], 'initial.ll');
    compile(['-S', '-emit-llvm'], 'optimized.ll');
    compile(['-S'], 'assembly.s');
  }
  const preprocessingAst = run(config.llvm.clang, [...preprocessedFlags(parsed.flags),
    '-fsyntax-only', '-x', 'cpp-output', '-Xclang', '-ast-dump=json', target('preprocessed.i')],
  target('preprocessed-ast.log'));
  writeJSON(target('preprocessed-ast.json'), JSON.parse(preprocessingAst));
  run(path.join(tools, 'badge-cpp'), ['--tokens', target('preprocessed.i'), '-o',
    target('preprocessed.tokens')], target('tokens.log'));
  fs.writeFileSync(target('object.txt'), run(config.llvm.objdump,
    ['-d', '--triple=thumbv8m.main-none-eabi', output], target('object.log')));
}

function walk(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const file = path.join(directory, entry.name);
    return entry.isDirectory() ? walk(file) : entry.isFile() ? [file] : [];
  });
}

function buildReport(directory, config) {
  const report = { schema: 1, status: 'complete', created: new Date().toISOString(),
    title: 'Compile Graph', stages, policy: config.policy, llvm_version: config.llvm_version,
    provenance: config.provenance, modules: [], images: [], artifacts: [],
    limitations: [
      'End-to-end comparison: frontend, optimizer, assembler and linker can all differ.',
      'Function correspondence uses exact names within a translation unit, not semantic equivalence.',
      'Missing functions are shown as absent at a stage; absence alone does not prove inlining or elimination.',
      'AST formats differ. Clang combines parsing and semantic analysis; its typed AST is shared between both views.',
      'Lowered LLVM IR disables LLVM passes; optimized LLVM IR and assembly are separate reruns of the production flags.',
      'Owned pass snapshots replay prefixes from initial IR and the final snapshot is checked against production IR.',
      'Blocks are discovered independently. Label or block order does not establish cross-compiler correspondence.',
      'Source/preprocessing ranges come from Clang AST byte offsets; no source-to-IR or instruction mapping is claimed.',
      'Text bytes and static counts are not execution time, optimization scores or semantic correctness evidence.',
      'Object code can include functions removed by linking. Final image sizes exclude ELF/debug and UF2 container overhead.',
    ] };
  const rel = file => path.relative(directory, file).split(path.sep).join('/');
  const artifact = file => {
    const entry = { file: rel(file), bytes: fs.statSync(file).size, sha256: sha(file) };
    report.artifacts.push(entry);
    return entry;
  };
  const stage = (file, format, regionRole, note = '') => {
    const text = fs.readFileSync(file, 'utf8');
    let regions = [];
    if (format === 'ast') regions = astRegions(JSON.parse(text), regionRole);
    else if (format === 'ir') regions = irRegions(text);
    else if (format === 'assembly') regions = assemblyRegions(text);
    else if (format === 'object') regions = objectRegions(text);
    return { ...artifact(file), format, regions, note, lines: text.split('\n').length };
  };
  const buildFiles = roles.flatMap(role => walk(path.join(directory, `${role}-build`)));
  const records = buildFiles.filter(file => file.endsWith('.command.json')).map(file => ({
    ...readJSON(file), recordFile: file,
  }));
  const compiles = records.filter(record => record.role === 'owned' && record.source?.endsWith('.c'));
  for (const owned of compiles.sort((a, b) => a.source.localeCompare(b.source))) {
    const peer = records.find(r => r.role === 'llvm' && r.source === owned.source &&
      path.basename(r.output) === path.basename(owned.output));
    assert(peer, `missing LLVM peer for ${owned.source}`);
    assert.equal(peer.source_hash, owned.source_hash, 'source differs between builds');
    assert.equal(sha(path.join(root, owned.source)), owned.source_hash, 'source changed during capture');
    const module = { id: path.basename(owned.output), source: owned.source, roles: {} };
    for (const record of [owned, peer]) {
      const role = record.role, dir = path.resolve(root, `${record.output}.stages`);
      const file = name => path.join(dir, name);
      fs.copyFileSync(path.join(root, record.source), file('source.c'));
      const s = {
        source: stage(file('source.c'), 'source', role),
        preprocessed: stage(file('preprocessed.i'), 'text', role,
          'Whole translation unit; line markers retained. Text size is not firmware size.'),
        ast: stage(file('ast.json'), 'ast', role),
        typed: stage(file(role === 'owned' ? 'typed.json' : 'ast.json'), 'ast', role,
          role === 'llvm' ? 'Clang parser and typed AST share this artifact.' : ''),
        initial: stage(file('initial.ll'), 'ir', role),
        optimized: stage(file('optimized.ll'), 'ir', role),
        assembly: stage(file('assembly.s'), 'assembly', role),
        object: stage(file('object.txt'), 'object', role),
      };
      s.source.regions = sourceRegions(fs.readFileSync(file('source.c'), 'utf8'),
        readJSON(path.resolve(root, `${peer.output}.stages/ast.json`)), record.source);
      const passes = role === 'owned' ? config.policy.BADGE_OPT_PASSES.split(',').map((name, i) => ({
        name, ...stage(file(`pass-${i + 1}.ll`), 'ir', role),
      })) : [];
      const sectionText = run(config.llvm.size, ['-A', path.resolve(root, record.output)]);
      const symbolText = run(config.llvm.nm, ['-S', '--defined-only', path.resolve(root, record.output)]);
      const sourceMappingFile = path.resolve(root, `${peer.output}.stages/ast.json`);
      s.source.mapping_artifact = artifact(sourceMappingFile).file;
      s.source.mapping_filename = record.source;
      const preprocessingMappingFile = file('preprocessed-ast.json');
      s.preprocessed.mapping_artifact = artifact(preprocessingMappingFile).file;
      s.preprocessed.mapping_filename = file('preprocessed.i');
      s.preprocessed.token_artifact = artifact(file('preprocessed.tokens')).file;
      s.preprocessed.presentation = preprocessingStats(fs.readFileSync(file('preprocessed.i'), 'utf8'));
      s.preprocessed.regions = sourceRegions(fs.readFileSync(file('preprocessed.i'), 'utf8'),
        readJSON(preprocessingMappingFile), file('preprocessed.i'));
      s.preprocessed.note = 'Regions use physical offsets in the archived preprocessed input, parsed with Clang. Line markers are retained.';
      const sectionFile = file('sections.txt'), symbolFile = file('symbols.txt');
      fs.writeFileSync(sectionFile, sectionText);
      fs.writeFileSync(symbolFile, symbolText);
      module.roles[role] = { stages: s, passes, sections: sections(sectionText),
        symbols: symbols(symbolText), section_artifact: artifact(sectionFile).file,
        symbol_artifact: artifact(symbolFile).file, invocation: record,
        object: artifact(path.resolve(root, record.output)) };
      artifact(record.recordFile);
    }
    module.functions = inventory(module);
    const preprocessingTokens = roles.map(role => tokens(fs.readFileSync(path.join(directory,
      module.roles[role].stages.preprocessed.token_artifact), 'utf8')));
    module.preprocessing_comparison = tokenDifference(...preprocessingTokens);
    module.source_conditionals = conditionalDirectives(fs.readFileSync(path.join(root, module.source), 'utf8'));
    report.modules.push(module);
  }
  assert(report.modules.length, 'no C translation units captured');
  for (const record of records.filter(r => r.objects)) {
    const elf = path.resolve(root, record.output), binary = elf.replace(/\.elf$/, '.bin');
    const sectionText = run(config.llvm.size, ['-A', elf]);
    const sec = sections(sectionText);
    assert(sec.length, 'no linked sections');
    const loaded = sec.filter(s => !/^\.bss(?:\.|$)/.test(s.name) &&
      ((s.address >= 0x10000000 && s.address < 0x11000000) || /^\.data(?:\.|$)/.test(s.name)));
    const loadedBytes = fs.statSync(binary).size;
    const sectionBytes = loaded.reduce((sum, s) => sum + s.bytes, 0);
    assert(loadedBytes >= sectionBytes, 'loaded sections exceed binary extent');
    const disassembly = `${elf}.txt`;
    fs.writeFileSync(disassembly, run(config.llvm.objdump, ['-d', '--triple=thumbv8m.main-none-eabi', elf]));
    const sectionFile = `${elf}.sections.txt`, symbolFile = `${elf}.symbols.txt`;
    fs.writeFileSync(sectionFile, sectionText);
    fs.writeFileSync(symbolFile, run(config.llvm.nm, ['-S', '--defined-only', elf]));
    report.images.push({ role: record.role, name: path.basename(elf, '.elf'),
      loaded_bytes: loadedBytes, section_bytes: sectionBytes, extent_gaps: loadedBytes - sectionBytes,
      ram_bytes: sec.filter(s => s.address >= 0x20000000 && s.address < 0x20082000)
        .reduce((sum, s) => sum + s.bytes, 0), sections: sec,
      symbols: symbols(fs.readFileSync(symbolFile, 'utf8')),
      section_artifact: artifact(sectionFile).file, symbol_artifact: artifact(symbolFile).file,
      disassembly: stage(disassembly, 'object', record.role),
      binary: artifact(binary), elf: artifact(elf), invocation: record });
    artifact(record.recordFile);
  }
  const expected = new Set(config.targets);
  assert.equal(report.images.length, expected.size * 2, 'unexpected linked image count');
  report.projects = projectIndex(report).map(project => ({ name: project.name, modules: project.modules }));
  for (const role of roles)
    assert.equal(new Set(report.images.filter(image => image.role === role).map(image => image.name)).size,
      expected.size, 'missing image peer');
  for (const role of roles) {
    for (const file of buildFiles.filter(f => f.endsWith('.owned/preprocessed.s') ||
      f.endsWith('.o') && !compiles.some(r => path.resolve(root, r.output) === f))) {
      if (file.startsWith(path.join(directory, `${role}-build`) + path.sep)) artifact(file);
    }
  }
  report.artifacts = [...new Map(report.artifacts.map(a => [a.file, a])).values()];
  return report;
}

function capture(directory, targets) {
  assert(targets.length && new Set(targets).size === targets.length, 'provide unique Makefile targets');
  assert(targets.every(target => /^[a-z][a-z0-9-]*$/.test(target)), 'invalid target name');
  fs.mkdirSync(directory, { recursive: false });
  writeJSON(path.join(directory, 'report.json'), { schema: 1, status: 'in-progress' });
  const llvmTools = Object.fromEntries([['clang', 'LLVM_CLANG'], ['objcopy', 'LLVM_OBJCOPY'],
    ['nm', 'LLVM_NM'], ['size', 'LLVM_SIZE'], ['objdump', 'LLVM_OBJDUMP']]
    .map(([name, variable]) => [name, llvm(name === 'clang' ? name : `llvm-${name}`, variable)]));
  run(executable('make'), ['-j4', '-C', 'badge-compiler', 'all', 'build/badge-codegen-attribution'],
    path.join(directory, 'host-tools.log'));
  const policy = {};
  for (const [variable, tool, option] of [
    ['BADGE_OPT_PASSES', 'badge-opt', 'passes'],
    ['BADGE_IR_EXPRESSION_LOWERING', 'badge-ir', 'expression-lowering'],
    ...['frame-addressing', 'register-residency', 'scalar-selection', 'memory-selection'].map(option =>
      [`BADGE_CODEGEN_${option.toUpperCase().replaceAll('-', '_')}`, 'badge-codegen', option])])
    policy[variable] = process.env[variable] ?? run(path.join(tools, tool), [`--default-${option}`]).trim();
  assert(policy.BADGE_OPT_PASSES.length, 'empty optimizer selection is not supported');
  const provenanceFiles = [...walk(path.join(root, 'src')), ...walk(path.join(root, 'badge-compiler/src')),
    ...walk(path.join(root, 'badge-compiler/scripts')), path.join(root, 'Makefile'),
    path.join(root, 'badge-compiler/tools/assembler-crossover.mjs'),
    path.join(root, 'badge-compiler/tools/assembler-listing.mjs'),
    path.join(root, 'badge-compiler/Makefile'), ...walk(path.dirname(ownFile)).filter(f =>
      !f.includes(`${path.sep}build${path.sep}`) && /\.(mjs|js|css|html)$/.test(f)),
    ...Object.values(llvmTools), ...['badge-cpp', 'badge-parse', 'badge-sema', 'badge-ir',
      'badge-opt', 'badge-codegen', 'badge-codegen-attribution', 'badge-asm', 'badge-link'].map(name => path.join(tools, name))];
  const provenance = provenanceFiles.filter(file => !file.endsWith('.DS_Store'))
    .map(file => ({ file, sha256: sha(file) }));
  const config = { targets, llvm: llvmTools, policy, provenance,
    llvm_version: run(llvmTools.clang, ['--version']).trim() };
  writeJSON(path.join(directory, 'config.json'), config);
  const env = { ...process.env, ...policy };
  for (const role of roles) {
    const prefix = [process.execPath, ownFile, '--cc', role, directory].map(quote).join(' ');
    run(executable('make'), ['-j4', `BUILD_DIR=${path.join(directory, `${role}-build`)}`,
      `RP2350_CC=${prefix}`,
      `RP2350_OBJCOPY=${role === 'owned' ? path.join(tools, 'elf2bin') : llvmTools.objcopy}`,
      `RP2350_NM=${role === 'owned' ? path.join(tools, 'badge-elf-info') : llvmTools.nm}`,
      `RP2350_SIZE=${role === 'owned' ? path.join(tools, 'badge-elf-info') : llvmTools.size}`,
      ...targets], path.join(directory, `${role}-build.log`), { env });
  }
  for (const entry of provenance) assert.equal(sha(entry.file), entry.sha256, `capture-time drift: ${entry.file}`);
  const report = buildReport(directory, config);
  try {
    report.assembler = captureCrossover(directory, report, config);
  } catch (error) {
    writeJSON(path.join(directory, 'report.json'), { schema: 1, status: 'failed',
      boundary: 'identical-assembly crossover', diagnostic: error.message });
    throw error;
  }
  report.artifacts.push(...report.assembler.artifacts);
  for (const file of ['index.html', 'app.js', 'style.css', 'insights.mjs'])
    fs.copyFileSync(path.join(root, 'compile-graph/web', file), path.join(directory, file));
  for (const file of ['index.html', 'app.js', 'style.css', 'insights.mjs', 'config.json'])
    report.artifacts.push({ file, bytes: fs.statSync(path.join(directory, file)).size,
      sha256: sha(path.join(directory, file)) });
  writeJSON(path.join(directory, 'report.json'), report);
  try {
    verify(directory);
  } catch (error) {
    report.status = 'failed';
    report.error = error.message;
    writeJSON(path.join(directory, 'report.json'), report);
    throw error;
  }
  console.log(`Compile Graph: ${report.modules.length} modules, ${report.images.length} images.\n${directory}/index.html`);
}

export function verify(directory) {
  const report = readJSON(path.join(directory, 'report.json'));
  assert.equal(report.schema, 1);
  assert.equal(report.status, 'complete', 'capture is incomplete; inspect build logs');
  assert(report.modules.length && report.images.length, 'empty report');
  if (report.assembler) verifyCrossover(directory, report.assembler);
  if (report.assembler) {
    assert.equal(report.assembler.modules.length, report.modules.length * 2);
    assert.equal(report.assembler.images.length, report.images.length);
    for (const module of report.modules) for (const role of roles) {
      const row = report.assembler.modules.find(row => row.module === module.id && row.source_role === role);
      assert(row && row.assembly === module.roles[role].stages.assembly.file, 'assembler input identity mismatch');
    }
    for (const image of report.images) {
      const row = report.assembler.images.find(row => row.name === image.name && row.source_role === image.role);
      assert(row && row.linkers.lld && row.linkers.owned, 'missing assembler image/linker control');
    }
  }
  for (const artifact of report.artifacts) {
    const file = path.resolve(directory, artifact.file);
    assert(file.startsWith(directory + path.sep), 'artifact outside report directory');
    assert.equal(fs.statSync(file).size, artifact.bytes, `size mismatch: ${artifact.file}`);
    assert.equal(sha(file), artifact.sha256, `hash mismatch: ${artifact.file}`);
  }
  for (const module of report.modules)
    for (const role of roles) {
      assert(module.roles[role], 'missing compiler role');
      for (const item of [...Object.values(module.roles[role].stages), ...module.roles[role].passes]) {
        const text = fs.readFileSync(path.join(directory, item.file), 'utf8');
        const format = item.format;
        const regions = item.mapping_artifact ?
          sourceRegions(text, readJSON(path.join(directory, item.mapping_artifact)),
            item.mapping_filename) :
          format === 'ast' ? astRegions(JSON.parse(text), role) :
          format === 'ir' ? irRegions(text) : format === 'assembly' ? assemblyRegions(text) :
            format === 'object' ? objectRegions(text) : null;
        if (regions) assert.deepEqual(regions, item.regions, `region mismatch: ${item.file}`);
      }
      const data = module.roles[role];
      if (data.section_artifact)
        assert.deepEqual(sections(fs.readFileSync(path.join(directory, data.section_artifact), 'utf8')),
          data.sections, 'object section inventory mismatch');
      if (data.symbol_artifact)
        assert.deepEqual(symbols(fs.readFileSync(path.join(directory, data.symbol_artifact), 'utf8')),
          data.symbols, 'object symbol inventory mismatch');
      assert.deepEqual(inventory(module), module.functions, 'function inventory mismatch');
    }
  for (const module of report.modules) {
    if (!module.preprocessing_comparison) continue;
    const tokenInputs = roles.map(role => {
      const stage = module.roles[role].stages.preprocessed;
      assert.deepEqual(preprocessingStats(fs.readFileSync(path.join(directory, stage.file), 'utf8')),
        stage.presentation, 'preprocessing presentation mismatch');
      return tokens(fs.readFileSync(path.join(directory, stage.token_artifact), 'utf8'));
    });
    assert.deepEqual(tokenDifference(...tokenInputs), module.preprocessing_comparison,
      'preprocessing token comparison mismatch');
    assert.deepEqual(conditionalDirectives(fs.readFileSync(path.join(directory,
      module.roles.owned.stages.source.file), 'utf8')), module.source_conditionals,
    'source conditional inventory mismatch');
  }
  if (report.projects)
    assert.deepEqual(projectIndex(report).map(project => ({ name: project.name, modules: project.modules })),
      report.projects, 'project inventory mismatch');
  for (const image of report.images) {
    if (!image.binary) continue;
    assert.equal(fs.statSync(path.join(directory, image.binary.file)).size, image.loaded_bytes,
      'linked loaded-byte mismatch');
    assert.deepEqual(sections(fs.readFileSync(path.join(directory, image.section_artifact), 'utf8')),
      image.sections, 'linked section inventory mismatch');
    assert.deepEqual(symbols(fs.readFileSync(path.join(directory, image.symbol_artifact), 'utf8')),
      image.symbols, 'linked symbol inventory mismatch');
    const sectionBytes = image.sections.filter(s => !/^\.bss(?:\.|$)/.test(s.name) &&
      ((s.address >= 0x10000000 && s.address < 0x11000000) || /^\.data(?:\.|$)/.test(s.name)))
      .reduce((sum, s) => sum + s.bytes, 0);
    assert.equal(sectionBytes, image.section_bytes, 'linked section byte total mismatch');
    assert.equal(image.loaded_bytes - sectionBytes, image.extent_gaps, 'linked gap mismatch');
    assert.equal(image.sections.filter(s => s.address >= 0x20000000 && s.address < 0x20082000)
      .reduce((sum, s) => sum + s.bytes, 0), image.ram_bytes, 'linked RAM mismatch');
  }
  return report;
}

function main(args) {
  if (args[0] === '--cc') wrapper(args[1], path.resolve(args[2]), args.slice(3));
  else if (args[0] === '--capture') {
    assert(args[1], 'missing output directory');
    capture(path.resolve(args[1]), args.slice(2).length ? args.slice(2) : ['red-square', 'tilt-maze']);
  } else if (args[0] === '--verify') {
    assert.equal(args.length, 2, 'expected report directory');
    verify(path.resolve(args[1]));
    console.log('Verified artifact hashes and automatic stage regions.');
  } else {
    assert(args.length === 0 || args[0] === '--help', 'unknown option');
    console.log('Usage: node compile-graph/capture.mjs --capture NEW_DIRECTORY [MAKE_TARGET ...]\n' +
      '       node compile-graph/capture.mjs --verify REPORT_DIRECTORY\n' +
      'Default targets: red-square tilt-maze. Set LLVM_BIN or LLVM_* tool paths as needed.\n' +
      'Capture uses owned defaults unless BADGE_* policies are explicitly set. No badge access.');
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === ownFile) {
  try { main(process.argv.slice(2)); }
  catch (error) { console.error(`compile-graph: ${error.message}`); process.exitCode = 1; }
}
