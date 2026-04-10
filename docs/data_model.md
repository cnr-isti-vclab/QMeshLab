# Data Model

This note describes the current data model used by QMeshLab.

See also:
- [Architecture](architecture.md)
- [Rendering](rendering.md)

## Core Idea

The application is **single-document, multi-view**:

- one `Document` owns all loaded meshes and log state
- UI/view widgets observe the document through Qt signals
- rendering state is split between document-owned mesh data/cache and per-view render state

## Document Ownership

`Document` is the canonical owner of mesh data (`Document::MeshEntry`).

Each mesh entry stores:

- identity and cache keys:
  - `meshId` (stable unique id)
  - `geometryRevision`
  - `materialRevision`
- source metadata:
  - `name`
  - `sourcePath`
- texture metadata:
  - `textureFileNames` / `textureFilePaths` (multiple textures supported)
- mesh state:
  - `visible`
  - `ioMask` (attributes present in the imported file)
  - `VCGMesh mesh`

The document also owns:

- `currentMeshIndex` (shared selection across views)
- per-document log (`LogEntry { message, source }`)
- `MeshIOPluginManager`
- `MeshGpuResourceCache` (shared mesh GPU cache, keyed by `QRhi*`)
- import plugin preference accessors (per extension):
  - `preferredImportPluginForExtension(...)`
  - `setPreferredImportPluginForExtension(...)`

## Mesh Data Type

Geometry uses `VCGMesh` (`vcg::tri::TriMesh` specialization) with standard VCG components (position, normals, colors, quality, wedge texcoords, etc. depending on load mask and file data).

## Signals and Reactive Views

The document emits signals for all relevant state changes:

- mesh lifecycle: `meshAdded`, `meshRemoved`
- visibility/current mesh: `meshVisibilityChanged`, `currentMeshChanged`
- loading progress: `loadProgressStarted`, `loadProgressUpdated`, `loadProgressFinished`
- log updates: `logCleared`, `logMessageAdded`

`RenderWidget`, `LayerWidget`, status bar progress UI, and the log dock subscribe to these signals.

## Loading Pipeline

`Document::loadMesh()`:

1. selects plugin from `MeshIOPluginManager`
2. loads mesh + optional callback progress (`vcg::CallBackPos`)
3. updates bounding box
4. preserves imported vertex normals if present, otherwise computes them when needed
5. collects texture declarations from `mesh.textures` and resolves absolute paths
6. stores mesh entry, logs detailed file/texture info, emits progress and mesh signals
7. sets the loaded mesh as current

Progress callback behavior:

- callback progress is throttled before emitting UI updates
- periodic `processEvents(...)` keeps the UI responsive during long imports
- callback messages are bucketed and forwarded into the document log
- load timing and callback statistics are logged in application log entries

## Plugin-Based I/O

Built-in plugin registration is centralized in `plugins/meshpluginregistry.*`.
Current import plugins:

- rapidobj OBJ importer (`*.obj`)
- VCGLib generic importer (`*.ply *.obj *.stl *.off *.vmi`)
- glTF importer (`*.gltf *.glb`, tinygltf-based)
- optional E57 importer (`*.e57`)

Selection model:

- plugin manager keeps plugins in registration order
- per-extension preferred plugin id is persisted in `QSettings`
- loader selection tries preferred plugin first, then falls back to first matching plugin
- file dialog filter is built dynamically from loaded plugins

## Layer and Selection Model

The layer dock (`LayerWidget`) mirrors document state:

- per-layer visibility checkbox
- compact data summary from `ioMask` (`VC`, `FC`, `VN`, `WT`, `TX`, ...)
- vertices/faces with locale thousands separators
- texture entries, one line per texture, including texture size
- current mesh highlighted with bold style

Changing current item in the layer tree updates `currentMeshIndex` in the document.

## Parametrization Availability Model

UV mode eligibility is derived from document mesh metadata:

- `RenderWidget::meshHasParametrization(...)` requires:
  - a valid current mesh with faces (`FN > 0`)
  - `ioMask` containing either:
    - `IOM_WEDGTEXCOORD`, or
    - `IOM_VERTTEXCOORD`

This keeps UV capability detection deterministic and tied to imported attributes already tracked in `Document::MeshEntry`.

## Shared vs Per-View State

Document/shared state:

- mesh geometry/material source data
- document-level mesh visibility (`MeshEntry::visible`)
- current mesh index
- log and load progress
- shared mesh GPU cache (`MeshGpuResourceCache`)

Per-view (`RenderWidget`) state:

- per-mesh render modes (fill/wire/edges/points/decorators + style)
- per-view mesh visibility vector
- view mode (`Scene3D` or `ParametrizationUV`)
- camera/trackball state
- UV pan/zoom/fit state (for UV mode)
- UV mesh GPU cache (widget-local, keyed by document mesh id/revisions)
- render settings (`RenderSettings`)
- per-widget pipelines, SRBs, uniform buffers, and render targets

`MainWindow` keeps document visibility and active-view visibility synchronized (document visibility acts as a shared proxy for the active view), while each view still keeps its own local visibility vector.

## GPU Cache Integration

`Document` exposes a renderer-facing API:

- `ensureMeshGpuResources(...)`
- `fillPassGpuView(...)`
- `wirePassGpuView(...)`
- `edgePassGpuView(...)`
- `pointsPassGpuView(...)`
- `bboxPassGpuView(...)`
- `decoratorPassGpuView(...)`

`ensureMeshGpuResources(...)` accepts explicit pass needs (`needFill`, `needWire`, `needEdges`, `needPoints`, `needBoundingBox`, `needDecoratorNormals`, `needDecoratorBoundaries`) so renderers can request only what is needed for the frame.

Under the hood this delegates to `MeshGpuResourceCache`, which caches GPU resources by `(QRhi*, meshId, variants, revisions)`.
The document can release resources per-RHI (`releaseRhiGpuResources`) or globally (`clearAllGpuResources`).
UV mode uses an additional widget-local cache (`RenderWidget::m_uvMeshGpu`) built from document mesh data/revisions for orthographic UV rendering; it may still reuse document fill texture batches when `fillColorSource == Texture`.
For frame-level pass execution details, see [Rendering](rendering.md).
