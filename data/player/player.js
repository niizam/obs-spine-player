(function () {
  const container = document.getElementById('player-container');
  const status = document.getElementById('status');
  const runtimeFiles = {
    '4.0': 'runtime/4.0.28/spine-player.min.js',
    '4.1': 'runtime/4.1.20/spine-player.min.js'
  };

  let loadedRuntime = null;
  let runtimePromise = null;
  let player = null;
  let controller = null;
  let assetFingerprint = null;
  let configureGeneration = 0;
  let latestConfiguration = {};

  function showStatus(message) {
    status.textContent = message;
    status.classList.add('visible');
  }

  function hideStatus() {
    status.classList.remove('visible');
  }

  function toFileUrl(path) {
    if (!path || /^[a-z][a-z0-9+.-]*:/i.test(path)) return path;
    const normalized = path.replace(/\\/g, '/');
    const prefix = /^[a-z]:\//i.test(normalized) ? 'file:///' : 'file://';
    return encodeURI(prefix + normalized);
  }

  function extension(path) {
    const clean = String(path).split(/[?#]/, 1)[0];
    const dot = clean.lastIndexOf('.');
    return dot >= 0 ? clean.slice(dot + 1).toLowerCase() : '';
  }

  async function detectRuntime(coreUrl) {
    const response = await fetch(coreUrl);
    if (!response.ok) throw new Error(`Could not read skeleton (${response.status})`);
    if (extension(coreUrl) === 'json') {
      return SpineVersionDetector.runtimeFamily(SpineVersionDetector.fromJson(await response.text()));
    }
    return SpineVersionDetector.runtimeFamily(SpineVersionDetector.fromBinary(await response.arrayBuffer()));
  }

  function loadRuntime(family) {
    if (loadedRuntime === family && window.spine) return Promise.resolve();
    if (loadedRuntime && loadedRuntime !== family) {
      location.reload();
      return new Promise(function () {});
    }
    if (runtimePromise) return runtimePromise;

    runtimePromise = new Promise(function (resolve, reject) {
      const stylesheet = document.createElement('link');
      stylesheet.rel = 'stylesheet';
      stylesheet.href = runtimeFiles[family].replace('.js', '.css');
      document.head.appendChild(stylesheet);

      const script = document.createElement('script');
      script.src = runtimeFiles[family];
      script.onload = function () {
        loadedRuntime = family;
        resolve();
      };
      script.onerror = function () {
        runtimePromise = null;
        reject(new Error(`The bundled Spine ${family} runtime could not be loaded`));
      };
      document.head.appendChild(script);
    });
    return runtimePromise;
  }

  function availableAnimations(currentPlayer) {
    return currentPlayer.animationState.data.skeletonData.animations.map(function (animation) {
      return animation.name;
    });
  }

  function applyControlConfiguration(configuration) {
    if (!controller) return;
    controller.configure(configuration);
    controller.setYapping(Boolean(configuration.yapEnabled && configuration.yapActive));
  }

  async function configure(configuration) {
    latestConfiguration = configuration;
    const generation = ++configureGeneration;
    const coreUrl = toFileUrl(configuration.corePath);
    const atlasUrl = toFileUrl(configuration.atlasPath);
    if (!coreUrl || !atlasUrl) {
      showStatus('Choose both a Spine skeleton and atlas file in Source Properties.');
      return;
    }

    try {
      const family = configuration.runtime === 'auto' ? await detectRuntime(coreUrl) : configuration.runtime;
      if (!runtimeFiles[family]) throw new Error(`Unsupported Spine runtime selection: ${family}`);
      await loadRuntime(family);
      if (generation !== configureGeneration) return;

      const fingerprint = JSON.stringify([coreUrl, atlasUrl, family]);
      if (fingerprint === assetFingerprint && player) {
        applyControlConfiguration(configuration);
        return;
      }
      assetFingerprint = fingerprint;
      if (player) player.dispose();
      controller = null;
      container.replaceChildren();
      showStatus('Loading Spine character…');

      const options = {
        atlasUrl,
        alpha: true,
        backgroundColor: '00000000',
        premultipliedAlpha: true,
        preserveDrawingBuffer: false,
        showControls: false,
        showLoading: true,
        success: function (loadedPlayer) {
          player = loadedPlayer;
          const animations = availableAnimations(loadedPlayer);
          controller = new SpineStateController(loadedPlayer, animations, latestConfiguration);
          controller.start();
          applyControlConfiguration(latestConfiguration);
          hideStatus();
        },
        error: function (_failedPlayer, message) {
          showStatus(`Could not load Spine character:\n${String(message)}`);
        }
      };
      options[extension(coreUrl) === 'json' ? 'jsonUrl' : 'skelUrl'] = coreUrl;
      player = new spine.SpinePlayer(container, options);
    } catch (error) {
      showStatus(`Could not configure Spine character:\n${error.message || error}`);
    }
  }

  window.addEventListener('obsSpineConfigure', function (event) {
    configure(event.detail || {});
  });

  window.addEventListener('obsSpineYap', function (event) {
    if (controller) controller.setYapping(Boolean(event.detail && event.detail.active));
  });

  window.addEventListener('obsSpineTrigger', function (event) {
    const detail = event.detail || {};
    if (controller) controller.trigger(detail.animation, detail.loop);
  });

  window.addEventListener('obsSpineReset', function () {
    if (controller) controller.reset();
  });
})();
