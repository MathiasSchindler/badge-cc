import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { ownedStages, preprocessStock } from './stock-input.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(here);
const build = path.join(here, 'build');
const upstream = path.join(build, 'upstream');
const port = path.join(upstream, 'ports/rp2');
const tools = path.join(root, 'badge-compiler/build');
const revision = '0fd6c573ea815774668bbb16b8e197c8822368b2';
const jobs = process.env.JOBS ?? '8';
const gcc = process.env.ARM_GCC ?? 'arm-none-eabi-gcc';
const arm = ['-mcpu=cortex-m33', '-mthumb'];
const source = path.join(upstream, 'lib/littlefs/lfs2_util.c');
const owned = path.join(build, 'lfs2-owned.o');
const smallint = path.join(upstream, 'py/smallint.c');
const ownedSmallint = path.join(build, 'smallint-owned.o');
const smallintNames = ['mp_small_int_modulo', 'mp_small_int_floor_divide'];
const radix = path.join(upstream, 'py/parsenumbase.c');
const ownedRadix = path.join(build, 'radix-owned.o');
const unicode = path.join(upstream, 'py/unicode.c');
const ownedUnicode = path.join(build, 'unicode-owned.o');
const timeutils = path.join(upstream, 'shared/timeutils/timeutils.c');
const ownedTimeutils = path.join(build, 'timeutils-owned.o');
const sequence = path.join(upstream, 'py/sequence.c');
const ownedSequence = path.join(build, 'sequence-owned.o');
const fixture = path.join(root, 'badge-compiler/tests/fixtures');
const qemu = process.env.QEMU_ARM ?? 'qemu-system-arm';
const emulator = ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
    '-semihosting-config', 'enable=on,target=native'];

function run(tool, args, { timeout = 120000, log, capture = false, ...options } = {}) {
    const result = spawnSync(tool, args, {
        cwd: root, timeout, encoding: 'utf8',
        stdio: capture || log ? ['ignore', 'pipe', 'pipe'] : 'inherit',
        maxBuffer: 8 * 1024 * 1024, ...options,
    });
    const text = `${result.stdout ?? ''}${result.stderr ?? ''}`;
    if (log) fs.writeFileSync(path.join(build, `${log}.log`), text);
    if (result.error || result.status !== 0)
        throw new Error(`${tool}: ${result.error?.message ?? `exit ${result.status}`}\n${text.slice(-6000)}`);
    return result.stdout ?? '';
}

function firmware(name, board, mixed = false, core = false, vm = false, sequenceObject = null) {
    const directory = path.join(build, name);
    const picotool = path.join(build, 'pico2-stock/_deps/picotool');
    const args = ['-S', port, '-B', directory, `-DMICROPY_BOARD=${board}`, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'];
    if (board === 'BADGE_RP2350') args.push(`-DMICROPY_BOARD_DIR=${path.join(here, 'board', board)}`);
    if (name !== 'pico2-stock' && fs.existsSync(path.join(picotool, 'picotoolConfig.cmake')))
        args.push(`-Dpicotool_DIR=${picotool}`);
    if (mixed) args.push(`-DCMAKE_PROJECT_INCLUDE=${path.join(here, 'mixed.cmake')}`, `-DBADGE_LFS2_OBJECT=${owned}`);
    if (core) args.push(`-DBADGE_SMALLINT_OBJECT=${ownedSmallint}`);
    if (vm) args.push(`-DBADGE_RADIX_OBJECT=${ownedRadix}`, `-DBADGE_UNICODE_OBJECT=${ownedUnicode}`,
        `-DBADGE_TIMEUTILS_OBJECT=${ownedTimeutils}`, `-DBADGE_SEQUENCE_OBJECT=${sequenceObject ?? ''}`);
    run('cmake', args, {
        cwd: port, timeout: 240000, log: `${name}-configure`,
        env: { ...process.env, CFLAGS: '-Wno-error=array-bounds' },
    });
    run('cmake', ['--build', directory, '-j', jobs], { timeout: 300000, log: name });
    const commands = JSON.parse(fs.readFileSync(path.join(directory, 'compile_commands.json'), 'utf8'));
    assert.equal(commands.filter(row => row.file === source).length, mixed ? 0 : 1);
    assert.equal(commands.filter(row => row.file === smallint).length, core ? 0 : 1);
    assert.equal(commands.filter(row => row.file === radix).length, vm ? 0 : 1);
    assert.equal(commands.filter(row => row.file === unicode).length, vm ? 0 : 1);
    assert.equal(commands.filter(row => row.file === timeutils).length, vm ? 0 : 1);
    assert.equal(commands.filter(row => row.file === sequence).length, sequenceObject ? 0 : 1);
    const map = fs.readFileSync(path.join(directory, 'firmware.elf.map'), 'utf8');
    assert(map.includes(mixed ? owned : '/lib/littlefs/lfs2_util.c.o'));
    assert(map.includes(core ? ownedSmallint : '/py/smallint.c.o'));
    assert(map.includes(vm ? ownedRadix : '/py/parsenumbase.c.o'));
    assert(map.includes(vm ? ownedUnicode : '/py/unicode.c.o'));
    assert(map.includes(vm ? ownedTimeutils : '/shared/timeutils/timeutils.c.o'));
    assert(map.includes(sequenceObject ?? '/py/sequence.c.o'));
    if (sequenceObject === ownedSequence) {
        assert.match(map, /\.text\.mp_seq_index_obj\s+[^\n]+\n[^\n]*sequence-owned\.o|\.text\.mp_seq_index_obj\s+[^\n]*sequence-owned\.o/);
        assert.match(map, /libg\.a\(libc_a-strcmp\.o\)/);
    }
    if (vm) {
        assert.match(map, /\.text\.utf8_get_char\s+[^\n]+\n[^\n]*unicode-owned\.o|\.text\.utf8_get_char\s+[^\n]*unicode-owned\.o/);
        assert.match(map, /\.text\.timeutils_mktime_1970\s+[^\n]+\n[^\n]*timeutils-owned\.o|\.text\.timeutils_mktime_1970\s+[^\n]*timeutils-owned\.o/);
    }
    const crc = crcSymbol(path.join(directory, 'firmware.elf')).bytes;
    const bytes = fs.statSync(path.join(directory, 'firmware.bin')).size;
    const uf2 = fs.statSync(path.join(directory, 'firmware.uf2')).size;
    console.log(`${name}: ${bytes} loaded bytes; ${crc} bytes in lfs2_crc`);
    return { directory, bytes, uf2, crc };
}

function compileStockModule(controlDirectory, source, object) {
    const db = JSON.parse(fs.readFileSync(path.join(controlDirectory, 'compile_commands.json'), 'utf8'));
    const stem = path.join(build, `${path.basename(source, '.c')}-stock`);
    preprocessStock(db, source, `${stem}.i`, run);
    for (const [, tool, args] of ownedStages(stem, object, tools))
        run(tool, args);
}

function compileSequence(controlDirectory) {
    const db = JSON.parse(fs.readFileSync(path.join(controlDirectory, 'compile_commands.json'), 'utf8'));
    const stem = path.join(build, 'sequence-stock'), baseline = path.join(build, 'sequence-baseline');
    preprocessStock(db, sequence, `${stem}.i`, run);
    fs.copyFileSync(`${stem}.i`, `${baseline}.i`);
    const optimizer = path.join(tools, 'badge-opt');
    const passes = run(optimizer, ['--default-passes'], { capture: true }).trim().split(',');
    const fold = passes.indexOf('constant-fold');
    assert(passes.includes('mem2reg') && fold > passes.indexOf('mem2reg'));
    passes[fold] = 'libc-strcmp';
    const baselineObject = `${baseline}.o`;
    for (const [, tool, args] of [
        ...ownedStages(baseline, baselineObject, tools),
        ...ownedStages(stem, ownedSequence, tools, [`--passes=${passes.join(',')}`]),
    ]) run(tool, args);
    return baselineObject;
}

function sequenceOracle(controlDirectory, baseline) {
    const gccObject = path.join(controlDirectory, 'CMakeFiles/firmware.dir', `${sequence}.o`);
    const before = path.join(build, 'sequence-oracle-before.o'), control = path.join(build, 'sequence-control.o');
    const name = 'mp_seq_index_obj', beforeName = 'before_seq_index_obj', gccName = 'gcc_seq_index_obj';
    // Keep the public symbols distinct; the oracle link discards unused sections.
    for (const [object, output, symbol, prefix] of [[baseline, before, beforeName, 'before'], [gccObject, control, gccName, 'gcc']]) {
        const symbols = run('llvm-nm', ['--defined-only', '--extern-only', object], { capture: true });
        const publicNames = [...symbols.matchAll(/^[\da-f]+ T (mp_seq_\w+)$/gm)].map(m => m[1]);
        assert(publicNames.includes(name));
        run('arm-none-eabi-objcopy', [...publicNames.map(n =>
            `--redefine-sym=${n}=${n === name ? symbol : `${prefix}_${n}`}`), object, output]);
    }
    const oracle = path.join(build, 'sequence-oracle.o'), elf = path.join(build, 'sequence-oracle.elf');
    const trace = path.join(build, 'sequence-oracle.trace');
    run(gcc, [...arm, '-Os', '-ffreestanding', '-c', path.join(here, 'sequence-oracle.c'), '-o', oracle]);
    run('ld.lld', ['--gc-sections', '-T', path.join(fixture, 'codegen_fixture.ld'),
        path.join(build, 'crc-start.o'), oracle, ownedSequence, before, control, '-o', elf]);
    run(qemu, [...emulator, '-kernel', elf, '-accel', 'tcg,one-insn-per-tb=on',
        '-d', 'exec,nochain', '-D', trace], { timeout: 20000 });
    const symbols = run('llvm-nm', ['--defined-only', '--print-size', elf], { capture: true });
    const pcs = tracePcs(trace);
    const symbolInfo = symbol => {
        const match = symbols.match(new RegExp(`^([\\da-f]+) ([\\da-f]+) T ${symbol}$`, 'm'));
        assert(match, `Missing oracle symbol ${symbol}`);
        const [start, bytes] = match.slice(1).map(hex => Number.parseInt(hex, 16));
        return { start, bytes };
    };
    const measure = symbol => {
        const { start, bytes } = symbolInfo(symbol);
        return { bytes, qemuInstructions: countReached(pcs, start, bytes) };
    };
    const strcmp = symbolInfo('strcmp').start;
    const result = { baseline: measure(beforeName), optimized: measure(name), gcc: measure(gccName),
        baselineStrcmpCalls: countReached(pcs, strcmp, 1), lookupCases: 4 };
    assert(result.baselineStrcmpCalls > 0);
    assert(result.optimized.bytes <= 256 && result.optimized.bytes * 20 < result.baseline.bytes);
    console.log('Sequence oracle: matching indexes and identical compressed ValueError bytes:', result);
    return result;
}

function calendarOracle(controlDirectory, vmDirectory) {
    const stockObject = path.join(controlDirectory, 'CMakeFiles/firmware.dir', `${timeutils}.o`);
    const control = path.join(build, 'calendar-control.o');
    const names = ['timeutils_seconds_since_1970_to_struct_time', 'timeutils_mktime_1970'];
    const renames = new Map([[names[0], 'gcc_calendar_from_seconds'], [names[1], 'gcc_calendar_to_seconds']]);
    const exports = run('llvm-nm', ['--defined-only', '--extern-only', stockObject], { capture: true });
    const stockNames = [...exports.matchAll(/^[\da-f]+ T (timeutils_\w+)$/gm)].map(m => m[1]);
    for (const name of names) assert(stockNames.includes(name));
    run('arm-none-eabi-objcopy', [...stockNames.map(name => `--redefine-sym=${name}=${renames.get(name) ?? `gcc_${name}`}`),
        stockObject, control]);
    const oracle = path.join(build, 'calendar-oracle.o'), elf = path.join(build, 'calendar-oracle.elf');
    run(gcc, [...arm, '-Os', '-ffreestanding', '-c', path.join(here, 'calendar-oracle.c'), '-o', oracle]);
    run('ld.lld', ['--gc-sections', '-T', path.join(fixture, 'codegen_fixture.ld'),
        path.join(build, 'crc-start.o'), oracle, ownedTimeutils, control, '-o', elf]);
    run(qemu, [...emulator, '-kernel', elf], { timeout: 20000 });
    const sizes = directory => {
        const symbols = run('llvm-nm', ['--defined-only', '--print-size', path.join(directory, 'firmware.elf')], { capture: true });
        return Object.fromEntries(names.map(name => {
            const m = symbols.match(new RegExp(`^[\\da-f]+ ([\\da-f]+) T ${name}$`, 'm'));
            assert(m, `Missing linked ${name} in ${directory}`);
            return [name, Number.parseInt(m[1], 16)];
        }));
    };
    const linked = { gcc: sizes(controlDirectory), owned: sizes(vmDirectory) };
    console.log('Calendar oracle: 2,048 timestamps passed; linked symbol sizes:', linked);
    return { oracleCases: 2048, sizes: linked };
}

function radixOracle(controlDirectory, vmDirectory) {
    const stockObject = path.join(controlDirectory, 'CMakeFiles/firmware.dir', `${radix}.o`);
    const control = path.join(build, 'radix-control.o');
    const oracle = path.join(build, 'radix-oracle.o'), elf = path.join(build, 'radix-oracle.elf');
    run('arm-none-eabi-objcopy', ['--redefine-sym=mp_parse_num_base=gcc_parse_num_base',
        stockObject, control]);
    run(gcc, [...arm, '-Os', '-ffreestanding', '-c', path.join(here, 'radix-oracle.c'), '-o', oracle]);
    run('ld.lld', ['--gc-sections', '-T', path.join(fixture, 'codegen_fixture.ld'),
        path.join(build, 'crc-start.o'), oracle, ownedRadix, control, '-o', elf]);
    run(qemu, [...emulator, '-kernel', elf], { timeout: 20000 });
    const bytes = directory => {
        const symbols = run('llvm-nm', ['--defined-only', '--print-size', path.join(directory, 'firmware.elf')], { capture: true });
        const symbol = symbols.match(/^[\da-f]+ ([\da-f]+) T mp_parse_num_base$/m);
        assert(symbol, `mp_parse_num_base missing from ${directory}`);
        return Number.parseInt(symbol[1], 16);
    };
    const sizes = { gcc: bytes(controlDirectory), owned: bytes(vmDirectory) };
    console.log('Radix oracle: 12,288 input/base cases passed; linked symbol sizes:', sizes);
    return { oracleCases: 12288, sizes };
}

function smallintOracle(controlDirectory, coreDirectory) {
    const stockObject = path.join(controlDirectory, 'CMakeFiles/firmware.dir', `${smallint}.o`);
    const control = path.join(build, 'smallint-control.o');
    run('arm-none-eabi-objcopy', [...smallintNames.map(n => `--redefine-sym=${n}=${n.replace('mp_small_int_', 'gcc_')}`),
        stockObject, control]);
    const oracle = path.join(build, 'smallint-oracle.o'), elf = path.join(build, 'smallint-oracle.elf');
    run(gcc, [...arm, '-mfloat-abi=softfp', '-Os', '-ffreestanding', '-c', path.join(here, 'smallint-oracle.c'), '-o', oracle]);
    run('ld.lld', ['--gc-sections', '-T', path.join(fixture, 'codegen_fixture.ld'),
        path.join(build, 'crc-start.o'), oracle, ownedSmallint, control, '-o', elf]);
    const trace = path.join(build, 'smallint-oracle.trace');
    run(qemu, [...emulator, '-kernel', elf, '-accel', 'tcg,one-insn-per-tb=on',
        '-d', 'exec,nochain', '-D', trace], { timeout: 20000 });
    const symbolSizes = directory => {
        const symbols = run('llvm-nm', ['--defined-only', '--print-size', path.join(directory, 'firmware.elf')], { capture: true });
        return Object.fromEntries(smallintNames.map(n => {
            const m = symbols.match(new RegExp(`^[\\da-f]+ ([\\da-f]+) T ${n}$`, 'm'));
            assert(m, `${n} missing from ${directory}`);
            return [n, Number.parseInt(m[1], 16)];
        }));
    };
    const sizes = { gcc: symbolSizes(controlDirectory), owned: symbolSizes(coreDirectory) };
    const symbols = run('llvm-nm', ['--defined-only', '--print-size', elf], { capture: true });
    const pcs = tracePcs(trace);
    const qemuInstructions = Object.fromEntries([...smallintNames,
        ...smallintNames.map(n => n.replace('mp_small_int_', 'gcc_'))].map(n => {
        const m = symbols.match(new RegExp(`^([\\da-f]+) ([\\da-f]+) T ${n}$`, 'm'));
        assert(m, `Missing oracle symbol ${n}`);
        const count = countReached(pcs, Number.parseInt(m[1], 16), Number.parseInt(m[2], 16));
        assert(count > 0, `Oracle did not reach ${n}`);
        return [n, count];
    }));
    console.log('Smallint oracle: 182 signed pairs passed; linked symbol sizes:', sizes,
        'QEMU helper instructions:', qemuInstructions);
    return { oraclePairs: 182, sizes, qemuInstructions };
}

function compileOwned() {
    const preprocessed = path.join(build, 'lfs2-owned.i');
    const ir = path.join(build, 'lfs2-owned.ll');
    const optimized = path.join(build, 'lfs2-owned.opt.ll');
    const assembly = path.join(build, 'lfs2-owned.s');
    const defines = ['MALLOC', 'ASSERT', 'DEBUG', 'WARN', 'ERROR'].map(name => `-DLFS2_NO_${name}`);
    run(path.join(tools, 'badge-cpp'), [`-I${path.join(here, 'include')}`,
        `-I${path.join(root, 'src/shared')}`, ...defines, source, '-o', preprocessed]);
    run(path.join(tools, 'badge-ir'), [preprocessed, '-o', ir]);
    run(path.join(tools, 'badge-opt'), [ir, '-o', optimized]);
    run(path.join(tools, 'badge-codegen'), [optimized, '-o', assembly]);
    run(path.join(tools, 'badge-asm'), [assembly, '-o', owned]);
    assert.match(run('llvm-nm', ['--defined-only', '--extern-only', owned], { capture: true }).trim(),
        /^[\da-f]+ T lfs2_crc$/);
}

function crcComparison() {
    const tokens = path.join(build, 'lfs2-owned.i');
    const common = [...arm, '-Oz', '-ffunction-sections', '-fdata-sections', '-x', 'c', '-c', tokens];
    const driver = path.join(build, 'crc-workload.o');
    run(gcc, [...arm, '-Os', '-ffreestanding', '-c', path.join(here, 'crc-workload.c'), '-o', driver]);
    const result = {};
    for (const [role, compiler, extra] of [
        ['gcc', gcc, []], ['llvm', process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi']],
    ]) {
        const out = path.join(build, `lfs2-same-${role}.o`);
        run(compiler, [...extra, ...common, '-o', out]);
        result[role] = measureCrc(role, out, driver);
    }
    return { ...result, owned: measureCrc('owned', owned, driver) };
}

function crcSymbol(file) {
    const symbols = run('llvm-nm', ['--defined-only', '--print-size', file], { capture: true });
    const crc = symbols.match(/^([\da-f]+) ([\da-f]+) T lfs2_crc$/m);
    assert(crc, `${file}: CRC symbol missing`);
    return { start: Number.parseInt(crc[1], 16), bytes: Number.parseInt(crc[2], 16) };
}

function measureCrc(role, object, driver) {
    const elf = path.join(build, `crc-workload-${role}.elf`);
    const trace = path.join(build, `crc-workload-${role}.trace`);
    run('ld.lld', ['--gc-sections', '-T', path.join(fixture, 'codegen_fixture.ld'),
        path.join(build, 'crc-start.o'), driver, object, '-o', elf]);
    run(qemu, [...emulator, '-kernel', elf, '-accel', 'tcg,one-insn-per-tb=on',
        '-d', 'exec,nochain', '-D', trace], { timeout: 20000 });
    const { start, bytes } = crcSymbol(elf);
    const instructions = countReached(tracePcs(trace), start, bytes);
    assert(instructions > 0);
    return { bytes, qemuInstructions: instructions };
}

function tracePcs(trace) {
    return Array.from(fs.readFileSync(trace, 'utf8').matchAll(/^Trace .*?\[[\da-f]+\/([\da-f]+)\//gm),
        m => Number.parseInt(m[1], 16));
}

function countReached(pcs, start, bytes) {
    return pcs.reduce((count, pc) => count + (pc >= start && pc < start + bytes ? 1 : 0), 0);
}

function crcOracle(controlDirectory) {
    const object = path.join(controlDirectory, 'CMakeFiles/firmware.dir', `${source}.o`);
    const control = path.join(build, 'lfs2-control.o');
    const oracle = path.join(build, 'crc-oracle.o');
    const start = path.join(build, 'crc-start.o');
    const elf = path.join(build, 'crc-oracle.elf');
    run('arm-none-eabi-objcopy', ['--redefine-sym', 'lfs2_crc=gcc_lfs2_crc', object, control]);
    run(gcc, [...arm, '-mfloat-abi=softfp', '-Os', '-ffreestanding', '-c', path.join(here, 'crc-oracle.c'), '-o', oracle]);
    run(path.join(tools, 'badge-asm'), [path.join(fixture, 'codegen_start.S'), '-o', start]);
    run('ld.lld', ['--gc-sections', '-T', path.join(fixture, 'codegen_fixture.ld'), start, oracle, owned, control, '-o', elf]);
    run(qemu, [...emulator, '-kernel', elf], { timeout: 20000 });
    console.log('CRC oracle: 1,028 seeded alignment/length cases and split-update checks passed.');
}

try {
    const options = process.argv.slice(2);
    assert(options.length === 0 || (options.length === 1 && options[0] === '--sequence-before'),
        'usage: node micropython/build.mjs [--sequence-before]');
    fs.mkdirSync(build, { recursive: true });
    if (!fs.existsSync(upstream))
        run('git', ['clone', '--depth', '1', '--branch', 'v1.29.0', 'https://github.com/micropython/micropython.git', upstream]);
    assert.equal(run('git', ['-C', upstream, 'rev-parse', 'HEAD'], { capture: true }).trim(), revision);
    assert.equal(run('git', ['-C', upstream, 'status', '--porcelain', '--untracked-files=no'], { capture: true }).trim(), '');
    run('make', ['-C', port, 'BOARD=RPI_PICO2', `BUILD=${path.join(build, 'pico2')}`, 'submodules'],
        { timeout: 300000, log: 'submodules' });
    run('make', ['-C', path.join(upstream, 'mpy-cross'), '-j', jobs], { log: 'mpy-cross' });
    run('make', ['-C', path.join(root, 'badge-compiler'), '-j', jobs, 'all'], { log: 'badge-compiler' });
    const pico2 = firmware('pico2-stock', 'RPI_PICO2');
    const stock = firmware('badge-stock', 'BADGE_RP2350');
    compileOwned();
    const mixed = firmware('badge-mixed', 'BADGE_RP2350', true);
    crcOracle(stock.directory);
    compileStockModule(stock.directory, smallint, ownedSmallint);
    const core = firmware('badge-core', 'BADGE_RP2350', true, true);
    const smallInteger = smallintOracle(stock.directory, core.directory);
    compileStockModule(stock.directory, radix, ownedRadix);
    compileStockModule(stock.directory, unicode, ownedUnicode);
    compileStockModule(stock.directory, timeutils, ownedTimeutils);
    const vm = firmware('badge-vm', 'BADGE_RP2350', true, true, true);
    const radixParser = radixOracle(stock.directory, vm.directory);
    const calendar = calendarOracle(stock.directory, vm.directory);
    const sequenceBaseline = compileSequence(stock.directory);
    const sequenceIndex = sequenceOracle(stock.directory, sequenceBaseline);
    const sequenceVm = firmware('badge-sequence', 'BADGE_RP2350', true, true, true, ownedSequence);
    const sequenceBeforeVm = options.length
        ? firmware('badge-sequence-before', 'BADGE_RP2350', true, true, true, sequenceBaseline) : undefined;
    if (sequenceBeforeVm) {
        assert(sequenceVm.bytes < sequenceBeforeVm.bytes);
        console.log('Sequence fold loaded flash savings:', sequenceBeforeVm.bytes - sequenceVm.bytes);
    }
    const sameTokens = crcComparison();
    console.log('Same-token CRC comparison (1,024-byte input):', sameTokens);
    fs.writeFileSync(path.join(build, 'comparison.json'),
        JSON.stringify({ revision, pico2, stock, mixed, core, vm, sequenceVm, sequenceBeforeVm, smallInteger, radixParser, calendar, sequenceIndex,
            sameTokens, oracleCases: 1028 }, null, 2) + '\n');
} catch (error) {
    console.error(`MicroPython build: ${error.message}`);
    process.exitCode = 1;
}
