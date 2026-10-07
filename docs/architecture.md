# Architecture

## Design summary

The plugin is a small native OBS source that owns a private Browser Source. Native code handles OBS integration, audio/cursor sampling, settings, and hotkeys. The browser page handles Spine asset loading, animation tracks, and additive rig controls.

```text
OBS audio source ──> RMS peak ──> level gate ──┐
                                               ├─> browser event bridge ─> SpineStateController ─> Spine tracks
OBS hotkeys ───────> emotion/action command ───┘
Desktop cursor ────> normalized x/y ─────────────> SpineEyeTracker ────────> eye bones
```

This boundary keeps OBS-specific lifetime and audio threading out of the player, while keeping Spine runtime objects out of native code.

## Components

- `src/spine-source.c` registers the source, owns the private `browser_source`, exposes properties/hotkeys, subscribes to a selected audio source, and emits normalized control events.
- `src/level-gate.c` converts an amplitude stream into a stable open/closed signal. It contains no OBS or speech-specific behavior.
- `src/cursor-input.c` is a platform adapter that samples and normalizes the global desktop cursor. It uses the Windows virtual desktop, macOS display coordinates, Hyprland's native command socket, or X11 without coupling platform APIs to the Spine renderer.
- `src/browser-bridge.c` is the only native dependency on the OBS Browser `javascript_event` procedure.
- `src/animation-catalog.c` discovers JSON animation keys, reads adjacent `.animations.txt` catalogs, and otherwise asks `src/skeleton-binary.c` for the names in a binary export. OBS properties use one shared catalog to populate editable dropdowns, preserving unknown values from existing scenes.
- `src/skeleton-binary.c` is a bounds-checked reader for the Spine 3.7, 4.0, and 4.1 binary layouts. It skips every section with its exact size (following spine-libgdx 3.7.94 and spine-ts 4.0/4.1) and only accepts a file it consumed to the last byte, so a layout mismatch produces no names rather than wrong ones. It has no OBS dependency, which keeps it unit-testable.
- `tools/generate-animation-catalog.js` uses the matching bundled Spine runtime to parse a skeleton and write a catalog file. It doubles as the reference the native reader is checked against.
- `data/player/version-detector.js` reads the version from Spine binary/JSON headers (Spine 3 binaries start with a hash string, Spine 4 binaries with eight hash bytes) and selects the bundled 3.7, 4.0, or 4.1 family.
- `data/player/spine37-binary.js` ports the spine-libgdx 3.7.94 `SkeletonBinary` reader to the spine-ts 3.7 object model, because spine-ts 3.7 only ships `SkeletonJson`.
- `data/player/spine37-player.js` is a small player over the official 3.7 `spine-webgl` runtime. The upstream 3.7 web player is JSON-only, drives track 0 from its timeline UI, and has no frame hooks, so the adapter exposes the subset of the 4.x `SpinePlayer` surface the renderer uses (asset URLs, default animation, `frame`/`update`/`success`/`error` callbacks, `skeleton`, `animationState`, `dispose`). It frames the sampled bounds of the default animation with 10% padding like the 4.x player.
- `data/player/mouth-overlay.js` decides what yap mode plays: a dedicated mouth animation unchanged, or a mouth-only animation assembled from an existing clip's timelines (see below). It is version-agnostic and DOM-free.
- `data/player/state-controller.js` owns animation semantics independently of DOM/loading code.
- `data/player/eye-tracker.js` resolves configurable eye slots to unique bones, smooths normalized cursor input, and applies additive screen-space offsets after animation evaluation.
- `data/player/player-options.js` builds runtime options and supplies the default animation during construction. Spine Player uses that animation to calculate its initial viewport; applying or resetting it inside the success callback can produce a loaded but invisible character.
- `data/player/player.js` loads assets, creates/disposes the matching Spine player, and adapts browser events to the state controller.
- `data/player/asset-url.js` maps native file paths to OBS Browser's `http://absolute/` local-file scheme. Do not use `file://` URLs here: the player page has an `http://absolute` origin, so Chromium rejects direct `file://` fetches as cross-origin requests.

## Animation model

Track 0 contains the base character state. It starts with `idle`. A looping emotion replaces track 0 until another state or reset is requested. A one-shot action is placed on track 0 and queues the default animation behind it.

Track 1 is reserved for mouth movement. Yap mode loops `talk_start` there and clears only track 1 after the release hold. This is the important behavior taken from the Nikke reference: mouth motion overlays the current body animation instead of replacing `idle`.

Some rigs have no dedicated mouth animation. CounterSide illustrations, for example, key a `mouth_talk` attachment and the mouth bone's scale inside the full-body `TOUCH` clip. For those, `SpineMouthOverlay` builds a new `spine.Animation` from the source clip's timelines for the mouth slots (every slot on the talking-mouth slot's bone, or the configured slots) and for bones used only by those slots. The timeline objects are shared, not copied, and the track entry's `animationStart`/`animationEnd` limit the loop to the talking stretch, so no version-specific keyframe manipulation is needed. When the track empties, Spine restores un-keyed mouth attachments to the setup pose or to the emotion on track 0.

Eye tracking does not use an animation track. The Spine Player `frame` callback first removes the previous additive offsets, then Spine evaluates active animations normally. The `update` callback converts the requested world-space displacement through each bone parent's inverse transform, applies local offsets, and refreshes world transforms before drawing. This prevents drift, preserves animated eye-bone motion, and keeps mirrored left/right rigs moving in the same screen direction.

Configured layer names are Spine slots rather than assumed bone names. Resolving slots at runtime lets multiple pupil, iris, and highlight attachments move as one controlling bone and keeps the settings portable to another rig. Mekami_Shifty's default left slots resolve to `bone151`; its right slots resolve to `bone152`.

The emotion state machine and emotion hotkey dispatch are separate settings. Turning the state machine off resets track 0 and rejects state transitions. Turning hotkeys off keeps the state machine available to future input adapters while ignoring OBS hotkey presses.

## Audio threading

OBS invokes the audio capture callback on its audio path. That callback calculates one-channel RMS and performs only an atomic maximum update. It never calls the browser, allocates, changes animation state, or performs recognition.

The video tick consumes the pending peak, advances the attack/release gate, and emits a browser event only when the boolean yap state changes. This avoids flooding CEF and gives a future audio feature a clear real-time-safe boundary.

Desktop cursor input is sampled from the video tick at 30 Hz and sent as normalized `[-1, 1]` coordinates. Browser-side exponential smoothing bridges those samples at render rate. Hyprland sessions use the compositor command socket and monitor layout directly because XWayland exposes a frozen or restricted pointer. Other native Wayland compositors require their own explicit adapter; X11 remains the generic Linux fallback, and a failed backend is logged once rather than retried noisily.

## Character assets

Character skeletons, atlases, textures, and animation catalogs are intentionally excluded from version control. The plugin has configurable eye-slot presets but does not bundle character data. A developer may keep assets in the ignored local `characters/` directory for testing; CMake installs that directory only when it exists.

## Reference decisions

- `references/pub_web_spine-web-player-custom` established the official 4.0.28/4.1.20 player runtimes, transparent WebGL configuration, raw/local asset loading pattern, and version-specific runtime split.
- `references/nikke-db-vue` established the `talk_start` check and the track-1 overlay/clear behavior used by yap mode.
- `references/obs-plugintemplate` established the module registration, localization, CMake, and install-layout conventions.

## Adding future inputs

New discrete inputs should produce one of three normalized commands rather than directly manipulating Spine:

- yap active/inactive;
- trigger animation with loop/one-shot intent;
- reset to the default animation.

Continuous pose inputs should follow the cursor adapter pattern: sample outside the Spine runtime, send normalized values through the browser bridge, then apply them in a small renderer-side controller after animation evaluation. Face gaze or remote pointer input can therefore replace `cursor-input.c` without changing eye-slot resolution or offset math.

For ASR/STT, add a separate adapter that consumes audio or transcript results off the OBS audio callback, then emits emotion/action commands through `browser_bridge_send`. Do not put recognition in `SpineStateController`, and do not make track selection depend on a recognizer. A speech-emotion model can therefore be added or removed without changing asset loading, the state machine, hotkeys, or microphone yap mode.

For non-speech integrations such as MIDI, WebSocket, stream-deck actions, or automation, follow the same command boundary. If external inputs need more than the current three commands, version the browser-event payload rather than exposing Spine runtime objects to native adapters.

## Tests

`tests/asset-url.test.js` verifies OBS-local URLs on Unix and Windows. `tests/player-options.test.js` verifies initial animation, asset loader options, and additive-control render hooks. `tests/state-controller.test.js` verifies idle, persistent states, one-shot return, optional states, and the independent mouth track. `tests/eye-tracker.test.js` verifies slot resolution, mirrored parent transforms, missing slots, and non-accumulating offsets. `tests/version-detector.test.js` verifies runtime selection, including the Spine 3 header. `tests/spine37-binary.test.js` reads a synthetic 3.7 export through the bundled runtime. `tests/mouth-overlay.test.js` verifies mouth detection, bone exclusivity, talk windows, and the dedicated-animation path. `tests/skeleton-binary-test.c` builds 3.7, 4.0, and 4.1 binaries in memory and checks names, truncation, trailing data, and unsupported versions; run it with file arguments to print the names of real exports. `tests/animation-catalog-test.c` verifies binary sidecars, native binary discovery from `tests/fixtures/spine37-synthetic.skel` (written by `skeleton-binary-tests --write`), and JSON discovery. `tests/level-gate-test.c` verifies attack/release behavior.
