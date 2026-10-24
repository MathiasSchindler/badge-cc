'use strict';

const { spawn } = require('node:child_process');
const { performance } = require('node:perf_hooks');

const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

async function supervise(command, args, milliseconds) {
    const deadline = performance.now() + milliseconds;
    const child = spawn(command, args, { stdio: 'inherit', detached: true });
    let stopped = 0, escalation, timer, spawnError = false;
    const signalGroup = signal => {
        if (!child.pid) return;
        try { process.kill(-child.pid, signal); }
        catch (error) { if (error.code !== 'ESRCH') throw error; }
    };
    const stop = code => {
        if (stopped) return;
        stopped = code;
        console.error(`USB process ${child.pid}: ${code === 124 ? 'deadline expired' : 'cancelled'}`);
        signalGroup('SIGTERM');
        escalation = delay(500).then(() => signalGroup('SIGKILL'));
    };
    const interrupt = () => stop(130), terminate = () => stop(143);
    process.on('SIGINT', interrupt);
    process.on('SIGTERM', terminate);
    timer = setTimeout(() => stop(124), Math.max(0, deadline - performance.now()));
    const code = await new Promise(resolve => {
        child.on('error', error => {
            spawnError = true;
            console.error(`Cannot start USB subprocess: ${error.message}`);
        });
        child.on('close', (status, signal) => {
            if (!spawnError && performance.now() >= deadline) stop(124);
            if (signal) console.error(`USB process ${child.pid}: terminated by ${signal}`);
            resolve(spawnError ? 127 : status ?? 1);
        });
    });
    clearTimeout(timer);
    if (code !== 0) console.error(`USB process ${child.pid ?? 'not started'}: exit ${code}`);
    // A child may exit before a descendant that ignored TERM.
    if (escalation) await escalation;
    else signalGroup('SIGKILL');
    process.removeListener('SIGINT', interrupt);
    process.removeListener('SIGTERM', terminate);
    return stopped || code;
}

async function main(args) {
    const [mode, secondsText, command, ...commandArgs] = args;
    const seconds = Number(secondsText);
    if (!['run', 'boot', 'ready'].includes(mode) || !command ||
        !Number.isFinite(seconds) || seconds <= 0 || seconds > 86400)
        throw new Error('Usage: node usb-process.cjs run|boot|ready SECONDS COMMAND [ARG ...]');
    const deadline = performance.now() + seconds * 1000;
    if (mode === 'run') return supervise(command, commandArgs, seconds * 1000);
    if (mode === 'boot' && !/^[0-9A-F]{16}$/.test(commandArgs[0] ?? ''))
        throw new Error('boot requires a 16-digit serial');
    while (performance.now() < deadline) {
        const left = () => Math.max(1, deadline - performance.now());
        const result = await supervise(command,
            mode === 'boot' ? ['info', '--ser', commandArgs[0]] : commandArgs,
            Math.min(2000, left()));
        if ([124, 127, 130, 143].includes(result)) return result;
        if (performance.now() >= deadline) {
            console.error(`USB ${mode}: readiness deadline expired`);
            return 124;
        }
        if (result === 0) {
            if (mode === 'ready') return 0;
            return supervise(command, ['reboot', '--ser', commandArgs[0]], Math.min(15000, left()));
        }
        if (mode === 'ready' && result !== 75) return result;
        await delay(Math.min(100, left()));
    }
    console.error(`USB ${mode}: readiness deadline expired`);
    return 124;
}

module.exports = { supervise, main };
if (require.main === module)
    main(process.argv.slice(2)).then(code => { process.exitCode = code; })
        .catch(error => { console.error(error.message); process.exitCode = 1; });
