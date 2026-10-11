import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { root, sha, request, decode, reportBytes, requestAddress, responseAddress } from './datasheet-experiments.mjs';

const sleep = milliseconds => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, milliseconds);

export function capture(buildDirectory, output, serials) {
  assert.equal(serials.length, 2, 'two badges required');
  assert(serials.every(x => /^[0-9A-F]{16}$/.test(x)) && new Set(serials).size === 2, 'distinct serials');
  buildDirectory = path.resolve(buildDirectory); output = path.resolve(output);
  const manifest = JSON.parse(fs.readFileSync(path.join(buildDirectory, 'manifest.json')));
  assert.equal(manifest.schema, 1);
  assert.equal(manifest.reportBytes, reportBytes, 'different experiment schema');
  for (const source of manifest.provenance)
    assert.equal(sha(fs.readFileSync(path.join(root, source.file))), source.sha256, `source drift ${source.file}`);
  for (const image of manifest.images) {
    assert.equal(sha(fs.readFileSync(image.uf2)), image.sha256, 'image drift');
    assert.equal(sha(fs.readFileSync(image.elf)), image.elf_sha256, 'ELF drift');
    assert.equal(sha(fs.readFileSync(path.join(buildDirectory, image.audit))), image.audit_sha256, 'audit drift');
    assert.equal(image.qemu_correctness, 'passed', 'missing correctness oracle');
  }
  fs.mkdirSync(output);
  const picotool = process.env.PICOTOOL || path.join(root, 'build/picotool-usb/picotool');
  let commandIndex = 0;
  const command = (args, label, timeout = 180000) => {
    const result = spawnSync(picotool, args, { encoding: 'utf8', timeout });
    const log = `${result.stdout || ''}${result.stderr || ''}${result.error?.message || ''}`;
    fs.writeFileSync(path.join(output, `${String(commandIndex++).padStart(3, '0')}-${label}.log`), log);
    if (result.error || result.status !== 0) throw new Error(`${label}: ${log.trim()}`);
    return result.stdout;
  };
  const wait = serial => {
    const deadline = Date.now() + 120000;
    while (Date.now() < deadline) {
      const poll = spawnSync(picotool, ['info', '-d', '--ser', serial], { encoding: 'utf8', timeout: 2000 });
      fs.appendFileSync(path.join(output, `${serial}-recovery.log`),
        `${poll.stdout || ''}${poll.stderr || ''}${poll.error?.message || ''}\n`);
      if (!poll.error && poll.status === 0 && poll.stdout.includes('RP2350')) return;
      sleep(250);
    }
    throw new Error(`${serial}: BOOTSEL recovery timeout; use physical Boot before restoring its snapshot`);
  };
  const backups = [], touched = new Set(), rounds = [], restoration = [];
  let failure;
  try {
    for (const serial of serials) {
      const identity = command(['info', '-d', '--ser', serial], `${serial}-identity`);
      assert(identity.includes('RP2350') && /current cpu:\s+ARM/.test(identity), 'RP2350 ARM required');
      assert(/secure boot:\s+0/.test(identity), 'unsigned experiment requires unsecured boot');
      const size = identity.match(/flash size:\s+(\d+)K/);
      assert(size, 'flash size missing');
      const bytes = Number(size[1]) * 1024;
      assert.equal(bytes, 16 * 1024 * 1024, 'persistent calibration mailbox requires 16 MiB flash');
      const file = path.join(output, `${serial}-original.bin`);
      command(['save', '-a', '-v', file, '-t', 'bin', '--ser', serial], `${serial}-snapshot`);
      assert.equal(fs.statSync(file).size, bytes, 'incomplete flash snapshot');
      backups.push({ serial, bytes, file, sha256: sha(fs.readFileSync(file)) });
    }
    const plan = [
      ['control', ['llvm', 'llvm']], ['control', ['llvm', 'llvm']],
      ['comparison-1', ['llvm', 'owned']], ['comparison-1', ['llvm', 'owned']],
      ['comparison-2', ['llvm', 'owned']], ['comparison-2', ['llvm', 'owned']],
    ];
    for (const [roundIndex, [kind, compilers]] of plan.entries()) {
      const assignments = compilers.map((compiler, i) => ({
        ...manifest.images.find(x => x.compiler === compiler),
        serial: serials[roundIndex % 2 === 0 ? i : 1 - i],
      }));
      const nonce = crypto.randomBytes(16), prefix = `round-${roundIndex}`;
      const requestFile = path.join(output, `${prefix}-request.bin`);
      const clear = path.join(output, 'clear.bin');
      if (!fs.existsSync(clear)) fs.writeFileSync(clear, Buffer.alloc(reportBytes));
      fs.writeFileSync(requestFile, request(manifest.build, nonce));
      for (const assignment of assignments) {
        touched.add(assignment.serial);
        command(['load', '-v', assignment.uf2, '--ser', assignment.serial], `${prefix}-${assignment.serial}-image`);
        command(['load', '-v', clear, '-t', 'bin', '-o', `0x${responseAddress.toString(16)}`, '--ser', assignment.serial],
          `${prefix}-${assignment.serial}-clear`);
        command(['load', '-v', requestFile, '-t', 'bin', '-o', `0x${requestAddress.toString(16)}`, '--ser', assignment.serial],
          `${prefix}-${assignment.serial}-request`);
      }
      for (const assignment of assignments)
        command(['reboot', '--ser', assignment.serial], `${prefix}-${assignment.serial}-start`);
      const captures = [];
      for (const assignment of assignments) {
        wait(assignment.serial);
        const file = path.join(output, `${prefix}-${assignment.serial}.bin`);
        command(['save', '-r', `0x${responseAddress.toString(16)}`, `0x${(responseAddress + reportBytes).toString(16)}`,
          file, '-t', 'bin', '--ser', assignment.serial], `${prefix}-${assignment.serial}-read`);
        const bytes = fs.readFileSync(file);
        const result = decode(bytes, manifest.build, nonce);
        captures.push({ compiler: assignment.compiler, serial: assignment.serial, image_sha256: assignment.sha256,
          file, sha256: sha(bytes), ...result });
      }
      rounds.push({ index: roundIndex, kind, nonce: nonce.toString('hex'), captures });
      fs.writeFileSync(path.join(output, `${prefix}.json`), JSON.stringify(rounds.at(-1), null, 2)+'\n');
    }
  } catch (error) {
    failure = error.message;
    fs.writeFileSync(path.join(output, 'failure.txt'), `${failure}\n`);
  } finally {
    for (const backup of backups.filter(b => touched.has(b.serial))) {
      try {
        wait(backup.serial);
        assert.equal(sha(fs.readFileSync(backup.file)), backup.sha256, 'snapshot changed');
        command(['load', '-u', '-v', backup.file, '-t', 'bin', '-o', '0x10000000', '--ser', backup.serial],
          `${backup.serial}-restore`);
        const verified = path.join(output, `${backup.serial}-restored.bin`);
        command(['save', '-a', '-v', verified, '-t', 'bin', '--ser', backup.serial], `${backup.serial}-verify-restore`);
        assert.equal(sha(fs.readFileSync(verified)), backup.sha256, 'restored flash differs');
        restoration.push({ serial: backup.serial, verified: true, sha256: backup.sha256, left_in_bootsel: true });
      } catch (error) {
        restoration.push({ serial: backup.serial, verified: false, error: error.message });
        failure = `${failure ? failure+'\n' : ''}Restoration failed: ${backup.serial}: ${error.message}`;
      }
    }
  }
  const result = { schema: 1, status: failure ? 'failed' : 'captured', manifest_file: path.join(buildDirectory, 'manifest.json'),
    manifest_sha256: sha(fs.readFileSync(path.join(buildDirectory, 'manifest.json'))), build: manifest.build,
    picotool_sha256: sha(fs.readFileSync(picotool)), serials, backups, restoration, rounds,
    ...(failure ? { error: failure } : {}) };
  fs.writeFileSync(path.join(output, 'capture.json'), JSON.stringify(result, null, 2)+'\n');
  if (failure) throw new Error(`${failure}\nCapture retained at ${output}`);
  assert.equal(restoration.length, 2, 'both restorations required');
  return { status: result.status, rounds: rounds.length, capture: path.join(output, 'capture.json'), restoration };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    assert.equal(process.argv.length, 6, 'usage: capture-datasheet.mjs BUILD_DIRECTORY NEW_DIRECTORY SERIAL SERIAL');
    console.log(JSON.stringify(capture(process.argv[2], process.argv[3], process.argv.slice(4)), null, 2));
  } catch (error) {
    console.error(`datasheet capture: ${error.message}`); process.exitCode = 1;
  }
}
