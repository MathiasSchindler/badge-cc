import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import test from 'node:test';
import { orderHotText } from '../tools/hot-layout.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const run = (tool, args) => execFileSync(tool, args, { cwd: root, encoding: 'utf8', timeout: 30000 });

test('hot flash sections group in requested order without retaining unused functions', t => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'badge-hot-layout-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = name => path.join(directory, name);
  const original = fs.readFileSync(path.join(root, 'tests/fixtures/codegen_fixture.ld'), 'utf8');
  const script = orderHotText(original, ['hot_b', 'hot_a', 'unused']);
  assert.throws(() => orderHotText(original, ['*']), /without wildcards/);
  assert.throws(() => orderHotText('SECTIONS {}', ['hot_a']), /Expected one/);
  fs.writeFileSync(file('layout.ld'), script);
  fs.writeFileSync(file('fixture.s'), `.syntax unified
.thumb
.section .text.main,"ax",%progbits
.global main
.type main, %function
.thumb_func
main:
  push {lr}
  bl hot_a
  bl hot_b
  pop {pc}
.section .text.hot_a,"ax",%progbits
.type hot_a, %function
.thumb_func
hot_a:
  movs r0, #1
  bx lr
.section .text.hot_b,"ax",%progbits
.type hot_b, %function
.thumb_func
hot_b:
  subs r0, #1
  bx lr
.section .text.unused,"ax",%progbits
.type unused, %function
.thumb_func
unused:
  bx lr
`);
  run('build/badge-asm', ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run('build/badge-asm', [file('fixture.s'), '-o', file('fixture.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['--gc-sections', '-T', file('layout.ld'),
    file('start.o'), file('fixture.o'), '-o', file('layout.elf')]);
  const symbols = run(process.env.LLVM_NM ?? 'llvm-nm', ['-n', file('layout.elf')]);
  assert.match(symbols, /\bhot_b\n[^\n]*\bhot_a\n/);
  assert.doesNotMatch(symbols, /\bunused$/m);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('layout.elf')]);
});
