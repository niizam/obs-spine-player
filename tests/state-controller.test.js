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

