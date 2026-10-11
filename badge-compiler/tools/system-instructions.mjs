import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { executable } from './pipeline-stages.mjs';
import { decodeElf } from './boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const compiler = path.join(root, 'badge-compiler');
const fixtures = path.join(compiler, 'tests/fixtures');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const tools = () => ({
  asm: path.join(compiler, 'build/badge-asm'),
  clang: executable(process.env.LLVM_CLANG ?? 'clang'),
  lld: executable(process.env.LLVM_LLD ?? 'ld.lld'),
  qemu: executable(process.env.QEMU_ARM ?? 'qemu-system-arm'),
  objcopy: executable(process.env.LLVM_OBJCOPY ?? 'llvm-objcopy'),
});
const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
function run(directory, label, command, args, timeout = 30000) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout, maxBuffer: 4000000 });
  fs.writeFileSync(path.join(directory, `${label}.log`), `${result.stdout ?? ''}${result.stderr ?? ''}`);
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${label}: ${result.stdout ?? ''}${result.stderr ?? ''}`);
  return result.stdout;
}
async function waitForBootsel(directory, picotool, serial) {
  const deadline = Date.now() + 45000;
  for (let attempt = 0; Date.now() < deadline; ++attempt) {
    const result = spawnSync(picotool, ['info', '-d', '--ser', serial],
      { cwd: root, encoding: 'utf8', timeout: 5000 });
    fs.writeFileSync(path.join(directory, `recovery-${attempt}.log`),
      `${result.stdout ?? ''}${result.stderr ?? ''}`);
    assert.ifError(result.error);
    if (result.status === 0) {
      assert(/RP2350/.test(result.stdout) && /current cpu:\s+ARM/.test(result.stdout));
      return;
    }
    assert(/No accessible RP-series devices|device returned an error: rebooting/.test(`${result.stdout}${result.stderr}`),
      `unexpected recovery error: ${result.stdout}${result.stderr}`);
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  throw new Error('badge did not return to BOOTSEL within 45 seconds; use its BOOTSEL button before restoring');
}
function objects(directory, sources, config) {
  const routes = { owned: [], llvm: [] };
  for (const source of sources) for (const role of Object.keys(routes)) {
    const output = path.join(directory, `${path.basename(source)}-${role}.o`);
    run(directory, `${path.basename(source)}-${role}`, role === 'owned' ? config.asm : config.clang,
      role === 'owned' ? [source, '-o', output] : [...target, '-x', 'assembler', '-c', source, '-o', output]);
    routes[role].push(output);
  }
  return routes;
}
export function qualifySystem(directory) {
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(path.join(directory, 'results.json'),
    JSON.stringify({ schema: 1, milestone: 'BA03', status: 'in-progress' }) + '\n');
  const config = tools(), sources = ['system_probe.S', 'system_runtime_start.S'].map(s => path.join(fixtures, s));
  const routes = objects(directory, sources, config), runtime = [];
  const script = path.join(fixtures, 'system_runtime.ld');
  for (const [role, inputs] of Object.entries(routes)) {
    const elf = path.join(directory, `${role}.elf`), binary = path.join(directory, `${role}.bin`);
    run(directory, `${role}-link`, config.lld, ['-T', script, ...inputs, '-o', elf]);
    run(directory, `${role}-binary`, config.objcopy, ['-O', 'binary', elf, binary]);
    run(directory, `${role}-runtime`, config.qemu, ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', elf], 15000);
    runtime.push({ role, elf, sha256: hash(elf), binary, binary_sha256: hash(binary), exit: 0 });
  }
  assert.equal(runtime[0].binary_sha256, runtime[1].binary_sha256, 'runtime assembler binaries differ');
  const inputs = [...sources, script, fileURLToPath(import.meta.url), ...Object.values(config), path.join(compiler, 'src/badge_thumb.h'),
    path.join(compiler, 'src/badge_asm.c')].map(file => ({ file, sha256: hash(file) }));
  const result = { schema: 1, milestone: 'BA03', status: 'host-runtime-qualified', inputs, runtime };
  fs.writeFileSync(path.join(directory, 'results.json'), JSON.stringify(result, null, 2) + '\n');
  return result;
}
export function verifySystem(result) {
  assert.equal(result.schema, 1);
  assert.equal(result.milestone, 'BA03');
  assert.equal(result.status, 'host-runtime-qualified');
  assert.deepEqual(result.runtime.map(route => route.role), ['owned', 'llvm']);
  for (const item of result.inputs) assert.equal(hash(item.file), item.sha256, 'system qualification input drift');
  for (const route of result.runtime) {
    assert.equal(route.exit, 0);
    assert.equal(hash(route.elf), route.sha256);
    assert.equal(hash(route.binary), route.binary_sha256);
  }
  assert.equal(result.runtime[0].binary_sha256, result.runtime[1].binary_sha256);
  return result;
}

export function validateHardwareImage(file) {
  const bytes = fs.readFileSync(file), elf = decodeElf(bytes);
  const reset = elf.symbols.find(symbol => symbol.name === 'ba03_reset');
  assert(reset && reset.binding === 1, 'hardware reset must be global');
  assert.equal(elf.entry, reset.value, 'hardware ELF entry differs from reset');
  assert(elf.entry & 1, 'hardware ELF entry must be Thumb');
  const vectors = elf.symbols.find(symbol => symbol.name === 'ba03_vectors');
  assert.equal(vectors?.value, 0x10000000, 'hardware vector table must start flash');
  const segment = elf.segments.find(s => s.address === vectors.value && s.fileSize >= 8);
  assert(segment, 'hardware vector table is not loaded');
  assert.equal(bytes.readUInt32LE(segment.offset), 0x20082000, 'hardware initial stack');
  assert.equal(bytes.readUInt32LE(segment.offset + 4), elf.entry, 'reset vector differs from entry');
  const commit = elf.symbols.find(symbol => symbol.name === 'ba03_flash_commit');
  assert(commit && commit.value >= 0x20000001 && commit.value < 0x2007f000,
    'flash commit must execute from RAM');
  const ram = elf.segments.find(s => s.address <= (commit.value & ~1) &&
    s.address + s.fileSize >= (commit.value & ~1) + commit.size);
  assert(ram && ram.load >= 0x10000000 && ram.load + ram.fileSize < 0x10400000,
    'flash commit RAM code must have a firmware load image');
  return { entry: elf.entry, stack: 0x20082000 };
}

export function buildHardware(directory, smoke = false, synchronization = false, stage = 0) {
  assert(!smoke || !synchronization, 'synchronization campaign requires its full probe');
  assert(Number.isInteger(stage) && stage >= 0 && stage <= 7 && (!stage || synchronization),
    'unsupported synchronization startup stage');
  fs.mkdirSync(directory, { recursive: true });
  const config = tools();
  const flags = [...target, '-std=c11', '-Oz', '-ffreestanding', '-fno-builtin', '-fno-unwind-tables',
    '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections', '-I', path.join(root, 'src/shared'),
    ...(smoke ? ['-DBA03_BOOT_SMOKE'] : []), ...(synchronization ? ['-DBA05_SYNCHRONIZATION',
      `-DBA05_STAGE=${stage}`] : [])];
  const sources = [path.join(fixtures, 'system_badge.c'), path.join(root, 'src/arch/rp2350/bootrom_reboot.c'),
    ...(synchronization ? [path.join(fixtures, 'synchronization_badge.c')] : [])];
  const assembly = sources.map(source => {
    const output = path.join(directory, `${path.basename(source)}.s`);
    run(directory, `${path.basename(source)}-compile`, config.clang, [...flags, '-S', source, '-o', output]);
    return output;
  });
  assembly.push(path.join(fixtures, synchronization ? 'synchronization_probe.S' : 'system_probe.S'), path.join(fixtures, 'system_flash.S'),
    path.join(root, 'src/arch/rp2350/picobin.S'));
  const routes = objects(directory, assembly, config), images = [];
  const script = path.join(root, 'src/arch/rp2350/memmap_report.ld');
  for (const [role, inputs] of Object.entries(routes)) {
    const elf = path.join(directory, `${role}.elf`), binary = path.join(directory, `${role}.bin`);
    run(directory, `${role}-link`, config.lld, ['-T', script, '-e', 'ba03_reset',
      '--fatal-warnings', '--defsym=ba03_progress=0x2007f140', '--gc-sections', ...inputs, '-o', elf]);
    const boot = validateHardwareImage(elf);
    if (synchronization) {
      const symbols = decodeElf(fs.readFileSync(elf)).symbols;
      const park = symbols.find(symbol => symbol.name === 'ba05_park');
      assert(park && park.value >= 0x20000001 && park.value < 0x2007f000,
        'core 1 must acknowledge completion while executing from SRAM');
    }
    run(directory, `${role}-binary`, config.objcopy, ['-O', 'binary', elf, binary]);
    images.push({ role, elf, sha256: hash(elf), binary, binary_sha256: hash(binary), boot });
  }
  assert.equal(images[0].binary_sha256, images[1].binary_sha256, 'physical assembler binaries differ');
  return { mode: smoke ? 'boot-smoke' : stage ? `startup-stage-${stage}` :
    synchronization ? 'synchronization-probe' : 'system-probe',
    report_bytes: synchronization ? 256 : 84, images,
    inputs: [...sources, ...assembly, script].map(file => ({ file, sha256: hash(file) })) };
}

export function validateHardwareResponse(bytes, nonce, mode) {
  assert.equal(bytes.length, 84, 'physical report size');
  assert.equal(nonce.length, 16, 'physical nonce size');
  assert(['boot-smoke', 'system-probe'].includes(mode), 'unknown physical report mode');
  assert.equal(bytes.readUInt32LE(0), 0x33304142, 'physical report magic');
  assert.equal(bytes.readUInt32LE(4), 3, 'physical report version');
  assert(bytes.subarray(8, 24).equals(nonce), 'stale response nonce');
  const response = { result: bytes.readUInt32LE(24), exception: bytes.readUInt32LE(28),
    before: [0, 1, 2, 3].map(i => bytes.readUInt32LE(32 + i * 4)),
    after: [0, 1, 2, 3].map(i => bytes.readUInt32LE(48 + i * 4)) };
  assert.equal(response.result, 0, 'physical probe failed');
  assert.equal(response.exception, mode === 'boot-smoke' ? 0 : 11, 'physical exception mismatch');
  assert.deepEqual(response.before, response.after, 'physical register state not restored');
  assert.equal(bytes.readUInt32LE(64), 0x104, 'diagnostic did not finish publishing');
  assert.equal(bytes.readUInt32LE(80), 0, 'recovery watchdog was not scheduled');
  return response;
}

export async function restoreHardware(directory) {
  const file = path.join(directory, 'results.json');
  const result = JSON.parse(fs.readFileSync(file));
  assert.equal(result.schema, 1);
  assert(['BA03', 'BA05'].includes(result.milestone));
  assert(/^[0-9A-F]{16}$/.test(result.serial), 'expected preserved BOOTSEL serial');
  const picotool = executable(process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool'));
  assert.equal(path.resolve(result.backup.file), path.join(path.resolve(directory), 'original.bin'),
    'backup must belong to this campaign');
  assert.equal(fs.statSync(result.backup.file).size, 16777216);
  assert.equal(hash(result.backup.file), result.backup.sha256, 'backup drift');
  await waitForBootsel(directory, picotool, result.serial);
  const command = (label, args) => run(directory, label, picotool, [...args, '--ser', result.serial], 180000);
  const identity = command('restore-identity', ['info', '-d']);
  assert(/RP2350/.test(identity) && /flash size:\s+16384K/.test(identity), 'restoration device mismatch');
  command('restore', ['load', '-v', '-u', result.backup.file, '-t', 'bin', '-o', '0x10000000', '--ignore-partitions']);
  const restored = path.join(directory, 'restored.bin');
  command('restore-readback', ['save', '-a', '-v', restored, '-t', 'bin']);
  assert.equal(hash(restored), result.backup.sha256, 'flash restoration mismatch');
  result.restored = true;
  result.restoration = { file: restored, sha256: hash(restored), left_in_bootsel: true };
  if (result.status !== 'physical-qualified') result.status = 'physical-failed-restored';
  fs.writeFileSync(file, JSON.stringify(result, null, 2) + '\n');
  return result;
}

export async function hardware(serial, directory, qualification) {
  assert(/^[0-9A-F]{16}$/.test(serial), 'expected explicit BOOTSEL serial');
  assert(!fs.existsSync(directory), 'fresh hardware evidence directory required');
  fs.mkdirSync(directory);
  const picotool = executable(process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool'));
  if (qualification) assert.equal(qualification.milestone, 'BA05', 'unsupported physical qualification');
  const host = qualification ? qualification.host(directory) : qualifySystem(path.join(compiler, 'build/ba03-system'));
  const result = { schema: 1, milestone: qualification?.milestone ?? 'BA03', serial, status: 'preparing', host,
    backup: null, response: null, restored: false };
  const save = () => fs.writeFileSync(path.join(directory, 'results.json'), JSON.stringify(result, null, 2) + '\n');
  save();
  const command = (label, args) => run(directory, label, picotool, [...args, '--ser', serial], 180000);
  const identity = command('identity', ['info', '-d']);
  assert(/RP2350/.test(identity) && /current cpu:\s+ARM/.test(identity) &&
    /secure boot:\s+0/.test(identity) && /flash size:\s+16384K/.test(identity), 'unsupported badge configuration');
  result.campaigns = [buildHardware(path.join(directory, 'boot-smoke'), true),
    ...(qualification?.campaigns ? qualification.campaigns(directory) :
      [buildHardware(path.join(directory, qualification ? 'synchronization-probe' : 'system-probe'), false, Boolean(qualification))])];
  result.inputs = [picotool, fileURLToPath(import.meta.url)].map(file => ({ file, sha256: hash(file) }));
  const backup = path.join(directory, 'original.bin');
  command('backup', ['save', '-a', '-v', backup, '-t', 'bin']);
  assert.equal(fs.statSync(backup).size, 16777216);
  result.backup = { file: backup, sha256: hash(backup) }; save();
  try {
    for (const campaign of result.campaigns) {
      const evidence = path.join(directory, campaign.mode);
      const command = (label, args) => run(evidence, label, picotool, [...args, '--ser', serial], 180000);
      const nonce = crypto.randomBytes(16), request = path.join(evidence, 'request.bin'),
        clear = path.join(evidence, 'clear.bin'), response = path.join(evidence, 'response.bin');
      fs.writeFileSync(request, nonce); fs.writeFileSync(clear, Buffer.alloc(campaign.report_bytes));
      campaign.nonce = nonce.toString('hex'); result.status = `deploying-${campaign.mode}`; save();
      command('load', ['load', '-v', campaign.images[0].elf]);
      command('clear', ['load', '-v', clear, '-t', 'bin', '-o', '0x2007f100']);
      const invalid = path.join(evidence, 'invalid-report.bin');
      fs.writeFileSync(invalid, Buffer.alloc(256));
      command('invalidate-report', ['load', '-v', invalid, '-t', 'bin', '-o', '0x10fff000', '--ignore-partitions']);
      command('request', ['load', '-v', request, '-t', 'bin', '-o', '0x2007f000']);
      const recoveryStart = Date.now();
      command('run', ['reboot']);
      result.status = 'awaiting-recovery'; save();
      // Do not mistake the pre-reboot USB instance for the recovered device.
      await new Promise(resolve => setTimeout(resolve, 1500));
      await waitForBootsel(evidence, picotool, serial);
      command('recovered-identity', ['info', '-a']);
      campaign.recovery_observed_ms = Date.now() - recoveryStart;
      command('response', ['save', '-r', '0x10fff000', `0x${(0x10fff000 + campaign.report_bytes).toString(16)}`, response, '-t', 'bin']);
      const bytes = fs.readFileSync(response);
      campaign.response = { file: response, sha256: hash(response), words:
        Array.from({ length: bytes.length / 4 }, (_, i) => bytes.readUInt32LE(i * 4)) };
      save();
      Object.assign(campaign.response, (qualification?.validate ?? validateHardwareResponse)(bytes, nonce, campaign.mode));
      result.response = campaign.response;
      campaign.status = 'passed'; save();
    }
    result.status = 'physical-qualified';
  } catch (error) {
    result.failure = error.message;
    result.status = 'physical-failed';
    save();
    throw error;
  } finally {
    save();
    try {
      Object.assign(result, await restoreHardware(directory));
    } catch (error) {
      result.status = 'recovery-required';
      result.restoration_failure = error.message;
      save();
      throw new Error(`Original flash is NOT restored: ${error.message}. Preserved backup: ${backup}`, { cause: error });
    }
  }
  return result;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2);
    assert(args.length === 1 && args[0] === '--qualify' || args.length === 2 && args[0] === '--verify' ||
      args.length === 3 && args[0] === '--hardware-flash-report',
      'usage: node system-instructions.mjs --qualify | --verify RESULTS.json | --hardware-flash-report BOOTSEL_SERIAL NEW_EVIDENCE_DIRECTORY (replaces application and final 4 KiB sector; requires permission; restores full flash)');
    const result = args[0] === '--qualify' ? qualifySystem(path.join(compiler, 'build/ba03-system')) :
      args[0] === '--verify' ? verifySystem(JSON.parse(fs.readFileSync(args[1]))) :
        await hardware(args[1], path.resolve(args[2]));
    console.log(`BA03: ${result.status}${result.restored ? '; original flash restored, badge in BOOTSEL' : ''}`);
  } catch (error) { console.error(`BA03: ${error.message}`); process.exitCode = 1; }
}
