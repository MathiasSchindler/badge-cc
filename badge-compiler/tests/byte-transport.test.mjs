import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { decodeElf } from '../tools/boundary-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
function run(command, args) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(r.error);
  assert.equal(r.status, 0, r.stderr);
  return r.stdout;
}
const header = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
  'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function stream(name, offsets, extra = '', args = 'i32 %x') {
  let text = `define void @${name}(${args}) {\nentry:\n`;
  for (let n = 0; n < offsets.length; n++) {
    const value = n === 1 ? '%x' : n === 0 ? '255' : '1073741824';
    text += `%p${n} = inttoptr i32 ${0x30002000 + offsets[n]} to ptr\n` +
      `store volatile i32 ${value}, ptr %p${n}, align 4\n`;
    if (n === 2) text += 'call void asm sideeffect "nop\\0Anop\\0Anop", ""()\n';
  }
  return text + extra + 'ret void\n}\n';
}
test('shared absolute-store bases preserve values, store order, NOPs and leaf ABI', t => {
  const directory = fs.mkdtempSync(path.join(root, 'build/byte-transport-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = name => path.join(directory, name);
  fs.writeFileSync(file('input.ll'), header +
    stream('stream', [36, 28, 32, 24]) +
    stream('boundary', [0, 124]) +
    stream('distant', [0, 128]) +
    stream('two_arguments', [36, 28], '', 'i32 %x, i32 %y') +
    stream('opaque', [36, 28], 'call void asm sideeffect "dsb sy", "~{memory}"()\n') +
    stream('called', [36, 28], 'call void @effect()\n'));
  fs.writeFileSync(file('oracle.c'), `
extern void stream(unsigned), boundary(unsigned), distant(unsigned),
  two_arguments(unsigned, unsigned), opaque(unsigned), called(unsigned);
extern unsigned check_stream(unsigned);
static volatile unsigned effects;
void effect(void) { effects++; }
int main(void) {
  volatile unsigned *p = (volatile unsigned *)0x30002000;
  const unsigned values[] = {0, 1, 255, 256, 0x80000000u, 0xffffffffu};
  unsigned i;
  for (i = 0; i < 6; i++) {
    unsigned x = values[i];
    if (check_stream(x)) return 7;
    if (p[9] != 255 || p[7] != x || p[8] != 1073741824u || p[6] != 1073741824u)
      return 1;
    boundary(x); if (p[0] != 255 || p[31] != x) return 2;
    distant(x); if (p[0] != 255 || p[32] != x) return 3;
    two_arguments(x, 17); if (p[9] != 255 || p[7] != x) return 4;
    opaque(x); if (p[9] != 255 || p[7] != x) return 5;
    called(x); if (p[9] != 255 || p[7] != x) return 6;
  }
  return effects != 6;
}`);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-Oz', '-ffreestanding', '-fno-builtin', '-c', file('oracle.c'), '-o', file('oracle.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  fs.writeFileSync(file('abi.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
.global check_stream
.thumb_func
check_stream:
push.w {r3-r11, lr}
movw r4, #44
movw r5, #55
movw r6, #66
movw r7, #77
movw r8, #88
movw r9, #99
movw r10, #110
movw r11, #121
bl stream
cmp r4, #44
bne failed
cmp r5, #55
bne failed
cmp r6, #66
bne failed
cmp r7, #77
bne failed
cmp r8, #88
bne failed
cmp r9, #99
bne failed
cmp r10, #110
bne failed
cmp r11, #121
bne failed
movw r0, #0
b done
failed:
movw r0, #1
done:
pop.w {r3-r11, pc}
`);
  run(tool('badge-asm'), [file('abi.s'), '-o', file('abi.o')]);
  const results = [];
  const instructions = [];
  for (const policy of ['thumb-widths', 'thumb-constants']) {
    run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`,
      file('input.ll'), '-o', file(`${policy}.s`)]);
    const assembly = fs.readFileSync(file(`${policy}.s`), 'utf8');
    const functions = assembly.split(/(?=\.section \.text\.)/).slice(1);
    if (policy === 'thumb-constants') {
      for (const [n, text] of functions.entries())
        assert.equal(text.includes('@ absolute-store:'), n < 2);
      assert(!functions[0].includes('push'));
      assert.equal((functions[0].match(/^str\./gm) ?? []).length, 4);
      assert.deepEqual([...functions[0].matchAll(/^str\.\w+ r\d+, \[r1, #(\d+)\]/gm)]
        .map(match => Number(match[1])), [12, 4, 8, 0]);
      assert.match(functions[0], /nop\nnop\nnop/);
    }
    run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
    run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
      '-mthumb', '-c', file(`${policy}.s`), '-o', file(`${policy}-llvm.o`)]);
    const code = name => decodeElf(fs.readFileSync(file(name))).sections
      .filter(s => s.flags & 4).map(s => s.data);
    assert.deepEqual(code(`${policy}.o`), code(`${policy}-llvm.o`));
    results.push(code(`${policy}.o`).reduce((n, b) => n + b.length, 0));
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
      file('start.o'), file('oracle.o'), file('abi.o'), file(`${policy}.o`), '-o', file(`${policy}.elf`)]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file(`${policy}.elf`), '-accel', 'tcg,one-insn-per-tb=on',
      '-d', 'exec,nochain', '-D', file(`${policy}.trace`)]);
    instructions.push((fs.readFileSync(file(`${policy}.trace`), 'utf8').match(/^Trace /gm) ?? []).length);
  }
  assert(results[1] < results[0]);
  assert(instructions[0] > 0 && instructions[1] < instructions[0]);
  console.log(`RS02 reached oracle instructions ${instructions[0]} -> ${instructions[1]}`);
});
