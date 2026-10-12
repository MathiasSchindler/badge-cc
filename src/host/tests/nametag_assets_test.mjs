import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const generator = path.resolve(process.env.NAMETAG_GENERATOR ?? 'build/host-tools/nametag_font_compile_sanitized');
const fonts = ['CascadiaMono-Regular.ttf', 'CascadiaMono-Bold.ttf', 'CascadiaCode-Regular.ttf', 'CascadiaCode-Bold.ttf']
  .map(name => path.join(root, 'vendor/cascadia-code', name));
const license = path.join(root, 'vendor/cascadia-code/LICENSE');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function generate(directory, bits, files = fonts, notice = license) {
  fs.mkdirSync(directory);
  const output = path.join(directory, 'nametag_fonts.c'), header = path.join(directory, 'nametag_fonts.h');
  const result = spawnSync(generator, [output, header, ...files, String(bits), notice],
    { cwd: root, encoding: 'utf8', timeout: 120000, maxBuffer: 1000000 });
  assert.ifError(result.error);
  return { result, output, header };
}

function tables(text, font) {
  const pixels = new RegExp(`static const uint8_t pixels_${font}\\[\\] = \\{([^}]+)\\}`).exec(text);
  const glyphs = new RegExp(`static const badger_font_glyph_t glyphs_${font}\\[\\] = \\{([\\s\\S]+?)\\n\\};`).exec(text);
  assert(pixels && glyphs);
  return {
    pixels: pixels[1].match(/\d+/g).map(Number),
    glyphs: [...glyphs[1].matchAll(/\{([^}]+)\}/g)].map(match => match[1].match(/-?\d+/g).map(Number))
  };
}

test('pinned font provenance, deterministic generator, loss-bounded 4-bit packing and malformed/missing inputs', () => {
  const expected = [
    '06520d032ec274fa5040b22c6f4a1d829081b24ba40b2da56dae89bf10c7b481',
    'b22cb603ed23cac36e8444846e1841caca21719a5e852780a8d37ae7a49b0a36',
    'c33ef522cdfeff99907fb54f3e97152bb18bfb9b56ec2fef4d4ceec51c8974a4',
    '58197c5c3ba2967d4780b0ef8aee72d0cbbfa42885ddede9ec96fa7a1df5aead'
  ];
  fonts.forEach((file, index) => assert.equal(hash(file), expected[index], 'pinned font changed'));
  assert.equal(hash(license), '51882cd3cdba4e16f220f44ddb08a635c38c44ea6e0975db2574f4be6f958238');
  const base = fs.mkdtempSync(path.join(root, 'build/host-tools/nametag-assets-test-'));
  try {
    const first = generate(path.join(base, 'first'), 8);
    const second = generate(path.join(base, 'second'), 8);
    const packed = generate(path.join(base, 'packed'), 4);
    for (const run of [first, second, packed]) {
      assert.equal(run.result.status, 0, run.result.stderr);
      assert.match(run.result.stdout, /20 fonts, 206 glyphs\/font/);
    }
    assert.equal(hash(first.output), hash(second.output));
    assert.equal(hash(first.header), hash(second.header));
    const eight = fs.readFileSync(first.output, 'utf8'), four = fs.readFileSync(packed.output, 'utf8');
    assert.match(eight, /SIL OPEN FONT LICENSE Version 1\.1/);
    for (let font = 0; font < 20; ++font) {
      const a = tables(eight, font), b = tables(four, font);
      assert.equal(a.glyphs.length, 206);
      for (let i = 0; i < 206; ++i) {
        const ga = a.glyphs[i], gb = b.glyphs[i];
        assert.deepEqual([ga[0], ...ga.slice(2)], [gb[0], ...gb.slice(2)]);
        for (let pixel = 0; pixel < ga[5] * ga[6]; ++pixel) {
          const value = b.pixels[gb[1] + Math.floor(pixel / 2)];
          const decoded = (pixel % 2 ? value & 15 : value >> 4) * 17;
          assert(Math.abs(a.pixels[ga[1] + pixel] - decoded) <= 8, '4-bit coverage error exceeds eight levels');
        }
      }
    }
    const malformed = path.join(base, 'malformed.ttf');
    fs.writeFileSync(malformed, 'not a font');
    const invalid = generate(path.join(base, 'invalid'), 8, [malformed, ...fonts.slice(1)]);
    assert.notEqual(invalid.result.status, 0);
    assert.match(invalid.result.stderr, /malformed|unsupported/);
    assert(!fs.existsSync(invalid.output) && !fs.existsSync(invalid.header));
    const missing = generate(path.join(base, 'missing'), 8, [path.join(base, 'absent.ttf'), ...fonts.slice(1)]);
    assert.notEqual(missing.result.status, 0);
    assert.match(missing.result.stderr, /No such file|i\/o error/);
    const noLicense = generate(path.join(base, 'no-license'), 8, fonts, malformed);
    assert.notEqual(noLicense.result.status, 0);
    assert.match(noLicense.result.stderr, /invalid Cascadia OFL/);

    const bytes = fs.readFileSync(fonts[0]);
    let cmap = 0;
    for (let table = 0; table < bytes.readUInt16BE(4); ++table) {
      const offset = 12 + table * 16;
      if (bytes.toString('ascii', offset, offset + 4) === 'cmap') cmap = bytes.readUInt32BE(offset + 8);
    }
    assert(cmap > 0);
    // A valid cmap with just 'B' demonstrates missing-glyph rejection without fallback.
    bytes.fill(0, cmap, cmap + 40);
    bytes.writeUInt16BE(1, cmap + 2);
    bytes.writeUInt16BE(3, cmap + 4);
    bytes.writeUInt16BE(10, cmap + 6);
    bytes.writeUInt32BE(12, cmap + 8);
    bytes.writeUInt16BE(12, cmap + 12);
    bytes.writeUInt32BE(28, cmap + 16);
    bytes.writeUInt32BE(1, cmap + 24);
    bytes.writeUInt32BE(66, cmap + 28);
    bytes.writeUInt32BE(66, cmap + 32);
    bytes.writeUInt32BE(1, cmap + 36);
    const subset = path.join(base, 'missing-glyph.ttf');
    fs.writeFileSync(subset, bytes);
    const absentGlyph = generate(path.join(base, 'missing-glyph'), 8, [subset, ...fonts.slice(1)]);
    assert.notEqual(absentGlyph.result.status, 0);
    assert.match(absentGlyph.result.stderr, /required glyph U\+0020 missing/);
    assert(!fs.existsSync(absentGlyph.output));
  } finally { fs.rmSync(base, { recursive: true, force: true }); }
});
