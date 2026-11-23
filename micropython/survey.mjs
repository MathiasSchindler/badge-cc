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
    for (const suffix of ['.i', '.ll', '.opt.ll', '.s', '.o'])
        fs.rmSync(`${stem}${suffix}`, { force: true });
    let failure;
    try {
        preprocessStock(db, source, `${stem}.i`, checked);
    } catch (error) {
        failure = { stage: 'preprocessing', diagnostic: error.message };
    }
    if (!failure)
        for (const [stage, tool, args] of ownedStages(stem, `${stem}.o`, compiler)) {
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
        module, mode: 'GCC preprocessing with stock headers; owned frontend through assembly',
        ...(failure ?? { stage: 'object-ready', diagnostic: null, link: 'not attempted by survey' }), bridge,
    };
}

try {
    const args = process.argv.slice(2);
    assert(args.length === 0 || (args.length === 1 && args[0] === '--core'), 'usage: node micropython/survey.mjs [--core]');
    const db = JSON.parse(fs.readFileSync(path.join(firmware, 'compile_commands.json'), 'utf8'));
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
    const oracle = path.join(work, 'ringbuf-oracle.o'), elf = path.join(work, 'ringbuf-oracle.elf');
    checked(gcc, [...arm, '-Os', '-ffreestanding', '-c', path.join(here, 'ringbuf-oracle.c'), '-o', oracle]);
    const start = path.join(work, 'crc-start.o');
    const fixture = path.join(root, 'badge-compiler/tests/fixtures/codegen_fixture.ld');
    checked('ld.lld', ['--gc-sections', '-T', fixture, start, oracle, ringbuf, control, '-o', elf]);
    checked(process.env.QEMU_ARM ?? 'qemu-system-arm',
        ['-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'none',
            '-semihosting-config', 'enable=on,target=native', '-kernel', elf]);
    const sizes = file => Object.fromEntries([...checked('llvm-nm', ['--defined-only', '--print-size', file])
        .matchAll(/^[\da-f]+ ([\da-f]+) T ringbuf_(\w+)$/gm)]
        .map(([, bytes, fn]) => [fn, Number.parseInt(bytes, 16)]));
    const ownedBytes = sizes(ringbuf), gccBytes = sizes(gccObject);
    for (const map of [ownedBytes, gccBytes]) assert.deepEqual(Object.keys(map).sort(), [...names].sort());
    console.log('Isolated ringbuf: 2,048 operations passed; public symbol bytes:', { owned: ownedBytes, gcc: gccBytes });
    results.push({ module: 'py/ringbuf.c', mode: 'owned object with stock headers; isolated QEMU oracle',
        operations: 2048, ownedBytes, gccBytes });
    fs.writeFileSync(path.join(work, args.length ? 'survey-core.json' : 'survey.json'), JSON.stringify(results, null, 2) + '\n');
} catch (error) {
    console.error(`MicroPython survey: ${error.message}`);
    process.exitCode = 1;
}
