'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { classify, referenceTargets, dependencies, extractSymbol, testStatus, psmSymbol, matrix } = require('./c01-baseline.cjs');

test('classification distinguishes runtime, host-only code, tools and native tests', () => {
    assert.deepEqual(classify('clean-rx/src/pmu.c'), ['controller-runtime']);
    assert.deepEqual(classify('clean-rx/src/bytes.h'), ['shared-abi-portable-runtime']);
    assert.deepEqual(classify('clean-rx/src/wpa2_crypto.c'), ['host-runtime']);
    assert.ok(classify('clean-rx/src/main.c').includes('firmware-side-diagnostic'));
    assert.deepEqual(classify('clean-rx/src/mac_test.c'), ['native-test']);
    assert.deepEqual(classify('clean-rx/badge/collector.c'), ['development-tool']);
    assert.deepEqual(classify('clean-rx/tools/c01-baseline.test.cjs'), ['native-test']);
    assert.throws(() => classify('clean-rx/src/unreviewed.h'), /Unclassified/);
});

test('reference catalogue retains recipes, locations, aliases and repeated test prerequisites', () => {
    const entries = referenceTargets([
        'reference: pair',
        'reference-early:', '\t$(MAKE) REF_FLAGS=\'-DCLEAN_RX_HT_RX=1\' reference',
        '.PHONY: reference-test',
        'reference-test: first', 'ifeq ($(MODE),1)', '\tfirst', 'endif',
        'reference-test: second', '\tsecond',
        'OTHER := ignored', '\tignored',
        'reference-force:',
    ].join('\n'));
    assert.equal(entries.length, 3);
    assert.equal(entries[1].line, 2);
    assert.match(entries[1].recipes[0], /CLEAN_RX_HT_RX=1/);
    assert.deepEqual(entries[2].recipes, ['first', 'second']);
    assert.match(entries[1].command, /reference-early$/);
    assert.ok(entries.every(entry => entry.coverageGap && entry.owner && entry.purpose));
});

test('dependency trace normalizes parent paths and joins continuation lines', () => {
    assert.deepEqual(dependencies('main.o: main.c \\\n ../../src/shared/bytes.h main.c\n'),
        ['clean-rx/src/main.c', 'src/shared/bytes.h']);
    assert.throws(() => dependencies('not a dependency rule'), /no target/);
});

test('PSM extraction uses file-backed ELF section addresses rather than assuming binary offsets', () => {
    const elf = Buffer.alloc(256);
    Buffer.from('7f454c460101', 'hex').copy(elf);
    elf.writeUInt32LE(64, 32);
    elf.writeUInt16LE(40, 46);
    elf.writeUInt16LE(1, 48);
    elf.writeUInt32LE(1, 68);
    elf.writeUInt32LE(0x1000, 76);
    elf.writeUInt32LE(128, 80);
    elf.writeUInt32LE(32, 84);
    Buffer.from('0102030405060708', 'hex').copy(elf, 136);
    assert.equal(extractSymbol(elf, 0x1008, 8).toString('hex'), '0102030405060708');
    assert.throws(() => extractSymbol(elf, 0x101f, 8), /no file-backed/);
    assert.throws(() => extractSymbol(elf, 0x1000, 0), /Invalid symbol/);
    assert.throws(() => extractSymbol(Buffer.alloc(64), 0, 8), /ELF32/);
    elf.writeUInt32LE(250, 80);
    assert.throws(() => extractSymbol(elf, 0x1000, 8), /exceeds ELF/);
});

test('disabled recovery coverage is not reported as a pass', () => {
    assert.equal(testStatus('RX recovery diagnostic disabled; no recovery program selected\n'), 'disabled');
    assert.equal(testStatus('RX recovery emitted-program bounds pass; NOT RF\n'), 'passed');
    const recovery = matrix.find(profile => profile.id === 'early-prefix');
    assert.ok(recovery.defines.includes('CLEAN_RX_HT_ORDERED_FLUSH=1'));
    assert.ok(recovery.defines.includes('CLEAN_RX_HT_PREFIX_RECOVERY=1'));
    assert.ok(recovery.extraTests.includes('early_rx_test'));
});

test('transport-only images explicitly omit PSM; unexpected absence or presence fails', () => {
    assert.equal(psmSymbol('', true), null);
    assert.throws(() => psmSymbol('', false), /missing/);
    const symbols = '00001000 00000008 T psm_ucode_bytes\n';
    assert.deepEqual(psmSymbol(symbols, false), { address: 0x1000, bytes: 8 });
    assert.throws(() => psmSymbol(symbols, true), /Unexpected PSM/);
});

test('CLI refuses to overwrite evidence directories or frozen records before building', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c01-test-'));
    try {
        const script = path.join(__dirname, 'c01-baseline.cjs');
        const existing = spawnSync(process.execPath, [script, directory], { encoding: 'utf8' });
        assert.equal(existing.status, 1);
        assert.match(existing.stderr, /Refusing to reuse evidence/);
        const record = path.join(directory, 'frozen.json');
        fs.writeFileSync(record, 'unchanged\n');
        const output = path.join(directory, 'new-evidence');
        const frozen = spawnSync(process.execPath, [script, output, record], { encoding: 'utf8' });
        assert.equal(frozen.status, 1);
        assert.match(frozen.stderr, /Record must be a new file/);
        assert.equal(fs.readFileSync(record, 'utf8'), 'unchanged\n');
        assert.equal(fs.existsSync(output), false);
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});
