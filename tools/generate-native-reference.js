#!/usr/bin/env node
'use strict';

/* Uses the bundled Spine web runtime of one version (what the browser renderer
 * runs) to write reference numbers for the native backend's parity tests:
 *   node tools/generate-native-reference.js 4.1   (also 4.0, 3.7)
 * Output: tests/fixtures/spine<ver without dot>/reference.h */

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const root = path.resolve(__dirname, '..');
const RUNTIMES = {
  '4.1': { file: 'tools/spine-ts/runtime/4.1.20/spine-player.min.js', label: '4.1.20 web player' },
  '4.0': { file: 'tools/spine-ts/runtime/4.0.28/spine-player.min.js', label: '4.0.28 web player' },
  '3.7': { file: 'tools/spine-ts/runtime/3.7.94/spine-webgl.js', label: '3.7.94 spine-webgl + spine37-binary.js' }
};

function loadSpine(version) {
  const context = {
    console,
    window: {},
    document: { createElement: () => ({ style: {}, getContext: () => null }) },
    navigator: { userAgent: 'node' },
    performance: { now: () => 0 },
    requestAnimationFrame: () => 0
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync(path.join(root, RUNTIMES[version].file), 'utf8') + '\n;globalThis.spine = spine;', context);
  /* The browser reads 3.7 .skel files through the repo's port of the 3.7 binary reader. */
  if (version === '3.7') vm.runInContext(fs.readFileSync(path.join(root, 'tools/spine-ts/spine37-binary.js'), 'utf8'), context);
  return context.spine;
}

function loadSkeleton(spine, version, fixtures) {
  const atlasText = fs.readFileSync(path.join(fixtures, 'spineboy-pma.atlas'), 'utf8').replace(/\r/g, '');
  if (version === '3.7') {
    const atlas = new spine.TextureAtlas(atlasText, () => new spine.FakeTexture({ width: 1024, height: 1024 }));
    const binary = new spine.SkeletonBinary(new spine.AtlasAttachmentLoader(atlas));
    return binary.readSkeletonData(new Uint8Array(fs.readFileSync(path.join(fixtures, 'spineboy-pro.skel'))));
  }
  const atlas = new spine.TextureAtlas(atlasText);
  for (const page of atlas.pages) page.setTexture(new spine.FakeTexture({ width: page.width, height: page.height }));
  const binary = new spine.SkeletonBinary(new spine.AtlasAttachmentLoader(atlas));
  return binary.readSkeletonData(new Uint8Array(fs.readFileSync(path.join(fixtures, 'spineboy-pro.skel'))));
}

function direction(spine) {
  return spine.MixDirection.mixIn !== undefined ? spine.MixDirection.mixIn : spine.MixDirection.in;
}

/* SpinePlayer.calculateAnimationViewport (4.x) or the former 3.7 web player's animationBounds (3.7), before padding. */
function animationBounds(spine, version, data, name) {
  const skeleton = new spine.Skeleton(data);
  const animation = data.findAnimation(name);
  const steps = 100;
  const stepTime = animation.duration ? animation.duration / steps : 0;
  let minX = 100000000, maxX = -100000000, minY = 100000000, maxY = -100000000;
  const offset = new spine.Vector2();
  const size = new spine.Vector2();
  skeleton.setToSetupPose();
  for (let i = 0, time = 0; i < steps; i++, time += stepTime) {
    if (version === '3.7') skeleton.setToSetupPose();
    animation.apply(skeleton, time, time, false, [], 1, spine.MixBlend.setup, direction(spine));
    skeleton.updateWorldTransform();
    skeleton.getBounds(offset, size, []);
    const valid = version === '3.7'
      ? Number.isFinite(offset.x) && Number.isFinite(size.x) && size.x > 0 && size.y > 0
      : !isNaN(offset.x) && !isNaN(offset.y) && !isNaN(size.x) && !isNaN(size.y);
    if (!valid) continue;
    minX = Math.min(offset.x, minX);
    maxX = Math.max(offset.x + size.x, maxX);
    minY = Math.min(offset.y, minY);
    maxY = Math.max(offset.y + size.y, maxY);
  }
  return [minX, minY, maxX - minX, maxY - minY];
}

/* World vertices in draw order, as the skeleton renderer emits them (no clipping in Spineboy's walk). */
function walkVertices(spine, version, data) {
  const skeleton = new spine.Skeleton(data);
  const state = new spine.AnimationState(new spine.AnimationStateData(data));
  skeleton.setToSetupPose();
  skeleton.updateWorldTransform();
  state.setAnimation(0, 'walk', true);
  state.update(0.25);
  state.apply(skeleton);
  skeleton.updateWorldTransform();

  const out = [];
  const blocks = [];
  for (const slot of skeleton.drawOrder) {
    const attachment = slot.getAttachment();
    if (!attachment || slot.bone.active === false) continue;
    const alpha = skeleton.color.a * slot.color.a * attachment.color.a;
    if (slot.color.a === 0 || attachment.color.a === 0 || alpha === 0) continue;
    let vertices = null;
    if (attachment instanceof spine.RegionAttachment) {
      vertices = new Array(8);
      attachment.computeWorldVertices(version === '4.1' ? slot : slot.bone, vertices, 0, 2);
    } else if (attachment instanceof spine.MeshAttachment) {
      vertices = new Array(attachment.worldVerticesLength);
      attachment.computeWorldVertices(slot, 0, attachment.worldVerticesLength, vertices, 0, 2);
    } else if (attachment instanceof spine.ClippingAttachment) {
      throw new Error('fixture unexpectedly uses clipping during walk');
    }
    if (vertices) {
      out.push(...vertices);
      blocks.push(vertices.length / 2);
    }
  }
  return { vertices: out, blocks };
}

function timelineCounts(data, name) {
  const animation = data.findAnimation(name);
  const counts = { slot: 0, bone: 0, event: 0 };
  if (!animation) return counts;
  for (const timeline of animation.timelines) {
    if (typeof timeline.slotIndex === 'number') counts.slot++;
    else if (typeof timeline.boneIndex === 'number') counts.bone++;
    else if (Array.isArray(timeline.events)) counts.event++;
  }
  return counts;
}

function floats(values) {
  return values.map(function (value) { return `${value.toFixed(5)}f`; }).join(', ');
}

function main() {
  const version = process.argv[2] || '4.1';
  if (!RUNTIMES[version]) throw new Error(`usage: generate-native-reference.js <${Object.keys(RUNTIMES).join('|')}>`);
  const fixtures = path.join(root, 'tests', 'fixtures', `spine${version.replace('.', '')}`);
  const spine = loadSpine(version);
  const data = loadSkeleton(spine, version, fixtures);
  const names = data.animations.map(function (animation) { return animation.name; });
  const idle = animationBounds(spine, version, data, 'idle');
  const walk = animationBounds(spine, version, data, 'walk');
  const walkDraw = walkVertices(spine, version, data);
  const vertices = walkDraw.vertices;
  const mouth = data.slots.findIndex(function (slot) { return slot.name === 'mouth'; });
  const mouthBone = data.slots[mouth].boneData;
  const run = timelineCounts(data, 'run');

  const lines = [
    `/* Generated by tools/generate-native-reference.js ${version} from the bundled ${RUNTIMES[version].label}. Do not edit. */`,
    '#pragma once',
    '',
    `static const char *const REF_ANIMATION_NAMES[] = {${names.map(function (name) { return JSON.stringify(name); }).join(', ')}};`,
    `#define REF_ANIMATION_COUNT ${names.length}`,
    `#define REF_SLOT_COUNT ${data.slots.length}`,
    `#define REF_BONE_COUNT ${data.bones.length}`,
    `#define REF_MOUTH_SLOT ${mouth}`,
    `#define REF_MOUTH_BONE_NAME ${JSON.stringify(mouthBone.name)}`,
    `#define REF_MOUTH_PARENT_NAME ${JSON.stringify(mouthBone.parent.name)}`,
    `#define REF_RUN_SLOT_TIMELINES ${run.slot}`,
    `#define REF_RUN_BONE_TIMELINES ${run.bone}`,
    `#define REF_RUN_EVENT_TIMELINES ${run.event}`,
    `static const float REF_BOUNDS_IDLE[4] = {${floats(idle)}};`,
    `static const float REF_BOUNDS_WALK[4] = {${floats(walk)}};`,
    '/* Vertices per attachment, in draw order. Region quads (4) may start at a different corner per runtime. */',
    `#define REF_WALK_BLOCK_COUNT ${walkDraw.blocks.length}`,
    `static const int REF_WALK_BLOCKS[${walkDraw.blocks.length}] = {${walkDraw.blocks.join(', ')}};`,
    `#define REF_WALK_VERTEX_COUNT ${vertices.length / 2}`,
    `static const float REF_WALK_VERTICES[${vertices.length}] = {`
  ];
  for (let i = 0; i < vertices.length; i += 8) lines.push(`\t${floats(vertices.slice(i, i + 8))},`);
  lines.push('};', '');
  fs.writeFileSync(path.join(fixtures, 'reference.h'), lines.join('\n'));
  console.log(`${version}: ${names.length} animations (${names.join(', ')}), ${data.slots.length} slots, ${data.bones.length} bones, ` +
    `mouth slot ${mouth} on ${mouthBone.name}<-${mouthBone.parent.name}, run slot/bone/event timelines ${run.slot}/${run.bone}/${run.event}, ` +
    `${vertices.length / 2} walk vertices`);
}

main();
