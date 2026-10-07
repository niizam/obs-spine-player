const test = require('node:test');
const assert = require('node:assert/strict');
const SpineMouthOverlay = require('../data/player/mouth-overlay.js');

class Animation {
  constructor(name, timelines, duration) {
    this.name = name;
    this.timelines = timelines;
    this.duration = duration;
  }
}

/*
 * Shaped like a CounterSide illustration: the head bone carries face slots, the mouth bone carries the closed
 * mouth and an expression slot whose skin also holds "mouth_talk", and TOUCH speaks inside a full-body clip.
 */
function counterSideRig(extraAnimations = []) {
  const bones = ['root', 'head', 'mouth', 'arm'].map((name, index) => ({ name, index, parent: null }));
  bones[1].parent = bones[0];
  bones[2].parent = bones[1];
  bones[3].parent = bones[0];
  const slot = (name, bone) => ({ name, boneData: bones[bone] });
  const slots = [slot('arm', 3), slot('face', 1), slot('mouth_closed', 2), slot('mouth_expression', 2), slot('eye', 1)];
  const attachments = [];
  attachments[3] = { mouth_hate: {}, mouth_laugh: {}, mouth_talk: {} };
  attachments[2] = { mouth_closed: {} };

  const touch = new Animation('TOUCH', [
    { slotIndex: 2, frames: [0.5, 3, 4], attachmentNames: [null, 'mouth_closed', null] },
    { slotIndex: 3, frames: [0.5, 1, 1.5, 3, 4, 6], attachmentNames: ['mouth_talk', 'mouth_laugh', 'mouth_talk', null, 'mouth_talk', null] },
    { boneIndex: 2, frames: [0, 1, 1], ENTRIES: 3 },
    { boneIndex: 3, frames: [0, 10] },
    { slotIndex: 4, frames: [2], attachmentNames: ['eye_closed'] },
    { events: [{}], frames: [1] }
  ], 8);
  const idle = new Animation('IDLE', [{ boneIndex: 1, frames: [0, 5] }], 10);
  return {
    bones,
    slots,
    skins: [{ name: 'default', attachments }, null],
    animations: [idle, touch, ...extraAnimations]
  };
}

test('derives a mouth-only loop from the clip that uses the talking mouth', () => {
  const plan = SpineMouthOverlay.plan(counterSideRig(), Animation, { yapAnimation: 'auto' });

  assert.equal(plan.mode, 'overlay');
  assert.equal(plan.source, 'TOUCH');
  assert.deepEqual(plan.slots, ['mouth_closed', 'mouth_expression']);
  assert.deepEqual(plan.bones, ['mouth']);
  assert.equal(plan.start, 0.5);
  assert.equal(plan.end, 6);
  assert.equal(plan.animation.duration, 8);
  assert.deepEqual(plan.animation.timelines.map((timeline) => timeline.slotIndex ?? `bone ${timeline.boneIndex}`), [
    2,
    3,
    'bone 2'
  ]);
});

test('reduces an explicitly chosen full-body clip to its mouth timelines', () => {
  const plan = SpineMouthOverlay.plan(counterSideRig(), Animation, { yapAnimation: 'touch' });
  assert.equal(plan.mode, 'overlay');
  assert.equal(plan.source, 'TOUCH');
});

test('plays a dedicated talk_start animation unchanged', () => {
  const talk = new Animation('talk_start', [{ slotIndex: 3, frames: [0], attachmentNames: ['mouth_talk'] }], 1);
  const plan = SpineMouthOverlay.plan(counterSideRig([talk]), Animation, { yapAnimation: 'auto' });
  assert.deepEqual({ mode: plan.mode, name: plan.name }, { mode: 'animation', name: 'talk_start' });
});

test('plays clips that touch no mouth timelines unchanged', () => {
  const plan = SpineMouthOverlay.plan(counterSideRig(), Animation, { yapAnimation: 'IDLE' });
  assert.deepEqual({ mode: plan.mode, name: plan.name }, { mode: 'animation', name: 'IDLE' });
});

test('reports no mouth animation when the rig has neither talk_start nor a talking mouth', () => {
  const rig = counterSideRig();
  rig.skins[0].attachments[3] = { mouth_hate: {} };
  const plan = SpineMouthOverlay.plan(rig, Animation, { yapAnimation: 'auto' });
  assert.equal(plan.mode, null);
});

test('uses configured mouth slots instead of detection and reports missing names', () => {
  const plan = SpineMouthOverlay.plan(counterSideRig(), Animation, {
    yapAnimation: 'TOUCH',
    mouthSlots: 'mouth_expression, missing_slot'
  });
  assert.equal(plan.mode, 'overlay');
  assert.deepEqual(plan.slots, ['mouth_expression']);
  assert.deepEqual(plan.bones, []);
  assert.deepEqual(plan.missingSlots, ['missing_slot']);
  assert.deepEqual(plan.animation.timelines.map((timeline) => timeline.slotIndex), [3]);
});

test('does not animate a bone that also carries slots outside the mouth', () => {
  const rig = counterSideRig();
  rig.slots.push({ name: 'cheek', boneData: rig.bones[2] });
  const plan = SpineMouthOverlay.plan(rig, Animation, { yapAnimation: 'auto' });
  assert.deepEqual(plan.bones, ['mouth']);
  assert.ok(plan.slots.includes('cheek'));

  const configured = SpineMouthOverlay.plan(rig, Animation, { yapAnimation: 'auto', mouthSlots: 'mouth_closed,mouth_expression' });
  assert.deepEqual(configured.bones, []);
});
