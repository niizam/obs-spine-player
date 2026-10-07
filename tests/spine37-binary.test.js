const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

/* Arrays from the vm realm have another Array prototype; compare plain values. */
const plain = (value) => JSON.parse(JSON.stringify(value));

const playerDirectory = path.join(__dirname, '..', 'data', 'player');

function runtime() {
  const context = vm.createContext({ console, Math, Float32Array, Uint8Array, Int16Array, DataView, ArrayBuffer, Map, JSON });
  for (const script of ['runtime/3.7.94/spine-webgl.js', 'spine37-binary.js']) {
    const file = path.join(playerDirectory, script);
    vm.runInContext(fs.readFileSync(file, 'utf8'), context, { filename: file });
  }
  return context.spine;
}

const ATLAS = [
  '',
  'fixture.png',
  'size: 32,32',
  'format: RGBA8888',
  'filter: Linear,Linear',
  'repeat: none',
  ...['image', 'slot'].flatMap((name, index) => [
    name,
    '  rotate: false',
    `  xy: ${index * 16}, 0`,
    '  size: 16, 16',
    '  orig: 16, 16',
    '  offset: 0, 0',
    '  index: -1'
  ])
].join('\n');

function readFixture(spine) {
  const texture = { setFilters() {}, setWraps() {}, getImage() { return { width: 32, height: 32 }; } };
  const atlas = new spine.TextureAtlas(ATLAS, () => texture);
  const binary = fs.readFileSync(path.join(__dirname, 'fixtures', 'spine37-synthetic.skel'));
  return new spine.SkeletonBinary(new spine.AtlasAttachmentLoader(atlas)).readSkeletonData(new Uint8Array(binary));
}

test('installs a binary reader into the spine-ts 3.7 runtime', () => {
  const spine = runtime();
  assert.equal(typeof spine.SkeletonBinary, 'function');
  assert.equal(typeof spine.SkeletonJson, 'function');
});

test('reads every section of a Spine 3.7 binary export', () => {
  const spine = runtime();
  const data = readFixture(spine);

  assert.equal(data.version, '3.7.93');
  assert.deepEqual(plain(data.bones.map((bone) => bone.name)), ['root', 'child']);
  assert.equal(data.slots[0].attachmentName, 'image');
  assert.equal(data.ikConstraints[0].bendDirection, 1);
  assert.deepEqual(plain(data.skins.map((skin) => skin.name)), ['default', 'skin']);

  const mesh = data.defaultSkin.getAttachment(0, 'slot');
  assert.ok(mesh instanceof spine.MeshAttachment);
  assert.deepEqual(Array.from(mesh.bones), [1, 1, 1, 1, 1, 1]);
  assert.equal(mesh.hullLength, 6);

  assert.equal(data.events[0].audioPath, 'beat.ogg');
  assert.deepEqual(plain(data.animations.map((animation) => animation.name)), ['idle', 'talk_start']);
  const events = data.animations[0].timelines.find((timeline) => timeline instanceof spine.EventTimeline);
  assert.equal(events.events[0].stringValue, 'custom');
  assert.equal(events.events[0].intValue, -2);
  assert.equal(data.animations[0].duration, 1);
});

test('rejects truncated Spine 3.7 data instead of returning partial skeletons', () => {
  const spine = runtime();
  const texture = { setFilters() {}, setWraps() {}, getImage() { return { width: 32, height: 32 }; } };
  const atlas = new spine.TextureAtlas(ATLAS, () => texture);
  const binary = fs.readFileSync(path.join(__dirname, 'fixtures', 'spine37-synthetic.skel'));
  const reader = new spine.SkeletonBinary(new spine.AtlasAttachmentLoader(atlas));
  assert.throws(() => reader.readSkeletonData(new Uint8Array(binary.subarray(0, binary.length - 3))), /end of Spine binary/);
});
