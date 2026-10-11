import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { decodeElf } from '../tools/boundary-evidence.mjs';
import { parseAssembly } from '../tools/benchmark-c06.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tool = name => path.join(root, 'badge-compiler/build', name);
const clang = process.env.LLVM_CLANG ?? 'clang';
const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz',
  '-ffreestanding', '-fno-builtin'];
const header = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
  'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${command}: ${result.stdout}\n${result.stderr}`);
}
function directory(t) {
  const dir = fs.mkdtempSync(path.join(root, 'badge-compiler/build/table-interpreter-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
function stream(name, { indirect = false, opaque = false, live = false, flagged = false, four = false,
  long = false, volatile = false } = {}) {
  return `define void @${name}(ptr %data, i32 %length${indirect ? ', ptr %callback' : ''}) {
entry:
br label %start
start:
%end = getelementptr i8, ptr %data, i32 %length
br label %outer
outer:
%record = phi ptr [%data, %start], [%cursor, %after]
%more = icmp ult ptr %record, %end
br i1 %more, label %head, label %done
head:
%countp = getelementptr i8, ptr %record, i32 1
%cmd = load ${volatile ? 'volatile ' : ''}i8, ptr %record, align 1
%command = zext i8 %cmd to i32
call void @record_command(i32 %command)
%payload = getelementptr i8, ptr %countp, i32 1
%raw = load i8, ptr %countp, align 1
%count = zext i8 %raw to i32
br label %inner
inner:
%remaining = phi i32 [%count, %head], [%decrement, %body]
%cursor = phi ptr [%payload, %head], [%next, %body]
%decrement = sub ${flagged ? 'nsw ' : ''}i32 %remaining, 1
${live ? '%side = inttoptr i32 805320708 to ptr\nstore volatile i32 %decrement, ptr %side, align 4' : ''}
%nonzero = icmp ne i32 %remaining, 0
br i1 %nonzero, label %body, label %after
body:
%next = getelementptr i8, ptr %cursor, i32 1
%byte = load ${volatile ? 'volatile ' : ''}i8, ptr %cursor, align 1
%value = zext i8 %byte to i32
${opaque ? 'call void asm sideeffect "nop", "~{memory}"()' : ''}
call void ${indirect ? '%callback' : four ? '@record_four' : '@record_byte'}(i32 %value${four ? ', i32 0, i32 0, i32 0' : ''})
${long ? Array.from({ length: 40 }, () => 'call void @record_byte(i32 %value)').join('\n') : ''}
br label %inner
after:
br label %outer
done:
ret void
}
`;
}
function encodings(file, policy) {
  run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
  run(clang, [...flags, '-c', file(`${policy}.s`), '-o', file(`${policy}-llvm.o`)]);
  const code = name => decodeElf(fs.readFileSync(file(name))).sections
    .filter(s => s.flags & 4).map(s => s.data);
  assert.deepEqual(code(`${policy}.o`), code(`${policy}-llvm.o`));
}
test('nested byte streams preserve callback order, unsigned counts and both ABI directions', t => {
  const file = directory(t);
  const shapes = [['parse_packets', {}], ['parse_indirect', { indirect: true }],
    ['parse_barrier', { opaque: true }], ['parse_live_exit', { live: true }],
    ['parse_flagged', { flagged: true }], ['parse_volatile', { volatile: true }],
    ['parse_long', { long: true }], ['parse_four', { four: true }]];
  fs.writeFileSync(file('input.ll'), header +
    'declare void @record_command(i32)\ndeclare void @record_byte(i32)\n' +
    'declare void @record_four(i32, i32, i32, i32)\n' +
    shapes.map(([n, opts]) => stream(n, opts)).join(''));
  fs.writeFileSync(file('abi.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
.global check_stream
.thumb_func
check_stream:
push.w {r3-r11, lr}
mov r12, r0
mov r0, r1
mov r1, r2
mov r2, r3
${Array.from({ length: 8 }, (_, n) => `movw r${n + 4}, #${(n + 4) * 17}`).join('\n')}
blx r12
${Array.from({ length: 8 }, (_, n) =>
    `cmp r${n + 4}, #${(n + 4) * 17}\nbne abi_failed`).join('\n')}
movw r0, #0
b abi_done
abi_failed:
movw r0, #1
abi_done:
pop.w {r3-r11, pc}
.global record_four
.thumb_func
record_four:
orr.w r1, r1, r2
orr.w r1, r1, r3
cmp r1, #0
beq four_valid
movw r0, #36864
four_valid:
b record_byte
${[['record_command', 256], ['record_byte', 512]].map(([name, tag]) => `
.global ${name}
.thumb_func
${name}:
movw r1, #12288
movt r1, #12288
mov r12, sp
and.w r12, r12, #7
cmp r12, #0
beq ${name}_aligned
movw r3, #1
str.w r3, [r1, #2048]
${name}_aligned:
ldr.n r2, [r1]
add.w r0, r0, #${tag}
add.w r3, r1, #4
str.w r0, [r3, r2, lsl #2]
adds r2, #1
str.n r2, [r1]
${[0, 1, 2, 3, 12].map(r => `movw r${r}, #${0xa100 + r}`).join('\n')}
cmp r0, r1
bx lr
`).join('')}
`);
  fs.writeFileSync(file('oracle.c'), `
typedef void (*stream_fn)(const unsigned char *, unsigned, void (*)(unsigned));
extern unsigned check_stream(stream_fn, const unsigned char *, unsigned, void (*)(unsigned));
extern void record_byte(unsigned);
${shapes.map(([name]) => `extern void ${name}(const unsigned char *, unsigned, void (*)(unsigned));`).join('\n')}
static unsigned char data[300];
int main(void) {
  volatile unsigned *events = (volatile unsigned *)0x30003000;
  stream_fn parsers[] = {${shapes.map(([name]) => name).join(',')}};
  unsigned parser, scenario, i, n, at, expected, count, command, repeats;
  for (parser = 0; parser < 8; parser++) for (scenario = 0; scenario < 7; scenario++) {
    n = 0;
    if (scenario == 1) { data[n++] = 255; data[n++] = 0; }
    if (scenario == 2) { data[n++] = 128; data[n++] = 1; data[n++] = 255; }
    if (scenario == 3) {
      data[n++] = 0; data[n++] = 0; data[n++] = 255; data[n++] = 2;
      data[n++] = 128; data[n++] = 0; data[n++] = 127; data[n++] = 0;
    }
    if (scenario >= 4) {
      count = scenario == 4 ? 127 : scenario == 5 ? 128 : 255;
      data[n++] = 200; data[n++] = count;
      for (i = 0; i < count; i++) data[n++] = (unsigned char)(i * 97 + 131);
    }
    if (parser == 6 && scenario >= 4) continue;
    events[0] = 0; events[512] = 0; events[513] = 0;
    if (check_stream(parsers[parser], data, n, record_byte) || events[512]) return 1;
    expected = 0; at = 0;
    while (at < n) {
      command = data[at++]; count = data[at++];
      if (events[1 + expected++] != 256 + command) return 2;
      while (count--) {
        repeats = parser == 6 ? 41 : 1;
        for (i = 0; i < repeats; i++)
          if (events[1 + expected++] != 512 + data[at]) return 3;
        at++;
      }
    }
    if (events[0] != expected) return 4;
    if (parser == 3 && n && events[513] != 0xffffffffu) return 5;
  }
  return 0;
}
`);
  run(tool('badge-asm'), [file('abi.s'), '-o', file('abi.o')]);
  run(tool('badge-asm'), ['badge-compiler/tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const caller of ['llvm', 'owned'])
    run(caller === 'llvm' ? clang : 'bash', [
      ...(caller === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
      ...flags, '-c', file('oracle.c'), '-o', file(`${caller}-oracle.o`)]);
  let controlFrames;
  const reached = {};
  for (const policy of ['thumb-widths', 'thumb-constants', 'llvm']) {
    if (policy === 'llvm') run(clang, [...flags, '-c', file('input.ll'), '-o', file(`${policy}.o`)]);
    else {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, file('input.ll'),
        '-o', file(`${policy}.s`)]);
      const text = fs.readFileSync(file(`${policy}.s`), 'utf8');
      const frames = parseAssembly(text, policy);
      if (policy === 'thumb-widths') controlFrames = frames;
      else {
        for (const [name, frame] of frames) {
          const before = controlFrames.get(name);
          for (const key of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
            assert(frame[key] <= before[key], `${name}: ${key} grew`);
          assert.deepEqual(frame.calls, before.calls);
        }
        const functions = new Map(text.split(/(?=\.section \.text\.)/).slice(1)
          .map(body => [body.match(/^\.section \.text\.([^,]+)/)[1], body]));
        assert.match(functions.get('parse_packets'), /push\.n \{r4, r5, r6, lr\}/);
        assert.match(functions.get('parse_packets'), /@ zero-branch: saved 2/);
        assert.match(functions.get('parse_packets'), /@ byte-call-forwarding: saved 2/);
        assert(!functions.get('parse_packets').includes('mov r3, r1'));
        assert(!functions.get('parse_barrier').includes('@ zero-branch:'));
        assert(!functions.get('parse_long').includes('@ zero-branch:'));
      }
      encodings(file, policy);
      if (policy === 'thumb-constants')
        assert.equal(decodeElf(fs.readFileSync(file(`${policy}.o`))).symbols
          .find(s => s.name === 'parse_packets').size, 40);
    }
    for (const caller of ['llvm', 'owned']) {
      const prefix = `${policy}-${caller}`;
      run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'badge-compiler/tests/fixtures/codegen_fixture.ld',
        file('start.o'), file(`${caller}-oracle.o`), file('abi.o'), file(`${policy}.o`),
        '-o', file(`${prefix}.elf`)]);
      const trace = caller === 'llvm' && policy !== 'llvm';
      run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
        '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
        '-kernel', file(`${prefix}.elf`),
        ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain',
          '-D', file(`${prefix}.trace`)] : [])]);
      if (trace) reached[policy] = (fs.readFileSync(file(`${prefix}.trace`), 'utf8').match(/^Trace /gm) ?? []).length;
    }
  }
  assert(reached['thumb-constants'] < reached['thumb-widths']);
  console.log(`RS04 reached oracle instructions ${reached['thumb-widths']} -> ${reached['thumb-constants']}`);
});
test('scalar scratch fallbacks, pointer offsets and zero-branch encoding limits remain exact', t => {
  const file = directory(t);
  const offsets = [0, 7, 8, 255, 256, 4095, 4096, 0x12345];
  fs.writeFileSync(file('input.ll'), header +
    'declare i32 @four(i32, i32, i32, i32)\n' +
    'declare i32 @eight(i32, i32, i32, i32, i32, i32, i32, i32)\n' +
    'declare i64 @wide(i64, i32)\ndeclare i32 @boxed(ptr byval({ i32 }) align 4)\n' +
    offsets.map(n => `define ptr @advance_${n}(ptr %base) {
entry:
%next = getelementptr i8, ptr %base, i32 ${n}
ret ptr %next
}
`).join('') + `define ptr @advance_dynamic(ptr %base, i32 %index) {
entry:
%next = getelementptr i8, ptr %base, i32 %index
ret ptr %next
}
define i32 @mixed_calls(i32 %x, ptr %record) {
entry:
%a = call i32 @four(i32 %x, i32 2, i32 %x, i32 4)
%b = call i32 @eight(i32 %x, i32 %a, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8)
%wide = call i64 @wide(i64 4294967297, i32 %x)
%low = trunc i64 %wide to i32
%box = call i32 @boxed(ptr byval({ i32 }) align 4 %record)
%s = add i32 %a, %b
%t = add i32 %s, %x
%u = add i32 %t, %low
%v = add i32 %u, %box
ret i32 %v
}
define i32 @local_sum(i32 %x) {
entry:
%storage = alloca [4 x i32], align 4
%index = and i32 %x, 3
%address = getelementptr [4 x i32], ptr %storage, i32 0, i32 %index
store volatile i32 %x, ptr %address, align 4
%loaded = load volatile i32, ptr %address, align 4
%result = add i32 %loaded, 1
ret i32 %result
}
`);
  fs.writeFileSync(file('callees.c'), `
struct box { unsigned x; };
#ifdef CLOBBER_REGISTERS
#define CLOBBER() __asm__ volatile("movw r12, #123" ::: "r0", "r1", "r2", "r3", "r12", "cc", "memory")
#else
#define CLOBBER() __asm__ volatile("nop" ::: "cc", "memory")
#endif
unsigned four(unsigned a, unsigned b, unsigned c, unsigned d) {
  unsigned result = a + b * 3 + c * 5 + d * 7; CLOBBER(); return result;
}
unsigned eight(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e,
 unsigned f, unsigned g, unsigned h) {
  unsigned result = a + b * 3 + c * 5 + d * 7 + e * 11 + f * 13 + g * 17 + h * 19;
  CLOBBER(); return result;
}
unsigned long long wide(unsigned long long a, unsigned x) {
  unsigned long long result = a + x; CLOBBER(); return result;
}
unsigned boxed(struct box b) { unsigned result = b.x * 13; CLOBBER(); return result; }
`);
  fs.writeFileSync(file('oracle.c'), `
struct box { unsigned x; };
extern unsigned mixed_calls(unsigned, const struct box *);
extern unsigned local_sum(unsigned);
extern void *advance_dynamic(void *, unsigned);
${offsets.map(n => `extern void *advance_${n}(void *);`).join('\n')}
int main(void) {
  unsigned inputs[] = {0,1,255,256,0x7fffffffu,0x80000000u,0xffffffffu};
  void *(*functions[])(void *) = {${offsets.map(n => `advance_${n}`).join(',')}};
  unsigned offsets[] = {${offsets.join(',')}}, i, j, x, a, b, expected;
  struct box record;
  for (i = 0; i < 7; i++) {
    x = inputs[i]; record.x = x ^ 0x12345678u;
    a = x + 2 * 3 + x * 5 + 4 * 7;
    b = x + a * 3 + 3 * 5 + 4 * 7 + 5 * 11 + 6 * 13 + 7 * 17 + 8 * 19;
    expected = a + b + x + (1 + x) + record.x * 13;
    if (mixed_calls(x, &record) != expected) return 1;
    if (local_sum(x) != x + 1) return 4;
    if ((unsigned)advance_dynamic((void *)0x30005000, x) != 0x30005000u + x) return 2;
    for (j = 0; j < ${offsets.length}; j++)
      if ((unsigned)functions[j]((void *)x) != x + offsets[j]) return 3;
  }
  return 0;
}
`);
  run(tool('badge-asm'), ['badge-compiler/tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const compiler of ['llvm', 'owned'])
    for (const source of ['oracle', 'callees'])
      run(compiler === 'llvm' ? clang : 'bash', [
        ...(compiler === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
        ...flags, ...(compiler === 'llvm' ? ['-DCLOBBER_REGISTERS'] : []),
        '-c', file(`${source}.c`), '-o', file(`${compiler}-${source}.o`)]);
  for (const policy of ['thumb-widths', 'thumb-constants', 'llvm']) {
    if (policy === 'llvm') run(clang, [...flags, '-c', file('input.ll'), '-o', file(`${policy}.o`)]);
    else {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, file('input.ll'),
        '-o', file(`${policy}.s`)]);
      encodings(file, policy);
    }
    for (const caller of ['llvm', 'owned'])
      for (const callee of ['llvm', 'owned']) {
        const elf = file(`${policy}-${caller}-${callee}.elf`);
        run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'badge-compiler/tests/fixtures/codegen_fixture.ld',
          file('start.o'), file(`${caller}-oracle.o`), file(`${callee}-callees.o`),
          file(`${policy}.o`), '-o', elf]);
        run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
          '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
          '-kernel', elf]);
      }
  }
  for (const padding of [1, 64, 65]) {
    const source = `.syntax unified\n.cpu cortex-m33\n.thumb\n.text\ncbz r7, target\n` +
      'nop\n'.repeat(padding) + 'target:\nbx lr\n';
    fs.writeFileSync(file('bounds.s'), source);
    const result = spawnSync(tool('badge-asm'), [file('bounds.s'), '-o', file('bounds.o')],
      { cwd: root, encoding: 'utf8' });
    if (padding === 65) {
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /outside forward 0\.\.126 range/);
    } else {
      assert.equal(result.status, 0, result.stderr);
      run(clang, [...flags, '-c', file('bounds.s'), '-o', file('bounds-llvm.o')]);
      const bytes = name => decodeElf(fs.readFileSync(file(name))).sections.find(s => s.name === '.text').data;
      assert.deepEqual(bytes('bounds.o'), bytes('bounds-llvm.o'));
    }
  }
});
