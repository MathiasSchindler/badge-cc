import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

function words(command) {
    return [...command.matchAll(/(?:[^\s"'\\]|\\.|"(?:[^"\\]|\\.)*"|'[^']*')+/g)]
        .map(m => m[0].replace(/"((?:[^"\\]|\\.)*)"|'([^']*)'|\\(.)/g,
            (_, double, single, escaped) => double !== undefined
                ? double.replace(/\\(["\\$`])/g, '$1') : single ?? escaped));
}

export function preprocessStock(db, source, output, run) {
    const row = db.find(e => e.file === source);
    assert(row, `Missing GCC compilation command for ${source}`);
    const [tool, ...args] = row.arguments ?? words(row.command);
    const floatFlags = args.filter(arg => arg.startsWith('-mfloat-abi='));
    assert(!floatFlags.length || /^(?:-mfloat-abi=soft|-mfloat-abi=softfp)$/.test(floatFlags.at(-1)),
        `Stock ARM object must use base PCS: ${source}`);
    const at = args.indexOf('-o');
    const compile = args.indexOf('-c');
    assert(at >= 0 && compile >= 0, `Malformed GCC compilation command for ${source}`);
    args.splice(at, 2, '-o', output);
    args[compile] = '-E';
    run(tool, [...args, '-P'], { cwd: row.directory });
    const preprocessed = fs.readFileSync(output, 'utf8');
    fs.writeFileSync(output, preprocessed.replace(/^[ \t]*#pragma GCC diagnostic[^\n]*$/gm, ''));
}

export function bridgeStages(stem, object, tools) {
    // O0 keeps LLVM optimization out; disabling optnone lets badge-opt promote locals.
    return [
        ['compatibility IR', process.env.LLVM_CLANG ?? 'clang',
            ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-mfloat-abi=softfp',
                '-std=gnu23', '-O0', '-fno-builtin', '-fno-discard-value-names', '-fno-unwind-tables',
                '-fno-asynchronous-unwind-tables', '-Xclang', '-disable-O0-optnone',
                '-S', '-emit-llvm', '-x', 'c', `${stem}.i`, '-o', `${stem}.ll`]],
        ...ownedStages(stem, object, tools).slice(1),
    ];
}

export function ownedStages(stem, object, tools, optimizerArgs = [], frontendArgs = []) {
    return [
        ['frontend', path.join(tools, 'badge-ir'), ['--float-lowering=base-pcs', ...frontendArgs, `${stem}.i`, '-o', `${stem}.ll`]],
        ['optimization', path.join(tools, 'badge-opt'), [...optimizerArgs, `${stem}.ll`, '-o', `${stem}.opt.ll`]],
        ['codegen', path.join(tools, 'badge-codegen'), [`${stem}.opt.ll`, '-o', `${stem}.s`]],
        ['assembly', path.join(tools, 'badge-asm'), [`${stem}.s`, '-o', object]],
    ];
}
