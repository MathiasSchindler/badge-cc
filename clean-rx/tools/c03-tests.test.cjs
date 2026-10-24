'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { flagKey } = require('./c02-isolation.cjs');
const root = path.resolve(__dirname, '../..');
const tests = path.join(root, 'clean-rx/tests');
const frozen = JSON.parse(fs.readFileSync(path.join(root, 'clean-rx/dev-doc/c01-baseline.json'), 'utf8'));

function make(args, cwd, input) {
    const result = spawnSync('make', args, {
        cwd, input, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024,
        env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' },
    });
    if (result.error) throw result.error;
    assert.equal(result.status, 0, result.stdout + result.stderr);
    return result.stdout + result.stderr;
}

test('named test profile flags match reviewed controller selections', () => {
    for (const [variable, id] of [['COPY_SCOPE_FLAGS', 'copy-scope'], ['RECOVERY_FLAGS', 'early-prefix']]) {
        const flags = make(['-s', '--no-print-directory', '-f', 'Makefile', '-f', '-', 'print-profile'],
            tests, `print-profile:;@printf '%s\\n' '$(${variable})'\n`).trim();
        assert.equal(flagKey(flags), flagKey(frozen.profiles.find(profile => profile.id === id).flags));
    }
    const early = make(['-s', '--no-print-directory', '-f', 'Makefile', '-f', '-', 'print-profile'],
        tests, "print-profile:;@printf '%s\\n' '$(EARLY_RX_FLAGS)'\n").trim();
    const expected = frozen.profiles.find(profile => profile.id === 'early-prefix').flags
        .split(' ').filter(flag => !/CLEAN_RX_HT_(ORDERED_FLUSH|PREFIX_RECOVERY)=/.test(flag)).join(' ');
    assert.equal(flagKey(early), flagKey(expected));
});

test('controller tree contains no native tests and production build does not enter the test tree', () => {
    assert.ok(!fs.readdirSync(path.join(root, 'clean-rx/src')).some(file => file.endsWith('_test.c')));
    const commands = make(['-n', 'CC=clang', 'BUILD=/nonexistent-clean-rx-c03-dry-run', 'all'],
        path.join(root, 'clean-rx/src'));
    assert.ok(!commands.includes('../tests'));
    assert.ok(!commands.includes('sanitize'));
    assert.ok(!commands.includes('diagnostics_logic'));
});

test('unit/model suites build with only production source and unit/support tests present', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c03-unit-'));
    try {
        const source = path.join(directory, 'src');
        const isolatedTests = path.join(directory, 'tests');
        fs.mkdirSync(source);
        fs.mkdirSync(isolatedTests);
        for (const file of fs.readdirSync(path.join(root, 'clean-rx/src'))) {
            if (/\.(c|h|S|ld)$/.test(file) || file === 'Makefile') {
                fs.copyFileSync(path.join(root, 'clean-rx/src', file), path.join(source, file));
            }
        }
        for (const file of ['Makefile', 'profiles.mk']) fs.copyFileSync(path.join(tests, file), path.join(isolatedTests, file));
        for (const dir of ['unit', 'support']) fs.cpSync(path.join(tests, dir), path.join(isolatedTests, dir), { recursive: true });
        const output = make(['BUILD=' + path.join(directory, 'build'), 'unit-test'], isolatedTests);
        assert.ok(output.includes('PSM: image identity, capture bounds'));
        assert.ok(output.includes('Block ACK agreement'));
        assert.ok(output.includes('RX recovery diagnostic disabled'));
        assert.ok(!output.includes('diagnostics_logic.c'));
        assert.ok(!output.includes('integration/'));
        assert.ok(!fs.existsSync(path.join(directory, 'badge')));
        const changed = make(['BUILD=' + path.join(directory, 'build'),
            'EXTRA_CFLAGS=-DC03_FLAG_CHANGE=1', 'unit-test'], isolatedTests);
        for (const file of ['runtime_test.c', 'psm_ucode_test.c', 'block_ack_test.c', 'rx_recovery_test.c'])
            assert.ok(changed.includes(`unit/${file}`), `Flag change did not rebuild ${file}`);
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});
