import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { captureConstantSelection, verifyConstantSelection } from './constant-selection-evidence.mjs';

export function verifyCommandWrapper(file) {
  const report = verifyConstantSelection(file);
  assert.equal(report.milestone, 'RS03');
  return report;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [mode, file, ...extra] = process.argv.slice(2);
    assert(file && !extra.length && ['--capture', '--capture-preservation', '--verify'].includes(mode),
      'Usage: command-wrapper-evidence.mjs --capture NEW_DIRECTORY | --capture-preservation NEW_DIRECTORY | --verify RS03.json');
    const report = mode === '--verify' ? verifyCommandWrapper(file) :
      captureConstantSelection(file, 'RS03', mode === '--capture-preservation');
    console.log(JSON.stringify({ status: report.status, summary: report.summary,
      red_square: report.red_square, command_wrapper: report.command_wrapper,
      call_preservation: report.call_preservation,
      reached_instructions: report.reached_instructions }, null, 2));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
