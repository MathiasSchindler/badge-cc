'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { classify, matrix } = require('./c01-baseline.cjs');
const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'clean-rx/src');

function execute(command, args, cwd = root) {
    const result = spawnSync(command, args, { cwd, encoding: 'utf8', maxBuffer: 8 * 1024 * 1024,
        env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' } });
    if (result.error) throw result.error;
    return { status: result.status, text: result.stdout + result.stderr };
}

function run(command, args, cwd) {
    const result = execute(command, args, cwd);
    assert.equal(result.status, 0, result.text);
    return result.text;
}

test('actual runtime dispatch guard reports diagnostic commands through firmware error 0x85', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c05-command-'));
    try {
        const main = fs.readFileSync(path.join(source, 'main.c'), 'utf8');
        assert.ok(main.includes('        uint32_t cmd = MBOX->host_cmd;\n#include "runtime/command_guard.inc"'));
        const guard = '        uint32_t cmd = MBOX->host_cmd;\n' +
            fs.readFileSync(path.join(source, 'runtime/command_guard.inc'), 'utf8');
        assert.ok(guard.includes('fail(0x85u, FW_MBOX_ADDR + 24u, cmd);'));
        fs.writeFileSync(path.join(directory, 'command.c'), `
#include <assert.h>
#include <stdint.h>
#include "firmware_command.h"
static struct { uint32_t host_cmd; } mailbox;
static uint8_t packet[1];
static uint32_t error, error_address, error_value;
#define MBOX (&mailbox)
#define FW_TX_BUF_ADDR ((uintptr_t)packet)
#define FW_MBOX_ADDR 0x20000u
static __attribute__((unused)) int fail(uint32_t code, uint32_t address, uint32_t value) {
    error = code; error_address = address; error_value = value;
    return 0;
}
static int dispatch(void) {
    for (;;) {
${guard}
        (void)cmd;
        return 1;
    }
    return 0;
}
int main(void) {
    for (uint32_t kind = 0; kind < 256; ++kind)
        for (uint32_t version = 0; version < 256; ++version) {
            mailbox.host_cmd = kind | (17u << 8u);
            packet[0] = (uint8_t)version;
            error = error_address = error_value = 0;
            int allowed = CLEAN_RX_DIAGNOSTIC ||
                clean_runtime_command_allowed(mailbox.host_cmd, packet[0]);
            assert(dispatch() == allowed);
            if (allowed) assert(!error && !error_address && !error_value);
            else assert(error == 0x85u && error_address == FW_MBOX_ADDR + 24u &&
                        error_value == mailbox.host_cmd);
        }
    return 0;
}
`);
        for (const mode of [0, 1]) {
            const executable = path.join(directory, `command-${mode}`);
            run('cc', ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
                '-fsanitize=address,undefined', `-DCLEAN_RX_DIAGNOSTIC=${mode}`,
                '-I', source, path.join(directory, 'command.c'), '-o', executable]);
            run(executable, []);
        }
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});

test('named profiles are freestanding, identified, rebuild safely and reject incompatible options', () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c05-build-'));
    try {
        const build = path.join(directory, 'build');
        const make = extra => run('make', ['-C', source, 'CC=clang', `BUILD=${build}`, ...extra, 'all']);
        const bytes = () => fs.readFileSync(path.join(build, 'cyw43_rx_fw.bin'));
        make([]);
        const runtime = bytes();
        assert.ok(!make([]).includes('-c main.c'), 'Unchanged configuration rebuilt firmware');
        const runtimeFlags = fs.readFileSync(path.join(build, 'build_flags'), 'utf8').split('\n')[1].trim().split(/\s+/);
        const dependencies = run('clang', [...runtimeFlags, '-M', 'main.c', 'psm_ucode.c'], source);
        assert.ok(!dependencies.includes('diagnostic/'), dependencies);
        assert.match(fs.readFileSync(path.join(build, 'profile.txt'), 'utf8'), /^profile=runtime\nprofile_id=0\n/);
        for (const [profile, id] of [['diagnostic', 1], ['sdio-test', 2], ['runtime', 0]]) {
            const output = make([`PROFILE=${profile}`]);
            assert.ok(output.includes('-c main.c'), `${profile} did not rebuild`);
            assert.match(fs.readFileSync(path.join(build, 'profile.txt'), 'utf8'),
                new RegExp(`^profile=${profile}\\nprofile_id=${id}\\n`));
            const symbols = run('llvm-nm', [path.join(build, 'cyw43_rx_fw.elf')]);
            assert.match(symbols, new RegExp(`^0*${id} A clean_rx_profile_id$`, 'm'));
            assert.equal(run('llvm-nm', ['-u', path.join(build, 'cyw43_rx_fw.elf')]).trim(), '');
            if (profile === 'runtime') assert.deepEqual(bytes(), runtime);
            else assert.notDeepEqual(bytes(), runtime);
        }
        const recovery = matrix.find(profile => profile.id === 'early-prefix');
        const reset = [...recovery.defines.filter(value =>
            !/^CLEAN_RX_HT_(ORDERED_FLUSH|PREFIX_RECOVERY)=/.test(value)), 'CLEAN_RX_HT_REARM_RESET=1'];
        for (const defines of [recovery.defines, reset]) {
            const recoveryFlags = defines.map(value => `-D${value}`).join(' ');
            make(['PROFILE=runtime', `EXTRA_CFLAGS=${recoveryFlags}`]);
            const recoveryCompilerFlags = fs.readFileSync(path.join(build, 'build_flags'), 'utf8').split('\n')[1].trim().split(/\s+/);
            assert.ok(!run('clang', [...recoveryCompilerFlags, '-M', 'main.c', 'psm_ucode.c'], source).includes('diagnostic/'));
        }
        const invalid = [
            ['PROFILE=unknown'],
            ['EXTRA_CFLAGS=-DCLEAN_RX_DIAGNOSTIC=1'],
            ['EXTRA_CFLAGS=-DCLEAN_SDIO_SELF_TEST=1'],
            ['EXTRA_CFLAGS=-DCLEAN_SDIO_SELF_TEST=0'],
            ['EXTRA_CFLAGS=-DCLEAN_SDIO_SELF_TEST_WINDOW=1u'],
            ['EXTRA_CFLAGS=-DCLEAN_RX_HT_PROBE=1'],
            ['EXTRA_CFLAGS=-DCLEAN_RX_HT_CLEAR_SIG_CRC=1'],
            ['EXTRA_CFLAGS=-DCLEAN_RX_HT_RX=1 -DCLEAN_RX_HT_OBSERVER=1 -DCLEAN_RX_HT_EARLY_SAVE=1 -DCLEAN_RX_HT_WINDOW=1 -DCLEAN_RX_HT_REARM_WAIT=1 -DCLEAN_RX_HT_REARM_RECOVER=1 -DCLEAN_RX_HT_REARM_INJECT=1'],
            ['PROFILE=sdio-test', 'EXTRA_CFLAGS=-DCLEAN_SDIO_SELF_TEST=0'],
        ];
        for (const flags of invalid) {
            const result = execute('make', ['-C', source, 'CC=clang', `BUILD=${build}`, ...flags, 'all']);
            assert.notEqual(result.status, 0, `Accepted incompatible options: ${flags}\n${result.text}`);
            assert.match(result.text, /error:|error "|\*\*\*/i);
        }
        const isolated = path.join(directory, 'src');
        fs.mkdirSync(isolated);
        for (const name of fs.readdirSync(source, { recursive: true }))
            if (!name.startsWith('diagnostic/') && (/\.(c|h|S|ld|mk|inc)$/.test(name) || name === 'Makefile')) {
                fs.mkdirSync(path.dirname(path.join(isolated, name)), { recursive: true });
                fs.copyFileSync(path.join(source, name), path.join(isolated, name));
            }
        run('make', ['-C', isolated, 'CC=clang', `BUILD=${path.join(directory, 'isolated')}`, 'all']);
        assert.deepEqual(fs.readFileSync(path.join(directory, 'isolated/cyw43_rx_fw.bin')), runtime);
        assert.deepEqual(classify('clean-rx/src/diagnostic/sdio_run.inc'), ['firmware-side-diagnostic']);
    } finally {
        fs.rmSync(directory, { recursive: true });
    }
});
