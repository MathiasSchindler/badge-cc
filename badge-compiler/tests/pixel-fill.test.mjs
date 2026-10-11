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
const flags = ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-Oz', '-ffreestanding', '-fno-builtin'];
function run(command, args) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(r.error);
  assert.equal(r.status, 0, `${command} ${args.join(' ')}: ${r.stdout}\n${r.stderr}`);
}
function fill(name, { indirect = false, opaque = false, wide = false, live = false, four = false } = {}) {
  return `define void @${name}(i32 %pixel, i32 %length${indirect ? ', ptr %callback' : ''}) {
entry:
call void @begin_stream(i32 73)
br label %header
header:
%count = phi i32 [%length, %entry], [%next, %body]
%next = sub i32 %count, 1
%more = icmp ne i32 %count, 0
br i1 %more, label %body, label %done
body:
%high = lshr i32 %pixel, 8
call void ${indirect ? '%callback' : '@put_scalar'}(i32 %high)
${opaque ? 'call void asm sideeffect "nop", "~{memory}"()' : ''}
%low = and i32 %pixel, ${wide ? 65535 : 255}
call void ${four ? '@put_four' : indirect ? '%callback' : '@put_scalar'}(i32 %low${four ? ', i32 %pixel, i32 %count, i32 %high' : ''})
${live ? '%address = inttoptr i32 805328900 to ptr\nstore volatile i32 %high, ptr %address, align 4' : ''}
br label %header
done:
ret void
}
`;
}
test('repeated scalar streams preserve pixel pairs, unsigned countdowns and clobbering ABI edges', t => {
  const dir = fs.mkdtempSync(path.join(root, 'badge-compiler/build/pixel-fill-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const file = name => path.join(dir, name);
  const variants = [['emit_pairs', {}], ['emit_indirect', { indirect: true }],
    ['emit_barrier', { opaque: true }], ['emit_halfword', { wide: true }],
    ['emit_live', { live: true }], ['emit_four', { four: true }]];
  const shifts = [['lshr', 1], ['lshr', 8], ['lshr', 31], ['ashr', 1],
    ['ashr', 31], ['shl', 0], ['shl', 8], ['shl', 31]];
  const header = 'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
    'target triple = "thumbv8m.main-unknown-none-eabi"\n';
  fs.writeFileSync(file('input.ll'), header +
    'declare void @begin_stream(i32)\ndeclare void @put_scalar(i32)\n' +
    'declare void @put_four(i32, i32, i32, i32)\n' +
    variants.map(([n, options]) => fill(n, options)).join('') + `define i32 @bounded_store(ptr %out, i32 %pixel, i32 %length, i32 %limit) {
entry:
br label %header
header:
%count = phi i32 [%length, %entry], [%next, %body]
%budget = phi i32 [%limit, %entry], [%less, %body]
%cursor = phi ptr [%out, %entry], [%advance, %body]
%more = icmp ne i32 %count, 0
br i1 %more, label %check, label %done
check:
%allowed = icmp ne i32 %budget, 0
br i1 %allowed, label %body, label %done
body:
%high = lshr i32 %pixel, 8
%h = trunc i32 %high to i8
store volatile i8 %h, ptr %cursor, align 1
%lowp = getelementptr i8, ptr %cursor, i32 1
%l = trunc i32 %pixel to i8
store volatile i8 %l, ptr %lowp, align 1
%advance = getelementptr i8, ptr %cursor, i32 2
%next = sub i32 %count, 1
%less = sub i32 %budget, 1
br label %header
done:
ret i32 %count
}
` +
    shifts.map(([op, n], i) => `define i32 @shift_${i}(i32 %x) {
entry:
%value = ${op} i32 %x, ${n}
call void @put_scalar(i32 %value)
ret i32 %value
}
`).join(''));
  fs.writeFileSync(file('abi.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
.global check_fill
.thumb_func
check_fill:
push.w {r3-r11, lr}
mov r12, r0
mov r0, r1
mov r1, r2
mov r2, r3
${Array.from({ length: 8 }, (_, i) => `movw r${i + 4}, #${(i + 4) * 17}`).join('\n')}
blx r12
${Array.from({ length: 8 }, (_, i) => `cmp r${i + 4}, #${(i + 4) * 17}\nbne failed`).join('\n')}
movw r0, #0
b finished
failed:
movw r0, #1
finished:
pop.w {r3-r11, pc}
.global begin_stream
.thumb_func
begin_stream:
b put_scalar
.global put_four
.thumb_func
put_four:
b put_scalar
.global put_scalar
.thumb_func
put_scalar:
movw r1, #20480
movt r1, #12288
mov r12, sp
and.w r12, r12, #7
cmp r12, #0
beq aligned
movw r3, #1
str.w r3, [r1, #2048]
aligned:
ldr.n r2, [r1]
add.w r3, r1, #4
str.w r0, [r3, r2, lsl #2]
adds r2, #1
str.n r2, [r1]
${[0, 1, 2, 3, 12].map(r => `movw r${r}, #${0xa100 + r}`).join('\n')}
cmp r0, r1
bx lr
`);
  fs.writeFileSync(file('oracle.c'), `
typedef void (*emitter)(unsigned,unsigned,void(*)(unsigned));
extern unsigned check_fill(emitter,unsigned,unsigned,void(*)(unsigned));
extern void put_scalar(unsigned);
extern unsigned bounded_store(volatile unsigned char *,unsigned,unsigned,unsigned);
${variants.map(([name]) => `extern void ${name}(unsigned,unsigned,void(*)(unsigned));`).join('\n')}
${shifts.map((_, i) => `extern unsigned shift_${i}(unsigned);`).join('\n')}
int main(void) {
  volatile unsigned *events = (volatile unsigned *)0x30005000;
  emitter emitters[] = {${variants.map(([name]) => name).join(',')}};
  unsigned colors[] = {0,1,255,256,0x1234,0xf800,0x07e0,0xffff,0x80000000u,0xffffffffu};
  unsigned counts[] = {0,1,2,7,127,128,255};
  unsigned p,c,k,i,expected;
  unsigned boundaries[] = {0,1,2,0x7fffffffu,0x80000000u,0xffffffffu};
  volatile unsigned char buffer[8];
  unsigned (*shift[])(unsigned) = {${shifts.map((_, i) => `shift_${i}`).join(',')}};
  for (p=0;p<6;p++) for(c=0;c<10;c++) for(k=0;k<7;k++) {
    events[0]=0; events[512]=0; events[513]=0;
    if(check_fill(emitters[p],colors[c],counts[k],put_scalar)||events[512]) return 1;
    if(events[0]!=1+2*counts[k]||events[1]!=73) return 2;
    for(i=0;i<counts[k];i++) {
      if(events[2+2*i]!=(colors[c]>>8)) return 3;
      if(events[3+2*i]!=(colors[c]&(p==3?65535:255))) return 20+p*10+c;
    }
    if(p==4&&counts[k]&&events[513]!=(colors[c]>>8)) return 5;
  }
  for(c=0;c<10;c++) for(i=0;i<${shifts.length};i++) {
    events[0]=0;
    switch(i) {
${shifts.map(([op, n], i) => `case ${i}: expected=${op === 'ashr' ?
    `(unsigned)((int)colors[c]>>${n})` : `colors[c]${op === 'shl' ? '<<' : '>>'}${n}`}; break;`).join('\n')}
      default: return 6;
    }
    if(shift[i](colors[c])!=expected||events[1]!=expected||events[0]!=1) return 7;
  }
  for(c=0;c<10;c++) for(k=0;k<6;k++) for(p=0;p<4;p++) {
    expected=boundaries[k]<p?boundaries[k]:p;
    for(i=0;i<8;i++) buffer[i]=0xa5;
    if(bounded_store(buffer,colors[c],boundaries[k],p)!=boundaries[k]-expected) return 8;
    for(i=0;i<expected;i++)
      if(buffer[2*i]!=(unsigned char)(colors[c]>>8)||buffer[2*i+1]!=(unsigned char)colors[c]) return 9;
    for(i=2*expected;i<8;i++) if(buffer[i]!=0xa5) return 10;
  }
  return 0;
}
`);
  run(tool('badge-asm'), [file('abi.s'), '-o', file('abi.o')]);
  run(tool('badge-asm'), ['badge-compiler/tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  for (const caller of ['llvm', 'owned'])
    run(caller === 'llvm' ? clang : 'bash', [
      ...(caller === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
      ...flags, '-c', file('oracle.c'), '-o', file(`${caller}-oracle.o`)]);
  const reached = {};
  let control;
  for (const policy of ['thumb-widths', 'thumb-constants', 'llvm']) {
    if (policy === 'llvm')
      run(clang, [...flags, '-c', file('input.ll'), '-o', file(`${policy}.o`)]);
    else {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, file('input.ll'), '-o', file(`${policy}.s`)]);
      const text = fs.readFileSync(file(`${policy}.s`), 'utf8');
      const frames = parseAssembly(text, policy);
      if (policy === 'thumb-widths') control = frames;
      else {
        for (const [name, frame] of frames) {
          for (const key of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
            assert(frame[key] <= control.get(name)[key], `${name}: ${key} grew`);
          assert.deepEqual(frame.calls, control.get(name).calls);
        }
        assert.match(text, /@ scalar-call-forwarding: saved 2/);
        assert.match(text, /@ scalar-shift: saved 2/);
        assert.match(text, /@ scalar-extract: saved 2/);
      }
      run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
      run(clang, [...flags, '-c', file(`${policy}.s`), '-o', file(`${policy}-llvm.o`)]);
      const code = name => decodeElf(fs.readFileSync(file(name))).sections.filter(s => s.flags & 4).map(s => s.data);
      assert.deepEqual(code(`${policy}.o`), code(`${policy}-llvm.o`));
      if (policy === 'thumb-constants')
        assert.equal(decodeElf(fs.readFileSync(file(`${policy}.o`))).symbols.find(s => s.name === 'emit_pairs').size, 34);
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
  console.log(`RS05 reached oracle instructions ${reached['thumb-widths']} -> ${reached['thumb-constants']}`);
});
