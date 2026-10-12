import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const eight = process.env.NAMETAG_PREVIEW_8 ?? path.join(root, 'build/host-tools/nametag_preview-8');
const four = process.env.NAMETAG_PREVIEW_4 ?? path.join(root, 'build/host-tools/nametag_preview-4');
const profile = {
  name: 'Mathias Schindler', role: 'Senior Manager Developer Policy', employer: 'GitHub',
  username: '@mathiasschindler', location: 'Potsdam, Germany', interests: ['CRA','copyright','AI','open data','FOIA']
};
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

test('live preview renders shared C frames and validates editable profiles without regenerating assets', { timeout: 20000 }, async () => {
  const hashes = [hash(eight), hash(four)];
  const child = spawn(process.execPath, [path.join(root, 'src/host/tools/nametag_preview_server.mjs'), eight, four],
    { cwd: root, env: { ...process.env, NAMETAG_PREVIEW_PORT: '0' }, stdio: ['ignore','pipe','pipe'] });
  let output = '', errors = '';
  child.stderr.on('data', chunk => { errors += chunk; });
  let timer;
  try {
    const port = await new Promise((resolve, reject) => {
      timer = setTimeout(() => reject(Error(`server readiness timeout: ${errors}`)), 5000);
      child.on('error', reject);
      child.on('exit', code => reject(Error(`server exited ${code}: ${errors}`)));
      child.stdout.on('data', chunk => {
        output += chunk;
        const match = /http:\/\/127\.0\.0\.1:(\d+)/.exec(output);
        if (match) { clearTimeout(timer); resolve(Number(match[1])); }
      });
    });
    const base = `http://127.0.0.1:${port}`;
    const post = (url, parameters) => fetch(base + url, {
      method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(parameters)
    });
    const info = await (await fetch(base + '/info')).json();
    assert.deepEqual(info.available_coverage, [8, 4]);
    const page = await (await fetch(base)).text();
    assert.match(page, /Shared|shared C renderer/);
    assert.match(await (await fetch(base + '/license')).text(), /SIL OPEN FONT LICENSE Version 1.1/);
    const input = [profile.name, profile.role, profile.employer, profile.username, profile.location,
      String(profile.interests.length), ...profile.interests].join('\n') + '\n';
    for (const coverage of [8, 4]) for (const variant of ['A','B','C']) {
      const response = await post('/frame', {profile, family:'mono', curve:'natural', time:5500, variant, coverage});
      assert.equal(response.status, 200, await response.clone().text());
      const pixels = Buffer.from(await response.arrayBuffer());
      assert.equal(pixels.length, 320 * 240 * 3);
      const oracle = spawnSync(coverage === 8 ? eight : four,
        ['--frame', variant, '5500', 'mono', 'natural'], { input, timeout: 5000 });
      assert.ifError(oracle.error);
      assert.equal(oracle.status, 0, oracle.stderr.toString());
      assert.deepEqual(pixels, oracle.stdout, 'browser endpoint must return the exact shared-renderer output');
    }
    const alternate = {...profile, name:'Zo\u00eb \u0160indler', role:'Engineer', interests:['AI', 'open data']};
    const edited = await post('/validate', {profile:alternate, family:'code', coverage:4});
    assert.equal(edited.status, 200, await edited.clone().text());
    assert.equal((await edited.json()).layouts.length, 3);
    assert.deepEqual([hash(eight), hash(four)], hashes, 'profile edits must not rebuild embedded atlases');
    for (const invalid of [
      {...profile, name:'X'.repeat(129)},
      {...profile, interests:Array(9).fill('AI')},
      {...profile, name:'Mathias\nSchindler'}
    ]) assert.equal((await post('/validate', {profile:invalid, family:'mono', coverage:8})).status, 400);
    for (const invalid of [
      {...profile, name:'\ud83d\ude00'}, {...profile, name:''},
      {...profile, role:'X'.repeat(192)}, {...profile, interests:['']}
    ]) assert.equal((await post('/validate', {profile:invalid, family:'mono', coverage:8})).status, 422);
    assert.equal((await post('/frame', {profile, family:'mono', curve:'natural', time:0, variant:'D', coverage:8})).status, 400);
    assert.equal((await post('/frame', {profile, family:'mono', curve:'natural', time:-1, variant:'A', coverage:8})).status, 400);
    assert.equal((await post('/validate', {profile, family:'mono', coverage:16})).status, 400);
    assert.equal((await fetch(base + '/frame', {method:'POST', body:'bad'})).status, 415);
    assert.equal((await fetch(base + '/frame', {method:'POST', headers:{'Content-Type':'application/json'}, body:'{bad'})).status, 400);
    assert.equal((await fetch(base + '/frame')).status, 404);
  } finally {
    clearTimeout(timer);
    if (child.exitCode === null) {
      const stopped = once(child, 'exit');
      child.kill('SIGTERM');
      await stopped;
    }
  }
});
