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

function verifyFiles(prefix, cycles, channel) {
    assert.ok(Number.isInteger(cycles) && cycles >= 0 && cycles <= 5);
    if (channel !== undefined) assert.ok([1,6,11].includes(channel));
    const station = JSON.parse(fs.readFileSync(`${prefix}-station.json`, 'utf8'));
    const ap = fs.readFileSync(`${prefix}-ap.jsonl`, 'utf8').trim().split('\n')
        .map(line => JSON.parse(line)).find(record => record.test === 'b1-ap-result');
    assert.ok(ap, 'missing AP result');
    const rawStation = fs.readFileSync(`${prefix}-station.bin`);
    const rawAp = fs.readFileSync(`${prefix}-ap.bin`);
    assert.ok([2048,2068,2092].includes(rawStation.length), 'unexpected station report length');
    assert.equal(rawStation.readUInt32LE(520), 0x314c5342);
    assert.equal(rawStation.readUInt32LE(544), station.connections);
    assert.equal(rawStation.readUInt32LE(548), station.data_connections);
    const mac = bytes => [...bytes].map(byte => byte.toString(16).padStart(2,'0')).join(':');
    assert.equal(mac(rawStation.subarray(1972,1978)),station.local_mac);
    assert.equal(rawAp.length, 320);
    assert.equal(rawAp.readUInt32LE(152), 0x31504142);
    assert.equal(rawAp.readUInt32LE(168), ap.data_epochs);
    assert.equal(rawAp.subarray(204,210).toString('hex'),ap.session);
    if (channel !== undefined) {
        assert.match(station.build,/^[0-9a-f]{8}$/);
        assert.match(ap.build,/^[0-9a-f]{8}$/);
        assert.equal(rawAp.readUInt32LE(8),Number.parseInt(ap.build,16));
        assert.equal(rawStation.readUInt32LE(8),0);
        assert.equal(rawStation[2000],32);
        assert.equal(rawStation.readUInt32LE(524),station.state);
        assert.equal(rawStation.readUInt32LE(528),station.reason);
        assert.equal(rawStation.readUInt32LE(532),station.epoch);
        assert.equal(rawStation.readUInt32LE(612),station.cleanup);
        assert.equal(rawAp.readUInt32LE(84),5,'not a B3 profile');
        assert.equal(rawAp[30],5); assert.equal(rawAp[31],channel);
        assert.equal(rawAp.readUInt32LE(92),channel,'hardware channel mismatch');
        assert.equal(rawAp.readUInt32LE(96),channel,'target channel mismatch');
        assert.equal(rawAp.readUInt32LE(100),channel===11?1:0,'nmode mismatch');
        assert.equal(rawAp.readUInt32LE(104),0,'open profile has security enabled');
        assert.equal(rawAp.readUInt32LE(180),channel);
        assert.equal(rawAp.readUInt32LE(156),2);
        assert.equal(rawAp.readUInt32LE(160),0);
        assert.equal(rawAp.readUInt32LE(164),cycles);
        assert.equal(rawAp.readUInt32LE(172),ap.received);
        assert.equal(rawAp.readUInt32LE(176),ap.echoed);
        assert.ok(Number.isInteger(ap.data_epochs) && ap.data_epochs>=1 && ap.data_epochs<=16);
        for (let i=0;i<ap.data_epochs;++i)
            assert.equal(rawAp.readUInt32LE(212+4*i),ap.epochs[i]);
        assert.equal(rawStation.readUInt32LE(536),channel);
        assert.equal(station.channel,channel); assert.equal(ap.channel,channel);
        assert.equal(ap.nmode,channel===11?1:0); assert.equal(ap.configuration_verified,true);
        assert.equal(mac(rawAp.subarray(192,198)),ap.bssid);
        assert.equal(mac(rawAp.subarray(198,204)),ap.peer);
        assert.equal(mac(rawStation.subarray(1980,1986)),ap.bssid);
    }
    verify(station,ap,cycles);
    return {test:channel===undefined?'b1-pair':'b3-pair',valid:true,cycles,channel,
        nmode:channel===undefined?undefined:ap.nmode,connections:station.connections,
        echoes:station.echoes,session:station.session,station_build:station.build,
        ap_build:ap.build,bssid:ap.bssid,station_serial:station.serial,ap_serial:ap.serial};
}
module.exports = {verifyFiles};

if (require.main === module && process.argv[2] === '--self-test') {
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
    const dir=fs.mkdtempSync(require('node:path').join(require('node:os').tmpdir(),'b3-oracle-'));
    const prefix=`${dir}/trial`,rawStation=Buffer.alloc(2048),rawAp=Buffer.alloc(320);
    const station={...s,build:'12345678',channel:6,state:5,reason:9};
    const ap={...a,test:'b1-ap-result',build:'87654321',channel:6,nmode:0,configuration_verified:true,
        bssid:'02:06:07:08:09:0a'};
    rawStation.writeUInt32LE(0x314c5342,520);
    rawStation.writeUInt32LE(5,524); rawStation.writeUInt32LE(9,528);
    rawStation.writeUInt32LE(1,532);
    rawStation.writeUInt32LE(1,612); rawStation[2000]=32;
    rawStation.writeUInt32LE(6,536);
    rawStation.writeUInt32LE(1,544); rawStation.writeUInt32LE(1,548);
    Buffer.from(s.local_mac.replaceAll(':',''),'hex').copy(rawStation,1972);
    Buffer.from(ap.bssid.replaceAll(':',''),'hex').copy(rawStation,1980);
    rawAp.writeUInt32LE(0x87654321,8); rawAp[30]=5; rawAp[31]=6;
    rawAp.writeUInt32LE(5,84); rawAp.writeUInt32LE(6,92); rawAp.writeUInt32LE(6,96);
    rawAp.writeUInt32LE(0x31504142,152); rawAp.writeUInt32LE(1,168);
    rawAp.writeUInt32LE(2,156); rawAp.writeUInt32LE(2,172); rawAp.writeUInt32LE(2,176);
    rawAp.writeUInt32LE(1,212);
    rawAp.writeUInt32LE(6,180);
    Buffer.from(ap.bssid.replaceAll(':',''),'hex').copy(rawAp,192);
    Buffer.from(s.local_mac.replaceAll(':',''),'hex').copy(rawAp,198);
    Buffer.from(s.session,'hex').copy(rawAp,204);
    try {
        fs.writeFileSync(`${prefix}-station.json`,JSON.stringify(station));
        fs.writeFileSync(`${prefix}-ap.jsonl`,JSON.stringify(ap));
        fs.writeFileSync(`${prefix}-station.bin`,rawStation);
        fs.writeFileSync(`${prefix}-ap.bin`,rawAp);
        assert.equal(verifyFiles(prefix,0,6).test,'b3-pair');
        const extended=Buffer.concat([rawStation,Buffer.alloc(20)]);
        extended.writeUInt32LE(0x31504e53,2048); extended.writeUInt32LE(3,2052);
        fs.writeFileSync(`${prefix}-station.bin`,extended);
        assert.equal(verifyFiles(prefix,0,6).test,'b3-pair');
        fs.writeFileSync(`${prefix}-station.bin`,extended.subarray(0,2067));
        assert.throws(()=>verifyFiles(prefix,0,6));
        fs.writeFileSync(`${prefix}-station.bin`,rawStation);
        for (const offset of [31,84,92,96,100,104,156,160,164,172,176,180,192,198,212]) {
            rawAp[offset]^=1; fs.writeFileSync(`${prefix}-ap.bin`,rawAp);
            assert.throws(()=>verifyFiles(prefix,0,6));
            rawAp[offset]^=1;
        }
    } finally {
        for (const suffix of ['station.json','ap.jsonl','station.bin','ap.bin'])
            fs.unlinkSync(`${prefix}-${suffix}`);
        fs.rmdirSync(dir);
    }
    console.log('B1 independent evidence oracle: positive and stale/mismatch/loss negatives pass');
} else if (require.main === module) {
    assert.ok(process.argv.length===4 || process.argv.length===5,
        'Usage: node lifecycle_verify.cjs PREFIX CYCLES [B3_CHANNEL]');
    const prefix = process.argv[2], cycles = Number(process.argv[3]);
    console.log(JSON.stringify(verifyFiles(prefix,cycles,
        process.argv.length===5?Number(process.argv[4]):undefined)));
}
