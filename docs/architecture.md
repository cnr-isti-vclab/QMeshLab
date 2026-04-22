# Architecture

QMeshLab follows a **single-document, multi-view** architecture:

- one `Document` is the authoritative model
- UI widgets observe it through Qt signals
- rendering ownership is split between shared mesh GPU data and per-view pipelines/state

See also:
- [Data Model](data_model.md)
- [Rendering](rendering.md)

## Architectural Layers

1. Application shell (`MainWindow`)
2. Data model (`Document`, `VCGMesh`, undo/redo, logs, progress/cancel state)
3. Plugin layer (`MeshIOPlugin*`, `MeshFilterPlugin*`, registries/managers)
4. Shared mesh GPU cache (`MeshGpuResourceCache`)
5. Per-view rendering (`RenderWidget`, `ViewTrackball`, `RenderOverlayPanel`)
6. Auxiliary views (`LayerWidget`, `MeshFilterPanel`, log dock, undo-history panel, status-bar stats/progress)

## Core Components

### `Document`

`Document` owns:

- ordered mesh list (`MeshEntry`)
- current mesh index
- per-document log
- load/save/filter progress callback forwarding + cancel flag
- undo/redo stack (`UndoState`, `UndoStep`)
- I/O plugin manager and filter plugin manager
- shared GPU mesh cache
- memory accounting APIs (CPU meshes, undo history, GPU cache)

`MeshEntry` stores:

- identity/revisions (`meshId`, `geometryRevision`, `materialRevision`)
- render placement (`renderTransform`)
- source metadata (`name`, `sourcePath`, `ioMask`)
- texture metadata (`textureFileNames`, `textureFilePaths`)
- material metadata (`materialSet`)
- view-independent mesh state (`visible`, `VCGMesh mesh`)

Renderer-facing APIs include:

- `ensureMeshGpuResources(...)`
- `fillPassGpuView(...)`
- `wirePassGpuView(...)`
- `edgePassGpuView(...)`
- `edgeFatPassGpuView(...)`
- `pointsPassGpuView(...)`
- `bboxPassGpuView(...)`
- `selectionPassGpuView(...)`
- `decoratorPassGpuView(...)`

`Document` does not own per-widget pipelines, per-widget render mode preferences, or camera state.

### `MeshGpuResourceCache`

Central cache for mesh GPU resources, keyed by:

- `QRhi*` backend
- mesh id
- fill/point variants
- geometry/material revisions
- quality-range mode for quality variants (auto vs fixed range + min/max)

Cached resources:

- fill batches (vertex/index buffers + optional base/normal/occlusion/roughness textures and per-batch factors)
- wire buffer
- edge line + edge fat-line buffers
- points buffer
- bbox buffer
- selection buffers (selected faces and selected vertices)
- decorator buffers:
  - vertex normals
  - face normals
  - geometric boundaries (line + fat-line)
  - texture seams (line + fat-line)

This avoids re-uploading mesh data when switching render modes or when multiple views share the same `QRhi`.

### `LineRenderer`

Shared utility for generating triangle-expanded fat-line geometry from line segments:

- used by `MeshGpuResourceCache` for edge/boundary/seam uploads
- used by `RenderWidget` fat-edge/fat-decorator pipelines

### `RenderWidget`

`RenderWidget` (`QRhiWidget`) owns per-view rendering state:

- graphics pipelines/SRBs/UBOs
- fallback textures (base/normal/occlusion/roughness) + quality LUT texture
- offscreen targets used in Scene mode (depth pick + current-mesh mask pipeline)
- Radiance Scaling gradient pre-pass resources
- view mode (`Scene3D` / `ParametrizationUV`)
- Scene camera/navigation state (`ViewTrackball`)
- UV pan/zoom/fit state
- per-widget UV GPU cache
- overlay settings panel integration
- per-mesh render modes (keyed by mesh id)
- per-view mesh visibility vector
- per-view quality histogram overlay cache

In `Scene3D`, it consumes pass views from `Document`/`MeshGpuResourceCache`.
In `ParametrizationUV`, it renders the current mesh in UV space and can reuse textured fill batches from the document cache.
Implementation is split across `renderwidget_*.cpp` translation units (`render`, `resources`, `selection`, `uv`, `modes`) with shared declarations in `renderwidget.h`.

### `ViewTrackball`

Dedicated Scene navigation math and interaction:

- arcball-like rotation with hyperbola fallback
- pan and dolly
- `Shift+Wheel` vertigo behavior (FOV + compensating dolly)
- reset/reframe and animated recenter support

### `RenderOverlayPanel`

Compact pass/settings panel with:

- pass toggles + settings arrows
- mode-specific world settings page (Scene vs UV)
- strongly typed `RenderSettings` sync
- per-pass style controls (colors, widths, lighting/culling options, quality histogram options)

### `MeshFilterPanel`

Filter browser/runner dock:

- search box + searchable result list
- parameter form generated from `MeshFilterDescriptor`
- optional long markdown description (`?` toggle)
- advanced-parameter visibility toggle only when needed
- per-filter parameter-value cache (values persist across runs and undo/redo)

### `MainWindow`

Composition and orchestration:

- central splitter with one or more `RenderWidget`s
- right column docks: `LayerWidget` (top) + `MeshFilterPanel` (bottom)
- bottom log dock
- status bar:
  - load progress bar
  - filter progress bar + cancel button
  - CPU/GPU frame-time stats (rolling 100-frame window)
- undo-history panel with stack labels and thumbnails
- menus:
  - file: `New`, `New Instance`, multi-file `Open`, reload current/all, save mesh, snapshot PNG, recent
  - edit: undo/redo
  - filters: hierarchical filter menu + filter browser
  - view: Scene/UV mode, split H/V, reset camera, copy/paste camera JSON
  - help: about, I/O plugin dialog, filter plugin dialog, memory info
- active-view handling:
  - split/close per current view
  - right-click view context menu (mode/split/close)
  - current-view highlight border when multiple views are open
  - document visibility proxy synchronized from active view

## Plugin System

### I/O Plugin Interface

`MeshIOPlugin` supports both import and export:

- import: `canLoad`, `load`, `filterString`, `errorString`
- export: `canSave`, `save`, `saveFilterString`, `saveMaskCapability`

### Filter Plugin Interface

`MeshFilterPlugin` provides:

- plugin id/name
- filter descriptors (`filters(const Document&)`)
- filter execution (`runFilter(filterId, parameters, Document&)`)

Descriptors include domain/codomain, requirements, tags, short/long descriptions, and typed parameters.

### Plugin Managers

- `MeshIOPluginManager`
- `MeshFilterPluginManager`

Both managers keep plugins in registration order and expose metadata for dialogs/menus.
I/O manager also stores per-extension preferred import plugin in `QSettings`.

### Built-in plugin registration

- I/O: `plugins/meshpluginregistry.*`
- filters: `plugins/filterpluginregistry.*`

Current built-in families (build-option/dependency gated):

- I/O:
  - `io_vcg`
  - `io_obj_rapidobj`
  - `io_gltf`
  - `io_e57`
- filters:
  - `filter_basic`
  - `filter_func`
  - `filter_embree`
  - `filter_select`
  - `filter_clean`
  - `filter_meshing`

## Views and Responsibilities

| View | Widget | Responsibility |
|------|--------|----------------|
| Render View (`Scene3D` or `ParametrizationUV`) | `RenderWidget` | mode-specific rendering/interaction, per-view mesh modes/visibility, frame stats |
| Layer Panel | `LayerWidget` | visibility/current mesh selection, compact mesh/data/texture summary |
| Filter Panel | `MeshFilterPanel` | filter search, parameter editing, run requests |
| Log Panel | `QListWidget` dock | per-document app + VCG logs |
| Undo History | `QListWidget` dock panel | undo stack navigation with labels/thumbnails |

## State Ownership Rules

Shared/document state:

- mesh geometry/material source data
- per-mesh render transforms
- mesh list and current mesh index
- document-level visibility proxy (`MeshEntry::visible`)
- import/export/filter metadata and logs
- undo/redo history
- shared mesh GPU cache

Per-view state:

- per-mesh render-mode/style preferences
- per-view mesh visibility vector
- view mode (`Scene3D`/`ParametrizationUV`)
- camera/trackball and UV pan/zoom state
- overlay settings
- pipelines/SRBs/uniforms/render targets
- transient pass resources (highlight/depth-pick path)

This keeps one canonical document while allowing independent view styles and navigation.

## Data Flow

```text
User Action
   |
   v
MainWindow (menus, docks, split-view orchestration)
   |
   +--> Document (load/save/run filter, undo/redo, logs, progress/cancel)
   |          |
   |          +--> MeshGpuResourceCache (shared per-mesh GPU data)
   |
   +--> RenderWidget(s) (per-view passes, camera, modes)
   +--> LayerWidget / MeshFilterPanel / Log Dock
```

## Typical Runtime Sequence

1. User opens one or more files.
2. `Document` resolves import plugin, loads mesh, logs file/attribute/texture info, emits signals.
3. Views sync mesh/mode/visibility state and ensure needed GPU resources through the shared cache.
4. Rendering runs either Scene layered passes or UV passes (plus overlays).
5. Optional filter run executes through filter manager, with progress/cancel and undo integration.
6. Status bar displays load/filter progress and rolling CPU/GPU frame timings.
