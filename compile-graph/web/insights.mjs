export function projectIndex(report) {
  const names = [...new Set(report.images.map(image => image.name))];
  return names.map(name => {
    const images = Object.fromEntries(['owned', 'llvm'].map(role => {
      const image = report.images.find(item => item.name === name && item.role === role);
      if (!image) throw new Error(`Missing ${role} image for ${name}`);
      return [role, image];
    }));
    const members = role => report.modules.filter(module =>
      images[role].invocation.objects.includes(module.roles[role].invocation.output)).map(module => module.id);
    const owned = members('owned'), llvm = members('llvm');
    if (JSON.stringify([...owned].sort()) !== JSON.stringify([...llvm].sort()))
      throw new Error(`Compiler translation-unit membership differs for ${name}`);
    if (!owned.length) throw new Error(`No captured C modules for ${name}`);
    return { name, images, modules: owned };
  });
}

export function preprocessingStats(text) {
  const lines = text.split('\n');
  const markers = lines.filter(line => /^\s*#\s*(?:line\s+)?\d+\b/.test(line));
  return { bytes: new TextEncoder().encode(text).length, lines: lines.length,
    line_markers: markers.length, blank_lines: lines.filter(line => !line.trim()).length,
    marker_bytes: markers.reduce((sum, line) => sum + new TextEncoder().encode(line).length, 0) };
}

export function expandedLines(text, start = 1) {
  return text.split('\n').map((text, index) => ({ text, number: start + index }))
    .filter(line => line.text.trim() && !/^\s*#\s*(?:line\s+)?\d+\b/.test(line.text));
}

export function tokens(text) {
  return text.endsWith('\n') ? text.slice(0, -1).split('\n').filter(Boolean) :
    text.split('\n').filter(Boolean);
}

// Exact ordered LCS on the changed middle, bounded to keep reports responsive.
export function tokenDifference(owned, llvm, cellBudget = 2000000) {
  let prefix = 0, suffix = 0;
  while (prefix < owned.length && prefix < llvm.length && owned[prefix] === llvm[prefix]) prefix++;
  while (suffix < owned.length - prefix && suffix < llvm.length - prefix &&
    owned[owned.length - 1 - suffix] === llvm[llvm.length - 1 - suffix]) suffix++;
  const a = owned.slice(prefix, owned.length - suffix), b = llvm.slice(prefix, llvm.length - suffix);
  if (!a.length && !b.length)
    return { status: 'identical', owned_count: owned.length, llvm_count: llvm.length, hunks: [] };
  if ((a.length + 1) * (b.length + 1) > cellBudget)
    return { status: 'too-large', owned_count: owned.length, llvm_count: llvm.length,
      common_prefix: prefix, common_suffix: suffix, hunks: [] };
  const stride = b.length + 1, grid = new Uint32Array((a.length + 1) * stride);
  for (let i = a.length - 1; i >= 0; i--)
    for (let j = b.length - 1; j >= 0; j--)
      grid[i * stride + j] = a[i] === b[j] ? 1 + grid[(i + 1) * stride + j + 1] :
        Math.max(grid[(i + 1) * stride + j], grid[i * stride + j + 1]);
  const hunks = [];
  let i = 0, j = 0, current;
  while (i < a.length || j < b.length) {
    if (i < a.length && j < b.length && a[i] === b[j]) { current = undefined; i++; j++; continue; }
    if (!current) {
      current = { owned_start: prefix + i, llvm_start: prefix + j, removed: [], added: [],
        before: owned.slice(Math.max(0, prefix + i - 8), prefix + i) };
      hunks.push(current);
    }
    if (i < a.length && (j === b.length || grid[(i + 1) * stride + j] >= grid[i * stride + j + 1]))
      current.removed.push(a[i++]);
    else current.added.push(b[j++]);
  }
  for (const hunk of hunks)
    hunk.after = owned.slice(hunk.owned_start + hunk.removed.length,
      hunk.owned_start + hunk.removed.length + 8);
  return { status: 'different', owned_count: owned.length, llvm_count: llvm.length,
    removed: hunks.reduce((sum, hunk) => sum + hunk.removed.length, 0),
    added: hunks.reduce((sum, hunk) => sum + hunk.added.length, 0), hunks };
}

export function conditionalDirectives(source) {
  return source.split('\n').flatMap((line, index) => /^\s*#\s*(?:if|ifdef|ifndef|elif)\b/.test(line) ?
    [{ line: index + 1, text: line.trim() }] : []);
}

export function astOutline(tree, role, root) {
  const lines = [];
  const types = new Map((tree.types ?? []).map(type => [type.id, type]));
  function visit(item, depth) {
    const description = [item.name ?? item.text, item.opcode, item.value,
      role === 'owned' ? (types.get(item.type)?.kind ?
        `type=${types.get(item.type).kind}` : undefined) : item.type?.qualType,
      item.category, item.castKind].filter(value => value !== undefined && value !== '');
    lines.push(`${'  '.repeat(depth)}${item.kind}${description.length ? `  ${description.join(' | ')}` : ''}`);
    for (const child of (role === 'owned' ? item.children : item.inner) ?? []) visit(child, depth + 1);
  }
  visit(root ?? (role === 'owned' ? tree.ast : tree), 0);
  return lines.join('\n');
}
