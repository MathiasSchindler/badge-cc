#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const frozenPath = path.join(root, 'clean-rx/dev-doc/c01-baseline.json');
const frozenHash = '3c2941a6b6fc684722b3ffbc9896ad28dddb6b04adf60146a74cca5de4454bf2';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

function checkMatrix(current, frozen) {
    assert.equal(current.status, 'complete');
    assert.deepEqual(current.profiles.map(profile => profile.id), frozen.profiles.map(profile => profile.id));
    const coverage = {};
    for (const profile of current.profiles) {
        const original = frozen.profiles.find(entry => entry.id === profile.id);
        for (const artifact of ['cyw43_rx_fw.bin', 'psm.bin'])
            assert.deepEqual(profile.artifacts[artifact], original.artifacts[artifact], `${profile.id}/${artifact}`);
        assert.deepEqual(profile.psm, original.psm, `${profile.id}: PSM layout changed`);
        const required = new Set(['psm_ucode_test', 'mac_test', 'block_ack_test']);
        if (profile.id === 'copy-scope') required.add('copy_scope_test');
        if (profile.id === 'early-prefix') {
            required.add('rx_recovery_test');
            required.add('early_rx_test');
        }
        for (const test of required)
            assert.equal(profile.tests.find(entry => entry.test === test)?.status, 'passed',
                `${profile.id}/${test}: selected diagnostic/model did not execute`);
        assert.deepEqual(profile.tests.map(test => [test.test, test.status]),
            original.tests.map(test => [test.test, test.status]), `${profile.id}: coverage changed`);
        for (const test of profile.tests) coverage[test.status] = (coverage[test.status] || 0) + 1;
    }
    return coverage;
}

function inputHashes() {
    const files = spawnSync('git', ['ls-files', '--cached', '--others', '--exclude-standard', '-z', '--', 'clean-rx'],
        { cwd: root, encoding: 'utf8' });
    if (files.error) throw files.error;
    if (files.status !== 0) throw new Error(files.stderr);
    return Object.fromEntries([...new Set(files.stdout.split('\0').concat([
        'Makefile', 'src/tools/diagnostics_logic.c', 'src/tools/diagnostics_logic.h',
        'src/shared/bytes.h', 'src/shared/stdint.h',
    ]).filter(file =>
        /(?:\.(?:c|h|S|ld|mk|inc|cjs|sh|md)|\/Makefile)$/.test(file) &&
        fs.existsSync(path.join(root, file))))]
        .sort().map(file => [file, hash(fs.readFileSync(path.join(root, file)))]));
}

function check(outputArgument) {
    const output = path.resolve(outputArgument);
    if (fs.existsSync(output)) throw new Error(`Refusing to reuse acceptance directory: ${output}`);
    assert.equal(hash(fs.readFileSync(frozenPath)), frozenHash, 'Frozen C01 evidence changed');
    const inputs = inputHashes();
    fs.mkdirSync(output, { recursive: true });
    const report = { schemaVersion: 1, status: 'in-progress', hardware: 'not-run',
        startedAt: new Date().toISOString(), frozenBaselineSha256: frozenHash, inputHashes: inputs, stages: [] };
    const save = () => fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify(report, null, 2) + '\n');
    save();
    function stage(id, command, args) {
        console.log(`Checking ${id}`);
        const entry = { id, command: [command, ...args], status: 'in-progress', log: `${id}.log` };
        report.stages.push(entry);
        save();
        const log = fs.openSync(path.join(output, entry.log), 'w');
        let result;
        try {
            result = spawnSync(command, args, { cwd: root, stdio: ['ignore', log, log],
                env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' } });
        } finally {
            fs.closeSync(log);
        }
        entry.status = result.error || result.status !== 0 ? 'failed' : 'passed';
        entry.exitCode = result.status;
        save();
        if (result.error) throw result.error;
        if (result.status !== 0) throw new Error(`${id} failed; see ${path.join(output, entry.log)}`);
    }
    try {
        const tests = fs.readdirSync(__dirname).filter(file => file.endsWith('.test.cjs'))
            .sort().map(file => path.join(__dirname, file));
        stage('tooling', process.execPath, ['--test', ...tests]);
        stage('documentation', process.execPath, [path.join(__dirname, 'documentation.cjs')]);
        stage('isolation', process.execPath, [path.join(__dirname, 'c02-isolation.cjs'), path.join(output, 'isolation')]);
        stage('runtime', process.execPath, [path.join(__dirname, 'c06-runtime.cjs'), path.join(output, 'runtime')]);
        stage('matrix', process.execPath, [path.join(__dirname, 'c01-baseline.cjs'), path.join(output, 'matrix')]);
        for (const profile of ['runtime', 'diagnostic'])
            stage(`native-${profile}`, 'make', ['-C', 'clean-rx/tests', `BUILD=${path.join(output, `native-${profile}`)}`,
                `PROFILE=${profile}`, 'test']);
        stage('native-profiles', 'make', ['-C', 'clean-rx/tests', `BUILD=${path.join(output, 'native-profiles')}`,
            'test-copy-scope', 'test-early-rx', 'test-recovery']);
        const read = name => JSON.parse(fs.readFileSync(path.join(output, name, 'manifest.json')));
        const isolation = read('isolation'), runtime = read('runtime');
        assert.equal(isolation.status, 'complete');
        assert.equal(isolation.profiles.length, 74);
        assert.equal(isolation.referenceSelections.length, 66);
        assert.ok(isolation.profiles.every(profile => profile.status === 'identical'));
        assert.equal(runtime.status, 'complete');
        assert.equal(runtime.profiles.length, 6);
        assert.ok(runtime.profiles.every(profile => profile.status === 'identical'));
        report.coverage = checkMatrix(read('matrix'), JSON.parse(fs.readFileSync(frozenPath)));
        assert.deepEqual(inputHashes(), inputs, 'Validation inputs changed during acceptance');
        assert.equal(hash(fs.readFileSync(frozenPath)), frozenHash, 'Frozen C01 evidence changed');
        report.status = 'complete';
        report.completedAt = new Date().toISOString();
        save();
        console.log(`PASS: offline acceptance complete; hardware NOT RUN. ${path.join(output, 'manifest.json')}`);
    } catch (error) {
        report.status = 'failed';
        report.failure = error.message;
        save();
        throw error;
    }
}

module.exports = { checkMatrix };
if (require.main === module) {
    try {
        if (process.argv.length !== 3) throw new Error('Usage: node clean-rx/tools/check.cjs NEW_EVIDENCE_DIRECTORY');
        check(process.argv[2]);
    } catch (error) {
        console.error(error.stack);
        process.exitCode = 1;
    }
}
