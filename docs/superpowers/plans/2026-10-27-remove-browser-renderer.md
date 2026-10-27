# Remove the Browser Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the Spine Character source native-only, and delete the browser renderer and its player page.

**Architecture:** `spine-source.c` owns one `native_renderer` for the source's lifetime. Developer-only spine-ts files move to `tools/spine-ts/`.

**Tech Stack:** C11, libobs, CMake/CTest, Node test runner.

**Spec:** `docs/superpowers/specs/2026-10-27-remove-browser-renderer-design.md`

## Global Constraints

- Saved setting keys stay unchanged (`native_aa`, `runtime`, every control key). Only `renderer` is erased.
- Unsupported-version log text: `Spine <version> is not supported; the plugin draws Spine 3.7, 4.0 and 4.1 exports`.
- `tests/fixtures/*/reference.h` must not change.

## Review Focus

- A scene saved with `renderer=browser` must draw natively without the user touching it.
- Hotkeys fire on the hotkey thread, and the renderer now has no lock around it. Only "any thread" renderer calls may happen there.
- Settings that change while a load runs must still reach the renderer, with no periodic resend.
- Destroying a source while its loader thread runs must not leak or crash (native_renderer_destroy joins).
- Installed data must not reference removed files: no `player/` directory, and the locale has no dangling keys.

---

### Task 1: Native-only source

**Files:**
- Modify: `src/spine-source.c`, `src/native/native-renderer.{c,h}`, `data/locale/en-US.ini`, `CMakeLists.txt`
- Delete: `src/browser-bridge.{c,h}`

- [ ] Rewrite `spine-source.c` per the spec's Source section. `context->native` is created in `create` before the first `update` and destroyed last in `destroy`. `update` loads when the skeleton, atlas or runtime changes, then configures, sets AA, and sends the controls.
- [ ] Rename `NATIVE_NEEDS_BROWSER` to `NATIVE_UNSUPPORTED`, and log the spec's error text in `load_asset`.
- [ ] Locale: delete the `Renderer*` keys. Change `NativeAntialiasing` to "Anti-aliasing" and reword its help. Reword `RuntimeVersion` help.
- [ ] Build plus `ctest`. Expected: every suite passes.
- [ ] GCC syntax check of the three OBS files. Expected: clean.
- [ ] Commit `feat(native)!: always draw natively; remove the browser renderer`.

### Task 2: Developer tools keep their reference runtimes

**Files:**
- Move: `data/player/runtime/` → `tools/spine-ts/runtime/`; `data/player/{spine37-binary,version-detector}.js` → `tools/spine-ts/`
- Delete: everything else in `data/player/`; `tests/{asset-url,player-options,state-controller,eye-tracker,mouth-overlay}.test.js`
- Modify: `tools/generate-native-reference.js`, `tools/generate-animation-catalog.js`, `tests/{spine37-binary,version-detector}.test.js`, `CMakeLists.txt` (test name, licence install), `PKGBUILD`, `THIRD_PARTY_NOTICES.md`

- [ ] Do the moves and deletions with `git mv` / `git rm`, and update the paths.
- [ ] `ctest`. Expected: `tools-js-tests` passes, along with every suite.
- [ ] `node tools/generate-native-reference.js 3.7|4.0|4.1` into the fixtures. Expected: `git diff --stat tests/fixtures` is empty.
- [ ] `node tools/generate-animation-catalog.js tests/fixtures/spine41/spineboy-pro.skel` into a temporary copy. Expected: it writes a catalog.
- [ ] `cmake --install` into a temporary prefix. Expected: no `player/`, and `licenses/spine-c-{3.7,4.0,4.1}.txt` present.
- [ ] Commit `build: ship only native data; keep spine-ts for developer tools`.

### Task 3: Documentation and OBS verification

**Files:** `README.md`, `docs/architecture.md`, `docs/performance.md`

- [ ] Rewrite the browser-specific sections as the spec says.
- [ ] Run the spec's OBS checks.
- [ ] Commit `docs: native-only plugin`.
