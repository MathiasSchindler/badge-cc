'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const sources = ['main.c', 'pmu.c', 'radio_phy.c', 'psm_ucode.c', 'mac.c', 'block_ack.c'];
const exportsAllowed = new Set([
    '__bss_start', '__bss_end', 'clean_rx_profile_id', 'cyw43_vectors',
    'cyw43_reset', 'cyw43_fault', 'cyw43_main', 'pmu_init_pll_and_resources',
    'd11_and_phy_cold_init', 'phy_fast_switch_channel_2g', 'phy_tune_channel_2g',
    'phy_tx_hw_configure', 'phy_tx_set_automatic', 'phy_tx_set_inhibit',
    'psm_ucode_byte_count', 'psm_ucode_bytes', 'clean_ampdu_delimiter_crc',
    'clean_dcf_bad_rx', 'clean_dcf_complete', 'clean_dcf_good_rx', 'clean_dcf_init',
    'clean_dcf_nav', 'clean_dcf_new_frame', 'clean_dcf_poll', 'clean_dcf_slot',
    'clean_edca_configure', 'clean_edca_decode', 'clean_edca_select',
    'clean_edca_txop_valid', 'clean_edca_valid', 'clean_mac_fcs', 'clean_mac_nav_duration',
    'clean_mac_retry', 'clean_mac_rx_relevant', 'clean_mac_valid',
]);
const forbiddenInput = /(?:^|\/)(?:tests?|fixtures?|support|tools|host|peer|badge|messenger|vendor-burst)(?:\/|$)|(?:^|\/)[^/]*_test\.(?:c|cc|cpp)$/i;

function contained(source, file) {
    const relative = path.relative(fs.realpathSync(source), fs.realpathSync(file));
    return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}

function checkInput(source, file) {
    if (!contained(source, file)) throw new Error(`Controller input escaped src: ${file}`);
    const relative = path.relative(fs.realpathSync(source), fs.realpathSync(file)).split(path.sep).join('/');
    if (forbiddenInput.test(relative)) throw new Error(`Test/host/tool dependency in controller: ${relative}`);
}

function checkSourceTree(source) {
    source = path.resolve(source);
    const inputs = [];
    function visit(directory) {
        for (const name of fs.readdirSync(directory)) {
            const file = path.join(directory, name), stat = fs.lstatSync(file);
            if (stat.isSymbolicLink()) throw new Error(`Symlink is not a standalone controller input: ${file}`);
            if (stat.isDirectory()) visit(file);
            else if (/\.(?:c|h|S|inc|ld|mk)$/.test(name) || name === 'Makefile') {
                checkInput(source, file);
                inputs.push(file);
                if (!/\.(?:c|h|S|inc)$/.test(name)) continue;
                const text = fs.readFileSync(file, 'utf8');
                if (/\.(?:include|incbin)\s+["<]/.test(text))
                    throw new Error(`Assembler file import in controller: ${file}`);
                for (const line of text.split('\n')) {
                    const include = /^\s*#\s*(include(?:_next)?)\s+(.+?)\s*$/.exec(line);
                    if (!include) continue;
                    const quoted = include[1] === 'include' && /^"([^"]+)"(?:\s*\/[/*].*)?$/.exec(include[2]);
                    if (!quoted) throw new Error(`Non-local or computed controller include: ${file}: ${line}`);
                    const local = path.resolve(path.dirname(file), quoted[1]);
                    const target = fs.existsSync(local) ? local : path.resolve(source, quoted[1]);
                    checkInput(source, target);
                }
            }
        }
    }
    visit(source);
    const linker = fs.readFileSync(path.join(source, 'cyw43.ld'), 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
    if (/\b(?:INPUT|GROUP|INCLUDE|SEARCH_DIR|STARTUP)\b/.test(linker))
        throw new Error('Controller linker script imports additional build inputs');
    return inputs;
}

function checkMakeInputs(source, build, args) {
    const fields = ['SRCS', 'OBJS', 'HEADERS', 'MAKEFILE_LIST', 'CFLAGS', 'LDFLAGS'];
    // Make 3.81 names stdin with a temporary file; exclude only this final query file.
    const input = '\nboundary-inputs:\n' + fields.map(field => `\t@printf '%s\\n' '${field}:` +
        (field === 'MAKEFILE_LIST' ? '$(filter-out $(lastword $(MAKEFILE_LIST)),$(MAKEFILE_LIST))' : `$(${field})`) +
        "'").join('\n') + '\n';
    const result = spawnSync('make', [...args.filter(arg => arg !== 'all'), '-s', '--no-print-directory',
        '-f', 'Makefile', '-f', '-', 'boundary-inputs'], { input, encoding: 'utf8',
        env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' } });
    if (result.error) throw result.error;
    if (result.status !== 0) throw new Error(result.stdout + result.stderr);
    const values = {};
    for (const line of result.stdout.trim().split('\n')) {
        const colon = line.indexOf(':');
        const key = line.slice(0, colon);
        if (!fields.includes(key) || values[key]) throw new Error(`Unexpected build-input response: ${line}`);
        values[key] = line.slice(colon + 1).trim().split(/\s+/).filter(Boolean);
    }
    if (Object.keys(values).length !== fields.length) throw new Error('Incomplete controller build-input response');
    if (JSON.stringify(values.SRCS) !== JSON.stringify(sources)) throw new Error('Unexpected controller source list');
    const objects = ['startup', ...sources.map(file => path.basename(file, '.c'))].map(name => path.resolve(build, `${name}.o`));
    if (JSON.stringify(values.OBJS.map(file => path.resolve(source, file))) !== JSON.stringify(objects))
        throw new Error('Unexpected controller object/link input');
    for (const file of [...values.SRCS, 'startup.S', 'cyw43.ld', ...values.HEADERS,
        ...values.MAKEFILE_LIST.filter(file => file !== '-')]) checkInput(source, path.resolve(source, file));
    for (const required of ['-ffreestanding', '-nostdinc', '-fno-builtin'])
        if (!values.CFLAGS.includes(required)) throw new Error(`Missing controller flag: ${required}`);
    for (let i = 0; i < values.CFLAGS.length; ++i) {
        const flag = values.CFLAGS[i];
        if (flag.startsWith('-I')) checkInput(source, path.resolve(source, flag.slice(2) || values.CFLAGS[++i]));
        else if (!/^(?:--target=arm-none-eabi|-mcpu=cortex-m3|-mthumb|-std=c11|-Wall|-Wextra|-Werror|-Oz|-ffreestanding|-nostdinc|-fno-[\w-]+|-ffunction-sections|-fdata-sections|-D\w+(?:=[\w.+-]+)?)$/.test(flag))
            throw new Error(`Unexpected controller compiler input/flag: ${flag}`);
    }
    const expectedLink = ['-nostdlib', '-fuse-ld=lld', '-Wl,--gc-sections', '-Wl,--fatal-warnings', '-T', 'cyw43.ld'];
    if (JSON.stringify(values.LDFLAGS.slice(0, 6)) !== JSON.stringify(expectedLink) ||
        values.LDFLAGS.length !== 7 || !/^-Wl,--defsym=clean_rx_profile_id=[012]$/.test(values.LDFLAGS[6]))
        throw new Error('Unexpected controller linker inputs/flags');
    return values;
}

function checkDependencies(source, dependencies, compilerFlags) {
    const runtime = compilerFlags.includes('-DCLEAN_RX_DIAGNOSTIC=0');
    const all = new Set();
    for (const dependency of dependencies)
        for (const file of dependency.inputs) {
            const absolute = path.resolve(source, file);
            checkInput(source, absolute);
            const relative = path.relative(fs.realpathSync(source), fs.realpathSync(absolute)).split(path.sep).join('/');
            if (runtime && relative.startsWith('diagnostic/'))
                throw new Error(`Runtime imported diagnostic implementation: ${file}`);
            all.add(relative);
        }
    const sdio = compilerFlags.some(flag => /^-DCLEAN_SDIO_SELF_TEST(?:=|$)/.test(flag));
    const required = sdio ? ['diagnostic/sdio_test.h', 'diagnostic/sdio_run.inc'] :
        runtime ? [] : ['diagnostic/ack_command.inc', 'diagnostic/ack_configure.inc', 'diagnostic/psm_ack_test.h'];
    for (const file of required)
        if (!all.has(file)) throw new Error(`Selected diagnostic implementation was not compiled: ${file}`);
}

function checkSymbols(defined, undefinedSymbols) {
    if (undefinedSymbols.trim()) throw new Error(`Undefined controller symbols: ${undefinedSymbols.trim()}`);
    const names = new Set();
    for (const line of defined.trim().split('\n')) {
        const match = /^\s*[0-9a-f]+(?:\s+[0-9a-f]+)?\s+([A-Za-z])\s+(\S+)\s*$/i.exec(line);
        if (!match) throw new Error(`Unexpected controller symbol record: ${line}`);
        const [, type, name] = match;
        if ((type === type.toUpperCase() && !exportsAllowed.has(name)) ||
            /^(?:__aeabi_|__gnu_|__cxa_|__stack_chk|malloc$|calloc$|realloc$|free$|memcpy$|memmove$|memset$|memcmp$|printf$|puts$)/.test(name))
            throw new Error(`Unexpected linked controller runtime symbol: ${name}`);
        names.add(name);
    }
    for (const required of ['cyw43_reset', 'cyw43_fault', 'cyw43_main', 'cyw43_vectors'])
        if (!names.has(required)) throw new Error(`Missing controller entry symbol: ${required}`);
}

module.exports = { checkSourceTree, checkMakeInputs, checkDependencies, checkSymbols };
