# Rendering

This note documents the current QRhi rendering architecture used by QMeshLab.

See also:
- [Architecture](architecture.md)
- [Data Model](data_model.md)

## High-Level Architecture

Rendering is performed by `RenderWidget` (`QRhiWidget`) in two modes:

- `Scene3D`: layered mesh rendering with trackball camera, depth picking, and current-mesh highlight.
- `ParametrizationUV`: orthographic UV-space rendering for the current mesh (when UV coordinates are available).

Ownership split:

- `Document` owns canonical mesh data and metadata.
- `MeshGpuResourceCache` (owned by `Document`) owns shared GPU mesh resources for scene passes.
- `RenderWidget` owns per-view render state (mode, pipelines, SRBs, UBOs, offscreen targets, camera/trackball, UV pan/zoom/cache, overlay state).
- `LineRenderer` is used by cache/build paths to generate triangle-expanded fat-line geometry from line segments.

This split allows GPU buffer reuse across render-mode changes and across views that share the same `QRhi`.

## Shared GPU Cache (`MeshGpuResourceCache`)

Cache key shape:

- per backend: `QRhi*`
- per mesh: `meshId`
- per variant:
  - fill: `Constant`, `PerVertex`, `PerFace`, `PerVertexQuality`, `PerFaceQuality`, `Texture`
  - points: `Constant`, `PerVertex`, `PerVertexQuality`
- revision checks:
  - fill depends on `geometryRevision` + `materialRevision`
  - wire/edges/points/bbox/decorators depend on `geometryRevision`

Cached pass data:

- fill pass: one or more batches (`vbuf`, optional `ibuf`, optional `texture`)
- wire pass: barycentric-expanded triangle buffer
- edges pass: line buffer and fat-line buffer from explicit mesh edges
- points pass: point buffer (position/color/normal payload + normal-valid flag)
- bbox pass: line buffer
- decorator pass buffers:
  - vertex normals
  - face normals
  - geometric boundary edges (line + fat-line)
  - texture seam edges (line + fat-line)

This cache is the primary source for `Scene3D`; UV mode uses a separate widget-local UV cache and only reuses document fill texture batches when textured UV fill is selected.

### Fat-Line Strategy

For thick lines, geometry is expanded into triangles (`p0`, `p1`, `along`, `side`) rather than relying on backend line-width support:

- edge meshes use fat-edge buffers when available
- boundary/seam decorators use fat buffers plus explicit width controls
- line pipelines remain as a fallback path

### Fill Upload Strategy

Two fill upload paths are used:

- indexed mesh path (shared vertices)
- expanded-triangle path when needed for:
  - per-face colors
  - texture batching by texture slot

For textured fill, faces are grouped by texture slot; each batch can carry its own uploaded `QRhiTexture`.
For quality fill/points, buffers store normalized quality values and the final color is resolved in shaders via a small LUT texture, so changing colormap no longer requires rebuilding those GPU buffers.

## Per-Widget GPU State (`RenderWidget`)

Per-widget resources include:

- dynamic UBOs (`m_ubuf`, outline/morph/debug/decorator/trackball UBOs)
- widget-local samplers and fallback 1x1 white texture
- mode state:
  - `Scene3D` / `ParametrizationUV`
  - UV view controls (`m_uvPan`, `m_uvZoom`, fit request/pan interaction)
  - UV mesh GPU cache (`m_uvMeshGpu`)
- SRBs:
  - base SRB (UBO + fallback texture)
  - per-texture SRB cache for textured fill batches
  - UV background SRB/UBO
- pipelines:
  - fill, wire, edges, bbox, points, decorators
  - fat edges
  - fat boundary/seam decorators
  - depth pick
  - current-mesh mask/depth-only variants
  - current-mask fat-edge depth-only variant
  - mask morphology/debug/outline extraction/composite
  - trackball gizmo
  - UV background
  - UV textured fill
- offscreen targets/textures:
  - depth picking
  - current-mesh mask/base/work

These stay per widget because they depend on per-view settings and render-pass descriptors.

## Per-Mesh Render Modes

`RenderWidget` keeps `MeshRenderMode` per mesh id (`m_meshRenderModes`), so each mesh can have independent pass/style toggles.
In UV mode, the current mesh mode is reused to control fill/wire/edges/points/bounding-box styling in UV space.

Default behavior:

- surface meshes (`FN > 0`): fill on, wire optionally on for small meshes (`FN < 10000`), edges off, points off
- edge-only meshes (`FN == 0 && EN > 0`): edges on (`edgeSize = 4.0` default)
- point-only meshes: points on

Default color-source preference for surfaces:

1. texture
2. per-vertex
3. per-face
4. constant

Color-source and point-lighting availability are clamped against the current mesh `ioMask` + texture availability.
Boundary/seam decorators share a dedicated width control (`decoratorBoundaryWidth`).

## Frame Flow and Draw Order

### `Scene3D` Mode

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

### `ParametrizationUV` Mode

Per frame:

1. Sync per-mesh mode state and UV cache against the current document.
2. Ensure UV resources for the current mesh (`m_uvMeshGpu`), and fit the UV view if requested.
3. Draw UV background (checker/grid shader pair).
4. Draw the current mesh in UV space using mode-controlled passes:
   - fill (constant/per-vertex/per-face from UV cache, or textured via document fill batches)
   - wire
   - edges
   - boundary edges (decorator)
   - texture seams (decorator)
   - points
5. Optionally draw the UV unit-box outline (`[0,1] x [0,1]`) when bounding-box display is enabled.

## `Scene3D` Pass Behavior Details

### Fill

- Smooth/Flat shading use distinct shader pairs.
- Depth test/write enabled.
- Backface culling controlled by `fillBackfaceCulling`.
- Color source: constant / per-vertex / per-face / texture.
- Color source: constant / per-vertex / per-face / per-vertex-quality / per-face-quality / texture.
- Quality variants use LUT sampling in fragment shader (shared per-view colormap texture).
- Textures are sampled through per-batch SRBs when available.

### Wireframe

- Uses barycentric-expanded triangles + fragment edge test.
- Depth test enabled (`LessOrEqual`), depth write disabled.
- Alpha blending enabled.
- Backface culling toggle (`wireBackfaceCulling`).

### Edges

- Uses fat-edge triangles (`overlay_fat_edges.*`) when cached fat buffers are available.
- Falls back to line topology over explicit mesh edges when needed.
- Depth test/write enabled (`LessOrEqual`), no culling.
- Alpha blending enabled.
- Width driven by `edgeSize`.

### Bounding Box

- Line topology.
- Depth test enabled, depth write disabled.
- Color driven by render settings.

### Points

- `QRhiGraphicsPipeline::Points`.
- Depth test/write enabled.
- Point size and lighting controlled by settings.
- Color source: constant or per-vertex.
- Color source: constant / per-vertex / per-vertex-quality.
- Per-vertex-quality uses LUT sampling in fragment shader.
- Vertex payload carries normal + validity flag for point lighting.

### Decorators

Decorator overlays are depth-tested (`LessOrEqual`) with depth write disabled:

- vertex normals
- face normals
- geometric boundary edges
- texture seams

Pipeline behavior:

- vertex/face normals: line pipeline
- boundary/seams: fat-decorator pipeline with `decoratorBoundaryWidth` (line fallback)

Boundary and seam extraction are done in the cache:

- geometric boundaries via topological edge incidence (`incidentCount == 1`)
- seams via per-topological-edge UV sample comparison (including texture-index changes and missing/invalid UV on incident faces)

## `ParametrizationUV` Mode Details

Entry conditions and scope:

- `setViewMode(ParametrizationUV)` is allowed only if the current mesh has faces plus UV coordinates (`IOM_WEDGTEXCOORD` or `IOM_VERTTEXCOORD`).
- Rendering is current-mesh-centric: the active mesh is drawn in UV space, and normal Scene3D depth-pick/highlight passes are bypassed.

UV-space camera model:

- orthographic projection
- `m_uvPan` + `m_uvZoom` control translation/scale in UV space
- reset and UV double click trigger fit-to-current-UV-bounds

UV mode interaction:

- left/middle drag: pan
- wheel: zoom around cursor anchor
- left double click: fit UV view to current mesh UV bounds

Additional UV rendering notes:

- UV background is drawn from `uv_background.vert/.frag`.
- Textured UV fill uses `uv_fill_texture.vert` + the regular fill fragment shader.
- UV cache also builds boundary-edge and texture-seam line buffers.
- UV boundary/seam overlays use decorator colors and `decoratorBoundaryWidth`.
- Scene corner/dimension overlays are hidden in UV mode.

## `Scene3D` Current Mesh Highlight Pipeline

Highlight is controlled by `highlightCurrentMesh`, and only runs when the current mesh is visible.

Two paths exist:

### Surface/Edge path

Used when current mesh has fill or edges enabled:

1. Render current mesh depth-encoded mask into `base`.
2. Extract current silhouette/boundary from `base` into `work`.
3. Render all other visible meshes into a second depth-encoded mask (`mask`) for occlusion comparison.
4. Composite outline in the main pass.

For edge meshes, highlight depth passes prefer fat-edge depth-only geometry when available.

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

## `Scene3D` Depth Picking

Double click schedules a depth-pick frame:

- offscreen RT encodes depth in RGB
- one pixel is read back
- backend conventions are handled:
  - framebuffer/NDC Y orientation
  - clip-depth convention (`0..1` vs `-1..1`)
- pixel depth is unprojected via inverse `MVP`
- picked world point starts center animation and emits `trackballCenterPicked(worldPos)`

## Camera and Interaction

### `Scene3D`

`ViewTrackball` navigation:

- left drag: arcball/hyperbola rotation
- middle/right drag or `Ctrl+Left`: pan
- wheel: dolly
- `Shift+Wheel`: vertigo zoom (FOV + compensating dolly)
- double click: depth-pick + animated recenter

Trackball gizmo is depth-aware and scale-stable across dolly/FOV changes.

### `ParametrizationUV`

- left/middle drag: pan in UV space
- wheel: zoom around the cursor
- double click: fit UV framing to current mesh UV bounds
- `Reset Camera`: requests UV fit instead of trackball reset

## Snapshot Capture Path

`MainWindow::saveSnapshotPng()` captures the active `RenderWidget` offscreen by:

1. setting a temporary fixed color-buffer size (`setFixedColorBufferSize`)
2. requesting an update and waiting for a `frameRendered` signal (with timeout)
3. reading the rendered image (`grabFramebuffer`)
4. restoring the previous fixed-size state

The saved PNG includes camera JSON metadata under `QMeshLab.CameraTrackballState`.

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
- UV mode is currently single-mesh (current mesh only) and requires imported UV attributes.
