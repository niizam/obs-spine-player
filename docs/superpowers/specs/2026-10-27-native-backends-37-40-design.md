# Native renderer, stage 4: Spine 4.0 and 3.7 backends

Branch: `feature/native-renderer`. Builds on stages 1–3. The user asked for all stages without per-stage approval.

## Goal

Renderer: Native draws Spine 3.7, 4.0 and 4.1 skeletons, so the browser fallback is only needed for unknown versions. All three runtimes are linked into the one plugin module, using the per-version symbol prefixes from stage 1 (approach A).

## Verified differences (upstream spine-c branch heads: 3.7 `9639bcc`, 4.0 `425ce41`, 4.1 `77a5db0`)

| Topic | 3.7 | 4.0 | 4.1 |
|---|---|---|---|
| Animation | `timelinesCount` + `spTimeline **timelines`; `spAnimation_create(name, count)`; dispose frees the timelines | `spTimelineArray` + `timelineIds`; `create(name, array, duration)` | same as 4.0 |
| Timeline types | ROTATE/TRANSLATE/SCALE/SHEAR (`spBaseTimeline.boneIndex`), ATTACHMENT, COLOR, TWOCOLOR, DEFORM (`slotIndex`), EVENT, … | 4.0 set (separate X/Y, RGBA/RGB/ALPHA, RGBA2/RGB2, …) | 4.0 set + SEQUENCE |
| Attachment timeline keys | `framesCount`, `frames[]` (times) | `super.frameCount`, `super.frames` | same as 4.0 |
| Region world vertices | `spRegionAttachment_computeWorldVertices(region, bone, …)` | by bone | by slot |
| Bone `active` | absent (all active) | present | present |
| Skin entries | `_spSkin.entries` list (no getter) | `spSkin_getAttachments` | same |
| Binary version check | none | none | "4.1" prefix |
| `disposeStatics` | yes | yes | yes |
| Track window, `darkColor`, clipping API | same in all three | | |

**Web runtimes, which define parity:**
- **Blend factors:** 4.0.28 matches 4.1.20: separate colour and alpha factors, as copied in stage 1. spine-ts 3.7 calls `gl.blendFunc(src, dst)`, so alpha uses the same factors:

  | Mode | src | dst |
  |---|---|---|
  | Normal | ONE | INV_SRC_ALPHA |
  | Additive | ONE | ONE |
  | Multiply | DST_COLOR | INV_SRC_ALPHA |
  | Screen | ONE | INV_SRC_ALPHA |

  For alpha, GL's `DST_COLOR` means destination alpha, so the 3.7 multiply alpha factors are `DST_ALPHA, INV_SRC_ALPHA`.
- **Framing for 3.7** (`spine37-player.js`): 100 samples, but the setup pose is reset **before every sample**, and samples with non-finite or non-positive size are skipped. Padding (10%) and contain-fit are the same as 4.x.

## Design

1. **Vendoring:** `third_party/spine-c-3.7` and `third_party/spine-c-4.0`, unmodified, at the commits above. Each gets a raw library, a generated prefix header (`sp37_`, `sp40_`), a backend library, and a prefix check (MSVC `dumpbin`, GCC `nm`).
2. **Shared 4.x adapter:**
   - `src/native/backend-41.c` becomes `src/native/backend-4x.c`, compiled twice with `SPINE_BACKEND_VERSION=40` or `41`.
   - Version `#if`s cover: the region-vertices argument, SEQUENCE timelines, the binary-format check, and the exported entry name (`spine_backend_40`, `spine_backend_41`).
3. **3.7 adapter:** `src/native/backend-37.c` implements the same interface for the 3.7 API:
   - timeline-array overlays, freed with `FREE` on the array, name and struct, never on the timelines;
   - descriptions built from `_spSkin.entries` and the 3.7 timeline structs;
   - region vertices from the bone, every bone active;
   - framing as in `spine37-player.js`;
   - binary validation against `SKELETON_BINARY_37`.
4. **Blend factors come from the backend:**
   - `struct spine_blend_factors { enum spine_blend_factor src_color, dst_color, src_alpha, dst_alpha; }`;
   - the backend table gains `const struct spine_blend_factors *blend_factors; /* [4], indexed by enum spine_blend */`;
   - the renderer maps these to `gs_blend_function_separate`.
5. **The backend travels with the asset:**
   - the loader picks the backend for the detected family (or the runtime override) and stores it in `load_result`;
   - stale or replaced results are freed with their own backend;
   - adopting a result releases the old skeleton with the old backend before switching `renderer->backend`;
   - a static `spine_backend_for_family(enum spine_family)` returns NULL for unknown families, which means falling back to Browser.
6. **Process-wide state:**
   - the allocator is set on all three backends once;
   - `obs_module_unload` calls `shutdown` on all three.
7. **Wording:**
   - the fallback log becomes `native renderer cannot draw Spine <version>; using the browser renderer`;
   - `Renderer.Native` becomes "Native (faster)";
   - `Renderer.Help` and the README list 3.7, 4.0 and 4.1.

## Testing

- **Fixtures:** the upstream Spineboy exports for 3.7 and 4.0 (`.skel`, `.json`, PMA atlas, PNG) go in `tests/fixtures/spine37`, `spine40`.
- **References:** `tools/generate-native-reference.js` gains a version argument. It writes `tests/fixtures/spine<ver>/reference.h` with:
  - the animation names;
  - the slot, bone and animation counts;
  - idle and walk bounds;
  - walk vertices and their blocks.

  The sources are the bundled 4.1.20 player for 4.1, the 4.0.28 player for 4.0, and spine-ts 3.7.94 `SkeletonJson` with the 3.7 framing rule for 3.7.
- **Shared backend test:** `tests/backend-41-test.c` becomes `tests/backend-test.c`, compiled once per version against its reference and fixture directory. Version-specific numbers come from `reference.h` (for example the slot count, the mouth slot index and the name of its bone).
- **Linking all three:** the plugin itself links all three backend libraries, which is the duplicate-symbol check, and every prefix check stays green. The GCC check builds all three.
- **In OBS:**
  - Spineboy 4.0 natively;
  - Curian (3.7.93) natively, with yap `auto`, expecting the derived mouth-overlay log, and yap toggled with the tone;
  - MekamiShifty (4.1) as a regression check;
  - 0 leaks at shutdown.

## Erratum (implementation, 2026-10-27)

- **One adapter source.** `src/native/backend.c` is compiled for 3.7, 4.0 and 4.1 with compatibility macros, instead of a separate `backend-37.c`. The differences turned out to be local.
- **The 3.7 parity reference** reads the `.skel` through `data/player/spine37-binary.js`, which is the browser's own 3.7 path. JSON exports round numbers.
- **spine-c 3.7 keeps the C runtime allocator.** Its `Skeleton.c` grows the update cache with the C library's `realloc`. With OBS's `bmalloc` (`_aligned_malloc` on Windows), that corrupted the heap and crashed OBS on Curian. The cost: OBS's leak counter does not cover 3.7 runtime allocations.
- **JSON exports missing `skeleton.hash` or `skeleton.spine`** are rejected before spine-c 3.7 or 4.0 reads them, because both copy those fields without a NULL check. 4.1 rejects them through its own version check.
- **Alpha blend factors are translated for Direct3D 11.** The spine-ts 4.x screen mode uses `ONE_MINUS_SRC_COLOR` as its alpha factor. WebGL reads that as `1 - alpha`, but Direct3D 11 rejects it: OBS logged `Failed to create blend state` every frame and skipped the slot. `spine_alpha_factor` maps `*_COLOR` alpha factors to their alpha equivalents. The tables stay as copied from spine-ts.
- **A detected version beats a mismatched runtime override.** The override exists because the browser cannot always probe local files. The native loader reads the file itself, so it logs a warning and uses the detected version.
- **The allocator policy lives in `spine_backends_set_host_allocator`** (in `backends.c`), not in the backend. As a result, the 3.7 leak and growth tests still count real allocations.
