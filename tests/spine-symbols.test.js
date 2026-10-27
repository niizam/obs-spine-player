const test = require('node:test');
const assert = require('node:assert/strict');
const { parseSymbols, prefixNames } = require('../tools/spine-symbols.js');

test('parses dumpbin externals', () => {
  const text = [
    '008 00000000 SECT3  notype ()    External     | spSkeleton_create',
    '00A 00000000 UNDEF  notype ()    External     | _spUtil_readFile',
    '00B 00000000 SECT1  notype       Static       | $SG123'
  ].join('\r\n');
  const parsed = parseSymbols(text, 'dumpbin');
  assert.deepEqual(parsed.defined, ['spSkeleton_create']);
  assert.deepEqual(parsed.undefined, ['_spUtil_readFile']);
});

test('parses nm output', () => {
  const text = [
    '0000000000000010 T spSkeleton_create',
    '                 U _spUtil_readFile',
    '0000000000000000 D spDefaultCurve',
    '                 U malloc'
  ].join('\n');
  const parsed = parseSymbols(text, 'nm');
  assert.deepEqual(parsed.defined.slice().sort(), ['spDefaultCurve', 'spSkeleton_create']);
  assert.ok(parsed.undefined.includes('_spUtil_readFile'));
  assert.ok(parsed.undefined.includes('malloc'));
});

test('strips Mach-O underscore', () => {
  const parsed = parseSymbols('0000000000000010 T _spSkeleton_create', 'nm', { machO: true });
  assert.deepEqual(parsed.defined, ['spSkeleton_create']);
});

test('prefix list keeps host hooks, drops libc', () => {
  assert.deepEqual(
    prefixNames({ defined: ['Json_create'], undefined: ['_spUtil_readFile', 'malloc'] }, 'sp41_'),
    ['Json_create', '_spUtil_readFile']
  );
});

test('prefix list skips compiler constants and C library inline functions', () => {
  const defined = [
    '??_C@_04LOAJBDKD@true@',
    '__real@3f800000',
    '__xmm@7fffffff7fffffff7fffffff7fffffff',
    '__local_stdio_printf_options',
    'printf',
    'sscanf',
    'readFloat',
    '_Entry_create'
  ];
  assert.deepEqual(prefixNames({ defined, undefined: [] }, 'sp41_'), ['_Entry_create', 'readFloat']);
});

test('prefix list skips names that already carry the prefix', () => {
  assert.deepEqual(prefixNames({ defined: ['sp41_spSkeleton_create', 'spBone_create'], undefined: [] }, 'sp41_'), [
    'spBone_create'
  ]);
});
