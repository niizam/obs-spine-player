(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpinePlayerOptions = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  function extension(path) {
    const clean = String(path).split(/[?#]/, 1)[0];
    const dot = clean.lastIndexOf('.');
    return dot >= 0 ? clean.slice(dot + 1).toLowerCase() : '';
  }

  function create(configuration, coreUrl, atlasUrl, callbacks) {
    const options = {
      atlasUrl,
      animation: configuration.defaultAnimation || 'idle',
      alpha: true,
      backgroundColor: '00000000',
      premultipliedAlpha: true,
      preserveDrawingBuffer: false,
      showControls: false,
      showLoading: true,
      frame: callbacks.frame,
      update: callbacks.update,
      success: callbacks.success,
      error: callbacks.error
    };
    options[extension(coreUrl) === 'json' ? 'jsonUrl' : 'skelUrl'] = coreUrl;
    return options;
  }

  return { create, extension };
});
