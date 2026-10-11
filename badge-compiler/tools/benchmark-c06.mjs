import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { performance } from 'node:perf_hooks';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const tools = path.join(root, 'badge-compiler/build');
const owned = path.join(tools, 'owned');
const reports = path.join(tools, 'c06-metrics');
const maxFile = 64 * 1024 * 1024;
function requireCondition(condition, message) {
  if (!condition) throw new Error(message);
}
function read(file) {
  const stat = fs.statSync(file);
  requireCondition(stat.isFile() && stat.size <= maxFile, `Invalid/oversized file: ${file}`);
  return fs.readFileSync(file);
}
function hash(file) {
  return crypto.createHash('sha256').update(read(file)).digest('hex');
}
function command(executable, args, options = {}) {
  const result = spawnSync(executable, args, {
    cwd: root, encoding: 'utf8', timeout: 15 * 60 * 1000, maxBuffer: maxFile, ...options,
  });
  if (result.error || result.status !== 0) {
    throw new Error(`${executable} ${args.join(' ')} failed: ${result.error?.message ??
      `exit ${result.status}, signal ${result.signal}`}\n${result.stderr ?? ''}`);
  }
  return result.stdout;
}
function writeJson(file, value) {
  const temporary = `${file}.tmp`;
  fs.writeFileSync(temporary, `${JSON.stringify(value, null, 2)}\n`, { flag: 'wx' });
  fs.renameSync(temporary, file);
}
export function decodeName(token) {
  const name = token.replace(/^[@%]/, '');
  return name.startsWith('"')
    ? name.slice(1, -1).replace(/\\([0-9a-f]{2})/gi, (_, hex) =>
      String.fromCharCode(Number.parseInt(hex, 16))) : name;
}
export function parseAssembly(text, module) {
  const functions = new Map(), globals = new Set();
  let constants = Array(13).fill(null);
  const trackConstants = line => {
    let m;
    if (/\]!|\],/.test(line)) constants.fill(null);
    else if ((m = line.match(/^movw r(\d+), #(\d+)$/)) && Number(m[1]) <= 12)
      constants[Number(m[1])] = Number(m[2]);
    else if ((m = line.match(/^movt r(\d+), #(\d+)$/)) && Number(m[1]) <= 12) {
      const reg = Number(m[1]);
      constants[reg] = constants[reg] === null ? null :
        (constants[reg] & 65535) + Number(m[2]) * 65536;
    } else if ((m = line.match(/^mov r(\d+), r(\d+)$/)) &&
      Number(m[1]) <= 12 && Number(m[2]) <= 12)
      constants[Number(m[1])] = constants[Number(m[2])];
    else if ((m = line.match(/^(addw|subw) r(\d+), r(\d+), #(\d+)$/)) &&
      Number(m[2]) <= 12 && Number(m[3]) <= 12) {
      const value = constants[Number(m[3])];
      constants[Number(m[2])] = value === null ? null :
        (value + (m[1] === 'addw' ? 1 : -1) * Number(m[4])) >>> 0;
    } else if (!line || /^@/.test(line) || line === 'nop' ||
      /^(?:str(?:[bh])?(?:\.[nw])?|cmp(?:\.w)?)\s/.test(line)) return;
    else if ((m = line.match(/^(?:mov(?:\.w)?|movw|movt|add\.w|sub\.w|subs\.w|mul|mla|mls|udiv|sdiv|and\.w|orr\.w|eor\.w|lsl\.w|lsr\.w|asr\.w|rsb\.w|ubfx|sxtb|sxth|uxtb\.n|uxth\.n|ldr(?:[bh])?(?:\.[nw])?) r(\d+),/)) &&
      Number(m[1]) <= 12) constants[Number(m[1])] = null;
    else constants.fill(null);
  };
  let frame = null, fn = null, immediate = null, active = 0, prologue = false, returning = false;
  let pushed = false, based = false, restored = false, popped = false, savedBytes = 0;
  let lightweight = null;
  const registerMask = list => list.split(/,\s*/).reduce((mask, reg) => {
    const n = reg === 'lr' || reg === 'pc' ? 14 : /^r(?:[0-9]|1[0-2])$/.test(reg) ? Number(reg.slice(1)) : -1;
    requireCondition(n >= 0 && !(mask & 2 ** n), `${module}: invalid save register`);
    return mask + 2 ** n;
  }, 0);
  const unknown = reason => fn.unknown.add(reason);
  // C07 branches around these data words; arbitrary inline encodings remain unknown.
  const modeled = text.replace(
    /^ldr\.w (r(?:[0-9]|1[0-2])), \.Lcg_lit(\d+)\nb\.w \.Lcg_after\2\n\.p2align 2\n\.Lcg_lit\2:\n\.word [\w.$]+[+-]\d+\n\.Lcg_after\2:$/gm,
    (_, reg, id) => `ldr.w ${reg}, .Lcg_lit${id}\nb.w .Lcg_after${id}\n.Lcg_after${id}:`);
  for (const original of modeled.split('\n')) {
    const line = original.trim();
    let m;
    if ((m = line.match(/^@ stack-frame: (\d+) \+ (\d+) saved-register bytes$/))) {
      frame = { local: Number(m[1]), saves: Number(m[2]) };
      continue;
    }
    if ((m = line.match(/^\.global ([\w.$]+)$/))) globals.add(m[1]);
    if ((m = line.match(/^\.type ([\w.$]+),\s*%function$/))) {
      requireCondition(!fn, `${module}: nested assembly function`);
      requireCondition(!functions.has(m[1]), `${module}: duplicate function ${m[1]}`);
      fn = { module, name: m[1], frame_bytes: frame?.local ?? null,
        saves_bytes: frame?.saves ?? null, max_outgoing_bytes: 0, calls: [], unknown: new Set() };
      if (!frame) unknown('missing_frame_annotation');
      functions.set(fn.name, fn);
      frame = null;
      immediate = null;
      active = 0;
      prologue = false;
      returning = false;
      pushed = based = restored = popped = false;
      savedBytes = 0;
      lightweight = null;
      constants.fill(null);
      continue;
    }
    if (!fn) continue;
    trackConstants(line);
    if ((m = line.match(/^@ lightweight-frame: mask (\d+) base (r7|sp) calls ([01])$/))) {
      lightweight = { mask: Number(m[1]), base: m[2], calls: Number(m[3]) };
      savedBytes = lightweight.mask.toString(2).replaceAll('0', '').length * 4;
      requireCondition(savedBytes === fn.saves_bytes, `${module}: save mask metadata differs`);
      requireCondition(!lightweight.calls || (savedBytes + fn.frame_bytes) % 8 === 0,
        `${module}: misaligned call frame`);
      fn.save_mask = lightweight.mask;
      fn.frame_base = lightweight.base;
      pushed = popped = lightweight.mask === 0;
      prologue = restored = fn.frame_bytes === 0;
      based = lightweight.base === 'sp';
      continue;
    }
    if (lightweight && /^\.Lcg_return\d+:$/.test(line)) { returning = true; continue; }
    if (lightweight && (m = line.match(/^(push|pop)\.[nw] \{([^}]+)\}$/))) {
      requireCondition(registerMask(m[2]) === lightweight.mask, `${module}: emitted save mask differs`);
      if (m[1] === 'push') {
        requireCondition(!pushed && !returning, `${module}: duplicate save`);
        pushed = true;
      } else {
        requireCondition(returning && restored && !popped, `${module}: premature restore`);
        popped = true;
      }
      continue;
    }
    if (lightweight && line === 'bx lr' && returning) {
      requireCondition(!lightweight.calls && restored && popped, `${module}: invalid leaf return`);
      continue;
    }
    if (line === '.thumb_func' && !prologue) continue;
    if ((m = line.match(/^\.size ([\w.$]+),/))) {
      requireCondition(m[1] === fn.name, `${module}: wrong function end`);
      if (!prologue || !returning || !pushed || !based || !restored || !popped || active !== 0)
        unknown('unverified_frame_balance');
      fn.global = globals.has(fn.name);
      fn.unknown = [...fn.unknown].sort();
      fn = null;
      continue;
    }
    if ((m = line.match(/^movw r12, #(\d+)$/))) { immediate = Number(m[1]); continue; }
    if ((m = line.match(/^movt r12, #(\d+)$/))) {
      immediate = immediate === null ? null : (immediate & 65535) + Number(m[1]) * 65536;
      continue;
    }
    if (/^(?:mov|addw|subw) r12,/.test(line)) {
      immediate = constants[12];
      continue;
    }
    if (line === 'push.w {r4-r8, lr}' || line === 'push.w {r4-r8, r10, r11, lr}') {
      savedBytes = line === 'push.w {r4-r8, lr}' ? 24 : 32;
      if (prologue || pushed || fn.saves_bytes !== savedBytes) unknown('unmodeled_register_saves');
      pushed = true;
      continue;
    }
    if (line === 'mov r7, sp') {
      if (!prologue || based || active !== 0) unknown('unverified_frame_base');
      based = true;
      continue;
    }
    if (line === 'sub.w sp, sp, r12') {
      if (!prologue) {
        if (immediate !== fn.frame_bytes) unknown('unverified_frame_allocation');
        prologue = true;
      } else {
        if (immediate === null) { unknown('dynamic_stack_allocation'); active = null; }
        else if (active !== null) active += immediate;
        if (active !== null) fn.max_outgoing_bytes = Math.max(fn.max_outgoing_bytes, active);
      }
      continue;
    }
    if (line === 'mov sp, r7') {
      if (active !== 0) unknown('unbalanced_outgoing_arguments');
      returning = true;
      continue;
    }
    if (line === 'add.w sp, sp, r12') {
      if (returning) {
        if (immediate !== fn.frame_bytes) unknown('unverified_frame_restore');
        restored = true;
      } else if (active === null || immediate === null || immediate > active) {
        unknown('unbalanced_outgoing_arguments');
        active = null;
      } else active -= immediate;
      continue;
    }
    if (line === 'pop.w {r4-r8, pc}' || line === 'pop.w {r4-r8, r10, r11, pc}') {
      const bytes = line === 'pop.w {r4-r8, pc}' ? 24 : 32;
      if (!returning || !restored || popped || bytes !== savedBytes)
        unknown('unverified_frame_restore');
      popped = true;
      continue;
    }
    // The backend's unreachable trap has no software stack effect; faults are excluded.
    if (line === '.short 0xde00') continue;
    if ((m = line.match(/^bl(?:\.w)? ([\w.$]+)$/))) {
      fn.calls.push({ target: m[1], outgoing_bytes: active });
      immediate = null;
    } else if (/^blx\b/.test(line)) {
      fn.calls.push({ target: null, outgoing_bytes: active });
      immediate = null;
    } else if (/^b(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(?:\.w)?\s+(?!\.L)[\w.$]+$/.test(line)) {
      unknown('unmodeled_tail_branch');
    }
    if (/^(?:v?push|v?pop)\b/.test(line) ||
        /^[a-z][\w.]*\s+(?:sp|r7|pc)(?:[!,]|\s*$)/.test(line) ||
        /^msr\s+(?:msp|psp|control)\b/i.test(line) ||
        (/\b(?:sp|r7)\b/.test(line) && /\]!|\],/.test(line)) ||
        /\{[^}]*(?:sp|r7|pc)\b/.test(line) ||
        /^\.(?!p2align\b)[\w.]+(?:\s|$)/.test(line) || /^bx\b/.test(line)) {
      unknown('unmodeled_stack_or_control_instruction');
    }
    if (/^[a-z][\w.]*\s+r12(?:,|$)/.test(line) && !/^str/.test(line)) immediate = null;
  }
  requireCondition(!fn, `${module}: unterminated assembly function`);
  return functions;
}
export function parseMap(text) {
  const inputs = [];
  for (const line of text.split('\n')) {
    const m = line.match(/^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+\d+\s+(.+\.o):\(([^)]+)\)\s*$/i);
    if (m) inputs.push({ address: Number.parseInt(m[1], 16),
      lma: Number.parseInt(m[2], 16), size: Number.parseInt(m[3], 16),
      module: path.basename(m[4]), section: m[5] });
  }
  requireCondition(inputs.length > 0, 'Link map has no recognized input sections');
  return inputs;
}
export function stackGraph(nodes, entry) {
  const local = new Map(nodes.map(n => [`${n.module}:${n.name}`, n]));
  requireCondition(local.size === nodes.length, 'Duplicate qualified stack function');
  const globals = new Map();
  for (const n of nodes.filter(n => n.binding !== 0)) {
    requireCondition(!globals.has(n.name), `Ambiguous linked global function ${n.name}`);
    globals.set(n.name, n);
  }
  const indices = new Map(nodes.map((n, i) => [n, i]));
  const calls = nodes.map(n => n.calls.map(call => {
    const target = call.target === null ? null :
      local.get(`${n.module}:${call.target}`) ?? globals.get(call.target);
    return { ...call, index: target ? indices.get(target) : null };
  }));
  const edges = calls.map(row => row.filter(c => c.index !== null).map(c => c.index));
  const reverse = nodes.map(() => []);
  edges.forEach((row, i) => row.forEach(j => reverse[j].push(i)));
  const seen = new Set(), order = [];
  for (let i = 0; i < nodes.length; i++) {
    if (seen.has(i)) continue;
    seen.add(i);
    const stack = [{ index: i, next: 0 }];
    while (stack.length) {
      const top = stack.at(-1), row = edges[top.index];
      if (top.next === row.length) {
        order.push(top.index);
        stack.pop();
      } else {
        const next = row[top.next++];
        if (!seen.has(next)) {
          seen.add(next);
          stack.push({ index: next, next: 0 });
        }
      }
    }
  }
  const component = new Map(), groups = [];
  for (const i of order.reverse()) {
    if (component.has(i)) continue;
    const members = [], stack = [i], id = groups.length;
    component.set(i, id);
    while (stack.length) {
      const next = stack.pop();
      members.push(next);
      for (const parent of reverse[next])
        if (!component.has(parent)) {
          component.set(parent, id);
          stack.push(parent);
        }
    }
    groups.push(members);
  }
  const results = new Map(), recursive = [];
  for (let g = groups.length - 1; g >= 0; g--) {
    const members = groups[g], reasons = new Set();
    const cyclic = members.length > 1 || edges[members[0]].includes(members[0]);
    if (cyclic) {
      const functions = members.map(i => `${nodes[i].module}:${nodes[i].name}`).sort();
      const reason = `recursion:${functions.join(',')}`;
      recursive.push({ functions, reason });
      reasons.add(reason);
    }
    for (const i of members) {
      const n = nodes[i], id = `${n.module}:${n.name}`;
      for (const reason of n.unknown) reasons.add(reason);
      if (n.frame_bytes === null || n.saves_bytes === null) reasons.add(`unknown_frame:${id}`);
      for (const call of calls[i]) {
        if (call.outgoing_bytes === null) reasons.add(`unknown_outgoing:${id}`);
        if (call.index === null)
          reasons.add(call.target === null ? `indirect_call:${id}` : `unresolved_call:${id}->${call.target}`);
        else if (component.get(call.index) !== g)
          for (const reason of results.get(call.index).reasons) reasons.add(reason);
      }
    }
    for (const i of members) {
      const n = nodes[i];
      let suffix = n.max_outgoing_bytes ?? 0;
      for (const call of calls[i]) {
        const child = call.index !== null && component.get(call.index) !== g
          ? results.get(call.index).known_prefix_bytes : 0;
        suffix = Math.max(suffix, (call.outgoing_bytes ?? 0) + child);
      }
      const prefix = (n.frame_bytes ?? 0) + (n.saves_bytes ?? 0) + suffix;
      results.set(i, { bound_bytes: reasons.size ? null : prefix,
        known_prefix_bytes: prefix, reasons: [...reasons].sort() });
    }
  }
  const visit = n => results.get(indices.get(n));
  const rows = nodes.map(n => ({ module: n.module, function: n.name,
    local_frame_plus_saves: n.frame_bytes === null || n.saves_bytes === null
      ? null : n.frame_bytes + n.saves_bytes,
    max_outgoing_bytes: n.max_outgoing_bytes, ...visit(n) }));
  const rootNode = nodes.find(n => n.address === (entry & ~1));
  requireCondition(rootNode, 'ELF entry is not a reported function');
  const complete = rows.every(r => r.bound_bytes !== null);
  const reachable = new Set(), pending = [indices.get(rootNode)];
  while (pending.length) {
    const i = pending.pop();
    if (reachable.has(i)) continue;
    reachable.add(i);
    for (const next of edges[i]) pending.push(next);
  }
  const known = rows.filter(r => r.bound_bytes !== null);
  return { entry: { module: rootNode.module, function: rootNode.name, ...visit(rootNode) },
    max_local_frame_plus_saves: rows.some(r => r.local_frame_plus_saves === null)
      ? null : Math.max(...rows.map(r => r.local_frame_plus_saves)),
    max_linked_direct_chain_bytes: complete ? Math.max(...rows.map(r => r.bound_bytes)) : null,
    max_known_direct_chain_bytes: known.length ? Math.max(...known.map(r => r.bound_bytes)) : null,
    entry_reachable_functions: reachable.size,
    entry_reachable_unknown_functions: [...reachable].filter(i => rows[i].bound_bytes === null).length,
    entry_reachability_complete: [...reachable].every(i =>
      nodes[i].unknown.length === 0 && calls[i].every(call => call.index !== null)),
    recursive_components: recursive.sort((a, b) => a.reason.localeCompare(b.reason)),
    unknown_functions: rows.filter(r => r.bound_bytes === null).length,
    interrupts_included: false, functions: rows };
}
export function timerMetrics(text, platform) {
  let peak, user, system;
  if (platform === 'darwin') {
    peak = text.match(/^\s*(\d+)\s+maximum resident set size\s*$/m);
    const cpu = text.match(/[\d.]+\s+real\s+([\d.]+)\s+user\s+([\d.]+)\s+sys/);
    user = cpu?.[1]; system = cpu?.[2];
  } else if (platform === 'linux') {
    peak = text.match(/Maximum resident set size \(kbytes\):\s*(\d+)/);
    user = text.match(/User time \(seconds\):\s*([\d.]+)/)?.[1];
    system = text.match(/System time \(seconds\):\s*([\d.]+)/)?.[1];
  } else throw new Error(`Unsupported timing platform: ${platform}`);
  requireCondition(peak && user !== undefined && system !== undefined, 'Missing OS timer resource fields');
  const bytes = Number(peak[1]) * (platform === 'linux' ? 1024 : 1);
  requireCondition(Number.isSafeInteger(bytes) && bytes > 0, 'Invalid OS-reported peak RSS');
  requireCondition(Number.isFinite(Number(user)) && Number.isFinite(Number(system)), 'Invalid CPU timing');
  return { user_seconds: Number(user), system_seconds: Number(system), os_max_rss_bytes: bytes };
}
export function verifyUf2(bin, uf2) {
  const blocks = Math.ceil(bin.length / 256);
  requireCondition(blocks > 0 && uf2.length === blocks * 512, 'Incorrect UF2 block count');
  for (let i = 0; i < blocks; i++) {
    const b = uf2.subarray(i * 512, (i + 1) * 512);
    for (const [off, value] of [[0, 0x0a324655], [4, 0x9e5d5157], [8, 0x2000],
      [12, 0x10000000 + i * 256], [16, 256], [20, i], [24, blocks],
      [28, 0xe48bff59], [508, 0x0ab16f30]]) {
      requireCondition(b.readUInt32LE(off) === value, `Invalid UF2 field ${off}, block ${i}`);
    }
    const length = Math.min(256, bin.length - i * 256);
    requireCondition(b.subarray(32, 32 + length).equals(bin.subarray(i * 256, i * 256 + length)),
      `UF2 payload mismatch, block ${i}`);
    requireCondition(b.subarray(32 + length, 288).every(v => v === 255) &&
      b.subarray(288, 508).every(v => v === 0), `UF2 padding mismatch, block ${i}`);
  }
}
function summarize(functions) {
  const result = { functions: functions.length, instructions: 0, alloca: 0,
    loads: 0, stores: 0, phis: 0, branches: 0, calls: 0, volatile_loads: 0, volatile_stores: 0 };
  for (const f of functions) {
    result.instructions += f.instructions;
    result.alloca += f.opcodes.alloca ?? 0;
    result.loads += f.opcodes.load ?? 0;
    result.stores += f.opcodes.store ?? 0;
    result.phis += f.opcodes.phi ?? 0;
    result.branches += (f.opcodes.br ?? 0) + (f.opcodes.switch ?? 0) + (f.opcodes.indirectbr ?? 0);
    result.calls += (f.opcodes.call ?? 0) + (f.opcodes.invoke ?? 0);
    result.volatile_loads += f.volatile_loads;
    result.volatile_stores += f.volatile_stores;
  }
  return result;
}
function pipeline(events) {
  return events.filter(e => e.event === 'pass' && e.reason === 'started')
    .sort((a, b) => a.step - b.step).map(e => e.pass);
}
function manifest(file) {
  return read(file).toString().trim().split('\n').map(line => {
    const m = line.match(/^([0-9a-f]{64}) [ *](.+)$/);
    requireCondition(m, `Invalid SHA-256 manifest row: ${line}`);
    requireCondition(hash(path.resolve(root, m[2])) === m[1], `Stale input/tool hash: ${m[2]}`);
    return { file: m[2], sha256: m[1] };
  });
}
function fingerprint() {
  const firmware = path.join(owned, 'universe26/firmware');
  const output = {};
  for (const name of fs.readdirSync(firmware).sort()) {
    const file = path.join(firmware, name);
    if (/\.(?:o|elf|bin|map)$/.test(name)) output[name] = hash(file);
    if (name.endsWith('.o.owned')) {
      for (const artifact of ['preprocessed.i', 'frontend.ll', 'optimized.ll', 'assembly.s', 'preprocessed.s']) {
        if (fs.existsSync(path.join(file, artifact))) output[`${name}/${artifact}`] = hash(path.join(file, artifact));
      }
    }
  }
  for (const name of fs.readdirSync(path.join(owned, 'universe26/uf2')).sort())
    if (name.endsWith('.uf2')) output[`uf2/${name}`] = hash(path.join(owned, 'universe26/uf2', name));
  return output;
}
function collect(stage, selected, commands) {
  const firmware = path.join(owned, 'universe26/firmware'), modules = [], models = new Map();
  fs.mkdirSync(path.join(stage, 'modules'));
  for (const name of fs.readdirSync(firmware).filter(n => n.endsWith('.o.owned')).sort()) {
    const dir = path.join(firmware, name), input = path.join(dir, 'frontend.ll');
    if (!fs.existsSync(input)) continue;
    const module = name.slice(0, -6), report = path.join(stage, 'modules', `${module}.json`);
    const args = [...(selected === undefined ? [] : [`--passes=${selected}`]),
      `--check=${path.join(dir, 'optimized.ll')}`, input, '-o', report];
    command(path.join(tools, 'badge-ir-metrics'), args);
    const data = JSON.parse(read(report));
    requireCondition(data.schema === 1 && Array.isArray(data.before) &&
      Array.isArray(data.after) && Array.isArray(data.events), `Invalid IR metrics: ${module}`);
    const before = summarize(data.before), after = summarize(data.after);
    const delta = Object.fromEntries(Object.keys(before).map(key => [key, after[key] - before[key]]));
    const parsed = parseAssembly(read(path.join(dir, 'assembly.s')).toString(), module);
    for (const f of data.after)
      requireCondition(parsed.has(decodeName(f.function)), `Missing assembly function ${module}:${f.function}`);
    models.set(module, parsed);
    modules.push({ module, source: manifest(path.join(dir, 'source.sha256'))[0],
      hashes: Object.fromEntries(['preprocessed.i', 'frontend.ll', 'optimized.ll', 'assembly.s']
        .map(n => [n, hash(path.join(dir, n))])),
      pipeline: pipeline(data.events), before, after, delta, functions: data, assembly: [...parsed.values()] });
  }
  requireCondition(modules.length === 36, `Expected 36 C modules, got ${modules.length}; run complete owned build`);
  const passes = modules[0].pipeline;
  requireCondition(passes.length > 0 && modules.every(m => JSON.stringify(m.pipeline) === JSON.stringify(passes)),
    'Inconsistent pass pipelines across modules');
  const assets = ['firmware', 'nvram', 'clm'].map(name => {
    const file = path.join(owned, 'wifi-assets', `wifi_${name}.bin`);
    return { name, size: read(file).length, sha256: hash(file) };
  });
  const images = [];
  fs.mkdirSync(path.join(stage, 'images'));
  for (const name of fs.readdirSync(firmware).filter(n => n.endsWith('.elf')).sort()) {
    const image = name.slice(0, -4);
    const elf = JSON.parse(command(path.join(tools, 'badge-elf-info'), ['--metrics', path.join(firmware, name)]));
    requireCondition(elf.schema === 1 && Array.isArray(elf.symbols) && Array.isArray(elf.sections),
      `Invalid ELF metadata: ${image}`);
    writeJson(path.join(stage, 'images', `${image}.elf.json`), elf);
    const inputs = parseMap(read(path.join(firmware, `${image}.map`)).toString());
    const nodes = elf.symbols.filter(s => s.type === 2 && s.section !== 0).map(s => {
      const address = s.value & ~1;
      const input = inputs.find(i => address >= i.address && address < i.address + i.size &&
        models.get(i.module)?.has(s.name));
      const model = input ? models.get(input.module).get(s.name) : null;
      return model ? { ...model, address, size: s.size, binding: s.binding }
        : { module: '<unmodeled>', name: s.name, address, size: s.size, binding: s.binding,
          frame_bytes: null, saves_bytes: null, max_outgoing_bytes: null, calls: [],
          unknown: ['no_owned_assembly_model'] };
    });
    requireCondition(nodes.length > 0, `No linked functions: ${image}`);
    const vendor = inputs.filter(i => /^\.rodata\.wifi_(?:firmware|nvram|clm)$/.test(i.section));
    let vendorBytes = 0;
    for (const input of vendor) {
      const asset = assets.find(a => input.section === `.rodata.wifi_${a.name}`);
      requireCondition(input.size === asset.size, `Vendor section size mismatch: ${image}:${asset.name}`);
      vendorBytes += input.size;
    }
    requireCondition(vendor.length === (image === 'usb_wifi_udp' ? 3 : 0), `Unexpected vendor sections: ${image}`);
    const bin = read(path.join(firmware, `${image}.bin`));
    for (const input of vendor) {
      const asset = assets.find(a => input.section === `.rodata.wifi_${a.name}`);
      const start = input.lma - 0x10000000;
      requireCondition(start >= 0 && start + input.size <= bin.length &&
        bin.subarray(start, start + input.size).equals(read(path.join(owned, 'wifi-assets', `wifi_${asset.name}.bin`))),
      `Embedded vendor payload mismatch: ${image}:${asset.name}`);
    }
    verifyUf2(bin, read(path.join(owned, 'universe26/uf2', `${image}.uf2`)));
    requireCondition(vendorBytes <= bin.length, `Vendor bytes exceed image: ${image}`);
    const link = commands.find(c => c.endsWith(` -o badge-compiler/build/owned/universe26/firmware/${image}.elf`));
    requireCondition(link, `Missing link command: ${image}`);
    const script = link.match(/(?:^|\s)-T(\S+)/)?.[1];
    requireCondition(script, `Missing linker script: ${image}`);
    const linkerText = read(path.resolve(root, script)).toString();
    const region = linkerText.match(/SCRATCH_Y\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*(0x[0-9a-f]+)\s*,\s*LENGTH\s*=\s*(\d+)([kKmM]?)/i);
    requireCondition(region, `Unsupported stack-region declaration: ${script}`);
    const regionBytes = Number(region[2]) * (region[3].toLowerCase() === 'k' ? 1024 :
      region[3].toLowerCase() === 'm' ? 1024 * 1024 : 1);
    const top = elf.symbols.find(s => s.name === '__StackTop');
    requireCondition(top && top.value === Number.parseInt(region[1], 16) + regionBytes,
      `Stack top does not match declared SCRATCH_Y region: ${image}`);
    images.push({ image, flash_bytes: bin.length, vendor_bytes: vendorBytes,
      nonvendor_bytes: bin.length - vendorBytes, elf_text: elf.text, elf_data: elf.data,
      elf_bss: elf.bss, hashes: { elf: hash(path.join(firmware, name)),
        bin: hash(path.join(firmware, `${image}.bin`)),
        uf2: hash(path.join(owned, 'universe26/uf2', `${image}.uf2`)) },
      linker_script: { file: script, sha256: hash(path.resolve(root, script)) },
      stack_region_bytes: regionBytes, stack: stackGraph(nodes, elf.entry),
      linked_functions: nodes.map(n => ({ module: n.module, function: n.name, address: n.address,
        code_bytes: n.size, calls: n.calls, unknown: n.unknown })) });
  }
  requireCondition(images.length === 11, `Expected 11 images, got ${images.length}`);
  return { modules, images, assets, passes };
}
export function compareReports(before, after) {
  requireCondition(before.schema === 1 && after.schema === 1, 'Unsupported report schema');
  for (const key of ['target', 'root_makefile_sha256', 'build_id', 'target_flags_sha256', 'assets']) {
    requireCondition(JSON.stringify(before.configuration[key]) === JSON.stringify(after.configuration[key]),
      `Incomparable configuration: ${key}`);
  }
  const previous = new Map(before.modules.map(m => [m.module, m]));
  requireCondition(previous.size === after.modules.length, 'Incomparable module sets');
  const modules = after.modules.map(m => {
    const old = previous.get(m.module);
    requireCondition(old && old.hashes['preprocessed.i'] === m.hashes['preprocessed.i'] &&
      old.hashes['frontend.ll'] === m.hashes['frontend.ll'] &&
      JSON.stringify(old.source) === JSON.stringify(m.source), `Incomparable C05 inputs: ${m.module}`);
    return { module: m.module, delta: Object.fromEntries(Object.keys(m.after)
      .map(key => [key, m.after[key] - old.after[key]])) };
  });
  const oldImages = new Map(before.images.map(i => [i.image, i]));
  requireCondition(oldImages.size === after.images.length, 'Incomparable image sets');
  const images = after.images.map(i => {
    const old = oldImages.get(i.image);
    requireCondition(old, `Missing baseline image: ${i.image}`);
    requireCondition(JSON.stringify(old.linker_script) === JSON.stringify(i.linker_script),
      `Incomparable linker script: ${i.image}`);
    const stackDelta = (a, b) => a === null || b === null ? null : a - b;
    return { image: i.image, flash_delta_bytes: i.flash_bytes - old.flash_bytes,
      nonvendor_delta_bytes: i.nonvendor_bytes - old.nonvendor_bytes,
      max_local_frame_delta_bytes: stackDelta(i.stack.max_local_frame_plus_saves, old.stack.max_local_frame_plus_saves),
      entry_stack_delta_bytes: stackDelta(i.stack.entry.bound_bytes, old.stack.entry.bound_bytes),
      entry_stack_before: old.stack.entry, entry_stack_after: i.stack.entry };
  });
  const sameBuild = JSON.stringify(before.host) === JSON.stringify(after.host) &&
    (before.configuration.build_jobs ?? 1) === (after.configuration.build_jobs ?? 1);
  return { modules, images, comparable_build_cost: sameBuild,
    build_wall_delta_seconds: sameBuild ? after.build_cost.median_wall_seconds - before.build_cost.median_wall_seconds : null,
    build_rss_delta_bytes: sameBuild ? after.build_cost.max_os_rss_bytes - before.build_cost.max_os_rss_bytes : null,
    flash_delta_bytes: after.totals.flash_bytes - before.totals.flash_bytes,
    nonvendor_delta_bytes: after.totals.nonvendor_bytes - before.totals.nonvendor_bytes };
}
function median(values) {
  const sorted = [...values].sort((a, b) => a - b), middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}
function commandsFromLog() {
  const commands = [];
  let command = '';
  for (const line of read(path.join(owned, 'build.log')).toString().split('\n')) {
    if (!command && !/^bash badge-compiler\/(?:scripts\/)?owned-cc\.sh /.test(line)) continue;
    command += (command ? ' ' : '') + line.trim().replace(/\\$/, '').trim();
    if (!line.trimEnd().endsWith('\\')) { commands.push(command); command = ''; }
  }
  requireCondition(!command, 'Truncated build command log');
  return commands;
}
function tables(stage, report) {
  const lines = ['image\tflash_bytes\tvendor_bytes\tnonvendor_bytes\tmax_local_frame_plus_saves\tentry_stack_bound\tunknown_functions'];
  for (const i of report.images) lines.push([i.image, i.flash_bytes, i.vendor_bytes, i.nonvendor_bytes,
    i.stack.max_local_frame_plus_saves ?? 'unknown', i.stack.entry.bound_bytes ?? 'unknown',
    i.stack.unknown_functions].join('\t'));
  fs.writeFileSync(path.join(stage, 'images.tsv'), `${lines.join('\n')}\n`, { flag: 'wx' });
  const functionLines = ['module\tfunction\tphase\tinstructions\talloca\tloads\tstores\tphis\tbranches\tcalls'];
  for (const m of report.modules) for (const phase of ['before', 'after']) for (const f of m.functions[phase]) {
    const counts = summarize([f]);
    functionLines.push([m.module, decodeName(f.function), phase, counts.instructions, counts.alloca,
      counts.loads, counts.stores, counts.phis, counts.branches, counts.calls].join('\t'));
  }
  fs.writeFileSync(path.join(stage, 'functions.tsv'), `${functionLines.join('\n')}\n`, { flag: 'wx' });
}
export function benchmark(options) {
  const label = options.label ?? new Date().toISOString().replace(/[-:.]/g, '');
  requireCondition(/^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,63}$/.test(label), 'Invalid report label');
  const runs = options.runs ?? 3;
  requireCondition(Number.isInteger(runs) && runs >= 1 && runs <= 9, 'Runs must be an integer from 1 to 9');
  requireCondition(options.passes === undefined || options.passes.length > 0, 'Empty pass selection');
  requireCondition(options.compare === undefined || options.compare.length > 0, 'Empty comparison path');
  const destination = path.join(reports, label), stage = path.join(reports, `.${label}.tmp-${process.pid}`);
  requireCondition(!fs.existsSync(destination) && !fs.existsSync(stage), `Report already exists: ${destination}`);
  fs.mkdirSync(reports, { recursive: true });
  const lock = path.join(reports, '.benchmark-lock');
  fs.mkdirSync(lock);
  let created = false;
  try {
    fs.mkdirSync(stage);
    created = true;
    const env = { ...process.env, CC: process.env.CC ?? '/usr/bin/cc',
      HOST_CC: process.env.HOST_CC ?? '/usr/bin/cc', BADGE_BUILD_JOBS: '1' };
    delete env.BADGE_OPT_PASSES;
    if (options.passes !== undefined) env.BADGE_OPT_PASSES = options.passes;
    command('make', ['-B', '-C', 'badge-compiler', 'all'], { env });
    const measurements = [];
    let outputs;
    for (let run = 0; run < runs; run++) {
      const buildLog = path.join(stage, `build-${run + 1}.log`);
      const resourceLog = path.join(stage, `resources-${run + 1}.log`);
      const out = fs.openSync(buildLog, 'wx'), err = fs.openSync(resourceLog, 'wx');
      const start = performance.now();
      try {
        const args = [process.platform === 'darwin' ? '-l' : '-v', 'bash', 'badge-compiler/scripts/build-owned.sh'];
        command('/usr/bin/time', args, { env, stdio: ['ignore', out, err] });
      } finally { fs.closeSync(out); fs.closeSync(err); }
      const wall = (performance.now() - start) / 1000;
      measurements.push({ run: run + 1, wall_seconds: wall,
        ...timerMetrics(read(resourceLog).toString(), process.platform) });
      const current = fingerprint();
      if (outputs) requireCondition(JSON.stringify(outputs) === JSON.stringify(current),
        `Nondeterministic generated artifacts between build runs ${run} and ${run + 1}`);
      outputs = current;
    }
    requireCondition(read(path.join(owned, 'SUCCESS')).toString().startsWith('PASS:'), 'Owned build lacks success marker');
    const harness = manifest(path.join(owned, 'harness.sha256'));
    const commands = commandsFromLog();
    const data = collect(stage, options.passes, commands);
    requireCondition(JSON.stringify(outputs) === JSON.stringify(fingerprint()), 'Build artifacts changed during collection');
    const buildHeader = read(path.join(owned, 'universe26/firmware/usb_udp_build.h')).toString();
    const buildId = buildHeader.match(/#define USB_REPORT_BUILD_ID (0x[0-9a-f]+)u/i)?.[1];
    requireCondition(buildId, 'Missing generated Wi-Fi build ID');
    const flags = commands.map(line => line.replace(/ -o .*/, '').replace(/ -c .*/, ''));
    requireCondition(flags.length > 0, 'Missing recorded target commands');
    const flagsHash = crypto.createHash('sha256').update(JSON.stringify(flags)).digest('hex');
    const report = { schema: 1, label, created_at: new Date().toISOString(),
      host: { platform: process.platform, architecture: process.arch, os_release: os.release(),
        cpu: os.cpus()[0]?.model, logical_cpus: os.cpus().length,
        node_version: process.version, host_cc: command(env.CC, ['--version']).trim(),
        packaging_host_cc: command(env.HOST_CC, ['--version']).trim(),
        make_version: command('make', ['--version']).trim() },
      configuration: { target: 'thumbv8m.main-unknown-none-eabi', build_directory: path.relative(root, owned),
        build_jobs: 1,
        root_makefile_sha256: hash(path.join(root, 'Makefile')), target_flags_sha256: flagsHash,
        target_commands: flags, build_id: buildId, passes: data.passes, assets: data.assets,
        targets: read(path.join(owned, 'targets.txt')).toString().trim().split('\n') },
      provenance: { harness, reporting_script_sha256: hash(fileURLToPath(import.meta.url)),
        ir_metrics_tool_sha256: hash(path.join(tools, 'badge-ir-metrics')),
        elf_info_tool_sha256: hash(path.join(tools, 'badge-elf-info')), output_hashes: outputs },
      build_cost: { method: 'Forced serial target rebuild; host tools bootstrapped before timing; no OS cache flush',
        memory_method: 'OS time maximum RSS, not simultaneous process-tree RSS or tracked arena allocations',
        runs: measurements, median_wall_seconds: median(measurements.map(m => m.wall_seconds)),
        max_os_rss_bytes: Math.max(...measurements.map(m => m.os_max_rss_bytes)) },
      modules: data.modules, images: data.images,
      totals: { modules: data.modules.length, images: data.images.length,
        flash_bytes: data.images.reduce((sum, i) => sum + i.flash_bytes, 0),
        vendor_bytes: data.images.reduce((sum, i) => sum + i.vendor_bytes, 0),
        nonvendor_bytes: data.images.reduce((sum, i) => sum + i.nonvendor_bytes, 0),
        before: summarize(data.modules.flatMap(m => m.functions.before)),
        after: summarize(data.modules.flatMap(m => m.functions.after)) } };
    report.configuration.codegen_frame_addressing =
      read(path.join(owned, 'codegen-frame-addressing.txt')).toString().trim();
    requireCondition(['materialized', 'direct'].includes(report.configuration.codegen_frame_addressing),
      'Invalid recorded C07 frame addressing selection');
    report.configuration.codegen_register_residency =
      read(path.join(owned, 'codegen-register-residency.txt')).toString().trim();
    requireCondition(['stack', 'local', 'global', 'global-flex', 'global-compact'].includes(report.configuration.codegen_register_residency),
      'Invalid recorded C07 register residency selection');
    report.configuration.codegen_scalar_selection =
      read(path.join(owned, 'codegen-scalar-selection.txt')).toString().trim();
    requireCondition(['legacy', 'thumb', 'thumb-registers', 'thumb-layout', 'thumb-compact', 'thumb-schedule', 'thumb-relax', 'thumb-branch', 'thumb-homes', 'thumb-calls', 'thumb-effects', 'thumb-frames', 'thumb-control', 'thumb-operands', 'thumb-widths', 'thumb-constants'].includes(report.configuration.codegen_scalar_selection),
      'Invalid recorded C07 scalar selection');
    report.configuration.codegen_memory_selection =
      read(path.join(owned, 'codegen-memory-selection.txt')).toString().trim();
    requireCondition(['byte', 'word'].includes(report.configuration.codegen_memory_selection),
      'Invalid recorded C07 memory selection');
    if (options.compare) {
      const baseline = path.resolve(options.compare);
      requireCondition(fs.existsSync(path.join(baseline, 'SUCCESS')), 'Baseline is not a complete snapshot');
      report.comparison = compareReports(JSON.parse(read(path.join(baseline, 'report.json'))), report);
      report.comparison.baseline = baseline;
    }
    manifest(path.join(owned, 'harness.sha256'));
    writeJson(path.join(stage, 'report.json'), report);
    tables(stage, report);
    fs.writeFileSync(path.join(stage, 'SUCCESS'), 'PASS: complete 36-module/11-image C06 metrics snapshot\n', { flag: 'wx' });
    fs.renameSync(stage, destination);
    console.log(`${destination}/report.json`);
    return report;
  } catch (error) {
    if (created) fs.writeFileSync(path.join(stage, 'FAILED'), `${error.stack}\n`);
    throw new Error(`${error.message}\nFailed snapshot logs: ${stage}`, { cause: error });
  } finally { fs.rmdirSync(lock); }
}
function main() {
  const options = {};
  for (const arg of process.argv.slice(2)) {
    if (arg === '--help') {
      console.log('Usage: node badge-compiler/tools/benchmark-c06.mjs [--label=name] [--runs=1..9] [--passes=name,...] [--compare=snapshot-directory]');
      return;
    }
    const match = arg.match(/^--(label|runs|passes|compare)=(.*)$/);
    requireCondition(match && options[match[1]] === undefined, `Unknown/duplicate option: ${arg}`);
    options[match[1]] = match[1] === 'runs' ? Number(match[2]) : match[2];
  }
  benchmark(options);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try { main(); } catch (error) { console.error(`benchmark-c06: ${error.stack}`); process.exitCode = 1; }
}
