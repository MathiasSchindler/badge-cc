import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { decodeElf, compareObjects, relocationAddend, inputPlacements, auditLink,
  auditLoadedLayout, auditBudgets, verifyBoundaryReport } from '../tools/boundary-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 120000 });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stdout + result.stderr);
  return result.stdout;
}
function temporary(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/boundary-test-'));
  t.after(() => {
    for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
    fs.rmdirSync(directory);
  });
  return name => path.join(directory, name);
}
function cloneElf(elf) {
  return { ...elf, sections: elf.sections.map(section => ({ ...section, data: Buffer.from(section.data) })),
    symbols: elf.symbols.map(symbol => ({ ...symbol })), relocations: elf.relocations.map(relocation => ({ ...relocation })) };
}
const assembly = `.syntax unified
.cpu cortex-m33
.thumb
.section .text.entry,"ax",%progbits
.balign 4
.global entry
.type entry,%function
.thumb_func
entry:
 bl callee
 movw r0, #:lower16:(message+7)
 movt r0, #:upper16:(message+7)
 movw r1, #:lower16:(number)
 movt r1, #:upper16:(number)
 bx lr
 .balign 4
 .word message+7
 .word suffix
 .word duplicate
 .word number
 .word common
 .word weak_missing
 .word marker
 .size entry,.-entry
.weak weak_missing
.comm common,8,8
.section .text.callee,"ax",%progbits
.balign 4
.global callee
.type callee,%function
.thumb_func
callee:
 bx lr
.size callee,.-callee
.section .text.dead,"ax",%progbits
.balign 4
.global dead
.thumb_func
dead:
 bx lr
.section .rodata.str1.1,"aMS",%progbits,1
message:
 .asciz "prefix-string"
suffix:
 .asciz "string"
duplicate:
 .asciz "string"
.section .rodata.cst4,"aM",%progbits,4
.balign 4
number:
 .word 0x12345678
.section .noinit,"aw",%progbits
.balign 4
.global marker
marker:
 .word 19
`;
function fixture(t, llvm = false) {
  const file = temporary(t), source = file('input.s'), script = file('script.ld'), object = file('input.o');
  const scriptText = fs.readFileSync(path.join(root, '../src/arch/rp2350/memmap_flash.ld'), 'utf8')
    .replace('ENTRY(bare_reset)', 'ENTRY(entry)');
  fs.writeFileSync(source, assembly); fs.writeFileSync(script, scriptText);
  run('build/badge-asm', [source, '-o', object]);
  const bytes = fs.readFileSync(object), model = decodeElf(bytes);
  const result = { file, source, script, scriptText, object, bytes, model, links: {} };
  for (const role of llvm ? ['owned', 'llvm'] : ['owned']) {
    const elfFile = file(`${role}.elf`), map = file(`${role}.map`);
    run(role === 'owned' ? 'build/badge-link' : process.env.LLVM_LLD ?? 'ld.lld',
      ['-T', script, '--gc-sections', '-Map', map, object, '-o', elfFile]);
    const elf = decodeElf(fs.readFileSync(elfFile)), mapText = fs.readFileSync(map, 'utf8');
    const objects = [{ file: object, object: model }], placements = inputPlacements(mapText);
    result.links[role] = { elf, mapText, placements, objects,
      audit: auditLink(objects, elf, placements), layout: auditLoadedLayout(objects, elf, mapText, scriptText),
      budgets: auditBudgets(elf, scriptText) };
  }
  return result;
}
test('ELF decoding rejects malformed headers, bounds, string tables and relocation shapes', t => {
  const f = fixture(t);
  for (const length of [0, 1, 51, f.bytes.length - 1])
    assert.throws(() => decodeElf(f.bytes.subarray(0, length)));
  for (const mutate of [
    bytes => { bytes[4] = 2; }, bytes => { bytes[5] = 2; },
    bytes => { bytes.writeUInt16LE(62, 18); }, bytes => { bytes.writeUInt32LE(0xfffffff0, 32); },
    bytes => { bytes.writeUInt16LE(0, 48); }, bytes => { bytes.writeUInt16LE(0xffff, 50); },
    bytes => { bytes.writeUInt16LE(0, 46); },
  ]) {
    const bytes = Buffer.from(f.bytes); mutate(bytes); assert.throws(() => decodeElf(bytes));
  }
  assert.equal(f.model.type, 1);
  assert(f.model.relocations.some(relocation => relocation.type === 47));
  const mov = f.model.relocations.find(relocation => relocation.type === 47);
  assert.equal(relocationAddend(f.model, mov), 7);
  assert.throws(() => relocationAddend(f.model, { ...mov, type: 255 }), /unsupported relocation/);
});
test('allocated object equality ignores file bookkeeping but rejects changed instructions and fixups', t => {
  const f = fixture(t), metadata = cloneElf(f.model);
  metadata.symbols.push({ name: 'file.c', binding: 0, type: 4, section: 0xfff1, value: 0 });
  assert.equal(compareObjects(f.model, metadata).owned_symbols + 1, compareObjects(f.model, metadata).llvm_symbols);
  const bad = cloneElf(f.model), section = bad.sections.find(section => section.name === '.text.callee');
  section.data[0] ^= 1;
  assert.throws(() => compareObjects(f.model, bad), /same assembly/);
  const relocation = cloneElf(f.model);
  relocation.relocations[0].type = 30;
  assert.throws(() => compareObjects(f.model, relocation), /same assembly/);
});
test('resolved targets, NOLOAD, GC, physical budgets and corrupted payloads are audited explicitly', t => {
  const f = fixture(t), link = f.links.owned;
  assert(link.audit.noload_sections > 0);
  assert(link.audit.discarded_nonmerge.some(name => name.endsWith(':.text.dead')));
  assert(link.audit.merge_references.length >= 4);
  const badCode = cloneElf(link.elf);
  badCode.sections.find(section => section.name === '.text').data[0] ^= 1;
  assert.throws(() => auditLink(link.objects, badCode, link.placements), /Thumb branch target|opcode/);
  const badValue = cloneElf(link.elf), ref = link.audit.merge_references[0];
  const output = badValue.sections.find(section => ref.address >= section.address &&
    ref.address < section.address + section.size);
  output.data[ref.address - output.address] ^= 1;
  assert.throws(() => auditLink(link.objects, badValue, link.placements), /merge reference value mismatch/);
  assert.throws(() => auditBudgets(link.elf, f.scriptText.replace('0x10000000', '0x10001000')), /physical region/);
  assert.throws(() => auditBudgets(link.elf, f.scriptText.replace('ENTRY(entry)', 'ENTRY(callee)')), /entry point/);
  const map = [...link.placements][0];
  const duplicate = `${map[1].address.toString(16)} 10000000 4 4 ${f.object}:(.text.entry)\n`;
  assert.throws(() => inputPlacements(duplicate + duplicate), /duplicate input placement/);
});
test('LLVM assembly/link crossover verifies interior/duplicate string references, constants and loaded coverage', {
  skip: process.env.BADGE_TEST_MODE === 'owned',
}, t => {
  const f = fixture(t, true), llvmObject = f.file('llvm.o');
  run(process.env.LLVM_CLANG ?? 'clang', ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
    '-x', 'assembler', '-c', f.source, '-o', llvmObject]);
  compareObjects(f.model, decodeElf(fs.readFileSync(llvmObject)));
  const a = f.links.owned, b = f.links.llvm;
  const values = link => link.audit.merge_references.map(reference =>
    [reference.section, reference.offset, reference.bytes]);
  assert.deepEqual(values(b), values(a));
  assert(b.layout.merged.some(group => group.name === '.rodata.cst4'));
  assert(b.layout.merged.some(group => group.name === '.rodata.str1.1'));
  const refs = b.audit.merge_references.filter(reference => reference.target_section === '.rodata.str1.1');
  assert(refs.every(reference => reference.bytes === Buffer.from('string\0').toString('hex')));
  const duplicates = refs.filter(reference => reference.target_offset >= Buffer.byteLength('prefix-string\0'));
  assert.equal(duplicates.length, 2);
  assert.equal(new Set(duplicates.map(reference => reference.address)).size, 1, 'actual exact-string pooling');
  const changed = cloneElf(b.elf), group = b.layout.merged.find(group => group.name === '.rodata.str1.1');
  const section = changed.sections.find(section => group.address >= section.address &&
    group.address < section.address + section.size);
  section.data[group.address - section.address] ^= 1;
  assert.throws(() => auditLoadedLayout(b.objects, changed, b.mapText, f.scriptText), /invented merged value/);
  const hole = b.layout.padding[0];
  assert(hole, 'alignment-fill negative test must exercise an actual gap');
  {
    const padded = cloneElf(b.elf), section = padded.sections.find(section => hole.address >= section.address &&
      hole.address < section.address + section.size);
    section.data[hole.address - section.address] = 0x55;
    assert.throws(() => auditLoadedLayout(b.objects, padded, b.mapText, f.scriptText), /unknown alignment fill/);
  }
});
test('persisted evidence rejects partial status, wrong milestone and altered artifacts', t => {
  const file = temporary(t), artifact = file('artifact'), report = file('report.json');
  fs.writeFileSync(artifact, 'original');
  for (const value of [
    { milestone: 'PO08', status: 'failed' },
    { milestone: 'PO00', status: 'qualified' },
    { milestone: 'PO08', status: 'qualified', artifacts: [{ file: artifact, sha256: '0'.repeat(64) }] },
  ]) {
    fs.writeFileSync(report, JSON.stringify(value));
    assert.throws(() => verifyBoundaryReport(report));
  }
});
