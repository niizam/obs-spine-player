'use strict';

/* Parses symbol tables of a vendored spine-c build so its external names can be
 * renamed per Spine version (see tools/generate-spine-prefix.js). */

/* C library functions that MSVC's CRT headers define inline in every object. */
const C_LIBRARY_INLINES = new Set([
  'printf', 'fprintf', 'sprintf', 'snprintf', 'scanf', 'fscanf', 'sscanf',
  'vprintf', 'vfprintf', 'vsprintf', 'vsnprintf', 'vscanf', 'vfscanf', 'vsscanf',
  '_snprintf', '_vsnprintf'
]);

function parseDumpbin(text) {
  const defined = new Set();
  const undefinedNames = new Set();
  for (const line of text.split(/\r?\n/)) {
    const match = /^\s*[0-9A-F]+\s+[0-9A-F]+\s+(\S+)\s+.*\bExternal\s+\|\s+(\S+)/.exec(line);
    if (!match) continue;
    (match[1] === 'UNDEF' ? undefinedNames : defined).add(match[2]);
  }
  return { defined: [...defined], undefined: [...undefinedNames] };
}

function parseNm(text, machO) {
  const defined = new Set();
  const undefinedNames = new Set();
  for (const line of text.split(/\r?\n/)) {
    const match = /^\s*(?:[0-9a-fA-F]+\s+)?([A-Za-z])\s+(\S+)\s*$/.exec(line);
    if (!match) continue;
    const type = match[1];
    let name = match[2];
    if (machO && name.startsWith('_')) name = name.slice(1);
    if (type === 'U' || type === 'w' || type === 'v') undefinedNames.add(name);
    else if (type === type.toUpperCase()) defined.add(name);
  }
  return { defined: [...defined], undefined: [...undefinedNames] };
}

function parseSymbols(text, tool, options = {}) {
  if (tool === 'dumpbin') return parseDumpbin(text);
  if (tool === 'nm') return parseNm(text, Boolean(options.machO));
  throw new Error(`Unknown symbol tool: ${tool}`);
}

function isCompilerSymbol(name) {
  return name.startsWith('?') || name.startsWith('__') || name.includes('@') || C_LIBRARY_INLINES.has(name);
}

/* Every external name the runtime defines, plus the host hooks it expects the
 * embedding program to define (_sp*), minus compiler and C library symbols. */
function prefixNames(parsed, prefix) {
  const names = new Set();
  for (const name of parsed.defined) {
    if (!isCompilerSymbol(name) && !name.startsWith(prefix)) names.add(name);
  }
  for (const name of parsed.undefined) {
    if (name.startsWith('_sp') && !name.startsWith(prefix)) names.add(name);
  }
  return [...names].sort();
}

module.exports = { parseSymbols, prefixNames, isCompilerSymbol };
