# QMeshLab

Minimal Qt 6 SDI mesh viewer using QRhi + vcglib.

## Documentation Index
- [Architecture](docs/architecture.md)
- [Data Model](docs/data_model.md)
- [Rendering](docs/rendering.md)

## Current Features
- Single `Document` shared by one or more `RenderWidget` views
- Split-view UI (horizontal/vertical), plus layer and log docks
- Plugin-based mesh loading with per-extension plugin preference
- Structured logging for app/VCG import progress and GPU buffer rebuild timing
- PNG snapshot export from the active view (with embedded camera/trackball JSON metadata)

Built-in import plugin families (dependency-gated at build time):
- `io_vcg` (`ply`, `obj`, `stl`, `off`, `vmi`)
- `io_obj_rapidobj` (`obj`)
- `io_gltf` (`gltf`, `glb`)
- `io_e57` (`e57`, optional)

## Build

```bash
git submodule update --init --recursive
mkdir -p build
cd build
cmake ..
cmake --build .
./QMeshLab
```

`io_e57` can fetch `libE57Format` from GitHub during CMake configuration (requires XercesC).
`io_gltf` and `io_obj_rapidobj` can also be enabled/disabled or configured through their CMake options under `plugins/`.
