# Data Model

See also: [Architecture](architecture.md) · [Rendering](rendering.md)

## Core Idea

QMeshLab is **single-document, multi-view**: one `Document` owns canonical meshes and document-level state; widgets consume that state via Qt signals; the shared GPU cache lives in `Document`, while per-view rendering state stays in each `RenderWidget`.

## `Document` Ownership

`Document::MeshEntry` is the canonical mesh record. Each entry stores:

- identity/revision keys: `meshId`, `geometryRevision`, `materialRevision`
- render placement: `transform`
- source metadata: `name`, `sourcePath`, `ioMask`
- texture metadata: `textureFileNames`, `textureFilePaths`, `textureAssets`
- material metadata: `materialSet`
- mesh state: `visible`, `VCGMesh mesh`

`Document` also owns: current mesh index, document log (application / VCG / error sources), I/O and filter plugin managers, shared GPU cache, undo/redo history, operation cancel flag, and memory accounting APIs (`cpuMeshMemoryStats`, `undoMemoryStats`, `gpuMemoryStats`).

## Mesh Data Type

`VCGMesh` is a `vcg::tri::TriMesh` specialization carrying the VCG attributes enabled by the imported data.

## Signals and Reactivity

- mesh lifecycle: `meshAdded`, `meshRemoved`, `meshDataChanged`
- selection/visibility: `currentMeshChanged`, `meshVisibilityChanged`
- progress: `loadProgress*`, `filterProgress*`
- logging: `logCleared`, `logMessageAdded`
- undo/redo: `undoRedoStateChanged`

## I/O Model

`Document::loadMesh()`: resolves import plugin, runs plugin load, compacts imported mesh storage, updates bbox/normals, initializes transform and material set, resolves texture paths/assets, logs stats, appends entry, emits signals. `reloadMesh(index)` follows the same path while preserving mesh identity.

`Document::saveMesh(...)`: resolves export plugin, passes `MeshIOSaveOptions` (mask, binary, embed textures, copy associated textures, Draco options).

Also exposes: `openDialogFilter()`, `saveDialogFilter()`, `saveMaskCapability()`, `importSupportedExtensions()`, `exportSupportedExtensions()`, `importPluginInfos()`, `exportPluginInfos()`.

## Filter Model

Metadata: `filterInfos()`, `loadedFilterPluginSummaries()`. Descriptors can be loaded declaratively from filter JSON resources via `FilterDescriptorLoader`, including requirements, input/parameter preparation codes (`FF`, `VF`, `BorderFF`, `BorderVF`, normals, bbox, marks), cleanup hooks, dynamic bounds/default tokens, texture input/output references, mesh parameters with their own requirements/preparation, point/vector parameters, incremental selection, and output-modifies codes.

Execution: `runFilter(filterKey, parameters)` with callback-based progress and cancel (`requestOperationCancel`, `isOperationCancelRequested`). The framework normalizes/validates parameters, exposes `validateFilterInvocation(...)` for preflight checks, prepares requested volatile VCG data, runs pre/post cleanup hooks, compacts modified meshes, and updates geometry/material/selection/transform revisions according to descriptor output codes.

## Undo/Redo Model

History is a tree of full-state nodes, not a flat stack:

- `m_undoNodes` — flat arena of `UndoNode` objects; node id is the vector index
- node `0` — root state before the first recorded action
- `m_undoCurrentNode` — node id representing the current live state
- each `UndoNode` — full `UndoState`, incoming action label, parent id, child ids, display lane, and preferred redo child

Committing an action appends a child to the current node, preserving alternate timelines instead of truncating siblings. `redo()` follows `preferredChild` when present, otherwise the first child. `jumpToUndoNode(nodeId, restoreCamera)` walks through the lowest common ancestor, suppressing intermediate GUI refresh signals and restoring camera only at the final target when requested. `undoTreeInfo()` exposes nodes, lanes, depths, current-node state, and current-path flags for `UndoGraphWidget`.

Undo-tree maintenance APIs keep the graph controllable after branching: `makeUndoRoot(nodeId)` promotes a chosen node to the new root and discards unreachable history, `purgeUndoBranch(nodeId)` deletes a descendant branch, and `linearizeUndoHistory()` keeps only the root-to-current path. These operations preserve the current live state and notify the UI through the normal undo/redo state signal.

Each `UndoState` holds a `std::vector<UndoState::MeshSnapshot>`. A `MeshSnapshot` copies all cheap metadata fields by value (`transform`, names/paths/assets/materials/visibility/mask/revisions) and holds geometry behind a `shared_ptr<const VCGMesh>`. `captureUndoState()` interns geometry objects in `m_undoGeometryCache` (keyed by `(meshId, geometryRevision)`, stored as `weak_ptr`): if the revision is unchanged since the last capture, nodes share the same allocation. A cache miss triggers a deep copy. On undo/redo, `restoreUndoState()` deep-copies geometry out of the shared pointer so the live document is always freely mutable. Main geometry replacement paths use a document-level monotonic revision source (`m_nextGeometryRevision`) so branch-local edits do not accidentally reuse an older cache key after undo/redo navigation. Branch restore also evicts newer cached revisions for restored mesh ids to avoid stale geometry reuse across branches. `undoMemoryStats()` de-duplicates shared geometry pointers across all undo nodes before summing total bytes, while per-step rows report the current path.

Each `UndoState` also stores a `ViewState` snapshot (`src/render/viewstate.h`) captured via `Document::setViewStateFunctions(...)`. Current wiring captures/restores the active `RenderWidget` camera/render-style state (`ViewTrackball::State`, `GlobalRenderSettings`, per-mesh `PerMeshRenderSettings`) with each node. Per-view visibility vectors, UV pan/zoom, and view mode are not part of `ViewState`; camera restore can be skipped when jumping to a node.

APIs: `beginUndoStep(label)`, `endUndoStep(commit, restoreOnCancel)`, `undo()`, `redo()`, `jumpToUndoNode(nodeId, restoreCamera)`, `updateUndoNodeCamera(nodeId)`, `makeUndoRoot(nodeId)`, `purgeUndoBranch(nodeId)`, `linearizeUndoHistory()`, `undoTreeInfo()`, `clearUndoHistory()`, `setUndoLimit(limit)`. Integrated with all mesh mutations (`add/remove/duplicate/reload/rename/visibility`, `setMeshTransform`, `markMeshGeometryChanged`, `markMeshMaterialChanged`, `markMeshSelectionChanged`).

## Revision and Transform Model

- `setMeshTransform(index, transform, contextMessage)` updates `MeshEntry::transform`, emits `meshDataChanged`, records undo.
- `markMeshGeometryChanged(...)` advances `geometryRevision`; GPU geometry resources are rebuilt lazily. Main geometry replacement/edit paths allocate revisions from a monotonic document counter so undo branches cannot collide on the same `(meshId, geometryRevision)` cache key.
- `markMeshMaterialChanged(...)` increments `materialRevision`; GPU material resources are rebuilt lazily.
- `markMeshSelectionChanged(...)` increments `geometryRevision` because selection flags live inside the mesh geometry snapshot.

## Render Settings Model

Defined in `renderingsettings.h`:

### `PerMeshRenderSettings`

One instance per mesh id in `RenderWidget::m_meshRenderModes`. Holds:

- pass toggles: `showFill`, `showWire`, `showEdges`, `showPoints`, `showBoundingBox`, `showSelection`, `showSelectionFaces`, `showSelectionVertices`
- decorator toggles: `decoratorVertexNormals`, `decoratorFaceNormals`, `decoratorBoundaryEdges`, `decoratorTextureSeams`, `decoratorNonManifoldEdges`, `decoratorNonManifoldVertices`, `decoratorCurvatureDir`
- lighting/culling flags: `fillLighting`, `fillBackfaceCulling`, `wireLighting`, `wireBackfaceCulling`, `wireRespectFaux`, `pointLighting`
- fill material: `fillMaterial` (`Plain` / `Pbr` / `RadianceScaling`) + sub-structs `fillPlain` (`PlainFillParams`), `fillPbr` (`PbrFillParams`), `fillRs` (`RsFillParams`, including RS shading mode)
- colors and sizes: `fillColor`, `wireColor`/`wireSize`, `edgeColor`/`edgeSize`, `pointColor`/`pointSize`, `bboxWireColor`, decorator colors, `decoratorBoundaryWidth`
- `pointColorSource`: `Constant` / `PerVertex` / `PerVertexQuality`

### `GlobalRenderSettings`

One instance per view in `m_renderSettings`. Holds:

- scene overlay: `highlightCurrentMesh`, `currentMeshOutlineColor`/`Width`, `currentMeshDilateRadius`/`ErodeRadius`, `currentMeshDebugView`, `showTrackballGizmo`, `showBoundingBoxCorners`, `showBoundingBoxDimensions`
- scene background: `sceneBackgroundTopColor`, `sceneBackgroundBottomColor`
- UV viewer: `uvShowReferenceFrame`, `uvShowFullTexture`, `uvTextureIndex`, `uvTextureNearestSampling`
- quality histogram/range: `showQualityHistogram`, `qualityHistogramBins`, `qualityHistogramSource`, `qualityHistogramFixedRange`, `qualityHistogramMin`/`Max`, `qualityHistogramCenterOnZero`, `qualityHistogramPercentileCrop`, `qualityHistogramColorMapId`, `qualityHistogramInvertColorMap`, `qualityIsolinesEnabled`, `qualityIsolineCount`
- overlay panel state: `settingsPanelVisible`, `currentPass`

`using RenderSettings = GlobalRenderSettings` and `using MeshRenderMode = PerMeshRenderSettings` (widget-local alias) are provided.

## Shared vs Per-View State

| Shared (Document) | Per-view (RenderWidget) |
|---|---|
| mesh geometry and material data | per-mesh render modes/styles |
| mesh transforms | per-view visibility vector |
| mesh list, current index | view mode, camera/trackball, UV pan/zoom |
| document visibility proxy (`MeshEntry::visible`) | overlay settings, histogram cache |
| logs, progress, plugin registries | pipelines, SRBs, UBOs, render targets |
| undo tree and node snapshots | UV-local GPU cache (`m_uvMeshGpu`) |
| shared mesh GPU cache | |

`MainWindow` synchronizes document visibility from the active view so the layer panel reflects it; each view keeps its own local visibility vector independently.

Undo stores serialized `ViewState` snapshots in undo-node history, but live render-widget ownership remains per-view.

## GPU Cache Integration

Renderer-facing APIs on `Document`: `ensureMeshGpuResources(...)`, `fillPassGpuView(...)`, `wirePassGpuView(...)`, `edgePassGpuView(...)`, `edgeFatPassGpuView(...)`, `pointsPassGpuView(...)`, `bboxPassGpuView(...)`, `selectionPassGpuView(...)`, `decoratorPassGpuView(...)`.

Cache keyed by `(QRhi*, meshId, variant, revision, quality-range mode/min/max, center-on-zero flag, percentile crop, wire faux-edge mode where relevant)`. `MeshSource` carries legacy texture paths, `textureAssets`, material metadata, and quality-range options. Invalidatable per-RHI or globally. Selection and decorator buffers are first-class cache outputs, including normals, boundary/seam lines, non-manifold edge/vertex markers, and curvature direction lines when the mesh provides the required data.

## Memory Diagnostics

`MainWindow::showMemoryInfo()` reads `cpuMeshMemoryStats`, `undoMemoryStats` (de-duplicates shared geometry), and `gpuMemoryStats` from `Document`.

These values are implementation-level mesh/cache estimates (capacity-based for VCG containers, VCG OCF side-data vectors, undo snapshots, and cache-owned GPU allocations), not full process RSS/Activity-Monitor memory.

For rendering pipeline details, see [Rendering](rendering.md).
