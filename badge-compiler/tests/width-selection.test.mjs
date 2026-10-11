import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly } from '../tools/benchmark-c06.mjs';
import { decodeElf } from '../tools/boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
const policies = ['thumb-operands', 'thumb-widths'];
const target = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stdout}\n${r.stderr}`);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/width-selection-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
const bytes = file => decodeElf(fs.readFileSync(file)).sections.filter(s => s.flags & 4)
  .reduce((n, s) => n + s.size, 0);
function compile(file, policy, residency = 'global-compact') {
  run(tool('badge-codegen-sanitize'), [`--scalar-selection=${policy}`, `--register-residency=${residency}`,
    file('input.ll'), '-o', file(`${policy}.s`)]);
  run(tool('badge-asm'), [file(`${policy}.s`), '-o', file(`${policy}.o`)]);
  return fs.readFileSync(file(`${policy}.s`), 'utf8');
}
function execute(file, policy, trace = false, extra = []) {
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file(`${policy}.o`), ...extra, '-o', file(`${policy}.elf`)]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${policy}.elf`),
    ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', file(`${policy}.trace`)] : [])]);
  return trace ? (fs.readFileSync(file(`${policy}.trace`), 'utf8').match(/^Trace /gm) ?? []).length : null;
}
function production(file, sources, main) {
  fs.writeFileSync(file('source.c'), sources.map(source =>
    `#include "${path.resolve(root, '..', source)}"`).join('\n') + '\n' + main);
  run(tool('badge-cpp'), ['-I../src/shared', file('source.c'), '-o', file('source.i')]);
  run(tool('badge-ir'), ['--expression-lowering=ssa', file('source.i'), '-o', file('raw.ll')]);
  run(tool('badge-opt'), ['--passes=mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function',
    file('raw.ll'), '-o', file('input.ll')]);
}
function compare(file, label) {
  let control;
  for (const policy of policies) {
    const assembly = compile(file, policy);
    const result = { bytes: bytes(file(`${policy}.o`)), instructions: execute(file, policy, true) };
    if (!control) control = result;
    else {
      assert(result.bytes < control.bytes);
      assert(result.instructions <= control.instructions + 1, 'one-instruction semihost termination noise only');
      console.log(`PO17 ${label}: bytes ${control.bytes} -> ${result.bytes}; reached instructions ${control.instructions} -> ${result.instructions}`);
    }
    assert(!assembly.includes('@ local-scheduling:'));
  }
}
test('final-width helpers agree with LLVM for register and frame reach boundaries without setting flags', t => {
  const file = files(t);
  const r = run(tool('width_selection_test'), []);
  assert.match(r.stderr, /encoding cases passed/);
  fs.writeFileSync(file('matrix.s'), r.stdout);
  run(tool('badge-asm'), [file('matrix.s'), '-o', file('owned.o')]);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33',
    '-mthumb', '-c', file('matrix.s'), '-o', file('llvm.o')]);
  const text = object => decodeElf(fs.readFileSync(object)).sections.find(s => s.name === '.text').data;
  assert.deepEqual(text(file('owned.o')), text(file('llvm.o')));
  assert(!/\b(?:adds|subs|ands|orrs|eors|muls|lsls|lsrs|asrs)\b/.test(r.stdout));
});
test('volatile widths, opaque assembly, IT/carry windows and signed division preserve reached results', t => {
  const file = files(t);
  fs.writeFileSync(file('input.ll'), target + `@sink = global i32 0
define i32 @work(ptr %p, i32 %x, i32 %y) {
entry:
 store volatile i8 255, ptr %p
 %a = load volatile i8, ptr %p
 call void asm sideeffect "nop\\0Adsb", "~{memory}"()
 %b = add i8 %a, 2
 %wide = zext i8 %b to i32
 %div = sdiv i32 %x, %y
 %rem = srem i32 %x, %y
 %product = mul i32 %div, %y
 %sum = add i32 %product, %rem
 %good = icmp eq i32 %sum, %x
 %chosen = select i1 %good, i32 %wide, i32 99
 ret i32 %chosen
}
define i32 @main() {
entry:
 %r = call i32 @work(ptr @sink, i32 -37, i32 5)
 %a = add i64 4294967295, 2
 %b = sub i64 %a, 1
 %ok1 = icmp eq i32 %r, 1
 %ok2 = icmp eq i64 %b, 4294967296
 %all = and i1 %ok1, %ok2
 br i1 %all, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}`);
  for (const residency of ['global-compact', 'local', 'stack']) {
    let before;
    for (const policy of policies) {
      const assembly = compile(file, policy, residency);
      const memory = [...assembly.matchAll(/^(strb|ldrb)(?:\.[wn])? r\d+, \[r\d+\]$/gm)].map(m => m[1]);
      assert.deepEqual(memory, ['strb', 'ldrb']);
      assert(assembly.includes('nop\ndsb\n'));
      execute(file, policy);
      const frames = [...parseAssembly(assembly, policy)].map(([name, f]) =>
        [name, f.frame_bytes, f.saves_bytes, f.max_outgoing_bytes, f.calls]);
      if (!before) before = frames;
      else assert.deepEqual(frames, before);
    }
  }
});
test('eight-argument signed ABI transport interoperates with LLVM in both directions', t => {
  const file = files(t);
  fs.writeFileSync(file('input.ll'), target + `declare i32 @llvm_helper(i8, i32, i32, i32, i32, i32, i32, i32)
define i32 @owned_helper(i8 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h) {
entry:
 %wide = sext i8 %a to i32
 %x = add i32 %wide, %h
 ret i32 %x
}
define i32 @main() {
entry:
 %r = call i32 @llvm_helper(i8 -5, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 42)
 %ok = icmp eq i32 %r, 37
 br i1 %ok, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}`);
  fs.writeFileSync(file('helper.c'), `extern int owned_helper(signed char,int,int,int,int,int,int,int);
int llvm_helper(signed char a,int b,int c,int d,int e,int f,int g,int h) {
 return owned_helper(a,b,c,d,e,f,g,h);
}`);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
    '-Oz', '-ffreestanding', '-c', file('helper.c'), '-o', file('helper.o')]);
  for (const policy of policies) {
    compile(file, policy);
    execute(file, policy, false, [file('helper.o')]);
  }
  const r = run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-widths',
    '--scalar-selection=thumb-widths', file('input.ll'), '-o', file('protected.s')], false);
  assert.match(r.stderr, /duplicate scalar/);
});
test('guarded large-addend literals select final load/branch widths and retain independent pointer values', t => {
  const file = files(t);
  fs.writeFileSync(file('input.ll'), target + `@buf = global [65536 x i8] zeroinitializer
declare ptr @reference()
define ptr @address() {
entry:
 ret ptr getelementptr ([65536 x i8], ptr @buf, i32 0, i32 32768)
}
define i32 @main() {
entry:
 %a = call ptr @address()
 %b = call ptr @reference()
 %ok = icmp eq ptr %a, %b
 br i1 %ok, label %yes, label %no
yes:
 ret i32 0
no:
 ret i32 1
}`);
  fs.writeFileSync(file('helper.c'), `extern unsigned char buf[65536];
void *reference(void) { return buf + 32768; }`);
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
    '-Oz', '-ffreestanding', '-c', file('helper.c'), '-o', file('helper.o')]);
  let control;
  for (const policy of policies) {
    const assembly = compile(file, policy);
    assert.match(assembly, new RegExp(`ldr${policy === 'thumb-widths' ? '' : '\\.w'} r0, \\.Lcg_lit`));
    assert.match(assembly, /\.word buf\+32768/);
    execute(file, policy, false, [file('helper.o')]);
    const size = bytes(file(`${policy}.o`));
    if (!control) control = size;
    else assert(size < control);
  }
});
test('actual USB report and CRC paths shrink without an instruction-speed claim', t => {
  const file = files(t);
  production(file, ['src/shared/usb_report.c', 'src/tools/diagnostics_logic.c'], `
static uint8_t request[64], response[64];
int main(void) {
 uint8_t nonce[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
 usb_report_request(request, 0x12345678, nonce);
 if (usb_report_check_request(request, 64, 0x12345678) != USB_RECORD_OK) return 1;
 usb_report_response(response, 0x12345678, nonce, USB_REPORT_OK, 0xfeedc0de);
 if (usb_report_check_response(response, 64, request, 64, 0x12345678) != USB_RECORD_OK) return 2;
 request[8] ^= 1;
 if (usb_report_check_request(request, 64, 0x12345678) != USB_RECORD_CRC) return 3;
 if (usb_report_check_request(request, 63, 0x12345678) != USB_RECORD_LENGTH) return 4;
 return 0;
}`);
  compare(file, 'USB');
});
test('actual Wi-Fi helper paths shrink without changing filtering and clearing', t => {
  const file = files(t);
  production(file, ['src/radio/wifi_link.c'], `
static wifi_link_t link;
int main(void) {
 uint8_t a[6] = {2,3,4,5,6,7}, b[6] = {2,3,4,5,6,7}, zero[6] = {0};
 if (!equal_mac(a,b) || !zero_mac(zero) || zero_mac(a)) return 1;
 b[5] = 8;
 if (equal_mac(a,b)) return 2;
 if (!peer_mac(&link,a)) return 3;
 copy(link.report.mac,a,6);
 if (peer_mac(&link,a)) return 4;
 for (unsigned i=0;i<6;i++) link.report.peer[i]=255;
 waiting(&link);
 for (unsigned i=0;i<6;i++) if(link.report.peer[i]) return 5;
 return link.report.authorized || link.report.peer_count;
}`);
  compare(file, 'WiFi');
});
