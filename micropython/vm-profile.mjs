import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { ownedStages } from './stock-input.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const board = path.join(here, 'vm-profile');
const build = path.join(here, 'build/vm-profile');
const upstream = path.join(here, 'build/upstream');
const tools = path.resolve(here, '../badge-compiler/build');
const port = path.join(upstream, 'ports/qemu');
const jobs = process.env.JOBS ?? '4';
const qemu = process.env.QEMU_ARM ?? 'qemu-system-arm';
const pinned = '0fd6c573ea815774668bbb16b8e197c8822368b2';

function run(tool, args, { cwd, log, timeout = 120000 } = {}) {
    const r = spawnSync(tool, args, { cwd, encoding: 'utf8', timeout, maxBuffer: 8 * 1024 * 1024 });
    const output = `${r.stdout ?? ''}${r.stderr ?? ''}`;
    if (log) fs.writeFileSync(path.join(build, `${log}.log`), output);
    if (r.error || r.status !== 0) {
        throw new Error(`${tool}: ${r.error?.message ?? `exit ${r.status}`}\n${output.slice(-5000)}`);
    }
    return { stdout: r.stdout, stderr: r.stderr };
}

function make(targets, log) {
    return run('make', ['-j', jobs, `BOARD_DIR=${board}`, `BUILD=${build}`,
        'FROZEN_MANIFEST=', 'CFLAGS_EXTRA=-Oz', ...targets], { cwd: port, log, timeout: 240000 });
}

function workload() {
    const smoke = fs.readFileSync(path.join(here, 'interpreter-smoke.py'), 'utf8');
    const parts = smoke.split('\nsamples = []\n');
    assert.equal(parts.length, 2, 'Expected one timed phase in the interpreter smoke script');
    const source = `${parts[0]}
gc.collect()
result, kept = total(4096)
gc.collect()
assert result == 589824
assert len(kept) == 512 and kept[0] == (0, 3) and kept[-1] == (4088, 203)
`;
    return `static char workload[] = {\n    ${[...Buffer.from(source)].join(', ')}, 0\n};\n`;
}

function symbols(elf) {
    const { stdout } = run('arm-none-eabi-nm', ['-nS', '--defined-only', elf]);
    const find = name => {
        const row = stdout.split('\n').find(line => line.endsWith(` ${name}`));
        assert(row, `Missing linked symbol: ${name}`);
        const [addr, size] = row.slice(0, -name.length).trim().split(/\s+/);
        assert.match(addr, /^[0-9a-f]+$/);
        return { addr: Number.parseInt(addr, 16), size: /^[0-9a-f]+$/.test(size) ? Number.parseInt(size, 16) : 0 };
    };
    return {
        function: find('mp_execute_bytecode'),
        stack: [find('_profile_stack_bottom').addr, find('_estack').addr],
    };
}

function profile(elf, plugin, label) {
    const { function: vm, stack } = symbols(elf);
    assert(vm.size > 0 && stack[0] < stack[1]);
    const range = ([start, end]) => `${start.toString(16)}:${end.toString(16)}`;
    const { stdout, stderr } = run(qemu, ['-M', 'mps2-an505', '-smp', '1', '-nographic', '-monitor', 'none',
        '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', elf,
        '-plugin', `file=${plugin},code=${range([vm.addr, vm.addr + vm.size])},stack=${range(stack)}`],
        { log: label });
    assert.match(stdout + stderr, /^VM_REPLAY_OK\r?$/m);
    assert.match(stderr, /^VM_PROFILE_END$/m);
    const { stdout: assembly } = run('arm-none-eabi-objdump', ['--disassemble=mp_execute_bytecode', elf]);
    const disassembly = new Map([...assembly.matchAll(/^\s*([0-9a-f]+):\t[\da-f ]+\s+\t([^\n]+)$/gm)]
        .map(([, pc, insn]) => [Number.parseInt(pc, 16), insn.trim()]));
    const seen = new Set();
    const rows = [...stderr.matchAll(/^VM_SITE ([0-9a-f]+) (\d+) (\d+) (\d+)$/gm)]
        .map(([, address, instructions, reads, writes]) => {
            const pc = Number.parseInt(address, 16);
            assert(pc >= vm.addr && pc < vm.addr + vm.size && disassembly.has(pc),
                `Missing VM disassembly for executed PC ${address}`);
            assert(!seen.has(pc), `Duplicate VM PC ${address}`);
            seen.add(pc);
            return { offset: pc - vm.addr, instructions: Number(instructions),
                reads: Number(reads), writes: Number(writes), instruction: disassembly.get(pc) };
        });
    assert(rows.length > 0);
    const stackTraffic = { directSp: { reads: 0, writes: 0 }, saveRestore: { reads: 0, writes: 0 },
        otherBase: { reads: 0, writes: 0 } };
    const totals = rows.reduce((acc, row) => {
        acc.instructions += row.instructions;
        acc.stackReads += row.reads;
        acc.stackWrites += row.writes;
        const kind = /^(?:push|pop|stmdb|ldmia)(?:\.[nw])?\s+/.test(row.instruction) &&
            (/^(?:push|pop)[.\s]/.test(row.instruction) || /\s+sp!/.test(row.instruction))
            ? 'saveRestore' : /\[sp(?:,|\])/.test(row.instruction) ? 'directSp' : 'otherBase';
        stackTraffic[kind].reads += row.reads;
        stackTraffic[kind].writes += row.writes;
        return acc;
    }, { instructions: 0, stackReads: 0, stackWrites: 0 });
    assert(totals.instructions > 0 && totals.stackReads > 0 && totals.stackWrites > 0);
    return { executableBytes: vm.size, stack, totals, stackTraffic, rows };
}

function linkedObjects(name) {
    const map = fs.readFileSync(path.join(build, `profile-${name}.map`), 'utf8');
    const objects = [...map.matchAll(/^LOAD (.+\.o)$/gm)].map(([, object]) => object);
    const vm = path.join(build, `vm-${name}.o`);
    assert.equal(objects.filter(object => object === vm).length, 1, `${name}: unexpected VM link`);
    assert(!objects.includes(path.join(build, 'py/vm.o')), `${name}: stock VM also linked`);
    return objects.filter(object => object !== vm);
}

try {
    const fixedIr = process.argv[2] === '--fixed-ir' && process.argv.length === 4
        ? path.resolve(process.argv[3]) : null;
    assert(process.argv.length === 2 || fixedIr, 'usage: node micropython/vm-profile.mjs [--fixed-ir FILE]');
    assert.equal(run('git', ['rev-parse', 'HEAD'], { cwd: upstream }).stdout.trim(), pinned);
    assert.equal(run('git', ['status', '--porcelain', '--untracked-files=no'], { cwd: upstream }).stdout.trim(), '');
    fs.mkdirSync(build, { recursive: true });
    fs.writeFileSync(path.join(build, 'workload.h'), workload());
    make(['-B', 'all', path.join(build, 'profile-vm.i')], 'gcc-build');
    run('make', ['-j', jobs, 'all'], { cwd: path.dirname(tools), log: 'owned-build' });
    const stem = path.join(build, 'profile-vm');
    fs.writeFileSync(`${stem}.i`, fs.readFileSync(`${stem}.i`, 'utf8').replace(/^[ \t]*#pragma GCC diagnostic[^\n]*$/gm, ''));
    const stages = ownedStages(stem, path.join(build, 'vm-owned.o'), tools, [], ['--libc-strcmp']);
    if (fixedIr) {
        fs.copyFileSync(fixedIr, `${stem}.opt.ll`);
        stages.splice(0, 2);
    }
    for (const [label, tool, args] of stages) {
        run(tool, args, { log: label });
    }
    const variants = ['gcc', 'owned'];
    make(variants.map(name => path.join(build, `profile-${name}.elf`)), 'link');
    assert.deepEqual(linkedObjects('gcc'), linkedObjects('owned'), 'The non-VM link inputs must be identical');
    const glib = run('pkg-config', ['--cflags', 'glib-2.0']).stdout.trim().split(/\s+/);
    const libraryOptions = process.platform === 'darwin'
        ? ['-dynamiclib', '-undefined', 'dynamic_lookup'] : ['-fPIC', '-shared'];
    const plugin = path.join(build, 'count.so');
    const headers = process.env.QEMU_PLUGIN_INCLUDE ? ['-I', process.env.QEMU_PLUGIN_INCLUDE] : [];
    run(process.env.HOST_CC ?? 'cc', ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        ...libraryOptions, ...glib, ...headers, path.join(board, 'count.c'), '-o', plugin]);
    const results = Object.fromEntries(variants.map(name => {
        const elf = path.join(build, `profile-${name}.elf`);
        const first = profile(elf, plugin, `${name}-1`);
        assert.deepEqual(profile(elf, plugin, `${name}-2`), first, `${name}: non-repeatable replay`);
        return [name, { ...first, elfSha256: crypto.createHash('sha256').update(fs.readFileSync(elf)).digest('hex') }];
    }));
    const report = { revision: pinned, machine: 'mps2-an505', workload: 'interpreter total(4096)',
        fixedIrSha256: fixedIr && crypto.createHash('sha256').update(fs.readFileSync(fixedIr)).digest('hex'),
        qemu: run(qemu, ['--version']).stdout.split('\n')[0],
        gcc: run('arm-none-eabi-gcc', ['--version']).stdout.split('\n')[0], results };
    fs.writeFileSync(path.join(build, 'comparison.json'), JSON.stringify(report, null, 2) + '\n');
    for (const [variant, result] of Object.entries(results)) console.log(variant, result.totals);
} catch (error) {
    console.error(`MicroPython VM profile: ${error.message}`);
    process.exitCode = 1;
}
