import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { captureSmallFunctions, inventoryFunctions, instructionLedger, reconcileImage,
  qualificationContract, verifySmallFunctionReport } from '../tools/small-function-attribution.mjs';
import { disassemblyFunctions } from '../tools/backend-attribution.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const fixture = body => `define void @"general_name"(ptr %p, i32 %n) {\nentry:\n${body}\n}\n`;
test('inventory covers pointer predicates, barriers, loops and call-live candidates without claiming proofs', () => {
  const [row] = inventoryFunctions(fixture(`  br label %loop
loop:
  %v = phi i32 [%n, %entry], [%next, %loop]
  %byte = load i8, ptr %p
  store volatile i8 %byte, ptr %p
  call void asm sideeffect "nop\\0Anop", "~{memory}"()
  call void @sink(i32 %v)
  %next = sub i32 %v, 1
  %cmp = icmp ult ptr %p, %p
  br i1 %cmp, label %loop, label %end
end:
  ret void`));
  assert.equal(row.function, 'general_name');
  assert.equal(row.ordinary_calls, 1);
  assert.equal(row.assembly_operations, 1);
  assert.equal(row.proved_nop_text_candidates, 1);
  assert.equal(row.volatile_operations, 1);
  assert.equal(row.byte_loads, 1);
  assert.equal(row.adjacent_pointer_predicates, 1);
  assert.equal(row.backward_textual_edges, 1);
  assert(row.lexically_call_live_values.includes('%v'));
  assert(row.fact_limits.some(limit => limit.includes('not CFG')));
});

test('unknown assembly and malformed inventories never masquerade as no-clobber nops', () => {
  const [row] = inventoryFunctions(fixture('  call void asm sideeffect "mov r4, #0", ""()\n  ret void'));
  assert.equal(row.unknown_assembly_operations, 1);
  assert.equal(row.proved_nop_text_candidates, 0);
  assert.throws(() => inventoryFunctions('define void @f() {\n'), /unparsed/);
  assert.throws(() => inventoryFunctions('\0'), /NUL/);
  assert.throws(() => inventoryFunctions(fixture('  ret void') + fixture('  ret void')), /duplicate/);
});

test('machine-byte ledger separates data, copies, constants and frame traffic and reconciles ELF totals', () => {
  const dis = disassemblyFunctions(`00000000 <f>:
 0: b580          push {r7, lr}
 2: 4608          mov r0, r1
 4: 2000          movs r0, #0
 6: 6838          ldr r0, [r7]
 8: 00000001      .word 0x1`);
  const symbols = [{ name: 'f', value: 1, size: 12, type: 2, section: 1 }];
  const [fn] = instructionLedger(dis, symbols, ['r7']);
  assert.equal(fn.instruction_bytes, 8);
  assert.equal(fn.literal_bytes, 4);
  assert.equal(fn.unresolved_bytes, 0);
  assert.equal(fn.categories.register_copies, 2);
  assert.equal(fn.categories.constant_materialization, 2);
  assert.equal(fn.categories.frame_memory, 2);
  assert.equal(instructionLedger(dis, symbols, ['sp'])[0].categories.non_frame_memory, 2,
    'an LLVM ordinary r7 pointer is not an assumed frame base');
  const metrics = { text: 20, data: 0, bss: 0, sections: [{}, { address: 0x10000000 }],
    symbols: [...symbols, { name: 'table', type: 1, size: 4, section: 1 }] };
  assert.deepEqual(reconcileImage(metrics, 20, [fn]), { loaded_bytes: 20, c_function_bytes: 12,
    constant_object_bytes: 4, startup_metadata_and_inter_symbol_bytes: 4, unresolved_function_bytes: 0 });
  assert.throws(() => reconcileImage(metrics, 21, [fn]), /ELF\/raw/);
  assert.throws(() => reconcileImage({ ...metrics, bss: 4 }, 20, [fn]), /BSS/);
  assert.throws(() => instructionLedger(dis, [{ ...symbols[0], size: 1 }]), /crosses/);
  assert.throws(() => instructionLedger([], symbols), /missing disassembly/);
});

test('undecoded function bytes stay explicit unknowns, not assumed padding or cycles', () => {
  const dis = disassemblyFunctions('00000000 <f>:\n 0: bf00          nop\n');
  const [fn] = instructionLedger(dis, [{ name: 'f', value: 0, size: 4, type: 2, section: 1 }]);
  assert.equal(fn.unresolved_bytes, 2);
  assert.equal(fn.unknowns.length, 1);
});

test('delta-focused gate preserves exact correctness/noise requirements and does not clone the old seven-profile matrix', () => {
  assert.equal(qualificationContract.static_gate.production_modules, 37);
  assert.equal(qualificationContract.static_gate.ordinary_images, 12);
  assert.deepEqual(qualificationContract.hardware_gate.profiles, ['baseline', 'candidate']);
  assert.deepEqual(qualificationContract.hardware_gate.seeds, [3276377847, 123456789]);
  assert.equal(qualificationContract.hardware_gate.repeats, 2);
  assert.equal(qualificationContract.hardware_gate.swap_board_roles, true);
  assert.equal(qualificationContract.hardware_gate.exact_noise_and_control_drift, true);
  assert(Object.values(qualificationContract.mechanism_targets).every(value => value === 0));
  assert.equal(qualificationContract.reuse.verify_hashes_before_reuse, true);
});

test('capture refuses an existing directory before touching its contents', t => {
  const directory = fs.mkdtempSync(path.join(root, 'build/po10-refusal-'));
  t.after(() => fs.rmSync(directory, { recursive: true }));
  fs.writeFileSync(path.join(directory, 'keep'), 'untouched');
  assert.throws(() => captureSmallFunctions(directory), /EEXIST/);
  assert.deepEqual(fs.readdirSync(directory), ['keep']);
});

test('persistent actual-source report recomputes and rejects hash, inventory and contract tampering',
  { skip: !process.env.PO10_REPORT }, t => {
    const file = path.resolve(root, process.env.PO10_REPORT);
    const original = verifySmallFunctionReport(file, true);
    const directory = fs.mkdtempSync(path.join(root, 'build/po10-verify-'));
    t.after(() => fs.rmSync(directory, { recursive: true }));
    const write = report => {
      const file = path.join(directory, 'results.json');
      fs.writeFileSync(file, JSON.stringify(report));
      return file;
    };
    const copy = () => JSON.parse(JSON.stringify(original));
    const changed = copy();
    changed.analysis.inventory.volatile_io.functions++;
    assert.throws(() => verifySmallFunctionReport(write(changed)), /does not recompute/);
    const hash = copy();
    hash.artifacts[0].sha256 = '0'.repeat(64);
    assert.throws(() => verifySmallFunctionReport(write(hash)));
    const contract = copy();
    contract.contract.hardware_gate.repeats = 1;
    assert.throws(() => verifySmallFunctionReport(write(contract)), /contract drift/);
    const omitted = copy();
    omitted.stages.pop();
    assert.throws(() => verifySmallFunctionReport(write(omitted)), /stage inventory/);
    const assembler = copy();
    assembler.assembler_control.binary = assembler.stages.find(stage => stage.id === 'owned-fixed').binary;
    assert.throws(() => verifySmallFunctionReport(write(assembler)), /changed loaded bytes/);
    const input = copy();
    input.assembler_control.input_assembly = input.stages.find(stage => stage.id === 'owned-fixed').assembly;
    assert.throws(() => verifySmallFunctionReport(write(input)), /control input differs/);
  });
