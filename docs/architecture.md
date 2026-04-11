# Architecture

QMeshLab follows a **single-document, multi-view** architecture:

- one `Document` is the authoritative model
- UI widgets observe it via Qt signals
- rendering data and rendering state are intentionally split

See also:
- [Data Model](data_model.md)
- [Rendering](rendering.md)

## Architectural Layers

1. Application shell (`MainWindow`)
2. Data model (`Document`, `VCGMesh`, log state)
3. I/O plugins (`MeshIOPlugin*`, plugin registry)
4. Shared GPU cache (`MeshGpuResourceCache`)
5. Per-view rendering (`RenderWidget`, `ViewTrackball`, `RenderOverlayPanel`)
6. Auxiliary views (`LayerWidget`, log dock, status-bar progress/frame stats)

## Core Components

### `Document`

`Document` owns:

- ordered mesh list (`MeshEntry`)
- current mesh index
- per-document log
- load progress forwarding
- plugin manager
- shared GPU mesh cache

`MeshEntry` stores:

- identity/revisions (`meshId`, `geometryRevision`, `materialRevision`)
- source metadata (`name`, `sourcePath`, `ioMask`)
- texture metadata (`textureFileNames`, `textureFilePaths`)
- view-independent mesh state (`visible`, `VCGMesh mesh`)

The document exposes renderer-facing APIs:

- `ensureMeshGpuResources(...)`
- `fillPassGpuView(...)`
- `wirePassGpuView(...)`
- `edgePassGpuView(...)`
- `edgeFatPassGpuView(...)`
- `pointsPassGpuView(...)`
- `bboxPassGpuView(...)`
- `decoratorPassGpuView(...)`

It does not own per-widget pipelines, per-widget mesh render modes, or camera state.
For ownership, signals, and loading specifics, see [Data Model](data_model.md).

### `MeshGpuResourceCache`

Central cache for mesh GPU resources, keyed by:

- `QRhi*` backend instance
- mesh id
- pass variants (fill/points)
- geometry/material revisions

What is cached:

- fill batches (vertex/index buffers + optional texture)
- wire vertex buffer
- edge line buffer + edge fat-line buffer
- points vertex buffer
- bbox vertex buffer
- decorator buffers:
  - vertex normals
  - face normals
  - geometric boundaries (line + fat-line)
  - texture seams (line + fat-line)

This enables reuse of heavy mesh uploads across rendering mode switches and across views sharing the same `QRhi`.

### `LineRenderer`

`LineRenderer` is a small shared utility used to build triangle-expanded "fat line" geometry from line segments:

- consumed by `MeshGpuResourceCache` for edge and boundary/seam uploads
- consumed by `RenderWidget` fat-edge/fat-decorator pipelines
- keeps line-thickness behavior consistent across Scene and UV modes

### `RenderWidget`

`RenderWidget` is a `QRhiWidget` and owns per-view rendering state:

- graphics pipelines
- per-widget SRBs and uniform buffers
- offscreen render targets for Scene mode (depth pick, current mesh mask/morph)
- view mode state (`Scene3D` / `ParametrizationUV`)
- 3D camera/navigation state (`ViewTrackball`)
- UV view state (`m_uvPan`, `m_uvZoom`, fit/pan interaction state)
- per-widget UV mesh GPU cache (rebuilt from document meshes/revisions)
- overlay settings panel integration
- per-mesh render modes (keyed by mesh id)
- per-view mesh visibility vector

In `Scene3D` mode it queries pass GPU views from `Document`/`MeshGpuResourceCache`.
In `ParametrizationUV` mode it renders an orthographic UV view for the current mesh and can reuse textured fill batches from the document cache.
For pass-level behavior and draw order, see [Rendering](rendering.md).

### `ViewTrackball`

Navigation logic for `Scene3D` mode is factored into a dedicated class:

- arcball-like rotate + hyperbola fallback
- pan
- dolly
- `Shift+Wheel` vertigo effect (FOV + compensating dolly)
- reset-to-frame and animated recenter target support (via `RenderWidget`)

### `RenderOverlayPanel`

Compact pass/settings UI for layered rendering:

- pass toggles (current mesh, normal decorators, boundary/seam decorators, bbox, points, edges, wire, fill)
- per-pass arrow buttons to open settings page
- one shared settings container with pass-specific pages
- strongly typed `RenderSettings` synchronization
- includes width controls for edges and boundary/seam decorators

### `MainWindow`

Composition and global orchestration:

- central splitter containing one or more `RenderWidget` views
- right dock `LayerWidget`
- bottom dock log list
- status bar:
  - load progress bar
  - CPU/GPU frame-time label (fixed-width font, rolling 100-frame stats)
- file/view/help actions:
  - file: `New`, `New Instance`, multi-file `Open`, `Snapshot PNG`, recent files
  - view: `3D Scene Mode`, `Parametrization (UV) Mode`, split horizontal/vertical, `Reset Camera`, camera/trackball JSON copy/paste
  - help: about + import plugin preference dialog
- active-view management (style border + explicit current-view indicator widget when multiple views are open, context menu split/close)
- snapshot export:
  - output resolution options with aspect-ratio lock
  - offscreen capture from the active view
  - writes camera state into PNG text metadata (`QMeshLab.CameraTrackballState`)

## Plugin System

### Plugin Interface

`MeshIOPlugin` defines:

- `canLoad(filename)`
- `load(filename, mesh, callback, outLoadMask)`
- `filterString()`
- `errorString(errCode)`

### Plugin Manager

`MeshIOPluginManager`:

- stores plugins in registration order
- supports per-extension preferred plugin selection (persisted in `QSettings`)
- resolves loader by preferred plugin first, then first registered matching plugin
- composes file dialog filters from all registered plugins

### Built-in plugin registration

`plugins/meshpluginregistry.*` registers plugins that are enabled/available at build time.

Current plugin families:

- `plugins/io_obj_rapidobj` (obj, rapidobj-based)
- `plugins/io_vcg` (ply/obj/stl/off/vmi)
- `plugins/io_gltf` (gltf/glb, tinygltf)
- `plugins/io_e57` (optional, dependency-gated)

## Views and Responsibilities

| View | Widget | Responsibility |
|------|--------|----------------|
| Render View (`Scene3D` or `ParametrizationUV`) | `RenderWidget` | Mode-specific rendering and interaction, per-view mesh modes/visibility, frame stats |
| Layer Panel | `LayerWidget` | Visibility toggles, current mesh selection, mesh/data/texture info |
| Log Panel | `QListWidget` in dock | Per-document app + VCG/import logs |

## State Ownership Rules

Shared/document state:

- mesh geometry/material data
- document-level layer visibility + current mesh
- import metadata and logs
- shared mesh GPU cache

Per-view state:

- mesh render-mode preferences (show fill/wire/edges/points/decorators, styling)
- per-view mesh visibility vector
- view mode (`Scene3D` or `ParametrizationUV`)
- camera/trackball state (Scene mode)
- UV pan/zoom/fit state + UV temporary GPU cache (UV mode)
- overlay render settings
- current-view indicator widget state
- pipelines/SRBs/uniforms
- offscreen targets and transient frame resources (Scene-mode highlight/depth pick path)

This keeps model consistency while allowing multiple independent views and render styles.

## Data Flow

```text
User Action
   |
   v
MainWindow (menu/toolbar/status orchestration)
   |
   v
Document (load/mutate/log/signal)
   |                     \
   |                      \--> LayerWidget / Log Dock / Progress UI
   v
MeshGpuResourceCache (shared GPU mesh resources)
   |
   v
RenderWidget (per-view modes, visibility, pipelines, passes, camera)
```

## Typical Runtime Sequence

1. User opens one or more files from `MainWindow`.
2. `Document` loads through plugin, updates metadata/log/progress, emits signals.
3. Each `RenderWidget` resolves its mode (`Scene3D` or `ParametrizationUV`) and ensures needed resources.
4. Frame render executes either:
   - Scene path: layered 3D passes (`fill`, `wire`, `edges` with fat-line path when available, `bbox`, `points`, decorators, gizmo, current-mesh highlight).
   - UV path: orthographic UV rendering of the current mesh plus UV background, boundary/seam overlays, and optional unit-box overlay.
5. Frame CPU/GPU timings are emitted to `MainWindow` and shown in status bar stats.
