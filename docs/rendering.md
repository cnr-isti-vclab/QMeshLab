# Rendering

This note documents the current QRhi rendering architecture used by QMeshLab.

See also:
- [Architecture](architecture.md)
- [Data Model](data_model.md)

## High-Level Architecture

Rendering is performed by `RenderWidget` (`QRhiWidget`).

Ownership split:

- `Document` owns canonical mesh data and metadata.
- `MeshGpuResourceCache` (owned by `Document`) owns shared GPU mesh resources.
- `RenderWidget` owns per-view render state (pipelines, SRBs, UBOs, offscreen targets, camera/trackball, overlay state).

This split allows GPU buffer reuse across render-mode changes and across views that share the same `QRhi`.

## Shared GPU Cache (`MeshGpuResourceCache`)

Cache key shape:

- per backend: `QRhi*`
- per mesh: `meshId`
- per variant:
  - fill: `Constant`, `PerVertex`, `PerFace`, `Texture`
  - points: `Constant`, `PerVertex`
- revision checks:
  - fill depends on `geometryRevision` + `materialRevision`
  - wire/edges/points/bbox/decorators depend on `geometryRevision`

Cached pass data:

- fill pass: one or more batches (`vbuf`, optional `ibuf`, optional `texture`)
- wire pass: barycentric-expanded triangle buffer
- edges pass: line buffer from explicit mesh edges
- points pass: point buffer (position/color/normal payload + normal-valid flag)
- bbox pass: line buffer
- decorator pass buffers:
  - vertex normals
  - face normals
  - geometric boundary edges
  - texture seam edges

### Fill Upload Strategy

Two fill upload paths are used:

- indexed mesh path (shared vertices)
- expanded-triangle path when needed for:
  - per-face colors
  - texture batching by texture slot

For textured fill, faces are grouped by texture slot; each batch can carry its own uploaded `QRhiTexture`.

## Per-Widget GPU State (`RenderWidget`)

Per-widget resources include:

- dynamic UBOs (`m_ubuf`, outline/morph/debug/decorator/trackball UBOs)
- widget-local samplers and fallback 1x1 white texture
- SRBs:
  - base SRB (UBO + fallback texture)
  - per-texture SRB cache for textured fill batches
- pipelines:
  - fill, wire, edges, bbox, points, decorators
  - depth pick
  - current-mesh mask/depth-only variants
  - mask morphology/debug/outline extraction/composite
  - trackball gizmo
- offscreen targets/textures:
  - depth picking
  - current-mesh mask/base/work

These stay per widget because they depend on per-view settings and render-pass descriptors.

## Per-Mesh Render Modes

`RenderWidget` keeps `MeshRenderMode` per mesh id (`m_meshRenderModes`), so each mesh can have independent pass/style toggles.

Default behavior:

- surface meshes (`FN > 0`): fill on, wire optionally on for small meshes (`FN < 10000`), edges off, points off
- edge-only meshes (`FN == 0 && EN > 0`): edges on
- point-only meshes: points on

Default color-source preference for surfaces:

1. texture
2. per-vertex
3. per-face
4. constant

Color-source and point-lighting availability are clamped against the current mesh `ioMask` + texture availability.

## Frame Flow and Draw Order

Per frame:

1. Ensure resources and dirty GPU buffers for view-visible meshes.
2. Optionally execute depth picking (if scheduled).
3. Optionally build/process current-mesh highlight masks.
4. Run the main onscreen pass.

Main pass draw order:

1. fill
2. wire
3. edges
4. bbox
5. points
6. decorators
7. trackball gizmo
8. current-mesh outline/debug composite

## Pass Behavior Details

### Fill

- Smooth/Flat shading use distinct shader pairs.
- Depth test/write enabled.
- Backface culling controlled by `fillBackfaceCulling`.
- Color source: constant / per-vertex / per-face / texture.
- Textures are sampled through per-batch SRBs when available.

### Wireframe

- Uses barycentric-expanded triangles + fragment edge test.
- Depth test enabled (`LessOrEqual`), depth write disabled.
- Alpha blending enabled.
- Backface culling toggle (`wireBackfaceCulling`).

### Edges

- Line topology over explicit mesh edges (`mesh.edge`).
- Depth test/write enabled (`LessOrEqual`), no culling.
- Alpha blending enabled.
- Line width driven by `edgeSize`.

### Bounding Box

- Line topology.
- Depth test enabled, depth write disabled.
- Color driven by render settings.

### Points

- `QRhiGraphicsPipeline::Points`.
- Depth test/write enabled.
- Point size and lighting controlled by settings.
- Color source: constant or per-vertex.
- Vertex payload carries normal + validity flag for point lighting.

### Decorators

Decorator overlays are line-based and depth-tested (`LessOrEqual`) with depth write disabled:

- vertex normals
- face normals
- geometric boundary edges
- texture seams

Boundary and seam extraction are done in the cache:

- geometric boundaries via MeshLab-style border flag extraction
- seams via UV-space face-face adjacency (`FaceFaceFromTexCoord`)

## Current Mesh Highlight Pipeline

Highlight is controlled by `highlightCurrentMesh`, and only runs when the current mesh is visible.

Two paths exist:

### Surface/Edge path

Used when current mesh has fill or edges enabled:

1. Render current mesh depth-encoded mask into `base`.
2. Extract current silhouette/boundary from `base` into `work`.
3. Render all other visible meshes into a second depth-encoded mask (`mask`) for occlusion comparison.
4. Composite outline in the main pass.

### Point-cloud path

Used when current mesh is points-only:

1. Render point occupancy mask.
2. Snapshot base point mask.
3. Dilate (`base -> work`) then erode (`work -> mask`) in screen space.
4. Composite outline from final mask.

Point-cloud outline width is governed by dilate/erode radii; regular outline width is used for surface/edge mode.

Debug modes:

- `FullMask`
- `VisibleMask`
- `OccludedMask`
- `DilatedMask`
- `ErodedMask`

(`Outline` mode is the normal composite path.)

## Depth Picking

Double click schedules a depth-pick frame:

- offscreen RT encodes depth in RGB
- one pixel is read back
- backend conventions are handled:
  - framebuffer/NDC Y orientation
  - clip-depth convention (`0..1` vs `-1..1`)
- pixel depth is unprojected via inverse `MVP`
- picked world point starts center animation and emits `trackballCenterPicked(worldPos)`

## Camera and Interaction

`ViewTrackball` navigation:

- left drag: arcball/hyperbola rotation
- middle/right drag or `Ctrl+Left`: pan
- wheel: dolly
- `Shift+Wheel`: vertigo zoom (FOV + compensating dolly)
- double click: depth-pick + animated recenter

Trackball gizmo is depth-aware and scale-stable across dolly/FOV changes.

## Runtime Instrumentation

### Buffer Build Logging

When cache rebuilds happen, `Document` logs rebuilt resources among:

- `fill`, `wire`, `edges`, `points`, `bbox`
- `decorator normals`, `decorator boundaries`

plus elapsed rebuild time.

### Frame Timing

`RenderWidget` emits per-frame:

- CPU frame time (`QElapsedTimer`)
- GPU frame time when `QRhi::Timestamps` is supported (`lastCompletedGpuTime`)

`MainWindow` shows rolling min/avg/max over the last 100 frames.

## Current Scope and Limits

- Material/shading model is intentionally minimal (no full PBR pipeline).
- Post-processing is raster full-screen passes (no compute path).
- Selection/highlight is centered on the **current mesh** concept rather than element-level selection.
