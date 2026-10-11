import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { readObject } from '../tools/assembler-crossover.mjs';
import { verifyListing } from '../tools/assembler-listing.mjs';
import { buildHardware, qualifySystem, validateHardwareImage, validateHardwareResponse,
  verifySystem } from '../tools/system-instructions.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const assembler = path.join(root, 'build/badge-asm');
const clang = process.env.LLVM_CLANG ?? 'clang';
function fixture(run) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'ba03-'));
  try { run(directory); } finally { fs.rmSync(directory, { recursive: true }); }
}
// Armv8-M SYSm fields; write mask is encoded separately from the register number.
const readable = { apsr: 0, iapsr: 1, eapsr: 2, xpsr: 3, ipsr: 5, epsr: 6, iepsr: 7,
  msp: 8, psp: 9, msplim: 10, psplim: 11, primask: 16, basepri: 17, basepri_max: 18, faultmask: 19, control: 20,
  msp_ns: 136, psp_ns: 137, msplim_ns: 138, psplim_ns: 139, primask_ns: 144,
  basepri_ns: 145, faultmask_ns: 147, control_ns: 148, sp_ns: 152 };
const writable = Object.fromEntries(Object.entries(readable).filter(([, n]) => n >= 8)
  .map(([name, n]) => [name, n | 0x800]));
Object.assign(writable, { basepri_max: 0x812, apsr_nzcvq: 0x800, apsr_g: 0x400, apsr_nzcvqg: 0xc00 });

test('MRS/MSR exact independent fields and LLVM agree for all supported names and data registers', () => fixture(dir => {
  const source = ['.syntax unified', '.cpu cortex-m33', '.thumb'], expected = [];
  for (const [operation, names] of [['mrs', readable], ['msr', writable]])
    for (const [name, field] of Object.entries(names))
      for (const r of [...Array(13).keys(), 14]) {
        const register = r === 14 ? 'lr' : `r${r}`;
        source.push(`${operation} ${operation === 'mrs' ? `${register},${name}` : `${name},${register}`}`);
        expected.push(operation === 'mrs' ? 0xf3ef : 0xf380 | r,
          operation === 'mrs' ? 0x8000 | (r << 8) | field : 0x8000 | field);
      }
  const input = path.join(dir, 'input.s'), owned = path.join(dir, 'owned.o'), llvm = path.join(dir, 'llvm.o'),
    listing = path.join(dir, 'listing.json');
  fs.writeFileSync(input, source.join('\n'));
  let response = spawnSync(assembler, ['--listing', listing, input, '-o', owned], { encoding: 'utf8' });
  assert.equal(response.status, 0, response.stderr);
  response = spawnSync(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-c', input, '-o', llvm],
    { encoding: 'utf8' });
  assert.equal(response.status, 0, response.stderr);
  const bytes = readObject(owned).sections.find(s => s.name === '.text').data;
  assert.equal(bytes.length, expected.length * 2);
  expected.forEach((value, index) => assert.equal(bytes.readUInt16LE(index * 2), value));
  assert(bytes.equals(readObject(llvm).sections.find(s => s.name === '.text').data));
  verifyListing(JSON.parse(fs.readFileSync(listing)), owned, input);
}));

test('CPS, exception immediates and unchanged SY barrier encodings have exact contracts', () => fixture(dir => {
  const input = path.join(dir, 'input.s'), output = path.join(dir, 'output.o');
  const instructions = [], expected = [];
  for (const [op, base] of [['cpsid', 0xb670], ['cpsie', 0xb660]])
    for (const [mask, value] of [['i', 2], ['f', 1], ['if', 3]]) {
      instructions.push(`${op} ${mask}`); expected.push(base | value);
    }
  for (const [op, base] of [['svc', 0xdf00], ['bkpt', 0xbe00]])
    for (let immediate = 0; immediate < 256; immediate++) {
      instructions.push(`${op} #${immediate}`); expected.push(base | immediate);
    }
  for (const [op, halfword] of [['dsb', 0x8f4f], ['dmb', 0x8f5f], ['isb', 0x8f6f]])
    for (const suffix of ['', ' sy', '.w sy']) {
      instructions.push(`${op}${suffix}`); expected.push(0xf3bf, halfword);
    }
  fs.writeFileSync(input, instructions.join('\n'));
  const response = spawnSync(assembler, [input, '-o', output], { encoding: 'utf8' });
  assert.equal(response.status, 0, response.stderr);
  const bytes = readObject(output).sections.find(s => s.name === '.text').data;
  assert.equal(bytes.length, expected.length * 2);
  expected.forEach((word, i) => assert.equal(bytes.readUInt16LE(i * 2), word));
}));

test('reserved fields, wrong access, bad registers/immediates/widths and IT restrictions preserve output', () => fixture(dir => {
  const input = path.join(dir, 'input.s'), output = path.join(dir, 'output.o');
  const rejected = [];
  for (const op of ['mrs', 'msr']) for (const r of ['sp', 'pc', 'r13', 'r15', 'r16', '#0'])
    rejected.push(`${op} ${op === 'mrs' ? `${r},primask` : `primask,${r}`}`);
  for (const name of ['#16', '16', 'reserved', 'primask_s', 'basepri_max_ns', 'pac_key_p_0'])
    rejected.push(`mrs r0,${name}`, `msr ${name},r0`);
  for (const name of ['apsr', 'iapsr', 'eapsr', 'xpsr', 'ipsr', 'epsr', 'iepsr'])
    rejected.push(`msr ${name},r0`);
  rejected.push('mrs r0,apsr_nzcvq', 'mrs.n r0,control', 'msr.n control,r0',
    'mrs.w r0,control', 'msr.w control,r0', 'mrs r0', 'msr control,r0,r1');
  for (const op of ['cpsid', 'cpsie']) {
    for (const mask of ['', 'a', 'fi', '#1', 'i,f', 'ia', 'if,#0', 'sy'])
      rejected.push(`${op} ${mask}`);
    rejected.push(`${op}.w i`, `it eq\n${op}eq i`, `${op}eq i`);
  }
  for (const op of ['svc', 'bkpt'])
    rejected.push(`${op} #-1`, `${op} #256`, `${op} #4294967296`, `${op} r0`, `${op}`,
      `${op} #0,#1`, `${op}.w #1`);
  rejected.push('itt eq\nsvceq #1\nmoveq r0,#0', 'it eq\nbkpteq #1');
  for (const op of ['dsb', 'dmb', 'isb'])
    for (const operand of ['ish', 'ishst', 'osh', 'st', '#15', '#0', 'sy,sy'])
      rejected.push(`${op} ${operand}`);
  for (const source of rejected) {
    fs.writeFileSync(input, `${source}\n`); fs.writeFileSync(output, 'preserved');
    const response = spawnSync(assembler, [input, '-o', output], { encoding: 'utf8' });
    assert.notEqual(response.status, 0, source);
    assert.match(response.stderr, /badge-asm:/, source);
    assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
  }
}));

test('Cortex-M33 runtime restores masks, exercises stack/control/PSR and returns from SVC', () => {
  const result = qualifySystem(path.join(root, 'build/ba03-system'));
  assert.equal(result.runtime.length, 2);
  assert(result.runtime.every(route => route.exit === 0));
  verifySystem(result);
  const broken = structuredClone(result);
  broken.runtime[0].exit = 1;
  assert.throws(() => verifySystem(broken));
});

test('physical smoke and probe images have global Thumb entry matching their loaded reset vector', () => fixture(dir => {
  for (const smoke of [true, false]) {
    const result = buildHardware(path.join(dir, smoke ? 'smoke' : 'probe'), smoke);
    assert.equal(result.images[0].binary_sha256, result.images[1].binary_sha256);
    for (const image of result.images) {
      assert(image.boot.entry & 1);
      const bytes = fs.readFileSync(image.elf);
      bytes.writeUInt32LE(0, 24);
      const broken = path.join(dir, 'missing-entry.elf');
      fs.writeFileSync(broken, bytes);
      assert.throws(() => validateHardwareImage(broken), /entry differs from reset/);
    }
  }
}));

test('physical reports require exact size, nonce, completed stage, SVC, recovery and restored state', () => {
  const nonce = Buffer.from('00112233445566778899aabbccddeeff', 'hex');
  const bytes = Buffer.alloc(84);
  bytes.writeUInt32LE(0x33304142, 0);
  bytes.writeUInt32LE(3, 4);
  nonce.copy(bytes, 8);
  bytes.writeUInt32LE(11, 28);
  bytes.writeUInt32LE(0x104, 64);
  assert.deepEqual(validateHardwareResponse(bytes, nonce, 'system-probe'),
    { result: 0, exception: 11, before: [0, 0, 0, 0], after: [0, 0, 0, 0] });
  for (const offset of [0, 4, 8, 24, 28, 32, 48, 64, 80]) {
    const broken = Buffer.from(bytes);
    broken.writeUInt32LE(broken.readUInt32LE(offset) ^ 1, offset);
    assert.throws(() => validateHardwareResponse(broken, nonce, 'system-probe'));
  }
  for (const broken of [bytes.subarray(0, 80), Buffer.concat([bytes, Buffer.alloc(4)])])
    assert.throws(() => validateHardwareResponse(broken, nonce, 'system-probe'), /size/);
  assert.throws(() => validateHardwareResponse(bytes, nonce, 'boot-smoke'), /exception/);
  assert.throws(() => validateHardwareResponse(bytes, nonce.subarray(0, 8), 'system-probe'), /nonce size/);
  assert.throws(() => validateHardwareResponse(bytes, nonce, 'unknown'), /mode/);
  bytes.writeUInt32LE(0, 28);
  assert.equal(validateHardwareResponse(bytes, nonce, 'boot-smoke').exception, 0);
});
