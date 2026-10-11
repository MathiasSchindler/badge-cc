import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { expressionCounts } from '../tools/expression-evidence.mjs';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const tool = name => path.join(root, 'build', name);
function run(command, args, success = true) {
  const r = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 180000,
    maxBuffer: 8 * 1024 * 1024 });
  assert.ifError(r.error);
  if (success) assert.equal(r.status, 0, `${command}\n${r.stderr}\n${r.stdout}`);
  else assert.notEqual(r.status, 0);
  return r;
}
function files(t) {
  const dir = fs.mkdtempSync(path.join(root, 'build/ssa-expression-'));
  t.after(() => { for (const f of fs.readdirSync(dir)) fs.unlinkSync(path.join(dir,f)); fs.rmdirSync(dir); });
  return name => path.join(dir,name);
}
const clang = process.env.LLVM_CLANG ?? 'clang', opt = process.env.LLVM_OPT ?? 'opt';
const target = ['--target=arm-none-eabi','-mcpu=cortex-m33','-mthumb','-ffreestanding','-fno-builtin'];
function lower(file, source) {
  fs.writeFileSync(file('source.c'), source);
  return ['legacy','ssa'].map(mode => {
    run(tool('badge-ir'), [`--expression-lowering=${mode}`,file('source.c'),'-o',file(`${mode}.ll`)]);
    run(opt, ['-passes=verify','-disable-output',file(`${mode}.ll`)]);
    return fs.readFileSync(file(`${mode}.ll`),'utf8');
  });
}
test('conditions stay i1 while C expression results remain int; scalar joins use PHIs', t => {
  const file = files(t);
  const [old, clean] = lower(file, `int branch(int x,int y){if(x<y)return 1;return 0;}
int value(int x,int y){return x<y;}
int choice(int x,int y,int z){return x?(y?3:4):(z?5:6);}
int logical(int x,int y){return x&&y;}
int arithmetic(int x,int y){return (x&&y)+7;}`);
  const fn = (ir,name) => ir.match(new RegExp(`define[^\\n]*@"${name}"[^]*?\\n\\}`))[0];
  assert(!fn(clean,'branch').includes('zext i1'));
  assert(fn(old,'branch').includes('zext i1'));
  assert(fn(clean,'value').includes('zext i1'));
  assert.equal((fn(clean,'choice').match(/ = phi i32 /g)??[]).length,3);
  assert(!fn(clean,'choice').includes('%t'));
  assert(fn(clean,'logical').includes('phi i1'));
  assert(fn(clean,'arithmetic').includes('zext i1'));
  assert((old.match(/alloca /g)??[]).length > (clean.match(/alloca /g)??[]).length);
  assert(!/\b(?:nsw|nuw|noundef|inbounds|noalias)\b/.test(clean));
  const a = expressionCounts(old), b = expressionCounts(clean);
  assert(b.operations.alloca < a.operations.alloca);
  assert(b.boolean_extensions < a.boolean_extensions);
  assert.equal(b.volatile_loads, a.volatile_loads);
  assert.equal(b.volatile_stores, a.volatile_stores);
});
test('float truth predicates retain NaN semantics and scalar float joins verify', t => {
  const file=files(t);
  const [,clean]=lower(file,`int truth(double x){if(x)return 1;return 0;}
int compare(double a,double b){if(a!=b)return 1;return 0;}
double pick(int c,double a,double b){return c?a:b;}`);
  assert.match(clean,/fcmp une double/);
  assert.match(clean,/phi double/);
  for(const mode of ['legacy','ssa'])
    run(clang,[...target,'-Oz','-c','-x','ir',file(`${mode}.ll`),'-o',file(`${mode}.o`)]);
});
test('Cortex-M33 validates nested effects, promotions, pointer/record layout and both backends', t => {
  const file=files(t);
  const source=fs.readFileSync(path.join(root,'tests/fixtures/ssa_expression_oracle.c'),'utf8');
  lower(file,source);
  run(clang,[...target,'-std=c11','-S','-emit-llvm','-O0',file('source.c'),'-o',file('clang.ll')]);
  const headers = text => [...text.matchAll(/^define [^\n]+$/gm)].map(m=>m[0]
    .replace(/ dso_local /,' ').replace(/(?:local_)?unnamed_addr /g,'')
    .replace(/(?:noundef |nonnull |nocapture |readonly |writeonly |returned )/g,'')
    .replace(/#[0-9]+.*$/,'').replace(/@"([^"]+)"/g,'@$1')
    .replace(/%[\w.]+/g,'%arg').replace(/\s+/g,' ').trim());
  // These scalar signatures are target ABI contracts, not a demand for identical ASTs.
  for(const name of ['relation','condition','choose_wide','choose_pointer','choose_bool','choose_narrow','choose_mixed']){
    const pick=ir=>headers(ir).find(line=>line.includes(`@${name}(`));
    assert.equal(pick(fs.readFileSync(file('ssa.ll'),'utf8')),
      pick(fs.readFileSync(file('clang.ll'),'utf8')),name);
  }
  run(tool('badge-asm'),['tests/fixtures/codegen_start.S','-o',file('start.o')]);
  const execute = object => {
    run(process.env.LLVM_LLD??'ld.lld',['-T','tests/fixtures/tilt_maze_fixture.ld',
      file('start.o'),object,'-o',file('run.elf')]);
    run(process.env.QEMU_ARM??'qemu-system-arm',['-M','mps2-an505','-nographic','-monitor','none',
      '-serial','none','-semihosting-config','enable=on,target=native','-kernel',file('run.elf')]);
  };
  run(clang,[...target,'-Oz','-c',file('source.c'),'-o',file('reference.o')]);
  execute(file('reference.o'));
  for(const mode of ['legacy','ssa']){
    run(clang,[...target,'-O0','-c','-x','ir',file(`${mode}.ll`),'-o',file('raw.o')]);
    execute(file('raw.o'));
    for(const optimizer of ['llvm','owned']){
      if(optimizer==='llvm') run(opt,['-S','-passes=default<Oz>',file(`${mode}.ll`),'-o',file('optimized.ll')]);
      else run(tool('badge-opt'),[file(`${mode}.ll`),'-o',file('optimized.ll')]);
      run(opt,['-passes=verify','-disable-output',file('optimized.ll')]);
      run(clang,[...target,'-Oz','-c','-x','ir',file('optimized.ll'),'-o',file('llvm.o')]);
      execute(file('llvm.o'));
      run(tool('badge-codegen'),['--scalar-selection=thumb-homes',file('optimized.ll'),'-o',file('owned.s')]);
      run(tool('badge-asm'),[file('owned.s'),'-o',file('owned.o')]);
      execute(file('owned.o'));
    }
  }
});
test('explicit policy, malformed C and duplicate options preserve existing output', t => {
  const file=files(t);
  assert.equal(run(tool('badge-ir'),['--default-expression-lowering']).stdout.trim(),'ssa');
  fs.writeFileSync(file('source.c'),'int f(int a){return a?1:;}');
  fs.writeFileSync(file('kept.ll'),'kept');
  for(const args of [
    ['--expression-lowering=ssa'],
    ['--expression-lowering=invalid'],
    ['--expression-lowering=ssa','--expression-lowering=legacy'],
  ]){
    run(tool('badge-ir'),[...args,file('source.c'),'-o',file('kept.ll')],false);
    assert.equal(fs.readFileSync(file('kept.ll'),'utf8'),'kept');
  }
});

test('nested conditions remain bounded and rejected inputs preserve output in both modes', t => {
  const file = files(t);
  lower(file, `int f(int a){if(${Array(48).fill('a').join('&&')})return 1;return 0;}`);
  fs.writeFileSync(file('source.c'), `int f(int a){if(${'!'.repeat(2048)}a)return 1;return 0;}`);
  for (const mode of ['legacy', 'ssa']) {
    fs.writeFileSync(file('kept.ll'), 'kept');
    const result = run(tool('badge-ir'), [`--expression-lowering=${mode}`,
      file('source.c'), '-o', file('kept.ll')], false);
    assert.match(result.stderr, /nesting limit exceeded/);
    assert.equal(fs.readFileSync(file('kept.ll'), 'utf8'), 'kept');
  }
});
