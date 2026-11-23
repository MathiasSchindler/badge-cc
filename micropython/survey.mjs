import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

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

function words(command) {
    return [...command.matchAll(/(?:[^\s"'\\]|\\.|"(?:[^"\\]|\\.)*"|'[^']*')+/g)]
        .map(m => m[0].replace(/"((?:[^"\\]|\\.)*)"|'([^']*)'|\\(.)/g,
            (_, double, single, escaped) => double !== undefined
                ? double.replace(/\\(["\\$`])/g, '$1') : single ?? escaped));
}

const modules = ['py/ringbuf.c', 'py/smallint.c', 'py/parsenumbase.c', 'py/unicode.c',
    'py/sequence.c', 'shared/timeutils/timeutils.c'];

try {
    const db = JSON.parse(fs.readFileSync(path.join(firmware, 'compile_commands.json'), 'utf8'));
    const results = [];
    for (const module of modules) {
        const source = path.join(upstream, module);
        const row = db.find(e => e.file === source);
        assert(row, `Missing GCC compilation command for ${module}`);
        const [tool, ...args] = row.arguments ?? words(row.command);
        const at = args.indexOf('-o');
        assert(at >= 0);
        const stem = path.join(work, `survey-${path.basename(module, '.c')}`);
        args.splice(at, 2, '-o', `${stem}.i`);
        args[args.indexOf('-c')] = '-E';
        checked(tool, [...args, '-P'], { cwd: row.directory });
        const preprocessed = fs.readFileSync(`${stem}.i`, 'utf8');
        fs.writeFileSync(`${stem}.i`, preprocessed.replace(/^[ \t]*#pragma GCC diagnostic[^\n]*$/gm, ''));
        const probe = run(path.join(compiler, 'badge-ir'), [`${stem}.i`, '-o', `${stem}.ll`]);
        const diagnostic = probe.status === 0 ? null : probe.stderr.trim();
        results.push({ module, mode: 'GCC preprocessing with stock headers; owned frontend', diagnostic });
        console.log(`${module}: ${diagnostic ?? 'frontend accepted'}`);
    }

    const ringbuf = path.join(work, 'ringbuf-owned.o');
    const source = path.join(upstream, 'py/ringbuf.c');
    checked('bash', [path.join(root, 'badge-compiler/scripts/owned-cc.sh'),
        '-std=c11', '-Oz', '-mcpu=cortex-m33', '-mthumb', '-ffreestanding',
        `-I${path.join(here, 'include')}`, `-I${path.join(root, 'src/shared')}`, `-I${upstream}`,
        '-c', source, '-o', ringbuf]);
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
    results.push({ module: 'py/ringbuf.c', mode: 'owned pipeline with minimal headers; isolated QEMU oracle',
        operations: 2048, ownedBytes, gccBytes });
    fs.writeFileSync(path.join(work, 'survey.json'), JSON.stringify(results, null, 2) + '\n');
} catch (error) {
    console.error(`MicroPython survey: ${error.message}`);
    process.exitCode = 1;
}
