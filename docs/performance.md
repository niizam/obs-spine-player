# Renderer performance

The browser renderer measured below was removed in stage 5 of the native rewrite. The plugin now always draws natively. The Browser rows are kept as history: they show what an upgrade saves.

## 2026-10-27: Browser vs Native (stage 1)

**Machine:** AMD Ryzen 7 5700X (16 logical cores), NVIDIA GPU, Windows 11 24H2.
**OBS:** portable OBS 32.2.2 (`.cache/obs-test`), base canvas 1920×1080 at 30 fps, no output running.
**Character:** MekamiShifty (`c610_00.skel`, Spine 4.1.20, one 2048×2048 atlas page), `idle`, source size 540×960.

**Method.**
- Each run showed exactly one Spine source in the program scene.
- In the native run, the hidden test source was also set to Native, so no browser process served it.
- Another scene in the collection holds one more Spine source in Browser mode; its browser processes ran in both runs and are included in both totals.
- Each run lasted 60 s, with a sample every second (PowerShell `Get-Counter`).

**Metrics.**
- **CPU:** `\Process(obs64*)` plus `\Process(obs-browser-page*)` `% Processor Time`, divided by the 16 logical cores.
- **GPU:** `\GPU Engine(*engtype_3D)\Utilization Percentage` for the OBS and browser-page process IDs.
- **Render time:** OBS's average frame render time, from obs-websocket `GetStats`.

| Renderer | CPU (whole machine) | GPU 3D | OBS avg. frame render time | Browser processes |
|---|---|---|---|---|
| Browser | 0.99 % | 2.05 % | 0.29–0.31 ms | 7 |
| Native | 0.36 % | 0.79 % | 0.39 ms | 6 |

**Reading.**
- In this run, Native used about 64 % less CPU and 61 % less GPU than Browser for the same character.
- OBS's own frame render time went up by about 0.1 ms. That is expected, because the character is now drawn on OBS's graphics thread instead of in a separate browser process.
- Both totals include the unrelated browser source mentioned above, so the true difference attributable to the character is larger than these percentages.
- This machine is fast, so absolute numbers are small. The measurement has not been repeated on a weak PC, which is the case the native renderer is for.

**Other observations from the same session.**
- Loading MekamiShifty natively took 135–182 ms on a background thread (Spineboy: 8–10 ms).
- OBS reported `Number of memory leaks: 0` at shutdown.

## 2026-10-27: Native anti-aliasing modes (stage 3)

Same machine and portable OBS (30 fps). MekamiShifty is in `idle` at 540x960, with yap and eye tracking off, and one Native source visible.

- OBS average frame render time was read after 10 s in each mode.
- GPU 3D utilisation (OBS and browser processes) was averaged over 30 s.

| Mode | Offscreen texture | OBS avg. frame render time | GPU 3D |
|---|---|---|---|
| Off | 540x960 | 0.50 ms | 0.45 % |
| FXAA | 540x960 | 0.48 ms | 0.52 % |
| Supersample 1.5x | 810x1440 | 0.48 ms | 0.49 % |
| Supersample 2x | 1080x1920 | 0.46 ms | 0.90 % |

**Reading:**
- Frame render time is CPU-side and barely changes between modes.
- GPU cost is noise-level for Off, FXAA and 1.5x on this GPU. 2x roughly doubles it.
- On a weak GPU the 2.25x and 4x pixel counts will matter more. This was not measured on such hardware.
- The cost scales with canvas size. These numbers are for 540x960. At the plugin's default 1080x1920 canvas, 2x renders 2160x3840: 8.3 million pixels per redraw, four times this test.
- FXAA and 2x were also checked under OBS's OpenGL renderer, which Linux and macOS use. Both render correctly with no shader errors.

**Visual comparison,** using zoomed crops of a still region:
- FXAA softens edges slightly.
- 1.5x and 2x give visibly sharper detail with smooth outlines, because the atlas is sampled at a higher mip level before the area downscale.
- No halos in any mode.
