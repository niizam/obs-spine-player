# Native Controls (stage 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emotions, hotkeys, yap mode (including the derived mouth overlay) and eye tracking work with Renderer: Native, matching the browser renderer.

**Architecture:**
- Three version-neutral C modules port the JS controllers. Each runs over a small callback table: `mouth-plan`, `state-machine` and `eye-tracker`.
- The 4.1 backend exposes a read-only skeleton description plus animation and bone operations.
- `native-renderer` owns one state machine and one eye tracker per instance and applies control inputs on the graphics thread.
- `spine-source.c` routes yap, cursor, hotkeys and configuration to the native renderer.

**Tech Stack:** C11, libobs 31, spine-c 4.1 (vendored), CTest.

**Spec:** `docs/superpowers/specs/2026-10-27-native-controls-design.md`. The JS files it names are the behaviour reference.

## Global Constraints

- **Branch:** `feature/native-renderer`. Never merge or open a PR: the user wants all stages done first.
- **Neutral modules:** `mouth-plan`, `state-machine` and `eye-tracker` never include Spine or OBS headers. They allocate with `malloc`.
- **Backend:** new backend functions are `static` in `backend-41.c`. The prefix check must stay green.
- **Tests:** C tests use `tests/test-check.h` `CHECK` macros. The full suite runs with `bash .superpowers/sdd/<plan>/test.sh`, which builds, fails on warnings and runs ctest with the OBS DLLs on PATH. Recreate that script from the stage 1 version if it's missing.
- **Constants from the JS:**
  - empty-animation mix `0.08`;
  - one-shot queue delay `0`;
  - auto-source score `talkKeys * 1000 + mouthTimelines`;
  - eye defaults: max X `6`, max Y `4`, smoothing `90` ms, with clamps 0–100, 0–100 and 0–2000;
  - frame time `1/60` when dt ≤ 0;
  - determinant epsilon `1e-6`;
  - trigger FIFO of `8` entries.
- **Log wording matches the JS:**
  - `Yap mode uses a mouth-only loop from '<src>' (<start>s-<end>s); slots: <a, b>; bones: <a, b | none>`
  - `Yap mode plays '<name>' on the mouth track`
  - `Yap mode has no mouth animation: '<requested>' is not in this skeleton and no talking mouth was detected`
  - `Mouth slots not found: <list>`
  - `Eye tracking slots not found: <list>`
  - `Eye tracking controls <n> bone(s): <names>`
  - `Eye tracking is enabled, but none of the configured slots exist in this skeleton`

  Seconds are formatted with 2 decimals.

## Review Focus

1. **Overlay lifetime.** Replace the yap plan while the overlay is playing and mixing out. Expected: no use-after-free and no double free; overlays are freed with the instance. Covered by the Task 4 tests plus the leak test.
2. **Reframing while eye tracking is active.** Change the size or default animation with the cursor away from the centre. Expected: no drift in eye-bone position. Covered by Task 5: restore before bounds.
3. **Controls arriving before the asset finishes loading.** Expected: they are applied once the instance exists. Covered by Task 5.
4. **Switching Native → Browser → Native** while yapping or after hotkeys. Expected: no stale state, and the browser receives the full configuration. Covered by Task 6 and the OBS check.
5. **Rigs without the configured eye or mouth slots.** Expected: a warning and no crash. Covered by Tasks 1 and 3.

---

### Task 1: Skeleton description types and the mouth plan

**Files:** create `src/native/skeleton-desc.h`, `src/native/mouth-plan.{c,h}`, `tests/mouth-plan-test.c`; modify `CMakeLists.txt` (add the test and the plugin sources).

**Interfaces (produced):**
- `skeleton-desc.h`: the `spine_timeline_kind`, `spine_desc_*` and `spine_skeleton_desc` structs exactly as in the spec. The counts are `size_t`: `slot_count`, `bone_count`, `animation_count`, `skin_attachment_count`.
- `mouth-plan.h`:

```c
enum mouth_plan_mode { MOUTH_PLAN_NONE, MOUTH_PLAN_ANIMATION, MOUTH_PLAN_OVERLAY };
struct mouth_plan {
	enum mouth_plan_mode mode;
	int animation;            /* source animation index, -1 when NONE */
	int *timelines; size_t timeline_count;   /* OVERLAY: selected source timeline indices, source order */
	float start, end;         /* OVERLAY talk window */
	char **slots; size_t slot_count;        /* names, index order */
	char **bones; size_t bone_count;
	char **missing_slots; size_t missing_count;
	char requested[256];
};
bool mouth_plan_build(const struct spine_skeleton_desc *desc, const char *yap_animation, const char *mouth_slots,
		      struct mouth_plan *out); /* false only on allocation failure */
void mouth_plan_free(struct mouth_plan *plan);
```

- [ ] **Step 1:** Write `tests/mouth-plan-test.c`, porting all seven cases of `tests/mouth-overlay.test.js`.
  - Build the same rig as a static desc:
    - bones `root`(−1), `head`(0), `mouth`(1), `arm`(0);
    - slots `arm`→3, `face`→1, `mouth_closed`→2, `mouth_expression`→2, `eye`→1;
    - skin attachments `(3, mouth_hate)`, `(3, mouth_laugh)`, `(3, mouth_talk)`, `(2, mouth_closed)`;
    - `IDLE` (duration 10): one bone timeline on bone 1;
    - `TOUCH` (duration 8) with these timelines:
      - slot 2: attachment frames `[0.5, 3, 4]`, names `[NULL, mouth_closed, NULL]`;
      - slot 3: frames `[0.5, 1, 1.5, 3, 4, 6]`, names `[mouth_talk, mouth_laugh, mouth_talk, NULL, mouth_talk, NULL]`;
      - bone 2;
      - bone 3;
      - slot 4: attachment `[2]` `[eye_closed]`;
      - an event timeline.
  - The rest of the variants are built per test. Same expectations as the JS:
    - `auto` → overlay, source TOUCH, slots `mouth_closed, mouth_expression`, bones `mouth`, window `0.5..6`, timelines `{0, 1, 2}`;
    - `touch` → overlay;
    - `talk_start` added → animation `talk_start`;
    - `IDLE` → animation IDLE;
    - no talk attachment → NONE;
    - configured `mouth_expression, missing_slot` → slots `[mouth_expression]`, bones `[]`, missing `[missing_slot]`, timelines `{1}`;
    - an extra `cheek` slot on the mouth bone → bones `[mouth]`, slots include `cheek`, and configured `mouth_closed,mouth_expression` → bones `[]`.
- [ ] **Step 2:** Run it and see the link failure.
- [ ] **Step 3:** Implement `mouth-plan.c` following `mouth-overlay.js` line by line. The talk pattern is "talk" (case-insensitive) with no ASCII letter immediately before or after it.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `feat(native): port the mouth-overlay planner to C`.

### Task 2: State machine

**Files:** create `src/native/state-machine.{c,h}`, `tests/state-machine-test.c`; modify `CMakeLists.txt`.

**Interfaces (produced):**

```c
struct state_machine_ops {
	void *param;
	void (*set)(void *param, int track, const char *name, bool loop);
	void (*add)(void *param, int track, const char *name, bool loop, float delay);
	void (*empty)(void *param, int track, float mix);
	void (*set_overlay)(void *param, int track, const struct mouth_plan *plan); /* loops plan's window */
	/* Fills plan for {yap_animation, mouth_slots}; NULL planner = 'auto' means talk_start, else the named animation. */
	void (*plan)(void *param, const char *yap_animation, const char *mouth_slots, struct mouth_plan *out);
};
struct state_machine_options { const char *default_animation; bool state_enabled; const char *yap_animation; const char *mouth_slots; };
struct state_machine *state_machine_create(const struct state_machine_ops *ops, const char *const *animations, size_t count,
					   const struct state_machine_options *options);
void state_machine_destroy(struct state_machine *machine);
void state_machine_configure(struct state_machine *machine, const struct state_machine_options *options);
bool state_machine_reset(struct state_machine *machine);
bool state_machine_trigger(struct state_machine *machine, const char *animation, bool loop);
bool state_machine_set_yapping(struct state_machine *machine, bool active, bool force);
const char *state_machine_default_animation(const struct state_machine *machine);
const struct mouth_plan *state_machine_yap_plan(const struct state_machine *machine); /* NULL when none */
```

`NULL` or `""` option strings mean "not given", which is the JS falsy rule.

- [ ] **Step 1:** Port every case of `tests/state-controller.test.js` to `tests/state-machine-test.c`, using fake ops that record calls as strings such as `set 0 idle 1`, `add 0 idle 1 0.00`, `empty 1 0.08` and `overlay 1 TOUCH (mouth) 0.87 9.13`.
  - The planner-less cases use `plan = NULL`.
  - The overlay case uses a fake planner that counts requests and returns an overlay plan named via `requested`.
  - Assertions match the JS, including:
    - "an unchanged request is not planned again";
    - "the default resolves to IDLE";
    - "an active mouth track restarts with the new plan".
- [ ] **Step 2:** Run it and see it fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `feat(native): port the emotion/yap state machine to C`.

### Task 3: Eye tracker

**Files:** create `src/native/eye-tracker.{c,h}`, `tests/eye-tracker-test.c`; modify `CMakeLists.txt` (link `m`).

**Interfaces (produced):**

```c
struct eye_tracker_ops {
	void *param;
	int (*find_slot_bone)(void *param, const char *slot);          /* -1 if missing */
	bool (*parent_matrix)(void *param, int bone, float m[4]);       /* false: no parent */
	void (*translate)(void *param, int bone, float dx, float dy);
	void (*update_world)(void *param);
	const char *(*bone_name)(void *param, int bone);
	void (*log)(void *param, int warning, const char *message);     /* warning: 1 warn, 0 info */
};
struct eye_tracker_options { bool enabled; const char *left_slots; const char *right_slots; double max_x, max_y, smoothing_ms; };
struct eye_tracker *eye_tracker_create(const struct eye_tracker_ops *ops, const struct eye_tracker_options *options);
void eye_tracker_destroy(struct eye_tracker *tracker);   /* restores offsets */
void eye_tracker_configure(struct eye_tracker *tracker, const struct eye_tracker_options *options);
void eye_tracker_set_target(struct eye_tracker *tracker, float x, float y);
void eye_tracker_before_frame(struct eye_tracker *tracker);
void eye_tracker_after_update(struct eye_tracker *tracker, float seconds);
size_t eye_tracker_bone_count(const struct eye_tracker *tracker);
```

- [ ] **Step 1:** Port the three cases of `tests/eye-tracker.test.js`. Use fake bones `left` (identity parent, x 10, y 20) and `right` (mirrored parent `a=-1`, x 30, y 40), and count world updates.
  - Expected: after target (1, 0.5) and smoothing 0, left is (16, 22) and right is (24, 42), with one world update.
  - Restore, then reapply: (16, 24) and (24, 44).
  - Missing slots: a warning message containing `missing_left, missing_right`, and 0 bones.
- [ ] **Step 2:** Run it and see it fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the suite. Expected: all pass.
- [ ] **Step 5:** Commit: `feat(native): port cursor eye tracking to C`.

### Task 4: Backend 4.1 additions

**Files:** modify `src/native/spine-backend.h`, `src/native/backend-41.c`, `tests/backend-41-test.c`.

**Interfaces (produced, added to `struct spine_backend`):**

```c
const struct spine_skeleton_desc *(*describe)(struct spine_asset *asset);
bool (*add_animation)(struct spine_instance *, int track, const char *name, bool loop, float delay);
void (*set_empty_animation)(struct spine_instance *, int track, float mix);
bool (*set_overlay)(struct spine_instance *, int track, int source_animation, const int *timelines, size_t count,
		    float start, float end, bool loop);
int (*find_slot_bone)(struct spine_instance *, const char *slot);
bool (*bone_parent_matrix)(struct spine_instance *, int bone, float m[4]);
void (*bone_translate)(struct spine_instance *, int bone, float dx, float dy);
void (*update_world_transform)(struct spine_instance *);
```

- `describe` builds once and caches on the asset; it is freed in `free_asset`.
- Overlays are kept in a growable list on the instance and freed in `free_instance`, after the animation state is disposed. Freeing releases `timelineIds`, the `timelines` array (not its items), the name and the struct.

- [ ] **Step 1:** Write the failing Spineboy tests.
  - `describe`: slot and bone counts match the skeleton data. The `mouth` slot's bone is named `head` (check in the generator output and assert the real value). `walk` has at least one BONE and one SLOT timeline.
  - The skin attachments include `(mouth slot, "mouth-smile")`.
  - `set_overlay` with `walk` on track 1, using only its slot timelines, window 0.1..0.5: returns true; updating 0.3 s works; replacing it twice is fine; free.
  - `find_slot_bone("mouth")` ≥ 0 and `find_slot_bone("nope")` == −1.
  - `bone_translate` and `update_world_transform` move the drawn vertices of that slot by the expected world delta. With an identity-like check, the vertex count is unchanged and the positions differ.
  - Extend `test_no_leaks_after_shutdown` to create two overlays and an empty animation before freeing.
- [ ] **Step 2:** Run it and see it fail (the function pointers are NULL).
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the suite and the prefix check. Expected: all pass.
- [ ] **Step 5:** Commit: `feat(native): expose skeleton description, overlays and bone access in the 4.1 backend`.

### Task 5: Renderer control inputs

**Files:** modify `src/native/native-renderer.{c,h}`.

**Interfaces (produced):**

```c
struct native_controls { bool state_enabled; bool yap_enabled; char yap_animation[256]; char mouth_slots[512];
			 bool eye_enabled; char eye_left[512]; char eye_right[512]; double eye_max_x, eye_max_y, eye_smoothing_ms; };
void native_renderer_set_controls(struct native_renderer *, const struct native_controls *);
void native_renderer_set_yapping(struct native_renderer *, bool active);
void native_renderer_set_cursor(struct native_renderer *, float x, float y);
void native_renderer_trigger(struct native_renderer *, const char *animation, bool loop);
void native_renderer_reset(struct native_renderer *);
```

**Behaviour:**
- **Adopting a new instance:**
  - create the state machine; its ops wrap the backend calls, and its planner is `mouth_plan_build` over `describe`, with plan logging;
  - create the eye tracker, whose ops wrap the backend bone functions and `native_log`;
  - configure both from the controls and the default animation, then `state_machine_reset`;
  - destroy the old pair before `release_skeleton`.
- **Tick:**
  1. Drain the inputs: controls go to configure, plus `set_yapping(yap_enabled && yapping)`. Then triggers and resets in FIFO order, then the latest cursor.
  2. If the source is showing: `eye_tracker_before_frame`, backend `update`, `eye_tracker_after_update(dt)`, mark for redraw.
- **`apply_config`:**
  - pass the default animation through `state_machine_configure`;
  - frame using `state_machine_default_animation`;
  - call `eye_tracker_before_frame` before bounds, then `after_update(0)` once framing is done.
- **No unit test:** this is graphics-thread glue. The OBS check in Task 7 covers it.

- [ ] **Step 1:** Implement it. Build: no warnings, and the suite is green.
- [ ] **Step 2:** Commit: `feat(native): drive the state machine and eye tracker from the native renderer`.

### Task 6: Source routing

**Files:** modify `src/spine-source.c`.

- Add a helper `with_native(context, fn)`, or inline lock blocks: under `renderer_mutex`, if `context->native` is set, call the native function and return true.
- `send_yap_state`, `send_cursor_position`, `send_emotion`, the reset hotkey and `send_configuration` each route to native when it is active. Otherwise they keep the browser behaviour exactly.
- `send_configuration` builds `struct native_controls` from the settings.
- Remove the stage 1 notices and their flags.
- Cursor sampling runs whenever eye tracking is enabled. Remove the `!native_active` guard.

- [ ] **Step 1:** Implement it. Build: no warnings, and the suite is green.
- [ ] **Step 2:** Commit: `feat: route yap, cursor, hotkeys and configuration to the native renderer`.

### Task 7: OBS verification and docs

- [ ] **Step 1:** Install, verify the DLL hash, and launch the portable OBS. Enable the websocket for the test only, with a backup. Use MekamiShifty with Native, eye tracking on and yap on.
- [ ] **Step 2:** Check the log for the yap plan line and the eye bones line. Trigger emotion hotkey 1 via `TriggerHotkeyByName` (`obs_spine_player.emotion_1`) after setting `emotion_1_animation` to `smile` with loop on, and confirm by screenshot that the pose changed. Then reset.
- [ ] **Step 3:** Yap: set the threshold to −60 dB with desktop audio as the source, or use any audio input that is producing sound. If no audio is flowing, mark yap as unverified in OBS and rely on the planner log and unit tests.
- [ ] **Step 4:** Switch Native → Browser → Native, close OBS, and confirm 0 leaks. Restore the websocket config.
- [ ] **Step 5:** Docs:
  - README: remove "Native mode currently plays the default animation only…", and say every control works in Native.
  - `docs/architecture.md`: add the controls paragraph.
  - Commit: `docs: native renderer supports all controls`.
