# Data Model

This note describes the current data model used by QMeshLab.

## Core Idea

The application is **single-document**:

- one `Document` owns all loaded meshes and log state
- UI/view widgets observe the document through Qt signals
- rendering state is split between document-owned mesh data and per-view settings

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
  - convenience selected texture: `textureFileName` / `textureFilePath`
- mesh state:
  - `visible`
  - `ioMask` (attributes present in the imported file)
  - `VCGMesh mesh`

The document also owns:

- `currentMeshIndex` (shared selection across views)
- per-document log (`LogEntry { message, source }`)
- `MeshIOPluginManager`
- `MeshGpuResourceCache` (shared mesh GPU cache, keyed by `QRhi*`)

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

Progress callback messages are also forwarded into the document log (deduplicated/bucketed).

## Plugin-Based I/O

Built-in plugin registration is centralized in `plugins/meshpluginregistry.*`.
Current import plugins:

- VCGLib generic importer (`*.ply *.obj *.stl *.off *.vmi`)
- glTF importer (`*.gltf *.glb`, tinygltf-based)
- optional E57 importer (`*.e57`)

The file dialog filter is built dynamically from loaded plugins.

## Layer and Selection Model

The layer dock (`LayerWidget`) mirrors document state:

- per-layer visibility checkbox
- compact data summary from `ioMask` (`VC`, `FC`, `VN`, `WT`, `TX`, ...)
- vertices/faces with locale thousands separators
- texture entries, one line per texture, including texture size
- current mesh highlighted with bold style

Changing current item in the layer tree updates `currentMeshIndex` in the document.

## Shared vs Per-View State

Document/shared state:

- mesh geometry/material source data
- mesh visibility
- current mesh index
- log and load progress
- shared mesh GPU cache (`MeshGpuResourceCache`)

Per-view (`RenderWidget`) state:

- camera/trackball state
- render settings (`RenderSettings`)
- per-widget pipelines, SRBs, uniform buffers, and render targets

This split lets multiple views share mesh data/cache while keeping independent cameras and UI rendering settings.

## GPU Cache Integration

`Document` exposes a renderer-facing API:

- `ensureMeshGpuResources(...)`
- `fillPassGpuView(...)`
- `wirePassGpuView(...)`
- `pointsPassGpuView(...)`
- `bboxPassGpuView(...)`

Under the hood this delegates to `MeshGpuResourceCache`, which caches GPU resources by `(QRhi*, meshId, variants, revisions)`.
The document can release resources per-RHI (`releaseRhiGpuResources`) or globally (`clearAllGpuResources`).

