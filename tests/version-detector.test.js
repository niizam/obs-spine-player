const test = require('node:test');
const assert = require('node:assert/strict');
const detector = require('../data/player/version-detector.js');

test('reads a Spine version from the binary header', () => {
  const version = Buffer.from('4.1.20');
  const binary = Buffer.concat([Buffer.alloc(8), Buffer.from([version.length + 1]), version]);
  assert.equal(detector.fromBinary(binary), '4.1.20');
});

test('reads a Spine version from JSON', () => {
  assert.equal(detector.fromJson('{"skeleton":{"spine":"4.0.64"}}'), '4.0.64');
});

test('accepts only the supported runtime families', () => {
  assert.equal(detector.runtimeFamily('4.0.28'), '4.0');
  assert.equal(detector.runtimeFamily('4.1.20'), '4.1');
  assert.throws(() => detector.runtimeFamily('4.2.0'), /Unsupported/);
});

