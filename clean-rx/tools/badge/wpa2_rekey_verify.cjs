const assert = require('node:assert/strict');
const fs = require('node:fs');

const stationFields = {error:8,channel:28,wpa_stage:1644,wpa_error:1648,
    m2_sent:1656,m3_validated:1660,m4_sent:1664,protected_echoes:1668,
    group_rx:1524,broadcast_rx:1528,multicast_rx:1532,group_rekeys:1536,
    pairwise_rekeys:1540,group_messages:1544,group_responses:1548,
    replay_drops:1552,plaintext_drops:1556,protected_duration_us:1560,
    group_verified:1580,pairwise_verified:1584};
const apFields = {state:156,error:160,received:168,echoed:172,channel:176,wpa_auth:180,
    operation:212,broadcast_tx:216,multicast_tx:220};
const mac = (b,at) => [...b.subarray(at,at+6)].map(x=>x.toString(16).padStart(2,'0')).join(':');

function validate(s,a,rs,ra,timing,station,ap,trafficOnly) {
    assert.match(station,/^[0-9A-F]{16}$/); assert.match(ap,/^[0-9A-F]{16}$/);
    assert.notEqual(station,ap);
    assert.equal(s.serial,station); assert.equal(a.serial,ap);
    assert.match(s.build,/^[0-9a-f]{8}$/); assert.match(a.build,/^[0-9a-f]{8}$/);
    assert.equal(rs.length,2048); assert.equal(ra.length,320);
    assert.equal(rs[2000],34); assert.ok(ra[30]===7 || ra[30]===8);
    assert.equal(rs.readUInt32LE(1520),0x324b4552);
    assert.equal(rs.readUInt32LE(1640),0x32575043);
    assert.equal(ra.readUInt32LE(152),0x31415045);
    assert.equal(ra.readUInt32LE(84),ra[30]);
    assert.equal(ra.readUInt32LE(8).toString(16).padStart(8,'0'),a.build);
    assert.equal(a.test,'e2-ap-result');
    for (const [field,at] of Object.entries(stationFields))
        assert.equal(rs.readUInt32LE(at),s[field],`station raw ${field}`);
    for (const [field,at] of Object.entries(apFields))
        assert.equal(ra.readUInt32LE(at),a[field],`AP raw ${field}`);
    for (const [field,at] of [['gtk_rekey_status',224],['ptk_rekey_status',228]])
        assert.equal(ra.readInt32LE(at),a[field],`AP raw ${field}`);
    assert.ok([1,6,11].includes(s.channel)); assert.equal(a.channel,s.channel);
    assert.equal(ra[31],s.channel); assert.equal(rs[2001],s.channel);
    assert.equal(ra.readUInt32LE(92),s.channel); assert.equal(ra.readUInt32LE(96),s.channel);
    assert.equal(ra.readUInt32LE(100),s.channel===11?1:0);
    assert.equal(ra.readUInt32LE(104),4); assert.equal(a.wsec,4);
    assert.equal(a.nmode,s.channel===11?1:0);
    assert.equal(a.wpa_auth,128); assert.equal(a.configuration_verified,true);
    assert.equal(a.state,2); assert.equal(a.operation,9); assert.equal(a.error,0);
    assert.equal(s.error,0); assert.equal(s.wpa_error,0); assert.equal(s.wpa_stage,4);
    assert.equal(s.associated,true); assert.equal(s.wpa2_authorized,true);
    assert.equal(a.authorized,true); assert.equal(a.valid,true);
    assert.equal(ra.readUInt32LE(164),1);
    assert.equal(mac(rs,1972),s.local_mac); assert.equal(mac(ra,190),s.local_mac);
    assert.equal(a.peer,s.local_mac); assert.equal(mac(rs,1980),a.bssid);
    assert.equal(mac(ra,184),a.bssid); assert.notEqual(a.bssid,s.local_mac);
    assert.equal(s.protected_token,rs.subarray(1564,1580).toString('hex'));
    assert.equal(rs.subarray(1564,1580).toString('hex'),ra.subarray(196,212).toString('hex'));
    assert.notEqual(s.protected_token,'0'.repeat(32));
    assert.equal(s.group_rx,s.broadcast_rx+s.multicast_rx);
    assert.ok(s.protected_duration_us>=35000000);
    assert.ok(s.protected_echoes>=10 && a.received>=s.protected_echoes);
    assert.equal(a.received,a.echoed);
    assert.ok(s.broadcast_rx>=1 && s.multicast_rx>=1);
    assert.ok(a.broadcast_tx>=s.broadcast_rx && a.multicast_tx>=s.multicast_rx);
    assert.ok(s.group_responses>=s.group_rekeys);
    assert.ok(s.group_messages>=s.group_responses);
    assert.ok(s.m2_sent>=1 && s.m3_validated>=1 && s.m4_sent>=1);
    assert.ok(s.group_verified<=s.group_rekeys && s.pairwise_verified<=s.pairwise_rekeys);
    if (!trafficOnly) {
        if (ra[30]===8) {
            for (const [field,at] of Object.entries({auth_state:232,auth_error:236,
                group_rekeys:240,pairwise_rekeys:244,m2:248,m4:252,g2:256,
                controller_status:260,unicast_rate:264,group_rate:268,
                group_probed:272,pairwise_verified:276}))
                assert.equal(ra.readUInt32LE(at),a[field],`authenticator raw ${field}`);
            assert.equal(a.auth_state,3); assert.equal(a.auth_error,0);
            assert.equal(a.controller_status,0);
            assert.equal((a.unicast_rate&0x3fffffff)>>>0,2);
            assert.equal((a.group_rate&0x3fffffff)>>>0,2);
            assert.equal(a.group_rekeys,s.group_rekeys);
            assert.equal(a.pairwise_rekeys,s.pairwise_rekeys);
            assert.equal(a.g2,a.group_rekeys);
            assert.equal(a.m2,a.pairwise_rekeys+1);
            assert.equal(a.m4,a.m2);
            assert.equal(a.group_probed,a.group_rekeys);
            assert.equal(a.pairwise_verified,a.pairwise_rekeys);
            assert.equal(rs.readUInt32LE(1588),22);
            assert.equal(s.pinned_rsn,'30140100000fac040100000fac040100000fac020c00');
            assert.equal(rs.subarray(1592,1614).toString('hex'),s.pinned_rsn);
        }
        assert.equal(a.gtk_rekey_status,0,'AP GTK rekey control unavailable');
        assert.equal(a.ptk_rekey_status,0,'AP PTK rekey control unavailable');
        assert.ok(s.group_rekeys>=3 && s.pairwise_rekeys>=3,'three rekeys per type required');
        assert.equal(s.group_verified,s.group_rekeys,'missing protected group epoch proof');
        assert.equal(s.pairwise_verified,s.pairwise_rekeys,'missing protected pairwise epoch proof');
        assert.ok(s.m2_sent>s.pairwise_rekeys && s.m4_sent>s.pairwise_rekeys);
        assert.equal(s.plaintext_drops,0,'plaintext application traffic observed');
    }
    for (const endpoint of ['station','ap'])
        assert.match(timing,new RegExp(`^${endpoint}_exit=0$`,'m'));
    return {test:trafficOnly?'e2-group-traffic':'e2-rekey',valid:true,
        milestone_qualified:!trafficOnly,station,ap,station_build:s.build,ap_build:a.build,
        channel:s.channel,protected_echoes:s.protected_echoes,broadcast_rx:s.broadcast_rx,
        multicast_rx:s.multicast_rx,group_rekeys:s.group_rekeys,pairwise_rekeys:s.pairwise_rekeys,
        group_verified:s.group_verified,pairwise_verified:s.pairwise_verified,
        gtk_rekey_status:a.gtk_rekey_status,ptk_rekey_status:a.ptk_rekey_status};
}

function selfTest() {
    const s={serial:'0000000000000001',build:'12345678',associated:true,wpa2_authorized:true,
        local_mac:'02:06:07:08:09:0a',protected_token:'12'.repeat(16)};
    const a={test:'e2-ap-result',serial:'0000000000000002',build:'87654321',valid:true,
        authorized:true,configuration_verified:true,wsec:4,nmode:0,bssid:'02:01:02:03:04:05',
        peer:s.local_mac,gtk_rekey_status:0,ptk_rekey_status:0};
    const rs=Buffer.alloc(2048),ra=Buffer.alloc(320);
    const values={error:0,channel:1,wpa_stage:4,wpa_error:0,m2_sent:4,m3_validated:4,m4_sent:4,
        protected_echoes:20,group_rx:12,broadcast_rx:6,multicast_rx:6,group_rekeys:3,
        pairwise_rekeys:3,group_messages:3,group_responses:3,replay_drops:0,plaintext_drops:0,
        protected_duration_us:35000000,group_verified:3,pairwise_verified:3};
    Object.assign(s,values);
    Object.assign(a,{state:2,error:0,received:20,echoed:20,channel:1,wpa_auth:128,operation:9,
        broadcast_tx:6,multicast_tx:6});
    for (const [field,at] of Object.entries(stationFields)) rs.writeUInt32LE(s[field],at);
    for (const [field,at] of Object.entries(apFields)) ra.writeUInt32LE(a[field],at);
    for (const [at,value] of [[8,0x87654321],[84,7],[92,1],[96,1],[104,4],[152,0x31415045],[164,1]])
        ra.writeUInt32LE(value,at);
    rs.writeUInt32LE(0x324b4552,1520); rs.writeUInt32LE(0x32575043,1640);
    rs[2000]=34; rs[2001]=1; ra[30]=7; ra[31]=1;
    for (const [b,at,address] of [[rs,1972,s.local_mac],[rs,1980,a.bssid],
        [ra,184,a.bssid],[ra,190,s.local_mac]])
        Buffer.from(address.split(':').map(x=>parseInt(x,16))).copy(b,at);
    Buffer.from(s.protected_token,'hex').copy(rs,1564);
    Buffer.from(s.protected_token,'hex').copy(ra,196);
    const check=(traffic=false)=>validate(s,a,rs,ra,'station_exit=0\nap_exit=0\n',s.serial,a.serial,traffic);
    assert.equal(check().milestone_qualified,true);
    for (const at of [...Object.values(stationFields),1520,1640,1564,1972,1980,2000]) {
        rs[at]^=1; assert.throws(()=>check()); rs[at]^=1;
    }
    for (const at of [...Object.values(apFields),8,30,84,92,96,100,104,152,164,184,190,196,224,228]) {
        ra[at]^=1; assert.throws(()=>check()); ra[at]^=1;
    }
    for (const [field,at] of [['group_rekeys',1536],['pairwise_rekeys',1540],
        ['group_verified',1580],['pairwise_verified',1584]]) {
        s[field]=0; rs.writeUInt32LE(0,at); assert.throws(()=>check());
        s[field]=3; rs.writeUInt32LE(3,at);
    }
    a.gtk_rekey_status=-23; ra.writeInt32LE(-23,224);
    assert.throws(()=>check());
    assert.equal(check(true).milestone_qualified,false);
    a.gtk_rekey_status=0; ra.writeInt32LE(0,224);
    for (const [count,proof,at,proofAt] of [['group_rekeys','group_verified',1536,1580],
        ['pairwise_rekeys','pairwise_verified',1540,1584]]) {
        s[count]=s[proof]=2; rs.writeUInt32LE(2,at); rs.writeUInt32LE(2,proofAt);
        assert.throws(()=>check()); assert.equal(check(true).milestone_qualified,false);
        s[count]=s[proof]=3; rs.writeUInt32LE(3,at); rs.writeUInt32LE(3,proofAt);
    }
    s.protected_duration_us=34999999; rs.writeUInt32LE(34999999,1560);
    assert.throws(()=>check(true));
    s.protected_duration_us=35000000; rs.writeUInt32LE(35000000,1560);
    rs[2000]=32; assert.throws(()=>check(true)); rs[2000]=34;
    ra[30]=8; ra.writeUInt32LE(8,84);
    Object.assign(a,{auth_state:3,auth_error:0,group_rekeys:3,pairwise_rekeys:3,
        m2:4,m4:4,g2:3,controller_status:0,unicast_rate:0xc0000002,group_rate:0xc0000002,
        group_probed:3,pairwise_verified:3});
    const authFields={auth_state:232,auth_error:236,group_rekeys:240,
        pairwise_rekeys:244,m2:248,m4:252,g2:256,controller_status:260,
        unicast_rate:264,group_rate:268,group_probed:272,pairwise_verified:276};
    for (const [field,at] of Object.entries(authFields)) ra.writeUInt32LE(a[field],at);
    s.pinned_rsn='30140100000fac040100000fac040100000fac020c00';
    rs.writeUInt32LE(22,1588); Buffer.from(s.pinned_rsn,'hex').copy(rs,1592);
    assert.equal(check().milestone_qualified,true);
    for (const at of [...Object.values(authFields),1588,1592,1612]) {
        const raw=at<1000?ra:rs, offset=at;
        raw[offset]^=1; assert.throws(()=>check()); raw[offset]^=1;
    }
    for (const [field,at,value] of [['group_probed',272,2],['pairwise_verified',276,2],
        ['unicast_rate',264,22],['auth_state',232,1],['auth_error',236,5]]) {
        const old=a[field]; a[field]=value; ra.writeUInt32LE(value,at);
        assert.throws(()=>check()); a[field]=old; ra.writeUInt32LE(old,at);
    }
    s.plaintext_drops=1; rs.writeUInt32LE(1,1556); assert.throws(()=>check());
    s.plaintext_drops=0; rs.writeUInt32LE(0,1556);
    console.log('E2 oracle: raw identity/security/token/counter/duration tampering and missing per-epoch proof rejected; traffic-only never qualifies rekeys');
}

if (process.argv[2]==='--self-test') selfTest();
else {
    const [prefix,station,ap,flag]=process.argv.slice(2);
    try {
        assert.ok(prefix && station && ap && (!flag || flag==='--traffic-only'));
        const s=JSON.parse(fs.readFileSync(`${prefix}-station.json`,'utf8'));
        const rows=fs.readFileSync(`${prefix}-ap.jsonl`,'utf8').trim().split('\n').map(x=>JSON.parse(x));
        const results=rows.filter(x=>x.test==='e2-ap-result'); assert.equal(results.length,1);
        console.log(JSON.stringify(validate(s,results[0],fs.readFileSync(`${prefix}-station.bin`),
            fs.readFileSync(`${prefix}-ap.bin`),fs.readFileSync(`${prefix}-timing.log`,'utf8'),
            station,ap,flag==='--traffic-only')));
    } catch (error) {
        console.error(error.stack); process.exitCode=1;
    }
}
