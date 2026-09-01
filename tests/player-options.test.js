const test = require('node:test');
const assert = require('node:assert/strict');
const playerOptions = require('../data/player/player-options.js');

test('provides idle during player construction for initial viewport calculation', () => {
  const options = playerOptions.create({}, 'character.skel', 'character.atlas', {});
  assert.equal(options.animation, 'idle');
  assert.equal(options.skelUrl, 'character.skel');
});

test('uses the configured default animation and JSON loader', () => {
  const options = playerOptions.create(
    { defaultAnimation: 'smile' },
    'character.json?revision=1',
    'character.atlas',
    {}
  );
  assert.equal(options.animation, 'smile');
  assert.equal(options.jsonUrl, 'character.json?revision=1');
});
