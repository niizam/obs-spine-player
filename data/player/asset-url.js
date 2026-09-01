(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpineAssetUrl = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  function fromPath(path) {
    const value = String(path || '');
    if (!value) return value;

    const windowsPath = /^[a-z]:[\\/]/i.test(value);
    if (!windowsPath && /^[a-z][a-z0-9+.-]*:/i.test(value)) return value;

    const normalized = value.replace(/\\/g, '/');
    const encoded = encodeURI(normalized).replace(/#/g, '%23').replace(/\?/g, '%3F');
    return `http://absolute/${encoded}`;
  }

  return { fromPath };
});
