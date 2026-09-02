const test = require('node:test');
const assert = require('node:assert/strict');
const SpineEyeTracker = require('../data/player/eye-tracker.js');

function fixture() {
  const identityParent = { a: 1, b: 0, c: 0, d: 1 };
  const mirroredParent = { a: -1, b: 0, c: 0, d: 1 };
  const left = { data: { name: 'bone151' }, parent: identityParent, x: 10, y: 20 };
  const right = { data: { name: 'bone152' }, parent: mirroredParent, x: 30, y: 40 };
  const slots = {
    f_eye_id_l: { bone: left },
    f_eye_hi_l3: { bone: left },
    f_eye_hi_l2: { bone: left },
    f_eye_id_r: { bone: right },
    f_eye_hi_r3: { bone: right },
    f_eye_hi_r2: { bone: right }
  };
  let worldUpdates = 0;
  const skeleton = {
    findSlot: function (name) { return slots[name] || null; },
    updateWorldTransform: function () { worldUpdates++; }
  };
  return { left, right, player: { skeleton }, worldUpdates: function () { return worldUpdates; } };
}

test('resolves configured layers to one bone per eye and applies screen-space movement', () => {
  const rig = fixture();
  const tracker = new SpineEyeTracker(rig.player, {
    eyeTrackingEnabled: true,
    eyeMaxX: 6,
    eyeMaxY: 4,
    eyeSmoothingMs: 0
  });

  tracker.setTarget(1, 0.5);
  tracker.beforeFrame();
  tracker.afterUpdate(1 / 60);

  assert.equal(tracker.bones.length, 2);
  assert.deepEqual({ x: rig.left.x, y: rig.left.y }, { x: 16, y: 22 });
  assert.deepEqual({ x: rig.right.x, y: rig.right.y }, { x: 24, y: 42 });
  assert.equal(rig.worldUpdates(), 1);
});

test('restores the previous additive offset before every animation frame', () => {
  const rig = fixture();
  const tracker = new SpineEyeTracker(rig.player, {
    eyeTrackingEnabled: true,
    eyeMaxX: 6,
    eyeMaxY: 4,
    eyeSmoothingMs: 0
  });

  tracker.setTarget(1, 1);
  tracker.afterUpdate(1 / 60);
  tracker.beforeFrame();
  assert.deepEqual({ x: rig.left.x, y: rig.left.y }, { x: 10, y: 20 });
  assert.deepEqual({ x: rig.right.x, y: rig.right.y }, { x: 30, y: 40 });

  tracker.afterUpdate(1 / 60);
  assert.deepEqual({ x: rig.left.x, y: rig.left.y }, { x: 16, y: 24 });
  assert.deepEqual({ x: rig.right.x, y: rig.right.y }, { x: 24, y: 44 });
});

test('reports missing configured eye layers without failing the player', () => {
  const rig = fixture();
  const messages = [];
  const tracker = new SpineEyeTracker(
    rig.player,
    {
      eyeTrackingEnabled: true,
      eyeLeftSlots: 'missing_left',
      eyeRightSlots: 'missing_right'
    },
    function (level, message) { messages.push({ level, message }); }
  );

  assert.equal(tracker.bones.length, 0);
  assert.equal(messages[0].level, 'warn');
  assert.match(messages[0].message, /missing_left, missing_right/);
});
