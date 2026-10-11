import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { crc32 } from './compiler-test-protocol.mjs';

export const root = fileURLToPath(new URL('../../', import.meta.url));
export const iterations = 4096;
export const samples = 7;
export const suite = 0x44303200;
export const requestAddress = 0x10fe0000;
export const responseAddress = 0x10ff0000;
export const modes = ['sram-warm', 'xip-warm', 'xip-cold-entry', 'sram-dma-same-bank', 'sram-dma-disjoint'];
const baseline = 'M33-TIMING-BASE-001';
const definition = (name, pattern, model, fact, extra = {}) =>
  ({ name, pattern, model, fact, repeats: 32, loopWidth: 2, loopAlignment: 0, dividend: 255, ...extra });
export const experiments = [
  definition('empty', [], '0u', 'M33-BRANCH-001', { repeats: 0 }),
  definition('alu-chain', ['add.w r3, r3, r7'], '(1u + count * 3u) ^ 1u', 'M33-TIMING-ALU-001'),
  definition('alu-parallel', ['add.w r3, r3, r7', 'add.w r4, r4, r7', 'add.w r5, r5, r7', 'add.w r6, r6, r7'],
    '(1u + count * 3u) ^ (3u + count * 3u) ^ (5u + count * 3u) ^ (7u + count * 3u)', 'M33-TIMING-ALU-001', { repeats: 8 }),
  definition('add-narrow', ['adds.n r3, #1'], '(1u + count) ^ 1u', 'THUMB-ADD-001'),
  definition('add-wide', ['add.w r3, r3, #1'], '(1u + count) ^ 1u', 'THUMB-ADD-002'),
  definition('shift-chain', ['add.w r3, r3, r7, lsl #5'], '(1u + count * 96u) ^ 1u', 'M33-TIMING-ALU-001'),
  definition('shift-independent', ['add.w r3, r7, r7, lsl #5'], '99u ^ 1u', 'M33-TIMING-ALU-001'),
  definition('shift-flags', ['adds.w r3, r7, r7, lsl #5'], '99u ^ 1u', 'M33-TIMING-ALU-001'),
  definition('mul-chain', ['mul r3, r3, r7'], 'd02_affine(1u, 3u, 0u, count) ^ 1u', 'M33-TIMING-MUL-001'),
  definition('mul-parallel', ['mul r3, r3, r7', 'mul r4, r4, r7', 'mul r5, r5, r7', 'mul r6, r6, r7'],
    'd02_affine(1u, 3u, 0u, count) ^ d02_affine(3u, 3u, 0u, count) ^ d02_affine(5u, 3u, 0u, count) ^ d02_affine(7u, 3u, 0u, count)', 'M33-TIMING-MUL-001', { repeats: 8 }),
  definition('mla-accumulator', ['mla r3, r7, r7, r3'], '(1u + count * 9u) ^ 1u', 'M33-TIMING-FWD-001'),
  definition('mla-multiplier', ['mla r3, r3, r7, r7'], 'd02_affine(1u, 3u, 3u, count) ^ 1u', 'M33-TIMING-MUL-001'),
  definition('mla-alternate', ['mla r3, r7, r7, r4', 'mla r4, r7, r7, r3'],
    '(3u + (count * 2u - 1u) * 9u) ^ (3u + count * 2u * 9u) ^ 2u', 'M33-TIMING-FWD-001', { repeats: 16 }),
  definition('umlal-accumulator', ['umlal r3, r4, r7, r7'], '(1u + count * 9u) ^ 1u', 'M33-TIMING-FWD-001'),
  ...[1, 3, 255, 65535, 0xffffffff].map(dividend =>
    definition(`divide-${dividend}`, ['udiv r3, r2, r7'], `${Math.floor(dividend / 3)}u ^ 1u`, 'M33-TIMING-DIV-001', { dividend })),
  definition('divide-use', ['udiv r3, r2, r7', 'add.w r4, r3, #1'],
    '85u ^ 86u ^ 2u', 'M33-TIMING-DIV-001'),
  definition('load-use', ['ldr.n r3, [r1]', 'add.w r4, r3, r7'], '7u', 'M33-TIMING-LOAD-001'),
  definition('load-independent', ['ldr.n r3, [r1]', 'add.w r4, r7, r7'], '7u', 'M33-TIMING-LOAD-001'),
  definition('ldm-first', ['ldmia.w r1, {r3-r6}', 'add.w r3, r3, r7'], '5u', 'M33-TIMING-LOAD-001'),
  definition('ldm-last', ['ldmia.w r1, {r3-r6}', 'add.w r3, r6, r7'], '5u', 'M33-TIMING-LOAD-001'),
  definition('load-store-data', ['ldr.n r3, [r1]', 'str.n r3, [r1, #32]'], '2u', 'M33-TIMING-STORE-001'),
  definition('load-store-address', ['ldr.n r3, [r1]', 'str.n r7, [r3]'], '0x2003e020u ^ 1u', 'M33-TIMING-STORE-001'),
  definition('literal', ['ldr.n r3, POOL'], '0x12345678u ^ 1u', 'THUMB-LITERAL-001'),
  definition('mov-pair', ['movw r3, #0x5678', 'movt r3, #0x1234'], '0x12345678u ^ 1u', 'THUMB-MOV-001'),
  definition('loop-narrow-half', [], '0u', 'M33-BRANCH-001', { repeats: 0, loopAlignment: 2 }),
  definition('loop-wide-word', [], '0u', 'M33-BRANCH-001', { repeats: 0, loopWidth: 4 }),
  definition('loop-wide-half', [], '0u', 'M33-BRANCH-001', { repeats: 0, loopWidth: 4, loopAlignment: 2 }),
  definition('conditional-taken', ['cmp.n r7, #0', 'bne.n NEXT', 'NEXT:', 'add.w r3, r3, #1'], '(1u + count) ^ 1u', 'M33-BRANCH-001'),
  definition('conditional-not-taken', ['cmp.n r7, #3', 'bne.n NEXT', 'NEXT:', 'add.w r3, r3, #1'], '(1u + count) ^ 1u', 'M33-BRANCH-001'),
  definition('fold-prev16', ['movs.n r3, #1', 'b.n NEXT', 'NEXT:', 'add.w r4, r4, #1'], '(3u + count) ^ 3u', 'M33-BRANCH-001'),
  definition('fold-prev32', ['mov.w r3, #1', 'b.n NEXT', 'NEXT:', 'adds.n r4, #1'], '(3u + count) ^ 3u', 'M33-BRANCH-001'),
  definition('nop-modern', ['nop.n', 'adds.n r3, #1'], '(1u + count) ^ 1u', 'M33-FOLD-001'),
  definition('nop-legacy', ['.inst.n 0x46c0', 'adds.n r3, #1'], '(1u + count) ^ 1u', 'M33-FOLD-001'),
  definition('signed-divide-positive', ['sdiv r3, r2, r7'], '85u ^ 1u', 'M33-TIMING-DIV-001'),
  definition('signed-divide-negative', ['sdiv r3, r2, r7'], '4294967211u ^ 1u', 'M33-TIMING-DIV-001', { dividend: 0xffffff01 }),
];
export const metadataRecords = 8;
export const reportBytes = (metadataRecords + experiments.length * modes.length) * 64;
export const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
export function execute(command, args, cwd = root) {
  return execFileSync(command, args, { cwd, encoding: 'utf8', timeout: 180000, maxBuffer: 16 * 1024 * 1024 });
}

export function assembly() {
  const lines = ['.syntax unified', '.cpu cortex-m33', '.thumb'];
  for (const placement of ['flash', 'ram']) {
    lines.push(`.section .measure_${placement},"ax",%progbits`);
    for (const [i, e] of experiments.entries()) {
      const name = `d02_${placement}_${i}`;
      lines.push('.p2align 2', `.global ${name}`, `.type ${name},%function`, '.thumb_func', `${name}:`,
        'push {r4-r7,lr}', 'movs r3,#1', 'movs r4,#3', 'movs r5,#5', 'movs r6,#7', 'movs r7,#3',
        '.p2align 2');
      if (e.loopAlignment) lines.push('nop.n');
      lines.push(`.global ${name}_loop`, `${name}_loop:`, `.global ${name}_body`, `${name}_body:`);
      for (let repeat = 0; repeat < e.repeats; repeat++) {
        for (const instruction of e.pattern) lines.push(instruction
          .replaceAll('POOL', `.L${name}_pool`).replaceAll('NEXT', `.L${name}_${repeat}`));
      }
      lines.push(`.global ${name}_body_end`, `${name}_body_end:`,
        e.loopWidth === 4 ? 'subs.w r0,r0,#1' : 'subs.n r0,#1',
        `bne.w ${name}_loop`, 'eor.w r0,r3,r4', 'eor.w r0,r0,r5', 'eor.w r0,r0,r6',
        'pop {r4-r7,pc}');
      if (e.name === 'literal') lines.push('.p2align 2', `.global ${name}_pool`, `${name}_pool:`, `.L${name}_pool:`, '.word 0x12345678');
      lines.push(`.global ${name}_end`, `${name}_end:`, `.size ${name},.-${name}`);
    }
  }
  lines.push('.section .text.d02_helpers,"ax",%progbits');
  for (const [name, body] of [
    ['d02_mask', ['mrs r0,primask', 'cpsid i']],
    ['d02_unmask', ['msr primask,r0']],
    ['d02_control', ['mrs r0,control']],
    ['d02_barrier', ['dsb', 'isb']],
  ]) lines.push(`.global ${name}`, `.type ${name},%function`, '.thumb_func', `${name}:`, ...body, 'bx lr', `.size ${name},.-${name}`);
  return `${lines.join('\n')}\n`;
}

export function header(build) {
  const lines = ['#ifndef D02_GENERATED_H', '#define D02_GENERATED_H',
    `#define D02_BUILD 0x${build.toString(16)}u`, `#define D02_CASES ${experiments.length}u`,
    `#define D02_SAMPLES ${samples}u`, `#define D02_ITERATIONS ${iterations}u`,
    `#define D02_METADATA ${metadataRecords}u`, `#define D02_BYTES ${reportBytes}u`,
    `#define D02_SUITE 0x${suite.toString(16)}u`,
    `#define D02_STORE_DATA ${experiments.findIndex(e => e.name === 'load-store-data')}u`,
    `#define D02_STORE_ADDRESS ${experiments.findIndex(e => e.name === 'load-store-address')}u`,
    'typedef uint32_t (*d02_function)(uint32_t, const uint32_t *, uint32_t);'];
  for (const placement of ['flash', 'ram'])
    for (const i of experiments.keys()) lines.push(`extern uint32_t d02_${placement}_${i}(uint32_t, const uint32_t *, uint32_t);`,
      `extern uint8_t d02_${placement}_${i}_end[];`);
  for (const placement of ['flash', 'ram']) {
    lines.push(`static d02_function const ${placement}_functions[] = {`,
      experiments.map((_, i) => `d02_${placement}_${i}`).join(', '), '};',
      `static uint8_t * const ${placement}_ends[] = {`,
      experiments.map((_, i) => `d02_${placement}_${i}_end`).join(', '), '};');
  }
  lines.push(`static uint32_t const dividends[] = {${experiments.map(e => `${e.dividend}u`).join(',')}};`,
    'static uint32_t d02_expected(uint32_t index) {', 'uint32_t count = 0u;',
    'switch (index) {');
  for (const [i, e] of experiments.entries()) lines.push(`case ${i}u: count = D02_ITERATIONS * ${e.repeats}u; return ${e.model};`);
  lines.push('default: return 0u;', '}', '}', '#endif');
  return `${lines.join('\n')}\n`;
}

export function request(build, nonce) {
  assert(Buffer.isBuffer(nonce) && nonce.length === 16 && nonce.some(x => x !== 0), 'nonce');
  const out = Buffer.alloc(64);
  out.writeUInt32LE(0x51363255); out[4] = 1; out[6] = 64;
  out.writeUInt32LE(build, 8); out.writeUInt32LE(suite, 12); nonce.copy(out, 16);
  out.writeUInt32LE(crc32(out.subarray(0, 60)), 60);
  return out;
}

export function decode(bytes, build, nonce) {
  assert.equal(bytes.length, reportBytes, 'report length');
  const readRecord = index => {
    const wire = bytes.subarray(index * 64, (index + 1) * 64);
    assert.equal(wire.readUInt32LE(0), 0x32423044, `record ${index}: commit`);
    assert.deepEqual([...wire.subarray(4, 8)], [1, 0, 64, 0], 'wire version');
    assert.equal(wire.readUInt32LE(8), build, 'build');
    assert.equal(wire.readUInt32LE(12), suite + index, 'record order');
    assert(wire.subarray(16, 32).equals(nonce), 'stale nonce');
    assert.equal(wire.readUInt32LE(60), crc32(wire.subarray(0, 60)), 'CRC');
    return Array.from({ length: 7 }, (_, i) => wire.readUInt32LE(32 + 4 * i));
  };
  const summary = readRecord(0);
  assert.equal(summary[0], 1, `device failure: status ${summary[0]}, detail 0x${summary[6].toString(16)}`);
  const records = Array.from({ length: reportBytes / 64 }, (_, index) => readRecord(index));
  assert.deepEqual(records[0], [1, experiments.length, modes.length, samples, iterations, 1, 0], 'device failure');
  const environment = records.slice(1, metadataRecords);
  assert.equal(environment[2][0], 1, 'timer is not clk_sys');
  assert.equal(environment[2][3], 0, 'timer paused');
  assert.deepEqual(environment[6].slice(0, 3), [1, 1, 0], 'clock drift or active DMA after measurement');
  assert.equal(environment[3][6], 0, 'not running on core 0');
  assert(environment[5].every(x => x <= 1000), 'timer overhead');
  const rows = records.slice(metadataRecords).map((values, index) => {
    const mode = modes[Math.floor(index / experiments.length)], experiment = experiments[index % experiments.length];
    assert(values.every(x => x >= 100 * Math.max(...environment[5]) && x < 0x40000000), `under-resolved/overlong ${mode}/${experiment.name}`);
    const sorted = [...values].sort((a, b) => a - b);
    return { mode, experiment: experiment.name, samples: values, min: sorted[0], median: sorted[3], max: sorted[6] };
  });
  return { environment, rows };
}

function symbols(nm) {
  return new Map(nm.trim().split('\n').map(line => {
    const fields = line.trim().split(/\s+/);
    return [fields.at(-1), parseInt(fields[0], 16) & ~1];
  }));
}

export function audit(nm, disassembly, definitions = experiments) {
  const sym = symbols(nm);
  const instructions = new Map();
  for (const line of disassembly.split('\n')) {
    const match = line.match(/^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}(?:[0-9a-f]{2})?\s+)+)\s*(\S.*)$/i);
    if (match) instructions.set(parseInt(match[1], 16), {
      bytes: match[2].trim().split(/\s+/).flatMap(x => x.length === 4 ? [x.slice(2), x.slice(0, 2)] : [x]),
      text: match[3].trim(),
    });
  }
  const audited = [];
  const normalize = op => op.split('@')[0].trim().toLowerCase()
    .replace(/\.w|\.n/g, '').replace(/^ldmia\b/, 'ldm')
    .replace(/r(\d+)-r(\d+)/g, (_, first, last) =>
      Array.from({ length: Number(last) - Number(first) + 1 }, (_, i) => `r${Number(first) + i}`).join(','))
    .replace(/#0x([0-9a-f]+)/g, (_, value) => `#${parseInt(value, 16)}`).replace(/\s+/g, '');
  for (const [i, e] of definitions.entries()) {
    const placements = [];
    for (const p of ['flash', 'ram']) {
      const name = `d02_${p}_${i}`;
      const address = sym.get(name), loop = sym.get(`${name}_loop`);
      assert(Number.isInteger(address) && Number.isInteger(loop), `missing ${name}`);
      assert.equal(address % 4, 0, 'entry alignment');
      assert.equal(loop % 4, e.loopAlignment, 'loop alignment');
      assert(address >= (p === 'flash' ? 0x10000000 : 0x20000000) &&
        address < (p === 'flash' ? 0x11000000 : 0x2003e000), 'placement');
      const start = sym.get(`${name}_body`), end = sym.get(`${name}_body_end`);
      const body = [...instructions].filter(([a]) => a >= start && a < end).map(([a, v]) => ({ address: a, ...v }));
      const expected = e.pattern.filter(x => !x.endsWith(':'));
      assert.equal(body.length, expected.length * e.repeats, `instruction count ${name}`);
      for (let j = 0; j < body.length; j++) {
        const op = expected[j % expected.length];
        const width = op.includes('.n') ? 2 : 4;
        assert.equal(body[j].bytes.length, width, `width ${name}: ${op}`);
        if (op.startsWith('.inst')) assert.equal(body[j].bytes.join(''), 'c046', 'legacy NOP');
        if (op.startsWith('nop')) assert.equal(body[j].bytes.join(''), '00bf', 'modern NOP');
        if (op.includes('NEXT')) {
          assert.equal(body[j].text.split(/\s/)[0].replace('.w', ''), op.split(/[.\s]/)[0], 'branch opcode');
          const target = body[j].text.match(/0x([0-9a-f]+)/i);
          assert(target && parseInt(target[1], 16) === body[j].address + 2, 'branch target');
        }
        if (op.includes('POOL')) {
          const pool = sym.get(`${name}_pool`), offset = pool - ((body[j].address + 4) & ~3);
          assert(offset >= 0 && offset <= 1020 && offset % 4 === 0, 'literal reach');
          const encoded = parseInt([...body[j].bytes].reverse().join(''), 16);
          assert.equal(encoded & 0xf800, 0x4800, 'literal encoding');
          assert.equal((encoded >> 8) & 7, 3, 'literal destination');
          assert.equal((encoded & 255) * 4, offset, 'literal offset');
          assert.deepEqual(instructions.get(pool)?.bytes, ['78', '56', '34', '12'], 'literal value');
        }
        if (!op.startsWith('.inst') && !op.includes('NEXT') && !op.includes('POOL'))
          assert.equal(normalize(body[j].text), normalize(op), `operands/opcode ${name}`);
      }
      assert.equal(instructions.get(end)?.bytes.length, e.loopWidth, 'loop control width');
      assert.equal(normalize(instructions.get(end).text), e.loopWidth === 4 ? 'subsr0,r0,#1' : 'subsr0,#1', 'counter operation');
      const back = instructions.get(end + e.loopWidth);
      assert.equal(back?.bytes.length, 4, 'counter branch width');
      assert(back.text.startsWith('bne.w') && parseInt(back.text.match(/0x([0-9a-f]+)/)[1], 16) === loop, 'counter branch target');
      placements.push({ placement: p, address, loop, end: sym.get(`${name}_end`), body });
    }
    assert.deepEqual(placements[0].body.map(x => x.bytes), placements[1].body.map(x => x.bytes), 'SRAM/XIP measured bytes differ');
    audited.push({ experiment: e.name, fact: e.fact, timing_context: baseline, placements });
  }
  return audited;
}

export function build(directory) {
  directory = path.resolve(directory);
  fs.mkdirSync(directory);
  const inputs = ['src/tools/datasheet_bench.c', 'src/arch/rp2350/start.c', 'src/arch/rp2350/picobin.S',
    'src/arch/rp2350/bootrom_reboot.c', 'src/arch/rp2350/memmap_datasheet.ld',
    'src/shared/usb_report.c', 'src/shared/usb_report.h', 'src/shared/stdint.h', 'src/shared/bytes.h',
    'src/tools/diagnostics_logic.c', 'src/tools/diagnostics_logic.h', 'src/arch/rp2350/bootrom_reboot.h',
    'badge-compiler/tests/fixtures/datasheet_experiments.ld', 'badge-compiler/tests/fixtures/codegen_start.S',
    'badge-compiler/scripts/owned-cc.sh', 'badge-compiler/scripts/owned-pipeline.sh',
    'badge-compiler/tools/compiler-test-protocol.mjs', 'badge-compiler/tools/datasheet-experiments.mjs'];
  const provenance = inputs.map(file => ({ file, sha256: sha(fs.readFileSync(path.join(root, file))) }));
  const buildId = parseInt(sha(Buffer.from(JSON.stringify(provenance))).slice(0, 8), 16);
  fs.writeFileSync(path.join(directory, 'experiments.S'), assembly());
  fs.writeFileSync(path.join(directory, 'd02_generated.h'), header(buildId));
  const cc = process.env.LLVM_CLANG || 'clang';
  const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-ffreestanding', '-fno-builtin',
    '-I'+path.join(root, 'src/shared'), '-I'+directory];
  for (const source of ['src/arch/rp2350/start.c', 'src/arch/rp2350/picobin.S']) {
    execute(cc, [...flags, '-Oz', '-c', source, '-o', path.join(directory, path.basename(source)+'.o')]);
  }
  execute(cc, [...flags, '-c', path.join(directory, 'experiments.S'), '-o', path.join(directory, 'experiments.o')]);
  const images = [];
  for (const compiler of ['llvm', 'owned']) {
    const objects = [];
    for (const source of ['src/tools/datasheet_bench.c', 'src/shared/usb_report.c',
      'src/tools/diagnostics_logic.c', 'src/arch/rp2350/bootrom_reboot.c']) {
      const object = path.join(directory, `${compiler}-${path.basename(source)}.o`);
      if (compiler === 'llvm') execute(cc, [...flags, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Oz',
        '-ffunction-sections', '-fdata-sections', '-c', source, '-o', object]);
      else execute('bash', ['badge-compiler/scripts/owned-cc.sh', ...flags, '-c', source, '-o', object]);
      objects.push(object);
    }
    const elf = path.join(directory, `${compiler}.elf`);
    execute(process.env.LLVM_LLD || 'ld.lld', ['--gc-sections', '-T', 'src/arch/rp2350/memmap_datasheet.ld',
      path.join(directory, 'start.c.o'), path.join(directory, 'picobin.S.o'), path.join(directory, 'experiments.o'),
      ...objects, '-o', elf]);
    const bin = path.join(directory, `${compiler}.bin`), uf2 = path.join(directory, `${compiler}.uf2`);
    execute(process.env.LLVM_OBJCOPY || 'llvm-objcopy', ['-O', 'binary', elf, bin]);
    execute(path.join(root, 'build/host-tools/bin2uf2'), [bin, uf2, '0x10000000', '0xe48bff59']);
    const disassembly = execute(process.env.LLVM_OBJDUMP || 'llvm-objdump', ['-d', elf]);
    const nm = execute(process.env.LLVM_NM || 'llvm-nm', ['--defined-only', elf]);
    fs.writeFileSync(path.join(directory, `${compiler}.disassembly`), disassembly);
    fs.writeFileSync(path.join(directory, `${compiler}.symbols`), nm);
    const audited = audit(nm, disassembly);
    fs.writeFileSync(path.join(directory, `${compiler}.audit.json`), JSON.stringify(audited, null, 2)+'\n');
    const oracle = path.join(directory, `${compiler}-oracle.o`);
    if (compiler === 'llvm') execute(cc, [...flags, '-DD02_ORACLE', '-Oz', '-c', 'src/tools/datasheet_bench.c', '-o', oracle]);
    else execute('bash', ['badge-compiler/scripts/owned-cc.sh', ...flags, '-DD02_ORACLE', '-c', 'src/tools/datasheet_bench.c', '-o', oracle]);
    execute(cc, [...flags, '-c', 'badge-compiler/tests/fixtures/codegen_start.S', '-o', path.join(directory, 'oracle-start.o')]);
    const oracleElf = path.join(directory, `${compiler}-oracle.elf`);
    execute(process.env.LLVM_LLD || 'ld.lld', ['-T', 'badge-compiler/tests/fixtures/datasheet_experiments.ld',
      path.join(directory, 'oracle-start.o'), path.join(directory, 'experiments.o'), oracle, '-o', oracleElf]);
    execute(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', oracleElf]);
    images.push({ compiler, elf, uf2, sha256: sha(fs.readFileSync(uf2)), audit: `${compiler}.audit.json`,
      elf_sha256: sha(fs.readFileSync(elf)), audit_sha256: sha(Buffer.from(JSON.stringify(audited, null, 2)+'\n')),
      qemu_correctness: 'passed', oracle_sha256: sha(fs.readFileSync(oracleElf)) });
  }
  const manifest = { schema: 1, build: buildId, iterations, samples, modes, experiments, images, provenance,
    clang: execute(cc, ['--version']), reportBytes,
    owned_tools: Object.fromEntries(['badge-cpp', 'badge-parse', 'badge-sema', 'badge-ir', 'badge-opt',
      'badge-codegen', 'badge-asm', 'badge-object'].map(name =>
      [name, sha(fs.readFileSync(path.join(root, 'badge-compiler/build', name)))])) };
  fs.writeFileSync(path.join(directory, 'manifest.json'), JSON.stringify(manifest, null, 2)+'\n');
  return manifest;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    assert(process.argv.length === 4 && process.argv[2] === 'build', 'usage: datasheet-experiments.mjs build NEW_DIRECTORY');
    console.log(JSON.stringify(build(process.argv[3]), null, 2));
  } catch (error) {
    console.error(`datasheet experiments: ${error.message}`); process.exitCode = 1;
  }
}
