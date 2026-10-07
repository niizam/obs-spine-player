const test = require('node:test');
const assert = require('node:assert/strict');
const SpineStateController = require('../data/player/state-controller.js');

function fixture(options = {}) {
  const calls = [];
  const player = {
    animationState: {
      setAnimation: (...arguments_) => calls.push(['set', ...arguments_]),
      addAnimation: (...arguments_) => calls.push(['add', ...arguments_]),
      setEmptyAnimation: (...arguments_) => calls.push(['empty', ...arguments_])
    }
  };
  const animations = ['idle', 'smile', 'action', 'talk_start'];
  return { calls, controller: new SpineStateController(player, animations, options) };
}

test('starts in idle', () => {
  const { calls, controller } = fixture();
  controller.start();
  assert.deepEqual(calls.at(-1), ['set', 0, 'idle', true]);
});

test('loops persistent emotions on track zero', () => {
  const { calls, controller } = fixture();
  assert.equal(controller.trigger('smile', true), true);
  assert.deepEqual(calls.at(-1), ['set', 0, 'smile', true]);
});

test('queues idle after one-shot actions', () => {
  const { calls, controller } = fixture();
  controller.trigger('action', false);
  assert.deepEqual(calls, [
    ['set', 0, 'action', false],
    ['add', 0, 'idle', true, 0]
  ]);
});

test('runs mouth movement independently on track one', () => {
  const { calls, controller } = fixture();
  controller.setYapping(true);
  controller.setYapping(false);
  assert.deepEqual(calls, [
    ['set', 1, 'talk_start', true],
    ['empty', 1, 0.08]
  ]);
});

test('ignores emotions when the optional state machine is disabled', () => {
  const { calls, controller } = fixture({ stateEnabled: false });
  calls.length = 0;
  assert.equal(controller.trigger('smile', true), false);
  assert.deepEqual(calls, []);
});


test('treats the automatic mouth animation as talk_start without a planner', () => {
  const { calls, controller } = fixture({ yapAnimation: 'auto' });
  controller.setYapping(true);
  assert.deepEqual(calls.at(-1), ['set', 1, 'talk_start', true]);
});

test('plays a derived mouth overlay inside its talk window', () => {
  const calls = [];
  const entry = {};
  const player = {
    animationState: {
      setAnimation: (...arguments_) => calls.push(['set', ...arguments_]),
      setAnimationWith: (track, animation, loop) => {
        calls.push(['setWith', track, animation.name, loop]);
        return entry;
      },
      setEmptyAnimation: (...arguments_) => calls.push(['empty', ...arguments_])
    }
  };
  const requests = [];
  const overlay = { name: 'TOUCH (mouth)' };
  const planner = (request) => {
    requests.push(request);
    return { mode: 'overlay', animation: overlay, start: 0.87, end: 9.13 };
  };
  const controller = new SpineStateController(player, ['IDLE', 'TOUCH'], { defaultAnimation: 'idle' }, planner);

  assert.deepEqual(requests, [{ yapAnimation: 'auto', mouthSlots: '' }]);
  assert.equal(controller.defaultAnimation, 'IDLE');
  controller.setYapping(true);
  assert.deepEqual(calls.at(-1), ['setWith', 1, 'TOUCH (mouth)', true]);
  assert.deepEqual(entry, { animationStart: 0.87, animationEnd: 9.13 });

  controller.configure({ defaultAnimation: 'idle', yapAnimation: 'auto' });
  assert.equal(requests.length, 1, 'an unchanged request is not planned again');
  controller.configure({ defaultAnimation: 'idle', yapAnimation: 'auto', mouthSlots: 'mouth_1' });
  assert.equal(requests.length, 2);
  assert.deepEqual(calls.at(-1), ['setWith', 1, 'TOUCH (mouth)', true], 'an active mouth track restarts with the new plan');
});

test('stops yap mode when the planner finds no mouth animation', () => {
  const { calls, controller } = fixture({ yapAnimation: 'missing' });
  assert.equal(controller.setYapping(true), false);
  assert.deepEqual(calls, []);
});
