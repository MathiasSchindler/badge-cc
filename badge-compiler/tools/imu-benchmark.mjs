import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { preserveFlash } from './application-benchmark.mjs';
import { decodeElf } from './boundary-evidence.mjs';
import { verifyUf2 } from './benchmark-c06.mjs';
import { applications, applicationSuiteId, compare, compareTiming, decodeApplication, decodeImu,
  imuApplications, imuSuiteId, request } from './compiler-test-protocol.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const roles = ['llvm', 'owned', 'literal', 'flash-thunk', 'sram-thunk'];
const symbols = ['render_visual', 'screen_pixel', 'filtered_x', 'filtered_y', 'filtered_z',
  'retained_sin_roll', 'retained_cos_roll', 'identity', 'read_ok', 'sample', 'visual'];
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const parse = file => JSON.parse(fs.readFileSync(file));
const write = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
const execute = (cmd, args, timeout = 30000) => execFileSync(cmd, args, { cwd: root, timeout });
function buildPath(value) {
  const dir = path.resolve(value);
  assert(dir.startsWith(path.join(root, 'build') + path.sep), 'use a directory under the ignored build directory');
  return dir;
}
const pathIn = (dir, file) => path.join(dir, file);
function imageFiles(dir, name) {
  const stem = 'universe26/firmware/compiler_test';
  const uf2 = pathIn(dir, 'universe26/uf2/compiler_test.uf2');
  const collector = pathIn(dir, 'host-tools/compiler_test_host');
  const header = fs.readFileSync(pathIn(dir, 'universe26/firmware/compiler_test_build.h'), 'utf8');
  const buildId = Number.parseInt(header.match(/BUILD_ID 0x([0-9a-f]+)u/)[1], 16);
  const binary = fs.readFileSync(pathIn(dir, `${stem}.bin`));
  verifyUf2(binary, fs.readFileSync(uf2));
  return { name, buildId, uf2, sha256: sha(uf2), flashBytes: binary.length,
    collector, collectorSha256: sha(collector) };
}
function build(directory, inputs, names = roles) {
  assert.equal(inputs.length, names.length, 'expected one object per role');
  const out = buildPath(directory);
  fs.mkdirSync(out, { mode: 0o700 });
  const linker = fs.readFileSync(pathIn(root, 'badge-compiler/scripts/memmap_compiler_test.ld'), 'utf8')
    .replace('0x20000000', '0x80000000').replace('0x2006f000', '0x8006f000')
    .replaceAll('0x2007f000', '0x8007f000').replaceAll('0x2007f100', '0x8007f100');
  fs.writeFileSync(pathIn(out, 'oracle.ld'), linker);
  const oracleObject = pathIn(out, 'oracle.o');
  execute(process.env.LLVM_CLANG ?? 'clang', [
    '--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz', '-ffreestanding',
    '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-I', 'src/shared',
    '-I', 'src/platform/badger', '-I', 'src/tools/compiler_test', '-c',
    'badge-compiler/tests/fixtures/imu_application_oracle.c', '-o', oracleObject]);
  const results = [];
  let digests;
  for (const [index, name] of names.entries()) {
    const dir = path.join(out, name);
    fs.mkdirSync(dir);
    const object = path.join(dir, 'imu.o'), input = path.resolve(inputs[index]);
    const before = decodeElf(fs.readFileSync(input));
    for (const symbol of symbols) assert(before.symbols.some(s => s.name === symbol), `${name}: ${symbol} missing`);
    execute(process.env.LLVM_OBJCOPY ?? 'llvm-objcopy', [
      '--redefine-sym=bare_main=imu_demo_bare_main', ...symbols.map(x => `--globalize-symbol=${x}`), input, object]);
    const after = decodeElf(fs.readFileSync(object));
    for (const section of before.sections.filter(s => s.flags & 4)) {
      const linked = after.sections.find(s => s.name === section.name);
      assert.deepEqual(linked?.data, section.data, `${name}: original code bytes changed`);
    }
    const uf2 = pathIn(dir, 'universe26/uf2/compiler_test.uf2');
    const collector = pathIn(dir, 'host-tools/compiler_test_host');
    execute('make', ['-j5', `BUILD_DIR=${dir}`, `COMPILER_TEST_IMU_OBJECT=${object}`, uf2, collector], 120000);
    const oracleElf = pathIn(dir, 'oracle.elf');
    execute(process.env.LLVM_LLD ?? 'ld.lld', ['--gc-sections', '-T', pathIn(out, 'oracle.ld'),
      pathIn(dir, 'universe26/firmware/start.o'), pathIn(dir, 'universe26/firmware/compiler_imu_application.o'),
      object, oracleObject, '-o', oracleElf]);
    const verified = spawnSync(process.env.QEMU_ARM ?? 'qemu-system-arm',
      ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
        '-semihosting-config', 'enable=on,target=native', '-kernel', oracleElf],
      { cwd: root, encoding: 'utf8', timeout: 30000 });
    assert.ifError(verified.error);
    assert.equal(verified.status, 0, verified.stderr);
    const observed = verified.stderr.trim().split('\n');
    assert.equal(observed.length, 6);
    assert(observed.every(x => /^[0-9a-f]{8}$/.test(x)));
    if (digests) assert.deepEqual(observed, digests, `${name}: offline outputs differ`);
    else digests = observed;
    results.push({ ...imageFiles(dir, name), objectSha256: sha(object), digests });
  }
  const manifest = { schema: 1, images: results };
  write(pathIn(out, 'manifest.json'), manifest);
  console.log(JSON.stringify(manifest, null, 2));
}
function measure(directory, capture, serial, mazeDirectory) {
  assert(/^[0-9A-F]{16}$/.test(serial), 'one explicit BOOTSEL serial is required');
  const dir = buildPath(directory), out = buildPath(capture);
  const manifest = parse(pathIn(dir, 'manifest.json'));
  assert.equal(manifest.schema, 1);
  assert.deepEqual(manifest.images.map(x => x.name), mazeDirectory ? ['owned', 'candidate'] : roles);
  const alternate = (a, b) => [a, b, b, a];
  const plans = [{
    name: 'imu', images: manifest.images, decode: decodeImu, applications: imuApplications, id: imuSuiteId,
    order: mazeDirectory ? alternate('owned', 'candidate') : [...roles, ...[...roles].reverse()],
  }];
  if (mazeDirectory) {
    const dir = buildPath(mazeDirectory);
    plans.push({ name: 'maze', images: ['llvm', 'owned'].map(name => imageFiles(pathIn(dir, name), name)),
      decode: decodeApplication, applications, id: applicationSuiteId, order: alternate('llvm', 'owned') });
  }
  for (const image of plans.flatMap(plan => plan.images)) {
    assert.equal(sha(image.uf2), image.sha256, 'image drift');
    assert.equal(sha(image.collector), image.collectorSha256, 'collector drift');
  }
  fs.mkdirSync(out, { mode: 0o700 });
  const picotool = path.resolve(process.env.PICOTOOL ?? pathIn(root, 'build/picotool-usb/picotool'));
  let commandIndex = 0;
  function logged(cmd, args, label, timeout) {
    const r = spawnSync(cmd, args, { cwd: root, timeout, encoding: 'utf8' });
    const message = `${r.stdout ?? ''}${r.stderr ?? ''}${r.error?.message ?? ''}`;
    fs.writeFileSync(pathIn(out, `${String(commandIndex++).padStart(3, '0')}-${label}.log`), message);
    if (r.error || r.status !== 0) throw new Error(`${label}: ${message.trim()}`);
    return r.stdout;
  }
  function wait() {
    const deadline = Date.now() + 120000;
    while (Date.now() < deadline) {
      const p = spawnSync(picotool, ['info', '-d', '--ser', serial], { cwd: root, encoding: 'utf8', timeout: 2000 });
      if (p.status === 0 && p.stdout.includes('RP2350')) return;
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 250);
    }
    throw new Error('Badge did not return to BOOTSEL; use physical BOOTSEL recovery before restoring');
  }
  const command = (args, label) => {
    if (label.endsWith('-restore')) wait();
    return logged(picotool, args, label, 180000);
  };
  const report = { schema: 1, scope: 'single-badge application timing; normal SIO only',
    images: plans.flatMap(plan => plan.images.map(image => ({ suite: plan.name, ...image }))),
    errors: [], backups: [], restoration: [], runs: [] };
  const save = () => write(pathIn(out, 'capture.json'), report);
  preserveFlash([serial], out, command, report, save, () => {
    report.comparisons = [];
    for (const plan of plans) {
      let control;
      for (const [index, name] of plan.order.entries()) {
        const image = plan.images.find(x => x.name === name);
        const nonce = crypto.randomBytes(16);
        nonce.writeUInt32LE(123456789);
        const prefix = `${plan.name}-${index}-${name}`;
        const input = pathIn(out, `${prefix}-request.bin`), output = pathIn(out, `${prefix}-response.bin`);
        fs.writeFileSync(input, request(image.buildId, nonce, plan.id));
        command(['load', '-v', image.uf2, '--ser', serial], `${prefix}-flash`);
        command(['reboot', '--ser', serial], `${prefix}-boot`);
        logged(image.collector, ['--single', serial, input, output], `${prefix}-usb`, 65000);
        wait();
        const timing = plan.decode(fs.readFileSync(output), image.buildId, nonce);
        if (image.digests) assert.deepEqual(timing.modules.map(module => module.values[0].toString(16).padStart(8, '0')),
          image.digests, `${name}: hardware results differ from the offline oracle`);
        if (control) {
          assert.deepEqual(timing.clock, control.clock, 'clock configuration changed');
          assert(compare(control.modules, timing.modules, plan.applications)
            .every(m => m.matched), `${plan.name} outputs differ for ${name}`);
        } else control = timing;
        report.runs.push({ suite: plan.name, name, responseSha256: sha(output), nonce: nonce.toString('hex'), timing });
        report.comparisons.push({ suite: plan.name, name, versusControl: compareTiming(control, timing) });
        save();
        console.log(`${prefix}: all result digests matched`);
      }
    }
  });
  assert.equal(report.restoration.length, 1, 'missing restoration');
  assert(report.restoration[0].verified, 'unverified restoration');
  report.status = 'measured-restored';
  save();
  console.log('Original full flash restored, verified and left in BOOTSEL.');
}
try {
  const [mode, ...args] = process.argv.slice(2);
  if (mode === 'build' && args.length === 6) build(args[0], args.slice(1));
  else if (mode === 'build-pair' && args.length === 3) build(args[0], args.slice(1), ['owned', 'candidate']);
  else if (mode === 'measure' && args.length === 3) measure(...args);
  else if (mode === 'measure-followup' && args.length === 4) measure(...args);
  else throw new Error('Usage: imu-benchmark.mjs build NEW_DIR LLVM.o OWNED.o LITERAL.o FLASH_THUNK.o SRAM_THUNK.o | build-pair NEW_DIR BASELINE.o CANDIDATE.o | measure BUILD_DIR NEW_CAPTURE_DIR SERIAL | measure-followup IMU_DIR NEW_CAPTURE_DIR SERIAL MAZE_DIR');
} catch (error) {
  console.error(`IMU benchmark: ${error.message}`);
  process.exitCode = 1;
}
