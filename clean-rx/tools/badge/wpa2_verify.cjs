const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {pbkdf2Sync} = require('node:crypto');

const stationFields = {error:8, channel:28, wpa_stage:1644, wpa_error:1648,
    eapol_m1_count:1652, m2_sent:1656, m3_validated:1660, m4_sent:1664,
    protected_echoes:1668, handshake_retries:1676};
const apFields = {state:156, error:160, received:168, echoed:172, channel:176, wpa_auth:180,
    operation:212};
const mac = (buffer,offset) => [...buffer.subarray(offset,offset+6)]
    .map(byte=>byte.toString(16).padStart(2,'0')).join(':');

function validate(s,a,rawS,rawA,timing,channel,mode,stationSerial,apSerial) {
    assert.ok([1,6,11].includes(channel));
    assert.ok(['passphrase','raw-psk','wrong-password'].includes(mode));
    assert.match(stationSerial,/^[0-9A-F]{16}$/); assert.match(apSerial,/^[0-9A-F]{16}$/);
    assert.notEqual(stationSerial,apSerial);
    assert.equal(s.serial,stationSerial); assert.equal(a.serial,apSerial);
    assert.match(s.build,/^[0-9a-f]{8}$/); assert.match(a.build,/^[0-9a-f]{8}$/);
    assert.equal(a.test,'e1-ap-result');
    assert.equal(rawS.length,2048); assert.equal(rawA.length,320);
    for (const [key,offset] of Object.entries(stationFields))
        assert.equal(rawS.readUInt32LE(offset),s[key],`station raw ${key}`);
    for (const [key,offset] of Object.entries(apFields))
        assert.equal(rawA.readUInt32LE(offset),a[key],`AP raw ${key}`);
    assert.equal(rawS[2000],18);
    assert.equal(rawS.readUInt32LE(1640),0x32575043);
    assert.equal(rawA.readUInt32LE(152),0x31415045);
    assert.equal(rawA.readUInt32LE(84),6); assert.equal(rawA[30],6); assert.equal(rawA[31],channel);
    assert.equal(rawA.readUInt32LE(8).toString(16).padStart(8,'0'),a.build);
    assert.equal(rawA.readUInt32LE(92),channel); assert.equal(rawA.readUInt32LE(96),channel);
    assert.equal(rawA.readUInt32LE(100),channel===11?1:0);
    assert.equal(a.nmode,channel===11?1:0);
    assert.equal(rawA.readUInt32LE(104),4); assert.equal(a.wsec,4);
    assert.equal(a.wpa_auth,128); assert.equal(a.configuration_verified,true);
    assert.equal(a.state,2,'AP did not finish'); assert.equal(a.error,0,'AP radio/setup failed');
    assert.equal(a.operation,9,'AP fixture did not finish cleanly');
    assert.equal(s.error,0,`station transport failed: stage=${s.stage}, error=${s.error}`);
    assert.equal(a.channel,channel); assert.equal(s.channel,channel);
    assert.equal(rawA.readUInt32LE(164),Number(a.authorized));
    assert.equal(mac(rawS,1972),s.local_mac); assert.equal(mac(rawA,190),s.local_mac);
    assert.equal(a.peer,s.local_mac);
    assert.equal(mac(rawS,1980),a.bssid); assert.equal(mac(rawA,184),a.bssid);
    assert.notEqual(a.bssid,s.local_mac);
    assert.ok(s.eapol_m1_count>0 && s.m2_sent>0,
        `no M1/M2: auth=${s.authentication_status}, assoc=${s.association_status}, WPA=${s.wpa_error}`);
    const wrong=mode==='wrong-password';
    if (wrong) {
        assert.equal(s.wpa2_authorized,false); assert.equal(a.authorized,false);
        assert.equal(a.valid,false); assert.equal(s.protected_echoes,0);
        assert.equal(s.m3_validated,0); assert.equal(s.m4_sent,0);
        assert.ok([3,4,9,11].includes(s.wpa_error),'wrong-password rejection must be nonce/MIC/timeout/departure');
        assert.equal(a.received,0); assert.equal(a.echoed,0);
    } else {
        assert.equal(s.associated,true,'station association not independently confirmed');
        assert.equal(s.wpa2_authorized,true,`station protected proof missing: WPA stage=${s.wpa_stage}, error=${s.wpa_error}`);
        assert.equal(a.authorized,true,'AP did not retain authorization'); assert.equal(a.valid,true);
        assert.equal(s.wpa_error,0); assert.equal(s.wpa_stage,4);
        assert.ok(s.m3_validated>0 && s.m4_sent>0 && s.protected_echoes>0);
        assert.ok(a.received>0); assert.equal(a.received,a.echoed);
    }
    for (const endpoint of ['station','ap']) {
        const match=timing.match(new RegExp(`^${endpoint}_exit=(\\d+)$`,'m'));
        assert.ok(match,`${endpoint} exit missing`); assert.equal(Number(match[1]),wrong?1:0);
    }
    return {test:'e1-pair',valid:true,channel,mode,station:stationSerial,ap:apSerial,
        station_build:s.build,ap_build:a.build,session:rawA.subarray(16,32).toString('hex'),
        m2:s.m2_sent,m3:s.m3_validated,m4:s.m4_sent,protected_echoes:s.protected_echoes,
        handshake_retries:s.handshake_retries};
}

function verify(prefix,channel,mode,stationSerial,apSerial) {
    const s=JSON.parse(fs.readFileSync(`${prefix}-station.json`,'utf8'));
    const rows=fs.readFileSync(`${prefix}-ap.jsonl`,'utf8').trim().split('\n').map(line=>JSON.parse(line));
    const results=rows.filter(row=>row.test==='e1-ap-result');
    assert.equal(results.length,1);
    return validate(s,results[0],fs.readFileSync(`${prefix}-station.bin`),
        fs.readFileSync(`${prefix}-ap.bin`),fs.readFileSync(`${prefix}-timing.log`,'utf8'),
        channel,mode,stationSerial,apSerial);
}

function trialSpec(i) {
    return {channel:i<=10?1:i<=13?6:i<=16?11:[1,6,11][i-17],
        mode:i>16?'wrong-password':i%2?'passphrase':'raw-psk'};
}

function verifyMatrix(prefix,stationSerial,apSerial) {
    const rows=[], sessions=new Set();
    let stationBuild, apBuild;
    for (let i=1;i<=19;++i) {
        const {channel,mode}=trialSpec(i);
        try {
            const row=verify(`${prefix}-${i}`,channel,mode,stationSerial,apSerial);
            if (stationBuild===undefined) { stationBuild=row.station_build; apBuild=row.ap_build; }
            assert.equal(row.station_build,stationBuild,'station build changed during matrix');
            assert.equal(row.ap_build,apBuild,'AP build changed during matrix');
            assert.ok(!sessions.has(row.session),'reused AP session');
            sessions.add(row.session); rows.push({trial:i,...row});
        } catch (error) {
            if (!(error instanceof assert.AssertionError || error instanceof SyntaxError ||
                error.code==='ENOENT')) throw error;
            rows.push({trial:i,valid:false,error:error.message});
        }
    }
    return {test:'e1-matrix',valid:rows.every(row=>row.valid),required_connections:16,
        required_consecutive_controlled_connections:10,
        verified_connections:rows.slice(0,16).filter(row=>row.valid).length,
        profiles:[1,6,11],credential_modes:['passphrase','raw-psk'],required_wrong_password_controls:3,
        verified_wrong_password_controls:rows.slice(16).filter(row=>row.valid).length,
        ap_implementations:1,rows};
}

function selfTest(collector) {
    const s={serial:'0000000000000001',build:'12345678',error:0,channel:1,wpa_stage:4,
        wpa_error:0,eapol_m1_count:1,m2_sent:1,m3_validated:1,m4_sent:1,
        protected_echoes:1,handshake_retries:0,local_mac:'02:06:07:08:09:0a',
        associated:true,wpa2_authorized:true};
    const a={test:'e1-ap-result',serial:'0000000000000002',build:'87654321',valid:true,
        configuration_verified:true,nmode:0,wsec:4,wpa_auth:128,state:2,error:0,
        channel:1,authorized:true,received:1,echoed:1,bssid:'02:01:02:03:04:05',peer:s.local_mac,
        operation:9};
    const rawS=Buffer.alloc(2048), rawA=Buffer.alloc(320), timing='station_exit=0\nap_exit=0\n';
    for (const [key,offset] of Object.entries(stationFields)) rawS.writeUInt32LE(s[key],offset);
    for (const [key,offset] of Object.entries(apFields)) rawA.writeUInt32LE(a[key],offset);
    rawS[2000]=18; rawS.writeUInt32LE(0x32575043,1640);
    for (const [offset,value] of [[8,0x87654321],[84,6],[92,1],[96,1],[104,4],
        [152,0x31415045],[164,1]]) rawA.writeUInt32LE(value,offset);
    rawA[30]=6; rawA[31]=1;
    Buffer.from(s.local_mac.replaceAll(':',''),'hex').copy(rawS,1972);
    Buffer.from(s.local_mac.replaceAll(':',''),'hex').copy(rawA,190);
    Buffer.from(a.bssid.replaceAll(':',''),'hex').copy(rawS,1980);
    Buffer.from(a.bssid.replaceAll(':',''),'hex').copy(rawA,184);
    const run=()=>validate(s,a,rawS,rawA,timing,1,'passphrase',s.serial,a.serial);
    assert.equal(run().valid,true);
    for (const offset of [8,30,31,84,92,96,100,104,152,156,160,164,168,172,176,180,184,190,212]) {
        rawA[offset]^=1; assert.throws(run,assert.AssertionError); rawA[offset]^=1;
    }
    for (const offset of [8,28,1640,1644,1648,1652,1656,1660,1664,1668,1676,1972,1980,2000]) {
        rawS[offset]^=1; assert.throws(run,assert.AssertionError); rawS[offset]^=1;
    }
    assert.throws(()=>validate(s,a,rawS,rawA,timing,1,'wrong-password',s.serial,a.serial),assert.AssertionError);
    const matrixDir=fs.mkdtempSync(path.join(os.tmpdir(),'clean-e1-matrix-'));
    const files=[], prefix=path.join(matrixDir,'trial');
    try {
        for (let i=1;i<=19;++i) {
            const {channel:ch,mode}=trialSpec(i), wrong=mode==='wrong-password';
            const station={...s,channel:ch}, ap={...a,channel:ch,nmode:ch===11?1:0};
            const staRaw=Buffer.from(rawS), apRaw=Buffer.from(rawA);
            if (wrong) {
                Object.assign(station,{wpa_stage:1,wpa_error:i===17?3:i===18?4:9,m3_validated:0,m4_sent:0,
                    protected_echoes:0,wpa2_authorized:false});
                Object.assign(ap,{authorized:false,valid:false,received:0,echoed:0});
            }
            for (const [key,offset] of Object.entries(stationFields)) staRaw.writeUInt32LE(station[key],offset);
            for (const [key,offset] of Object.entries(apFields)) apRaw.writeUInt32LE(ap[key],offset);
            apRaw[16]=i; apRaw[31]=ch;
            apRaw.writeUInt32LE(ch,92); apRaw.writeUInt32LE(ch,96);
            apRaw.writeUInt32LE(ap.nmode,100); apRaw.writeUInt32LE(Number(ap.authorized),164);
            const content={'station.json':JSON.stringify(station),'ap.jsonl':JSON.stringify(ap),
                'station.bin':staRaw,'ap.bin':apRaw,'timing.log':`station_exit=${wrong?1:0}\nap_exit=${wrong?1:0}\n`};
            for (const [suffix,data] of Object.entries(content)) {
                const file=`${prefix}-${i}-${suffix}`;
                fs.writeFileSync(file,data); files.push(file);
            }
        }
        const matrix=()=>verifyMatrix(prefix,s.serial,a.serial);
        assert.equal(matrix().valid,true);
        assert.equal(matrix().verified_connections,16);
        assert.equal(matrix().verified_wrong_password_controls,3);
        assert.equal(verifyMatrix(prefix,a.serial,s.serial).valid,false);
        const fourth=`${prefix}-4-ap.bin`, saved=fs.readFileSync(fourth);
        const reused=Buffer.from(saved); fs.readFileSync(`${prefix}-1-ap.bin`).copy(reused,16,16,32);
        fs.writeFileSync(fourth,reused); assert.equal(matrix().valid,false);
        fs.writeFileSync(fourth,saved);
        const third=`${prefix}-3-station.json`, savedThird=fs.readFileSync(third);
        fs.writeFileSync(third,JSON.stringify({...JSON.parse(savedThird),build:'12345679'}));
        assert.equal(matrix().valid,false); fs.writeFileSync(third,savedThird);
        fs.unlinkSync(`${prefix}-19-station.json`);
        assert.equal(matrix().valid,false);
    } finally {
        for (const file of files) if (fs.existsSync(file)) fs.unlinkSync(file);
        fs.rmdirSync(matrixDir);
    }
    if (collector) {
        const dir=fs.mkdtempSync(path.join(os.tmpdir(),'clean-e1-'));
        const input=path.join(dir,'input'), output=path.join(dir,'output');
        try {
            const known='f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e';
            const vectors=['password',known,known.toUpperCase()]
                .map(credential=>({ssid:'IEEE',credential,expected:known}));
            vectors.push({ssid:'s'.repeat(32),credential:'p'.repeat(63),
                expected:pbkdf2Sync('p'.repeat(63),'s'.repeat(32),4096,32,'sha1').toString('hex')});
            vectors.push({ssid:'s'.repeat(32),credential:known,expected:known});
            for (const {ssid,credential,expected} of vectors) {
                fs.writeFileSync(input,`${ssid}\n${credential}\n`,{mode:0o600});
                const result=spawnSync(collector,['--derive-psk',input,output],{encoding:'utf8'});
                assert.equal(result.status,0,result.stderr); assert.equal(result.stdout,'');
                assert.equal(fs.statSync(output).mode&0o777,0o600);
                assert.equal(fs.readFileSync(output,'utf8'),`${ssid}\n${expected}\n`);
                assert.equal(spawnSync(collector,['--derive-psk',input,output]).status,1);
                fs.unlinkSync(output);
            }
            for (const text of ['IEEE\nshort\n',`IEEE\n${'g'.repeat(64)}\n`,
                `IEEE\n${'a'.repeat(65)}\n`,'IEEE\npassword','IEEE\npassword\nextra\n',
                'IEEE\npassword\n\n',`IEEE\npass\0word\n`,
                `${'s'.repeat(33)}\npassword\n`,'\npassword\n']) {
                fs.writeFileSync(input,text);
                assert.equal(spawnSync(collector,['--derive-psk',input,output]).status,1);
                assert.equal(fs.existsSync(output),false);
            }
            fs.writeFileSync(input,'IEEE\npassword\n'); fs.chmodSync(input,0o644);
            assert.equal(spawnSync(collector,['--derive-psk',input,output]).status,1);
            fs.chmodSync(input,0o600);
            fs.symlinkSync(input,output);
            assert.equal(spawnSync(collector,['--derive-psk',input,output]).status,1);
        } finally {
            for (const file of [input,output])
                if (fs.lstatSync(file,{throwIfNoEntry:false})) fs.unlinkSync(file);
            fs.rmdirSync(dir);
        }
    }
    console.log('E1 oracle: raw identity/configuration/counter tampering rejected; protected proof cannot pass wrong-password control; private credential parsing/derivation pass');
}

module.exports={verify};
if (require.main===module) {
    if (process.argv[2]==='--self-test') selfTest(process.argv[3]);
    else if (process.argv[2]==='--matrix') {
        assert.equal(process.argv.length,6);
        const result=verifyMatrix(process.argv[3],process.argv[4],process.argv[5]);
        console.log(JSON.stringify(result)); if (!result.valid) process.exitCode=1;
    }
    else {
        try {
            assert.equal(process.argv.length,7);
            console.log(JSON.stringify(verify(process.argv[2],Number(process.argv[3]),
                process.argv[4],process.argv[5],process.argv[6])));
        } catch (error) {
            if (!(error instanceof assert.AssertionError || error instanceof SyntaxError ||
                error.code==='ENOENT')) throw error;
            console.error(`E1 evidence rejected: ${error.message}`);
            console.log(JSON.stringify({test:'e1-pair',valid:false,error:error.message}));
            process.exitCode=1;
        }
    }
}
