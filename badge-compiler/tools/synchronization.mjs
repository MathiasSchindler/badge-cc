import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { buildHardware, hardware, restoreHardware, validateHardwareResponse } from './system-instructions.mjs';
import { executable } from './pipeline-stages.mjs';

const compiler = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

export const startupStages = [
  'core-launch-inherited', 'single-core-exclusives', 'cross-core-invalidation',
  'xosc-enable', 'safe-clock-switch', 'watchdog-inherited', 'watchdog-xosc'
];

function reportHeader(bytes, nonce) {
  assert.equal(bytes.length, 256, 'synchronization report size');
  assert.equal(nonce.length, 16, 'synchronization nonce size');
  const word = i => bytes.readUInt32LE(i * 4);
  assert.equal(word(0), 0x35304142, 'synchronization report magic');
  assert.equal(word(1), 6, 'synchronization report version');
  assert(bytes.subarray(8, 24).equals(nonce), 'stale synchronization report nonce');
  assert.equal(word(6), 0, 'physical synchronization failed');
  assert.equal(word(7), 0, 'unexpected exception');
  assert.deepEqual([8, 9, 10, 11].map(word), [12, 13, 14, 15].map(word), 'core 0 state was not restored');
  assert.equal(word(16), 0x104, 'report did not finish publishing');
  assert.deepEqual([17, 18, 19].map(word), [0, 0, 0], 'fault registers');
  assert.equal(word(20), 0, 'recovery watchdog failed');
  return word;
}

export function validateSynchronizationResponse(bytes, nonce, mode) {
  if (mode === 'boot-smoke') return validateHardwareResponse(bytes, nonce, mode);
  assert.equal(mode, 'synchronization-probe', 'unknown synchronization report mode');
  const word = reportHeader(bytes, nonce);
  assert.equal(word(21), 0x5352414d, 'SRAM protocol identity');
  assert.equal(word(22), 1024, 'per-core iteration count');
  assert.equal(word(23), 2048, 'lost or duplicated updates');
  assert.equal(word(24), 2048 * 2049 / 2, 'payload ordering invariant');
  assert.deepEqual([25, 26].map(word), [1024, 1024], 'both cores must make progress');
  assert.deepEqual([27, 28].map(word), [0, 0], 'core timeout or payload error');
  assert.notEqual(word(29), 0, 'competing core write must invalidate reservation');
  assert.equal(word(30), 1, 'wrong second core');
  assert.equal(word(31), 0, 'FIFO launch failed');
  assert.equal(word(32), 1, 'contended bounded lock must time out');
  assert.equal(word(33), 0x12345678, 'failed exclusive store modified payload');
  for (const [index, value] of [[36, 65536], [38, 1], [40, 2], [42, 65536], [44, 4]]) {
    assert.equal(word(index), value, 'core 0 clock configuration');
    assert.equal(word(index + 1), value, 'core 1 clock configuration');
  }
  assert.equal(word(34) & 1, 0, 'core 0 must use clk_ref');
  assert.equal(word(34), word(35), 'core system clock controls differ');
  assert.equal(word(46), word(47), 'core CPU profiles differ');
  assert.equal(word(46) >>> 4 & 0xfff, 0xd21, 'expected Cortex-M33');
  assert.deepEqual([48, 49, 50, 51, 54, 55].map(word), [0, 0, 0, 0, 0, 0],
    'protocol requires privileged Thread mode, interrupts unmasked and MPU disabled');
  assert.equal(word(56), word(57), 'core ACTLR configurations differ');
  assert(word(56) & 0x20000000, 'external exclusives must be enabled on both cores');
  assert.equal(word(52), 0x10000000, 'core 0 vector table');
  assert.equal(word(53), word(61), 'core 1 vector table');
  assert.equal(word(58), 1, 'core 1 must finish');
  assert.equal(word(59), 1 << 24, 'core 1 must be held in reset before flash publication');
  assert(word(60) & 1 && word(60) >= 0x20000001 && word(60) < 0x2007f000, 'park must execute from SRAM');
  assert.equal(word(62), 0, 'protocol result');
  assert(word(63) & 0x80000000, 'XOSC must be stable');
  return { result: 0, iterations_per_core: 1024, total_updates: word(23), checksum: word(24),
    invalidated_store_status: word(29), contended_timeout: word(32),
    nominal_clock_hz: 12000000, clock_source: 'shared clk_sys = clk_ref = badge 12 MHz XOSC, divisors 1',
    measured_timing: false, core1_park: word(60), core1_vectors: word(61) };
}

export function validateStartupResponse(bytes, nonce, mode) {
  if (mode === 'boot-smoke') return validateHardwareResponse(bytes, nonce, mode);
  const match = /^startup-stage-([1-7])$/.exec(mode);
  assert(match, 'unknown startup stage');
  const stage = Number(match[1]), word = reportHeader(bytes, nonce);
  assert.equal(word(21), 0x4c354142, 'startup ladder identity');
  assert.equal(word(22), stage, 'wrong startup stage');
  assert.equal(word(62), 0, 'startup stage failed');
  assert.deepEqual([27, 28].map(word), [0, 0], 'per-core errors');
  assert.equal(word(59), 1 << 24, 'core 1 must be held in reset before publication');
  assert(word(60) & 1 && word(60) >= 0x20000001 && word(60) < 0x2007f000, 'park must execute from SRAM');
  assert.equal(word(46), word(47), 'snapshot CPU profiles differ');
  assert.equal(word(46) >>> 4 & 0xfff, 0xd21, 'expected Cortex-M33');
  assert.deepEqual([48, 49, 50, 51, 54, 55].map(word), [0, 0, 0, 0, 0, 0], 'unexpected control/mask/MPU state');
  assert.equal(word(56), word(57), 'ACTLR state drift');
  assert(word(56) & 0x20000000, 'external exclusives must be enabled in both snapshots');
  assert.equal(word(52), 0x10000000, 'core 0 vector table');
  const multicore = stage === 1 || stage === 3, switched = stage === 5 || stage === 7;
  assert.equal(word(56), (word(24) | 0x20000000) >>> 0, 'core 0 ACTLR unrelated bits changed');
  assert.equal(word(25), word(24), 'core 0 ACTLR was not restored');
  if (multicore) {
    assert.equal(word(57), (word(26) | 0x20000000) >>> 0, 'core 1 ACTLR unrelated bits changed');
    assert.equal(word(30), 1, 'wrong second core');
    assert.equal(word(31), 0, 'FIFO launch failed');
    assert.equal(word(53), word(61), 'core 1 vectors');
    assert.equal(word(58), 1, 'core 1 did not finish');
  } else {
    assert.equal(word(30), 0, 'unexpected second core launch');
    assert.equal(word(53), word(52), 'core 0 vectors changed');
    assert.equal(word(58), 0, 'unexpected second core progress');
  }
  if (!switched)
    for (const i of [34, 36, 38, 40, 42, 44]) assert.equal(word(i), word(i + 1), 'inherited clocks changed');
  else {
    assert.equal(word(35), word(34) & ~1, 'live AUXSRC must be preserved during glitchless switch');
    assert.equal(word(37), 65536, 'system divisor');
    assert.equal(word(39), 1, 'system selected source');
    assert.equal(word(41), (word(40) & ~3) | 2, 'reference must select XOSC without changing AUXSRC');
    assert.equal(word(43), 65536, 'reference divisor');
    assert.equal(word(45), 4, 'reference selected source');
  }
  if (stage === 4 || switched) assert(word(63) & 0x80000000, 'XOSC must be stable');
  if (stage === 2) assert.equal(word(23), 0, 'single-core exclusive/timeout probe failed');
  if (stage === 3) {
    assert.notEqual(word(29), 0, 'competing core must invalidate reservation');
    assert.equal(word(33), 0x12345678, 'failed store modified competing payload');
  }
  if (stage === 6 || stage === 7) {
    assert.equal(word(31), 0, 'watchdog scheduling failed');
    assert.equal(word(32), 250, 'wrong scheduled delay');
  }
  return { result: 0, stage, stage_name: startupStages[stage - 1],
    snapshots: multicore ? 'core0/core1' : 'core0-before/core0-after',
    recovery: stage >= 6 ? 'scheduled-watchdog-only' : 'explicit-bootrom-reboot',
    clocks: [34, 36, 38, 40, 42, 44].map(i => ({ before: word(i), after: word(i + 1) })),
    external_exclusives: { core0_before: word(24), core0_active: word(56),
      core0_restored: word(25), ...(multicore ? { core1_before: word(26), core1_active: word(57) } : {}) },
    full_contention_tested: false };
}

export function qualifySynchronization(directory) {
  const log = path.join(directory, 'host.log');
  const result = spawnSync(process.execPath, ['--test', path.join(compiler, 'tests/synchronization.test.mjs')],
    { encoding: 'utf8', timeout: 180000 });
  fs.writeFileSync(log, `${result.stdout ?? ''}${result.stderr ?? ''}`);
  assert.ifError(result.error);
  assert.equal(result.status, 0, `host qualification failed; see ${log}`);
  return { status: 'host-qualified', log, sha256: hash(log),
    inputs: [fileURLToPath(import.meta.url), path.join(compiler, 'tools/system-instructions.mjs'),
      path.join(compiler, 'tests/synchronization.test.mjs'),
      path.join(compiler, 'build/badge-asm'), path.join(compiler, 'src/badge_asm.c'),
      path.join(compiler, 'src/badge_thumb.h'),
      ...['synchronization_probe.S', 'synchronization_runtime.S', 'synchronization_oracle_fixture.S',
        'system_runtime.ld'].map(file => path.join(compiler, 'tests/fixtures', file)),
      ...[process.env.LLVM_CLANG ?? 'clang', process.env.LLVM_LLD ?? 'ld.lld',
        process.env.QEMU_ARM ?? 'qemu-system-arm'].map(executable)]
      .map(file => ({ file, sha256: hash(file) })) };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2);
    assert(args.length === 2 && ['--build', '--build-startup', '--restore'].includes(args[0]) ||
      args.length === 3 && ['--hardware-flash-report', '--hardware-startup-ladder'].includes(args[0]),
    'usage: node synchronization.mjs --build NEW_DIRECTORY | --build-startup NEW_DIRECTORY | --restore EVIDENCE_DIRECTORY | --hardware-flash-report SERIAL NEW_DIRECTORY | --hardware-startup-ladder SERIAL NEW_DIRECTORY (hardware requires permission; replaces application/final 4 KiB sector and restores full flash)');
    if (args[0] === '--build') {
      assert(!fs.existsSync(args[1]), 'fresh image directory required');
      const result = buildHardware(path.resolve(args[1]), false, true);
      fs.writeFileSync(path.join(args[1], 'images.json'), JSON.stringify(result, null, 2) + '\n');
      console.log('BA05: both assembler images built and verified');
    } else if (args[0] === '--build-startup') {
      assert(!fs.existsSync(args[1]), 'fresh image directory required');
      fs.mkdirSync(args[1]);
      const images = startupStages.map((name, i) => ({ name,
        ...buildHardware(path.join(args[1], name), false, true, i + 1) }));
      fs.writeFileSync(path.join(args[1], 'images.json'), JSON.stringify(images, null, 2) + '\n');
      console.log('BA05: isolated startup ladder images built; no hardware touched');
    } else if (args[0] === '--restore') {
      const result = await restoreHardware(path.resolve(args[1]));
      console.log(`BA05: ${result.status}; original flash restored, selected badge in BOOTSEL`);
    } else {
      const startup = args[0] === '--hardware-startup-ladder';
      const result = await hardware(args[1], path.resolve(args[2]), { milestone: 'BA05',
        host: qualifySynchronization,
        ...(startup ? { campaigns: directory => startupStages.map((name, i) =>
          ({ ...buildHardware(path.join(directory, `startup-stage-${i + 1}`), false, true, i + 1), name })) } : {}),
        validate: startup ? validateStartupResponse : validateSynchronizationResponse });
      console.log(`BA05: ${result.status}; original flash restored`);
    }
  } catch (error) { console.error(`BA05: ${error.message}`); process.exitCode = 1; }
}
