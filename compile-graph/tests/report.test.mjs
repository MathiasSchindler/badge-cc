import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { verify } from '../capture.mjs';

const directory = process.env.COMPILE_GRAPH_REPORT ? path.resolve(process.env.COMPILE_GRAPH_REPORT) : null;

test('real paired report has complete stages, exact image accounting and automatic demo regions',
  { skip: !directory && 'Set COMPILE_GRAPH_REPORT to a captured report directory.' }, () => {
    const report = verify(directory);
    assert.equal(report.modules.length, 9);
    assert.equal(report.images.length, 4);
    const square = report.modules.find(m => m.source === 'src/tools/red_square.c');
    const maze = report.modules.find(m => m.source === 'src/tools/tilt_maze.c');
    assert(square && maze);
    if (report.projects) {
      assert.deepEqual(report.projects.map(p => [p.name, p.modules.length]), [
        ['red_square', 1], ['tilt_maze', 8],
      ]);
      assert.equal(square.preprocessing_comparison.status, 'different');
      assert.equal(square.preprocessing_comparison.removed, 0);
      assert(square.preprocessing_comparison.hunks.some(hunk => hunk.added.includes('noinline')));
      assert(square.roles.owned.stages.preprocessed.presentation.line_markers >
        square.roles.llvm.stages.preprocessed.presentation.line_markers);
      assert(square.source_conditionals.some(item => item.text === '#ifdef __clang__'));
    }
    for (const role of ['owned', 'llvm']) {
      assert.equal(square.roles[role].stages.preprocessed.regions.length, 6);
      assert.equal(square.roles[role].stages.ast.regions.length, 6);
      assert.equal(maze.roles[role].stages.source.regions.length, 31);
      for (const module of report.modules) {
        assert.deepEqual(Object.keys(module.roles[role].stages), report.stages.map(([key]) => key));
        for (const item of Object.values(module.roles[role].stages))
          assert(item.bytes > 0 && item.regions.length > 0, `${module.source}/${role}/${item.format}`);
      }
    }
    if (report.policy.BADGE_CODEGEN_SCALAR_SELECTION === 'thumb-constants') {
      assert.deepEqual(report.images.map(i => [i.role, i.name, i.loaded_bytes]), [
        ['owned', 'red_square', 624], ['owned', 'tilt_maze', 19600],
        ['llvm', 'red_square', 568], ['llvm', 'tilt_maze', 8704],
      ]);
    }
    const ownedMaze = maze.roles.owned;
    assert.equal(ownedMaze.passes.length, report.policy.BADGE_OPT_PASSES.split(',').length);
    assert.equal(ownedMaze.passes.at(-1).sha256, ownedMaze.stages.optimized.sha256);
  });

test('copied report verifies offline and rejects changed image metrics',
  { skip: !directory && 'Set COMPILE_GRAPH_REPORT to a captured report directory.' }, () => {
    const destination = fs.mkdtempSync(path.join(os.tmpdir(), 'compile-graph-relocated-'));
    try {
      fs.cpSync(directory, destination, { recursive: true });
      const report = verify(destination);
      report.images[0].loaded_bytes++;
      fs.writeFileSync(path.join(destination, 'report.json'), JSON.stringify(report));
      assert.throws(() => verify(destination), /loaded-byte mismatch/);
      report.images[0].loaded_bytes--;
      if (report.projects) {
        report.projects[0].modules.push('unrelated.o');
        fs.writeFileSync(path.join(destination, 'report.json'), JSON.stringify(report));
        assert.throws(() => verify(destination), /project inventory mismatch/);
        report.projects[0].modules.pop();
        report.modules[0].preprocessing_comparison.status = 'false-equality';
        fs.writeFileSync(path.join(destination, 'report.json'), JSON.stringify(report));
        assert.throws(() => verify(destination), /token comparison mismatch/);
      }
    } finally { fs.rmSync(destination, { recursive: true }); }
  });
