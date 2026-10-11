import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { modules, reportBytes, suiteId, crc32, request, decode, compare,
  timingReportBytes, timingSamples, timingRepeats, decodeTiming, compareTiming,
  timingStatistics, kernels, compareKernelTiming } from '../tools/compiler-test-protocol.mjs';
import { compareTimingReports } from '../tools/compiler-timing-compare.mjs';
import { verifyCampaign } from '../tools/qualify-datasheet.mjs';

const build = 0x12345678;
const nonce = Buffer.from('0102030405060708090a0b0c0d0e0f10', 'hex');
function fixture(timing = false, version = 3) {
  const data = Buffer.alloc(timing ? version === 3 ? timingReportBytes : 16 * 64 : reportBytes);
  for (let i = 0; i < data.length / 64; i++) {
    const wire = data.subarray(i * 64, (i + 1) * 64);
    wire.writeUInt32LE(0x31544342); wire[4] = 1; wire[6] = 64;
    wire.writeUInt32LE(build, 8); wire.writeUInt32LE(suiteId + i, 12); nonce.copy(wire, 16);
    if (i === 0) {
      wire.writeUInt32LE(1, 32); wire.writeUInt32LE(modules.length, 36);
      wire.writeUInt32LE(nonce.readUInt32LE(0), 44);
      if (timing) {
        wire.writeUInt32LE(timingSamples, 48);
        wire.writeUInt32LE(timingRepeats, 52);
        wire.writeUInt32LE(version, 56);
      }
    } else if (i <= modules.length) wire.writeUInt32LE(modules[i - 1].cases, 32);
    else if (i >= 16 && i % 2 === 0) {
      wire.writeUInt32LE(kernels[(i - 16) / 2].cases, 32);
      wire.writeUInt32LE(1, 48); wire.writeUInt32LE(1, 52); wire.writeUInt32LE(128, 56);
    } else {
      const values = i === 7 ? [0, 65536, 0, 65536, 3, 24, 0] :
        i === 8 ? [0, 0, 0, 0, 0, 0, 1] :
          i === 9 ? [1, 1, 1, 1, 1, 1, 1] : [1000, 1001, 999, 1002, 998, 1003, 997];
      values.forEach((value, k) => wire.writeUInt32LE(value, 32 + k * 4));
    }
    wire.writeUInt32LE(crc32(wire.subarray(0, 60)), 60);
  }
  return data;
}
function mutate(data, slot, offset, value) {
  const wire = data.subarray(slot * 64, (slot + 1) * 64);
  wire.writeUInt32LE(value, offset);
  wire.writeUInt32LE(crc32(wire.subarray(0, 60)), 60);
  return data;
}
test('independent CRC oracle and request layout', () => {
  assert.equal(crc32(Buffer.from('123456789')), 0xcbf43926);
  const wire = request(build, nonce);
  assert.equal(wire.readUInt32LE(12), suiteId);
  assert.equal(wire.readUInt32LE(60), crc32(wire.subarray(0, 60)));
  assert.throws(() => request(build, Buffer.alloc(16)), /zero nonce/);
});
test('full suite validates and compares each lane', () => {
  const llvm = decode(fixture(), build, nonce);
  assert(compare(llvm, llvm).every(x => x.matched));
  const owned = decode(mutate(fixture(), 3, 44, 123), build, nonce);
  const result = compare(llvm, owned);
  assert.equal(result.filter(x => !x.matched).length, 1);
  assert.deepEqual(result[2].differences, [{ lane: 2, llvm: 0, owned: 123 }]);
});
test('rejects truncated, stale, wrong-build, corrupt, reordered and incomplete records', () => {
  assert.throws(() => decode(fixture().subarray(0, -1), build, nonce), /truncated/);
  assert.throws(() => decode(Buffer.concat([fixture(), Buffer.alloc(1)]), build, nonce), /excess/);
  assert.throws(() => decode(fixture(), build + 1, nonce), /wrong build/);
  assert.throws(() => decode(fixture(), build, Buffer.alloc(16, 2)), /stale/);
  const corrupt = fixture(); corrupt[100] ^= 1;
  assert.throws(() => decode(corrupt, build, nonce), /CRC/);
  assert.throws(() => decode(mutate(fixture(), 2, 12, suiteId + 1), build, nonce), /order/);
  assert.throws(() => decode(mutate(fixture(), 1, 32, 255), build, nonce), /incomplete/);
  assert.throws(() => decode(mutate(fixture(), 0, 32, 3), build, nonce), /device failure/);
  assert.throws(() => decode(mutate(fixture(), 4, 0, 0), build, nonce), /commit/);
});

test('timing records, medians, ratios, resolution, clocks and version safety', () => {
  assert.deepEqual(timingStatistics([7, 1, 6, 2, 5, 3, 4]), { min: 1, median: 4, max: 7 });
  assert.throws(() => timingStatistics([1, 2]), /sample count/);
  const llvm = decodeTiming(fixture(true), build, nonce);
  const ownedBytes = fixture(true);
  for (let sample = 0; sample < 7; sample++)
    mutate(ownedBytes, 10, 32 + sample * 4, llvm.modules[0].timing.samples[sample] * 2);
  const comparisons = compareTiming(llvm, decodeTiming(ownedBytes, build, nonce));
  assert.equal(comparisons[0].ownedToLlvm, 2);
  assert.equal(comparisons[0].percentSlower, 100);
  assert.equal(compareTiming(decodeTiming(ownedBytes, build, nonce), llvm)[0].percentSlower, -50);
  assert(compare(llvm.modules, decodeTiming(ownedBytes, build, nonce).modules).every(x => x.matched));
  assert.throws(() => decodeTiming(fixture(), build, nonce), /truncated/);
  assert.throws(() => decode(fixture(true), build, nonce), /excess/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 0, 52, 1), build, nonce), /summary/);
  const failed = mutate(fixture(true), 0, 32, 2);
  mutate(failed, 0, 40, 6);
  failed.fill(0, 64);
  assert.throws(() => decodeTiming(failed, build, nonce), /device timing failure, detail 6/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 10, 32, 99), build, nonce), /under-resolved/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 10, 32, 1000001), build, nonce), /overlong/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 9, 32, 11), build, nonce), /overhead/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 7, 48, 0), build, nonce), /disabled/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 8, 44, 0xc0000001), build, nonce), /lock-loss/);
  assert.throws(() => compareTiming(llvm,
    decodeTiming(mutate(fixture(true), 7, 32, 1), build, nonce)), /clock configurations/);
  const legacy = decodeTiming(fixture(true, 1), build, nonce);
  assert.equal(legacy.version, 1);
  assert.throws(() => compareTiming(legacy, llvm), /workload versions differ/);
  assert.equal(decodeTiming(fixture(true, 2), build, nonce).kernels.length, 0);
  assert.throws(() => decodeTiming(mutate(fixture(true), 0, 56, 4), build, nonce),
    /unknown timing workload version/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 15, 12, suiteId + 14), build, nonce), /order/);
  assert.equal(llvm.kernels.length, kernels.length);
  assert(compare(llvm.kernels, llvm.kernels, kernels).every(x => x.matched));
  assert(compareKernelTiming(llvm, llvm).every(x => x.ownedToLlvm === 1));
  assert.throws(() => compareKernelTiming(legacy, legacy), /requires version 3/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 16, 48, 0), build, nonce), /guards/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 16, 52, 0), build, nonce), /source modified/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 16, 56, 127), build, nonce), /kernel work/);
  assert.throws(() => decodeTiming(mutate(fixture(true), 27, 32, 99), build, nonce), /under-resolved/);
  assert.throws(() => decodeTiming(fixture(true).subarray(0, -64), build, nonce), /truncated/);
});

test('runner mock: distinct identities, dual flash/start, comparison failures and recovery', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'u26-compiler-compare-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'badge-compiler/tools'), { recursive: true });
  fs.mkdirSync(path.join(root, 'mock-bin'));
  const source = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
  for (const file of ['tools/compiler-compare.mjs', 'tools/compiler-test-protocol.mjs', 'tools/compiler-image-cache.mjs'])
    fs.copyFileSync(path.join(source, file), path.join(root, 'badge-compiler', file));
  const log = path.join(root, 'commands.jsonl');
  const executable = (name, body) => {
    const file = path.join(root, 'mock-bin', name);
    fs.writeFileSync(file, '#!/usr/bin/env node\n' + body);
    fs.chmodSync(file, 0o755);
    return file;
  };
  const picotool = executable('picotool', `
const fs=require('node:fs');
fs.appendFileSync(process.env.MOCK_LOG,JSON.stringify(['picotool',...process.argv.slice(2)])+'\\n');
if(process.argv[2]==='info')console.log('Device Information: RP2350');
`);
  executable('clang', `console.log('Mock LLVM for runner tests');`);
  executable('make', `
const fs=require('node:fs'),path=require('node:path'),args=process.argv.slice(2);
fs.appendFileSync(process.env.MOCK_LOG,JSON.stringify(['make',...args])+'\\n');
if(args.includes('all')){
 fs.mkdirSync('badge-compiler/build',{recursive:true});
 for(const name of ['badge-cpp','badge-parse','badge-sema','badge-ir','badge-opt','badge-codegen','badge-asm','badge-object','badge-link','elf2bin','badge-sha256'])
  fs.writeFileSync('badge-compiler/build/'+name,'mock tool');
 fs.writeFileSync('badge-compiler/build/badge-opt','#!/usr/bin/env node\\nconsole.log("mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division");\\n');
 fs.chmodSync('badge-compiler/build/badge-opt',0o755);
 fs.writeFileSync('badge-compiler/build/badge-ir','#!/usr/bin/env node\\nconsole.log("legacy");\\n');
 fs.chmodSync('badge-compiler/build/badge-ir',0o755);
 fs.writeFileSync('badge-compiler/build/badge-codegen','#!/usr/bin/env node\\nconsole.log(process.argv[2]==="--default-register-residency"?"stack":process.argv[2]==="--default-scalar-selection"?"legacy":process.argv[2]==="--default-memory-selection"?"byte":"materialized");\\n');
 fs.chmodSync('badge-compiler/build/badge-codegen',0o755);
}else{
 const dir=args.find(x=>x.startsWith('BUILD_DIR=')).slice(10);
 for(const suffix of ['universe26/firmware','universe26/uf2','host-tools'])fs.mkdirSync(dir+'/'+suffix,{recursive:true});
 const bin=Buffer.alloc(64);bin.writeUInt32LE(0x2007f000);bin.writeUInt32LE(0x10000009,4);
 fs.writeFileSync(dir+'/universe26/firmware/compiler_test.bin',bin);
 const uf2=Buffer.alloc(512);const fields=[[0,0x0a324655],[4,0x9e5d5157],[8,0x2000],[12,0x10000000],[16,256],[20,0],[24,1],[28,0xe48bff59],[508,0x0ab16f30]];
 for(const [o,v]of fields)uf2.writeUInt32LE(v,o);uf2.fill(255,32,288);bin.copy(uf2,32);
 fs.writeFileSync(dir+'/universe26/uf2/compiler_test.uf2',uf2);
 fs.writeFileSync(dir+'/universe26/firmware/compiler_test_build.h','#define COMPILER_TEST_BUILD_ID 0x12345678u');
 if(dir.endsWith('-frontend-ir'))for(let i=0;i<11;i++){
  const module=dir+'/universe26/firmware/module'+i+'.o.owned';fs.mkdirSync(module,{recursive:true});
  fs.writeFileSync(module+'/frontend.ll','define void @f'+i+'() {\\nentry:\\nret void\\n}\\n');
 }
 fs.copyFileSync('mock-bin/collector',dir+'/host-tools/compiler_test_host');fs.chmodSync(dir+'/host-tools/compiler_test_host',0o755);
}
`);
  executable('collector', `
const fs=require('node:fs'),args=process.argv.slice(2);
fs.appendFileSync(process.env.MOCK_LOG,JSON.stringify(['collector',...args])+'\\n');
if(process.env.MOCK_MODE==='collector-failure'){console.error('intentional collection failure');process.exit(1);}
function crc(b){let c=0xffffffff;for(const x of b){c^=x;for(let i=0;i<8;i++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (~c)>>>0;}
const application=process.env.MOCK_APPLICATION==='1';
const nonce=fs.readFileSync(args[2]).subarray(16,32),counts=application?[16,64,4,4,16,256]:[256,8,64,128,64,76800];
for(let role=0;role<2;role++){
 const timed=process.env.MOCK_TIMING==='1',bytes=Buffer.alloc(application?1024:timed?1792:448);
 for(let i=0;i<bytes.length/64;i++){
  const w=bytes.subarray(i*64,(i+1)*64);w.writeUInt32LE(0x31544342);w[4]=1;w[6]=64;w.writeUInt32LE(0x12345678,8);w.writeUInt32LE((application?0x41505030:0x434f4d50)+i,12);nonce.copy(w,16);
  if(i===0){w.writeUInt32LE(1,32);w.writeUInt32LE(6,36);w.writeUInt32LE(nonce.readUInt32LE(0),44);
   if(timed){w.writeUInt32LE(7,48);w.writeUInt32LE(64,52);w.writeUInt32LE(application?4:3,56);}
  }else if(i<=6){w.writeUInt32LE(counts[i-1],32);if(application)[123,200,1,1,1,1].forEach((x,k)=>w.writeUInt32LE(x,36+k*4));}
  else if(i>=16&&i%2===0){w.writeUInt32LE(128,32);w.writeUInt32LE(1,48);w.writeUInt32LE(1,52);w.writeUInt32LE(128,56);}
  else{
   const values=i===7?[0,65536,0,65536,3,24,0]:i===8?[0,0,0,0,0,0,1]:i===9?[1,1,1,1,1,1,1]:Array(7).fill(role===0?1000:process.env.MOCK_MODE==='faster'?500:2000);
   values.forEach((x,k)=>w.writeUInt32LE(x,32+k*4));
  }
  if(role===1&&i===3&&process.env.MOCK_MODE==='mismatch')w.writeUInt32LE(42,40);
  if(role===1&&i===16&&process.env.MOCK_MODE==='kernel-mismatch')w.writeUInt32LE(42,36);
  if(role===1&&process.env.MOCK_MODE==='stale')w[16]^=1;
  w.writeUInt32LE(crc(w.subarray(0,60)),60);
 }
 fs.writeFileSync(args[3+role],bytes);
}
`);
  const run = (mode, serials = ['9BBF88DD17D66E7D', '08DA26F86E57F5EA'], timing = false, seed, frameAddressing, registerResidency, scalarSelection, memorySelection, expressionLowering, frontendIr = false) => {
    fs.writeFileSync(log, '');
    const env = { ...process.env };
    delete env.BADGE_CODEGEN_FRAME_ADDRESSING;
    delete env.BADGE_CODEGEN_REGISTER_RESIDENCY;
    delete env.BADGE_CODEGEN_SCALAR_SELECTION;
    delete env.BADGE_CODEGEN_MEMORY_SELECTION;
    delete env.BADGE_IR_EXPRESSION_LOWERING;
    if (frameAddressing !== undefined) env.BADGE_CODEGEN_FRAME_ADDRESSING = frameAddressing;
    if (registerResidency !== undefined) env.BADGE_CODEGEN_REGISTER_RESIDENCY = registerResidency;
    if (scalarSelection !== undefined) env.BADGE_CODEGEN_SCALAR_SELECTION = scalarSelection;
    if (memorySelection !== undefined) env.BADGE_CODEGEN_MEMORY_SELECTION = memorySelection;
    if (expressionLowering !== undefined) env.BADGE_IR_EXPRESSION_LOWERING = expressionLowering;
    const output = spawnSync(process.execPath, [path.join(root, 'badge-compiler/tools/compiler-compare.mjs'),
      ...(frontendIr ? ['--application', '--frontend-ir'] : timing ? ['--timing'] : []),
      ...(seed === undefined ? [] : [`--seed=${seed}`]), ...serials],
      { encoding: 'utf8', timeout: 10000, env: { ...env, PICOTOOL: picotool,
        LLVM_BIN: path.join(root, 'mock-bin'), PATH: path.join(root, 'mock-bin') + path.delimiter + process.env.PATH,
        MOCK_LOG: log, MOCK_MODE: mode, MOCK_TIMING: timing || frontendIr ? '1' : '0',
        MOCK_APPLICATION: frontendIr ? '1' : '0' } });
    assert.equal(output.error, undefined);
    const commands = fs.readFileSync(log, 'utf8').trim().split('\n').filter(Boolean).map(x => JSON.parse(x));
    return { ...output, commands };
  };
  const good = run('success');
  assert.equal(good.status, 0, good.stderr);
  const flashes = good.commands.map((x, i) => x[0] === 'picotool' && x[1] === 'load' ? i : -1).filter(i => i >= 0);
  const starts = good.commands.map((x, i) => x[0] === 'picotool' && x[1] === 'reboot' ? i : -1).filter(i => i >= 0);
  const collector = good.commands.findIndex(x => x[0] === 'collector');
  assert.equal(flashes.length, 2); assert.equal(starts.length, 2);
  assert(Math.max(...flashes) < Math.min(...starts) && Math.max(...starts) < collector);
  const timed = run('success', undefined, true);
  assert.equal(timed.status, 0, timed.stderr);
  const collections = timed.commands.filter(x => x[0] === 'collector');
  assert.equal(collections.length, 2);
  assert.deepEqual(collections[0].slice(1, 3), collections[1].slice(1, 3).reverse());
  assert.equal(timed.commands.filter(x => x[0] === 'picotool' && x[1] === 'load').length, 4);
  assert.match(timed.stdout, /owned\/LLVM 2.000x; 100.0% slower/);
  const timedReport = JSON.parse(fs.readFileSync(path.join(path.dirname(collections[0][3]), 'results.json')));
  assert.equal(timedReport.schema, 3);
  assert.equal(timedReport.matched, true);
  const campaignFile = path.join(path.dirname(collections[0][3]), 'results.json');
  assert.equal(verifyCampaign(campaignFile).artifacts.length, 8);
  const rawFile = path.join(path.dirname(campaignFile), 'round1-owned.bin');
  const raw = fs.readFileSync(rawFile), corrupt = Buffer.from(raw);
  corrupt[100] ^= 1;
  fs.writeFileSync(rawFile, corrupt);
  assert.throws(() => verifyCampaign(campaignFile), /CRC/);
  fs.writeFileSync(rawFile, raw);
  assert.equal(timedReport.toolchain.ownedPasses,
    'mem2reg,scalar-cleanup,constant-fold,aggregate-memory,wide-division');
  assert.equal(timedReport.toolchain.ownedFrameAddressing, 'materialized');
  assert.equal(timedReport.toolchain.ownedRegisterResidency, 'stack');
  assert.equal(timedReport.toolchain.ownedScalarSelection, 'legacy');
  const direct = run('success', undefined, true, undefined, 'direct', 'local');
  assert.equal(direct.status, 0, direct.stderr);
  const directCollection = direct.commands.find(x => x[0] === 'collector');
  const directReport = JSON.parse(fs.readFileSync(path.join(path.dirname(directCollection[3]), 'results.json')));
  assert.equal(directReport.toolchain.ownedFrameAddressing, 'direct');
  assert.equal(directReport.toolchain.ownedRegisterResidency, 'local');
  const global = run('success', undefined, true, undefined, 'direct', 'global', 'thumb', 'word');
  assert.equal(global.status, 0, global.stderr);
  const globalCollection = global.commands.find(x => x[0] === 'collector');
  const globalReport = JSON.parse(fs.readFileSync(path.join(path.dirname(globalCollection[3]), 'results.json')));
  assert.equal(globalReport.toolchain.ownedRegisterResidency, 'global');
  assert.equal(globalReport.toolchain.ownedScalarSelection, 'thumb');
  assert.equal(globalReport.toolchain.ownedMemorySelection, 'word');
  const layout = run('success', undefined, true, undefined, 'direct', 'global-compact', 'thumb-layout', 'word');
  assert.equal(layout.status, 0, layout.stderr);
  const layoutCollection = layout.commands.find(x => x[0] === 'collector');
  const layoutReport = JSON.parse(fs.readFileSync(path.join(path.dirname(layoutCollection[3]), 'results.json')));
  assert.equal(layoutReport.toolchain.ownedScalarSelection, 'thumb-layout');
  assert.equal(layoutReport.toolchain.ownedRegisterResidency, 'global-compact');
  const ssa = run('success', undefined, true, undefined, 'direct', 'global-compact', 'thumb-homes', 'word', 'ssa');
  assert.equal(ssa.status, 0, ssa.stderr);
  const ssaCollection = ssa.commands.find(x => x[0] === 'collector');
  const ssaReport = JSON.parse(fs.readFileSync(path.join(path.dirname(ssaCollection[3]), 'results.json')));
  assert.equal(ssaReport.toolchain.ownedExpressionLowering, 'ssa');
  const calls = run('success', undefined, true, undefined, 'direct', 'global-compact',
    'thumb-calls', 'word', 'ssa');
  assert.equal(calls.status, 0, calls.stderr);
  const callsCollection = calls.commands.find(x => x[0] === 'collector');
  const callsReport = JSON.parse(fs.readFileSync(path.join(path.dirname(callsCollection[3]), 'results.json')));
  assert.equal(callsReport.toolchain.ownedScalarSelection, 'thumb-calls');
  assert.equal(callsReport.toolchain.ownedExpressionLowering, 'ssa');
  const constants = run('success', undefined, true, undefined, 'direct', 'global-compact',
    'thumb-constants', 'word', 'ssa');
  assert.equal(constants.status, 0, constants.stderr);
  const constantsCollection = constants.commands.find(x => x[0] === 'collector');
  const constantsReport = JSON.parse(fs.readFileSync(path.join(path.dirname(constantsCollection[3]), 'results.json')));
  assert.equal(constantsReport.toolchain.ownedScalarSelection, 'thumb-constants');
  const crossover = run('success', undefined, false, undefined, 'direct', 'global-compact',
    'thumb-homes', 'word', 'ssa', true);
  assert.equal(crossover.status, 0, crossover.stderr);
  const crossoverDirectory = crossover.stdout.match(/Compiler comparison: ([^\n]+)/)[1];
  const crossoverInputs = fs.readdirSync(crossoverDirectory).filter(name => name.endsWith('.initial.ll'));
  assert.equal(crossoverInputs.length, 11);
  const crossReport = path.join(crossoverDirectory, 'results.json');
  assert.equal(verifyCampaign(crossReport).artifacts.length, 19);
  const initial = path.join(crossoverDirectory, crossoverInputs[0]);
  const initialText = fs.readFileSync(initial);
  fs.appendFileSync(initial, 'corrupt');
  assert.throws(() => verifyCampaign(crossReport));
  fs.writeFileSync(initial, initialText);
  assert.equal(timedReport.toolchain.ownedMemorySelection, 'byte');
  for (const mode of ['', 'invalid']) {
    const invalid = run('success', undefined, true, undefined, mode);
    assert.equal(invalid.status, 2);
    assert.equal(invalid.commands.length, 0);
    const badResidency = run('success', undefined, true, undefined, undefined, mode);
    assert.equal(badResidency.status, 2);
    assert.equal(badResidency.commands.length, 0);
    const badScalar = run('success', undefined, true, undefined, undefined, undefined, mode);
    assert.equal(badScalar.status, 2);
    assert.equal(badScalar.commands.length, 0);
    const badMemory = run('success', undefined, true, undefined, undefined, undefined, undefined, mode);
    assert.equal(badMemory.status, 2);
    assert.equal(badMemory.commands.length, 0);
    const badFrontend = run('success', undefined, true, undefined, undefined, undefined, undefined, undefined, mode);
    assert.equal(badFrontend.status, 2);
    assert.equal(badFrontend.commands.length, 0);
  }
  assert.equal(timedReport.rounds.length, 2);
  for (const config of timedReport.rounds[0].configurations) {
    assert.equal(path.dirname(config.image), path.dirname(collections[0][3]));
    assert.equal(path.basename(config.image), `${config.name}.uf2`);
    assert.equal(fs.readFileSync(config.image).length, 512);
  }
  assert(timedReport.rounds.every(x => x.timingVersion === 3));
  assert(timedReport.modules.every(x => x.timing.ownedToLlvm === 2 && x.timing.runs.length === 2));
  assert(timedReport.kernels.every(x => x.timing.ownedToLlvm === 2 && x.timing.runs.length === 2));
  const candidate = structuredClone(timedReport);
  candidate.rounds.forEach(round => {
    const fresh = Buffer.from(round.nonce, 'hex'); fresh[15] ^= 0x80; round.nonce = fresh.toString('hex');
    for (const field of ['modules', 'kernels'])
      for (const result of round[field]) {
        const timing = result.timing.owned;
        timing.samples = timing.samples.map(x => x / 2);
        Object.assign(timing, timingStatistics(timing.samples));
      }
  });
  const improvement = compareTimingReports(timedReport, candidate);
  assert([...improvement.modules, ...improvement.kernels].every(x =>
    x.candidateToBaselineOwned === 0.5 && x.percentChange === -50 && x.llvmControlChange === 0));
  assert.throws(() => compareTimingReports(timedReport, timedReport), /nonces/);
  for (const [change, error] of [
    [x => { x.rounds[0].seed++; }, /nonce\/seed|seeds/],
    [x => { x.rounds[0].configurations[0].sha256 = '0'.repeat(64); }, /images/],
    [x => { x.rounds[0].configurations[1].build++; }, /identities/],
    [x => { x.rounds[1].configurations.reverse(); }, /roles/],
    [x => { x.rounds[0].clocks[0][0]++; }, /clocks/],
    [x => { x.rounds[0].kernels[0].timing.repetitions--; }, /work/],
    [x => { x.rounds[0].kernels[0].timing.owned.iterationsPerCall--; }, /iteration/],
    [x => { x.rounds[0].kernels[0].timing.owned.median++; }, /statistics/],
    [x => { x.rounds[0].kernels[0].matched = false; }, /unmatched/],
    [x => { x.schema = 2; }, /schema/],
    [x => { for (const r of x.rounds) for (const c of r.clocks) c[7] = 0x40000000; }, /lock-loss/],
    [x => { for (const r of x.rounds) for (const c of r.clocks) c[6] = 2; }, /timer source/],
    [x => { for (const r of x.rounds) for (const k of r.kernels) k.owned[0] = k.llvm[0] = 17; }, /fingerprints/],
  ]) {
    const invalid = structuredClone(candidate); change(invalid);
    assert.throws(() => compareTimingReports(timedReport, invalid), error);
  }
  const timedMismatch = run('mismatch', undefined, true);
  assert.equal(timedMismatch.status, 1);
  assert.doesNotMatch(timedMismatch.stdout, /TIMING/);
  const kernelMismatch = run('kernel-mismatch', undefined, true);
  assert.equal(kernelMismatch.status, 1);
  assert.doesNotMatch(kernelMismatch.stdout, /TIMING/);
  const faster = run('faster', undefined, true);
  assert.equal(faster.status, 0, faster.stderr);
  assert.match(faster.stdout, /owned\/LLVM 0.500x; 50.0% faster/);
  const nonces = collections.map(x => fs.readFileSync(x[3]).subarray(16, 32));
  assert.deepEqual(nonces[0].subarray(0, 4), nonces[1].subarray(0, 4));
  assert(!nonces[0].equals(nonces[1]));
  for (const seed of [0, 3276377845, 4294967295]) {
    const fixed = run('success', undefined, true, seed);
    assert.equal(fixed.status, 0, fixed.stderr);
    const again = run('success', undefined, true, seed);
    assert.equal(again.status, 0, again.stderr);
    const requests = [...fixed.commands, ...again.commands].filter(x => x[0] === 'collector')
      .map(x => fs.readFileSync(x[3]).subarray(16, 32));
    assert(requests.every(x => x.readUInt32LE(0) === seed));
    assert.equal(new Set(requests.map(x => x.toString('hex'))).size, 4);
  }
  for (const seed of ['-1', '4294967296', '01', '', '1.0', 'abc']) {
    const bad = run('success', undefined, true, seed);
    assert.equal(bad.status, 2);
    assert.equal(bad.commands.length, 0);
  }
  for (const mode of ['mismatch', 'stale', 'collector-failure']) {
    const bad = run(mode);
    assert.equal(bad.status, 1);
    const at = bad.commands.findIndex(x => x[0] === 'collector');
    const recovery = bad.commands.slice(at + 1).filter(x => x[0] === 'picotool' && x[1] === 'info');
    assert.equal(recovery.length, 2, `${mode}: both recoveries must be checked`);
    assert.match(bad.stderr, /failed/);
  }
  const duplicate = run('success', ['9BBF88DD17D66E7D', '9BBF88DD17D66E7D']);
  assert.equal(duplicate.status, 2);
  assert.equal(duplicate.commands.length, 0);
});
