'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { checkSourceTree, checkMakeInputs, checkDependencies, checkSymbols } = require('./controller-boundaries.cjs');
const { checkMatrix } = require('./check.cjs');
const { buildProfile } = require('./c02-isolation.cjs');
const { currentSource } = require('./c06-runtime.cjs');
const root = path.resolve(__dirname, '../..');

function fixture(callback) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c08-'));
    try {
        const source = path.join(directory, 'src'), build = path.join(directory, 'build');
        for (const [name, text] of currentSource()) {
            fs.mkdirSync(path.dirname(path.join(source, name)), { recursive: true });
            fs.writeFileSync(path.join(source, name), text);
        }
        callback(source, build, directory);
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
}

test('source boundaries reject escaped, dormant, test, libc and symlink dependencies', () => {
    fixture((source, build, directory) => {
        assert.ok(checkSourceTree(source).length > 50);
        const header = path.join(source, 'injected.h');
        const external = path.join(directory, 'outside.h');
        fs.writeFileSync(external, '');
        for (const include of ['"../outside.h"', '<stdio.h>', 'COMPUTED_HEADER']) {
            fs.writeFileSync(header, `#if 0\n#include ${include}\n#endif\n`);
            assert.throws(() => checkSourceTree(source), /escaped|Non-local/);
        }
        fs.writeFileSync(header, '#include "tests/fixture.h"\n');
        fs.mkdirSync(path.join(source, 'tests'));
        fs.writeFileSync(path.join(source, 'tests/fixture.h'), '');
        assert.throws(() => checkSourceTree(source), /Test\/host\/tool/);
        fs.unlinkSync(header);
        fs.rmSync(path.join(source, 'tests'), { recursive: true });
        fs.symlinkSync(external, header);
        assert.throws(() => checkSourceTree(source), /Symlink/);
        fs.unlinkSync(header);
        fs.writeFileSync(header, '#include_next "types.h"\n');
        assert.throws(() => checkSourceTree(source), /Non-local/);
        fs.unlinkSync(header);
        const startup = path.join(source, 'startup.S'), original = fs.readFileSync(startup, 'utf8');
        fs.appendFileSync(startup, '\n.incbin "../outside.bin"\n');
        assert.throws(() => checkSourceTree(source), /Assembler file import/);
        fs.writeFileSync(startup, original);
        fs.appendFileSync(path.join(source, 'cyw43.ld'), '\nINCLUDE ../outside.h\n');
        assert.throws(() => checkSourceTree(source), /linker script imports/);
    });
});

test('evaluated Make inputs reject extra sources, external Makefiles, headers and libraries', () => {
    fixture((source, build, directory) => {
        const makefile = path.join(source, 'Makefile');
        const original = fs.readFileSync(makefile, 'utf8');
        const args = ['-C', source, 'CC=clang', `BUILD=${build}`, 'all'];
        checkMakeInputs(source, build, args);
        fs.writeFileSync(path.join(directory, 'outside.mk'), '');
        fs.writeFileSync(path.join(directory, 'outside.h'), '');
        for (const addition of [
            '\nSRCS += ../outside.c\n',
            '\nOBJS += injected.o\n',
            '\nHEADERS += ../outside.h\n',
            '\ninclude ../outside.mk\n',
            '\nCFLAGS += -I..\n',
            '\nCFLAGS += -include ../outside.h\n',
            '\nLDFLAGS += -lc\n',
        ]) {
            fs.writeFileSync(makefile, original + addition);
            assert.throws(() => checkMakeInputs(source, build, args), /Unexpected|escaped/, addition);
        }
    });
});

test('compiled dependencies and symbols enforce runtime/diagnostic and freestanding boundaries', () => {
    fixture((source, build, directory) => {
        const runtime = ['-DCLEAN_RX_DIAGNOSTIC=0'];
        checkDependencies(source, [{ inputs: ['main.c'] }], runtime);
        assert.throws(() => checkDependencies(source, [{ inputs: ['diagnostic/ht_command.h'] }], runtime), /Runtime imported diagnostic/);
        assert.throws(() => checkDependencies(source, [{ inputs: ['main.c'] }], ['-DCLEAN_RX_DIAGNOSTIC=1']), /not compiled/);
        fs.writeFileSync(path.join(directory, 'outside.h'), '');
        assert.throws(() => checkDependencies(source, [{ inputs: ['../outside.h'] }], runtime), /escaped/);
        const valid = ['cyw43_reset', 'cyw43_fault', 'cyw43_main', 'cyw43_vectors']
            .map(name => `00000000 00000004 T ${name}`).join('\n');
        checkSymbols(valid, '');
        for (const [type, name] of [['T', 'printf'], ['t', '__aeabi_uidiv'], ['D', 'stderr'], ['T', 'surprise_runtime']])
            assert.throws(() => checkSymbols(`${valid}\n00000000 00000004 ${type} ${name}`, ''), /Unexpected linked/, name);
        assert.throws(() => checkSymbols(valid, ' U missing'), /Undefined/);
        assert.throws(() => checkSymbols('', ''), /Unexpected controller symbol|Missing controller/);
        const image = buildProfile(source, build, '', true, 'runtime');
        assert.ok(image.binarySha256);
    });
});

test('selected recovery/copy diagnostics cannot pass as disabled or absent coverage', () => {
    const frozen = JSON.parse(fs.readFileSync(path.join(root, 'clean-rx/dev-doc/c01-baseline.json')));
    assert.deepEqual(checkMatrix(frozen, frozen), { passed: 24, disabled: 6, 'not-selected': 12 });
    for (const [profile, name, status] of [
        ['early-prefix', 'rx_recovery_test', 'disabled'],
        ['copy-scope', 'copy_scope_test', 'not-selected'],
        ['early-prefix', 'early_rx_test', 'disabled'],
        ['default', 'psm_ucode_test', 'disabled'],
    ]) {
        const changed = structuredClone(frozen);
        changed.profiles.find(entry => entry.id === profile).tests.find(entry => entry.test === name).status = status;
        assert.throws(() => checkMatrix(changed, frozen), /did not execute/);
    }
});

test('acceptance refuses reused evidence without writing a success manifest', () => {
    fixture((source, build, directory) => {
        const result = spawnSync(process.execPath, [path.join(__dirname, 'check.cjs'), directory], { encoding: 'utf8' });
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /Refusing to reuse/);
        assert.ok(!fs.existsSync(path.join(directory, 'manifest.json')));
    });
});
