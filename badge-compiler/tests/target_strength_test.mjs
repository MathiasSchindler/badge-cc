import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const oracle = process.env.BADGE_TEST_MODE !== 'owned';
const tool = name => path.join(root, 'build', name);
const clang = process.env.LLVM_CLANG || 'clang';
const lld = process.env.LLVM_LLD || 'ld.lld';
function invoke(command, args, success = true, timeout = 30000) {
  const r = spawnSync(command, args, { encoding: 'utf8', timeout });
  assert.ifError(r.error);
  if (success) assert.equal(r.status, 0, r.stderr);
  else assert.notEqual(r.status, 0, 'expected explicit failure');
  return r;
}
function fixture(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/target-strength-test-'));
  t.after(() => {
    for (const name of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, name));
    fs.rmdirSync(dir);
  });
  return name => path.join(dir, name);
}
const target = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb'];
function matrix() {
  let ir = 'target triple = "thumbv8m.main-unknown-none-eabi"\ntarget datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n', caller = '';
  for (let shift = 0; shift < 32; shift++) {
    const d = 2 ** shift;
    for (const [name, op] of [['q', 'udiv'], ['r', 'urem']]) {
      ir += `define i32 @${name}${shift}(i32 %x) {\nentry:\n%v = ${op} i32 %x, ${d}\nret i32 %v\n}\n`;
      caller += `extern unsigned ${name}${shift}(unsigned);\n`;
    }
    for (const [name, first, second] of [['p', 'udiv', 'urem'], ['b', 'urem', 'udiv']]) {
      ir += `define i32 @${name}${shift}(i32 %x) {\nentry:\n%a = ${first} i32 %x, ${d}\n%b = ${second} i32 %x, ${d}\n%v = xor i32 %a, %b\nret i32 %v\n}\n`;
      caller += `extern unsigned ${name}${shift}(unsigned);\n`;
    }
  }
  ir += 'define i32 @opaque(i32 %x) {\nentry:\n%v = xor i32 %x, 12345\nret i32 %v\n}\n';
  ir += 'define i32 @flow(i32 %x, ptr %cell, i32 %n) {\nentry:\n%old = load volatile i32, ptr %cell, align 4\n';
  for (let k = 0; k < 8; k++) ir += `%a${k} = udiv i32 %x, ${2 ** (k + 1)}\n`;
  ir += '%call = call i32 @opaque(i32 %x)\n%s0 = add i32 %call, %a0\n';
  for (let k = 1; k < 8; k++) ir += `%s${k} = add i32 %s${k - 1}, %a${k}\n`;
  ir += '%initial = xor i32 %s7, %old\nbr label %loop\nloop:\n';
  ir += '%v = phi i32 [ %initial, %entry ], [ %next, %loop ]\n%i = phi i32 [ 0, %entry ], [ %step, %loop ]\n';
  ir += '%q = udiv i32 %v, 8\n%r = urem i32 %v, 8\n%sum = add i32 %q, %r\n%next = add i32 %sum, %i\n';
  ir += 'store volatile i32 %next, ptr %cell, align 4\n%step = add i32 %i, 1\n%done = icmp uge i32 %step, %n\nbr i1 %done, label %end, label %loop\nend:\nret i32 %next\n}\n';
  caller += 'extern unsigned flow(unsigned, volatile unsigned *, unsigned);\nint main(void) {\nunsigned seed = 1;\n';
  for (let shift = 0; shift < 32; shift++) {
    const d = 2 ** shift;
    caller += `{\nvolatile unsigned divisor = ${d}u;\n`;
    caller += 'for (unsigned i = 0; i < 2048; i++) {\nseed = seed * 1664525u + 1013904223u;\n';
    caller += `unsigned x = i == 0 ? 0 : i == 1 ? ~0u : i == 2 ? ${d - 1}u : i == 3 ? ${d}u : i == 4 ? 0x80000000u : seed;\n`;
    caller += `unsigned q = x / divisor, r = x % divisor;\nif (q${shift}(x) != q || r${shift}(x) != r || p${shift}(x) != (q ^ r) || b${shift}(x) != (q ^ r)) return 1;\n}\n}\n`;
  }
  caller += 'for (unsigned i = 0; i < 512; i++) {\nseed = seed * 1664525u + 1013904223u;\nvolatile unsigned cell = seed ^ 55u;\nunsigned expected = seed ^ 12345u;\n';
  for (let k = 0; k < 8; k++) caller += `expected += seed / ${2 ** (k + 1)}u;\n`;
  caller += 'expected ^= cell;\nunsigned count = i % 5, rounds = count ? count : 1;\nfor (unsigned j = 0; j < rounds; j++) expected = expected / 8u + expected % 8u + j;\n';
  caller += 'if (flow(seed, &cell, count) != expected || cell != expected) return 2;\n}\n';
  return { ir, caller: caller + 'return 0;\n}\n' };
}

test('target strength reduction is deterministic, LLVM-valid and preserves work-failure output', t => {
  const file = fixture(t), { ir } = matrix();
  fs.writeFileSync(file('input.ll'), ir);
  for (const name of ['badge-opt', 'badge-opt-sanitize']) {
    invoke(tool(name), ['--passes=target-strength-reduce', file('input.ll'), '-o', file(`${name}.ll`)]);
  }
  assert.deepEqual(fs.readFileSync(file('badge-opt.ll')), fs.readFileSync(file('badge-opt-sanitize.ll')));
  const result = fs.readFileSync(file('badge-opt.ll'), 'utf8');
  assert.doesNotMatch(result, /\b(?:udiv|urem)\b/);
  if (oracle)
    invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('badge-opt.ll')]);
  fs.writeFileSync(file('preserved.ll'), 'preserved\n');
  const failure = invoke(tool('badge-opt-strength-budget'),
    ['--passes=target-strength-reduce', file('input.ll'), '-o', file('preserved.ll')], false);
  assert.match(failure.stderr, /target-strength-reduce work limit exceeded/);
  assert.equal(fs.readFileSync(file('preserved.ll'), 'utf8'), 'preserved\n');
});

test('power-of-two matrix removes divider work and executes across all residency policies', t => {
  const file = fixture(t), { ir, caller } = matrix();
  fs.writeFileSync(file('input.ll'), ir);
  fs.writeFileSync(file('caller.c'), caller);
  fs.writeFileSync(file('size.ld'), fs.readFileSync(path.join(root, 'tests/fixtures/link_oracle_fixture.ld'),
    'utf8').replace('ENTRY(entry)', 'ENTRY(q0)'));
  invoke(tool('badge-opt'), ['--passes=target-strength-reduce', file('input.ll'), '-o', file('candidate.ll')]);
  if (oracle) {
    invoke(clang, [...target, '-ffreestanding', '-fno-builtin', '-Oz', '-c', file('caller.c'), '-o', file('caller.o')]);
    invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  }
  for (const residency of ['stack', 'local', 'global', 'global-flex', 'global-compact']) {
    for (const selection of ['legacy', 'thumb-layout']) {
      let oldSize = 0, newSize = 0;
      for (const side of ['input', 'candidate']) {
        invoke(tool('badge-codegen'), [`--register-residency=${residency}`, `--scalar-selection=${selection}`,
          file(`${side}.ll`), '-o', file(`${side}.s`)]);
        const assembly = fs.readFileSync(file(`${side}.s`), 'utf8');
        if (side === 'candidate') assert.doesNotMatch(assembly, /\b(?:udiv|mls)\b/);
        else assert.match(assembly, /\budiv\b/);
        invoke(tool('badge-asm'), [file(`${side}.s`), '-o', file(`${side}.o`)]);
        invoke(tool('badge-link'), ['-T', file('size.ld'),
          '--no-gc-sections', '-o', file(`${side}-size.elf`), file(`${side}.o`)]);
        invoke(tool('elf2bin'), ['-O', 'binary', file(`${side}-size.elf`), file(`${side}.bin`)]);
        if (side === 'input') oldSize = fs.statSync(file(`${side}.bin`)).size;
        else newSize = fs.statSync(file(`${side}.bin`)).size;
      }
      assert(newSize <= oldSize, `${residency}/${selection}: text growth ${oldSize} -> ${newSize}`);
      if (oracle) {
        invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
          file('start.o'), file('caller.o'), file('candidate.o'), '-o', file('execute.elf')]);
        invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
          '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
          '-kernel', file('execute.elf')], true, 120000);
      }
    }
  }
  if (oracle) {
    invoke(clang, [...target, '-Oz', '-c', '-x', 'ir', file('candidate.ll'), '-o', file('llvm.o')]);
    invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
      file('start.o'), file('caller.o'), file('llvm.o'), '-o', file('llvm.elf')]);
    invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('llvm.elf')], true, 120000);
  }
});
