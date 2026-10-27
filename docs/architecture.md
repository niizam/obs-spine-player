# Architecture

## Design summary

The plugin is one native OBS source. It handles OBS integration, audio/cursor sampling, settings and hotkeys. It draws the character itself through libobs graphics, using the official spine-c runtimes.

```text
OBS audio source ──> RMS peak ──> level gate ──┐
                                               ├─> native_renderer_* ─> state machine ─> Spine tracks
OBS hotkeys ───────> emotion/action command ───┘
Desktop cursor ────> normalized x/y ─────────────> eye tracker ─────────> eye bones
```

`spine-source.c` only produces normalized commands. Everything that touches a skeleton runs inside the renderer, on OBS's graphics thread.

Versions before stage 5 of the native rewrite drew through a hidden OBS Browser Source and a spine-ts player page. That path was removed once the native renderer matched it for every supported asset and control. Design: [stage 5 spec](superpowers/specs/2026-10-27-remove-browser-renderer-design.md).

## Components

- **`src/spine-source.c`** registers the source and owns one `native_renderer` for the source's lifetime.
  - It exposes properties and hotkeys, subscribes to a selected audio source, and sends normalized controls to the renderer.
  - It erases the `renderer` setting that earlier versions saved.
- **`src/level-gate.c`** converts an amplitude stream into a stable open/closed signal. It contains no OBS or speech-specific behavior.
- **`src/cursor-input.c`** is a platform adapter that samples and normalizes the global desktop cursor.
  - It uses the Windows virtual desktop, macOS display coordinates, Hyprland's native command socket, or X11.
  - Platform APIs stay out of the renderer.
- **`src/animation-catalog.c`** discovers the names that populate the editable animation dropdowns. It preserves unknown values from existing scenes.
  - It reads JSON animation keys, or an adjacent `.animations.txt` catalog when present.
  - Otherwise it asks `src/skeleton-binary.c` for the names in a binary export.
- **`src/skeleton-binary.c`** is a bounds-checked reader for the Spine 3.7, 4.0 and 4.1 binary layouts.
  - It skips every section by its exact size, following spine-libgdx 3.7.94 and spine-ts 4.0/4.1.
  - It only accepts a file it consumed to the last byte, so a layout mismatch produces no names rather than wrong ones.
- **`tools/`** holds developer tools. They are not installed.
  - `generate-animation-catalog.js` writes a catalog file.
  - `generate-native-reference.js` writes the parity references in `tests/fixtures/*/reference.h`.
  - Both run the official spine-ts runtimes kept in `tools/spine-ts/runtime/`. For 3.7 binaries, `tools/spine-ts/spine37-binary.js` is a port of the spine-libgdx 3.7.94 reader, because spine-ts 3.7 only reads JSON.
  - `tools/spine-ts/version-detector.js` is the JS counterpart of `src/native/skeleton-version.c`.

## Renderer

`src/native/native-renderer.c` draws one skeleton. Design: [stage 1 spec](superpowers/specs/2026-10-27-native-renderer-41-design.md).

- **Version backends.** `src/native/spine-backend.h` is a version-neutral interface: load, animate, bounds, draw lists, skeleton description, overlays, bone access, and blend factors.
  - `src/native/backend.c` implements it and is compiled three times (`SPINE_BACKEND_VERSION` 37, 40, 41) against the vendored spine-c 3.7, 4.0 and 4.1 (`third_party/spine-c-<ver>`). Compatibility macros cover the API differences.
  - `src/native/backends.c` maps a detected Spine family to its backend. The loader stores that backend with each result, so an asset is always freed by its own runtime.
  - Each runtime is compiled with a generated forced-include header (`src/native/spine37-prefix.h`, `spine40-prefix.h`, `spine41-prefix.h`, from `tools/generate-spine-prefix.js`). The header renames every external symbol (`sp37_*`, `sp40_*`, `sp41_*`), so all three runtimes share one plugin module. The `spine<ver>-prefix-check` tests fail if a symbol escapes its prefix.
  - Each backend refuses JSON that declares another version family, because another version's layout crashes spine-c's reader.
  - Other Spine versions log `Spine <version> is not supported; …` and draw nothing.
- **Threads.**
  - Loading happens on a per-source background thread, and only the latest request is kept.
  - GPU resources, the skeleton instance and drawing live on the OBS graphics thread.
  - Control input (`set_controls`, `set_yapping`, `set_cursor`, `trigger`, `reset`) may arrive from any thread, including the hotkey thread. It is applied on the next tick.
- **Drawing.** It mirrors the spine-ts renderer: a premultiplied two-colour tint (`data/effects/spine.effect`), each version's spine-ts blend factors, and clipping.
  - Direct3D 11 rejects colour factors in the alpha channel, so `spine_alpha_factor` maps them to their alpha equivalents, which is what WebGL does.
  - The skeleton is drawn into an offscreen texture with gamma-space blending, as in WebGL. That texture is composited with sRGB decode and `ONE, INVSRCALPHA`.
  - Framing copies the web player: 100 animation samples, 10 % padding, contain-fit. 3.7 uses the old 3.7 player's sampling.
- **Anti-aliasing.** libobs has no MSAA (render targets are single-sample) and never builds mipmaps.
  - Atlas pages are resized to power-of-two sizes and given a CPU-built premultiplied mip chain (`atlas-image.c`). They are sampled with 16× anisotropic filtering, and the CPU copies are freed after upload.
  - FXAA (`data/effects/spine-fxaa.effect`) and 1.5×/2× supersampling with an area downscale are optional.
- **Controls.** Three version-neutral modules reach the skeleton only through callbacks and the backend's read-only `spine_skeleton_desc`:
  - `src/native/state-machine.c` handles emotions, one-shot actions and reset on track 0, and the mouth on track 1;
  - `src/native/mouth-plan.c` decides what yap mode plays;
  - `src/native/eye-tracker.c` runs cursor eye tracking.

  The renderer applies control input on the graphics thread. Each frame runs: eye restore → animation update → eye offsets.

  Mouth overlays share the source clip's timelines. They are freed only with their instance, never through `spAnimation_dispose`.
- **OBS allocator rules.**
  - spine-c 4.0 and 4.1 allocate through `bmalloc`. Zero-byte requests are rounded up, because OBS treats them as fatal.
  - spine-c 3.7 keeps the C library allocator, because its `Skeleton.c` calls `realloc` directly. `spine_backends_set_host_allocator` encodes that rule.
  - The runtimes' shared statics are freed in `obs_module_unload`.

## Animation model

Track 0 contains the base character state, starting with `idle`.

- A looping emotion replaces track 0 until another state or reset is requested.
- A one-shot action is placed on track 0, and queues the default animation behind it.

Track 1 is reserved for mouth movement. Yap mode loops `talk_start` there, and clears only track 1 after the release hold. This is the important behavior taken from the Nikke reference: mouth motion overlays the current body animation instead of replacing `idle`.

Some rigs have no dedicated mouth animation. CounterSide illustrations, for example, key a `mouth_talk` attachment and the mouth bone's scale inside the full-body `TOUCH` clip.

- For those, `mouth-plan.c` picks the source clip and the mouth slots: every slot on the talking-mouth slot's bone, or the configured slots, plus the bones used only by those slots.
- The backend builds an overlay animation from that clip's timelines for those slots and bones. The timelines are shared, not copied.
- The track entry's animation start and end limit the loop to the talking stretch.
- When the track empties, Spine restores un-keyed mouth attachments to the setup pose, or to the emotion on track 0.

Eye tracking does not use an animation track.

- Before each update, the eye tracker removes its previous offsets. Spine then evaluates the active animations normally.
- Afterwards, the tracker converts the requested world-space displacement through each bone parent's inverse transform, applies local offsets, and refreshes world transforms before drawing.
- This prevents drift, preserves animated eye-bone motion, and keeps mirrored left/right rigs moving in the same screen direction.

Configured eye layers are named by Spine slot, not by bone. Slots are resolved at runtime, so multiple pupil, iris and highlight attachments move as one controlling bone, and the settings carry over to another rig. Mekami_Shifty's default left slots resolve to `bone151`; its right slots resolve to `bone152`.

The emotion state machine and emotion hotkey dispatch are separate settings.

- Turning the state machine off resets track 0 and rejects state transitions.
- Turning hotkeys off ignores OBS hotkey presses, but keeps the state machine available to future input adapters.

## Audio threading

OBS invokes the audio capture callback on its audio path.

- The callback calculates one-channel RMS and performs only an atomic maximum update.
- It never touches the renderer, allocates, changes animation state, or performs recognition.

The video tick consumes the pending peak and advances the attack/release gate. It passes the yap state to the renderer only when the boolean changes, which gives a future audio feature a clear real-time-safe boundary.

Desktop cursor input is sampled from the video tick at 30 Hz and sent as normalized `[-1, 1]` coordinates. The renderer's eye tracker smooths them exponentially at render rate.

On Linux:

- Hyprland sessions use the compositor command socket and monitor layout directly, because XWayland exposes a frozen or restricted pointer.
- Other native Wayland compositors need their own explicit adapter.
- X11 remains the generic fallback.
- A failed backend is logged once rather than retried noisily.

## Character assets

Character skeletons, atlases, textures and animation catalogs are intentionally excluded from version control. The plugin has configurable eye-slot presets but bundles no character data. A developer may keep assets in the ignored local `characters/` directory for testing; CMake installs that directory only when it exists.

## Reference decisions

- `references/pub_web_spine-web-player-custom` established the official 4.0.28/4.1.20 player runtimes. Their framing and blending are what the native renderer is checked against.
- `references/nikke-db-vue` established the `talk_start` check and the track-1 overlay/clear behavior used by yap mode.
- `references/obs-plugintemplate` established the module registration, localization, CMake, and install-layout conventions.

## Adding future inputs

New discrete inputs should produce one of three normalized commands, rather than manipulating Spine directly:

- yap active/inactive (`native_renderer_set_yapping`);
- trigger animation with loop/one-shot intent (`native_renderer_trigger`);
- reset to the default animation (`native_renderer_reset`).

Continuous pose inputs should follow the cursor pattern: sample outside the Spine runtime, pass normalized values to the renderer, then apply them in a small controller after animation evaluation. Face gaze or remote pointer input can therefore replace `cursor-input.c` without changing eye-slot resolution or offset math.

For ASR/STT:

- Add a separate adapter that consumes audio or transcript results off the OBS audio callback, then sends emotion/action commands through those functions.
- Do not put recognition in `state-machine.c`, and do not make track selection depend on a recognizer.
- A speech-emotion model can then be added or removed without changing asset loading, the state machine, hotkeys, or microphone yap mode.

Non-speech integrations such as MIDI, WebSocket, stream-deck actions or automation follow the same command boundary. If they need more than the three commands, extend the `native_renderer` control API, rather than exposing spine-c objects to input adapters.

## Tests

`ctest` runs every suite.

**Native renderer:**

- `backend-{37,40,41}-tests` check each runtime against its spine-ts reference: bounds, draw lists, slots, overlays, version checks, and memory.
- `backends-tests` links all three runtimes into one executable and checks the allocator policy.
- `spine{37,40,41}-prefix-check` verifies every runtime symbol is renamed.
- `atlas-image`, `framing`, `blend`, `antialiasing`, `skeleton-version`, `mouth-plan`, `state-machine` and `eye-tracker` tests cover the version-neutral modules.

**Source and catalogs:**

- `skeleton-binary-tests` builds 3.7, 4.0 and 4.1 binaries in memory, and checks names, truncation, trailing data and unsupported versions. Run it with file arguments to print the names of real exports.
- `animation-catalog-tests` checks binary sidecars, native binary discovery from `tests/fixtures/spine37-synthetic.skel` (written by `skeleton-binary-tests --write`), and JSON discovery.
- `level-gate-tests` checks attack/release behavior.

**Tools:** `tools-js-tests` runs the Node tests for the developer tools: the version detector, the 3.7 binary reader, and the symbol-prefix parser.
