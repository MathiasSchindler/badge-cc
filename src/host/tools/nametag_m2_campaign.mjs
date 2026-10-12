import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import {
  validateIdentity, fingerprint, sourceFiles, validSerial, saveState, commands,
  waitForBootsel, checkBackup, restore
} from './nametag_control.mjs';

const script = fileURLToPath(import.meta.url);
const root = path.resolve(path.dirname(script), '../../..');
const flashBytes = 16 * 1024 * 1024;
const names = ['full_transfer', 'footer_320x64', 'tile_64x64', 'commands',
  'background', 'text', 'render', 'render_transfer'];
const statistics = samples => {
  const sorted = [...samples].sort((a,b) => a-b);
  return {min:sorted[0], median:sorted[Math.ceil(sorted.length * .5) - 1],
    p99:sorted[Math.ceil(sorted.length * .99) - 1], max:sorted.at(-1)};
};

export function validateMeasurement(record, serial, build) {
  assert.equal(record.verified, true, 'unverified transport');
  assert.equal(record.test, 'nametag-m2');
  assert.equal(record.serial, serial, 'measurement serial mismatch');
  assert.equal(record.build_id, build, 'measurement build mismatch');
  assert.match(record.nonce, /^[0-9a-f]{32}$/);
  assert.notEqual(record.nonce, '0'.repeat(32), 'empty nonce');
  const w = record.words;
  assert(Array.isArray(w) && w.length === 512 &&
    w.every(value => Number.isInteger(value) && value >= 0 && value <= 0xffffffff), 'invalid payload shape');
  assert.equal(w[0], 0x324d544e);
  assert.equal(w[1], 1);
  assert.equal(record.diagnostic_ok, true, `device diagnostic failed: error ${w[2]}, stage ${w[3]}, detail ${w[52]}`);
  assert.equal(w[2], 0, 'device error');
  assert.equal(w[3], 8, 'incomplete measurement');
  assert.equal(w[4] & ~7, 0, 'unknown renderer flags');
  if (w[4] & 4) assert.equal(w[4] & 3, 3, 'presenter profiling requires cached differential transfer');
  if (w[4] & 1) {
    assert(w[53] > 0, 'missing cache preparation timing');
    assert(w[54] > 0 && w[54] <= 508 * 1024, 'invalid cache storage size');
  }
  assert.equal(w[5], 10, 'missing loop periods');
  assert.equal(w[33], 128, 'missing inherited calibration');
  assert(w[32] > 0);
  for (const [ticks,frames] of [[34,35],[47,48]]) {
    assert.equal(w[frames], 128, 'missing crystal calibration');
    assert(Math.abs(w[ticks] / 128000 - 1) <= .01, 'timer is not within 1% of nominal USB milliseconds');
  }
  const before = w.slice(8,20), after = w.slice(20,32);
  assert.equal(after[1], before[1], 'CPU divider changed');
  if (!(before[0] & 1)) {
    assert.equal(before[3] & 3, 0, 'unqualified inherited REF source');
    assert.equal(before[4], 0x10000);
    assert.equal(after[0] & 0xe1, 0x41, 'CPU must retain ROSC, not follow switched REF');
  } else assert.equal(after[0], before[0], 'CPU source changed');
  assert.equal(after[3] & 3, 2);
  assert.equal(after[4], 0x10000);
  assert.equal(after[5], 4);
  assert.equal(after[6], 3);
  assert.equal(after[7], 12);
  assert.deepEqual(w.slice(436,448), after, 'clock/tick state changed during campaign');
  assert(w[36] > 0 && Math.abs(w[37] / (12000 * 32) - 1) < .01, 'missing/invalid frequency counter result');
  assert(w[42] <= 32 && w[43] > 0 && w[43] < 2048, 'invalid SOF capture');
  const loops = Array.from({length:10}, (_,i) => ({ticks:w[384+i*2], sof:w[385+i*2]}));
  assert(loops.every(loop => loop.ticks > 0 && loop.sof > 0), 'empty loop evidence');
  assert.equal(loops.reduce((sum,loop)=>sum+loop.ticks,0), w[45]);
  assert.equal(loops.reduce((sum,loop)=>sum+loop.sof,0), w[46]);
  assert(Math.abs(w[46] / 80000 - 1) <= .01, 'ten 8-second loop markers exceed 1% nominal SOF timing');
  assert(Math.abs(w[45] / (w[46] * 1000) - 1) <= .01, 'loop timer/SOF disagreement');
  const usPerTick = w[35] * 1000 / w[34];
  let presentation = {mode:'full-frame'};
  if (w[4] & 2) {
    assert(w[4] & 1, 'differential transfer requires the prepared cache');
    assert(w[62] > 153600 && w[62] <= 508 * 1024, 'invalid presenter storage size');
    assert.equal(w[63],0x0204,'unqualified differential grid');
    const samples = w.slice(460,492);
    assert(samples.every(value=>value>0),'missing differential presentation timings');
    assert(w[55] > 0 && w[55] <= 32*76800 && w[55] % 32 === 0,
      'missing/inconsistent changed-frame payload');
    assert(w[56] > 0 && w[56] <= 32*4941 && w[56] % 32 === 0,
      'missing/inconsistent changed-frame regions');
    assert(w[57] <= 32 && w[57]*76800 <= w[55],'invalid full-frame benchmark count');
    assert(w[61] > 0 && w[61] <= w[7],'invalid animation frame count');
    assert(w[58] > 0 && w[58] <= w[61]*76800 && w[59] > 0 && w[59] <= w[61]*4941,
      'invalid animation payload/regions');
    assert(w[60] <= w[61] && w[60]*76800 <= w[58],'invalid full-frame animation count');
    presentation = {mode:'changed-regions',state_bytes:w[62],grid:{tile:4,offset:2},
      benchmark:{previous_ms:5467,current_ms:5500,mean_pixels:w[55]/32,mean_regions:w[56]/32,
        full_frames:w[57],presenter_microseconds:statistics(samples.map(value=>value*usPerTick))},
      animation:{frames:w[61],pixels:w[58],regions:w[59],full_frames:w[60],
        payload_fraction_of_full:w[58]/(w[61]*76800),completed_frames_per_second:w[61]/80}};
    if (w[4] & 4) {
      const total = samples.reduce((sum,value)=>sum+value,0);
      assert(w[492] > 0 && w[492] <= total,'invalid writer profiling time');
      assert(w[493] > 0 && w[493] <= w[492],'invalid writer service profiling time');
      assert(w[494] >= w[56]*2 && w[494] <= w[56]*2 + w[55]/16,
        'invalid writer service profiling count');
      presentation.benchmark.profile = {
        mean_writer_us:w[492]*usPerTick/32,
        mean_writer_service_us:w[493]*usPerTick/32,
        mean_presenter_cpu_us:(total-w[492])*usPerTick/32,
        mean_writer_service_calls:w[494]/32,
        scope:'writer timings include instrumentation; service is inside writer; residual includes comparison, planning, commit and outer service'
      };
    }
  }
  const benches = Object.fromEntries(names.map((name,index) => {
    const raw = w.slice(64+index*32,96+index*32);
    assert(raw.every(sample => sample > 0), `missing ${name} samples`);
    return [name,{samples_ticks:raw, microseconds:statistics(raw.map(value=>value*usPerTick))}];
  }));
  assert(w[40] <= 32 && w[41] <= 32, 'invalid TE sample counts');
  const periods = w.slice(320,320+w[40]), widths = w.slice(352,352+w[41]);
  assert(periods.every(v=>v>0) && widths.every(v=>v>0), 'empty TE timing sample');
  return {
    timer:{inherited_ticks_per_ms:w[32]/w[33], crystal_ticks_per_ms:w[34]/w[35],
      final_ticks_per_ms:w[47]/w[48], reference:'nominal 1 ms USB SOF; not metrology-grade absolute calibration'},
    cpu_khz:w[36]/32, reference_khz:w[37]/32, clocks_before:before, clocks_after:after,
    benches, loops, loop_mean_sof_ms:w[46]/10, loop_total_timer_us:w[45], loop_total_sof_ms:w[46],
    renderer:{mode:w[4] & 1 ? 'prepared-scene-cache' : 'stateless',
      cache_prepare_us:w[4] & 1 ? w[53] * usPerTick : null,
      cache_bytes:w[4] & 1 ? w[54] : 0},
    presentation,
    skipped_presentation_deadlines:w[44], max_sof_step:w[43],
    frames:w[7], duration_us:w[6],
    te:{off_edges:w[38], on_edges:w[39], rising_period_ticks:periods, high_width_ticks:widths,
      periods_us:periods.length ? statistics(periods.map(v=>v*usPerTick)) : null,
      high_width_us:widths.length ? statistics(widths.map(v=>v*usPerTick)) : null,
      rising_wait_timeouts:w[42], aligned_full_transfers_ticks:Array.from({length:16},(_,i)=>w[404+i*2]),
      tear_free_qualified:false},
    visual_acceptance:'not recorded', cold_boot:'not performed by software launch'
  };
}

async function verifyUntouched(io, directory, state, selected) {
  const backup = state.backups.find(item=>item.serial === selected);
  checkBackup(directory, backup);
  validateIdentity(io.device(`final-identity-${selected}`, selected, ['info','-a']), selected);
  const file = path.join(directory, `${selected}-final-readback.bin`);
  io.device(`final-readback-${selected}`, selected, ['save','-r','0x10000000','0x11000000',file,'-t','bin']);
  assert.equal(fs.statSync(file).size, flashBytes);
  assert.equal(fingerprint(file).sha256, backup.sha256, 'original flash mismatch');
  return fingerprint(file);
}

export async function restoreCampaign(directory, picotool, execute = spawnSync,
  delay = ms=>new Promise(resolve=>setTimeout(resolve,ms))) {
  directory = fs.realpathSync(directory);
  const state = JSON.parse(fs.readFileSync(path.join(directory,'results.json'),'utf8'));
  assert.equal(state.schema,'nametag-m2-campaign-v1');
  const io = commands(directory,picotool,execute);
  try {
    for (const selected of state.flash_attempts) {
      validSerial(selected);
      state.serial = selected;
      await restore(io,directory,state,delay);
    }
    for (const backup of state.backups) await verifyUntouched(io,directory,state,backup.serial);
    if (state.cold_boot_inflight) {
      const run = state.cold_boot_runs.at(-1);
      if (run.status === 'awaiting-physical-power-cycle') run.status='cancelled-and-restored';
      state.cold_boot_inflight=null;
    }
  } catch(error) {
    state.errors.push(`Manual restoration failed: ${error.message}`);
    state.status='failed';
    saveState(directory,state);
    throw error;
  }
  saveState(directory,state);
  return state;
}

export async function stageColdBoot(directory, selected, picotool, execute = spawnSync,
  delay = ms=>new Promise(resolve=>setTimeout(resolve,ms))) {
  directory = fs.realpathSync(directory);
  const state = JSON.parse(fs.readFileSync(path.join(directory,'results.json'),'utf8'));
  assert.equal(state.schema,'nametag-m2-campaign-v1');
  assert.equal(state.measurements.length,2,'normal paired campaign must pass before cold-boot staging');
  for (const measurement of state.measurements) {
    validateMeasurement(measurement.record,measurement.serial,state.build_id);
    assert(measurement.no_collector_recovery,'normal no-collector recovery missing');
  }
  assert.equal(state.final_readbacks.length,2,'normal final readbacks missing');
  assert(state.errors.every(error=>error.startsWith('Cold-boot ')),'unresolved normal campaign failure');
  validSerial(selected);
  const backup = state.backups.find(item=>item.serial === selected);
  assert(backup,'device was not backed up by this campaign');
  checkBackup(directory,backup);
  const io = commands(directory,picotool,execute);
  validateIdentity(io.device('cold-identity',selected,['info','-a']),selected);
  io.device('cold-backup-verify',selected,['verify',backup.file,'-t','bin','-o','0x10000000']);
  for (const artifact of state.artifacts)
    assert.equal(fingerprint(artifact.file).sha256,artifact.sha256,'cold-boot artifact drift');
  assert(!state.cold_boot_inflight,'another cold-boot device is already staged');
  state.serial = selected;
  state.cold_boot_inflight = selected;
  state.cold_boot_runner = fingerprint(script);
  state.cold_boot_runs ??= [];
  const run = {serial:selected,staged_at:new Date().toISOString(),status:'awaiting-physical-power-cycle'};
  state.cold_boot_runs.push(run);
  saveState(directory,state);
  let attempted = false;
  try {
    const build = path.join(directory,`cold-build-${state.cold_boot_runs.length}`);
    io.run('cold-build','make',[`BUILD_DIR=${build}`,'NAMETAG_COVERAGE=8','nametag-m2'],180000);
    const header = fs.readFileSync(path.join(build,'nametag/m2-8/nametag_m2_build.h'),'utf8');
    const match = /#define USB_REPORT_BUILD_ID 0x([0-9a-f]{8})u/.exec(header);
    assert(match,'cold-boot build identity missing');
    run.build_id=match[1];
    run.artifacts=[path.join(build,'universe26/uf2/nametag_m2.uf2'),path.join(build,'host-tools/nametag_m2_host'),
      path.join(build,'nametag/m2-8/nametag_m2.elf')].map(fingerprint);
    saveState(directory,state);
    attempted = true;
    io.device('cold-load-no-execute',selected,['load','-v',run.artifacts[0].file]);
    validateIdentity(io.device('cold-staged-identity',selected,['info','-a']),selected);
  } catch(error) {
    run.status='failed'; state.errors.push(`Cold-boot staging ${selected}: ${error.message}`);
    state.status='failed';
    try { if (attempted) await restore(io,directory,state,delay); state.cold_boot_inflight=null; }
    catch(recovery) { state.errors.push(`Cold-boot original flash NOT restored: ${recovery.message}`); }
    saveState(directory,state);
    throw error;
  }
  return state;
}

export async function collectColdBoot(directory, selected, picotool, execute = spawnSync,
  delay = ms=>new Promise(resolve=>setTimeout(resolve,ms))) {
  directory = fs.realpathSync(directory);
  const state = JSON.parse(fs.readFileSync(path.join(directory,'results.json'),'utf8'));
  assert.equal(state.schema,'nametag-m2-campaign-v1');
  validSerial(selected);
  assert.equal(state.cold_boot_inflight,selected,'selected device was not staged for cold boot');
  const run = state.cold_boot_runs.at(-1);
  assert.equal(run.serial,selected);
  assert.equal(run.status,'awaiting-physical-power-cycle');
  const io = commands(directory,picotool,execute);
  state.serial = selected;
  let failure;
  try {
    for (const artifact of run.artifacts)
      assert.equal(fingerprint(artifact.file).sha256,artifact.sha256,'cold-boot artifact drift');
    checkBackup(directory,state.backups.find(item=>item.serial === selected));
    const text = io.run('cold-collector',run.artifacts[1].file,[selected],225000);
    fs.writeFileSync(path.join(directory,`${selected}-cold-measurement.json`),text,{mode:0o600});
    const record = JSON.parse(text);
    const summary = validateMeasurement(record,selected,run.build_id);
    const previous = [...state.measurements.map(item=>item.record.nonce),
      ...state.cold_boot_runs.flatMap(item=>item.record ? [item.record.nonce] : [])];
    assert(!previous.includes(record.nonce),'repeated cold-boot measurement nonce');
    run.record=record; run.summary=summary;
    run.owner_confirmed_full_power_cycle=true;
    run.status='machine-passed-after-owner-confirmed-power-cycle';
  } catch(error) {
    failure=error;
    run.status='failed';
    state.errors.push(`Cold-boot collection ${selected}: ${error.message}`);
    state.status='failed';
  } finally {
    try {
      await waitForBootsel(io,selected,'cold-trace-recovery',delay);
      const traceFile=path.join(directory,`${selected}-startup-trace-${state.cold_boot_runs.length}.bin`);
      io.device('cold-startup-trace',selected,['save','-r','0x2007f100','0x2007f140',traceFile,'-t','bin']);
      const trace=fs.readFileSync(traceFile);
      if (trace.length === 64 && trace.readUInt32LE(0) === 0x3254534d &&
          trace.readUInt32LE(4).toString(16).padStart(8,'0') === run.build_id) {
        run.startup_trace={matched_build:true,stage:trace.readUInt32LE(8),error:trace.readInt32LE(12),
          clocks:Array.from({length:12},(_,i)=>trace.readUInt32LE(16+i*4)),
          scope:'retained startup telemetry, not a nonce-authenticated passing report'};
      } else run.startup_trace={matched_build:false,scope:'no matching retained startup telemetry'};
    } catch(error) {
      run.startup_trace_error=error.message;
    }
    try {
      await restore(io,directory,state,delay);
      run.restoration=state.restoration;
      state.cold_boot_inflight=null;
      run.control_readbacks=[];
      for (const backup of state.backups)
        run.control_readbacks.push({serial:backup.serial,...await verifyUntouched(io,directory,state,backup.serial)});
    } catch(error) {
      failure ??= error;
      state.errors.push(`Cold-boot original flash NOT verified restored: ${error.message}`);
      state.status='failed';
    }
    run.finished_at=new Date().toISOString();
    saveState(directory,state);
  }
  if (failure) throw failure;
  return state;
}

export async function runCampaign({serial,controlSerial,directory,picotool,execute = spawnSync,
  delay = ms=>new Promise(resolve=>setTimeout(resolve,ms))}) {
  validSerial(serial); validSerial(controlSerial);
  assert.notEqual(serial,controlSerial);
  directory = path.resolve(directory);
  const parent = fs.realpathSync(path.dirname(directory)), buildRoot = fs.realpathSync(path.join(root,'build'));
  assert(parent === path.dirname(directory) && (parent === buildRoot || parent.startsWith(buildRoot + path.sep)),
    'fresh evidence directory must be under ignored build without symlink parents');
  fs.mkdirSync(directory,{mode:0o700});
  const state = {schema:'nametag-m2-campaign-v1',started_at:new Date().toISOString(),
    serial,control_serial:controlSerial,status:'preflight',backups:[],flash_attempts:[],
    restorations:[],measurements:[],errors:[],restored:false};
  saveState(directory,state);
  const io = commands(directory,picotool,execute), nonces = new Set();
  try {
    state.inputs = [script,path.join(root,'src/host/tools/nametag_control.mjs'),path.join(root,'Makefile'),picotool,process.execPath,
      ...['arch/rp2350','shared','tools','platform/badger','usb','radio'].flatMap(dir=>sourceFiles(path.join(root,'src',dir)))].map(fingerprint);
    const build = path.join(directory,'build');
    io.run('build','make',[`BUILD_DIR=${build}`,'NAMETAG_COVERAGE=8','nametag-m2'],180000);
    const image = path.join(build,'universe26/uf2/nametag_m2.uf2'), collector = path.join(build,'host-tools/nametag_m2_host');
    const header = fs.readFileSync(path.join(build,'nametag/m2-8/nametag_m2_build.h'),'utf8');
    const match = /#define USB_REPORT_BUILD_ID 0x([0-9a-f]{8})u/.exec(header);
    assert(match,'missing M2 build identity');
    state.build_id = match[1];
    state.artifacts = [image,collector,path.join(build,'nametag/m2-8/nametag_m2.elf'),
      path.join(build,'host-tools/bin2uf2')].map(fingerprint);
    saveState(directory,state);
    for (const selected of [serial,controlSerial]) {
      console.log(`M2 ${selected}: fresh full-flash backup and verification; no diagnostic loaded yet.`);
      validateIdentity(io.device(`identity-${selected}`,selected,['info','-a']),selected);
      const file = path.join(directory,`${selected}-original.bin`);
      io.device(`backup-${selected}`,selected,['save','-r','0x10000000','0x11000000',file,'-t','bin']);
      assert.equal(fs.statSync(file).size,flashBytes);
      state.backups.push({serial:selected,...fingerprint(file)});
      saveState(directory,state);
      io.device(`backup-verify-${selected}`,selected,['verify',file,'-t','bin','-o','0x10000000']);
    }
    for (const selected of [serial,controlSerial]) {
      console.log(`M2 ${selected}: starting diagnostic; watch for the serial-labelled name and contrast screens.`);
      state.serial = selected; state.status = 'running'; state.restored = false;
      for (const backup of state.backups) checkBackup(directory,backup);
      for (const artifact of state.artifacts) assert.equal(fingerprint(artifact.file).sha256,artifact.sha256,'artifact drift');
      validateIdentity(io.device(`run-identity-${selected}`,selected,['info','-a']),selected);
      state.flash_attempts.push(selected); saveState(directory,state);
      io.device(`load-${selected}`,selected,['load','-v','-x',image]);
      const text = io.run(`collector-${selected}`,collector,[selected],225000);
      fs.writeFileSync(path.join(directory,`${selected}-measurement.json`),text,{mode:0o600});
      const record = JSON.parse(text);
      const summary = validateMeasurement(record,selected,state.build_id);
      assert(!nonces.has(record.nonce),'repeated measurement nonce'); nonces.add(record.nonce);
      state.measurements.push({serial:selected,record,summary});
      saveState(directory,state);
      await waitForBootsel(io,selected,`report-return-${selected}`,delay);
      validateIdentity(io.device(`no-collector-identity-${selected}`,selected,['info','-a']),selected);
      io.device(`no-collector-load-${selected}`,selected,['load','-v','-x',image]);
      state.measurements.at(-1).no_collector_recovery =
        await waitForBootsel(io,selected,`no-collector-return-${selected}`,delay,true);
      await restore(io,directory,state,delay);
      state.restorations.push({serial:selected,...state.restoration});
      console.log(`M2 ${selected}: original full flash independently verified restored; device in BOOTSEL.`);
      await verifyUntouched(io,directory,state,selected === serial ? controlSerial : serial);
      saveState(directory,state);
    }
  } catch(error) { state.errors.push(error.message); saveState(directory,state); }
  finally {
    for (const selected of state.flash_attempts) {
      if (state.restorations.some(item=>item.serial === selected)) continue;
      try {
        state.serial = selected;
        await restore(io,directory,state,delay);
        state.restorations.push({serial:selected,...state.restoration});
      } catch(error) { state.errors.push(`Original flash NOT restored for ${selected}: ${error.message}`); }
      saveState(directory,state);
    }
    state.final_readbacks = [];
    for (const backup of state.backups) {
      try { state.final_readbacks.push({serial:backup.serial,...await verifyUntouched(io,directory,state,backup.serial)}); }
      catch(error) { state.errors.push(`Final verification ${backup.serial}: ${error.message}`); }
    }
    state.status = state.errors.length === 0 && state.measurements.length === 2 &&
      state.restorations.length === 2 && state.final_readbacks.length === 2 ? 'machine-passed' : 'failed';
    state.finished_at = new Date().toISOString();
    state.visual_acceptance = 'pending'; state.cold_boot_acceptance = 'pending';
    saveState(directory,state);
  }
  assert.equal(state.status,'machine-passed',`M2 campaign failed: ${state.errors.join('; ')}; evidence: ${directory}`);
  return state;
}

if (process.argv[1] && path.resolve(process.argv[1]) === script) {
  try {
    const args = process.argv.slice(2);
    const picotool = fs.realpathSync(process.env.PICOTOOL ?? path.join(root,'build/picotool-usb/picotool'));
    if (args.length === 2 && args[0] === '--restore') {
      await restoreCampaign(args[1],picotool);
      console.log('Original flashes verified; both badges in BOOTSEL. A failed campaign remains failed.');
    } else if (args.length === 3 && args[0] === '--cold-boot-stage') {
      await stageColdBoot(args[1],args[2],picotool);
      console.log(`M2 ${args[2]} staged without execution. Remove all power, including battery power, then reconnect USB without holding Boot; confirm promptly before recovery expires. Unplugging USB alone is not a cold boot when the battery remains connected. If full power removal is not practical, use --restore RUN_DIRECTORY instead; do not confirm a power cycle.`);
    } else if (args.length === 4 && args[0] === '--cold-boot-collect' && args[3] === '--power-cycle-confirmed') {
      await collectColdBoot(args[1],args[2],picotool);
      console.log(`M2 ${args[2]} cold-boot collection passed; both original flashes verified and devices in BOOTSEL. Visual acceptance is separate.`);
    } else {
      assert.equal(args.length,3,'Usage: nametag_m2_campaign.mjs PRIMARY_SERIAL CONTROL_SERIAL NEW_BUILD_DIRECTORY | --restore RUN_DIRECTORY | --cold-boot-stage RUN_DIRECTORY SERIAL | --cold-boot-collect RUN_DIRECTORY SERIAL --power-cycle-confirmed');
      const state = await runCampaign({serial:args[0],controlSerial:args[1],directory:args[2],picotool});
      console.log(`M2 ${state.status}: both badges measured and original flash verified. Visual/cold-boot acceptance remains pending.`);
      console.log(`Private evidence: ${path.resolve(args[2])}`);
    }
  } catch(error) { console.error(error.message); process.exitCode = 1; }
}
