const test = require('node:test');
const assert = require('node:assert/strict');
const assetUrl = require('../data/player/asset-url.js');

test('maps Unix paths to the OBS Browser local-file scheme', () => {
  assert.equal(
    assetUrl.fromPath('/usr/share/obs/My Character/model.skel'),
    'http://absolute//usr/share/obs/My%20Character/model.skel'
  );
});

test('maps Windows paths without mistaking the drive for a URL scheme', () => {
  assert.equal(
    assetUrl.fromPath('C:\\Spine Assets\\model.atlas'),
    'http://absolute/C:/Spine%20Assets/model.atlas'
  );
});

test('escapes URL delimiters that are valid in file names', () => {
  assert.equal(
    assetUrl.fromPath('/tmp/model#1?.skel'),
    'http://absolute//tmp/model%231%3F.skel'
  );
});

test('leaves existing non-file URLs unchanged', () => {
  assert.equal(assetUrl.fromPath('https://example.com/model.json'), 'https://example.com/model.json');
  assert.equal(assetUrl.fromPath('data:application/json;base64,e30='), 'data:application/json;base64,e30=');
});
