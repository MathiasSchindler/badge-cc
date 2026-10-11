import assert from 'node:assert/strict';
import { irCounts } from '../badge-compiler/tools/pipeline-stages.mjs';

export const stages = [
  ['source', 'Source'], ['preprocessed', 'Preprocessing'], ['ast', 'Parser AST'],
  ['typed', 'Typed AST'], ['initial', 'Lowered IR'], ['optimized', 'Optimized IR'],
  ['assembly', 'Assembly'], ['object', 'Object code'],
];

function finishRegions(regions, lines) {
  for (let i = 0; i < regions.length; i++) {
    const region = regions[i];
    region.end ??= (regions[i + 1]?.start ?? lines.length + 1) - 1;
    region.blocks ??= [];
    for (let j = 0; j < region.blocks.length; j++)
      region.blocks[j].end = (region.blocks[j + 1]?.start ?? region.end + 1) - 1;
  }
  return regions;
}

export function irRegions(text) {
  const lines = text.split('\n'), regions = [];
  let current;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const definition = line.match(/^define\b.*?@(?:"([^"]+)"|([-\w.$]+))\(/);
    if (definition) {
      assert(!current, 'nested IR function');
      current = { name: definition[1] ?? definition[2], start: i + 1, blocks: [] };
      regions.push(current);
    } else if (current && line === '}') {
      current.end = i + 1;
      current.stats = irCounts(lines.slice(current.start - 1, current.end).join('\n')).operations;
      current = undefined;
    } else if (current) {
      const label = line.match(/^([-\w.$]+|"[^"]+"):\s*(?:;.*)?$/);
      if (label) current.blocks.push({ name: label[1], start: i + 1 });
    }
  }
  assert(!current, 'unterminated IR function');
  return finishRegions(regions, lines);
}

export function assemblyRegions(text) {
  const lines = text.split('\n'), regions = [];
  const names = new Set([...text.matchAll(/^\s*\.type\s+([^,\s]+)\s*,\s*[%@]function/gm)]
    .map(match => match[1]));
  let current;
  for (let i = 0; i < lines.length; i++) {
    const label = lines[i].match(/^([-\w.$]+):(?:\s*(?:[@;#].*)?)$/);
    if (label && names.has(label[1])) {
      current = { name: label[1], start: i + 1, blocks: [] };
      regions.push(current);
    } else if (current && label) {
      current.blocks.push({ name: label[1], start: i + 1 });
    }
    const size = lines[i].match(/^\s*\.size\s+([^,\s]+),/);
    if (current && size?.[1] === current.name) { current.end = i + 1; current = undefined; }
  }
  return finishRegions(regions, lines);
}

export function objectRegions(text) {
  const lines = text.split('\n'), regions = [];
  for (let i = 0; i < lines.length; i++) {
    const match = lines[i].match(/^[0-9a-f]+ <([^>]+)>:$/i);
    if (match && !/^[.$]/.test(match[1]))
      regions.push({ name: match[1], start: i + 1 });
  }
  return finishRegions(regions, lines);
}

const ownChildren = node => node.children ?? [];
const clangChildren = node => node.inner ?? [];
function identifier(node) {
  if (node.kind === 'name') return node.text;
  for (const child of ownChildren(node)) {
    const name = identifier(child);
    if (name) return name;
  }
}
function countNodes(node, children) {
  const counts = {};
  function visit(item) {
    counts[item.kind] = (counts[item.kind] ?? 0) + 1;
    for (const child of children(item)) visit(child);
  }
  visit(node);
  return counts;
}

export function astRegions(tree, role) {
  const own = role === 'owned', children = own ? ownChildren : clangChildren;
  const root = own ? tree.ast : tree, regions = [];
  const structural = own ? new Set(['block', 'if', 'for', 'while', 'do', 'switch']) :
    new Set(['CompoundStmt', 'IfStmt', 'ForStmt', 'WhileStmt', 'DoStmt', 'SwitchStmt']);
  function visit(node, nodePath) {
    const isFunction = own ? node.kind === 'function_definition' :
      node.kind === 'FunctionDecl' && children(node).some(n => n.kind === 'CompoundStmt');
    if (isFunction) {
      const name = own ? identifier(children(node).find(n => n.kind === 'init_declarator') ?? node) : node.name;
      assert(name, 'AST function has no discoverable name');
      const region = { name, path: nodePath, blocks: [], stats: countNodes(node, children),
        location: node.location ?? node.loc ?? null };
      function blocks(item, itemPath) {
        if (structural.has(item.kind))
          region.blocks.push({ name: `${item.kind} ${region.blocks.length + 1}`, path: itemPath });
        children(item).forEach((child, index) => blocks(child, [...itemPath, index]));
      }
      blocks(node, nodePath);
      regions.push(region);
      return;
    }
    children(node).forEach((child, index) => visit(child, [...nodePath, index]));
  }
  visit(root, []);
  return regions;
}

export function astNode(tree, role, nodePath) {
  let node = role === 'owned' ? tree.ast : tree;
  for (const index of nodePath) node = (role === 'owned' ? node.children : node.inner)[index];
  return node;
}

export function sourceRegions(source, clangTree, filename) {
  const regions = [];
  const bytes = Buffer.from(source);
  const lineAt = offset => bytes.subarray(0, offset).toString('utf8').split('\n').length;
  let currentFile = filename;
  for (const node of clangTree.inner ?? []) {
    currentFile = node.loc?.file ?? currentFile;
    if (node.kind !== 'FunctionDecl' || !node.inner?.some(n => n.kind === 'CompoundStmt')) continue;
    const file = node.loc?.file ?? node.range?.begin?.file ?? currentFile;
    if (!file || !filename.endsWith(file) && !file.endsWith(filename)) continue;
    const begin = node.range?.begin, end = node.range?.end;
    if (!Number.isInteger(begin?.offset) || !Number.isInteger(end?.offset)) continue;
    const start = begin.offset, stop = end.offset + end.tokLen;
    assert(start >= 0 && stop <= bytes.length && stop > start, 'source range outside file');
    const region = { name: node.name, start: lineAt(start), end: lineAt(stop),
      blocks: [], mapping: 'clang-source-range' };
    function visit(item) {
      if (['CompoundStmt', 'IfStmt', 'ForStmt', 'WhileStmt', 'DoStmt', 'SwitchStmt'].includes(item.kind)) {
        const begin = item.range?.begin?.offset, end = item.range?.end?.offset;
        if (Number.isInteger(begin) && Number.isInteger(end) && begin >= start && end < stop)
          region.blocks.push({ name: `${item.kind} ${region.blocks.length + 1}`,
            start: lineAt(begin), end: lineAt(end + item.range.end.tokLen) });
      }
      for (const child of item.inner ?? []) visit(child);
    }
    visit(node);
    regions.push(region);
  }
  return regions;
}

export function sections(text) {
  return [...text.matchAll(/^(\.\S+)\s+(\d+)\s+(\d+)\s*$/gm)]
    .map(match => ({ name: match[1], bytes: Number(match[2]), address: Number(match[3]) }));
}

export function symbols(text) {
  return [...text.matchAll(/^([0-9a-f]+)\s+([0-9a-f]+)\s+([A-Za-z])\s+(.+)$/gm)]
    .map(match => ({ address: parseInt(match[1], 16), bytes: parseInt(match[2], 16),
      type: match[3], name: match[4] }));
}

export function inventory(module) {
  const names = new Set();
  for (const role of ['owned', 'llvm'])
    for (const stage of Object.values(module.roles[role].stages))
      for (const region of stage.regions) names.add(region.name);
  return [...names].sort();
}
