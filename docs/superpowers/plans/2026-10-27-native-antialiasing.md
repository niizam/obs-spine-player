# Native Anti-aliasing (stage 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Native-only anti-aliasing setting with four values: Off, FXAA, Supersample 1.5× and 2×.

**Architecture:** A pure size and parse module, renderer support for scaled offscreen textures plus two composite paths (FXAA, area downscale), and the source setting and property.

**Tech Stack:** C11, libobs 31 effects, CTest.

**Spec:** `docs/superpowers/specs/2026-10-27-native-antialiasing-design.md`

## Global Constraints

- **Branch:** `feature/native-renderer`. No merge and no PR until stage 5.
- **Default `off`.** Mipmaps stay on in every mode.
- **Composite blending is unchanged:** sRGB framebuffer, `ONE, INVSRCALPHA`.
- **Constants:** factors `1.5` and `2`; maximum texture `16384`; FXAA `REDUCE_MIN 1/128`, `REDUCE_MUL 1/8`, `SPAN_MAX 8`.
- **Setting values:** `off`, `fxaa`, `ss15`, `ss2`. The setting key is `native_aa`.

## Review Focus

1. **Huge source sizes at 2×.** For example 7680×4320, the property maximum. Expected: the texture is clamped and the aspect ratio kept, with no texture creation failure. Covered by the Task 1 test.
2. **Switching the mode while running.** Expected: an immediate redraw and no stale texture size. Covered by the Task 2 OBS check.
3. **Edges.** FXAA must not halo the silhouette against transparency. Covered by visual inspection in Task 3.

### Task 1: `antialiasing` module

**Files:** `src/native/antialiasing.{c,h}`, `tests/antialiasing-test.c`, `CMakeLists.txt`.

- [ ] Write the tests from the spec; they should fail to link.
- [ ] Implement the module; the tests pass.
- [ ] Commit `feat(native): anti-aliasing modes and supersampled render size`.

### Task 2: Renderer and source

**Files:** `src/native/native-renderer.{c,h}`, `data/effects/spine-fxaa.effect`, `src/spine-source.c`, `data/locale/en-US.ini`.

- [ ] Add `native_renderer_set_antialiasing`, with a guarded field copied in render.
- [ ] Size the texrender with `native_aa_render_size` and add the composite paths:
  - **FXAA:** load `effects/spine-fxaa.effect` lazily; log and fall back to off if it fails.
  - **Area:** `obs_get_base_effect(OBS_EFFECT_AREA)`.
- [ ] Add the source setting, property (visible only for Native), locale strings, and the hand-off in `update`.
- [ ] Build: no warnings, suite green. Commit `feat(native): anti-aliasing quality setting (FXAA, 1.5x, 2x supersampling)`.

### Task 3: OBS verification, measurements and docs

- [ ] Use the portable OBS with MekamiShifty Native at 540×960. For each mode:
  - take a screenshot and a 2× nearest-neighbour crop around the hair and arm edges;
  - record `averageFrameRenderTime` after 10 s;
  - sample GPU 3D for 30 s.
- [ ] Check the log for effect errors, then confirm 0 leaks at shutdown.
- [ ] Add the README section and the `docs/performance.md` table. Commit `docs: native anti-aliasing modes and costs`.
