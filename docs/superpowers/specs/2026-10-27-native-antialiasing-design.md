# Native renderer, stage 3: anti-aliasing quality

Branch: `feature/native-renderer`. Builds on stages 1 and 2. The user asked to continue through all stages without per-stage approval.

## Goal

Add a setting, **Anti-aliasing (Native)**, so users choose how smooth the character's edges are against how much GPU it costs:

| Value | Label | What it does | Cost |
|---|---|---|---|
| `off` (default) | Off (mipmaps only) | Stage 1 behaviour: mipmapped, anisotropic textures | none extra |
| `fxaa` | FXAA (light) | A one-pass edge filter applied while compositing | one extra texture read pattern per output pixel |
| `ss15` | Supersample 1.5× | The skeleton is drawn at 1.5× size, then area-downscaled | 2.25× the pixels |
| `ss2` | Supersample 2× | The skeleton is drawn at 2× size, then area-downscaled | 4× the pixels |

The default is `off`, because weak PCs are the reason for the native renderer. Mipmaps stay on in every mode. Browser mode ignores the setting, and the property is hidden unless **Renderer** is Native.

## Verified facts

- libobs has no MSAA: render targets are single-sample. This was checked in stage 1.
- `OBS_EFFECT_AREA` (`libobs/data/area.effect`) is OBS's area downscaler. Its `Draw` technique needs `base_dimension` and `base_dimension_i` (the source texture size and its reciprocal) and does not use `multiplier`. OBS sets these the same way in `obs-scene.c` for "Area" scale filtering.
- The offscreen texture is sampled with `gs_effect_set_texture_srgb`, so both the downscale and FXAA run on linear-light values. That is the right space for averaging.

## Design

### Pure module: `src/native/antialiasing.{c,h}`

```c
enum native_aa { NATIVE_AA_OFF, NATIVE_AA_FXAA, NATIVE_AA_SS15, NATIVE_AA_SS2 };
enum native_aa native_aa_from_setting(const char *value);   /* unknown/NULL -> OFF */
/* Size of the offscreen texture for a source of width x height. Supersampling multiplies by 1.5 or 2,
 * rounded to the nearest pixel, then scales down uniformly so neither side exceeds max_size. */
void native_aa_render_size(enum native_aa mode, uint32_t width, uint32_t height, uint32_t max_size,
			   uint32_t *out_width, uint32_t *out_height);
```

### Renderer

- **New call:** `native_renderer_set_antialiasing(r, enum native_aa)`, thread-safe; it is applied on the next render.
- **`draw_skeleton`** draws into a texrender of `native_aa_render_size(...)`, using the same ortho rectangle. The framing doesn't change, only the pixel density.
- **Compositing:**
  - **off:** unchanged.
  - **FXAA:** the new `data/effects/spine-fxaa.effect` replaces the default effect. Its `texel` uniform is the reciprocal of the texture size.
  - **ss15 and ss2:** `OBS_EFFECT_AREA` with technique `Draw`. `base_dimension` is the texture size and `base_dimension_i` its reciprocal. The sprite is drawn at the source size.
- **Blending is the same in every mode:** sRGB framebuffer, `ONE, INVSRCALPHA`.
- **Redraws:** changing the mode marks the frame for redraw.

### FXAA

FXAA here is Lottes' classic single-pass version:
- **Luma:** the four diagonal neighbours and the centre.
- **Edge direction:** worked out from those lumas.
- **Blur:** two samples along the edge (`rgbA`) and four samples (`rgbB`). `rgbA` is chosen when `rgbB`'s luma leaves the local range.
- **Constants:** `REDUCE_MIN = 1/128`, `REDUCE_MUL = 1/8`, `SPAN_MAX = 8`.

The image is premultiplied RGBA with a transparent background, so the luma includes coverage: `luma = sqrt(dot(rgb, (0.299, 0.587, 0.114)) + 0.5 × (1 − a))`. That is the colour as seen over 50% grey, which lets the silhouette edge against transparency count as an edge. All four channels are filtered together, so premultiplication stays consistent and there's no halo.

### Source

- **New setting** `native_aa`, default `off`.
- **List property** "Anti-aliasing (Native)", placed after Renderer, with a long description of the costs. A modified-callback on Renderer shows it only when Native is selected.
- **`update`** passes the value to the native renderer whenever native is active, and once more after the renderer is created.
- **Locale strings:** `NativeAntialiasing`, `NativeAntialiasing.Off`, `.Fxaa`, `.Ss15`, `.Ss2`, `.Help`.

## Testing

- **C unit tests** (`tests/antialiasing-test.c`):
  - parsing every value, including unknown and NULL;
  - render sizes: 540×960 at `ss15` → 810×1440; `ss2` → 1080×1920; `off` and `fxaa` → unchanged;
  - clamping: 10000×5000 at `ss2` with max 16384 → 16384×8192; odd sizes round to the nearest pixel.
- **In OBS:**
  - every mode loads with no effect errors in the log;
  - zoomed edge crops for off / FXAA / ss2 on MekamiShifty show smoother edges with FXAA and ss2;
  - `averageFrameRenderTime` and GPU utilisation per mode go into `docs/performance.md`;
  - 0 leaks at shutdown.
- **GCC check** stays green.
