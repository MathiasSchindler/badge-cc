import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { verify } from './capture.mjs';
import { compareObjects, readObject } from '../badge-compiler/tools/assembler-crossover.mjs';
import { verifyUf2 } from '../badge-compiler/tools/benchmark-c06.mjs';
import { decodeElf } from '../badge-compiler/tools/boundary-evidence.mjs';
import { disassemblyFunctions } from '../badge-compiler/tools/backend-attribution.mjs';

const root = fileURLToPath(new URL('..', import.meta.url));
const names = ['arithmetic', 'control', 'crc', 'memory', 'calls', 'fixed-point'];
const extended = [...names, 'rotate', 'bit-insert', 'packed-dot', 'saturate', 'divmod'];
const current = [...extended, 'byte-swap', 'extend-add', 'parity', 'nibble-map', 'wide-sum'];
const composed = [...current, 'blend-span', 'sparse-scan', 'ring-checksum', 'filter-update', 'byte-parser'];
const pipelines = [...composed, 'tile-pipeline', 'stencil-tile', 'record-dispatch'];
const seeds = [0, 1, 0x12345678, 0x8000ffff, 0xffffffff];
const validationSeeds = [
  ...seeds, 0x7fffffff, 0x80000000, 0x80008000, 0x7fff7fff, 0x00007f00,
  0x00008000, 0x00ff7f00, 0x00ff8000, 2, 3, 4, 5, 6, 7,
  0xffffffdf, 0xffffffe0, 0xffffffe1
];
for (let x = 0x9e3779b9; validationSeeds.length < 256;) {
  x = (Math.imul(x, 1664525) + 1013904223) >>> 0;
  validationSeeds.push(x);
}
const tools = {
  cc: process.env.HOST_CC ?? 'cc',
  clang: process.env.LLVM_CLANG ?? 'clang',
  lld: process.env.LLVM_LLD ?? 'ld.lld',
  objdump: process.env.LLVM_OBJDUMP ?? 'llvm-objdump',
  qemu: process.env.QEMU_ARM ?? 'qemu-system-arm',
};
const run = (tool, args) => execFileSync(tool, args, { cwd: root, encoding: 'utf8', timeout: 60000 });
const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
const fixture = 'badge-compiler/tests/fixtures';
const commonObjects = ['start_red_square.o', 'micro_boot.o', 'picobin.o'];
const parseElf = file => decodeElf(fs.readFileSync(file));

function reference(source, tmp) {
  const driver = path.join(tmp, 'reference.c'), program = path.join(tmp, 'reference');
  fs.writeFileSync(driver, `#include <stdint.h>
#include <stdio.h>
uint32_t micro_kernel(uint32_t);
int main(void) {
${validationSeeds.map(seed => `    printf("%08x\\n", (unsigned)micro_kernel(${seed}u));`).join('\n')}
    return 0;
}
`);
  run(tools.cc, ['-std=c11', '-O0', '-Wall', '-Wextra', '-Werror', source, driver, '-o', program]);
  const expected = run(program, []).trim().split('\n');
  assert.equal(expected.length, validationSeeds.length);
  assert(expected.every(line => /^[0-9a-f]{8}$/.test(line)));
  return expected;
}

function oracle(inputs, expected, tmp, name) {
  const move = (reg, value) => `movw ${reg}, #${value & 65535}\nmovt ${reg}, #${value >>> 16}`;
  const source = path.join(tmp, `${name}.s`), object = path.join(tmp, `${name}.o`);
  fs.writeFileSync(source, `.syntax unified
.thumb
.text
.global main
.type main, %function
.thumb_func
main:
push {r4, lr}
${inputs.map((seed, i) => `${move('r0', seed)}
bl micro_kernel
${move('r4', Number.parseInt(expected[i], 16))}
cmp r0, r4
bne .Lfailed`).join('\n')}
movs r0, #0
pop {r4, pc}
.Lfailed:
movs r0, #1
pop {r4, pc}
.size main, .-main
`);
  run(tools.clang, [...target, '-c', source, '-o', object]);
  return object;
}

function kernelStats(elf, cNames) {
  const functions = parseElf(elf).symbols.filter(s => s.type === 2 && cNames.has(s.name));
  const disassembly = disassemblyFunctions(run(tools.objdump,
    ['-d', '--triple=thumbv8m.main-none-eabi', '--mcpu=cortex-m33', elf]));
  const instructions = disassembly.flatMap(f => f.instructions);
  const data = disassembly.flatMap(f => f.data);
  return functions.map(s => {
    const start = s.value & ~1, end = start + s.size;
    const inside = row => row.address >= start && row.address + row.bytes <= end;
    const decoded = instructions.filter(inside), literals = data.filter(inside);
    const bytes = rows => rows.reduce((sum, row) => sum + row.bytes, 0);
    assert(decoded.length, `missing decoded instructions: ${s.name}`);
    assert(bytes(decoded) + bytes(literals) <= s.size, `decoded bytes outside ${s.name}`);
    return { name: s.name, extent_bytes: s.size, instruction_bytes: bytes(decoded),
      literal_bytes: bytes(literals), instructions: decoded.length };
  });
}

function execute(object, start, driver, cNames, tmp, countInstructions = true) {
  const elf = path.join(tmp, 'oracle.elf'), trace = path.join(tmp, 'execution.log');
  run(tools.lld, ['--gc-sections', '-T', `${fixture}/tilt_maze_fixture.ld`, start, driver, object, '-o', elf]);
  run(tools.qemu, ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
    '-semihosting-config', 'enable=on,target=native', '-kernel', elf,
    ...(countInstructions ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', trace] : [])]);
  if (!countInstructions) return;
  const functions = parseElf(elf).symbols.filter(s => s.type === 2 && cNames.has(s.name))
    .map(s => ({ start: s.value & ~1, end: (s.value & ~1) + s.size }));
  const reached = [...fs.readFileSync(trace, 'utf8').matchAll(/^Trace .*?\[[0-9a-f]+\/([0-9a-f]+)\//gm)]
    .map(([, pc]) => Number.parseInt(pc, 16))
    .filter(pc => functions.some(s => pc >= s.start && pc < s.end)).length;
  assert(reached > 0, 'no reached kernel instructions in the QEMU trace');
  return reached;
}

function main(directory) {
  const report = verify(directory), file = rel => path.join(directory, rel);
  const programsToCheck = [names, extended, current, composed, pipelines].find(group => group.length === report.modules.length);
  assert(programsToCheck, 'capture must contain a complete microbenchmark collection');
  assert.equal(report.modules.length, programsToCheck.length);
  assert.deepEqual(new Set(report.projects.map(p => p.name)), new Set(programsToCheck.map(n => `micro-${n}`)));
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'compile-graph-micro-'));
  try {
    const oracleStart = path.join(tmp, 'start.o');
    run(tools.clang, [...target, '-c', `${fixture}/codegen_start.S`, '-o', oracleStart]);
    const programs = programsToCheck.map(name => {
      const project = `micro-${name}`;
      const module = report.modules.find(m => m.id === `${project}.o`);
      assert(module, `missing module: ${project}`);
      assert.equal(module.preprocessing_comparison.status, 'identical', `${project}: C tokens differ`);
      const expected = reference(file(module.roles.owned.stages.source.file), tmp);
      const validation = oracle(validationSeeds, expected, tmp, 'validation');
      const driver = oracle(seeds, expected.slice(0, seeds.length), tmp, 'timing');
      const images = Object.fromEntries(['owned', 'llvm'].map(role => [role,
        report.images.find(i => i.name === project && i.role === role)]));
      const bootBytes = commonObjects.reduce((sum, basename) => {
        const objects = ['owned', 'llvm'].map(role => {
          const elf = file(images[role].elf.file);
          return readObject(path.join(path.dirname(elf), basename));
        });
        const check = compareObjects(...objects);
        assert(check.raw_allocated_equal && check.relocations_equal, `different boot inputs: ${basename}`);
        return sum + objects[0].sections.filter(s => (s.flags & 2) && s.type !== 8)
          .reduce((total, s) => total + s.size, 0);
      }, 0);
      const roles = Object.fromEntries(Object.entries(images).map(([role, img]) => {
        const elf = file(img.elf.file), object = file(module.roles[role].object.file);
        const cNames = new Set(parseElf(object).symbols.filter(s => s.type === 2 && s.section).map(s => s.name));
        const functions = kernelStats(elf, cNames);
        assert(functions.some(f => f.name === 'micro_kernel'));
        const uf2 = path.join(path.dirname(elf), '../uf2', `${project}.uf2`);
        const payload = fs.readFileSync(file(img.binary.file)), container = fs.readFileSync(uf2);
        verifyUf2(payload, container);
        assert.equal(img.ram_bytes, 0, `${project} must not need startup data or BSS`);
        const extent = functions.reduce((sum, f) => sum + f.extent_bytes, 0);
        const remaining = payload.length - bootBytes - extent;
        assert(remaining >= 0, 'function and boot extents exceed flash bytes');
        execute(object, oracleStart, validation, cNames, tmp, false);
        return [role, { flash_bytes: payload.length, uf2_bytes: container.length,
          c_function_bytes: extent, other_flash_bytes: remaining, functions,
          qemu_kernel_instructions: execute(object, oracleStart, driver, cNames, tmp) }];
      }));
      return { name: project, shared_boot_input_bytes: bootBytes,
        expected: expected.slice(0, seeds.length), ...roles };
    });
    return { scope: 'RP2350 ELF/UF2 and Cortex-M33 QEMU execution; not physical timing',
      seeds, validation_seed_count: validationSeeds.length, programs };
  } finally { fs.rmSync(tmp, { recursive: true }); }
}

try {
  assert.equal(process.argv.length, 3, 'Usage: node compile-graph/check-micro.mjs REPORT_DIRECTORY');
  console.log(JSON.stringify(main(path.resolve(process.argv[2])), null, 2));
} catch (error) {
  console.error(`check-micro: ${error.message}`);
  process.exitCode = 1;
}
