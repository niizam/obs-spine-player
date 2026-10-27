# Renderer performance

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
