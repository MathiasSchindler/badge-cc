import assert from 'node:assert/strict';
import test from 'node:test';
import { fixedBackendComparison, optimizationRemarks, optimizationTrace } from '../optimization.mjs';

test('LLVM remarks preserve provenance and do not invent locations or reasons', () => {
  const raw = `--- !Missed
Pass: inline
Name: TooCostly
DebugLoc: { File: 'src/it''s,here.c', Line: 18, Column: 4 }
Function: main
Args:
  - Callee: helper
  - Cost: '98'
...
--- !Analysis
Pass: asm-printer
Name: InstructionCount
Function: helper
Args:
  - NumInstructions: '7'
...
`;
  assert.deepEqual(optimizationRemarks(raw), [
    { kind: 'Missed', pass: 'inline', name: 'TooCostly', function: 'main',
      location: { file: "src/it's,here.c", line: 18, column: 4 },
      args: [{ key: 'Callee', value: 'helper' }, { key: 'Cost', value: '98' }] },
    { kind: 'Analysis', pass: 'asm-printer', name: 'InstructionCount', function: 'helper',
      location: null, args: [{ key: 'NumInstructions', value: '7' }] },
  ]);
  assert.throws(() => optimizationRemarks('--- !Passed\nName: inline\n'), /Malformed/);
});

test('trace slices distinguish whole-module entries from selected functions', () => {
  const raw = `["clang"]
*** IR Dump After EarlyCSEPass on main ***
-  %x = add i32 0, 0
*** IR Dump After GlobalOptPass on [module] ***
*** IR for function [module] ***
+ @g = global i32 0`;
  assert.deepEqual(optimizationTrace(raw), [
    { pass: 'EarlyCSEPass', scope: 'main', start: 2, end: 3 },
    { pass: 'GlobalOptPass', scope: '[module]', start: 4, end: 6 },
  ]);
});

test('same-IR inventory separates encoded data and missing symbols from frame traffic', () => {
  const owned = `00000000 <main>:
       0: 6838          ldr r0, [r7]
       2: 4601          mov r1, r0
       4: 10000000      .word 0x10000000
00000000 <extra>:
       0: 4770          bx lr`;
  const llvm = `00000000 <main>:
       0: 6838          ldr r0, [r7]
       2: 4770          bx lr`;
  assert.deepEqual(fixedBackendComparison(owned, llvm), [
    { name: 'extra', owned: { bytes: 2, instructions: 1, literals: 0, frame_accesses: 0, copies: 0, calls: 0 },
      llvm: null },
    { name: 'main', owned: { bytes: 4, instructions: 2, literals: 4, frame_accesses: 1, copies: 1, calls: 0 },
      llvm: { bytes: 4, instructions: 2, literals: 0, frame_accesses: 0, copies: 0, calls: 0 } },
  ]);
});
