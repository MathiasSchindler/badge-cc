import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const script = fileURLToPath(import.meta.url);
const root = path.resolve(path.dirname(script), '../../..');
const flashBytes = 16 * 1024 * 1024;
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export const fingerprint = file => ({ file, sha256: hash(file) });

export function validateIdentity(text, serial) {
  const identities = [...text.matchAll(/chipid:\s+0x([0-9a-f]{16})/gi)];
  assert.equal(identities.length, 1, 'expected exactly one selected device');
  assert.equal(identities[0][1].toUpperCase(), serial, 'selected chip identity mismatch');
  for (const pattern of [/type:\s+RP2350\b/, /revision:\s+A4\b/, /package:\s+QFN80\b/,
    /current cpu:\s+ARM\b/, /secure boot:\s+0\b/, /boot type:\s+bootsel\b/,
    /flash size:\s+16384K\b/]) {
    assert(pattern.test(text), `unsupported device state: ${pattern}`);
  }
}

export function validateReports(text, serial, buildId) {
  const records = text.trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
  assert.equal(records.length, 3, 'expected three fresh reports');
  const nonces = new Set();
  records.forEach((record, index) => {
    assert.equal(record.verified, true, 'unverified report');
    assert.equal(record.test, 'known-report', 'unexpected test mode');
    assert.equal(record.transport, 'live-usb-control', 'unexpected report transport');
    assert.equal(record.serial, serial, 'report serial mismatch');
    assert.equal(record.build_id, buildId, 'report build mismatch');
    assert.equal(record.run, index + 1, 'report sequence mismatch');
    assert.match(record.nonce, /^[0-9a-f]{32}$/, 'invalid report nonce');
    assert(!nonces.has(record.nonce), 'repeated report nonce');
    nonces.add(record.nonce);
  });
  return records;
}

export function sourceFiles(directory) {
  return fs.readdirSync(directory, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))
    .flatMap(entry => {
      const file = path.join(directory, entry.name);
      return entry.isDirectory() ? sourceFiles(file) : /\.(c|h|S|ld)$/.test(entry.name) ? [file] : [];
    });
}

export function validSerial(serial) {
  assert.match(serial, /^[0-9A-F]{16}$/, 'serial must be 16 uppercase hexadecimal digits');
}

export function saveState(directory, state) {
  const temporary = path.join(directory, 'results.json.tmp');
  fs.writeFileSync(temporary, `${JSON.stringify(state, null, 2)}\n`, { mode: 0o600 });
  fs.renameSync(temporary, path.join(directory, 'results.json'));
}

export function commands(directory, picotool, execute) {
  let sequence = 0;
  const session = crypto.randomBytes(4).toString('hex');
  function run(label, command, args, timeout = 180000, allowAbsent = false) {
    const result = execute(command, args, {
      cwd: root, encoding: 'utf8', timeout, killSignal: 'SIGKILL', maxBuffer: 4000000
    });
    const log = path.join(directory, `${session}-${String(++sequence).padStart(3, '0')}-${label}.log`);
    fs.writeFileSync(log, `${JSON.stringify({ command, args, timeout })}\n${result.stdout ?? ''}${result.stderr ?? ''}`);
    assert.ifError(result.error);
    if (allowAbsent && result.status !== 0 &&
        /No accessible RP-series devices|device returned an error: rebooting/.test(
          `${result.stdout ?? ''}${result.stderr ?? ''}`)) return null;
    assert.equal(result.status, 0, `${label} failed; see ${log}`);
    return result.stdout;
  }
  return {
    run,
    device(label, serial, args, timeout = 180000, allowAbsent = false) {
      return run(label, picotool, [...args, '--ser', serial], timeout, allowAbsent);
    }
  };
}

export async function waitForBootsel(io, serial, label, delay, requireDeparture = false) {
  const started = performance.now();
  let departed = !requireDeparture;
  for (let attempt = 0; attempt < 90 && performance.now() - started < 45000; ++attempt) {
    const text = io.device(`${label}-${attempt}`, serial, ['info', '-a'], 5000, true);
    if (text === null) departed = true;
    else {
      validateIdentity(text, serial);
      assert(departed, 'no-collector control never observed departure from BOOTSEL');
      return { elapsed_host_ms: performance.now() - started, attempts: attempt + 1 };
    }
    await delay(500);
  }
  throw new Error(`BOOTSEL recovery timed out for ${serial}; use its physical Boot button, then --restore`);
}

export function checkBackup(directory, backup) {
  validSerial(backup.serial);
  const expected = path.join(directory, `${backup.serial}-original.bin`);
  assert.equal(backup.file, expected, 'backup must belong to this run and device');
  assert.equal(fs.realpathSync(backup.file), expected, 'backup must not be a symlink');
  assert.equal(fs.statSync(backup.file).size, flashBytes, 'incomplete flash backup');
  assert.equal(hash(backup.file), backup.sha256, 'backup hash drift');
}

export async function restore(io, directory, state, delay) {
  const backup = state.backups.find(item => item.serial === state.serial);
  assert(backup, 'missing primary backup');
  checkBackup(directory, backup);
  await waitForBootsel(io, state.serial, 'restore-recovery', delay);
  validateIdentity(io.device('restore-identity', state.serial, ['info', '-a']), state.serial);
  state.restored = false;
  saveState(directory, state);
  io.device('restore', state.serial, ['load', '-v', '-u', backup.file, '-t', 'bin',
    '-o', '0x10000000', '--ignore-partitions']);
  const readback = path.join(directory, `${state.serial}-restored.bin`);
  io.device('restore-readback', state.serial, ['save', '-r', '0x10000000', '0x11000000', readback, '-t', 'bin']);
  assert.equal(fs.statSync(readback).size, flashBytes, 'incomplete restoration readback');
  assert.equal(hash(readback), backup.sha256, 'flash restoration mismatch');
  validateIdentity(io.device('restored-identity', state.serial, ['info', '-a']), state.serial);
  state.restored = true;
  state.restoration = { ...fingerprint(readback), left_in_bootsel: true };
  saveState(directory, state);
}

export async function restoreControl(directory, picotool, execute = spawnSync,
  delay = ms => new Promise(resolve => setTimeout(resolve, ms))) {
  directory = fs.realpathSync(directory);
  const state = JSON.parse(fs.readFileSync(path.join(directory, 'results.json'), 'utf8'));
  assert.equal(state.schema, 'nametag-m0-control-v1');
  validSerial(state.serial);
  const io = commands(directory, picotool, execute);
  try { await restore(io, directory, state, delay); }
  catch (error) {
    state.status = 'failed';
    state.errors.push(`Manual restoration failed: ${error.message}`);
    saveState(directory, state);
    throw error;
  }
  return state;
}

export async function runControl({ serial, controlSerial, directory, picotool,
  execute = spawnSync, delay = ms => new Promise(resolve => setTimeout(resolve, ms)) }) {
  validSerial(serial);
  validSerial(controlSerial);
  assert.notEqual(serial, controlSerial, 'primary and control must be distinct');
  directory = path.resolve(directory);
  const parent = fs.realpathSync(path.dirname(directory));
  const buildRoot = fs.realpathSync(path.join(root, 'build'));
  assert(parent === buildRoot || parent.startsWith(`${buildRoot}${path.sep}`),
    'evidence must be inside the ignored build directory');
  assert.equal(parent, path.dirname(directory), 'evidence parent must not be a symlink');
  fs.mkdirSync(directory, { mode: 0o700 });
  const io = commands(directory, picotool, execute);
  const state = {
    schema: 'nametag-m0-control-v1', started_at: new Date().toISOString(),
    serial, control_serial: controlSerial, status: 'preflight', backups: [],
    flash_attempted: false, restored: false, control_unchanged: false, errors: []
  };
  saveState(directory, state);
  try {
    state.inputs = [script, path.join(root, 'Makefile'), picotool, process.execPath,
      ...['arch/rp2350', 'shared', 'tools', 'platform/badger', 'usb', 'radio']
        .flatMap(dir => sourceFiles(path.join(root, 'src', dir)))].map(fingerprint);
    saveState(directory, state);
    const build = path.join(directory, 'build');
    io.run('build', 'make', [`BUILD_DIR=${build}`, 'usb-live-report'], 180000);
    const image = path.join(build, 'universe26/uf2/usb_live_report.uf2');
    const collector = path.join(build, 'host-tools/usb_live_host');
    const header = fs.readFileSync(path.join(build, 'universe26/firmware/usb_live_build.h'), 'utf8');
    const match = /#define USB_REPORT_BUILD_ID 0x([0-9a-f]{8})u/.exec(header);
    assert(match, 'missing live-report build identity');
    state.build_id = match[1];
    state.artifacts = [image, collector, path.join(build, 'universe26/firmware/usb_live_report.elf'),
      path.join(build, 'host-tools/bin2uf2')].map(fingerprint);
    saveState(directory, state);
    for (const selected of [serial, controlSerial]) {
      validateIdentity(io.device(`identity-${selected}`, selected, ['info', '-a']), selected);
      const file = path.join(directory, `${selected}-original.bin`);
      io.device(`backup-${selected}`, selected, ['save', '-r', '0x10000000', '0x11000000', file, '-t', 'bin']);
      assert.equal(fs.statSync(file).size, flashBytes, 'incomplete flash backup');
      const backup = { serial: selected, ...fingerprint(file) };
      state.backups.push(backup);
      saveState(directory, state);
      io.device(`backup-verify-${selected}`, selected, ['verify', file, '-t', 'bin', '-o', '0x10000000']);
    }
    state.status = 'running';
    saveState(directory, state);
    for (const mode of ['reports', 'no-collector']) {
      for (const backup of state.backups) checkBackup(directory, backup);
      assert.equal(hash(image), state.artifacts[0].sha256, 'test image drift');
      assert.equal(hash(collector), state.artifacts[1].sha256, 'collector drift');
      validateIdentity(io.device(`${mode}-identity`, serial, ['info', '-a']), serial);
      state.flash_attempted = true;
      saveState(directory, state);
      io.device(`${mode}-load`, serial, ['load', '-v', '-x', image]);
      if (mode === 'reports') {
        const text = io.run('collector', collector, [serial, '3'], 35000);
        state.reports = validateReports(text, serial, state.build_id);
        fs.writeFileSync(path.join(directory, 'reports.jsonl'), text);
      }
      state[mode === 'reports' ? 'report_recovery' : 'no_collector_recovery'] =
        await waitForBootsel(io, serial, `${mode}-recovery`, delay, mode === 'no-collector');
      saveState(directory, state);
    }
  } catch (error) {
    state.errors.push(error.message);
    saveState(directory, state);
  } finally {
    if (state.flash_attempted) {
      try { await restore(io, directory, state, delay); }
      catch (error) {
        state.errors.push(`Original flash NOT verified restored: ${error.message}`);
        saveState(directory, state);
      }
    }
    const controlBackup = state.backups.find(item => item.serial === controlSerial);
    if (controlBackup) {
      try {
        checkBackup(directory, controlBackup);
        validateIdentity(io.device('control-final-identity', controlSerial, ['info', '-a']), controlSerial);
        const file = path.join(directory, `${controlSerial}-control-readback.bin`);
        io.device('control-readback', controlSerial, ['save', '-r', '0x10000000', '0x11000000', file, '-t', 'bin']);
        assert.equal(fs.statSync(file).size, flashBytes, 'incomplete control readback');
        assert.equal(hash(file), controlBackup.sha256, 'control flash changed');
        state.control_unchanged = true;
        state.control_readback = fingerprint(file);
      } catch (error) { state.errors.push(`Control verification failed: ${error.message}`); }
    }
    state.status = state.errors.length === 0 && state.restored && state.control_unchanged &&
      state.reports?.length === 3 && state.no_collector_recovery ? 'passed' : 'failed';
    state.finished_at = new Date().toISOString();
    saveState(directory, state);
  }
  assert.equal(state.status, 'passed', `M0 control failed: ${state.errors.join('; ')}; evidence: ${directory}`);
  return state;
}

if (process.argv[1] && path.resolve(process.argv[1]) === script) {
  try {
    const args = process.argv.slice(2);
    const picotool = fs.realpathSync(process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool'));
    if (args.length === 2 && args[0] === '--restore') {
      const state = await restoreControl(args[1], picotool);
      console.log(`Original flash restored for ${state.serial}; badge left in BOOTSEL. This does not pass a failed experiment.`);
    } else {
      assert.equal(args.length, 3,
        'Usage: node src/host/tools/nametag_control.mjs PRIMARY_SERIAL CONTROL_SERIAL NEW_BUILD_DIRECTORY | --restore RUN_DIRECTORY');
      const state = await runControl({ serial: args[0], controlSerial: args[1], directory: args[2], picotool });
      console.log(`M0 control ${state.status}: three nonce-bound reports, no-collector recovery, original flash restored, control unchanged.`);
      console.log(`Private evidence: ${path.resolve(args[2])}`);
    }
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
