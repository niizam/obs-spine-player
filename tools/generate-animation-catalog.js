#!/usr/bin/env node

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const versionDetector = require('../data/player/version-detector.js');

function usage() {
  console.error('Usage: node tools/generate-animation-catalog.js <skeleton.skel|json> [atlas.atlas] [output.txt]');
  process.exit(2);
}

function runtimeContext() {
  return vm.createContext({
    console,
    TextDecoder,
    TextEncoder,
    Uint8Array,
    Uint16Array,
    Uint32Array,
    Float32Array,
    ArrayBuffer,
    DataView,
    Math,
    Number,
    JSON,
    Map,
    Set,
    WeakMap,
    Promise,
    performance: { now: function () { return 0; } },
    requestAnimationFrame: function () { return 0; },
    cancelAnimationFrame: function () {},
    navigator: { userAgent: 'obs-spine-player-catalog-generator' }
  });
}

function binaryAnimations(skeletonPath, atlasPath) {
  const binary = fs.readFileSync(skeletonPath);
  const version = versionDetector.fromBinary(binary);
  const family = versionDetector.runtimeFamily(version);
  const runtimeVersions = { '4.0': '4.0.28', '4.1': '4.1.20' };
  const runtimePath = path.join(
    __dirname,
    '..',
    'data',
    'player',
    'runtime',
    runtimeVersions[family],
    'spine-player.min.js'
  );
  const context = runtimeContext();
  vm.runInContext(fs.readFileSync(runtimePath, 'utf8'), context, { filename: runtimePath });

  const spine = context.spine;
  const atlas = new spine.TextureAtlas(fs.readFileSync(atlasPath, 'utf8'));
  for (const page of atlas.pages) {
    page.setTexture({
      getImage: function () { return { width: page.width, height: page.height }; },
      setFilters: function () {},
      setWraps: function () {}
    });
  }

  const loader = new spine.AtlasAttachmentLoader(atlas);
  const skeleton = new spine.SkeletonBinary(loader).readSkeletonData(new Uint8Array(binary));
  return skeleton.animations.map(function (animation) { return animation.name; });
}

function jsonAnimations(skeletonPath) {
  const document = JSON.parse(fs.readFileSync(skeletonPath, 'utf8'));
  return Object.keys(document.animations || {});
}

const skeletonArgument = process.argv[2];
if (!skeletonArgument) usage();

const skeletonPath = path.resolve(skeletonArgument);
const extension = path.extname(skeletonPath).toLowerCase();
const atlasPath = process.argv[3]
  ? path.resolve(process.argv[3])
  : skeletonPath.replace(/\.[^.]+$/, '.atlas');
const outputPath = process.argv[4]
  ? path.resolve(process.argv[4])
  : skeletonPath.replace(/\.[^.]+$/, '.animations.txt');

const animations = extension === '.json'
  ? jsonAnimations(skeletonPath)
  : binaryAnimations(skeletonPath, atlasPath);
if (!animations.length) throw new Error(`No animations found in ${skeletonPath}`);

fs.writeFileSync(outputPath, `${animations.join('\n')}\n`);
console.log(`Wrote ${animations.length} animations to ${outputPath}`);
