import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ownFile = fileURLToPath(import.meta.url);
const root = path.resolve(path.dirname(ownFile), '../..');
const tools = path.join(root, 'badge-compiler/build');
const policyNames = ['BADGE_OPT_PASSES', 'BADGE_CODEGEN_FRAME_ADDRESSING',
  'BADGE_CODEGEN_REGISTER_RESIDENCY', 'BADGE_CODEGEN_SCALAR_SELECTION',
  'BADGE_CODEGEN_MEMORY_SELECTION', 'BADGE_IR_EXPRESSION_LOWERING'];
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const readJSON = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const writeJSON = (file, data) => fs.writeFileSync(file, JSON.stringify(data, null, 2) + '\n');
const relative = file => path.relative(root, file);
const walk = dir => fs.readdirSync(dir, { withFileTypes: true }).flatMap(entry => {
  const file = path.join(dir, entry.name);
  return entry.isDirectory() ? walk(file) : entry.isFile() ? [file] : [];
});
const shellQuote = text => `'${text.replaceAll("'", "'\\''")}'`;
const llvmTool = (name, variable) => process.env[variable] ||
  (process.env.LLVM_BIN ? path.join(process.env.LLVM_BIN, name) : name);

export function invocation(args) {
  const outputIndex = args.indexOf('-o');
  assert(outputIndex >= 0 && args[outputIndex + 1], 'compiler invocation has no output');
  assert.equal(args.lastIndexOf('-o'), outputIndex, 'multiple output arguments');
  const output = args[outputIndex + 1];
  const rest = args.filter((_, i) => i !== outputIndex && i !== outputIndex + 1);
  const sources = [];
  for (let i = 0; i < rest.length; i++) {
    if (['-I', '-D', '-U', '-T'].includes(rest[i])) { i++; continue; }
    if (!rest[i].startsWith('-') && /\.(c|S)$/.test(rest[i])) sources.push(rest[i]);
  }
  if (rest.includes('-c')) {
    assert.equal(sources.length, 1, 'expected one compilation source');
    return { output, source: sources[0],
      flags: rest.filter(arg => arg !== '-c' && arg !== sources[0]) };
  }
  const objects = rest.filter(arg => arg.endsWith('.o'));
  assert(objects.length && !sources.length, 'expected link objects');
  let script;
  const linkerFlags = [];
  for (let i = 0; i < rest.length; i++) {
    const arg = rest[i];
    if (arg === '-T') { assert(rest[i + 1], 'missing linker script'); script = rest[++i]; }
    else if (arg.startsWith('-T')) script = arg.slice(2);
    else if (arg.startsWith('-Wl,')) {
      const parts = arg.slice(4).split(',');
      for (let j = 0; j < parts.length; j++) {
        if (parts[j] === '-T') { assert(parts[j + 1]); script = parts[++j]; }
        else if (parts[j].startsWith('-T')) script = parts[j].slice(2);
        else if (['--gc-sections', '--no-gc-sections'].includes(parts[j])) linkerFlags.push(parts[j]);
        else assert(parts[j] === '--fatal-warnings' || parts[j].startsWith('-Map='),
          `unsupported link option: ${parts[j]}`);
      }
    }
  }
  assert(script, 'linker script missing');
  return { output, objects, script, linkerFlags };
}

export function normalizeInitialIr(text) {
  text = text.replace(/^; ModuleID = .*\n/m, '').replace(/^source_filename = .*\n/m, '');
  const ids = new Set([...text.matchAll(/!srcloc !(\d+)/g)].map(match => match[1]));
  for (const id of ids) {
    const pattern = new RegExp(`^!${id} = !\\{i(?:32|64) \\d+(?:, i(?:32|64) \\d+)*\\}$`, 'm');
    assert(pattern.test(text), `unrecognized source-location metadata: !${id}`);
    text = text.replace(pattern, match => match.replace(/(i(?:32|64)) \d+/g, '$1 SOURCE_LOCATION'));
  }
  return text;
}

export function preprocessedFlags(flags) {
  const result = [];
  for (let i = 0; i < flags.length; i++) {
    if (['-I', '-D', '-U'].includes(flags[i])) {
      assert(flags[i + 1], `missing preprocessing argument: ${flags[i]}`);
      i++;
    } else if (!/^-[IDU]./.test(flags[i])) result.push(flags[i]);
  }
  return result;
}

export function irCounts(text) {
  let inside = false, functions = 0;
  const operations = {};
  for (const line of text.split('\n')) {
    if (/^define /.test(line)) { functions++; inside = true; continue; }
    if (line === '}') { inside = false; continue; }
    if (!inside || !/^  \S/.test(line)) continue;
    const match = line.match(/^\s+(?:%[^=]+ = )?(?:(?:tail|musttail|notail) )?([a-z][a-z0-9.]*)\b/);
    if (match) operations[match[1]] = (operations[match[1]] || 0) + 1;
  }
  return { functions, operations };
}

export function byteDifference(a, b) {
  const first = [];
  let count = 0;
  for (let offset = 0; offset < Math.max(a.length, b.length); offset++) {
    if (a[offset] === b[offset]) continue;
    count++;
    if (first.length < 16) first.push({ offset, a: a[offset] ?? null, b: b[offset] ?? null });
  }
  return { equal: count === 0, lengths: [a.length, b.length], differing_bytes: count, first };
}

export function verifyReport(file) {
  const report = readJSON(file);
  assert.equal(report.schema, 1);
  assert.equal(report.milestone, 'PO00');
  assert.equal(report.status, 'qualified-stage-capture', report.error || report.status);
  for (const manifest of [report.sources, report.executables, report.artifacts])
    for (const artifact of manifest) {
      assert.equal(sha(path.resolve(report.root, artifact.file)), artifact.sha256, artifact.file);
    }
  assert(report.modules.length && report.images.length, 'empty corpus');
  return report;
}

export function executable(name) {
  const candidates = name.includes(path.sep) ? [path.resolve(root, name)] :
    (process.env.PATH || '').split(path.delimiter).map(dir => path.join(dir, name));
  for (const file of candidates) {
    try { fs.accessSync(file, fs.constants.X_OK); return path.resolve(file); }
    catch (error) { if (!['ENOENT', 'EACCES', 'ENOTDIR'].includes(error.code)) throw error; }
  }
  throw new Error(`executable not found: ${name}`);
}

function compilerWrapper(role, directory, args) {
  assert(['owned', 'llvm'].includes(role), 'invalid compiler role');
  const parsed = invocation(args);
  const output = path.resolve(root, parsed.output);
  assert(output.startsWith(path.resolve(directory) + path.sep), 'output outside capture directory');
  const command = role === 'owned' ? executable('bash') : executable(llvmTool('clang', 'LLVM_CLANG'));
  const compilerArgs = role === 'owned' ? ['badge-compiler/scripts/owned-cc.sh', ...args] : args;
  const result = spawnSync(command, compilerArgs, { cwd: root, stdio: 'inherit', timeout: 180000 });
  const record = { role, ...parsed, args, compiler: command, status: result.status,
    error: result.error?.message || null,
    source_sha256: parsed.source ? sha(path.resolve(root, parsed.source)) : null };
  const records = path.join(directory, `${role}-invocations`);
  fs.mkdirSync(records, { recursive: true });
  writeJSON(path.join(records, `${crypto.createHash('sha256').update(output).digest('hex')}.json`), record);
  if (result.error) throw result.error;
  assert.equal(result.status, 0, `${role} compiler failed: ${parsed.output}`);
}

function capture(directory, scope) {
  assert(['maze', 'corpus'].includes(scope), 'scope must be maze or corpus');
  // mkdir refuses existing files, directories and symlinks before any build.
  fs.mkdirSync(directory);
  const logDirectory = path.join(directory, 'logs');
  fs.mkdirSync(logDirectory);
  const reportFile = path.join(directory, 'results.json');
  const report = { schema: 1, milestone: 'PO00', root, scope, status: 'in-progress',
    host: { node: process.version, platform: process.platform, architecture: process.arch },
    started_at: new Date().toISOString(), commands: [], sources: [], executables: [],
    policy: {}, modules: [], images: [], assembly_sources: [], open_issues: [], artifacts: [],
    normalization: ['ModuleID/source_filename', 'integer inline-assembly !srcloc coordinates; types/arity preserved',
      'AST locations omitted; tokens normalized only by the existing badge-cpp --tokens mode'],
    limitations: ['Static stage isolation; no badge programming or timing.',
      'Different AST formats; function inventories do not prove full semantic AST equivalence.',
      'LLVM codegen includes target legalization/machine passes; its normal IR optimizer is disabled in same-IR crossovers.',
      'Object text and instruction counts do not predict application runtime.',
      'Unsupported optional crossovers remain explicit; no fallback compiler is used.',
      'Placement differences are open issues unless proven by a separate relocation/value audit.'] };
  const save = () => writeJSON(reportFile, report);
  let sequence = 0;
  function run(command, args, label, optional = false) {
    const log = path.join(logDirectory, `${String(++sequence).padStart(4, '0')}-${label}.log`);
    const result = spawnSync(command, args, { cwd: root, env: process.env, encoding: 'utf8',
      timeout: 600000, maxBuffer: 64 * 1024 * 1024 });
    const output = `${result.stdout || ''}${result.stderr || ''}${result.error || ''}`;
    fs.writeFileSync(log, output);
    const record = { command, args, log: relative(log), status: result.status,
      error: result.error?.message || null };
    report.commands.push(record);
    assert.ifError(result.error);
    assert.equal(result.signal, null, `${label}: terminated by ${result.signal}`);
    if (result.status !== 0) {
      assert(optional && output.trim(), `${label}: ${output.slice(-3000)}`);
      return { accepted: false, diagnostic: output.trim(), command: record };
    }
    return { accepted: true, stdout: result.stdout, command: record };
  }
  const native = (name, args, label, optional) => run(path.join(tools, name), args, label, optional);
  const recordFiles = files => files.map(file => ({ file: relative(file), sha256: sha(file) }));
  const verifySources = () => {
    for (const artifact of [...report.sources, ...report.executables])
      assert.equal(sha(path.join(root, artifact.file)), artifact.sha256, `source drift: ${artifact.file}`);
  };
  const issue = (boundary, module, detail) => report.open_issues.push({ boundary, module, detail });
  save();
  try {
    const sourceFiles = [...walk(path.join(root, 'src')), ...walk(path.join(root, 'badge-compiler/src')),
      ...walk(path.join(root, 'badge-compiler/scripts')), ownFile,
      path.join(root, 'badge-compiler/tests/pipeline-stages.test.mjs'),
      path.join(root, 'Makefile'), path.join(root, 'badge-compiler/Makefile')];
    report.sources = recordFiles(sourceFiles);
    const llvm = {
      clang: executable(llvmTool('clang', 'LLVM_CLANG')), opt: executable(llvmTool('opt', 'LLVM_OPT')),
      lld: executable(llvmTool('ld.lld', 'LLVM_LLD')),
      objcopy: executable(llvmTool('llvm-objcopy', 'LLVM_OBJCOPY')),
      nm: executable(llvmTool('llvm-nm', 'LLVM_NM')), size: executable(llvmTool('llvm-size', 'LLVM_SIZE')),
    };
    run(executable('make'), ['-C', 'badge-compiler', 'all'], 'host-tools');
    report.executables = recordFiles([...Object.values(llvm), executable('make'), executable('bash'),
      process.execPath, ...['badge-cpp', 'badge-parse', 'badge-sema', 'badge-ir', 'badge-opt',
        'badge-codegen', 'badge-asm', 'badge-link', 'badge-elf-info', 'elf2bin']
        .map(name => path.join(tools, name))]);
    report.llvm_version = run(llvm.clang, ['--version'], 'llvm-version').stdout.trim();
    for (const [variable, name, option] of [
      ['BADGE_OPT_PASSES', 'badge-opt', '--default-passes'],
      ['BADGE_IR_EXPRESSION_LOWERING', 'badge-ir', '--default-expression-lowering'],
      ...['frame-addressing', 'register-residency', 'scalar-selection', 'memory-selection'].map(option =>
        [`BADGE_CODEGEN_${option.toUpperCase().replaceAll('-', '_')}`, 'badge-codegen', `--default-${option}`])])
      report.policy[variable] = process.env[variable] ?? native(name, [option], option).stdout.trim();
    assert(['legacy', 'ssa'].includes(report.policy.BADGE_IR_EXPRESSION_LOWERING),
      'BADGE_IR_EXPRESSION_LOWERING must be legacy or ssa');
    const records = {};
    for (const role of ['owned', 'llvm']) {
      const build = path.join(directory, `${role}-build`);
      const prefix = [process.execPath, ownFile, '--cc', role, directory].map(shellQuote).join(' ');
      const buildArgs = ['-j4', `BUILD_DIR=${relative(build)}`, `RP2350_CC=${prefix}`,
        `RP2350_OBJCOPY=${role === 'owned' ? path.join(tools, 'elf2bin') : llvm.objcopy}`,
        `RP2350_NM=${role === 'owned' ? path.join(tools, 'badge-elf-info') : llvm.nm}`,
        `RP2350_SIZE=${role === 'owned' ? path.join(tools, 'badge-elf-info') : llvm.size}`,
        ...(scope === 'maze' ? ['tilt-maze'] : ['all', `${relative(build)}/universe26/uf2/usb_wifi_udp.uf2`])];
      run(executable('make'), buildArgs, `${role}-build`);
      records[role] = fs.readdirSync(path.join(directory, `${role}-invocations`)).sort()
        .map(name => readJSON(path.join(directory, `${role}-invocations`, name)));
      assert(records[role].every(record => record.status === 0 && !record.error));
    }
    const ownedCompiles = records.owned.filter(r => r.source).sort((a, b) => a.output.localeCompare(b.output));
    const findPair = record => {
      const pair = records.llvm.find(r => path.basename(r.output) === path.basename(record.output));
      assert(pair && pair.source === record.source, `unpaired module: ${record.output}`);
      return pair;
    };
    const fileText = file => fs.readFileSync(file, 'utf8');
    const objectSize = object => {
      const output = run(llvm.size, ['-A', object], 'object-sections').stdout;
      const sections = [...output.matchAll(/^(\.\S+)\s+(\d+)\s+\d+$/gm)]
        .map(match => ({ name: match[1], bytes: Number(match[2]) }));
      assert(sections.length, 'missing object sections');
      const sum = prefix => sections.filter(s => s.name === prefix || s.name.startsWith(prefix + '.'))
        .reduce((total, s) => total + s.bytes, 0);
      return { text: sum('.text'), rodata: sum('.rodata'), data: sum('.data'), bss: sum('.bss') };
    };
    const reassembled = new Map();
    for (const owned of ownedCompiles) {
      const pair = findPair(owned);
      const module = path.basename(owned.output);
      const stage = path.join(directory, 'stages', module);
      fs.mkdirSync(stage, { recursive: true });
      const output = name => path.join(stage, name);
      const ownedStage = path.resolve(root, `${owned.output}.owned`);
      const item = { module, source: owned.source, source_sha256: owned.source_sha256,
        owned_invocation: owned, llvm_invocation: pair };
      if (owned.source.endsWith('.S')) {
        run(llvm.clang, [...pair.flags, '-E', '-x', 'assembler-with-cpp', pair.source,
          '-o', output('llvm.s')], 'assembly-preprocess');
        item.preprocessing = byteDifference(fs.readFileSync(path.join(ownedStage, 'preprocessed.s')),
          fs.readFileSync(output('llvm.s')));
        report.assembly_sources.push(item);
        continue;
      }
      const sourceInput = path.resolve(root, owned.source);
      assert.equal(sha(sourceInput), owned.source_sha256);
      run(llvm.clang, [...pair.flags, '-E', pair.source, '-o', output('llvm.i')], 'llvm-preprocess');
      fs.copyFileSync(path.join(ownedStage, 'preprocessed.i'), output('owned.i'));
      const astFiles = {};
      const irFiles = {};
      const attempts = {};
      for (const role of ['owned', 'llvm']) {
        const input = output(`${role}.i`);
        native('badge-cpp', ['--tokens', input, '-o', output(`${role}.tokens`)], 'tokens');
        const ast = native('badge-parse', ['--no-locations', input, '-o', output(`${role}.ast.json`)],
          'ast', role === 'llvm');
        const sema = native('badge-sema', ['--no-locations', input, '-o', output(`${role}.sema.json`)],
          'typed-ast', role === 'llvm');
        const badgeIr = output(`badge-from-${role}.ll`);
        const badge = native('badge-ir', [`--expression-lowering=${report.policy.BADGE_IR_EXPRESSION_LOWERING}`,
          input, '-o', badgeIr], 'initial-owned-ir', role === 'llvm');
        const clangIr = output(`clang-from-${role}.ll`);
        const clang = run(llvm.clang, [...preprocessedFlags(pair.flags), '-x', 'cpp-output', '-Xclang',
          '-disable-llvm-passes', '-S', '-emit-llvm', input, '-o', clangIr],
        'initial-clang-ir', role === 'owned');
        for (const [file, result] of [[badgeIr, badge], [clangIr, clang]])
          if (result.accepted) run(llvm.opt, ['-passes=verify', '-disable-output', file], 'verify-initial-ir');
        attempts[role] = Object.fromEntries(Object.entries({ ast, sema, badge, clang }).map(([name, result]) =>
          [name, { accepted: result.accepted, command: result.command,
            ...(result.accepted ? {} : { diagnostic: result.diagnostic }) }]));
        for (const [name, result] of Object.entries(attempts[role]))
          if (!result.accepted) issue('rejected-frontend-crossover', `${module}/${role}/${name}`,
            'Explicit parser/semantic/warning-policy rejection; diagnostic retained. No alternative compilation or missing-output equality.');
        astFiles[role] = output(`${role}.ast.json`);
        irFiles[role] = { badge: badgeIr, clang: clangIr };
      }
      item.frontend_attempts = attempts;
      const tokens = role => fileText(output(`${role}.tokens`));
      item.preprocessing = { tokens_identical: tokens('owned') === tokens('llvm'),
        token_counts: ['owned', 'llvm'].map(role => tokens(role).trimEnd().split('\n').length) };
      item.frontend_crossovers = {
        badge_ast_identical: attempts.llvm.ast.accepted ?
          fileText(astFiles.owned) === fileText(astFiles.llvm) : null,
        badge_typed_ast_identical: attempts.llvm.sema.accepted ?
          fileText(output('owned.sema.json')) === fileText(output('llvm.sema.json')) : null,
        badge_ir_identical: attempts.llvm.badge.accepted ?
          normalizeInitialIr(fileText(irFiles.owned.badge)) === normalizeInitialIr(fileText(irFiles.llvm.badge)) : null,
        clang_ir_identical: attempts.owned.clang.accepted ?
          normalizeInitialIr(fileText(irFiles.owned.clang)) === normalizeInitialIr(fileText(irFiles.llvm.clang)) : null };
      if (!item.preprocessing.tokens_identical)
        issue('preprocessing', module, 'Tokens differ; actual invocations/generated definitions are retained. No semantic normalization.');
      for (const [boundary, equal] of Object.entries(item.frontend_crossovers))
        if (equal === false) issue(boundary, module, 'Same-frontend outputs differ; inspect retained raw inputs, flags and outputs.');
      const clangAst = run(llvm.clang, [...preprocessedFlags(pair.flags), '-x', 'cpp-output', '-fsyntax-only',
        '-Xclang', '-ast-dump=json', output('llvm.i')], 'clang-ast').stdout;
      fs.writeFileSync(output('clang.ast.json'), clangAst);
      const badgeAst = readJSON(output('owned.ast.json'));
      const functionName = node => node.kind === 'name' ? node.text :
        (node.children || []).map(functionName).find(Boolean);
      const badgeFunctions = badgeAst.ast.children.filter(n => n.kind === 'function_definition')
        .map(n => functionName(n.children.find(n => n.kind === 'init_declarator'))).sort();
      const clangFunctions = JSON.parse(clangAst).inner.filter(n => n.kind === 'FunctionDecl' &&
        n.inner?.some(c => c.kind === 'CompoundStmt')).map(n => n.name).sort();
      item.ast = { badge_functions: badgeFunctions, clang_functions: clangFunctions,
        inventory_identical: JSON.stringify(badgeFunctions) === JSON.stringify(clangFunctions) };
      if (!item.ast.inventory_identical) issue('ast-inventory', module, 'Function inventories differ; not a bytewise AST-equivalence test.');
      item.initial_ir = { badge: irCounts(fileText(irFiles.owned.badge)), clang: irCounts(fileText(irFiles.llvm.clang)) };
      const optimized = path.join(ownedStage, 'optimized.ll');
      assert.equal(normalizeInitialIr(fileText(path.join(ownedStage, 'frontend.ll'))),
        normalizeInitialIr(fileText(irFiles.owned.badge)), `${module}: owned frontend replay drift`);
      run(llvm.opt, ['-passes=verify', '-disable-output', optimized], 'verify-owned-optimized');
      const llvmOptimized = output('clang-optimized.ll');
      run(llvm.clang, [...pair.flags, '-S', '-emit-llvm', pair.source,
        '-o', llvmOptimized], 'clang-optimized-ir');
      run(llvm.opt, ['-passes=verify', '-disable-output', llvmOptimized], 'verify-clang-optimized');
      const llvmAssembly = output('clang-assembly.s');
      run(llvm.clang, [...pair.flags, '-S', pair.source, '-o', llvmAssembly], 'clang-assembly');
      item.primary_stages = {
        owned_frontend: relative(path.join(ownedStage, 'frontend.ll')),
        owned_optimized: relative(optimized), owned_assembly: relative(path.join(ownedStage, 'assembly.s')),
        owned_object: relative(path.resolve(root, owned.output)),
        clang_frontend: relative(irFiles.llvm.clang), clang_optimized: relative(llvmOptimized),
        clang_assembly: relative(llvmAssembly), clang_object: relative(path.resolve(root, pair.output)),
        clang_object_bytes: objectSize(path.resolve(root, pair.output)) };
      const llvmObject = output('same-ir-llvm.o');
      const sameIr = run(llvm.clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
        '-Oz', '-ffunction-sections', '-fdata-sections', '-Xclang', '-disable-llvm-passes',
        '-x', 'ir', '-c', optimized, '-o', llvmObject], 'same-ir-llvm-backend');
      item.same_ir_backend = { input: relative(optimized), sha256: sha(optimized),
        llvm_command: sameIr.command, owned_object: objectSize(path.resolve(root, owned.output)),
        llvm_object: objectSize(llvmObject) };
      const reverse = native('badge-codegen', [llvmOptimized, '-o', output('clang-ir-badge.s')],
        'reverse-ir-backend', true);
      item.reverse_ir_backend = reverse.accepted ? { accepted: true, command: reverse.command } : reverse;
      const assembly = path.join(ownedStage, 'assembly.s');
      const assembled = output('same-assembly-llvm.o');
      run(llvm.clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-x', 'assembler',
        '-c', assembly, '-o', assembled], 'same-assembly');
      reassembled.set(path.resolve(root, owned.output), assembled);
      item.same_assembly = { input: relative(assembly), sha256: sha(assembly),
        llvm_object: relative(assembled) };
      report.modules.push(item);
      save();
      console.log(`PO00: ${module} stages and crossovers captured.`);
    }
    const linked = (name, role, record, objects) => {
      const dir = path.join(directory, 'links', name);
      fs.mkdirSync(dir, { recursive: true });
      const elf = path.join(dir, `${role}.elf`), bin = path.join(dir, `${role}.bin`);
      run(role === 'owned' ? path.join(tools, 'badge-link') : llvm.lld,
        ['-T', record.script, ...record.linkerFlags, ...objects, '-o', elf], 'same-object-link');
      native('elf2bin', ['-O', 'binary', elf, bin], 'linked-binary');
      const metrics = JSON.parse(native('badge-elf-info', ['--metrics', elf], 'linked-metrics').stdout);
      return { elf: relative(elf), binary: relative(bin), sha256: sha(bin),
        metrics: { text: metrics.text, data: metrics.data, bss: metrics.bss },
        functions: metrics.symbols.filter(s => s.type === 2 && s.name)
          .map(s => `${s.name}:${s.value}:${s.size}`).sort() };
    };
    const compareLinks = (a, b) => ({ ...byteDifference(fs.readFileSync(path.join(root, a.binary)),
      fs.readFileSync(path.join(root, b.binary))),
      function_layout_identical: JSON.stringify(a.functions) === JSON.stringify(b.functions) });
    for (const owned of records.owned.filter(r => r.objects).sort((a, b) => a.output.localeCompare(b.output))) {
      const llvmRecord = records.llvm.find(r => path.basename(r.output) === path.basename(owned.output));
      assert(llvmRecord?.objects, `unpaired image: ${owned.output}`);
      const image = path.basename(owned.output);
      const pools = {};
      for (const [role, record] of [['owned', owned], ['llvm', llvmRecord]]) {
        const a = linked(`${image}-${role}-objects`, 'owned', record, record.objects);
        const b = linked(`${image}-${role}-objects`, 'llvm', record, record.objects);
        pools[role] = { invocation: record, script_sha256: sha(path.resolve(root, record.script)),
          owned_linker: a, llvm_linker: b, comparison: compareLinks(a, b) };
        if (!pools[role].comparison.equal)
          issue('same-object-link', `${image}/${role}`, 'Loaded bytes differ; sizes/layout and first offsets retained. Review placement/relocation values in PO08; not evidence of different instructions.');
      }
      const assemblyLink = linked(`${image}-same-assembly`, 'llvm', owned,
        owned.objects.map(object => reassembled.get(path.resolve(root, object)) || object));
      const sameAssembly = compareLinks(pools.owned.llvm_linker, assemblyLink);
      if (!sameAssembly.equal)
        issue('same-assembly-image', image, 'Identical owned assembly reassembled by LLVM changes flash bytes; inspect relocations and instruction encodings.');
      const primary = path.resolve(root, owned.output).replace(/\.elf$/, '.bin');
      assert(byteDifference(fs.readFileSync(primary),
        fs.readFileSync(path.join(root, pools.owned.owned_linker.binary))).equal,
      `${image}: original owned link not reproduced`);
      const llvmPrimary = path.resolve(root, llvmRecord.output).replace(/\.elf$/, '.bin');
      const llvmReplay = byteDifference(fs.readFileSync(llvmPrimary),
        fs.readFileSync(path.join(root, pools.llvm.llvm_linker.binary)));
      if (!llvmReplay.equal)
        issue('original-llvm-link-or-extraction', image, 'Original LLVM image differs from direct LLD/elf2bin replay; inspect retained link flags and extraction.');
      report.images.push({ image, pools, reassembled_image: assemblyLink,
        same_assembly: sameAssembly, original_llvm_replay: llvmReplay });
      save();
    }
    verifySources();
    report.status = 'qualified-stage-capture';
  } catch (error) {
    report.status = 'failed';
    report.error = error.message;
    throw error;
  } finally {
    report.completed_at = new Date().toISOString();
    report.artifacts = recordFiles(walk(directory).filter(file => file !== reportFile));
    save();
  }
  try { verifyReport(reportFile); }
  catch (error) {
    report.status = 'failed';
    report.error = `Final evidence verification: ${error.message}`;
    save();
    throw error;
  }
  console.log(`PO00: ${report.modules.length} C modules, ${report.assembly_sources.length} assembly inputs, ` +
    `${report.images.length} images, ${report.open_issues.length} explicit open issues. ${reportFile}`);
  return report;
}

function main(args) {
  if (args.length === 1 && args[0] === '--help') {
    console.log('Usage: pipeline-stages.mjs --capture NEW_DIRECTORY [maze|corpus]\n' +
      '       pipeline-stages.mjs --verify REPORT.json\n' +
      'Host-only actual-source stage isolation; no badge access. LLVM tools must be installed.');
    return;
  }
  if (args[0] === '--cc') {
    assert(args.length > 3);
    compilerWrapper(args[1], path.resolve(args[2]), args.slice(3));
  } else if (args[0] === '--capture') {
    assert(args.length === 2 || args.length === 3, 'expected a new directory and optional scope');
    capture(path.resolve(args[1]), args[2] || 'corpus');
  } else if (args[0] === '--verify') {
    assert.equal(args.length, 2);
    const report = verifyReport(path.resolve(args[1]));
    console.log(`Verified PO00: ${report.modules.length} modules, ${report.images.length} images, ` +
      `${report.open_issues.length} open issues.`);
  } else throw new Error('Use --help for pipeline-stages.mjs usage.');
}

if (process.argv[1] && path.resolve(process.argv[1]) === ownFile) {
  try { main(process.argv.slice(2)); }
  catch (error) { console.error(`pipeline-stages: ${error.message}`); process.exitCode = 1; }
}
