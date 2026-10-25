'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');

function crc(bytes) {
    let value = 0xffffffff;
    for (const byte of bytes) {
        value ^= byte;
        for (let bit = 0; bit < 8; ++bit)
            value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
    }
    return (value ^ 0xffffffff) >>> 0;
}
function fixture(config) {
    const n = Buffer.from(config.challenge, 'hex'), length = config.mpdu_bytes;
    const ack = config.fixture === 'ack', aggregate = length === 3869;
    function data(at) {
        if (!at) return 8;
        if ((at >= 4 && at < 10) || (at >= 16 && at < 22)) return 255;
        if (at === 10) return 2;
        if (at >= 11 && at < 16) return n[at - 11];
        if (at === 22) return (n[0] << 4) & 255;
        if (at === 23) return ((n[0] >>> 4) | (n[1] << 4)) & 255;
        return at >= 24 ? n[(at - 24) % 14] : 0;
    }
    function amsdu(at) {
        if (!at) return 0x88;
        if (at >= 4 && at < 22) {
            const address = Math.floor((at - 4) / 6), pos = (at - 4) % 6;
            return pos ? n[pos - 1] ^ (pos === 5 ? [128, 0, 64][address] : 0) : 2;
        }
        if (at === 22 || at === 23) return data(at);
        if (at === 24) return 0xa0;
        if (at < 26) return 0;
        let offset = at - 26;
        if (offset === 2318 || offset === 2319) return 0;
        const size = offset < 2320 ? 2304 : 1505;
        if (offset >= 2320) offset -= 2320;
        if (offset < 12) return amsdu(4 + offset);
        if (offset === 12) return size >>> 8;
        if (offset === 13) return size & 255;
        return n[(offset - 14) % 14];
    }
    const m = Buffer.alloc(length);
    for (let at = 0; at < length - 4; ++at) {
        if (ack) {
            if (at === 2) {
                const ndbps = config.mcs === 0 ? 24 : config.mcs <= 2 ? 48 : 96;
                m[at] = 10 + 26 + 4 * Math.ceil((22 + 8 * 14) / ndbps);
            } else m[at] = at < 24 ? amsdu(at) : at === 24 ? n[6] & 15 :
                at === 25 ? 0 : n[(at - 26) % 14];
        } else m[at] = aggregate ? amsdu(at) : data(at);
    }
    m.writeUInt32LE(crc(m.subarray(0, -4)), length - 4);
    return m;
}
function ackFrame(m) {
    const ack = Buffer.alloc(14);
    ack[0] = 0xd4;
    m.copy(ack, 4, 10, 16);
    ack.writeUInt32LE(crc(ack.subarray(0, 10)), 10);
    return ack;
}
function sigValid(sig, length) {
    if (sig[0] >= 8 || sig.readUInt16LE(1) !== length ||
        (sig[3] & 254) !== 6 || (sig[4] & 3) || (sig[5] & 252)) return false;
    let value = 255;
    for (let bit = 0; bit < 34; ++bit) {
        const feedback = ((value >>> 7) ^ (sig[bit >>> 3] >>> (bit & 7))) & 1;
        value = ((value << 1) & 255) ^ (feedback ? 7 : 0);
    }
    value ^= 255;
    for (let i = 0; i < 8; ++i)
        if (((sig[(34 + i) >>> 3] >>> ((34 + i) & 7)) & 1) !== ((value >>> (7 - i)) & 1))
            return false;
    return true;
}
function endpoint(raw, json, config, role, m) {
    assert.equal(raw.length, 2092, 'incomplete raw HT report');
    const u32 = at => raw.readUInt32LE(at), u16 = at => raw.readUInt16LE(at);
    const mode = config.fixture === 'ack' ? (role === 'tx' ? 28 : 27) : (role === 'tx' ? 24 : 25);
    assert.equal(u32(0), 0x31585243);
    assert.equal(raw[2000], mode);
    assert.equal(raw[2001], config.channel);
    assert.equal(u32(2048), 0x31504e53);
    assert.equal(raw.subarray(2072, 2086).toString('hex'), config.challenge, 'stale raw nonce');
    assert.equal(raw[2086], mode);
    assert.equal(raw[2087], config.channel);
    const generation = (Buffer.from(config.challenge, 'hex').readUInt32LE(0) | 1) >>> 0;
    assert.equal(u32(2088), generation);
    const available = u32(2052) === 1 && !u32(2056) && (u32(2068) & 15) === 15 &&
        u32(32) === 0x4f505258 && u32(64) === generation;
    assert.equal(json.snapshot?.valid, available, 'raw/JSON snapshot availability differs');
    assert.equal(json.capture_available, available);
    if (!available) {
        assert.equal(json.fresh_ht_rx, null);
        assert.equal(json.rx_captures, null);
        assert.equal(json.tx_submitted, null);
        return {available: false};
    }
    assert.equal(json.challenge, config.challenge);
    assert.equal(json.channel, config.channel);
    assert.equal(json.mcs, config.mcs);
    assert.equal(json.role, role);
    assert.equal(json.error, u32(8));
    assert.equal(json.fw_error, u32(44));
    assert.equal(json.rx_captures, u32(524));
    const completed = !u32(8) && u32(36) === 10;
    if (completed) assert.equal(u32(4), 8, 'successful operation has no stop-stage evidence');
    const submitted = completed && u32(1096) > 0;
    assert.equal(json.tx_submitted, submitted);
    if (submitted) {
        assert.equal(u32(1092), 1, 'missing command attempt');
        assert.equal(u32(1988), m.length + 6, 'submitted command length differs');
    }
    const delivered = role === 'rx' && completed && u32(680) === 0x31524448 &&
        u32(684) === m.length && u32(688) === config.mcs &&
        u32(692) === m.readUInt32LE(m.length - 4) && u32(1996) > 0 &&
        u32(1088) === m.length + 18 && raw[1119] === config.mcs &&
        raw[1120] === (0xc0 | config.channel) && raw[1121] === 1 &&
        raw.subarray(1122, 1122 + Math.min(m.length, 510)).equals(m.subarray(0, 510));
    assert.equal(json.fresh_ht_rx, delivered, 'raw/JSON delivered fixture differs');
    const captured = u32(520) === 0x31525448 && u32(524) > 0;
    const capture = raw.subarray(544, 544 + Math.min(m.length, 96));
    const correlated = captured && capture.subarray(10, 16).equals(m.subarray(10, 16)) &&
        capture.subarray(22, 24).equals(m.subarray(22, 24));
    const trace = captured && m.length <= 96 && u16(528) === m.length + 6 &&
        (u16(530) & 3) === 2 && raw[538] === config.mcs &&
        sigValid(raw.subarray(538, 544), m.length) && capture.equals(m);
    assert.equal(json.fresh_ht_trace, role === 'rx' && completed && trace);
    if (role === 'rx') {
        rejectedCopy(raw, json, config);
        rejectedBaseline(raw, json, config);
        rejectedCandidate(raw, json, config);
    }
    const corrupt = correlated && (u16(528) !== m.length + 6 ||
        !capture.equals(m.subarray(0, 96)) || (u16(530) & 3) !== 2 ||
        raw[538] !== config.mcs || !sigValid(raw.subarray(538, 544), m.length));
    let returned = false, responses = null;
    if (config.fixture === 'ack') {
        if (role === 'tx') {
            returned = completed && submitted && u32(1000) === 0x314b4341 &&
                u32(1004) === 1 && u32(1008) === 1 && u32(1012) === 1 &&
                u32(720) === 0x32524b41 && u16(724) === 1 &&
                (u16(730) & 3) === 1 && raw.subarray(732, 746).equals(ackFrame(m));
            assert.equal(json.fresh_return_ack, returned, 'raw/JSON returned ACK differs');
        } else {
            responses = u32(696);
            assert.equal(json.ack_responses_started, responses);
            assert.equal(json.ack_response_errors, u32(700));
            assert.equal(json.response_configured, u32(704) !== 0);
        }
    }
    return {available: true, completed, submitted, delivered, captured, correlated, corrupt,
        returned_ack: returned, responses, trace, error: u32(8), fw_error: u32(44)};
}
function witness(raw, json, config, m) {
    assert.equal(raw.length, 320);
    assert.equal(raw.readUInt32LE(0), 0x46363255);
    assert.deepEqual(raw.subarray(4, 8), Buffer.from([1, 0, 64, 0]));
    assert.equal(raw.readUInt32LE(12), 3);
    assert.equal(raw.readUInt32LE(60), crc(raw.subarray(0, 60)));
    assert.equal(raw.subarray(16, 30).toString('hex'), config.challenge);
    assert.equal(raw[30], config.fixture === 'ack' ? 16 : 15);
    assert.equal(raw[31], 6);
    assert.equal(json.build, raw.readUInt32LE(8).toString(16).padStart(8, '0'));
    if (config.witness_build) assert.equal(json.build, config.witness_build);
    const t = raw.subarray(64), u = at => t.readUInt32LE(at);
    assert.equal(u(0), 0x31504843);
    assert.equal(u(20), raw[30]);
    assert.equal(u(24), 0x31575448);
    assert.equal(u(28), 1);
    if (config.witness_polling) assert.ok(u(120) > 0, 'required read-only monitor polling is missing');
    const available = u(84) === 1 && u(88) === 0;
    assert.equal(json.available, available);
    if (!available) {
        assert.equal(json.frames, null);
        assert.equal(json.acks, null);
        assert.equal(json.error, u(88));
        assert.equal(json.cleanup, u(84));
        return {available: false, error: u(88), cleanup: u(84)};
    }
    assert.equal(json.available, true);
    assert.equal(json.challenge, config.challenge);
    assert.equal(json.channel, 6);
    for (const [key, at] of [['frames', 56], ['acks', 60], ['candidates', 40],
        ['truncated', 44], ['bad_payload', 48], ['bad_fcs', 52], ['mpdu_bytes', 72],
        ['fcs', 64], ['ack_fcs', 68]]) assert.equal(json[key], u(at), `witness ${key}`);
    if (u(56)) {
        assert.equal(u(72), m.length);
        assert.equal(u(64), m.readUInt32LE(m.length - 4));
        assert.deepEqual(t.subarray(128, 128 + Math.min(m.length, 64)), m.subarray(0, 64));
    }
    if (u(60)) assert.deepEqual(t.subarray(192, 206), ackFrame(m));
    return {available: true, frames: u(56), acks: u(60),
        partial: u(40) > 0 && u(44) > 0, bad_fcs: u(52), bad_payload: u(48)};
}
function sourceWitness(raw, json, config, m) {
    if (config.witness_build) assert.equal(json.build, config.witness_build);
    const passiveConfig = {...config, fixture: 'data'};
    const decoded = endpoint(raw, json, passiveConfig, 'rx', fixture(passiveConfig));
    if (!decoded.available || !decoded.completed) return {available: false};
    const u = at => raw.readUInt32LE(at), s = at => raw.readUInt16LE(at);
    assert.equal(u(1856), 0x31534f48, 'not the receive-only observer image');
    assert.equal(json.observer_only, true);
    assert.equal(u(1092), 0, 'passive observer attempted transmission');
    assert.equal(u(1096), 0);
    assert.equal(u(1988), 0);
    assert.equal(u(704), 0, 'passive observer configured ACK responses');
    assert.equal(u(696), 0);
    assert.equal(u(700), 0);
    const phy = u(520) === 0x31525448 && u(524) > 0 &&
        s(528) === m.length + 6 && (s(530) & 3) === 2 && raw[538] === config.mcs &&
        sigValid(raw.subarray(538, 544), m.length);
    const frames = phy && (m.length <= 96 ?
        raw.subarray(544, 544 + m.length).equals(m) : decoded.delivered);
    const count = u(1836);
    assert.equal(json.passive_ack_count, count);
    if (count) {
        assert.equal(u(1832), 0x31574148);
        assert.equal(config.fixture, 'ack');
        assert.deepEqual(raw.subarray(1840, 1854), ackFrame(m));
        assert.equal(json.passive_ack_mpdu, raw.subarray(1840, 1854).toString('hex'));
    } else {
        assert.equal(u(1832), 0);
        assert.equal(json.passive_ack_mpdu, null);
    }
    return {available: true, kind: 'source', frames: frames ? 1 : 0, acks: count,
        full_frame_archive: m.length <= 96, phy_rate_observed: !!frames};
}
function verify(config, tx, rx, rawTx, rawRx, witnessJson, witnessRaw) {
    assert.match(config.challenge, /^[0-9a-f]{28}$/);
    assert.ok(['data', 'ack'].includes(config.fixture));
    assert.ok(Number.isInteger(config.mpdu_bytes) && (config.mpdu_bytes === 3869 ||
        (config.mpdu_bytes >= 28 && config.mpdu_bytes <= 2332)));
    assert.ok(Number.isInteger(config.mcs) && config.mcs >= 0 && config.mcs <= 7);
    assert.ok(Number.isInteger(config.channel) && config.channel >= 1 && config.channel <= 11);
    const nonce = Buffer.from(config.challenge, 'hex');
    assert.equal(nonce[13], config.mcs);
    assert.equal(nonce.readUInt16LE(11), config.mpdu_bytes);
    if (config.fixture === 'ack') {
        assert.equal(config.mpdu_bytes, 60);
        assert.ok((nonce[6] & 15) <= 7);
    }
    const m = fixture(config);
    const txEvidence = rawTx ? endpoint(rawTx, tx, config, 'tx', m) : {available: false};
    const rxEvidence = rawRx ? endpoint(rawRx, rx, config, 'rx', m) : {available: false};
    const external = witnessRaw ? (config.witness_kind === 'source' ?
        sourceWitness(witnessRaw, witnessJson, config, m) :
        witness(witnessRaw, witnessJson, config, m)) : {available: false};
    const accepted = txEvidence.available && rxEvidence.available && txEvidence.submitted &&
        rxEvidence.delivered && (config.fixture !== 'ack' ||
        (txEvidence.returned_ack && rxEvidence.trace && rxEvidence.responses === 1 &&
         rx.response_configured && rx.ack_response_errors === 0));
    let classification;
    if (config.control === 'omit-tx') {
        assert.equal(rawTx, null);
        assert.equal(external.available, true, 'negative control requires completed external observation');
        assert.equal(rxEvidence.available, true);
        assert.ok(!external.frames && !rxEvidence.delivered, 'negative control contains fresh TX');
        classification = 'intentional_no_transmission';
    } else if (config.control === 'omit-rx') {
        assert.equal(rawRx, null);
        assert.equal(txEvidence.returned_ack, false);
        classification = external.frames ? 'witnessed_tx_without_receiver_or_ack' : 'unwitnessed_control';
    } else if (!txEvidence.available || !rxEvidence.available) classification = 'evidence_unavailable';
    else if (accepted) classification = 'delivered';
    else if (rxEvidence.corrupt) classification = 'corrupt_correlated_capture';
    else if (config.fixture === 'ack' && rxEvidence.delivered && !txEvidence.returned_ack)
        classification = external.acks ? 'witnessed_ack_not_returned_to_tx' :
            rxEvidence.responses ? 'ack_started_but_unwitnessed' : 'missing_ack_response';
    else if (config.fixture === 'ack' && rxEvidence.delivered) classification = 'ack_evidence_inconsistent';
    else if (!rxEvidence.delivered && external.frames) classification = 'receiver_missed_witnessed_frame';
    else if (rxEvidence.captured && !rxEvidence.correlated) classification = 'unrelated_capture_tx_rx_unresolved';
    else if (external.partial) classification = 'partial_witness_tx_integrity_unresolved';
    else classification = 'transmission_reception_unresolved';
    return {mode: 'ht-evidence-pair', challenge: config.challenge, classification, accepted: !!accepted,
        fresh_ht_rx: rxEvidence.available ? rxEvidence.delivered : null,
        tx_over_air_confirmed: rxEvidence.delivered === true || external.frames > 0,
        independent_witness: external, tx: txEvidence, rx: rxEvidence,
        ht_interoperability_qualified: false, sifs_timing_qualified: false};
}
function inspect(prefix) {
    const config = JSON.parse(fs.readFileSync(`${prefix}-config.json`));
    if (!config.fixture) {
        const role = config.control === 'omit-tx' ? 'rx' : 'tx';
        const mode = fs.readFileSync(`${prefix}-${role}.bin`)[2000];
        config.fixture = mode === 27 || mode === 28 ? 'ack' : 'data';
    }
    const read = role => JSON.parse(fs.readFileSync(`${prefix}-${role}.json`));
    const raw = role => fs.readFileSync(`${prefix}-${role}.bin`);
    const hasWitness = fs.existsSync(`${prefix}-witness.bin`);
    return verify(config, config.control === 'omit-tx' ? null : read('tx'),
        config.control === 'omit-rx' ? null : read('rx'),
        config.control === 'omit-tx' ? null : raw('tx'),
        config.control === 'omit-rx' ? null : raw('rx'),
        hasWitness ? read('witness') : null, hasWitness ? raw('witness') : null);
}
function rejectedBaseline(raw, json, config) {
    const u = at => raw.readUInt32LE(at);
    const available = [25, 27].includes(raw[2000]) && config.mpdu_bytes === 60 &&
        u(1520) === 0x31425248;
    if (config.require_reject_baseline !== undefined)
        assert.equal(typeof config.require_reject_baseline, 'boolean');
    if (config.require_reject_baseline)
        assert.ok(available, 'required rejected-frame baseline is missing');
    if (!available) {
        if (json.ht_reject_baseline) assert.equal(json.ht_reject_baseline.available, false);
        return {available: false};
    }
    const baseline = {available: true, generation: u(1524),
        maccontrol_before: u(1528), maccontrol_after: u(1532),
        begin_us: u(1536), end_us: u(1540), raw: raw.subarray(1544, 1610).toString('hex'),
        shm_copy_freshness_proven: false};
    for (const [key, value] of Object.entries(baseline))
        assert.deepEqual(json.ht_reject_baseline?.[key], value, `raw/JSON baseline ${key} differs`);
    assert.equal(baseline.generation, u(64), 'baseline generation differs');
    assert.equal(baseline.maccontrol_before & 3, 0, 'baseline MAC/PSM was active before read');
    assert.equal(baseline.maccontrol_after & 3, 0, 'baseline MAC/PSM was active after read');
    assert.equal(u(1800), 0x31545448, 'baseline requires valid receive timing');
    assert.equal(u(1804), baseline.generation, 'baseline receive timing generation differs');
    assert.equal(json.ht_timing?.valid, true);
    assert.equal(json.ht_timing.listen_begin_us, u(1808));
    assert.ok(baseline.begin_us <= baseline.end_us && baseline.end_us <= u(1808),
        'baseline must precede the receive window');
    assert.equal(raw.readUInt16LE(1610), 0, 'baseline reserved word differs');
    assert.ok(u(1088) <= 1520 - 1104, 'baseline overlaps retained packet');
    return baseline;
}
function rejectedCopy(raw, json, config) {
    const s = at => raw.readUInt16LE(at), u = at => raw.readUInt32LE(at);
    const available = [25, 27].includes(raw[2000]) && u(1040) === 0x31504348;
    if (config.require_reject_copy !== undefined)
        assert.equal(typeof config.require_reject_copy, 'boolean');
    if (config.require_reject_copy) assert.ok(available, 'required rejected-copy trace is missing');
    if (!available) {
        if (json.ht_reject_copy) assert.equal(json.ht_reject_copy.available, false);
        return {available: false};
    }
    const copy = {available: true, count: s(1044), cause: s(1046),
        time_us: u(1048), length: s(1052), stream_threshold: s(1054),
        before: Array.from({length: 5}, (_, i) => s(1056 + 2*i)),
        after: Array.from({length: 5}, (_, i) => s(1066 + 2*i)),
        shm_copy_freshness_proven: false};
    for (const [key, value] of Object.entries(copy))
        assert.deepEqual(json.ht_reject_copy?.[key], value, `raw/JSON rejected-copy ${key} differs`);
    assert.ok(copy.count <= 1, 'first-rejection trace cannot contain multiple records');
    const committed = copy.count === 1;
    if (committed) {
        assert.ok([2, 3].includes(copy.cause));
        assert.ok(copy.length <= 66);
        assert.ok([64, 128, 384].includes(copy.stream_threshold));
    }
    const correlated = committed && u(1640) === 0x32464848 && s(1644) === copy.count &&
        s(1646) === copy.cause && u(1648) === copy.time_us && s(1652) === copy.length;
    return {...copy, committed, record_correlated: correlated,
        status: !committed ? 'uncommitted' : correlated ? 'bound_to_rejection' : 'record_mismatch',
        below_stream_threshold: correlated ? copy.length < copy.stream_threshold : null,
        copy_registers_stable: correlated ? copy.before.every((v, i) => v === copy.after[i]) : null};
}
function rejectedCandidate(raw, json, config) {
    const s = at => raw.readUInt16LE(at), u = at => raw.readUInt32LE(at);
    const available = [25, 27].includes(raw[2000]) && config.mpdu_bytes === 60 &&
        u(1216) === 0x31435048;
    if (config.require_candidate_trace !== undefined)
        assert.equal(typeof config.require_candidate_trace, 'boolean');
    if (config.require_candidate_trace) assert.ok(available, 'required per-candidate trace is missing');
    if (!available) {
        if (json.ht_candidate) assert.equal(json.ht_candidate.available, false);
        return {available: false};
    }
    const candidate = {available: true, sequence: s(1220), start_us: u(1222),
        length: s(1226), status1: s(1228), status2: s(1230), phy: s(1232),
        copy: Array.from({length: 5}, (_, i) => s(1234 + 2*i)),
        raw: raw.subarray(1244, 1310).toString('hex'), cause: s(1310),
        reject_us: u(1312), reject_length: s(1316), committed: s(1318),
        shm_copy_freshness_proven: false};
    for (const [key, value] of Object.entries(candidate))
        assert.deepEqual(json.ht_candidate?.[key], value, `raw/JSON candidate ${key} differs`);
    assert.ok(u(1088) <= 1216 - 1104, 'candidate trace overlaps retained packet');
    assert.ok(candidate.committed <= 1, 'invalid candidate commit');
    const copy = rejectedCopy(raw, json, config);
    assert.equal(candidate.committed, copy.committed ? 1 : 0, 'candidate/rejection commit differs');
    if (candidate.committed) {
        assert.equal(copy.record_correlated, true, 'candidate lacks bound rejection');
        assert.equal(candidate.cause, copy.cause);
        assert.equal(candidate.reject_us, copy.time_us);
        assert.equal(candidate.reject_length, copy.length);
        assert.ok(((candidate.reject_us - candidate.start_us) >>> 0) < 0x80000000,
            'candidate snapshot follows rejection');
    }
    return {...candidate, record_correlated: !!candidate.committed};
}
function diagnose(raw, json, config, observer = false) {
    const m = fixture(config), passive = observer ? {...config, fixture: 'data'} : config;
    const role = raw[2000] === 24 || raw[2000] === 28 ? 'tx' : 'rx';
    const e = endpoint(raw, json, passive, role, observer ? fixture(passive) : m);
    if (!e.available) return {available: false};
    const u = at => raw.readUInt32LE(at), s = at => raw.readUInt16LE(at);
    let f2 = {available: false};
    if (u(1860) === 0x31463248) {
        const count = u(1864);
        assert.equal(json.ht_f2?.available, true);
        assert.equal(json.ht_f2.correlated, count);
        f2 = {available: true, correlated: count};
        if (count) {
            const bytes = u(1868), length = bytes - 18;
            assert.ok(bytes >= 42 && bytes <= 3916);
            const prefix = raw.subarray(1888, 1888 + Math.min(length, 64));
            const metadata = raw.subarray(1884, 1888);
            assert.deepEqual(prefix.subarray(10, 16), m.subarray(10, 16));
            assert.deepEqual(prefix.subarray(22, 24), m.subarray(22, 24));
            assert.equal(json.ht_f2.packet_bytes, bytes);
            assert.equal(json.ht_f2.elapsed_us, u(1872));
            assert.equal(json.ht_f2.calculated_fcs, u(1876));
            assert.equal(json.ht_f2.received_fcs, u(1880));
            assert.equal(json.ht_f2.metadata, metadata.toString('hex'));
            assert.equal(json.ht_f2.prefix, prefix.toString('hex'));
            if (length <= 64) {
                assert.equal(u(1876), crc(prefix.subarray(0, length - 4)));
                assert.equal(u(1880), prefix.readUInt32LE(length - 4));
            }
            Object.assign(f2, {packet_bytes: bytes, elapsed_us: u(1872),
                fcs_valid: u(1876) === u(1880),
                prefix_matches: length === m.length && prefix.equals(m.subarray(0, 64)),
                metadata_matches: metadata[1] === config.mcs &&
                    metadata[2] === (0xc0 | config.channel) && metadata[3] === 1,
                full_bytes_archived: length <= 64});
        }
    } else if (json.ht_f2) {
        assert.equal(json.ht_f2.available, false);
        assert.equal(json.ht_f2.correlated, null);
    }
    const sig = raw.subarray(648, 654), earlyLength = sig.readUInt16LE(1);
    const early = {available: u(640) === 0x33445448};
    assert.equal(json.ht_diag_valid, early.available);
    for (const [key, at] of [['early_candidates', 644], ['early_phy', 646],
        ['early_initial_count', 654], ['early_final_count', 656], ['early_status1', 658],
        ['early_status2', 660], ['early_outcome', 662]]) assert.equal(json[key], s(at));
    assert.equal(json.early_sig, sig.toString('hex'));
    assert.equal(json.early_ht20_sig_valid, early.available && sigValid(sig, earlyLength));
    if (early.available) Object.assign(early, {candidates: s(644), outcome: s(662),
        final_count: s(656), matches_requested_sig: s(644) > 0 && sig[0] === config.mcs &&
            sigValid(sig, config.mpdu_bytes), identity_correlated: false});
    const copy = role === 'rx' ? rejectedCopy(raw, json, config) : {available: false};
    const baseline = role === 'rx' ? rejectedBaseline(raw, json, config) : {available: false};
    const candidateTrace = role === 'rx' ? rejectedCandidate(raw, json, config) : {available: false};
    let rejection = {available: false};
    if ((!observer && config.fixture === 'ack') ||
        (copy.available && config.fixture === 'ack')) {
        const valid = u(1640) === 0x32464848, count = s(1644), length = s(1652);
        assert.equal(json.failure_record_valid, valid);
        assert.equal(json.failure_count, count);
        assert.equal(json.failure_length, length);
        assert.equal(json.failure_cause, s(1646));
        if (copy.available) {
            assert.equal(json.failure_time_us, u(1648));
            for (const [key, at] of [['failure_status1',1654],['failure_status2',1656],
                ['failure_phy',1658],['failure_final_length',1792],
                ['failure_final_status1',1794],['failure_final_status2',1796],
                ['failure_final_phy',1798]]) assert.equal(json[key],s(at));
        }
        if (valid && count) {
            assert.ok(length <= 66);
            const captured = raw.subarray(1660, 1660 + length);
            const repeated = raw.subarray(1726, 1726 + length);
            const complete = length === 66, body = captured.subarray(6);
            const candidate = complete && captured[0] === config.mcs &&
                sigValid(captured.subarray(0, 6), 60) &&
                captured.subarray(10, 22).equals(m.subarray(4, 16));
            const fcsValid = complete && crc(body.subarray(0, 56)) === body.readUInt32LE(56);
            assert.equal(json.failure_raw, captured.toString('hex'));
            assert.equal(json.failure_repeated_raw, repeated.toString('hex'));
            assert.equal(json.failure_candidate_match, candidate);
            assert.equal(json.failure_fcs_valid, fcsValid);
            assert.equal(json.failure_raw_repeat_match, !!length && captured.equals(repeated));
            rejection = {available: true, count, cause: s(1646),
                identity_correlated: candidate && body.subarray(22, 24).equals(m.subarray(22, 24)),
                fcs_valid: fcsValid, repeat_matches: captured.equals(repeated),
                matches_expected_bytes: complete && body.equals(m),
                shm_copy_freshness_proven: false};
            for (const prior of [baseline, candidateTrace].filter(record =>
                record.available && (record !== candidateTrace || record.record_correlated))) {
                const initial = Buffer.from(prior.raw, 'hex'), unchanged = [], changed = [],
                    unchangedIncorrectMac = [];
                for (let i = 0; i < captured.length; ++i) {
                    (captured[i] === initial[i] ? unchanged : changed).push(i);
                    if (i >= 6 && captured[i] === initial[i] && captured[i] !== m[i - 6])
                        unchangedIncorrectMac.push(i - 6);
                }
                prior.rejection_comparison = {length, unchanged_offsets: unchanged,
                    changed_offsets: changed, unchanged_incorrect_mac_offsets: unchangedIncorrectMac,
                    shm_copy_freshness_proven: false};
            }
        }
    }
    let stage = 'no_correlated_capture';
    if (e.delivered || (observer && sourceWitness(raw, json, config, m).frames))
        stage = 'fixture_delivered';
    else if (f2.correlated) stage = 'correlated_f2_not_accepted';
    else if (rejection.identity_correlated) stage = 'rxe_rejection_with_correlated_shm';
    else if (e.trace && e.correlated) stage = 'controller_ht_capture_without_correlated_f2';
    else if (early.matches_requested_sig) stage = 'uncorrelated_ht_phy_candidate';
    if (role === 'tx') stage = e.returned_ack ? 'returned_ack_verified' :
        e.submitted ? 'tx_submitted_only' : 'tx_not_submitted';
    return {available: true, stage, f2, early, rejection,
        ...(copy.available ? {copy_trace: copy} : {}),
        ...(baseline.available ? {baseline} : {}),
        ...(candidateTrace.available ? {candidate_trace: candidateTrace} : {})};
}
module.exports = {crc, fixture, ackFrame, endpoint, witness, sourceWitness, verify, inspect, diagnose};
if (require.main === module) {
    try {
        const detailed = process.argv[2] === '--diagnose';
        assert.equal(process.argv.length, detailed ? 4 : 3,
            'Usage: node ht_verify.cjs [--diagnose] EVIDENCE_PREFIX');
        const prefix = process.argv[detailed ? 3 : 2], result = inspect(prefix);
        if (detailed) {
            const config = JSON.parse(fs.readFileSync(`${prefix}-config.json`));
            result.diagnosis = {};
            for (const role of ['tx', 'rx', ...(config.witness_kind === 'source' ? ['witness'] : [])]) {
                if (config.control === `omit-${role}`) continue;
                result.diagnosis[role] = diagnose(fs.readFileSync(`${prefix}-${role}.bin`),
                    JSON.parse(fs.readFileSync(`${prefix}-${role}.json`)), config, role === 'witness');
            }
        }
        console.log(JSON.stringify(result));
        process.exitCode = result.accepted ? 0 : 1;
    } catch (error) {
        console.error(`HT evidence verification failed: ${error.message}`);
        process.exitCode = 2;
    }
}
