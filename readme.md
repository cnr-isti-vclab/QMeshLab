# QMeshLab

Minimal Qt 6 SDI mesh viewer using QRhi + vcglib.

## Current Features
- Single `Document` with multiple views (3D viewport, layer tree, log pane)
- Plugin-based mesh loading with isolated per-plugin folders under `plugins/`
- Built-in support for `ply`, `obj`, `stl`, `off`, `vmi` and optional `e57`
- Structured logging (app vs vcglib), including load/buffer timing

`e57` support is built from `plugins/io_e57/` and can use an installed `libE57Format` or fetch it from GitHub during CMake configuration.

## Build

```bash
git submodule update --init --recursive
mkdir -p build
cd build
cmake ..
cmake --build .
./QMeshLab
```