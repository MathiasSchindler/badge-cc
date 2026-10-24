'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'clean-rx/src');
const frozen = JSON.parse(fs.readFileSync(path.join(root, 'clean-rx/dev-doc/c01-baseline.json'), 'utf8'));

function run(command, args, input) {
    const result = spawnSync(command, args, { cwd: root, input, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024 });
    if (result.error) throw result.error;
    assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}${result.stderr}`);
    return result.stdout;
}

function original(file, digest) {
    const text = run('git', ['show', `${frozen.provenance.revision}:${file}`]);
    assert.equal(crypto.createHash('sha256').update(text).digest('hex'), digest, file);
    return text;
}

function operationBodies(text) {
    return [...text.matchAll(/^static inline [^{;]+?\{[\s\S]*?^}\n/gm)].map(match => match[0]);
}

test('unmodified split operations and moved crypto/soak implementations retain their original bodies', () => {
    for (const [old, current] of [
        ['ht20.h', ['src/ht20.h', 'src/diagnostic/ht_command.h', 'tests/fixtures/ht20.h']],
        ['unicast.h', ['src/unicast.h', 'tests/fixtures/unicast.h']],
    ]) {
        const file = `clean-rx/src/${old}`;
        const before = original(file, frozen.inventory.find(entry => entry.file === file).sha256);
        const after = current.flatMap(file => operationBodies(fs.readFileSync(path.join(root, 'clean-rx', file), 'utf8')));
        const retained = body => !/\b(?:clean_ht_payload_command_valid|clean_ht_ampdu_dma_decode|clean_ht_rx_version)\(/.test(body.split('{')[0]);
        assert.deepEqual(after.filter(retained).sort(), operationBodies(before).filter(retained).sort(), old);
    }
    for (const [old, current] of [
        ['mac_soak.h', 'tests/fixtures/mac_soak.h'],
        ['wpa2_crypto.c', 'host/wpa2_crypto.c'],
        ['wpa2_crypto.h', 'host/wpa2_crypto.h'],
    ]) {
        const file = `clean-rx/src/${old}`;
        const before = original(file, frozen.inventory.find(entry => entry.file === file).sha256);
        const after = fs.readFileSync(path.join(root, 'clean-rx', current), 'utf8');
        const withoutIncludes = text => text.replace(/^#include .+\n/gm, '')
            .replaceAll('badger_bytes_', 'clean_bytes_');
        assert.equal(withoutIncludes(after), withoutIncludes(before), old);
        assert.ok(!fs.existsSync(path.join(source, old)), old);
    }
});

test('controller dependencies stay internal and split headers compile independently without libc', () => {
    const splitHeaders = new Set(['frame_limits.h', 'rx_contract.h', 'ht_contract.h',
        'ht20.h', 'ht_payload.h', 'diagnostic/ht_command.h', 'unicast_contract.h', 'unicast.h']);
    for (const file of fs.readdirSync(source, { recursive: true }).filter(file => file.endsWith('.h'))) {
        const dependencies = run('clang', ['--target=arm-none-eabi', '-mcpu=cortex-m3',
            '-mthumb', '-std=c11', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
            '-nostdinc', '-fno-builtin', '-I', source, '-M', '-x', 'c', '-'], `#include "${file}"\n`);
        if (splitHeaders.has(file) && file !== 'diagnostic/ht_command.h')
            assert.ok(!dependencies.includes('/ht_command.h'), `${file} imports diagnostic command codecs`);
        for (const dependency of dependencies.replace(/\\\n/g, ' ').split(':').slice(1).join(':').trim().split(/\s+/)) {
            const relative = path.relative(source, dependency);
            assert.ok(relative !== '..' && !relative.startsWith('../') && !path.isAbsolute(relative), dependency);
        }
        if (splitHeaders.has(file))
            run('clang', ['--target=arm-none-eabi', '-mcpu=cortex-m3', '-mthumb', '-std=c11',
                '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-nostdinc', '-fno-builtin',
                '-I', source, '-fsyntax-only', '-x', 'c', '-'], `#include "${file}"\n`);
    }
    for (const cpu of ['cortex-m3', 'cortex-m33'])
        run('clang', ['--target=arm-none-eabi', `-mcpu=${cpu}`, '-mthumb', '-std=c11',
            '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-nostdinc', '-fno-builtin',
            '-fsyntax-only', 'clean-rx/host/wpa2_crypto.c']);
});

test('all split constants, structure sizes, alignments and field offsets match frozen C01', t => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c04-contracts-'));
    try {
        for (const entry of frozen.inventory.filter(entry =>
            entry.file.startsWith('clean-rx/src/') && entry.file.endsWith('.h'))) {
            const destination = path.join(directory, entry.file);
            fs.mkdirSync(path.dirname(destination), { recursive: true });
            fs.writeFileSync(destination, original(entry.file, entry.sha256));
        }
        for (const file of ['src/shared/stdint.h', 'src/shared/bytes.h']) {
            const destination = path.join(directory, file);
            fs.mkdirSync(path.dirname(destination), { recursive: true });
            fs.writeFileSync(destination, original(file, frozen.inputHashes[file]));
        }
        const oldSource = path.join(directory, 'clean-rx/src');
        const headers = ['frame_limits.h', 'ht20.h', 'unicast.h'].map(file =>
            fs.readFileSync(path.join(oldSource, file), 'utf8')).join('\n');
        const constants = [...new Set([...headers.matchAll(/^#define (CLEAN_\w+) [^\n]+/gm)]
            .map(match => match[1]).filter(name => name !== 'CLEAN_RX_LAYOUT_VALUES'))];
        const fields = [];
        for (const match of headers.matchAll(/typedef struct \{([\s\S]*?)\} (clean_\w+_t);/g)) {
            fields.push(`sizeof(${match[2]})`, `_Alignof(${match[2]})`);
            for (const declaration of match[1].split(';').filter(text => text.trim())) {
                const callback = /\(\*(\w+)\)/.exec(declaration);
                const names = callback ? [callback[1]] : declaration.split(',').map(text => {
                    const name = /(\w+)(?:\[[^\]]+\])*\s*$/.exec(text);
                    assert.ok(name, declaration);
                    return name[1];
                });
                for (const name of names) fields.push(`__builtin_offsetof(${match[2]}, ${name})`);
            }
        }
        assert.ok(constants.length > 200, `Only ${constants.length} constants checked`);
        assert.ok(fields.length > 100, `Only ${fields.length} layout properties checked`);
        const probe = '#include "ht20.h"\n#include "unicast.h"\n' +
            '#ifdef C04_CURRENT\n#include "rx_contract.h"\n#endif\n' +
            `const unsigned long long contract[] = {${[...constants, ...fields].join(',')}};\n` +
            'const unsigned layout[] = CLEAN_RX_LAYOUT_VALUES;\n';
        const profileFlags = [
            ...frozen.profiles.map(profile => profile.flags),
            '-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_VARIABLE=1',
            '-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_HANDOFF_GUARD=1',
            '-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_VARIABLE=1 -DCLEAN_RX_HT_VARIABLE_STREAM64=1',
            '-DCLEAN_RX_HT_COPY_STREAM_PHASE=1',
            '-DCLEAN_RX_HT_COPY_STREAM_PHASE=3',
            '-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3 -DCLEAN_RX_AMPDU_HANDOFF=2 -DCLEAN_RX_AMPDU_DMA=1',
            '-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_DATA=1 -DCLEAN_RX_AMPDU_PROBE=1 -DCLEAN_RX_AMPDU_INTERFACE=3 -DCLEAN_RX_AMPDU_HANDOFF=2 -DCLEAN_RX_AMPDU_SHM=1 -DCLEAN_RX_AMPDU_BA=1',
        ];
        for (const flags of profileFlags) {
            const output = include => run('clang', ['--target=arm-none-eabi', '-mcpu=cortex-m3',
                '-mthumb', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Oz',
                '-ffreestanding', '-nostdinc', '-fno-builtin', '-I', include,
                ...flags.split(' ').filter(Boolean), ...(include === source ? ['-DC04_CURRENT'] : []),
                '-S', '-emit-llvm', '-x', 'c', '-', '-o', '-'], probe)
                .split('\n').filter(line => /^@(contract|layout) =/.test(line)).join('\n');
            const expected = output(oldSource), actual = output(source);
            assert.ok(expected.includes('@contract =') && expected.includes('@layout ='));
            assert.equal(actual, expected, flags || 'default');
        }
        t.diagnostic(`${constants.length} constants and ${fields.length} layout properties plus the eight-word manifest match in ${profileFlags.length} selections.`);
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});
