import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const dir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
function fixture(t) {
  const root = fs.mkdtempSync(path.join(dir, 'build/owned-driver test-'));
  t.after(() => fs.rmSync(root, { recursive: true }));
  const compiler = path.join(root, 'badge-compiler'), tools = path.join(compiler, 'build');
  const bin = path.join(root, 'bin'), trace = path.join(root, 'trace');
  fs.mkdirSync(tools, { recursive: true });
  fs.mkdirSync(path.join(compiler, 'scripts'));
  fs.mkdirSync(bin);
  fs.writeFileSync(path.join(tools, 'badge-ir'), '#!/bin/sh\nprintf "ssa\\n"\n', { mode: 0o755 });
  fs.writeFileSync(path.join(tools, 'badge-codegen'), '#!/bin/sh\ncase "$1" in --default-register-residency) printf "stack\\n";; --default-scalar-selection) printf "legacy\\n";; --default-memory-selection) printf "byte\\n";; *) printf "materialized\\n";; esac\n',
    { mode: 0o755 });
  fs.copyFileSync(path.join(dir, 'scripts/build-owned.sh'), path.join(compiler, 'scripts/build-owned.sh'));
  fs.writeFileSync(path.join(bin, 'make'), `#!/bin/sh
{ printf 'make'; printf ' <%s>' "$@"; printf '\\n'; } >> "$DRIVER_TRACE"
if [ "\${FAIL_MAKE:-0}" = 1 ]; then exit 17; fi
`, { mode: 0o755 });
  fs.writeFileSync(path.join(tools, 'badge-sha256'), `#!/bin/sh
printf 'hash\\n' >> "$DRIVER_TRACE"
if [ "\${FAIL_HASH:-0}" = 1 ]; then printf 'hash failure\\n' >&2; exit 19; fi
printf 'manifest\\n'
`, { mode: 0o755 });
  return { root, compiler, trace, bin };
}
function run(f, jobs, args = [], extra = {}) {
  const env = { ...process.env, PATH: `${f.bin}:${process.env.PATH}`, DRIVER_TRACE: f.trace, ...extra };
  delete env.BADGE_BUILD_JOBS;
  if (extra.BADGE_CODEGEN_FRAME_ADDRESSING === undefined) delete env.BADGE_CODEGEN_FRAME_ADDRESSING;
  if (extra.BADGE_CODEGEN_REGISTER_RESIDENCY === undefined) delete env.BADGE_CODEGEN_REGISTER_RESIDENCY;
  if (extra.BADGE_CODEGEN_MEMORY_SELECTION === undefined) delete env.BADGE_CODEGEN_MEMORY_SELECTION;
  if (extra.BADGE_CODEGEN_SCALAR_SELECTION === undefined) delete env.BADGE_CODEGEN_SCALAR_SELECTION;
  if (extra.BADGE_IR_EXPRESSION_LOWERING === undefined) delete env.BADGE_IR_EXPRESSION_LOWERING;
  if (jobs !== undefined) env.BADGE_BUILD_JOBS = jobs;
  const result = spawnSync('bash', [path.join(f.compiler, 'scripts/build-owned.sh'), ...args],
    { env, encoding: 'utf8', timeout: 15000 });
  assert.ifError(result.error);
  return result;
}

test('default and explicit bounded jobs reach both Make invocations and provenance', t => {
  for (const [jobs, expected] of [[undefined, '4'], ['1', '1'], ['16', '16']]) {
    const f = fixture(t), result = run(f, jobs);
    assert.equal(result.status, 0, result.stderr);
    const trace = fs.readFileSync(f.trace, 'utf8').trim().split('\n');
    assert.equal(trace.length, 3);
    assert.match(trace[0], new RegExp(`<\\-j${expected}> <\\-C> <badge-compiler> <all>`));
    assert.equal(trace[1], 'hash');
    assert.match(trace[2], new RegExp(`<\\-B> <\\-j${expected}>`));
    const owned = path.join(f.compiler, 'build/owned');
    assert.equal(fs.readFileSync(path.join(owned, 'build-jobs.txt'), 'utf8'), `${expected}\n`);
    assert.equal(fs.readFileSync(path.join(owned, 'codegen-frame-addressing.txt'), 'utf8'), 'materialized\n');
    assert.equal(fs.readFileSync(path.join(owned, 'codegen-register-residency.txt'), 'utf8'), 'stack\n');
    assert.equal(fs.readFileSync(path.join(owned, 'codegen-scalar-selection.txt'), 'utf8'), 'legacy\n');
    assert.equal(fs.readFileSync(path.join(owned, 'frontend-expression-lowering.txt'), 'utf8'), 'ssa\n');
    assert.equal(fs.readFileSync(path.join(owned, 'targets.txt'), 'utf8'), 'all\nusb-wifi-udp\n');
    assert.match(fs.readFileSync(path.join(owned, 'SUCCESS'), 'utf8'), /^PASS:/);
  }
});

test('codegen selection is explicit and separate from optimizer provenance', t => {
  for (const residency of ['local', 'global', 'global-flex', 'global-compact']) {
    const f = fixture(t), result = run(f, '4', [], { BADGE_CODEGEN_FRAME_ADDRESSING: 'direct',
      BADGE_CODEGEN_REGISTER_RESIDENCY: residency });
    assert.equal(result.status, 0, result.stderr);
    const owned = path.join(f.compiler, 'build/owned');
    assert.equal(fs.readFileSync(path.join(owned, 'codegen-frame-addressing.txt'), 'utf8'), 'direct\n');
    assert.equal(fs.readFileSync(path.join(owned, 'codegen-register-residency.txt'), 'utf8'), `${residency}\n`);
  }
  for (const mode of ['', 'invalid']) {
    const invalid = fixture(t), output = run(invalid, '4', [], { BADGE_CODEGEN_FRAME_ADDRESSING: mode });
    assert.equal(output.status, 2, output.stderr);
    assert.equal(fs.readFileSync(invalid.trace, 'utf8').includes('<-B>'), false);
  }
  for (const mode of ['', 'invalid']) {
    const invalid = fixture(t), output = run(invalid, '4', [], { BADGE_CODEGEN_REGISTER_RESIDENCY: mode });
    assert.equal(output.status, 2, output.stderr);
    assert.equal(fs.readFileSync(invalid.trace, 'utf8').includes('<-B>'), false);
  }
});

test('scalar selection provenance and rejection are independent of register policy', t => {
  for (const selection of ['legacy', 'thumb', 'thumb-registers', 'thumb-layout', 'thumb-compact', 'thumb-schedule', 'thumb-relax', 'thumb-branch', 'thumb-homes', 'thumb-calls', 'thumb-effects', 'thumb-frames', 'thumb-control', 'thumb-operands', 'thumb-widths', 'thumb-constants']) {
    const f = fixture(t), result = run(f, '4', [], {
      BADGE_CODEGEN_FRAME_ADDRESSING: 'direct', BADGE_CODEGEN_REGISTER_RESIDENCY: 'global',
      BADGE_CODEGEN_SCALAR_SELECTION: selection });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(fs.readFileSync(path.join(f.compiler, 'build/owned/codegen-scalar-selection.txt'), 'utf8'),
      `${selection}\n`);
  }
  for (const selection of ['', 'invalid']) {
    const f = fixture(t), result = run(f, '4', [], { BADGE_CODEGEN_SCALAR_SELECTION: selection });
    assert.equal(result.status, 2);
    assert.equal(fs.readFileSync(f.trace, 'utf8').includes('<-B>'), false);
  }
});

test('frontend expression selection is explicit, preserved in provenance and validated', t => {
  for (const selection of ['legacy', 'ssa']) {
    const f=fixture(t), result=run(f,'4',[],{ BADGE_IR_EXPRESSION_LOWERING: selection });
    assert.equal(result.status,0,result.stderr);
    assert.equal(fs.readFileSync(path.join(f.compiler,'build/owned/frontend-expression-lowering.txt'),'utf8'),`${selection}\n`);
  }
  for (const selection of ['', 'invalid']) {
    const f=fixture(t), result=run(f,'4',[],{ BADGE_IR_EXPRESSION_LOWERING: selection });
    assert.equal(result.status,2,result.stderr);
    assert(!fs.readFileSync(f.trace,'utf8').includes('<-B>'));
  }
});

test('invalid jobs are rejected before builds or output creation', t => {
  for (const jobs of ['', '0', '17', '-1', '01', '1.5', 'unlimited', '4 8']) {
    const f = fixture(t), result = run(f, jobs);
    assert.equal(result.status, 2, jobs);
    assert.match(result.stderr, /BADGE_BUILD_JOBS must be an integer from 1 to 16/);
    assert.equal(fs.existsSync(f.trace), false);
    assert.equal(fs.existsSync(path.join(f.compiler, 'build/owned')), false);
  }
});
test('memory policy provenance and invalid policy rejection', t => {
  for (const selection of ['byte', 'word', '', 'invalid']) {
    const f = fixture(t), result = run(f, '4', [], { BADGE_CODEGEN_MEMORY_SELECTION: selection });
    assert.equal(result.status, selection === '' || selection === 'invalid' ? 2 : 0, result.stderr);
    if (result.status === 0)
      assert.equal(fs.readFileSync(path.join(f.compiler, 'build/owned/codegen-memory-selection.txt'), 'utf8'),
        `${selection}\n`);
    else
      assert.equal(fs.readFileSync(f.trace, 'utf8').includes('<-B>'), false);
  }
});

test('help and target validation remain available without compiling', t => {
  const f = fixture(t);
  const help = run(f, undefined, ['--help']);
  assert.equal(help.status, 0);
  assert.match(help.stdout, /BADGE_BUILD_JOBS=1\.\.16/);
  const invalid = run(f, undefined, ['unknown']);
  assert.equal(invalid.status, 2);
  assert.match(invalid.stderr, /unsupported target/);
  assert.equal(fs.existsSync(f.trace), false);
});

test('host build and provenance failures do not run firmware compilation or report success', t => {
  for (const extra of [{ FAIL_MAKE: '1' }, { FAIL_HASH: '1' }]) {
    const f = fixture(t), result = run(f, '4', ['diagnostics'], extra);
    assert.equal(result.status, extra.FAIL_MAKE ? 17 : 19);
    const trace = fs.readFileSync(f.trace, 'utf8');
    assert.equal(trace.includes('<-B>'), false);
    assert.equal(fs.existsSync(path.join(f.compiler, 'build/owned/SUCCESS')), false);
  }
});
