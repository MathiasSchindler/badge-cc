'use strict';

const path = require('node:path');
const { spawnSync } = require('node:child_process');
const catalogue = path.resolve(__dirname, '../src/profiles.mk');

function profileFlags(names) {
    if (names.some(name => !/^[a-z][a-z0-9-]*$/.test(name)))
        throw new Error('Invalid controller selection name');
    const input = names.map(name => `ifeq ($(filter ${name},$(CLEAN_PROFILE_NAMES)),)\n` +
        `$(error Unknown controller selection '${name}')\nendif`).join('\n') +
        '\nprint-profiles:\n' + names.map(name =>
            `\t@printf '%s\\n' '${name}:$(CLEAN_PROFILE_${name})'`).join('\n') + '\n';
    const result = spawnSync('make', ['-s', '--no-print-directory', '-f', catalogue, '-f', '-', 'print-profiles'],
        { input, encoding: 'utf8', env: { ...process.env, MAKEFLAGS: '', MFLAGS: '' } });
    if (result.error) throw result.error;
    if (result.status !== 0) throw new Error(result.stdout + result.stderr);
    const lines = result.stdout.trim().split('\n');
    if (lines.length !== names.length) throw new Error('Incomplete controller profile response');
    return Object.fromEntries(lines.map((line, index) => {
        const prefix = `${names[index]}:`;
        if (!line.startsWith(prefix)) throw new Error(`Unexpected controller profile response: ${line}`);
        return [names[index], line.slice(prefix.length).trim()];
    }));
}

module.exports = { profileFlags };
