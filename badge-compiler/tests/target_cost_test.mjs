import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { rankOpportunities } from '../tools/rank-target-costs.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
function fixture(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/target-cost-test-'));
  t.after(() => {
    for (const name of fs.readdirSync(directory)) fs.unlinkSync(path.join(directory, name));
    fs.rmdirSync(directory);
  });
  return name => path.join(directory, name);
}
function run(tool, args) {
  const result = spawnSync(path.join(root, 'build', tool), args, { encoding: 'utf8', timeout: 15000 });
  assert.ifError(result.error);
  return result;
}
const source = `target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
define i32 @sample(i32 %a, i32 %b, ptr %p) {
entry:
%m = mul i32 %a, %b
%d = udiv i32 %m, %b
%l = load volatile i32, ptr %p, align 4
store volatile i32 %d, ptr %p, align 4
%v = add i32 %l, %d
%w = add i64 1, 2
ret i32 %v
}
`;
test('all modeled facts resolve to the verified ledger; no instruction-name fixture recognition', () => {
  const model = fs.readFileSync(path.join(root, 'src/target_cost.c'), 'utf8');
  const ledger = JSON.parse(fs.readFileSync(path.join(root, 'docs/datasheet/fact-ledger.json')));
  for (const id of model.match(/(?:M33|THUMB|RP2350)-[A-Z0-9-]+-\d{3}/g))
    assert(ledger.facts.some(f => f.id === id && f.status !== 'unresolved'), id);
});
test('opt-in queries are conditional and never turn static IR counts into runtime predictions', t => {
  const file = fixture(t), input = file('input.ll'), output = file('report.json');
  fs.writeFileSync(input, source);
  let enabled;
  for (const tool of ['badge-ir-metrics', 'badge-ir-metrics-sanitize']) {
    const result = run(tool, ['--target-costs', input, '-o', output]);
    assert.equal(result.status, 0, result.stderr);
    const report = JSON.parse(fs.readFileSync(output));
    assert.equal(report.target_model.runtime_prediction, false);
    assert(report.target_model.work > 0 && report.target_model.work < report.target_model.work_limit);
    const rows = report.before[0].target_costs;
    assert.equal(rows.find(r => r.rule === 'multiply').occurrences, 1);
    assert.equal(rows.find(r => r.rule === 'divide').single_selected_instruction_baseline_issue.max, 11);
    assert.equal(rows.find(r => r.rule === 'load').result_ready, null);
    assert.equal(rows.find(r => r.rule === 'load').memory_wait_unknown, true);
    assert(rows.every(r => r.instruction_bytes === null && r.extra_temporaries === null));
    assert.equal(rows.find(r => r.rule === 'unknown').occurrences, 2);
    if (enabled) assert.deepEqual(report, enabled);
    enabled = report;
    assert.equal(run(tool, [input, '-o', output]).status, 0);
    const disabled = JSON.parse(fs.readFileSync(output));
    assert.equal(disabled.target_model, undefined);
    assert.equal(disabled.before[0].target_costs, undefined);
    const stripped = structuredClone(report);
    for (const phase of ['before', 'after'])
      for (const fn of stripped[phase]) delete fn.target_costs;
    delete stripped.target_model;
    assert.deepEqual(disabled, stripped);
  }
});
test('duplicate flags and work-budget exhaustion do not overwrite existing output', t => {
  const file = fixture(t), input = file('input.ll'), output = file('report.json');
  fs.writeFileSync(input, source);
  fs.writeFileSync(output, 'preserved');
  assert.equal(run('badge-ir-metrics', ['--target-costs', '--target-costs', input, '-o', output]).status, 2);
  const result = run('badge-ir-metrics-cost-budget', ['--target-costs', input, '-o', output]);
  assert.equal(result.status, 1);
  assert.match(result.stderr, /target-cost analysis work limit/);
  assert.equal(fs.readFileSync(output, 'utf8'), 'preserved');
  assert.equal(run('badge-ir-metrics-cost-budget', [input, '-o', output]).status, 0);
});
function rankingFixture() {
  return { schema: 1, functions: [
    { module: 'main.o', function: 'root', global: true, calls: [{ target: 'work' }],
      target_costs: [{ rule: 'branch', occurrences: 2 }] },
    { module: 'worker.o', function: 'work', global: true, calls: [{ target: 'root' }, { target: 'callback' }],
      target_costs: [{ rule: 'load', occurrences: 3 }, { rule: 'multiply', occurrences: 1 }] },
    { module: 'unused.o', function: 'dead', global: true, calls: [],
      target_costs: [{ rule: 'divide', occurrences: 10000 }] },
  ], profiles: [{ name: 'workload', root: 'main.o:root', owned_ticks: 100,
    reference_ticks: 25, unit: 'ticks/invocation', source: 'test measured profile' }] };
}
test('ranking uses measured scope gaps and reached code, handles cycles and exposes unresolved calls', () => {
  const report = rankOpportunities(rankingFixture());
  assert.equal(report.scopes[0].reached_functions.length, 2);
  assert.equal(report.scopes[0].reachability_complete, false);
  assert(report.scopes[0].unresolved_calls[0].includes('callback'));
  assert(report.ranked.every(row => row.predicted_saving === null));
  assert(!report.ranked.some(row => row.opportunity === 'division-profitability'));
  assert.equal(report.ranked[0].observed_owned_reference_ratio, 4);
  assert.equal(report.ranked.find(row => row.opportunity === 'dependency-scheduling').reached_ir_occurrences, 4);
});
test('ranking refuses missing roots, ambiguous globals, unmeasured profiles and analysis-budget excess', () => {
  const input = rankingFixture();
  assert.throws(() => rankOpportunities({ ...input, functions: [] }), /budget/);
  const bad = structuredClone(input);
  bad.profiles[0].reference_ticks = 0;
  assert.throws(() => rankOpportunities(bad), /measured profile/);
  bad.profiles[0] = { ...input.profiles[0], root: 'missing' };
  assert.throws(() => rankOpportunities(bad), /missing linked/);
  bad.profiles = input.profiles;
  bad.functions[2].function = 'root';
  assert.throws(() => rankOpportunities(bad), /ambiguous/);
  const large = structuredClone(input);
  large.functions[0].calls = Array(200001).fill({ target: 'work' });
  assert.throws(() => rankOpportunities(large), /edge analysis budget/);
});
