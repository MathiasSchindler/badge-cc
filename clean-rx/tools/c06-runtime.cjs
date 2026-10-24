#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { buildProfile } = require('./c02-isolation.cjs');
const { matrix } = require('./c01-baseline.cjs');
const { profileFlags } = require('./profile-flags.cjs');
const root = path.resolve(__dirname, '../..');
const revision = 'fb493e321ddf23c4a7caebd26c5e9b8202498233';
const production = file => /(?:\.(c|h|S|ld|mk|inc)|\/Makefile)$/.test(file);
const hash = data => crypto.createHash('sha256').update(data).digest('hex');

function run(command, args) {
    const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
    if (result.error) throw result.error;
    if (result.status !== 0) throw new Error(`${command} ${args.join(' ')}\n${result.stdout}${result.stderr}`);
    return result.stdout;
}

function previousSource() {
    return new Map(run('git', ['ls-tree', '-r', '--name-only', revision, '--', 'clean-rx/src'])
        .trim().split('\n').filter(production).map(file =>
            [file.slice('clean-rx/src/'.length), run('git', ['show', `${revision}:${file}`])]));
}

function currentSource() {
    const source = path.join(root, 'clean-rx/src');
    return new Map(fs.readdirSync(source, { recursive: true }).filter(file => production(`src/${file}`))
        .map(file => [file, fs.readFileSync(path.join(source, file), 'utf8')]));
}

function expand(file, sources) {
    const text = sources.get(file);
    if (text === undefined) throw new Error(`Missing composition input: ${file}`);
    return text.replace(/^#include "([^"]+\.inc)"\s*$/gm, (_, include) => {
        const local = path.posix.normalize(path.posix.join(path.posix.dirname(file), include));
        return expand(sources.has(local) ? local : include, sources);
    });
}

function storageLayout(elf) {
    return run('llvm-nm', ['-n', '-S', elf]).split('\n')
        .filter(line => /^[0-9a-f]+ [0-9a-f]+ [bBdD] /i.test(line))
        .map(line => line.replace(/clean_(?:transport|station|publication|aggregate)_/g, '')).sort();
}

function runtimeProfiles() {
    const defines = id => matrix.find(profile => profile.id === id).defines;
    const flags = profileFlags(['passive-observer', 'passive-reset']);
    const selected = id => flags[id].split(/\s+/).map(flag => flag.slice(2));
    return [
        { id: 'runtime', defines: [] },
        { id: 'ht-data', defines: defines('ht-data') },
        { id: 'wmm-edca', defines: defines('wmm-edca') },
        { id: 'passive-observer', defines: selected('passive-observer') },
        { id: 'early-recovery', defines: defines('early-prefix') },
        { id: 'passive-reset', defines: selected('passive-reset') },
    ];
}

function verify(outputArgument) {
    const output = path.resolve(outputArgument);
    if (fs.existsSync(output)) throw new Error(`Refusing to reuse evidence directory: ${output}`);
    const before = previousSource(), after = currentSource();
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c06-runtime-'));
    fs.mkdirSync(output, { recursive: true });
    const report = { schemaVersion: 1, baselineRevision: revision, status: 'in-progress',
        capturedAt: new Date().toISOString(), profiles: [],
        sourceHashes: Object.fromEntries([...after].map(([file, text]) => [file, hash(text)])) };
    const save = () => fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify(report, null, 2) + '\n');
    save();
    try {
        for (const [name, sources] of [['before', before], ['after', after]]) {
            for (const [file, text] of sources) {
                const destination = path.join(temporary, name, file);
                fs.mkdirSync(path.dirname(destination), { recursive: true });
                fs.writeFileSync(destination, text);
            }
        }
        for (const profile of runtimeProfiles()) {
            console.log(`Checking C05 runtime ${profile.id}`);
            const directory = path.join(output, profile.id);
            fs.mkdirSync(directory);
            const flags = profile.defines.map(value => `-D${value}`).join(' ');
            const original = buildProfile(path.join(temporary, 'before'), path.join(directory, 'before'), flags, false, 'runtime');
            const current = buildProfile(path.join(temporary, 'after'), path.join(directory, 'after'), flags, true, 'runtime');
            assert.equal(current.binarySha256, original.binarySha256, `${profile.id}: controller bytes changed`);
            assert.equal(current.psmSha256, original.psmSha256, `${profile.id}: PSM bytes changed`);
            const storage = storageLayout(path.join(directory, 'after/cyw43_rx_fw.elf'));
            assert.deepEqual(storage, storageLayout(path.join(directory, 'before/cyw43_rx_fw.elf')),
                `${profile.id}: mutable storage moved or changed size`);
            report.profiles.push({ ...profile, original, current, storage, status: 'identical' });
            save();
        }
        assert.deepEqual(currentSource(), after, 'Production inputs changed during verification');
        report.status = 'complete';
        save();
        console.log(`PASS: ${report.profiles.length} runtime profiles match C05 controller, PSM and mutable-storage layouts.`);
    } catch (error) {
        report.status = 'failed';
        report.failure = error.message;
        save();
        throw error;
    } finally {
        fs.rmSync(temporary, { recursive: true });
    }
}

module.exports = { revision, previousSource, currentSource, expand, runtimeProfiles };
if (require.main === module) {
    try {
        if (process.argv.length !== 3) throw new Error('Usage: node clean-rx/tools/c06-runtime.cjs NEW_EVIDENCE_DIRECTORY');
        verify(process.argv[2]);
    } catch (error) {
        console.error(error.stack);
        process.exitCode = 1;
    }
}
