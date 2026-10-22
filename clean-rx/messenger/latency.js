#!/usr/bin/env node
'use strict';
const fs = require('node:fs');
const assert = require('node:assert/strict');

function crc(bytes) {
    let c = 0xffffffff;
    for (const byte of bytes) {
        c ^= byte;
        for (let bit = 0; bit < 8; ++bit) c = (c >>> 1) ^ (c & 1 ? 0xedb88320 : 0);
    }
    return (~c) >>> 0;
}

function delta(end, begin) {
    const value = (end - begin) >>> 0;
    assert(value < 0x80000000, 'Ambiguous or reversed timer interval');
    return value;
}

function distribution(values) {
    if (!values.length) return {n: 0};
    values.sort((a, b) => a - b);
    const rank = p => Math.round(values[Math.ceil(p * values.length) - 1]);
    return {n: values.length, min: Math.round(values[0]), median: rank(.5),
        p95: rank(.95), p99: rank(.99), max: Math.round(values.at(-1))};
}

assert(process.argv.length > 2, 'Usage: node clean-rx/messenger/latency.js LOG.jsonl [...]');
for (const file of process.argv.slice(2)) {
    const rows = fs.readFileSync(file, 'utf8').trim().split('\n').filter(Boolean).map(JSON.parse);
    const clocks = new Map(rows.filter(r => r.test === 'ht20-clock').map(r => [r.role, r]));
    assert.equal(clocks.size, 2, 'Need both local clock calibrations');
    const scale = role => {
        const c = clocks.get(role);
        assert(c.ticks > 0 && c.sof_frames >= 32);
        return 1000 * c.sof_frames / c.ticks;
    };
    const samples = rows.filter(r => r.test === 'ht20-latency');
    const tokens = new Set();
    for (const r of samples) {
        assert(!tokens.has(r.token), 'Repeated token within a campaign');
        tokens.add(r.token);
        assert.equal(r.tx.length, 12);
        assert.equal(r.rx.length, 9);
        if (r.terminal) {
            assert.equal(r.tx[0], r.token);
            assert(r.tx[9] >= 1 && r.tx[9] <= 6);
            assert([1, 2].includes(r.tx[10]));
            assert.equal(r.acked, r.tx[10] === 1);
            delta(r.tx[4], r.tx[3]);
            delta(r.tx[8], r.tx[4]);
            if (r.tx[11] & 1) delta(r.tx[6], r.tx[4]);
        } else assert(!r.acked);
        if (r.delivered) {
            assert.equal(r.rx[0], r.token);
            assert.equal(r.rx[1], r.sender_session);
            if (r.terminal) assert.equal(r.rx[2], r.tx[1]);
            const p = Buffer.from(r.rx_packet_hex, 'hex');
            assert.equal(p.length, 98);
            assert.deepEqual([...p.subarray(15, 18)], [0, 0xc1, 1]);
            const m = p.subarray(18);
            assert.equal(crc(m.subarray(0, 76)), m.readUInt32LE(76));
            assert.equal(m.readUInt32LE(32), 0x3147534d);
            assert.deepEqual([...m.subarray(36, 39)], [1, r.sender_role, 1]);
            assert.equal(m.readUInt32LE(40), r.sender_session);
            assert.equal(m.readUInt32LE(44), r.rx[2]);
            assert.equal(m[52], 2);
            assert.equal(m.readUInt32LE(56), r.token);
            if (r.rx[7] & 2) delta(r.rx[4], r.rx[3]);
            if (r.rx[7] & 4) delta(r.rx[5], r.rx[4]);
            if (r.rx[7] & 8) delta(r.rx[6], r.rx[3]);
        }
    }
    const totals = rows.find(r => r.test === 'ht20-latency-total');
    if (totals) {
        assert.equal(totals.requested, samples.length);
        assert.equal(totals.passed, samples.filter(r => r.acked && r.delivered).length);
    }
    const results = [];
    for (const role of [1, 2]) {
        const selected = samples.filter(r => r.sender_role === role);
        const acked = selected.filter(r => r.acked);
        const first = acked.filter(r => r.tx[9] === 1);
        const delivered = selected.filter(r => r.delivered);
        const tx = (a, end, begin) => distribution(a.map(r =>
            delta(r.tx[end], r.tx[begin]) * scale(role)));
        const rx = (a, end, begin) => distribution(a.map(r =>
            delta(r.rx[end], r.rx[begin]) * scale(3 - role)));
        results.push({role, requested: selected.length, acked: acked.length,
            delivered: delivered.length, unconfirmed: selected.length - acked.length,
            retried: selected.filter(r => r.terminal && r.tx[9] > 1).length,
            first_attempt_rtt_us: tx(first, 8, 4),
            all_acked_rtt_us: tx(acked, 8, 4),
            enqueue_to_ack_us: tx(acked, 8, 3),
            queue_us: tx(selected.filter(r => r.terminal), 4, 3),
            tx_command_us: tx(selected.filter(r => r.terminal && (r.tx[11] & 1)), 6, 4),
            receiver_ack_turnaround_us: rx(delivered.filter(r => r.rx[7] & 2), 4, 3),
            receiver_to_display_us: rx(delivered.filter(r => r.rx[7] & 8), 6, 3)});
    }
    console.log(JSON.stringify({file, complete_campaign: Boolean(totals),
        clocks: [...clocks.values()], results,
        statuses: rows.filter(r => r.serial)}, null, 2));
}
