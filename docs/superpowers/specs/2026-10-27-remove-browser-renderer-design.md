# Remove the browser renderer (stage 5)

## Goal

The Spine Character source always draws natively. The hidden OBS Browser Source, its HTML/JS player and the Renderer setting are removed, so the plugin no longer needs obs-browser and never starts a Chromium process.

Stages 1–4 gave the native renderer parity with the browser for every supported asset (Spine 3.7, 4.0, 4.1) and every control (yap mode with the mouth overlay, eye tracking, emotions and hotkeys, anti-aliasing).

## What changes

### Source (`src/spine-source.c`)

- **One renderer per source.** Each source owns one `native_renderer`, created with the source and destroyed with it.
  - The pointer never changes during the source's life, so `renderer_mutex` goes. The selection and fallback serials, the fallback task and the browser reference all go with it.
- **Removed:** `src/browser-bridge.{c,h}`, browser creation and sizing, every `obsSpine*` browser event, and the once-a-second configuration resend (it existed for page reloads).
  - Controls are sent to the renderer whenever settings change, as before.
- **Source flags:** `OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB`.
  - `OBS_SOURCE_COMPOSITE` goes, together with `audio_render` and `enum_active_sources`; they existed only for the browser child.
  - The icon becomes `OBS_ICON_TYPE_IMAGE`.
- **Saved scenes:** the `renderer` setting is no longer read. It is erased from the source's settings when the source is created, so a scene saved with `renderer=browser` simply draws natively.
- **Properties:**
  - The Renderer list goes.
  - Anti-aliasing is always visible and is labelled "Anti-aliasing". The setting key stays `native_aa` so saved scenes keep their choice.
  - The Spine runtime list stays. Its help text explains that it is only used when the version cannot be detected.

### Native renderer

- `NATIVE_NEEDS_BROWSER` becomes `NATIVE_UNSUPPORTED`.
- The loader logs one error per load: `Spine <version> is not supported; the plugin draws Spine 3.7, 4.0 and 4.1 exports`.
- The source shows nothing in that case. The browser could not play those versions either: it bundled the same three runtimes.
- Comments that describe behaviour relative to "the browser" are reworded where the browser no longer exists. Where the web runtime is still the reference, the comment names it ("spine-ts", "the web player").

### Data, tools and tests

- `data/player/` goes, except the files the developer tools still need. Those move to `tools/spine-ts/`, which is not installed:
  - `runtime/` (spine-ts 3.7.94 webgl, 4.0.28 and 4.1.20 players, licence);
  - `spine37-binary.js`;
  - `version-detector.js`.
- `tools/generate-native-reference.js`, `tools/generate-animation-catalog.js` and the JS tests load them from there.
  - `generate-native-reference.js` must reproduce the committed `reference.h` files byte for byte.
- **JS tests:**
  - Deleted, because their C ports have their own tests: `asset-url`, `player-options`, `state-controller`, `eye-tracker`, `mouth-overlay`.
  - Kept: `version-detector`, `spine37-binary`, `spine-symbols`.
  - The CTest entry is renamed `tools-js-tests`.
- **Installed data:** `effects/`, `locale/`, and `licenses/spine-c-<ver>.txt` (the vendored spine-c licences, compiled into the plugin).
- **Packaging:**
  - `PKGBUILD` drops the `obs-studio-plugin-browser` dependency and installs the spine-c licence.
  - `THIRD_PARTY_NOTICES.md` describes spine-c (shipped) and spine-ts (developer tools only).

### Documentation

- README and `docs/architecture.md` describe the native-only plugin.
- `docs/performance.md` keeps its Browser-vs-Native measurements as history, with a note that the browser renderer was removed.

## Verification

- `ctest`: all suites pass, including the renamed JS tools test.
- `node tools/generate-native-reference.js <ver>` for 3.7, 4.0 and 4.1 leaves `tests/fixtures/*/reference.h` unchanged.
- `cmake --install` into a temporary prefix installs no `player/` directory, and does install the three licences.
- GCC `-Wall -Wextra -Wpedantic -Werror` syntax check of `spine-source.c`, `native-renderer.c` and `plugin-main.c`.
- **OBS 32.2.2** (portable test install):
  - A source saved with `renderer=browser` draws natively after the upgrade, and its saved settings no longer contain `renderer`.
  - Spineboy 3.7, 4.0 and 4.1, Curian (3.7 mouth overlay) and Mekami (4.1) all draw.
  - A 4.2-labelled skeleton logs the unsupported-version error and draws nothing.
  - No `obs-browser` source is created for the plugin; a log search for `OBS Spine Player Renderer` finds nothing.
  - 0 memory leaks at exit.
