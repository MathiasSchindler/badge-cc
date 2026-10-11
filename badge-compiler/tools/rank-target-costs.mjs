import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const limits = { functions: 50000, edges: 200000, scopes: 32 };
export function rankOpportunities(input) {
  assert.equal(input.schema, 1);
  assert(input.functions.length > 0 && input.functions.length <= limits.functions, 'function budget');
  assert(input.profiles.length > 0 && input.profiles.length <= limits.scopes, 'profile budget');
  const nodes = new Map(), globals = new Map();
  for (const fn of input.functions) {
    const key = `${fn.module}:${fn.function}`;
    assert(!nodes.has(key), 'duplicate linked function');
    assert(Array.isArray(fn.calls) && Array.isArray(fn.target_costs), 'missing linked calls/cost evidence');
    nodes.set(key, fn);
    if (fn.global) {
      assert(!globals.has(fn.function), 'ambiguous linked global');
      globals.set(fn.function, key);
    }
  }
  let edges = 0;
  const definitions = [
    ['dependency-scheduling', ['load', 'multiply'], 'D05', ['M33-TIMING-LOAD-001', 'M33-TIMING-MUL-001']],
    ['compact-encoding', ['alu', 'load', 'store'], 'D04', ['THUMB-ADD-001', 'THUMB-ADD-002']],
    ['division-profitability', ['divide'], 'D07', ['M33-TIMING-DIV-001']],
    ['branch-layout', ['branch'], 'D06', ['M33-BRANCH-001']],
  ];
  const scopes = input.profiles.map(profile => {
    assert(Number.isFinite(profile.owned_ticks) && profile.owned_ticks > 0 &&
           Number.isFinite(profile.reference_ticks) && profile.reference_ticks > 0, 'invalid measured profile');
    assert(profile.source && profile.unit && profile.root, 'profile provenance/unit/root required');
    const pending = [profile.root], reached = new Set(), unresolved = new Set();
    while (pending.length) {
      const key = pending.pop();
      if (reached.has(key)) continue;
      const fn = nodes.get(key);
      assert(fn, `missing linked root/target: ${key}`);
      reached.add(key);
      for (const call of fn.calls) {
        assert(++edges <= limits.edges, 'edge analysis budget');
        assert(typeof call.target === 'string', 'call target');
        const local = `${fn.module}:${call.target}`;
        const target = nodes.has(local) ? local : globals.get(call.target);
        if (target) pending.push(target);
        else unresolved.add(`${key}->${call.target}`);
      }
      for (const reason of fn.unknown || [])
        if (reason.startsWith('indirect_call')) unresolved.add(`${key}:${reason}`);
    }
    const coverage = Object.create(null);
    for (const key of reached) for (const row of nodes.get(key).target_costs) {
      assert(Number.isSafeInteger(row.occurrences) && row.occurrences >= 0, 'invalid occurrence count');
      const count = (coverage[row.rule] || 0) + row.occurrences;
      assert(Number.isSafeInteger(count), 'occurrence overflow');
      coverage[row.rule] = count;
    }
    return { ...profile, owned_reference_ratio: profile.owned_ticks / profile.reference_ticks,
      reached_functions: [...reached].sort(), unresolved_calls: [...unresolved].sort(),
      reachability_complete: unresolved.size === 0, coverage };
  });
  const ranked = scopes.flatMap(scope => definitions.map(([name, rules, milestone, facts]) => ({
    opportunity: name, milestone, scope: scope.name, facts,
    observed_owned_reference_ratio: scope.owned_reference_ratio,
    reached_ir_occurrences: rules.reduce((sum, rule) => sum + (scope.coverage[rule] || 0), 0),
    prerequisites: name === 'compact-encoding'
      ? 'allocated registers, exact encoding legality and live APSR/IT effects'
      : name === 'dependency-scheduling'
        ? 'machine effects, alias/volatile/call barriers, operand-position forwarding and pressure'
        : name === 'division-profitability'
          ? 'operand magnitudes, definedness, legalization, code bytes and extra live values'
          : 'final widths/addresses and dynamic edge/folding history',
    predicted_saving: null,
  }))).filter(row => row.reached_ir_occurrences > 0)
    .sort((a, b) => b.observed_owned_reference_ratio - a.observed_owned_reference_ratio ||
      b.reached_ir_occurrences - a.reached_ir_occurrences ||
      a.opportunity.localeCompare(b.opportunity));
  return { schema: 1, model: 'RP2350-D03', scopes, ranked,
    work: { functions: nodes.size, traversed_edges: edges, limits },
    ranking_policy: 'investigation priority: measured scope gap, then reached IR coverage; not CPU share or predicted savings',
    limitations: ['Profile units/invocation rates cannot be summed across scopes.',
      'IR occurrences are not dynamic counts, legal compact forms or schedulable pairs.',
      'Indirect/unresolved calls remain visible; their bodies are not guessed.',
      'No per-opcode cycle sum, scheduler, allocation decision or default promotion.'] };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    assert.equal(process.argv.length, 3, 'usage: rank-target-costs.mjs INPUT.json');
    const stat = fs.statSync(process.argv[2]);
    assert(stat.isFile() && stat.size <= 64 * 1024 * 1024, 'input must be a regular file within 64 MiB');
    console.log(JSON.stringify(rankOpportunities(JSON.parse(fs.readFileSync(process.argv[2]))), null, 2));
  } catch (error) {
    console.error(`target-cost ranking: ${error.message}`);
    process.exitCode = 1;
  }
}
