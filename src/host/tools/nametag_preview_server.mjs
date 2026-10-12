import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const directory = path.dirname(fileURLToPath(import.meta.url));
const binary = path.resolve(process.argv[2] ?? 'build/host-tools/nametag_preview-8');
const html = fs.readFileSync(path.join(directory, 'nametag_preview.html'));
const license = fs.readFileSync(path.resolve(directory, '../../../vendor/cascadia-code/LICENSE'));
const port = Number(process.env.NAMETAG_PREVIEW_PORT ?? 8766);
if (!Number.isInteger(port) || (port !== 0 && port < 1024) || port > 65535) throw new Error('NAMETAG_PREVIEW_PORT must be 0 or 1024-65535');
fs.accessSync(binary, fs.constants.X_OK);
const binaries = new Map();
for (const file of [binary, ...(process.argv[3] ? [path.resolve(process.argv[3])] : [])]) {
  fs.accessSync(file, fs.constants.X_OK);
  const result = spawnSync(file, ['--info'], { timeout: 5000, encoding: 'utf8', maxBuffer: 1048576 });
  if (result.error || result.status !== 0) throw new Error(`Preview metadata failed: ${result.error?.message ?? result.stderr}`);
  const info = JSON.parse(result.stdout);
  if (![4, 8].includes(info.coverage_bits) || binaries.has(info.coverage_bits)) throw new Error('Expected distinct 4/8-bit preview binaries');
  binaries.set(info.coverage_bits, file);
}
const defaultCoverage = [...binaries.keys()][0];

function profileInput(profile) {
  if (profile === null || typeof profile !== 'object' || Array.isArray(profile)) throw new Error('profile must be an object');
  const keys = ['name', 'role', 'employer', 'username', 'location'];
  const limits = [128, 192, 96, 40, 128];
  const fields = keys.map((key, index) => {
    const value = profile[key];
    if (typeof value !== 'string' || /[\0\r\n]/.test(value) || Buffer.byteLength(value) > limits[index]) {
      throw new Error(`${key}: invalid field or UTF-8 byte limit exceeded`);
    }
    return value;
  });
  if (!Array.isArray(profile.interests) || profile.interests.length > 8 ||
      profile.interests.some(value => typeof value !== 'string' || /[\0\r\n]/.test(value) || Buffer.byteLength(value) > 64)) {
    throw new Error('interests: expected at most eight single-line strings, each <=64 UTF-8 bytes');
  }
  return [...fields, String(profile.interests.length), ...profile.interests].join('\n') + '\n';
}

function render(args, input, coverage = defaultCoverage) {
  const result = spawnSync(binaries.get(coverage), args, { input, timeout: 5000, killSignal: 'SIGKILL', maxBuffer: 1048576 });
  if (result.error) return { status: 500, body: `Renderer process failed: ${result.error.message}` };
  if (result.status !== 0) return { status: 422, body: result.stderr.toString() || 'Renderer failed without a diagnostic' };
  return { status: 200, body: result.stdout };
}

const server = http.createServer((request, response) => {
  const send = (status, body, type = 'text/plain; charset=utf-8') => {
    response.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'" });
    response.end(body);
  };
  if (request.method === 'GET' && request.url === '/') return send(200, html, 'text/html; charset=utf-8');
  if (request.method === 'GET' && request.url === '/license') return send(200, license);
  if (request.method === 'GET' && request.url === '/info') {
    const result = render(['--info']);
    if (result.status === 200) {
      const info = JSON.parse(result.body.toString());
      info.available_coverage = [...binaries.keys()];
      return send(200, JSON.stringify(info), 'application/json');
    }
    return send(result.status, result.body, result.status === 200 ? 'application/json' : undefined);
  }
  if (request.method !== 'POST' || !['/frame', '/validate'].includes(request.url)) return send(404, 'Not found');
  if (!request.headers['content-type']?.startsWith('application/json')) return send(415, 'Expected application/json');
  const chunks = [];
  let bytes = 0, rejected = false;
  request.on('data', chunk => {
    if (rejected) return;
    bytes += chunk.length;
    if (bytes > 8192) {
      rejected = true;
      send(413, 'Profile request exceeds 8192 bytes');
      return;
    }
    chunks.push(chunk);
  });
  request.on('error', error => {
    console.error(`Preview request failed: ${error.message}`);
    if (!response.headersSent) send(400, 'Request body could not be read');
  });
  request.on('end', () => {
    if (rejected) return;
    let parameters, input;
    try {
      parameters = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      input = profileInput(parameters.profile);
      if (!['mono', 'code'].includes(parameters.family)) throw new Error('Invalid font family');
      if (!binaries.has(parameters.coverage)) throw new Error('Invalid or unavailable coverage depth');
      if (request.url === '/frame' && (!['A', 'B', 'C'].includes(parameters.variant) ||
          !['natural', 'crisp'].includes(parameters.curve) || !Number.isInteger(parameters.time) ||
          parameters.time < 0 || parameters.time > 0xffffffff)) throw new Error('Invalid frame parameters');
    } catch (error) { return send(400, error.message); }
    const result = request.url === '/validate' ? render(['--validate', parameters.family], input, parameters.coverage) :
      render(['--frame', parameters.variant, String(parameters.time), parameters.family, parameters.curve], input, parameters.coverage);
    if (request.url === '/frame' && result.status === 200 && result.body.length !== 320 * 240 * 3) {
      return send(500, 'Renderer returned an invalid RGB frame length');
    }
    send(result.status, result.body, result.status === 200 ?
      request.url === '/validate' ? 'application/json' : 'application/octet-stream' : undefined);
  });
});
server.requestTimeout = 10000;
server.headersTimeout = 10000;
server.on('error', error => { console.error(`Preview server failed: ${error.message}`); process.exitCode = 1; });
server.listen(port, '127.0.0.1', () => console.log(`Nametag host preview: http://127.0.0.1:${server.address().port} (not badge timing evidence)`));
