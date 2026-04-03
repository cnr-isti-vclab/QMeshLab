# Rendering

This note describes the current rendering path used by QMeshLab.

## Overview

Rendering is handled by `RenderWidget`, a `QRhiWidget`-based viewport placed in the center of the main window.

The rendering flow is intentionally simple:

1. The `Document` owns the loaded meshes.
2. `RenderWidget` listens to document changes (`meshAdded`, `meshRemoved`).
3. When mesh content or rendering mode changes, GPU buffers and/or pipeline state are marked dirty.
4. During the next frame, the widget prepares the missing GPU resources and renders all visible geometry.

This keeps the data model (`Document`) independent from the rendering backend while still allowing the viewport to rebuild GPU state on demand.

## Main Components

### Document

`Document` is the authoritative owner of mesh data. After a plugin loads a mesh, the document:

- updates bounding information and normals
- stores the mesh as a `MeshEntry`
- emits Qt signals so views can react

The renderer never loads files directly and never owns the canonical mesh representation.

### RenderWidget

`RenderWidget` converts document meshes into GPU buffers and draws them through Qt RHI.
it creates the bugffers on demand when the document signals that a mesh was added or removed, or when the rendering mode changes. The widget is responsible for managing the graphics pipeline, uniform buffers, and mesh GPU buffers. It also handles camera controls and emits timing information to the document log when buffers are rebuilt.

It owns:

- one graphics pipeline for the current shading mode
- one uniform buffer for camera and transform data
- one shader resource binding set
- one GPU buffer set per mesh

The widget also owns a simple orbit camera defined by rotation angles, scene center, and camera distance.

## Frame Preparation

The render widget uses two kinds of lazy rebuilds:

- `m_buffersDirty`: mesh GPU buffers must be rebuilt and uploaded again
- pipeline reset: shader/pipeline state must be recreated, typically after a rendering-mode change

This work is performed during normal rendering, not only during resize or initialization. That detail matters because otherwise newly loaded meshes or changed shading modes could appear only after an unrelated window event.

### Mesh Buffer Preparation

For standard shaded modes, each mesh is uploaded as:

- one vertex buffer with interleaved position and normal data
- one index buffer with triangle indices

For wireframe mode, the renderer does not store explicit edges. Instead, each triangle is expanded to three independent vertices carrying barycentric coordinates. This allows edge detection entirely in the fragment shader.

## Uniform Data

The current uniform block contains:

- `mat4 mvp`
- `mat4 modelView`
- `mat3 normalMatrix` stored with std140-compatible padding

These values are recomputed every frame from the current camera configuration.

## Rendering Modes
The renderer support overlayed rendering modes that can be added and switched on the fly
we assume a rendering layer for 
- points
- edges
- faces
Each rendering layer can be switched on and off independently, and the rendering mode for each layer can be changed independently too.

For each of these layers we support different rendering modalities where we distinguish shading and coloring policies. 
- faces can be rendered with flat or smooth shading, or none shading at all. Flat mode reconstructs face normals in the shader for a faceted look, while smooth mode uses per-vertex normals for a smoother appearance. 
Color can be per vertex, per face or per mesh. The shader currently supports only per vertex color, but it can be easily extended to support the other cases too.
- wireframe
edges can be rendered as lines. The current implementation uses a barycentric approach to draw antialiased wireframes without needing a separate edge list. Future improvements could include a separate line-only pass for better control over edge styling.
- point cloud 
vertices can be rendered as points with a fixed screen size. The shader can be extended to support per-vertex point size and per-vertex color in the future.


## Camera

The viewport uses a basic orbit camera:

- left mouse drag rotates around the scene
- mouse wheel changes camera distance

When buffers are rebuilt, the scene bounding box is recomputed from all meshes and used to update the camera center and default distance.

## Logging

The renderer writes timing information into the document log when buffers are rebuilt. At the moment this includes preparation and upload time plus mesh/vertex/triangle counts.

This is meant as lightweight observability rather than a full profiler.

## Current Limits

The rendering path is intentionally minimal. Some notable limitations are:

- no material system
- no per-mesh color or style overrides yet
- no selection or picking
- no transparency pipeline
- no line-only overlay pass separated from fill

The current architecture is still adequate for experimenting with viewport behavior because mesh ownership, GPU upload, and shading policies are already separated in a reasonably clean way.