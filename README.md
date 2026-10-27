# OBS Spine Player

OBS Spine Player adds a transparent **Spine Character** source to OBS. It supports Spine 3.7, 4.0, and 4.1 binary or JSON exports, starts in `idle`, moves a compatible mouth animation from microphone volume, and exposes optional emotion/action hotkeys.

No speech recognition, network service, or external audio process is used.

## Features

- Draws the character directly with OBS graphics, using the official spine-c runtimes for Spine 3.7, 4.0 and 4.1. The version is detected from the file, and no browser process is involved.
- Mipmapped atlas textures and optional anti-aliasing (FXAA or 1.5×/2× supersampling).
- Automatic animation catalogs: animation names are read directly from JSON and from Spine 3.7/4.0/4.1 binary skeletons.
- Lightweight microphone gate with configurable threshold, attack, and release hold.
- Independent mouth overlay on Spine track 1, leaving `idle` or the selected emotion on track 0. Rigs without a dedicated `talk_start` animation, such as CounterSide illustrations, get a mouth-only loop derived from their own talking clip.
- Optional desktop-cursor eye tracking with configurable Spine slot groups, travel, and smoothing.
- Optional emotion state machine with eight configurable looping or one-shot slots.
- Optional OBS hotkeys for all eight slots and returning to the default animation.

## Requirements

- OBS Studio. The Browser Source plugin is not needed.
- CMake 3.22 or newer and a C11 compiler.
- The `libobs` development package discoverable by CMake.
- Node.js, only to run the JavaScript tool tests and the developer tools in `tools/`.
- A valid Spine license for use of the Spine runtimes and assets, as required by Esoteric Software's runtime license.

## Build and install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr
```

Restart OBS after installation.

- Linux is the locally verified build target. The CMake install destinations also cover conventional Windows and macOS OBS plugin layouts, when built inside an OBS plugin development environment.
- On Arch Linux, `makepkg -Csi` builds the checked-out Git branch.
  - The `PKGBUILD` redirects makepkg's internal `src/` and `pkg/` work directories, so `--cleanbuild` cannot collide with this repository's tracked `src/` directory.
  - Set `OBS_SPINE_PLAYER_BRANCH=main` to package `main` explicitly while another branch is checked out.
- GitHub Actions builds Linux x86_64 and Windows x64 packages on pushes to `main`, on pull requests, and on manual workflow dispatches.
  - Download them from the artifacts of the `Build Plugin Linux` or `Build Plugin Windows` workflow run.
  - The Windows artifact contains a portable ZIP and an unsigned Inno Setup installer. When installing to a non-default location, select the OBS Studio installation folder.

## Use

1. Add a **Spine Character** source and choose its `.skel` or `.json` file and `.atlas` file. Atlas image pages must remain at the paths the atlas references.
2. The character starts in `idle` by default; choose another detected animation when needed.
3. Leave **Spine runtime** on auto-detect. A forced version is used only when the file's version cannot be detected.
4. Enable yap mode, choose an existing OBS microphone/audio input, and tune the threshold. The selected source must be active in OBS to produce audio callbacks.
5. Choose default, mouth, and emotion/action animations from the detected dropdowns. Disable **Loop** for actions that should return to `idle` after one play.
6. Open **Settings → Hotkeys**, search for "Spine Player," and bind the desired source hotkeys.

## Drawing and anti-aliasing

The character is drawn into an offscreen texture on OBS's graphics thread, then composited like any other source.

- Atlas textures are mipmapped and sampled with trilinear/anisotropic filtering, so edges stay smooth when a large atlas is drawn small.
- Compared with the earlier browser-based renderer, it used about 64 % less CPU and 61 % less GPU for the same character ([numbers](docs/performance.md)).

**Anti-aliasing** sets how smooth edges are. The default is Off:

- **Off:** mipmaps only.
- **FXAA (light):** a cheap filter that smooths the character's outline. Detail inside the character is left exactly as drawn.
- **Supersample 1.5x / 2x:** draws the character at 2.25x or 4x the pixels, then area-downscales it. This is the smoothest and sharpest option, and the most GPU work. Keep Off on weak PCs.

Skeletons exported by other Spine versions (for example 3.8 or 4.2) are not drawn. The OBS log says so: `Spine 4.2.10 is not supported; the plugin draws Spine 3.7, 4.0 and 4.1 exports`.

Scenes saved by earlier versions with **Renderer: Browser** draw natively after the upgrade. The old setting is removed from the source.

## Cursor eye tracking

Enable **Cursor eye tracking** in Source Properties to move eye attachments toward the desktop cursor without replacing `idle` or an emotion animation. The defaults match Mekami_Shifty:

- Left eye: `f_eye_id_l,f_eye_hi_l3,f_eye_hi_l2`
- Right eye: `f_eye_id_r,f_eye_hi_r3,f_eye_hi_r2`

Those slots resolve to two controlling bones in the supplied rig, so the pupil, iris, and highlight layers stay together. For another character, enter comma-separated slot names for each eye.

**Horizontal eye travel**, **Vertical eye travel**, and **Eye smoothing** control the range and response. The defaults (`6`, `4`, and `90 ms`) are a conservative starting point.

Windows uses the virtual desktop cursor. Linux uses direct Hyprland IPC when available and falls back to X11. Other native Wayland compositors may deny global cursor coordinates by design; OBS then logs a warning and the eyes stay centered. No camera, face tracking, or speech recognition is involved.

## Animation catalogs

Animation names are discovered automatically for any model:

- JSON exports list them directly.
- Binary `.skel` exports are parsed by `src/skeleton-binary.c`. It walks the Spine 3.7, 4.0, or 4.1 binary layout to the animation section, without guessing names from strings.
- An adjacent catalog takes precedence when present, so a curated list can override the detected one. It has the skeleton's base name with an `.animations.txt` suffix, and one name per line.

Dropdowns remain editable, so an existing scene or an unsupported export is never blocked. OBS logs which source the catalog came from.

To write a catalog file anyway, run `node tools/generate-animation-catalog.js /path/to/model.skel`. The tool detects Spine 3.7/4.0/4.1 and parses the skeleton with the matching spine-ts runtime in `tools/spine-ts/`. The atlas must sit next to the skeleton with the same base name.

## Yap mode and mouth detection

**Mouth animation** defaults to **Automatic**:

- If the skeleton has `talk_start`, it loops on track 1 unchanged, as before.
- Otherwise the renderer looks for a talking-mouth attachment: a name containing `talk`, for example `mouth_talk`.
  - The mouth is every slot on that attachment's bone. A bone is animated only when every slot it carries belongs to the mouth.
  - The clip with the most talking-mouth keys becomes the source. Only its mouth timelines are looped, from the first talking key to the end of the last talking stretch.
- Choosing a specific clip that moves more than the mouth (for example `TOUCH`) also reduces it to its mouth timelines.

**Mouth slots** overrides detection with a comma-separated slot list. The OBS log reports the decision, for example `Yap mode uses a mouth-only loop from 'TOUCH' (0.87s-9.13s); slots: face_13, mouth_1_hate; bones: face_13`.

### CounterSide illustrations

CounterSide unit illustrations (`ab_unit_illust_*`) are Spine 3.7 rigs. Their emotions (`IDLE`, `LAUGH`, `SERIOUS`, ...) swap mouth and eye attachments, while `TOUCH` contains the voiced line. With the defaults, yap mode talks over any emotion and returns to that emotion's mouth when you stop.

- Keep the `.atlas` and its `.png` in the same folder. Extracted bundles store them under `TextAsset/` and `Texture2D/`, but atlas pages are resolved next to the atlas.
- The `_BG` skeleton is a separate background layer. If you want it, add it as a second source behind the character.

Character assets are intentionally excluded from version control. A local `characters/` directory is ignored by Git and installed when present, but published source packages contain no character skeletons, atlases, textures, or animation catalogs.

## Troubleshooting

Open **Help → Log Files → View Current Log** in OBS and search for `[OBS Spine Player`. The plugin logs:

- source dimensions, the selected runtime and animation;
- asset paths and whether they are readable;
- the detected Spine version and load time;
- the yap-mode decision and the animation catalog source.

Load failures and unsupported Spine versions are logged as errors.

Eye tracking also logs the desktop cursor backend and the resolved Spine bones. Missing slot names are reported without preventing the character from loading.

## Maintenance

- [Architecture](docs/architecture.md) covers component boundaries, the reference findings, and the extension path for future ASR/STT or speech-based emotion inputs.
- [Third-party notices](THIRD_PARTY_NOTICES.md) records third-party runtime licensing.
