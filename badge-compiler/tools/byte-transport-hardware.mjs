import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { verifyByteTransport } from './byte-transport-evidence.mjs';
import { verifyCommandWrapper } from './command-wrapper-evidence.mjs';
import { verifyConstantSelection } from './constant-selection-evidence.mjs';
import { decodeElf } from './boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const campaigns = {
  RS02: { directory: 'rs02-hardware', host: 'rs02-byte-transport', bytes: 748, verify: verifyByteTransport },
  RS03: { directory: 'rs03-reuse-hardware', host: 'rs03-call-preservation', bytes: 724, verify: verifyCommandWrapper },
  RS04: { directory: 'rs04-hardware', host: 'rs04-byte-streams', bytes: 644,
    verify: verifyConstantSelection, rounds: 1, boots: 1, roles: ['llvm', 'owned'] },
  RS05: { directory: 'rs05-hardware', host: 'rs05-pixel-fill', bytes: 636,
    verify: verifyConstantSelection, rounds: 1, boots: 1, roles: ['llvm', 'owned'] },
  RS06: { directory: 'rs06-hardware', host: 'rs06-counted-delay', bytes: 624,
    verify: verifyConstantSelection, rounds: 1, boots: 1, roles: ['llvm', 'owned'] },
};
let campaign = campaigns.RS02;
let directory = path.join(root, 'badge-compiler/build', campaign.directory);
let manifest = path.join(directory, 'results.json');
const serials = ['08DA26F86E57F5EA', '9BBF88DD17D66E7D'];
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const save = report => fs.writeFileSync(manifest, JSON.stringify(report, null, 2) + '\n');
function command(args, label, executable = 'build/picotool-usb/picotool') {
  const result = spawnSync(path.join(root, executable), args,
    { cwd: root, encoding: 'utf8', timeout: 180000 });
  fs.writeFileSync(path.join(directory, `${label}.log`), `${result.stdout ?? ''}${result.stderr ?? ''}`);
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${label}: ${result.stderr}`);
  return result.stdout;
}
function identity(serial, label) {
  const text = command(['info', '-d', '--ser', serial], label);
  assert(/RP2350/.test(text) && /current cpu:\s+ARM/.test(text) &&
    /secure boot:\s+0/.test(text) && /flash size:\s+16384K/.test(text));
}
function deploy(report, round) {
  assert.equal(report.backups.length, 2);
  const configurations = serials.map((serial, index) => ({
    serial, role: (campaign.roles ?? ['owned', 'llvm'])[(index + round) % 2],
  }));
  report.rounds.push({ round, configurations, observations: null });
  save(report);
  for (const config of configurations) {
    identity(config.serial, `round-${round}-${config.serial}-identity`);
    const image = report.images.find(image => image.role === config.role);
    assert.equal(hash(image.file), image.sha256);
    command(['load', '-v', image.file, '--ser', config.serial],
      `round-${round}-${config.serial}-load`);
    config.verified = true;
    config.sha256 = image.sha256;
    save(report);
  }
  for (const config of configurations)
    command(['reboot', '--ser', config.serial], `round-${round}-${config.serial}-run`);
  report.status = `awaiting-round-${round}-observation`;
  save(report);
}

export function verifyHardware(file) {
  const report = JSON.parse(fs.readFileSync(file));
  assert.equal(report.schema, 1);
  assert(['RS02', 'RS03', 'RS04', 'RS05', 'RS06'].includes(report.milestone));
  const config = campaigns[report.milestone];
  assert.equal(report.host_report.file,
    path.join(root, 'badge-compiler/build', config.host, 'results.json'));
  assert.equal(hash(report.host_report.file), report.host_report.sha256);
  const host = config.verify(report.host_report.file);
  if (report.milestone === 'RS03') {
    assert.equal(host.call_preservation.target_met, true);
    assert.equal(host.command_wrapper.bytes, 20);
  }
  if (report.milestone === 'RS04') {
    assert.equal(host.table_interpreter.target_met, true);
    assert.equal(host.table_interpreter.bytes, 40);
    assert(host.final_validation);
  }
  if (report.milestone === 'RS05') {
    assert.equal(host.pixel_fill.target_met, true);
    assert.equal(host.pixel_fill.bytes, 34);
    assert(host.final_validation);
  }
  if (report.milestone === 'RS06') {
    assert.equal(host.initialization_delay.target_met, true);
    assert.equal(host.initialization_delay.bytes, 20);
    assert.equal(host.minimum_delay.minimum_cycles, 24000000);
    assert(host.final_validation);
  }
  assert.equal(report.electrical_timing_measured, false);
  assert.equal(report.default_changed, false);
  assert.deepEqual(report.backups.map(item => item.serial), serials);
  for (const item of report.backups) {
    assert.equal(fs.statSync(item.file).size, 16777216);
    assert.equal(hash(item.file), item.sha256);
  }
  assert.deepEqual(report.images.map(item => [item.role, item.bytes]), [['owned', config.bytes], ['llvm', 568]]);
  for (const item of report.images) {
    assert.equal(hash(item.file), item.sha256);
    assert.equal(hash(item.elf), item.elf_sha256);
    assert.equal(hash(item.binary), item.binary_sha256);
    assert.equal(fs.statSync(item.binary).size, item.bytes);
    const elfBytes = fs.readFileSync(item.elf);
    const elf = decodeElf(elfBytes);
    if (report.milestone === 'RS06') {
      const delay = elf.symbols.find(s => s.name === 'delay_cycles' && s.type === 2);
      assert.equal(delay?.size, 20);
      const section = elf.sections[delay.section];
      const offset = (delay.value & ~1) - section.address;
      const body = section.data.subarray(offset, offset + delay.size);
      assert.equal([...Array(body.length / 2)].filter((_, n) =>
        body.readUInt16LE(n * 2) === 0xbf00).length, 6);
    }
    const binary = fs.readFileSync(item.binary);
    for (const segment of elf.segments.filter(s => s.fileSize > 0)) {
      const offset = segment.load - 0x10000000;
      assert(offset >= 0 && offset + segment.fileSize <= binary.length);
      assert.deepEqual(binary.subarray(offset, offset + segment.fileSize),
        elfBytes.subarray(segment.offset, segment.offset + segment.fileSize));
    }
    const helper = elf.symbols.find(s => s.name === 'write_byte' && s.type === 2);
    assert.equal(helper.size, 32);
    const section = elf.sections[helper.section];
    const offset = (helper.value & ~1) - section.address;
    const code = section.data.subarray(offset, offset + helper.size);
    const tail = item.role === 'owned' ? 18 : 14;
    assert.deepEqual([...Array(7)].map((_, n) => code.readUInt16LE(tail + n * 2)),
      [0xbf00, 0xbf00, 0xbf00, item.role === 'owned' ? 0x6188 : 0x6008,
        0xbf00, 0xbf00, 0x4770]);
    const uf2 = fs.readFileSync(item.file);
    assert.equal(uf2.length % 512, 0);
    const reached = Buffer.alloc(binary.length);
    for (let at = 0; at < uf2.length; at += 512) {
      assert.equal(uf2.readUInt32LE(at), 0x0a324655);
      assert.equal(uf2.readUInt32LE(at + 4), 0x9e5d5157);
      assert.equal(uf2.readUInt32LE(at + 508), 0x0ab16f30);
      const address = uf2.readUInt32LE(at + 12);
      const length = uf2.readUInt32LE(at + 16);
      assert(length > 0 && length <= 476);
      if (uf2.readUInt32LE(at + 8) & 1) continue;
      const start = address - 0x10000000;
      assert(start >= 0);
      const end = Math.min(start + length, binary.length);
      if (start >= binary.length) continue;
      assert.deepEqual(uf2.subarray(at + 32, at + 32 + end - start),
        binary.subarray(start, end));
      reached.fill(1, start, end);
    }
    assert(reached.every(byte => byte === 1), 'UF2 did not cover loaded image');
  }
  assert.equal(report.images[0].elf_sha256,
    hash(path.join(root, 'badge-compiler/build', config.host, 'red_square.elf')));
  assert.equal(report.rounds.length, config.rounds ?? 2);
  for (const [round, item] of report.rounds.entries()) {
    assert.equal(item.round, round);
    assert.deepEqual(item.configurations.map(c => c.serial), serials);
    assert.deepEqual(item.configurations.map(c => c.role),
      serials.map((_, index) => (config.roles ?? ['owned', 'llvm'])[(index + round) % 2]));
    for (const config of item.configurations) {
      assert.equal(config.verified, true);
      assert.equal(config.sha256, report.images.find(i => i.role === config.role).sha256);
    }
    assert.equal(item.observations.passed, true);
    assert.equal(item.observations.cold_boots_per_badge, config.boots ?? 3);
    assert.equal(item.observations.source, 'user-visual-confirmation');
  }
  assert.equal(report.status, 'functional-hardware-qualified-electrical-unmeasured');
  assert.equal(report.acceptance.source, 'user-explicit-acceptance');
  assert.equal(report.acceptance.scope, 'functional-hardware-only');
  assert.equal(report.acceptance.final_state, config.rounds === 1 ?
    'keep-single-round-comparison-pair' : 'keep-role-swapped-comparison-pair');
  return report;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const args = process.argv.slice(2);
    if (['--rs03', '--rs04', '--rs05', '--rs06'].includes(args[0])) {
      campaign = campaigns[args.shift().slice(2).toUpperCase()];
      directory = path.join(root, 'badge-compiler/build', campaign.directory);
      manifest = path.join(directory, 'results.json');
    }
    const [mode, argument, ...extra] = args;
    assert.equal(extra.length, 0);
    if (mode === '--prepare') {
      assert.equal(argument, undefined);
      const host = path.join(root, 'badge-compiler/build', campaign.host, 'results.json');
      const qualified = campaign.verify(host);
      if (campaign === campaigns.RS03) assert.equal(qualified.call_preservation.target_met, true);
      if (campaign === campaigns.RS04) {
        assert.equal(qualified.table_interpreter.target_met, true);
        assert(qualified.final_validation);
      }
      if (campaign === campaigns.RS05) {
        assert.equal(qualified.pixel_fill.target_met, true);
        assert(qualified.final_validation);
      }
      if (campaign === campaigns.RS06) {
        assert.equal(qualified.initialization_delay.target_met, true);
        assert(qualified.final_validation);
      }
      assert(!fs.existsSync(directory), 'fresh hardware campaign directory required');
      fs.mkdirSync(directory);
      const report = { schema: 1, milestone: Object.keys(campaigns).find(key => campaigns[key] === campaign),
        status: 'preserving',
        host_report: { file: host, sha256: hash(host) }, backups: [], images: [], rounds: [],
        electrical_timing_measured: false, default_changed: false, restoration: [] };
      save(report);
      for (const serial of serials) {
        identity(serial, `${serial}-identity`);
        const file = path.join(directory, `${serial}-original.bin`);
        command(['save', '-a', '-v', file, '-t', 'bin', '--ser', serial], `${serial}-backup`);
        assert.equal(fs.statSync(file).size, 16777216);
        report.backups.push({ serial, file, sha256: hash(file) });
        save(report);
      }
      for (const [role, bytes, source] of [
        ['owned', campaign.bytes, `badge-compiler/build/${campaign.host}/red_square.elf`],
        ['llvm', 568, 'badge-compiler/build/po18-red-square/llvm.elf'],
      ]) {
        const elf = path.join(directory, `${role}.elf`);
        fs.copyFileSync(path.join(root, source), elf);
        const binary = path.join(directory, `${role}.bin`);
        command(['-O', 'binary', elf, binary], `${role}-binary`, 'badge-compiler/build/elf2bin');
        assert.equal(fs.statSync(binary).size, bytes);
        const file = path.join(directory, `${role}.uf2`);
        command(['uf2', 'convert', elf, file], `${role}-uf2`);
        report.images.push({ role, bytes, file, sha256: hash(file), elf, elf_sha256: hash(elf),
          binary, binary_sha256: hash(binary) });
        save(report);
      }
      deploy(report, 0);
    } else if (mode === '--verify') {
      console.log(JSON.stringify(verifyHardware(argument ?? manifest), null, 2));
    } else {
      const report = JSON.parse(fs.readFileSync(manifest));
      if (mode === '--observe') {
        assert.equal(argument, campaign.boots === 1 ? 'passed-one-cold-boot' : 'passed-three-cold-boots');
        const round = report.rounds.at(-1);
        assert.equal(report.status, `awaiting-round-${round.round}-observation`);
        round.observations = { passed: true, cold_boots_per_badge: campaign.boots ?? 3,
          source: 'user-visual-confirmation', recorded_at: new Date().toISOString() };
        report.status = round.round === 0 && campaign.rounds !== 1 ? 'ready-for-swap' :
          'functional-hardware-qualified-electrical-unmeasured';
        save(report);
      } else if (mode === '--swap') {
        assert.equal(report.status, 'ready-for-swap');
        assert.equal(report.rounds.length, 1);
        deploy(report, 1);
      } else if (mode === '--restore') {
        for (const backup of report.backups) {
          identity(backup.serial, `${backup.serial}-restore-identity`);
          assert.equal(hash(backup.file), backup.sha256);
          command(['load', '-u', '-v', backup.file, '-t', 'bin', '-o', '0x10000000',
            '--ser', backup.serial], `${backup.serial}-restore`);
          const file = path.join(directory, `${backup.serial}-restored.bin`);
          command(['save', '-a', '-v', file, '-t', 'bin', '--ser', backup.serial],
            `${backup.serial}-restored-readback`);
          assert.equal(hash(file), backup.sha256);
          report.restoration.push({ serial: backup.serial, file, sha256: hash(file), verified: true });
          save(report);
        }
      } else if (mode === '--accept-functional') {
        assert.equal(argument, undefined);
        assert.equal(report.status, 'functional-hardware-qualified-electrical-unmeasured');
        report.acceptance = { source: 'user-explicit-acceptance', scope: 'functional-hardware-only',
          final_state: campaign.rounds === 1 ? 'keep-single-round-comparison-pair' :
            'keep-role-swapped-comparison-pair', recorded_at: new Date().toISOString(),
          observations: campaign.rounds === 1 ? ['User confirmed correct display and one cold boot per badge'] :
            ['Round 1: user confirmed correct display and three cold boots per badge',
            'Round 2: user confirmed correct display and three cold boots per badge'] };
        save(report);
      } else {
        throw new Error('Expected [--rs03|--rs04|--rs05|--rs06] --prepare, --observe passed-three-cold-boots|passed-one-cold-boot, --swap, --accept-functional, --restore or --verify [REPORT]');
      }
    }
  } catch (error) {
    console.error(error.message);
    console.error('If flash was changed, preserved backups remain in the campaign directory; use --restore with badges in BOOTSEL.');
    process.exitCode = 1;
  }
}
