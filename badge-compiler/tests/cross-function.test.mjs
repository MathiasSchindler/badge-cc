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
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status === 0, success, `${command}: ${r.stdout}\n${r.stderr}`);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/cross-function-'));
  t.after(() => fs.rmSync(dir, { recursive: true }));
  return name => path.join(dir, name);
}
const target = 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
  'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n';
function optimize(file, source) {
  fs.writeFileSync(file('input.ll'), target + source);
  run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('candidate.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
  run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('candidate.ll'), '-o', file('twice.ll')]);
  assert.deepEqual(fs.readFileSync(file('twice.ll')), fs.readFileSync(file('candidate.ll')));
  return fs.readFileSync(file('candidate.ll'), 'utf8');
}
function production(file, sources, main) {
  fs.writeFileSync(file('source.c'), sources.map(source =>
    `#include "${path.resolve(root, '..', source)}"`).join('\n') + '\n' + main);
  run(tool('badge-cpp'), ['-I../src/shared', file('source.c'), '-o', file('source.i')]);
  run(tool('badge-ir'), ['--expression-lowering=ssa', file('source.i'), '-o', file('raw.ll')]);
  run(tool('badge-opt'), [file('raw.ll'), '-o', file('input.ll')]);
  run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('candidate.ll')]);
  run(process.env.LLVM_OPT ?? 'opt', ['-passes=verify', '-disable-output', file('candidate.ll')]);
}
function execute(file, role, trace = false) {
  run(tool('badge-codegen-sanitize'), ['--scalar-selection=thumb-operands', file(`${role === 'control' ? 'input' : 'candidate'}.ll`),
    '-o', file(`${role}.s`)]);
  const assembly = fs.readFileSync(file(`${role}.s`), 'utf8'), frames = parseAssembly(assembly, role);
  run(tool('badge-asm'), [file(`${role}.s`), '-o', file(`${role}.o`)]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/codegen_fixture.ld',
    file('start.o'), file(`${role}.o`), '-o', file(`${role}.elf`)]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file(`${role}.elf`),
    ...(trace ? ['-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', file(`${role}.trace`)] : [])]);
  const count = trace ? (fs.readFileSync(file(`${role}.trace`), 'utf8').match(/^Trace /gm) ?? []).length : null;
  return { frames, count, bytes: decodeElf(fs.readFileSync(file(`${role}.o`))).sections
    .filter(s => s.flags & 4).reduce((n, s) => n + s.size, 0) };
}
const encoder = `define internal i32 @encode(i32 %x, i32 %mode) {
entry:
 %which = icmp eq i32 %mode, 0
 br i1 %which, label %low, label %high
low:
 %a = and i32 %x, 255
 ret i32 %a
high:
 %b = lshr i32 %x, 8
 ret i32 %b
}\n`;
test('constant specialization removes dead paths without duplicating effects and lowers whole-module code', t => {
  const file = files(t);
  const source = encoder + `define i32 @main() {
entry:
 %a = call i32 @encode(i32 305419896, i32 0)
 %b = icmp eq i32 %a, 120
 br i1 %b, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert(!result.includes('%which = icmp'));
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes);
  assert(b.count < a.count);
  console.log(`PO16 encoder oracle: ${a.count} -> ${b.count} reached instructions`);
});
test('straight-line inlining retains volatile order and caller ABI, charging removed helper', t => {
  const file = files(t);
  const result = optimize(file, `@sink = global i32 0
define internal i32 @emit(i32 %x) {
entry:
 store volatile i32 %x, ptr @sink
 %next = add i32 %x, 1
 ret i32 %next
}
define i32 @main() {
entry:
 %a = call i32 @emit(i32 41)
 %value = load volatile i32, ptr @sink
 %ok1 = icmp eq i32 %a, 42
 %ok2 = icmp eq i32 %value, 41
 %ok = and i1 %ok1, %ok2
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`);
  assert(!result.includes('define internal i32 @emit'));
  execute(file, 'control'); execute(file, 'candidate');
});
test('identical non-address-taken helpers share one body without changing call multiplicity', t => {
  const file = files(t);
  const helper = name => `define internal void @${name}(i32 %x) {
entry:
 store volatile i32 %x, ptr @sink
 call void asm sideeffect "nop", "~{memory}"()
 ret void
}\n`;
  const result = optimize(file, '@sink = global i32 0\n' + helper('a') + helper('b') + `define i32 @main() {
entry:
 call void @a(i32 1)
 call void @b(i32 2)
 %value = load volatile i32, ptr @sink
 %ok = icmp eq i32 %value, 2
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`);
  assert(!result.includes('define internal void @b'));
  assert.equal((result.match(/call void @a/g) ?? []).length, 2);
  const a = execute(file, 'control'), b = execute(file, 'candidate');
  assert(b.bytes < a.bytes);
});
test('public, address-taken, recursive and noinline helpers remain unchanged; alwaysinline fails explicitly', t => {
  const file = files(t);
  for (const source of [
    encoder.replace('internal ', ''),
    encoder + '@address = global ptr @encode\n',
    encoder.replace('%a = and i32 %x, 255', '%a = call i32 @encode(i32 %x, i32 %mode)'),
    encoder.replace(') {', ') noinline {'),
    encoder.replace(') {', ') #7 {') + 'attributes #7 = { noinline }\n',
  ]) {
    const full = source + `define i32 @main() {\nentry:\n%r = call i32 @encode(i32 17, i32 0)\nret i32 %r\n}`;
    assert.equal(optimize(file, full), target + full);
  }
  fs.writeFileSync(file('input.ll'), target + encoder.replace(') {', ') alwaysinline {'));
  fs.writeFileSync(file('protected.ll'), 'original-output');
  const r = run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('protected.ll')], false);
  assert.match(r.stderr, /alwaysinline.*supported.*contract/);
  assert.equal(fs.readFileSync(file('protected.ll'), 'utf8'), 'original-output');
});
test('resource exhaustion fails without partial publication', t => {
  const file = files(t);
  fs.writeFileSync(file('input.ll'), target + encoder + 'define i32 @main() {\nentry:\n%r = call i32 @encode(i32 17, i32 0)\nret i32 %r\n}');
  fs.writeFileSync(file('protected.ll'), 'original-output');
  const r = run(tool('badge-opt-cross-budget'), ['--passes=cross-function', file('input.ll'), '-o', file('protected.ll')], false);
  assert.match(r.stderr, /cross-function work limit exceeded/);
  assert.equal(fs.readFileSync(file('protected.ll'), 'utf8'), 'original-output');
});
test('a beneficial reduced-ABI clone pays for its generic helper and preserves dynamic calls', t => {
  const file = files(t);
  let calls = '', sum = '%dynamic';
  for (let i = 0; i < 20; i++) {
    calls += `%v${i} = call i32 @encode(i32 %x, i32 0)\n`;
    calls += `%sum${i} = add i32 ${sum}, %v${i}\n`;
    sum = `%sum${i}`;
  }
  const source = encoder + `define i32 @batch(i32 %x, i32 %mode) {
entry:
 %dynamic = call i32 @encode(i32 %x, i32 %mode)
 ${calls}
 ret i32 ${sum}
}
define i32 @main() {
entry:
 %r = call i32 @batch(i32 305419896, i32 1)
 %ok = icmp eq i32 %r, 1195446
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert.match(result, /define internal i32 @encode/);
  assert.match(result, /define internal i32 @"\.badge\.opt\.\d+" \( i32 %x \)/);
  assert.match(result, /call i32 @encode\(i32 %x, i32 %mode\)/);
  const a = execute(file, 'control'), b = execute(file, 'candidate');
  assert(b.bytes < a.bytes);
});
test('mixed fifth/eighth arguments, signed narrow transport and quoted operands preserve reduced ABI', t => {
  const file = files(t);
  const source = `define internal i32 @"sum"(i8 signext %"a", i32 %b, i32 %c, i32 %d,
i32 %e, i32 %f, i32 %g, i32 %h) {
entry:
 %wide = sext i8 %"a" to i32
 %ab = add i32 %wide, %b
 %cd = add i32 %c, %d
 %ef = add i32 %e, %f
 %gh = add i32 %g, %h
 %first = add i32 %ab, %cd
 %second = add i32 %ef, %gh
 %all = add i32 %first, %second
 ret i32 %all
}
define i32 @main() {
entry:
 %r = call i32 @"sum"(i8 signext 255, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7)
 %ok = icmp eq i32 %r, 27
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source.replace('i32 %d,\ni32', 'i32 %d, i32'));
  const a = execute(file, 'control'), b = execute(file, 'candidate');
  assert(b.bytes < a.bytes);
  assert(!result.includes('call i32 @sum(i8'));
});
test('canonical quoted address references and mutual recursion prohibit specialization', t => {
  const file = files(t);
  const source = encoder + '@address = global ptr @"encode"\n' +
    'define i32 @main() {\nentry:\n%r = call i32 @"encode"(i32 17, i32 0)\nret i32 %r\n}';
  assert.equal(optimize(file, source), target + source);
  const mutual = encoder.replace('%a = and i32 %x, 255', '%a = call i32 @other(i32 %x, i32 %mode)') +
    'define internal i32 @other(i32 %x, i32 %mode) {\nentry:\n%r = call i32 @"encode"(i32 %x, i32 %mode)\nret i32 %r\n}' +
    '\ndefine i32 @main() {\nentry:\nret i32 0\n}';
  assert.equal(optimize(file, mutual), target + mutual);
});
test('rejected clone attempts release names/output before subsequent candidates', t => {
  const file = files(t);
  const source = encoder + `define internal i32 @small(i32 %x) {
entry:
 %r = add i32 %x, 7
 ret i32 %r
}
define i32 @main() {
entry:
 %a = call i32 @encode(i32 1234, i32 0)
 %b = call i32 @encode(i32 %a, i32 1)
 %r = call i32 @small(i32 %b)
 %ok = icmp eq i32 %r, 7
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  optimize(file, source);
  execute(file, 'control'); execute(file, 'candidate');
});
test('musttail and unsupported forced inline fail even with completion markers or later functions', t => {
  const file = files(t);
  for (const source of [
    encoder.replace(') {', ') #8 {') + 'attributes #8 = { alwaysinline }\n; badge-cross-function: bounded selection complete\n',
    encoder + 'define i32 @main() {\nentry:\n%r = musttail call i32 @encode(i32 17, i32 0)\nret i32 %r\n}',
  ]) {
    fs.writeFileSync(file('input.ll'), target + source);
    const r = run(tool('badge-opt-sanitize'), ['--passes=cross-function', file('input.ll'), '-o', file('out.ll')], false);
    assert.match(r.stderr, /(?:musttail|alwaysinline).*supported.*contract/);
  }
});
test('actual USB report/CRC call graph preserves request/response bytes and rejection behavior', t => {
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
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes);
  assert(b.count <= a.count);
  console.log(`PO16 production USB oracle: ${a.count} -> ${b.count} reached instructions`);
});
test('actual Wi-Fi link helper call graph preserves peer filtering, clearing and skipped comparisons', t => {
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
  if (link.report.authorized || link.report.peer_count) return 6;
  return 0;
}`);
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes);
  assert(b.count <= a.count);
  console.log(`PO16 production WiFi oracle: ${a.count} -> ${b.count} reached instructions`);
});
test('tail-forwarding helpers eliminate one call boundary without native tail-frame assumptions', t => {
  const file = files(t);
  const source = `@sink = global i32 0
define i32 @external(i32 %x) {
entry:
 store volatile i32 %x, ptr @sink
 ret i32 %x
}
define internal i32 @forward(i32 %x) {
entry:
 %r = call i32 @external(i32 %x)
 ret i32 %r
}
define i32 @main() {
entry:
 %r = call i32 @forward(i32 42)
 %value = load volatile i32, ptr @sink
 %a = icmp eq i32 %r, 42
 %b = icmp eq i32 %value, 42
 %ok = and i1 %a, %b
 br i1 %ok, label %yes, label %no
yes:\nret i32 0\nno:\nret i32 1\n}`;
  const result = optimize(file, source);
  assert(!result.includes('define internal i32 @forward'));
  assert.match(result, /badge-cross-decision: tail_forwarder_inlined/);
  const a = execute(file, 'control', true), b = execute(file, 'candidate', true);
  assert(b.bytes < a.bytes && b.count < a.count);
});
test('untracked assembly symbol references retain every helper and call ABI', t => {
  const file = files(t);
  const source = encoder + `define i32 @main() {
entry:
 call void asm sideeffect "bl encode", "~{lr}"()
 %r = call i32 @encode(i32 17, i32 0)
 ret i32 %r
}`;
  assert.equal(optimize(file, source), target + source);
});
