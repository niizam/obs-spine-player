/*
 * Minimal Spine 3.7 player for the OBS renderer.
 *
 * The spine-ts 3.7 web player only loads JSON, owns track 0 for its timeline UI, and has no frame hooks. This
 * adapter drives the official 3.7 spine-webgl runtime directly and exposes the subset of the 4.x SpinePlayer
 * surface the renderer uses: skelUrl/jsonUrl/atlasUrl, animation, premultipliedAlpha, frame/update/success/error
 * callbacks, the skeleton and animationState properties, and dispose().
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.Spine37Player = api;
  if (root.spine && root.spine.webgl) root.spine.SpinePlayer = api.SpinePlayer;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const VIEWPORT_PADDING = 0.1;
  const VIEWPORT_STEPS = 100;

  function errorText(error) {
    if (error && error.message) return String(error.message);
    return String(error);
  }

  function findAnimation(skeletonData, name) {
    if (!name) return null;
    const exact = skeletonData.findAnimation(name);
    if (exact) return exact;
    const folded = String(name).toLowerCase();
    return skeletonData.animations.find(function (animation) { return animation.name.toLowerCase() === folded; }) || null;
  }

  /* Samples the animation like the 4.x player so the initial framing covers the whole motion. */
  function animationBounds(spine, skeleton, animation) {
    const offset = new spine.Vector2();
    const size = new spine.Vector2();
    const temp = [];
    let minX = Number.POSITIVE_INFINITY;
    let minY = Number.POSITIVE_INFINITY;
    let maxX = Number.NEGATIVE_INFINITY;
    let maxY = Number.NEGATIVE_INFINITY;

    const steps = animation && animation.duration > 0 ? VIEWPORT_STEPS : 1;
    const stepTime = animation && animation.duration > 0 ? animation.duration / steps : 0;
    for (let step = 0; step < steps; step++) {
      skeleton.setToSetupPose();
      if (animation) {
        const time = step * stepTime;
        animation.apply(skeleton, time, time, false, [], 1, spine.MixBlend.setup, spine.MixDirection.in);
      }
      skeleton.updateWorldTransform();
      skeleton.getBounds(offset, size, temp);
      if (!Number.isFinite(offset.x) || !Number.isFinite(size.x) || size.x <= 0 || size.y <= 0) continue;
      minX = Math.min(minX, offset.x);
      minY = Math.min(minY, offset.y);
      maxX = Math.max(maxX, offset.x + size.x);
      maxY = Math.max(maxY, offset.y + size.y);
    }
    skeleton.setToSetupPose();
    skeleton.updateWorldTransform();

    if (!Number.isFinite(minX)) return { x: -100, y: -100, width: 200, height: 200 };
    const width = maxX - minX;
    const height = maxY - minY;
    return {
      x: minX - width * VIEWPORT_PADDING,
      y: minY - height * VIEWPORT_PADDING,
      width: width * (1 + VIEWPORT_PADDING * 2),
      height: height * (1 + VIEWPORT_PADDING * 2)
    };
  }

  class SpinePlayer {
    constructor(parent, config) {
      const spine = globalThis.spine;
      if (!spine || !spine.webgl) throw new Error('The Spine 3.7 webgl runtime is not loaded');
      if (!spine.SkeletonBinary) throw new Error('The Spine 3.7 binary reader is not loaded');
      this.spine = spine;
      this.config = config || {};
      this.skeleton = null;
      this.animationState = null;
      this.disposed = false;
      this.loaded = false;
      this.failed = false;
      this.skeletonBytes = null;
      this.skeletonText = null;
      this.time = new spine.TimeKeeper();

      this.dom = document.createElement('div');
      this.dom.className = 'spine-player';
      this.dom.style.width = '100%';
      this.dom.style.height = '100%';
      this.canvas = document.createElement('canvas');
      this.canvas.className = 'spine-player-canvas';
      this.canvas.style.display = 'block';
      this.canvas.style.width = '100%';
      this.canvas.style.height = '100%';
      this.dom.appendChild(this.canvas);
      parent.appendChild(this.dom);

      try {
        this.context = new spine.webgl.ManagedWebGLRenderingContext(this.canvas, {
          alpha: this.config.alpha !== false,
          premultipliedAlpha: true
        });
        this.sceneRenderer = new spine.webgl.SceneRenderer(this.canvas, this.context, true);
        this.assetManager = new spine.webgl.AssetManager(this.context);
      } catch (error) {
        this.fail(`WebGL is unavailable: ${errorText(error)}`);
        return;
      }

      if (!this.config.atlasUrl || !(this.config.skelUrl || this.config.jsonUrl)) {
        this.fail('Both a skeleton URL and an atlas URL are required');
        return;
      }
      this.assetManager.loadTextureAtlas(this.config.atlasUrl);
      this.loadSkeletonFile();
      this.frameRequest = requestAnimationFrame(() => this.drawFrame());
    }

    loadSkeletonFile() {
      const url = this.config.skelUrl || this.config.jsonUrl;
      fetch(url)
        .then(function (response) {
          if (!response.ok) throw new Error(`HTTP ${response.status} for ${url}`);
          return response;
        })
        .then((response) => (this.config.skelUrl ? response.arrayBuffer() : response.text()))
        .then((data) => {
          if (this.config.skelUrl) this.skeletonBytes = new Uint8Array(data);
          else this.skeletonText = data;
        })
        .catch((error) => this.fail(`Skeleton could not be loaded: ${errorText(error)}`));
    }

    fail(message) {
      if (this.failed || this.disposed) return;
      this.failed = true;
      if (typeof this.config.error === 'function') this.config.error(this, message);
    }

    loadSkeleton() {
      const spine = this.spine;
      const atlas = this.assetManager.get(this.config.atlasUrl);
      const loader = new spine.AtlasAttachmentLoader(atlas);
      const skeletonData = this.skeletonBytes
        ? new spine.SkeletonBinary(loader).readSkeletonData(this.skeletonBytes)
        : new spine.SkeletonJson(loader).readSkeletonData(this.skeletonText);
      this.skeletonBytes = null;
      this.skeletonText = null;

      this.skeleton = new spine.Skeleton(skeletonData);
      const skin = this.config.skin ? skeletonData.findSkin(this.config.skin) : skeletonData.defaultSkin;
      if (skin) this.skeleton.setSkin(skin);
      this.skeleton.setSlotsToSetupPose();

      const stateData = new spine.AnimationStateData(skeletonData);
      stateData.defaultMix = typeof this.config.defaultMix === 'number' ? this.config.defaultMix : 0.25;
      this.animationState = new spine.AnimationState(stateData);

      const animation = findAnimation(skeletonData, this.config.animation) || skeletonData.animations[0] || null;
      this.viewport = animationBounds(spine, this.skeleton, animation);
      if (animation) this.animationState.setAnimationWith(0, animation, true);
      this.loaded = true;
      if (typeof this.config.success === 'function') this.config.success(this);
    }

    drawFrame() {
      if (this.disposed) return;
      this.frameRequest = requestAnimationFrame(() => this.drawFrame());

      const gl = this.context.gl;
      this.sceneRenderer.resize(this.spine.webgl.ResizeMode.Expand);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      if (this.failed) return;

      if (!this.loaded) {
        if (!this.assetManager.isLoadingComplete() || !(this.skeletonBytes || this.skeletonText)) {
          if (this.assetManager.hasErrors()) {
            this.fail(`Assets could not be loaded: ${JSON.stringify(this.assetManager.getErrors())}`);
          }
          return;
        }
        if (this.assetManager.hasErrors()) {
          this.fail(`Assets could not be loaded: ${JSON.stringify(this.assetManager.getErrors())}`);
          return;
        }
        try {
          this.loadSkeleton();
        } catch (error) {
          this.fail(`Skeleton could not be read: ${errorText(error)}`);
          return;
        }
      }

      this.time.update();
      const delta = this.time.delta;
      if (typeof this.config.frame === 'function') this.config.frame(this, delta);
      this.animationState.update(delta);
      this.animationState.apply(this.skeleton);
      this.skeleton.updateWorldTransform();
      if (typeof this.config.update === 'function') this.config.update(this, delta);

      this.render();
    }

    render() {
      const canvas = this.canvas;
      const viewport = this.viewport;
      if (!canvas.width || !canvas.height) return;
      const scale = Math.min(canvas.width / viewport.width, canvas.height / viewport.height);
      const camera = this.sceneRenderer.camera;
      camera.zoom = 1 / scale;
      camera.position.x = viewport.x + viewport.width / 2;
      camera.position.y = viewport.y + viewport.height / 2;

      this.sceneRenderer.begin();
      this.sceneRenderer.drawSkeleton(this.skeleton, this.config.premultipliedAlpha !== false);
      this.sceneRenderer.end();
    }

    dispose() {
      if (this.disposed) return;
      this.disposed = true;
      if (this.frameRequest) cancelAnimationFrame(this.frameRequest);
      try {
        if (this.assetManager) this.assetManager.dispose();
        if (this.sceneRenderer) this.sceneRenderer.dispose();
        const lose = this.context && this.context.gl.getExtension('WEBGL_lose_context');
        if (lose) lose.loseContext();
      } catch (error) {
        console.warn(`[OBS Spine Player] Spine 3.7 player cleanup failed: ${errorText(error)}`);
      }
      this.dom.remove();
    }
  }

  return { SpinePlayer, findAnimation, animationBounds };
});
