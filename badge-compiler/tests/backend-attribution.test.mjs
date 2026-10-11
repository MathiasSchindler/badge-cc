import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { backendFlags, canonicalIr, disassemblyFunctions, traceFunctions,
  llvmSpillAnnotations, emissionInventory } from '../tools/backend-attribution.mjs';

test('fixed IR removes only filename metadata and disables normal LLVM IR optimization', () => {
  assert.equal(canonicalIr('source_filename = "a"\ndefine void @f() {\n ret void\n}\n'),
    canonicalIr('source_filename = "b"\ndefine void @f() {\n ret void\n}\n'));
  assert.notEqual(canonicalIr('ret i32 1\n'), canonicalIr('ret i32 2\n'));
  assert(backendFlags.includes('-Oz'));
  assert(backendFlags.includes('-disable-llvm-passes'));
  assert(backendFlags.includes('-mcpu=cortex-m33'));
});

test('opportunity inventory counts only adjacent comparison branches and required volatile work', () => {
  const inventory = emissionInventory({ entry: ['push.w {r7, lr}'], operations: [
    { ir: '%v0 = icmp slt i32 %x, 0', assembly: ['cmp r0, #0', 'mov r0, #0'] },
    { ir: 'br i1 %v0, label %a, label %b', assembly: ['cmp r0, #0'] },
    { ir: 'store volatile i32 1, ptr %p', assembly: ['str.w r0, [r4]'] },
    { ir: '%v1 = load i32, ptr @global', assembly: ['ldr.w r0, .Lcg_lit1', 'b.w .Lcg_after1'] },
  ] });
  assert.equal(inventory.adjacent_compare_branches, 1);
  assert.equal(inventory.volatile_ir_operations, 1);
  assert.equal(inventory.literal_pool_skip_branches, 1);
});

test('instruction bytes exclude marked data and categories distinguish frame traffic', () => {
  const [row] = disassemblyFunctions(`00000000 <f>:
       0: b580          push {r7, lr}
00000002 <.Lcg_block>:
       2: f8d7 0004     ldr.w r0, [r7, #0x4]
       6: 6001          str r1, [r0]
       8: f000 f801     bl 0xe <g>
       c: 4608          mov r0, r1
       e: f100 0007     add.w r0, r0, #8
      12: d001          beq 0x18
      14: bd80          pop {r7, pc}
      16: 12345678      .word 0x12345678`);
  assert.equal(row.instructions.length, 8);
  assert.equal(row.executable_bytes, 22);
  assert.equal(row.literal_bytes, 4);
  assert.equal(row.loads, 1); assert.equal(row.stores, 1);
  assert.equal(row.frame_loads, 1); assert.equal(row.frame_stores, 0);
  assert.equal(row.calls, 1); assert.equal(row.branches, 1);
  assert.equal(row.copies, 1); assert.equal(row.extensions, 0);
});

test('IR emission trace separates entry, private objects, PHI transport and scalar homes', () => {
  const ir = 'define void @f() {\n %x = load i32, ptr %a\n br label %b\n}\n';
  const [row] = traceFunctions(`@ attribution-function f
@ attribution-slot %a 0 4 8 4 1
@ attribution-slot %x 4 4 0 0 0
@ attribution-register %x 8
@ attribution-phi 12 4
@ stack-frame: 16 + 32 saved-register bytes
str.w r0, [r7, #0]
@ attribution-ir 2
ldr.w r0, [r7, #8]
str.w r0, [r7, #4]
@ attribution-ir 3
str.w r0, [r7, #12]
str.w r0, [r7, #4]
ldr.w r0, [r7, #48]
`, ir);
  assert.equal(row.registers['%x'], 8);
  assert.deepEqual(row.stack_categories, {
    'parameter-entry-homes-or-incoming-arguments:store': 1,
    'private-frame-object:load': 1,
    'ssa-or-parameter-homes-not-eviction-spills:store': 1,
    'phi-transfer-buffer:store': 1,
    'phi-edge-home-transfer:store': 1,
    'incoming-arguments:load': 1,
  });
  assert.throws(() => traceFunctions('@ attribution-function f\n@ attribution-ir 99\n', ir), /missing IR/);
});

  test('LLVM allocator annotations are not confused with all frame accesses', () => {
    const rows = llvmSpillAnnotations(`.type f,%function
   .pad #16
   str r0, [sp, #4] @ 4-byte Spill
   ldr r0, [sp, #4] @ 4-byte Reload
   str r0, [sp, #8]
   .type g,%function
   str r0, [sp, #4] @ 4-byte Folded Spill
  `);
    assert.deepEqual(rows.f, { spill_stores: 1, reload_loads: 1, frame_adjustment_bytes: 16 });
    assert.equal(rows.g.spill_stores, 1);
    const [fn] = disassemblyFunctions('00000000 <f>:\n 0: 6838          ldr r0, [r7]\n', ['sp']);
    assert.equal(fn.loads, 1);
    assert.equal(fn.frame_loads, 0, 'LLVM r7 can be an ordinary base, not a frame pointer');
  });

for (const role of ['llvm', 'llvm-frontend']) test(`${role} hybrid executes actual application semantic tests on Cortex-M33`, () => {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'backend-attribution-'));
  const run = (command, args) => {
    const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000,
      maxBuffer: 16 * 1024 * 1024 });
    assert.ifError(result.error);
    assert.equal(result.status, 0, `${command}: ${result.stdout}\n${result.stderr}`);
  };
  try {
    const objects = [];
    for (const source of ['badge-compiler/tests/application_test.c',
      'src/platform/badger/graphics.c', 'src/tools/diagnostics_logic.c']) {
      const output = path.join(directory, `${path.basename(source)}.o`);
      run(process.execPath, ['badge-compiler/tools/backend-attribution.mjs', '--cc', role,
        '--target=arm-none-eabi', '-mcpu=cortex-m33', '-mthumb', '-std=c11', '-Oz',
        '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
        '-DCOMPILER_TEST_APPLICATION', '-DCOMPILER_TEST_TIMING',
        '-Isrc/shared', '-Isrc/platform/badger', '-Isrc/tools', '-c', source, '-o', output]);
      const meta = JSON.parse(fs.readFileSync(`${output}.owned/crossover.json`));
      assert.equal(meta.normal_llvm_ir_optimizer, role === 'llvm-frontend');
      assert.equal(meta.args.includes('-disable-llvm-passes'), role !== 'llvm-frontend');
      objects.push(output);
    }
    const start = path.join(directory, 'start.o');
    run('badge-compiler/build/badge-asm', ['badge-compiler/tests/fixtures/codegen_start.S', '-o', start]);
    const elf = path.join(directory, 'application.elf');
    run(process.env.LLVM_LLD ?? 'ld.lld', ['--gc-sections',
      '-T', 'badge-compiler/tests/fixtures/tilt_maze_fixture.ld', start, ...objects, '-o', elf]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic',
      '-monitor', 'none', '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', elf]);
  } finally {
    for (const name of fs.readdirSync(directory)) {
      const file = path.join(directory, name);
      if (fs.statSync(file).isDirectory()) {
        for (const child of fs.readdirSync(file)) fs.unlinkSync(path.join(file, child));
        fs.rmdirSync(file);
      } else fs.unlinkSync(file);
    }
    fs.rmdirSync(directory);
  }
});
