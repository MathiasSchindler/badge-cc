import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export function orderHotText(script, symbols) {
  if (!symbols.length || symbols.length > 32 ||
      symbols.some(name => !/^[A-Za-z_][A-Za-z0-9_.]*$/.test(name)) ||
      new Set(symbols).size !== symbols.length)
    throw new Error('Hot text requires 1-32 distinct function names without wildcards');
  const selector = /(\.text\s*:\s*\{[^{}]*?)\*\(\.text\*\)(?=[^{}]*\})/g;
  if ([...script.matchAll(selector)].length !== 1)
    throw new Error('Expected one .text output section with a *(.text*) selector');
  return script.replace(selector, (_, prefix) =>
    prefix + symbols.map(name => `*(.text.${name}) `).join('') + '*(.text*)');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const [input, output, ...symbols] = process.argv.slice(2);
    if (!input || !output)
      throw new Error('Usage: hot-layout.mjs INPUT.ld NEW_OUTPUT.ld HOT_FUNCTION ...');
    fs.writeFileSync(output, orderHotText(fs.readFileSync(input, 'utf8'), symbols), { flag: 'wx' });
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
