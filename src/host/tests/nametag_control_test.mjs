import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { runControl, restoreControl, validateIdentity, validateReports } from '../tools/nametag_control.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const primary = '9BBF88DD17D66E7D';
const control = '08DA26F86E57F5EA';
const identity = serial => `type: RP2350\nrevision: A4\npackage: QFN80\nchipid: 0x${serial.toLowerCase()}
current cpu: ARM\nsecure boot: 0\nboot type: bootsel\nflash size: 16384K\n`;
const reports = () => Array.from({ length: 3 }, (_, i) => ({
  verified: true, test: 'known-report', transport: 'live-usb-control', serial: primary,
  run: i + 1, build_id: '12345678', nonce: (i + 1).toString(16).padStart(32, '0')
}));
const jsonl = records => records.map(record => JSON.stringify(record)).join('\n') + '\n';
const ok = stdout => ({ status: 0, stdout: stdout ?? '', stderr: '' });

test('identity and report contracts reject stale, wrong-device and malformed evidence', () => {
  validateIdentity(identity(primary), primary);
  for (const text of [identity(control), identity(primary).replace('A4', 'A3'),
    identity(primary).replace('16384K', '4096K'), identity(primary).replace('bootsel', 'normal'),
    identity(primary) + identity(primary)]) assert.throws(() => validateIdentity(text, primary));
  validateReports(jsonl(reports()), primary, '12345678');
  for (const field of ['verified', 'test', 'transport', 'serial', 'run', 'build_id', 'nonce']) {
    const records = reports();
    records[0][field] = null;
    assert.throws(() => validateReports(jsonl(records), primary, '12345678'));
  }
  const repeated = reports();
  repeated[1].nonce = repeated[0].nonce;
  assert.throws(() => validateReports(jsonl(repeated), primary, '12345678'));
  assert.throws(() => validateReports('', primary, '12345678'));
  assert.throws(() => validateReports('{bad json}', primary, '12345678'));
});

async function scenario(mode) {
  fs.mkdirSync(path.join(root, 'build/host-tools'), { recursive: true });
  const base = fs.mkdtempSync(path.join(root, 'build/host-tools/nametag-control-test-'));
  const directory = path.join(base, 'run');
  const picotool = path.join(base, 'picotool');
  fs.writeFileSync(picotool, 'mock');
  const flash = {
    [primary]: Buffer.alloc(16 * 1024 * 1024, 0x31),
    [control]: Buffer.alloc(16 * 1024 * 1024, 0x42)
  };
  const calls = [];
  let loads = 0, absent = false, failed = false;
  const execute = (command, args, options) => {
    calls.push({ command, args });
    assert(options.timeout > 0);
    assert.equal(options.killSignal, 'SIGKILL');
    if (command === 'make') {
      const build = args[0].slice('BUILD_DIR='.length);
      for (const file of ['universe26/uf2/usb_live_report.uf2',
        'universe26/firmware/usb_live_report.elf', 'host-tools/usb_live_host', 'host-tools/bin2uf2']) {
        fs.mkdirSync(path.dirname(path.join(build, file)), { recursive: true });
        fs.writeFileSync(path.join(build, file), 'mock artifact');
      }
      fs.writeFileSync(path.join(build, 'universe26/firmware/usb_live_build.h'),
        '#define USB_REPORT_BUILD_ID 0x12345678u\n');
      return ok();
    }
    if (command !== picotool) {
      assert.deepEqual(args, [primary, '3']);
      if (mode === 'collector-timeout') return { error: new Error('collector deadline'), status: null };
      if (mode === 'collector-failure') return { status: 1, stderr: 'failed collector' };
      const records = reports();
      if (mode === 'stale') records[1].nonce = records[0].nonce;
      return ok(jsonl(records));
    }
    const serial = args[args.indexOf('--ser') + 1];
    assert([primary, control].includes(serial), 'every device operation is serial-selected');
    if (args[0] === 'info') {
      if (mode === 'wrong-identity' && loads === 0) return ok(identity(control));
      if (mode === 'absent-preflight') return { status: 1, stderr: 'No accessible RP-series devices' };
      if (mode === 'never-recover' && loads > 0 && serial === primary) {
        return { status: 1, stderr: 'No accessible RP-series devices' };
      }
      if (absent && serial === primary) {
        absent = false;
        return { status: 1, stderr: 'No accessible RP-series devices' };
      }
      return ok(identity(serial));
    }
    if (args[0] === 'save') {
      assert.deepEqual(args.slice(0, 4), ['save', '-r', '0x10000000', '0x11000000']);
      let bytes = flash[serial];
      if (mode === 'short-backup' && loads === 0) bytes = bytes.subarray(0, 4096);
      if (mode === 'restore-mismatch' && args[4].endsWith('-restored.bin')) bytes = Buffer.alloc(bytes.length);
      fs.writeFileSync(args[4], bytes);
      return ok();
    }
    if (args[0] === 'verify') {
      assert.deepEqual(fs.readFileSync(args[1]), flash[serial]);
      return ok();
    }
    assert.equal(args[0], 'load');
    assert.equal(serial, primary, 'control must never be flashed');
    if (args.includes('-x')) {
      loads++;
      flash[serial] = Buffer.alloc(flash[serial].length, 0x99);
      absent = mode !== 'no-departure';
      if (mode === 'control-changed') flash[control][0] = 0;
      if (mode === 'load-failure' && !failed) {
        failed = true;
        return { status: 1, stderr: 'partial load failed' };
      }
      return ok();
    }
    assert(args.includes('--ignore-partitions'));
    assert(args.includes('-u') && args.includes('-v') && !args.includes('-x'));
    flash[serial] = fs.readFileSync(args[3]);
    return ok();
  };
  let state;
  try {
    if (mode === 'valid') state = await runControl({
      serial: primary, controlSerial: control, directory, picotool, execute, delay: async () => {}
    });
    else await assert.rejects(runControl({
      serial: primary, controlSerial: control, directory, picotool, execute, delay: async () => {}
    }));
    state ??= JSON.parse(fs.readFileSync(path.join(directory, 'results.json')));
    const writes = calls.filter(call => call.command === picotool && call.args[0] === 'load');
    if (['wrong-identity', 'absent-preflight', 'short-backup'].includes(mode)) {
      assert.equal(writes.length, 0, 'preflight failure must prevent flashing');
    } else {
      if (mode === 'never-recover') {
        assert.equal(writes.length, 1, 'never flash restoration without recovered identity');
        assert.match(state.errors.join('; '), /NOT verified restored/);
      } else assert(writes.some(call => !call.args.includes('-x')), 'failure must attempt full restoration');
      assert.equal(state.restored, !['restore-mismatch', 'never-recover'].includes(mode));
      assert.equal(state.control_unchanged, mode !== 'control-changed');
    }
    if (mode === 'valid') {
      assert.equal(state.status, 'passed');
      assert.equal(loads, 2);
      assert.equal(state.reports.length, 3);
      await assert.rejects(runControl({
        serial: primary, controlSerial: control, directory, picotool, execute
      }), /EEXIST/);
      const backup = state.backups[0].file;
      fs.writeFileSync(backup, Buffer.alloc(16 * 1024 * 1024));
      const before = calls.length;
      await assert.rejects(restoreControl(directory, picotool, execute), /backup hash drift/);
      assert.equal(calls.length, before, 'drifted backup must not reach a device operation');
    } else {
      assert.equal(state.status, 'failed');
      if (mode === 'collector-failure') {
        const restored = await restoreControl(directory, picotool, execute, async () => {});
        assert.equal(restored.restored, true);
        assert.equal(restored.status, 'failed', 'restoring must not promote a failed experiment');
        assert(restored.errors.length > 0);
      }
    }
  } finally {
    fs.rmSync(base, { recursive: true, force: true });
  }
}

for (const mode of ['valid', 'wrong-identity', 'absent-preflight', 'short-backup',
  'collector-failure', 'collector-timeout', 'stale', 'load-failure',
  'no-departure', 'never-recover', 'restore-mismatch', 'control-changed']) {
  test(`hardware control: ${mode}`, async () => scenario(mode));
}

test('unspecified, invalid and duplicate serials fail before filesystem/device operations', async () => {
  let calls = 0;
  const execute = () => { calls++; return ok(); };
  for (const serial of ['', 'bad', primary.toLowerCase()]) {
    await assert.rejects(runControl({ serial, controlSerial: control, directory: '/unused', execute }));
  }
  await assert.rejects(runControl({ serial: primary, controlSerial: primary, directory: '/unused', execute }));
  assert.equal(calls, 0);
});
