'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs');
const os=require('node:os'),path=require('node:path'),{spawnSync}=require('node:child_process');
const {cross,distribution}=require('./reliability.cjs'),{archive,metrics,host}=require('./badge/network_verify.cjs');
const {crc}=require('./ht_verify.cjs');
function example() {
    const source='0000000000000001',destination='0000000000000002';
    const r={test:'clean-rx-three-node',source,destination,session:1,sequence:1,attempts:1,
        elapsed_ms:20,data_hex:'0102030405060708090a0b0c0d0e',verified:true};
    const packet=(serial,node,peer,kind)=>{
        const m=Buffer.alloc(80),id=Buffer.from(serial,'hex').reverse();
        m[0]=8;m.fill(255,4,10);m[10]=2;id.copy(m,11,0,5);
        Buffer.from('0250325752011000aaaa0300000088b5','hex').copy(m,16);
        m.writeUInt32LE(0x31523250,32);m[36]=1;m[37]=kind;m[38]=node;m[39]=peer;
        id.copy(m,40);m.writeUInt32LE(1,48);m.writeUInt32LE(1,52);
        Buffer.from(r.data_hex,'hex').copy(m,56);m.writeUInt32LE(crc(m.subarray(0,76)),76);
        const p=Buffer.alloc(98);p[15]=11;p[16]=0x16;m.copy(p,18);return p.toString('hex');
    };
    r.received_request_hex=packet(source,1,2,1);r.received_reply_hex=packet(destination,2,1,2);
    return [r,{summary:'three-node-exchanges',passed:1,expected:1,verified:true}];
}
test('cross-board distribution uses complete identity-bound frames and explicit missing device timing',()=>{
    const value=cross(example());
    const link=Object.values(value.links)[0];
    assert.equal(link.first_attempt_rate,1);assert.equal(link.device_round_trip_us,null);
    assert.deepEqual(link.attempts,[1,0,0,0,0,0]);assert.equal(link.host_elapsed_ms.max,20);
    assert.deepEqual(distribution([4,1,3,2]),{samples:4,min:1,median:2,p95:4,max:4});
});
test('cross-board analyzer rejects stale, forged, truncated and wrong-denominator evidence',()=>{
    for(const change of [
        r=>r.attempts=7,r=>r.elapsed_ms=-1,r=>r.source='BAD',
        r=>r.received_request_hex=r.received_request_hex.slice(2),
        r=>r.received_reply_hex=r.received_request_hex,r=>r.sequence++,
        r=>r.data_hex='00'.repeat(14),r=>r.timing={available:true}
    ]){const rows=example();change(rows[0]);assert.throws(()=>cross(rows));}
    const duplicate=example();duplicate.splice(1,0,{...duplicate[0]});assert.throws(()=>cross(duplicate));
    const bad=example();bad[1].passed=2;assert.throws(()=>cross(bad));
});
test('archived device timing is independent of host timing and must agree with raw traces',()=>{
    const rows=example(),r=rows[0],s=Buffer.alloc(512),d=Buffer.alloc(512);
    for(const [trace,node] of [[s,1],[d,2]]) {
        trace.writeUInt32LE(0x31523250,0);trace.writeUInt32LE(node,4);
        trace.writeUInt32LE(1,8);trace.writeUInt32LE(1,12);
    }
    s.writeUInt32LE(1,80);
    Buffer.from(r.received_reply_hex,'hex').copy(s,256);
    Buffer.from(r.received_request_hex,'hex').copy(d,128);
    s.writeUInt32LE(0x314d3852,384);
    s.writeUInt32LE(1000,388);s.writeUInt32LE(4000,392);s.writeUInt32LE(3000,396);
    r.sender_trace_hex=s.toString('hex');r.receiver_trace_hex=d.toString('hex');
    r.timing={available:true,first_tx_delay_us:1000,request_reply_us:4000,round_trip_us:3000};
    assert.equal(Object.values(cross(rows).links)[0].device_round_trip_us.max,3000);
    r.timing.round_trip_us++;
    assert.throws(()=>cross(rows));
    r.timing.round_trip_us--;
    s[256+76]^=1;r.sender_trace_hex=s.toString('hex');
    assert.throws(()=>cross(rows));
});
test('failed exchanges remain in the first-attempt denominator',()=>{
    const rows=example();
    rows.splice(1,0,{...rows[0],verified:false});
    Object.assign(rows[2],{expected:2,verified:false});
    const result=cross(rows),link=Object.values(result.links)[0];
    assert.equal(result.complete,false);
    assert.equal(link.first_attempt_rate,0.5);
    assert.equal(link.failed,1);
});
test('unavailable old network metrics are not invented zeros',()=>{
    assert.deepEqual(metrics({}),{available:false});
    assert.deepEqual(host({host_stalls:2}),{available:false,pause_count:2,duration_us:null});
});
test('truncated diagnostic timelines do not claim a complete correlation result',()=>{
    const n={role:1,generated:1,acknowledged:1,retries:3,elapsed_us:1000000,latency_samples:1,latency_max_us:300000,
        diagnostics:{available:true,ack_attempts:[0,0,0,1],latency_total_us:300000,
            latency_histogram:[0,0,1,0],service_gaps:2,radio_timeouts:1,event_count:6,events_truncated:true,
            events:[{kind:1,elapsed_ms:250},{kind:2,elapsed_ms:500},
                {kind:3,elapsed_ms:500},{kind:1,elapsed_ms:750}]}};
    assert.equal(metrics(n).event_timeline_complete,false);
    assert.equal(metrics(n).retry_gap_same_ms,null);
    assert.equal(metrics(n).radio_gap_same_ms,null);
    n.diagnostics.events_truncated=false;assert.throws(()=>metrics(n));
});
test('measured desktop pauses are distinct from firmware service gaps',()=>{
    const n={host_stalls:1,host_observation:{pauses_enabled:true,pause_target_us:2000000,
        pauses:[{begin_us:10,end_us:2000011}]}};
    assert.deepEqual(host(n).duration_us,[2000001]);
    n.host_observation.pauses_enabled=false;assert.throws(()=>host(n));
    n.host_observation.pauses_enabled=true;n.host_observation.pauses[0].end_us=9;
    assert.throws(()=>host(n));
});
test('real C engine retry accounting and JSON agree with the frozen raw extension',()=>{
    const dir=fs.mkdtempSync(path.join(os.tmpdir(),'r08-native-'));
    const binary=path.join(dir,'fixture'),raw=path.join(dir,'report.bin');
    try {
        const c=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror',
            'clean-rx/tools/network_report_test.c','clean-rx/peer/network.c','-o',binary],{encoding:'utf8'});
        assert.equal(c.status,0,c.stderr);
        const result=spawnSync(binary,[raw],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
        const n=JSON.parse(result.stdout);archive(raw,1280,n,2048);
        assert.equal(metrics(n).attempts[1],1);
        assert.equal(metrics(n).retry_gap_same_ms,0);
        n.diagnostics.ack_attempts[0]++;assert.throws(()=>archive(raw,1280,n,2048));
        n.diagnostics.ack_attempts[0]--;
        const saved=fs.readFileSync(raw);saved[1280+172]^=1;fs.writeFileSync(raw,saved);
        assert.throws(()=>archive(raw,1280,n,2048));
    } finally {
        for(const file of [binary,raw])if(fs.existsSync(file))fs.unlinkSync(file);
        fs.rmdirSync(dir);
    }
});
test('host pause controls validate without sleeping or accessing USB',()=>{
    const dir=fs.mkdtempSync(path.join(os.tmpdir(),'r08-host-')),binary=path.join(dir,'host');
    try {
        const c=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror',
            'clean-rx/tools/network_host_test.c','-o',binary],{encoding:'utf8'});
        assert.equal(c.status,0,c.stderr);
        const result=spawnSync(binary,[],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);
        assert.match(result.stderr,/must be 0 or 1/);assert.match(result.stderr,/capacity exhausted/);
    } finally {if(fs.existsSync(binary))fs.unlinkSync(binary);fs.rmdirSync(dir);}
});
