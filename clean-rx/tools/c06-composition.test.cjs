'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { previousSource, currentSource, expand, runtimeProfiles } = require('./c06-runtime.cjs');
const { matrix } = require('./c01-baseline.cjs');
const root = path.resolve(__dirname, '../..');
const namespaces = /\b(?:TRANSPORT|STATION|PUBLICATION|AGGREGATE|LOOP)\((\w+)\)/g;
const normalize = text => text.replace(namespaces, '$1').replace(/\s+/g, ' ').trim();

function run(command, args, cwd = root) {
    const result = spawnSync(command, args, { cwd, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
        env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' } });
    if (result.error) throw result.error;
    assert.equal(result.status, 0, result.stdout + result.stderr);
    return result.stdout;
}

function writeSource(directory, sources) {
    for (const [file, text] of sources) {
        const destination = path.join(directory, file);
        fs.mkdirSync(path.dirname(destination), { recursive: true });
        fs.writeFileSync(destination, text);
    }
}

test('unmodified controller functions, control flow, storage types and ordering retain C05 bodies', () => {
    const before = expand('main.c', previousSource());
    const sources = currentSource();
    const reviewed = new Map(sources);
    reviewed.set('runtime/d11_start.inc', sources.get('runtime/d11_start.inc').replace(
        '    ((volatile clean_ampdu_dma_state_t *)CLEAN_AMPDU_DMA_STATE_ADDR)->magic = 0u;\n', ''));
    reviewed.set('runtime/shutdown.inc', sources.get('runtime/shutdown.inc').replace(
        '#if CLEAN_RX_AMPDU_DMA\n    ampdu_dma_snapshot(mmio_read32(D11_RX_DMA_STATUS0));\n#endif\n', ''));
    const after = expand('main.c', reviewed);
    const functions = text => [...text.matchAll(/^(?:static [^{;]+?|void cyw43_main\(void\)) \{[\s\S]*?^}/gm)]
        .filter(match => !/\b(?:ampdu_dma_receive|ampdu_dma_snapshot|ampdu_dma_fail)\(/.test(match[0].split('{')[0]))
        .map(match => normalize(match[0]));
    assert.ok(functions(before).length > 50);
    assert.deepEqual(functions(after), functions(before));
    const storage = text => text.replace(namespaces, '$1').match(/^static [^;{\n]+;/gm)
        .filter(line => !line.includes('(')).map(normalize).sort();
    assert.deepEqual(storage(after), storage(before));
    assert.ok(sources.get('main.c').split('\n').length < 100);
    assert.ok(sources.get('psm_ack.h').split('\n').length < 250);
});

test('mutable state references use declared owner namespaces rather than hidden bare globals', () => {
    const sources = currentSource();
    const owners = new Map();
    for (const name of ['runtime/storage.inc', 'runtime/receive.inc']) {
        for (const line of sources.get(name).split('\n').filter(line => line.startsWith('static ') && line.endsWith(';')))
            for (const match of line.matchAll(/\b(TRANSPORT|STATION|PUBLICATION|AGGREGATE)\((\w+)\)/g)) {
                assert.ok(!owners.has(match[2]), `Duplicate mutable storage: ${match[2]}`);
                owners.set(match[2], match[1]);
            }
    }
    assert.ok(owners.size > 40);
    for (const [file, input] of sources) {
        if (!file.endsWith('.inc') && file !== 'main.c') continue;
        const text = input.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\])*"/g, '');
        for (const match of text.matchAll(/\b(TRANSPORT|STATION|PUBLICATION|AGGREGATE)\((\w+)\)/g))
            assert.equal(owners.get(match[2]), match[1], `${file}: wrong state owner for ${match[2]}`);
        const unqualified = text.replace(namespaces, '');
        for (const match of unqualified.matchAll(/\b[A-Za-z_]\w*\b/g)) {
            const prefix = unqualified.slice(0, match.index).trimEnd();
            if (prefix.endsWith('.') || prefix.endsWith('->')) continue;
            assert.ok(!owners.has(match[0]), `${file}: bare mutable global ${match[0]}`);
        }
    }
});

test('PSM tuples and hook targets match C05 except the explicit R06 DMA transition', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c06-psm-'));
    try {
        writeSource(path.join(directory, 'before'), previousSource());
        writeSource(path.join(directory, 'after'), currentSource());
        const probe = path.join(directory, 'labels.c');
        fs.writeFileSync(probe, '#include <stdio.h>\n#include "cyw43_hw.h"\n#include "psm_ack.h"\n' +
            '#define EMIT(name,op,a,b,d) printf("%s %u %u %u %u %u\\n",#name,(unsigned)name,(unsigned)(op),(unsigned)(a),(unsigned)(b),(unsigned)(d));\n' +
            'int main(void) { printf("%u %u\\n",(unsigned)CLEAN_ACK_HOOK,(unsigned)ACK_PROGRAM_END); ACK_PROGRAM(EMIT) return 0; }\n');
        const profiles = [...matrix.map(profile => ({ ...profile, mode: 'diagnostic' })),
            ...runtimeProfiles().map(profile => ({ ...profile, mode: 'runtime' })),
            { id: 'aggregate-interface', mode: 'diagnostic', defines: ['CLEAN_RX_HT_RX=1', 'CLEAN_RX_HT_DATA=1', 'CLEAN_RX_AMPDU_PROBE=1', 'CLEAN_RX_AMPDU_INTERFACE=3', 'CLEAN_RX_AMPDU_HANDOFF=2', 'CLEAN_RX_AMPDU_DMA=1'] },
            { id: 'aggregate-shm', mode: 'diagnostic', defines: ['CLEAN_RX_HT_RX=1', 'CLEAN_RX_HT_DATA=1', 'CLEAN_RX_AMPDU_PROBE=1', 'CLEAN_RX_AMPDU_INTERFACE=3', 'CLEAN_RX_AMPDU_HANDOFF=2', 'CLEAN_RX_AMPDU_SHM=1', 'CLEAN_RX_AMPDU_BA=1'] }];
        for (const [index, profile] of profiles.entries()) {
            const outputs = [];
            for (const side of ['before', 'after']) {
                const executable = path.join(directory, `labels-${side}-${index}`);
                run('cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O0',
                    `-DCLEAN_RX_DIAGNOSTIC=${profile.mode === 'diagnostic' ? 1 : 0}`,
                    ...profile.defines.map(value => `-D${value}`), '-I', path.join(directory, side),
                    probe, '-o', executable]);
                outputs.push(run(executable, []));
            }
            if (profile.id === 'aggregate-interface') {
                const parse = output => {
                    const lines = output.trim().split('\n');
                    const [hook,end] = lines.shift().split(' ').map(Number);
                    const rows = lines.map(line => {
                        const [name,...values] = line.split(' ');
                        const [address,op,a,b,d] = values.map(Number);
                        return {name,address,op,a,b,d};
                    });
                    return {hook,end,rows};
                };
                const old = parse(outputs[0]), current = parse(outputs[1]);
                const tuple = name => {
                    const {op,a,b,d} = current.rows.find(row => row.name === name);
                    return [op,a,b,d];
                };
                assert.deepEqual(tuple('AMPDU_DMA_LENGTH'),[0x3f0,0x1781,0x1800,0x7f4]);
                assert.deepEqual(tuple('AMPDU_IF_ADV_BODY_COPY'),[0x376,0x18c3,0x1004,0x1004]);
                const changed = new Set(['AMPDU_DMA_LENGTH','AMPDU_DMA_LENGTH_ORDINAL',
                    'AMPDU_DMA_LENGTH_BODY','AMPDU_DMA_LENGTH_BODY_RETURN','AMPDU_DMA_LENGTH_FIRST',
                    'AMPDU_IF_ADV_BODY_COPY']);
                const normalizeProgram = program => {
                    const labels = new Map(program.rows.map(row => [row.address,row.name]));
                    labels.set(program.end,'ACK_PROGRAM_END');
                    return {hook:labels.get(program.hook) ?? program.hook,
                        rows:program.rows.filter(row => !changed.has(row.name)).map(({name,op,a,b,d}) =>
                            [name,op,a,b,op>=0x400 || (op>=0xd0 && op<=0xdf) ? labels.get(d) ?? d : d])};
                };
                assert.deepEqual(normalizeProgram(current),normalizeProgram(old),
                    'Only the reviewed R06 DMA length/next-copy tuples and relocated labels may differ');
            } else {
                assert.equal(outputs[1], outputs[0], `${profile.mode}/${profile.id}: instruction or label changed`);
            }
        }
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});

test('build prerequisites track extracted runtime and PSM implementation fragments', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c06-make-'));
    try {
        const source = path.join(directory, 'src'), build = path.join(directory, 'build');
        writeSource(source, currentSource());
        const args = ['-C', source, 'CC=clang', `BUILD=${build}`];
        run('make', [...args, 'all']);
        const transport = run('make', [...args, '-n', '-W', 'runtime/transport.inc', 'all']);
        assert.ok(transport.includes('-c main.c'));
        const psm = run('make', [...args, '-n', '-W', 'psm/response.h', 'all']);
        assert.ok(psm.includes('-c psm_ucode.c'));
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});
