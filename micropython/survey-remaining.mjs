import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { isMainThread, parentPort, Worker, workerData } from 'node:worker_threads';
import { decodeElf } from '../badge-compiler/tools/boundary-evidence.mjs';
import { ownedStages, preprocessStock } from './stock-input.mjs';

const root = path.dirname(import.meta.dirname);
const work = path.join(import.meta.dirname, 'build');
const upstream = path.join(work, 'upstream');
const database = path.join(work, 'badge-interpreter/compile_commands.json');
const tools = path.join(root, 'badge-compiler/build');
const timeout = 25_000;
const extensions = ['.i', '.ll', '.opt.ll', '.s', '.o'];
const standardLibc = new Set(['py/objint.c', 'py/objstr.c']);

function run(tool, args, options) {
    const r = spawnSync(tool, args, { cwd: root, encoding: 'utf8', timeout, ...options });
    if (r.error) throw r.error;
    if (r.status !== 0) throw new Error(r.stderr?.trim() || (r.signal ?? `exit ${r.status}`));
}

function probe(row, stem) {
    const rel = path.relative(upstream, row.file);
    const source = rel.startsWith('..' + path.sep) ? path.relative(work, row.file) : rel;
    const group = rel.startsWith('..' + path.sep) ? 'generated' :
        /^(lib|ports)\//.test(rel) ? rel.split('/').slice(0, 2).join('/') : rel.split('/')[0];
    let active = 'GCC preprocessing';
    const result = { source, group, standardLibc: standardLibc.has(source) };
    try {
        preprocessStock([row], row.file, `${stem}.i`, run);
        for (const [stage, tool, args] of ownedStages(stem, `${stem}.o`, tools, [],
            result.standardLibc ? ['--libc-strcmp'] : [])) {
            active = stage;
            run(tool, args);
        }
        active = 'ELF verification';
        const object = fs.readFileSync(`${stem}.o`);
        assert.equal(object.readUInt16LE(16), 1);
        const { sections } = decodeElf(object);
        return { ...result, status: 'object-ready',
            codeBytes: sections.filter(s => s.flags & 4).reduce((sum, s) => sum + s.size, 0) };
    } catch (error) {
        const diagnostic = error.message.replaceAll(stem, '<translation-unit>')
            .replaceAll(upstream + '/', '').replaceAll(root + '/', '');
        const first = diagnostic.split('\n')[0];
        const located = first.match(/^badge-[\w-]+: <translation-unit>[^:]*:(\d+):\s*(.*)$/);
        return { ...result, status: error.code === 'ETIMEDOUT' ? 'timed-out' : 'blocked', stage: active,
            reason: located?.[2] ?? first, diagnostic, inputLine: located ? Number(located[1]) : null };
    } finally {
        for (const ext of extensions) fs.rmSync(`${stem}${ext}`, { force: true });
    }
}

if (isMainThread) {
    const rows = JSON.parse(fs.readFileSync(database, 'utf8')).filter(row => row.file.endsWith('.c'));
    assert(rows.length && new Set(rows.map(row => row.file)).size === rows.length, 'Expected distinct C files');
    const jobs = 6;
    const temp = fs.mkdtempSync(path.join(work, 'compile-audit-temp-'));
    try {
        const batches = await Promise.allSettled(Array.from({ length: jobs }, (_, i) =>
            new Promise((resolve, reject) => {
                const worker = new Worker(import.meta.filename,
                    { workerData: { rows: rows.filter((_, n) => n % jobs === i), stem: path.join(temp, `${i}`) } });
                let result;
                worker.on('message', data => { result = data; });
                worker.on('error', reject);
                worker.on('exit', status => {
                    if (status || !result) reject(new Error(`Worker ${i} exited ${status}`));
                    else resolve(result);
                });
            })));
        const results = batches.flatMap(batch => {
            if (batch.status === 'rejected') throw batch.reason;
            return batch.value;
        }).sort((a, b) => a.source.localeCompare(b.source));
        assert.equal(results.length, rows.length);
        const mode = 'GCC preprocessing; owned frontend (--float-lowering=base-pcs), optimizer, codegen and assembler; not linked';
        const json = path.join(work, 'remaining-c-audit.json');
        fs.writeFileSync(json, JSON.stringify({ database: path.relative(work, database), mode, rows: results }, null, 2) + '\n');
        const keys = ['source', 'group', 'status', 'stage', 'reason', 'inputLine', 'codeBytes', 'standardLibc'];
        const cell = value => `"${String(value ?? '').replaceAll('"', '""')}"`;
        const csv = [keys.join(','), ...results.map(row => keys.map(k => cell(row[k])).join(','))].join('\r\n');
        fs.writeFileSync(path.join(work, 'remaining-c-audit.csv'), csv + '\r\n');
        const counts = Object.create(null);
        for (const r of results) {
            const key = r.stage ?? r.status;
            counts[key] = (counts[key] ?? 0) + 1;
        }
        console.log(rows.length, 'C files:', counts);
    } finally {
        fs.rmdirSync(temp);
    }
} else {
    parentPort.postMessage(workerData.rows.map(row => probe(row, workerData.stem)));
}
