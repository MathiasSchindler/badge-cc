'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { profileFlags } = require('./profile-flags.cjs');
const { flagKey, resolverMakefile, referenceProfileUnchanged } = require('./c02-isolation.cjs');
const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'clean-rx/src');
const frozen = JSON.parse(fs.readFileSync(path.join(root, 'clean-rx/dev-doc/c01-baseline.json')));

function run(command, args, options = {}) {
    const result = spawnSync(command, args, { cwd: root, encoding: 'utf8',
        maxBuffer: 16 * 1024 * 1024, env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' }, ...options });
    if (result.error) throw result.error;
    assert.equal(result.status, 0, result.stdout + result.stderr);
    return result.stdout;
}

test('central selections retain frozen flags and every reference campaign selection', () => {
    const flags = profileFlags(frozen.profiles.map(profile => profile.id));
    for (const profile of frozen.profiles) assert.equal(flagKey(flags[profile.id]), flagKey(profile.flags), profile.id);
    assert.throws(() => profileFlags(['nonexistent']), /Unknown controller selection/);
    assert.throws(() => profileFlags(['bad$name']), /Invalid controller selection/);
    const current = fs.readFileSync(path.join(root, 'clean-rx/vendor-burst/Makefile'), 'utf8');
    const original = run('git', ['show', `${frozen.provenance.revision}:clean-rx/vendor-burst/Makefile`]);
    assert.ok(referenceProfileUnchanged(current, original), 'Default campaign selection changed');
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c07-selection-'));
    try {
        const resolvers = [current, original].map((text, index) => {
            const file = path.join(directory, `${index}.mk`);
            fs.writeFileSync(file, resolverMakefile(text).text);
            return file;
        });
        for (const [target, parameters] of [
            ['reference', ['REF_STIMULUS=1']],
            ['reference-handoff-copy', ['REF_NEXT_FIELD=0xc3u', 'REF_VARIABLE=1', 'REF_NEXT_STREAM=1', 'REF_VARIABLE_STREAM64=1', 'REF_SHM_ONLY=1', 'REF_SHORT_STREAM=1', 'REF_DELIMITER_GUARD=1']],
            ['reference-early-fifo-recovery', ['REF_REARM_INJECT=1', 'REF_ORDERED_FLUSH=2', 'REF_PREFIX_RECOVERY=1']],
            ['reference-handoff-guard', ['REF_VARIABLE=1']],
            ['reference-handoff', ['REF_FLAGS=-DCUSTOM_SELECTION=1']],
        ]) {
            const outputs = resolvers.map(file => run('make',
                ['-s', '--no-print-directory', '-f', file, `C02_RESOLVER=${file}`, ...parameters, target]));
            assert.equal(outputs[0], outputs[1], `${target}: parameterized campaign changed`);
        }
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});

test('named controller and native selections emit the same bytes as reviewed explicit flags', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c07-build-'));
    try {
        const cases = [
            ['ht-data', 'runtime', frozen.profiles.find(profile => profile.id === 'ht-data').flags],
            ['early-prefix', 'runtime', frozen.profiles.find(profile => profile.id === 'early-prefix').flags],
            ['copy-scope', 'diagnostic', frozen.profiles.find(profile => profile.id === 'copy-scope').flags],
            ['sdio-paced', 'sdio-test', '-DCLEAN_SDIO_SELF_TEST_PACED'],
            ['sdio-window32', 'sdio-test', '-DCLEAN_SDIO_SELF_TEST_WINDOW=1u -DCLEAN_SDIO_SELF_TEST_TRANSITION -DCLEAN_SDIO_SELF_TEST_DMA32'],
            ['sdio-credit', 'sdio-test', '-DCLEAN_SDIO_SELF_TEST_WINDOW=CLEAN_SDIO_WINDOW -DCLEAN_SDIO_SELF_TEST_DMA32 -DCLEAN_SDIO_SELF_TEST_LARGE'],
        ];
        const build = path.join(directory, 'build');
        const make = args => run('make', ['-C', source, 'CC=clang', `BUILD=${build}`, ...args, 'all']);
        for (const [selection, profile, flags] of cases) {
            make([`PROFILE=${profile}`, 'SELECTION=default', `EXTRA_CFLAGS=${flags}`]);
            const expected = fs.readFileSync(path.join(build, 'cyw43_rx_fw.bin'));
            make([`PROFILE=${profile}`, `SELECTION=${selection}`]);
            assert.deepEqual(fs.readFileSync(path.join(build, 'cyw43_rx_fw.bin')), expected, selection);
            assert.ok(fs.readFileSync(path.join(build, 'profile.txt'), 'utf8').includes(`selection=${selection}\n`));
        }
        make(['PROFILE=diagnostic', 'SELECTION=copy-scope']);
        make(['PROFILE=diagnostic', 'SELECTION=reference-handoff-scope-on']);
        assert.match(fs.readFileSync(path.join(build, 'profile.txt'), 'utf8'), /selection=reference-handoff-scope-on\n/);
        const invalid = spawnSync('make', ['-C', source, 'SELECTION=nonexistent', 'all'], { encoding: 'utf8' });
        assert.notEqual(invalid.status, 0);
        assert.match(invalid.stderr, /Unknown SELECTION/);
        const native = run('make', ['-C', 'clean-rx/tests', `BUILD=${directory}/native`,
            'PROFILE=diagnostic', 'SELECTION=copy-scope', 'copy-scope-test']);
        assert.match(native, /copy.scope/i);
        const named = run('make', ['-n', '-C', 'clean-rx/tests', `BUILD=${directory}/named`,
            'EXTRA_CFLAGS=-DUNSELECTED_CALLER_FLAG=1', 'test-copy-scope', 'test-early-rx', 'test-recovery']);
        assert.doesNotMatch(named, /-DUNSELECTED_CALLER_FLAG/);
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});

test('development tools are separated and firmware-only builds do not invoke them or hardware', () => {
    for (const app of ['badge', 'messenger', 'peer', 'vendor-burst']) {
        const files = fs.readdirSync(path.join(root, 'clean-rx', app));
        assert.ok(!files.some(file => file === 'collector.c' || /\.(sh|cjs)$/.test(file)), app);
        assert.ok(fs.existsSync(path.join(root, 'clean-rx/tools', app, 'collector.c')));
        for (const file of fs.readdirSync(path.join(root, 'clean-rx/tools', app)))
            if (file.endsWith('.sh')) run('bash', ['-n', path.join(root, 'clean-rx/tools', app, file)]);
    }
    const catalogue = fs.readFileSync(path.join(source, 'profiles.mk'), 'utf8');
    assert.doesNotMatch(catalogue, /\$\(shell|^\s*include\s|^\t/m);
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c07-dry-'));
    try {
        for (const args of [
            ['-C', source, 'CC=clang', `BUILD=${directory}/controller`, 'all'],
            ['-C', 'clean-rx/badge', `BUILD=${directory}/badge`, 'firmware'],
            ['-C', 'clean-rx/messenger', `BUILD=${directory}/messenger`, 'firmware'],
            ['-f', 'clean-rx/peer/Makefile', `HT_PEER_BUILD=${directory}/peer`, 'ht-peer-firmware'],
            ['-f', 'clean-rx/vendor-burst/Makefile', `REF_BUILD=${directory}/reference`, 'reference-firmware'],
            ['-C', 'clean-rx/tests', `BUILD=${directory}/native`, 'test'],
        ]) {
            const output = run('make', ['-n', ...args]);
            assert.doesNotMatch(output, /pkg-config|libusb|collector\.c|tools\/(?:badge|messenger|peer|vendor-burst)\//);
            assert.doesNotMatch(output, /\b(?:picotool|openocd|dfu-util)\b|bash [^\n]*(?:probe|run|trial|soak)\.sh/);
        }
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});

test('unevolved relocated tools preserve their bodies apart from paths', () => {
    const moves = [];
    for (const app of ['badge', 'messenger', 'peer', 'vendor-burst'])
        for (const file of fs.readdirSync(path.join(root, 'clean-rx/tools', app)))
            moves.push([`clean-rx/${app}/${file}`, `clean-rx/tools/${app}/${file}`]);
    for (const [before, after] of moves) {
        // R02 evolves these tools; their native/oracle suites cover the new report trailer.
        if (['badge/collector.c', 'badge/lifecycle_verify.cjs', 'badge/network_verify.cjs',
            'badge/wpa2_verify.cjs', 'badge/wpa2_rekey_verify.cjs', 'badge/ht_probe.sh']
            .some(file => after === `clean-rx/tools/${file}`)) continue;
        let expected = run('git', ['show', `fb493e321ddf23c4a7caebd26c5e9b8202498233:${before}`]);
        if (before.endsWith('.c')) expected = expected.replace(/^#include "([^"]+)"$/gm, (line, include) =>
            include === 'build_id.h' ? line : `#include "${path.posix.relative(path.posix.dirname(after),
                path.posix.join(path.posix.dirname(before), include))}"`);
        if (before.endsWith('.sh'))
            expected = expected.replace('$(dirname "$0")/../..', '$(dirname "$0")/../../..');
        for (const [from, to] of moves) expected = expected.replaceAll(from, to);
        assert.equal(fs.readFileSync(path.join(root, after), 'utf8'), expected, after);
    }
});
