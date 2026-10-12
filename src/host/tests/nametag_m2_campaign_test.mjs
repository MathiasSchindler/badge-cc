import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { runCampaign, validateMeasurement, stageColdBoot, collectColdBoot, restoreCampaign } from '../tools/nametag_m2_campaign.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const primary = '9BBF88DD17D66E7D', control = '08DA26F86E57F5EA', build = '123abc88';
function report(serial = primary) {
  const words = Array(512).fill(0);
  words[0] = 0x324d544e; words[1] = 1; words[3] = 8; words[5] = 10;
  words[6] = 120000000; words[7] = 200; words[32] = 61120; words[33] = 128;
  words[34] = 128000; words[35] = 128; words[36] = 183360; words[37] = 384000;
  words[43] = 12; words[44] = 2500; words[45] = 80000000; words[46] = 80000;
  words[47] = 128000; words[48] = 128;
  words.splice(8,12,0,0x10000,1,0,0x10000,1,3,12,3,12,0xfabaa0,0x80001000);
  words.splice(20,12,0x41,0x10000,2,2,0x10000,4,3,12,3,12,0xfabaa0,0x80001000);
  words.splice(436,12,...words.slice(20,32));
  for (let i = 64; i < 320; ++i) words[i] = 10000 + i;
  for (let i = 0; i < 10; ++i) { words[384+i*2] = 8000000; words[385+i*2] = 8000; }
  return {verified:true,test:'nametag-m2',serial,build_id:build,nonce:serial.toLowerCase().repeat(2),
    diagnostic_ok:true,error:0,stage:8,words};
}

test('M2 payload accepts measured distributions without inventing TE or visual evidence', () => {
  const summary = validateMeasurement(report(),primary,build);
  assert.equal(summary.loop_mean_sof_ms,8000);
  assert.equal(summary.timer.inherited_ticks_per_ms,477.5);
  assert.equal(summary.skipped_presentation_deadlines,2500);
  assert.equal(summary.te.periods_us,null);
  assert.equal(summary.te.tear_free_qualified,false);
  assert.match(summary.cold_boot,/not performed/);
  assert.equal(summary.benches.full_transfer.microseconds.p99,10095);
});
test('M2 rejects stale, incomplete, slow/wrong-clock or inconsistent evidence', () => {
  for (const change of [
    r=>r.serial=control, r=>r.build_id='deadbeef', r=>r.nonce='0'.repeat(32),
    r=>r.diagnostic_ok=false, r=>r.words[5]=9, r=>r.words[34]=125000,
    r=>r.words[47]=131000, r=>r.words[20]=1, r=>r.words[64]=0,
    r=>r.words[436]^=1, r=>r.words[46]=82000, r=>r.words.pop(),
    r=>r.words[32]=-1, r=>r.words[4]=4, r=>r.words[4]=1,
    r=>{r.words[4]=1;r.words[53]=1;r.words[54]=0;}
  ]) { const r=report(); change(r); assert.throws(()=>validateMeasurement(r,primary,build)); }
});

test('M2 distinguishes cache preparation and storage from cached frame timings', () => {
  const r = report();
  r.words[4] = 1; r.words[53] = 27000; r.words[54] = 118840;
  const summary = validateMeasurement(r,primary,build);
  assert.equal(summary.renderer.mode,'prepared-scene-cache');
  assert.equal(summary.renderer.cache_prepare_us,27000);
  assert.equal(summary.renderer.cache_bytes,118840);
  assert.equal(validateMeasurement(report(),primary,build).renderer.mode,'stateless');
});

test('M2 validates changing-frame differential timing, traffic and animation evidence', () => {
  const r = report(), w = r.words;
  w[4]=3; w[53]=34000; w[54]=118840; w[55]=32*12000; w[56]=32*80;
  w[58]=100*12000; w[59]=100*80; w[61]=100; w[62]=158544; w[63]=0x0204;
  for(let i=0;i<32;++i) w[460+i]=5000+i;
  const summary=validateMeasurement(r,primary,build);
  assert.equal(summary.presentation.benchmark.mean_pixels,12000);
  assert.equal(summary.presentation.benchmark.presenter_microseconds.median,5015);
  assert.equal(summary.presentation.animation.completed_frames_per_second,1.25);
  for(const change of [
    v=>v[4]=2, v=>v[62]=0, v=>v[63]=8, v=>v[460]=0, v=>v[55]=0,
    v=>v[55]++, v=>v[56]=0, v=>v[57]=33, v=>v[61]=201,
    v=>v[58]=101*76800, v=>v[59]=101*4941, v=>v[60]=101
  ]) {
    const bad={...r,words:[...w]}; change(bad.words);
    assert.throws(()=>validateMeasurement(bad,primary,build));
  }
});

test('M2 validates hardware bottleneck profiling without attributing writer time twice', () => {
  const r=report(), w=r.words;
  w[4]=7; w[53]=34000; w[54]=118840; w[55]=32*12000; w[56]=32*80;
  w[58]=100*12000; w[59]=100*80; w[61]=100; w[62]=158544; w[63]=0x0204;
  w.fill(5000,460,492);
  w[492]=32*3000; w[493]=32*200; w[494]=32*160;
  const p=validateMeasurement(r,primary,build).presentation.benchmark.profile;
  assert.equal(p.mean_writer_us,3000);
  assert.equal(p.mean_writer_service_us,200);
  assert.equal(p.mean_presenter_cpu_us,2000);
  assert.equal(p.mean_writer_service_calls,160);
  for(const change of [
    v=>v[4]=4, v=>v[4]=8, v=>v[492]=0, v=>v[492]=160001,
    v=>v[493]=96001, v=>v[494]=5119, v=>v[494]=32*1000
  ]) {
    const bad={...r,words:[...w]}; change(bad.words);
    assert.throws(()=>validateMeasurement(bad,primary,build));
  }
});

function identity(serial) {
  return `type: RP2350\nrevision: A4\npackage: QFN80\nchipid: 0x${serial}\ncurrent cpu: ARM\nsecure boot: 0\nboot type: bootsel\nflash size: 16384K\n`;
}
function fixture(fault) {
  const directory = fs.mkdtempSync(path.join(root,'build/nametag-m2-test-'));
  const run = path.join(directory,'campaign'), picotool = path.join(directory,'picotool');
  fs.writeFileSync(picotool,'mock');
  const devices = new Map([[primary,{byte:17,absent:false}],[control,{byte:34,absent:false}]]);
  const loads = [];
  let collections = 0;
  function execute(command,args) {
    const pass = stdout=>({status:0,stdout:stdout??'',stderr:''});
    if (command === 'make') {
      const buildDir = args.find(arg=>arg.startsWith('BUILD_DIR=')).slice(10);
      const files = ['universe26/uf2/nametag_m2.uf2','host-tools/nametag_m2_host',
        'nametag/m2-8/nametag_m2.elf','host-tools/bin2uf2','nametag/m2-8/nametag_m2_build.h'];
      for (const file of files) {
        const target = path.join(buildDir,file);
        fs.mkdirSync(path.dirname(target),{recursive:true});
        fs.writeFileSync(target,file.endsWith('.h')?`#define USB_REPORT_BUILD_ID 0x${build}u\n`:'artifact');
      }
      return pass();
    }
    if (command.endsWith('nametag_m2_host')) {
      const r=report(args[0]);
      r.nonce=r.nonce.slice(0,24)+(++collections).toString(16).padStart(8,'0');
      if (fault === 'report') r.build_id='deadbeef';
      if (fault === 'cold-report' && collections > 2) r.diagnostic_ok=false;
      return pass(JSON.stringify(r)+'\n');
    }
    const serial = args.at(-1), device = devices.get(serial), op=args[0];
    if (op === 'info') {
      if (device.absent) { device.absent=false; return {status:1,stdout:'',stderr:'No accessible RP-series devices'}; }
      return pass(identity(serial));
    }
    if (op === 'save') {
      if (fault === 'cold-trace' && args[2] === '0x2007f100')
        return {status:1,stdout:'',stderr:'startup trace read failed'};
      const file=args[4];
      const fd=fs.openSync(file,'w');
      fs.ftruncateSync(fd,Number.parseInt(args[3],16)-Number.parseInt(args[2],16));
      fs.writeSync(fd,Buffer.from([device.byte]),0,1,0); fs.closeSync(fd);
      return pass();
    }
    if (op === 'verify') {
      if (fault === 'backup') return {status:1,stderr:'backup verification failed',stdout:''};
      return pass();
    }
    if (op === 'load') {
      loads.push({serial,restore:args.includes('--ignore-partitions'),execute:args.includes('-x')});
      if (args.includes('--ignore-partitions')) {
        device.byte = fs.readFileSync(args[3])[0];
        if (fault === 'restore') device.byte=255;
      } else {
        device.byte=85;
        device.absent=loads.filter(item=>item.serial===serial&&!item.restore).length===2;
        if (fault === 'partial-load') return {status:1,stderr:'partial flash load failed',stdout:''};
        if (fault === 'cold-load' && !args.includes('-x')) return {status:1,stderr:'cold load failed',stdout:''};
      }

      return pass();
    }
    throw Error(`unhandled mock operation: ${op}`);
  }
  return {directory,run,picotool,devices,loads,execute,cleanup:()=>fs.rmSync(directory,{recursive:true,force:true})};
}
for (const fault of [undefined,'backup','report','partial-load','restore']) {
  test(`M2 serial-bound campaign ${fault??'success'} restores attempted flash and does not flash control after failure`, async () => {
    const f=fixture(fault);
    try {
      const promise=runCampaign({serial:primary,controlSerial:control,directory:f.run,picotool:f.picotool,
        execute:f.execute,delay:async()=>{}});
      if (fault) await assert.rejects(promise,/M2 campaign failed/);
      else {
        const result=await promise;
        assert.equal(result.status,'machine-passed');
        assert.equal(result.measurements.length,2);
        assert.equal(result.restorations.length,2);
        assert.equal(result.final_readbacks.length,2);
        assert.equal(f.devices.get(control).byte,34);
      }
      if (fault && fault !== 'restore') assert.equal(f.devices.get(primary).byte,17);
      if (fault) assert(!f.loads.some(item=>item.serial===control&&!item.restore));
      if (fault === 'backup') assert.equal(f.loads.length,0);
      if (fault === 'partial-load' || fault === 'report') assert(f.loads.some(item=>item.serial===primary&&item.restore));
      if (fault === 'restore') {
        const state=JSON.parse(fs.readFileSync(path.join(f.run,'results.json'),'utf8'));
        assert(state.errors.some(error=>/NOT restored|mismatch/.test(error)));
        assert.equal(state.status,'failed');
      }
    } finally { f.cleanup(); }
  });
}

for (const fault of [undefined,'cold-report','cold-load','cold-trace','cancel']) {
  test(`M2 cold-boot ${fault??'success'} stages without execution and independently restores originals`, async () => {
    const f=fixture(fault);
    try {
      await runCampaign({serial:primary,controlSerial:control,directory:f.run,picotool:f.picotool,
        execute:f.execute,delay:async()=>{}});
      const stage=stageColdBoot(f.run,primary,f.picotool,f.execute,async()=>{});
      if (fault === 'cold-load') await assert.rejects(stage,/cold-load-no-execute failed/);
      else {
        await stage;
        assert.equal(f.loads.filter(item=>!item.restore).at(-1).execute,false);
        if (fault === 'cancel') {
          const state=await restoreCampaign(f.run,f.picotool,f.execute,async()=>{});
          assert.equal(state.cold_boot_inflight,null);
          assert.equal(state.cold_boot_runs[0].status,'cancelled-and-restored');
        } else if (fault === 'cold-report') await assert.rejects(
          collectColdBoot(f.run,primary,f.picotool,f.execute,async()=>{}),/device diagnostic failed/);
        else {
          const state=await collectColdBoot(f.run,primary,f.picotool,f.execute,async()=>{});
          assert.equal(state.cold_boot_runs[0].owner_confirmed_full_power_cycle,true);
          assert.equal(state.cold_boot_runs[0].control_readbacks.length,2);
          assert.equal(state.cold_boot_inflight,null);
          if (fault === 'cold-trace')
            assert.match(state.cold_boot_runs[0].startup_trace_error,/cold-startup-trace failed/);
        }
      }
      assert.equal(f.devices.get(primary).byte,17);
      assert.equal(f.devices.get(control).byte,34);
    } finally { f.cleanup(); }
  });
}
