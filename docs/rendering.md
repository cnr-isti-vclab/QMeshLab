# Rendering

See also: [Architecture](architecture.md) · [Data Model](data_model.md)

## Overview

`RenderWidget` (`QRhiWidget`) runs in two modes:

- **`Scene3D`**: layered mesh rendering with trackball camera, depth picking, and current-mesh highlight.
- **`ParametrizationUV`**: orthographic UV-space rendering for the current mesh (requires faces + UV coords).

Ownership: `Document` owns canonical mesh data; `MeshGpuResourceCache` (owned by `Document`) holds shared GPU mesh resources; `RenderWidget` owns per-view pipelines/SRBs/UBOs, offscreen targets, camera/UV state, and per-mesh render modes.

## Shared GPU Cache (`MeshGpuResourceCache`)

Cache key: `(QRhi*, meshId, variant, geometryRevision, materialRevision)`. Quality variants also include fixed-range mode and min/max.

Cached outputs:

- **fill**: one or more batches — vertex/index buffers, optional base/normal/occlusion/roughness textures and per-batch PBR factors. Variants: `Constant`, `PerVertex`, `PerFace`, `PerVertexQuality`, `PerFaceQuality`, `Texture`.
- **wire**: barycentric-expanded triangle buffer.
- **edges**: line buffer + fat-line buffer from explicit mesh edges.
- **points**: position/color/normal payload + normal-valid flag. Variants: `Constant`, `PerVertex`, `PerVertexQuality`.
- **bbox**: line buffer.
- **selection**: selected-face triangles, selected-vertex points.
- **decorators**: vertex normals, face normals, boundary edges (line + fat-line), texture seams (line + fat-line).

Fill uses an indexed path (shared vertices) or an expanded-triangle path for per-face colors or texture batching. For quality variants, normalized quality is stored in the buffer and resolved via LUT sampling in shaders — changing colormap does not rebuild GPU buffers.

Boundary extraction: topological edge incidence (`incidentCount == 1`). Seam extraction: per-topological-edge UV sample comparison (texture-index changes, missing/invalid UV).

## Per-Mesh Render Modes

`RenderWidget` holds a `PerMeshRenderSettings` per mesh id (`m_meshRenderModes`; `MeshRenderMode` is a local alias). View-level settings are in `m_renderSettings` (`GlobalRenderSettings`). See [Data Model](data_model.md) for field details.

Default mode for new meshes:
- surfaces (`FN > 0`): fill on, wire on for `FN < 10000`
- edge-only meshes: edges on (`edgeSize = 4.0`)
- point-only: points on

Default fill color source preference: texture → per-vertex → per-face → per-vertex-quality → per-face-quality → constant, clamped to mesh `ioMask` + texture availability.

## `Scene3D` Frame Sequence

1. Ensure GPU resources for visible meshes.
2. Execute depth pick (if scheduled).
3. Build current-mesh highlight masks (if `highlightCurrentMesh`).
4. Upload background gradient to per-view UBO.
5. Run Radiance Scaling gradient pre-pass (if any mesh uses `FillMaterial::RadianceScaling`).
6. Run main onscreen pass.

Main pass draw order: scene background · fill · wire · edges · bbox · points · decorators · trackball gizmo · current-mesh outline/debug composite · selection overlay.

## `Scene3D` Pass Details

**Scene background**: full-screen gradient triangle, `sceneBackgroundBottomColor`/`TopColor`, drawn first.

**Fill**: Smooth/Flat shading use distinct shader pairs. Depth test+write on; `fillBackfaceCulling` controls culling. Quality variants LUT-sample from the per-view colormap texture. PBR binds base/normal/occlusion/roughness per batch. Radiance Scaling pre-pass renders fill batches into `m_rsGradTexture` (`RGBA32F`), storing `(gx, gy, logZ, 1)`; the main fill pass samples it for final RS shading.

**Wireframe**: barycentric triangles + fragment edge test; depth `LessOrEqual`, no depth write; alpha blending; `wireBackfaceCulling`.

**Edges**: fat-edge triangles when available, line fallback; depth `LessOrEqual`; alpha blending; width from `edgeSize`.

**Bounding box**: line topology; depth on, no depth write.

**Points**: `QRhiGraphicsPipeline::Points`; depth test+write; point lighting, size, and color source from settings; quality variant LUT-sampled.

**Decorators**: depth `LessOrEqual`, no depth write. Normals: line pipeline. Boundary/seams: fat-decorator pipeline (`decoratorBoundaryWidth`), line fallback.

**Selection overlay** (final pass): semi-transparent red fill triangles + red vertex points; depth `LessOrEqual`, no depth write; per-mesh `showSelection`/`showSelectionFaces`/`showSelectionVertices`.

## Current Mesh Highlight

Runs when `highlightCurrentMesh` is on and the current mesh is visible.

**Surface/edge path**: render current mesh depth mask → extract silhouette into `work` → render all other meshes into `mask` for occlusion → composite outline. Occluded outline portions use half alpha.

**Point-cloud path**: render occupancy mask → dilate (`base → work`) → erode (`work → mask`) → composite outline from final mask.

Debug views: `FullMask`, `VisibleMask`, `OccludedMask`, `DilatedMask`, `ErodedMask`. Normal path is `Outline`.

## Depth Picking

Double click schedules an offscreen depth-pick frame: depth encoded in RGB → one pixel read back → backend conventions normalized (Y flip, clip-depth range) → unprojected via inverse MVP → `trackballCenterPicked(worldPos)` emitted, animated recenter starts.

## `Scene3D` Camera and Interaction

`ViewTrackball`: left drag = arcball/hyperbola rotation; middle/right drag or `Ctrl+Left` = pan; wheel = dolly; `Shift+Wheel` = vertigo (FOV + compensating dolly); double click = depth-pick + animated recenter. Gizmo is depth-aware and scale-stable across dolly/FOV changes.

## `ParametrizationUV` Frame Sequence

1. Sync per-mesh mode state and UV cache against current document.
2. Ensure UV resources (`m_uvMeshGpu`); fit UV view if requested.
3. Draw UV background (`uv_background.vert/.frag`).
4. If `uvShowFullTexture`: draw the texture selected by `uvTextureIndex` over `[0,1]²` (`uvTextureNearestSampling` switches bilinear → nearest).
5. Draw current mesh in UV space: fill · wire · edges · boundary edges · texture seams · points.
6. If `uvShowReferenceFrame`: draw unit square outline + colored U/V axes from origin.

UV fill: color source from `fillPlain.colorSource`. When `Texture`, `renderParametrization()` matches `uvTextureIndex` (an index into the full PBR texture path list) by path across all four PBR channels (base, normal, occlusion, roughness) to find the `QRhiTexture*` pointer, then draws all fill batches with it. Non-texture paths force `fillMaterial = Plain`. Scene corner/dimension overlays are hidden in UV mode.

## `ParametrizationUV` Camera and Interaction

Orthographic projection; `m_uvPan` + `m_uvZoom`. Left/middle drag = pan; wheel = zoom around cursor; double click = fit to mesh UV bounds (or `[0,1]²` when `uvShowFullTexture`); `Reset Camera` = UV fit.

## Quality Histogram Overlay

2D overlay label inside `RenderWidget`. Controlled by `showQualityHistogram` and `qualityHistogramSource` (auto / forced vertex / forced face). Configurable bin count, optional fixed range (`qualityHistogramFixedRange`/`Min`/`Max`), selectable colormap (`qualityHistogramColorMapId`), colormap inversion. Color mapping is shared with quality-based rendering so histogram colors and rendered quality colors stay aligned.

## Snapshot Capture

`MainWindow::saveSnapshotPng()`: set fixed color-buffer size → request update → wait for `frameRendered` → `grabFramebuffer` → restore previous size. Saved PNG embeds camera JSON in `QMeshLab.CameraTrackballState` metadata.

## Frame Timing

`RenderWidget` emits per-frame CPU time (`QElapsedTimer`) and GPU time when `QRhi::Timestamps` is supported (`lastCompletedGpuTime`). `MainWindow` displays a rolling 100-frame average in the status bar.
