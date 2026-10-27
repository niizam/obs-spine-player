# Native renderer, stage 1: Spine 4.1 core

Branch: `feature/native-renderer`. Status: approved design (2026-10-27).

## Why

Some users run OBS on weak PCs and struggle with the plugin. Today every character is drawn by a private OBS Browser Source (Chromium/CEF): a separate GPU process, a WebGL context and a frame handoff back to OBS for each source. Drawing the skeleton with the Spine C runtime directly through OBS's own graphics API removes all of that. The rewrite is also where anti-aliasing (the original request) gets done properly.

## Staged delivery

The rewrite is split into stages, each with its own spec, plan and implementation:

1. **This spec.** Native core for Spine 4.1, the Renderer dropdown, mipmapped textures, and before/after measurements.
2. Port the controls to C: emotion state machine and hotkeys, yap mode with the derived mouth overlay, cursor eye tracking.
3. Anti-aliasing quality setting: Off / FXAA / 1.5x / 2x supersampling.
4. Spine 4.0 and 3.7 backends behind the same interface.
5. Remove the browser renderer once native has parity.

## Goals and success criteria

- With **Renderer: Native**, a Spine 4.1 character plays its default animation with the same framing, colours, blend modes, two-colour tint and clipping as **Renderer: Browser**.
- In Native mode no browser source is created.
- Measured on the same machine, scene and character, Native uses less CPU and less GPU time than Browser. Both sets of numbers are recorded in `docs/performance.md`.
- Edges and fine lines no longer shimmer when a large atlas is drawn small (mipmaps plus trilinear/anisotropic filtering).
- Browser mode behaves exactly as before. The existing JS and C tests still pass.

## Out of scope for stage 1

- Emotions, hotkeys, yap mode, mouth overlay and eye tracking in Native mode (stage 2). If any of them is enabled while Native is active, the plugin logs one notice that it is not yet supported natively.
- FXAA and supersampling (stage 3).
- Spine 4.0 and 3.7 in Native mode (stage 4). They fall back to the browser renderer automatically.

## Verified facts this design relies on

Each fact was checked against the OBS 31.1.1 sources in `.github/ci/.deps/obs-studio-31.1.1` or the upstream Spine repository.

- **No MSAA in libobs.** `gs_texrender_create(format, zsformat)` has no sample count, and every D3D11 texture and render target is created with `SampleDesc.Count = 1`.
- **libobs never generates mipmaps.** With `GS_BUILD_MIPMAPS`, the D3D11 backend sets `MipLevels = 0` and `InitSRD` reads one caller-supplied buffer per level. Neither backend calls `GenerateMips` or `glGenerateMipmap`. `gs_texture_create` also drops mipmaps for non-power-of-two textures and for render targets.
- **Anisotropic filtering exists.** `GS_FILTER_ANISOTROPIC` and `gs_sampler_info.max_anisotropy`.
- **Vertex buffers** (`gs_vb_data`) carry points, one `uint32` colour and any number of texture-coordinate arrays of width 1 to 4.
- **How obs-browser composites** (`.cache/obs-browser/obs-browser-source.cpp:609`):
  - framebuffer sRGB is enabled;
  - the texture is set with `gs_effect_set_texture_srgb`;
  - blending is `ONE, INVSRCALPHA`.
- **How the 4.1 web player handles pixels.** It uploads atlas PNG pixels unmodified (`UNPACK_PREMULTIPLY_ALPHA_WEBGL` is false) and draws with `premultipliedAlpha: true`, which the plugin sets. In practice, page pixels are treated as already premultiplied.
- **How the 4.1 web player frames** (`calculateAnimationViewport`):
  - it samples the animation at 100 evenly spaced times from the setup pose;
  - it unions `skeleton.getBounds()` across those samples;
  - it pads 10% on each side;
  - it fits the result into the canvas.
- **spine-c exists** on the upstream `3.7`, `4.0` and `4.1` branches, with `SkeletonBinary.c` and `SkeletonJson.c`. Stage 1 vendors the 4.1 branch at commit `77a5db0ec6d16331f5efbaa7662bba9355bd3424`.
- **spine-c 4.1 has no `spSkeleton_getBounds`.** The backend implements bounds the same way as spine-ts 4.1 `Skeleton.getBounds`: the world vertices of every region and mesh attachment on an active bone.
- **spine-c names are global.** These would collide when 4.0 and 3.7 are added:
  - `sp*` functions;
  - the unprefixed `Json_*` functions;
  - the host callbacks `_spAtlasPage_createTexture`, `_spAtlasPage_disposeTexture` and `_spUtil_readFile`.

## Architecture

### User-facing behaviour

- **New setting:** **Renderer**, with values `browser` (the default) and `native`, and locale strings `Renderer`, `Renderer.Browser` and `Renderer.Native`.
- **Native with a 4.1 skeleton:** native drawing, with no browser source.
- **Native with a skeleton that isn't 4.1:** the plugin logs `native renderer supports Spine 4.1 only for now; using the browser renderer for <version>` and uses the browser renderer.
- **Browser:** unchanged.

### Components

| Unit | Purpose | Depends on |
|---|---|---|
| `third_party/spine-c-4.1/` | Upstream spine-c (`include/`, `src/`), pinned, unmodified, with its LICENSE | nothing |
| `src/native/spine41-prefix.h` | Generated header of `#define name sp41_name` lines for every external symbol the 4.1 runtime defines | nothing |
| `tools/generate-spine-prefix.js` | Produces the prefix header from the static library's symbol table (`dumpbin /symbols` or `nm -g --defined-only`); rerun only when a vendored runtime changes | Node |
| `src/native/spine-backend.h` | Version-neutral interface. Plain C types only, no Spine headers. | nothing |
| `src/native/backend-41.c` | The interface implemented on spine-c 4.1, plus the host callbacks. The only file that includes Spine 4.1 headers. | spine-c 4.1 (prefixed), `atlas-image` |
| `src/native/atlas-image.c` | CPU pixel work: resize to a power of two (bilinear), build the mip chain (2×2 box filter on premultiplied RGBA) | nothing |
| `src/native/skeleton-version.c` | Version detection for `.json` (reads the `"spine"` field) and `.skel` (the existing `skeleton_binary_detect`) | `skeleton-binary` |
| `src/native/native-renderer.c` | libobs side: owns the loader thread, GPU textures, dynamic vertex and index buffers, the effect, the offscreen texrender, framing and compositing | libobs, backend interface |
| `data/effects/spine.effect` | Two-colour tint shader in OBS effect format | — |

`spine-source.c` keeps every OBS integration it has today (properties, hotkeys, audio, cursor). It gains the renderer setting and branches to the browser source or the native renderer in `create`, `update`, `video_tick`, `video_render` and `destroy`.

### The backend interface

`spine-backend.h` (shape; exact names settled in the plan):

```c
struct spine_draw_vertex { float x, y, u, v; uint32_t light; uint32_t dark; };
enum spine_blend { SPINE_BLEND_NORMAL, SPINE_BLEND_ADDITIVE, SPINE_BLEND_MULTIPLY, SPINE_BLEND_SCREEN };
struct spine_draw_batch { uint32_t page; enum spine_blend blend; uint32_t first_index, index_count; };
struct spine_page_image { uint32_t width, height, levels; bool bgra; uint8_t **level_pixels; /* 4 bytes/pixel, mip 0..n */ };

struct spine_backend {
	const char *name; /* "4.1" */
	/* decode_image returns 4-byte pixels (bgra set accordingly) or false; the backend then resizes and mips. */
	struct spine_asset *(*load)(const char *skeleton_path, const char *atlas_path,
				    const struct spine_image_decoder *decoder, char *error, size_t error_size);
	void (*free_asset)(struct spine_asset *);
	size_t (*page_count)(const struct spine_asset *);
	const struct spine_page_image *(*page_image)(const struct spine_asset *, size_t page);
	size_t (*animation_count)(const struct spine_asset *);
	const char *(*animation_name)(const struct spine_asset *, size_t index);
	struct spine_instance *(*create_instance)(struct spine_asset *);
	void (*free_instance)(struct spine_instance *);
	bool (*set_animation)(struct spine_instance *, int track, const char *name, bool loop);
	void (*update)(struct spine_instance *, float seconds);
	bool (*animation_bounds)(struct spine_instance *, const char *name, float out[4]); /* x, y, w, h */
	/* Fills caller-owned growable arrays; returns false on allocation failure. */
	bool (*build_draw_list)(struct spine_instance *, struct spine_draw_list *out);
};
const struct spine_backend *spine_backend_41(void);
```

`struct spine_image_decoder` is a function pointer plus a user pointer. The renderer supplies one built on `gs_image_file_init`. Unit tests supply a stub that returns a blank image at the size the atlas declares. This keeps the backend and its tests free of libobs.

An asset is immutable after loading: skeleton data, atlas, decoded page images and the mip chain. An instance holds the per-source skeleton and animation state. Stages 2 to 4 add functions to this interface (bone and slot lookup, timeline access) and further implementations (`backend-40.c`, `backend-37.c`), without changing the renderer.

### Symbol isolation (approach A)

- spine-c 4.1 builds as the static library `spine-c-41`, with `/FI spine41-prefix.h` (MSVC) or `-include spine41-prefix.h` (GCC and Clang) on every source file.
- `backend-41.c` is compiled with the same forced include, so its calls and its host-callback definitions get the same prefix.
- The header renames every external symbol the library defines, including `Json_*` and the `_sp*` host hooks. Type names don't need renaming because they never meet across translation units: only the backend files include Spine headers.
- A CTest check runs `dumpbin` or `nm` over `spine-c-41` and `backend-41.c.o` and fails if any defined external symbol lacks the `sp41_` prefix. Two exceptions: the `spine_backend_41` entry point and the deliberately exported backend functions.
- The plugin still builds as one module. The project language stays C (C11).

### Data flow per frame

1. **`video_tick(seconds)`.**
   - If the source isn't showing (`obs_source_showing` is false), return. A hidden character costs nothing.
   - Otherwise, `update(instance, seconds)`: animation state update, apply, then world transforms.
2. **`video_render`:**
   1. Pick up a finished load, if there is one (see Loading).
   2. `build_draw_list` returns vertices, 16-bit or 32-bit indices, and batches. A new batch starts whenever the page or blend mode changes. Clipping goes through `spSkeletonClipping`, and two-colour tint uses the slot's dark colour. With no dark colour, the dark colour is black with alpha 0.
   3. Copy into one dynamic vertex buffer (`GS_DYNAMIC`) and one index buffer, both kept between frames:
      - the points carry the positions;
      - the colours array carries the light colour;
      - `tvarray[0]` (width 2) carries the UVs;
      - `tvarray[1]` (width 4) carries the dark colour as floats.

      They grow by doubling and never shrink.
   4. Begin a texrender sized to the source (`GS_RGBA`, no depth), and clear it to transparent. Framebuffer sRGB is **off**, so blending happens on gamma-encoded values as in WebGL.
   5. Set the projection with `gs_ortho` from the framing rectangle, with Y flipped because Spine is Y-up.
   6. For each batch:
      - set the page texture;
      - set the blend state (below);
      - `gs_draw(GS_TRIS, …)`.
   7. Composite the texrender texture into the scene the way obs-browser does: framebuffer sRGB on, `gs_effect_set_texture_srgb`, `ONE, INVSRCALPHA`, and `obs_source_draw`-style sprite drawing at source size.

**Blend states (premultiplied):**

| Mode | Colour blending |
|---|---|
| Normal | `ONE, INVSRCALPHA` |
| Additive | `ONE, ONE` |
| Multiply | `DSTCOLOR, INVSRCALPHA` |
| Screen | `ONE, INVSRCCOLOR` |

Alpha always uses `ONE, INVSRCALPHA` (`gs_blend_function_separate`).

**Shader** (`spine.effect`). This is the spine-ts two-colour formula with premultiplied texels:

```
tex = sample(page, uv)
rgb = ((tex.a - 1) * dark.a + 1 - tex.rgb) * dark.rgb + tex.rgb * light.rgb
out = float4(rgb, tex.a * light.a)
```

Light colour is premultiplied in the backend, as spine-ts does when `premultipliedAlpha` is true.

**Sampler:** `GS_FILTER_ANISOTROPIC`, `max_anisotropy = 16`, clamp addressing. The D3D11 and OpenGL backends treat anisotropic as including linear mip filtering.

### Framing

`animation_bounds` mirrors `calculateAnimationViewport`:
1. Set the setup pose.
2. Apply the animation at `i * duration / 100` for i from 0 to 99, updating world transforms each time.
3. Union the bounds.
4. Restore the instance's state.

The renderer adds 10% padding on each side, then fits the result to the source with the aspect ratio kept and the character centred. Framing is recomputed when the asset, default animation or source size changes. A zero-duration animation uses one sample. If the bounds are empty or not a number, the plugin logs a warning and uses the setup-pose bounds.

### Textures and anti-aliasing

The loader thread processes each atlas page like this:
1. Decode the image through the renderer's decoder, which uses `gs_image_file_init` and needs no graphics context. Use its `texture_data` directly and never call `gs_image_file_init_texture`. Keep the decoder's reported colour format (`GS_RGBA` or `GS_BGRA`) and create the texture with that same format.
2. Leave the pixels **as stored**, matching the browser renderer, which treats atlas pixels as premultiplied.
3. If either dimension isn't a power of two, resize up to the next power of two with bilinear filtering. UVs are normalised, so they stay valid.
4. Build the full mip chain down to 1×1 with a 2×2 box filter. On premultiplied data this is the correct average, and it avoids dark or bright edge halos.

The render thread creates each page with `gs_texture_create(w, h, format, levels, data, 0)`, passing the decoded format and an explicit level count.

Pages larger than `gs_get_texture_max_size()` (or 16384 if unavailable) fail the load with a clear error.

### Loading, threads and lifetime

- **`update`** (UI thread) reads the settings. If the skeleton path, atlas path or renderer changed, it increments a generation counter, stores a copy of the paths as the latest request, and signals the source's single loader thread. The thread is started on first use and joined in `destroy`. It always loads the latest request, so a burst of path changes never queues up stale loads and `update` never blocks. If only the default animation or size changed, it stores them in a pending-config block under a mutex.
- **Loader thread:**
  - detect the version;
  - if it isn't 4.1, post a "fall back to browser" result;
  - otherwise `backend->load`: parse the atlas and skeleton, and decode and mip every page.

  It posts `{generation, asset or error}` under a mutex. Results whose generation is stale are freed immediately.
- **`video_render`** (graphics thread) takes any posted result:
  - **asset:** upload the textures, create the instance, set the default animation, compute the framing, then free the previous asset and its textures (they are owned by the graphics thread);
  - **error:** log it, free the previous asset, and draw nothing;
  - **fallback:** set a flag that `update` and `tick` act on to create the browser source.
- **Browser-source creation and release** happen under the source mutex. `video_render` takes a strong reference (`obs_source_get_ref`) under that mutex before drawing it, so a renderer switch can never free a source mid-draw.
- **`destroy`:**
  1. join the loader thread;
  2. inside `obs_enter_graphics`, free the textures, buffers, texrender and effect;
  3. free the assets.
- **spine-c allocation** goes through `_spSetMalloc`, `_spSetDebugMalloc`, `_spSetRealloc` and `_spSetFree`, pointing at OBS's `bmalloc`, `brealloc` and `bfree`. OBS's shutdown leak counter then covers the runtime.

### Errors

Each failure is one `spine_log` line and a transparent source, never a crash or a retry loop:
- missing or unreadable skeleton or atlas;
- a spine-c atlas or skeleton parse error (spine-c's error text is included);
- a missing page image;
- a page too large;
- an unsupported version (falls back to browser, with the reason logged);
- texture or buffer creation failure.

The default animation resolves in this order: exact name, then case-insensitive match, then the first animation (as `SpineStateController.resolve` does). An empty skeleton with no animations shows the setup pose.

## Testing and measurement

**C unit tests,** no OBS needed. They are added to CTest next to the existing C tests:
- **`atlas-image`:** power-of-two resize of odd sizes; mip level count and sizes; the box filter on known premultiplied inputs (an opaque/transparent checker averages to 50% premultiplied grey, not a dark fringe); 1×N and N×1 pages.
- **`skeleton-version`:** `.json` and `.skel` detection for 3.7, 4.0, 4.1 and garbage input.
- **`backend-41`** (against the Spineboy 4.1 example, vendored from upstream `examples/spineboy/export` at the same pinned commit as `tests/fixtures/spine41/`):
  - loading succeeds for both `.skel` and `.json`;
  - animation names match the runtime's list;
  - a missing atlas or a corrupt skeleton returns an error without crashing;
  - `build_draw_list` returns non-empty, well-formed batches, with every index in range.
- **Parity with the browser runtime:**
  - `tools/generate-native-reference.js` uses the bundled 4.1 web runtime under Node to write reference numbers for Spineboy: the framing bounds of `idle`/`walk`, and world vertices for chosen slots at fixed times;
  - the C test compares the native backend against them within 1e-3.
- **Symbol-prefix check:** as described under Symbol isolation.
- **Unchanged tests:** all existing tests (`node --test tests/*.test.js`, `skeleton-binary-tests`, `animation-catalog-tests`, `level-gate-tests`) still pass.

**In real OBS.** The portable OBS 31.1.1 install in `.cache/obs-test` is used for these checks:
1. Install the build there and load a scene with two sources showing the same 4.1 character, one Browser and one Native.
2. Visual check by screenshot: framing, colours, additive and multiply slots, tint and clipping should match. Some small edge differences are expected and wanted from mipmaps.
3. Switch the Renderer setting back and forth while OBS runs; change paths quickly; remove and re-add the source; close OBS. The OBS log should show no errors or crashes, and the shutdown leak count should be 0.
4. Point a Native source at a 3.7 or 4.0 asset and confirm the logged fallback to the browser renderer.

**Measurement** (`docs/performance.md`). Setup: one source at 1080p, Spineboy or a sample character, 60 fps canvas, 60 seconds, measured once with Browser and once with Native. Record:
- total CPU of `obs64` plus the `obs-browser-page` processes (Windows performance counters);
- GPU engine utilisation (`\GPU Engine(*)\Utilization Percentage`, summed per process);
- OBS's own "average time to render frame" from the log or stats.

Also record the hardware used. Results are reported as measured, with no claims beyond them.

## Licensing

spine-c and the Spineboy fixture come from the Spine Runtimes repository, under the Spine Runtimes License. Add `third_party/spine-c-4.1/LICENSE` and update `THIRD_PARTY_NOTICES.md` to list the vendored runtime, its pinned commit and the test fixture. The existing note that using the Spine Runtimes may require a Spine Editor license is kept.

## Risks

- **Matching the browser exactly.** Blending on gamma-encoded values plus premultiplied texels should match WebGL. The screenshot comparison is the check. Mipmapped edges are expected to look smoother than the browser's, which is the intended improvement.
- **Missed renamed symbols** only cause harm once a second runtime is linked. The prefix check catches them in stage 1 already.
- **Atlases straight from older exporters** are not truly premultiplied. They look the same as in the browser (same assumption), so parity holds. Fixing that belongs to a later stage.
- **The mouth overlay's shared timelines** (stage 2) must not be freed twice by `spAnimation_dispose`. This is recorded here so the stage 2 spec handles it.

## Erratum (implementation, 2026-10-27)

Where the code differs from this spec, the code is right. Later stages should follow these points:

- **Blend factors.** They are copied from the bundled spine-ts 4.1 runtime, not from the table above:

  | Mode | Colour (src, dst) | Alpha (src, dst) |
  |---|---|---|
  | Normal | `ONE, INVSRCALPHA` | `ONE, INVSRCALPHA` |
  | Additive | `ONE, ONE` | `ONE, ONE` |
  | Multiply | `DSTCOLOR, INVSRCALPHA` | `INVSRCALPHA, INVSRCALPHA` |
  | Screen | `ONE, INVSRCALPHA` | `INVSRCCOLOR, INVSRCALPHA` |

  Screen is not visually verified: no available asset uses it.
- **Dark colour.** It is `(0, 0, 0, 1)` when the slot has no dark colour, and `(dark × alpha, 1)` otherwise, as in spine-ts.
- **Backend interface additions.**
  - `release_page_images()` frees the CPU pixels after GPU upload.
  - `shutdown()` frees spine-c's static empty animation; it is called from `obs_module_unload`.
  - `spine_draw_vertex.dark` is `float[4]`.
  - The image decoder also has `read_file`, so all file access is UTF-8 safe.
- **Allocation.**
  - The backend rounds zero-byte requests up to one byte, because OBS's `bmalloc` treats a zero-byte allocation as fatal.
  - `.skel` files are validated by `src/skeleton-binary.c` before spine-c reads them.
- **Skeleton format.** A skeleton is treated as JSON only for a `.json` extension; anything else is binary, as in the browser player.
- **Drawing.** The skeleton is drawn once per OBS frame. Further views (Studio Mode, projectors) reuse the offscreen texture.
- **Fallback.** The switch to the browser renderer runs on the UI thread (`obs_queue_task`), not in `video_tick`.
