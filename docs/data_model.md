# Data Model

This note describes the current model/state ownership in QMeshLab.

See also:
- [Architecture](architecture.md)
- [Rendering](rendering.md)

## Core Idea

QMeshLab is **single-document, multi-view**:

- one `Document` owns canonical meshes and document-level state
- widgets consume that state via signals
- rendering keeps shared mesh GPU data in the document, while per-view rendering state stays in each `RenderWidget`

## `Document` Ownership

`Document::MeshEntry` is the canonical mesh record.

Each entry stores:

- identity/revision keys:
  - `meshId`
  - `geometryRevision`
  - `materialRevision`
- source metadata:
  - `name`
  - `sourcePath`
- texture metadata:
  - `textureFileNames`
  - `textureFilePaths`
- mesh state:
  - `visible`
  - `ioMask`
  - `VCGMesh mesh`

`Document` also owns:

- current mesh index (`currentMeshIndex`)
- document log (`LogEntry { message, source }`)
- I/O plugin manager (`MeshIOPluginManager`)
- filter plugin manager (`MeshFilterPluginManager`)
- shared mesh GPU cache (`MeshGpuResourceCache`)
- undo/redo history (`UndoState`, `UndoStep`)
- operation cancel flag used by load/save/filter callbacks

## Mesh Data Type

`VCGMesh` is the common geometry container (`vcg::tri::TriMesh` specialization) carrying the standard VCG attributes enabled by the imported/created data.

## Signals and Reactivity

Main document signals:

- mesh lifecycle:
  - `meshAdded`
  - `meshRemoved`
  - `meshDataChanged`
- selection/visibility:
  - `currentMeshChanged`
  - `meshVisibilityChanged`
- load progress:
  - `loadProgressStarted`
  - `loadProgressUpdated`
  - `loadProgressFinished`
- filter progress:
  - `filterProgressStarted`
  - `filterProgressUpdated`
  - `filterProgressFinished`
- logging:
  - `logCleared`
  - `logMessageAdded`
- undo/redo:
  - `undoRedoStateChanged`

These drive `RenderWidget`, `LayerWidget`, `MeshFilterPanel`, status-bar progress UI, and log dock.

## I/O Model

### Import

`Document::loadMesh()`:

1. resolves import plugin by filename/extension preference
2. runs plugin load with `vcg::CallBackPos`
3. updates bbox and normals (preserves imported vertex normals if present)
4. resolves texture paths, updates mesh/texture metadata
5. logs mesh counts, mask summary, texture info, timing, callback stats
6. appends mesh entry, emits signals, sets current mesh

`Document::reloadMesh(index)` follows a similar path and keeps mesh identity while bumping revisions.

### Export

`Document::saveMesh(...)` / `saveCurrentMesh(...)`:

- resolves export plugin by target filename
- passes `MeshIOSaveOptions` (mask, binary, embed textures, Draco options)
- routes callback logs/progress similarly

`Document` also exposes:

- `openDialogFilter()`
- `saveDialogFilter()`
- `saveMaskCapability(filename)`
- `importSupportedExtensions()`
- `exportSupportedExtensions()`
- plugin metadata (`importPluginInfos()`, `exportPluginInfos()`)

## Filter Model

Filter metadata is exposed through:

- `filterInfos()`
- `loadedFilterPluginSummaries()`

Filter execution:

- `runFilter(filterKey, parameters)`
- `beginFilterProgress(...)`, `finishFilterProgress(...)`
- callback-based progress and cancel support (`requestOperationCancel`, `isOperationCancelRequested`)

Filter keys are fully qualified (`pluginId + local filter id`) in the manager layer, while each plugin executes by local descriptor id.

## Undo/Redo Model

`Document` snapshots full mesh state for undo steps:

- entry metadata
- full `VCGMesh` deep copy
- current mesh index
- mesh id allocator state

APIs:

- `beginUndoStep(label)`
- `endUndoStep(commit, restoreOnCancel)`
- `undo()`, `redo()`
- `clearUndoHistory()`
- `setUndoLimit(limit)`

Mesh mutations (`add/remove/duplicate/reload/mark changed/visibility`) are integrated with this framework.

## Layer Widget Model

`LayerWidget` is a document projection:

- visibility toggle per mesh
- current mesh selection
- compact `ioMask` capability badges
- V/E/F counts with locale separators and right alignment
- texture rows with thumbnail + filename + size

## UV Availability Model

UV mode availability uses document metadata:

- current mesh exists
- current mesh has faces (`FN > 0`)
- `ioMask` contains UV coordinates (`IOM_WEDGTEXCOORD` or `IOM_VERTTEXCOORD`)

## Shared vs Per-View State

Shared/document state:

- canonical mesh list and source/material metadata
- document visibility proxy (`MeshEntry::visible`)
- current mesh index
- logs and operation progress
- plugin/filter registries
- undo/redo history
- shared mesh GPU cache

Per-view (`RenderWidget`) state:

- per-mesh render modes/styles
- per-view visibility vector
- scene vs UV mode
- camera/trackball or UV pan/zoom state
- per-view overlay settings and histogram cache
- per-widget pipelines/SRBs/UBOs/render targets
- UV-local GPU cache (`m_uvMeshGpu`)

`MainWindow` synchronizes document visibility from the current view, so the layer panel reflects the active view while each view still keeps its own local visibility vector.

## GPU Cache Integration

Renderer-facing document APIs:

- `ensureMeshGpuResources(...)`
- `fillPassGpuView(...)`
- `wirePassGpuView(...)`
- `edgePassGpuView(...)`
- `edgeFatPassGpuView(...)`
- `pointsPassGpuView(...)`
- `bboxPassGpuView(...)`
- `selectionPassGpuView(...)`
- `decoratorPassGpuView(...)`

`ensureMeshGpuResources(...)` accepts explicit pass needs, fill/point variants, and quality-range hints.
The underlying cache is keyed by `(QRhi*, meshId, variant, revision)` and can be invalidated per-RHI or globally.

For scene/UV pass execution details, see [Rendering](rendering.md).
