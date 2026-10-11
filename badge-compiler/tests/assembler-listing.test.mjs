import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { verifyListing, placements, linkListing, verifiedEmissionSections, verifyLinkedRanges } from '../tools/assembler-listing.mjs';
import { readObject } from '../tools/assembler-crossover.mjs';

const tool = fileURLToPath(new URL('../build/badge-asm', import.meta.url));
function fixture(run) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'ba02-'));
  try { run(directory); } finally { fs.rmSync(directory, { recursive: true }); }
}
const assembly = `.syntax unified
.cpu cortex-m33
.thumb
.file "escaped\\\\path\\\"name.c"
.section .text.entry,"ax",%progbits
.global entry
.type entry,%function
.thumb_func
entry:
@ attribution-ir 2
movs r0, #1
b far
.space 2050
far:
ldr r0, literal
b.n done
.p2align 3
literal:
.word external
done:
bx lr
.byte 0
.p2align 4, 90, 1
.section .bss,"aw",%nobits
.space 17
.section .rodata,"a",%progbits
.asciz "héllo"
.ident "listing-test"
`;
function assemble(directory, source = assembly) {
  const input = path.join(directory, 'quote"name.s'), object = path.join(directory, 'out.o'),
    listing = path.join(directory, 'out.json'), ir = path.join(directory, 'origin.ll');
  fs.writeFileSync(input, source); fs.writeFileSync(ir, '\nret i32 1\n');
  const args = ['--listing', listing, '--ir-source', ir, input, '-o', object];
  const response = spawnSync(tool, args, { encoding: 'utf8' });
  assert.equal(response.status, 0, response.stderr);
  return { input, object, listing, ir, args, data: JSON.parse(fs.readFileSync(listing)) };
}

test('listing covers every emitted byte, metadata, NOBITS, relocations and encoding decisions', () => fixture(dir => {
  const result = assemble(dir);
  const counts = verifyListing(result.data, result.object, result.input, result.ir);
  assert.equal(counts.instruction, 12);
  assert.equal(counts.nobits, 17);
  assert(counts['generated-metadata'] > 0 && counts.alignment > 0);
  const branch = result.data.ranges.find(r => r.operation === 'b');
  assert.equal(branch.width, 4); assert.match(branch.decision, /exceeds narrow reach/);
  assert(result.data.decisions.some(d => d.max_skip_exceeded));
  assert(result.data.ranges.some(r => r.operation === 'b.n' && r.decision === 'explicit narrow encoding'));
  assert(result.data.ranges.some(r => r.relocations.some(rel => rel.symbol === 'external')));
  assert(result.data.ranges.some(r => r.origin_line === 2));
  const plain = path.join(dir, 'plain.o');
  assert.equal(spawnSync(tool, [result.input, '-o', plain]).status, 0);
  assert(fs.readFileSync(plain).equals(fs.readFileSync(result.object)));
}));

test('independent verifier rejects gaps, changed bytes, changed source, IR marker and relocation tampering', () => fixture(dir => {
  const { data, object, input, ir } = assemble(dir);
  for (const mutate of [
    d => d.ranges[0].offset++,
    d => d.ranges[0].hex = '0000',
    d => d.source += 'x',
    d => d.ranges.find(r => r.origin_line).origin_line = 1,
    d => d.ranges.find(r => r.relocations.length).relocations[0].type = 3,
  ]) {
    const broken = structuredClone(data); mutate(broken);
    assert.throws(() => verifyListing(broken, object, input, ir));
  }
}));

test('both linker map formats preserve section-offset VMA/LMA and missing/ambiguous contributions', () => {
  const object = '/capture/assembler/unit.o';
  for (const line of [
    `10000100 10000100 00000020     4   ${object}:(.text)`,
    `10000100 10000100       20     4         ${object}:(.text)`,
  ]) {
    const rows = placements(line, object), listing = { ranges: [
      { section: '.text', offset: 4, size: 2 }, { section: '.bss', offset: 0, size: 2 },
    ] };
    assert.equal(linkListing(listing, rows)[0].address, 0x10000104);
    assert.equal(linkListing(listing, rows)[1].address, null);
    assert.equal(linkListing(listing, [...rows, ...rows])[0].placement, 'ambiguous');
    assert.throws(() => linkListing({ ranges: [{ section: '.text', offset: 31, size: 2 }] }, rows));
  }
});

test('assembly failure and invalid listing options preserve existing outputs', () => fixture(dir => {
  const result = assemble(dir), before = fs.readFileSync(result.object), listingBefore = fs.readFileSync(result.listing);
  fs.writeFileSync(result.input, 'invalid r0\n');
  assert.notEqual(spawnSync(tool, result.args).status, 0);
  assert(fs.readFileSync(result.object).equals(before));
  assert(fs.readFileSync(result.listing).equals(listingBefore));
  fs.writeFileSync(result.input, assembly);
  for (const args of [
    ['--listing', result.object, result.input, '-o', result.object],
    ['--listing', result.input, result.input, '-o', result.object],
    ['--listing', result.listing, '--ir-source', result.ir, result.input, '-o', result.ir],
    ['--ir-source', result.ir, result.input, '-o', result.object],
    ['--listing', result.listing, '--listing', result.listing, result.input, '-o', result.object],
  ]) assert.notEqual(spawnSync(tool, args).status, 0);
  assert(fs.readFileSync(result.object).equals(before));
}));

test('nonexistent path aliases, malformed IR markers, incbin aliases and listing budgets fail explicitly', () => fixture(dir => {
  const input = path.join(dir, 'input.s'), output = path.join(dir, 'new.o'), listing = path.join(dir, 'listing.json'),
    ir = path.join(dir, 'input.ll'), binary = path.join(dir, 'input.bin');
  fs.writeFileSync(input, 'movs r0, #1\n'); fs.writeFileSync(ir, 'ret void\n');
  let response = spawnSync(tool, ['--listing', `${dir}/./new.o`, input, '-o', output], { encoding: 'utf8' });
  assert.notEqual(response.status, 0); assert.match(response.stderr, /aliases/); assert(!fs.existsSync(output));
  for (const marker of ['@ attribution-ir 0', '@ attribution-ir 999', '@ attribution-ir 1 garbage']) {
    fs.writeFileSync(input, `${marker}\nmovs r0, #1\n`);
    response = spawnSync(tool, ['--listing', listing, '--ir-source', ir, input, '-o', output], { encoding: 'utf8' });
    assert.notEqual(response.status, 0); assert.match(response.stderr, /IR|marker/);
  }
  fs.writeFileSync(binary, 'abc'); fs.writeFileSync(input, `.incbin "${binary}"\n`);
  assert.notEqual(spawnSync(tool, ['--listing', binary, input, '-o', output]).status, 0);
  assert.equal(fs.readFileSync(binary, 'utf8'), 'abc');
  fs.writeFileSync(input, '.data\n.space 34000000\n');
  response = spawnSync(tool, ['--listing', listing, input, '-o', output], { encoding: 'utf8' });
  assert.notEqual(response.status, 0); assert.match(response.stderr, /listing exceeds|memory.*limit/);
  assert(!fs.existsSync(output));
}));

const directory = process.env.COMPILE_GRAPH_REPORT ? path.resolve(process.env.COMPILE_GRAPH_REPORT) : null;
test('real BA02 accounts for all 18 inputs, verifies production placements and rejects unsafe trace sections',
  { skip: !directory && 'Set COMPILE_GRAPH_REPORT to a BA02 report.' }, () => {
    const report = JSON.parse(fs.readFileSync(path.join(directory, 'report.json')));
    assert.equal(report.assembler.modules.filter(m => m.listing).length, 18);
    assert.equal(report.assembler.modules.filter(m => m.emission).length, 9);
    for (const image of report.assembler.images) for (const control of Object.values(image.linkers)) {
      const route = control.routes[0];
      assert(route.listings.length > 0);
      for (const file of route.listings) {
        const linked = JSON.parse(fs.readFileSync(path.join(directory, file)));
        const module = report.assembler.modules.find(m => m.module === linked.module && m.source_role === image.source_role);
        const object = readObject(path.join(directory, module.objects.owned));
        verifyLinkedRanges(linked.ranges, object, path.join(directory, route.elf), path.join(directory, route.binary));
        const mapped = linked.ranges.find(r => r.placement === 'mapped' && object.sections.find(s => s.name === r.section).flags & 2);
        if (mapped) {
          const wrong = structuredClone(linked.ranges);
          wrong[linked.ranges.indexOf(mapped)].load_address += 2;
          assert.throws(() => verifyLinkedRanges(wrong, object,
            path.join(directory, route.elf), path.join(directory, route.binary)));
        }
        if (!module.emission) continue;
        const trace = readObject(path.join(directory, module.emission.object));
        assert.deepEqual(verifiedEmissionSections(trace, object), module.emission.verified_sections);
        for (const range of linked.emission_ranges)
          if (!module.emission.verified_sections.includes(range.section))
            assert(range.address === null && range.origin_line === 0 && range.placement === 'trace-differs-from-production');
      }
    }
  });
