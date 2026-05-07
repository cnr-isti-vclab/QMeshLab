# Architecture

QMeshLab follows a **single-document, multi-view** architecture:

- one `Document` is the authoritative model
- UI widgets observe it through Qt signals
- rendering ownership is split between shared mesh GPU data and per-view pipelines/state

See also: [Data Model](data_model.md) · [Rendering](rendering.md)

## Architectural Layers

1. Application shell (`src/app`, `src/ui/MainWindow`)
2. Data model (`src/core/Document`, `VCGMesh`, tree undo/redo, logs, progress/cancel state)
3. Plugin interfaces/managers (`src/plugins`) and built-in plugin registration (`plugins/`)
4. Shared mesh GPU cache (`src/render/MeshGpuResourceCache`)
5. Per-view rendering (`RenderWidget`, `ViewTrackball`, `RenderOverlayPanel`)
6. Auxiliary views (`LayerWidget`, `MeshFilterPanel`, log dock, undo graph, status-bar stats/progress)

## Core Components

### `Document`

Owns the ordered mesh list (`MeshEntry`), current mesh index, per-document log, tree-shaped undo/redo history, I/O and filter plugin managers, the shared GPU cache, and memory accounting APIs. Does not own live per-widget pipelines/camera state; those remain in `RenderWidget`. Undo nodes include a serialized `ViewState` snapshot (captured/restored through callbacks, with camera restoration optionally skipped during history jumps).

Each `MeshEntry` stores: identity/revision keys (`meshId`, `geometryRevision`, `materialRevision`), render placement (`transform`), source metadata (`name`, `sourcePath`, `ioMask`), texture metadata (`textureFileNames`, `textureFilePaths`, `textureAssets`), material set, `visible` flag, and the `VCGMesh`.

### `MeshGpuResourceCache`

Central cache for mesh GPU resources, keyed by `(QRhi*, meshId, variant, revision, quality-range mode/min/max where applicable)`. Caches fill batches (vertex/index buffers + PBR textures), wire, edge, points, bbox, selection, and decorator buffers (normals, boundaries, seams). Allows GPU buffer reuse across render-mode changes and across views sharing the same `QRhi`.

### `LineRenderer`

Shared utility for generating triangle-expanded fat-line geometry from line segments. Used by the cache for edge/boundary/seam uploads and by `RenderWidget` fat-line pipelines.

### `RenderWidget`

`QRhiWidget` owning per-view state: pipelines/SRBs/UBOs, fallback textures, quality LUT texture, offscreen targets (depth pick, current-mesh mask), Radiance Scaling pre-pass resources, view mode (`Scene3D` / `ParametrizationUV`), `ViewTrackball`, UV pan/zoom/cache, per-mesh render modes, per-view visibility vector, and quality histogram cache. Implementation is split under `src/render/` across `renderwidget_{render,resources,selection,uv,modes}.cpp`.

### `ViewState`

Lightweight snapshot type (`src/render/viewstate.h`) for one `RenderWidget`: `ViewTrackball::State`, `GlobalRenderSettings`, and per-mesh `PerMeshRenderSettings` map. `MainWindow` wires `Document::setViewStateFunctions(...)` so each undo node captures/restores the current view's camera/render-style state together with mesh data.

### `ViewTrackball`

Scene navigation: arcball/hyperbola rotation, pan, dolly, `Shift+Wheel` vertigo (FOV + compensating dolly), reset/reframe, animated recenter.

### `RenderOverlayPanel`

Compact pass/settings panel: pass toggles, mode-specific world settings page (Scene vs UV), per-pass style controls (colors, widths, lighting/culling, quality histogram), `PerMeshRenderSettings`/`GlobalRenderSettings` sync.

### `MeshFilterPanel`

Filter browser/runner: search box, parameter form from `MeshFilterDescriptor`, optional markdown description, advanced-parameter toggle, per-filter parameter-value cache.

### `MainWindow`

Orchestrates the central splitter (one or more `RenderWidget`s), right-column docks (`LayerWidget` + `MeshFilterPanel`), bottom log dock, status bar (progress bars, frame-time stats), undo graph panel, and menus (file, edit, filters, view, help). Manages split/close, active-view highlight border, document visibility proxy synchronization from the current view, and undo-node thumbnails/snapshots.

## Render Settings Types

Defined in `renderingsettings.h`:

- **`PerMeshRenderSettings`** — one instance per mesh id in `RenderWidget::m_meshRenderModes`. Holds pass toggles, decorator toggles, lighting/culling flags, wire faux-edge handling, fill material and sub-structs (`PlainFillParams`, `PbrFillParams`, `RsFillParams`), colors, sizes, and point color source.
- **`GlobalRenderSettings`** — one instance per view in `m_renderSettings`. Holds scene highlight parameters, background colors, UV viewer options, quality histogram/isolines options, and overlay panel state. `using RenderSettings = GlobalRenderSettings` is provided as an alias.
- **`MeshRenderMode`** — widget-local alias for `PerMeshRenderSettings`.

## Plugin System

**I/O plugins** (`MeshIOPlugin`): `canLoad`/`load`, `canSave`/`save`, dialog filter strings, mask capability.

**Filter plugins** (`MeshFilterPlugin`): plugin id/name, `filters(Document&)` returning descriptors (domain/codomain, requirements, tags, parameters), `runFilter(id, params, Document&)`.

**Managers** (`MeshIOPluginManager`, `MeshFilterPluginManager`): keep plugins in registration order; I/O manager stores per-extension preferred plugin in `QSettings`. Registration via `plugins/meshpluginregistry.*` and `plugins/filterpluginregistry.*`.

Built-in I/O (when enabled at build time): `io_vcg`, `io_obj_rapidobj`, `io_gltf`, `io_e57`.  
Built-in filters (when enabled at build time): `filter_basic`, `filter_func`, `filter_embree`, `filter_select`, `filter_clean`, `filter_meshing`, `filter_screened_poisson`, `filter_sampling`, `filter_unsharp`, `filter_create`, `filter_geodesic`, `filter_texture`, `filter_measure`, `filter_mls`, `filter_sample`, `filter_layer`.

## State Ownership

| Shared (Document) | Per-view (RenderWidget) |
|---|---|
| mesh geometry and material data | per-mesh render modes/styles |
| mesh transforms | per-view visibility vector |
| mesh list, current index | view mode, camera/trackball, UV pan/zoom |
| document visibility proxy | overlay settings, histogram cache |
| logs, progress, plugin registries | pipelines, SRBs, UBOs, render targets |
| undo tree, labels, lanes, snapshots | UV-local GPU cache |
| shared mesh GPU cache | |

Note: undo history stores `ViewState` snapshots, but this is serialized view data attached to undo nodes, not live ownership of render widgets.

## Data Flow

```text
User Action
   │
   ▼
MainWindow (menus, docks, split-view orchestration)
   │
   ├──▶ Document (load/save/filter, undo/redo, logs, progress/cancel)
   │         │
   │         └──▶ MeshGpuResourceCache (shared per-mesh GPU data)
   │
   ├──▶ RenderWidget(s) (per-view passes, camera, modes)
   └──▶ LayerWidget / MeshFilterPanel / Log Dock
```

## Typical Runtime Sequence

1. User opens files → `Document` resolves plugin, loads mesh, emits signals.
2. Views sync mesh/mode/visibility and ensure GPU resources via the shared cache.
3. Rendering runs Scene layered passes or UV passes plus overlays.
4. Undo/redo or undo-graph jumps restore mesh snapshots and the active view snapshot (`ViewState`).
5. Filter runs through the filter manager with progress/cancel and undo integration.
6. Status bar shows load/filter progress and rolling CPU/GPU frame timings.
