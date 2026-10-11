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
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(r.error);
  assert.equal(r.status, 0, `${command}: ${r.stderr}`);
}
const header = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
  'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function leaf(name, linkage = 'internal', extra = '', value = '%x') {
  return `define ${linkage} void @${name}(i32 %x) {
entry:
${extra}
%p = inttoptr i32 ${0x30002040} to ptr
store volatile i32 ${value}, ptr %p, align 4
%q = inttoptr i32 ${0x30002044} to ptr
store volatile i32 1, ptr %q, align 4
ret void
}
`;
}
function wrapper(name, callee, value = 0x10000000) {
  return `define void @${name}(i32 %x) {
entry:
%p = inttoptr i32 ${0x30002008} to ptr
store volatile i32 ${value}, ptr %p, align 4
call void @${callee}(i32 %x)
%q = inttoptr i32 ${0x30002000} to ptr
store volatile i32 ${value}, ptr %q, align 4
ret void
}
`;
}
test('finalized register proofs cover hidden scratch, nested calls, ownership and conservative fallbacks', t => {
  const directory = fs.mkdtempSync(path.join(root, 'badge-compiler/build/call-preservation-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  const file = name => path.join(directory, name);
  const ir = header + leaf('safe') +
    `define internal void @nested(i32 %x) {
entry:
call void @safe(i32 %x)
ret void
}
` +
    leaf('visible', '') +
    leaf('division', 'internal', '%div = udiv i32 %x, 17', '%div') +
    leaf('assembly', 'internal', 'call void asm sideeffect "movw r2, #7", "~{memory}"()') +
    `define internal void @recursive(i32 %x) {
entry:
%zero = icmp eq i32 %x, 0
br i1 %zero, label %end, label %again
again:
call void @recursive(i32 0)
br label %end
end:
%p = inttoptr i32 ${0x30002040} to ptr
store volatile i32 %x, ptr %p, align 4
%q = inttoptr i32 ${0x30002044} to ptr
store volatile i32 1, ptr %q, align 4
ret void
}
` +
    wrapper('known', 'safe') + wrapper('transitive', 'nested') +
    wrapper('public_target', 'visible') + wrapper('hidden_scratch', 'division') +
    wrapper('opaque', 'assembly') + wrapper('recursion', 'recursive') +
    wrapper('forward', 'later') + wrapper('small', 'safe', 255) +
    wrapper('large', 'safe', 0x12345678) + leaf('later');
  fs.writeFileSync(file('input.ll'), ir);
  fs.writeFileSync(file('oracle.c'), `
extern void known(unsigned), transitive(unsigned), public_target(unsigned),
 hidden_scratch(unsigned), opaque(unsigned), recursion(unsigned), forward(unsigned),
 small(unsigned), large(unsigned);
extern unsigned check_wrapper(void(*)(unsigned),unsigned);
int main(void) {
  volatile unsigned * const p = (volatile unsigned *)0x30002000;
  void (*const functions[])(unsigned) = {known,transitive,public_target,hidden_scratch,
    opaque,recursion,forward,small,large};
  const unsigned inputs[] = {0,1,17,255,256,0x80000000u,0xffffffffu};
  unsigned n, i;
  for (n = 0; n < 9; n++) for (i = 0; i < 7; i++) {
    unsigned expected = n == 7 ? 255 : n == 8 ? 0x12345678u : 0x10000000u;
    p[0] = 0; p[2] = 0; p[16] = ~inputs[i]; p[17] = 0;
    if (check_wrapper(functions[n], inputs[i])) return 1;
    if (p[0] != expected || p[2] != expected || p[17] != 1 ||
      p[16] != (n == 3 ? inputs[i] / 17 : inputs[i])) return 2;
  }
  return 0;
}
`);
  fs.writeFileSync(file('abi.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
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
  let controlFrames;
  const reached = {};
  for (const policy of ['thumb-widths', 'thumb-constants']) {
    run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, file('input.ll'), '-o', file(`${policy}.s`)]);
    const assembly = fs.readFileSync(file(`${policy}.s`), 'utf8');
    const frames = parseAssembly(assembly, policy);
    if (policy === 'thumb-widths') controlFrames = frames;
    else {
      for (const [name, frame] of frames) {
        const before = controlFrames.get(name);
        for (const key of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
          assert(frame[key] <= before[key], `${name}: ${key} grew`);
        assert.deepEqual(frame.calls, before.calls);
      }
      const functions = new Map(assembly.split(/(?=\.section \.text\.)/).slice(1)
        .map(text => [text.match(/^\.section \.text\.([^,]+)/)[1], text]));
      for (const name of ['known', 'transitive', 'small', 'large']) {
        const text = functions.get(name);
        assert.match(text, /r2 retained through proven internal call/);
        assert.equal((text.match(/^mov(?:w|t|s)?(?:\.w|\.n)? r2,/gm) ?? []).length, name === 'large' ? 2 : 1);
        assert.equal((text.match(/^str\.n r2, \[r4,/gm) ?? []).length, 2);
        assert.match(text, /push\.n \{r4, lr\}/);
      }
      for (const name of ['public_target', 'hidden_scratch', 'opaque', 'recursion', 'forward'])
        assert(!functions.get(name).includes('r2 retained through'), name);
      const divisionMask = functions.get('division').match(/finalized-register-mentions: known 1 mask (\d+)/);
      assert(divisionMask && (Number(divisionMask[1]) & 4), 'hidden machine r2 scratch not summarized');
    }
    run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
    run(clang, [...flags, '-c', file(`${policy}.s`), '-o', file(`${policy}-llvm.o`)]);
    const code = name => decodeElf(fs.readFileSync(file(name))).sections.filter(s => s.flags & 4).map(s => s.data);
    assert.deepEqual(code(`${policy}.o`), code(`${policy}-llvm.o`));
    for (const caller of ['llvm', 'owned']) {
      const prefix = `${policy}-${caller}`;
      run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'badge-compiler/tests/fixtures/codegen_fixture.ld',
        file('start.o'), file(`${caller}-oracle.o`), file('abi.o'), file(`${policy}.o`), '-o', file(`${prefix}.elf`)]);
      const trace = caller === 'llvm';
      run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
        '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${prefix}.elf`),
        ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', file(`${prefix}.trace`)] : [])]);
      if (trace) reached[policy] = (fs.readFileSync(file(`${prefix}.trace`), 'utf8').match(/^Trace /gm) ?? []).length;
    }
  }
  assert(reached['thumb-constants'] < reached['thumb-widths']);
  console.log(`RS03 preservation oracle instructions ${reached['thumb-widths']} -> ${reached['thumb-constants']}`);
});
