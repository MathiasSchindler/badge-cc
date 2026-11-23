import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { bridgeStages, ownedStages, preprocessStock } from './stock-input.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.dirname(here);
const work = path.join(here, 'build');
const upstream = path.join(work, 'upstream');
const firmware = path.join(work, 'badge-stock');
const compiler = path.join(root, 'badge-compiler/build');
const gcc = process.env.ARM_GCC ?? 'arm-none-eabi-gcc';
const arm = ['-mcpu=cortex-m33', '-mthumb', '-mfloat-abi=softfp'];

function run(tool, args, options = {}) {
    const r = spawnSync(tool, args, { cwd: root, encoding: 'utf8', timeout: 30000, ...options });
    if (r.error) throw r.error;
    return r;
}

function checked(tool, args, options) {
    const r = run(tool, args, options);
    if (r.status !== 0) throw new Error(`${tool}: ${r.stderr}`);
    return r.stdout;
}

const modules = ['py/ringbuf.c', 'py/smallint.c', 'py/parsenumbase.c', 'py/unicode.c',
    'py/sequence.c', 'shared/timeutils/timeutils.c'];
const coreModules = ['py/objstr.c', 'py/runtime.c', 'py/vm.c', 'py/qstr.c', 'py/binary.c', 'py/objint.c'];

function probeModule(db, module, useBridge) {
    const source = path.join(upstream, module);
    const stem = path.join(work, `survey-${path.basename(module, '.c')}`);
    const literalLookup = ['py/objstr.c', 'py/objint.c', 'py/runtime.c', 'py/vm.c'].includes(module);
    const flags = literalLookup ? ['--libc-strcmp'] : [];
    for (const suffix of ['.i', '.ll', '.opt.ll', '.s', '.o'])
        fs.rmSync(`${stem}${suffix}`, { force: true });
    let failure;
    try {
        preprocessStock(db, source, `${stem}.i`, checked);
    } catch (error) {
        failure = { stage: 'preprocessing', diagnostic: error.message };
    }
    if (!failure)
        for (const [stage, tool, args] of ownedStages(stem, `${stem}.o`, compiler, [], flags)) {
            const r = run(tool, args);
            if (r.status !== 0) {
                failure = { stage, diagnostic: r.stderr.trim() || `exit ${r.status}` };
                break;
            }
        }
    let bridge;
    if (useBridge && failure?.stage === 'frontend') {
        const compatible = `${stem}-bridge`;
        fs.copyFileSync(`${stem}.i`, `${compatible}.i`);
        const object = `${compatible}.o`;
        fs.rmSync(object, { force: true });
        bridge = { stage: 'object-ready', diagnostic: null, link: 'not attempted by survey' };
        for (const [stage, tool, args] of bridgeStages(compatible, object, compiler)) {
            const r = run(tool, args);
            if (r.status !== 0) {
                bridge = { stage, diagnostic: r.stderr.trim() || `exit ${r.status}` };
                break;
            }
        }
    }
    return {
        module, mode: `GCC preprocessing with stock headers; owned frontend through assembly${literalLookup ? '; standard libc strcmp' : ''}`,
        ...(failure ?? { stage: 'object-ready', diagnostic: null, link: 'not attempted by survey' }), bridge,
    };
}

function executeOracle(name, owned, control, libraries = [], flags = []) {
    const oracle = path.join(work, `${name}-oracle.o`), elf = path.join(work, `${name}-oracle.elf`);
    checked(gcc, [...arm, ...flags, '-Os', '-ffreestanding', '-c', path.join(here, `${name}-oracle.c`), '-o', oracle]);
    const fixture = path.join(root, 'badge-compiler/tests/fixtures/codegen_fixture.ld');
    checked('ld.lld', ['--gc-sections', '-T', fixture, path.join(work, 'crc-start.o'), oracle, owned, control,
        ...libraries, '-o', elf]);
    checked(process.env.QEMU_ARM ?? 'qemu-system-arm',
        ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
            '-semihosting-config', 'enable=on,target=native', '-kernel', elf]);
}

function stockOracleObjects(module, name) {
    const owned = path.join(work, `survey-${module}.o`);
    const gccObject = path.join(firmware, 'CMakeFiles/firmware.dir', `${upstream}/py/${module}.c.o`);
    const control = path.join(work, `${name}-control.o`);
    const exports = file => [...checked('llvm-nm', ['--defined-only', '--extern-only', '--print-size', file])
        .matchAll(/^[\da-f]+ ([\da-f]+) [A-Z] (\w+)$/gm)];
    const gccExports = exports(gccObject);
    assert(gccExports.length > 0);
    checked('arm-none-eabi-objcopy', [...gccExports.map(([, , fn]) =>
        `--redefine-sym=${fn}=gcc_${fn}`), gccObject, control]);
    return { owned, control, gccExports, ownedExports: exports(owned) };
}

function qualifyQstr() {
    const { owned, control, gccExports, ownedExports } = stockOracleObjects('qstr', 'qstr-pool');
    const poolEntries = rows => Object.fromEntries(rows
        .filter(([, , fn]) => /^mp_qstr_const_pool(_static)?$/.test(fn))
        .map(([, bytes, fn]) => [fn, (Number.parseInt(bytes, 16) - 24) / 4]));
    const entries = poolEntries(gccExports);
    assert.equal(Object.keys(entries).length, 2);
    assert(Object.values(entries).every(n => Number.isInteger(n) && n > 0));
    assert.deepEqual(poolEntries(ownedExports), entries);
    executeOracle('qstr-pool', owned, control);
    console.log('Isolated qstr pools: exact object layouts, strings, lengths and hashes match GCC:', entries);
    return { module: 'py/qstr.c', mode: 'owned object with stock headers; isolated QEMU oracle', entries };
}

function qualifyBinary() {
    const { owned, control } = stockOracleObjects('binary', 'binary-float');
    const libc = checked(gcc, [...arm, '-print-file-name=libg.a']).trim();
    const libgcc = checked(gcc, [...arm, '-print-libgcc-file-name']).trim();
    const stem = path.join(work, 'binary-float-fixture');
    const fixture = `${stem}.o`;
    checked(gcc, [...arm, '-E', '-P', '-DOWNED_FLOAT_FIXTURE', path.join(here, 'binary-float-oracle.c'), '-o', `${stem}.i`]);
    for (const [, tool, args] of ownedStages(stem, fixture, compiler)) checked(tool, args);
    executeOracle('binary-float', owned, control, [fixture, libc, libgcc]);
    console.log('Isolated binary float ABI: 99 format/byte-order/value combinations match GCC in both directions');
    return { module: 'py/binary.c', mode: 'owned object with stock headers; isolated QEMU oracle', cases: 99 };
}

function qualifyRuntime() {
    const { owned, control } = stockOracleObjects('runtime', 'runtime-varargs');
    executeOracle('runtime-varargs', owned, control, [], ['-mfloat-abi=soft']);
    console.log('Isolated runtime varargs: exact MicroPython objects match GCC across the register/stack boundary');
    return { module: 'py/runtime.c', mode: 'owned object with stock headers; isolated QEMU oracle', symbol: 'mp_raise_msg_varg' };
}

try {
    const args = process.argv.slice(2);
    assert(args.length === 0 || (args.length === 1 && args[0] === '--core'), 'usage: node micropython/survey.mjs [--core]');
    const db = JSON.parse(fs.readFileSync(path.join(firmware, 'compile_commands.json'), 'utf8'));
    if (args.length) assert.match(fs.readFileSync(path.join(firmware, 'firmware.elf.map'), 'utf8'), /libg\.a\(libc_a-strcmp\.o\)/);
    const results = [];
    for (const module of args.length ? [...modules, ...coreModules] : modules) {
        const result = probeModule(db, module, modules.includes(module));
        results.push(result);
        console.log(`${module} [${result.stage}]: ${result.diagnostic ?? 'object generated'}`);
        if (result.bridge)
            console.log(`  bridge [${result.bridge.stage}]: ${result.bridge.diagnostic ?? 'object generated'}`);
    }

    assert.equal(results.find(r => r.module === 'py/ringbuf.c').stage, 'object-ready');
    const ringbuf = path.join(work, 'survey-ringbuf.o');
    const source = path.join(upstream, 'py/ringbuf.c');
    const names = ['get16', 'peek16', 'put16', 'get_bytes', 'put_bytes'];
    const gccObject = path.join(firmware, 'CMakeFiles/firmware.dir', `${source}.o`);
    const control = path.join(work, 'ringbuf-control.o');
    checked('arm-none-eabi-objcopy', [...names.map(n => `--redefine-sym=ringbuf_${n}=gcc_${n}`), gccObject, control]);
    executeOracle('ringbuf', ringbuf, control);
    const sizes = file => Object.fromEntries([...checked('llvm-nm', ['--defined-only', '--print-size', file])
        .matchAll(/^[\da-f]+ ([\da-f]+) T ringbuf_(\w+)$/gm)]
        .map(([, bytes, fn]) => [fn, Number.parseInt(bytes, 16)]));
    const ownedBytes = sizes(ringbuf), gccBytes = sizes(gccObject);
    for (const map of [ownedBytes, gccBytes]) assert.deepEqual(Object.keys(map).sort(), [...names].sort());
    console.log('Isolated ringbuf: 2,048 operations passed; public symbol bytes:', { owned: ownedBytes, gcc: gccBytes });
    results.push({ module: 'py/ringbuf.c', mode: 'owned object with stock headers; isolated QEMU oracle',
        operations: 2048, ownedBytes, gccBytes });
    if (results.some(r => r.module === 'py/qstr.c' && r.stage === 'object-ready'))
        results.push(qualifyQstr());
    if (results.some(r => r.module === 'py/binary.c' && r.stage === 'object-ready'))
        results.push(qualifyBinary());
    if (results.some(r => r.module === 'py/runtime.c' && r.stage === 'object-ready'))
        results.push(qualifyRuntime());
    fs.writeFileSync(path.join(work, args.length ? 'survey-core.json' : 'survey.json'), JSON.stringify(results, null, 2) + '\n');
} catch (error) {
    console.error(`MicroPython survey: ${error.message}`);
    process.exitCode = 1;
}
