#!/usr/bin/env node

const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const versionDetector = require('../data/player/version-detector.js');

function usage() {
  console.error('Usage: node tools/generate-animation-catalog.js <skeleton.skel|json> [atlas.atlas] [output.txt]');
  console.error('Supports Spine 3.7, 4.0, and 4.1 exports.');
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

const playerDirectory = path.join(__dirname, '..', 'data', 'player');

/* The same files the OBS renderer loads for each family; Spine 3.7 adds the local binary reader. */
const runtimeScripts = {
  '3.7': ['runtime/3.7.94/spine-webgl.js', 'spine37-binary.js'],
  '4.0': ['runtime/4.0.28/spine-player.min.js'],
  '4.1': ['runtime/4.1.20/spine-player.min.js']
};

function loadRuntime(family) {
  const context = runtimeContext();
  for (const script of runtimeScripts[family]) {
    const scriptPath = path.join(playerDirectory, script);
    vm.runInContext(fs.readFileSync(scriptPath, 'utf8'), context, { filename: scriptPath });
  }
  return context.spine;
}

function placeholderTexture(width, height) {
  return {
    getImage: function () { return { width: width || 1, height: height || 1 }; },
    setFilters: function () {},
    setWraps: function () {}
  };
}

function readAtlas(spine, family, atlasText) {
  if (family === '3.7') {
    return new spine.TextureAtlas(atlasText, function () { return placeholderTexture(); });
  }
  const atlas = new spine.TextureAtlas(atlasText);
  for (const page of atlas.pages) page.setTexture(placeholderTexture(page.width, page.height));
  return atlas;
}

function binaryAnimations(skeletonPath, atlasPath) {
  const binary = fs.readFileSync(skeletonPath);
  const version = versionDetector.fromBinary(binary);
  const family = versionDetector.runtimeFamily(version);
  const spine = loadRuntime(family);

  const atlas = readAtlas(spine, family, fs.readFileSync(atlasPath, 'utf8'));
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
