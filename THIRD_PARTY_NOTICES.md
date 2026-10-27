# Third-party notices

## Shipped with the plugin

`third_party/spine-c-3.7/`, `third_party/spine-c-4.0/` and `third_party/spine-c-4.1/` are the unmodified spine-c runtimes (`spine-c/spine-c/include` and `src`) from https://github.com/EsotericSoftware/spine-runtimes. Each folder has its own `LICENSE` and `VERSION`.

| Runtime | Branch | Commit |
| --- | --- | --- |
| spine-c 3.7 | `3.7` | `9639bcc81722d7178fd9d1cdc1a3d55a4c91f989` |
| spine-c 4.0 | `4.0` | `425ce416bb218b28caeec47b317aa57cd7140375` |
| spine-c 4.1 | `4.1` | `77a5db0ec6d16331f5efbaa7662bba9355bd3424` |

- The plugin compiles each runtime with a generated header that renames its external symbols (`sp37_`, `sp40_`, `sp41_`).
- The licences are installed as `licenses/spine-c-<version>.txt` in the plugin's data folder.
- `src/skeleton-binary.c` follows the binary layouts of the 3.7.94, 4.0 and 4.1 Spine Runtimes, so it is a derivative work under the same licence.

Use and redistribution of the runtimes and Spine assets are subject to the Spine Runtimes License, and may require a Spine Editor license.

## Developer tools only (not installed)

`tools/spine-ts/runtime/` holds the official spine-ts web runtimes. The test-reference and animation-catalog tools use them as the reference implementation:

- the Spine Web Player 4.0.28 and 4.1.20 builds, copied from the supplied `pub_web_spine-web-player-custom` reference;
- `3.7.94/spine-webgl.js`, the unmodified `spine-ts/build/spine-webgl.js` from the `3.7.94` tag.

`tools/spine-ts/spine37-binary.js` is a port of that tag's spine-libgdx `SkeletonBinary` reader. The licence for these files is at `tools/spine-ts/runtime/SPINE-RUNTIMES-LICENSE.txt`.

## Test fixtures

`tests/fixtures/spine37/`, `spine40/` and `spine41/` hold the Spineboy example exports (`spineboy-pro.skel`, `spineboy-pro.json`, `spineboy-pma.atlas`, `spineboy-pma.png`). They come from `examples/spineboy/export` at the commits listed above, are used only by tests, and are covered by the Spine Runtimes License.
