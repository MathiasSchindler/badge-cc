'use strict';
const assert = require('node:assert/strict');
const {verifyFiles} = require('./lifecycle_verify.cjs');

function summarize(rows,rounds) {
    const passed=rows.filter(row=>row.valid);
    const sessions=new Set(passed.map(row=>row.session));
    const valid=rows.length===3*rounds && passed.length===rows.length &&
        sessions.size===passed.length && [1,6,11].every(channel=>
            passed.filter(row=>row.channel===channel).length===rounds);
    return {test:'b3-matrix',valid,open_configurations_qualified:valid && rounds>=3,
        b3_qualified:false,ap_implementations:1,rounds,profiles:[
            {channel:1,nmode:0,security:'open'},{channel:6,nmode:0,security:'open'},
            {channel:11,nmode:1,security:'open'}],
        exclusions:['additional AP implementations','software-WPA2 persistence',
            'separate mesh nodes','roaming','B2 60-minute qualification'],rows};
}
if (process.argv[2]==='--self-test') {
    const rows=[1,6,11].flatMap(channel=>[0,1,2].map(round=>
        ({valid:true,channel,session:`${channel}-${round}`})));
    assert.equal(summarize(rows,3).open_configurations_qualified,true);
    assert.equal(summarize(rows,3).b3_qualified,false);
    assert.equal(summarize(rows.slice(1),3).valid,false);
    assert.equal(summarize(rows.map((r,i)=>i? r:{...r,valid:false}),3).valid,false);
    assert.equal(summarize(rows.map(r=>({...r,session:'reused'})),3).valid,false);
    assert.equal(summarize(rows.map(r=>({...r,channel:1})),3).valid,false);
    assert.equal(summarize([1,6,11].map(channel=>({valid:true,channel,session:String(channel)})),1)
        .open_configurations_qualified,false);
    console.log('B3 matrix: missing/failed/reused/mislabeled trials rejected; no WPA2/mesh/roaming claim');
} else {
    assert.equal(process.argv.length,6,
        'Usage: node qualification_verify.cjs PREFIX ROUNDS:1-10 STATION_SERIAL AP_SERIAL');
    const prefix=process.argv[2],rounds=Number(process.argv[3]),rows=[];
    assert.ok(Number.isInteger(rounds) && rounds>=1 && rounds<=10);
    assert.match(process.argv[4],/^[0-9A-F]{16}$/);
    assert.match(process.argv[5],/^[0-9A-F]{16}$/);
    assert.notEqual(process.argv[4],process.argv[5]);
    for (const channel of [1,6,11]) for (let round=1;round<=rounds;++round) {
        const trial=`${prefix}-ch${channel}-${round}`;
        try {
            const row=verifyFiles(trial,0,channel);
            assert.equal(row.station_serial,process.argv[4]);
            assert.equal(row.ap_serial,process.argv[5]);
            rows.push({...row,prefix:trial});
        }
        catch (error) {
            if (!(error instanceof assert.AssertionError) && !(error instanceof SyntaxError) &&
                error.code!=='ENOENT') throw error;
            rows.push({valid:false,channel,round,prefix:trial,error:error.message});
        }
    }
    const result=summarize(rows,rounds);
    console.log(JSON.stringify(result));
    if (!result.valid) process.exitCode=1;
}
