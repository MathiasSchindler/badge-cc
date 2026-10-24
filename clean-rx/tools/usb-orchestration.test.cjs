'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const supervisor = path.join(__dirname, 'usb-process.cjs');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const serials = ['0123456789ABCDEF', 'FEDCBA9876543210'];

function run(command, args, options = {}) {
    const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 15000, ...options });
    if (result.error) throw result.error;
    return result;
}
function supervise(mode, seconds, code, ...args) {
    return run(process.execPath, [supervisor, mode, String(seconds), process.execPath, '-e', code, ...args]);
}
async function until(predicate) {
    for (let i = 0; i < 100; ++i) {
        if (predicate()) return;
        await pause(20);
    }
    assert.fail('Expected process state was not reached');
}
function alive(pid) {
    try { process.kill(pid, 0); return true; }
    catch (error) { if (error.code !== 'ESRCH') throw error; return false; }
}

test('subprocess deadlines preserve exits, report spawn errors and terminate ignored TERM', () => {
    assert.equal(supervise('run', 2, 'process.exit(7)').status, 7);
    assert.equal(supervise('run', 2, '').status, 0);
    const missing = run(process.execPath, [supervisor, 'run', '1', '/missing/r04-executable']);
    assert.equal(missing.status, 127);
    assert.match(missing.stderr, /Cannot start/);
    const start = Date.now();
    const timed = supervise('run', 0.2, "process.on('SIGTERM',()=>{});setInterval(()=>{},100)");
    assert.equal(timed.status, 124, timed.stderr);
    assert.match(timed.stderr, /deadline expired/);
    assert.ok(Date.now() - start < 1800);
    assert.equal(supervise('run', 0.001, '').status, 124);
});

test('readiness retry has a total deadline and never retries protocol mismatch', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'r04-ready-'));
    try {
        const count = path.join(dir, 'count');
        const code = `const fs=require('node:fs'),p=process.argv[1];let n=fs.existsSync(p)?+fs.readFileSync(p):0;fs.writeFileSync(p,String(++n));process.exit(n<3?75:0)`;
        assert.equal(supervise('ready', 2, code, count).status, 0);
        assert.equal(fs.readFileSync(count, 'utf8'), '3');
        fs.unlinkSync(count);
        const mismatch = code.replace('n<3?75:0', '1');
        assert.equal(supervise('ready', 2, mismatch, count).status, 1);
        assert.equal(fs.readFileSync(count, 'utf8'), '1');
        const start = Date.now();
        assert.equal(supervise('ready', 0.3, 'process.exit(75)').status, 124);
        assert.ok(Date.now() - start < 1500);
    } finally { fs.rmSync(dir, { recursive: true }); }
});

test('cancellation kills TERM-resistant descendants and returns the original signal status', async () => {
    for (const [signal, expected] of [['SIGINT', 130], ['SIGTERM', 143]]) {
        const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'r04-cancel-'));
        const file = path.join(dir, 'pid');
        const grandchild = `require('node:fs').writeFileSync(${JSON.stringify(file)},String(process.pid));process.on('SIGTERM',()=>{});setInterval(()=>{},100)`;
        const childCode = `require('node:child_process').spawn(process.execPath,['-e',${JSON.stringify(grandchild)}],{stdio:'inherit'});setInterval(()=>{},100)`;
        const child = spawn(process.execPath, [supervisor, 'run', '10', process.execPath, '-e', childCode],
            { cwd: root, stdio: ['ignore', 'ignore', 'ignore'] });
        const exited = new Promise(resolve => child.on('exit', resolve));
        try {
            await until(() => fs.existsSync(file));
            const pid = +fs.readFileSync(file);
            child.kill(signal);
            assert.equal(await exited, expected);
            await until(() => !alive(pid));
        } finally {
            if (child.exitCode === null) { child.kill('SIGTERM'); await exited; }
            fs.rmSync(dir, { recursive: true });
        }
    }
});

const mock = `#!/usr/bin/env node
const fs=require('node:fs'),path=require('node:path');
const kind=path.basename(process.argv[1]),args=process.argv.slice(2),mode=process.env.R04_MODE;
const log=x=>fs.appendFileSync(process.env.R04_EVENTS,JSON.stringify(x)+'\\n');
log({kind,args,pid:process.pid});
if(kind==='picotool'){
 if((mode==='hung-info'&&args[0]==='info')||(mode==='hung-reboot'&&args[0]==='reboot')||(mode==='hung-load'&&args[0]==='load')){
  process.on('SIGTERM',()=>{});setInterval(()=>{},100);return;
 }else process.exit((mode==='reboot-fail'&&args[0]==='reboot')||(mode==='load-fail'&&args[0]==='load')?7:0);
}
if(kind==='ready'){
 const p=process.env.R04_EVENTS+'.'+args[0],n=fs.existsSync(p)?+fs.readFileSync(p):0;
 fs.writeFileSync(p,String(n+1));
 if(mode==='not-ready')process.exit(1);
 if(mode==='vanished'&&n>=1)process.exit(75);
 if(mode==='delayed'&&n<2)process.exit(75);
 log({ready:args[0]});process.exit(0);
}
if(kind==='peer'){
 fs.writeFileSync(args.at(-1),'private-test-ssid\\nprivate-test-passphrase\\n',{mode:0o600});
 console.log(JSON.stringify({test:'e1-ap-ready',bssid:'02:00:00:00:00:01'}));
 setTimeout(()=>process.exit(124),500);
}else{
 console.log(JSON.stringify({error:0,fresh_ht_rx:true,tx_frame_submitted:true}));
 if(mode==='disconnect'&&args.some(x=>x.endsWith('-rx')))setTimeout(()=>process.exit(7),300);
 else setTimeout(()=>process.exit(0),mode==='disconnect'||mode==='ap-failure'||mode==='cancel'?10000:700);
}
`;
function fixture(mode) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'r04-runner-'));
    const env = { ...process.env, R04_MODE: mode, R04_EVENTS: path.join(dir, 'events'),
        CLEAN_RX_PICOTOOL: path.join(dir, 'picotool'), CLEAN_RX_USB_READY: path.join(dir, 'ready'),
        CLEAN_RX_COLLECTOR: path.join(dir, 'collector'), CLEAN_RX_AP_COLLECTOR: path.join(dir, 'peer'),
        CLEAN_RX_PEER_COLLECTOR: path.join(dir, 'peer'), CLEAN_VENDOR_PEER_COLLECTOR: path.join(dir, 'peer') };
    for (const name of ['picotool', 'ready', 'collector', 'peer'])
        fs.writeFileSync(path.join(dir, name), mock, { mode: 0o755 });
    return { dir, env, prefix: path.join(dir, 'trial'),
        events: () => fs.existsSync(env.R04_EVENTS) ?
            fs.readFileSync(env.R04_EVENTS, 'utf8').trim().split('\n').map(JSON.parse) : [] };
}
function ht(f) {
    return run('bash', ['clean-rx/tools/badge/ht_probe.sh', ...serials, f.prefix, '0'], { env: f.env });
}

test('paired HT waits for both actual USB endpoints before starting either radio window', () => {
    const f = fixture('delayed');
    try {
        const result = ht(f);
        assert.equal(result.status, 0, result.stderr);
        const events = f.events(), firstCollector = events.findIndex(e => e.kind === 'collector');
        for (const serial of serials) assert.ok(events.slice(0, firstCollector).some(e => e.ready === serial));
        assert.equal(events.filter(e => e.kind === 'ready').length, 8);
        assert.equal(JSON.parse(fs.readFileSync(f.prefix + '-pair.json')).fresh_ht_rx, true);
        assert.match(fs.readFileSync(f.prefix + '-ready-' + serials[0] + '.log', 'utf8'), /exit 75/);
    } finally { fs.rmSync(f.dir, { recursive: true }); }
});

test('failed reboot/readiness and a vanished first endpoint stop collectors without overwriting evidence', () => {
    for (const mode of ['reboot-fail', 'not-ready', 'vanished']) {
        const f = fixture(mode);
        try {
            const result = ht(f);
            assert.notEqual(result.status, 0);
            assert.ok(!f.events().some(e => e.kind === 'collector'));
            assert.ok(fs.existsSync(f.prefix + '-boot-' + serials[1] + '.log'));
            const before = fs.readFileSync(f.prefix + '-config.json');
            assert.notEqual(ht(f).status, 0);
            assert.deepEqual(fs.readFileSync(f.prefix + '-config.json'), before);
        } finally { fs.rmSync(f.dir, { recursive: true }); }
    }
});

test('hung BOOTSEL info/reboot are bounded and never trigger a dependent retry or collector', () => {
    for (const mode of ['hung-info', 'hung-reboot']) {
        const f = fixture(mode), start = Date.now();
        try {
            const result = run(process.execPath, [supervisor, 'boot', '0.3',
                f.env.CLEAN_RX_PICOTOOL, serials[0]], { env: f.env });
            assert.equal(result.status, 124, result.stderr);
            assert.ok(Date.now() - start < 1800);
            assert.equal(f.events().filter(e => e.args[0] === 'info').length, 1);
            assert.equal(f.events().filter(e => e.args[0] === 'reboot').length, mode === 'hung-info' ? 0 : 1);
            for (const e of f.events()) assert.ok(!alive(e.pid));
        } finally { fs.rmSync(f.dir, { recursive: true }); }
    }
});

test('all other hardware runners stop on USB readiness failure before dependent RF work', () => {
    for (const [script, extra] of [
        ['badge/lifecycle_probe.sh', ['0']],
        ['badge/wpa2_trial.sh', ['--peer-ap', '1']],
        ['badge/mac_soak.sh', ['1']],
        ['badge/mac_trials.sh', []],
        ['badge/qualification_probe.sh', ['1']],
        ['badge/wpa2_matrix.sh', []],
        ['peer/run.sh', ['0']],
    ]) {
        const f = fixture('not-ready');
        try {
            const result = run('bash', [`clean-rx/tools/${script}`, ...serials, f.prefix, ...extra], { env: f.env });
            assert.notEqual(result.status, 0, script);
            assert.ok(!f.events().some(e => ['collector', 'peer'].includes(e.kind)), script);
            assert.equal(f.events().filter(e => e.kind === 'picotool' && e.args[0] === 'reboot').length, 2, script);
        } finally { fs.rmSync(f.dir, { recursive: true }); }
    }
});

test('a failed flash stops the reference runner; cancellation drains an in-progress load', async () => {
    for (const mode of ['load-fail', 'hung-load']) {
        const f = fixture(mode);
        f.env.CLEAN_RX_REFERENCE_BUILD = f.dir;
        const args = ['clean-rx/tools/vendor-burst/run.sh', ...serials, f.prefix];
        try {
            if (mode === 'load-fail') {
                assert.notEqual(run('bash', args, { env: f.env }).status, 0);
            } else {
                const child = spawn('bash', args, { cwd: root, env: f.env, stdio: 'ignore' });
                const exited = new Promise(resolve => child.on('exit', resolve));
                try {
                    await until(() => f.events().some(e => e.args[0] === 'load'));
                    child.kill('SIGTERM');
                    assert.equal(await exited, 143);
                } finally {
                    if (child.exitCode === null) { child.kill('SIGTERM'); await exited; }
                }
            }
            const events = f.events();
            assert.equal(events.filter(e => e.args[0] === 'load').length, 1);
            assert.ok(!events.some(e => e.args[0] === 'reboot'));
            for (const e of events) await until(() => !alive(e.pid));
        } finally { fs.rmSync(f.dir, { recursive: true }); }
    }
});

test('disconnect terminates the other endpoint promptly and cannot produce a passing pair', async () => {
    const f = fixture('disconnect'), start = Date.now();
    try {
        assert.notEqual(ht(f).status, 0);
        assert.ok(Date.now() - start < 4000);
        assert.ok(!fs.existsSync(f.prefix + '-pair.json'));
        for (const e of f.events().filter(e => e.kind === 'collector')) await until(() => !alive(e.pid));
        assert.match(fs.readFileSync(f.prefix + '-tx.stderr', 'utf8'), /cancelled/);
    } finally { fs.rmSync(f.dir, { recursive: true }); }
});

test('WPA2 infrastructure failure cannot masquerade as a passing wrong-password control; secrets are cleaned', async () => {
    const f = fixture('ap-failure');
    try {
        const result = run('bash', ['clean-rx/tools/badge/wpa2_trial.sh', ...serials,
            f.prefix, '--peer-ap', '1', '--wrong-password'], { env: f.env });
        assert.notEqual(result.status, 0);
        assert.ok(!fs.existsSync(f.prefix + '-pair.json'));
        assert.ok(!fs.readdirSync(f.dir).some(name => name.includes('.private.')));
        for (const e of f.events().filter(e => ['collector', 'peer'].includes(e.kind)))
            await until(() => !alive(e.pid));
    } finally { fs.rmSync(f.dir, { recursive: true }); }
});

test('explicit negative-control exit 1 is allowed, but deadline exit 124 is never accepted', () => {
    const f = fixture('normal');
    try {
        for (const exit of [1, 124]) {
            const result = run('bash', ['-c', `
source clean-rx/tools/usb-run.sh
"\${usb_command[@]}" 2 node -e 'process.exit(${exit})' & first=$!
"\${usb_command[@]}" 2 node -e 'process.exit(1)' & second=$!
usb_wait_pair "$first" "$second" 1
`], { env: f.env });
            assert.equal(result.status, exit === 1 ? 0 : 1, result.stderr);
        }
    } finally { fs.rmSync(f.dir, { recursive: true }); }
});

test('runner cancellation drains both supervised collectors', async () => {
    const f = fixture('cancel');
    const child = spawn('bash', ['clean-rx/tools/badge/ht_probe.sh', ...serials, f.prefix, '0'],
        { cwd: root, env: f.env, stdio: 'ignore' });
    const exited = new Promise(resolve => child.on('exit', resolve));
    try {
        await until(() => f.events().filter(e => e.kind === 'collector').length === 2);
        child.kill('SIGTERM');
        assert.equal(await exited, 143);
        for (const e of f.events().filter(e => e.kind === 'collector')) await until(() => !alive(e.pid));
    } finally {
        if (child.exitCode === null) { child.kill('SIGTERM'); await exited; }
        fs.rmSync(f.dir, { recursive: true });
    }
});

test('readiness probe validates configuration/protocol without submitting a radio request', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'r04-c-ready-'));
    try {
        const flags = run('pkg-config', ['--cflags', 'libusb-1.0']);
        assert.equal(flags.status, 0, flags.stderr);
        const binary = path.join(dir, 'test');
        const built = run('cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', ...flags.stdout.trim().split(/\s+/),
            'clean-rx/tools/usb-ready-test.c', '-o', binary]);
        assert.equal(built.status, 0, built.stderr);
        const checked = run(binary, []);
        assert.equal(checked.status, 0, checked.stderr);
    } finally { fs.rmSync(dir, { recursive: true }); }
});

test('readiness tool output is independent of inherited firmware BUILD overrides', () => {
    const result = run('make', ['-n', '-C', 'clean-rx/badge',
        'BUILD=/unused-r04-badge-build', 'usb-tools']);
    assert.equal(result.status, 0, result.stderr);
    assert.doesNotMatch(result.stdout, /\/unused-r04-badge-build\/usb-ready/);
    assert.match(result.stdout, /tools\/Makefile/);
});
