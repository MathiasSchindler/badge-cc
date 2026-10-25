'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const {crc, mpdu} = require('./ampdu_verify.cjs');

const CAPACITY = 8;
const MARKER = 0x5250;
const MAX_BYTES = 4 * 1024 * 1024;
const u16 = value => value & 0xffff;
const u32 = value => value >>> 0;
const scenarios = ['repeated', 'pressure', 'wrap'];

function schedule(name) {
    if (name === 'repeated')
        return Array.from({length: 100}, (_, i) => ({identity: i + 1, count: 1 + i % 3, bad: i & 7}));
    if (name === 'pressure')
        return [...Array.from({length: CAPACITY}, (_, i) => ({identity: i + 1, count: 1, bad: 0})),
            {identity: 100, count: 3, bad: 7}, {identity: 101, count: 3, bad: 0}];
    return Array.from({length: 4}, (_, i) => ({identity: i + 1, count: 1, bad: 0}));
}

function fixture(identity, index, bad) {
    const nonce = Buffer.alloc(14);
    nonce.writeUInt32LE(identity);
    nonce[13] = identity & 7;
    const sig = Buffer.from([nonce[13], 252, 0, 15, 0, 0]);
    let check = 255;
    for (let bit = 0; bit < 34; ++bit)
        check = ((check << 1) ^ (((check >> 7) ^ ((sig[bit >> 3] >> (bit & 7)) & 1)) ? 7 : 0)) & 255;
    check ^= 255;
    for (let bit = 0; bit < 8; ++bit)
        sig[(34 + bit) >> 3] |= ((check >> (7 - bit)) & 1) << ((34 + bit) & 7);
    const body = mpdu(nonce, index, false);
    if (bad) body[79] ^= 1; // The native input fault flips bit zero, not the RF fixture's bit seven.
    assert.equal(body.readUInt32LE(76) === crc(body.subarray(0, 76)), !bad);
    return Buffer.concat([Buffer.alloc(2), sig, body]);
}

function bytes(hex, length) {
    assert.equal(typeof hex, 'string');
    assert.match(hex, new RegExp(`^[0-9a-f]{${2 * length}}$`), 'invalid complete byte record');
    return Buffer.from(hex, 'hex');
}

function frame(raw, index) {
    return {index: u16(index), length: 86, status1: 0, status2: 0, phy: 2, tick_hi: 1,
        dma_length: 0, dma_completed: 0, f2_received: 0, f2_length: 0, raw: raw.toString('hex')};
}

function verify(text) {
    assert.equal(typeof text, 'string');
    assert.ok(Buffer.byteLength(text) <= MAX_BYTES, 'transcript exceeds bounded model capture');
    assert.ok(text.endsWith('\n'), 'truncated transcript');
    const lines = text.slice(0, -1).split('\n');
    assert.ok(lines.length <= 2048, 'too many transcript events');
    const records = lines.map(line => JSON.parse(line));
    assert.deepEqual(records.shift(), {event: 'header', format: 'clean-rx-repeat-model',
        version: 1, capacity: CAPACITY, source: 'controller-psm-model'});
    assert.deepEqual(records.pop(), {event: 'complete'}, 'missing completed execution');
    let state, queue, active, expected, position, scenario, finished = 0;
    let probe, produced = 0, consumed = 0, publications = 0, pressure = 0, badWithoutCredit = 0;
    let leaseWraps = 0, ringWraps = 0;
    const checkState = record => {
        for (const [key, value] of Object.entries(state))
            assert.equal(record[key], value, `${scenario} ${record.event}: ${key}`);
    };
    for (const record of records) {
        assert.ok(record && typeof record === 'object' && !Array.isArray(record));
        if (record.event === 'begin') {
            assert.equal(state, undefined, 'overlapping scenarios');
            scenario = scenarios[finished];
            assert.equal(record.scenario, scenario, 'unexpected/missing scenario');
            const seed = scenario === 'wrap' ? 65534 : 0;
            const ringSeed = scenario === 'wrap' ? 0xfffffffe : 0;
            state = {published: seed, released: seed, claimed: 0, claim_state: 0,
                state: 0, marker: 0, probe_count: 0, mode: 0, rows: 0,
                captured: ringSeed, credited: ringSeed, copied: 0, next: 0, lease: seed};
            queue = [];
            active = undefined;
            expected = schedule(scenario);
            position = 0;
            checkState(record);
            continue;
        }
        assert.ok(state, 'event outside a scenario');
        if (record.event === 'publish') {
            assert.equal(active, undefined, 'overwriting a held lease');
            assert.ok(position < expected.length, 'unexpected extra PPDU');
            const input = expected[position++];
            for (const [key, value] of Object.entries(input)) assert.equal(record[key], value, key);
            const saved = bytes(record.probe, 288);
            assert.equal(saved.readUInt16LE(0), input.count);
            assert.equal(saved.readUInt16LE(2), input.bad & (1 << (input.count - 1)) ? 2 : 1);
            assert.equal(saved.readUInt16LE(4), 86);
            assert.equal(saved.readUInt16LE(6), 0x4000);
            assert.equal(saved.readUInt16LE(8), 0);
            assert.equal(saved.readUInt16LE(10), 2);
            const outcomes = [];
            const bodies = [];
            for (let i = 0; i < input.count; ++i) {
                const bad = !!(input.bad & (1 << i));
                outcomes.push(bad ? 2 : 1);
                bodies.push(fixture(input.identity, i, bad));
                assert.deepEqual(saved.subarray(16 + 88 * i, 104 + 88 * i), bodies[i],
                    'stale/corrupt retained fixture, HT-SIG or FCS');
                assert.equal(saved.readUInt16LE(280 + 2 * i), 0);
            }
            assert.deepEqual(record.outcomes, outcomes, 'wrong retained outcomes');
            if (state.published === 65535) ++leaseWraps;
            state.published = u16(state.published + 1);
            state.state = 16;
            state.marker = MARKER;
            state.probe_count = state.rows = input.count;
            state.mode = 3;
            active = {outcomes, bodies};
            probe = saved;
            ++publications;
            checkState(record);
        } else if (record.event === 'poll') {
            assert.equal(record.ok, 1, 'controller rejected lease');
            assert.equal(record.error, 0, 'controller error');
            assert.ok(active, 'poll without a published lease');
            if (!state.copied) {
                state.lease = state.published;
                state.claimed = u16(state.claimed + 1);
                state.claim_state = 16;
                state.copied = 1;
            }
            let delivered = null;
            if (active.outcomes[state.next] === 2) {
                if (queue.length === CAPACITY) ++badWithoutCredit;
                ++state.next;
            } else if (queue.length < CAPACITY) {
                const raw = active.bodies[state.next++];
                if (state.captured === 0xffffffff) ++ringWraps;
                state.captured = u32(state.captured + 1);
                delivered = frame(raw, state.captured);
                queue.push(delivered);
                ++produced;
            } else {
                ++pressure;
            }
            if (state.next === active.outcomes.length) {
                state.released = u16(state.released + 1);
                state.state = state.copied = state.next = state.probe_count = state.mode = state.rows = 0;
                active = undefined;
                probe = Buffer.from(probe);
                probe.writeUInt16LE(0);
            }
            assert.deepEqual(record.frame, delivered, 'wrong/stale/duplicate delivered frame');
            assert.deepEqual(bytes(record.probe, 288), probe, 'held bytes changed or rearm damaged capture');
            checkState(record);
        } else if (record.event === 'consume') {
            assert.ok(queue.length, 'credit without a delivered frame');
            assert.deepEqual(record.frame, queue.shift(), 'corruption or out-of-order consumer delivery');
            state.credited = u32(state.credited + 1);
            ++consumed;
            checkState(record);
        } else if (record.event === 'end') {
            assert.equal(record.scenario, scenario);
            assert.equal(active, undefined, 'unreleased terminal lease');
            assert.equal(queue.length, 0, 'undrained frames');
            assert.equal(position, expected.length, 'missing PPDU');
            checkState(record);
            state = undefined;
            ++finished;
        } else {
            assert.fail(`unknown event: ${record.event}`);
        }
    }
    assert.equal(state, undefined, 'unterminated scenario');
    assert.equal(finished, scenarios.length, 'incomplete scenario set');
    assert.equal(produced, consumed, 'missing consumer delivery');
    assert.equal(pressure, 10, 'missing exact full-ring stall control');
    assert.equal(badWithoutCredit, 3, 'missing all-bad/full-ring control');
    assert.equal(leaseWraps, 1, 'missing 16-bit lease wrap');
    assert.equal(ringWraps, 1, 'missing 32-bit ring wrap');
    return {mode: 'ampdu-repeat-model', valid: true, source: 'controller-psm-model',
        scenarios: finished, ppdus: publications, delivered: consumed,
        full_ring_stalls: pressure, bad_without_credit: badWithoutCredit,
        lease_wraps: leaseWraps, ring_wraps: ringWraps,
        variable_length_qualified: false, live_collection_qualified: false,
        rf_qualified: false, sifs_timing_qualified: false, ht_qualified: false};
}

function selfTest(text) {
    const result = verify(text);
    const records = text.trimEnd().split('\n').map(line => JSON.parse(line));
    const encode = rows => `${rows.map(row => JSON.stringify(row)).join('\n')}\n`;
    const corrupt = change => {
        const copy = structuredClone(records);
        change(copy);
        assert.throws(() => verify(encode(copy)));
    };
    for (const key of ['published', 'released', 'claimed', 'claim_state', 'state', 'marker',
        'probe_count', 'mode', 'rows', 'captured', 'credited', 'copied', 'next', 'lease'])
        corrupt(rows => { ++rows.find(row => row.event === 'poll')[key]; });
    for (const event of ['header', 'begin', 'publish', 'poll', 'consume', 'end', 'complete']) {
        corrupt(rows => { rows.splice(rows.findIndex(row => row.event === event), 1); });
        corrupt(rows => {
            const index = rows.findIndex(row => row.event === event);
            rows.splice(index, 0, structuredClone(rows[index]));
        });
    }
    corrupt(rows => { rows[0].source = 'radio'; });
    corrupt(rows => { rows[0].capacity *= 2; });
    corrupt(rows => { ++rows.find(row => row.event === 'publish').identity; });
    corrupt(rows => { rows.find(row => row.event === 'publish').outcomes[0] = 2; });
    corrupt(rows => {
        const published = rows.filter(row => row.event === 'publish');
        published[1].probe = published[0].probe;
    });
    corrupt(rows => {
        const poll = rows.find(row => row.event === 'poll' && row.frame);
        const raw = Buffer.from(poll.frame.raw, 'hex');
        raw[60] ^= 1;
        raw.writeUInt32LE(crc(raw.subarray(8, 84)), 84);
        poll.frame.raw = raw.toString('hex');
    });
    corrupt(rows => {
        const poll = rows.find(row => row.event === 'poll' && row.copied && row.frame);
        const raw = Buffer.from(poll.probe, 'hex');
        raw[2] ^= 1;
        poll.probe = raw.toString('hex');
    });
    corrupt(rows => { rows.find(row => row.event === 'poll').frame.raw = '00'; });
    corrupt(rows => {
        rows.find(row => row.event === 'poll' && row.frame === null).frame =
            structuredClone(rows.find(row => row.event === 'poll' && row.frame).frame);
    });
    corrupt(rows => { rows.find(row => row.event === 'poll').error = 0x8d; });
    corrupt(rows => {
        const first = rows.findIndex(row => row.event === 'consume');
        [rows[first], rows[first - 1]] = [rows[first - 1], rows[first]];
    });
    corrupt(rows => {
        rows.splice(rows.findIndex(row => row.event === 'poll' && row.released === 0 && row.lease === 0), 1);
    });
    corrupt(rows => { rows.find(row => row.event === 'poll' && row.lease === 0).released = 65536; });
    corrupt(rows => { rows.find(row => row.event === 'poll' && row.captured === 0).captured = 0x100000000; });
    assert.throws(() => verify(text.slice(0, -1)), /truncated/);
    assert.throws(() => verify(`${text}{}\n`));
    assert.throws(() => verify(`${' '.repeat(MAX_BYTES)}\n`), /bounded/);
    console.log(JSON.stringify(result));
    console.log('Repeated model oracle: stale bytes, recomputed FCS, lost/duplicate events, leases, pressure and wrap controls fail closed; NOT RF');
}

module.exports = {verify};
if (require.main === module) {
    const self = process.argv[2] === '--self-test';
    assert.equal(process.argv.length, self ? 4 : 3,
        'Usage: node ampdu_repeat_verify.cjs [--self-test] TRANSCRIPT.ndjson');
    const file = process.argv[self ? 3 : 2];
    assert.ok(fs.statSync(file).size <= MAX_BYTES, 'transcript exceeds bounded model capture');
    const text = fs.readFileSync(file, 'utf8');
    if (self) selfTest(text);
    else console.log(JSON.stringify(verify(text)));
}
