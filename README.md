# OBS Spine Player

OBS Spine Player adds a transparent **Spine Character** source to OBS. It supports Spine 4.0 and 4.1 binary or JSON exports, starts in `idle`, moves a compatible mouth animation from microphone volume, and exposes optional emotion/action hotkeys.

No speech recognition, network service, or external audio process is used.

## Features

- Bundled Spine Web Player 4.0.28 and 4.1.20 runtimes with automatic asset-version detection.
- Native OBS source properties and transparent rendering through the installed OBS Browser plugin.
- Lightweight microphone gate with configurable threshold, attack, and release hold.
- Independent mouth overlay on Spine track 1, leaving `idle` or the selected emotion on track 0.
- Optional desktop-cursor eye tracking with configurable Spine slot groups, travel, and smoothing.
- Optional emotion state machine with eight configurable looping or one-shot slots.
- Optional OBS hotkeys for all eight slots and returning to the default animation.

## Requirements

- OBS Studio with the Browser Source plugin enabled.
- CMake 3.22 or newer and a C11 compiler.
- The `libobs` development package discoverable by CMake.
- A valid Spine license for use of the Spine runtimes and assets, as required by Esoteric Software's runtime license.

## Build and install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr
```

Restart OBS after installation. Linux is the locally verified build target. The CMake install destinations also cover conventional Windows and macOS OBS plugin layouts when built inside an OBS plugin development environment.

GitHub Actions builds Linux x86_64 and Windows x64 packages on pushes to `main`, pull requests, and manual workflow dispatches. Download them from the `Build Plugin Linux` or `Build Plugin Windows` workflow run's artifacts. The Windows artifact contains both a portable ZIP and an unsigned Inno Setup installer; select the OBS Studio installation folder when installing to a non-default location.

## Use

1. Add a **Spine Character** source and choose its `.skel` or `.json` file and `.atlas` file. Atlas image pages must remain at paths referenced by the atlas.
2. The character starts in `idle` by default; choose another detected animation when needed.
3. Leave **Spine runtime** on auto-detect, or force 4.0/4.1 if a local-file policy prevents version probing.
4. Enable yap mode, choose an existing OBS microphone/audio input, and tune the threshold. The selected source must be active in OBS to produce audio callbacks.
5. Choose default, mouth, and emotion/action animations from the detected dropdowns. Disable **Loop** for actions that should return to `idle` after one play.
6. Open **Settings → Hotkeys**, search for “Spine Player,” and bind the desired source hotkeys.

## Cursor eye tracking

Enable **Cursor eye tracking** in Source Properties to move eye attachments toward the desktop cursor without replacing `idle` or an emotion animation. The defaults match Mekami_Shifty:

- Left eye: `f_eye_id_l,f_eye_hi_l3,f_eye_hi_l2`
- Right eye: `f_eye_id_r,f_eye_hi_r3,f_eye_hi_r2`

Those slots resolve to two controlling bones in the supplied rig, so the pupil, iris, and highlight layers stay together. For another character, enter comma-separated slot names for each eye. **Horizontal eye travel**, **Vertical eye travel**, and **Eye smoothing** control the range and response; the default `6`, `4`, and `90 ms` values are a conservative starting point.

Windows uses the virtual desktop cursor. Linux uses X11; a native Wayland session may deny global cursor coordinates by design, in which case OBS logs a warning and the eyes remain centered. No camera, face tracking, or speech recognition is involved.

Animation names are read directly from Spine JSON exports. Binary `.skel` exports use an adjacent catalog with the same base name and an `.animations.txt` suffix, one animation name per line. Dropdowns remain editable so an existing scene or an uncatalogued binary export is never blocked.

Generate or refresh a binary catalog from the asset itself with `node tools/generate-animation-catalog.js /path/to/model.skel`. The tool detects Spine 4.0/4.1 and uses the matching bundled runtime; it does not guess names from binary strings.

Character assets are intentionally excluded from version control. A local `characters/` directory is ignored by Git and installed when present, but published source packages contain no character skeletons, atlases, textures, or animation catalogs.

## Troubleshooting

Open **Help → Log Files → View Current Log** in OBS and search for `[OBS Spine Player` and `[obs-browser: 'OBS Spine Player Renderer']`. The plugin logs its installed binary/data locations, source dimensions, selected runtime and animation, asset paths and readability, Browser Source creation, first render, runtime detection, asset fetch results, and the final animation catalog. Browser exceptions and Spine loading failures are emitted as errors instead of only appearing inside the source canvas.

Eye tracking additionally logs the desktop cursor backend and resolved Spine bones. Missing slot names are reported by the renderer without preventing the character from loading.

## Maintenance

See [Architecture](docs/architecture.md) for component boundaries, the supplied-reference findings, and the extension path for future ASR/STT or speech-based emotion inputs. Third-party runtime licensing is recorded in [Third-party notices](THIRD_PARTY_NOTICES.md).
