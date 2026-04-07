# Architecture

QMeshLab follows a **single-document, multi-view** architecture:

- one `Document` is the authoritative model
- UI widgets observe it via Qt signals
- rendering data and rendering state are intentionally split

## Architectural Layers

1. Application shell (`MainWindow`)
2. Data/model (`Document`, `VCGMesh`, log state)
3. I/O plugins (`MeshIOPlugin*`, plugin registry)
4. Shared GPU cache (`MeshGpuResourceCache`)
5. Per-view rendering (`RenderWidget`, `ViewTrackball`, `RenderOverlayPanel`)
6. Auxiliary views (`LayerWidget`, log dock, status-bar stats/progress)

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
- texture metadata (`textureFileNames`, `textureFilePaths`, selected texture convenience fields)
- view-independent mesh state (`visible`, `VCGMesh mesh`)

The document exposes renderer-oriented methods (`ensureMeshGpuResources`, `*PassGpuView`) but does not own per-widget pipelines or camera state.

### `MeshGpuResourceCache`

Central cache for mesh GPU resources, keyed by:

- `QRhi*` backend instance
- mesh id
- pass variants (fill/points)
- geometry/material revisions

What is cached:

- fill batches (vertex/index buffers + optional texture)
- wire vertex buffer
- points vertex buffer
- bbox vertex buffer

This enables reuse of heavy mesh uploads across rendering mode switches and across views sharing the same `QRhi`.

### `RenderWidget`

`RenderWidget` is a `QRhiWidget` and owns per-view rendering state:

- graphics pipelines
- per-widget SRBs and uniform buffers
- offscreen render targets (depth pick, current mesh mask/morph)
- trackball camera/navigation state
- overlay settings panel integration

It queries mesh GPU views from `Document`/`MeshGpuResourceCache` and issues pass draws each frame.

### `ViewTrackball`

Navigation logic is factored into a dedicated class:

- arcball-like rotate + hyperbola fallback
- pan
- dolly
- `Shift+Wheel` vertigo effect (FOV + compensating dolly)
- reset-to-frame and animated recenter target support (via `RenderWidget`)

### `RenderOverlayPanel`

Compact pass/settings UI for layered rendering:

- pass toggles (current mesh, bbox, points, wire, fill)
- per-pass arrow buttons to open settings page
- one shared settings container with pass-specific pages
- strongly typed `RenderSettings` synchronization

### `MainWindow`

Composition and global orchestration:

- central `RenderWidget`
- right dock `LayerWidget`
- bottom dock log list
- status bar:
  - load progress bar
  - CPU/GPU frame-time label (fixed-width font, rolling 100-frame stats)
- file/view/help actions (`New`, multi-file `Open`, recent files, `Reset Camera`, shading actions)

## Plugin System

### Plugin interface

`MeshIOPlugin` defines:

- `canLoad(filename)`
- `load(filename, mesh, callback, outLoadMask)`
- `filterString()`
- `errorString(errCode)`

### Plugin manager

`MeshIOPluginManager`:

- stores plugins in registration order
- returns first matching loader for a file
- composes file dialog filters from all registered plugins

### Built-in plugin registration

`plugins/meshpluginregistry.*` registers plugins that are enabled/available at build time.

Current plugin families:

- `plugins/io_vcg` (ply/obj/stl/off/vmi)
- `plugins/io_gltf` (gltf/glb, tinygltf)
- `plugins/io_e57` (optional, dependency-gated)

## Views and Responsibilities

| View | Widget | Responsibility |
|------|--------|----------------|
| 3D View | `RenderWidget` | Layered rendering, camera interaction, picking |
| Layer Panel | `LayerWidget` | Visibility toggles, current mesh selection, mesh/data/texture info |
| Log Panel | `QListWidget` in dock | Per-document app + VCG/import logs |

## State Ownership Rules

Shared/document state:

- mesh geometry/material data
- visibility and current mesh
- import metadata and logs
- shared mesh GPU cache

Per-view state:

- camera/trackball state
- overlay render settings
- pipelines/SRBs/uniforms
- offscreen targets and transient frame resources

This rule keeps model consistency while allowing multiple independent views.

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
   |                      \--> LayerWidget / Log Dock
   v
MeshGpuResourceCache (shared GPU mesh resources)
   |
   v
RenderWidget (per-view pipelines + passes + camera)
```

## Typical Runtime Sequence

1. User opens one or more files from `MainWindow`.
2. `Document` loads through plugin, updates metadata/log/progress, emits signals.
3. `RenderWidget` reacts, ensures needed GPU pass resources in shared cache.
4. Frame render runs layered passes and optional current-mesh outline/picking logic.
5. Frame CPU/GPU timings are emitted to `MainWindow` and shown in status bar stats.
