(function (root, factory) {
  const Controller = factory();
  if (typeof module === 'object' && module.exports) module.exports = Controller;
  root.SpineStateController = Controller;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  class SpineStateController {
    constructor(player, animations, options) {
      this.player = player;
      this.animations = animations.slice();
      this.defaultAnimation = 'idle';
      this.yapAnimation = 'talk_start';
      this.enabled = true;
      this.yapping = false;
      this.configure(options || {});
    }

    resolve(requested, fallback) {
      if (this.animations.includes(requested)) return requested;
      const folded = String(requested || '').toLowerCase();
      const match = this.animations.find(function (name) { return name.toLowerCase() === folded; });
      return match || fallback || null;
    }

    configure(options) {
      const wasEnabled = this.enabled;
      const oldDefault = this.defaultAnimation;
      const oldYap = this.yapAnimation;
      this.enabled = options.stateEnabled !== false;
      this.defaultAnimation = this.resolve(options.defaultAnimation || this.defaultAnimation, this.animations[0]);
      this.yapAnimation = this.resolve(options.yapAnimation || this.yapAnimation, null);
      if (oldDefault !== this.defaultAnimation || (wasEnabled && !this.enabled)) this.reset();
      if (this.yapping && oldYap !== this.yapAnimation) this.setYapping(true, true);
    }

    start() {
      this.reset();
    }

    reset() {
      if (!this.defaultAnimation) return false;
      this.player.animationState.setAnimation(0, this.defaultAnimation, true);
      return true;
    }

    trigger(animation, loop) {
      if (!this.enabled) return false;
      const resolved = this.resolve(animation, null);
      if (!resolved) return false;
      this.player.animationState.setAnimation(0, resolved, Boolean(loop));
      if (!loop && this.defaultAnimation) {
        this.player.animationState.addAnimation(0, this.defaultAnimation, true, 0);
      }
      return true;
    }

    setYapping(active, force) {
      const next = Boolean(active && this.yapAnimation);
      if (!force && next === this.yapping) return false;
      this.yapping = next;
      if (next) {
        this.player.animationState.setAnimation(1, this.yapAnimation, true);
      } else {
        this.player.animationState.setEmptyAnimation(1, 0.08);
      }
      return true;
    }
  }

  return SpineStateController;
});

