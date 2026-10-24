#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');

function inside(file, directory) {
    const relative = path.relative(directory, file);
    return relative === '' || (!relative.startsWith(`..${path.sep}`) &&
        relative !== '..' && !path.isAbsolute(relative));
}

function documents(directory) {
    return fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
        if (['node_modules', 'build', '.git'].includes(entry.name)) return [];
        const file = path.join(directory, entry.name);
        if (entry.isSymbolicLink()) throw new Error(`Symlink in documentation tree: ${file}`);
        if (entry.isDirectory()) return documents(file);
        return entry.isFile() && entry.name.endsWith('.md') ? [file] : [];
    });
}

function unescape(text) {
    const entities = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: '\u00a0' };
    return text.replace(/\\([\\`*{}\[\]()#+.!_<>~-])/g, '$1')
        .replace(/&(amp|lt|gt|quot|apos|nbsp|#x[0-9a-f]+|#\d+);/gi, (match, entity) => {
            if (entity[0] !== '#') return entities[entity.toLowerCase()];
            const point = entity[1].toLowerCase() === 'x' ?
                parseInt(entity.slice(2), 16) : Number(entity.slice(1));
            return point > 0 && point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff) ?
                String.fromCodePoint(point) : '\ufffd';
        });
}

function htmlAttributes(html) {
    const attributes = [];
    for (const tag of html.replace(/<!--[\s\S]*?-->/g, '').matchAll(/<[a-z][\w:-]*\b([^>]*)>/gi)) {
        for (const attribute of tag[1].matchAll(/(?:^|\s)(href|src|id|name)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+))/gi))
            attributes.push({ name: attribute[1].toLowerCase(),
                value: unescape(attribute[2] ?? attribute[3] ?? attribute[4]) });
    }
    return attributes;
}

function destination(text) {
    const match = text.trim().match(/^(?:<([^>]*)>|((?:\\.|[^\s])+))/);
    return unescape(match ? match[1] ?? match[2] : '');
}

function parse(file) {
    const lines = fs.readFileSync(file, 'utf8').replace(/<!--[\s\S]*?-->/g,
        comment => comment.replace(/[^\n]/g, ' ')).split(/\r?\n/);
    const anchors = new Set(), headings = new Set(), links = [], references = new Map();
    const label = text => unescape(text).trim().replace(/\s+/g, ' ').toLowerCase();
    let fence;
    for (let i = 0; i < lines.length; i++) {
        const marker = lines[i].match(/^ {0,3}(`{3,}|~{3,})(.*)$/);
        if (fence) {
            if (marker && marker[1][0] === fence[0] && marker[1].length >= fence.length && !marker[2].trim())
                fence = undefined;
            lines[i] = '';
        } else if (marker) {
            fence = marker[1];
            lines[i] = '';
        }
    }
    for (let i = 0; i < lines.length; i++) {
        const definition = lines[i].match(/^ {0,3}\[([^\]]+)\]:\s*(.+)$/);
        if (definition) {
            const key = label(definition[1]);
            if (!references.has(key)) references.set(key, destination(definition[2]));
            lines[i] = '';
        }
    }
    for (let i = 0; i < lines.length; i++) {
        const heading = lines[i].match(/^ {0,3}#{1,6}\s+(.+?)\s*$/);
        const setext = lines[i].trim() && /^ {0,3}(?:=+|-+)\s*$/.test(lines[i + 1] || '');
        if (heading || setext) {
            const text = (heading ? heading[1].replace(/\s+#+$/, '') : lines[i].trim())
                .replace(/!?\[([^\]]*)\](?:\([^)]*\)|\[[^\]]*\])?/g, '$1')
                .replace(/<\/?[a-z][^>]*>/gi, '').replace(/[`*~]/g, '')
                .replace(/(^|[\s(])_+|_+(?=$|[\s)])/g, '$1');
            const base = unescape(text).toLowerCase().replace(/[^\p{L}\p{M}\p{N}_\-\s]/gu, '').replace(/\s/g, '-');
            let slug = base, suffix = 0;
            while (headings.has(slug)) slug = `${base}-${++suffix}`;
            headings.add(slug);
            anchors.add(slug);
        }
        let text = lines[i].replace(/(`+)(.*?)\1/g, code => ' '.repeat(code.length));
        for (const attribute of htmlAttributes(text)) {
            if (attribute.name === 'id' || attribute.name === 'name') anchors.add(attribute.value);
            else links.push({ href: attribute.value, line: i + 1 });
        }
        // Match the repository's single-line links, including one nested filename parenthesis.
        text = text.replace(/!?\[([^\]\n]*)\]\((<[^>\n]*>|(?:\\.|[^()\\\n]|\([^()\n]*\))*)\)/g,
            (match, title, href) => {
                links.push({ href: destination(href), line: i + 1 });
                return ' '.repeat(match.length);
            });
        for (const match of text.matchAll(/!?\[([^\]]+)\](?:\[([^\]]*)\])?/g)) {
            const key = label(match[2] || match[1]);
            if (references.has(key)) links.push({ href: references.get(key), line: i + 1 });
        }
    }
    return { anchors, links };
}

async function checkDocumentation(repository) {
    const root = fs.realpathSync(repository);
    const reference = path.join(root, 'clean-rx/doc');
    const firmware = ['src', 'host', 'badge', 'messenger'].map(name => path.join(root, 'clean-rx', name));
    const parsed = new Map(), errors = [];
    const files = documents(path.join(root, 'clean-rx'));
    const read = file => {
        if (!parsed.has(file)) parsed.set(file, parse(file));
        return parsed.get(file);
    };
    function permitted(target) {
        return inside(target, reference) ||
            (firmware.some(directory => inside(target, directory)) && !/\.md$/i.test(target));
    }
    let localLinks = 0;
    for (const file of files) {
        for (const { href, line } of read(file).links) {
            const location = `${path.relative(root, file)}:${line}`;
            const fail = message => errors.push(`${location}: ${message}: ${href}`);
            const isReference = inside(file, reference);
            if (/^(?:https?:)?\/\//i.test(href)) {
                const url = new URL(href.startsWith('//') ? `https:${href}` : href);
                const match = url.pathname.match(/^\/MathiasSchindler\/badge-cc\/(?:blob|tree)\/[^/]+\/(.+)$/i);
                if (isReference && url.hostname.toLowerCase() === 'github.com' && match) {
                    const target = path.resolve(root, decodeURIComponent(match[1]));
                    if (!permitted(target)) fail('Reference links to development material');
                }
                continue;
            }
            if (/^[a-z][a-z0-9+.-]*:/i.test(href)) {
                if (/^file:/i.test(href)) fail('Nonportable file URL');
                continue;
            }
            localLinks++;
            let name, anchor;
            try {
                const hash = href.indexOf('#');
                name = decodeURIComponent((hash < 0 ? href : href.slice(0, hash)).split('?')[0]);
                anchor = hash < 0 ? '' : decodeURIComponent(href.slice(hash + 1));
            } catch (error) {
                if (!(error instanceof URIError)) throw error;
                fail('Malformed URL encoding');
                continue;
            }
            const target = name ? (name.startsWith('/') ? path.resolve(root, `.${name}`) :
                path.resolve(path.dirname(file), name)) : file;
            if (!inside(target, root)) { fail('Link escapes repository'); continue; }
            if (isReference && !permitted(target)) {
                fail('Reference links to development material');
                continue;
            }
            if (!fs.existsSync(target)) { fail('Missing local target'); continue; }
            const real = fs.realpathSync(target);
            if (!inside(real, root)) { fail('Link escapes repository through symlink'); continue; }
            if (isReference && !permitted(real)) {
                fail('Reference links to development material through symlink');
                continue;
            }
            let document = real;
            if (anchor && fs.statSync(real).isDirectory()) document = path.join(real, 'README.md');
            if (anchor && document.endsWith('.md')) {
                if (!fs.existsSync(document) || !read(document).anchors.has(anchor))
                    fail('Missing Markdown anchor');
            } else if (anchor && fs.statSync(real).isDirectory()) fail('Missing Markdown anchor');
        }
    }
    if (errors.length) throw new Error(errors.join('\n'));
    return { files: files.length, localLinks };
}

module.exports = { checkDocumentation };
if (require.main === module) {
    checkDocumentation(path.resolve(__dirname, '../..')).then(result => {
        console.log(`PASS: ${result.files} Markdown files, ${result.localLinks} local links; reference boundaries checked.`);
    }).catch(error => {
        console.error(error.message);
        process.exitCode = 1;
    });
}
