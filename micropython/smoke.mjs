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

const python = phase => `
import gc, os, sys
assert sys.platform == "rp2"
assert sys.implementation.version[:3] == (1, 29, 0)
assert sum(i*i for i in range(10)) == 285
assert 3.5 * 2.0 == 7.0
gc.collect()
payload = bytes((i * 13 + (i >> 4)) & 255 for i in range(8193))
alternate = bytes((n ^ 165) for n in payload)
if ${JSON.stringify(phase)} != "stock":
    with open("/crc-check.bin", "rb") as f:
        assert f.read() == (payload if ${JSON.stringify(phase)} == "mixed" else alternate)
if ${JSON.stringify(phase)} != "stock-readback":
    with open("/crc-next.bin", "wb") as f:
        data = payload if ${JSON.stringify(phase)} == "stock" else alternate
        assert f.write(data) == len(data)
    if ${JSON.stringify(phase)} == "mixed":
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
print("MP_CRC_OK " + ${JSON.stringify(phase)})
`;

try {
    assert.match(serial ?? '', /^[0-9A-F]{16}$/, 'Pass the explicitly selected BOOTSEL serial');
    usbPorts();
    const comparison = JSON.parse(fs.readFileSync(path.join(build, 'comparison.json'), 'utf8'));
    capture = fs.mkdtempSync(path.join(build, 'smoke-'));
    const report = { revision: comparison.revision, backups: [], restoration: [], errors: [], runs: [] };
    const save = () => fs.writeFileSync(path.join(capture, 'capture.json'), JSON.stringify(report, null, 2) + '\n');
    preserveFlash([serial], capture, command, report, save, () => {
        command(['erase', '-r', '0x10d00000', '0x11000000', '--ser', serial], 'erase-experiment-filesystem');
        for (const phase of ['stock', 'mixed', 'stock-readback']) {
            const image = comparison[phase === 'mixed' ? 'mixed' : 'stock'];
            const uf2 = path.join(image.directory, 'firmware.uf2');
            const ports = usbPorts();
            command(['load', '-v', uf2, '--ser', serial], `${phase}-flash`);
            command(['reboot', '--ser', serial], `${phase}-boot`);
            const port = waitRepl(ports);
            let output;
            try {
                output = exec(port, python(phase), `${phase}-repl`);
                assert(output.includes(`MP_CRC_OK ${phase}`), `${phase}: missing REPL success marker`);
                report.runs.push({ phase, uf2Sha256: sha(uf2), output });
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
