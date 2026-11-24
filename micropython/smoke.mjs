import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { preserveFlash } from '../badge-compiler/tools/application-benchmark.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(here);
const build = path.join(here, 'build');
const serial = process.argv[2]?.toUpperCase();
const interpreterSplit = process.argv[3] === '--interpreter-split';
const interpreterPair = process.argv[3] === '--interpreter-pair';
const interpreterMatrix = process.argv[3] === '--interpreter-matrix';
const archivedRoles = interpreterMatrix ? ['baseline', 'first', 'second', 'both'] :
    interpreterPair ? ['baseline', 'candidate'] : null;
const interpreter = interpreterSplit || archivedRoles !== null || process.argv[3] === '--interpreter';
const vm = interpreter || process.argv[3] === '--vm';
const core = vm || process.argv[3] === '--core';
const picotool = process.env.PICOTOOL ?? path.join(root, 'build/picotool-usb/picotool');
const mpremote = process.env.MPREMOTE_PYTHON ?? 'mpremote';
const mpArgs = process.env.MPREMOTE_PYTHON ? ['-m', 'mpremote'] : [];
const sleep = () => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 250);
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
let capture;

function run(tool, args, timeout, log, allowDisconnect = false) {
    const r = spawnSync(tool, args, { cwd: root, encoding: 'utf8', timeout });
    const message = `${r.stdout ?? ''}${r.stderr ?? ''}${r.error?.message ?? ''}`;
    if (log) fs.writeFileSync(path.join(capture, `${log}.log`), message);
    if (!allowDisconnect && (r.error || r.status !== 0))
        throw new Error(`${tool}: ${r.error?.message ?? `exit ${r.status}`} ${message.trim()}`);
    return r.stdout ?? '';
}

function waitBootsel() {
    const deadline = Date.now() + 120000;
    while (Date.now() < deadline) {
        const info = spawnSync(picotool, ['info', '-d', '--ser', serial], { encoding: 'utf8', timeout: 2500 });
        if (info.status === 0 && /QFN80/.test(info.stdout)) return;
        sleep();
    }
    throw new Error('Badge did not return to BOOTSEL; use physical BOOTSEL recovery before restoring');
}

function command(args, label) {
    if (label.endsWith('-restore')) waitBootsel();
    const info = run(picotool, args, 180000, label);
    if (label.endsWith('-identity')) assert.match(info, /package:\s+QFN80\b/);
    return info;
}

function usbPorts() {
    const lines = run(mpremote, [...mpArgs, 'connect', 'list'], 5000).trim().split('\n');
    return lines.filter(line => /\b2e8a:0005\b/i.test(line)).map(line => line.split(/\s+/)[0]);
}

function waitRepl(before) {
    const deadline = Date.now() + 25000;
    while (Date.now() < deadline) {
        const added = usbPorts().filter(port => !before.includes(port));
        if (added.length > 1) throw new Error('Multiple MicroPython devices appeared; refusing to send REPL input');
        if (added.length === 1) return added[0];
        sleep();
    }
    throw new Error('The flashed badge did not expose a USB REPL');
}

function exec(port, code, label, timeout = 25000, allowDisconnect = false) {
    return run(mpremote, [...mpArgs, 'connect', port, 'exec', code], timeout, label, allowDisconnect);
}

function measure(port, phase, workload, marker) {
    const check = fs.readFileSync(path.join(here, `${workload}-smoke.py`), 'utf8');
    const result = exec(port, check, `${phase}-${workload}`);
    const match = result.match(new RegExp(`^${marker} ([0-9,]+)\\r?$`, 'm'));
    assert(match, `${phase}: missing ${workload} success marker`);
    const samples = match[1].split(',').map(Number);
    assert.equal(samples.length, 7);
    assert(samples.every(t => Number.isSafeInteger(t) && t > 0));
    process.stdout.write(result);
    return { samples, median: [...samples].sort((a, b) => a - b)[3] };
}

const python = (phase, index, count) => `
import gc, machine, os, sys
assert sys.platform == "rp2"
assert sys.implementation.version[:3] == (1, 29, 0)
assert sum(i*i for i in range(10)) == 285
assert 3.5 * 2.0 == 7.0
gc.collect()
def payload(seed):
    mask = (seed * 165) & 255
    return bytes((((i * 13 + (i >> 4)) & 255) ^ mask) for i in range(8193))
if ${index} != 0:
    with open("/crc-check.bin", "rb") as f:
        assert f.read() == payload(${index} - 1)
if ${index} != ${count} - 1:
    with open("/crc-next.bin", "wb") as f:
        data = payload(${index})
        assert f.write(data) == len(data)
    if ${index} != 0:
        os.remove("/crc-check.bin")
    os.rename("/crc-next.bin", "/crc-check.bin")
    os.sync()
    with open("/crc-check.bin", "rb") as f:
        assert f.read() == data
else:
    os.remove("/crc-check.bin")
    os.sync()
print(sys.version)
print(os.uname().machine)
print("MP_CLOCK_HZ", machine.freq())
print("MP_CRC_OK " + ${JSON.stringify(phase)})
`;

function measurePlan(variant) {
    if (!interpreterSplit && !archivedRoles)
        return [['stock', 'stock'], ['mixed', variant], ['stock-readback', 'stock']];
    const roles = archivedRoles ?? ['sequenceVm', 'runtimeOnly', 'vmOnly', 'interpreter'];
    return [...roles, ...[...roles].reverse()].map((role, i) => [`${i + 1}-${role}`, role]);
}

try {
    assert.match(serial ?? '', /^[0-9A-F]{16}$/, 'Pass the explicitly selected BOOTSEL serial');
    assert(archivedRoles ? process.argv.length === 4 + archivedRoles.length :
        process.argv.length === 3 || (core && process.argv.length === 4),
        'Expected a serial and optional --core, --vm, --interpreter, --interpreter-split, ' +
        '--interpreter-pair BASELINE_DIR CANDIDATE_DIR, or --interpreter-matrix BASELINE_DIR FIRST_DIR SECOND_DIR BOTH_DIR');
    usbPorts();
    const comparison = JSON.parse(fs.readFileSync(path.join(build, 'comparison.json'), 'utf8'));
    if (archivedRoles) {
        for (const [i, role] of archivedRoles.entries()) {
            const directory = path.resolve(process.argv[i + 4]);
            comparison[role] = { directory, bytes: fs.statSync(path.join(directory, 'firmware.bin')).size };
        }
    }
    capture = fs.mkdtempSync(path.join(build, 'smoke-'));
    const variant = interpreterMatrix ? 'interpreter-matrix' : interpreterPair ? 'interpreter-pair' : interpreterSplit ? 'interpreter-split' :
        interpreter ? 'interpreter' : vm ? 'vm' : core ? 'core' : 'mixed';
    const plan = measurePlan(variant);
    const images = {};
    for (const role of new Set(plan.map(([, role]) => role))) {
        assert(comparison[role], `Build the ${role} image before measuring`);
        const uf2 = path.join(comparison[role].directory, 'firmware.uf2');
        images[role] = { uf2, bytes: comparison[role].bytes, sha256: sha(uf2) };
    }
    if (archivedRoles)
        assert.equal(new Set(Object.values(images).map(image => image.sha256)).size, archivedRoles.length,
            'Archived comparisons require distinct UF2s');
    const report = { revision: comparison.revision, variant: variant === 'mixed' ? 'crc' : variant,
        images, order: plan.map(([, role]) => role), backups: [], restoration: [], errors: [], runs: [] };
    const save = () => fs.writeFileSync(path.join(capture, 'capture.json'), JSON.stringify(report, null, 2) + '\n');
    preserveFlash([serial], capture, command, report, save, () => {
        command(['erase', '-r', '0x10d00000', '0x11000000', '--ser', serial], 'erase-experiment-filesystem');
        let clock;
        for (const [index, [phase, role]] of plan.entries()) {
            const { uf2, sha256 } = images[role];
            assert.equal(sha(uf2), sha256, `${role}: UF2 changed during the campaign`);
            const ports = usbPorts();
            command(['load', '-v', uf2, '--ser', serial], `${phase}-flash`);
            command(['reboot', '--ser', serial], `${phase}-boot`);
            const port = waitRepl(ports);
            let output;
            try {
                output = exec(port, python(phase, index, plan.length), `${phase}-repl`);
                assert(output.includes(`MP_CRC_OK ${phase}`), `${phase}: missing REPL success marker`);
                const measuredClock = Number(output.match(/^MP_CLOCK_HZ (\d+)\r?$/m)?.[1]);
                assert(Number.isSafeInteger(measuredClock) && measuredClock > 0, `${phase}: missing clock`);
                clock ??= measuredClock;
                assert.equal(measuredClock, clock, `${phase}: clock changed`);
                const smallint = core ? measure(port, phase, 'smallint', 'MP_SMALLINT_OK') : undefined;
                const radix = vm ? measure(port, phase, 'radix', 'MP_RADIX_OK') : undefined;
                const calendar = vm ? measure(port, phase, 'calendar', 'MP_CALENDAR_OK') : undefined;
                const pythonVm = interpreter ? measure(port, phase, 'interpreter', 'MP_INTERPRETER_OK') : undefined;
                report.runs.push({ phase, role, clockHz: clock, uf2Sha256: sha256, output, smallint, radix, calendar, pythonVm });
                save();
                process.stdout.write(output);
            } finally {
                exec(port, 'import machine; machine.bootloader()', `${phase}-bootsel`, 6500, true);
                waitBootsel();
            }
        }
    });
    assert(report.restoration[0]?.verified);
    report.status = 'measured-restored';
    save();
    console.log(`Original flash verified. Report: ${path.join(capture, 'capture.json')}`);
} catch (error) {
    console.error(`MicroPython badge smoke: ${error.message}${capture ? `\nLogs: ${capture}` : ''}`);
    process.exitCode = 1;
}
