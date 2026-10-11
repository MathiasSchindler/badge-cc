import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import { assembly, header, request, decode, audit, experiments, modes, iterations, samples,
  reportBytes, metadataRecords, suite } from '../tools/datasheet-experiments.mjs';
import { crc32 } from '../tools/compiler-test-protocol.mjs';
import { analyze, validateCampaign } from '../tools/analyze-datasheet.mjs';

const nonce = Buffer.from('0123456789abcdef0123456789abcdef', 'hex');
const build = 0xabcdef01;
function wire(values) {
  const output = Buffer.alloc(reportBytes);
  for (let i = 0; i < reportBytes / 64; i++) {
    const record = output.subarray(i * 64, (i + 1) * 64);
    record.writeUInt32LE(0x32423044); record[4] = 1; record[6] = 64;
    record.writeUInt32LE(build, 8); record.writeUInt32LE(suite + i, 12); nonce.copy(record, 16);
    const payload = values[i] || Array(7).fill(100000);
    payload.forEach((value, index) => record.writeUInt32LE(value >>> 0, 32 + 4 * index));
    record.writeUInt32LE(crc32(record.subarray(0, 60)), 60);
  }
  return output;
}
const payloads = [
  [1, experiments.length, modes.length, samples, iterations, 1, 0],
  [0x30004927, 0x410fd213, 0, 0, 0, 0, 0], Array(7).fill(0),
  [1, 0, 0, 0, 0, 0, 0], [0x2003e000, 0x2003e020, 0x20040000, 0x20040004, 0, 0, 0],
  Array(7).fill(0), Array(7).fill(10), [1, 1, 0, 0, 0, 0, 0],
];
test('experiment registry is source-bound, unique and fits the mailbox', () => {
  assert.equal(experiments.length, 39);
  assert.equal(new Set(experiments.map(e => e.name)).size, experiments.length);
  assert(reportBytes <= 0x7f00);
  const ledger = JSON.parse(fs.readFileSync(new URL('../docs/datasheet/fact-ledger.json', import.meta.url)));
  assert(experiments.every(e => ledger.facts.some(f => f.id === e.fact)));
  assert.equal(metadataRecords, payloads.length);
});
test('generation is deterministic with exact widths, placements and independent store targets', () => {
  assert.equal(assembly(), assembly());
  assert.equal(header(build), header(build));
  assert(assembly().includes('.section .measure_flash'));
  assert(assembly().includes('.section .measure_ram'));
  assert(assembly().includes('str.n r3, [r1, #32]'));
  assert(assembly().includes('str.n r7, [r3]'));
  assert(header(build).includes('#define D02_STORE_DATA 24u'));
});
test('request uses shared CRC protocol and rejects an empty nonce', () => {
  const encoded = request(build, nonce);
  assert.equal(encoded.readUInt32LE(8), build);
  assert.equal(encoded.readUInt32LE(12), suite);
  assert.equal(encoded.readUInt32LE(60), crc32(encoded.subarray(0, 60)));
  assert.throws(() => request(build, Buffer.alloc(16)), /nonce/);
});
test('all samples and every row survive decoding with no synthetic replacement', () => {
  const decoded = decode(wire(payloads), build, nonce);
  assert.equal(decoded.rows.length, experiments.length * modes.length);
  assert.equal(decoded.rows[0].experiment, 'empty');
  assert.equal(decoded.rows.at(-1).experiment, 'signed-divide-negative');
  assert.deepEqual(decoded.rows[0].samples, Array(7).fill(100000));
});
for (const [name, modify, pattern] of [
  ['timer unit', p => p[3][0] = 0, /clk_sys/],
  ['pause', p => p[3][3] = 1, /paused/],
  ['clock drift', p => p[7][0] = 0, /clock drift/],
  ['DMA still busy', p => p[7][2] = 0x04000000, /active DMA/],
  ['wrong core', p => p[4][6] = 1, /core 0/],
  ['excessive overhead', p => p[6][0] = 1001, /overhead/],
  ['timer resolution', p => p[metadataRecords] = Array(7).fill(999), /under-resolved/],
]) test(`rejects ${name}`, () => {
  const p = structuredClone(payloads); modify(p);
  assert.throws(() => decode(wire(p), build, nonce), pattern);
});
test('device failures are surfaced before uncommitted row errors', () => {
  const p = structuredClone(payloads); p[0][0] = 2; p[0][6] = 0x10025;
  assert.throws(() => decode(wire(p), build, nonce), /device failure.*10025/);
});
test('rejects stale, corrupt, reordered, truncated or excess reports', () => {
  const good = wire(payloads);
  assert.throws(() => decode(good, build, Buffer.alloc(16, 5)), /nonce/);
  assert.throws(() => decode(good, build + 1, nonce), /build/);
  assert.throws(() => decode(good.subarray(0, -1), build, nonce), /length/);
  assert.throws(() => decode(Buffer.concat([good, Buffer.alloc(64)]), build, nonce), /length/);
  const corrupt = Buffer.from(good); corrupt[50] ^= 1;
  assert.throws(() => decode(corrupt, build, nonce), /CRC/);
  const reordered = Buffer.from(good);
  good.copy(reordered, metadataRecords * 64, (metadataRecords + 1) * 64, (metadataRecords + 2) * 64);
  assert.throws(() => decode(reordered, build, nonce), /order/);
});
test('linked audit rejects wrong widths, operands, alignment and branch targets', () => {
  const spec = { ...experiments.find(e => e.name === 'add-narrow'), repeats: 1 };
  let nm = '', disassembly = '';
  for (const [p, base] of [['flash', 0x10000000], ['ram', 0x20000000]]) {
    for (const [suffix, offset] of [['', 0], ['_loop', 12], ['_body', 12], ['_body_end', 14], ['_end', 24]])
      nm += `${(base + offset).toString(16)} T d02_${p}_0${suffix}\n`;
    disassembly += `${(base+12).toString(16)}: 3301 adds r3, #0x1\n`;
    disassembly += `${(base+14).toString(16)}: 3801 subs r0, #0x1\n`;
    disassembly += `${(base+16).toString(16)}: f47f affc bne.w 0x${(base+12).toString(16)}\n`;
  }
  assert.equal(audit(nm, disassembly, [spec]).length, 1);
  assert.throws(() => audit(nm, disassembly.replace('3301', 'f103 0301'), [spec]), /width/);
  assert.throws(() => audit(nm, disassembly.replace('adds r3', 'adds r4'), [spec]), /operands/);
  assert.throws(() => audit(nm.replace('1000000c T d02_flash_0_loop', '1000000e T d02_flash_0_loop'), disassembly, [spec]), /alignment/);
  assert.throws(() => audit(nm, disassembly.replace('bne.w 0x1000000c', 'bne.w 0x1000000e'), [spec]), /branch target/);
});

function campaignFixture() {
  const serials = ['0000000000000001', '0000000000000002'];
  const decoded = decode(wire(payloads), build, nonce);
  return { schema: 1, status: 'captured', serials,
    backups: serials.map(serial => ({ serial, sha256: 'c'.repeat(64) })),
    restoration: serials.map(serial => ({ serial, verified: true, left_in_bootsel: true, sha256: 'c'.repeat(64) })),
    rounds: Array.from({ length: 6 }, (_, index) => ({
      index, kind: index < 2 ? 'control' : index < 4 ? 'comparison-1' : 'comparison-2',
      nonce: (index + 1).toString(16).padStart(32, '0'),
      captures: [0, 1].map(i => ({
        serial: serials[index % 2 ? 1 - i : i],
        compiler: index < 2 || i === 0 ? 'llvm' : 'owned',
        image_sha256: (index < 2 || i === 0 ? 'a' : 'b').repeat(64),
        ...structuredClone(decoded),
      })),
    })),
  };
}
test('campaign requires complete repeated controls, physical swaps and verified restoration', () => {
  const campaign = campaignFixture();
  validateCampaign(campaign);
  const bad = structuredClone(campaign);
  bad.rounds[1].captures.reverse();
  assert.throws(() => validateCampaign(bad), /roles/);
  assert.throws(() => validateCampaign({ ...campaign, rounds: campaign.rounds.slice(0, 4) }), /repeated/);
  bad.rounds = campaign.rounds;
  bad.restoration[0].verified = false;
  assert.throws(() => validateCampaign(bad), /restoration/);
});
test('analysis reports unresolved predictions within a measured noise envelope', () => {
  const result = analyze(campaignFixture());
  assert.equal(result.raw_samples, 16380);
  assert.equal(result.observations.find(x => x.name === 'multiply-dependency').verdict, 'cannot-resolve');
  assert.equal(result.observations.find(x => x.name === 'simple-alu-dependency').verdict, 'supports-within-noise');
  assert.deepEqual(result.division[0].values.map(x => x.documented_cycles), [2, 4, 5, 7, 11]);
});
test('reported medians cannot replace or disagree with raw measurements', () => {
  const campaign = campaignFixture();
  campaign.rounds[0].captures[0].rows[0].median++;
  assert.throws(() => validateCampaign(campaign), /invented statistics/);
});
test('campaign rejects stale nonces, changed images and failed capture status', () => {
  const campaign = campaignFixture();
  assert.throws(() => validateCampaign({ ...campaign, status: 'failed' }), /failed campaign/);
  campaign.rounds[1].nonce = campaign.rounds[0].nonce;
  assert.throws(() => validateCampaign(campaign), /stale/);
  campaign.rounds[1].nonce = '0'.repeat(32);
  assert.throws(() => validateCampaign(campaign), /nonce/);
  campaign.rounds[1].nonce = 'f'.repeat(32);
  campaign.rounds[1].captures[0].image_sha256 = 'd'.repeat(64);
  assert.throws(() => validateCampaign(campaign), /changed image/);
});
test('campaign rejects changed core or clock configuration between rounds', () => {
  const campaign = campaignFixture();
  campaign.rounds[2].captures[0].environment[1][1]++;
  assert.throws(() => validateCampaign(campaign), /configuration changed/);
});
test('prediction classifications distinguish direction, contradiction and measured noise', () => {
  const campaign = campaignFixture();
  for (const round of campaign.rounds) {
    for (const capture of round.captures) {
      for (const [name, value] of [['mul-chain', 200000], ['shift-chain', 50000]]) {
        const row = capture.rows.find(r => r.mode === 'sram-warm' && r.experiment === name);
        row.samples.fill(value);
        row.min = row.median = row.max = value;
      }
    }
  }
  const result = analyze(campaign);
  assert.equal(result.observations.find(x => x.name === 'multiply-dependency').verdict, 'supports');
  assert.equal(result.observations.find(x => x.name === 'complex-alu-result-delay').verdict, 'contradicts-sequence-prediction');
  const row = campaign.rounds[0].captures[0].rows.find(r => r.mode === 'sram-warm' && r.experiment === 'mul-chain');
  row.samples[0] = row.min = 1;
  const noisy = analyze(campaign).observations.find(x => x.name === 'multiply-dependency');
  assert.equal(noisy.verdict, 'cannot-resolve');
  assert(noisy.runs.every(r => r.noise_envelope >= 199999));
});
