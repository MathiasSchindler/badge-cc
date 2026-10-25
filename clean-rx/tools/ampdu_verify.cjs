'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

function crc(bytes) {
    let value=0xffffffff;
    for (const byte of bytes) {
        value^=byte;
        for (let bit=0;bit<8;++bit) value=(value>>>1)^((value&1)?0xedb88320:0);
    }
    return (value^0xffffffff)>>>0;
}
function mpdu(nonce,sequence,bad) {
    const m=Buffer.alloc(80);
    m[0]=0x88; m[1]=2; m[4]=m[10]=m[16]=2;
    for (let i=0;i<5;++i) m[5+i]=m[11+i]=m[17+i]=nonce[i];
    m[15]^=0x80; m[21]^=0x80; m[22]=sequence<<4; m[24]=0x60;
    nonce.copy(m,26); m.write('AMPDU-D2',40);
    for (let i=48;i<76;++i) m[i]=nonce[i%14]^i^sequence;
    m.writeUInt32LE(crc(m.subarray(0,76)),76);
    if (bad) m[79]^=0x80;
    return m;
}
function bound(raw,config,mode) {
    assert.equal(raw.length,2092,'missing complete R03 raw report');
    assert.equal(raw.readUInt32LE(0),0x31585243);
    assert.equal(raw.readUInt32LE(4),8,'operation did not finish');
    assert.equal(raw.readUInt32LE(8),0,'operation failed');
    assert.equal(raw.readUInt32LE(32),0x4f505258);
    assert.equal(raw.readUInt32LE(36),10,'controller not stopped');
    assert.equal(raw.readUInt32LE(2048),0x31504e53);
    assert.equal(raw.readUInt32LE(2052),1,'snapshot not complete');
    assert.equal(raw.readUInt32LE(2056),0,'snapshot failed');
    assert.equal(raw.readUInt32LE(2068)&63,63,'missing snapshot sections/freeze');
    assert.equal(raw.subarray(2072,2086).toString('hex'),config.challenge,'stale challenge');
    assert.equal(raw[2086],mode); assert.equal(raw[2087],config.channel);
    assert.equal(raw[2000],mode); assert.equal(raw[2001],config.channel);
    assert.ok(raw.readUInt32LE(2088)>0,'missing controller generation');
    assert.equal(raw.readUInt32LE(2088),(Buffer.from(config.challenge,'hex').readUInt32LE(0)|1)>>>0,
        'expected generation differs from controller boot request');
    assert.equal(raw.readUInt32LE(64),raw.readUInt32LE(2088),'stale controller generation');
}
function verify(config,tx,rx,rawTx,rawRx) {
    assert.match(config.challenge,/^[0-9a-f]{28}$/);
    assert.ok(Number.isInteger(config.channel) && config.channel>=1 && config.channel<=11);
    const nonce=Buffer.from(config.challenge,'hex');
    assert.equal(nonce[13],config.mcs);
    bound(rawTx,config,30); bound(rawRx,config,31);
    for (const record of [tx,rx]) {
        assert.equal(record.error,0); assert.equal(record.channel,config.channel);
        assert.equal(record.mcs,config.mcs);
        assert.equal(record.snapshot?.valid,true,'unavailable snapshot');
    }
    assert.equal(tx.local_tx_submission,true);
    assert.equal(rawTx.readUInt32LE(1092),1); assert.equal(rawTx.readUInt32LE(1096),1);
    assert.equal(rx.capture_available,true); assert.equal(rx.lease_available,true);
    assert.equal(rawRx.readUInt32LE(768),0x32504441,'missing capture');
    assert.equal(rawRx.readUInt32LE(1104),0x32464941,'missing lease');
    assert.equal(rawRx.readUInt16LE(1264),0,
        'repeated/unknown handoff requires a multi-PPDU oracle, not finite-fixture acceptance');
    assert.equal(rx.interface?.valid,true);
    assert.equal(rawRx.readUInt16LE(1108),3);
    assert.ok(rawRx.readUInt16LE(1110)>=1 && rawRx.readUInt16LE(1110)<=3);
    const bad=tx.fixture_bad_fcs_mask;
    assert.ok(Number.isInteger(bad) && bad>=0 && bad<=7);
    assert.equal(rx.fixture_bad_fcs_mask,bad);
    const expected=7&~bad, count=[0,1,2].filter(i=>expected&(1<<i)).length;
    assert.equal(rx.observed_fixture_bitmap,7,'missing fresh bad/good fixture bodies');
    assert.equal(rawRx.readUInt16LE(772),3);
    for (let i=0;i<3;++i) {
        assert.deepEqual(rawRx.subarray(796+88*i,876+88*i),mpdu(nonce,i,bad&(1<<i)),
            'stale/corrupt retained fixture');
        if (expected&(1<<i))
            assert.deepEqual(rawRx.subarray(1664+80*i,1744+80*i),mpdu(nonce,i,false),
                'delivered bytes/FCS differ from independent fixture');
    }
    assert.equal(rx.delivery_payloads_valid,true);
    assert.equal(rawRx.readUInt32LE(704),0x314d4441);
    for (const [field,offset,value] of [['delivered_subframe_bitmap',708,expected],
        ['reported_delivered_subframe_bitmap',708,expected],['delivered_subframes',712,count],
        ['delivered_duplicates',716,0]]) {
        assert.equal(rx[field],value,field);
        assert.equal(rawRx.readUInt32LE(offset),value,`raw ${field}`);
    }
    assert.equal(rx.retained_subframe_bitmap,expected);
    if (rx.shm_delivery_enabled) {
        assert.equal(rx.shm_terminal?.valid,true);
        for (const [field,offset,value] of [['published_count',1248,1],['released_count',1250,1],
            ['claim_state',1252,16],['claimed_count',1254,1]]) {
            assert.equal(rx.shm_terminal[field],value);
            assert.equal(rawRx.readUInt16LE(offset),value);
        }
    }
    assert.equal(typeof rx.ba_experiment?.enabled,'boolean');
    if (rx.ba_experiment.enabled) {
        assert.equal(rx.ba_experiment.available,true);
        for (const [field,offset,value] of [['bitmap',1270,expected],['attempts',1272,expected?1:0],
            ['started',1274,expected?1:0],['errors',1276,0]]) {
            assert.equal(rx.ba_experiment[field],value);
            assert.equal(rawRx.readUInt16LE(offset),value);
        }
        assert.equal(tx.ba_experiment.returned_ba,!!expected);
        assert.equal(tx.ba_experiment.returned_bitmap,expected);
        assert.equal(rawTx.readUInt32LE(1480),0x31524241,'unavailable BA receipt record');
        assert.equal(rawTx.readUInt32LE(1484),expected?1:0,'BA receipt count');
        if (expected) {
            const ba=rawTx.subarray(1488,1520);
            assert.equal(ba[0],0x94); assert.equal(ba[20],expected);
            assert.deepEqual(ba.subarray(1,4),Buffer.alloc(3));
            assert.equal(ba[16],4); assert.deepEqual(ba.subarray(17,20),Buffer.alloc(3));
            assert.deepEqual(ba.subarray(21,28),Buffer.alloc(7));
            assert.equal(ba.readUInt32LE(28),crc(ba.subarray(0,28)));
            for (let i=0;i<6;++i) {
                const own=i?nonce[i-1]:2;
                assert.equal(ba[4+i],own^(i===5?0x80:0)); assert.equal(ba[10+i],own);
            }
        }
    }
    return {mode:'ampdu-probe-pair',valid:true,channel:config.channel,mcs:config.mcs,
        fixture_bad_fcs_mask:bad,delivered_subframe_bitmap:expected,delivered_subframes:count,
        zero_delivery_control:expected===0,shm_terminal:rx.shm_terminal,
        ba_experiment:rx.ba_experiment,compressed_ba_return:tx.ba_experiment,
        all_subframes_delivered:expected===7,immediate_ba:false,sifs_timing_qualified:false,
        ht_qualified:false};
}

function selfTest(collector) {
    const config={challenge:'0102030405060708090a0b500000',channel:6,mcs:0};
    const nonce=Buffer.from(config.challenge,'hex');
    const raw=mode=>{
        const b=Buffer.alloc(2092);
        for (const [offset,value] of [[0,0x31585243],[4,8],[32,0x4f505258],[36,10],[64,1],
            [2048,0x31504e53],[2052,1],[2068,63],[2088,1]]) b.writeUInt32LE(value,offset);
        b.writeUInt32LE((nonce.readUInt32LE(0)|1)>>>0,64);
        b.writeUInt32LE((nonce.readUInt32LE(0)|1)>>>0,2088);
        nonce.copy(b,2072); b[2000]=b[2086]=mode; b[2001]=b[2087]=6;
        return b;
    };
    const txRaw=raw(30), rxRaw=raw(31);
    txRaw.writeUInt32LE(1,1092); txRaw.writeUInt32LE(1,1096);
    rxRaw.writeUInt32LE(0x32504441,768); rxRaw.writeUInt16LE(3,772);
    rxRaw.writeUInt32LE(0x32464941,1104); rxRaw.writeUInt16LE(3,1108);
    rxRaw.writeUInt16LE(1,1110);
    rxRaw.writeUInt32LE(0x314d4441,704);
    for (const [offset,value] of [[1248,1],[1250,1],[1252,16],[1254,1]])
        rxRaw.writeUInt16LE(value,offset);
    for (const bad of [0,2,7]) {
        const mask=7&~bad, count=[0,1,2].filter(i=>mask&(1<<i)).length;
        for (let i=0;i<3;++i) {
            mpdu(nonce,i,bad&(1<<i)).copy(rxRaw,796+88*i);
            mpdu(nonce,i,false).copy(rxRaw,1664+80*i);
        }
        rxRaw.writeUInt32LE(mask,708); rxRaw.writeUInt32LE(count,712);
        const tx={channel:6,mcs:0,error:0,snapshot:{valid:true},local_tx_submission:true,
            fixture_bad_fcs_mask:bad};
        const rx={...tx,capture_available:true,lease_available:true,interface:{valid:true},
            delivery_payloads_valid:true,observed_fixture_bitmap:7,fixture_bad_fcs_mask:bad,
            delivered_subframe_bitmap:mask,reported_delivered_subframe_bitmap:mask,
            delivered_subframes:count,delivered_duplicates:0,retained_subframe_bitmap:mask,
            shm_delivery_enabled:true,shm_terminal:{valid:true,published_count:1,released_count:1,
                claim_state:16,claimed_count:1},ba_experiment:{enabled:false}};
        assert.equal(verify(config,tx,rx,txRaw,rxRaw).valid,true);
        rx.ba_experiment={enabled:true,available:true,bitmap:mask,attempts:mask?1:0,
            started:mask?1:0,errors:0};
        tx.ba_experiment={enabled:true,returned_ba:!!mask,returned_bitmap:mask};
        for (const [offset,value] of [[1270,mask],[1272,mask?1:0],[1274,mask?1:0],[1276,0]])
            rxRaw.writeUInt16LE(value,offset);
        txRaw.fill(0,1480,1520);
        txRaw.writeUInt32LE(0x31524241,1480);
        txRaw.writeUInt32LE(mask?1:0,1484);
        if (mask) {
            const ba=txRaw.subarray(1488,1520);
            ba[0]=0x94; ba[16]=4; ba[20]=mask;
            for (let i=0;i<6;++i) {
                const own=i?nonce[i-1]:2;
                ba[4+i]=own^(i===5?0x80:0); ba[10+i]=own;
            }
            ba.writeUInt32LE(crc(ba.subarray(0,28)),28);
        }
        assert.equal(verify(config,tx,rx,txRaw,rxRaw).valid,true);
        for (const offset of [1270,1272,1274,1276]) {
            rxRaw[offset]^=1; assert.throws(()=>verify(config,tx,rx,txRaw,rxRaw)); rxRaw[offset]^=1;
        }
        txRaw[1480]^=1; assert.throws(()=>verify(config,tx,rx,txRaw,rxRaw)); txRaw[1480]^=1;
        txRaw[1484]^=1; assert.throws(()=>verify(config,tx,rx,txRaw,rxRaw)); txRaw[1484]^=1;
        for (const field of ['delivered_subframe_bitmap','delivered_subframes','delivered_duplicates',
            'capture_available','lease_available','delivery_payloads_valid','observed_fixture_bitmap'])
            assert.throws(()=>verify(config,tx,{...rx,[field]:null},txRaw,rxRaw));
        for (const offset of [4,8,36,64,708,712,716,768,796,1104,1248,1250,1252,1254,1264,1265,
            2048,2052,2056,2068,2072,2088]) {
            rxRaw[offset]^=1; assert.throws(()=>verify(config,tx,rx,txRaw,rxRaw)); rxRaw[offset]^=1;
        }
        assert.throws(()=>verify(config,tx,rx,txRaw,rxRaw.subarray(0,2068)));
        assert.throws(()=>verify(config,tx,{...rx,shm_terminal:{...rx.shm_terminal,valid:false}},txRaw,rxRaw));
    }
    if (collector) {
        const dir=fs.mkdtempSync(path.join(os.tmpdir(),'ampdu-validity-')), file=path.join(dir,'raw.bin');
        try {
            fs.writeFileSync(file,txRaw);
            const zeroReceipt=spawnSync(collector,['--inspect-ampdu',file,config.challenge],{encoding:'utf8'});
            assert.equal(zeroReceipt.status,0,zeroReceipt.stderr);
            const zeroRecord=JSON.parse(zeroReceipt.stdout);
            assert.equal(zeroRecord.reported_delivered_subframe_bitmap,null);
            const repeated=Buffer.from(rxRaw);
            repeated.writeUInt16LE(0x5250,1264);
            fs.writeFileSync(file,repeated);
            const repeatReport=spawnSync(collector,['--inspect-ampdu',file,config.challenge],{encoding:'utf8'});
            assert.equal(repeatReport.status,1,repeatReport.stderr);
            assert.equal(JSON.parse(repeatReport.stdout).finite_fixture_qualified,false);
            assert.match(repeatReport.stderr,/multi-PPDU/);
            if (zeroRecord.dma_experiment_enabled) {
                const words=[0x10002,0x10000030,0x40,0,0x314d4441,3968,0x35000,0,
                    0x10003,0x560002,0xa411922,0x8000002];
                words.forEach((word,i)=>rxRaw.writeUInt32LE(word,720+4*i));
                rxRaw.fill(0x5a,1480,1608);
                rxRaw.writeUInt32LE(rxRaw.readUInt32LE(64),1608);
                rxRaw.writeUInt32LE(0x8e,44);
                for (const stale of [false,true]) {
                    if (stale) rxRaw[1608]^=1;
                    fs.writeFileSync(file,rxRaw);
                    const report=spawnSync(collector,['--inspect-ampdu',file,config.challenge],{encoding:'utf8'});
                    const record=JSON.parse(report.stdout);
                    if (stale) {
                        assert.equal(record.dma_state,null);
                        assert.equal(record.dma_fault_prefix,null);
                    } else {
                        assert.deepEqual(record.dma_state,{generation:rxRaw.readUInt32LE(64),
                            words,prefix:'5a'.repeat(128)});
                        assert.equal(record.dma_fault_prefix,'5a'.repeat(128));
                    }
                }
                rxRaw.fill(0,720,768); rxRaw.fill(0,1480,1612);
                rxRaw.writeUInt32LE(0,44);
            }
            if (zeroRecord.ba_experiment.enabled) {
                assert.equal(zeroRecord.ba_experiment.returned_ba,false);
                assert.equal(zeroRecord.ba_experiment.returned_bitmap,0);
                txRaw[1480]^=1; fs.writeFileSync(file,txRaw); txRaw[1480]^=1;
                const missingReceipt=spawnSync(collector,['--inspect-ampdu',file,config.challenge],{encoding:'utf8'});
                assert.equal(missingReceipt.status,0,missingReceipt.stderr);
                const missingRecord=JSON.parse(missingReceipt.stdout);
                assert.equal(missingRecord.ba_experiment.returned_ba,null);
                assert.equal(missingRecord.ba_experiment.returned_bitmap,null);
            }
            rxRaw.writeUInt32LE(99,712); fs.writeFileSync(file,rxRaw);
            rxRaw.writeUInt32LE(0,712);
            const misleading=spawnSync(collector,['--inspect-ampdu',file,config.challenge],{encoding:'utf8'});
            const counters=JSON.parse(misleading.stdout);
            assert.equal(counters.delivery_payloads_valid,false);
            assert.equal(counters.delivered_subframe_bitmap,null);
            assert.equal(counters.delivered_subframes,null);
            assert.equal(counters.delivered_duplicates,null);
            for (const offset of [64,2052,2068,2072,2088]) {
                rxRaw[offset]^=1; fs.writeFileSync(file,rxRaw); rxRaw[offset]^=1;
                const result=spawnSync(collector,['--inspect-ampdu',file,config.challenge],{encoding:'utf8'});
                assert.equal(result.status,1,result.stderr);
                const record=JSON.parse(result.stdout);
                assert.equal(record.snapshot.valid,false);
                for (const field of ['aggregate_observations','delivered_subframes','delivered_duplicates',
                    'delivered_subframe_bitmap','retained_subframe_bitmap','shm_terminal'])
                    assert.equal(record[field],null,field);
            }
        } finally { fs.unlinkSync(file); fs.rmdirSync(dir); }
    }
    console.log('Aggregate evidence: complete/zero controls, stale/missing/partial reports, false counters, invalid lease and collector nulls pass');
}
module.exports={verify,crc,mpdu};
if (require.main===module) {
    if (process.argv[2]==='--self-test') selfTest(process.argv[3]);
    else {
        assert.equal(process.argv.length,3,'Usage: node ampdu_verify.cjs PREFIX');
        const p=process.argv[2], read=s=>JSON.parse(fs.readFileSync(`${p}-${s}.json`,'utf8'));
        console.log(JSON.stringify(verify(read('config'),read('tx'),read('rx'),
            fs.readFileSync(`${p}-tx.bin`),fs.readFileSync(`${p}-rx.bin`))));
    }
}
