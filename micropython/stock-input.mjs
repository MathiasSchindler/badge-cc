import assert from 'node:assert/strict';
import fs from 'node:fs';

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
    const at = args.indexOf('-o');
    const compile = args.indexOf('-c');
    assert(at >= 0 && compile >= 0, `Malformed GCC compilation command for ${source}`);
    args.splice(at, 2, '-o', output);
    args[compile] = '-E';
    run(tool, [...args, '-P'], { cwd: row.directory });
    const preprocessed = fs.readFileSync(output, 'utf8');
    fs.writeFileSync(output, preprocessed.replace(/^[ \t]*#pragma GCC diagnostic[^\n]*$/gm, ''));
}
