# Third-party notices

The bundled files under `data/player/runtime/` are copied from the supplied `pub_web_spine-web-player-custom` reference and contain the official Esoteric Software Spine Web Player 4.0.28 and 4.1.20 runtimes.

`data/player/runtime/3.7.94/spine-webgl.js` is the unmodified `spine-ts/build/spine-webgl.js` from the `3.7.94` tag of https://github.com/EsotericSoftware/spine-runtimes. `data/player/spine37-binary.js` is a port of that tag's spine-libgdx `SkeletonBinary` reader, and `src/skeleton-binary.c` follows the binary layouts of the 3.7.94 and 4.0/4.1 Spine Runtimes; both are derivative works of the Spine Runtimes under the same license.

Their license is included at `data/player/runtime/SPINE-RUNTIMES-LICENSE.txt`. Use and redistribution of the runtimes and Spine assets are subject to that license and may require a Spine Editor license.


`third_party/spine-c-4.1/` is the unmodified spine-c runtime (`spine-c/spine-c/include` and `src`) from the `4.1` branch of https://github.com/EsotericSoftware/spine-runtimes at commit `77a5db0ec6d16331f5efbaa7662bba9355bd3424` (recorded in `third_party/spine-c-4.1/VERSION`). The native renderer compiles it with a generated header that renames its external symbols. `tests/fixtures/spine41/` holds the Spineboy example export (`spineboy-pro.skel`, `spineboy-pro.json`, `spineboy-pma.atlas`, `spineboy-pma.png`) from `examples/spineboy/export` at the same commit, used only by tests. Both are covered by the Spine Runtimes License included at `third_party/spine-c-4.1/LICENSE`.
