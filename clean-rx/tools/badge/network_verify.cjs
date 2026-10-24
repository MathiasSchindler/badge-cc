'use strict';
const fs = require('node:fs');
const assert = require('node:assert/strict');
const fields = ['magic','state','error','seconds','elapsed_us','generated','acknowledged',
    'delivered','replies','retries','lost','duplicates','corrupt','stale','arp_tx','arp_rx',
    'tx_full','rx_full','tx_high','rx_high','tx_packets','rx_packets','abandoned',
    'idle_periods','service_max_us','tx_hash','rx_hash'];
function verify(station, ap, seconds) {
    assert.equal(station.valid, true, 'station rejected continuous data');
    assert.equal(ap.valid, true, 'independent AP rejected continuous data');
    assert.equal(station.radio_error,0);
    assert.ok(station.connections>0);
    assert.equal(station.connections,station.data_connections);
    const s = station.network, a = ap.network;
    assert.equal(s.role,1); assert.equal(a.role,0);
    assert.equal(s.peer,a.mac); assert.equal(a.peer,s.mac);
    assert.equal(s.session,a.session); assert.equal(s.epoch,a.epoch);
    for (const n of [s,a]) {
        assert.equal(n.magic,0x324e5342); assert.equal(n.state,2); assert.equal(n.error,0);
        assert.equal(n.bound,1);
        assert.equal(n.seconds,seconds); assert.equal(n.generated,n.acknowledged);
        for (const key of ['lost','corrupt','abandoned','rx_full']) assert.equal(n[key],0,key);
        assert.ok(n.tx_high<=4 && n.rx_high<=4);
        assert.ok(n.generated>=Math.floor((seconds-5)*0.75),'insufficient continuous stimulus');
        assert.ok(n.large_generated>=Math.floor(n.generated/3),'missing full-sized traffic');
        assert.equal(n.large_generated,n.large_acknowledged);
        assert.equal(n.latency_samples,n.acknowledged);
        assert.ok(n.arp_rx && n.arp_tx);
        assert.ok(n.elapsed_us>=seconds*1000000-1000000,'run ended before requested interval');
        assert.ok(n.idle_periods>=Math.floor(seconds/20),'missing scheduled idle periods');
        assert.ok(n.service_max_us>0 && n.service_max_us<=2000000,'network service starved');
    }
    assert.equal(s.generated,a.delivered); assert.equal(a.generated,s.delivered);
    assert.equal(s.large_generated,a.large_delivered);
    assert.equal(a.large_generated,s.large_delivered);
    assert.equal(s.tx_hash,a.rx_hash); assert.equal(a.tx_hash,s.rx_hash);
    if (seconds>=30) {
        assert.ok(station.host_stalls>=Math.floor(seconds/30) &&
            ap.host_stalls>=Math.floor(seconds/30),'missing actual host stalls');
        assert.ok(s.idle_periods && a.idle_periods,'missing idle periods');
    }
}
function archive(path,offset,n,size) {
    const raw = fs.readFileSync(path);
    assert.ok(raw.length===size || (size===2048 && [2068,2092].includes(raw.length)),
        'unexpected archived report length');
    fields.forEach((name,i)=>assert.equal(raw.readUInt32LE(offset+i*4),n[name],name));
    const mac = at=>[...raw.subarray(offset+at,offset+at+6)]
        .map(byte=>byte.toString(16).padStart(2,'0')).join(':');
    assert.equal(mac(108),n.mac); assert.equal(mac(114),n.peer);
    assert.equal(raw.subarray(offset+120,offset+126).toString('hex'),n.session);
    assert.equal(raw[offset+126],n.role); assert.equal(raw[offset+127],n.bound);
    assert.equal(raw.readUInt32LE(offset+128),n.epoch);
    ['large_generated','large_acknowledged','large_delivered','latency_max_us','latency_samples']
        .forEach((key,i)=>assert.equal(raw.readUInt32LE(offset+132+i*4),n[key],key));
    if (size===2048) {
        assert.equal(raw.readUInt32LE(8),0,'station radio error');
        assert.equal(raw[2000],33,'station mode');
        assert.equal(raw.readUInt32LE(612),1,'station cleanup');
        assert.equal(raw.readUInt32LE(620),0,'station elapsed high word');
        assert.ok(raw.readUInt32LE(616)>=n.seconds*1000000,'station deadline not reached');
    } else {
        assert.equal(raw.readUInt32LE(84),4,'AP mode');
        assert.ok(n.elapsed_us>=n.seconds*1000000,'AP deadline not reached');
    }
}
if (process.argv[2]==='--self-test') {
    const n={magic:0x324e5342,state:2,error:0,seconds:30,elapsed_us:30000000,generated:100,
        acknowledged:100,delivered:100,lost:0,corrupt:0,abandoned:0,rx_full:0,tx_high:4,
        rx_high:4,arp_tx:1,arp_rx:1,idle_periods:1,service_max_us:1000,
        bound:1,large_generated:50,large_acknowledged:50,large_delivered:50,latency_max_us:1000,
        latency_samples:100,session:'010203040506',epoch:1,tx_hash:5,rx_hash:7};
    const s={valid:true,radio_error:0,connections:1,data_connections:1,host_stalls:1,
        network:{...n,role:1,mac:'sta',peer:'ap'}};
    const a={valid:true,host_stalls:1,network:{...n,role:0,mac:'ap',peer:'sta',tx_hash:7,rx_hash:5}};
    verify(s,a,30);
    for (const [key,value] of [['generated',99],['session','stale'],['tx_hash',0],
        ['lost',1],['corrupt',1],['rx_high',5],['elapsed_us',1000],['idle_periods',0],
        ['service_max_us',2000001],['large_generated',0],['large_delivered',49],
        ['latency_samples',99]])
        assert.throws(()=>verify(s,{...a,network:{...a.network,[key]:value}},30));
    assert.throws(()=>verify({...s,host_stalls:0},a,30));
    const saved={...n,mac:'02:01:02:03:04:05',peer:'02:06:07:08:09:0a',role:1};
    for (const name of fields) if (saved[name]===undefined) saved[name]=0;
    const raw=Buffer.alloc(2068), offset=1280;
    raw.writeUInt32LE(0x31504e53,2048); raw.writeUInt32LE(3,2052);
    fields.forEach((name,i)=>raw.writeUInt32LE(saved[name],offset+i*4));
    Buffer.from(saved.mac.replaceAll(':',''),'hex').copy(raw,offset+108);
    Buffer.from(saved.peer.replaceAll(':',''),'hex').copy(raw,offset+114);
    Buffer.from(saved.session,'hex').copy(raw,offset+120);
    raw[offset+126]=saved.role; raw[offset+127]=saved.bound;
    raw.writeUInt32LE(saved.epoch,offset+128);
    ['large_generated','large_acknowledged','large_delivered','latency_max_us','latency_samples']
        .forEach((key,i)=>raw.writeUInt32LE(saved[key],offset+132+i*4));
    raw[2000]=33; raw.writeUInt32LE(1,612); raw.writeUInt32LE(30000000,616);
    const directory=fs.mkdtempSync(require('node:path').join(require('node:os').tmpdir(),'b2-archive-'));
    const file=`${directory}/station.bin`;
    try {
        for (const length of [2048,2068,2067]) {
            fs.writeFileSync(file,raw.subarray(0,length));
            if (length===2067) assert.throws(()=>archive(file,offset,saved,2048));
            else archive(file,offset,saved,2048);
        }
    } finally {
        fs.unlinkSync(file); fs.rmdirSync(directory);
    }
    console.log('B2 oracle: bidirectional counts/hashes, stale session, loss, bounds and duration gates pass');
} else {
    assert.equal(process.argv.length,4,'Usage: node network_verify.cjs PREFIX SECONDS');
    const prefix=process.argv[2],seconds=Number(process.argv[3]);
    assert.ok(Number.isInteger(seconds) && seconds>=10 && seconds<=3900);
    const s=JSON.parse(fs.readFileSync(`${prefix}-station.json`,'utf8'));
    const a=fs.readFileSync(`${prefix}-ap.jsonl`,'utf8').trim().split('\n')
        .map(JSON.parse).find(record=>record.test==='b2-ap-result');
    archive(`${prefix}-station.bin`,1280,s.network,2048);
    archive(`${prefix}-ap.bin`,96,a.network,320);
    verify(s,a,seconds);
    console.log(JSON.stringify({test:'b2-pair',valid:true,seconds,soak_qualified:seconds>=3600,
        station_messages:s.network.generated,ap_messages:a.network.generated,
        station_large_messages:s.network.large_generated,ap_large_messages:a.network.large_generated,
        station_retries:s.network.retries,ap_retries:a.network.retries,
        station_host_stalls:s.host_stalls,ap_host_stalls:a.host_stalls}));
}
