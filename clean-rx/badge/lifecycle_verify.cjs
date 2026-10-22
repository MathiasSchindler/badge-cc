'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');

function verify(station, ap, cycles) {
    assert.equal(station.valid, true, 'station lifecycle failed');
    assert.equal(ap.valid, true, 'independent AP failed');
    assert.equal(station.error, 0);
    assert.equal(ap.error, 0);
    assert.equal(station.cleanup, 1);
    assert.equal(station.connections, cycles + 1, 'unexpected association count');
    assert.equal(station.data_connections, station.connections, 'unverified connection');
    assert.equal(station.auth_failures, 0, 'authentication failure retained');
    assert.equal(station.assoc_failures, 0, 'association failure retained');
    assert.equal(station.disconnects, cycles, 'unexplained disconnect');
    assert.equal(ap.cycles, cycles);
    assert.equal(ap.data_epochs, station.data_connections);
    assert.equal(ap.peer, station.local_mac, 'independent receiver saw a different station');
    assert.equal(ap.session, station.session, 'stale application session');
    assert.equal(new Set(ap.epochs).size, cycles + 1, 'reused connection epoch');
    assert.equal(Math.max(...ap.epochs), station.epoch);
    assert.ok(station.echoes >= station.connections);
    assert.ok(ap.received >= station.echoes && ap.echoed === ap.received);
}

if (process.argv[2] === '--self-test') {
    const s = {valid:true,error:0,cleanup:1,connections:1,data_connections:1,
        auth_failures:0,assoc_failures:0,disconnects:0,local_mac:'02:01:02:03:04:05',
        session:'010203040506',epoch:1,echoes:2};
    const a = {valid:true,error:0,cycles:0,data_epochs:1,peer:s.local_mac,
        session:s.session,epochs:[1],received:2,echoed:2};
    verify(s, a, 0);
    const recoveredStation = {...s,connections:6,data_connections:6,disconnects:5,epoch:6,echoes:6};
    const recoveredAp = {...a,cycles:5,data_epochs:6,epochs:[1,2,3,4,5,6],received:6,echoed:6};
    verify(recoveredStation, recoveredAp, 5);
    assert.throws(() => verify(recoveredStation, {...recoveredAp,epochs:[1,2,3,4,5,5]}, 5));
    for (const [key, value] of [['session','stale'],['peer','other'],['epochs',[2]],
        ['echoed',0],['valid',false],['cycles',1]])
        assert.throws(() => verify(s, {...a,[key]:value}, 0));
    assert.throws(() => verify({...s,data_connections:0}, a, 0));
    assert.throws(() => verify({...s,auth_failures:1}, a, 0));
    console.log('B1 independent evidence oracle: positive and stale/mismatch/loss negatives pass');
} else {
    assert.equal(process.argv.length, 4, 'Usage: node lifecycle_verify.cjs PREFIX CYCLES');
    const prefix = process.argv[2], cycles = Number(process.argv[3]);
    assert.ok(Number.isInteger(cycles) && cycles >= 0 && cycles <= 5);
    const station = JSON.parse(fs.readFileSync(`${prefix}-station.json`, 'utf8'));
    const ap = fs.readFileSync(`${prefix}-ap.jsonl`, 'utf8').trim().split('\n')
        .map(line => JSON.parse(line)).find(record => record.test === 'b1-ap-result');
    const rawStation = fs.readFileSync(`${prefix}-station.bin`);
    const rawAp = fs.readFileSync(`${prefix}-ap.bin`);
    assert.equal(rawStation.length, 2048);
    assert.equal(rawStation.readUInt32LE(520), 0x314c5342);
    assert.equal(rawStation.readUInt32LE(544), station.connections);
    assert.equal(rawStation.readUInt32LE(548), station.data_connections);
    assert.equal([...rawStation.subarray(1972, 1978)]
        .map(byte => byte.toString(16).padStart(2, '0')).join(':'), station.local_mac);
    assert.equal(rawAp.length, 320);
    assert.equal(rawAp.readUInt32LE(152), 0x31504142);
    assert.equal(rawAp.readUInt32LE(168), ap.data_epochs);
    assert.equal(rawAp.subarray(204, 210).toString('hex'), ap.session);
    verify(station, ap, cycles);
    console.log(JSON.stringify({test:'b1-pair',valid:true,cycles,
        connections:station.connections,echoes:station.echoes,session:station.session}));
}
