(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpineVersionDetector = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
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

  function fromBinary(input) {
    const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
    if (bytes.length < 10) throw new Error('Spine binary is too short');
    const length = readVarint(bytes, 8);
    if (length.value <= 1 || length.position + length.value - 1 > bytes.length) {
      throw new Error('Spine binary has no readable version');
    }
    return new TextDecoder().decode(bytes.subarray(length.position, length.position + length.value - 1));
  }

  function fromJson(value) {
    const document = typeof value === 'string' ? JSON.parse(value) : value;
    const version = document && document.skeleton && document.skeleton.spine;
    if (typeof version !== 'string') throw new Error('Spine JSON has no skeleton.spine version');
    return version;
  }

  function runtimeFamily(version) {
    const match = /^4\.(0|1)(?:\.|$)/.exec(String(version || ''));
    if (!match) throw new Error(`Unsupported Spine version: ${version || 'unknown'}`);
    return `4.${match[1]}`;
  }

  return { fromBinary, fromJson, runtimeFamily };
});

