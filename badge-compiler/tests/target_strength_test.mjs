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
const irTarget = 'target triple = "thumbv8m.main-unknown-none-eabi"\ntarget datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n';
function matrix() {
  let ir = irTarget, caller = '';
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

test('signed quotient and remainder share a divide through a duplicate extension', t => {
  const file = fixture(t);
  fs.writeFileSync(file('input.ll'), irTarget + `
define i32 @pair(i16 signext %x, i32 %y) {
entry:
  %nx = sext i16 %x to i32
  %r = srem i32 %nx, %y
  %xx = sext i16 %x to i32
  %q = sdiv i32 %xx, %y
  %out = xor i32 %q, %r
  ret i32 %out
}
define i32 @different(i32 %x, i32 %y, i32 %z) {
entry:
  %r = srem i32 %x, %y
  %q = sdiv i32 %x, %z
  %out = xor i32 %q, %r
  ret i32 %out
}
define i32 @barrier(i32 %x, i32 %y, ptr %p) {
entry:
  %r = srem i32 %x, %y
  store volatile i32 %r, ptr %p, align 4
  %q = sdiv i32 %x, %y
  ret i32 %q
}
define i32 @literal_pair(i32 %n) {
entry:
  %x = xor i32 %n, 1
  %q = sdiv i32 %x, 60
  %shifted = xor i32 %q, 7
  %r = srem i32 %x, 60
  %out = xor i32 %shifted, %r
  ret i32 %out
}
define i32 @literal_near_miss(i32 %n) {
entry:
  %x = xor i32 %n, 1
  %q = sdiv i32 %x, 60
  %shifted = xor i32 %q, 7
  %r = srem i32 %x, 61
  %out = xor i32 %shifted, %r
  ret i32 %out
}
`);
  invoke(tool('badge-opt-sanitize'), ['--passes=divrem-pairs', file('input.ll'), '-o', file('out.ll')]);
  const optimized = fs.readFileSync(file('out.ll'), 'utf8');
  assert.match(optimized, /%q = sdiv i32 %nx, %y\n  %r = srem i32 %nx, %y/);
  assert.doesNotMatch(optimized, /%xx/);
  assert.match(optimized, /%r = srem i32 %x, %y\n  %q = sdiv i32 %x, %z/);
  assert.match(optimized, /store volatile i32 %r, ptr %p, align 4\n  %q = sdiv/);
  const literal = optimized.split('define i32 @literal_pair')[1].split('}')[0];
  assert.match(literal, /mul i32 %q, 60\n\s*%r = sub i32 %x,/);
  assert.doesNotMatch(literal, /srem/);
  assert.match(optimized, /%r = srem i32 %x, 61/);
  invoke(tool('badge-codegen'), [file('out.ll'), '-o', file('out.s')]);
  const assembly = fs.readFileSync(file('out.s'), 'utf8').split('\npair:\n')[1].split('.size pair,')[0];
  assert.equal(assembly.match(/^sdiv /gm)?.length, 1);
  assert.match(assembly, /^mls /m);
  if (!oracle) return;
  fs.writeFileSync(file('caller.c'), `
extern int pair(short, int);
extern int literal_pair(int), literal_near_miss(int);
int main(void) {
  static const short dividends[] = {-32768, -199, -1, 0, 1, 199, 32767};
  static const int divisors[] = {-2147483647 - 1, -17, -1, 1, 5, 37, 2147483647};
  for (unsigned i = 0; i < sizeof(dividends) / sizeof(*dividends); ++i)
    for (unsigned j = 0; j < sizeof(divisors) / sizeof(*divisors); ++j) {
      volatile int x = dividends[i], y = divisors[j];
      if (pair(x, y) != ((x / y) ^ (x % y))) return 1;
    }
  static const int inputs[] = {-2147483647 - 1, -121, -61, -60, -59, -1,
                               0, 1, 59, 60, 61, 121, 2147483647};
  volatile int d = 60, other = 61;
  for (unsigned i = 0; i < sizeof(inputs) / sizeof(*inputs); ++i) {
    volatile int x = inputs[i];
    if (literal_pair(x ^ 1) != (((x / d) ^ 7) ^ (x % d))) return 2;
    if (literal_near_miss(x ^ 1) != (((x / d) ^ 7) ^ (x % other))) return 3;
  }
  return 0;
}
`);
  invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('out.ll')]);
  invoke(tool('badge-asm'), [file('out.s'), '-o', file('out.o')]);
  invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  invoke(clang, [...target, '-ffreestanding', '-Oz', '-c', file('caller.c'), '-o', file('caller.o')]);
  invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
    file('start.o'), file('caller.o'), file('out.o'), '-o', file('out.elf')]);
  invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('out.elf')]);
});

test('bounded scaled squares cancel exact factors before wide division', t => {
  const file = fixture(t);
  const example = (name, bound, divisor) => `
define i32 @${name}(i32 %x) {
entry:
  %valid = icmp sgt i32 %x, -100
  br i1 %valid, label %high, label %fallback
high:
  %large = icmp sgt i32 %x, ${bound}
  br i1 %large, label %cap, label %join
cap:
  br label %join
join:
  %c = phi i32 [%x, %high], [${bound}, %cap]
  %y = sext i32 %c to i64
  %sq = mul i64 %y, %y
  %n = mul i64 %sq, 2240
  %q = sdiv i64 %n, ${divisor}
  %out = trunc i64 %q to i32
  ret i32 %out
fallback:
  ret i32 99
}`;
  fs.writeFileSync(file('input.ll'), irTarget + example('bounded', 11200, 125440000) +
    example('not_exact', 11200, 125440001) + example('too_wide', 46341, 125440000));
  invoke(tool('badge-opt-sanitize'),
    ['--passes=wide-division,dead-instructions', file('input.ll'), '-o', file('out.ll')]);
  const ir = fs.readFileSync(file('out.ll'), 'utf8');
  const body = name => ir.match(new RegExp(`define i32 @${name}[^]*?\\n}`))[0];
  assert.match(body('bounded'), /udiv i32 %[^,]+, 56000/);
  assert.doesNotMatch(body('bounded'), /mul i64|call i64/);
  assert.match(body('not_exact'), /call i64/);
  assert.match(body('too_wide'), /call i64/);
  if (!oracle) return;
  invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('out.ll')]);
  fs.writeFileSync(file('caller.c'), `
extern int bounded(int);
int main(void) {
  for (int x = -103; x <= 11300; ++x) {
    volatile int n = x > 11200 ? 11200 : x;
    if (bounded(x) != (x <= -100 ? 99 : n*n / 56000)) return 1;
  }
  return 0;
}`);
  invoke(tool('badge-codegen'), [file('out.ll'), '-o', file('out.s')]);
  invoke(tool('badge-asm'), [file('out.s'), '-o', file('out.o')]);
  invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  invoke(clang, [...target, '-ffreestanding', '-Oz', '-c', file('caller.c'), '-o', file('caller.o')]);
  invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
    file('start.o'), file('caller.o'), file('out.o'), '-o', file('out.elf')]);
  invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('out.elf')]);
});

test('optional CLZ seeds and bounded reciprocals preserve exact integer boundaries', t => {
  const file = fixture(t);
  fs.writeFileSync(file('input.ll'), irTarget + `
define i32 @seed(i32 %x) {
entry:
  br label %header
header:
  %bit = phi i32 [1073741824, %entry], [%half, %shift]
  %more = icmp ugt i32 %bit, %x
  br i1 %more, label %shift, label %exit
shift:
  %half = lshr i32 %bit, 2
  br label %header
exit:
  ret i32 %bit
}
define i32 @divide(i16 signext %x) {
entry:
  %wide = sext i16 %x to i32
  %out = sdiv i32 %wide, 3
  ret i32 %out
}
define i32 @unbounded(i32 %x) {
entry:
  %out = sdiv i32 %x, 3
  ret i32 %out
}
`);
  invoke(tool('badge-opt-sanitize'),
    ['--passes=loop-bit-seed,bounded-signed-reciprocal', file('input.ll'), '-o', file('out.ll')]);
  const text = fs.readFileSync(file('out.ll'), 'utf8');
  assert.match(text, /call i32 @llvm.ctlz.i32/);
  assert.doesNotMatch(text, /label %shift/);
  assert.match(text, /mul i32/);
  assert.equal(text.match(/sdiv i32/g)?.length, 1);
  if (!oracle) return;
  invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('out.ll')]);
  fs.writeFileSync(file('caller.c'), `
extern unsigned seed(unsigned);
extern int divide(short);
int main(void) {
  unsigned x = 0;
  volatile int divisor = 3;
  for (int n = -32768; n < 32768; ++n) {
    if (divide((short)n) != n / divisor) return 1;
    unsigned want = 1u << 30;
    while (want > x) want >>= 2;
    if (seed(x) != want) return 2;
    x = x * 1664525u + 1013904223u;
  }
  return seed(~0u) != 1u << 30;
}`);
  invoke(tool('badge-codegen'), [file('out.ll'), '-o', file('out.s')]);
  invoke(tool('badge-asm'), [file('out.s'), '-o', file('out.o')]);
  invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  invoke(clang, [...target, '-ffreestanding', '-Oz', '-c', file('caller.c'), '-o', file('caller.o')]);
  invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
    file('start.o'), file('caller.o'), file('out.o'), '-o', file('out.elf')]);
  invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('out.elf')]);
});

test('exact signed power-of-two division follows local and internal-return multiples only', t => {
  const file = fixture(t);
  fs.writeFileSync(file('input.ll'), `target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
define internal i32 @scale(i32 %x) {
entry:
  %n = shl i32 %x, 8
  ret i32 %n
}
define i32 @exact(i32 %x) {
entry:
  %n = call i32 @scale(i32 %x)
  %q = sdiv i32 %n, 256
  ret i32 %q
}
define i32 @round(i32 %x) {
entry:
  %q = sdiv i32 %x, 256
  ret i32 %q
}
define i32 @main() {
entry:
  %negative = call i32 @exact(i32 -32767)
  %minimum = call i32 @exact(i32 8388608)
  %small = call i32 @round(i32 -255)
  %a = xor i32 %negative, -32767
  %b = xor i32 %minimum, -8388608
  %c = or i32 %a, %b
  %status = or i32 %c, %small
  ret i32 %status
}`);
  invoke(tool('badge-opt-sanitize'), ['--passes=exact-pow2', file('input.ll'), '-o', file('out.ll')]);
  const result = fs.readFileSync(file('out.ll'), 'utf8');
  assert.match(result, /ashr i32 %n, 8/);
  assert.match(result, /sdiv i32 %x, 256/);
  if (oracle) {
    invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('out.ll')]);
    invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
    for (const name of ['input', 'out']) {
      invoke(tool('badge-codegen'), ['--scalar-selection=thumb-constants', file(`${name}.ll`), '-o', file(`${name}.s`)]);
      invoke(tool('badge-asm'), [file(`${name}.s`), '-o', file(`${name}.o`)]);
      invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
        file('start.o'), file(`${name}.o`), '-o', file(`${name}.elf`)]);
      invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
        '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${name}.elf`)]);
    }
  }
});

test('wide literal division switches paths without changing signed or unsigned results', t => {
  const file = fixture(t);
  const limits = [1n, 3n, 125440000n, (1n << 31n) - 1n, 1n << 31n,
    (1n << 31n) + 1n, 4294967291n, 1n << 32n, 1n << 62n, 1n << 63n];
  let ir = irTarget;
  let declarations = '', rows = '';
  const fixtureValues = [0n, 1n, 0xffffffffn, 0x100000000n,
    0x100000001n, 0x8000000000000000n, 0x7fffffffffffffffn,
    0x8000000000000001n, 0xffffffff00000000n, 0xffffffffffffffffn];
  const literal = x => `${BigInt.asUintN(64, x)}ull`;
  for (const [i, d] of limits.entries()) {
    const values = new Set([...fixtureValues, d - 1n, d, d + 1n,
      BigInt.asUintN(64, -d + 1n), BigInt.asUintN(64, -d), BigInt.asUintN(64, -d - 1n)]);
    let seed = BigInt(i + 17);
    for (let j = 0; j < 24; j++) {
      seed = BigInt.asUintN(64, seed * 6364136223846793005n + 1n);
      values.add(seed);
    }
    for (const kind of ['u', 's']) {
      if (kind === 's' && d === (1n << 63n)) continue;
      for (const op of ['div', 'rem']) {
        const fn = `${kind}${op}${i}`;
        ir += `define i64 @${fn}(i64 %x) {\nentry:\n%v = ${kind}${op} i64 %x, ${d}\nret i64 %v\n}\n`;
        declarations += `extern u64 ${fn}(u64);\n`;
        for (const n of values) {
          const x = kind === 's' ? BigInt.asIntN(64, n) : n;
          rows += `{${fn}, ${literal(n)}, ${literal(op === 'div' ? x / d : x % d)}},\n`;
        }
      }
    }
  }
  ir += `define i64 @exact(i64 %x) {
entry:
%v = sdiv exact i64 %x, 4294967296
ret i64 %v
}
define i64 @dynamic(i64 %x, i64 %y) {
entry:
%v = udiv i64 %x, %y
ret i64 %v
}
`;
  fs.writeFileSync(file('input.ll'), ir);
  invoke(tool('badge-opt-sanitize'), [file('input.ll'), '-o', file('ready.ll')]);
  const optimized = fs.readFileSync(file('ready.ll'), 'utf8');
  assert.match(optimized, /call i64 [^(]+\([^,]+, i32 125440000\)/);
  assert.match(optimized, /call i64 [^(]+\([^,]+, i64 2147483649\)/);
  assert.match(optimized, /call i64 [^(]+\([^,]+, i64 4294967291\)/);
  assert.match(optimized, /ashr exact i64 %x, 32/);
  assert.match(optimized, /lshr i64 %x, 63/);
  assert.match(optimized, /and i64 %x, 9223372036854775807/);
  if (!oracle) return;
  fs.writeFileSync(file('caller.c'), `
typedef unsigned long long u64;
${declarations}
extern u64 exact(u64);
extern u64 dynamic(u64, u64);
static const struct {u64 (*call)(u64); u64 n, want;} cases[] = {
${rows}
};
int main(void) {
  for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++)
    if (cases[i].call(cases[i].n) != cases[i].want) return 1;
  return exact(0x8000000000000000ull) != 0xffffffff80000000ull ||
         dynamic(0xffffffffffffffffull, 4294967296ull) != 4294967295ull;
}
`);
  invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('ready.ll')]);
  invoke(tool('badge-codegen'), [file('ready.ll'), '-o', file('ready.s')]);
  invoke(tool('badge-asm'), [file('ready.s'), '-o', file('ready.o')]);
  invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  invoke(clang, [...target, '-ffreestanding', '-Oz', '-c', file('caller.c'), '-o', file('caller.o')]);
  invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
    file('start.o'), file('caller.o'), file('ready.o'), '-o', file('ready.elf')]);
  invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('ready.elf')]);
});

test('normalized runtime division and wide shifts match independent integer results', t => {
  const file = fixture(t);
  const ops = ['udiv', 'urem', 'sdiv', 'srem', 'shl'];
  const ir = irTarget + 'declare i64 @"llvm.ctlz.i64"(i64, i1 immarg)\n' +
    ops.map(op => `define i64 @test_${op}(i64 %x, i64 %y) {
entry:
  %r = ${op} i64 %x, %y
  ret i64 %r
}
`).join('') + `define i64 @test_clz(i64 %x) {
entry:
  %r = call i64 @llvm.ctlz.i64(i64 %x, i1 false)
  ret i64 %r
}
`;
  fs.writeFileSync(file('input.ll'), ir);
  invoke(tool('badge-opt-sanitize'), [file('input.ll'), '-o', file('out.ll')]);
  const optimized = fs.readFileSync(file('out.ll'), 'utf8');
  assert.equal(optimized.match(/^declare i64 @"?llvm\.ctlz\.i64"?\(/gm)?.length, 1);
  assert.match(optimized, /call i64 @llvm\.ctlz\.i64/);
  if (!oracle) return;
  const bits = n => BigInt.asUintN(64, n);
  const literal = n => `${bits(n)}ull`;
  const rows = [];
  function vector(op, a, b, expected) {
    rows.push(`{test_${op},${literal(a)},${literal(b)},${literal(expected)}}`);
  }
  const edges = [0n, 1n, 2n, (1n << 31n) - 1n, 1n << 31n,
    (1n << 32n) - 1n, 1n << 32n, (1n << 32n) + 1n,
    (1n << 63n) - 1n, 1n << 63n, (1n << 63n) + 1n, bits(-1n)];
  let seed = 19n;
  const next = () => (seed = bits(seed * 6364136223846793005n + 1n));
  const pairs = edges.flatMap(x => edges.slice(1).map(y => [x, y]));
  for (let i = 0; i < 384; i++)
    pairs.push([next(), (next() >> BigInt(i % 64)) || 1n]);
  for (const [x, y] of pairs) {
    vector('udiv', x, y, x / y);
    vector('urem', x, y, x % y);
    const a = BigInt.asIntN(64, x), b = BigInt.asIntN(64, y);
    if (a === -(1n << 63n) && b === -1n) continue;
    vector('sdiv', a, b, a / b);
    vector('srem', a, b, a % b);
  }
  for (const x of edges)
    for (let n = 0n; n < 64n; n++)
      vector('shl', x, n, x << n);
  fs.writeFileSync(file('caller.c'), `
typedef unsigned long long u64;
${ops.map(op => `extern u64 test_${op}(u64, u64);`).join('\n')}
extern u64 test_clz(u64);
static const struct { u64 (*fn)(u64, u64); u64 x, y, want; } cases[] = {
${rows.join(',\n')}
};
int main(void) {
  for (unsigned i = 0; i < sizeof(cases)/sizeof(*cases); i++)
    if (cases[i].fn(cases[i].x, cases[i].y) != cases[i].want) return 1;
  if (test_clz(0) != 64) return 2;
  u64 power = 1;
  for (unsigned i = 0; i < 64; i++, power <<= 1)
    if (test_clz(power) != 63 - i) return 3;
  return 0;
}`);
  invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('out.ll')]);
  invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  invoke(clang, [...target, '-ffreestanding', '-Oz', '-c', file('caller.c'), '-o', file('caller.o')]);
  for (const policy of [[], ['--scalar-selection=legacy']]) {
    invoke(tool('badge-codegen-sanitize'), [...policy, file('out.ll'), '-o', file('out.s')]);
    if (!policy.length) {
      assert.match(fs.readFileSync(file('out.s'), 'utf8'), /itt eq\nmoveq r0, r2\nmoveq r1, r3/);
      invoke(clang, [...target, '-c', file('out.s'), '-o', file('encoded.o')]);
    }
    invoke(tool('badge-asm'), [file('out.s'), '-o', file('out.o')]);
    invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
      file('start.o'), file('caller.o'), file('out.o'), '-o', file('out.elf')]);
    invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('out.elf')]);
  }
});

test('decimal reciprocal requires a bound and shares the quotient with the remainder', t => {
  const file = fixture(t);
  const divrem = (name, source, prefix = '') => `define i32 @${name}(i32 %x) {
entry:
${prefix}  %q = udiv i32 ${source}, 10
  %r = urem i32 ${source}, 10
  %high = shl i32 %q, 4
  %result = or i32 %high, %r
  ret i32 %result
}
`;
  fs.writeFileSync(file('input.ll'), irTarget +
    divrem('bounded', '%n', '  %n = and i32 %x, 65535\n') +
    divrem('unbounded', '%x') + divrem('too_wide', '%n', '  %n = and i32 %x, 131071\n'));
  invoke(tool('badge-opt-sanitize'), ['--passes=bounded-decimal', file('input.ll'), '-o', file('out.ll')]);
  const out = fs.readFileSync(file('out.ll'), 'utf8');
  assert.match(out, /define i32 @bounded\b[^}]*mul i32 .*52429/);
  assert.doesNotMatch(out, /define i32 @bounded\b[^}]*[ur](?:div|rem) i32/);
  assert.match(out, /define i32 @unbounded\b[^}]*udiv i32 %x, 10/);
  assert.match(out, /define i32 @too_wide\b[^}]*urem i32 %n, 10/);
  if (!oracle) return;
  fs.writeFileSync(file('caller.c'), `
extern unsigned bounded(unsigned);
int main(void) {
  for (unsigned x = 0; x <= 65535; x++) {
    unsigned expected = ((x / 10u) << 4) | (x % 10u);
    if (bounded(x) != expected || bounded(x | 0xa5000000u) != expected) return 1;
  }
  return 0;
}`);
  invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('out.ll')]);
  invoke(tool('badge-codegen'), [file('out.ll'), '-o', file('out.s')]);
  invoke(tool('badge-asm'), [file('out.s'), '-o', file('out.o')]);
  invoke(tool('badge-asm'), [path.join(root, 'tests/fixtures/codegen_start.S'), '-o', file('start.o')]);
  invoke(clang, [...target, '-ffreestanding', '-O2', '-c', file('caller.c'), '-o', file('caller.o')]);
  invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
    file('start.o'), file('caller.o'), file('out.o'), '-o', file('out.elf')]);
  invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
    '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
    '-kernel', file('out.elf')]);
});

test('power-of-two matrix removes divider work and executes across all residency policies', t => {
  const file = fixture(t), { ir, caller } = matrix();
  fs.writeFileSync(file('input.ll'), ir);
  fs.writeFileSync(file('caller.c'), caller);
  fs.writeFileSync(file('size.ld'), fs.readFileSync(path.join(root, 'tests/fixtures/link_oracle_fixture.ld'),
    'utf8').replace('ENTRY(entry)', 'ENTRY(q0)'));
  invoke(tool('badge-opt'), ['--passes=target-strength-reduce', file('input.ll'), '-o', file('candidate.ll')]);
  invoke(tool('badge-opt-sanitize'), [file('input.ll'), '-o', file('default.ll')]);
  const defaults = fs.readFileSync(file('default.ll'), 'utf8');
  assert.match(defaults, /define i32 @q31\b[^}]*lshr i32 %x, 31/);
  assert.match(defaults, /define i32 @r3\b[^}]*and i32 %x, 7/);
  assert.match(defaults, /define i32 @b3\b[^}]*and i32 %x, 7[^}]*lshr i32 %x, 3/);
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
    invoke(process.env.LLVM_OPT || 'opt', ['-passes=verify', '-disable-output', file('default.ll')]);
    invoke(tool('badge-codegen'), [file('default.ll'), '-o', file('default.s')]);
    invoke(tool('badge-asm'), [file('default.s'), '-o', file('default.o')]);
    invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
      file('start.o'), file('caller.o'), file('default.o'), '-o', file('default.elf')]);
    invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('default.elf')], true, 120000);
    invoke(clang, [...target, '-Oz', '-c', '-x', 'ir', file('candidate.ll'), '-o', file('llvm.o')]);
    invoke(lld, ['-T', path.join(root, 'tests/fixtures/codegen_fixture.ld'),
      file('start.o'), file('caller.o'), file('llvm.o'), '-o', file('llvm.elf')]);
    invoke(process.env.QEMU_ARM || 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
      '-kernel', file('llvm.elf')], true, 120000);
  }
});
