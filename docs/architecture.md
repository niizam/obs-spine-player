# Architecture

## Design summary

The plugin is a small native OBS source that owns a private Browser Source. Native code handles OBS integration, audio sampling, settings, and hotkeys. The browser page handles only Spine asset loading and animation tracks.

```text
OBS audio source ──> RMS peak ──> level gate ──┐
                                               ├─> browser event bridge ─> SpineStateController ─> Spine tracks
OBS hotkeys ───────> emotion/action command ───┘
```

This boundary keeps OBS-specific lifetime and audio threading out of the player, while keeping Spine runtime objects out of native code.

## Components

- `src/spine-source.c` registers the source, owns the private `browser_source`, exposes properties/hotkeys, subscribes to a selected audio source, and emits normalized control events.
- `src/level-gate.c` converts an amplitude stream into a stable open/closed signal. It contains no OBS or speech-specific behavior.
- `src/browser-bridge.c` is the only native dependency on the OBS Browser `javascript_event` procedure.
- `data/player/version-detector.js` reads the version from Spine binary/JSON headers and selects only the bundled 4.0 or 4.1 family.
- `data/player/state-controller.js` owns animation semantics independently of DOM/loading code.
- `data/player/player-options.js` builds runtime options and supplies the default animation during construction. Spine Player uses that animation to calculate its initial viewport; applying or resetting it inside the success callback can produce a loaded but invisible character.
- `data/player/player.js` loads assets, creates/disposes the matching Spine player, and adapts browser events to the state controller.
- `data/player/asset-url.js` maps native file paths to OBS Browser's `http://absolute/` local-file scheme. Do not use `file://` URLs here: the player page has an `http://absolute` origin, so Chromium rejects direct `file://` fetches as cross-origin requests.

## Animation model

Track 0 contains the base character state. It starts with `idle`. A looping emotion replaces track 0 until another state or reset is requested. A one-shot action is placed on track 0 and queues the default animation behind it.

Track 1 is reserved for mouth movement. Yap mode loops `talk_start` there and clears only track 1 after the release hold. This is the important behavior taken from the Nikke reference: mouth motion overlays the current body animation instead of replacing `idle`.

The emotion state machine and emotion hotkey dispatch are separate settings. Turning the state machine off resets track 0 and rejects state transitions. Turning hotkeys off keeps the state machine available to future input adapters while ignoring OBS hotkey presses.

## Audio threading

OBS invokes the audio capture callback on its audio path. That callback calculates one-channel RMS and performs only an atomic maximum update. It never calls the browser, allocates, changes animation state, or performs recognition.

The video tick consumes the pending peak, advances the attack/release gate, and emits a browser event only when the boolean yap state changes. This avoids flooding CEF and gives a future audio feature a clear real-time-safe boundary.

## Supplied asset findings

`characters/Mekami_Shifty/c610_00.skel` reports Spine `4.1.20`. Its bounds are approximately 961 × 2286 units and its animation set is:

```text
action, etc, expression_0, idle, no, pain, sad, smile,
special, surprise, talk_end, talk_start
```

The defaults use persistent facial/body states for `smile`, `sad`, `surprise`, `pain`, and `expression_0`; `action`, `special`, and `no` default to one-shot behavior.

## Reference decisions

- `references/pub_web_spine-web-player-custom` established the official 4.0.28/4.1.20 player runtimes, transparent WebGL configuration, raw/local asset loading pattern, and version-specific runtime split.
- `references/nikke-db-vue` established the `talk_start` check and the track-1 overlay/clear behavior used by yap mode.
- `references/obs-plugintemplate` established the module registration, localization, CMake, and install-layout conventions.

## Adding future inputs

New inputs should produce one of three normalized commands rather than directly manipulating Spine:

- yap active/inactive;
- trigger animation with loop/one-shot intent;
- reset to the default animation.

For ASR/STT, add a separate adapter that consumes audio or transcript results off the OBS audio callback, then emits emotion/action commands through `browser_bridge_send`. Do not put recognition in `SpineStateController`, and do not make track selection depend on a recognizer. A speech-emotion model can therefore be added or removed without changing asset loading, the state machine, hotkeys, or microphone yap mode.

For non-speech integrations such as MIDI, WebSocket, stream-deck actions, or automation, follow the same command boundary. If external inputs need more than the current three commands, version the browser-event payload rather than exposing Spine runtime objects to native adapters.

## Tests

`tests/asset-url.test.js` verifies OBS-local URLs on Unix and Windows. `tests/player-options.test.js` verifies initial animation and asset loader options. `tests/state-controller.test.js` verifies idle, persistent states, one-shot return, optional states, and the independent mouth track. `tests/version-detector.test.js` verifies runtime selection. `tests/level-gate-test.c` verifies attack/release behavior. CTest runs all five groups.
