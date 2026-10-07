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


test('reads a Spine 3.7 version after the hash string', () => {
  const hash = Buffer.from('lvvf7VoJQ6PFFnylIDh8Ri98fvQ');
  const version = Buffer.from('3.7.93');
  const binary = Buffer.concat([
    Buffer.from([hash.length + 1]), hash,
    Buffer.from([version.length + 1]), version,
    Buffer.alloc(9)
  ]);
  assert.equal(detector.fromBinary(binary), '3.7.93');
});

test('reads the synthetic Spine 3.7 fixture', () => {
  const binary = require('node:fs').readFileSync(require('node:path').join(__dirname, 'fixtures', 'spine37-synthetic.skel'));
  assert.equal(detector.fromBinary(binary), '3.7.93');
});

test('maps Spine 3.7 exports to the 3.7 runtime and rejects 3.8', () => {
  assert.equal(detector.runtimeFamily('3.7.93'), '3.7');
  assert.throws(() => detector.runtimeFamily('3.8.99'), /Unsupported/);
});
