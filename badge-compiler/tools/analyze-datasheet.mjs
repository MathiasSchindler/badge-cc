import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { experiments, modes, iterations, samples, sha, decode, root } from './datasheet-experiments.mjs';

export function validateCampaign(campaign) {
  assert.equal(campaign.schema, 1);
  assert.equal(campaign.status, 'captured', 'incomplete/failed campaign');
  assert.equal(campaign.serials.length, 2);
  assert.equal(new Set(campaign.serials).size, 2, 'duplicate devices');
  assert.equal(campaign.restoration.length, 2);
  for (const serial of campaign.serials) {
    const restored = campaign.restoration.find(r => r.serial === serial);
    const backup = campaign.backups.find(r => r.serial === serial);
    assert(restored && backup && restored.verified && restored.left_in_bootsel, 'unverified restoration');
    assert.equal(restored.sha256, backup.sha256, 'restoration hash');
  }
  assert.equal(campaign.rounds.length, 6, 'requires repeated control and two repeated role-swapped comparisons');
  assert.equal(new Set(campaign.rounds.map(r => r.nonce)).size, 6, 'stale round nonce');
  const images = new Map();
  const environments = new Map();
  for (const [index, round] of campaign.rounds.entries()) {
    assert.equal(round.index, index);
    assert.equal(round.kind, ['control', 'control', 'comparison-1', 'comparison-1', 'comparison-2', 'comparison-2'][index]);
    assert(/^[0-9a-f]{32}$/.test(round.nonce) && round.nonce !== '0'.repeat(32), 'nonce');
    assert.deepEqual(round.captures.map(c => c.serial), index % 2 ? [...campaign.serials].reverse() : campaign.serials, 'roles not swapped');
    assert.deepEqual(round.captures.map(c => c.compiler), index < 2 ? ['llvm', 'llvm'] : ['llvm', 'owned']);
    for (const capture of round.captures) {
      assert(/^[0-9a-f]{64}$/.test(capture.image_sha256), 'image hash');
      if (images.has(capture.compiler)) assert.equal(capture.image_sha256, images.get(capture.compiler), 'changed image');
      images.set(capture.compiler, capture.image_sha256);
      assert.equal(capture.rows.length, experiments.length * modes.length, 'missing rows');
      assert.equal(capture.environment.length, 7, 'missing environment');
      const configuration = [capture.environment[0], capture.environment[1],
        capture.environment[4].slice(0, 5)];
      if (environments.has(capture.serial))
        assert.deepEqual(configuration, environments.get(capture.serial), 'core/clock configuration changed between rounds');
      else environments.set(capture.serial, configuration);
      for (const [i, row] of capture.rows.entries()) {
        assert.equal(row.mode, modes[Math.floor(i / experiments.length)]);
        assert.equal(row.experiment, experiments[i % experiments.length].name);
        assert.equal(row.samples.length, samples);
        assert(row.samples.every(x => Number.isInteger(x) && x > 0 && x < 0x40000000));
        const sorted = [...row.samples].sort((a, b) => a - b);
        assert.deepEqual([row.min, row.median, row.max], [sorted[0], sorted[3], sorted[6]], 'invented statistics');
      }
    }
  }
}

export function analyze(campaign) {
  validateCampaign(campaign);
  const captures = campaign.rounds.flatMap(round => round.captures);
  const controls = campaign.rounds.slice(0, 2).flatMap(round => round.captures);
  const row = (capture, name, mode) => capture.rows.find(r => r.experiment === name && r.mode === mode);
  const noise = (capture, name, mode) => {
    const current = row(capture, name, mode);
    const sameDevice = controls.filter(c => c.serial === capture.serial).map(c => row(c, name, mode).median);
    const controlBoardDifferences = campaign.rounds.slice(0, 2).map(r =>
      Math.abs(row(r.captures[0], name, mode).median - row(r.captures[1], name, mode).median));
    const controlSpreads = controls.map(c => {
      const r = row(c, name, mode);
      return r.max - r.min;
    });
    return Math.max(current.max - current.min, ...controlSpreads,
      Math.max(...sameDevice) - Math.min(...sameDevice), ...controlBoardDifferences);
  };
  const comparisons = [
    ['simple-alu-dependency', 'alu-chain', 'alu-parallel', 'equal', 'M33-TIMING-ALU-001'],
    ['complex-alu-result-delay', 'shift-chain', 'alu-chain', 'positive', 'M33-TIMING-ALU-001'],
    ['complex-alu-flag-penalty', 'shift-flags', 'shift-independent', 'positive', 'M33-TIMING-ALU-001'],
    ['multiply-dependency', 'mul-chain', 'mul-parallel', 'positive', 'M33-TIMING-MUL-001'],
    ['mac-operand-position', 'mla-multiplier', 'mla-accumulator', 'positive', 'M33-TIMING-FWD-001'],
    ['alternate-mac-destinations', 'mla-alternate', 'mla-accumulator', 'equal', 'M33-TIMING-FWD-001'],
    ['long-mac-accumulator', 'umlal-accumulator', 'mla-accumulator', 'equal', 'M33-TIMING-FWD-001'],
    ['load-result-delay', 'load-use', 'load-independent', 'positive', 'M33-TIMING-LOAD-001'],
    ['last-ldm-register', 'ldm-last', 'ldm-first', 'positive', 'M33-TIMING-LOAD-001'],
    ['store-address-versus-data', 'load-store-address', 'load-store-data', 'positive', 'M33-TIMING-STORE-001'],
    ['wide-target-halfword', 'loop-wide-half', 'loop-wide-word', 'positive', 'M33-BRANCH-001'],
    ['narrow-target-halfword', 'loop-narrow-half', 'empty', 'equal', 'M33-BRANCH-001'],
    ['conditional-direction', 'conditional-taken', 'conditional-not-taken', 'positive', 'M33-BRANCH-001'],
    ['branch-fold-predecessor', 'fold-prev32', 'fold-prev16', 'positive', 'M33-BRANCH-001'],
    ['nop-encoding-fold', 'nop-legacy', 'nop-modern', 'positive', 'M33-FOLD-001'],
    ['simple-narrow-versus-wide', 'add-wide', 'add-narrow', 'equal', 'M33-TIMING-ALU-001'],
    ['signed-magnitude', 'signed-divide-negative', 'signed-divide-positive', 'equal', 'M33-TIMING-DIV-001'],
  ];
  const observations = comparisons.map(([name, a, b, prediction, fact]) => {
    const runs = captures.map(capture => {
      const x = row(capture, a, 'sram-warm'), y = row(capture, b, 'sram-warm');
      const delta = x.median - y.median;
      const envelope = noise(capture, a, 'sram-warm') + noise(capture, b, 'sram-warm');
      let verdict;
      if (prediction === 'equal') verdict = Math.abs(delta) <= envelope ? 'supports-within-noise' : 'contradicts-sequence-prediction';
      else verdict = delta > envelope ? 'supports' : delta < -envelope ? 'contradicts-sequence-prediction' : 'cannot-resolve';
      return { serial: capture.serial, compiler: capture.compiler, delta, noise_envelope: envelope, verdict };
    });
    const verdicts = [...new Set(runs.map(r => r.verdict))];
    return { name, fact, prediction, condition: 'warm SRAM, full audited sequences; not a universal opcode rule',
      verdict: verdicts.length === 1 ? verdicts[0] : 'mixed-or-unresolved', runs };
  });
  const rows = modes.flatMap(mode => experiments.map(e => ({
    mode, experiment: e.name, fact: e.fact,
    runs: captures.map(c => {
      const r = row(c, e.name, mode);
      return { serial: c.serial, compiler: c.compiler, min: r.min, median: r.median, max: r.max,
        noise_envelope: noise(c, e.name, mode) };
    }),
  })));
  const memoryObservations = experiments.map(e => ({
    experiment: e.name,
    condition: 'cold means target lines invalidated before one long invocation; not a permanently cold loop',
    runs: captures.map(c => ({
      serial: c.serial, compiler: c.compiler,
      xip_cold_minus_warm: row(c, e.name, 'xip-cold-entry').median - row(c, e.name, 'xip-warm').median,
      warm_xip_minus_sram: row(c, e.name, 'xip-warm').median - row(c, e.name, 'sram-warm').median,
      same_bank_dma_minus_disjoint: row(c, e.name, 'sram-dma-same-bank').median - row(c, e.name, 'sram-dma-disjoint').median,
      same_bank_noise: noise(c, e.name, 'sram-dma-same-bank') + noise(c, e.name, 'sram-dma-disjoint'),
      cold_noise: noise(c, e.name, 'xip-cold-entry') + noise(c, e.name, 'xip-warm'),
    })),
  }));
  const division = captures.map(c => {
    const control = row(c, 'empty', 'sram-warm');
    return { serial: c.serial, compiler: c.compiler,
      normalization: '(division median - same-image empty-loop median) / (4096 * 32); measured control, not a fitted constant',
      values: [1, 3, 255, 65535, 0xffffffff].map(value => ({
        dividend: value, divisor: 3,
        documented_cycles: value === 1 ? 2 : 4 + Math.floor((31 - Math.clz32(value) - 1) / 4),
        incremental_cycles: (row(c, `divide-${value}`, 'sram-warm').median - control.median) / (iterations * 32),
        uncertainty_cycles: (noise(c, `divide-${value}`, 'sram-warm') + noise(c, 'empty', 'sram-warm')) / (iterations * 32),
      })),
    };
  });
  return { schema: 1, milestone: 'D02', unit: 'timer0-clk_sys-cycles', clock_resolution: 'one system clock tick',
    method: 'six paired rounds; control/control first, then two repeated role-swapped LLVM/owned comparisons; raw data retained',
    cases: experiments.length, modes: modes.length, raw_samples: captures.length * experiments.length * modes.length * samples,
    observations, division, memoryObservations, rows,
    environments: captures.map(c => ({ serial: c.serial, compiler: c.compiler, registers: c.environment })),
    limits: ['Whole audited sequences, not isolated universal instruction costs.',
      'Endpoint/argument setup is included; overhead samples and same-image loop controls are reported.',
      'DMA mode combines SRAM instruction-fetch and data contention; no bus-priority changes.',
      'No core-1 launch; no interrupt handler executes during measured regions.',
      'Cold entry becomes warm within the long invocation; cache-miss costs are not independently identified.',
      'No FP/DCP acceleration, general scheduler or default promotion.'],
  };
}

export function analyzeFiles(file) {
  file = path.resolve(file);
  const campaign = JSON.parse(fs.readFileSync(file));
  const manifestBytes = fs.readFileSync(campaign.manifest_file);
  assert.equal(sha(manifestBytes), campaign.manifest_sha256, 'manifest drift');
  const manifest = JSON.parse(manifestBytes);
  assert.equal(campaign.build, manifest.build);
  assert.equal(manifest.iterations, iterations);
  assert.equal(manifest.samples, samples);
  assert.deepEqual(manifest.modes, modes);
  assert.deepEqual(manifest.experiments, experiments);
  for (const source of manifest.provenance)
    assert.equal(sha(fs.readFileSync(path.join(root, source.file))), source.sha256, `source drift: ${source.file}`);
  for (const image of manifest.images) {
    assert.equal(sha(fs.readFileSync(image.uf2)), image.sha256, 'image drift');
    assert.equal(sha(fs.readFileSync(image.elf)), image.elf_sha256, 'ELF drift');
    assert.equal(sha(fs.readFileSync(path.join(path.dirname(campaign.manifest_file), image.audit))), image.audit_sha256, 'audit drift');
  }
  for (const backup of campaign.backups) {
    assert.equal(sha(fs.readFileSync(backup.file)), backup.sha256, 'snapshot drift');
    assert.equal(sha(fs.readFileSync(path.join(path.dirname(file), `${backup.serial}-restored.bin`))), backup.sha256, 'restored snapshot drift');
  }
  for (const round of campaign.rounds) for (const c of round.captures) {
    const bytes = fs.readFileSync(c.file);
    assert.equal(sha(bytes), c.sha256, 'raw capture drift');
    const actual = decode(bytes, campaign.build, Buffer.from(round.nonce, 'hex'));
    assert.deepEqual(actual.environment, c.environment, 'metadata changed');
    assert.deepEqual(actual.rows, c.rows, 'derived samples changed');
    assert.equal(c.image_sha256, manifest.images.find(i => i.compiler === c.compiler).sha256);
  }
  const preservation = JSON.parse(fs.readFileSync(path.join(root, 'badge-compiler/build/datasheet/d01-results.json')))
    .compiler_firmware_and_tool_preservation;
  for (const entry of preservation) assert.equal(sha(fs.readFileSync(path.join(root, entry.file))), entry.sha256, 'D01 production drift');
  return { ...analyze(campaign), capture_file: file, capture_sha256: sha(fs.readFileSync(file)),
    manifest_sha256: campaign.manifest_sha256, production_preservation: preservation, restoration: campaign.restoration };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    assert.equal(process.argv.length, 3, 'usage: analyze-datasheet.mjs CAPTURE_JSON');
    console.log(JSON.stringify(analyzeFiles(process.argv[2]), null, 2));
  } catch (error) {
    console.error(`datasheet analysis: ${error.message}`); process.exitCode = 1;
  }
}
