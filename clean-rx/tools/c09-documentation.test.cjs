'use strict';

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { checkDocumentation } = require('./documentation.cjs');

async function fixture(callback) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'clean-rx-c09-'));
    const write = (file, text) => {
        const target = path.join(root, file);
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, text);
    };
    try {
        write('clean-rx/doc/README.md', '# Reference\n');
        write('clean-rx/dev-doc/README.md', '# Development\n');
        write('clean-rx/src/main.c', '');
        await callback(root, write);
    } finally {
        fs.rmSync(root, { recursive: true });
    }
}

test('documentation checks inline/reference/image links, encoded paths and GitHub heading anchors', async () => {
    await fixture(async (root, write) => {
        write('clean-rx/doc/a file.md', '# *PHY* `RX` &amp; TX!\n## Same\n## Same\n## Same-1\n\nSetext\n======\n');
        write('clean-rx/doc/image.svg', '<svg/>');
        write('clean-rx/doc/README.md', [
            '# Reference',
            '[inline](<a file.md#phy-rx--tx>)',
            '[reference][target]', '[collapsed][]', '[shortcut]',
            '[duplicate](a%20file.md#same-1)', '[collision](a%20file.md#same-1-1)',
            '[setext](a%20file.md#setext)', '![image](image.svg)',
            '[source](../src/main.c)', '[directory](../src/)',
            '[empty]()', '[self](#reference)',
            '1. Item', '    [list continuation](../src/main.c)',
            '', '[target]: a%20file.md#same "Title"',
            '[collapsed]: a%20file.md#same-1',
            '[shortcut]: a%20file.md#setext',
            '```md', '[example](missing.md)', '# Not a heading', '```',
            '`[inline code](missing.md)`',
            '[public](https://example.invalid/not-fetched)',
        ].join('\n'));
        const result = await checkDocumentation(root);
        assert.equal(result.files, 3);
        assert.equal(result.localLinks, 13);
    });
});

test('missing local targets, images and Markdown anchors fail with source locations', async () => {
    await fixture(async (root, write) => {
        for (const link of ['[missing](gone.md)', '![missing](gone.png)',
            '[missing](#gone)', '[missing][target]\n\n[target]: gone.md',
            '[missing](../src/#gone)', '[bad](bad%ZZ.md)']) {
            write('clean-rx/doc/README.md', `# Reference\n${link}\n`);
            await assert.rejects(checkDocumentation(root), /clean-rx\/doc\/README.md:2: (Missing|Malformed)/);
        }
    });
});

test('HTML links and explicit anchors are checked, but code examples are ignored', async () => {
    await fixture(async (root, write) => {
        write('clean-rx/doc/README.md', '# Reference\n<span id="explicit"></span>\n<a href="#explicit">link</a>\n<img src="../src/main.c">\n<!-- <a href="absent.md">comment</a> -->\n<div data-href="absent.md"></div>\n');
        assert.equal((await checkDocumentation(root)).localLinks, 2);
        write('clean-rx/doc/README.md', '<a href="#absent">bad</a>\n');
        await assert.rejects(checkDocumentation(root), /Missing Markdown anchor/);
        write('clean-rx/doc/README.md', '<img src="absent.svg">\n');
        await assert.rejects(checkDocumentation(root), /Missing local target/);
    });
});

test('reference boundary rejects direct, encoded, reference-style and repository-URL development links', async () => {
    await fixture(async (root, write) => {
        write('clean-rx/tests/README.md', '# Tests\n');
        write('clean-rx/tools/runner.cjs', '');
        write('clean-rx/cleanup.md', '# Cleanup\n');
        write('todo.md', '# Work\n');
        write('clean-rx/badge/README.md', '# Application development\n');
        for (const link of [
            '[dev](../dev-doc/README.md)', '[dev](../%64ev-doc/README.md)',
            '[test](../tests/README.md)', '[tool](../tools/runner.cjs)',
            '[roadmap](../cleanup.md)', '[work](/todo.md)',
            '[mixed](../badge/README.md)',
            '[reference][dev]\n\n[dev]: ../dev-doc/README.md',
            '<a href="../dev-doc/README.md">dev</a>',
            '[remote](https://github.com/MathiasSchindler/badge-cc/blob/main/clean-rx/dev-doc/README.md)',
        ]) {
            write('clean-rx/doc/README.md', `${link}\n`);
            await assert.rejects(checkDocumentation(root), /Reference links to development material/);
        }
        write('clean-rx/doc/README.md', '# Reference\n');
        write('clean-rx/dev-doc/README.md', '[reference](../doc/README.md)\n[roadmap](../cleanup.md)\n');
        await checkDocumentation(root);
    });
});

test('repository escapes, symlink bypasses and nonportable file URLs fail', async () => {
    await fixture(async (root, write) => {
        for (const link of ['[escape](../../../outside.md)', '[file](file:///tmp/outside.md)']) {
            write('clean-rx/doc/README.md', link);
            await assert.rejects(checkDocumentation(root), /escapes repository|Nonportable file URL/);
        }
        write('clean-rx/doc/README.md', '[alias](../src/alias.c)\n');
        write('clean-rx/dev-doc/notes.c', '');
        fs.symlinkSync(path.join(root, 'clean-rx/dev-doc/notes.c'), path.join(root, 'clean-rx/src/alias.c'));
        await assert.rejects(checkDocumentation(root), /Symlink|development material through symlink/);
    });
});

test('real documentation and Make entry point pass without running firmware or hardware', async () => {
    const root = path.resolve(__dirname, '../..');
    const result = await checkDocumentation(root);
    assert.ok(result.files >= 32);
    const command = spawnSync('make', ['-C', path.join(root, 'clean-rx'), 'check-docs'], { encoding: 'utf8' });
    assert.equal(command.status, 0, command.stdout + command.stderr);
    assert.match(command.stdout, /PASS:/);
    await fixture(async (directory, write) => {
        write('clean-rx/tools/documentation.cjs', fs.readFileSync(path.join(__dirname, 'documentation.cjs')));
        for (const [link, error] of [
            ['[development](../dev-doc/README.md)', /Reference links to development material/],
            ['[missing](#missing)', /Missing Markdown anchor/],
        ]) {
            write('clean-rx/doc/README.md', link);
            const failure = spawnSync(process.execPath, [path.join(directory, 'clean-rx/tools/documentation.cjs')],
                { encoding: 'utf8' });
            assert.equal(failure.status, 1, failure.stdout + failure.stderr);
            assert.match(failure.stderr, error);
            assert.doesNotMatch(failure.stdout, /PASS:/);
        }
    });
});
