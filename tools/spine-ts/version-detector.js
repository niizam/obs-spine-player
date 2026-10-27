(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpineVersionDetector = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const VERSION_PATTERN = /^\d+\.\d+(?:\.\d+)?/;

  function readVarint(bytes, offset) {
    let value = 0;
    let shift = 0;
    let position = offset;
    while (position < bytes.length && shift < 35) {
      const current = bytes[position++];
      value |= (current & 0x7f) << shift;
      if ((current & 0x80) === 0) return { value, position };
      shift += 7;
    }
    throw new Error('Invalid Spine binary string length');
  }

  function readString(bytes, offset) {
    const length = readVarint(bytes, offset);
    if (length.value === 0) return { value: null, position: length.position };
    const end = length.position + length.value - 1;
    if (end > bytes.length) throw new Error('Spine binary string is truncated');
    return { value: new TextDecoder().decode(bytes.subarray(length.position, end)), position: end };
  }

  function versionAt(bytes, offset) {
    try {
      const version = readString(bytes, offset).value;
      return version && VERSION_PATTERN.test(version) ? version : null;
    } catch (error) {
      return null;
    }
  }

  /* Spine 4 starts with an eight-byte hash; Spine 3 starts with a hash string. */
  function fromBinary(input) {
    const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
    if (bytes.length < 10) throw new Error('Spine binary is too short');

    const spine4 = versionAt(bytes, 8);
    if (spine4 && /^4\./.test(spine4)) return spine4;

    let spine3 = null;
    try {
      spine3 = versionAt(bytes, readString(bytes, 0).position);
    } catch (error) {
      spine3 = null;
    }
    if (spine3 && /^3\./.test(spine3)) return spine3;
    if (spine4) return spine4;
    throw new Error('Spine binary has no readable version');
  }

  function fromJson(value) {
    const document = typeof value === 'string' ? JSON.parse(value) : value;
    const version = document && document.skeleton && document.skeleton.spine;
    if (typeof version !== 'string') throw new Error('Spine JSON has no skeleton.spine version');
    return version;
  }

  function runtimeFamily(version) {
    const match = /^(3\.7|4\.0|4\.1)(?:\.|$)/.exec(String(version || ''));
    if (!match) throw new Error(`Unsupported Spine version: ${version || 'unknown'}`);
    return match[1];
  }

  return { fromBinary, fromJson, runtimeFamily };
});
