'use strict';
const hex = b => Array.from(b, v => v.toString(16).padStart(2, '0')).join('');
function unhex(s) {
    if (!/^(?:[0-9a-f]{2})*$/i.test(s)) throw Error('Invalid packet hex');
    return Uint8Array.from(s.match(/../g) || [], v => parseInt(v, 16));
}
const defaultKey = unhex('d4f1bb3a20290759f0bcffabcf4e6901');
const xtime = x => ((x << 1) ^ (x & 128 ? 0x11b : 0)) & 255;
function multiply(a, b) {
    let r = 0;
    for (; b; b >>= 1, a = xtime(a)) if (b & 1) r ^= a;
    return r;
}
const sbox = Uint8Array.from({length: 256}, (_, x) => {
    let y = x ? 1 : 0;
    for (let i = 0; i < 254 && x; i++) y = multiply(y, x);
    let s = y ^ 0x63;
    for (let i = 1; i <= 4; i++) s ^= ((y << i) | (y >> (8 - i))) & 255;
    return s;
});
// AES-CTR in plain JS also works on a device's local HTTP origin.
function expandKey(key) {
    if (![16, 32].includes(key.length)) throw Error('AES key must be 16 or 32 bytes');
    const nk = key.length / 4, rounds = nk + 6, w = new Uint8Array(16 * (rounds + 1));
    w.set(key);
    let rc = 1;
    for (let i = nk; i < w.length / 4; i++) {
        let t = Array.from(w.slice(4 * i - 4, 4 * i));
        if (i % nk === 0) {
            t = [sbox[t[1]] ^ rc, sbox[t[2]], sbox[t[3]], sbox[t[0]]];
            rc = xtime(rc);
        } else if (nk === 8 && i % nk === 4) t = t.map(x => sbox[x]);
        for (let j = 0; j < 4; j++) w[4 * i + j] = w[4 * (i - nk) + j] ^ t[j];
    }
    return {w, rounds};
}
function aes(a, input) {
    let s = input.map((v, i) => v ^ a.w[i]);
    for (let r = 1; r <= a.rounds; r++) {
        const t = new Uint8Array(16);
        for (let c = 0; c < 4; c++)
            for (let j = 0; j < 4; j++) t[j + 4 * c] = sbox[s[j + 4 * ((c + j) & 3)]];
        if (r !== a.rounds) for (let c = 0; c < 16; c += 4) {
            const v = t[c] ^ t[c + 1] ^ t[c + 2] ^ t[c + 3], first = t[c];
            for (let j = 0; j < 4; j++) t[c + j] ^= v ^ xtime(t[c + j] ^ (j === 3 ? first : t[c + j + 1]));
        }
        s = t.map((v, i) => v ^ a.w[r * 16 + i]);
    }
    return s;
}
function decrypt(key, frame) {
    const a = expandKey(key), iv = new Uint8Array(16), out = frame.slice(16);
    iv.set(frame.slice(8, 12)); iv.set(frame.slice(4, 8), 8);
    for (let i = 0; i < out.length; i += 16) {
        const stream = aes(a, iv);
        for (let j = 0; j < 16 && i + j < out.length; j++) out[i + j] ^= stream[j];
        for (let j = 15; j >= 12; j--) if (++iv[j]) break;
    }
    return out;
}
function parseKeys(text) {
    const keys = [{name: 'LongFast', key: defaultKey}];
    for (const line of text.split(/\r?\n/)) {
        if (!line.trim() || line.trim().startsWith('#')) continue;
        const parts = line.trim().split(/\s+/);
        if (parts.length !== 2 || !/^[!-~]{1,31}$/.test(parts[0]) ||
            !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(parts[1]))
            throw Error('Use CHANNEL_NAME BASE64_PSK on each line');
        let key = Uint8Array.from(atob(parts[1]), c => c.charCodeAt(0));
        if (key.length === 1) {
            const n = key[0];
            key = n ? defaultKey.slice() : new Uint8Array();
            if (n) key[15] = (key[15] + n - 1) & 255;
        }
        if (![0, 16, 32].includes(key.length)) throw Error('PSK must select a default key, or contain 16/32 bytes');
        keys.push({name: parts[0], key});
        if (keys.length > 8) throw Error('At most 7 additional keys');
    }
    return keys;
}
function protobuf(bytes) {
    let p = 0;
    const fields = [];
    function varint() {
        let n = 0n;
        for (let i = 0; i < 10; i++) {
            if (p === bytes.length) throw Error('Truncated protobuf');
            const b = bytes[p++];
            if (i === 9 && b > 1) throw Error('Protobuf integer overflow');
            n |= BigInt(b & 127) << BigInt(i * 7);
            if (!(b & 128)) return n;
        }
        throw Error('Invalid protobuf integer');
    }
    while (p < bytes.length) {
        const tag = varint(), number = Number(tag >> 3n), wire = Number(tag & 7n);
        if (!number || number > 0x1fffffff) throw Error('Invalid protobuf field');
        let value;
        if (wire === 0) value = varint().toString();
        else {
            const length = wire === 2 ? Number(varint()) : wire === 5 ? 4 : wire === 1 ? 8 : -1;
            if (length < 0 || !Number.isSafeInteger(length) || length > bytes.length - p)
                throw Error('Invalid protobuf length/wire type');
            const b = bytes.slice(p, p + length);
            p += length;
            value = hex(b);
        }
        fields.push({number, wire, value});
    }
    return fields;
}
const textDecoder = new TextDecoder();
const ports = {1:'TEXT_MESSAGE_APP', 3:'POSITION_APP', 4:'NODEINFO_APP', 5:'ROUTING_APP',
    6:'ADMIN_APP', 8:'WAYPOINT_APP', 10:'DETECTION_SENSOR_APP', 11:'ALERT_APP',
    66:'RANGE_TEST_APP', 67:'TELEMETRY_APP', 70:'TRACEROUTE_APP', 71:'NEIGHBORINFO_APP'};
function interpret(port, bytes) {
    if ([1, 10, 11, 66].includes(port)) return {text: textDecoder.decode(bytes)};
    if (![3, 4, 67].includes(port)) return {payload_hex: hex(bytes)};
    const fields = protobuf(bytes), result = {protobuf: fields};
    for (const f of fields) {
        if (port === 3) {
            if ([1, 2].includes(f.number) && f.wire === 5)
                result[f.number === 1 ? 'latitude' : 'longitude'] =
                    new DataView(unhex(f.value).buffer).getInt32(0, true) * 1e-7;
            if (f.number === 3 && f.wire === 0) result.altitude_m = Number(BigInt.asIntN(32, BigInt(f.value)));
            if (f.number === 14 && f.wire === 0) result.satellites = Number(f.value);
        }
        if (port === 4) {
            if (f.wire === 2 && f.number <= 3)
                result[['', 'node_id', 'long_name', 'short_name'][f.number]] = textDecoder.decode(unhex(f.value));
            if (f.wire === 0 && [5, 7].includes(f.number))
                result[f.number === 5 ? 'hardware_model' : 'role'] = Number(f.value);
        }
        if (port === 67 && [2, 3].includes(f.number) && f.wire === 2) {
            const metrics = protobuf(unhex(f.value));
            const names = f.number === 2 ? ['', 'battery_percent', 'voltage_V', 'channel_util_percent',
                'air_tx_util_percent', 'uptime_s'] : ['', 'temperature_C', 'humidity_percent', 'pressure_hPa'];
            const values = {protobuf: metrics};
            for (const m of metrics) if (names[m.number] && [0, 5].includes(m.wire))
                values[names[m.number]] = m.wire === 0 ? Number(m.value) :
                    new DataView(unhex(m.value).buffer).getFloat32(0, true);
            result[f.number === 2 ? 'device' : 'environment'] = values;
        }
    }
    return result;
}
function decodePacket(p, keys) {
    if (p.crc_error) return {status: 'CRC error: unreliable payload, not decoded'};
    if (!p.mesh) return {status: 'No Meshtastic header; see raw packet'};
    if (p.mesh.channel === 0 && p.mesh.to !== 'ffffffff')
        return {status: 'Possible PKI direct message: private recipient key required'};
    const frame = unhex(p.hex), failures = [];
    for (const c of keys) {
        const hash = [...new TextEncoder().encode(c.name), ...c.key].reduce((a, b) => a ^ b, 0);
        if (!c.key.length && hash !== p.mesh.channel) continue;
        const plain = c.key.length ? decrypt(c.key, frame) : frame.slice(16);
        try {
            const fields = protobuf(plain), wires = [9, 0, 2, 0, 5, 5, 5, 5, 5, 0, 2];
            if (fields.some(f => f.number <= 10 && wires[f.number] !== f.wire)) continue;
            const portField = fields.find(f => f.number === 1), payload = fields.find(f => f.number === 2);
            if (!portField || Number(portField.value) > 511) continue;
            const port = Number(portField.value);
            if (hash !== p.mesh.channel && !ports[port]) continue;
            const data = interpret(port, payload ? unhex(payload.value) : new Uint8Array());
            return {status: 'Decoded candidate (not authenticated)', key_name: c.name,
                channel_name_confirmed: false, hash_matches: hash === p.mesh.channel,
                port, port_name: ports[port] || 'PORT_' + port, data,
                data_protobuf: fields, plaintext_hex: hex(plain)};
        } catch (e) { failures.push(e.message); }
    }
    return {status: 'Encrypted / no valid decode with supplied keys', decode_errors: [...new Set(failures)]};
}
class Capture {
    constructor(limit = 1000) {
        this.limit = limit; this.rows = []; this.last = null; this.previous = null;
        this.run = 1; this.missed = 0; this.evicted = 0; this.total = 0;
        this.restarts = 0; this.channels = new Map();
    }
    ingest(d, now = Date.now()) {
        const restart = this.previous && (d.build_id !== this.previous.build_id ||
            (d.uptime_ms < this.previous.uptime_ms &&
             ((d.uptime_ms - this.previous.uptime_ms) >>> 0) > 0x80000000));
        if (restart) { this.last = null; this.run++; this.restarts++; }
        this.previous = {build_id: d.build_id, uptime_ms: d.uptime_ms};
        const added = [];
        for (const p of [...d.packets].reverse()) {
            const delta = this.last === null ? 1 : (p.sequence - this.last) >>> 0;
            if (!delta || delta >= 0x80000000) continue;
            this.missed += delta - 1;
            const age = (d.uptime_ms - p.uptime_ms) >>> 0;
            const row = {...p, run: this.run, build_id: d.build_id, observed_at: new Date(now).toISOString(),
                estimated_received_at: new Date(now - age).toISOString()};
            this.rows.push(row); added.push(row); this.total++; this.last = p.sequence;
            if (p.mesh && !p.crc_error) {
                const ch = p.mesh.channel, s = this.channels.get(ch) || {count: 0, best: -Infinity};
                s.count++; s.best = Math.max(s.best, p.snr_qdb / 4); s.last = row.estimated_received_at;
                this.channels.set(ch, s);
            }
        }
        if (this.rows.length > this.limit) {
            this.evicted += this.rows.length - this.limit;
            this.rows.splice(0, this.rows.length - this.limit);
        }
        return added;
    }
    clear() { this.rows = []; }
}
if (typeof module !== 'undefined') module.exports = {aes, expandKey, decrypt, parseKeys, protobuf, interpret,
    decodePacket, Capture, unhex, hex};
if (typeof document !== 'undefined') {
    const el = id => document.getElementById(id), capture = new Capture();
    const activityCapture = new Capture();
    let activityLatest = null, activityPending = false;
    let keys = parseKeys(''), initialized = false, busy = false, paused = false, failures = 0, redraw = false;
    const nodes = new Map();
    const hashText = n => n.toString(16).padStart(2, '0');
    async function json(url, options) {
        const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 10000);
        try {
            const r = await fetch(url, {cache: 'no-store', ...options, signal: controller.signal});
            const d = await r.json();
            if (!r.ok) throw Error(d.error || 'HTTP ' + r.status);
            return d;
        } finally { clearTimeout(timer); }
    }
    function visible(p) {
        const f = el('filter').value.trim().replace(/^0x/i, '').toLowerCase();
        return !f || (p.mesh && hashText(p.mesh.channel) === f);
    }
    function row(p) {
        const decoded = decodePacket(p, keys);
        p.decoded = decoded;
        const item = document.createElement('details'), title = document.createElement('summary');
        item.className = 'packet' + (p.crc_error ? ' bad' : '');
        const m = p.mesh;
        title.textContent = '#' + p.sequence + ' | ' + new Date(p.estimated_received_at).toLocaleTimeString() +
            ' | ' + (m ? 'ch 0x' + hashText(m.channel) + ' | !' + m.from + ' > ' +
            (m.to === 'ffffffff' ? 'broadcast' : '!' + m.to) : p.profile) +
            ' | ' + p.length + ' B | RSSI ' + p.rssi_qdb / 4 + ' dBm | SNR ' + p.snr_qdb / 4 +
            ' dB | ' + (decoded.port_name || decoded.status);
        const info = document.createElement('pre');
        info.textContent = JSON.stringify({...p, rssi_dBm: p.rssi_qdb / 4, snr_dB: p.snr_qdb / 4,
            signal_rssi_dBm: p.signal_rssi_qdb / 4,
            hops_used_estimate: m && m.hop_start >= m.hop_limit ? m.hop_start - m.hop_limit : null}, null, 2);
        item.append(title, info); item.hidden = !visible(p);
        return item;
    }
    function render(full = false) {
        if (full) redraw = true;
        if (paused) return;
        const container = el('packets'), allowed = new Set(capture.rows.map(p => p.run + ':' + p.sequence));
        if (redraw) { container.replaceChildren(); nodes.clear(); redraw = false; }
        for (const [id, node] of nodes) if (!allowed.has(id)) { node.remove(); nodes.delete(id); }
        let appended = false;
        for (const p of capture.rows) {
            const id = p.run + ':' + p.sequence;
            if (!nodes.has(id)) { const node = row(p); nodes.set(id, node); container.append(node); appended = true; }
            nodes.get(id).hidden = !visible(p);
        }
        el('history').textContent = capture.rows.length + ' retained / ' + capture.total +
            ' observed in this tab. ' + (capture.rows.length ? '' : 'Waiting for packets.');
        el('channels').replaceChildren();
        for (const [ch, s] of [...capture.channels].sort((a, b) => a[0] - b[0])) {
            const tr = document.createElement('tr');
            for (const v of ['0x' + hashText(ch), s.count, new Date(s.last).toLocaleTimeString(), s.best + ' dB']) {
                const td = document.createElement('td'); td.textContent = v; tr.append(td);
            }
            el('channels').append(tr);
        }
        if (appended && el('follow').value === 'yes') container.lastElementChild?.scrollIntoView({block: 'nearest'});
    }
    function show(d) {
        capture.ingest(d);
        el('received').textContent = d.received;
        el('radioerrors').textContent = d.crc_errors + ' / ' + d.header_errors;
        el('noise').textContent = d.noise_samples ? [d.noise_qdb, d.noise_min_qdb, d.noise_max_qdb]
            .map(q => (q / 4).toFixed(1)).join(' / ') : '-';
        el('overwritten').textContent = d.overwritten;
        el('uptime').textContent = (d.uptime_ms / 1000).toFixed(1) + ' s';
        el('details').textContent = 'IP ' + d.ip + ' | Build ' + d.build_id +
            ' | Network drops ' + d.network_drops + ' | TCP retries ' + d.retransmits;
        el('error').textContent = (d.wifi_error ? 'Wi-Fi error ' + d.wifi_error + '\n' : '') +
            (d.lora_error || d.lora_state !== 1 ? 'LoRa receiver not running: error ' + d.lora_error : '');
        el('message').textContent = 'Live: ' + (d.config.frequency_hz / 1e6).toFixed(3) + ' MHz / SF' +
            d.config.sf + ' / BW' + d.config.bandwidth_khz + ' / ' + d.config.profile +
            ' | Last update ' + new Date().toLocaleTimeString();
        el('warning').textContent = 'Device snapshot: latest 8 packets; packets before opening may be unavailable. ' +
            'Gaps since opening: ' + capture.missed + '. Browser history evicted: ' + capture.evicted +
            '. Device restarts detected: ' + capture.restarts + '. Connection failures: ' + failures + '.\n' +
            'Reception times are estimates from device uptime and this computer clock. ' +
            (d.config.profile !== 'meshtastic' || d.config.frequency_hz !== 869525000 ||
             d.config.sf !== 11 || d.config.bandwidth_khz !== 250 ? 'WARNING: not listening on LongFast.' : '');
        if (!initialized) {
            el('profile').value = d.config.profile; el('frequency').value = (d.config.frequency_hz / 1e6).toFixed(3);
            el('sf').value = d.config.sf; el('bw').value = d.config.bandwidth_khz; initialized = true;
        }
        render();
    }
    function showActivity(d) {
        activityLatest = d;
        activityCapture.ingest({...d, build_id: d.build_id + ':' + d.session,
            packets: d.events.map(e => ({...e, uptime_ms: e.start_ms}))});
        el('activity-start').disabled = activityPending || !!d.running;
        el('activity-stop').disabled = activityPending || !d.running;
        el('apply').disabled = !!d.running;
        el('activity-error').textContent = d.error || d.restore_error ?
            'Survey error ' + d.error + '; restore error ' + d.restore_error : '';
        el('activity-status').textContent = (d.running ? 'SURVEY ACTIVE: ' +
            (d.config.frequency_hz / 1e6).toFixed(6) + ' MHz / SF' + d.config.sf +
            ' / BW' + d.config.bandwidth_khz + ', setting ' + (d.index + 1) + '/65, sweep ' +
            (d.sweep + 1) : 'Survey stopped; normal listening active if radio is healthy') +
            '. Dwell samples: ' + d.sample_count + ', largest sample gap: ' + d.max_gap_ms + ' ms.';
        el('activity-warning').textContent = activityCapture.rows.length + ' events retained in this tab; ' +
            'missed between polls: ' + activityCapture.missed + ', browser evictions: ' + activityCapture.evicted +
            ', device ring overwrites: ' + d.overwritten + '. New survey runs clear device totals. ' +
            (d.active_burst ? 'Energy burst in progress: ' + d.active_burst.duration_ms + ' ms.' : '');
        el('activity-channels').replaceChildren();
        for (const v of d.channels) {
            if (!v[1]) continue;
            const tr = document.createElement('tr');
            for (const value of [(v[0] / 1e6).toFixed(6), v[1] + ' / ' + (v[2] / 1000).toFixed(2),
                (v[8] === null ? '-' : v[8] / 4) + ' / ' + (v[9] === null ? '-' : v[9] / 4),
                v[5], v[6] + ' / ' + v[7], v[4]]) {
                const td = document.createElement('td'); td.textContent = value; tr.append(td);
            }
            el('activity-channels').append(tr);
        }
        const container = el('activity-events');
        const old = new Map([...container.children].map(n => [n.dataset.id, n]));
        const ids = new Set();
        for (const e of activityCapture.rows) {
            const id = e.run + ':' + e.sequence; ids.add(id);
            if (old.has(id)) continue;
            const item = document.createElement('details'), title = document.createElement('summary');
            item.className = 'packet'; item.dataset.id = id;
            title.textContent = '#' + e.sequence + ' | ' + (e.frequency_hz / 1e6).toFixed(6) +
                ' MHz SF' + e.sf + '/BW' + e.bandwidth_khz + ' | ' +
                (e.kind === 1 ? 'Energy ' + e.duration_ms + ' ms' + (e.truncated ? ' [truncated]' : '') :
                    'LoRa packet (no active RSSI burst)') + ' | Peak ' + e.peak_qdb / 4 + ' dBm, ' +
                (e.above_floor_qdb === null ? 'baseline unavailable' : '+' + e.above_floor_qdb / 4 + ' dB vs baseline') +
                ' | Packets ' + e.packets + ', CRC ' + e.crc_errors +
                (e.interval_ms === null ? '' : ' | Observed start gap ' + e.interval_ms +
                    ' ms (' + e.blind_ms + ' ms off frequency)');
            const info = document.createElement('pre');
            info.textContent = JSON.stringify(e, null, 2);
            item.append(title, info); container.append(item);
        }
        for (const [id, node] of old) if (!ids.has(id)) node.remove();
    }
    async function poll() {
        if (!busy) {
            busy = true;
            try { show(await json('/api/status')); showActivity(await json('/api/activity')); }
            catch (e) { failures++; el('error').textContent = 'Connection/status error: ' + e.message; }
            finally { busy = false; }
        }
        setTimeout(poll,1000);
    }
    el('pause').onclick = () => {
        paused = !paused; el('pause').textContent = paused ? 'Resume display' : 'Pause display';
        render();
    };
    el('clear').onclick = () => { capture.clear(); el('packets').replaceChildren(); nodes.clear(); render(); };
    el('filter').oninput = () => render();
    el('loadkeys').onclick = () => {
        try { keys = parseKeys(el('keys').value); el('keys').value = '';
            el('keystatus').textContent = keys.length + ' keys loaded in this tab'; render(true);
        } catch (e) { el('keystatus').textContent = 'Key error: ' + e.message; }
    };
    el('export').onclick = () => {
        const packets = capture.rows.map(p => ({...p, decoded: decodePacket(p, keys)}));
        const data = {exported_at: new Date().toISOString(), missed: capture.missed, evicted: capture.evicted,
            restarts: capture.restarts, connection_failures: failures, packets};
        const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], {type: 'application/json'}));
        const a = document.createElement('a'); a.href = url; a.download = 'pico-lora-packets.json'; a.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    };
    el('settings').addEventListener('submit', async event => {
        event.preventDefault(); if (busy) return;
        busy = true; el('apply').disabled = true;
        try {
            const body = 'frequency_hz=' + Math.round(Number(el('frequency').value) * 1e6) + '&sf=' +
                el('sf').value + '&bandwidth_khz=' + el('bw').value + '&profile=' + el('profile').value;
            const d = await json('/api/config', {method: 'POST', headers: {
                'Content-Type': 'application/x-www-form-urlencoded', 'X-Pico-Request':'1'}, body});
            initialized = false; show(d);
        } catch (e) { el('error').textContent = e.message; }
        finally { busy = false; el('apply').disabled = false; }
    });
    el('profile').onchange = () => {
        const m = el('profile').value === 'meshtastic';
        el('frequency').value = m ? '869.525' : '868.100';
        el('sf').value = m ? '11' : '7'; el('bw').value = m ? '250' : '125';
    };
    async function activityCommand(action) {
        if (activityPending) return;
        activityPending = true;
        el('activity-start').disabled = el('activity-stop').disabled = true;
        el('activity-error').textContent = busy ? 'Waiting for the current status request...' : '';
        while (busy) await new Promise(resolve => setTimeout(resolve, 50));
        busy = true;
        try {
            showActivity(await json('/api/activity', {method: 'POST', headers: {
                'Content-Type': 'application/x-www-form-urlencoded', 'X-Pico-Request':'1'},
                body: 'action=' + action}));
            show(await json('/api/status'));
        } catch (e) { el('activity-error').textContent = e.message; }
        finally {
            busy = false;
            activityPending = false;
            el('activity-start').disabled = !!activityLatest?.running;
            el('activity-stop').disabled = !activityLatest?.running;
        }
    }
    el('activity-start').onclick = () => activityCommand('start');
    el('activity-stop').onclick = () => activityCommand('stop');
    el('activity-export').onclick = () => {
        const data = {exported_at: new Date().toISOString(), latest: activityLatest,
            events: activityCapture.rows, missed: activityCapture.missed, evicted: activityCapture.evicted,
            warning: 'Observed gaps are not device periods; surveys interrupt normal listening.'};
        const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], {type: 'application/json'}));
        const a = document.createElement('a'); a.href = url; a.download = 'pico-868-activity.json'; a.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    };
    poll();
}
