# QMeshLab

Minimal Qt 6 SDI mesh viewer using QRhi + vcglib.

## Documentation Index
- [Architecture](docs/architecture.md)
- [Data Model](docs/data_model.md)
- [Rendering](docs/rendering.md)

## Current Features
- Single `Document` shared by one or more `RenderWidget` views
- Split-view UI (horizontal/vertical), active-view indicator, plus layer and log docks
- Per-view mode switching between `3D Scene` and `Parametrization (UV)` (when the current mesh has UVs)
- Scene overlays for selection, normals, boundaries, texture seams, current-mesh outline, and quality histogram
- Fat-edge rendering for edge meshes and decorator boundaries/seams (configurable width)
- UV mode support for boundary-edge and texture-seam overlays on the current mesh
- Plugin-based mesh import/export with per-extension preferred import plugin
- Plugin-based filter framework with searchable filter browser and auto-generated parameter dialogs
- Undo/redo integrated with mesh operations and filter runs
- Structured logging for app/VCG import progress and GPU buffer rebuild timing
- PNG snapshot export from the active view (custom resolution + embedded camera/trackball JSON metadata)

Built-in I/O plugin families (dependency-gated at build time):
- `io_vcg` (`ply`, `obj`, `stl`, `off`, `vmi`)
- `io_obj_rapidobj` (`obj`)
- `io_gltf` (`gltf`, `glb`)
- `io_e57` (`e57`, optional)

Built-in filter plugin families (dependency-gated at build time):
- `filter_basic`
- `filter_func`
- `filter_embree`
- `filter_select`
- `filter_clean`
- `filter_meshing`

## Build

### Recommended: vcpkg manifest mode

Prerequisites:
- Qt 6.11+
- CMake 3.25+
- Build tool: `ninja` or `make`
- vcpkg clone

This repository contains `vcpkg.json`; non-Qt dependencies are installed via vcpkg manifest mode.
Qt6 is intentionally kept outside vcpkg, and `vcglib` remains a git submodule.

Managed non-Qt dependencies:
- `draco`
- `tinygltf`
- `rapidobj`
- `libe57format`
- `xerces-c`
- `muparser`
- `embree`

### Setup

```bash
git submodule update --init --recursive
git clone https://github.com/microsoft/vcpkg ./vcpkg
./vcpkg/bootstrap-vcpkg.sh
```

If your vcpkg is elsewhere, set:

```bash
export VCPKG_ROOT="/absolute/path/to/vcpkg"
```

### Build from terminal

Release:

```bash
cmake --preset vcpkg-release
cmake --build --preset vcpkg-release -j8
./build-release/QMeshLab
```

Debug:

```bash
cmake --preset vcpkg-debug
cmake --build --preset vcpkg-debug -j8
./build-debug/QMeshLab
```

Notes:
- `vcpkg-debug` and `vcpkg-release` are defined in `CMakeUserPresets.json`.
- They use `Unix Makefiles` and set `VCPKG_ROOT=${sourceDir}/vcpkg` explicitly, which helps in GUI environments (for example VS Code) where shell environment variables are not always inherited.

### Build from VS Code (CMake Tools)

1. `CMake: Select Configure Preset` -> choose `vcpkg-debug` or `vcpkg-release`.
2. `CMake: Configure`
3. `CMake: Build`
4. Launch with the CMake Tools run/debug actions.

If presets were changed and VS Code still uses stale values:
1. `CMake: Delete Cache and Reconfigure`
2. `Developer: Reload Window`

### Optional: local minimal build without vcpkg

```bash
git submodule update --init --recursive
cmake --preset local-no-vcpkg
cmake --build --preset local-no-vcpkg
./build-local/QMeshLab
```

The local minimal preset disables dependency-heavy plugins (`io_gltf`, `io_e57`, `io_obj_rapidobj`).
