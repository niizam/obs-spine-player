(function () {
  const container = document.getElementById('player-container');
  const status = document.getElementById('status');
  /* Spine 3.7 has no binary loader or usable web player upstream, so its runtime adds local adapters. */
  const runtimes = {
    '3.7': {
      scripts: ['runtime/3.7.94/spine-webgl.js', 'spine37-binary.js', 'spine37-player.js']
    },
    '4.0': {
      scripts: ['runtime/4.0.28/spine-player.min.js'],
      stylesheet: 'runtime/4.0.28/spine-player.min.css'
    },
    '4.1': {
      scripts: ['runtime/4.1.20/spine-player.min.js'],
      stylesheet: 'runtime/4.1.20/spine-player.min.css'
    }
  };

  let loadedRuntime = null;
  let runtimePromise = null;
  let player = null;
  let controller = null;
  let eyeTracker = null;
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

    const runtime = runtimes[family];
    log('info', `Loading bundled Spine ${family} runtime from ${runtime.scripts.join(', ')}`);
    if (runtime.stylesheet) {
      const stylesheet = document.createElement('link');
      stylesheet.rel = 'stylesheet';
      stylesheet.href = runtime.stylesheet;
      document.head.appendChild(stylesheet);
    }

    runtimePromise = runtime.scripts
      .reduce(function (previous, source) {
        return previous.then(function () { return loadScript(source, family); });
      }, Promise.resolve())
      .then(function () {
        loadedRuntime = family;
        log('info', `Bundled Spine ${family} runtime loaded successfully`);
      })
      .catch(function (error) {
        runtimePromise = null;
        throw error;
      });
    return runtimePromise;
  }

  function loadScript(source, family) {
    return new Promise(function (resolve, reject) {
      const script = document.createElement('script');
      script.src = source;
      script.onload = function () { resolve(); };
      script.onerror = function () {
        reject(new Error(`The bundled Spine ${family} runtime file ${source} could not be loaded`));
      };
      document.head.appendChild(script);
    });
  }

  function formatSeconds(value) {
    return `${Number(value).toFixed(2)}s`;
  }

  function logYapPlan(plan) {
    if (plan && plan.missingSlots && plan.missingSlots.length) {
      log('warn', `Mouth slots not found: ${plan.missingSlots.join(', ')}`);
    }
    if (plan && plan.mode === 'overlay') {
      log(
        'info',
        `Yap mode uses a mouth-only loop from '${plan.source}' (${formatSeconds(plan.start)}-${formatSeconds(
          plan.end
        )}); slots: ${plan.slots.join(', ')}; bones: ${plan.bones.join(', ') || 'none'}`
      );
    } else if (plan && plan.mode === 'animation') {
      log('info', `Yap mode plays '${plan.name}' on the mouth track`);
    } else {
      const requested = plan && plan.requested ? plan.requested : 'auto';
      log(
        'warn',
        `Yap mode has no mouth animation: '${requested}' is not in this skeleton and no talking mouth was detected`
      );
    }
  }

  function yapPlanner(loadedPlayer) {
    const skeletonData = loadedPlayer.animationState.data.skeletonData;
    return function (request) {
      const plan = SpineMouthOverlay.plan(skeletonData, spine.Animation, request);
      logYapPlan(plan);
      return plan;
    };
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
    if (eyeTracker) eyeTracker.configure(configuration);
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
      if (!runtimes[family]) throw new Error(`Unsupported Spine runtime selection: ${family}`);
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
      if (eyeTracker) {
        eyeTracker.dispose();
        eyeTracker = null;
      }
      if (player) {
        player.dispose();
        player = null;
      }
      controller = null;
      container.replaceChildren();
      showStatus('Loading Spine character…');

      const options = SpinePlayerOptions.create(configuration, coreUrl, atlasUrl, {
        frame: function () {
          if (eyeTracker) eyeTracker.beforeFrame();
        },
        update: function (currentPlayer, deltaSeconds) {
          if (eyeTracker && eyeTracker.player === currentPlayer) eyeTracker.afterUpdate(deltaSeconds);
        },
        success: function (loadedPlayer) {
          if (player !== loadedPlayer) return;
          player = loadedPlayer;
          const animations = availableAnimations(loadedPlayer);
          controller = new SpineStateController(
            loadedPlayer,
            animations,
            latestConfiguration,
            yapPlanner(loadedPlayer)
          );
          eyeTracker = new SpineEyeTracker(loadedPlayer, latestConfiguration, log);
          applyControlConfiguration(latestConfiguration);
          lastErrorMessage = null;
          log('info', `Character loaded with ${animations.length} animations: ${animations.join(', ')}`);
          const defaultAnimation = latestConfiguration.defaultAnimation || 'idle';
          if (controller.resolve(defaultAnimation, null) === null) {
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

  window.addEventListener('obsSpineCursor', function (event) {
    const detail = event.detail || {};
    if (eyeTracker) eyeTracker.setTarget(detail.x, detail.y);
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
