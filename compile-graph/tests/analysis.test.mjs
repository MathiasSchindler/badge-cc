import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import test from 'node:test';
import { astRegions, astNode, irRegions, assemblyRegions, objectRegions,
  sourceRegions, sections, symbols, inventory } from '../analysis.mjs';
import { verify } from '../capture.mjs';

test('IR functions, operations and block extents are discovered without segment definitions', () => {
  const input = `define void @loop(i32 %x) {
entry:
  br label %again
again:
  %y = sub i32 %x, 1
  ret void
}
define i32 @"quoted.name"() {
  ret i32 7
}`;
  const regions = irRegions(input);
  assert.deepEqual(regions.map(r => r.name), ['loop', 'quoted.name']);
  assert.deepEqual(regions[0].stats, { br: 1, sub: 1, ret: 1 });
  assert.deepEqual(regions[0].blocks, [
    { name: 'entry', start: 2, end: 3 }, { name: 'again', start: 4, end: 7 },
  ]);
  assert.throws(() => irRegions('define void @bad() {\n  ret void'), /unterminated/);
});

test('assembly regions preserve functions and local labels without treating data as a function', () => {
  const input = `.type run,%function
run:
  cbz r0, .Lexit
.Lloop:
  subs r0, #1
.Lexit:
  bx lr
.size run, .-run
.type table,%object
table:
  .word 1
.type next,@function
next:
  bx lr
.size next, .-next`;
  const regions = assemblyRegions(input);
  assert.deepEqual(regions.map(r => r.name), ['run', 'next']);
  assert.equal(regions[0].end, 8);
  assert.equal(regions[0].blocks[1].name, '.Lexit');
});

test('owned AST uses declarator names, structural node paths and exact node counts', () => {
  const loop = { kind: 'while', children: [{ kind: 'binary', children: [] }] };
  const tree = { ast: { kind: 'translation_unit', children: [
    { kind: 'function_definition', children: [
      { kind: 'specifiers', children: [{ kind: 'typedef_name', text: 'not_the_function' }] },
      { kind: 'init_declarator', children: [{ kind: 'function_declarator', children: [
        { kind: 'name', text: 'actual_function' },
      ] }] },
      { kind: 'block', children: [loop] },
    ] },
  ] } };
  const [region] = astRegions(tree, 'owned');
  assert.equal(region.name, 'actual_function');
  assert.equal(region.stats.while, 1);
  assert.equal(astNode(tree, 'owned', region.blocks[1].path), loop);
});

test('Clang AST excludes prototypes and finds nested regions', () => {
  const tree = { kind: 'TranslationUnitDecl', inner: [
    { kind: 'FunctionDecl', name: 'prototype', inner: [] },
    { kind: 'FunctionDecl', name: 'defined', inner: [
      { kind: 'CompoundStmt', inner: [{ kind: 'IfStmt', inner: [] }] },
    ] },
  ] };
  const [region] = astRegions(tree, 'llvm');
  assert.equal(region.name, 'defined');
  assert.deepEqual(region.blocks.map(b => b.name), ['CompoundStmt 1', 'IfStmt 2']);
  assert.equal(astNode(tree, 'llvm', region.path).name, 'defined');
});

test('source ranges filter included files and retain multiple same-file functions', () => {
  const source = 'void first() {}\nvoid second() {}\n';
  const functionNode = (name, begin, end, file) => ({ kind: 'FunctionDecl', name,
    loc: { ...(file ? { file } : {}), offset: begin },
    range: { begin: { offset: begin }, end: { offset: end, tokLen: 1 } },
    inner: [{ kind: 'CompoundStmt' }] });
  const tree = { inner: [functionNode('included', 0, 14, 'src/shared/header.h'),
    functionNode('first', 0, 14, 'src/tools/demo.c'), functionNode('second', 16, 31)] };
  assert.deepEqual(sourceRegions(source, tree, 'src/tools/demo.c').map(r => r.name), ['first', 'second']);
});

test('section and symbol inventories retain bytes, addresses and symbol kinds', () => {
  assert.deepEqual(sections('.text 24 268435456\n.bss 8 536870912\nTotal 32\n'), [
    { name: '.text', bytes: 24, address: 268435456 }, { name: '.bss', bytes: 8, address: 536870912 },
  ]);
  assert.deepEqual(symbols('10000001 00000018 T run\n20000000 00000008 b state\n'), [
    { address: 0x10000001, bytes: 24, type: 'T', name: 'run' },
    { address: 0x20000000, bytes: 8, type: 'b', name: 'state' },
  ]);
  assert.deepEqual(objectRegions('00000000 <run>:\n  0: bx lr\n00000002 <.Lexit>:\n  2: nop\n')
    .map(r => r.name), ['run']);
});

test('function inventory includes functions missing from one compiler or stage', () => {
  const module = { roles: {
    owned: { stages: { initial: { regions: [{ name: 'helper' }, { name: 'main' }] } } },
    llvm: { stages: { optimized: { regions: [{ name: 'main' }, { name: 'other' }] } } },
  } };
  assert.deepEqual(inventory(module), ['helper', 'main', 'other']);
});

test('offline verification detects content tampering and altered derived regions', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'compile-graph-test-'));
  try {
    const text = 'define void @run() {\n  ret void\n}\n';
    fs.writeFileSync(path.join(directory, 'input.ll'), text);
    const artifact = { file: 'input.ll', bytes: Buffer.byteLength(text),
      sha256: crypto.createHash('sha256').update(text).digest('hex') };
    const item = { ...artifact, format: 'ir', regions: irRegions(text) };
    const report = { schema: 1, status: 'complete', artifacts: [artifact], images: [{}],
      modules: [{ functions: ['run'], roles: {
        owned: { stages: { initial: item }, passes: [] },
        llvm: { stages: { initial: item }, passes: [] },
      } }] };
    const save = () => fs.writeFileSync(path.join(directory, 'report.json'), JSON.stringify(report));
    save();
    assert.equal(verify(directory).status, 'complete');
    item.regions[0].name = 'false_label'; save();
    assert.throws(() => verify(directory), /region mismatch/);
    item.regions = irRegions(text); save();
    fs.writeFileSync(path.join(directory, 'input.ll'), text.replace('run', 'bad'));
    assert.throws(() => verify(directory), /hash mismatch/);
    report.status = 'in-progress'; save();
    assert.throws(() => verify(directory), /incomplete/);
  } finally {
    fs.rmSync(directory, { recursive: true });
  }
});

test('source offsets are UTF-8 byte offsets, not JavaScript character offsets', () => {
  const source = '// café\nvoid run() {}\n', bytes = Buffer.from(source);
  const tree = { inner: [{ kind: 'FunctionDecl', name: 'run',
    loc: { file: 'demo.c' }, range: { begin: { offset: bytes.indexOf('void') },
      end: { offset: bytes.indexOf('}'), tokLen: 1 } }, inner: [{ kind: 'CompoundStmt',
      range: { begin: { offset: bytes.indexOf('{') }, end: { offset: bytes.indexOf('}'), tokLen: 1 } } }] }] };
  const [region] = sourceRegions(source, tree, 'demo.c');
  assert.equal(region.start, 2);
  assert.equal(region.end, 2);
  assert.equal(region.blocks[0].start, 2);
});

test('capture refuses an existing directory without overwriting its files', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'compile-graph-existing-'));
  try {
    const file = path.join(directory, 'report.json');
    fs.writeFileSync(file, 'existing report');
    const result = spawnSync(process.execPath, [
      new URL('../capture.mjs', import.meta.url).pathname, '--capture', directory,
    ], { encoding: 'utf8' });
    assert.equal(result.status, 1);
    assert.match(result.stderr, /EEXIST/);
    assert.equal(fs.readFileSync(file, 'utf8'), 'existing report');
  } finally { fs.rmSync(directory, { recursive: true }); }
});
