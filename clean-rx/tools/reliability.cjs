'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const {crc}=require('./ht_verify.cjs');
const network=require('./badge/network_verify.cjs');
function distribution(values) {
    if (!values.length) return null;
    const sorted=[...values].sort((a,b)=>a-b);
    return {samples:sorted.length,min:sorted[0],median:sorted[Math.ceil(sorted.length*0.5)-1],
        p95:sorted[Math.ceil(sorted.length*0.95)-1],max:sorted.at(-1)};
}
function hex(text,bytes) {
    assert.match(text,new RegExp(`^[0-9a-f]{${bytes*2}}$`));return Buffer.from(text,'hex');
}
function frame(text,source,destination,kind,row) {
    const packet=hex(text,98),m=packet.subarray(18);
    assert.equal(packet[15],11);assert.equal(packet[16],0x16);
    const expected=Buffer.alloc(80),id=Buffer.from(source,'hex').reverse();
    expected[0]=8;expected.fill(255,4,10);expected[10]=2;id.copy(expected,11,0,5);
    Buffer.from([2,80,50,87,82,1]).copy(expected,16);
    expected.writeUInt16LE((row.sequence<<4)&65535,22);
    Buffer.from('aaaa0300000088b5','hex').copy(expected,24);
    expected.writeUInt32LE(0x31523250,32);expected[36]=1;expected[37]=kind;
    expected[38]=m[38];expected[39]=m[39];
    assert.ok(m[38]>=1 && m[38]<=3 && m[39]>=1 && m[39]<=3 && m[38]!==m[39]);
    id.copy(expected,40);expected.writeUInt32LE(row.session,48);expected.writeUInt32LE(row.sequence,52);
    hex(row.data_hex,14).copy(expected,56);expected.writeUInt32LE(crc(expected.subarray(0,76)),76);
    assert.deepEqual(m,expected,`${kind===1?'request':'reply'} bytes/FCS mismatch`);
    return {node:m[38],peer:m[39],packet};
}
function cross(records) {
    const rows=records.filter(r=>r.test==='clean-rx-three-node'),links={},seen=new Set(),nodes=new Map();
    for(const r of rows) {
        assert.match(r.source,/^[0-9A-F]{16}$/);assert.match(r.destination,/^[0-9A-F]{16}$/);
        assert.notEqual(r.source,r.destination);
        const key=`${r.source}->${r.destination}`;
        const g=links[key]??={completed:0,failed:0,attempts:Array(6).fill(0),latency:[],device:[]};
        if(!r.verified){assert.equal(r.verified,false);++g.failed;continue;}
        assert.ok(Number.isInteger(r.attempts) && r.attempts>=1 && r.attempts<=6);
        assert.ok(Number.isInteger(r.elapsed_ms) && r.elapsed_ms>=0);
        assert.ok(Number.isInteger(r.session) && r.session>0 && r.session<=0xffffffff);
        assert.ok(Number.isInteger(r.sequence) && r.sequence>0 && r.sequence<=0xffffffff);
        assert.ok(!seen.has(r.data_hex),'reused challenge');seen.add(r.data_hex);
        const request=frame(r.received_request_hex,r.source,r.destination,1,r);
        const reply=frame(r.received_reply_hex,r.destination,r.source,2,r);
        assert.equal(request.node,reply.peer);assert.equal(request.peer,reply.node);
        for(const [serial,node] of [[r.source,request.node],[r.destination,reply.node]]) {
            if(nodes.has(serial))assert.equal(nodes.get(serial),node);else nodes.set(serial,node);
        }
        if(r.sender_trace_hex!==undefined) {
            const s=hex(r.sender_trace_hex,512),d=hex(r.receiver_trace_hex,512);
            assert.equal(s.readUInt32LE(0),0x31523250);assert.equal(d.readUInt32LE(0),0x31523250);
            assert.equal(s.readUInt32LE(4),request.node);assert.equal(d.readUInt32LE(4),reply.node);
            assert.equal(s.readUInt32LE(8),r.session);assert.equal(s.readUInt32LE(12),r.sequence);
            assert.equal(s.readUInt32LE(76),0);assert.equal(s.readUInt32LE(80),r.attempts);
            assert.deepEqual(s.subarray(256,354),reply.packet);assert.deepEqual(d.subarray(128,226),request.packet);
            assert.equal(s.readUInt32LE(384),0x314d3852);assert.equal(r.timing?.available,true);
            for(const [key,at] of [['first_tx_delay_us',388],['request_reply_us',392],['round_trip_us',396]])
                assert.equal(r.timing[key],s.readUInt32LE(at));
            assert.equal(r.timing.request_reply_us,r.timing.first_tx_delay_us+r.timing.round_trip_us);
            g.device.push(r.timing.round_trip_us);
        } else assert.ok(!r.timing?.available,'device timing without archived trace');
        ++g.completed;++g.attempts[r.attempts-1];g.latency.push(r.elapsed_ms);
    }
    assert.ok(rows.length,'no exchanges');
    const summary=records.find(r=>r.summary==='three-node-exchanges');
    assert.ok(summary,'missing collector summary');
    assert.equal(summary.passed,Object.values(links).reduce((s,g)=>s+g.completed,0));
    assert.ok(Number.isInteger(summary.expected) && summary.expected>=rows.length);
    if(summary.verified)assert.equal(summary.passed,summary.expected);
    for(const g of Object.values(links)) {
        g.first_attempt_rate=g.completed+g.failed?g.attempts[0]/(g.completed+g.failed):null;
        g.host_elapsed_ms=distribution(g.latency);g.device_round_trip_us=distribution(g.device);
        delete g.latency;delete g.device;
    }
    return {limits:{max_attempts:6,collector_window_ms:5000},completed:summary.passed,
        expected:summary.expected,complete:summary.verified===true,links};
}
function pair(prefix,seconds) {
    const s=JSON.parse(fs.readFileSync(`${prefix}-station.json`));
    const records=fs.readFileSync(`${prefix}-ap.jsonl`,'utf8').trim().split('\n').map(JSON.parse);
    const a=records.find(r=>r.test==='b2-ap-result');assert.ok(a,'missing AP result');
    network.archive(`${prefix}-station.bin`,1280,s.network,2048);
    network.archive(`${prefix}-ap.bin`,96,a.network,320);
    network.verify(s,a,seconds);
    const roles={};
    for(const [name,r] of [['station',s],['ap',a]]) {
        roles[name]={metrics:network.metrics(r.network),host:network.host(r),
            generated:r.network.generated,retries:r.network.retries,lost:r.network.lost,
            service_max_us:r.network.service_max_us};
    }
    const raw=fs.readFileSync(`${prefix}-station.bin`);
    assert.equal(raw.readUInt32LE(520),0x314c5342,'station lifecycle radio evidence');
    roles.station.radio={attempts:raw.readUInt32LE(588),ack_timeouts:raw.readUInt32LE(592),
        busy:raw.readUInt32LE(628)};
    for(const [key,offset] of [['radio_tx_attempts',588],['radio_tx_timeouts',592],['radio_tx_busy',628]])
        if(s[key]!==undefined)assert.equal(s[key],raw.readUInt32LE(offset));
    roles.ap.radio={available:false};
    return {limits:{max_application_attempts:4,service_max_us:2000000},seconds,roles,
        rf_loss_correlation:'unavailable_without_independent_rf_loss_timeline'};
}
module.exports={cross,pair,distribution};
if(require.main===module) {
    try {
        const [kind,file,seconds]=process.argv.slice(2);
        assert.ok((kind==='cross' && process.argv.length===4) || (kind==='network' && process.argv.length===5),
            'Usage: reliability.cjs cross EXCHANGES.jsonl | network PREFIX SECONDS');
        const result=kind==='cross'?cross(fs.readFileSync(file,'utf8').trim().split('\n').map(JSON.parse)):
            pair(file,Number(seconds));
        console.log(JSON.stringify(result,null,2));
    } catch(error) {console.error(`Reliability evidence invalid: ${error.message}`);process.exitCode=2;}
}
