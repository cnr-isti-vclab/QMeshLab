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
- edge vertex buffer
- points vertex buffer
- bbox vertex buffer
- decorator buffers:
  - vertex normals
  - face normals
  - geometric boundaries
  - texture seams

This enables reuse of heavy mesh uploads across rendering mode switches and across views sharing the same `QRhi`.

### `RenderWidget`

`RenderWidget` is a `QRhiWidget` and owns per-view rendering state:

- graphics pipelines
- per-widget SRBs and uniform buffers
- offscreen render targets (depth pick, current mesh mask/morph)
- trackball camera/navigation state
- overlay settings panel integration
- per-mesh render modes (keyed by mesh id)
- per-view mesh visibility vector

It queries mesh GPU views from `Document`/`MeshGpuResourceCache` and issues pass draws each frame.
For pass-level behavior and draw order, see [Rendering](rendering.md).

### `ViewTrackball`

Navigation logic is factored into a dedicated class:

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
  - view: split horizontal/vertical, `Reset Camera`, camera/trackball JSON copy/paste
  - help: about + import plugin preference dialog
- active-view management (view border highlight, context menu split/close)

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
| 3D View | `RenderWidget` | Layered rendering, camera interaction, picking, per-view mesh modes |
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
- camera/trackball state
- overlay render settings
- pipelines/SRBs/uniforms
- offscreen targets and transient frame resources

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
3. `RenderWidget` ensures required pass resources in `MeshGpuResourceCache` for visible meshes.
4. Frame render runs layered passes (`fill`, `wire`, `edges`, `bbox`, `points`, decorators, gizmo, current-mesh highlight).
5. Frame CPU/GPU timings are emitted to `MainWindow` and shown in status bar stats.
