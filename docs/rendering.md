# Rendering

See also: [Architecture](architecture.md) · [Data Model](data_model.md)

## Overview

`RenderWidget` (`QRhiWidget`) runs in two modes:

- **`Scene3D`**: layered mesh rendering with trackball camera, depth picking, and current-mesh highlight.
- **`ParametrizationUV`**: orthographic UV-space rendering for the current mesh (requires faces + UV coords).

Ownership: `Document` owns canonical mesh data; `MeshGpuResourceCache` (owned by `Document`) holds shared GPU mesh resources; `RenderWidget` owns per-view pipelines/SRBs/UBOs, offscreen targets, camera/UV state, headlight/gizmo state, overlays, and per-mesh render modes.

Undo/redo integration: undo-tree nodes include one `ViewState` snapshot (active view camera + render settings + per-mesh style map) captured/restored via `Document::setViewStateFunctions(...)`. History jumps can restore render style while intentionally leaving the live camera untouched until the final target node.

## Scene3D Request and Plan Pipeline

Scene3D rendering is split into a lightweight request layer and a concrete GPU draw-plan layer.

`RenderFramePassRequests` is collected once per frame from visible meshes and resolved `PerMeshRenderSettings`. It contains one `RenderMeshPassRequests` per visible mesh, plus frame-wide aggregate flags for fill, wire, edges, bbox, points, selection, decorator normals, and decorator boundary/seam/non-manifold resources.

The same request object is used for two things:

1. `prepareDirtyBuffers(...)` asks `Document::ensureMeshGpuResources(...)` only for the cache products needed by the requested passes, plus extra resources needed by current-mesh highlighting.
2. `buildRenderFramePlan(...)` converts the request into concrete draw items after resources/pipelines are available.

`RenderFrameRequest` adds frame-local state to the pass requests: view mode, pixel size, projection matrix, view matrix, and light direction. `RenderFramePlan` is the concrete in-process result: fill items, buffer items, decorator items, and selection items with QRhi buffers/pipelines and material renderer pointers. Pass presence is derived from non-empty draw-item lists (`hasFillPass()`, `hasSceneDrawItems()`, etc.).

This is an internal architecture boundary, not a public serialization contract. A future programmatic/JSON render path should describe request-level intent and let QMeshLab build the GPU `RenderFramePlan` internally.

## Shared GPU Cache (`MeshGpuResourceCache`)

Cache key: `(QRhi*, meshId, variant, geometryRevision, materialRevision)`. Quality variants also include fixed-range mode, min/max, center-on-zero, and percentile crop. Wire resources also track whether faux polygon edges should be respected.

Cached outputs:

- **fill**: one or more batches — vertex/index buffers, optional base/normal/occlusion/roughness textures and per-batch PBR factors. Variants: `Constant`, `PerVertex`, `PerFace`, `PerVertexQuality`, `PerFaceQuality`, `Texture`. Texture lookup can use legacy texture paths, `MeshIOTextureAsset` entries, and material slots. PBR normal textures can be interpreted as tangent-space maps or object-space maps according to `fillPbr.normalMapSpace`.
- **wire**: barycentric-expanded triangle buffer, optionally honoring faux-edge bits for polygonal faces.
- **edges**: line buffer + fat-line buffer from explicit mesh edges.
- **points**: position/color/normal payload + normal-valid flag. Variants: `Constant`, `PerVertex`, `PerVertexQuality`.
- **bbox**: line buffer.
- **selection**: selected-face triangles, selected-vertex points.
- **decorators**: vertex normals, face normals, boundary edges (line + fat-line), texture seams (line + fat-line), non-manifold edges (line + fat-line), non-manifold vertices, and curvature principal-direction lines.

Fill uses an indexed path (shared vertices) or an expanded-triangle path for per-face colors or texture batching. For quality variants, normalized quality is stored in the buffer and resolved via LUT sampling in shaders. Changing colormap, inversion, or isoline settings updates only the per-view LUT texture; changing fixed range, center-on-zero, or percentile crop changes normalization and rebuilds the affected quality buffers.

Boundary extraction: topological edge incidence (`incidentCount == 1`). Non-manifold edge extraction: topological edge incidence above two. Seam extraction: per-topological-edge UV sample comparison (texture-index changes, missing/invalid UV).

## Per-Mesh Render Modes

`RenderWidget` holds a `PerMeshRenderSettings` per mesh id (`m_meshRenderModes`; `MeshRenderMode` is a local alias). View-level settings are in `m_renderSettings` (`GlobalRenderSettings`). See [Data Model](data_model.md) for field details.

Default mode for new meshes:
- surfaces (`FN > 0`): fill on, wire on for `FN < 10000`
- edge-only meshes: edges on (`edgeSize = 4.0`)
- point-only: points on

Default fill color source preference: texture → per-vertex → per-face → per-vertex-quality → per-face-quality → constant, clamped to mesh `ioMask` + texture availability. Texture availability is based on `Document::meshTextureAssociationCount(...)`.

## `Scene3D` Frame Sequence

1. Advance animation/camera state, sync per-mesh render modes, and update the camera frame when needed.
2. Collect `RenderFramePassRequests` once for visible meshes.
3. Prepare shared mesh GPU resources from those requests.
4. Upload per-frame textures/UBOs such as the quality LUT, gizmo buffers, light gizmo UBO, and background gradient.
5. Execute depth pick (if scheduled).
6. Build current-mesh highlight masks (if `highlightCurrentMesh`).
7. Build a concrete `RenderFramePlan` from a `RenderFrameRequest`.
8. Run Radiance Scaling gradient pre-pass (if any planned fill item uses `FillMaterial::RadianceScaling`).
9. Run main onscreen pass.

Main pass draw order: scene background · fill · wire · edges · bbox · points · decorators · trackball gizmo · light gizmo · current-mesh outline/debug composite · selection overlay.

## `Scene3D` Pass Details

**Scene background**: full-screen gradient triangle, `sceneBackgroundBottomColor`/`TopColor`, drawn first.

**Fill**: Fill planning and execution are isolated in `renderwidget_fill.cpp`. `SceneFillFramePlan` contains fill draw items, each with a material renderer selected from `PlainFillRenderer`, `PbrFillRenderer`, or `RadianceScalingFillRenderer`. Shared behavior such as UBO upload, texture SRB resolution, selected PBR texture lookup, and Radiance Scaling gradient resource access goes through `FillRenderServices`.

Smooth/Flat shading use distinct shader pairs. Depth test+write on; `fillBackfaceCulling` controls culling. Quality variants LUT-sample from the per-view colormap texture, including optional isoline stripes. Quality normalization honors fixed range, center-on-zero, and percentile crop settings; automatic ranges default to a `0.01` crop at both tails to reduce outlier influence when filters map newly computed quality values to color. PBR binds base/normal/occlusion/roughness per batch. Tangent-space normal maps derive TBN from screen-space derivatives of view position and UV; object-space normal maps are transformed by the normal matrix and blended with the base normal using `normalScale`. Radiance Scaling pre-pass is implemented by `RadianceScalingFillRenderer::renderPrepass(...)`; it renders planned RS fill batches into `m_rsGradTexture` (`RGBA32F`), storing `(gx, gy, logZ, 1)`, and the main fill pass samples it for final RS shading.

**Wireframe**: barycentric triangles + fragment edge test; depth `LessOrEqual`, no depth write; alpha blending; `wireBackfaceCulling`; optional `wireRespectFaux` controls faux polygon edge handling in the cached wire data.

**Edges**: fat-edge triangles when available, line fallback; depth `LessOrEqual`; alpha blending; width from `edgeSize`.

**Bounding box**: line topology; depth on, no depth write.

**Points**: `QRhiGraphicsPipeline::Points`; depth test+write; point lighting, size, and color source from settings; quality variant LUT-sampled.

**Decorators**: depth `LessOrEqual`, no depth write. Normals and curvature directions use the line pipeline. Boundary, seams, and non-manifold edges use the fat-decorator pipeline (`decoratorBoundaryWidth`) with line fallback. Non-manifold vertices use a point pipeline.

**Selection overlay** (final pass): semi-transparent red fill triangles + red vertex points; depth `LessOrEqual`, no depth write; per-mesh `showSelection`/`showSelectionFaces`/`showSelectionVertices`.

Simple buffer pass execution (wire, edges, bbox, points), decorator execution, and selection execution are isolated in `renderwidget_scene_passes.cpp`. Their draw order remains controlled by `renderwidget_render.cpp`.

## Current Mesh Highlight

Runs when `highlightCurrentMesh` is on and the current mesh is visible.

**Surface/edge path**: render current mesh depth mask → extract silhouette into `work` → render all other meshes into `mask` for occlusion → composite outline. Occluded outline portions use half alpha.

**Point-cloud path**: render occupancy mask → dilate (`base → work`) → erode (`work → mask`) → composite outline from final mask.

Debug views: `FullMask`, `VisibleMask`, `OccludedMask`, `DilatedMask`, `ErodedMask`. Normal path is `Outline`.

## Depth Picking

Double click schedules an offscreen depth-pick frame: depth encoded in RGB → one pixel read back → backend conventions normalized (Y flip, clip-depth range) → unprojected via inverse MVP → `trackballCenterPicked(worldPos)` emitted, animated recenter starts.

## `Scene3D` Camera and Interaction

`ViewTrackball`: left drag = arcball/hyperbola rotation; middle/right drag or `Ctrl+Left` = pan; wheel = dolly; `Shift+Wheel` = vertigo (FOV + compensating dolly); double click = depth-pick + animated recenter. `Ctrl+Shift+Left` rotates the view-space headlight and shows the light gizmo while dragging. Gizmo is depth-aware and scale-stable across dolly/FOV changes. `MainWindow` can optionally synchronize camera state across 3D views; UV views keep independent pan/zoom.

## `ParametrizationUV` Frame Sequence

Current status: UV mode is still a separate renderer in `renderwidget_uv.cpp`. It shares the document, per-mesh render settings, color-map/range controls, and some mesh GPU products, but it does not yet consume the Scene3D `RenderFrameRequest`/`RenderFramePlan` path. The intended next architecture step is UV convergence: reuse the Scene3D material/fill lighting path and substitute UV-space geometry/projection so lighting can remain the original 3D lighting reprojected into texture space.

1. Sync per-mesh mode state and UV cache against current document.
2. Ensure UV resources (`m_uvMeshGpu`); fit UV view if requested.
3. Draw UV background (`uv_background.vert/.frag`).
4. If `uvShowFullTexture`: draw the requested background texture over `[0,1]²` (best-effort from `uvTextureIndex`, with fallback to first available base-color texture; `uvTextureNearestSampling` switches bilinear → nearest).
5. Draw current mesh in UV space: fill · wire · edges · boundary edges · texture seams · points.
6. If `uvShowReferenceFrame`: draw unit square outline + colored U/V axes from origin.

UV full-texture background: selection is resolved from fill batches by `textureGroupIndex` (base-color textures); if not found, falls back to the first available base-color texture.

UV fill: color source from `fillPlain.colorSource`. Quality UV buffers use the same fixed-range, center-on-zero, and percentile-crop normalization controls as Scene3D quality rendering. When `Texture`, `renderParametrization()` resolves `uvTextureIndex` against `Document::meshTextureAssociationCount(...)` / texture association helpers and matches by normalized path across all four PBR channels (base, normal, occlusion, roughness) to choose a texture pointer. All fill batches are still drawn (no geometry filtering by texture group), using that selected texture when available, otherwise each batch's base-color texture. Non-texture paths force `fillMaterial = Plain`. Scene corner/dimension overlays are hidden in UV mode.

## `ParametrizationUV` Camera and Interaction

Orthographic projection; `m_uvPan` + `m_uvZoom`. Left/middle drag = pan; wheel = zoom around cursor; double click = fit to mesh UV bounds (or `[0,1]²` when `uvShowFullTexture`); `Reset Camera` = UV fit.

Undo/redo restores trackball/render-style `ViewState`; UV pan/zoom and per-view visibility remain local runtime state.

## Texture Normal-Map Workflow

PBR rendering can consume normal maps directly as either tangent-space or object-space textures through the render overlay's `Normal space` control. The Texture filter `convert_object_space_normal_map_to_tangent_space` converts an associated object-space normal map into a tangent-space image for the selected UV/material slot. It samples only faces using that slot, supports axis inversion and output-to-existing-or-new texture targets, and can bind the generated image as the selected material slot's PBR normal texture.

## Quality Histogram Overlay

2D overlay label inside `RenderWidget`. Controlled by `showQualityHistogram` and `qualityHistogramSource` (auto / forced vertex / forced face). Configurable bin count, optional fixed range (`qualityHistogramFixedRange`/`Min`/`Max`), center-on-zero mode, percentile crop (default `0.01` for automatic ranges), selectable colormap (`qualityHistogramColorMapId`), colormap inversion, and optional isolines (`qualityIsolinesEnabled`, `qualityIsolineCount`). Color mapping is shared with quality-based rendering so histogram colors and rendered quality colors stay aligned.

## Snapshot Capture

`MainWindow::saveSnapshotPng()`: set fixed color-buffer size → request update → wait for `frameRendered` → `grabFramebuffer` → restore previous size. Saved PNG embeds camera JSON in `QMeshLab.CameraTrackballState` metadata.

## Frame Timing

`RenderWidget` emits per-frame CPU time (`QElapsedTimer`) and GPU time when `QRhi::Timestamps` is supported (`lastCompletedGpuTime`). `MainWindow` displays a rolling 100-frame average in the status bar.
