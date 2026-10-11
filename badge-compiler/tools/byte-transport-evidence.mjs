import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { captureConstantSelection, verifyConstantSelection } from './constant-selection-evidence.mjs';

export function verifyByteTransport(file) {
  const report = verifyConstantSelection(file);
  assert.equal(report.milestone, 'RS02');
  return report;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, ...extra] = process.argv.slice(2);
    assert(file && !extra.length && ['--capture', '--verify'].includes(mode),
      'Usage: byte-transport-evidence.mjs --capture NEW_DIRECTORY | --verify RS02.json');
    const report = mode === '--capture' ? captureConstantSelection(file, 'RS02') : verifyByteTransport(file);
    console.log(JSON.stringify({ status: report.status, summary: report.summary,
      red_square: report.red_square, byte_transport: report.byte_transport,
      reached_instructions: report.reached_instructions }, null, 2));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
