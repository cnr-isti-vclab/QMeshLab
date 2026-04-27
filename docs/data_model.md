# Data Model

See also: [Architecture](architecture.md) · [Rendering](rendering.md)

## Core Idea

QMeshLab is **single-document, multi-view**: one `Document` owns canonical meshes and document-level state; widgets consume that state via Qt signals; the shared GPU cache lives in `Document`, while per-view rendering state stays in each `RenderWidget`.

## `Document` Ownership

`Document::MeshEntry` is the canonical mesh record. Each entry stores:

- identity/revision keys: `meshId`, `geometryRevision`, `materialRevision`
- render placement: `renderTransform`
- source metadata: `name`, `sourcePath`, `ioMask`
- texture metadata: `textureFileNames`, `textureFilePaths`
- material metadata: `materialSet`
- mesh state: `visible`, `VCGMesh mesh`

`Document` also owns: current mesh index, document log, I/O and filter plugin managers, shared GPU cache, undo/redo history, operation cancel flag, and memory accounting APIs (`cpuMeshMemoryStats`, `undoMemoryStats`, `gpuMemoryStats`).

## Mesh Data Type

`VCGMesh` is a `vcg::tri::TriMesh` specialization carrying the VCG attributes enabled by the imported data.

## Signals and Reactivity

- mesh lifecycle: `meshAdded`, `meshRemoved`, `meshDataChanged`
- selection/visibility: `currentMeshChanged`, `meshVisibilityChanged`
- progress: `loadProgress*`, `filterProgress*`
- logging: `logCleared`, `logMessageAdded`
- undo/redo: `undoRedoStateChanged`

## I/O Model

`Document::loadMesh()`: resolves import plugin, runs plugin load, updates bbox/normals, initializes transform and material set, resolves texture paths, logs stats, appends entry, emits signals. `reloadMesh(index)` follows the same path while preserving mesh identity.

`Document::saveMesh(...)`: resolves export plugin, passes `MeshIOSaveOptions` (mask, binary, embed textures, Draco options).

Also exposes: `openDialogFilter()`, `saveDialogFilter()`, `saveMaskCapability()`, `importSupportedExtensions()`, `exportSupportedExtensions()`, `importPluginInfos()`, `exportPluginInfos()`.

## Filter Model

Metadata: `filterInfos()`, `loadedFilterPluginSummaries()`.  
Execution: `runFilter(filterKey, parameters)` with callback-based progress and cancel (`requestOperationCancel`, `isOperationCancelRequested`).

## Undo/Redo Model

History is stored as two parallel vectors:

- `m_undoCheckpoints[i]` — full document state after `i` committed actions
- `m_undoLabels[i]` — label of the action transitioning checkpoint `i` → `i+1`
- `m_undoCursor` — index of the current checkpoint

Each checkpoint holds a `std::vector<UndoState::MeshSnapshot>`. A `MeshSnapshot` copies all cheap metadata fields by value and holds geometry behind a `shared_ptr<const VCGMesh>`. `captureUndoState()` interns geometry objects in `m_undoGeometryCache` (keyed by `(meshId, geometryRevision)`, stored as `weak_ptr`): if the revision is unchanged since the last capture, all checkpoints share the same allocation — no deep copy. A cache miss triggers a deep copy. On undo/redo, `restoreUndoState()` deep-copies geometry out of the shared pointer so the live document is always freely mutable. `undoMemoryStats()` de-duplicates shared geometry pointers before summing total bytes.

APIs: `beginUndoStep(label)`, `endUndoStep(commit, restoreOnCancel)`, `undo()`, `redo()`, `clearUndoHistory()`, `setUndoLimit(limit)`. Integrated with all mesh mutations (`add/remove/duplicate/reload/mark changed/visibility`, `setMeshRenderTransform`, `markMeshGeometryChanged`, `markMeshMaterialChanged`).

## Revision and Transform Model

- `setMeshRenderTransform(index, transform)` updates `renderTransform`, emits `meshDataChanged`, records undo.
- `markMeshGeometryChanged(...)` increments `geometryRevision`; GPU geometry resources are rebuilt lazily.
- `markMeshMaterialChanged(...)` increments `materialRevision`; GPU material resources are rebuilt lazily.

## Render Settings Model

Defined in `renderingsettings.h`:

### `PerMeshRenderSettings`

One instance per mesh id in `RenderWidget::m_meshRenderModes`. Holds:

- pass toggles: `showFill`, `showWire`, `showEdges`, `showPoints`, `showBoundingBox`, `showSelection`, `showSelectionFaces`, `showSelectionVertices`
- decorator toggles: `decoratorVertexNormals`, `decoratorFaceNormals`, `decoratorBoundaryEdges`, `decoratorTextureSeams`
- lighting/culling flags: `fillLighting`, `fillBackfaceCulling`, `wireLighting`, `wireBackfaceCulling`, `pointLighting`
- fill material: `fillMaterial` (`Plain` / `Pbr` / `RadianceScaling`) + sub-structs `fillPlain` (`PlainFillParams`), `fillPbr` (`PbrFillParams`), `fillRs` (`RsFillParams`)
- colors and sizes: `fillColor`, `wireColor`/`wireSize`, `edgeColor`/`edgeSize`, `pointColor`/`pointSize`, `bboxWireColor`, decorator colors, `decoratorBoundaryWidth`
- `pointColorSource`: `Constant` / `PerVertex` / `PerVertexQuality`

### `GlobalRenderSettings`

One instance per view in `m_renderSettings`. Holds:

- scene overlay: `highlightCurrentMesh`, `currentMeshOutlineColor`/`Width`, `currentMeshDilateRadius`/`ErodeRadius`, `currentMeshDebugView`, `showTrackballGizmo`, `showBoundingBoxCorners`, `showBoundingBoxDimensions`
- scene background: `sceneBackgroundTopColor`, `sceneBackgroundBottomColor`
- UV viewer: `uvShowReferenceFrame`, `uvShowFullTexture`, `uvTextureIndex`, `uvTextureNearestSampling`
- quality histogram: `showQualityHistogram`, `qualityHistogramBins`, `qualityHistogramSource`, `qualityHistogramFixedRange`, `qualityHistogramMin`/`Max`, `qualityHistogramColorMapId`, `qualityHistogramInvertColorMap`
- overlay panel state: `settingsPanelVisible`, `currentPass`

`using RenderSettings = GlobalRenderSettings` and `using MeshRenderMode = PerMeshRenderSettings` (widget-local alias) are provided.

## Shared vs Per-View State

| Shared (Document) | Per-view (RenderWidget) |
|---|---|
| mesh geometry and material data | per-mesh render modes/styles |
| render transforms | per-view visibility vector |
| mesh list, current index | view mode, camera/trackball, UV pan/zoom |
| document visibility proxy (`MeshEntry::visible`) | overlay settings, histogram cache |
| logs, progress, plugin registries | pipelines, SRBs, UBOs, render targets |
| undo/redo history | UV-local GPU cache (`m_uvMeshGpu`) |
| shared mesh GPU cache | |

`MainWindow` synchronizes document visibility from the active view so the layer panel reflects it; each view keeps its own local visibility vector independently.

## GPU Cache Integration

Renderer-facing APIs on `Document`: `ensureMeshGpuResources(...)`, `fillPassGpuView(...)`, `wirePassGpuView(...)`, `edgePassGpuView(...)`, `edgeFatPassGpuView(...)`, `pointsPassGpuView(...)`, `bboxPassGpuView(...)`, `selectionPassGpuView(...)`, `decoratorPassGpuView(...)`.

Cache keyed by `(QRhi*, meshId, variant, revision, quality-range mode)`. Invalidatable per-RHI or globally. Selection and decorator buffers are first-class cache outputs.

## Memory Diagnostics

`MainWindow::showMemoryInfo()` reads `cpuMeshMemoryStats`, `undoMemoryStats` (de-duplicates shared geometry), and `gpuMemoryStats` from `Document`.

For rendering pipeline details, see [Rendering](rendering.md).
