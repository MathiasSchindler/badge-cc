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
function loop(name, { nops = 6, eq = false, reversed = false, clobber = '', opaque = false,
  live = false, flagged = false, extra = false } = {}) {
  return `define ${live ? 'i32' : 'void'} @${name}(i32 %initial) {
entry:
br label %start
start:
br label %header
header:
%count = phi i32 ${reversed ? '[%next, %body], [%initial, %start]' : '[%initial, %start], [%next, %body]'}
%next = sub ${flagged ? 'nuw ' : ''}i32 %count, 1
%test = icmp ${eq ? 'eq' : 'ne'} i32 %count, 0
br i1 %test, label %${eq ? 'exit' : 'body'}, label %${eq ? 'body' : 'exit'}
body:
${extra ? 'call void @observer(i32 %count)' : ''}
call void asm sideeffect "${opaque ? 'nop\\0Adsb sy' : Array(nops).fill('nop').join('\\0A')}", "${clobber}"()
br label %header
exit:
ret ${live ? 'i32 %next' : 'void'}
}
`;
}
function symbolBytes(elf, name) {
  const symbol = elf.symbols.find(s => s.name === name);
  assert(symbol);
  const section = elf.sections[symbol.section];
  const offset = (symbol.value & ~1) - section.address;
  return Buffer.from(section.data).subarray(offset, offset + symbol.size);
}
// Independent Thumb decoding checks bounded prefixes of the actual emitted code.
function executePrefix(code, input, budget) {
  let pc = 0, counter = input >>> 0, zero = false, nops = 0, iterations = 0;
  for (let steps = 0; steps < 100000; steps++) {
    assert(pc >= 0 && pc + 2 <= code.length && !(pc & 1));
    const op = code.readUInt16LE(pc);
    if ((op & 0xf500) === 0xb100) {
      assert.equal(op & 7, 0);
      const take = (counter === 0) !== Boolean(op & 0x800);
      pc += take ? 4 + ((op >> 3) & 31) * 2 + ((op >> 9) & 1) * 64 : 2;
    } else if (op === 0xbf00) {
      nops++;
      pc += 2;
    } else if (op === 0x1e40 || op === 0x3801) {
      counter = (counter - 1) >>> 0;
      zero = counter === 0;
      iterations++;
      pc += 2;
    } else if ((op & 0xff00) === 0xd100) {
      const displacement = (op & 255) < 128 ? op & 255 : (op & 255) - 256;
      pc += zero ? 2 : 4 + displacement * 2;
      if (!zero && iterations === budget) return { counter, nops, iterations, returned: false };
    } else if (op === 0x4770) {
      return { counter, nops, iterations, returned: true };
    } else assert.fail(`unexpected counted-leaf opcode ${op.toString(16)}`);
  }
  assert.fail('bounded prefix did not terminate');
}
test('closed counted assembly leaves retain exact unsigned iteration prefixes, ABI and explicit NOPs', t => {
  const dir = fs.mkdtempSync(path.join(root, 'badge-compiler/build/counted-delay-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  const file = name => path.join(dir, name);
  const accepted = [['pause_six', {}], ['pause_one', { nops: 1 }],
    ['pause_seven', { nops: 7, eq: true, reversed: true }],
    ['pause_barrier', { nops: 2, clobber: '~{memory},~{cc}' }],
    ['pause_max', { nops: 60 }]];
  const rejected = [['pause_long', { nops: 61 }], ['pause_opaque', { opaque: true }],
    ['pause_live', { live: true }], ['pause_flagged', { flagged: true }],
    ['pause_call', { extra: true }]];
  fs.writeFileSync(file('input.ll'),
    'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n' +
    'target triple = "thumbv8m.main-unknown-none-eabi"\ndeclare void @observer(i32)\n' +
    [...accepted, ...rejected].map(([name, opts]) => loop(name, opts)).join(''));
  fs.writeFileSync(file('abi.s'), `.syntax unified
.cpu cortex-m33
.thumb
.text
.global check_pause
.thumb_func
check_pause:
push.w {r3-r11, lr}
mov r12, r0
mov r0, r1
${Array.from({ length: 8 }, (_, i) => `movw r${i + 4}, #${(i + 4) * 17}`).join('\n')}
blx r12
${Array.from({ length: 8 }, (_, i) => `cmp r${i + 4}, #${(i + 4) * 17}\nbne failed`).join('\n')}
movw r0, #0
b finished
failed:
movw r0, #1
finished:
pop.w {r3-r11, pc}
.global observer
.thumb_func
observer:
movw r1, #20480
movt r1, #12288
str.n r0, [r1]
movw r0, #123
movw r2, #456
movw r3, #789
movw r12, #987
cmp r0, r1
bx lr
`);
  fs.writeFileSync(file('oracle.c'), `
typedef void (*pause_fn)(unsigned);
extern unsigned check_pause(pause_fn,unsigned);
${[...accepted, ...rejected.filter(([n]) => !['pause_live', 'pause_flagged'].includes(n))]
    .map(([n]) => `extern void ${n}(unsigned);`).join('\n')}
extern unsigned pause_live(unsigned);
extern void pause_flagged(unsigned);
int main(void) {
  pause_fn functions[] = {${[...accepted, ...rejected.filter(([n]) =>
    !['pause_live', 'pause_flagged'].includes(n))].map(([n]) => n).join(',')}};
  unsigned counts[] = {0,1,2,7,31,127,128,255};
  unsigned f,k;
  volatile unsigned *observed = (volatile unsigned *)0x30005000;
  for(f=0;f<${accepted.length + rejected.length - 2};f++) for(k=0;k<8;k++) {
    *observed=0xdeadbeefu;
    if(check_pause(functions[f],counts[k])) return 1;
    if(f==${accepted.length + rejected.length - 3} && *observed!=(counts[k]?1:0xdeadbeefu)) return 2;
  }
  for(k=0;k<8;k++) if(pause_live(counts[k])!=0xffffffffu) return 3;
  /* Flagged arithmetic retains its original lowering. */
  for(k=1;k<8;k++) if(check_pause(pause_flagged,counts[k])) return 4;
  return 0;
}
`);
  const clang = process.env.LLVM_CLANG ?? 'clang';
  run(tool('badge-asm'), [file('abi.s'), '-o', file('abi.o')]);
  run(tool('badge-asm'), ['badge-compiler/tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const caller of ['llvm', 'owned'])
    run(caller === 'llvm' ? clang : 'bash', [
      ...(caller === 'llvm' ? [] : ['badge-compiler/scripts/owned-cc.sh']),
      ...flags, '-c', file('oracle.c'), '-o', file(`${caller}-oracle.o`)]);
  const reached = {};
  let control;
  for (const policy of ['thumb-widths', 'thumb-constants', 'llvm']) {
    if (policy === 'llvm') run(clang, [...flags, '-c', file('input.ll'), '-o', file(`${policy}.o`)]);
    else {
      run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, file('input.ll'), '-o', file(`${policy}.s`)]);
      const assembly = fs.readFileSync(file(`${policy}.s`), 'utf8');
      const frames = parseAssembly(assembly, policy);
      if (policy === 'thumb-widths') control = frames;
      else for (const [name, frame] of frames) {
        for (const key of ['frame_bytes', 'saves_bytes', 'max_outgoing_bytes'])
          assert(frame[key] <= control.get(name)[key], `${name}: ${key} grew`);
        assert.deepEqual(frame.calls, control.get(name).calls);
        const marked = assembly.split(`\n${name}:\n`)[1].split('.size ')[0].includes('@ counted-leaf:');
        assert.equal(marked, accepted.some(([n]) => n === name), name);
        if (marked) assert.equal(frame.frame_bytes + frame.saves_bytes, 0);
      }
      run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
      run(clang, [...flags, '-c', file(`${policy}.s`), '-o', file(`${policy}-llvm.o`)]);
      const code = name => decodeElf(fs.readFileSync(file(name))).sections.filter(s => s.flags & 4).map(s => s.data);
      assert.deepEqual(code(`${policy}.o`), code(`${policy}-llvm.o`));
      if (policy === 'thumb-constants') {
        const elf = decodeElf(fs.readFileSync(file(`${policy}.o`)));
        for (const [name, options] of accepted) {
          const nops = options.nops ?? 6, code = symbolBytes(elf, name);
          assert.equal(code.length, 8 + 2 * nops);
          for (const count of [0,1,2,3,7,255,0x7fffffff,0x80000000,0xffffffff]) {
            const executed = Math.min(count, 3);
            assert.deepEqual(executePrefix(code, count, 3), {
              counter: (count - executed) >>> 0, nops: nops * executed,
              iterations: executed, returned: count <= 3,
            });
          }
          for (let count = 0; count <= 255; count++)
            assert.deepEqual(executePrefix(code, count, 255), {
              counter: 0, nops: nops * count, iterations: count, returned: true,
            });
          if (name === 'pause_six') {
            assert.equal(4000000 * nops, 24000000);
            assert.equal(24000000 / 150000000, 0.16);
          }
        }
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
      if (trace) {
        const text = fs.readFileSync(file(`${prefix}.trace`), 'utf8');
        reached[policy] = (text.match(/^Trace /gm) ?? []).length;
        const elf = decodeElf(fs.readFileSync(file(`${prefix}.elf`)));
        for (const [name, opts] of accepted) {
          const symbol = elf.symbols.find(s => s.name === name);
          const code = symbolBytes(elf, name);
          const base = symbol.value & ~1;
          let nops = 0;
          for (let i = 0; i < code.length; i += 2) if (code.readUInt16LE(i) === 0xbf00) {
            const address = (base + i).toString(16).padStart(8, '0');
            nops += (text.match(new RegExp(`/0*${address}/`, 'g')) ?? []).length;
          }
          assert.equal(nops, (opts.nops ?? 6) * (0+1+2+7+31+127+128+255), `${name}: reached NOPs`);
        }
      }
    }
  }
  assert(reached['thumb-constants'] < reached['thumb-widths']);
  for (const replacement of ['"nop", "~{r0}"()', '"nop $0", "r"(i32 %count)']) {
    fs.writeFileSync(file('bad.ll'), fs.readFileSync(file('input.ll'), 'utf8')
      .replace('"nop", ""()', replacement));
    fs.writeFileSync(file('preserved.s'), 'existing-output\n');
    const failure = spawnSync(tool('badge-codegen-sanitize'),
      ['--scalar-selection=thumb-constants', file('bad.ll'), '-o', file('preserved.s')],
      { cwd: root, encoding: 'utf8', timeout: 120000 });
    assert.ifError(failure.error);
    assert.notEqual(failure.status, 0);
    assert.match(failure.stderr, /unsupported inline assembly/);
    assert.equal(fs.readFileSync(file('preserved.s'), 'utf8'), 'existing-output\n');
  }
  console.log(`RS06 reached oracle instructions ${reached['thumb-widths']} -> ${reached['thumb-constants']}`);
});
