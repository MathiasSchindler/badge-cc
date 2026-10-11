import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { request, decode, compare, decodeTiming, compareTiming, kernels, compareKernelTiming,
  applications, applicationSuiteId, suiteId, decodeApplication } from './compiler-test-protocol.mjs';
import { imageIdentity, cacheKey, verifyCachedBuild, sealCachedBuild } from './compiler-image-cache.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
const application = args[0] === '--application';
const timing = args[0] === '--timing' || application;
if (timing) args.shift();
const fixedIr = args[0] === '--fixed-ir';
if (fixedIr) args.shift();
const frontendIr = args[0] === '--frontend-ir';
if (frontendIr) args.shift();
if (fixedIr && frontendIr) {
  console.error('Select only one IR crossover reference.');
  process.exit(2);
}
if ((fixedIr || frontendIr) && !application) {
  console.error('--fixed-ir/--frontend-ir require --application.');
  process.exit(2);
}
let requestedSeed;
if (args[0]?.startsWith('--seed=')) {
  const text = args.shift().slice(7);
  if (!/^(0|[1-9][0-9]{0,9})$/.test(text) || Number(text) > 0xffffffff) {
    console.error('Seed must be a decimal uint32 (0..4294967295).');
    process.exit(2);
  }
  requestedSeed = Number(text);
}
const ownedPasses = process.env.BADGE_OPT_PASSES;
const frameAddressing = process.env.BADGE_CODEGEN_FRAME_ADDRESSING;
const registerResidency = process.env.BADGE_CODEGEN_REGISTER_RESIDENCY;
const scalarSelection = process.env.BADGE_CODEGEN_SCALAR_SELECTION;
const memorySelection = process.env.BADGE_CODEGEN_MEMORY_SELECTION;
const expressionLowering = process.env.BADGE_IR_EXPRESSION_LOWERING;
if (expressionLowering !== undefined && !['legacy', 'ssa'].includes(expressionLowering)) {
  console.error('BADGE_IR_EXPRESSION_LOWERING must be legacy or ssa.');
  process.exit(2);
}
if (memorySelection !== undefined && !['byte', 'word'].includes(memorySelection)) {
  console.error('BADGE_CODEGEN_MEMORY_SELECTION must be byte or word.');
  process.exit(2);
}
if (scalarSelection !== undefined && !['legacy', 'thumb', 'thumb-registers', 'thumb-layout', 'thumb-compact', 'thumb-schedule', 'thumb-relax', 'thumb-branch', 'thumb-homes', 'thumb-calls', 'thumb-effects', 'thumb-frames', 'thumb-control', 'thumb-operands', 'thumb-widths', 'thumb-constants'].includes(scalarSelection)) {
  console.error('BADGE_CODEGEN_SCALAR_SELECTION must be legacy, thumb, thumb-registers, thumb-layout, thumb-compact, thumb-schedule, thumb-relax, thumb-branch, thumb-homes, thumb-calls, thumb-effects, thumb-frames, thumb-control, thumb-operands, thumb-widths or thumb-constants.');
  process.exit(2);
}
if (registerResidency !== undefined && !['stack', 'local', 'global', 'global-flex', 'global-compact'].includes(registerResidency)) {
  console.error('BADGE_CODEGEN_REGISTER_RESIDENCY must be stack, local, global, global-flex or global-compact.');
  process.exit(2);
}
if (frameAddressing !== undefined && !['materialized', 'direct'].includes(frameAddressing)) {
  console.error('BADGE_CODEGEN_FRAME_ADDRESSING must be materialized or direct.');
  process.exit(2);
}
if (args.length !== 2 || !args.every(x => /^[0-9a-f]{16}$/i.test(x)) ||
    args[0].toUpperCase() === args[1].toUpperCase()) {
  console.error('Usage: node badge-compiler/tools/compiler-compare.mjs [--timing|--application [--fixed-ir|--frontend-ir]] [--seed=UINT32] LLVM_SERIAL OWNED_SERIAL');
  console.error('Replaces both selected applications; both devices must initially be in BOOTSEL.');
  process.exit(2);
}
const serials = args.map(x => x.toUpperCase());
const picotool = process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool');
const hostCC = process.env.HOST_CC ?? '/usr/bin/cc';
const llvmBin = process.env.LLVM_BIN ?? '';
const llvmTool = name => llvmBin ? path.join(llvmBin, name) : name;
const outputDir = path.join(root, 'build/compiler-compare-runs');
fs.mkdirSync(outputDir, { recursive: true });
const runDir = fs.mkdtempSync(path.join(outputDir, 'run.'));
console.log(`Compiler comparison: ${runDir}`);

function execute(command, arguments_, label, timeout = 15000) {
  const result = spawnSync(command, arguments_, { cwd: root, encoding: 'utf8', timeout,
    env: { ...process.env, ...(ownedPasses === undefined ? {} : { BADGE_OPT_PASSES: ownedPasses }) },
    maxBuffer: 16 * 1024 * 1024 });
  const output = (result.stdout ?? '') + (result.stderr ?? '');
  fs.writeFileSync(path.join(runDir, `${label}.log`), output + (result.error ? `\n${result.error.message}\n` : ''));
  if (result.error || result.status !== 0)
    throw new Error(`${label} failed: ${result.error?.message ?? `exit ${result.status}`}\n${output.slice(-4000)}`);
  return output;
}

function inspectUf2(image) {
  const data = fs.readFileSync(image);
  if (!data.length || data.length % 512) throw new Error(`Invalid UF2 size: ${image}`);
  const count = data.length / 512;
  const binary = fs.readFileSync(path.join(path.dirname(image), '../firmware/compiler_test.bin'));
  if (count !== Math.ceil(binary.length / 256)) throw new Error(`${image}: inconsistent binary size`);
  for (let i = 0; i < count; i++) {
    const block = data.subarray(i * 512, (i + 1) * 512);
    const expected = [[0, 0x0a324655], [4, 0x9e5d5157], [8, 0x2000],
      [12, 0x10000000 + i * 256], [16, 256], [20, i], [24, count],
      [28, 0xe48bff59], [508, 0x0ab16f30]];
    for (const [offset, value] of expected)
      if (block.readUInt32LE(offset) !== value) throw new Error(`${image}: invalid UF2 block ${i}`);
    const payload = Buffer.alloc(256, 0xff);
    binary.copy(payload, 0, i * 256, Math.min(binary.length, (i + 1) * 256));
    if (!block.subarray(32, 288).equals(payload) || !block.subarray(288, 508).every(x => x === 0))
      throw new Error(`${image}: invalid payload/padding in block ${i}`);
  }
  const reset = data.readUInt32LE(36);
  if (data.readUInt32LE(32) !== 0x2007f000 || !(reset & 1) ||
      reset < 0x10000000 || reset >= 0x10000000 + binary.length)
    throw new Error(`${image}: invalid stack/reset vector`);
  return crypto.createHash('sha256').update(data).digest('hex');
}

const configurations = [
  { name: 'llvm', serial: serials[0], dir: `badge-compiler/build/compiler-llvm${frontendIr ? '-frontend-ir' : fixedIr ? '-fixed-ir' : application ? '-application' : timing ? '-timing' : ''}`,
    tools: [`RP2350_CC=${frontendIr ? 'node badge-compiler/tools/backend-attribution.mjs --cc llvm-frontend' : fixedIr ? 'node badge-compiler/tools/backend-attribution.mjs --cc llvm' : llvmTool('clang')}`, `RP2350_OBJCOPY=${llvmTool('llvm-objcopy')}`,
      `RP2350_NM=${llvmTool('llvm-nm')}`, `RP2350_SIZE=${llvmTool('llvm-size')}`] },
  { name: 'owned', serial: serials[1], dir: `badge-compiler/build/compiler-owned${frontendIr ? '-frontend-ir' : fixedIr ? '-fixed-ir' : application ? '-application' : timing ? '-timing' : ''}`,
    tools: [`RP2350_CC=${fixedIr ? 'node badge-compiler/tools/backend-attribution.mjs --cc owned' : 'bash badge-compiler/scripts/owned-cc.sh'}`,
      'RP2350_OBJCOPY=badge-compiler/build/elf2bin', 'RP2350_NM=badge-compiler/build/badge-elf-info',
      'RP2350_SIZE=badge-compiler/build/badge-elf-info'] },
];

let collectionError;
try {
  for (const config of configurations) {
    const identity = execute(picotool, ['info', '-d', '--ser', config.serial], `${config.name}-preflight`);
    if (!identity.includes('RP2350')) throw new Error(`${config.serial}: not an RP2350`);
  }
  execute('make', ['-C', 'badge-compiler', 'all', `CC=${hostCC}`], 'compiler-tools', 180000);
  const selectedPasses = ownedPasses ?? execute(path.join(root, 'badge-compiler/build/badge-opt'),
    ['--default-passes'], 'optimizer-default').trim();
  if (!/^[a-z][a-z0-9-]*(,[a-z][a-z0-9-]*)*$/.test(selectedPasses))
    throw new Error('Invalid optimizer default/selected pipeline');
  const selectedFrameAddressing = frameAddressing ?? execute(
    path.join(root, 'badge-compiler/build/badge-codegen'),
    ['--default-frame-addressing'], 'codegen-default').trim();
  if (!['materialized', 'direct'].includes(selectedFrameAddressing))
    throw new Error('Invalid codegen frame addressing default/selection');
  const selectedRegisterResidency = registerResidency ?? execute(
    path.join(root, 'badge-compiler/build/badge-codegen'),
    ['--default-register-residency'], 'codegen-register-default').trim();
  if (!['stack', 'local', 'global', 'global-flex', 'global-compact'].includes(selectedRegisterResidency))
    throw new Error('Invalid codegen register residency default/selection');
  const selectedScalarSelection = scalarSelection ?? execute(
    path.join(root, 'badge-compiler/build/badge-codegen'),
    ['--default-scalar-selection'], 'scalar-selection-default').trim();
  if (!['legacy', 'thumb', 'thumb-registers', 'thumb-layout', 'thumb-compact', 'thumb-schedule', 'thumb-relax', 'thumb-branch', 'thumb-homes', 'thumb-calls', 'thumb-effects', 'thumb-frames', 'thumb-control', 'thumb-operands', 'thumb-widths', 'thumb-constants'].includes(selectedScalarSelection))
    throw new Error('Invalid codegen scalar default/selection');
  const selectedMemorySelection = memorySelection ?? execute(
    path.join(root, 'badge-compiler/build/badge-codegen'),
    ['--default-memory-selection'], 'memory-selection-default').trim();
  if (!['byte', 'word'].includes(selectedMemorySelection))
    throw new Error('Invalid codegen memory default/selection');
  const selectedExpressionLowering = expressionLowering ?? execute(
    path.join(root, 'badge-compiler/build/badge-ir'),
    ['--default-expression-lowering'], 'frontend-expression-default').trim();
  if (!['legacy', 'ssa'].includes(selectedExpressionLowering))
    throw new Error('Invalid frontend expression lowering default/selection');
  const cache = process.env.COMPILER_COMPARE_IMAGE_CACHE;
  if (cache && (fixedIr || frontendIr))
    throw new Error('Image caching currently supports ordinary whole-source comparisons only.');
  const llvmVersion = execute(llvmTool('clang'), ['--version'], 'llvm-version');
  const toolFiles = ['badge-cpp', 'badge-ir', 'badge-opt', 'badge-codegen', 'badge-asm',
    'badge-link', 'elf2bin', 'badge-elf-info', 'badge-sha256'].map(name =>
    path.join(root, 'badge-compiler/build', name));
  if (cache) {
    for (const name of [llvmTool('clang'), llvmTool('llvm-objcopy'), llvmTool('llvm-nm'),
      llvmTool('llvm-size'), 'ld.lld', hostCC]) {
      const file = path.isAbsolute(name) ? name : execute('/usr/bin/which', [name], `cache-tool-${path.basename(name)}`).trim();
      if (!file || !path.isAbsolute(file)) throw new Error(`Cannot identify cache tool: ${name}`);
      toolFiles.push(file);
    }
  }
  for (const config of configurations) {
    let identity, reused = false;
    if (cache) {
      identity = imageIdentity(root, { compiler: config.name, timing, application,
        llvm: llvmVersion, hostCC, tools: config.tools,
        policy: config.name === 'owned' ? {
          passes: selectedPasses, frame: selectedFrameAddressing, registers: selectedRegisterResidency,
          scalar: selectedScalarSelection, memory: selectedMemorySelection, expression: selectedExpressionLowering,
        } : null }, toolFiles);
      config.dir = path.relative(root, path.resolve(root, cache, cacheKey(identity)));
      if (fs.existsSync(path.join(root, config.dir))) {
        verifyCachedBuild(path.join(root, config.dir), identity);
        reused = true;
      }
      config.cache = { key: cacheKey(identity), reused,
        manifest: path.join(root, config.dir, 'image-cache.json') };
    }
    if (!reused) execute('make', ['-B', '-j1', `BUILD_DIR=${config.dir}`, `HOST_CC=${hostCC}`,
      `COMPILER_TEST_TIMING=${timing ? 1 : 0}`,
      `COMPILER_TEST_APPLICATION=${application ? 1 : 0}`,
      ...config.tools, 'compiler-test'], `${config.name}-build`, 180000);
    if (cache && !reused) sealCachedBuild(path.join(root, config.dir), identity);
    if (cache) fs.writeFileSync(path.join(runDir, `${config.name}-cache.json`),
      JSON.stringify(config.cache, null, 2) + '\n');
    config.image = path.join(root, config.dir, 'universe26/uf2/compiler_test.uf2');
    config.sha256 = inspectUf2(config.image);
    const archive = path.join(runDir, `${config.name}.uf2`);
    fs.copyFileSync(config.image, archive);
    if (crypto.createHash('sha256').update(fs.readFileSync(archive)).digest('hex') !== config.sha256)
      throw new Error(`${config.name}: firmware changed while archiving`);
    config.image = archive;
    const header = fs.readFileSync(path.join(root, config.dir,
      'universe26/firmware/compiler_test_build.h'), 'utf8');
    const build = header.match(/COMPILER_TEST_BUILD_ID 0x([0-9a-f]{8})u/);
    if (!build) throw new Error(`${config.name}: malformed build identity`);
    config.build = parseInt(build[1], 16);
  }
  const ownedTools = ['badge-cpp', 'badge-parse', 'badge-sema', 'badge-ir', 'badge-opt',
    'badge-codegen', 'badge-asm', 'badge-object', 'badge-link', 'elf2bin', 'badge-sha256'];
  const toolchain = {
    llvm: llvmVersion,
    owned: Object.fromEntries(ownedTools.map(name => [name, crypto.createHash('sha256')
      .update(fs.readFileSync(path.join(root, 'badge-compiler/build', name))).digest('hex')])),
    ownedPasses: selectedPasses,
    ownedFrameAddressing: selectedFrameAddressing,
    ownedRegisterResidency: selectedRegisterResidency,
    ownedScalarSelection: selectedScalarSelection,
    ownedMemorySelection: selectedMemorySelection,
    ownedExpressionLowering: selectedExpressionLowering,
    ...(fixedIr ? { reference: 'LLVM -Oz backend of byte-identical canonical owned optimized IR; normal LLVM IR optimizer disabled; owned assembler for original assembly inputs and owned linker for both images' } : {}),
    ...(frontendIr ? { reference: 'LLVM -Oz optimizer and backend of byte-identical canonical owned initial SSA IR; owned assembler for original assembly inputs and owned linker for both images' } : {}),
  };
  if (frontendIr) {
    const firmware = config => path.join(root, config.dir, 'universe26/firmware');
    const names = fs.readdirSync(firmware(configurations[1])).filter(name =>
      name.endsWith('.o.owned') && fs.existsSync(path.join(firmware(configurations[1]), name, 'frontend.ll'))).sort();
    if (names.length !== 11) throw new Error('Initial-IR application module scope changed');
    toolchain.initialIr = names.map(name => {
      const files = configurations.map(config => path.join(firmware(config), name, 'frontend.ll'));
      const texts = files.map(file => fs.readFileSync(file, 'utf8').replace(/^source_filename = .*\n/m, ''));
      if (texts[0] !== texts[1]) throw new Error(`${name}: initial IR differs across optimizer routes`);
      const input = path.join(runDir, `${name}.initial.ll`);
      fs.writeFileSync(input, texts[0]);
      return { module: name, file: path.relative(root, input), sha256: crypto.createHash('sha256').update(texts[0]).digest('hex') };
    });
  }
  if (configurations[0].build !== configurations[1].build)
    throw new Error('Source-suite identities differ; refusing comparison');
  const rounds = [];
  const seed = crypto.randomBytes(4);
  if (requestedSeed !== undefined) seed.writeUInt32LE(requestedSeed);
  for (let round = 0; round < (timing ? 2 : 1); round++) {
    const assignments = configurations.map(
        (config, i) =>
            ({...config, serial : serials[round === 0 ? i : 1 - i]}));
    const prefix = timing ? `round${round + 1}-` : '';
    const nonce = crypto.randomBytes(16);
    seed.copy(nonce);
    const requestPath = path.join(runDir, `${prefix}request.bin`);
    fs.writeFileSync(requestPath, request(configurations[0].build, nonce,
      application ? applicationSuiteId : suiteId));
    fs.writeFileSync(path.join(runDir, `${prefix}inputs.json`),
                     JSON.stringify({
                       schema : timing ? 3 : 1,
                       nonce : nonce.toString('hex'),
                       seed : nonce.readUInt32LE(0),
                       configurations : assignments,
                       toolchain,
                     },
                                    null, 2) +
                         '\n');
    for (const config of assignments) {
      execute(picotool, [ 'load', '-v', config.image, '--ser', config.serial ],
              `${prefix}${config.name}-flash`);
    }
    try {
      for (const config of assignments)
        execute(picotool, [ 'reboot', '--ser', config.serial ],
                `${prefix}${config.name}-start`);
      execute(path.join(root, configurations[0].dir,
                        'host-tools/compiler_test_host'),
              [
                ...assignments.map(x => x.serial), requestPath,
                path.join(runDir, `${prefix}llvm.bin`),
                path.join(runDir, `${prefix}owned.bin`)
              ],
              `${prefix}collector`, 65000);
    } catch (error) {
      collectionError = error;
      throw error;
    } finally {
      const missing = [];
      // The firmware watchdog uses the inherited reference clock, not a
      // host-calibrated wall clock. Leave time for recovery after live teardown.
      for (const config of assignments) {
        const deadline = Date.now() + 120000;
        let recovered = false;
        while (Date.now() < deadline) {
          const poll =
              spawnSync(picotool, [ 'info', '-d', '--ser', config.serial ],
                        {cwd : root, encoding : 'utf8', timeout : 2000});
          fs.appendFileSync(
              path.join(runDir, `${prefix}${config.name}-recovery.log`),
              `${poll.stdout ?? ''}${poll.stderr ?? ''}${
                  poll.error?.message ?? ''}\n`);
          if (!poll.error && poll.status === 0 &&
              poll.stdout.includes('RP2350')) {
            recovered = true;
            break;
          }
          Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 250);
        }
        if (!recovered)
          missing.push(config.serial);
      }
      if (missing.length)
        throw new Error(
            `${collectionError ? collectionError.message + '\n' : ''}` +
            `BOOTSEL recovery timeout: ${
                missing.join(', ')}; use physical Boot if necessary`);
    }
    const reports = assignments.map(config => {
      const bytes =
          fs.readFileSync(path.join(runDir, `${prefix}${config.name}.bin`));
      return application ? decodeApplication(bytes, config.build, nonce) :
        timing ? decodeTiming(bytes, config.build, nonce)
                    : {modules : decode(bytes, config.build, nonce)};
    });
    const results = reports.map(x => x.modules);
    const comparisons = compare(results[0], results[1], application ? applications : undefined);
    const kernelComparisons = timing && !application ? compare(reports[0].kernels, reports[1].kernels, kernels) : [];
    if (timing) {
      const timings = compareTiming(reports[0], reports[1]);
      comparisons.forEach((result, i) => { result.timing = timings[i]; });
      if (!application) {
        const kernelTimings = compareKernelTiming(reports[0], reports[1]);
        kernelComparisons.forEach((result, i) => { result.timing = kernelTimings[i]; });
      }
    }
    const matched = [...comparisons, ...kernelComparisons].every(x => x.matched);
    rounds.push({
      matched,
      nonce : nonce.toString('hex'),
      seed : nonce.readUInt32LE(0),
      configurations : assignments,
      modules : comparisons,
      ...(timing ? {
        kernels : kernelComparisons,
        timingVersion : reports[0].version,
        clocks : reports.map(x => x.clock),
        overhead : reports.map(x => x.overhead)
      }
                 : {})
    });
    fs.writeFileSync(
        path.join(runDir, timing ? `${prefix}results.json` : 'results.json'),
        JSON.stringify({
          schema : timing ? 3 : 1,
          build : configurations[0].build.toString(16).padStart(8, '0'),
          toolchain,
          ...rounds[round]
        },
                       null, 2) +
            '\n');
    for (const result of [...comparisons, ...kernelComparisons])
      console.log(`${result.matched ? 'PASS' : 'FAIL'} ${result.module}: ${
          result.cases} cases; ${JSON.stringify(result.differences)}`);
    if (!matched)
      throw new Error('Compiler results differ; see results.json');
  }
  if (timing) {
    const aggregate = field => rounds[0][field].map((result, i) => {
      const runs = rounds.map(x => x[field][i].timing);
      const ratio = Math.sqrt(runs[0].ownedToLlvm * runs[1].ownedToLlvm);
      return { ...result, timing: { unit: 'timer0-raw-ticks', runs,
        ownedToLlvm: ratio, percentSlower: (ratio - 1) * 100 } };
    });
    const modules = aggregate('modules');
    const kernelResults = aggregate('kernels');
    fs.writeFileSync(path.join(runDir, 'results.json'), JSON.stringify({
      schema: 3, matched: true, toolchain, rounds, modules, kernels: kernelResults,
      method: application ?
        'geometric mean of two role-swapped median ratios; actual application warm batches; setup/checksums outside intervals; scripted input/driver work included' :
        'geometric mean of two role-swapped median ratios; warm-working-state batches; kernel fingerprints/guards outside intervals',
    }, null, 2) + '\n');
    for (const result of [...modules, ...kernelResults]) {
      const difference = result.timing.percentSlower;
      console.log(`TIMING ${result.module}: owned/LLVM ${result.timing.ownedToLlvm.toFixed(3)}x; ` +
        `${Math.abs(difference).toFixed(1)}% ${difference < 0 ? 'faster' : 'slower'}`);
    }
  }
  console.log('All modules match; both selected badges recovered to BOOTSEL.');
} catch (error) {
  console.error(error.message);
  console.error(`Comparison failed; evidence retained in ${runDir}.`);
  process.exitCode = 1;
}
