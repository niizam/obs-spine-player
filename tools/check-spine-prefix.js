#!/usr/bin/env node
'use strict';

/* CTest check: every external symbol a prefixed spine backend library defines
 * must carry its prefix, so several Spine versions can share the plugin module.
 *
 *   node tools/check-spine-prefix.js --tool <dumpbin or nm path> --prefix sp41_ \
 *     --allow spine_backend_41 <library> */

const { execFileSync } = require('node:child_process');
const path = require('node:path');
const { parseSymbols, isCompilerSymbol } = require('./spine-symbols.js');

function parseArgs(argv) {
  const options = { tool: null, prefix: null, allow: new Set(), library: null };
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index];
    if (arg === '--tool') options.tool = argv[++index];
    else if (arg === '--prefix') options.prefix = argv[++index];
    else if (arg === '--allow') options.allow.add(argv[++index]);
    else options.library = arg;
  }
  if (!options.tool || !options.prefix || !options.library) {
    throw new Error('usage: check-spine-prefix.js --tool PATH --prefix sp41_ [--allow NAME]... LIBRARY');
  }
  return options;
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  const isDumpbin = /dumpbin(\.exe)?$/i.test(path.basename(options.tool));
  const args = isDumpbin ? ['-symbols', options.library] : ['-g', '--defined-only', options.library];
  const output = execFileSync(options.tool, args, { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });
  const parsed = parseSymbols(output, isDumpbin ? 'dumpbin' : 'nm', { machO: process.platform === 'darwin' });
  const bad = parsed.defined
    .filter(function (name) {
      return !name.startsWith(options.prefix) && !options.allow.has(name) && !isCompilerSymbol(name);
    })
    .sort();
  if (!parsed.defined.length) throw new Error(`no defined symbols read from ${options.library}`);
  if (bad.length) {
    console.error(`${bad.length} external symbol(s) in ${options.library} lack the ${options.prefix} prefix:`);
    for (const name of bad) console.error(`  ${name}`);
    console.error('Regenerate the prefix header with tools/generate-spine-prefix.js.');
    process.exit(1);
  }
  console.log(`${parsed.defined.length} defined symbols checked; all carry ${options.prefix} or are allowed`);
}

try {
  main();
} catch (error) {
  console.error(error.message);
  process.exit(1);
}
