import assert from 'node:assert/strict';
import { disassemblyFunctions } from '../badge-compiler/tools/backend-attribution.mjs';
import { compareObjects, readObject } from '../badge-compiler/tools/assembler-crossover.mjs';

function scalar(text) {
  if (text.startsWith("'") && text.endsWith("'"))
    return text.slice(1, -1).replaceAll("''", "'");
  if (text.startsWith('"') && text.endsWith('"'))
    return text.slice(1, -1);
  return text;
}

export function optimizationRemarks(text) {
  const docs = [...text.matchAll(/^--- !([A-Za-z]+)\s*$/gm)];
  return docs.map((head, i) => {
    const document = text.slice(head.index + head[0].length, docs[i + 1]?.index);
    const field = key => scalar(document.match(new RegExp(`^${key}:\\s*(.+)$`, 'm'))?.[1].trim() ?? '');
    const pass = field('Pass'), name = field('Name'), fn = field('Function');
    assert(pass && name && fn, `Malformed LLVM remark ${i + 1}`);
    const loc = document.match(/^DebugLoc:\s*\{\s*File:\s*(.*),\s*Line:\s*(\d+),\s*Column:\s*(\d+)\s*\}/m);
    const args = [...document.matchAll(/^ {2}- ([\w-]+):[ \t]+(.+)$/gm)]
      .filter(match => !/^[>|]/.test(match[2]))
      .map(([, key, value]) => ({ key, value: scalar(value.trim()) }));
    return { kind: head[1], pass, name, function: fn,
      location: loc ? { file: scalar(loc[1]), line: Number(loc[2]), column: Number(loc[3]) } : null, args };
  });
}

export function optimizationTrace(text) {
  const lines = text.split('\n'), passes = [];
  for (let i = 0; i < lines.length; i++) {
    const header = lines[i].match(/^\*\*\* IR Dump After (.+?) on (.+) \*\*\*$/);
    if (!header) continue;
    if (passes.length) passes.at(-1).end = i;
    passes.push({ pass: header[1], scope: header[2], start: i + 1, end: lines.length });
  }
  return passes;
}

export function diagnosticObjectMatch(production, diagnostic) {
  const result = compareObjects(readObject(production), readObject(diagnostic));
  return result.allocated_equal && result.relocations_equal && result.public_symbols_equal;
}

export function fixedBackendComparison(owned, llvm) {
  const summarize = (text, bases) => new Map(disassemblyFunctions(text, bases).map(fn => [fn.name, {
    bytes: fn.executable_bytes, instructions: fn.instructions.length, literals: fn.literal_bytes,
    frame_accesses: fn.frame_loads + fn.frame_stores, copies: fn.copies, calls: fn.calls,
  }]));
  const a = summarize(owned, ['r7', 'sp']), b = summarize(llvm, ['sp']);
  return [...new Set([...a.keys(), ...b.keys()])].sort()
    .map(name => ({ name, owned: a.get(name) ?? null, llvm: b.get(name) ?? null }));
}
