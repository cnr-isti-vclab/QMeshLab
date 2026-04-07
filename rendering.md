# Rendering

This note describes the current QRhi rendering architecture used by QMeshLab.

## High-Level Architecture

Rendering is performed by `RenderWidget` (`QRhiWidget`).

Data ownership split:

- `Document` owns canonical mesh data.
- `MeshGpuResourceCache` (owned by `Document`) owns shared mesh GPU geometry/texture resources.
- `RenderWidget` owns per-view rendering state (pipelines, SRBs, uniform buffers, offscreen targets, camera/trackball, overlay UI state).

This split is what enables reuse of mesh GPU buffers across rendering mode changes and across views that share the same `QRhi` backend.

## Shared GPU Cache (`MeshGpuResourceCache`)

Cache key shape:

- per backend: `QRhi*`
- per mesh: `meshId`
- per variant:
  - fill: `Constant`, `PerVertex`, `PerFace`
  - points: `Constant`, `PerVertex`
- with revision checks:
  - `geometryRevision`
  - `materialRevision`

Cached pass data:

- fill pass: one or more batches (`vbuf`, optional `ibuf`, optional `texture`)
- wire pass: barycentric-expanded triangle buffer
- points pass: point buffer (position/color/normal payload)
- bbox pass: line buffer

### Fill Buffer Strategy

Two fill upload paths are used:

- indexed mesh path when triangles can share vertices
- expanded-triangle batches when needed for:
  - per-face color
  - texture slot grouping (multi-texture meshes)

When textures are present, faces are grouped by texture slot and each batch carries its texture handle.

## Per-Widget GPU State (`RenderWidget`)

Per-widget resources include:

- dynamic uniform buffers (`m_ubuf`, outline/morph/debug UBOs)
- widget-local samplers and fallback 1x1 white texture
- shader resource bindings:
  - base SRB (UBO + fallback texture)
  - per-texture SRB cache for sampled fill batches
- graphics pipelines (fill/wire/points/bbox, depth-pick, mask/morph/debug/outline, trackball gizmo)
- offscreen render targets/textures for:
  - depth picking
  - current-mesh mask + morphology

These remain per widget because they bind per-view uniforms/settings and render-pass descriptors tied to that widget render target.

## Layered Render Passes

Main visible passes (toggleable):

- Fill
- Wireframe
- Bounding box
- Points
- Current mesh highlight

Additional always-available overlay:

- trackball gizmo (depth-aware line rendering)

Draw order in the main pass:

1. fill
2. wire (alpha blended, independent overlay)
3. bbox
4. points
5. trackball gizmo
6. current mesh outline composite

## Pass Behavior Details

### Fill

- Smooth/Flat shading use distinct shader pairs.
- Depth test/write enabled.
- Backface culling controlled by `fillBackfaceCulling`.
- Color source: constant / per-vertex / per-face.
- Texture sampling supported through fill batches and per-batch SRBs.

### Wireframe

- Uses barycentric-expanded triangles + fragment edge test.
- Separate transparent overlay pass.
- Depth test enabled (`LessOrEqual`), depth write disabled.
- Backface culling toggle (`wireBackfaceCulling`).

### Points

- `QRhiGraphicsPipeline::Points`.
- Depth test and depth write enabled.
- Point size and lighting controlled by settings.
- Color source: constant or per-vertex.
- Vertex payload includes optional normal + availability flag for point lighting.

### Bounding Box

- Line topology.
- Depth test enabled, depth write disabled.
- Color controlled from overlay settings.

## Current Mesh Highlight Pipeline

The current mesh highlight is independent from regular fill/wire/points toggles.

Pipeline:

1. Render current mesh occupancy mask to offscreen RT.
   - surface meshes: fill-style mask pipeline (depth-aware)
   - point clouds: points mask pipeline (no depth/shading, occupancy only)
2. Snapshot base mask.
3. For point clouds, apply screen-space morphology:
   - dilate(base -> work)
   - erode(work -> mask)
4. Final outline extraction/composite from the mask texture.

Debug view modes can show intermediate masks (`Base`, `Dilated`, `Eroded`) instead of final outline.

## Camera and Interaction

`ViewTrackball` is used for navigation.

- Left drag: arcball/hyperbola rotation (VCG-style math, stable around 180 degrees).
- Middle/Right drag or `Ctrl+Left`: pan.
- Wheel: dolly (distance change).
- `Shift+Wheel`: vertigo zoom (FOV + compensating dolly).
  - FOV range: 10..120 degrees.
  - distance is adjusted to keep apparent object/trackball scale stable.
- Double click: depth-pick under cursor and animated recenter to picked world point.

Trackball gizmo scale stays visually stable across dolly and FOV changes.

## Depth Picking

Double click schedules a depth-pick frame:

- offscreen color RT encodes depth in RGB
- readback of one pixel
- backend-specific handling:
  - Y orientation (`isYUpInFramebuffer`, `isYUpInNDC`)
  - clip-space depth convention (`isClipDepthZeroToOne`)
- unproject via inverse `MVP` to world point
- start center animation and emit `trackballCenterPicked(worldPos)`

## Runtime Performance Instrumentation

### Buffer Build Logging

When cache rebuilds happen, `Document` logs:

- which pass buffers were rebuilt (`fill`, `wire`, `points`, `bbox`)
- rebuild elapsed time in ms

### Frame Timing

`RenderWidget` emits per-frame:

- CPU frame time (`nsecsElapsed`)
- GPU frame time (if backend supports `QRhi::Timestamps`, via `lastCompletedGpuTime`)

`MainWindow` accumulates rolling stats over the last 100 frames and shows min/avg/max in a fixed-width status-bar label.

## Defaults and Behavior Notes

- For point-cloud-only scenes, default render mode switches to points.
- Point color source defaults to per-vertex when available.
- Point lighting defaults on when vertex normals are available.
- Render setting availability is clamped to loaded data (invalid color-source options are disabled/fallbacked).

## Current Scope and Limits

- Material system is intentionally minimal (base color + texture usage for fill path).
- No full PBR path yet.
- No compute-based post-processing; current outline/morph is raster full-screen passes.
- Selection/highlight currently focuses on the concept of **current mesh** rather than a full element-level selection model.
