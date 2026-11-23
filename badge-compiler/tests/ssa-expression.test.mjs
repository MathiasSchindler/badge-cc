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
      if(optimizer==='llvm') run(clang,[...target,'-Oz','-x','ir','-S','-emit-llvm',file(`${mode}.ll`),'-o',file('optimized.ll')]);
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
test('forced inline calls, linker names and SDK builtins work in Cortex-M33 objects', t => {
  const file = files(t);
  const [, ir] = lower(file, `
extern int link_value __asm__("wire_" "value");
extern int adjust(int) __asm__("wire_adjust");
static inline __attribute__((always_inline)) int step(int x) {
    static int calls;
    ++calls;
    if (x < 0) goto negative;
    return adjust(x) + calls;
negative:
    return link_value - calls;
}
inline __attribute__((__always_inline__)) int outer(int x) {
    int local = x + 1;
    return local * 2;
}
static volatile int x = 21, large = 2147483647;
int main(void) {
    int first = step(x++);
    int second = step(-1);
    int product;
    if (first != 43 || second != 5 || x != 22 || outer(step(2)) != 16) return 1;
    if (__builtin_clz((unsigned)x) != 27 || __builtin_ctz((unsigned)x) != 1) return 2;
    if (__builtin_mul_overflow(x, -2, &product) || product != -44) return 3;
    if (!__builtin_mul_overflow(large, 2, &product) || product != -2) return 4;
    if (__builtin_expect(x++, 22) != 22 || x != 23) return 5;
    return 0;
}`);
  assert.doesNotMatch(ir, /@"(?:step|outer|adjust|link_value)"/);
  fs.writeFileSync(file('provider.c'), 'int wire_value = 7; int wire_adjust(int x) { return x * 2; }');
  run(clang, [...target, '-Oz', '-c', file('provider.c'), '-o', file('provider.o')]);
  run(tool('badge-opt'), [file('ssa.ll'), '-o', file('output.ll')]);
  run(tool('badge-codegen'), [file('output.ll'), '-o', file('output.s')]);
  run(tool('badge-asm'), [file('output.s'), '-o', file('output.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
    file('start.o'), file('output.o'), file('provider.o'), '-o', file('run.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file('run.elf')]);
});
test('literal lookup specialization is opt-in and does not elide an effectful arm', t => {
  const file = files(t);
  const lookup = (effect = '') => `
int strcmp(const char *, const char *);
int writes;
static inline __attribute__((always_inline)) const char *lookup(const char *s) {
    ${Array.from({ length: 20 }, (_, i) =>
        `if (strcmp(s, "key${i}") == 0) { ${i === 12 ? effect : ''} return "value${i}"; } else`).join('\n')}
    return s;
}
int main(void) {
    const char *hit = lookup("key12");
    const char *miss = lookup("other");
    return hit[0] != 'v' || hit[5] != '1' || hit[6] != '2' || miss[0] != 'o';
}`;
  const [unfolded] = lower(file, lookup());
  assert.match(unfolded, /call i32 @"strcmp"/);
  const compile = source => {
    fs.writeFileSync(file('source.c'), source);
    run(tool('badge-ir'), ['--libc-strcmp', file('source.c'), '-o', file('lookup.ll')]);
    run(opt, ['-passes=verify', '-disable-output', file('lookup.ll')]);
    return fs.readFileSync(file('lookup.ll'), 'utf8');
  };
  assert.match(compile(lookup('++writes;')), /call i32 @"strcmp"/);
  assert.doesNotMatch(compile(lookup()), /call i32 @"strcmp"/);
  run(tool('badge-opt'), [file('lookup.ll'), '-o', file('output.ll')]);
  run(tool('badge-codegen'), [file('output.ll'), '-o', file('output.s')]);
  run(tool('badge-asm'), [file('output.s'), '-o', file('output.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
    file('start.o'), file('output.o'), '-o', file('run.elf')]);
  run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
    '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file('run.elf')]);
});
test('AAPCS bit-fields preserve adjacent bytes and GNU zero-length tails are addressable', t => {
  const file = files(t);
  lower(file, `
struct Mixed {
    unsigned char prefix;
    unsigned narrow:5;
    unsigned medium:11;
    unsigned char guard;
    unsigned wide:24;
    unsigned tag:8;
    unsigned full:32;
};
struct Tail { char tag; unsigned values[0]; };
struct Pool { unsigned live:1; const unsigned *items[]; };
_Static_assert(sizeof(struct Mixed)==12 && sizeof(struct Tail)==4, "layout");
_Static_assert(sizeof(((struct Tail *)0)->values)==0, "zero-length array is complete");
static const unsigned item=713, other=41;
static const struct Pool pool={.live=1,.items={&item,&other,0}};
_Static_assert(sizeof(pool)==4, "only the initialized symbol has extra storage");
static struct Mixed g={.prefix=0xa7,.narrow=17,.medium=(signed char)-1,.guard=0xcc,
                       .wide=0x123456,.tag=0x5a,.full=0x87654321};
static unsigned evaluations;
static struct Mixed *get(void) { ++evaluations; return &g; }
int main(void) {
    unsigned char *raw=(unsigned char*)&g;
    if (raw[0]!=0xa7 || raw[1]!=0xf1 || raw[2]!=0xff || raw[3]!=0xcc || g.medium!=2047)
        return 1;
    unsigned value=63;
    if ((get()->narrow=value)!=31 || evaluations!=1) return 2;
    if (++g.narrow!=0 || g.medium++!=2047 || (g.medium+=0x123)!=0x123) return 3;
    if (-g.medium>=0 || raw[1]!=0x60 || raw[2]!=0x24 || raw[3]!=0xcc || raw[0]!=0xa7)
        return 4;
    g.wide^=0x543210;
    if (g.wide!=0x460646 || g.tag!=0x5a || g.full!=0x87654321) return 5;
    struct Mixed local={0x11,2,511,0x33,{0xabcd12},0xa5,0xfedcba98};
    if (local.narrow!=2 || local.medium!=511 || local.guard!=0x33 ||
        local.wide!=0xabcd12 || local.tag!=0xa5 || local.full!=0xfedcba98) return 6;
    union Buffer { struct Tail packet; unsigned words[3]; } storage;
    storage.packet.tag=0x65;
    storage.packet.values[1]=local.medium;
    if (storage.words[2]!=511 || storage.packet.tag!=0x65) return 7;
    if (pool.live!=1 || *pool.items[0]!=713 || *pool.items[1]!=41 || pool.items[2]!=0) return 8;
    return 0;
}`);
  run(clang, [...target, '-std=gnu11', '-Oz', '-c', file('source.c'), '-o', file('reference.o')]);
  run(tool('badge-opt'), [file('ssa.ll'), '-o', file('output.ll')]);
  run(tool('badge-codegen'), [file('output.ll'), '-o', file('output.s')]);
  run(tool('badge-asm'), [file('output.s'), '-o', file('output.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const object of ['reference.o', 'output.o']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
      file('start.o'), file(object), '-o', file('run.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file('run.elf')]);
  }
});
test('dynamic stack and va-list handoff preserve AAPCS frames across calls', t => {
  const file = files(t);
  lower(file, `
extern unsigned consume(__builtin_va_list);
extern unsigned aligned_call(unsigned char *, unsigned, unsigned, unsigned, unsigned);
unsigned variadic(unsigned tag, ...) {
    __builtin_va_list args;
    __builtin_c23_va_start(args, tag);
    unsigned sum=consume(args);
    __builtin_va_end(args);
    return sum+tag;
}
unsigned stack_args(unsigned a,unsigned b,unsigned c,unsigned d,unsigned tag, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, tag);
    unsigned sum=consume(args);
    __builtin_va_end(args);
    return sum+a+b+c+d+tag;
}
unsigned allocate(unsigned n) {
    unsigned char *a=__builtin_alloca(n);
    unsigned char *b=__builtin_alloca(n+1);
    a[0]=29; b[0]=31; a[n-1]=37; b[n]=41;
    if (((unsigned)a&7) || ((unsigned)b&7)) return 0;
    unsigned first=aligned_call(a,n,3,5,7);
    unsigned second=aligned_call(b,n+1,11,13,17);
    return first+second+a[0]+b[0]+n;
}
unsigned leaf_stack(unsigned n) {
    unsigned char *p=__builtin_alloca(n);
    p[0]=30; p[n-1]=55;
    return p[0]+p[n-1]+((unsigned)p&7);
}`);
  fs.writeFileSync(file('provider.c'), `
extern unsigned variadic(unsigned, ...), stack_args(unsigned,unsigned,unsigned,unsigned,unsigned,...);
extern unsigned allocate(unsigned), leaf_stack(unsigned);
unsigned consume(__builtin_va_list args) {
    unsigned a=__builtin_va_arg(args,unsigned);
    unsigned long long b=__builtin_va_arg(args,unsigned long long);
    unsigned c=__builtin_va_arg(args,unsigned);
    return a+(unsigned)b+(unsigned)(b>>32)+c;
}
unsigned aligned_call(unsigned char *p, unsigned n, unsigned a,unsigned b,unsigned c) {
    unsigned sp;
    __asm__("mov %0, sp" : "=r"(sp));
    if ((sp&7) || ((unsigned)p&7)) return 0;
    return p[0]+p[n-1]+a+b+c;
}
int main(void) {
    if (variadic(2, 5u, 0x10000000bULL, 13u)!=32) return 1;
    if (stack_args(1,2,3,4,5,5u,0x10000000bULL,13u)!=45) return 2;
    for (unsigned n=2;n<75;++n)
        if (allocate(n)!=254+n || leaf_stack(n)!=85) return 3;
    return 0;
}`);
  run(clang, [...target, '-Oz', '-c', file('provider.c'), '-o', file('provider.o')]);
  run(tool('badge-opt'), [file('ssa.ll'), '-o', file('output.ll')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const scalar of ['legacy', 'thumb-homes', 'thumb-constants']) {
    run(tool('badge-codegen'), [`--scalar-selection=${scalar}`, file('output.ll'), '-o', file('output.s')]);
    run(tool('badge-asm'), [file('output.s'), '-o', file('output.o')]);
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
      file('start.o'), file('output.o'), file('provider.o'), '-o', file('run.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file('run.elf')]);
  }
});
test('local label table dispatch preserves range overrides and index side effects', t => {
  const file = files(t);
  const [, ir] = lower(file, `
static __attribute__((noinline)) unsigned dispatch(const unsigned char *code) {
    static const void *const targets[256] = {[0 ... 255]=&&fallback,[1 ... 3]=&&plus,[2]=&&twice,[0]=&&end};
    unsigned n=1;
    goto *targets[*code++];
plus:
    n+=3;
    goto *targets[*code++];
twice:
    n*=2;
    goto *targets[*code++];
fallback:
    n+=9;
    goto *targets[*code++];
end:
    return n;
}
int main(void) {
    const unsigned char code[]={1,2,3,255,0};
    return dispatch(code)!=20;
}`);
  assert.equal((ir.match(/switch i32/g) ?? []).length, 1);
  assert.doesNotMatch(ir, /blockaddress|indirectbr/);
  assert.match(ir, /noinline/);
  run(clang, [...target, '-std=gnu11', '-Oz', '-c', file('source.c'), '-o', file('reference.o')]);
  run(tool('badge-opt'), [file('ssa.ll'), '-o', file('output.ll')]);
  run(tool('badge-codegen'), [file('output.ll'), '-o', file('output.s')]);
  run(tool('badge-asm'), [file('output.s'), '-o', file('output.o')]);
  run(tool('badge-asm'), ['tests/fixtures/codegen_start.S', '-o', file('start.o')]);
  for (const object of ['reference.o', 'output.o']) {
    run(process.env.LLVM_LLD ?? 'ld.lld', ['-T', 'tests/fixtures/tilt_maze_fixture.ld',
      file('start.o'), file(object), '-o', file('run.elf')]);
    run(process.env.QEMU_ARM ?? 'qemu-system-arm', ['-M', 'mps2-an505', '-nographic', '-monitor', 'none',
      '-serial', 'none', '-semihosting-config', 'enable=on,target=native', '-kernel', file('run.elf')]);
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
