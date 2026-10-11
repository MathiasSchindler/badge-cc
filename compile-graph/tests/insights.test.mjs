import assert from 'node:assert/strict';
import test from 'node:test';
import { projectIndex, preprocessingStats, expandedLines, tokens, tokenDifference,
  conditionalDirectives, astOutline } from '../web/insights.mjs';

test('project membership comes from actual linker inputs and includes shared units', () => {
  const module = name => ({ id: `${name}.o`, roles: Object.fromEntries(['owned', 'llvm'].map(role =>
    [role, { invocation: { output: `${role}/${name}.o` } }])) });
  const image = (name, role, inputs) => ({ name, role,
    invocation: { objects: inputs.map(input => `${role}/${input}.o`) } });
  const report = { modules: [module('square'), module('maze'), module('display')],
    images: [image('square', 'owned', ['square', 'display']), image('square', 'llvm', ['square', 'display']),
      image('maze', 'owned', ['maze', 'display']), image('maze', 'llvm', ['maze', 'display'])] };
  assert.deepEqual(projectIndex(report).map(p => [p.name, p.modules]), [
    ['square', ['square.o', 'display.o']], ['maze', ['maze.o', 'display.o']],
  ]);
  report.images[1].invocation.objects.pop();
  assert.throws(() => projectIndex(report), /membership differs/);
  report.images.pop();
  assert.throws(() => projectIndex(report), /membership differs|Missing/);
});

test('expanded view removes only line markers and blank lines and preserves physical line numbers', () => {
  const source = '# 1 "a.c"\n\nchar *s = "a  b";\n  #line 8 "a.c"\n#define KEEP 1\n';
  assert.deepEqual(expandedLines(source, 10), [
    { text: 'char *s = "a  b";', number: 12 }, { text: '#define KEEP 1', number: 14 },
  ]);
  assert.equal(preprocessingStats(source).line_markers, 2);
  assert.equal(preprocessingStats(source).blank_lines, 2);
  assert.equal(preprocessingStats('é').bytes, 2);
  assert.deepEqual(tokens('string\n"a  b"\n'), ['string', '"a  b"']);
});

test('ordered token comparison detects attributes rather than whitespace artifacts', () => {
  const a = ['static', 'void', 'run', '(', ')', ';'];
  const b = ['static', 'void', 'run', '(', ')', '__attribute__', '(', '(', 'noinline', ')', ')', ';'];
  const result = tokenDifference(a, b);
  assert.equal(result.status, 'different');
  assert.equal(result.removed, 0);
  assert.equal(result.added, 6);
  assert.deepEqual(result.hunks[0].added, ['__attribute__', '(', '(', 'noinline', ')', ')']);
  assert.equal(tokenDifference(a, a).status, 'identical');
  assert.equal(tokenDifference(['x'], ['y'], 1).status, 'too-large');
});

test('ordered hunks reconstruct target tokens including duplicates, moves, inserts and deletes', () => {
  let state = 13579;
  const random = () => { state = (Math.imul(state, 1664525) + 1013904223) >>> 0; return state; };
  for (let attempt = 0; attempt < 200; attempt++) {
    const a = Array.from({ length: random() % 20 }, () => String(random() % 5));
    const b = Array.from({ length: random() % 20 }, () => String(random() % 5));
    const result = tokenDifference(a, b);
    assert.notEqual(result.status, 'too-large');
    const rebuilt = [...a];
    for (const hunk of [...result.hunks].reverse())
      rebuilt.splice(hunk.owned_start, hunk.removed.length, ...hunk.added);
    assert.deepEqual(rebuilt, b);
  }
});

test('AST outlines remove serialization details while retaining operators and type evidence', () => {
  const tree = { types: [{ id: 9, kind: 'unsigned_int' }], ast: { kind: 'binary', text: '+',
    type: 9, category: 'rvalue', tokens: [4, 8], location: { line: 44 }, children: [
      { kind: 'identifier', text: 'counter', symbol: 88, children: [] },
    ] } };
  const outline = astOutline(tree, 'owned');
  assert.match(outline, /binary  \+ \| type=unsigned_int \| rvalue/);
  assert.match(outline, /  identifier  counter/);
  assert.doesNotMatch(outline, /tokens|location|88|44/);
  assert.equal(astOutline({ kind: 'BinaryOperator', opcode: '+', id: '0x123',
    type: { qualType: 'unsigned int' }, inner: [] }, 'llvm'),
  'BinaryOperator  + | unsigned int');
});

test('conditional boundaries retain source coordinates without guessing branch selection', () => {
  assert.deepEqual(conditionalDirectives('#ifdef __clang__\nint x;\n#endif\n#if X > 3\n#elif Y\n'), [
    { line: 1, text: '#ifdef __clang__' }, { line: 4, text: '#if X > 3' }, { line: 5, text: '#elif Y' },
  ]);
});
