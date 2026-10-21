'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const {aes, expandKey, decrypt, parseKeys, protobuf, interpret, decodePacket, Capture, unhex, hex} =
    require('../src/app/dashboard.js');

for (const [key, cipher] of [
    ['000102030405060708090a0b0c0d0e0f', '69c4e0d86a7b0430d8cdb78070b4c55a'],
    ['000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f', '8ea2b7ca516745bfeafc49904b496089']
]) assert.equal(hex(aes(expandKey(unhex(key)), unhex('00112233445566778899aabbccddeeff'))), cipher);
assert.throws(() => expandKey(new Uint8Array(5)));
assert.throws(() => unhex('xx'));
assert.throws(() => protobuf(unhex('1280')));
assert.throws(() => protobuf(unhex('0f')));
assert.throws(() => protobuf(unhex('08ffffffffffffffffffff02')));
assert.throws(() => parseKeys('wrong'));
assert.throws(() => parseKeys('test !!!'));
assert.throws(() => parseKeys('test AAA='));
assert.equal(parseKeys('Plain AA==')[1].key.length, 0);

const keys = parseKeys('Test AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=');
function packet(sequence = 1) {
    const frame = unhex('ffffffff443322118877665563080044');
    return {sequence, uptime_ms: 1000, profile: 'meshtastic', hex: hex(frame),
        length: 16, crc_error: false, snr_qdb: 8, rssi_qdb: -400, signal_rssi_qdb: -404,
        mesh: {to:'ffffffff', from:'11223344', id:'55667788', channel:8, hop_limit:3, hop_start:3}};
}
const plaintext = unhex('0801120568656c6c6f');
function encryptPacket(p, key, plain = plaintext) {
    const frame = unhex(p.hex).slice(0, 16), iv = new Uint8Array(16);
    iv.set(frame.slice(8, 12)); iv.set(frame.slice(4, 8), 8);
    const c = crypto.createCipheriv('aes-' + key.length * 8 + '-ctr', key, iv);
    const encrypted = Buffer.concat([c.update(plain), c.final()]);
    p.hex = hex(Buffer.concat([frame, encrypted])); p.length = p.hex.length / 2;
    assert.equal(hex(decrypt(key, unhex(p.hex))), hex(plain));
    return p;
}
const p = encryptPacket(packet(), keys[0].key);
assert.equal(decodePacket(p, keys).data.text, 'hello');
assert.equal(decodePacket(p, keys).port_name, 'TEXT_MESSAGE_APP');
p.mesh.channel = 0x57;
assert.equal(decodePacket(p, keys).hash_matches, false);
assert.equal(decodePacket(p, keys).data.text, 'hello');
p.crc_error = true;
assert.match(decodePacket(p, keys).status, /not decoded/);
p.crc_error = false; p.mesh.channel = 0; p.mesh.to = '12345678';
assert.match(decodePacket(p, keys).status, /PKI/);
const privatePacket = encryptPacket(packet(), keys[1].key);
assert.equal(decodePacket(privatePacket, keys).key_name, 'Test');
assert.match(decodePacket(privatePacket, keys.slice(0, 1)).status, /no valid decode/);
const long = new Uint8Array(239).map((_, i) => i);
encryptPacket(packet(), keys[1].key, long);
assert.deepEqual(interpret(3, unhex('0d8004a31e1500e1f50518227007')).latitude, 51.4);
assert.equal(interpret(3, unhex('18ffffffff0f')).altitude_m, -1);
assert.equal(interpret(4, unhex('120474657374')).long_name, 'test');
assert.equal(interpret(67, unhex('120708641500008040')).device.voltage_V, 4);
assert.equal(interpret(257, unhex('abcd')).payload_hex, 'abcd');
const badProto = encryptPacket(packet(), keys[0].key, unhex('080112ff'));
assert.match(decodePacket(badProto, keys).status, /no valid decode/);

function status(seqs, uptime_ms = 2000, build_id = '12345678') {
    return {packets: seqs.map(packet), uptime_ms, build_id};
}
const capture = new Capture(3);
assert.equal(capture.ingest(status([2, 1]), 100000).length, 2);
assert.equal(capture.ingest(status([2, 1]), 100500).length, 0);
assert.equal(capture.rows[0].estimated_received_at, new Date(99000).toISOString());
assert.equal(capture.channels.get(8).count, 2);
capture.ingest(status([6, 5]));
assert.equal(capture.missed, 2);
assert.equal(capture.evicted, 1);
assert.deepEqual(capture.rows.map(p => p.sequence), [2, 5, 6]);
capture.clear();
capture.ingest(status([6, 5]));
assert.equal(capture.rows.length, 0); // Clear must not replay the device ring.
capture.ingest(status([1], 1500));
assert.equal(capture.run, 2);
assert.equal(capture.restarts, 1);
assert.equal(capture.rows.length, 1);
capture.ingest(status([1], 1500, 'ffffffff'));
assert.equal(capture.run, 3);
const wrap = new Capture();
wrap.ingest(status([0xffffffff, 0xfffffffe], 0xfffffff0));
wrap.ingest(status([1, 0xffffffff], 100));
assert.equal(wrap.run, 1);
assert.equal(wrap.missed, 1); // Sequence 0 skipped by this fixture, not by capture logic.
assert.equal(wrap.rows.length, 3);
const bounded = new Capture();
for (let n = 1; n <= 1008; n += 8) {
    const newest = Math.min(n + 7, 1001);
    if (newest < n) break;
    bounded.ingest(status(Array.from({length: newest - n + 1}, (_, i) => newest - i), 5000 + newest));
}
assert.equal(bounded.rows.length, 1000);
assert.equal(bounded.rows[0].sequence, 2);
assert.equal(bounded.rows[999].sequence, 1001);
assert.equal(bounded.evicted, 1);
assert.equal(bounded.total, 1001);
assert.equal(bounded.missed, 0);
const activity = new Capture();
const activityData = (session, events) => ({build_id:'firmware:' + session, uptime_ms:5000,
    packets:events.map(sequence => ({sequence, uptime_ms:sequence*10, kind:1, baseline_qdb:-460}))});
activity.ingest(activityData(1, [3,2,1]));
activity.ingest(activityData(1, [3,2,1]));
assert.equal(activity.rows.length, 3);
activity.ingest(activityData(1, [14,13,12,11,10,9,8,7]));
assert.equal(activity.missed, 3);
activity.ingest(activityData(2, [1]));
assert.equal(activity.run, 2);
assert.equal(activity.rows.length, 12);
assert.equal(activity.rows.at(-1).sequence, 1);
console.log('PASS: AES-128/256, Meshtastic CTR, keys, protobuf, capture dedup/gaps/restarts/wrap/eviction');

async function testQueuedSurveyControls() {
    const vm = require('node:vm'), fs = require('node:fs');
    const nodes = new Map(), requests = [];
    const node = () => ({value:'', textContent:'', children:[], dataset:{}, disabled:false,
        replaceChildren() { this.children = []; }, append(...items) { this.children.push(...items); },
        addEventListener() {}, click() {}});
    const element = id => {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
    };
    const config = {frequency_hz:869525000, sf:11, bandwidth_khz:250, profile:'meshtastic'};
    const device = {build_id:'test', uptime_ms:5000, packets:[], config, noise_samples:0, lora_state:1};
    const survey = {build_id:'test', uptime_ms:5000, session:1, running:0, events:[], channels:[], config};
    let releaseStatus, first = true, failCommand = false, exported;
    const reply = (data, ok = true) => ({ok, status:ok ? 200 : 503, json:async () => data});
    const sandbox = {
        document:{getElementById:element, createElement:node},
        TextDecoder, TextEncoder, atob, AbortController, Blob,
        URL:{createObjectURL(blob) { exported = blob; return 'blob:test'; }, revokeObjectURL() {}},
        setTimeout:(fn, ms) => ms === 50 ? setImmediate(fn) : 0,
        clearTimeout() {},
        fetch:async (url, options) => {
            requests.push([url, options?.body]);
            if (url === '/api/status' && first) {
                first = false;
                return new Promise(resolve => { releaseStatus = () => resolve(reply(device)); });
            }
            if (url === '/api/status') return reply(device);
            if (options?.method === 'POST') {
                if (failCommand) return reply({error:'Radio restore failed'}, false);
                survey.running = options.body === 'action=start' ? 1 : 0;
            }
            return reply({...survey});
        }
    };
    vm.runInNewContext(fs.readFileSync(require.resolve('../src/app/dashboard.js'), 'utf8'), sandbox);
    const pending = element('activity-start').onclick();
    assert.equal(requests.length, 1); // A queued click must not race the ongoing status poll.
    assert.equal(element('activity-start').disabled, true);
    assert.match(element('activity-error').textContent, /Waiting/);
    releaseStatus();
    await pending;
    assert.deepEqual(requests.map(r => r[0]), ['/api/status','/api/activity','/api/activity','/api/status']);
    assert.equal(requests[2][1], 'action=start');
    assert.equal(element('activity-start').disabled, true);
    assert.equal(element('activity-stop').disabled, false);
    failCommand = true;
    await element('activity-stop').onclick();
    assert.equal(element('activity-error').textContent, 'Radio restore failed');
    assert.equal(element('activity-stop').disabled, false);
    element('activity-export').onclick();
    const data = JSON.parse(await exported.text());
    assert.equal(data.latest.session, 1);
    assert.equal(data.latest.running, 1);
    assert.equal(data.missed, 0);
    assert.deepEqual(data.events, []);
    assert.equal('keys' in data, false);
    console.log('PASS: queued survey controls serialize requests and surface radio failures');
}
testQueuedSurveyControls().catch(error => { console.error(error); process.exitCode = 1; });
