import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import crypto from 'node:crypto';
import { compareObjects, readObject } from '../tools/assembler-crossover.mjs';
import { verifyListing } from '../tools/assembler-listing.mjs';
import { buildHardware, restoreHardware } from '../tools/system-instructions.mjs';
import { startupStages, validateStartupResponse, validateSynchronizationResponse } from '../tools/synchronization.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const tool = name => path.join(root, 'build', name);
const fixtures = path.join(root, 'tests/fixtures');
const clang = process.env.LLVM_CLANG ?? 'clang';
const lld = process.env.LLVM_LLD ?? 'ld.lld';
const qemu = process.env.QEMU_ARM ?? 'qemu-system-arm';
const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
const loads = ['ldrex', 'ldrexb', 'ldrexh', 'lda', 'ldab', 'ldah', 'ldaex', 'ldaexb', 'ldaexh'];
const stores = ['strex', 'strexb', 'strexh', 'stlex', 'stlexb', 'stlexh'];
const plainStores = ['stl', 'stlb', 'stlh'];
const header = '.syntax unified\n.cpu cortex-m33\n.thumb\n';
function invoke(command, args, success = true) {
  const result = spawnSync(command, args, { encoding: 'utf8', timeout: 30000 });
  assert.ifError(result.error);
  if (success) assert.equal(result.status, 0, result.stderr);
  else assert.notEqual(result.status, 0, 'expected explicit failure');
  return result;
}
function fixture(run) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ba05-'));
  try { run(dir); } finally { fs.rmSync(dir, { recursive: true }); }
}
function compare(dir, input) {
  const owned = path.join(dir, 'owned.o'), llvm = path.join(dir, 'llvm.o');
  const listing = path.join(dir, 'listing.json');
  invoke(tool('badge-asm'), ['--listing', listing, input, '-o', owned]);
  invoke(clang, [...target, '-c', input, '-o', llvm]);
  verifyListing(JSON.parse(fs.readFileSync(listing)), owned, input);
  const a = readObject(owned), b = readObject(llvm), comparison = compareObjects(a, b);
  assert(comparison.allocated_equal, JSON.stringify(comparison.normalized));
  assert(comparison.relocations_equal, JSON.stringify(comparison.normalized));
  assert(comparison.public_symbols_equal, JSON.stringify(comparison.normalized));
  return { a, b, owned, llvm };
}
function rejected(dir, source) {
  const input = path.join(dir, 'invalid.s'), output = path.join(dir, 'preserved.o'),
    listing = path.join(dir, 'preserved.json');
  fs.writeFileSync(input, header + source);
  fs.writeFileSync(output, 'preserved'); fs.writeFileSync(listing, 'preserved');
  const result = invoke(tool('badge-asm'), ['--listing', listing, input, '-o', output], false);
  assert.match(result.stderr, /badge-asm:/);
  assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
  assert.equal(fs.readFileSync(listing, 'utf8'), 'preserved');
}

test('synchronization oracle has exact independently specified halfwords and LLVM/listing agreement', () => fixture(dir => {
  const { a } = compare(dir, path.join(fixtures, 'synchronization_oracle_fixture.S'));
  const bytes = a.sections.find(s => s.name === '.text').data;
  const pairs = [[0xe851, 0x0f00], [0xe85d, 0xefff], [0xe841, 0x0201], [0xe84d, 0xecff],
    [0xe8d4, 0x3f4f], [0xe8dd, 0xef5f], [0xe8c4, 0x3f45], [0xe8cd, 0xef5c],
    [0xf3bf, 0x8f2f], [0xe8d1, 0x0faf], [0xe8dd, 0xef8f], [0xe8d4, 0x3f9f],
    [0xe8c1, 0x0faf], [0xe8cd, 0xef8f], [0xe8c4, 0x3f9f], [0xe8d1, 0x0fef],
    [0xe8dd, 0xefcf], [0xe8d4, 0x3fdf], [0xe8c1, 0x0fe2], [0xe8cd, 0xefcc],
    [0xe8c4, 0x3fd5]];
  for (const [i, pair] of pairs.entries()) {
    assert.equal(bytes.readUInt16LE(i * 4), pair[0]);
    assert.equal(bytes.readUInt16LE(i * 4 + 2), pair[1]);
  }
}));

test('register fields, permitted overlaps and every word immediate match LLVM', () => fixture(dir => {
  const lines = [], registers = [0, 7, 8, 12, 14], bases = [...registers, 13];
  for (const op of [...loads, ...plainStores]) for (const rt of registers) for (const rn of bases)
    lines.push(`${op} r${rt},[r${rn}]`);
  for (const op of stores) for (const rd of registers) for (const rt of registers) for (const rn of bases)
    if (rd !== rt && rd !== rn) lines.push(`${op} r${rd},r${rt},[r${rn}]`);
  for (let offset = 0; offset <= 1020; offset += 4)
    lines.push(`ldrex r0,[r1,#${offset}]`, `strex r2,r0,[r1,#${offset}]`);
  const instructionCount = lines.length;
  for (const condition of ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le'])
    for (const op of [...loads, ...plainStores, ...stores, 'clrex']) {
      const args = op === 'clrex' ? '' : stores.includes(op) ? 'r2,r0,[r1]' : 'r0,[r1]';
      lines.push(`it ${condition}`, `${op}${condition} ${args}`);
    }
  const input = path.join(dir, 'matrix.s');
  fs.writeFileSync(input, header + lines.join('\n') + '\n');
  const { a } = compare(dir, input);
  assert.equal(a.sections.find(s => s.name === '.text').data.length,
    instructionCount * 4 + (lines.length - instructionCount) / 2 * 6);
}));

test('illegal data/status/base registers and status overlap reject atomically', () => fixture(dir => {
  for (const op of [...loads, ...plainStores]) {
    for (const rt of ['sp', 'pc']) rejected(dir, `${op} ${rt},[r1]\n`);
    rejected(dir, `${op} r0,[pc]\n`);
  }
  for (const op of stores) for (const args of ['sp,r0,[r1]', 'pc,r0,[r1]', 'r2,sp,[r1]',
    'r2,pc,[r1]', 'r2,r0,[pc]', 'r0,r0,[r1]', 'r1,r0,[r1]'])
    rejected(dir, `${op} ${args}\n`);
}));

test('addressing modes, boundaries, widths and malformed operands reject atomically', () => fixture(dir => {
  for (const op of [...loads, ...plainStores, ...stores]) {
    const data = stores.includes(op) ? 'r2,r0,' : 'r0,';
    for (const memory of ['[r1,r3]', '[r1,r3,lsl #2]', '[r1]!', '[r1],#4', '[r1,#-4]'])
      rejected(dir, `${op} ${data}${memory}\n`);
    for (const suffix of ['.n', '.w']) rejected(dir, `${op}${suffix} ${data}[r1]\n`);
    for (const offset of op === 'ldrex' || op === 'strex' ? [1, 2, 1021, 1024] : [1, 4, 1020])
      rejected(dir, `${op} ${data}[r1,#${offset}]\n`);
    rejected(dir, `${op}\n`);
  }
  for (const source of ['clrex #15\n', 'clrex.n\n', 'clrex.w\n', 'ldrexd r0,r1,[r2]\n',
    'strexd r3,r0,r1,[r2]\n', 'ldaexd r0,r1,[r2]\n', 'ldaxr r0,[r1]\n',
    'ldrexeq r0,[r1]\n', 'it eq\nstrexne r2,r0,[r1]\n'])
    rejected(dir, source);
}));

test('Cortex-M33 executes all widths, CLREX failure, successful stores and bounded lock timeout/recovery', () => fixture(dir => {
  const sources = ['synchronization_runtime.S', 'synchronization_probe.S'];
  const objects = sources.map((source, i) => {
    const result = compare(dir, path.join(fixtures, source));
    const owned = path.join(dir, `${i}-owned.o`), llvm = path.join(dir, `${i}-llvm.o`);
    fs.copyFileSync(result.owned, owned); fs.copyFileSync(result.llvm, llvm);
    return { owned, llvm };
  });
  for (const role of ['owned', 'llvm']) {
    const elf = path.join(dir, `${role}.elf`);
    invoke(lld, ['--fatal-warnings', '-T', path.join(fixtures, 'system_runtime.ld'),
      '-e', 'ba05_reset', ...objects.map(o => o[role]), '-o', elf]);
    invoke(qemu, ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
      '-semihosting-config', 'enable=on,target=native', '-kernel', elf]);
  }
}));

function response() {
  const bytes = Buffer.alloc(256), nonce = Buffer.from('0123456789abcdef');
  const fields = { 0: 0x35304142, 1: 6, 16: 0x104, 21: 0x5352414d, 22: 1024,
    23: 2048, 24: 2048 * 2049 / 2, 25: 1024, 26: 1024, 29: 1, 30: 1, 32: 1,
    33: 0x12345678, 36: 65536, 37: 65536, 38: 1, 39: 1, 40: 2, 41: 2,
    42: 65536, 43: 65536, 44: 4, 45: 4, 46: 0x410fd213, 47: 0x410fd213,
    52: 0x10000000, 53: 0x10000800, 56: 0x20000000, 57: 0x20000000, 58: 1, 59: 1 << 24,
    60: 0x20000101, 61: 0x10000800, 63: 0x80000000 };
  for (const [index, value] of Object.entries(fields)) bytes.writeUInt32LE(value, Number(index) * 4);
  nonce.copy(bytes, 8);
  return { bytes, nonce };
}

test('physical report validates nonce, both cores, exact work, ordering, timeouts and clock/configuration', () => {
  const { bytes, nonce } = response();
  assert.equal(validateSynchronizationResponse(bytes, nonce, 'synchronization-probe').total_updates, 2048);
  for (const index of [0, 1, 6, 7, 8, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 61, 62]) {
    const bad = Buffer.from(bytes);
    bad.writeUInt32LE((bad.readUInt32LE(index * 4) ^ 1) >>> 0, index * 4);
    assert.throws(() => validateSynchronizationResponse(bad, nonce, 'synchronization-probe'), `field ${index}`);
  }
  for (const [index, value] of [[29, 0], [60, 0x10000101], [63, 0]]) {
    const bad = Buffer.from(bytes); bad.writeUInt32LE(value, index * 4);
    assert.throws(() => validateSynchronizationResponse(bad, nonce, 'synchronization-probe'));
  }
  assert.throws(() => validateSynchronizationResponse(bytes.subarray(0, 252), nonce, 'synchronization-probe'));
  assert.throws(() => validateSynchronizationResponse(bytes, Buffer.alloc(16), 'synchronization-probe'));
  assert.throws(() => validateSynchronizationResponse(bytes, nonce, 'unknown'));
});

test('physical images are byte-identical and park core 1 in SRAM with a valid flash entry', () => fixture(dir => {
  const result = buildHardware(dir, false, true);
  assert.equal(result.mode, 'synchronization-probe');
  assert.equal(result.report_bytes, 256);
  assert.equal(result.images[0].binary_sha256, result.images[1].binary_sha256);
  assert.equal(result.images[0].boot.stack, 0x20082000);
}));

function startupResponse(stage) {
  const { bytes, nonce } = response();
  const set = (i, value) => bytes.writeUInt32LE(value >>> 0, i * 4);
  set(21, 0x4c354142); set(22, stage);
  for (const i of [23, 24, 25, 26, 29, 30, 31, 32, 33, 58]) set(i, 0);
  for (const i of [34, 35]) set(i, 0x41);
  for (const i of [38, 39]) set(i, 2);
  for (const i of [40, 41]) set(i, 0);
  for (const i of [44, 45]) set(i, 1);
  const multicore = stage === 1 || stage === 3;
  set(53, multicore ? bytes.readUInt32LE(61 * 4) : 0x10000000);
  if (multicore) { set(30, 1); set(58, 1); }
  if (stage === 3) { set(29, 1); set(33, 0x12345678); }
  if (stage === 5 || stage === 7) {
    set(35, 0x40); set(39, 1); set(41, 2); set(45, 4);
  }
  if (stage >= 6) set(32, 250);
  return { bytes, nonce };
}

test('isolated stage reports bind mode, clock preservation, core launch and watchdog-only recovery', () => {
  for (let stage = 1; stage <= startupStages.length; ++stage) {
    const { bytes, nonce } = startupResponse(stage);
    const mode = `startup-stage-${stage}`;
    const decoded = validateStartupResponse(bytes, nonce, mode);
    assert.equal(decoded.stage_name, startupStages[stage - 1]);
    assert.equal(decoded.full_contention_tested, false);
    for (const [i, value] of [[21, 0], [22, 0], [62, 1], [27, 1], [28, 1], [59, 0],
      [35, 0x21], [39, 0], [41, 5], [43, 17], [45, 0], [30, stage === 1 || stage === 3 ? 0 : 1],
      [58, stage === 1 || stage === 3 ? 0 : 1], [24, 1], [25, 1], [56, 0], [57, 0]]) {
      const bad = Buffer.from(bytes); bad.writeUInt32LE(value, i * 4);
      assert.throws(() => validateStartupResponse(bad, nonce, mode), `stage ${stage}, field ${i}`);
    }
    if (stage === 3) {
      const bad = Buffer.from(bytes); bad.writeUInt32LE(0, 29 * 4);
      assert.throws(() => validateStartupResponse(bad, nonce, mode));
    }
    if (stage === 1 || stage === 3) {
      const bad = Buffer.from(bytes); bad.writeUInt32LE(1, 26 * 4);
      assert.throws(() => validateStartupResponse(bad, nonce, mode));
    }
    if (stage >= 6) {
      const bad = Buffer.from(bytes); bad.writeUInt32LE(0, 32 * 4);
      assert.throws(() => validateStartupResponse(bad, nonce, mode));
    }
    assert.throws(() => validateStartupResponse(bytes, Buffer.alloc(16), mode));
  }
  assert.throws(() => validateStartupResponse(Buffer.alloc(256), Buffer.alloc(16), 'startup-stage-8'));
});

test('all isolated startup images match LLVM, preserve valid entry/stack and exclude full contention', () => fixture(dir => {
  for (let stage = 1; stage <= startupStages.length; ++stage) {
    const result = buildHardware(path.join(dir, String(stage)), false, true, stage);
    assert.equal(result.mode, `startup-stage-${stage}`);
    assert.equal(result.images[0].binary_sha256, result.images[1].binary_sha256);
    const assembly = fs.readFileSync(path.join(dir, String(stage), 'synchronization_badge.c.s'), 'utf8');
    assert(!assembly.includes('\nupdates:'), 'no contention worker may be reached');
    const wrapper = fs.readFileSync(path.join(dir, String(stage), 'system_badge.c.s'), 'utf8');
    if (stage >= 6) assert(wrapper.includes('\tbl\tba05_watchdog_wait\n'),
      'watchdog-only success path must enter its no-reboot assembly wait');
  }
}));

test('actual clock-control functions preserve live auxiliary sources and stop on failed mux selection', () => fixture(dir => {
  const source = fs.readFileSync(path.join(fixtures, 'synchronization_badge.c'), 'utf8');
  const functions = ['enable_xosc', 'configure_clock', 'external_exclusives'].map(name => {
    const match = new RegExp(`^static uint32_t ${name}\\(void\\) \\{[\\s\\S]*?^\\}`, 'm').exec(source);
    assert(match, `missing clock function ${name}`);
    return match[0];
  }).join('\n').replaceAll('__asm__ volatile ("dsb sy\\nisb sy" ::: "memory");', 'barrier();')
    .replaceAll('__asm__ volatile ("dsb sy" ::: "memory");', 'barrier();')
    .replace('__asm__ volatile ("mrs %0,control" : "=r"(control));', 'control = model_control;');
  assert(!functions.includes('__asm__'), 'only the execution barrier is replaced for this host register model');
  const wrapper = fs.readFileSync(path.join(fixtures, 'system_badge.c'), 'utf8');
  const restore = /^static uint32_t restore_actlr\(void\) \{[\s\S]*?^\}/m.exec(wrapper);
  assert(restore, 'missing ACTLR restoration');
  const restoreFunction = restore[0]
    .replaceAll('*(volatile uint32_t *)0xe000e008u', 'REG(0xe000e008u)')
    .replaceAll('*(const volatile uint32_t *)0xe000e008u', 'REG(0xe000e008u)')
    .replace('__asm__ volatile ("dsb sy\\nisb sy" ::: "memory");', 'barrier();');
  const input = path.join(dir, 'clock.c'), output = path.join(dir, 'clock');
  fs.writeFileSync(input, `
#include <assert.h>
#include <stdint.h>
static volatile uint32_t sysctrl,sysdiv,sysselected,refctrl,refdiv,refselected,xoscctrl,xoscstartup,xoscstatus,actlr,report[64];
static unsigned fail,barriers,model_control;
static volatile uint32_t mpu;
static volatile uint32_t *reg(uintptr_t address) {
    switch(address) {
    case 0x4001003c: return &sysctrl; case 0x40010040: return &sysdiv;
    case 0x40010044: return &sysselected; case 0x40010030: return &refctrl;
    case 0x40010034: return &refdiv; case 0x40010038: return &refselected;
    case 0x40048000: return &xoscctrl; case 0x4004800c: return &xoscstartup;
    case 0x40048004:
        if(xoscctrl==0x00fabaa0 && fail!=1) xoscstatus=0x80001000;
        return &xoscstatus;
    case 0xe000e008:
        if(fail==4) actlr&=~(1u<<29);
        return &actlr;
    case 0xe000ed94: return &mpu;
    default: assert(0); return 0;
    }
}
#define REG(address) (*reg(address))
#define REPORT report
#define RESPONSE report
#define BA05_STAGE 1
#define POLL_BUDGET 16
static uint32_t saved_actlr,actlr_saved;
static void barrier(void) { ++barriers; }
static uint32_t wait_value(volatile uint32_t *address,uint32_t expected) {
    if((uintptr_t)address==0x40010044) {
        assert(sysctrl==0x40 && expected==1);
        if(fail==2) return 1;
        sysselected=1;
    } else {
        assert((uintptr_t)address==0x40010038 && expected==4);
        assert(refctrl==0x62 && refdiv==65536);
        if(fail==3) return 1;
        refselected=4;
    }
    return 0;
}
${functions}
${restoreFunction}
int main(void) {
    for(fail=0;fail<4;++fail) {
        sysctrl=0x41; sysdiv=131072; sysselected=2;
        refctrl=0x60; refdiv=131072; refselected=1;
        xoscctrl=0; xoscstatus=0; barriers=0;
        assert(configure_clock()==fail);
        if(fail==0) {
            assert(sysctrl==0x40 && sysdiv==65536 && sysselected==1);
            assert(refctrl==0x62 && refdiv==65536 && refselected==4 && barriers==1);
        } else if(fail==1) assert(sysctrl==0x41 && refctrl==0x60);
        else if(fail==2) assert(refctrl==0x60 && refdiv==131072);
    }
    fail=0; actlr=0x101; barriers=0;
    assert(external_exclusives()==0 && actlr==0x20000101 && barriers==2);
    fail=4; actlr=0x101; barriers=0;
    assert(external_exclusives()==1 && actlr==0x101 && barriers==2);
    fail=0; model_control=1; barriers=0;
    assert(external_exclusives()==2 && actlr==0x101 && barriers==0);
    model_control=0; mpu=1;
    assert(external_exclusives()==2 && actlr==0x101 && barriers==0);
    saved_actlr=0x101; actlr_saved=1; actlr=0x20000101; barriers=0;
    assert(restore_actlr()==0 && actlr==0x101 && report[25]==0x101 && barriers==1);
    saved_actlr=0x20000101; fail=4;
    assert(restore_actlr()==1 && report[25]==0x101);
    actlr_saved=0; barriers=0;
    assert(restore_actlr()==0 && barriers==0);
    return 0;
}
`);
  invoke(process.env.CC ?? 'cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', input, '-o', output]);
  invoke(output, []);
}));

test('recovery targets only the preserved serial and requires hash-verified full flash readback', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ba05-restore-'));
  const previous = process.env.PICOTOOL;
  try {
    const original = path.join(dir, 'original.bin'), file = path.join(dir, 'results.json');
    const bytes = Buffer.alloc(16777216, 0x5a);
    fs.writeFileSync(original, bytes);
    const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
    const result = { schema: 1, milestone: 'BA05', serial: '9BBF88DD17D66E7D',
      status: 'recovery-required', restored: false, backup: { file: original, sha256 } };
    fs.writeFileSync(file, JSON.stringify(result));
    const mock = path.join(dir, 'picotool');
    fs.writeFileSync(mock, `#!${process.execPath}
const fs=require('node:fs'),path=require('node:path');
const dir=__dirname,args=process.argv.slice(2);
if(args.at(-2)!=='--ser'||args.at(-1)!=='9BBF88DD17D66E7D')process.exit(2);
fs.appendFileSync(path.join(dir,'commands.jsonl'),JSON.stringify(args)+'\\n');
if(args[0]==='info')console.log('RP2350\\ncurrent cpu: ARM\\nflash size: 16384K');
else if(args[0]==='load') {
 if(fs.realpathSync(args[args.indexOf('-u')+1])!==fs.realpathSync(path.join(dir,'original.bin'))||!args.includes('0x10000000'))process.exit(3);
} else if(args[0]==='save') {
 const output=args[args.indexOf('-v')+1];
 fs.copyFileSync(path.join(dir,'original.bin'),output);
 if(fs.existsSync(path.join(dir,'corrupt')))fs.writeFileSync(output,'corrupt');
} else process.exit(4);
`, { mode: 0o755 });
    process.env.PICOTOOL = mock;
    const restored = await restoreHardware(dir);
    assert.equal(restored.restored, true);
    assert.equal(restored.status, 'physical-failed-restored');
    assert.equal(restored.restoration.sha256, sha256);
    assert.equal(restored.restoration.left_in_bootsel, true);
    const commands = fs.readFileSync(path.join(dir, 'commands.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
    assert.deepEqual(commands.map(args => args[0]), ['info', 'info', 'load', 'save']);
    fs.writeFileSync(file, JSON.stringify(result));
    fs.writeFileSync(path.join(dir, 'corrupt'), '');
    await assert.rejects(restoreHardware(dir), /restoration mismatch/);
    assert.equal(JSON.parse(fs.readFileSync(file)).restored, false);
    fs.writeFileSync(original, 'drift');
    await assert.rejects(restoreHardware(dir));
  } finally {
    if (previous === undefined) delete process.env.PICOTOOL;
    else process.env.PICOTOOL = previous;
    fs.rmSync(dir, { recursive: true });
  }
});
