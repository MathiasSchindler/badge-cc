import test from 'node:test';
import assert from 'node:assert/strict';
import { comparisonChanges } from '../tools/compare-branch-evidence.mjs';

const fn = operations => ({ name: 'f', frame_bytes: 16, saved_bytes: 16,
  registers: { '%x': 8 }, entry: ['push {r4, r7, r8, lr}'], operations });
test('traffic evidence counts actual removed compare/branch instructions without merging PHI work', () => {
  const compare = '%c = icmp slt i32 %x, 0', branch = 'br i1 %c, label %a, label %b';
  const before = fn([
    { ir: compare, line: 4, assembly: ['cmp r0, r1', 'it lt', 'movlt r0, #1'] },
    { ir: branch, line: 5, assembly: ['cmp r0, #0', 'beq.w .Lcg_edge1', 'mov r8, r9'] },
  ]);
  const after = fn([
    { ir: compare, line: 4, assembly: [] },
    { ir: branch, line: 5, assembly: ['cmp r0, r1', 'bge.w .Lcg_edge1', 'mov r8, r9'] },
  ]);
  const result = comparisonChanges([before], [after])[0];
  assert.equal(result.pairs[0].removed, 3);
  assert.equal(result.before_instructions - result.after_instructions, 3);
  const wrong = structuredClone(after);
  wrong.registers['%x'] = 9;
  assert.throws(() => comparisonChanges([before], [wrong]));
  wrong.registers['%x'] = 8;
  wrong.operations[1].assembly.push('cmp r0, #0');
  assert.throws(() => comparisonChanges([before], [wrong]));
});
