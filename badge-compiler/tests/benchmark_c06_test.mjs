import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { parseAssembly, parseMap, stackGraph, timerMetrics, verifyUf2, compareReports } from '../tools/benchmark-c06.mjs';

const dir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const target = 'target triple = "thumbv8m.main-unknown-none-eabi"\n' +
  'target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"\n';
function fixture(t) {
  const folder = fs.mkdtempSync(path.join(dir, 'build/metrics-test-')), files = new Set();
  t.after(() => {
    for (const file of files) {
      try { fs.unlinkSync(file); } catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
    fs.rmdirSync(folder);
  });
  return name => { const file = path.join(folder, name); files.add(file); return file; };
}
function run(tool, args) {
  const result = spawnSync(path.join(dir, 'build', tool), args, { encoding: 'utf8', timeout: 15000 });
  assert.ifError(result.error);
  return result;
}
test('native IR counts, multiline switch, pass outcomes, volatile preservation and checked output', t => {
  const file = fixture(t), input = file('input.ll'), output = file('report.json'), ready = file('ready.ll');
  const source = target + `define i32 @f(i32 %x, ptr %p) {
entry:
%slot = alloca i32, align 4
%a = add i32 2, 3
%b = add nsw i32 %x, 1
%c = shl i32 1, 32
%load = load volatile i32, ptr %p, align 4
store volatile i32 %a, ptr %p, align 4
br i1 true, label %left, label %right
left:
br label %merge
right:
br label %merge
merge:
%join = phi i32 [ %a, %left ], [ %x, %right ]
switch i32 %join, label %exit [
i32 1, label %exit
]
exit:
ret i32 %join
}
`;
  fs.writeFileSync(input, source);
  const legacy = '--passes=constant-fold,aggregate-memory,wide-division';
  assert.equal(run('badge-opt', [legacy, input, '-o', ready]).status, 0);
  for (const tool of ['badge-ir-metrics', 'badge-ir-metrics-sanitize']) {
    const result = run(tool, [legacy, `--check=${ready}`, input, '-o', output]);
    assert.equal(result.status, 0, result.stderr);
    const report = JSON.parse(fs.readFileSync(output));
    assert.equal(report.before[0].instructions, 12);
    assert.deepEqual(report.before[0].opcodes, {
      alloca: 1, add: 2, shl: 1, load: 1, store: 1, br: 3, phi: 1, switch: 1, ret: 1,
    });
    assert.equal(report.after[0].instructions, 12);
    assert.equal(report.after[0].opcodes.freeze, 1);
    assert.equal(report.after[0].volatile_loads, 1);
    assert.equal(report.after[0].volatile_stores, 1);
    const cf = report.events.filter(e => e.pass === 'constant-fold');
    assert.equal(cf.find(e => e.event === 'candidate').count, 3);
    assert.equal(cf.find(e => e.reason === 'flags_preserved').count, 1);
    assert.equal(cf.find(e => e.reason === 'undefined_or_unsupported_constant_operation').count, 1);
    assert.equal(cf.find(e => e.event === 'transformed').count, 1);
    assert.equal(cf.find(e => e.event === 'propagated').count, 2);
    fs.writeFileSync(output, 'preserved');
    fs.writeFileSync(ready, 'wrong');
    assert.equal(run(tool, [legacy, `--check=${ready}`, input, '-o', output]).status, 1);
    assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
    assert.equal(run('badge-opt', [legacy, input, '-o', ready]).status, 0);
  }
});
test('Compiler and metrics defaults agree with the qualified explicit profile', t => {
  const file = fixture(t), input = file('input.ll'), ready = file('ready.ll'),
    explicit = file('explicit.ll'), output = file('report.json');
  const passes = 'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division,local-memory,loop-scalar,cross-function';
  assert.equal(run('badge-opt', ['--default-passes']).stdout.trim(), passes);
  fs.writeFileSync(input, target + `define i32 @f(i32 %x) {
entry:
%slot = alloca i32
store i32 %x, ptr %slot
%value = load i32, ptr %slot
%neutral = add i32 %value, 0
ret i32 %neutral
}
`);
  assert.equal(run('badge-opt', [input, '-o', ready]).status, 0);
  assert.equal(run('badge-opt', [`--passes=${passes}`, input, '-o', explicit]).status, 0);
  assert.equal(fs.readFileSync(ready, 'utf8'), fs.readFileSync(explicit, 'utf8'));
  for (const tool of ['badge-ir-metrics', 'badge-ir-metrics-sanitize']) {
    const result = run(tool, [`--check=${ready}`, input, '-o', output]);
    assert.equal(result.status, 0, result.stderr);
    const report = JSON.parse(fs.readFileSync(output));
    assert.deepEqual(report.events.filter(e => e.event === 'pass' && e.reason === 'started')
      .map(e => e.pass), passes.split(','));
    assert.equal(report.after[0].instructions, 1);
    assert.deepEqual(report.after[0].opcodes, { ret: 1 });
  }
});
test('carrier and division observations count generated work, and explicit profiles are supported', t => {
  const file = fixture(t), input = file('input.ll'), output = file('report.json');
  fs.writeFileSync(input, target + `define i64 @f(ptr %p, [3 x i32] %v, i64 %n) {
entry:
store [3 x i32] %v, ptr %p, align 4
%a = load [3 x i32], ptr %p, align 4
%d = sdiv i64 %n, 17
%q = sdiv i64 %n, 1024
ret i64 %d
}
`);
  const r = run('badge-ir-metrics-sanitize', ['--passes=aggregate-memory,wide-division', input, '-o', output]);
  assert.equal(r.status, 0, r.stderr);
  const data = JSON.parse(fs.readFileSync(output));
  assert.equal(data.before.length, 1);
  assert.equal(data.after.length, 3);
  assert.equal(data.after[0].opcodes.load, 3);
  assert.equal(data.after[0].opcodes.store, 3);
  assert.equal(data.events.find(e => e.reason === 'wide_carrier_scalarized').count, 2);
  assert.equal(data.events.find(e => e.reason === 'scalar_memory_access').count, 6);
  assert.equal(data.events.find(e => e.reason === 'division_lowered').count, 1);
  assert.equal(data.events.find(e => e.reason === 'direct_i64_power_of_two').count, 1);
});
test('metrics CLI rejects malformed/NUL/oversized IR, aliases and invalid selections without replacing reports', t => {
  const file = fixture(t), input = file('input.ll'), output = file('report.json'), alias = file('alias.ll');
  for (const source of ['', target + 'define i32 @f() {\n', Buffer.from('bad\0IR')]) {
    fs.writeFileSync(input, source);
    fs.writeFileSync(output, 'preserved');
    assert.equal(run('badge-ir-metrics-sanitize', [input, '-o', output]).status, 1);
    assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
  }
  fs.writeFileSync(input, target + 'define i32 @f() {\nentry:\nret i32 0\n}\n');
  fs.linkSync(input, alias);
  assert.equal(run('badge-ir-metrics-sanitize', [input, '-o', alias]).status, 1);
  fs.unlinkSync(alias);
  fs.symlinkSync(input, alias);
  assert.equal(run('badge-ir-metrics-sanitize', [input, '-o', alias]).status, 1);
  for (const selection of ['', 'missing', 'constant-fold,']) {
    assert.equal(run('badge-ir-metrics-sanitize', [`--passes=${selection}`, input, '-o', output]).status, 1);
    assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
  }
  const fd = fs.openSync(input, 'w');
  fs.ftruncateSync(fd, 16 * 1024 * 1024 + 1);
  fs.closeSync(fd);
  assert.equal(run('badge-ir-metrics-sanitize', [input, '-o', output]).status, 1);
  assert.equal(run('badge-ir-metrics-sanitize', []).status, 2);
});
function asm(name, bytes, body = '', global = false) {
  return `@ stack-frame: ${bytes} + 24 saved-register bytes
${global ? `.global ${name}\n` : ''}.type ${name}, %function
.thumb_func
${name}:
push.w {r4-r8, lr}
movw r12, #${bytes}
sub.w sp, sp, r12
mov r7, sp
${body}
mov sp, r7
movw r12, #${bytes}
add.w sp, sp, r12
pop.w {r4-r8, pc}
.size ${name}, .-${name}
`;
}
function nodes(text, module = 'a.o') {
  return [...parseAssembly(text, module).values()].map((n, k) => ({
    ...n, binding: n.global ? 1 : 0, address: 0x10000010 + k * 128,
  }));
}
test('stack graph uses per-call outgoing allocations rather than summing all call sites', () => {
  const text = asm('root', 80, `movw r12, #16
sub.w sp, sp, r12
bl leaf
movw r12, #16
add.w sp, sp, r12
movw r12, #8
sub.w sp, sp, r12
bl leaf
movw r12, #8
add.w sp, sp, r12`, true) + asm('leaf', 56);
  const graph = stackGraph(nodes(text), 0x10000011);
  assert.equal(graph.entry.bound_bytes, 200);
  assert.equal(graph.functions[0].max_outgoing_bytes, 16);
  assert.equal(graph.unknown_functions, 0);
  assert.equal(graph.interrupts_included, false);
});
test('global allocator saves are balanced, counted and matched to restores', () => {
  const source = asm('root', 80, '', true)
    .replace('24 saved-register bytes', '32 saved-register bytes')
    .replace('push.w {r4-r8, lr}', 'push.w {r4-r8, r10, r11, lr}')
    .replace('pop.w {r4-r8, pc}', 'pop.w {r4-r8, r10, r11, pc}');
  assert.equal(stackGraph(nodes(source), 0x10000011).entry.bound_bytes, 112);
  for (const mismatch of [
    source.replace('32 saved-register bytes', '24 saved-register bytes'),
    source.replace('pop.w {r4-r8, r10, r11, pc}', 'pop.w {r4-r8, pc}'),
    asm('root', 80).replace('pop.w {r4-r8, pc}', 'pop.w {r4-r8, r10, r11, pc}'),
  ]) assert.equal(stackGraph(nodes(mismatch), 0x10000011).entry.bound_bytes, null);
});
test('reused numeric registers and nonflagging offsets prove outgoing sizes without hiding clobbers', () => {
  const body = `movw r2, #8
addw r12, r2, #8
sub.w sp, sp, r12
bl leaf
movw r2, #16
mov r12, r2
add.w sp, sp, r12`;
  const graph = stackGraph(nodes(asm('root', 80, body) + asm('leaf', 8)), 0x10000011);
  assert.equal(graph.entry.bound_bytes, 152);
  assert.equal(graph.functions[0].max_outgoing_bytes, 16);
  for (const clobber of ['ldr.w r2, [r4]', 'bl leaf', 'it eq\nmoveq r2, r0',
    '.Ljoin:', 'str.w r0, [r2], #4']) {
    const source = asm('root', 80, `movw r2, #8\n${clobber}
mov r12, r2
sub.w sp, sp, r12
bl leaf`) + asm('leaf', 8);
    assert.equal(stackGraph(nodes(source), 0x10000011).entry.bound_bytes, null, clobber);
  }
});
test('indirect/unresolved calls, recursion, unknown stack operations and missing annotations never get complete bounds', () => {
  for (const body of ['blx r12', 'bl missing', 'bl root', 'push {r0}', 'sub sp, sp, #8',
    '.short 0x1234', 'mov r7, r0', 'bne.w other_function', 'msr msp, r0',
    'msr control, r0', 'ldmia sp!, {r0}', 'ldmia r0!, {r7}', 'mov pc, r0']) {
    const graph = stackGraph(nodes(asm('root', 80, body, true)), 0x10000011);
    assert.equal(graph.entry.bound_bytes, null, body);
    assert(graph.entry.reasons.length > 0);
    assert(graph.entry.known_prefix_bytes >= 104);
  }
  const missing = asm('root', 80).replace(/^@ stack-frame:.*\n/, '');
  assert.equal(stackGraph(nodes(missing), 0x10000011).entry.bound_bytes, null);
  const recursion = asm('root', 8, 'bl child', true) + asm('child', 16, 'bl root');
  assert.equal(stackGraph(nodes(recursion), 0x10000011).max_linked_direct_chain_bytes, null);
  const missingSave = asm('root', 80).replace('push.w {r4-r8, lr}\n', '');
  assert.equal(stackGraph(nodes(missingSave), 0x10000011).entry.bound_bytes, null);
  const clobbered = asm('root', 80, 'movw r12, #8\nbl leaf\nsub.w sp, sp, r12');
  assert.equal(stackGraph(nodes(clobbered + asm('leaf', 8)), 0x10000011).entry.bound_bytes, null);
});
test('exact C07 branch-around literal pools and unreachable traps do not invent unknown stack effects', () => {
  const literal = 'ldr.w r0, .Lcg_lit8\nb.w .Lcg_after8\n.p2align 2\n' +
    '.Lcg_lit8:\n.word symbol+0\n.Lcg_after8:';
  assert.equal(stackGraph(nodes(asm('root', 8, literal + '\n.short 0xde00')), 0x10000011).entry.bound_bytes, 32);
  assert.equal(stackGraph(nodes(asm('root', 8, literal.replace('b.w .Lcg_after8\n', ''))),
    0x10000011).entry.bound_bytes, null);
});
test('recursive stack components and prefixes are independent of function order', () => {
  const source = asm('root', 8, 'bl a', true) +
    asm('a', 16, 'bl b\nbl leaf') + asm('b', 24, 'bl a') + asm('leaf', 32);
  const original = nodes(source);
  const before = stackGraph(original, original[0].address);
  const after = stackGraph([...original].reverse(), original[0].address);
  const sorted = graph => [...graph.functions].sort((a, b) => a.function.localeCompare(b.function));
  assert.deepEqual(sorted(before), sorted(after));
  assert.deepEqual(before.entry, after.entry);
  assert.deepEqual(before.recursive_components, after.recursive_components);
  assert.deepEqual(before.recursive_components[0].functions, ['a.o:a', 'a.o:b']);
  assert.equal(before.entry.bound_bytes, null);
  assert.equal(before.max_linked_direct_chain_bytes, null);
  assert.equal(before.max_known_direct_chain_bytes, 56);
  assert.equal(before.entry_reachable_functions, 4);
  assert.equal(before.entry_reachable_unknown_functions, 3);
  assert.equal(before.entry_reachability_complete, true);
});
test('deep direct chains use iterative analysis and unreachable uncertainty does not poison entry bounds', () => {
  const count = 3000;
  const chain = Array.from({ length: count }, (_, i) => ({
    module: 'chain.o', name: `f${i}`, binding: 1, address: 0x10000010 + i * 128,
    frame_bytes: 8, saves_bytes: 24, max_outgoing_bytes: 0, unknown: [],
    calls: i + 1 < count ? [{ target: `f${i + 1}`, outgoing_bytes: 0 }] : [],
  }));
  const extra = { ...chain[0], name: 'callback', address: 0x11000000,
    calls: [{ target: null, outgoing_bytes: 0 }] };
  const graph = stackGraph([...chain, extra], chain[0].address);
  assert.equal(graph.entry.bound_bytes, count * 32);
  assert.equal(graph.max_known_direct_chain_bytes, count * 32);
  assert.equal(graph.max_linked_direct_chain_bytes, null);
  assert.equal(graph.entry_reachable_functions, count);
  assert.equal(graph.entry_reachable_unknown_functions, 0);
  assert.equal(graph.entry_reachability_complete, true);
  assert.equal(graph.unknown_functions, 1);
  assert.throws(() => stackGraph([chain[0], chain[0]], chain[0].address), /Duplicate qualified/);
});
test('local helper identities are qualified by object, and map ranges retain vendor LMAs', () => {
  const a = nodes(asm('root', 8, 'bl helper', true) + asm('helper', 16), 'a.o');
  const b = nodes(asm('helper', 200), 'b.o').map(n => ({ ...n, address: n.address + 1000 }));
  assert.equal(stackGraph([...a, ...b], 0x10000011).entry.bound_bytes, 72);
  assert.deepEqual(parseMap('10000010 10000010 00000080 4 folder/a.o:(.text.root)\n' +
    '10000100 10000100 00000004 1 folder/assets.o:(.rodata.wifi_clm)\n')[1],
  { address: 0x10000100, lma: 0x10000100, size: 4, module: 'assets.o', section: '.rodata.wifi_clm' });
  assert.throws(() => parseMap('not a map'), /no recognized/);
});
test('OS timer units and missing resource data are explicit', () => {
  assert.deepEqual(timerMetrics('1.00 real 0.50 user 0.25 sys\n 1234 maximum resident set size\n', 'darwin'),
    { user_seconds: 0.5, system_seconds: 0.25, os_max_rss_bytes: 1234 });
  assert.equal(timerMetrics('User time (seconds): 0.50\nSystem time (seconds): 0.25\n' +
    'Maximum resident set size (kbytes): 1234\n', 'linux').os_max_rss_bytes, 1234 * 1024);
  assert.throws(() => timerMetrics('bad', 'darwin'), /Missing/);
  assert.throws(() => timerMetrics('', 'win32'), /Unsupported/);
});
test('before/after comparisons reject changed input/configuration and preserve unknown stack deltas', () => {
  const report = { schema: 1, host: { cpu: 'test' },
    configuration: { target: 'arm', root_makefile_sha256: 'root', build_id: 'id',
      target_flags_sha256: 'flags', assets: [] },
    modules: [{ module: 'a.o', source: { file: 'a.c', sha256: 'source' },
      hashes: { 'preprocessed.i': 'pp', 'frontend.ll': 'ir' }, after: { instructions: 10 } }],
    images: [{ image: 'test', flash_bytes: 100, nonvendor_bytes: 100,
      linker_script: { sha256: 'script' }, stack: { max_local_frame_plus_saves: 20,
        entry: { bound_bytes: null, reasons: ['indirect'] } } }],
    totals: { flash_bytes: 100, nonvendor_bytes: 100 },
    build_cost: { median_wall_seconds: 1, max_os_rss_bytes: 100 } };
  const after = structuredClone(report);
  after.modules[0].after.instructions = 8;
  after.images[0].flash_bytes = after.images[0].nonvendor_bytes = 90;
  after.totals.flash_bytes = after.totals.nonvendor_bytes = 90;
  const comparison = compareReports(report, after);
  assert.equal(comparison.modules[0].delta.instructions, -2);
  assert.equal(comparison.flash_delta_bytes, -10);
  assert.equal(comparison.images[0].entry_stack_delta_bytes, null);
  after.configuration.build_jobs = 1;
  assert.equal(compareReports(report, after).comparable_build_cost, true);
  after.configuration.build_jobs = 4;
  assert.equal(compareReports(report, after).comparable_build_cost, false);
  assert.equal(compareReports(report, after).build_wall_delta_seconds, null);
  after.configuration.build_jobs = 1;
  after.host.cpu = 'different';
  assert.equal(compareReports(report, after).build_wall_delta_seconds, null);
  after.modules[0].hashes['frontend.ll'] = 'changed';
  assert.throws(() => compareReports(report, after), /Incomparable C05/);
  after.modules[0].hashes['frontend.ll'] = 'ir';
  after.configuration.build_id = 'other';
  assert.throws(() => compareReports(report, after), /Incomparable configuration/);
});
test('UF2 checks cover payload, family, block numbering and padding', () => {
  const bin = Buffer.from([1, 2, 3]), uf2 = Buffer.alloc(512);
  for (const [at, value] of [[0, 0x0a324655], [4, 0x9e5d5157], [8, 0x2000],
    [12, 0x10000000], [16, 256], [20, 0], [24, 1], [28, 0xe48bff59], [508, 0x0ab16f30]])
    uf2.writeUInt32LE(value, at);
  uf2.fill(255, 32, 288); bin.copy(uf2, 32);
  verifyUf2(bin, uf2);
  for (const offset of [28, 20, 32, 35, 288]) {
    const bad = Buffer.from(uf2); bad[offset] ^= 1;
    assert.throws(() => verifyUf2(bin, bad), /UF2/);
  }
});
test('benchmark rejects invalid CLI input before starting any build', () => {
  for (const argument of ['--label=../bad', '--runs=0', '--runs=10', '--runs=NaN',
    '--passes=', '--compare=', '--unknown', '--runs']) {
    const result = spawnSync(process.execPath, [path.join(dir, 'tools/benchmark-c06.mjs'), argument],
      { encoding: 'utf8', timeout: 15000 });
    assert.ifError(result.error);
    assert.equal(result.status, 1, argument);
    assert.match(result.stderr, /benchmark-c06:/);
  }
});
