#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const { referenceTargets, extractSymbol, psmSymbol } = require('./c01-baseline.cjs');
const root = path.resolve(__dirname, '../..');

function hash(bytes) {
    return crypto.createHash('sha256').update(bytes).digest('hex');
}

function run(command, args, cwd = root, logFile) {
    const result = spawnSync(command, args, {
        cwd, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
        env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' },
    });
    if (result.error) throw result.error;
    const text = result.stdout + result.stderr;
    if (logFile) fs.writeFileSync(logFile, text);
    if (result.status !== 0) throw new Error(`${command} ${args.join(' ')} failed (${result.status})\n${text}`);
    return text;
}

function flagKey(flags) {
    const tokens = flags.trim() ? flags.trim().split(/\s+/) : [];
    if (tokens.some(token => !/^-D\w+=[\w.+-]+$/.test(token))) throw new Error(`Unexpected profile flags: ${flags}`);
    const definitions = new Map();
    for (const token of tokens) {
        const name = token.slice(2, token.indexOf('='));
        if (definitions.has(name)) throw new Error(`Duplicate profile definition: ${name}`);
        definitions.set(name, token);
    }
    return [...definitions.values()].sort().join(' ');
}

function resolverMakefile(text) {
    const targets = referenceTargets(text);
    const wrappers = targets.filter(entry => entry.recipes.some(recipe =>
        recipe.startsWith('$(MAKE) -f $(REF_DIR)/Makefile ')));
    const leaves = new Set(['reference', 'reference-controller', 'reference-test']);
    for (const entry of targets) {
        if (!leaves.has(entry.target) && !wrappers.includes(entry)) throw new Error(`Unknown reference entry: ${entry.target}`);
    }
    const stimulus = /EXTRA_CFLAGS='([^']*CLEAN_RX_HT_STIMULUS[^']*)'/.exec(text);
    if (!stimulus) throw new Error('Cannot find source stimulus-controller flags');
    const assignments = text.split('\n').filter(line => /^REF_\w+\s*[:?+]?=/.test(line) &&
        !/^REF_HEADERS\s/.test(line) && !line.endsWith('\\'));
    const recipes = wrappers.map(entry => `${entry.target}:\n${entry.recipes.map(recipe => {
        if (!recipe.startsWith('$(MAKE) -f $(REF_DIR)/Makefile ')) throw new Error(`Unexpected wrapper recipe: ${recipe}`);
        return '\t' + recipe.replace('-f $(REF_DIR)/Makefile', '-f "$(C02_RESOLVER)"');
    }).join('\n')}`).join('\n');
    return {
        targets: ['reference', ...wrappers.map(entry => entry.target)],
        text: `${assignments.join('\n')}
.PHONY: reference reference-test ${wrappers.map(entry => entry.target).join(' ')}
reference:
\t@printf '%s\\n' 'C02:observer:$(REF_FLAGS)'
ifeq ($(REF_STIMULUS),1)
\t@printf '%s\\n' 'C02:stimulus:${stimulus[1]}'
endif
reference-test:
\t@:
${recipes}
`,
    };
}

function inside(directory, file) {
    const relative = path.relative(directory, path.resolve(directory, file));
    return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative));
}

function referenceProfileUnchanged(current, original) {
    return resolverMakefile(current).text === resolverMakefile(original).text;
}

function buildProfile(source, build, flags, isolated) {
    fs.mkdirSync(build);
    const args = ['-C', source, 'CC=clang', `BUILD=${build}`, `EXTRA_CFLAGS=${flags}`, 'all'];
    run('make', args, root, path.join(build, 'build.log'));
    const compilerFlags = fs.readFileSync(path.join(build, 'build_flags'), 'utf8').trim().split('\n')[1].trim().split(/\s+/);
    const sourceList = /^SRCS\s*:=\s*(.+)$/m.exec(fs.readFileSync(path.join(source, 'Makefile'), 'utf8'));
    if (!sourceList) throw new Error('Controller source list missing');
    const dependencies = [];
    for (const file of [...sourceList[1].trim().split(/\s+/), 'startup.S']) {
        const text = run('clang', [...compilerFlags, '-M', file], source);
        fs.writeFileSync(path.join(build, `${file}.d`), text);
        const joined = text.replace(/\\\r?\n/g, ' ');
        const colon = joined.indexOf(':');
        if (colon < 0) throw new Error(`Dependency target missing: ${file}`);
        const inputs = joined.slice(colon + 1).trim().split(/\s+/);
        if (isolated && inputs.some(input => !inside(source, input))) throw new Error(`Dependency escaped standalone source: ${text}`);
        dependencies.push({ source: file, inputs });
    }
    const elf = path.join(build, 'cyw43_rx_fw.elf');
    const symbols = run('llvm-nm', ['--print-size', '--radix=x', '--defined-only', elf]);
    fs.writeFileSync(path.join(build, 'symbols.txt'), symbols);
    if (run('llvm-nm', ['--undefined-only', elf]).trim()) throw new Error(`Undefined symbols in ${elf}`);
    if (/\b(?:malloc|calloc|realloc|free|memcpy|memmove|memset|printf|puts)\s*$/m.test(symbols)) {
        throw new Error(`Unexpected library symbol in ${elf}`);
    }
    const psm = psmSymbol(symbols, /(?:^|\s)-DCLEAN_SDIO_SELF_TEST=/.test(flags));
    const psmBytes = psm && extractSymbol(fs.readFileSync(elf), psm.address, psm.bytes);
    if (psmBytes) fs.writeFileSync(path.join(build, 'psm.bin'), psmBytes);
    return {
        command: ['make', ...args], compilerFlags, dependencies,
        binarySha256: hash(fs.readFileSync(path.join(build, 'cyw43_rx_fw.bin'))),
        psmSha256: psmBytes ? hash(psmBytes) : null,
        sizes: run('llvm-size', [elf]).trim(),
    };
}

function verify(outputArgument, recordArgument) {
    const output = path.resolve(outputArgument);
    const record = recordArgument && path.resolve(recordArgument);
    if (fs.existsSync(output)) throw new Error(`Refusing to reuse evidence directory: ${output}`);
    if (record && fs.existsSync(record)) throw new Error(`Refusing to overwrite record: ${record}`);
    const frozen = JSON.parse(fs.readFileSync(path.join(root, 'clean-rx/dev-doc/c01-baseline.json'), 'utf8'));
    const owner = 'clean-rx/vendor-burst/Makefile';
    const ownerText = fs.readFileSync(path.join(root, owner), 'utf8');
    const originalOwner = run('git', ['show', `${frozen.provenance.revision}:${owner}`]);
    if (hash(originalOwner) !== frozen.inventory.find(entry => entry.file === owner).sha256) {
        throw new Error('Cannot reconstruct frozen C01 reference owner.');
    }
    if (!referenceProfileUnchanged(ownerText, originalOwner)) {
        throw new Error('Reference owner changed since C01; review the profile catalogue before comparing.');
    }
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c02-'));
    fs.mkdirSync(output, { recursive: true });
    const report = {
        schemaVersion: 1, capturedAt: new Date().toISOString(), status: 'in-progress',
        baselineRevision: frozen.provenance.revision, worktree: run('git', ['status', '--short']),
        tools: Object.fromEntries(['clang', 'cc', 'ld.lld', 'llvm-objcopy', 'llvm-nm', 'llvm-size', 'make']
            .map(tool => [tool, run(tool, ['--version']).trim()])),
        sourceHashes: {}, referenceSelections: [], profiles: [],
    };
    const manifest = path.join(output, 'manifest.json');
    const save = () => fs.writeFileSync(manifest, JSON.stringify(report, null, 2) + '\n');
    save();
    try {
        const before = path.join(temporary, 'before/clean-rx/src');
        const after = path.join(temporary, 'after/source');
        fs.mkdirSync(before, { recursive: true });
        fs.mkdirSync(after, { recursive: true });
        const productionFile = file => /(?:\.(c|h|S|ld)|\/Makefile)$/.test(file) && !file.endsWith('_test.c');
        for (const entry of frozen.inventory.filter(entry => entry.file.startsWith('clean-rx/src/') && productionFile(entry.file))) {
            const text = run('git', ['show', `${frozen.provenance.revision}:${entry.file}`]);
            if (hash(text) !== entry.sha256) throw new Error(`Cannot reconstruct frozen C01 input: ${entry.file}`);
            fs.writeFileSync(path.join(before, path.basename(entry.file)), text);
        }
        for (const file of ['src/shared/stdint.h', 'src/shared/bytes.h']) {
            const text = run('git', ['show', `${frozen.provenance.revision}:${file}`]);
            if (hash(text) !== frozen.inputHashes[file]) throw new Error(`C01 external input mismatch: ${file}`);
            const destination = path.join(temporary, 'before', file);
            fs.mkdirSync(path.dirname(destination), { recursive: true });
            fs.writeFileSync(destination, text);
        }
        for (const file of fs.readdirSync(path.join(root, 'clean-rx/src')).filter(file => productionFile(`src/${file}`))) {
            const bytes = fs.readFileSync(path.join(root, 'clean-rx/src', file));
            report.sourceHashes[file] = hash(bytes);
            fs.writeFileSync(path.join(after, file), bytes);
        }
        const resolver = resolverMakefile(ownerText);
        const resolverFile = path.join(temporary, 'profiles.mk');
        fs.writeFileSync(resolverFile, resolver.text);
        const profiles = new Map();
        const add = (id, flags, selection, baseline) => {
            const key = flagKey(flags);
            if (!profiles.has(key)) profiles.set(key, { id, flags, selections: [], ...(baseline ? { baselineId: baseline } : {}) });
            profiles.get(key).selections.push(selection);
        };
        for (const profile of frozen.profiles) add(profile.id, profile.flags, `C01:${profile.id}`, profile.id);
        for (const target of resolver.targets) {
            const text = run('make', ['--no-print-directory', '-s', '-f', resolverFile,
                `C02_RESOLVER=${resolverFile}`, target]);
            const selections = text.trim().split('\n').map(line => {
                const match = /^C02:(observer|stimulus):(.*)$/.exec(line);
                if (!match) throw new Error(`Unexpected resolver output: ${line}`);
                return { endpoint: match[1], flags: match[2] };
            });
            report.referenceSelections.push({ owner, target, selections });
            for (const selection of selections) add(`${target}-${selection.endpoint}`, selection.flags, `${target}:${selection.endpoint}`);
        }
        for (const profile of profiles.values()) {
            process.stdout.write(`Checking ${profile.id}\n`);
            const directory = path.join(output, profile.id);
            fs.mkdirSync(directory);
            const original = buildProfile(before, path.join(directory, 'before'), profile.flags, false);
            const isolated = buildProfile(after, path.join(directory, 'isolated'), profile.flags, true);
            if (original.binarySha256 !== isolated.binarySha256 || original.psmSha256 !== isolated.psmSha256) {
                throw new Error(`C02 changes emitted controller/PSM bytes: ${profile.id}`);
            }
            if (profile.baselineId) {
                const baseline = frozen.profiles.find(entry => entry.id === profile.baselineId);
                if (original.binarySha256 !== baseline.artifacts['cyw43_rx_fw.bin'].sha256 ||
                    original.psmSha256 !== (baseline.psm.sha256 || null)) {
                    throw new Error(`Toolchain/source no longer reproduces frozen C01: ${profile.id}`);
                }
            }
            report.profiles.push({ ...profile, original, isolated, status: 'identical' });
            save();
        }
        const probe = '#include "bytes.h"\n_Static_assert(sizeof(uintptr_t) == sizeof(void *), "pointer width");\n' +
            '_Static_assert(sizeof(size_t) == sizeof(sizeof(0)), "size width");\n' +
            'uint32_t probe(uint8_t *p) { clean_bytes_store_u32(p, 0x12345678u); return clean_bytes_load_u32(p); }\n';
        const probeFile = path.join(after, 'probe.c');
        fs.writeFileSync(probeFile, probe);
        report.targetProbes = ['cortex-m3', 'cortex-m33'].map(cpu => {
            const args = ['--target=arm-none-eabi', `-mcpu=${cpu}`, '-mthumb', '-std=c11',
                '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-nostdinc', '-fno-builtin',
                '-I', after, '-fsyntax-only', probeFile];
            run('clang', args);
            return { cpu, command: ['clang', ...args], status: 'passed' };
        });
        for (const [file, digest] of Object.entries(report.sourceHashes)) {
            if (hash(fs.readFileSync(path.join(root, 'clean-rx/src', file))) !== digest) throw new Error(`Input changed: ${file}`);
        }
        report.status = 'complete';
        save();
        if (record) fs.writeFileSync(record, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
        process.stdout.write(`PASS: ${report.profiles.length} unique flag sets; ${report.referenceSelections.length} reference entry points; isolated binaries and PSM match C01 source.\n`);
    } catch (error) {
        report.status = 'failed';
        report.failure = error.message;
        save();
        throw error;
    } finally {
        fs.rmSync(temporary, { recursive: true });
    }
}

module.exports = { flagKey, resolverMakefile, referenceProfileUnchanged, inside };
if (require.main === module) {
    try {
        if (process.argv.length < 3 || process.argv.length > 4) {
            throw new Error('Usage: node clean-rx/tools/c02-isolation.cjs NEW_EVIDENCE_DIRECTORY [NEW_RECORD_JSON]');
        }
        verify(process.argv[2], process.argv[3]);
    } catch (error) {
        console.error(error.stack);
        process.exitCode = 1;
    }
}
