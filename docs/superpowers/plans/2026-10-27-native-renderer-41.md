# Native Spine 4.1 Renderer (stage 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a **Renderer: Native** option that draws Spine 4.1 characters with spine-c through libobs graphics, with no browser source, using mipmapped textures for anti-aliasing.

**Architecture:**
- Upstream spine-c 4.1 is vendored. It is compiled together with a version adapter (`backend-41.c`) into a static library whose external symbols are all renamed `sp41_*` by a generated forced-include header.
- The rest of the plugin talks only to the version-neutral `spine-backend.h`.
- `native-renderer.c` owns the loader thread and all `gs_*` work.
- `spine-source.c` switches between it and the existing browser source.

**Tech Stack:** C11, libobs 31.1.1 graphics (`gs_*`), spine-c (upstream `4.1` branch at `77a5db0ec6d16331f5efbaa7662bba9355bd3424`), CMake ≥ 3.22 with CTest, Node (tools and JS tests).

**Spec:** `docs/superpowers/specs/2026-10-27-native-renderer-41-design.md`. Read it first. This plan says how; the spec says what and why.

## Global Constraints

- **Branch:** `feature/native-renderer`. Commit after every task.
- **Language:** C11 for plugin code. Do not add C++. The project stays `LANGUAGES C`.
- **Vendored code:** the vendored spine-c is unmodified upstream code. Never edit files under `third_party/`. Never edit `data/player/runtime/**`.
- **Browser mode:** the default is `renderer=browser`, and browser behaviour must be byte-for-byte unchanged.
- **Symbol isolation:** every defined external symbol of the `spine-backend-41` library starts with `sp41_`, except `spine_backend_41`.
- **Build (Windows, this machine):**
  - `CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"`
  - build: `"$CMAKE" --build build --config RelWithDebInfo`
  - tests: `"${CMAKE%cmake.exe}ctest.exe" --test-dir build -C RelWithDebInfo --output-on-failure`
- **CI** also builds on Linux with GCC (`.github/workflows/build-linux.yml`). Forced include there is `-include`, and the symbol tool is `nm`.
- **Compiler warnings:** vendored spine-c is compiled with warnings off (`/W0` or `-w`). Plugin code keeps `/W4` or `-Wall -Wextra -Wpedantic` with no new warnings.
- **Framing constants** (from the 4.1 web player):
  - 100 samples at `i * duration / 100`;
  - padding `10%` of width and height on each side;
  - contain-fit, centred.
- **Blending:**
  - Colour blending per mode:

    | Mode | Blend |
    |---|---|
    | Normal | `ONE, INVSRCALPHA` |
    | Additive | `ONE, ONE` |
    | Multiply | `DSTCOLOR, INVSRCALPHA` |
    | Screen | `ONE, INVSRCCOLOR` |

  - Alpha is always `ONE, INVSRCALPHA`.
  - Final composite: framebuffer sRGB on, `gs_effect_set_texture_srgb`, `ONE, INVSRCALPHA`.
- **Sampler:** `GS_FILTER_ANISOTROPIC`, `max_anisotropy = 16`, `GS_ADDRESS_CLAMP`.
- **Atlas pixels** are used as stored and treated as premultiplied.
- **Log message wording:**
  - fallback: `native renderer supports Spine 4.1 only for now; using the browser renderer for <version>`
  - unported controls: `<feature> is not supported by the native renderer yet` (logged once per source and feature)

## Review Focus

1. **Quick changes.** The skeleton or atlas path is changed several times quickly, or the renderer is switched while a load is running. Expected: only the latest request is shown, nothing leaks, and nothing crashes. Covered by the Task 7 generation-discard test and the Task 9 OBS checklist.
2. **Unusual atlas pages.** Non-power-of-two pages, a 1×N page, or a page larger than the GPU limit. Expected: resized correctly or a clear error, never a crash. Covered by Task 3 tests and Task 7's size check.
3. **Non-ASCII asset paths** (Windows usernames and folder names often aren't ASCII). Expected: loads work. spine-c's default `fopen` would fail, so `_spUtil_readFile` must use OBS-style UTF-8 file reading through the decoder or reader the renderer supplies. Covered by a Task 5 test using a UTF-8 path.
4. **Size and default-animation changes, and the runtime override.** Changing the source size or default animation must reframe without reloading. A user-set **runtime** override (`runtime` setting `4.0`, `3.7`) must be respected: anything other than `auto` or `4.1` falls back to browser. Covered by Task 8.
5. **Skeletons with no animations, or zero-duration or invalid bounds.** Expected: setup pose, one bounds sample, a warning, and no division by zero. Covered by Task 6 and Task 7 tests.

---

## File Structure

| File | Responsibility |
|---|---|
| `third_party/spine-c-4.1/{include,src,LICENSE}` | Vendored upstream spine-c (read-only) |
| `third_party/spine-c-4.1/VERSION` | One line: the pinned commit hash |
| `tests/fixtures/spine41/spineboy-pro.{skel,json}`, `spineboy-pma.{atlas,png}` | Upstream example export at the same commit |
| `cmake/spine-runtime.cmake` | `spine_add_backend(<ver> <prefix>)`: builds the `spine-backend-<ver>` static library with the forced include, plus a prefix-check test |
| `tools/spine-symbols.js` | Shared parser for `dumpbin /symbols` and `nm` output (exports `parseSymbols(text, tool)`) |
| `tools/generate-spine-prefix.js` | CLI: symbol dump → `#define` header |
| `tools/check-spine-prefix.js` | CLI used by CTest: runs the tool on the library and fails on unprefixed defined externals |
| `src/native/spine41-prefix.h` | Generated header, committed |
| `src/native/atlas-image.{c,h}` | Power-of-two resize and premultiplied mip chain (pure) |
| `src/native/skeleton-version.{c,h}` | Version family detection for `.json` and `.skel` (pure) |
| `src/native/framing.{c,h}` | Padding and contain-fit maths (pure) |
| `src/native/spine-backend.h` | Version-neutral interface |
| `src/native/backend-41.c` | spine-c 4.1 adapter |
| `src/native/native-renderer.{c,h}` | Loader thread, GPU resources, draw, composite |
| `data/effects/spine.effect` | Two-colour tint shader |
| `tests/atlas-image-test.c`, `tests/skeleton-version-test.c`, `tests/framing-test.c`, `tests/backend-41-test.c`, `tests/spine-symbols.test.js` | Tests |
| `tools/generate-native-reference.js`, `tests/fixtures/spine41/reference.h` | Parity reference produced by the bundled 4.1 web runtime |
| `src/spine-source.c`, `data/locale/en-US.ini`, `CMakeLists.txt` | Integration |
| `README.md`, `docs/architecture.md`, `THIRD_PARTY_NOTICES.md`, `docs/performance.md` | Docs |

---

### Task 1: Vendor spine-c 4.1 and the fixture, build an unprefixed library

**Files:**
- Create: `third_party/spine-c-4.1/**`, `third_party/spine-c-4.1/VERSION`, `tests/fixtures/spine41/*`, `cmake/spine-runtime.cmake`
- Modify: `CMakeLists.txt`, `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Produces: the CMake target `spine-c-41-raw`, an `EXCLUDE_FROM_ALL` static library of `third_party/spine-c-4.1/src/spine/*.c` with no forced include. It is used only to dump symbols. Include dir: `third_party/spine-c-4.1/include`.

- [ ] **Step 1: Vendor the runtime and fixture**

Use a scratch clone (`git clone --filter=blob:none --sparse https://github.com/EsotericSoftware/spine-runtimes`, then `git checkout 77a5db0ec6d16331f5efbaa7662bba9355bd3424`, then sparse-add `spine-c/spine-c` and `examples/spineboy/export`) in the scratchpad.
- Copy `spine-c/spine-c/include` and `src` into `third_party/spine-c-4.1/`.
- Copy the repository `LICENSE` into `third_party/spine-c-4.1/LICENSE`, and write the hash into `VERSION`.
- Copy `spineboy-pro.skel`, `spineboy-pro.json`, `spineboy-pma.atlas` and `spineboy-pma.png` into `tests/fixtures/spine41/`.

- [ ] **Step 2: Add `cmake/spine-runtime.cmake`, include it from `CMakeLists.txt`, and define `spine-c-41-raw`**

Compile vendored sources with `/W0` (MSVC) or `-w`, using `c_std_11`. Mark the target `EXCLUDE_FROM_ALL`.

- [ ] **Step 3: Build it**

Run: `"$CMAKE" -S . -B build && "$CMAKE" --build build --config RelWithDebInfo --target spine-c-41-raw`
Expected: the build succeeds and `build/RelWithDebInfo/spine-c-41-raw.lib` exists.

- [ ] **Step 4: Update `THIRD_PARTY_NOTICES.md`**

Add a paragraph naming `third_party/spine-c-4.1` (spine-c, upstream `4.1` branch, commit `77a5db0…`) and the Spineboy fixture, both under the Spine Runtimes License at `third_party/spine-c-4.1/LICENSE`.

- [ ] **Step 5: Commit** — `chore: vendor spine-c 4.1 and Spineboy 4.1 fixture`

### Task 2: Symbol prefix generator, prefixed library and prefix check

**Files:**
- Create: `tools/spine-symbols.js`, `tools/generate-spine-prefix.js`, `tools/check-spine-prefix.js`, `tests/spine-symbols.test.js`, `src/native/spine41-prefix.h`, `src/native/backend-41.c` (stub in this task)
- Modify: `cmake/spine-runtime.cmake`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `parseSymbols(text, tool) -> { defined: string[], undefined: string[] }`, where `tool` is `'dumpbin'` or `'nm'`. It strips the single leading `_` that Mach-O `nm` adds when the dump is from macOS (option `{ machO: true }`).
  - `prefixNames(parsed, prefix) -> string[]`: every defined external name, plus undefined names matching `/^_sp/`. The second group covers the host hooks `_spAtlasPage_createTexture`, `_spAtlasPage_disposeTexture` and `_spUtil_readFile`, which the library references but the backend defines. The result is sorted and deduplicated.
  - The CMake function `spine_add_backend(VERSION 41 PREFIX sp41_ SOURCES src/native/backend-41.c)` creates the static library `spine-backend-41` (vendored sources plus the backend sources, forced include `src/native/spine41-prefix.h`) and the test `spine41-prefix-check`.

- [ ] **Step 1: Write the failing JS tests** in `tests/spine-symbols.test.js` (node:test):
  - `parses dumpbin externals`: input lines `008 00000000 SECT3  notype ()    External     | spSkeleton_create` and `00A 00000000 UNDEF  notype ()    External     | _spUtil_readFile` and `00B 00000000 SECT1  notype       Static       | $SG123`. Expect `defined == ['spSkeleton_create']` and `undefined == ['_spUtil_readFile']`.
  - `parses nm output`: input `0000000000000010 T spSkeleton_create\n                 U _spUtil_readFile\n0000000000000000 D spDefaultCurve\n                 U malloc`. Expect `defined` to equal `['spDefaultCurve','spSkeleton_create']` once sorted, and `undefined` to contain `_spUtil_readFile` and `malloc`.
  - `strips Mach-O underscore`: with `{ machO: true }`, `T _spSkeleton_create` becomes `spSkeleton_create`.
  - `prefix list keeps host hooks, drops libc`: `prefixNames({defined:['Json_create'], undefined:['_spUtil_readFile','malloc']}, 'sp41_')` equals `['Json_create','_spUtil_readFile']`.

- [ ] **Step 2: Run `node --test tests/spine-symbols.test.js` and confirm it fails** (module not found)

- [ ] **Step 3: Implement `tools/spine-symbols.js`**, then the two CLIs:
  - `generate-spine-prefix.js --tool dumpbin|nm --prefix sp41_ --out src/native/spine41-prefix.h <dump.txt>` writes an include-guarded header with one `#define NAME sp41_NAME` per name, and a comment naming the vendored commit.
  - `check-spine-prefix.js --tool <path to dumpbin or nm> --prefix sp41_ --allow spine_backend_41 <library>` runs the tool (`/symbols` or `-g --defined-only`), lists the bad names, and exits 1 if there are any.

- [ ] **Step 4: Run the JS tests and confirm they pass**

- [ ] **Step 5: Generate the header**

Run `dumpbin /symbols build/RelWithDebInfo/spine-c-41-raw.lib > <scratch>/sp41.txt`, then `node tools/generate-spine-prefix.js --tool dumpbin --prefix sp41_ --out src/native/spine41-prefix.h <scratch>/sp41.txt`. dumpbin is in the same directory as `cl.exe`.

Expected: the header contains `#define spSkeleton_create sp41_spSkeleton_create`, `#define Json_create sp41_Json_create` and `#define _spUtil_readFile sp41__spUtil_readFile`.

- [ ] **Step 6: Add `spine_add_backend` and register the check**
  - Create a stub `backend-41.c` that defines the three host hooks (returning failure) and `const struct spine_backend *spine_backend_41(void) { return NULL; }`.
  - Locate the symbol tool: `find_program` for `dumpbin` with `HINTS` from the directory of `CMAKE_C_COMPILER` (MSVC), otherwise `CMAKE_NM`.
  - Register `add_test(NAME spine41-prefix-check COMMAND node tools/check-spine-prefix.js …)` when Node is found.
  - Link `spine-backend-41` into `obs-spine-player`.

- [ ] **Step 7: Build and run CTest**

Expected:
- the plugin builds;
- `spine41-prefix-check` passes;
- temporarily deleting one `#define` from the header makes it fail (restore it afterwards);
- all existing tests pass.

- [ ] **Step 8: Commit** — `build: isolate spine-c 4.1 symbols behind an sp41_ prefix`

### Task 3: Atlas image processing

**Files:**
- Create: `src/native/atlas-image.h`, `src/native/atlas-image.c`, `tests/atlas-image-test.c`
- Modify: `CMakeLists.txt` (add the test executable `atlas-image-tests` and add the source to the plugin)

**Interfaces:**
- Produces:
  - `uint32_t atlas_image_next_pow2(uint32_t v);` with `1→1`, `3→4`, `4→4`, `1000→1024`.
  - `uint8_t *atlas_image_resize_pow2(const uint8_t *src, uint32_t w, uint32_t h, uint32_t *out_w, uint32_t *out_h);` does bilinear resizing on 4-byte pixels (channel order preserved). It returns a `malloc`-ed buffer, or `NULL` on allocation failure. When `w` and `h` are already powers of two, it returns a copy.
  - `uint32_t atlas_image_level_count(uint32_t w, uint32_t h);` returns `1 + floor(log2(max(w,h)))`.
  - `uint8_t **atlas_image_build_mips(const uint8_t *level0, uint32_t w, uint32_t h, uint32_t *out_levels);` takes power-of-two input. Each level is `max(1,w>>i) × max(1,h>>i)`, using a 2×2 box filter that averages each byte channel with rounding (`(a+b+c+d+2)/4`). When a dimension has already reached 1, it averages the two texels along the other axis. Level 0 is a copy. Everything is `malloc`-ed.
  - `void atlas_image_free_mips(uint8_t **levels, uint32_t count);`

- [ ] **Step 1: Write the failing tests** in `tests/atlas-image-test.c`, in the style of `tests/level-gate-test.c` (plain `assert`-style checks, non-zero exit on failure):
  - `next_pow2`: the values above.
  - `level_count(256,64) == 9`; `level_count(1,1) == 1`.
  - Resizing 3×5 gives 4×8. A uniform-colour input stays uniform (every byte equals the input).
  - Mips of a 2×2 checker with premultiplied texels `{255,255,255,255}` and `{0,0,0,0}` give a 1×1 level 1 equal to `{128,128,128,128}`.
  - Mips of an 8×1 page give 4 levels with sizes 8, 4, 2, 1 (all height 1).
  - Allocation-failure path: not tested (only `NULL` propagates).

- [ ] **Step 2: Build and confirm the tests fail** (link errors)
- [ ] **Step 3: Implement `atlas-image.c`**
- [ ] **Step 4: Build, run `atlas-image-tests`, and confirm they pass**
- [ ] **Step 5: Commit** — `feat(native): power-of-two resize and premultiplied mip chain`

### Task 4: Skeleton version detection

**Files:**
- Create: `src/native/skeleton-version.h`, `src/native/skeleton-version.c`, `tests/skeleton-version-test.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `skeleton_binary_detect` from `src/skeleton-binary.h`.
- Produces:
  - `enum spine_family { SPINE_FAMILY_UNKNOWN = 0, SPINE_FAMILY_37, SPINE_FAMILY_40, SPINE_FAMILY_41 };`
  - `enum spine_family skeleton_version_detect(const uint8_t *data, size_t size, bool is_json, char *version, size_t version_size);`
    - JSON: find the first `"spine"` key and read its string value. The family is chosen by the `3.7`, `4.0` or `4.1` prefix followed by `.` or the end of the string, the same rule as `runtimeFamily` in `version-detector.js`.
    - Binary: map `SKELETON_BINARY_37/40/41`.
  - `enum spine_family spine_family_from_setting(const char *runtime_setting);` maps `"3.7"`, `"4.0"`, `"4.1"` to their families, and `"auto"`, `""` or `NULL` to `UNKNOWN`.

- [ ] **Step 1: Write the failing tests:**
  - `{"skeleton":{"hash":"x","spine":"4.1.24"}}` gives `41` with version `"4.1.24"`.
  - `"spine":"4.0.64"` gives `40`; `"3.7.94"` gives `37`; `"4.2.10"` gives `UNKNOWN` with version `"4.2.10"`.
  - Binary: `tests/fixtures/spine41/spineboy-pro.skel` gives `41`, and `tests/fixtures/spine37-synthetic.skel` gives `37`.
  - Garbage bytes give `UNKNOWN`.
  - `spine_family_from_setting("auto") == UNKNOWN` and `("4.0") == SPINE_FAMILY_40`.
- [ ] **Step 2: Confirm the tests fail.** **Step 3: Implement.** **Step 4: Confirm the tests pass.**
- [ ] **Step 5: Commit** — `feat(native): detect Spine version family from skeleton files`

### Task 5: Backend interface and 4.1 loading and animation

**Files:**
- Create: `src/native/spine-backend.h`, `tests/backend-41-test.c`
- Modify: `src/native/backend-41.c` (replacing the stub), `CMakeLists.txt` (`backend-41-tests` links `spine-backend-41` and `atlas-image.c`, with `TEST_FIXTURE_DIR`)

**Interfaces:**
- Consumes: the Task 3 `atlas_image_*` functions.
- Produces, in `spine-backend.h`. This is the exact contract every later task and stage uses:

```c
struct spine_allocator { void *(*alloc)(size_t); void *(*realloc)(void *, size_t); void (*free)(void *); };
struct spine_decoded_image { uint8_t *pixels; uint32_t width, height; bool bgra; }; /* pixels freed by decoder->free_pixels */
struct spine_image_decoder {
	void *param;
	bool (*decode)(void *param, const char *path, struct spine_decoded_image *out);
	void (*free_pixels)(void *param, uint8_t *pixels);
	/* Reads a whole file (UTF-8 path). Returns malloc'd bytes or NULL. */
	char *(*read_file)(void *param, const char *path, size_t *size);
};
struct spine_page_image { uint32_t width, height, levels; bool bgra; uint8_t **level_pixels; };
struct spine_draw_vertex { float x, y, u, v; uint32_t light; float dark[4]; }; /* light = ABGR8 premultiplied */
enum spine_blend { SPINE_BLEND_NORMAL, SPINE_BLEND_ADDITIVE, SPINE_BLEND_MULTIPLY, SPINE_BLEND_SCREEN };
struct spine_draw_batch { uint32_t page; enum spine_blend blend; uint32_t first_index, index_count; };
struct spine_draw_list {
	struct spine_draw_vertex *vertices; size_t vertex_count, vertex_capacity;
	uint32_t *indices; size_t index_count, index_capacity;
	struct spine_draw_batch *batches; size_t batch_count, batch_capacity;
};
struct spine_bounds { float x, y, width, height; };
struct spine_asset;
struct spine_instance;
struct spine_backend {
	const char *name;
	void (*set_allocator)(const struct spine_allocator *allocator);
	struct spine_asset *(*load)(const char *skeleton_path, const char *atlas_path,
				    const struct spine_image_decoder *decoder, char *error, size_t error_size);
	void (*free_asset)(struct spine_asset *asset);
	size_t (*page_count)(const struct spine_asset *asset);
	const struct spine_page_image *(*page_image)(const struct spine_asset *asset, size_t page);
	size_t (*animation_count)(const struct spine_asset *asset);
	const char *(*animation_name)(const struct spine_asset *asset, size_t index);
	struct spine_instance *(*create_instance)(struct spine_asset *asset);
	void (*free_instance)(struct spine_instance *instance);
	bool (*set_animation)(struct spine_instance *instance, int track, const char *name, bool loop);
	void (*update)(struct spine_instance *instance, float seconds);
	bool (*animation_bounds)(struct spine_instance *instance, const char *name, struct spine_bounds *out);
	bool (*build_draw_list)(struct spine_instance *instance, struct spine_draw_list *list);
};
const struct spine_backend *spine_backend_41(void);
```

`src/native/draw-list.{c,h}` provides `void spine_draw_list_free(struct spine_draw_list *list)`, together with `bool spine_draw_list_reserve(list, extra_vertices, extra_indices, extra_batches)` and `void spine_draw_list_clear(list)` (which resets the counts and keeps the capacity). It is compiled into both the plugin and the tests, not into the prefixed library.

**Behaviour that `backend-41.c` must implement:**
- **Host hooks.**
  - `_spUtil_readFile` uses `decoder->read_file`, never `fopen`.
  - `_spAtlasPage_createTexture` decodes through `decoder->decode`, then runs `atlas_image_resize_pow2` and `atlas_image_build_mips`, stores the result in the asset's page list, and sets `page->rendererObject` to the page index plus one (`(void *)(uintptr_t)(index + 1)`). It records failures in the asset's error buffer.
  - The decoder pointer is passed through a thread-local (`_Thread_local` or `__declspec(thread)`) set for the duration of `load`, so concurrent loads in different sources are safe.
- **Loading:**
  - `spAtlas_createFromFile`;
  - `spSkeletonBinary` for a `.skel` extension, `spSkeletonJson` otherwise, with scale 1;
  - on failure, copy `binary->error` or `json->error` into `error`.
- **`set_allocator`** calls `_spSetMalloc`, `_spSetRealloc` and `_spSetFree`. It is a no-op when the argument is `NULL`.
- **`set_animation`** resolves the name exactly, then case-insensitively. If neither matches it returns false and changes nothing.
- **`update`** runs `spAnimationState_update`, then `spAnimationState_apply`, then `spSkeleton_updateWorldTransform`.

- [ ] **Step 1: Write the failing tests** in `tests/backend-41-test.c`, using a stub decoder that returns a zeroed `width×height` image. It parses the `size:` line of the atlas or falls back to 1024×1024, and `read_file` uses `fopen`, except in the UTF-8 test, which uses `os_fopen`-style conversion via `_wfopen` on Windows. Tests:
  - `loads skel`: `load(fixture/spineboy-pro.skel, fixture/spineboy-pma.atlas)` is non-NULL, `page_count == 1`, the page width and height are powers of two, and `levels == atlas_image_level_count(w,h)`.
  - `loads json`: the same with `spineboy-pro.json`.
  - `animation names`: the count and names equal the list `tools/generate-native-reference.js` will print, which includes `"idle"`, `"walk"`, `"run"`, `"jump"` and `"shoot"`.
  - `missing atlas`: returns NULL, and `error` is non-empty and contains the atlas path.
  - `corrupt skeleton`: a skeleton of 64 random bytes written to a temporary `.skel` returns NULL with a non-empty `error`.
  - `utf8 path`: copy the fixture into a temporary directory named `ñ-テスト`; loading succeeds.
  - `set_animation`: `"IDLE"` resolves (true); `"nope"` returns false.
  - `update`: after `set_animation("walk")` and `update(0.5f)`, the instance draws (checked in Task 6).
  - `concurrent`: two threads load simultaneously, and both succeed with independent pages.
- [ ] **Step 2: Confirm the tests fail.** **Step 3: Implement** `spine-backend.h`, `draw-list.c` and `backend-41.c` (loading and animation; `animation_bounds` and `build_draw_list` return false for now). **Step 4: Confirm the tests pass, and confirm `spine41-prefix-check` still passes.** If it doesn't, regenerate the header (Task 2, Step 5) after adding the new defined names. Only names from the vendored runtime belong in the header; backend functions should be `static`.
- [ ] **Step 5: Commit** — `feat(native): spine-c 4.1 backend loads assets and drives animation`

### Task 6: Draw lists, bounds, and parity with the web runtime

**Files:**
- Create: `tools/generate-native-reference.js`, `tests/fixtures/spine41/reference.h`
- Modify: `src/native/backend-41.c`, `tests/backend-41-test.c`

**Interfaces:**
- Produces: working `animation_bounds` and `build_draw_list`, with the semantics below.

**`build_draw_list`.** Follow `spine-sfml/c/src/spine/spine-sfml.cpp` on the `4.1` branch for traversal and clipping:
- go through slots in draw order;
- skip slots whose bone is inactive;
- regions: `spRegionAttachment_computeWorldVertices`, with stride 2 and the four-corner index pattern `0,1,2,2,3,0`;
- meshes: `spVertexAttachment_computeWorldVertices` with the mesh's triangles;
- clipping attachments: start or end `spSkeletonClipping`;
- clipped geometry: `spSkeletonClipping_clipTriangles`;
- the page comes from the attachment's `spAtlasRegion->page->rendererObject - 1`;
- blend comes from `slot->data->blendMode`.

Colours follow the spine-ts 4.1 `SkeletonRenderer` with `premultipliedAlpha = true`:
- `light = skeleton.color * slot.color * attachment.color`, then `rgb *= a`, packed as ABGR8;
- `dark`:
  - when `slot->darkColor` is set: `(dark.r*a, dark.g*a, dark.b*a, 1.0)`;
  - otherwise: `(0, 0, 0, 1.0)`.

Start a new batch when the page or blend mode changes. Clear the list at the start (`spine_draw_list_clear`) and reuse its capacity.

**`animation_bounds`.** Mirror `calculateAnimationViewport`:
- `spSkeleton_setToSetupPose` once;
- for `i` in `0..99`: `t = duration ? i*duration/100 : 0`, `spAnimation_apply(anim, skeleton, t, t, 0, NULL, NULL, 1, SP_MIX_BLEND_SETUP, SP_MIX_DIRECTION_IN)`, `spSkeleton_updateWorldTransform`, then union the bounds of all region and mesh world vertices on active bones. This is spine-ts 4.1 `getBounds` semantics, with no clipping.

Skip non-finite results. Return false if no finite sample exists. Afterwards, restore the pose: setup pose, reapply the instance's animation state, and update world transforms.

- [ ] **Step 1: Write `tools/generate-native-reference.js`**

It loads `data/player/runtime/4.1.20/spine-player.min.js` in a Node `vm` context with minimal `window` and `document` stubs. If the player bundle can't load headless, it reads only the `spine` core classes it exposes. It parses the Spineboy `.skel` with an atlas whose pages are fake 1024×1024 texture objects.

It writes `tests/fixtures/spine41/reference.h` containing:
- `REF_ANIMATION_NAMES[]`;
- `REF_BOUNDS_IDLE[4]` and `REF_BOUNDS_WALK[4]` (`calculateAnimationViewport` before padding);
- for `walk` at `t = 0.25`: `REF_WALK_VERTEX_COUNT` and `REF_WALK_VERTICES[]` (x, y of every vertex, in draw order, from the same traversal: regions as 4 vertices, meshes as world vertices, no clipping because Spineboy has no clipping).

Run: `node tools/generate-native-reference.js`. Expected: the header is written, and printing it shows non-zero bounds.

- [ ] **Step 2: Write the failing tests** in `backend-41-test.c`:
  - `bounds match web runtime`: `idle` and `walk` bounds equal `REF_BOUNDS_*` within `1e-3`.
  - `draw list matches web runtime`: after `set_animation(0,"walk",true)` and `update(0.25f)`, the vertex count equals `REF_WALK_VERTEX_COUNT` and every x, y is within `1e-3`.
  - `draw list well formed`: `batch_count ≥ 1`, every index is `< vertex_count`, batches cover the indices contiguously, every page is `< page_count`, and the light alpha byte is 255 for opaque Spineboy parts.
  - `reuse`: building twice gives the same counts without increasing capacity.
  - `empty skeleton`: a minimal JSON skeleton written to a temporary file (one bone, no slots, no animations) gives `animation_bounds` false and `build_draw_list` true with 0 batches.
- [ ] **Step 3: Confirm the tests fail.** **Step 4: Implement.** **Step 5: Confirm the tests pass and the prefix check passes.**
- [ ] **Step 6: Commit** — `feat(native): build draw lists and animation bounds matching the 4.1 web player`

### Task 7: Framing maths and the native renderer

**Files:**
- Create: `src/native/framing.{c,h}`, `tests/framing-test.c`, `src/native/native-renderer.{c,h}`, `data/effects/spine.effect`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct spine_bounds framing_pad(struct spine_bounds b);` adds `0.1*width` on the left and right and `0.1*height` on the top and bottom.
  - `bool framing_fit(struct spine_bounds padded, uint32_t width, uint32_t height, float *left, float *right, float *top, float *bottom);` contain-fits and centres. It returns world-space ortho edges for `gs_ortho(left, right, top, bottom, -100, 100)`, with `top > bottom` so Spine's Y-up becomes OBS's Y-down. It returns false for zero or non-finite inputs.
  - `native-renderer.h`:
    ```c
    struct native_renderer;
    struct native_renderer *native_renderer_create(obs_source_t *owner);
    void native_renderer_destroy(struct native_renderer *r);             /* joins loader, frees GPU in obs_enter_graphics */
    void native_renderer_load(struct native_renderer *r, const char *skeleton_path, const char *atlas_path,
                              enum spine_family forced_family);         /* UI thread, non-blocking, latest wins */
    void native_renderer_configure(struct native_renderer *r, const char *default_animation,
                                   uint32_t width, uint32_t height);    /* UI thread; reframes, no reload */
    void native_renderer_tick(struct native_renderer *r, float seconds); /* skips when !obs_source_showing(owner) */
    void native_renderer_render(struct native_renderer *r);             /* graphics thread */
    enum native_renderer_state { NATIVE_IDLE, NATIVE_LOADING, NATIVE_READY, NATIVE_FAILED, NATIVE_NEEDS_BROWSER };
    enum native_renderer_state native_renderer_state(struct native_renderer *r, char *version, size_t version_size);
    ```
  - `src/native/load-gate.h`: `static inline bool native_load_is_current(uint64_t posted, uint64_t latest)`. The renderer uses it to discard stale loads, and `framing-test.c` tests it.

**Renderer behaviour** (spec sections "Data flow per frame", "Textures and anti-aliasing", "Loading, threads and lifetime"):
- **Loader thread:** `pthread` from `util/threading.h`, `os_event_t` to wake, `pthread_mutex_t` for request and result slots.
  - Version: `forced_family` if it is not `UNKNOWN`, otherwise `skeleton_version_detect`.
  - Anything other than `SPINE_FAMILY_41` posts `NEEDS_BROWSER` with the version string.
- **Decoder:** `gs_image_file_init` and `texture_data`, `bgra = (format == GS_BGRA)`, freed with `gs_image_file_free`. `read_file` uses `os_fopen` plus a read of the whole file into `bmalloc`. The backend allocator is set once to `bmalloc`, `brealloc` and `bfree`.
- **Render:**
  1. adopt a posted result if `native_load_is_current`;
  2. on first use, create the effect from `obs_module_file("effects/spine.effect")`, and the sampler state;
  3. textures: `gs_texture_create(w, h, bgra ? GS_BGRA : GS_RGBA, levels, (const uint8_t **)level_pixels, 0)`, failing when `w` or `h` is larger than `gs_get_texture_max_size()` (or 16384 if that returns 0);
  4. dynamic VB and IB (`GS_DYNAMIC`), grown by doubling;
  5. draw into a `gs_texrender` (`GS_RGBA`, `GS_ZS_NONE`) at source size, with sRGB off and cleared to 0;
  6. per batch: `gs_blend_function_separate`, then `gs_draw(GS_TRIS, first_index, index_count)` with the IB loaded;
  7. composite as in the Global Constraints.
- **Unported controls:** none in the renderer. `spine-source.c` handles the notices.

- [ ] **Step 1: Write the failing `tests/framing-test.c`:**
  - `pad({0,0,100,200}) == {-10,-20,120,240}`.
  - Fitting `{-10,-20,120,240}` into 1920×1080: the height limits it, so the world height spans 240 and the width is `240*1920/1080`, centred on x = 50. Expect `left == 50 - 213.333`, `right == 50 + 213.333`, `top == 220`, `bottom == -20` (within 1e-3).
  - Fit with a zero-width bound returns false; a NaN bound returns false.
  - `native_load_is_current(3,3)` is true; `(2,3)` is false.
- [ ] **Step 2: Confirm the tests fail.** **Step 3: Implement `framing.c` and `load-gate.h`.** **Step 4: Confirm the tests pass.**
- [ ] **Step 5: Write `data/effects/spine.effect`**

It has one technique, `Draw`, with vertex input `float4 pos : POSITION; float4 color : COLOR; float2 uv : TEXCOORD0; float4 dark : TEXCOORD1;`. The pixel shader is the spec formula, with the light colour taken from `color` (OBS unpacks ABGR8 to float4 RGBA). The sampler is set in C. Use `ViewProj` and `image` in the same way as OBS's `default.effect`.

- [ ] **Step 6: Implement `native-renderer.c`**

Build the plugin. Expected: it compiles with no warnings under `/W4`.

- [ ] **Step 7: Commit** — `feat(native): libobs renderer with mipmapped atlas textures`

### Task 8: Integrate the Renderer setting in the source

**Files:**
- Modify: `src/spine-source.c`, `data/locale/en-US.ini`, `CMakeLists.txt` (install `data/effects`; it's already covered by `install(DIRECTORY data/ …)`, so verify)

**Interfaces:**
- Consumes: `native-renderer.h` (Task 7), `spine_family_from_setting` (Task 4).

**Changes:**
- `#define SETTING_RENDERER "renderer"`; the default is `"browser"`; there's a list property with items `Renderer.Browser`→`browser` and `Renderer.Native`→`native`, placed directly above the runtime property.
- `struct spine_source` gains:
  - `char renderer[16]`;
  - `struct native_renderer *native`;
  - `pthread_mutex_t browser_mutex`;
  - `bool native_fallback`;
  - logged-once flags for each unported control (`yap`, `eye tracking`, `emotion states`, `hotkeys`).
- `update`:
  - if the effective renderer is native (setting is `native` and `!native_fallback`):
    - make sure `native` exists;
    - call `native_renderer_load` when the skeleton or atlas path or the runtime setting changed;
    - call `native_renderer_configure` every time;
    - release the browser source under `browser_mutex`.
  - Otherwise:
    - destroy `native`;
    - (re)create the browser source under the mutex if it's missing;
    - keep all existing code paths, including `send_configuration`.
  - When the user changes the paths or switches the setting, reset `native_fallback` to false.
  - Log the unported-control notices when native is active and the control is enabled.
- `tick`: when native is active, call `native_renderer_tick`. Check `native_renderer_state`: on `NATIVE_NEEDS_BROWSER`, log the fallback message with the version, set `native_fallback = true`, and switch to the browser path in the same way `update` does. The browser source is then created on this graphics-thread tick under the mutex; OBS allows private source creation there, and obs-browser itself creates sources from tick. The existing audio, cursor and configuration-timer code stays, but `send_*` calls are made only when a browser source exists.
- `render`: when native, `native_renderer_render`. Otherwise, take `obs_source_get_ref(browser)` under the mutex, render it, then release.
- `destroy`: destroy `native`, then destroy the mutex.
- `get_width` and `get_height` are unchanged.
- `en-US.ini`: `Renderer="Renderer"`, `Renderer.Browser="Browser (compatible)"`, `Renderer.Native="Native (faster, Spine 4.1 only for now)"`.

- [ ] **Step 1: Implement the changes above**

Build, then run all CTest and JS tests. Expected: everything passes and there are no new warnings.

- [ ] **Step 2: Commit** — `feat: Renderer setting with native Spine 4.1 path and browser fallback`

### Task 9: Real OBS verification, measurement and docs

**Files:**
- Create: `docs/performance.md`
- Modify: `README.md` (Renderer setting, native limits, anti-aliasing note), `docs/architecture.md` (native path section and component list), `THIRD_PARTY_NOTICES.md` (already done in Task 1; check it)

- [ ] **Step 1: Install into the portable OBS**

Run `"$CMAKE" --install build --config RelWithDebInfo --prefix .cache/obs-test`. Confirm `obs-plugins/64bit/obs-spine-player.dll` and `data/obs-plugins/obs-spine-player/effects/spine.effect` exist there.

- [ ] **Step 2: Scene checklist**

Start `.cache/obs-test/bin/64bit/obs64.exe` (portable mode). Create two sources on the Spineboy fixture: one Browser, one Native. Take screenshots, using OBS's Screenshot (Output) hotkey or obs-websocket if it's enabled, and compare. Check:
- framing and position match;
- colours match;
- the animation plays;
- the Native edges are smoother when the source is scaled down to 25%.

- [ ] **Step 3: Stress checklist**
  - Switch the renderer back and forth 5 times.
  - Change the skeleton path 3 times quickly.
  - Point Native at `tests/fixtures/spine37-synthetic.skel` with an atlas, or a 4.0 sample from `Characters/`, and confirm the fallback log line.
  - Hide and show the source.
  - Change the default animation and size.
  - Close OBS.

  Then read the newest file in `.cache/obs-test/config/obs-studio/logs/`. Expected: no `error` lines from `obs-spine-player` other than intentional ones, no crash, and `Number of memory leaks: 0`.

- [ ] **Step 4: Measure**

Use a 1920×1080 canvas at 60 fps, a single source per run, the same character, 60 seconds per run, and `typeperf` sampling every second:
- `\Process(obs64*)\% Processor Time`;
- `\Process(obs-browser-page*)\% Processor Time`;
- `\GPU Engine(*engtype_3D)\Utilization Percentage`.

Also note OBS's "Average time to render frame" from the stats dock. Write `docs/performance.md` with the hardware, method, a raw-numbers table for Browser and Native, and a one-paragraph reading. Report numbers as measured. If a measurement could not be taken, say so there.

- [ ] **Step 5: Update `README.md` and `docs/architecture.md`, then commit** — `docs: native renderer usage, architecture and measurements`
