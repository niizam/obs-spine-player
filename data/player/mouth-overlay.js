/*
 * Chooses what yap mode plays on the mouth track.
 *
 * Rigs with a dedicated mouth animation (talk_start) play it unchanged. Rigs that only animate speech inside a
 * larger clip, such as CounterSide illustrations whose TOUCH clip swaps in a "mouth_talk" attachment and pumps
 * the mouth bone, get a mouth-only animation built from that clip's own timelines. Only slots on the mouth's
 * bones and those bones are kept, so the base animation keeps control of the body, eyes, and expression.
 * The module works with Spine 3.7, 4.0, and 4.1 skeleton data and contains no OBS or DOM code.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.SpineMouthOverlay = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  const AUTO = 'auto';
  const DEDICATED_ANIMATION = 'talk_start';
  const TALK_ATTACHMENT = /(^|[^a-z])talk([^a-z]|$)/i;

  function slotList(value) {
    const source = Array.isArray(value) ? value : String(value || '').split(/[\s,]+/);
    return source.map(function (name) { return String(name).trim(); }).filter(Boolean);
  }

  function findAnimation(skeletonData, name) {
    if (!name) return null;
    const folded = String(name).toLowerCase();
    return (
      skeletonData.animations.find(function (animation) { return animation.name === name; }) ||
      skeletonData.animations.find(function (animation) { return animation.name.toLowerCase() === folded; }) ||
      null
    );
  }

  function slotIndexByName(skeletonData, name) {
    const folded = name.toLowerCase();
    return skeletonData.slots.findIndex(function (slot) { return slot.name.toLowerCase() === folded; });
  }

  /* slot index -> attachment names that look like a speaking mouth, across every skin. */
  function talkAttachments(skeletonData) {
    const result = new Map();
    for (const skin of skeletonData.skins) {
      if (!skin || !skin.attachments) continue;
      skin.attachments.forEach(function (entries, slotIndex) {
        if (!entries) return;
        for (const name of Object.keys(entries)) {
          if (!TALK_ATTACHMENT.test(name)) continue;
          if (!result.has(slotIndex)) result.set(slotIndex, new Set());
          result.get(slotIndex).add(name);
        }
      });
    }
    return result;
  }

  function slotBoneIndex(slot) {
    return slot.boneData.index;
  }

  /* Mouth slots are every slot sharing a bone with a talking-mouth slot, so the closed and open mouths swap together. */
  function mouthSlots(skeletonData, configured, talk) {
    const missing = [];
    let seeds;
    if (configured.length) {
      seeds = [];
      for (const name of configured) {
        const index = slotIndexByName(skeletonData, name);
        if (index < 0) missing.push(name);
        else seeds.push(index);
      }
      return { slots: new Set(seeds), missing };
    }

    seeds = Array.from(talk.keys());
    const bones = new Set(seeds.map(function (index) { return slotBoneIndex(skeletonData.slots[index]); }));
    const slots = new Set();
    skeletonData.slots.forEach(function (slot, index) {
      if (bones.has(slotBoneIndex(slot))) slots.add(index);
    });
    return { slots, missing };
  }

  /* Bones are animated only when every slot they carry, and every descendant bone's slots, belong to the mouth. */
  function mouthBones(skeletonData, slots) {
    const slotsByBone = new Map();
    skeletonData.slots.forEach(function (slot, index) {
      const bone = slotBoneIndex(slot);
      if (!slotsByBone.has(bone)) slotsByBone.set(bone, []);
      slotsByBone.get(bone).push(index);
    });
    const exclusive = function (boneIndex) {
      return (slotsByBone.get(boneIndex) || []).every(function (slot) { return slots.has(slot); });
    };

    const bones = new Set();
    for (const slot of slots) {
      const bone = slotBoneIndex(skeletonData.slots[slot]);
      if (exclusive(bone)) bones.add(bone);
    }
    for (const bone of skeletonData.bones) {
      if (bone.parent && bones.has(bone.parent.index) && exclusive(bone.index)) bones.add(bone.index);
    }
    for (const bone of Array.from(bones)) {
      const descendants = skeletonData.bones.filter(function (candidate) {
        let parent = candidate.parent;
        while (parent && parent.index !== bone) parent = parent.parent;
        return Boolean(parent);
      });
      if (!descendants.every(function (candidate) { return exclusive(candidate.index); })) bones.delete(bone);
    }
    return bones;
  }

  function isEventTimeline(timeline) {
    return Array.isArray(timeline.events);
  }

  function belongsToMouth(timeline, slots, bones) {
    if (typeof timeline.slotIndex === 'number') return slots.has(timeline.slotIndex);
    if (typeof timeline.boneIndex === 'number') return bones.has(timeline.boneIndex);
    return false;
  }

  function attachmentKeys(timeline) {
    return timeline && timeline.attachmentNames && timeline.frames ? timeline.attachmentNames : null;
  }

  function talkKeyCount(animation, talk) {
    let count = 0;
    for (const timeline of animation.timelines) {
      const names = attachmentKeys(timeline);
      const talking = names && talk.get(timeline.slotIndex);
      if (!talking) continue;
      for (const name of names) if (talking.has(name)) count++;
    }
    return count;
  }

  function mouthTimelineCount(animation, slots, bones) {
    return animation.timelines.filter(function (timeline) { return belongsToMouth(timeline, slots, bones); }).length;
  }

  /* From the first talking-mouth key to the key that ends the last talking stretch. */
  function talkWindow(animation, talk) {
    let start = Number.POSITIVE_INFINITY;
    let end = Number.NEGATIVE_INFINITY;
    for (const timeline of animation.timelines) {
      const names = attachmentKeys(timeline);
      const talking = names && talk.get(timeline.slotIndex);
      if (!talking) continue;
      let last = -1;
      for (let frame = 0; frame < names.length; frame++) {
        if (!talking.has(names[frame])) continue;
        start = Math.min(start, timeline.frames[frame]);
        last = frame;
      }
      if (last >= 0) end = Math.max(end, last + 1 < names.length ? timeline.frames[last + 1] : animation.duration);
    }
    if (!Number.isFinite(start) || !(end > start)) return { start: 0, end: animation.duration };
    return { start, end };
  }

  function names(collection, indices) {
    return Array.from(indices)
      .sort(function (left, right) { return left - right; })
      .map(function (index) { return collection[index].name; });
  }

  /*
   * request.yapAnimation: 'auto' (talk_start, otherwise the clip with the most talking-mouth keys) or a name.
   * request.mouthSlots: optional comma-separated slots that override talking-mouth detection.
   * Returns { mode: 'animation', name }, { mode: 'overlay', animation, start, end, ... }, or null.
   */
  function plan(skeletonData, AnimationClass, request) {
    const options = request || {};
    const requested = String(options.yapAnimation || AUTO).trim();
    const configured = slotList(options.mouthSlots);
    const talk = talkAttachments(skeletonData);
    const mouth = mouthSlots(skeletonData, configured, talk);
    const bones = mouthBones(skeletonData, mouth.slots);

    let source = null;
    if (requested.toLowerCase() === AUTO) {
      source = findAnimation(skeletonData, DEDICATED_ANIMATION);
      if (!source && mouth.slots.size) {
        let best = 0;
        for (const animation of skeletonData.animations) {
          const score = talkKeyCount(animation, talk) * 1000 + mouthTimelineCount(animation, mouth.slots, bones);
          if (score > best) {
            best = score;
            source = animation;
          }
        }
      }
    } else {
      source = findAnimation(skeletonData, requested);
    }
    if (!source) return { mode: null, requested, missingSlots: mouth.missing };

    const mouthTimelines = source.timelines.filter(function (timeline) {
      return belongsToMouth(timeline, mouth.slots, bones);
    });
    const mouthOnly = source.timelines.every(function (timeline) {
      return isEventTimeline(timeline) || belongsToMouth(timeline, mouth.slots, bones);
    });
    if (!mouth.slots.size || !mouthTimelines.length || (mouthOnly && !configured.length)) {
      return { mode: 'animation', name: source.name, requested, missingSlots: mouth.missing };
    }

    const window = talkWindow(source, talk);
    return {
      mode: 'overlay',
      requested,
      source: source.name,
      animation: new AnimationClass(`${source.name} (mouth)`, mouthTimelines, source.duration),
      start: window.start,
      end: window.end,
      slots: names(skeletonData.slots, mouth.slots),
      bones: names(skeletonData.bones, bones),
      missingSlots: mouth.missing
    };
  }

  return { plan, talkAttachments, mouthSlots, mouthBones, talkWindow, AUTO, DEDICATED_ANIMATION };
});
