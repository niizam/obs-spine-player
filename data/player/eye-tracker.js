(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpineEyeTracker = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const DEFAULT_LEFT_SLOTS = 'f_eye_id_l,f_eye_hi_l3,f_eye_hi_l2';
  const DEFAULT_RIGHT_SLOTS = 'f_eye_id_r,f_eye_hi_r3,f_eye_hi_r2';

  function clamp(value, minimum, maximum) {
    return Math.max(minimum, Math.min(maximum, value));
  }

  function numeric(value, fallback, minimum, maximum) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? clamp(parsed, minimum, maximum) : fallback;
  }

  function slotNames(value, fallback) {
    const source = Array.isArray(value) ? value : String(value || fallback).split(/[\s,]+/);
    return source.map(function (name) { return String(name).trim(); }).filter(Boolean);
  }

  function uniqueBones(skeleton, names, missing) {
    const bones = [];
    for (const name of names) {
      const slot = skeleton.findSlot(name);
      if (!slot) {
        missing.push(name);
        continue;
      }
      if (!bones.includes(slot.bone)) bones.push(slot.bone);
    }
    return bones;
  }

  function parentLocalOffset(bone, worldX, worldY) {
    if (!bone.parent) return { x: worldX, y: worldY };
    const parent = bone.parent;
    const determinant = parent.a * parent.d - parent.b * parent.c;
    if (Math.abs(determinant) < 0.000001) return { x: 0, y: 0 };
    return {
      x: (worldX * parent.d - worldY * parent.b) / determinant,
      y: (worldY * parent.a - worldX * parent.c) / determinant
    };
  }

  class SpineEyeTracker {
    constructor(player, configuration, logger) {
      this.player = player;
      this.logger = typeof logger === 'function' ? logger : function () {};
      this.targetX = 0;
      this.targetY = 0;
      this.currentX = 0;
      this.currentY = 0;
      this.bones = [];
      this.offsets = [];
      this.configurationFingerprint = null;
      this.configure(configuration || {});
    }

    configure(configuration) {
      const leftSlots = slotNames(configuration.eyeLeftSlots, DEFAULT_LEFT_SLOTS);
      const rightSlots = slotNames(configuration.eyeRightSlots, DEFAULT_RIGHT_SLOTS);
      const fingerprint = JSON.stringify([leftSlots, rightSlots]);
      this.enabled = Boolean(configuration.eyeTrackingEnabled);
      this.maxX = numeric(configuration.eyeMaxX, 6, 0, 100);
      this.maxY = numeric(configuration.eyeMaxY, 4, 0, 100);
      this.smoothingMs = numeric(configuration.eyeSmoothingMs, 90, 0, 2000);

      if (fingerprint !== this.configurationFingerprint) {
        this.restoreOffsets();
        this.configurationFingerprint = fingerprint;
        this.resolve(leftSlots, rightSlots);
      }
      if (!this.enabled) {
        this.targetX = 0;
        this.targetY = 0;
      }
    }

    resolve(leftSlots, rightSlots) {
      const skeleton = this.player && this.player.skeleton;
      if (!skeleton) {
        this.bones = [];
        this.offsets = [];
        return;
      }

      const missing = [];
      const leftBones = uniqueBones(skeleton, leftSlots, missing);
      const rightBones = uniqueBones(skeleton, rightSlots, missing);
      this.bones = leftBones.concat(rightBones.filter(function (bone) { return !leftBones.includes(bone); }));
      this.offsets = this.bones.map(function () { return { x: 0, y: 0 }; });

      if (missing.length) this.logger('warn', `Eye tracking slots not found: ${missing.join(', ')}`);
      if (this.bones.length) {
        const boneNames = this.bones.map(function (bone) { return bone.data ? bone.data.name : 'unnamed'; });
        this.logger('info', `Eye tracking controls ${this.bones.length} bone(s): ${boneNames.join(', ')}`);
      } else if (this.enabled) {
        this.logger('warn', 'Eye tracking is enabled, but none of the configured slots exist in this skeleton');
      }
    }

    setTarget(x, y) {
      this.targetX = clamp(Number(x) || 0, -1, 1);
      this.targetY = clamp(Number(y) || 0, -1, 1);
    }

    restoreOffsets() {
      for (let index = 0; index < this.bones.length; index++) {
        this.bones[index].x -= this.offsets[index].x;
        this.bones[index].y -= this.offsets[index].y;
        this.offsets[index].x = 0;
        this.offsets[index].y = 0;
      }
    }

    beforeFrame() {
      this.restoreOffsets();
    }

    afterUpdate(deltaSeconds) {
      if (!this.bones.length) return;
      const targetX = this.enabled ? this.targetX : 0;
      const targetY = this.enabled ? this.targetY : 0;
      const seconds = Number(deltaSeconds) > 0 ? Number(deltaSeconds) : 1 / 60;
      const blend = this.smoothingMs === 0 ? 1 : 1 - Math.exp((-seconds * 1000) / this.smoothingMs);
      this.currentX += (targetX - this.currentX) * blend;
      this.currentY += (targetY - this.currentY) * blend;

      const worldX = this.currentX * this.maxX;
      const worldY = this.currentY * this.maxY;
      for (let index = 0; index < this.bones.length; index++) {
        const offset = parentLocalOffset(this.bones[index], worldX, worldY);
        this.bones[index].x += offset.x;
        this.bones[index].y += offset.y;
        this.offsets[index] = offset;
      }
      this.player.skeleton.updateWorldTransform();
    }

    dispose() {
      this.restoreOffsets();
      this.bones = [];
      this.offsets = [];
      this.player = null;
    }
  }

  SpineEyeTracker.DEFAULT_LEFT_SLOTS = DEFAULT_LEFT_SLOTS;
  SpineEyeTracker.DEFAULT_RIGHT_SLOTS = DEFAULT_RIGHT_SLOTS;
  return SpineEyeTracker;
});
