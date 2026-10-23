'use strict';
const assert = require('node:assert/strict');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const exe = path.join(__dirname, '..', 'build', process.platform === 'win32' ? 'experiment_test.exe' : 'experiment_test');
const source = execFileSync(exe, ['--script'], {encoding:'ascii'});

async function main() {
    const elements = new Map(), calls = [], polls = [];
    const el = id => {
        if (!elements.has(id)) elements.set(id, {value:'',textContent:''});
        return elements.get(id);
    };
    const config = {frequency_hz:869850000,sf:7,bandwidth_khz:125,coding_rate:1,preamble:8};
    const radio = {revision:1,config,airtime_us:77056,queued:0,wait_ms:1000};
    let release, first = true, fail = true;
    const context = {
        document:{getElementById:el},
        AbortSignal:{timeout:()=>undefined},
        setTimeout:(fn, delay) => delay === 50 ? setImmediate(fn) : polls.push(fn),
        fetch:async (url, options) => {
            calls.push([url, options.method, options.body]);
            if (first) {
                first = false;
                await new Promise(resolve => {release = resolve;});
            }
            const command = options.method === 'POST';
            return {ok:!command || !fail, status:command && fail ? 503 : 200,
                json:async () => command && fail ? {error:'Configuration failed'} :
                    url === '/api/health' ? {serial:'test',ip:'192.168.1.1'} : radio};
        }
    };
    vm.runInNewContext(source, context);
    const pending = el('baseline').onclick();
    assert.equal(calls.length,1);
    release();
    await pending;
    assert.deepEqual(calls.map(c=>c[0]),['/api/health','/api/radio','/api/radio/reset']);
    assert.equal(el('error').textContent,'Configuration failed');
    await polls.shift()();
    assert.equal(el('error').textContent,'Configuration failed'); // Successful poll must not hide command failures.
    fail = false;
    await el('baseline').onclick();
    assert.equal(el('error').textContent,'');
    el('frequency').value='868.300000';
    el('sf').value='8';el('bw').value='250';el('cr').value='2';el('preamble').value='12';
    el('settings').onsubmit({preventDefault(){}});
    await new Promise(resolve=>setImmediate(resolve));
    const command = calls.find(c=>c[0]==='/api/radio/config');
    assert.equal(command[2],'frequency_hz=868300000&sf=8&bandwidth_khz=250&coding_rate=2&preamble=12');
    console.log('PASS: serialized browser commands, exact profile fields and persistent error reporting');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
