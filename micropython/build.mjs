import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

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

function firmware(name, board, mixed = false) {
    const directory = path.join(build, name);
    const picotool = path.join(build, 'pico2-stock/_deps/picotool');
    const args = ['-S', port, '-B', directory, `-DMICROPY_BOARD=${board}`, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'];
    if (board === 'BADGE_RP2350') args.push(`-DMICROPY_BOARD_DIR=${path.join(here, 'board', board)}`);
    if (name !== 'pico2-stock' && fs.existsSync(path.join(picotool, 'picotoolConfig.cmake')))
        args.push(`-Dpicotool_DIR=${picotool}`);
    if (mixed) args.push(`-DCMAKE_PROJECT_INCLUDE=${path.join(here, 'mixed.cmake')}`, `-DBADGE_LFS2_OBJECT=${owned}`);
    run('cmake', args, {
        cwd: port, timeout: 240000, log: `${name}-configure`,
        env: { ...process.env, CFLAGS: '-Wno-error=array-bounds' },
    });
    run('cmake', ['--build', directory, '-j', jobs], { timeout: 300000, log: name });
    const commands = JSON.parse(fs.readFileSync(path.join(directory, 'compile_commands.json'), 'utf8'));
    assert.equal(commands.filter(row => row.file === source).length, mixed ? 0 : 1);
    const map = fs.readFileSync(path.join(directory, 'firmware.elf.map'), 'utf8');
    assert(map.includes(mixed ? owned : '/lib/littlefs/lfs2_util.c.o'));
    const crc = crcSymbol(path.join(directory, 'firmware.elf')).bytes;
    const bytes = fs.statSync(path.join(directory, 'firmware.bin')).size;
    const uf2 = fs.statSync(path.join(directory, 'firmware.uf2')).size;
    console.log(`${name}: ${bytes} loaded bytes; ${crc} bytes in lfs2_crc`);
    return { directory, bytes, uf2, crc };
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
    let instructions = 0;
    for (const [, value] of fs.readFileSync(trace, 'utf8').matchAll(/^Trace .*?\[[\da-f]+\/([\da-f]+)\//gm)) {
        const pc = Number.parseInt(value, 16);
        if (pc >= start && pc < start + bytes) instructions++;
    }
    assert(instructions > 0);
    return { bytes, qemuInstructions: instructions };
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
    const sameTokens = crcComparison();
    console.log('Same-token CRC comparison (1,024-byte input):', sameTokens);
    fs.writeFileSync(path.join(build, 'comparison.json'), JSON.stringify({ revision, pico2, stock, mixed, sameTokens, oracleCases: 1028 }, null, 2) + '\n');
} catch (error) {
    console.error(`MicroPython build: ${error.message}`);
    process.exitCode = 1;
}
