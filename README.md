# OBS Spine Player

OBS Spine Player is a native OBS source plugin backed by the official Spine Web Player runtimes. It targets Spine 4.0 and 4.1 character assets and is designed for transparent-background streaming overlays.

Development is in progress. See `docs/architecture.md` for the implementation boundaries once the source, input adapters, and player state controller are in place.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
cmake --install build --prefix /usr/local
```

OBS and the OBS Browser plugin must be installed. The build requires the `libobs` CMake development package.

