(function (root, factory) {
  const Controller = factory();
  if (typeof module === 'object' && module.exports) module.exports = Controller;
  root.SpineStateController = Controller;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const YAP_AUTO = 'auto';
  const DEDICATED_YAP_ANIMATION = 'talk_start';

  class SpineStateController {
    /*
     * yapPlanner(request) decides what the mouth track plays for { yapAnimation, mouthSlots } and returns
     * { mode: 'animation', name }, { mode: 'overlay', animation, start, end }, or a falsy/modeless result.
     * Without a planner, yap mode plays the named animation (or talk_start for 'auto') unchanged.
     */
    constructor(player, animations, options, yapPlanner) {
      this.player = player;
      this.animations = animations.slice();
      this.yapPlanner = typeof yapPlanner === 'function' ? yapPlanner : null;
      this.defaultAnimation = 'idle';
      this.yapRequest = null;
      this.yapPlan = null;
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

    planYap(request) {
      if (this.yapPlanner) return this.yapPlanner(request);
      const requested = request.yapAnimation.toLowerCase() === YAP_AUTO ? DEDICATED_YAP_ANIMATION : request.yapAnimation;
      const name = this.resolve(requested, null);
      return name ? { mode: 'animation', name } : null;
    }

    configure(options) {
      const wasEnabled = this.enabled;
      const oldDefault = this.defaultAnimation;
      this.enabled = options.stateEnabled !== false;
      this.defaultAnimation = this.resolve(options.defaultAnimation || this.defaultAnimation, this.animations[0]);

      const request = {
        yapAnimation: String(options.yapAnimation || (this.yapRequest && this.yapRequest.yapAnimation) || YAP_AUTO),
        mouthSlots: String(options.mouthSlots || '')
      };
      const requestKey = JSON.stringify(request);
      const yapChanged = requestKey !== JSON.stringify(this.yapRequest);
      if (yapChanged) {
        this.yapRequest = request;
        const plan = this.planYap(request);
        this.yapPlan = plan && plan.mode ? plan : null;
      }

      if (oldDefault !== this.defaultAnimation || (wasEnabled && !this.enabled)) this.reset();
      if (this.yapping && yapChanged) this.setYapping(true, true);
    }

    get yapAnimation() {
      if (!this.yapPlan) return null;
      return this.yapPlan.mode === 'overlay' ? this.yapPlan.animation.name : this.yapPlan.name;
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
      const plan = this.yapPlan;
      const next = Boolean(active && plan);
      if (!force && next === this.yapping) return false;
      this.yapping = next;
      const state = this.player.animationState;
      if (!next) {
        state.setEmptyAnimation(1, 0.08);
      } else if (plan.mode === 'overlay') {
        const entry = state.setAnimationWith(1, plan.animation, true);
        if (entry) {
          entry.animationStart = plan.start;
          entry.animationEnd = plan.end;
        }
      } else {
        state.setAnimation(1, plan.name, true);
      }
      return true;
    }
  }

  SpineStateController.YAP_AUTO = YAP_AUTO;
  return SpineStateController;
});
