# QMeshLab

Minimal Qt 6 SDI mesh viewer using QRhi + vcglib.

## Current Features
- Single `Document` with multiple views (3D viewport, layer tree, log pane)
- Plugin-based mesh loading (`MeshIOPlugin`), with built-in support for `ply`, `obj`, `stl`, `off`
- Structured logging (app vs vcglib), including load/buffer timing

## Build

```bash
git submodule update --init --recursive
mkdir -p build
cd build
cmake ..
cmake --build .
./QMeshLab
```