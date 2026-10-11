import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tool = name => path.join(root, 'badge-compiler/build', name);
function run(command, args, selection) {
  const env = { ...process.env };
  if (selection) env.BADGE_CODEGEN_SCALAR_SELECTION = selection;
  const result = spawnSync(command, args, { cwd: root, env, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${command}: ${result.stderr}`);
}

test('LLVM and owned Cortex-M33 execute the exact square/panel oracle; startup bytes agree', t => {
  const directory = fs.mkdtempSync(path.join(root, 'build/red-square-oracle-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = name => path.join(directory, name);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
    '-Oz', '-ffreestanding', '-fno-builtin', '-Isrc/shared'];
  run(clang, [...flags, '-DRED_SQUARE_TARGET_TEST', '-c', 'tests/red_square_test.c',
    '-o', file('oracle.o')]);
  run(tool('badge-asm'), ['badge-compiler/tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const compiler of ['llvm', 'owned', 'owned-constants']) {
    const selection = compiler === 'owned-constants' ? 'thumb-constants' : undefined;
    const object = file(`${compiler}.o`);
    run(compiler === 'llvm' ? clang : 'bash',
      [...(compiler === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
        ...flags, '-DRED_SQUARE_TEST', '-c', 'src/tools/red_square.c', '-o', object], selection);
    run(process.env.LLVM_LLD ?? 'ld.lld',
      ['-T', 'badge-compiler/tests/fixtures/tilt_maze_fixture.ld', file('start.o'),
        file('oracle.o'), object, '-o', file(`${compiler}.elf`)]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm',
      ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
        '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${compiler}.elf`)]);
    run(compiler === 'llvm' ? clang : 'bash',
      [...(compiler === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
        '--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
        '-c', 'src/arch/rp2350/start_red_square.S', '-o', file(`${compiler}-startup.o`)], selection);
    run(process.env.LLVM_OBJCOPY ?? 'llvm-objcopy',
      ['--dump-section', `.text.bare_reset=${file(`${compiler}-startup.bin`)}`,
        file(`${compiler}-startup.o`)]);
  }
  assert.deepEqual(fs.readFileSync(file('llvm-startup.bin')), fs.readFileSync(file('owned-startup.bin')));
  assert.deepEqual(fs.readFileSync(file('llvm-startup.bin')),
    fs.readFileSync(file('owned-constants-startup.bin')));
});

test('compact initialization retains a compiler-independent 150 ms minimum at 150 MHz', t => {
  const directory = fs.mkdtempSync(path.join(root, 'build/red-square-delay-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = name => path.join(directory, name);
  const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
    '-Oz', '-ffreestanding', '-fno-builtin', '-Isrc/shared'];
  run(process.env.LLVM_CLANG ?? 'clang', [...flags, '-S', 'src/tools/red_square.c', '-o', file('llvm.s')]);
  run('bash', ['badge-compiler/scripts/owned-cc.sh', ...flags,
    '-c', 'src/tools/red_square.c', '-o', file('owned.o')]);
  run('bash', ['badge-compiler/scripts/owned-cc.sh', ...flags,
    '-c', 'src/tools/red_square.c', '-o', file('constants.o')], 'thumb-constants');
  for (const assembly of [file('llvm.s'), file('owned.o.owned/assembly.s'),
    file('constants.o.owned/assembly.s')])
    assert.match(fs.readFileSync(assembly, 'utf8'), /(?:^\s*nop(?:\.n)?\s*\n){6}/m);
  assert(4000000 * 6 >= 150000000 * 0.150);
});
