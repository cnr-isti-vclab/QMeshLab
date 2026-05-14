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

### Dependency Installation

The dependencies listed in `vcpkg.json` (e.g., `rapidobj`, `draco`, etc.) are installed automatically using vcpkg's manifest mode. After bootstrapping vcpkg, simply run the following command to install all required dependencies:

```powershell
vcpkg install --triplet x64-windows
```

This will ensure all non-Qt dependencies are set up correctly. Qt6 remains outside of vcpkg and must be installed separately.

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

## GitHub Actions: macOS DMG

The repository includes a manual GitHub Actions workflow at
`.github/workflows/macos-dmg.yml` that builds an unsigned macOS `.dmg`.

What it does:
- checks out the repo with submodules
- installs `ninja` and `libomp` with Homebrew
- installs Qt 6.11 with `install-qt-action`
- reuses the action cache for Qt downloads/install files when available
- bootstraps local `vcpkg`
- configures a `Release` build from the tracked `vcpkg-manifest` preset
- generates a proper macOS `.icns` from the MeshLab app icon and embeds it in the app bundle
- bundles `libomp.dylib` into the app when OpenMP-linked plugins are present
- applies the same icon to the mounted DMG volume when the runner provides `SetFile`
- runs `macdeployqt -dmg`
- uploads the generated `.dmg` as a workflow artifact

How to use it:
1. Open the `Actions` tab on GitHub
2. Select `macOS DMG`
3. Click `Run workflow`
4. Download the `qmeshlab-macos-dmg` artifact from the completed run

Current status:
- packaging is unsigned/ad-hoc only
- the workflow currently runs on `macos-15-intel`, so the produced app is `x86_64`
- Developer ID signing and notarization can be added later using repository secrets

## GitHub Actions: Windows Portable ZIP

The repository also includes a manual GitHub Actions workflow at
`.github/workflows/windows-portable.yml` that builds a portable Windows `.zip`.

What it does:
- checks out the repo with submodules
- sets up MSVC on `windows-2022`
- installs Qt 6.11 from the public `download.qt.io` package archives and caches the local Qt SDK directory
- bootstraps local `vcpkg`
- installs vcpkg dependencies in a separate manifest step before CMake configure
- uses a custom release-only Windows vcpkg triplet so CI does not build debug dependency variants too
- configures and builds a release build with the `vcpkg-manifest` preset
- runs `windeployqt` on `QMeshLab.exe`
- copies additional runtime `.dll` files from the release-only manifest `vcpkg_installed/<triplet>/bin`
- archives the deploy directory as `QMeshLab-portable-win64.zip`
- uploads the generated `.zip` as a workflow artifact

How to use it:
1. Open the `Actions` tab on GitHub
2. Select `Windows Portable`
3. Click `Run workflow`
4. Download the `qmeshlab-windows-portable` artifact from the completed run

Current status:
- packaging is portable `.zip`, not an installer
- the workflow targets `x64` on `windows-2022`
- code signing can be added later if you want a more polished distribution path

## Setting up the development environment on Windows

To build on Windows you need:

1. [Visual Studio 2022](https://aka.ms/vs/17/release/) with the Desktop C++ workload.
2. [CMake 3.25+](https://cmake.org/download/).
3. [Git](https://git-scm.com/).
4. [Qt 6.11](https://www.qt.io/download/).
5. A local [vcpkg](https://github.com/microsoft/vcpkg) checkout.

Setup steps:

```powershell
git submodule update --init --recursive
git clone https://github.com/microsoft/vcpkg .\vcpkg
.\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Resolve-Path .\vcpkg)
cmake --preset default
cmake --build --preset default
```

Notes:
- The `default` preset uses vcpkg manifest mode through `VCPKG_ROOT`.
- Packages from `vcpkg.json` such as `draco`, `rapidobj`, `tinygltf`, `libe57format`, `xerces-c`, `muparser`, `embree`, `nanobind`, and `cgal` are resolved automatically during configure. Do not install them manually.
- Qt stays outside vcpkg and must be installed separately.
