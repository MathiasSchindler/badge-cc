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
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, `${command}: ${result.stderr}`);
}
const header = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
  'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function wrapper(name, value, post = 0, extra = '', args = 'i32 %x', call = 'call void @sink(i32 %x)') {
  return `define void @${name}(${args}) {
entry:
br label %body
body:
%p = inttoptr i32 ${0x30002008} to ptr
store volatile i32 ${value}, ptr %p, align 4
${extra}
${call}
%q = inttoptr i32 ${0x30002000 + post} to ptr
store volatile i32 ${value}, ptr %q, align 4
ret void
}
`;
}
test('ordered wrappers forward public arguments and preserve post-call effects across clobbering ABI edges', t => {
  const directory = fs.mkdtempSync(path.join(root, 'badge-compiler/build/command-wrapper-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = name => path.join(directory, name);
  const accepted = [0, 1, 255, 256, 0x10000000, 0x80000000, 0xffffffff];
  const source = header + 'declare void @sink(i32)\n' +
    accepted.map((value, n) => wrapper(`stream_${n}`, value)).join('') +
    wrapper('boundary', 17, 132) +
    wrapper('distant', 17, 136) +
    wrapper('two_args', 17, 0, '', 'i32 %x, i32 %y') +
    wrapper('opaque', 17, 0, 'call void asm sideeffect "dsb sy", "~{memory}"()') +
    wrapper('twice', 17, 0, '', 'i32 %x', 'call void @sink(i32 %x)\ncall void @sink(i32 %x)') +
    wrapper('argument_store', '%x') +
    wrapper('different', 17).replace('store volatile i32 17, ptr %q', 'store volatile i32 18, ptr %q') +
    wrapper('shared_pointer', 17, 0, 'store volatile i32 17, ptr %p, align 4') +
    wrapper('narrow', 17, 0, '', 'i8 %x', '%wide = zext i8 %x to i32\ncall void @sink(i32 %wide)') +
    wrapper('returned', 17).replace('define void @returned', 'define i32 @returned').replace('ret void', 'ret i32 9') +
    wrapper('indirect', 17, 0, '', 'i32 %x, ptr %f', 'call void %f(i32 %x)') +
    wrapper('conditional', 17).replace('br label %body',
      '%cond = icmp eq i32 %x, 0\nbr i1 %cond, label %body, label %exit').replace('ret void\n}',
      'br label %exit\nexit:\nret void\n}');
  fs.writeFileSync(file('input.ll'), source);
  const declarations = accepted.map((_, n) => `extern void stream_${n}(unsigned);`).join('\n');
  fs.writeFileSync(file('oracle.c'), `
${declarations}
extern void boundary(unsigned), distant(unsigned), opaque(unsigned), twice(unsigned),
 argument_store(unsigned), different(unsigned), shared_pointer(unsigned), narrow(unsigned char),
 two_args(unsigned,unsigned), indirect(unsigned,void(*)(unsigned)), conditional(unsigned);
extern unsigned returned(unsigned), check_wrapper(void(*)(unsigned),unsigned);
extern void sink(unsigned);
static volatile unsigned * const ram = (volatile unsigned *)0x30002000;
static volatile unsigned expected, seen, bad, count, post;
void observer(unsigned x, unsigned stack) {
  if (ram[2] != expected || ram[post] != 0x12345678u || (stack & 7u)) bad = 1;
  seen = x; count++;
}
int main(void) {
  void (*const functions[])(unsigned) = {
    ${accepted.map((_, n) => `stream_${n}`).join(',')}, boundary, distant, opaque, shared_pointer
  };
  const unsigned constants[] = {${accepted.map(n => `${n}u`).join(',')},17,17,17,17};
  const unsigned inputs[] = {0,1,255,256,0x80000000u,0xffffffffu};
  unsigned n, i;
  for (n = 0; n < sizeof(functions)/sizeof(functions[0]); n++) {
    post = n == 7 ? 33 : n == 8 ? 34 : 0;
    expected = constants[n];
    for (i = 0; i < 6; i++) {
      ram[2] = 0; ram[post] = 0x12345678u; count = 0; seen = ~inputs[i];
      if (check_wrapper(functions[n], inputs[i])) return 1;
      if (bad || seen != inputs[i] || count != 1 || ram[post] != expected) return 2;
    }
  }
  expected = 17; post = 0;
  ram[0] = 0x12345678u; count = 0; two_args(0xffffffffu,8);
  if (bad || count != 1 || seen != 0xffffffffu || ram[0] != 17) return 3;
  ram[0] = 0x12345678u; count = 0; twice(256);
  if (bad || count != 2 || seen != 256 || ram[0] != 17) return 4;
  expected = 0xffffffffu; ram[0] = 0x12345678u; argument_store(expected);
  if (bad || ram[0] != expected) return 5;
  expected = 17; ram[0] = 0x12345678u; different(99);
  if (bad || ram[0] != 18) return 6;
  ram[0] = 0x12345678u; narrow(255);
  if (bad || seen != 255 || ram[0] != 17) return 7;
  ram[0] = 0x12345678u;
  if (returned(7) != 9 || bad || ram[0] != 17) return 8;
  ram[0] = 0x12345678u; indirect(88,sink);
  if (bad || seen != 88 || ram[0] != 17) return 9;
  ram[0] = 0x12345678u; count = 0; conditional(1);
  if (count || ram[0] != 0x12345678u) return 10;
  conditional(0);
  return bad || count != 1 || seen != 0 || ram[0] != 17;
}
`);
  fs.writeFileSync(file('abi.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
.global sink
.thumb_func
sink:
mov r1, sp
push.n {r4, lr}
bl observer
movw r0, #101
movw r1, #102
movw r2, #103
movw r3, #104
movw r12, #112
cmp r0, r1
pop.n {r4, pc}
.global check_wrapper
.thumb_func
check_wrapper:
push.w {r3-r11, lr}
mov r12, r0
mov r0, r1
${Array.from({ length: 8 }, (_, n) => `movw r${n + 4}, #${(n + 4) * 11}`).join('\n')}
blx r12
${Array.from({ length: 8 }, (_, n) => `cmp r${n + 4}, #${(n + 4) * 11}\nbne failed`).join('\n')}
movw r0, #0
b done
failed:
movw r0, #1
done:
pop.w {r3-r11, pc}
`);
  run(tool('badge-asm'), [file('abi.s'), '-o', file('abi.o')]);
  run(tool('badge-asm'), ['badge-compiler/tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz', '-ffreestanding', '-fno-builtin'];
  for (const compiler of ['llvm', 'owned'])
    run(compiler === 'llvm' ? clang : 'bash', [
      ...(compiler === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
      ...flags, '-c', file('oracle.c'), '-o', file(`${compiler}-oracle.o`)]);
  const reached = {};
  let controlFrames;
  let controlFunctions;
  for (const policy of ['thumb-widths', 'thumb-constants', 'llvm']) {
    if (policy === 'llvm')
      run(clang, [...flags, '-c', file('input.ll'), '-o', file(`${policy}.o`)]);
    else {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, file('input.ll'),
        '-o', file(`${policy}.s`)]);
      const assembly = fs.readFileSync(file(`${policy}.s`), 'utf8');
      const frames = parseAssembly(assembly, policy);
      if (policy === 'thumb-widths') controlFrames = frames;
      else {
        for (const [name, frame] of frames) {
          const before = controlFrames.get(name);
          for (const field of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
            assert(frame[field] <= before[field], `${name}: ${field} grew`);
          assert.deepEqual(frame.calls, before.calls);
        }
        const functions = assembly.split(/(?=\.section \.text\.)/).slice(1);
        assert.equal(functions.length, 19);
        for (const [n, text] of functions.entries()) {
          assert.equal(text.includes('@ ordered-wrapper:'), n < 8, `function ${n}`);
          if (n < 8) {
            assert.match(text, /push\.n \{r4, lr\}/);
            assert.match(text, /pop\.n \{r4, pc\}/);
            assert(!/^mov r\d+, r0$/m.test(text));
            assert.match(text, /str\.n r1, \[r4, #\d+\][\s\S]*bl sink[\s\S]*str\.n r1, \[r4, #\d+\]/);
          }
        }
      }
      run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
      run(clang, [...flags, '-c', file(`${policy}.s`), '-o', file(`${policy}-llvm.o`)]);
      const code = name => decodeElf(fs.readFileSync(file(name))).sections.filter(s => s.flags & 4).map(s => s.data);
      assert.deepEqual(code(`${policy}.o`), code(`${policy}-llvm.o`));
      const functions = decodeElf(fs.readFileSync(file(`${policy}.o`))).symbols.filter(s => s.type === 2);
      if (policy === 'thumb-widths') controlFunctions = functions;
      else {
        for (const fn of functions) {
          const before = controlFunctions.find(s => s.name === fn.name);
          assert(before && fn.size <= before.size, `${fn.name}: executable bytes grew`);
        }
        assert.equal(functions.find(s => s.name === 'stream_4').size, 28);
      }
    }
    for (const caller of ['llvm', 'owned']) {
      const prefix = `${policy}-${caller}`;
      run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'badge-compiler/tests/fixtures/codegen_fixture.ld',
        file('start.o'), file(`${caller}-oracle.o`), file('abi.o'), file(`${policy}.o`), '-o', file(`${prefix}.elf`)]);
      const trace = caller === 'llvm' && policy !== 'llvm';
      run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
        '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native',
        '-kernel', file(`${prefix}.elf`),
        ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', file(`${prefix}.trace`)] : [])]);
      if (trace) reached[policy] = (fs.readFileSync(file(`${prefix}.trace`), 'utf8').match(/^Trace /gm) ?? []).length;
    }
  }
  assert(reached['thumb-constants'] < reached['thumb-widths']);
  console.log(`RS03 reached oracle instructions ${reached['thumb-widths']} -> ${reached['thumb-constants']}`);
});
