# Native renderer, stage 2: controls

Branch: `feature/native-renderer`. Builds on [stage 1](2026-10-27-native-renderer-41-design.md). Status: approved direction (the user asked to carry on through all stages without per-stage approval).

## Goal

With **Renderer: Native**, every control the Browser renderer has works the same way:
- the emotion state machine and its eight emotion slots;
- emotion hotkeys and the reset hotkey;
- yap mode, including the derived mouth-only overlay for rigs without `talk_start`;
- cursor eye tracking.

Browser mode is unchanged. The stage 1 log notices ("… is not supported by the native renderer yet") go away.

## Source of truth

The JS modules are the specification, and their tests become C tests with the same cases:

| JS | C (new) | Test |
|---|---|---|
| `data/player/state-controller.js` | `src/native/state-machine.{c,h}` | `tests/state-machine-test.c` ← `tests/state-controller.test.js` |
| `data/player/mouth-overlay.js` | `src/native/mouth-plan.{c,h}` | `tests/mouth-plan-test.c` ← `tests/mouth-overlay.test.js` |
| `data/player/eye-tracker.js` | `src/native/eye-tracker.{c,h}` | `tests/eye-tracker-test.c` ← `tests/eye-tracker.test.js` |

All three are version-neutral. They never include Spine headers, so they can be reused by the 4.0 and 3.7 backends in stage 4. Each one takes a small table of callbacks, which is what makes it testable with fakes, exactly as the JS tests do.

## Behaviour to preserve (from the JS)

### State machine

These are the `SpineStateController` rules.
- **Names** resolve exactly, then case-insensitively.
- **Default animation:**
  - It falls back to the first animation when the configured one is absent.
  - It starts as `idle`, and `configure` runs once at construction, so a case-only difference (configured `idle`, actual `IDLE`) causes one reset.
- **Reset** puts the default animation on track 0, looping.
- **`trigger(name, loop)`:**
  - It returns false when the state machine is disabled or the name doesn't resolve.
  - Otherwise it sets track 0. When `loop` is false, it also queues the default animation, looping, with delay 0.
- **`configure`:**
  - It resets when the default animation changes, or when the state machine goes from enabled to disabled.
  - It re-plans yap only when `{yapAnimation, mouthSlots}` changes. `yapAnimation` falls back to the previous value, then to `auto`.
  - If yap is active when the plan changes, the mouth track restarts with the new plan.
- **`setYapping(active, force)`:**
  - "Active" means active and a plan exists; nothing happens when that is unchanged, unless forced.
  - When it turns off: an empty animation on track 1, with mix 0.08.
  - With an overlay plan: track 1 plays the overlay, looping, with `animationStart`/`animationEnd` set to the talk window.
  - Otherwise: track 1 plays the named animation, looping.
- **Without a planner,** `auto` means `talk_start`. The renderer always provides a planner (mouth-plan).

### Mouth plan

These are the `SpineMouthOverlay.plan` rules: the talk-attachment pattern, mouth slots, the mouth-bone exclusivity rules, the auto source score `talkKeys × 1000 + mouthTimelines`, the talk window, and the mode choice. The result names slots and bones in index order, and reports any missing configured slots.

- **Talk-attachment pattern:** "talk" with no letter on either side, case-insensitive, across every skin.
- **Mouth slots:** the configured names (matched case-insensitively), or else every slot on a bone that carries a talking attachment.
- **Mouth bones:** a bone counts if all of its slots are mouth slots. Child bones are added when they also qualify. A bone is dropped if any descendant doesn't qualify.
- **Talk window:** from the first talking key to the key that ends the last talking stretch. If that is empty, the whole animation.
- **Mode:**
  - no source → none;
  - no mouth slots, no mouth timelines, or a mouth-only source without configured slots → play the source unchanged;
  - otherwise → overlay.

### Eye tracker

These are the `SpineEyeTracker` rules.
- **Slot lists** are split on whitespace or commas. The defaults are `f_eye_id_l,f_eye_hi_l3,f_eye_hi_l2` and `f_eye_id_r,f_eye_hi_r3,f_eye_hi_r2`.
- **Bones:** slots resolve to unique bones, left first, then right bones not already listed. Missing slots produce a warning listing them, and the log names the bones.
- **Limits:** `maxX` defaults to 6, `maxY` to 4, both clamped to 0–100. Smoothing defaults to 90 ms, clamped to 0–2000.
- **Target** is clamped to [−1, 1], and is 0 when tracking is disabled.
- **Before each frame,** the previous offsets are subtracted from the bones' local x and y.
- **After the animation update:**
  - Ease toward the target with `blend = smoothing == 0 ? 1 : 1 − exp(−dt·1000 / smoothing)` (dt defaults to 1/60).
  - The world offset is `(currentX × maxX, currentY × maxY)`.
  - Convert it into each bone's parent space with the inverse of the parent's 2×2 matrix. A bone with no parent gets the offset as is; a determinant below 1e−6 gives 0.
  - Add the result to the bone's local x and y, then update world transforms once.
- **Configure** re-resolves (and restores the offsets first) only when the slot lists change.

## Architecture

### Backend interface additions (`spine-backend.h`)

**Read-only description of the skeleton data,** built once per asset by the backend:

```c
enum spine_timeline_kind { SPINE_TIMELINE_OTHER, SPINE_TIMELINE_SLOT, SPINE_TIMELINE_BONE, SPINE_TIMELINE_EVENT };
struct spine_desc_timeline { enum spine_timeline_kind kind; int target; int frame_count;
                             const float *frame_times; const char *const *attachment_names; /* NULL unless an attachment timeline */ };
struct spine_desc_animation { const char *name; float duration; int timeline_count; const struct spine_desc_timeline *timelines; };
struct spine_desc_slot { const char *name; int bone; };
struct spine_desc_bone { const char *name; int parent; };
struct spine_desc_skin_attachment { int slot; const char *name; };
struct spine_skeleton_desc { slots, bones, animations, skin_attachments (+ counts) };
const struct spine_skeleton_desc *(*describe)(const struct spine_asset *asset);
```

Timeline kinds for 4.1:
- **Slot:** attachment, RGBA, RGB, alpha, RGBA2, RGB2, deform and sequence timelines.
- **Bone:** rotate, translate, translate X/Y, scale, scale X/Y, shear and shear X/Y timelines.
- **Event:** event timelines.
- **Other:** everything else.

`frame_times[i]` is the time of key `i`.

**Animation control, for the state machine:**
- `add_animation(instance, track, name, loop, delay)`
- `set_empty_animation(instance, track, mix)`
- `set_overlay(instance, track, source_animation, timeline_indices, count, start, end, loop)`. This builds an animation named `"<source> (mouth)"` that **shares** the source's timeline objects, has the source's duration, and plays from `start` to `end`.
  - The instance owns every overlay it creates, and frees them only when the instance is freed: the animation state may still be mixing out of an old overlay.
  - Freeing releases only the overlay's own memory (name, arrays, struct), never the shared timelines, which avoids freeing them twice.

**Bones, for the eye tracker:**
- `find_slot_bone(instance, slot_name)`, exact match like `findSlot`; returns a bone index or −1.
- `bone_parent_matrix(instance, bone, float m[4])`, the parent's a, b, c, d; returns false when the bone has no parent.
- `bone_translate(instance, bone, dx, dy)`, adds to the local x and y.
- `update_world_transform(instance)`.

### Version-neutral modules

- **`mouth-plan`:** `bool mouth_plan_build(const struct spine_skeleton_desc *, const char *yap_animation, const char *mouth_slots, struct mouth_plan *out)`, plus `mouth_plan_free`. The plan holds the mode (none / animation / overlay), the source animation index, the selected timeline indices, the start and end, the slot and bone names, and the missing slot names. It allocates with `malloc`, and the caller frees the plan.
- **`state-machine`:** the controller over `struct state_machine_ops`, which provides set, add, empty and set_overlay callbacks, plus a planner callback.
- **`eye-tracker`:** the tracker over `struct eye_tracker_ops`, which provides find_slot_bone, parent_matrix, translate, update_world, bone_name and log callbacks.

### Renderer (`native-renderer`)

**Control inputs.** These are thread-safe; each copies under the renderer mutex.
- `native_renderer_set_controls(r, const struct native_controls *)`. The struct holds `state_enabled`, `yap_enabled`, `yap_animation`, `mouth_slots`, `eye_enabled`, `eye_left`, `eye_right`, `eye_max_x`, `eye_max_y` and `eye_smoothing_ms`.
- `native_renderer_set_yapping(r, active)`, `native_renderer_set_cursor(r, x, y)`, `native_renderer_trigger(r, name, loop)` and `native_renderer_reset(r)`.
- Triggers and resets go into a FIFO of 8 entries; when it's full, the oldest is dropped. Yap and the cursor keep only their latest value.

**Graphics thread.** The tick processes control inputs even while the source is hidden. While it is showing, each frame runs:
1. the eye tracker's before-frame step;
2. the backend update;
3. the eye tracker's after-update step;
4. marking the frame for redraw.

**On a new instance:** create the state machine (with the asset's animation names and the mouth-plan planner) and the eye tracker, apply the current controls and default animation, then reset.

**Framing:** the default animation now goes through the state machine's `configure`, and framing uses the state machine's resolved default animation. Eye offsets are restored before bounds are sampled, so reframing can't make them drift.

**Logging** matches the JS messages: the yap plan, missing mouth slots, the eye bones or missing slots, and a missing default animation.

### Source (`spine-source.c`)

- **When native is active:**
  - the yap gate, cursor samples, emotion and reset hotkeys, and configuration all go to the native renderer instead of browser events;
  - cursor sampling runs again.
- **Locking:** the calls hold `renderer_mutex` while they use `context->native`.
- The "not supported" notices are removed.

## Testing

- **C unit tests:**
  - `mouth-plan`, `state-machine` and `eye-tracker`, with every JS test case ported;
  - backend tests on Spineboy for `describe` (counts, bone parents, timeline kinds) and `set_overlay` (sharing timelines, the window, no double free);
  - extending the counting-allocator leak test to cover overlays.
- **The JS tests stay** and still pass.
- **In OBS** (portable install, MekamiShifty, Native), checked through obs-websocket:
  - trigger an emotion hotkey (`TriggerHotkeyByName`);
  - check that the logs show the yap plan and eye bones;
  - toggle yap (lower the yap threshold so desktop audio or a test input passes it, or check the planner result in the log);
  - screenshot to see the pose change;
  - shut down with 0 leaks.

## Out of scope

FXAA and supersampling (stage 3), and the 4.0 and 3.7 backends (stage 4).
