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
  let lastDiagnosticFingerprint = null;
  let lastErrorMessage = null;

  function errorText(error) {
    if (error && error.stack) return String(error.stack);
    if (error && error.message) return String(error.message);
    return String(error);
  }

  function log(level, message) {
    const method = typeof console[level] === 'function' ? console[level] : console.log;
    method.call(console, `[OBS Spine Player] ${message}`);
  }

  function reportError(summary, error) {
    const detail = errorText(error);
    const message = `${summary}: ${detail}`;
    showStatus(`${summary}:\n${detail}`);
    if (message !== lastErrorMessage) {
      log('error', message);
      lastErrorMessage = message;
    }
  }

  function showStatus(message) {
    status.textContent = message;
    status.classList.add('visible');
  }

  function hideStatus() {
    status.classList.remove('visible');
  }

  async function fetchAsset(url, label) {
    let response;
    try {
      response = await fetch(url);
    } catch (error) {
      throw new Error(`${label} request failed for ${url}: ${errorText(error)}`);
    }
    if (!response.ok) throw new Error(`${label} request returned HTTP ${response.status} for ${url}`);
    return response;
  }

  async function detectRuntime(coreUrl, shouldLog) {
    const response = await fetchAsset(coreUrl, 'Skeleton');
    let version;
    if (SpinePlayerOptions.extension(coreUrl) === 'json') {
      version = SpineVersionDetector.fromJson(await response.text());
    } else {
      version = SpineVersionDetector.fromBinary(await response.arrayBuffer());
    }
    const family = SpineVersionDetector.runtimeFamily(version);
    if (shouldLog) log('info', `Detected Spine ${version}; using the bundled ${family} runtime`);
    return family;
  }

  function loadRuntime(family) {
    if (loadedRuntime === family && window.spine) return Promise.resolve();
    if (loadedRuntime && loadedRuntime !== family) {
      log('info', `Runtime changed from ${loadedRuntime} to ${family}; reloading the browser page`);
      location.reload();
      return new Promise(function () {});
    }
    if (runtimePromise) return runtimePromise;

    runtimePromise = new Promise(function (resolve, reject) {
      log('info', `Loading bundled Spine ${family} runtime from ${runtimeFiles[family]}`);
      const stylesheet = document.createElement('link');
      stylesheet.rel = 'stylesheet';
      stylesheet.href = runtimeFiles[family].replace('.js', '.css');
      document.head.appendChild(stylesheet);

      const script = document.createElement('script');
      script.src = runtimeFiles[family];
      script.onload = function () {
        loadedRuntime = family;
        log('info', `Bundled Spine ${family} runtime loaded successfully`);
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
    const coreUrl = SpineAssetUrl.fromPath(configuration.corePath);
    const atlasUrl = SpineAssetUrl.fromPath(configuration.atlasPath);
    const diagnosticFingerprint = JSON.stringify([
      configuration.corePath,
      configuration.atlasPath,
      configuration.runtime,
      configuration.defaultAnimation
    ]);
    const shouldLogAttempt = diagnosticFingerprint !== lastDiagnosticFingerprint;
    lastDiagnosticFingerprint = diagnosticFingerprint;
    if (!coreUrl || !atlasUrl) {
      showStatus('Choose both a Spine skeleton and atlas file in Source Properties.');
      if (shouldLogAttempt) log('warn', 'Waiting for both a skeleton file and an atlas file');
      return;
    }

    const fingerprint = JSON.stringify([coreUrl, atlasUrl, configuration.runtime]);
    if (fingerprint === assetFingerprint && player) {
      applyControlConfiguration(configuration);
      return;
    }

    try {
      if (shouldLogAttempt) {
        log(
          'info',
          `Configuring character: runtime=${configuration.runtime || 'auto'}, animation=${
            configuration.defaultAnimation || 'idle'
          }, skeleton=${coreUrl}, atlas=${atlasUrl}`
        );
      }
      const family =
        configuration.runtime === 'auto' ? await detectRuntime(coreUrl, shouldLogAttempt) : configuration.runtime;
      if (!runtimeFiles[family]) throw new Error(`Unsupported Spine runtime selection: ${family}`);
      const atlasResponse = await fetchAsset(atlasUrl, 'Atlas');
      await atlasResponse.text();
      if (configuration.runtime !== 'auto') {
        const skeletonResponse = await fetchAsset(coreUrl, 'Skeleton');
        await skeletonResponse.arrayBuffer();
      }
      if (shouldLogAttempt) log('info', 'Skeleton and atlas files are readable by OBS Browser');
      await loadRuntime(family);
      if (generation !== configureGeneration) return;

      assetFingerprint = fingerprint;
      if (player) {
        player.dispose();
        player = null;
      }
      controller = null;
      container.replaceChildren();
      showStatus('Loading Spine character…');

      const options = SpinePlayerOptions.create(configuration, coreUrl, atlasUrl, {
        success: function (loadedPlayer) {
          if (player !== loadedPlayer) return;
          player = loadedPlayer;
          const animations = availableAnimations(loadedPlayer);
          controller = new SpineStateController(loadedPlayer, animations, latestConfiguration);
          applyControlConfiguration(latestConfiguration);
          lastErrorMessage = null;
          log('info', `Character loaded with ${animations.length} animations: ${animations.join(', ')}`);
          const defaultAnimation = latestConfiguration.defaultAnimation || 'idle';
          if (!animations.includes(defaultAnimation)) {
            log('warn', `Configured default animation '${defaultAnimation}' is not present in the loaded skeleton`);
          }
          const canvas = container.querySelector('canvas');
          log(
            'info',
            `Render surface: container=${container.clientWidth}x${container.clientHeight}, canvas=${
              canvas ? `${canvas.width}x${canvas.height}` : 'missing'
            }`
          );
          hideStatus();
        },
        error: function (failedPlayer, message) {
          if (player !== failedPlayer) return;
          assetFingerprint = null;
          player = null;
          reportError('Could not load Spine character', message);
        }
      });
      player = new spine.SpinePlayer(container, options);
    } catch (error) {
      if (generation === configureGeneration) {
        assetFingerprint = null;
        player = null;
        reportError('Could not configure Spine character', error);
      }
    }
  }

  window.addEventListener('error', function (event) {
    const location = event.filename ? ` (${event.filename}:${event.lineno || 0})` : '';
    log('error', `Unhandled browser error${location}: ${event.message || 'unknown error'}`);
  });

  window.addEventListener('unhandledrejection', function (event) {
    log('error', `Unhandled browser promise rejection: ${errorText(event.reason)}`);
  });

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

  log('info', `Player page initialized at ${location.href}`);
})();
