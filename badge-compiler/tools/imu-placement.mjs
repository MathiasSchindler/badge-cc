import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { decodeElf } from './boundary-evidence.mjs';
import { verifyUf2 } from './benchmark-c06.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const flash = 0x10000000, ram = 0x20000000, ramBudget = 64;
const run = (cmd, args) => execFileSync(cmd, args, { cwd: root, timeout: 30000 });
const owned = name => path.join(root, 'badge-compiler/build', name);
const readElf = file => decodeElf(fs.readFileSync(file));
const symbol = (elf, name) => {
  const found = elf.symbols.find(s => s.name === name && s.section);
  assert(found, `missing symbol: ${name}`);
  return found;
};
const body = (elf, name) => {
  const s = symbol(elf, name), section = elf.sections[s.section];
  const start = (s.value & ~1) - section.address;
  return section.data.subarray(start, start + s.size);
};
function assertThunk(elf) {
  const bytes = body(elf, 'imu_placement_sqrt');
  assert.equal(bytes.length, 10);
  const imm16 = (offset, opcode) => {
    const first = bytes.readUInt16LE(offset), second = bytes.readUInt16LE(offset + 2);
    assert.equal(first & 0xfbf0, opcode);
    assert.equal(second & 0x8f00, 0x0c00);
    return ((first & 15) << 12) | ((first & 0x400) << 1) |
      ((second & 0x7000) >> 4) | (second & 255);
  };
  assert.equal(bytes.readUInt16LE(8), 0x4760);
  const address = imm16(0, 0xf240) + imm16(4, 0xf2c0) * 65536;
  assert.equal(address, symbol(elf, 'integer_sqrt').value);
  assert.equal(address % 2, 1, 'thunk must branch in Thumb state');
}
const sourceLine = /^\.section \.text\.integer_sqrt,"ax",%progbits$/gm;
const sqrtSection = /^\.section \.(?:text|data\.ram_code)\.integer_sqrt,"ax",%progbits\n[\s\S]*?^\.size integer_sqrt, \.-integer_sqrt$/gm;
const thunk = `.section .text.imu_placement_sqrt,"ax",%progbits
.global imu_placement_sqrt
.type imu_placement_sqrt, %function
.thumb_func
imu_placement_sqrt:
movw r12, #:lower16:(integer_sqrt+0)
movt r12, #:upper16:(integer_sqrt+0)
bx r12
.size imu_placement_sqrt, .-imu_placement_sqrt
`;

function samplePool(assembly) {
  const pattern = /^screen_pixel:\n[\s\S]*?^\.size screen_pixel, \.-screen_pixel$/gm;
  const matches = [...assembly.matchAll(pattern)];
  assert.equal(matches.length, 1, 'unexpected screen_pixel assembly');
  let sites = 0;
  const changed = assembly.replace(pattern, text => text.replace(
    /^movw (r\d+), #:lower16:\(sample\+0\)\nmovt \1, #:upper16:\(sample\+0\)$/gm,
    (_, reg) => { sites++; return `ldr.w ${reg}, .Limu_sample_pool`; })
    .replace(/^(pop(?:\.w)? \{[^}]*\bpc\}|bx lr)\n((?:@[^\n]*\n)*)\.size screen_pixel, \.-screen_pixel$/m,
      '$1\n$2.p2align 2\n.Limu_sample_pool:\n.word sample\n.size screen_pixel, .-screen_pixel'));
  assert(sites >= 2 && changed.includes('\n.Limu_sample_pool:\n'), 'no bounded shared sample pool');
  return { changed, sites };
}

const oracle = `#include <stdint.h>
extern uint32_t imu_placement_sqrt(uint32_t);
volatile uint32_t data_guard = 0x435a1234;
static const uint32_t seeds[] = {0, 1, 2, 3, 4, 63, 64, 65, 65535, 65536, 0x7fffffff, 0xffffffff};
void bare_main(void) {
    uint32_t failed = data_guard != 0x435a1234;
    for (unsigned n = 0; n < sizeof(seeds) / sizeof(seeds[0]); n++) {
        uint32_t input = seeds[n], got = imu_placement_sqrt(input);
        if (got > 65535 || (got && got > input / got) || got + 1 <= input / (got + 1))
            failed = 1;
    }
    const uint32_t result[2] = {0x20026, failed};
    register uint32_t command __asm__("r0") = 32;
    register const uint32_t *argument __asm__("r1") = result;
    __asm__ volatile("bkpt #0xab" : "+r"(command) : "r"(argument) : "memory");
    for (;;) {}
}
`;

function main(input, output) {
  const folder = path.resolve(input), out = path.resolve(output);
  const emit = (name, text) => fs.writeFileSync(path.join(out, name), text);
  const asmFile = path.join(folder, 'imu_demo.o.owned/assembly.s');
  const asm = fs.readFileSync(asmFile, 'utf8');
  const originalObject = readElf(path.join(folder, 'imu_demo.o'));
  const leaf = originalObject.sections[symbol(originalObject, 'integer_sqrt').section];
  assert.equal(leaf.name, '.text.integer_sqrt');
  assert(!originalObject.relocations.some(r => r.section === leaf.index));
  assert(originalObject.relocations.filter(r => r.symbol.name === 'integer_sqrt')
    .every(r => r.type === 10), 'unexpected square-root references');
  assert.equal([...asm.matchAll(sourceLine)].length, 1);
  const control = asm.replace(/^bl integer_sqrt$/gm, 'bl imu_placement_sqrt') + thunk;
  assert.notEqual(control.slice(0, asm.length), asm, 'square root must be reached by a call');
  const sram = control.replace(sourceLine, '.section .data.ram_code.integer_sqrt,"ax",%progbits');
  const pool = samplePool(asm);
  const linker = path.join(root, 'src/arch/rp2350/memmap_flash.ld');
  const objects = ['start', 'picobin', 'display', 'i2c_bitbang', 'lsm6ds3']
    .map(name => path.join(folder, `${name}.o`));
  fs.mkdirSync(out);
  const rows = {};
  for (const [name, text] of [['mov-pair', asm], ['literal', pool.changed], ['flash-thunk', control], ['sram-thunk', sram]]) {
    const p = path.join(out, name);
    emit(`${name}.s`, text);
    run(owned('badge-asm'), [`${p}.s`, '-o', `${p}.o`]);
    run(owned('badge-link'), ['-T', linker, ...objects, `${p}.o`, '-o', `${p}.elf`]);
    const elf = readElf(`${p}.elf`);
    assert.equal(run(owned('badge-elf-info'), ['-u', `${p}.elf`]).length, 0);
    run(owned('elf2bin'), ['-O', 'binary', `${p}.elf`, `${p}.bin`]);
    run(path.join(root, 'build/host-tools/bin2uf2'), [`${p}.bin`, `${p}.uf2`, '0x10000000', '0xe48bff59']);
    const bin = fs.readFileSync(`${p}.bin`);
    const uf2 = fs.readFileSync(`${p}.uf2`);
    verifyUf2(bin, uf2);
    const fn = symbol(elf, 'integer_sqrt'), address = fn.value & ~1;
    assert(fn.value & 1, 'Thumb function pointer');
    assert.deepEqual(body(elf, 'integer_sqrt'), body(originalObject, 'integer_sqrt'));
    const segment = elf.segments.find(s => address >= s.address && address + fn.size <= s.address + s.fileSize);
    assert(segment, 'square-root load segment');
    const load = segment.load + address - segment.address;
    assert.deepEqual(bin.subarray(load - flash, load - flash + fn.size), body(elf, 'integer_sqrt'));
    const dataBytes = symbol(elf, '__data_end').value - symbol(elf, '__data_start').value;
    if (name.endsWith('-thunk')) assertThunk(elf);
    if (name === 'sram-thunk') {
      assert(address >= ram && address + fn.size <= symbol(elf, '__data_end').value);
      assert.equal(segment.load, symbol(elf, '__data_source').value);
      assert(dataBytes - rows['flash-thunk'].dataBytes <= ramBudget);
    } else assert(address >= flash && address < ram);
    rows[name] = { flashBytes: bin.length, dataBytes, sqrtBytes: fn.size, sqrtAddress: address,
      sqrtLoadAddress: load, pixelFunctionBytes: symbol(elf, 'screen_pixel').size,
      elf: `${name}.elf`, uf2: `${name}.uf2`, uf2Sha256: createHash('sha256').update(uf2).digest('hex') };
  }
  assert.deepEqual(fs.readFileSync(path.join(out, 'mov-pair.bin')),
    fs.readFileSync(path.join(folder, 'imu_demo.bin')), 'replayed XIP baseline must match');
  assert.equal(rows.literal.pixelFunctionBytes, rows['mov-pair'].pixelFunctionBytes - pool.sites * 4 + 4 +
    (4 - (rows['mov-pair'].pixelFunctionBytes % 4)) % 4);
  assert(rows['sram-thunk'].dataBytes > rows['flash-thunk'].dataBytes);

  emit('oracle.c', oracle);
  const oracleScript = fs.readFileSync(linker, 'utf8')
    .replace('0x20000000', '0x30000000').replace('0x20080000', '0x800f0000')
    .replace('0x20081000', '0x800f1000');
  emit('oracle.ld', oracleScript);
  const cc = process.env.LLVM_CLANG ?? 'clang';
  run(cc, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz', '-ffreestanding',
    '-fno-builtin', '-ffunction-sections', '-fdata-sections', '-c', path.join(out, 'oracle.c'),
    '-o', path.join(out, 'oracle.o'), '-I', path.join(root, 'src/shared')]);
  for (const [name, text] of [['flash-thunk', control], ['sram-thunk', sram]]) {
    const elf = path.join(out, `${name}-oracle.elf`);
    const object = path.join(out, `${name}-leaf.o`);
    const leafSections = [...text.matchAll(sqrtSection)];
    assert.equal(leafSections.length, 1, 'missing oracle leaf');
    const source = path.join(out, `${name}-leaf.s`);
    fs.writeFileSync(source, `.syntax unified\n.cpu cortex-m33\n.thumb\n${leafSections[0][0]}\n${thunk}`);
    run(owned('badge-asm'), [source, '-o', object]);
    run(owned('badge-link'), ['-T', path.join(out, 'oracle.ld'),
      objects[0], object, path.join(out, 'oracle.o'), '-o', elf]);
    const linked = readElf(elf);
    assertThunk(linked);
    assert.deepEqual(body(linked, 'integer_sqrt'), body(originalObject, 'integer_sqrt'));
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', elf]);
    rows[name].qemuResetAndSqrt = 'pass';
  }
  const report = { poolSites: pool.sites, ramBudget, hardwareTimed: false, rows };
  emit('results.json', JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
}

try {
  if (process.argv.length !== 4)
    throw new Error('Usage: node badge-compiler/tools/imu-placement.mjs FIRMWARE_DIR NEW_OUTPUT_DIR');
  main(...process.argv.slice(2));
} catch (error) {
  console.error(`imu placement: ${error.message}`);
  process.exitCode = 1;
}
