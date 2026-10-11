import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { preserveFlash, verifyApplicationCampaign } from '../tools/application-benchmark.mjs';
import { compareTimingReports } from '../tools/compiler-timing-compare.mjs';
import { assessTiming } from '../tools/qualify-datasheet.mjs';
import { applications, applicationSuiteId, crc32, request, decodeApplication,
  decodeTiming, compare, compareTiming } from '../tools/compiler-test-protocol.mjs';

const build = 0x12345678;
const nonce = Buffer.from('0102030405060708090a0b0c0d0e0f10', 'hex');
function wire() {
  const data = Buffer.alloc(16 * 64);
  for (let page = 0; page < 16; ++page) {
    const record = data.subarray(page * 64, (page + 1) * 64);
    record.writeUInt32LE(0x31544342);
    record[4] = 1; record[6] = 64;
    record.writeUInt32LE(build, 8);
    record.writeUInt32LE(applicationSuiteId + page, 12);
    nonce.copy(record, 16);
    const values = page === 0 ? [1, 6, 0, nonce.readUInt32LE(0), 7, 64, 4] :
      page <= 6 ? [applications[page - 1].cases, 123, 200, 1, 1, 1, 1] :
      page === 7 ? [0, 65536, 0, 65536, 3, 24, 0] :
      page === 8 ? [0, 0, 0, 0, 0, 0, 1] :
      page === 9 ? [1, 1, 1, 1, 1, 1, 1] : [1000, 1001, 999, 1002, 998, 1003, 997];
    values.forEach((value, i) => record.writeUInt32LE(value, 32 + i * 4));
    record.writeUInt32LE(crc32(record.subarray(0, 60)), 60);
  }
  return data;
}
function mutate(data, page, offset, value) {
  const record = data.subarray(page * 64, (page + 1) * 64);
  record.writeUInt32LE(value, offset);
  record.writeUInt32LE(crc32(record.subarray(0, 60)), 60);
  return data;
}
test('application suite is isolated and keeps raw samples and actual batch sizes', () => {
  assert.equal(request(build, nonce, applicationSuiteId).readUInt32LE(12), applicationSuiteId);
  const decoded = decodeApplication(wire(), build, nonce);
  assert.equal(decoded.version, 4);
  assert.deepEqual(decoded.kernels, []);
  assert.deepEqual(decoded.modules.map(x => x.timing.repetitions), applications.map(x => x.cases));
  assert(compare(decoded.modules, decoded.modules, applications).every(x => x.matched));
  assert(compareTiming(decoded, decoded).every(x => x.ownedToLlvm === 1));
  assert.throws(() => decodeTiming(wire(), build, nonce), /order/);
  assert.throws(() => request(build, nonce, 0), /unknown/);
});
test('application reports reject corrupt, stale, incomplete, unguarded, unresolved and failed work', () => {
  const decode = data => decodeApplication(data, build, nonce);
  assert.throws(() => decode(wire().subarray(0, -1)), /truncated/);
  assert.throws(() => decode(Buffer.concat([wire(), Buffer.alloc(64)])), /excess/);
  assert.throws(() => decodeApplication(wire(), build + 1, nonce), /build/);
  assert.throws(() => decodeApplication(wire(), build, Buffer.alloc(16, 2)), /nonce/);
  const corrupt = wire(); corrupt[100] ^= 1;
  assert.throws(() => decode(corrupt), /CRC/);
  assert.throws(() => decode(mutate(wire(), 1, 32, 15)), /incomplete/);
  assert.throws(() => decode(mutate(wire(), 1, 52, 0)), /guards/);
  assert.throws(() => decode(mutate(wire(), 0, 56, 3)), /version/);
  assert.throws(() => decode(mutate(wire(), 0, 32, 2)), /failure/);
  assert.throws(() => decode(mutate(wire(), 10, 32, 99)), /under-resolved/);
  assert.throws(() => decode(mutate(wire(), 9, 32, 11)), /overhead/);
  const badCrc = wire(); badCrc[7 * 64 + 60] ^= 1;
  assert.throws(() => decode(badCrc), /CRC/);
  const a = decode(wire()), b = decode(mutate(wire(), 8, 56, 2));
  assert.throws(() => compareTiming(a, b), /clock/);
  const different = decode(mutate(wire(), 1, 36, 124));
  assert.equal(compare(a.modules, different.modules, applications)[0].matched, false);
});

for (const scenario of ['body-failure', 'snapshot-failure', 'restore-failure', 'readback-mismatch']) {
  test(`full-flash safety: ${scenario}`, t => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'application-preservation-'));
    const files = [];
    t.after(() => {
      for (const file of files) if (fs.existsSync(file)) fs.unlinkSync(file);
      fs.rmdirSync(directory);
    });
    const serials = ['9BBF88DD17D66E7D', '08DA26F86E57F5EA'];
    const report = { backups: [], restoration: [], errors: [] };
    const calls = [];
    let entered = false;
    const command = (args, label) => {
      calls.push(label);
      const serial = args[args.indexOf('--ser') + 1];
      if (args[0] === 'info') return 'RP2350\ncurrent cpu: ARM\nsecure boot: 0\nflash size: 16384K\n';
      if (scenario === 'snapshot-failure' && label === `${serials[1]}-snapshot`)
        throw new Error('snapshot failed');
      if (scenario === 'restore-failure' && label === `${serials[0]}-restore`)
        throw new Error('restore failed');
      if (args[0] === 'save') {
        const file = args[3]; files.push(file);
        const fill = serial === serials[0] ? 0x12 : 0x34;
        fs.writeFileSync(file, Buffer.alloc(16777216,
          scenario === 'readback-mismatch' && label.endsWith('-verify') ? fill + 1 : fill));
      }
      return '';
    };
    assert.throws(() => preserveFlash(serials, directory, command, report, () => {}, () => {
      entered = true;
      assert.equal(report.backups.length, 2, 'body entered before all snapshots');
      if (scenario === 'body-failure') throw new Error('collection failed');
    }), scenario === 'body-failure' ? /collection failed/ : scenario === 'snapshot-failure' ?
      /snapshot failed/ : scenario === 'restore-failure' ? /restore failed/ : /restored flash differs/);
    assert.equal(entered, scenario !== 'snapshot-failure');
    assert(calls.includes(`${serials[0]}-restore`));
    if (scenario !== 'snapshot-failure') assert(calls.includes(`${serials[1]}-restore`));
    if (scenario === 'body-failure') assert(report.restoration.every(item => item.verified));
    if (scenario === 'restore-failure') assert.equal(report.restoration[1].verified, true);
    if (scenario === 'readback-mismatch') assert(report.restoration.every(item => !item.verified));
  });
}

      test('campaign verification recomputes raw outputs, ratios, nonces, physical roles and requests', t => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'application-campaign-'));
        t.after(() => {
          for (const file of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, file));
          fs.rmdirSync(directory);
        });
        const image = path.join(directory, 'firmware.uf2');
        fs.writeFileSync(image, 'test transport image');
        const sha256 = crypto.createHash('sha256').update(fs.readFileSync(image)).digest('hex');
        const serials = ['9BBF88DD17D66E7D', '08DA26F86E57F5EA'];
        const rounds = [0, 1].map(index => {
          const roundNonce = Buffer.from(nonce); roundNonce[15] += index;
          const data = wire();
          for (let page = 0; page < 16; ++page) {
            const record = data.subarray(page * 64, (page + 1) * 64);
            roundNonce.copy(record, 16);
            record.writeUInt32LE(crc32(record.subarray(0, 60)), 60);
          }
          const decoded = decodeApplication(data, build, roundNonce);
          const timings = compareTiming(decoded, decoded);
          const modules = compare(decoded.modules, decoded.modules, applications);
          modules.forEach((row, i) => { row.timing = timings[i]; });
          fs.writeFileSync(path.join(directory, `round${index + 1}-request.bin`),
            request(build, roundNonce, applicationSuiteId));
          const configurations = ['llvm', 'owned'].map((name, role) => {
            fs.writeFileSync(path.join(directory, `round${index + 1}-${name}.bin`), data);
            return { name, serial: serials[index ? 1 - role : role], image, sha256, build };
          });
          return { matched: true, timingVersion: 4, seed: roundNonce.readUInt32LE(0),
            nonce: roundNonce.toString('hex'), configurations, modules, kernels: [],
            clocks: [decoded.clock, decoded.clock], overhead: [decoded.overhead, decoded.overhead] };
        });
        const result = { schema: 3, matched: true, rounds, toolchain: {
          owned: { 'badge-codegen': 'a'.repeat(64) },
          llvm: 'clang version 22.1.8', ownedPasses: 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division',
          ownedFrameAddressing: 'direct', ownedRegisterResidency: 'global-compact',
          ownedScalarSelection: 'thumb-layout', ownedMemorySelection: 'word',
        }, modules: rounds[0].modules.map((row, i) => ({ ...row, timing: {
          unit: 'timer0-raw-ticks', runs: rounds.map(round => round.modules[i].timing),
          ownedToLlvm: 1, percentSlower: 0,
        } })) };
        const file = path.join(directory, 'results.json');
        const write = data => fs.writeFileSync(file, JSON.stringify(data));
        write(result); assert.equal(verifyApplicationCampaign(file).result.matched, true);
        const candidate = structuredClone(result);
        candidate.toolchain.ownedScalarSelection = 'thumb-branch';
        write(candidate);
        assert.throws(() => verifyApplicationCampaign(file), /policy/);
        assert.equal(verifyApplicationCampaign(file, 'thumb-branch').result.matched, true);
        const homeCandidate = structuredClone(candidate);
        homeCandidate.toolchain.ownedScalarSelection = 'thumb-homes';
        write(homeCandidate);
        assert.throws(() => verifyApplicationCampaign(file, 'thumb-branch'), /policy/);
        assert.equal(verifyApplicationCampaign(file, 'thumb-homes').result.matched, true);
        write(candidate);
        const fresh = offset => {
          const report = structuredClone(candidate);
          for (const round of report.rounds) {
            const n = Buffer.from(round.nonce, 'hex'); n[14] += offset;
            round.nonce = n.toString('hex');
          }
          return report;
        };
        const b = fresh(1), a1 = fresh(2), b1 = fresh(3);
        assert.equal(compareTimingReports(result, b).modules.length, 6);
        assert.deepEqual(assessTiming(result, b, a1, b1).regressions, []);
        assert.throws(() => compareTimingReports(result, result), /fresh/);
        const invalid = fresh(4);
        invalid.rounds[0].modules[0].timing.repetitions++;
        assert.throws(() => compareTimingReports(result, invalid), /batch/);
        invalid.rounds[0].modules[0].timing.repetitions--;
        invalid.rounds[0].modules[0].owned[4] = 0;
        invalid.rounds[0].modules[0].llvm[4] = 0;
        assert.throws(() => compareTimingReports(result, invalid), /guard/);
        for (const mutate of [
          data => { data.rounds[1].configurations[0].serial = serials[0]; },
          data => { data.rounds[1].seed++; },
          data => { data.rounds[0].modules[0].timing.ownedToLlvm = 2; },
          data => { data.modules[0].timing.ownedToLlvm = 2; },
          data => { data.toolchain.ownedScalarSelection = 'thumb-compact'; },
          data => { data.rounds[1].nonce = data.rounds[0].nonce; },
        ]) {
          const changed = structuredClone(result); mutate(changed); write(changed);
          assert.throws(() => verifyApplicationCampaign(file));
        }
        write(result);
        fs.writeFileSync(path.join(directory, 'round1-request.bin'), Buffer.alloc(64));
        assert.throws(() => verifyApplicationCampaign(file));
      });
