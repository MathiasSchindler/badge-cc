'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {crc, fixture, ackFrame, endpoint, witness, sourceWitness, verify, diagnose} = require('./ht_verify.cjs');
const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
const {spawnSync} = require('node:child_process');

function example() {
    const config = {challenge: '0102030405060708090a0b1c0000',
        fixture: 'data', mcs: 0, channel: 6, mpdu_bytes: 28};
    const m = fixture(config);
    function board(role) {
        const raw = Buffer.alloc(2092), mode = role === 'tx' ? 24 : 25;
        for (const [at, value] of [[0, 0x31585243], [4, 8], [32, 0x4f505258],
            [36, 10], [64, 0x04030201], [2048, 0x31504e53], [2052, 1],
            [2068, 15], [2088, 0x04030201]]) raw.writeUInt32LE(value, at);
        Buffer.from(config.challenge, 'hex').copy(raw, 2072);
        raw[2000] = raw[2086] = mode; raw[2001] = raw[2087] = 6;
        if (role === 'tx') {
            raw.writeUInt32LE(1, 1096); raw.writeUInt32LE(1, 1092); raw.writeUInt32LE(34, 1988);
        }
        const json = {snapshot: {valid: true}, capture_available: true,
            challenge: config.challenge, channel: 6, mcs: 0, role, error: 0,
            fw_error: 0, rx_captures: 0, tx_submitted: role === 'tx',
            fresh_ht_rx: false, fresh_ht_trace: false};
        return {raw, json};
    }
    const tx = board('tx'), rx = board('rx');
    const wr = Buffer.alloc(320);
    wr.writeUInt32LE(0x46363255, 0); wr.writeUInt32LE(3, 12);
    Buffer.from([1, 0, 64, 0]).copy(wr, 4);
    Buffer.from(config.challenge, 'hex').copy(wr, 16);
    wr[30] = 15; wr[31] = 6;
    wr.writeUInt32LE(crc(wr.subarray(0, 60)), 60);
    const t = wr.subarray(64);
    for (const [at, value] of [[0, 0x31504843], [20, 15], [24, 0x31575448], [28, 1], [84, 1]])
        t.writeUInt32LE(value, at);
    const wj = {available: true, build: '00000000', challenge: config.challenge, channel: 6,
        frames: 0, acks: 0, candidates: 0, truncated: 0, bad_payload: 0, bad_fcs: 0,
        mpdu_bytes: 0, fcs: 0, ack_fcs: 0};
    return {config, m, tx, rx, wr, wj,
        run: () => verify(config, tx.json, rx.json, tx.raw, rx.raw, wj, wr)};
}
test('no witness is inconclusive; submission never proves transmission', () => {
    const e = example(), result = e.run();
    assert.equal(result.classification, 'transmission_reception_unresolved');
    assert.equal(result.tx_over_air_confirmed, false);
    assert.equal(result.accepted, false);
});
test('required rejected-copy diagnostics fail closed rather than disappearing from a valid snapshot', () => {
    const e = example();
    e.config.require_reject_copy = true;
    assert.throws(e.run, /required rejected-copy trace is missing/);
    e.config.require_reject_copy = 'true';
    assert.throws(e.run);
    delete e.config.require_reject_copy;
    e.rx.json.ht_reject_copy = {available: true};
    assert.throws(e.run);
});
test('required stopped-SHM baseline fails closed when missing or incorrectly configured', () => {
    const e = example();
    e.config.require_reject_baseline = true;
    assert.throws(e.run, /required rejected-frame baseline is missing/);
    e.config.require_reject_baseline = 'true';
    assert.throws(e.run);
    delete e.config.require_reject_baseline;
    e.rx.json.ht_reject_baseline = {available: true};
    assert.throws(e.run);
});
test('required candidate trace fails closed when missing or incorrectly configured', () => {
    const e = example();
    e.config.require_candidate_trace = true;
    assert.throws(e.run, /required per-candidate trace is missing/);
    e.config.require_candidate_trace = 'true';
    assert.throws(e.run);
    delete e.config.require_candidate_trace;
    e.rx.json.ht_candidate = {available: true};
    assert.throws(e.run);
});
test('positive independent complete witness separates target receiver miss', () => {
    const e = example(), t = e.wr.subarray(64);
    t.writeUInt32LE(1, 56); e.wj.frames = 1;
    t.writeUInt32LE(28, 72); e.wj.mpdu_bytes = 28;
    t.writeUInt32LE(e.m.readUInt32LE(24), 64); e.wj.fcs = e.m.readUInt32LE(24);
    e.m.copy(t, 128);
    assert.equal(e.run().classification, 'receiver_missed_witnessed_frame');
    assert.equal(e.run().tx_over_air_confirmed, true);
    t[128] ^= 1;
    assert.throws(e.run, /Expected values/);
});
test('unavailable snapshots are not zero reception', () => {
    const e = example();
    e.rx.raw.writeUInt32LE(2, 2052);
    Object.assign(e.rx.json, {snapshot: {valid: false}, capture_available: false,
        fresh_ht_rx: null, fresh_ht_trace: null, rx_captures: null, tx_submitted: null});
    const result = e.run();
    assert.equal(result.classification, 'evidence_unavailable');
    assert.equal(result.fresh_ht_rx, null);
    e.rx.json.rx_captures = 0;
    assert.throws(e.run);
});
test('stale nonce, generation, direction and raw/JSON disagreements fail closed', () => {
    for (const at of [64, 2000, 2001, 2072, 2086, 2087, 2088]) {
        const e = example(); e.rx.raw[at] ^= 1;
        assert.throws(e.run, `offset ${at}`);
    }
    for (const key of ['challenge', 'rx_captures', 'tx_submitted', 'fresh_ht_rx']) {
        const e = example(); e.rx.json[key] = 'tampered';
        assert.throws(e.run, key);
    }
});
test('unrelated captures are not corrupt fixture captures', () => {
    const e = example();
    e.rx.raw.writeUInt32LE(0x31525448, 520);
    e.rx.raw.writeUInt32LE(1, 524); e.rx.json.rx_captures = 1;
    assert.equal(e.run().classification, 'unrelated_capture_tx_rx_unresolved');
    e.m.copy(e.rx.raw, 544); e.rx.raw[544] ^= 1;
    e.rx.raw.writeUInt16LE(34, 528);
    assert.equal(e.run().classification, 'corrupt_correlated_capture');
});
test('partial independent capture cannot claim complete transmission', () => {
    const e = example(), t = e.wr.subarray(64);
    t.writeUInt32LE(1, 40); t.writeUInt32LE(1, 44);
    e.wj.candidates = e.wj.truncated = 1;
    const result = e.run();
    assert.equal(result.classification, 'partial_witness_tx_integrity_unresolved');
    assert.equal(result.tx_over_air_confirmed, false);
});
test('independent witness binding, FCS and JSON counters are checked', () => {
    for (const at of [0, 8, 16, 30, 31, 60, 64, 84, 88, 92, 148, 152]) {
        const e = example(); e.wr[at] ^= 1;
        assert.throws(() => witness(e.wr, e.wj, e.config, e.m));
    }
});
test('delivered frame requires retained bytes, full-check summary and metadata', () => {
    const e = example(), raw = e.rx.raw;
    for (const [at, value] of [[680, 0x31524448], [684, 28], [688, 0],
        [692, e.m.readUInt32LE(24)], [1996, 1], [1088, 46]]) raw.writeUInt32LE(value, at);
    raw[1120] = 0xc6; raw[1121] = 1; e.m.copy(raw, 1122);
    e.rx.json.fresh_ht_rx = true;
    assert.equal(e.run().accepted, true);
    for (let at = 1122; at < 1150; ++at) {
        raw[at] ^= 1;
        assert.throws(() => endpoint(raw, e.rx.json, e.config, 'rx', e.m));
        raw[at] ^= 1;
    }
});
test('independent fixtures agree with the real C collector, including full long-frame FCS and ACK',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-oracle-'));
        try {
            for (const [kind, length] of [['data', 28], ['data', 3869], ['ack', 60]])
                for (let mcs = 0; mcs < 8; ++mcs) {
                    const e = example(), n = Buffer.from(e.config.challenge, 'hex');
                    n.writeUInt16LE(length, 11); n[13] = mcs;
                    Object.assign(e.config, {fixture: kind, mpdu_bytes: length, mcs, challenge: n.toString('hex')});
                    const m = fixture(e.config);
                    const sig = Buffer.from([mcs, length & 255, length >>> 8, 7, 0, 0]);
                    let value = 255;
                    for (let i = 0; i < 34; ++i) {
                        const feedback = (value >>> 7) ^ ((sig[i >>> 3] >>> (i & 7)) & 1);
                        value = ((value << 1) ^ (feedback ? 7 : 0)) & 255;
                    }
                    value ^= 255;
                    for (let i = 0; i < 8; ++i)
                        sig[(34 + i) >>> 3] |= ((value >>> (7 - i)) & 1) << ((34 + i) & 7);
                    for (const role of ['tx', 'rx']) {
                        const raw = e[role].raw, mode = kind === 'ack' ? (role === 'tx' ? 28 : 27) :
                            role === 'tx' ? 24 : 25;
                        n.copy(raw, 2072); raw[2000] = raw[2086] = mode;
                        if (role === 'tx') raw.writeUInt32LE(length + 6, 1988);
                        if (role === 'rx') {
                            for (const [at, number] of [[520, 0x31525448], [524, 1],
                                [680, 0x31524448], [684, length], [688, mcs],
                                [692, m.readUInt32LE(length - 4)], [1996, 1], [1088, length + 18]])
                                raw.writeUInt32LE(number, at);
                            raw.writeUInt16LE(length + 6, 528); raw.writeUInt16LE(2, 530);
                            sig.copy(raw, 538); m.subarray(0, 96).copy(raw, 544);
                            raw[1119] = mcs; raw[1120] = 0xc6; raw[1121] = 1;
                            m.subarray(0, 510).copy(raw, 1122);
                            if (kind === 'ack') {
                                raw.writeUInt32LE(1, 696); raw.writeUInt32LE(1, 704);
                            }
                        }
                        if (kind === 'ack' && role === 'tx') {
                            raw.writeUInt32LE(0x314b4341, 1000);
                            for (const at of [1004, 1008, 1012]) raw.writeUInt32LE(1, at);
                            raw.writeUInt32LE(0x32524b41, 720);
                            raw.writeUInt16LE(1, 724); raw.writeUInt16LE(1, 730);
                            ackFrame(m).copy(raw, 732);
                            let signal = (mcs === 0 ? 0xb : mcs < 3 ? 0xa : 0x9) | (14 << 5);
                            let parity = 0;
                            for (let bits = signal; bits; bits >>>= 1) parity ^= bits & 1;
                            signal |= parity << 17;
                            raw[746] = signal & 255; raw[747] = (signal >>> 8) & 255;
                            raw[748] = signal >>> 16;
                        }
                        const file = path.join(directory, 'report.bin');
                        fs.writeFileSync(file, raw);
                        const result = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                            ['--inspect-ht', file, e.config.challenge], {encoding: 'utf8'});
                        assert.equal(result.signal, null);
                        assert.ok([0, 1].includes(result.status), result.stderr);
                        const json = JSON.parse(result.stdout);
                        const evidence = endpoint(raw, json, e.config, role, m);
                        assert.equal(evidence.available, true);
                        if (role === 'rx') assert.equal(evidence.delivered, true, result.stderr);
                        if (kind === 'ack' && role === 'tx') assert.equal(evidence.returned_ack, true, result.stderr);
                        if (kind === 'data' && length === 28 && !mcs && role === 'rx') {
                            for (const at of [64, 2052, 2068, 2072, 2086, 2087, 2088]) {
                                raw[at] ^= 1; fs.writeFileSync(file, raw);
                                const failed = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                                    ['--inspect-ht', file, e.config.challenge], {encoding: 'utf8'});
                                assert.equal(failed.status, 1, failed.stderr);
                                const unavailable = JSON.parse(failed.stdout);
                                assert.equal(unavailable.snapshot.valid, false);
                                assert.equal(unavailable.rx_captures, null);
                                assert.equal(unavailable.tx_submitted, null);
                                raw[at] ^= 1;
                            }
                        }
                    }
                }
        } finally {
            fs.unlinkSync(path.join(directory, 'report.bin'));
            fs.rmdirSync(directory);
        }
    });
test('failed external monitor is unavailable, never a zero-frame witness', () => {
    const e = example(), t = e.wr.subarray(64);
    t.writeUInt32LE(0, 84); t.writeUInt32LE(7, 88);
    Object.assign(e.wj, {available: false, frames: null, acks: null, error: 7, cleanup: 0});
    const result = e.run();
    assert.equal(result.independent_witness.available, false);
    assert.equal(result.tx_over_air_confirmed, false);
    e.wj.frames = 0;
    assert.throws(e.run);
});
test('an explicitly polled observation requires raw polling evidence', () => {
    const e = example();
    e.config.witness_polling = true;
    assert.throws(e.run, /polling is missing/);
    e.wr.writeUInt32LE(99, 64 + 120);
    assert.equal(e.run().independent_witness.available, true);
});

function sourceExample(kind = 'data', length = 28, mcs = 0) {
    const e = example(), n = Buffer.from(e.config.challenge, 'hex');
    n.writeUInt16LE(length, 11); n[13] = mcs;
    Object.assign(e.config, {fixture: kind, mpdu_bytes: length, mcs,
        challenge: n.toString('hex'), witness_kind: 'source'});
    const m = fixture(e.config), raw = e.rx.raw, json = e.rx.json;
    n.copy(raw, 2072);
    for (const [at, value] of [[520, 0x31525448], [524, 1], [680, 0x31524448],
        [684, length], [688, mcs], [692, m.readUInt32LE(length - 4)], [1996, 1],
        [1088, length + 18], [1856, 0x31534f48]]) raw.writeUInt32LE(value, at);
    raw.writeUInt16LE(length + 6, 528); raw.writeUInt16LE(2, 530);
    const sig = Buffer.from([mcs, length & 255, length >>> 8, 7, 0, 0]);
    let value = 255;
    for (let i = 0; i < 34; ++i) {
        const feedback = (value >>> 7) ^ ((sig[i >>> 3] >>> (i & 7)) & 1);
        value = ((value << 1) ^ (feedback ? 7 : 0)) & 255;
    }
    value ^= 255;
    for (let i = 0; i < 8; ++i)
        sig[(34 + i) >>> 3] |= ((value >>> (7 - i)) & 1) << ((34 + i) & 7);
    sig.copy(raw, 538); m.subarray(0, 96).copy(raw, 544);
    raw[1119] = mcs; raw[1120] = 0xc6; raw[1121] = 1;
    m.subarray(0, 510).copy(raw, 1122);
    Object.assign(json, {challenge: e.config.challenge, mcs, rx_captures: 1,
        fresh_ht_rx: kind === 'data', fresh_ht_trace: kind === 'data' && length <= 96,
        observer_only: true, passive_ack_count: 0, passive_ack_mpdu: null});
    return {config: e.config, m, raw, json};
}
test('source observer requires bound frozen evidence and cannot transmit or configure responses', () => {
    const e = sourceExample();
    assert.equal(sourceWitness(e.raw, e.json, e.config, e.m).frames, 1);
    for (const at of [64, 1092, 1096, 1988, 696, 700, 704, 1856, 2072, 2086, 2087, 2088]) {
        e.raw[at] ^= 1;
        assert.throws(() => sourceWitness(e.raw, e.json, e.config, e.m), `offset ${at}`);
        e.raw[at] ^= 1;
    }
    const pair = example(); pair.config.witness_kind = 'source';
    assert.equal(verify(pair.config, pair.tx.json, pair.rx.json, pair.tx.raw, pair.rx.raw,
        e.json, e.raw).classification, 'receiver_missed_witnessed_frame');
});
test('passive ACK60 is verified as an actual wire fixture, not the C Data60 interpretation', () => {
    const e = sourceExample('ack', 60, 7);
    assert.equal(e.json.fresh_ht_rx, false);
    assert.equal(sourceWitness(e.raw, e.json, e.config, e.m).frames, 1);
    e.raw[538] ^= 1;
    assert.equal(sourceWitness(e.raw, e.json, e.config, e.m).frames, 0);
    e.raw[538] ^= 1;
    e.raw[603] ^= 1;
    assert.equal(sourceWitness(e.raw, e.json, e.config, e.m).frames, 0);
});
test('passive ACK evidence retains complete nonce-address-bound FCS bytes', () => {
    const e = sourceExample('ack', 60);
    e.raw.writeUInt32LE(0x31574148, 1832); e.raw.writeUInt32LE(1, 1836);
    ackFrame(e.m).copy(e.raw, 1840);
    Object.assign(e.json, {passive_ack_count: 1, passive_ack_mpdu: ackFrame(e.m).toString('hex')});
    assert.equal(sourceWitness(e.raw, e.json, e.config, e.m).acks, 1);
    for (let at = 1840; at < 1854; ++at) {
        e.raw[at] ^= 1;
        assert.throws(() => sourceWitness(e.raw, e.json, e.config, e.m));
        e.raw[at] ^= 1;
    }
});
test('source observer matches the real C decoder for all fixtures and rates',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-observer-'));
        const file = path.join(directory, 'report.bin');
        try {
            for (const [kind, length] of [['data', 28], ['data', 3869], ['ack', 60]])
                for (let mcs = 0; mcs < 8; ++mcs) {
                    const e = sourceExample(kind, length, mcs);
                    if (kind === 'ack') {
                        e.raw.writeUInt32LE(0x31574148, 1832); e.raw.writeUInt32LE(1, 1836);
                        ackFrame(e.m).copy(e.raw, 1840);
                    }
                    fs.writeFileSync(file, e.raw);
                    const result = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                        ['--inspect-ht', file, e.config.challenge], {encoding: 'utf8'});
                    assert.equal(result.status, kind === 'ack' ? 1 : 0, result.stderr);
                    const json = JSON.parse(result.stdout);
                    assert.equal(json.fresh_ht_rx, kind === 'data');
                    const decoded = sourceWitness(e.raw, json, e.config, e.m);
                    assert.equal(decoded.frames, 1);
                    assert.equal(decoded.acks, kind === 'ack' ? 1 : 0);
                    assert.equal(decoded.full_frame_archive, length <= 96);
                }
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
test('invalid source snapshots stay unavailable rather than recording zero observation', () => {
    const e = sourceExample();
    e.raw.writeUInt32LE(2, 2052);
    Object.assign(e.json, {snapshot: {valid: false}, capture_available: false,
        fresh_ht_rx: null, fresh_ht_trace: null, rx_captures: null, tx_submitted: null});
    assert.deepEqual(sourceWitness(e.raw, e.json, e.config, e.m), {available: false});
    e.json.rx_captures = 0;
    assert.throws(() => sourceWitness(e.raw, e.json, e.config, e.m));
});
test('a failed source observer cannot become a completed zero-frame observation', () => {
    const e = sourceExample();
    e.raw.writeUInt32LE(2, 8);
    Object.assign(e.json, {error: 2, fresh_ht_rx: false, fresh_ht_trace: false});
    assert.deepEqual(sourceWitness(e.raw, e.json, e.config, e.m), {available: false});
});
test('correlated invalid PHY/HT-SIG evidence is a corrupt capture, not missing transmission', () => {
    for (const at of [528, 530, 538, 541, 543, 544]) {
        const e = sourceExample();
        e.raw.writeUInt32LE(0, 680);
        e.json.fresh_ht_rx = e.json.fresh_ht_trace = false;
        e.raw[at] ^= 1;
        assert.equal(endpoint(e.raw, e.json, e.config, 'rx', e.m).corrupt, true, `offset ${at}`);
    }
});
test('missing, locally started and independently witnessed ACKs are distinct failures', () => {
    const w = sourceExample('ack', 60), e = example(), config = w.config;
    const tx = e.tx, rx = {raw: Buffer.from(w.raw), json: {...w.json}};
    for (const [role, mode] of [[tx, 28], [rx, 27]]) {
        Buffer.from(config.challenge, 'hex').copy(role.raw, 2072);
        role.raw[2000] = role.raw[2086] = mode;
        Object.assign(role.json, {challenge: config.challenge});
    }
    tx.raw.writeUInt32LE(66, 1988); tx.json.fresh_return_ack = false;
    rx.raw.writeUInt32LE(1, 704);
    Object.assign(rx.json, {fresh_ht_rx: true, fresh_ht_trace: true,
        ack_responses_started: 0, ack_response_errors: 0, response_configured: true});
    const run = () => verify(config, tx.json, rx.json, tx.raw, rx.raw, w.json, w.raw);
    assert.equal(run().classification, 'missing_ack_response');
    rx.raw.writeUInt32LE(1, 696); rx.json.ack_responses_started = 1;
    assert.equal(run().classification, 'ack_started_but_unwitnessed');
    w.raw.writeUInt32LE(0x31574148, 1832); w.raw.writeUInt32LE(1, 1836);
    ackFrame(w.m).copy(w.raw, 1840);
    Object.assign(w.json, {passive_ack_count: 1, passive_ack_mpdu: ackFrame(w.m).toString('hex')});
    assert.equal(run().classification, 'witnessed_ack_not_returned_to_tx');
});
test('correlated F2 evidence agrees with native C for every fixture/rate and rejects tampering',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-f2-'));
        const file = path.join(directory, 'report.bin');
        try {
            for (const [kind, length] of [['data', 28], ['data', 3869], ['ack', 60]])
                for (let mcs = 0; mcs < 8; ++mcs) {
                    const e = sourceExample(kind, length, mcs);
                    for (const [at, value] of [[1860, 0x31463248], [1864, 1], [1868, length + 18],
                        [1872, 123], [1876, e.m.readUInt32LE(length - 4)],
                        [1880, e.m.readUInt32LE(length - 4)]]) e.raw.writeUInt32LE(value, at);
                    Buffer.from([length > 64 ? 0x21 : 0x20, mcs, 0xc6, 1]).copy(e.raw, 1884);
                    e.m.subarray(0, 64).copy(e.raw, 1888);
                    fs.writeFileSync(file, e.raw);
                    const c = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                        ['--inspect-ht', file, e.config.challenge], {encoding: 'utf8'});
                    assert.ok([0, 1].includes(c.status), c.stderr);
                    const json = JSON.parse(c.stdout);
                    const d = diagnose(e.raw, json, e.config, true);
                    assert.equal(d.stage, 'fixture_delivered');
                    assert.equal(d.f2.fcs_valid, true);
                    assert.equal(d.f2.prefix_matches, true);
                    assert.equal(d.f2.metadata_matches, true);
                    json.ht_f2.correlated++;
                    assert.throws(() => diagnose(e.raw, json, e.config, true));
                    json.ht_f2.correlated--;
                    e.raw[1888 + 11] ^= 1;
                    assert.throws(() => diagnose(e.raw, json, e.config, true));
                }
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
test('stopped-SHM baseline binds generation, preserves byte differences and never proves freshness',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-baseline-'));
        const file = path.join(directory, 'report.bin');
        function collect(raw, config) {
            fs.writeFileSync(file, raw);
            const c = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                ['--inspect-ht', file, config.challenge], {encoding: 'utf8'});
            assert.ok([0, 1].includes(c.status), c.stderr);
            return JSON.parse(c.stdout);
        }
        try {
            for (const observer of [false, true]) {
                const e = sourceExample('ack', 60, 7);
                e.config.require_reject_baseline = true;
                e.raw[2000] = e.raw[2086] = observer ? 25 : 27;
                for (const at of [680, 1996, 524]) e.raw.writeUInt32LE(0, at);
                e.raw.writeUInt32LE(0x32464848, 1640);
                e.raw.writeUInt16LE(1, 1644); e.raw.writeUInt16LE(2, 1646);
                e.raw.writeUInt16LE(66, 1652);
                e.raw.writeUInt32LE(0x31504348, 1040);
                for (const [at, value] of [[1044,1],[1046,2],[1052,66],[1054,128]])
                    e.raw.writeUInt16LE(value, at);
                const captured = Buffer.concat([e.raw.subarray(538, 544), e.m]);
                captured[32] ^= 1;
                captured.copy(e.raw, 1660); captured.copy(e.raw, 1726);
                for (const [at, value] of [[1520,0x31425248],[1524,e.raw.readUInt32LE(64)],
                    [1528,0x84020400],[1532,0x84020400],[1536,100],[1540,200],
                    [1800,0x31545448],[1804,e.raw.readUInt32LE(64)],[1808,1000]])
                    e.raw.writeUInt32LE(value, at);
                for (const unchanged of [true, false]) {
                    Buffer.from(captured.map(v => unchanged ? v : v ^ 255)).copy(e.raw, 1544);
                    const j = collect(e.raw, e.config), d = diagnose(e.raw, j, e.config, observer);
                    const comparison = d.baseline.rejection_comparison;
                    assert.equal(comparison.unchanged_offsets.length, unchanged ? 66 : 0);
                    assert.equal(comparison.changed_offsets.length, unchanged ? 0 : 66);
                    assert.deepEqual(comparison.unchanged_incorrect_mac_offsets, unchanged ? [26] : []);
                    assert.equal(d.baseline.shm_copy_freshness_proven, false);
                    assert.equal(comparison.shm_copy_freshness_proven, false);
                    for (const at of [1520,1524,1528,1532,1536,1540,1544,1610,1800,1804]) {
                        const bad = Buffer.from(e.raw); bad[at] ^= 1;
                        assert.throws(() => diagnose(bad, j, e.config, observer));
                    }
                    for (const [at, value] of [[1524,8],[1528,0x84020401],
                        [1532,0x84020402],[1536,201],[1540,1001],[1088,417]]) {
                        const bad = Buffer.from(e.raw); bad.writeUInt32LE(value, at);
                        assert.throws(() => diagnose(bad, collect(bad, e.config), e.config, observer));
                    }
                }
            }
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
test('per-candidate pre-copy bytes bind the first rejection without promoting copy freshness',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-candidate-'));
        const file = path.join(directory, 'report.bin');
        const collect = (raw, config) => {
            fs.writeFileSync(file, raw);
            const c = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                ['--inspect-ht', file, config.challenge], {encoding: 'utf8'});
            assert.ok([0, 1].includes(c.status), c.stderr);
            return JSON.parse(c.stdout);
        };
        try {
            for (const observer of [false, true]) {
                const e = sourceExample('ack', 60, 7);
                e.config.require_candidate_trace = e.config.require_reject_copy = true;
                e.raw[2000] = e.raw[2086] = observer ? 25 : 27;
                for (const at of [680, 1996, 524]) e.raw.writeUInt32LE(0, at);
                for (const [at, value] of [[1040,0x31504348],[1640,0x32464848],
                    [1048,5],[1648,5],[1216,0x31435048],[1222,0xfffffff0],[1312,5]])
                    e.raw.writeUInt32LE(value, at);
                for (const [at, value] of [[1044,1],[1046,2],[1052,66],[1054,128],
                    [1644,1],[1646,2],[1652,66],[1220,0],[1310,2],[1316,66],[1318,1]])
                    e.raw.writeUInt16LE(value, at);
                const captured = Buffer.concat([e.raw.subarray(538, 544), e.m]);
                captured[32] ^= 1;
                captured.copy(e.raw, 1660); captured.copy(e.raw, 1726);
                for (const unchanged of [true, false]) {
                    Buffer.from(captured.map(v => unchanged ? v : v ^ 255)).copy(e.raw, 1244);
                    const json = collect(e.raw, e.config);
                    const d = diagnose(e.raw, json, e.config, observer).candidate_trace;
                    assert.equal(d.record_correlated, true);
                    assert.equal(d.sequence, 0, 'counter wrap is a valid candidate');
                    assert.equal(d.rejection_comparison.unchanged_offsets.length, unchanged ? 66 : 0);
                    assert.deepEqual(d.rejection_comparison.unchanged_incorrect_mac_offsets,
                        unchanged ? [26] : []);
                    assert.equal(d.shm_copy_freshness_proven, false);
                    for (const at of [1216,1220,1222,1226,1228,1230,1232,1234,1244,
                        1310,1312,1316,1318]) {
                        const bad = Buffer.from(e.raw); bad[at] ^= 1;
                        assert.throws(() => diagnose(bad, json, e.config, observer));
                    }
                    for (const [at, value] of [[1310,3],[1316,65],[1318,2],[1088,113]]) {
                        const bad = Buffer.from(e.raw); bad.writeUInt16LE(value, at);
                        assert.throws(() => diagnose(bad, collect(bad, e.config), e.config, observer));
                    }
                    const late = Buffer.from(e.raw); late.writeUInt32LE(6, 1222);
                    assert.throws(() => diagnose(late, collect(late, e.config), e.config, observer));
                }
                e.raw.writeUInt16LE(0, 1318);
                assert.throws(() => diagnose(e.raw, collect(e.raw, e.config), e.config, observer));
                for (const at of [1044,1046,1052,1048,1644,1310,1312,1316])
                    e.raw.writeUInt16LE(0, at);
                const pending = diagnose(e.raw, collect(e.raw, e.config), e.config, observer).candidate_trace;
                assert.equal(pending.record_correlated, false);
                assert.equal(pending.rejection_comparison, undefined);
            }
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
test('native-correlated RXE rejection distinguishes stable bad-FCS bytes from changing capture reads',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-reject-'));
        const file = path.join(directory, 'report.bin'), e = sourceExample('ack', 60);
        try {
            e.raw[2000] = e.raw[2086] = 27;
            e.raw.writeUInt32LE(0, 680); e.raw.writeUInt32LE(0, 1996);
            e.raw.writeUInt32LE(0, 524);
            e.raw.writeUInt32LE(0x32464848, 1640);
            e.raw.writeUInt16LE(1, 1644); e.raw.writeUInt16LE(2, 1646);
            e.raw.writeUInt16LE(66, 1652);
            const rejected = Buffer.concat([e.raw.subarray(538, 544), e.m]);
            rejected[32] ^= 1;
            rejected.copy(e.raw, 1660); rejected.copy(e.raw, 1726);
            for (const changed of [false, true]) {
                if (changed) e.raw[1758] ^= 1;
                fs.writeFileSync(file, e.raw);
                const c = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                    ['--inspect-ht', file, e.config.challenge], {encoding: 'utf8'});
                assert.ok([0, 1].includes(c.status), c.stderr);
                const d = diagnose(e.raw, JSON.parse(c.stdout), e.config);
                assert.equal(d.stage, 'rxe_rejection_with_correlated_shm');
                assert.equal(d.rejection.identity_correlated, true);
                assert.equal(d.rejection.fcs_valid, false);
                assert.equal(d.rejection.repeat_matches, !changed);
                assert.equal(d.rejection.shm_copy_freshness_proven, false);
            }
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
test('first-rejection copy operands bind active/passive evidence without proving SHM freshness',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-copy-'));
        const file = path.join(directory, 'report.bin');
        function collect(raw, config) {
            fs.writeFileSync(file, raw);
            const c = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                ['--inspect-ht', file, config.challenge], {encoding: 'utf8'});
            assert.ok([0, 1].includes(c.status), c.stderr);
            return JSON.parse(c.stdout);
        }
        try {
            for (const observer of [false, true]) {
                const e = sourceExample('ack', 60, 7);
                e.config.require_reject_copy = true;
                e.raw[2000] = e.raw[2086] = observer ? 25 : 27;
                for (const at of [680, 1996, 524]) e.raw.writeUInt32LE(0, at);
                e.raw.writeUInt32LE(0x32464848, 1640);
                for (const [at, value] of [[1644,1],[1646,2],[1648,65535],[1650,12],
                    [1652,66],[1792,66]]) e.raw.writeUInt16LE(value, at);
                const stale = Buffer.concat([e.raw.subarray(538, 544), e.m]);
                stale[32] ^= 1;
                stale.copy(e.raw, 1660); stale.copy(e.raw, 1726);
                e.raw.writeUInt32LE(0x31504348, 1040);
                e.raw.subarray(1644, 1654).copy(e.raw, 1044);
                e.raw.writeUInt16LE(128, 1054);
                const values = [0, 0, 4095, 0, 0x8000];
                for (let i=0;i<5;++i) {
                    e.raw.writeUInt16LE(values[i],1056+2*i);
                    e.raw.writeUInt16LE(values[i],1066+2*i);
                }
                const json = collect(e.raw, e.config);
                const check = (raw=e.raw, j=json) => diagnose(raw,j,e.config,observer);
                const d = check();
                assert.equal(d.stage,'rxe_rejection_with_correlated_shm');
                assert.equal(d.rejection.identity_correlated,true);
                assert.equal(d.rejection.fcs_valid,false);
                assert.equal(d.copy_trace.status,'bound_to_rejection');
                assert.equal(d.copy_trace.below_stream_threshold,true);
                assert.equal(d.copy_trace.copy_registers_stable,true);
                assert.equal(d.copy_trace.shm_copy_freshness_proven,false);
                assert.deepEqual(d.copy_trace.before,values);
                for (const key of ['count','cause','time_us','length','stream_threshold',
                    'before','after','shm_copy_freshness_proven']) {
                    const changed = structuredClone(json);
                    changed.ht_reject_copy[key] = key === 'before' || key === 'after' ?
                        [0,0,0,0,0] : 123;
                    assert.throws(()=>check(e.raw,changed),key);
                }
                for (const at of [1044,1046,1048,1050,1052,1054,1056,1064,1066,1074]) {
                    const changed = Buffer.from(e.raw); changed[at] ^= 1;
                    assert.throws(()=>check(changed),`raw offset ${at}`);
                }
                e.raw.writeUInt16LE(2,1066);
                let changed = check(e.raw,collect(e.raw,e.config));
                assert.equal(changed.copy_trace.copy_registers_stable,false);
                assert.equal(changed.copy_trace.shm_copy_freshness_proven,false);
                e.raw.writeUInt16LE(13,1050);
                changed = check(e.raw,collect(e.raw,e.config));
                assert.equal(changed.copy_trace.status,'record_mismatch');
                assert.equal(changed.copy_trace.below_stream_threshold,null);
                e.raw.writeUInt16LE(0,1044);
                changed = check(e.raw,collect(e.raw,e.config));
                assert.equal(changed.copy_trace.status,'uncommitted');
                assert.equal(changed.copy_trace.record_correlated,false);
                e.raw.writeUInt16LE(2,1044);
                assert.throws(()=>check(e.raw,collect(e.raw,e.config)),/multiple records/);
                e.raw.writeUInt32LE(0,1040);
                assert.throws(()=>check(e.raw,collect(e.raw,e.config)),/trace is missing/);
            }
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
test('a correlated valid-FCS F2 arrival with rejected metadata is not missing reception',
    {skip: !process.env.CLEAN_RX_HT_COLLECTOR}, () => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-ht-f2-reject-'));
        const file = path.join(directory, 'report.bin'), e = sourceExample();
        try {
            e.raw.writeUInt32LE(0, 680); e.raw.writeUInt32LE(0, 1996);
            e.raw.writeUInt32LE(0, 524);
            for (const [at, value] of [[1860, 0x31463248], [1864, 1], [1868, 46],
                [1872, 123], [1876, e.m.readUInt32LE(24)], [1880, e.m.readUInt32LE(24)]])
                e.raw.writeUInt32LE(value, at);
            Buffer.from([0x20, 7, 0xc6, 1]).copy(e.raw, 1884);
            e.m.copy(e.raw, 1888);
            fs.writeFileSync(file, e.raw);
            const c = spawnSync(process.env.CLEAN_RX_HT_COLLECTOR,
                ['--inspect-ht', file, e.config.challenge], {encoding: 'utf8'});
            assert.ok([0, 1].includes(c.status), c.stderr);
            const d = diagnose(e.raw, JSON.parse(c.stdout), e.config);
            assert.equal(d.stage, 'correlated_f2_not_accepted');
            assert.equal(d.f2.metadata_matches, false);
            assert.equal(d.f2.prefix_matches, true);
            assert.equal(d.f2.fcs_valid, true);
        } finally {
            if (fs.existsSync(file)) fs.unlinkSync(file);
            fs.rmdirSync(directory);
        }
    });
