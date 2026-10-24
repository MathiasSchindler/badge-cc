#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '../..');
const controller = path.join(root, 'clean-rx/src');
const nativeTests = path.join(root, 'clean-rx/tests');
const testSources = {
    psm_ucode_test: 'unit/psm_ucode_test.c',
    mac_test: 'integration/mac_test.c',
    block_ack_test: 'unit/block_ack_test.c',
    rx_recovery_test: 'unit/rx_recovery_test.c',
    copy_scope_test: 'integration/copy_scope_test.c',
    early_rx_test: 'integration/early_rx_test.c',
};
const referenceOwner = 'clean-rx/vendor-burst/Makefile';
const ht = ['CLEAN_RX_HT_RX=1', 'CLEAN_RX_HT_DATA=1'];
const observer = ['CLEAN_RX_HT_RX=1', 'CLEAN_RX_HT_OBSERVER=1', 'CLEAN_RX_HT_EARLY_SAVE=1'];
const matrix = [
    { id: 'default', owner: 'clean-rx/src/Makefile', purpose: 'Ordinary controller', defines: [] },
    { id: 'ht-data', owner: 'clean-rx/messenger/Makefile', purpose: 'Messenger extended HT payload API', defines: ht },
    { id: 'wmm-edca', owner: 'clean-rx/doc/HT20.md', purpose: 'HT response, EDCA and WMM policy gates',
        defines: [...ht, 'CLEAN_RX_HT_RESPONSE=1', 'CLEAN_RX_EDCA=1', 'CLEAN_RX_WMM=1'] },
    { id: 'sdio-self-test', owner: 'clean-rx/src/main.c', purpose: 'Synthetic SDIO boundary traffic; no RF validation',
        defines: ['CLEAN_SDIO_SELF_TEST=1', 'CLEAN_SDIO_SELF_TEST_LARGE=1', 'CLEAN_SDIO_SELF_TEST_WINDOW=8'],
        psmNotLinked: true },
    { id: 'observer', owner: referenceOwner, purpose: 'Default reference observer controller',
        defines: [...observer, 'CLEAN_RX_HT_ADVANCE=1', 'REF_CLEAN_STIMULUS=0'] },
    { id: 'copy-scope', owner: referenceOwner, purpose: 'reference-handoff-scope-on controller',
        defines: [...observer, 'REF_CLEAN_STIMULUS=1', 'REF_CLEAN_HANDOFF=1',
            'CLEAN_RX_HT_ADVANCE=1', 'CLEAN_RX_HT_OBSERVER_FIFO=1', 'CLEAN_RX_HT_COPY_PROBE=1',
            'CLEAN_RX_HT_COPY_DEFER_STREAM=1', 'CLEAN_RX_HT_COPY_STREAM_PHASE=2',
            'CLEAN_RX_HT_COPY_STREAM_REARM=6', 'CLEAN_RX_HT_COPY_SCOPE=1'],
        extraTests: ['copy_scope_test'] },
    { id: 'early-prefix', owner: referenceOwner, purpose: 'reference-early-fifo-prefix controller',
        defines: [...observer, 'REF_CLEAN_STIMULUS=1', 'CLEAN_RX_HT_WINDOW=1',
            'CLEAN_RX_HT_REARM_WAIT=1', 'CLEAN_RX_HT_OBSERVER_FIFO=1', 'CLEAN_RX_HT_EARLY_FIFO=1',
            'CLEAN_RX_HT_REARM_RECOVER=1', 'CLEAN_RX_HT_REARM_INJECT=0', 'CLEAN_RX_HT_ORDERED_FLUSH=1',
            'CLEAN_RX_HT_PREFIX_RECOVERY=1'], extraTests: ['early_rx_test'] },
];
const ordinaryTests = ['psm_ucode_test', 'mac_test', 'block_ack_test', 'rx_recovery_test'];
const allTests = [...ordinaryTests, 'copy_scope_test', 'early_rx_test'];
const sourceRoles = {
    'main.c': ['controller-runtime', 'firmware-side-diagnostic'],
    'pmu.c': ['controller-runtime'],
    'radio_phy.c': ['controller-runtime'],
    'startup.S': ['controller-runtime'],
    'cyw43.ld': ['controller-runtime'],
    'types.h': ['controller-runtime'],
    'bytes.h': ['shared-abi-portable-runtime'],
    'cyw43_hw.h': ['controller-runtime', 'shared-abi-portable-runtime'],
    'mac.c': ['shared-abi-portable-runtime'],
    'mac.h': ['shared-abi-portable-runtime'],
    'block_ack.c': ['shared-abi-portable-runtime'],
    'block_ack.h': ['shared-abi-portable-runtime'],
    'wpa2_crypto.c': ['host-runtime'],
    'wpa2_crypto.h': ['host-runtime'],
    'sdio_ring.h': ['shared-abi-portable-runtime'],
    'phy_rates.h': ['shared-abi-portable-runtime'],
    'tx_diag.h': ['shared-abi-portable-runtime'],
    'frame_limits.h': ['shared-abi-portable-runtime', 'firmware-side-diagnostic'],
    'unicast.h': ['shared-abi-portable-runtime', 'firmware-side-diagnostic'],
    'ht20.h': ['shared-abi-portable-runtime', 'firmware-side-diagnostic', 'development-tool'],
    'ht_observer.h': ['controller-runtime', 'firmware-side-diagnostic'],
    'psm_ack.h': ['controller-runtime', 'firmware-side-diagnostic'],
    'psm_ucode.c': ['controller-runtime', 'firmware-side-diagnostic'],
    'copy_scope.h': ['firmware-side-diagnostic'],
    'rx_recovery.h': ['firmware-side-diagnostic'],
    'early_rx.h': ['firmware-side-diagnostic'],
    'observer_dma.h': ['firmware-side-diagnostic'],
    'sdio_test.h': ['firmware-side-diagnostic'],
    'mac_soak.h': ['development-tool', 'shared-test-fixture'],
};

function sha256(bytes) {
    return crypto.createHash('sha256').update(bytes).digest('hex');
}

function run(command, args, cwd = root, logFile) {
    const result = spawnSync(command, args, {
        cwd, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024,
        env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' },
    });
    if (result.error) throw result.error;
    if (logFile) fs.writeFileSync(logFile, result.stdout + result.stderr);
    if (result.status !== 0) {
        throw new Error(`${command} ${args.join(' ')} failed (${result.status})\n${result.stdout}${result.stderr}`);
    }
    return result.stdout + result.stderr;
}

function classify(file) {
    const name = path.basename(file);
    if (file.endsWith('.md')) return ['documentation'];
    if (name === 'Makefile') return ['build-infrastructure'];
    if (file === 'clean-rx/tests/profiles.mk') return ['build-infrastructure'];
    if (file.startsWith('clean-rx/tests/')) return ['native-test'];
    if (name.endsWith('_test.c') || name.endsWith('.test.cjs')) return ['native-test'];
    if (file.startsWith('clean-rx/tools/')) return ['development-tool'];
    if (file.startsWith('clean-rx/src/')) {
        if (!sourceRoles[name]) throw new Error(`Unclassified controller file: ${file}`);
        return sourceRoles[name];
    }
    if (/\.(sh|cjs|js)$/.test(name) || name === 'collector.c') return ['development-tool'];
    if (file.startsWith('clean-rx/vendor-burst/')) return ['firmware-side-diagnostic', 'shared-test-fixture'];
    if (file.startsWith('clean-rx/peer/')) return ['host-runtime', 'shared-test-fixture'];
    if (file.startsWith('clean-rx/badge/') || file.startsWith('clean-rx/messenger/')) return ['host-runtime'];
    if (file.startsWith('clean-rx/dev-doc/') && file.endsWith('.json')) return ['baseline-evidence'];
    throw new Error(`Unclassified file: ${file}`);
}

function referenceTargets(text) {
    const entries = new Map();
    let current;
    for (const [index, line] of text.split('\n').entries()) {
        const match = /^(reference(?:-[\w-]+)?):(?:\s|$)/.exec(line);
        if (match) {
            current = match[1];
            if (!entries.has(current)) entries.set(current, { target: current, line: index + 1, recipes: [] });
        } else if (line.startsWith('\t') && current) {
            entries.get(current).recipes.push(line.trim());
        } else if (line && !line.startsWith('#') && !/^(ifeq|ifneq|else|endif)/.test(line)) {
            current = undefined;
        }
    }
    return [...entries.values()].filter(entry => entry.target !== 'reference-force').map(entry => ({
        ...entry, owner: referenceOwner,
        command: `make -f ${referenceOwner} ${entry.target}`,
        purpose: entry.target.replace(/^reference-?/, '').replaceAll('-', ' ') || 'vendor/observer reference pair',
        lifecycle: 'retained build entry point; experimental, not a production support claim',
        coverageGap: 'Full host/peer build and RF campaign not executed by C01; controller subset coverage is listed separately.',
    }));
}

function dependencies(text, cwd = controller) {
    const joined = text.replace(/\\\r?\n/g, ' ');
    const colon = joined.indexOf(':');
    if (colon < 0) throw new Error('Compiler dependency output has no target');
    return [...new Set(joined.slice(colon + 1).trim().split(/\s+/)
        .map(file => path.relative(root, path.resolve(cwd, file))))].sort();
}

function extractSymbol(elf, address, size) {
    if (elf.subarray(0, 6).toString('hex') !== '7f454c460101') throw new Error('Expected little-endian ELF32');
    if (!Number.isInteger(size) || size <= 0) throw new Error('Invalid symbol size');
    const offset = elf.readUInt32LE(32);
    const stride = elf.readUInt16LE(46);
    const count = elf.readUInt16LE(48);
    if (stride < 40 || offset + stride * count > elf.length) throw new Error('Invalid section table');
    for (let i = 0; i < count; ++i) {
        const at = offset + stride * i;
        const type = elf.readUInt32LE(at + 4);
        const base = elf.readUInt32LE(at + 12);
        const start = elf.readUInt32LE(at + 16);
        const length = elf.readUInt32LE(at + 20);
        if (type === 1 && address >= base && address + size <= base + length) {
            const begin = start + address - base;
            if (begin + size > elf.length) throw new Error('Symbol exceeds ELF file');
            return elf.subarray(begin, begin + size);
        }
    }
    throw new Error('Symbol has no file-backed section');
}

function testStatus(output) {
    return output.includes('RX recovery diagnostic disabled; no recovery program selected') ? 'disabled' : 'passed';
}

function psmSymbol(symbols, notLinked) {
    const match = /^([0-9a-f]+)\s+([0-9a-f]+)\s+\w\s+psm_ucode_bytes$/m.exec(symbols);
    if (!match && !notLinked) throw new Error('PSM symbol missing');
    if (match && notLinked) throw new Error('Unexpected PSM linked into transport-only image');
    return match && { address: parseInt(match[1], 16), bytes: parseInt(match[2], 16) };
}

function captureProfile(profile, output) {
    const build = path.join(output, profile.id);
    fs.mkdirSync(build);
    const flags = profile.defines.map(value => `-D${value}`).join(' ');
    const tests = [...ordinaryTests, ...(profile.extraTests || [])];
    const args = ['-C', controller, 'CC=clang', `BUILD=${build}`, `EXTRA_CFLAGS=${flags}`,
        'all'];
    run('make', args, root, path.join(build, 'build.log'));
    const testArgs = ['-C', nativeTests, `BUILD=${build}`, `EXTRA_CFLAGS=${flags}`,
        ...tests.map(test => path.join(build, test))];
    run('make', testArgs, root, path.join(build, 'test-build.log'));
    run('make', ['-C', nativeTests, `BUILD=${build}`, `EXTRA_CFLAGS=${flags}`, 'runtime-test'],
        root, path.join(build, 'runtime-test.log'));
    const buildFlags = fs.readFileSync(path.join(build, 'build_flags'), 'utf8').trim().split('\n');
    const compilerFlags = buildFlags[1].trim().split(/\s+/);
    const sourcesMatch = /^SRCS\s*:=\s*(.+)$/m.exec(fs.readFileSync(path.join(controller, 'Makefile'), 'utf8'));
    if (!sourcesMatch) throw new Error('Cannot identify controller source list');
    const sources = [...sourcesMatch[1].trim().split(/\s+/), 'startup.S'];
    const inputs = sources.map(source => {
        const dep = run('clang', [...compilerFlags, '-M', source], controller);
        fs.writeFileSync(path.join(build, `${source}.d`), dep);
        return { source: `clean-rx/src/${source}`, object: source.replace(/\.(c|S)$/, '.o'),
            dependencies: dependencies(dep) };
    });
    const elfPath = path.join(build, 'cyw43_rx_fw.elf');
    const symbols = run('llvm-nm', ['--print-size', '--radix=x', '--defined-only', elfPath]);
    fs.writeFileSync(path.join(build, 'symbols.txt'), symbols);
    const psm = psmSymbol(symbols, profile.psmNotLinked);
    let psmEvidence = { status: 'not-linked', reason: 'Transport-only variant bypasses radio/PSM startup; linker garbage-collects the PSM image.' };
    if (psm) {
        const psmBytes = extractSymbol(fs.readFileSync(elfPath), psm.address, psm.bytes);
        if (psmBytes.length % 8) throw new Error('PSM image is not whole instructions');
        fs.writeFileSync(path.join(build, 'psm.bin'), psmBytes);
        psmEvidence = { ...psm, status: 'linked', sha256: sha256(psmBytes) };
    }
    const undefinedSymbols = run('llvm-nm', ['--undefined-only', elfPath]).trim();
    if (undefinedSymbols) throw new Error(`Unexpected undefined symbols: ${undefinedSymbols}`);
    const sizeOutput = run('llvm-size', [elfPath]);
    const sizes = /^\s*(\d+)\s+(\d+)\s+(\d+)\s+\d+\s+[0-9a-f]+\s+/m.exec(sizeOutput);
    if (!sizes) throw new Error('Unrecognized llvm-size output');
    const sections = run('llvm-size', ['--format=sysv', elfPath]);
    fs.writeFileSync(path.join(build, 'sections.txt'), sections);
    const testResults = allTests.map(test => {
        if (!tests.includes(test)) return { test, status: 'not-selected', reason: 'Requires a dedicated diagnostic profile.' };
        const testOutput = run(path.join(build, test), [], root, path.join(build, `${test}.log`));
        const dep = run('cc', ['-std=c11', ...profile.defines.map(value => `-D${value}`),
            '-I../src', '-Isupport', '-MM', testSources[test]], nativeTests);
        fs.writeFileSync(path.join(build, `${test}.d`), dep);
        return { test, status: testStatus(testOutput), logSha256: sha256(testOutput),
            source: `clean-rx/tests/${testSources[test]}`,
            dependencies: dependencies(dep, nativeTests),
            ...(testStatus(testOutput) === 'disabled' ? { reason: 'Ordered-flush diagnostic was not selected.' } : {}) };
    });
    const files = ['cyw43_rx_fw.bin', 'cyw43_rx_fw.elf', 'cyw43_rx_fw.map', ...(psm ? ['psm.bin'] : []),
        ...inputs.map(input => input.object)];
    return {
        ...profile, flags, command: ['make', ...args], testCommand: ['make', ...testArgs], compilerFlags, inputs,
        linkerScript: 'clean-rx/src/cyw43.ld',
        externalIncludes: [...new Set(inputs.flatMap(input => input.dependencies)
            .filter(file => !file.startsWith('clean-rx/src/')))].sort(),
        artifacts: Object.fromEntries(files.map(file => {
            const bytes = fs.readFileSync(path.join(build, file));
            return [file, { sha256: sha256(bytes), bytes: bytes.length }];
        })),
        sizes: { text: Number(sizes[1]), data: Number(sizes[2]), bss: Number(sizes[3]) },
        psm: psmEvidence,
        tests: testResults,
        coverageGap: 'Native tests model selected helpers/PSM paths, not execution of main.c on silicon; no host-app build or RF qualification.',
    };
}

function capture(outputArgument, recordArgument) {
    const output = path.resolve(outputArgument);
    const record = recordArgument && path.resolve(recordArgument);
    if (fs.existsSync(output)) throw new Error(`Refusing to reuse evidence directory: ${output}`);
    if (record && (fs.existsSync(record) || record.startsWith(`${output}${path.sep}`))) {
        throw new Error('Record must be a new file outside the evidence directory');
    }
    const provenance = {
        revision: run('git', ['rev-parse', 'HEAD']).trim(),
        worktree: run('git', ['status', '--short']),
        platform: `${process.platform}/${process.arch}`, node: process.version,
        tools: Object.fromEntries(['clang', 'cc', 'ld.lld', 'llvm-objcopy', 'llvm-nm', 'llvm-size', 'make']
            .map(tool => [tool, run(tool, ['--version']).trim()])),
    };
    const files = run('git', ['ls-files', '--cached', '--others', '--exclude-standard', '-z', '--', 'clean-rx'])
        .split('\0').filter(Boolean);
    const inventory = [...new Set(files)].filter(file => fs.existsSync(path.join(root, file))).sort().map(file => ({
        file, roles: classify(file), sha256: sha256(fs.readFileSync(path.join(root, file))),
        ...(/\.(c|h|S)$/.test(file) ? {
            includes: [...fs.readFileSync(path.join(root, file), 'utf8')
                .matchAll(/^\s*#include\s+([<"][^>"]+[>"])/gm)].map(match => match[1]),
        } : {}),
    }));
    const externalInputPaths = ['src/shared/stdint.h', 'src/shared/bytes.h',
        'src/tools/diagnostics_logic.c', 'src/tools/diagnostics_logic.h'];
    const externalInputHashes = Object.fromEntries(externalInputPaths.map(file =>
        [file, sha256(fs.readFileSync(path.join(root, file)))]));
    const catalogue = referenceTargets(fs.readFileSync(path.join(root, referenceOwner), 'utf8'));
    const flagReferences = inventory.filter(entry => /(?:Makefile|\.md|\.sh)$/.test(entry.file))
        .flatMap(entry => fs.readFileSync(path.join(root, entry.file), 'utf8').split('\n')
            .flatMap((line, index) => /EXTRA_CFLAGS|CLEAN_(?:RX_\w+|SDIO_SELF_TEST\w*)\s*=/.test(line)
                ? [{ file: entry.file, line: index + 1, text: line }] : []));
    const flagDefaults = inventory.filter(entry => entry.file.startsWith('clean-rx/src/') && entry.file.endsWith('.h'))
        .flatMap(entry => [...fs.readFileSync(path.join(root, entry.file), 'utf8')
            .matchAll(/#ifndef (CLEAN_RX_\w+|CLEAN_SDIO_SELF_TEST\w*)\s*\n#define \1 ([^\n]+)/g)]
            .map(match => ({ owner: entry.file, flag: match[1], default: match[2] })));
    fs.mkdirSync(output, { recursive: true });
    const report = {
        schemaVersion: 1, capturedAt: new Date().toISOString(), provenance, evidenceDirectory: output,
        inventory, referenceProfiles: catalogue, flagReferences, flagDefaults, externalInputHashes,
        consumers: [
            { owner: 'clean-rx/badge/Makefile', command: 'make -C clean-rx/badge all test',
                purpose: 'RP2350 host, collector and station/security integration tests',
                selection: 'EXTRA_CFLAGS propagated to the default controller; CONTROLLER_BIN selects an independently built image.' },
            { owner: 'clean-rx/messenger/Makefile', command: 'make -C clean-rx/messenger pair test',
                purpose: 'Two-role autonomous application, HT data controller and collector',
                selection: 'FLAGS selects HT data/extended RX; ROLE and BASELINE select host application variants.' },
            { owner: 'clean-rx/peer/Makefile', command: 'make -f clean-rx/peer/Makefile ht-peer ht-peer-test',
                purpose: 'Independent vendor-firmware peer and host-side tests, not a source-controller image',
                selection: 'HT_PEER_PICO=0 or 1 selects host board support; consumes portable crypto and fixture contracts.' },
            { owner: referenceOwner, command: `make -f ${referenceOwner} reference reference-test`,
                purpose: 'Vendor/stimulus and observer/monitor diagnostic hosts and collector',
                selection: 'REF_FLAGS and named wrapper recipes select controller experiments; REF_STIMULUS and REF_MONITOR select endpoint firmware.' },
        ].map(consumer => ({ ...consumer,
            coverageGap: 'Not built or run by the C01 controller matrix. No device or RF execution; board/host overrides are not exhaustively tested.' })),
        profiles: [], status: 'in-progress',
    };
    const manifest = path.join(output, 'manifest.json');
    fs.writeFileSync(manifest, JSON.stringify(report, null, 2) + '\n');
    for (const profile of matrix) {
        process.stdout.write(`Capturing ${profile.id}\n`);
        try {
            report.profiles.push(captureProfile(profile, output));
        } catch (error) {
            report.status = 'failed';
            report.failure = { profile: profile.id, message: error.message };
            fs.writeFileSync(manifest, JSON.stringify(report, null, 2) + '\n');
            throw error;
        }
        fs.writeFileSync(manifest, JSON.stringify(report, null, 2) + '\n');
    }
    const changed = inventory.filter(entry => !fs.existsSync(path.join(root, entry.file)) ||
        sha256(fs.readFileSync(path.join(root, entry.file))) !== entry.sha256);
    const externalInputs = [...new Set(report.profiles.flatMap(profile => profile.inputs
        .flatMap(input => input.dependencies)).concat('clean-rx/src/cyw43.ld', externalInputPaths))].sort();
    report.inputHashes = Object.fromEntries(externalInputs.map(file =>
        [file, sha256(fs.readFileSync(path.join(root, file)))]));
    if (changed.length) throw new Error(`Source changed during capture: ${changed.map(entry => entry.file).join(', ')}`);
    for (const [file, hash] of Object.entries(externalInputHashes)) {
        if (report.inputHashes[file] !== hash) throw new Error(`External input changed during capture: ${file}`);
    }
    report.status = 'complete';
    fs.writeFileSync(manifest, JSON.stringify(report, null, 2) + '\n');
    if (record) fs.writeFileSync(record, JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
    process.stdout.write(`Captured ${inventory.length} files, ${catalogue.length} reference entry points and ${matrix.length} controller profiles.\n${manifest}\n`);
}

module.exports = { classify, referenceTargets, dependencies, extractSymbol, testStatus, psmSymbol, matrix };
if (require.main === module) {
    try {
        if (process.argv.length < 3 || process.argv.length > 4) {
            throw new Error('Usage: node clean-rx/tools/c01-baseline.cjs NEW_EVIDENCE_DIRECTORY [NEW_RECORD_JSON]');
        }
        capture(process.argv[2], process.argv[3]);
    } catch (error) {
        console.error(error.stack);
        process.exitCode = 1;
    }
}
