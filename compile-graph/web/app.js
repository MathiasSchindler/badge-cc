import { projectIndex, expandedLines, astOutline } from './insights.mjs';

const $ = id => document.getElementById(id);
const roles = ['owned', 'llvm'], cache = new Map();
let report, projects, project, module, stage = 'source', selected = '', revision = 0;
const blockSelection = { owned: '', llvm: '' }, expanded = { owned: false, llvm: false };
const number = value => value.toLocaleString();
const signed = value => `${value > 0 ? '+' : ''}${number(value)}`;

function node(tag, text, className) {
  const item = document.createElement(tag);
  if (text !== undefined) item.textContent = text;
  if (className) item.className = className;
  return item;
}
function error(message) {
  $('error').hidden = false;
  $('error').textContent = `${message}\nServe this report directory with a static HTTP server if opening file:// fails.`;
}
async function load(file) {
  if (!cache.has(file)) cache.set(file, fetch(file).then(response => {
    if (!response.ok) throw new Error(`Cannot load ${file}: HTTP ${response.status}`);
    return response.text();
  }));
  return cache.get(file);
}
function option(select, value, label) {
  const item = node('option', label);
  item.value = value;
  select.append(item);
}
function symbolBytes(role, name) {
  const found = module.roles[role].symbols.filter(s => s.name === name && /[tTwW]/.test(s.type));
  return found.length ? found.reduce((sum, s) => sum + s.bytes, 0) : null;
}
function effectiveStage(role) {
  const index = Number($('pass').value);
  return role === 'owned' && stage === 'optimized' && index >= 0 ?
    module.roles.owned.passes[index] : module.roles[role].stages[stage];
}
function regionFor(item) {
  return selected ? item.regions.find(r => r.name === selected) : null;
}
function resetRegions() {
  for (const role of roles) { blockSelection[role] = ''; expanded[role] = false; }
}
function imageCards() {
  const container = $('images');
  container.replaceChildren();
  for (const name of [project.name]) {
    const images = roles.map(role => report.images.find(i => i.name === name && i.role === role));
    if (images.some(i => !i)) throw new Error(`Missing image peer: ${name}`);
    const card = node('div', undefined, 'image-card');
    card.append(node('h3', name));
    const table = node('table'), head = node('tr');
    for (const title of ['Metric', 'Owned', 'LLVM', 'Delta']) head.append(node('th', title));
    table.append(head);
    for (const [title, key] of [['Loaded bytes', 'loaded_bytes'], ['Static RAM', 'ram_bytes'],
      ['Extent gaps', 'extent_gaps']]) {
      const row = node('tr');
      row.append(node('td', title), ...images.map(i => node('td', number(i[key]))),
        node('td', signed(images[0][key] - images[1][key]), 'delta'));
      table.append(row);
    }

    card.append(table, node('p', 'Loaded extent includes code, data, constants and layout gaps. RAM excludes dynamic stack.', 'hint'));
    if (report.assembler) {
      const controls = report.assembler.images.filter(image => image.name === name);
      const detail = node('details');
      detail.append(node('summary', 'BA01: identical-assembly crossover'));
      detail.append(node('p', 'Each compiler-generated assembly input is assembled by both assemblers, then linked with a common linker. This isolates assemblers, not compilers.', 'hint'));
      detail.append(insightTable(['Assembly producer', 'LLD pair', 'Owned linker pair'], controls.map(control => [
        control.source_role,
        `${control.linkers.lld.equal ? 'identical' : 'DIFFERENT'}: ${number(control.linkers.lld.routes[0].bytes)} B`,
        `${control.linkers.owned.equal ? 'identical' : 'DIFFERENT'}: ${number(control.linkers.owned.routes[0].bytes)} B`,
      ])));
      detail.append(node('p', 'ELF symbol tables and relocation representations can differ without changing linked firmware. Full evidence is retained in report.json.', 'hint'));
      for (const control of controls)
        detail.append(node('p', `${control.source_role} assembly: LLD versus owned linker bytes are ${control.linkers_equal ? 'identical' : 'different (separate linker observation; not an assembler mismatch)'}.`, 'hint'));
      card.append(detail);
    }
    const detail = node('details'), sectionTable = node('table');
    detail.append(node('summary', 'Linked sections'));
    const sectionHead = node('tr');
    for (const title of ['Section', 'Owned', 'LLVM']) sectionHead.append(node('th', title));
    sectionTable.append(sectionHead);
    for (const sectionName of [...new Set(images.flatMap(image => image.sections.map(s => s.name)))]) {
      const row = node('tr');
      row.append(node('td', sectionName), ...images.map(image =>
        node('td', number(image.sections.find(s => s.name === sectionName)?.bytes ?? 0))));
      sectionTable.append(row);
    }
    detail.append(sectionTable, node('p',
      'Section names describe linker layout, not semantic categories. A text section may also contain constants and alignment.', 'hint'));
    card.append(detail);
    const links = node('div');
    for (const image of images) {
      const link = node('a', `${image.role}: linked assembly`);
      link.href = image.disassembly.file;
      links.append(link, node('span', '  '));
    }
    card.append(links);
    container.append(card);
  }
}

function insightTable(headers, rows) {
      const table = node('table'), head = node('tr');
      for (const title of headers) head.append(node('th', title));
      table.append(head);
      for (const values of rows) {
        const row = node('tr');
        for (const value of values) row.append(node('td', String(value)));
        table.append(row);
      }
      return table;
    }
    function insights() {
      const container = $('insights');
      container.replaceChildren();
      container.hidden = false;
      if (stage === 'preprocessed') {
        container.append(node('h3', 'Formatting noise or different program input?'));
        container.append(node('p', 'This analysis covers the entire translation unit, including declarations outside function definitions.'));
        const comparison = module.preprocessing_comparison;
        if (!comparison) {
          container.append(node('p', 'This older capture has no token analysis. Capture a new report to enable these insights.'));
          return;
        }
        const summary = roles.map(role => module.roles[role].stages.preprocessed.presentation);
        container.append(insightTable(['Presentation', 'Owned', 'LLVM'], [
          ['Artifact bytes', ...summary.map(s => number(s.bytes))],
          ['Line-marker directives', ...summary.map(s => number(s.line_markers))],
          ['Blank lines', ...summary.map(s => number(s.blank_lines))],
          ['C tokens', number(comparison.owned_count), number(comparison.llvm_count)],
        ]));
        if (comparison.status === 'identical') {
          container.append(node('p', 'The ordered C token sequences are identical after removing line markers and whitespace. The visible preprocessing differences are presentation, not token changes.'));
        } else if (comparison.status === 'too-large') {
          container.append(node('p', 'The token sequences differ, but exact alignment exceeds the analysis budget. No approximate equality or edit counts are claimed; inspect the raw token artifacts.'));
        } else {
          container.append(node('p', `Real token differences: ${comparison.removed} owned-only and ${comparison.added} LLVM-only tokens in ${comparison.hunks.length} ordered change groups. These are input differences, not proof of behavior differences.`));
          const detail = node('details');
          detail.open = true;
          detail.append(node('summary', 'Ordered token changes (whitespace and markers ignored)'));
          for (const hunk of comparison.hunks.slice(0, 30)) {
            const change = node('div', undefined, 'token-change');
            change.append(node('p', `Owned token ${hunk.owned_start + 1} / LLVM token ${hunk.llvm_start + 1}`));
            change.append(node('code', hunk.before.join(' '), 'token-context'));
            change.append(node('p', `Owned: ${hunk.removed.join(' ') || '(nothing)'}`),
              node('p', `LLVM: ${hunk.added.join(' ') || '(nothing)'}`));
            change.append(node('code', hunk.after.join(' '), 'token-context'));
            detail.append(change);
          }
          if (comparison.hunks.length > 30)
            detail.append(node('p', `${comparison.hunks.length - 30} additional groups are retained in report.json.`));
          container.append(detail);
        }
        const links = node('p');
        for (const role of roles) {
          const link = node('a', `${role} token stream`);
          link.href = module.roles[role].stages.preprocessed.token_artifact;
          links.append(link, node('span', '  '));
        }
        container.append(links);
        const conditional = node('details');
        conditional.append(node('summary', 'Source conditional-compilation boundaries'));
        conditional.append(node('p', 'These directives can explain input differences. Their presence alone does not establish which branch each compiler selected.'));
        conditional.append(node('pre', module.source_conditionals.map(item => `${item.line}: ${item.text}`).join('\n') ||
          'No conditional-compilation boundaries in this source file.'));
        container.append(conditional);
      } else if (stage === 'ast' || stage === 'typed') {
        container.append(node('h3', 'Compare structure, not JSON serialization'));
        container.append(node('p', 'The outline hides node IDs, token offsets and location metadata, retaining node kinds, names, operators and available type/category information. Each compiler still uses its own AST vocabulary; extra nodes do not imply worse optimization.'));
        container.append(node('p', 'Clang performs parsing and semantic analysis together. Its Parser AST and Typed AST views share an artifact; the owned compiler has separate artifacts.'));
      } else if (stage === 'initial' || stage === 'optimized') {
        const items = roles.map(effectiveStage);
        const regions = items.map(item => selected ? item.regions.filter(region => region.name === selected) : item.regions);
        const counts = regions.map(list => list.reduce((result, region) => {
          for (const [op, count] of Object.entries(region.stats ?? {})) result[op] = (result[op] ?? 0) + count;
          return result;
        }, {}));
        const operations = [...new Set(counts.flatMap(count => Object.keys(count)))];
        container.append(node('h3', 'IR operation inventory'));
        container.append(node('p', selected ? `Scope: ${selected}. Missing functions have no inventory, not a zero-cost equivalent.` :
          'Scope: all discovered functions in this translation unit.'));
        container.append(insightTable(['Operation', 'Owned', 'LLVM'], operations.sort((a, b) =>
          Math.abs((counts[0][b] ?? 0) - (counts[1][b] ?? 0)) -
          Math.abs((counts[0][a] ?? 0) - (counts[1][a] ?? 0)) || a.localeCompare(b)).map(op =>
          [op, regions[0].length ? counts[0][op] ?? 0 : 'absent', regions[1].length ? counts[1][op] ?? 0 : 'absent'])));
        container.append(node('p', 'Counts reveal representation differences such as allocas, loads/stores, PHIs and branches. They do not score optimization quality or predict runtime. Owned pass snapshots and final LLVM IR are different pipeline boundaries.'));
      } else {
        const controls = report.assembler?.modules.filter(item => item.module === module.id);
        if (stage === 'object' && controls) {
          container.append(node('h3', 'Same assembly, two assemblers'));
          container.append(insightTable(['Assembly producer', 'Allocated encoding', 'Relocations', 'ELF bytes owned / LLVM'],
            controls.map(control => [control.source_role,
              control.comparison.allocated_equal ? 'identical after relocation masking' : 'different',
              control.comparison.relocations_equal ? 'equivalent targets/addends' : 'different',
              `${control.comparison.metadata.owned.elf_bytes} / ${control.comparison.metadata.llvm.elf_bytes}`])));
          container.append(node('p', 'Relocation immediate fields are compared through decoded target/addend contracts; all other allocated bytes and section layout are compared exactly. Local bookkeeping symbols are not public ABI differences.'));
          for (const control of controls.filter(control => !control.comparison.symbols_equal)) {
            container.append(node('p', `${control.source_role} assembly: local symbol metadata differs. These differences are retained, not treated as byte identity.`));
            container.append(node('pre', JSON.stringify(control.comparison.symbol_differences, null, 2)));
          }
        } else container.hidden = true;
      }
    }
function functions() {
  $('functions').replaceChildren();
  const filter = $('search').value.toLowerCase();
  const items = module.functions.map(name => {
    const sizes = roles.map(role => symbolBytes(role, name));
    return { name, sizes, delta: (sizes[0] ?? 0) - (sizes[1] ?? 0) };
  }).sort((a, b) => b.delta - a.delta || a.name.localeCompare(b.name));
  let count = 0;
  for (const item of items) {
    const caption = `${item.name} ${signed(item.delta)}`;
    if (!caption.toLowerCase().includes(filter)) continue;
    count++;
    const button = node('button', item.name, `function${selected === item.name ? ' active' : ''}`);
    button.append(node('small', `${item.sizes.map(n => n === null ? 'absent' : `${number(n)} B`).join(' / ')}  (${signed(item.delta)} B)`));
    button.addEventListener('click', () => { selected = item.name; resetRegions(); functions(); refresh(); });
    $('functions').append(button);
  }
  $('function-count').textContent = `${count}/${items.length}`;
}
function pipeline() {
  $('pipeline').replaceChildren();
  for (const [key, title] of report.stages) {
    const button = node('button', undefined, `stage${stage === key ? ' active' : ''}`);
    button.append(node('strong', title));
    const labels = roles.map(role => {
      const item = module.roles[role].stages[key], region = regionFor(item);
      if (selected && !region) return 'absent';
      if (region?.stats) return `${number(Object.values(region.stats).reduce((a, b) => a + b, 0))} nodes/ops`;
      if (key === 'object' && selected) return `${symbolBytes(role, selected) ?? '?'} B`;
      return selected && region?.start ? `${region.end - region.start + 1} lines` :
        `${number(item.regions.length)} functions`;
    });
    button.append(node('small', labels.join(' / ')));
    button.addEventListener('click', () => { stage = key; $('pass').value = '-1'; resetRegions(); refresh(); });
    $('pipeline').append(button);
  }
}
function astSelection(tree, role, path) {
  let item = role === 'owned' ? tree.ast : tree;
  for (const index of path) item = (role === 'owned' ? item.children : item.inner)[index];
  return item;
}
async function panel(role) {
  const item = effectiveStage(role), region = regionFor(item);
  const select = $(`${role}-block`);
  select.replaceChildren();
  option(select, '', selected ? 'Entire function' : 'Entire artifact');
  for (const [index, block] of (region?.blocks ?? []).entries()) option(select, String(index), block.name);
  select.value = blockSelection[role];
  select.disabled = !region?.blocks?.length;
  $(`${role}-download`).href = item.file;
  $(`${role}-info`).textContent = `${number(item.bytes)} artifact bytes | ${number(item.lines)} lines${item.note ? ` | ${item.note}` : ''}`;
  $(`${role}-stats`).textContent = region?.stats ?
    Object.entries(region.stats).sort((a, b) => b[1] - a[1]).slice(0, 12)
      .map(([key, value]) => `${key}: ${value}`).join(' | ') : '';
  if (selected && !region)
    return { text: `${selected} is absent from this artifact's discovered function inventory.\n` +
      'This does not by itself establish whether it was inlined, eliminated or renamed.', start: 1 };
  const text = await load(item.file);
  const block = blockSelection[role] === '' ? null : region.blocks[Number(blockSelection[role])];
  if (item.format === 'ast') {
    const tree = JSON.parse(text);
    const subtree = region ? astSelection(tree, role, block?.path ?? region.path) : null;
    return { text: $('ast-outline').checked ? astOutline(tree, role, subtree) :
      JSON.stringify(subtree ?? tree, null, 2),
      start: 1 };
  }
  const range = block ?? region;
  const view = { text: range ? text.split('\n').slice(range.start - 1, range.end).join('\n') : text,
    start: range?.start ?? 1 };
  if (stage === 'preprocessed' && $('expanded-c').checked) {
    const lines = expandedLines(view.text, view.start);
    return { ...view, text: lines.map(line => line.text).join('\n'),
      lineNumbers: lines.map(line => line.number) };
  }
  return view;
}
function paint(role, view, peer) {
  const lines = view.text.split('\n'), peerLines = new Set(peer.text.split('\n').map(line => line.trim()));
  const pre = $(`${role}-code`);
  pre.replaceChildren();
  const limit = expanded[role] ? lines.length : Math.min(lines.length, 400);
  for (let i = 0; i < limit; i++) {
    const unique = $('differences').checked && lines[i].trim() && !peerLines.has(lines[i].trim());
    const line = node('span', undefined, `code-line${unique ? ` unique-${role}` : ''}`);
    line.append(node('span', String(view.lineNumbers?.[i] ?? view.start + i), 'line-number'),
      document.createTextNode(lines[i]));
    pre.append(line);
  }
  $(`${role}-more`).hidden = limit === lines.length;
  $(`${role}-more`).textContent = `Show all ${number(lines.length)} lines (${number(lines.length - limit)} hidden)`;
}
async function listingView(ticket) {
  const container = $('listing');
  container.replaceChildren();
  container.hidden = !['assembly', 'object'].includes(stage);
  if (container.hidden) return;
  const capturedModule = module, capturedProject = project.name, capturedFunction = selected;
  const rows = report.assembler?.modules.filter(row => row.module === capturedModule.id && row.listing);
  if (!rows?.length) {
    container.append(node('p', 'This capture has no BA02 listings; capture a new report.'));
    return;
  }
  container.append(node('h3', 'BA02: final bytes, layout and emission provenance'));
  container.append(node('p', 'Both assembly producers are listed by the owned assembler; BA01 controls verify LLVM equivalence. IR markers describe emission context after optimization, not C-source or semantic equivalence. Merged and discarded contributions remain unmapped.', 'hint'));
  for (const row of rows) {
    const listing = JSON.parse(await load(row.listing));
    const control = report.assembler.images.find(i => i.name === capturedProject && i.source_role === row.source_role);
    const route = control?.linkers[control.production_linker].routes.find(r => r.assembler === 'owned');
    const linkedFile = route?.listings.find(file => file.endsWith(`-${capturedModule.id}.listing.json`));
    const linked = linkedFile ? JSON.parse(await load(linkedFile)) : null;
    if (ticket !== revision) return;
    const detail = node('details'); detail.open = true;
    detail.append(node('summary', `${row.source_role} assembly: ${Object.entries(row.listing_bytes).map(([k, v]) => `${k} ${number(v)} B`).join(' | ')}`));
    const link = node('a', 'Raw structured listing'); link.href = row.listing; detail.append(link);
    const toolbar = node('div', undefined, 'toolbar'), section = node('select'), search = node('input');
    section.setAttribute('aria-label', `${row.source_role} listing section`);
    option(section, '', 'All sections');
    for (const name of [...new Set(listing.ranges.map(r => r.section))]) option(section, name, name);
    search.type = 'search'; search.placeholder = 'Line, address, kind or operation';
    search.setAttribute('aria-label', `${row.source_role} listing search`);
    toolbar.append(section, search); detail.append(toolbar);
    const tableHost = node('div', undefined, 'listing-table'), more = node('button', 'Show all matching ranges');
    detail.append(tableHost, more);
    let all = false;
    const paintTable = () => {
      const region = capturedFunction ? capturedModule.roles[row.source_role].stages.assembly.regions
        .find(r => r.name === capturedFunction) : null;
      const ranges = (linked?.ranges ?? listing.ranges).filter(r =>
        (!capturedFunction || region && r.line >= region.start && r.line <= region.end) &&
        (!section.value || r.section === section.value) &&
        `${r.line} ${r.address == null ? '' : `0x${r.address.toString(16)}`} ${r.section} 0x${r.offset.toString(16)} ${r.kind} ${r.operation} ${r.decision}`
          .toLowerCase().includes(search.value.toLowerCase()));
      more.hidden = all || ranges.length <= 200;
      tableHost.replaceChildren(node('p', `${ranges.length} matching ranges`));
      const table = insightTable(['Section + offset', 'VMA / LMA', 'Assembly line', 'Bytes', 'Kind / decision'], []);
      for (const r of ranges.slice(0, all ? ranges.length : 200)) {
        const tr = node('tr'), line = node('td'), jump = node('button', String(r.line || 'generated'));
        jump.disabled = !r.line;
        jump.addEventListener('click', () => {
          stage = 'assembly'; selected = ''; resetRegions(); functions();
          expanded[row.source_role] = true;
          refresh().then(() => $(`${row.source_role}-code`).children[r.line - 1]?.scrollIntoView({ block: 'center' }));
        });
        line.append(jump);
        const decision = listing.decisions.find(d => d.line === r.line && d.offset === r.offset);
        tr.append(node('td', `${r.section} + 0x${r.offset.toString(16)}`),
          node('td', r.address == null ? r.placement ?? 'unmapped' :
            `0x${r.address.toString(16)} / 0x${r.load_address.toString(16)}`),
          line, node('td', r.hex === null ? `${r.size} B (NOBITS)` :
            `${r.size} B: ${r.hex.slice(0, 64)}${r.hex.length > 64 ? '...' : ''}`),
          node('td', `${r.kind}: ${r.decision}${decision?.pc_base != null ?
            `; PC=0x${decision.pc_base.toString(16)}, displacement=${decision.displacement}, reach=${decision.reach_min}..${decision.reach_max}` : ''}`));
        if (r.relocations.length) tr.lastChild.append(node('p',
          r.relocations.map(rel => `REL ${rel.type} @ +${rel.offset}: ${rel.symbol}`).join('; ')));
        table.append(tr);
      }
      tableHost.append(table);
    };
    section.addEventListener('change', () => { all = false; paintTable(); });
    search.addEventListener('input', () => { all = false; paintTable(); });
    more.addEventListener('click', () => { all = true; paintTable(); });
    paintTable();
    const alignments = node('details');
    alignments.append(node('summary', 'Alignment decisions (including zero-byte and max-skip cases)'));
    alignments.append(insightTable(['Section + offset', 'Assembly line', 'Alignment', 'Required padding', 'Result'],
      listing.decisions.filter(d => d.alignment).slice(0, 200).map(d => [
        `${d.section} + 0x${d.offset.toString(16)}`, d.line, d.alignment, d.required_padding,
        d.max_skip_exceeded ? 'Skipped: maximum padding exceeded' : 'Applied',
      ])));
    detail.append(alignments);
    if (linked?.emission_ranges) {
      const emission = node('details');
      emission.append(node('summary', 'Owned optimized-IR emission context (section byte controls)'));
      const ir = (await load(row.emission.ir)).split('\n');
      if (ticket !== revision) return;
      const traceLink = node('a', 'Full IR emission listing'); traceLink.href = row.emission.listing;
      emission.append(traceLink, node('p', 'Only sections with identical production bytes, canonical relocations and public symbols are mapped. The existing trace can inhibit peepholes; differing sections are rejected. Markers describe producer emission context, not precise per-instruction C mappings.', 'hint'));
      const irRegion = capturedFunction ? capturedModule.roles.owned.stages.optimized.regions
        .find(r => r.name === capturedFunction) : null;
      const mapped = linked.emission_ranges.filter(r => r.origin_line && r.kind === 'instruction' &&
        (!capturedFunction || irRegion && r.origin_line >= irRegion.start && r.origin_line <= irRegion.end));
      emission.append(node('p', `${mapped.length} instruction ranges have an IR emission context. Unmarked prologues, generated data and other code stay unmapped.`));
      const table = insightTable(['Section + offset', 'VMA', 'IR line', 'Optimized IR'], []);
      for (const r of mapped.slice(0, 100)) {
        const tr = node('tr'), line = node('td'), jump = node('button', String(r.origin_line));
        jump.addEventListener('click', () => {
          stage = 'optimized'; $('pass').value = '-1'; selected = ''; resetRegions(); functions();
          expanded.owned = true;
          refresh().then(() => $('owned-code').children[r.origin_line - 1]?.scrollIntoView({ block: 'center' }));
        });
        line.append(jump);
        tr.append(node('td', `${r.section} + 0x${r.offset.toString(16)}`),
          node('td', r.address == null ? 'unmapped' : `0x${r.address.toString(16)}`), line,
          node('td', ir[r.origin_line - 1]));
        table.append(tr);
      }
      emission.append(table);
      detail.append(emission);
    } else detail.append(node('p', row.emission_unavailable ?? 'IR mapping unavailable.', 'hint'));
    container.append(detail);
  }
}
async function refresh() {
  const ticket = ++revision;
  $('selection').textContent = selected || 'Whole translation unit';
  pipeline();
  insights();
  $('expanded-control').hidden = stage !== 'preprocessed';
  $('outline-control').hidden = stage !== 'ast' && stage !== 'typed';
  $('pass').disabled = stage !== 'optimized';
  const prefix = stage === 'optimized' && Number($('pass').value) >= 0 ?
    'Owned pass-prefix replay versus final LLVM optimized IR; these are not equivalent pass boundaries. ' : '';
  $('comparison-note').textContent = prefix +
    'Functions match by name. Regions are independent; highlighting marks line text absent from the other panel, not semantic differences.';
  try {
    const views = await Promise.all(roles.map(panel));
    if (ticket !== revision) return;
    roles.forEach((role, index) => paint(role, views[index], views[1 - index]));
    await listingView(ticket);
  } catch (failure) { if (ticket === revision) error(failure.message); }
}
function selectModule() {
  module = report.modules[Number($('module').value)];
  selected = ''; $('search').value = ''; resetRegions();
  $('pass').replaceChildren(); option($('pass'), '-1', 'Production optimized IR');
  module.roles.owned.passes.forEach((pass, index) =>
    option($('pass'), String(index), `${index + 1}. through ${pass.name}`));
  functions(); refresh();
}
function selectProject() {
  project = projects[Number($('project').value)];
  imageCards();
  $('module').replaceChildren();
  for (const [index, item] of report.modules.entries())
    if (project.modules.includes(item.id)) option($('module'), String(index), item.source);
  const primary = report.modules.findIndex(item => project.modules.includes(item.id) &&
    item.id === `${project.name}.o`);
  if (primary >= 0) $('module').value = String(primary);
  selectModule();
}
async function start() {
  report = JSON.parse(await load('report.json'));
  if (report.schema !== 1 || report.status !== 'complete') throw new Error('Report is incomplete or unsupported.');
  $('capture-date').textContent = new Date(report.created).toLocaleString();
  projects = projectIndex(report);
  for (const [index, item] of projects.entries()) option($('project'), String(index), item.name);
  for (const note of report.limitations) $('limitations').append(node('li', note));
  $('configuration').textContent = JSON.stringify({ llvm: report.llvm_version,
    owned_policy: report.policy, provenance: report.provenance }, null, 2);
  $('module').addEventListener('change', selectModule);
  $('project').addEventListener('change', selectProject);
  $('search').addEventListener('input', functions);
  $('clear').addEventListener('click', () => { selected = ''; resetRegions(); functions(); refresh(); });
  $('pass').addEventListener('change', () => { resetRegions(); refresh(); });
  $('differences').addEventListener('change', refresh);
  $('expanded-c').addEventListener('change', () => { resetRegions(); refresh(); });
  $('ast-outline').addEventListener('change', () => { resetRegions(); refresh(); });
  for (const role of roles) {
    $(`${role}-block`).addEventListener('change', () => {
      blockSelection[role] = $(`${role}-block`).value; expanded[role] = false; refresh();
    });
    $(`${role}-more`).addEventListener('click', () => { expanded[role] = true; refresh(); });
  }
  selectProject();
}
start().catch(failure => error(failure.message));
