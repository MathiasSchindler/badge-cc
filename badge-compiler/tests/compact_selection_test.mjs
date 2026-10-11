import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const clang = process.env.LLVM_CLANG || 'clang';
const objcopy = process.env.LLVM_OBJCOPY || 'llvm-objcopy';
const objdump = process.env.LLVM_OBJDUMP || 'llvm-objdump';
const mode = process.env.BADGE_TEST_MODE || 'oracle';
assert(['owned', 'oracle'].includes(mode), 'BADGE_TEST_MODE must be owned or oracle');
function run(command, args) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8',
    timeout: 120000, maxBuffer: 32 * 1024 * 1024 });
  assert.ifError(r.error);
  assert.equal(r.status, 0, `${command}: ${r.stderr}`);
  return r.stdout;
}
const tool = name => path.join(root, 'build', name);
function directory(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/compact-test-'));
  t.after(() => {
    for (const name of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir, name));
    fs.rmdirSync(dir);
  });
  return name => path.join(dir, name);
}
function assemble(file, source) {
  fs.writeFileSync(file('input.s'), source +
    '\n.text\n.global entry\n.type entry, %function\n.thumb_func\nentry:\nbx lr\n.size entry, .-entry\n');
  run(tool('badge-asm'), [file('input.s'), '-o', file('owned.o')]);
  if (mode === 'oracle') {
    run(clang, ['--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb',
      '-c', file('input.s'), '-o', file('llvm.o')]);
    for (const compiler of ['owned', 'llvm'])
      run(objcopy, ['-O', 'binary', '--only-section=.text', file(`${compiler}.o`), file(`${compiler}.bin`)]);
    assert.deepEqual(fs.readFileSync(file('owned.bin')), fs.readFileSync(file('llvm.bin')));
    run(objdump, ['-d', file('owned.o')]);
  }
  run(tool('badge-link'), ['-T', 'tests/fixtures/link_oracle_fixture.ld',
    file('owned.o'), '-o', file('owned.elf')]);
  return JSON.parse(run(tool('badge-elf-info'), ['--metrics', file('owned.elf')]));
}
test(`all allocated-register combinations verify explicit widths (${mode} mode)`, t => {
  const file = directory(t);
  const source = run(tool('compact_selection_test'), []);
  const report = assemble(file, source);
  const bodies = [...source.matchAll(/compact_(\d+):\n([^\n]+)\nbx lr/g)];
  assert.equal(bodies.length, 24826);
  const sizes = new Map(report.symbols.map(s => [s.name, s.size]));
  for (const [, id, instruction] of bodies)
    assert.equal(sizes.get(`compact_${id}`), instruction.includes('.n ') ? 4 : 6,
      `${id}: ${instruction}`);
});
const header = 'target triple = "thumbv8m.main-unknown-none-eabi"\n';
test('immediate boundaries, high registers and release/sanitizer API policies agree', t => {
  const file = directory(t);
  for (const residency of ['stack', 'local', 'global', 'global-flex', 'global-compact']) {
    const bodies = [];
    for (const op of ['add', 'sub', 'shl', 'lshr', 'ashr'])
      for (const n of [0, 1, 7, 8, 31, 255, 256, 4095, 4096, -7, -255]) {
        if (['shl', 'lshr', 'ashr'].includes(op) && (n < 0 || n > 31)) continue;
        bodies.push(`define i32 @f${bodies.length}(i32 %x) {\nentry:\n%v = ${op} i32 %x, ${n}\nret i32 %v\n}\n`);
      }
    fs.writeFileSync(file('input.ll'), header + bodies.join(''));
    for (const scalar of ['thumb-layout', 'thumb-compact']) {
      for (const suffix of ['', '-sanitize'])
        run(tool(`badge-codegen${suffix}`), [`--scalar-selection=${scalar}`,
          `--register-residency=${residency}`, file('input.ll'), '-o', file(`out${suffix}.s`)]);
      assert.equal(fs.readFileSync(file('out.s'), 'utf8'), fs.readFileSync(file('out-sanitize.s'), 'utf8'));
      const source = fs.readFileSync(file('out.s'), 'utf8');
      // Generated functions use individual text sections; compare all text below.
      assemble(file, source.replace(/\.section \.text\.[^\n]+/g, '.text'));
      if (scalar === 'thumb-compact' && residency === 'stack') {
        assert.match(source, /adds\.n r0, r0, #255/);
        assert.match(source, /subs\.n r0, r0, #7/);
        assert.match(source, /lsls\.n r0, r0, #31/);
        assert.match(source, /addw r0, r0, #4095/);
        assert.match(source, /add\.w r0, r0, #256/);
      }
    }
  }
});
test('carry chains and comparison/IT windows retain flag-preserving emissions', t => {
  const file = directory(t);
  fs.writeFileSync(file('input.ll'), header + `define i64 @wide(i64 %a, i64 %b) {
entry:
%s = add i64 %a, %b
%d = sub i64 %s, %a
ret i64 %d
}
define i32 @compare(i32 %a, i32 %b) {
entry:
%p = icmp slt i32 %a, %b
%s = select i1 %p, i32 %a, i32 %b
ret i32 %s
}
`);
  run(tool('badge-codegen'), ['--scalar-selection=thumb-compact', file('input.ll'), '-o', file('out.s')]);
  const source = fs.readFileSync(file('out.s'), 'utf8');
  assert.match(source, /adds\.w r0, r0, r2\nadc\.w r1, r1, r3/);
  assert.match(source, /subs\.w r0, r0, r2\nsbc\.w r1, r1, r3/);
  assert.match(source, /cmp r0, r1\nmov\.w r0, #0\nit lt\nmovlt r0, #1/);
  assemble(file, source.replace(/\.section \.text\.[^\n]+/g, '.text'));
});
